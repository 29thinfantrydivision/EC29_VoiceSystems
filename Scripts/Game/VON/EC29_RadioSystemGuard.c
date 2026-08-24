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
    //! OnGameStart can fire as late as the first player's connection (GM sits in
    //! pre-game until someone joins), so a manager spawned there races the first
    //! joiner's radio/VoN session setup. Ensure at world init instead - deferred
    //! one tick so the spawn runs outside entity loading - and keep OnGameStart
    //! as the belt-and-braces retry.
    override void EOnInit(IEntity owner)
    {
        super.EOnInit(owner);

        if (!Replication.IsServer())
            return;

        GetGame().GetCallqueue().CallLater(EC29_EnsureRadioManager, 1, false);
    }

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
            EC29_MarkRadioSystemReady();
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
        EC29_MarkRadioSystemReady();
    }

    //------------------------------------------------------------------------------------------------
    //! Flip the replicated ready flag so every machine - including any client
    //! whose radios registered before the manager existed - re-runs the
    //! receiver repair against a radio system that can actually hold it.
    protected void EC29_MarkRadioSystemReady()
    {
        EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
        if (settings)
            settings.EC29_MarkRadioSystemReady();
        else
            Print("[EC29-DBG][RadioGuard] Radio system up but EC29_VONSettingsComponent missing - clients will not be told to re-verify radios", LogLevel.WARNING);
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
    protected bool m_bRadioSystemReadySeen;

    //------------------------------------------------------------------------------------------------
    //! Server confirmed the RadioManagerEntity exists (replicated ready flag).
    //! Radios cycled before that confirmation were repaired into a possibly
    //! manager-less radio system; cycle them once more now that registration
    //! can stick. Radios scheduled after this point get their normal
    //! entry-add cycle, so the one-shot flag is enough.
    void OnRadioSystemReady()
    {
        if (m_bRadioSystemReadySeen)
            return;
        m_bRadioSystemReadySeen = true;

        int recycled = 0;
        foreach (BaseRadioComponent radio : m_aScheduledRadios)
        {
            if (!radio)
                continue;

            GetGame().GetCallqueue().CallLater(CycleRadio, STABILIZATION_DELAY_MS, false, radio);
            recycled++;
        }

        if (recycled > 0)
            PrintFormat("[EC29-DBG][RadioGuard] Radio system ready - re-cycling %1 radio(s) that registered before the manager was confirmed", recycled, level: LogLevel.WARNING);
    }

    //------------------------------------------------------------------------------------------------
    void OnRadioEntryAdded(notnull BaseTransceiver transceiver)
    {
        // Another system's net (spectator ghost radio): a silent 150 ms power
        // cycle would drop their reception and re-key state - their mod owns
        // that radio's lifecycle (observed in the field: a 29000 kHz cycle).
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
            return;

        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || radio.IsEditorRadio() || !radio.IsPowered())
            return;

        // Deleted radios null their handles; sweep them so the dedupe list
        // cannot grow for the world lifetime.
        for (int i = m_aScheduledRadios.Count() - 1; i >= 0; i--)
        {
            if (!m_aScheduledRadios[i])
                m_aScheduledRadios.Remove(i);
        }

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
        {
            int freq = -1;
            if (radio.TransceiversCount() > 0)
            {
                BaseTransceiver tsv = radio.GetTransceiver(0);
                if (tsv)
                    freq = tsv.GetFrequency();
            }
            PrintFormat("[EC29-DBG][RadioGuard] Power-cycling radio (freq %1 kHz) to re-register its receiver (1.8 registration defect)", freq, level: LogLevel.NORMAL);
        }

        radio.SetPower(false);
        GetGame().GetCallqueue().CallLater(RestorePower, POWER_OFF_MS, false, radio);
    }

    //------------------------------------------------------------------------------------------------
    protected void RestorePower(BaseRadioComponent radio)
    {
        if (!IsRadioAlive(radio))
            return;

        radio.SetPower(true);

        // Entry usability snapshots the power state at entry init or menu
        // refresh; one taken during the 150 ms off-window silently reroutes
        // this radio's key-ups to direct speech until the next refresh
        // (2026-08-23 field case: dead TX, working RX). Re-sync now.
        PlayerController pc = GetGame().GetPlayerController();
        if (pc)
        {
            SCR_VONController vonController = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
            if (vonController)
                vonController.EC29_ResyncRadioEntries(radio);
        }
    }
}
