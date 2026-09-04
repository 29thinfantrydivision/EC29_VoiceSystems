//! Repairs the two Reforger 1.8.0.10 defects that kill radio VON until a radio is
//! power-cycled or a relay entity exists in the world (issue #4 research):
//!
//! 1. Game Master and custom terrain worlds ship without a RadioManagerEntity.
//!    The game mode hook below spawns the vanilla prefab server-side when absent.
//!    FIELD REALITY (2026-08-24, server logs from every fleet box): on any
//!    populated world the null check reads "already present" and the spawn
//!    never runs - ChimeraWorld.GetRadioManager() returns a non-null native
//!    stub once any radio initialized before the check, whether or not the
//!    entity exists. Radio VON demonstrably works fleet-wide without the
//!    entity, so this path is a safety net for genuinely empty worlds (where
//!    the getter does return null - observed in Workbench), not the
//!    load-bearing repair it was designed as. The getter can NEVER prove the
//!    entity exists, and on manager-less worlds calling methods on its
//!    non-null result is a native access violation (the 2026-08-21 CTD).
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
    //! An owner that is transiently invalid at restore time (inventory transfer
    //! or spawn streaming landing inside the off-window) gets retried instead
    //! of abandoned - abandoning leaves a radio this guard switched off stuck
    //! OFF with no trace: deaf RX plus key-ups silently rerouting to direct
    //! speech (2026-08-24 field case, dead RX all session).
    protected static const int RESTORE_RETRY_MS = 500;
    protected static const int RESTORE_MAX_ATTEMPTS = 4;
    //! Receive-health telemetry: a powered, tuned radio that has gone this long
    //! without a single voice packet is either on a quiet net or holding a dead
    //! receiver. The log line is the field signature this class was missing -
    //! Chan's 2026-08-24 dead-RX session produced zero EC29 lines.
    protected static const int RX_HEARTBEAT_INTERVAL_MS = 300000;

    //! Dedupe: multiple VON entries share one physical radio; cycle each radio once.
    protected ref array<BaseRadioComponent> m_aScheduledRadios = {};
    protected bool m_bRadioSystemReadySeen;
    protected bool m_bHeartbeatRunning;

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
        // Another system's net: a silent 150 ms power cycle would drop their
        // reception and re-key state - whoever owns that radio's lifecycle
        // owns its repair (observed in the field: a 29000 kHz cycle). For the
        // spectator ghost radio that owner is EC29's own spectator voice
        // service, which holds the mute state this cycle used to fight - hand
        // it the entry so ITS repair anchors on AddEntry like every other
        // radio's, instead of on the camera's later body registration. The
        // service ignores the call unless the local player is spectating.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
        {
            EC29_RadioState.GetInstance().SpectatorVon().OnSpecialNetEntryAdded(transceiver);
            return;
        }

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

        EC29_EnsureHeartbeat();
    }

    //------------------------------------------------------------------------------------------------
    //! Client-only receive-health watchdog. The dedicated server has no local
    //! receive path (OnVoicePacket never fires there), so it has nothing to
    //! measure and must not tick.
    protected void EC29_EnsureHeartbeat()
    {
        if (m_bHeartbeatRunning)
            return;

        if (!GetGame().GetPlayerController())
            return;

        m_bHeartbeatRunning = true;
        GetGame().GetCallqueue().CallLater(EC29_HeartbeatTick, RX_HEARTBEAT_INTERVAL_MS, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void EC29_HeartbeatTick()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            m_bHeartbeatRunning = false;
            return;
        }

        // A world change rebuilds EC29_RadioState with a fresh guard; a stale
        // instance's queued tick must not adopt the new world's radios.
        if (EC29_RadioState.GetInstance().ReceiverGuard() != this)
        {
            m_bHeartbeatRunning = false;
            return;
        }

        float nowMs = world.GetWorldTime();
        EC29_RadioRxSquelch squelch = EC29_RadioState.GetInstance().Squelch();
        squelch.EC29_SweepDeadRadioRxRecords();

        foreach (BaseRadioComponent radio : m_aScheduledRadios)
        {
            if (!IsRadioAlive(radio) || !radio.IsPowered() || radio.IsEditorRadio())
                continue;

            if (radio.TransceiversCount() == 0)
                continue;

            BaseTransceiver transceiver = radio.GetTransceiver(0);
            if (!transceiver || transceiver.GetFrequency() <= 0)
                continue;

            // Another system's net stays their business (and its traffic
            // pattern is not ours to judge).
            if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
                continue;

            float lastRxMs = squelch.EC29_GetLastRadioRxMs(radio);
            if (lastRxMs >= 0 && nowMs - lastRxMs < RX_HEARTBEAT_INTERVAL_MS)
                continue;

            string lastSeen = "never this session";
            if (lastRxMs >= 0)
                lastSeen = string.Format("%1 ms ago", nowMs - lastRxMs);

            PrintFormat("[EC29-DBG][RadioGuard] RX heartbeat: powered radio on %1 kHz has received no voice packets (last: %2). A quiet net is normal - but if others WERE transmitting on this net, this radio's receiver is dead (native registration loss)", transceiver.GetFrequency(), lastSeen, level: LogLevel.WARNING);
        }

        GetGame().GetCallqueue().CallLater(EC29_HeartbeatTick, RX_HEARTBEAT_INTERVAL_MS, false);
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

        // Kill-switch (mission header, replicated): with the repair off, the radio is left
        // exactly as native registration made it, and the RX heartbeat below becomes the
        // measurement of whether the game fixed the defect. Covers the ready-flag re-cycle
        // too, since that path lands here as well.
        EC29_VONSettingsComponent repairSettings = EC29_VONSettingsComponent.GetInstance();
        if (repairSettings && !repairSettings.EC29_IsReceiverRepairEnabled())
        {
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][RadioGuard] Receiver repair DISABLED by mission settings - radio left untouched (measuring native registration)", LogLevel.NORMAL);
            return;
        }

        // A radio that has already delivered voice packets has a provably
        // registered receiver - the defect this cycle repairs cannot be
        // present, and the 150 ms off-window is pure risk (2026-08-24 field
        // case: a fresh spawn's radio received fine at t+0s, was cycled at
        // t+3s, and never received again).
        if (EC29_RadioState.GetInstance().Squelch().EC29_GetLastRadioRxMs(radio) >= 0)
        {
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][RadioGuard] Radio already receiving - receiver provably registered, skipping repair cycle", LogLevel.NORMAL);
            return;
        }

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
        RestorePowerAttempt(radio, 1);
    }

    //------------------------------------------------------------------------------------------------
    protected void RestorePowerAttempt(BaseRadioComponent radio, int attempt)
    {
        // Deleted radios null their handles - nothing is left powered off.
        if (!radio)
            return;

        // Transiently invalid owner (inventory transfer / spawn streaming
        // landing inside the off-window): retry, never abandon silently -
        // see RESTORE_RETRY_MS comment for the field case this closes.
        if (!IsRadioAlive(radio))
        {
            if (attempt >= RESTORE_MAX_ATTEMPTS)
            {
                Print("[EC29-DBG][RadioGuard] Radio owner never returned during power restore - a radio this guard switched off may be stuck OFF (deaf RX, key-ups fall back to direct speech)", LogLevel.WARNING);
                return;
            }

            GetGame().GetCallqueue().CallLater(RestorePowerAttempt, RESTORE_RETRY_MS, false, radio, attempt + 1);
            return;
        }

        radio.SetPower(true);

        if (!radio.IsPowered())
            Print("[EC29-DBG][RadioGuard] SetPower(true) did not stick after the repair cycle - radio remains OFF", LogLevel.WARNING);
        else if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][RadioGuard] Repair cycle complete (restore attempt %1) - radio back ON", attempt);

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
