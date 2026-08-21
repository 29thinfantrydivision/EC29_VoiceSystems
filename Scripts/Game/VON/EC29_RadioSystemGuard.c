//! Repairs the two Reforger 1.8.0.10 defects that kill radio VON until a radio is
//! power-cycled or a relay entity exists in the world (issue #4 research):
//!
//! 1. Game Master and custom terrain worlds ship without a RadioManagerEntity. The
//!    native VoN system requires one for any radio traffic; official Conflict and
//!    CombatOps worlds place it in the world file, GM and custom worlds do not.
//!    The game mode hook below spawns the vanilla prefab server-side when absent
//!    (the prefab replicates - RplComponent, Streamable Disabled).
//!
//! 2. An already-powered radio can miss receiver registration with the native
//!    radio system around the time its VON entry registers: it transmits but never
//!    receives, and a manual off/on repairs it. The receiver guard below verifies
//!    each radio against the native transceiver registry
//!    (RadioManagerEntity.GetTransceiversInRange returns only power-ON
//!    transceivers, so a powered radio absent from the query is unregistered) and
//!    only then reproduces the off/on transition. A healthy radio is never
//!    touched, so the guard becomes a no-op the day BI fixes the engine side.
//!
//! Both defects are native-code regressions: the 1.7.0.54 -> 1.8.0.10 script diff
//! (BohemiaInteractive/Arma-Reforger-Script-Diff) contains no functional change in
//! the radio/VON chain.

//------------------------------------------------------------------------------------------------
//! Server-side: guarantee the radio VON prerequisite entity exists in every world.
modded class SCR_BaseGameMode
{
    protected static const ResourceName EC29_RADIO_MANAGER_PREFAB = "{B8E09FAB91C4ECCD}Prefabs/Systems/Radio/RadioManager.et";

    //------------------------------------------------------------------------------------------------
    override protected void OnGameStart()
    {
        super.OnGameStart();

        if (!Replication.IsServer())
            return;

        EC29_EnsureRadioManager();
    }

    //------------------------------------------------------------------------------------------------
    protected void EC29_EnsureRadioManager()
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return;

        if (world.GetRadioManager())
        {
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][RadioGuard] RadioManagerEntity already present in world", LogLevel.NORMAL);
            return;
        }

        Resource managerResource = Resource.Load(EC29_RADIO_MANAGER_PREFAB);
        if (!managerResource || !managerResource.IsValid())
        {
            Print("[EC29-DBG][RadioGuard] RadioManager prefab failed to load - radio VON will not work in this world", LogLevel.ERROR);
            return;
        }

        IEntity manager = GetGame().SpawnEntityPrefab(managerResource, world);
        if (!manager)
        {
            Print("[EC29-DBG][RadioGuard] RadioManager spawn failed - radio VON will not work in this world", LogLevel.ERROR);
            return;
        }

        Print("[EC29-DBG][RadioGuard] World had no RadioManagerEntity (radio VON prerequisite, absent from GM/custom worlds) - spawned vanilla prefab", LogLevel.WARNING);
    }
}

//------------------------------------------------------------------------------------------------
//! Client and server: verify each radio against the native transceiver registry
//! shortly after its VON entry registers; power-cycle only radios proven missing.
//! Owned by EC29_RadioState (world-scoped - state discards with the world).
class EC29_RadioReceiverGuard
{
    //! Native radio setup continues after AddEntry; earlier checks get overwritten
    //! (community-established timing - shorter delays lose the repair).
    protected static const int STABILIZATION_DELAY_MS = 3000;
    //! Separate call-queue turn so the native side actually unregisters the receiver.
    protected static const int POWER_OFF_MS = 150;
    protected static const int VERIFY_DELAY_MS = 1000;
    protected static const int RETRY_DELAY_MS = 2000;
    protected static const int MAX_REPAIR_ATTEMPTS = 2;
    protected static const float REGISTRY_QUERY_RANGE_M = 100.0;

    //! Dedupe: multiple VON entries share one physical radio; check each radio once.
    protected ref array<BaseRadioComponent> m_aScheduledRadios = {};
    protected ref map<BaseRadioComponent, int> m_mRepairAttempts = new map<BaseRadioComponent, int>();
    protected bool m_bWarnedNoManager;

    //------------------------------------------------------------------------------------------------
    void OnRadioEntryAdded(notnull BaseTransceiver transceiver)
    {
        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || radio.IsEditorRadio() || !radio.IsPowered())
            return;

        if (m_aScheduledRadios.Contains(radio))
            return;

        m_aScheduledRadios.Insert(radio);
        GetGame().GetCallqueue().CallLater(CheckAndRepair, STABILIZATION_DELAY_MS, false, radio);
    }

    //------------------------------------------------------------------------------------------------
    protected bool IsRadioAlive(BaseRadioComponent radio)
    {
        if (!radio)
            return false;

        IEntity owner = radio.GetOwner();
        if (!owner || owner.IsDeleted())
            return false;

        return true;
    }

    //------------------------------------------------------------------------------------------------
    //! True when every transceiver of this powered radio appears in the native
    //! registry. Returns true (= take no action) when the registry cannot be
    //! queried: cycling power cannot help a world with no RadioManagerEntity.
    protected bool IsRegistered(BaseRadioComponent radio)
    {
        ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
        if (!world)
            return true;

        RadioManagerEntity radioManager = world.GetRadioManager();
        if (!radioManager)
        {
            if (!m_bWarnedNoManager)
            {
                m_bWarnedNoManager = true;
                Print("[EC29-DBG][RadioGuard] No RadioManagerEntity in world - receiver check impossible, radio VON dead regardless (server-side ensure missing?)", LogLevel.WARNING);
            }
            return true;
        }

        array<BaseTransceiver> nativeRegistry = {};
        radioManager.GetTransceiversInRange(radio.GetOwner().GetOrigin(), REGISTRY_QUERY_RANGE_M, nativeRegistry);

        int tsvCount = radio.TransceiversCount();
        for (int i = 0; i < tsvCount; i++)
        {
            BaseTransceiver tsv = radio.GetTransceiver(i);
            if (tsv && nativeRegistry.Find(tsv) == -1)
                return false;
        }

        return true;
    }

    //------------------------------------------------------------------------------------------------
    protected void CheckAndRepair(BaseRadioComponent radio)
    {
        // A radio the player deliberately powered off is left alone.
        if (!IsRadioAlive(radio) || !radio.IsPowered())
            return;

        if (IsRegistered(radio))
        {
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][RadioGuard] Radio receiver verified in native registry - no action", LogLevel.NORMAL);
            return;
        }

        int attempts;
        m_mRepairAttempts.Find(radio, attempts);
        if (attempts >= MAX_REPAIR_ATTEMPTS)
        {
            Print("[EC29-DBG][RadioGuard] Radio receiver still unregistered after repair attempts - giving up on this radio (manual off/on may recover it)", LogLevel.ERROR);
            return;
        }
        m_mRepairAttempts.Set(radio, attempts + 1);

        Print(string.Format("[EC29-DBG][RadioGuard] Powered radio absent from native transceiver registry (1.8 receiver-registration defect) - power-cycling to re-register (attempt %1/%2)", attempts + 1, MAX_REPAIR_ATTEMPTS), LogLevel.WARNING);

        radio.SetPower(false);
        GetGame().GetCallqueue().CallLater(RestorePower, POWER_OFF_MS, false, radio);
    }

    //------------------------------------------------------------------------------------------------
    protected void RestorePower(BaseRadioComponent radio)
    {
        if (!IsRadioAlive(radio))
            return;

        radio.SetPower(true);
        GetGame().GetCallqueue().CallLater(VerifyRepair, VERIFY_DELAY_MS, false, radio);
    }

    //------------------------------------------------------------------------------------------------
    protected void VerifyRepair(BaseRadioComponent radio)
    {
        // Player powered it off during the verify window - their call stands.
        if (!IsRadioAlive(radio) || !radio.IsPowered())
            return;

        if (IsRegistered(radio))
        {
            Print("[EC29-DBG][RadioGuard] Radio receiver re-registered after power cycle", LogLevel.WARNING);
            return;
        }

        GetGame().GetCallqueue().CallLater(CheckAndRepair, RETRY_DELAY_MS, false, radio);
    }
}
