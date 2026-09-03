//! Radio VON prerequisites and receive-health telemetry.
//!
//! HISTORY. Reforger 1.8.0.10 shipped a native receiver-registration defect: an already-powered
//! radio could miss receiver registration around the time its VON entry registered - it
//! transmitted but never received until power-cycled. From 2026-08-20 to the 1.8.0.13 update
//! this file carried the repair the community fix mods converged on (a silent 150 ms power
//! cycle per radio 3 s after its VON entry registered, with restore retries, entry-usability
//! re-sync, a replicated ready flag to re-cycle radios that registered before the manager
//! existed, and a spectator ghost-radio variant in EC29_SpectatorVonService). 1.8.0.13
//! ("Fixed: Radio would not work sometimes") was measured with that repair switched off - the
//! registration probe reported every carried radio REGISTERED and the RX heartbeat stayed
//! silent under live traffic - and the repair machinery was removed. The git history holds
//! the whole implementation if the defect ever returns; the heartbeat below is the detector
//! that would say so.
//!
//! WHAT REMAINS.
//! 1. Game Master and custom terrain worlds can ship without a RadioManagerEntity. The game
//!    mode hook spawns the vanilla prefab server-side when absent. FIELD REALITY (2026-08-24,
//!    every fleet box): on any populated world ChimeraWorld.GetRadioManager() returns a
//!    non-null native stub once any radio initialized before the check, so the spawn only ever
//!    runs on genuinely empty worlds (observed in Workbench). The getter can NEVER prove the
//!    entity exists, and on manager-less worlds calling methods on its non-null result is a
//!    native access violation (the 2026-08-21 CTD) - no code path here or anywhere in EC29
//!    may call methods on it.
//! 2. Receive-health telemetry: every powered radio the local controller registers is
//!    tracked, and a client-side heartbeat WARNs on any powered, tuned radio that has gone
//!    5 minutes without a single voice packet. A quiet net is normal; a warning while others
//!    were transmitting on that net is the field signature of a dead receiver.

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
//! Client and server: track every powered radio whose VON entry registers, for the
//! receive-health heartbeat. Owned by EC29_RadioState (world-scoped - state discards with the
//! world). The name is historical: this class carried the 1.8 receiver repair until 1.8.0.13.
class EC29_RadioReceiverGuard
{
    //! Receive-health telemetry: a powered, tuned radio that has gone this long
    //! without a single voice packet is either on a quiet net or holding a dead
    //! receiver. The log line is the field signature the 1.8 defect never had -
    //! the 2026-08-24 dead-RX session produced zero EC29 lines.
    protected static const int RX_HEARTBEAT_INTERVAL_MS = 300000;

    //! Multiple VON entries share one physical radio; track each radio once.
    protected ref array<BaseRadioComponent> m_aTrackedRadios = {};
    protected bool m_bHeartbeatRunning;

    //------------------------------------------------------------------------------------------------
    void OnRadioEntryAdded(notnull BaseTransceiver transceiver)
    {
        // Another system's net (spectator ghost radio, third-party special nets):
        // its traffic pattern is not ours to judge, so it is not tracked.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
            return;

        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || radio.IsEditorRadio() || !radio.IsPowered())
            return;

        // Deleted radios null their handles; sweep them so the list cannot
        // grow for the world lifetime.
        for (int i = m_aTrackedRadios.Count() - 1; i >= 0; i--)
        {
            if (!m_aTrackedRadios[i])
                m_aTrackedRadios.Remove(i);
        }

        if (m_aTrackedRadios.Contains(radio))
            return;

        m_aTrackedRadios.Insert(radio);

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

        foreach (BaseRadioComponent radio : m_aTrackedRadios)
        {
            if (!IsRadioAlive(radio) || !radio.IsPowered() || radio.IsEditorRadio())
                continue;

            if (radio.TransceiversCount() == 0)
                continue;

            BaseTransceiver transceiver = radio.GetTransceiver(0);
            if (!transceiver || transceiver.GetFrequency() <= 0)
                continue;

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
}
