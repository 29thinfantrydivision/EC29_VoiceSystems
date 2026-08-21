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
//!    receives, and a manual off/on repairs it. The receiver guard below reproduces
//!    that off/on transition once per radio shortly after its VON entry registers -
//!    the pattern every community 1.8 fix mod converged on.
//!
//! The registry-verification variant shipped in v1.0.3 ("cycle only radios provably
//! missing from the native registry") is gone deliberately: on a client whose world
//! loaded without a RadioManagerEntity, ChimeraWorld.GetRadioManager() still
//! returns non-null, and calling GetTransceiversInRange on that handle is a native
//! access violation - a guaranteed client CTD on exactly the GM worlds this file
//! exists to repair (2026-08-21 field crashes). No runtime signal distinguishes a
//! usable manager from that degraded handle, so the guard must never query it.
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
//! Client and server: power-cycle each radio once shortly after its VON entry
//! registers, reproducing the manual off/on that repairs the 1.8 receiver defect.
//! Owned by EC29_RadioState (world-scoped - state discards with the world).
class EC29_RadioReceiverGuard
{
    //! Native radio setup continues after AddEntry; earlier cycles get overwritten
    //! (community-established timing - shorter delays lose the repair).
    protected static const int STABILIZATION_DELAY_MS = 3000;
    //! Separate call-queue turn so the native side actually unregisters the receiver.
    protected static const int POWER_OFF_MS = 150;

    //! Dedupe: multiple VON entries share one physical radio; cycle each radio once.
    protected ref array<BaseRadioComponent> m_aScheduledRadios = {};

    //------------------------------------------------------------------------------------------------
    void OnRadioEntryAdded(notnull BaseTransceiver transceiver)
    {
        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || radio.IsEditorRadio() || !radio.IsPowered())
            return;

        if (m_aScheduledRadios.Contains(radio))
            return;

        m_aScheduledRadios.Insert(radio);
        GetGame().GetCallqueue().CallLater(CycleRadio, STABILIZATION_DELAY_MS, false, radio);
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
    protected void CycleRadio(BaseRadioComponent radio)
    {
        // A radio the player deliberately powered off is left alone.
        if (!IsRadioAlive(radio) || !radio.IsPowered())
            return;

        if (EC29_Debug.VERBOSE)
            Print("[EC29-DBG][RadioGuard] Power-cycling radio to re-register its receiver (1.8 registration defect)", LogLevel.NORMAL);

        radio.SetPower(false);
        GetGame().GetCallqueue().CallLater(RestorePower, POWER_OFF_MS, false, radio);
    }

    //------------------------------------------------------------------------------------------------
    protected void RestorePower(BaseRadioComponent radio)
    {
        if (!IsRadioAlive(radio))
            return;

        radio.SetPower(true);
    }
}
