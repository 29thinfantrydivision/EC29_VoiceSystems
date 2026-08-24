//! Per-frequency receive-side squelch state, one channel per frequency.
//! Fed by two sources: authoritative key-state RPCs relayed through
//! EC29_RFPropagationNetworkComponent (instant open/close, dead keys audible)
//! and the voice packet stream from SCR_VoNComponent (fallback for senders
//! that never sent a key RPC, e.g. other mods or GM transmissions).
//! A keyed channel stays open through dead air; a voice-only channel closes
//! by silence timeout via Tick().
class EC29_RxChannelState
{
    //! Sender id -> last key-start time; only senders whose key-start passed
    //! this receiver's tuning/reachability gates are entered, so their stops
    //! are the only ones that can release the channel.
    ref map<int, float> m_mKeyedSenders = new map<int, float>();
    bool m_bVoiceActive;
    float m_fLastVoiceMs;
    bool m_bOpen;
    float m_fClosedAtMs;
    float m_fRpcClosedAtMs;
}

class EC29_RadioRxSquelch
{
    //! Voice capture does silence detection, so the timeout must ride through
    //! natural speech pauses; the grace keeps borderline gaps from replaying
    //! the open beep. MAX_KEY_HOLD is a failsafe against a lost key-stop RPC.
    protected static const float SILENCE_TIMEOUT_MS = 600;
    protected static const float REOPEN_GRACE_MS = 500;
    protected static const float MAX_KEY_HOLD_MS = 120000;
    protected static const float MIN_SIGNAL_QUALITY = 0.05;
    //! Voice frames still in flight behind the reliable key-stop RPC must not
    //! reopen the channel (that would earn a second close beep from Tick);
    //! kept below REOPEN_GRACE_MS so a genuine still-talking voice-only sender
    //! resumes silently right after the window.
    protected static const float VOICE_TAIL_DISCARD_MS = 400;


    protected ref map<int, ref EC29_RxChannelState> m_mChannels = new map<int, ref EC29_RxChannelState>();

    //! Receiver-registration evidence: last world-time a voice packet arrived
    //! per local radio. Recorded BEFORE the mute/special-net gates below,
    //! because packet arrival proves the native receiver is registered no
    //! matter what policy does with the audio. The receiver guard reads this
    //! to skip repair cycles on provably-working radios and to flag radios
    //! that never receive (2026-08-24 field case: dead RX all session).
    protected ref map<BaseRadioComponent, float> m_mLastRxMsByRadio = new map<BaseRadioComponent, float>();

    //! Single-ticker rule: this singleton owns its own 150ms Tick loop. Both feed
    //! paths (voice packets via SCR_VoNComponent, key-state RPCs via
    //! EC29_RFPropagationNetworkComponent) call EnsureTicking() instead of running
    //! their own CallLater loops, so the state machine is never double-ticked.
    protected static const int TICK_INTERVAL_MS = 150;
    protected bool m_bTicking = false;


    //------------------------------------------------------------------------------------------------

    //------------------------------------------------------------------------------------------------
    void EnsureTicking()
    {
        if (m_bTicking)
            return;

        m_bTicking = true;
        GetGame().GetCallqueue().CallLater(EC29_TickLoop, TICK_INTERVAL_MS, false);
    }

    //------------------------------------------------------------------------------------------------
    protected void EC29_TickLoop()
    {
        // The self-rescheduling ticker can fire into world teardown; stop
        // cleanly so the stale instance drops out of the call queue.
        BaseWorld world = GetGame().GetWorld();
        if (!world)
        {
            m_bTicking = false;
            return;
        }

        float nowMs = world.GetWorldTime();

        if (Tick(nowMs))
            GetGame().GetCallqueue().CallLater(EC29_TickLoop, TICK_INTERVAL_MS, false);
        else
            m_bTicking = false;
    }

    //------------------------------------------------------------------------------------------------
    //! Remote player key state relayed by the server. Key-start is gated by
    //! frequency tuning and reachability so squelch mirrors what the voice
    //! path could actually deliver; key-stop is always processed so counts
    //! cannot wedge when the receiver moved out of range mid-transmission.
    void OnRemoteKeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)
    {
        PlayerController playerController = GetGame().GetPlayerController();
        if (!playerController)
            return;

        if (playerController.GetPlayerId() == senderPlayerId)
            return;

        float nowMs = GetGame().GetWorld().GetWorldTime();

        if (keyed)
        {
            BaseTransceiver transceiver = FindTunedTransceiver(frequency);
            if (!transceiver)
                return;

            if (!IsReachable(senderPlayerId, frequency, range, senderPos, playerController))
                return;

            EC29_RxChannelState state = GetOrCreateState(frequency);
            ExpireStuckKeys(state, nowMs);
            state.m_mKeyedSenders.Set(senderPlayerId, nowMs);
            Open(state, transceiver, nowMs);
        }
        else
        {
            EC29_RxChannelState state;
            if (!m_mChannels.Find(frequency, state))
                return;

            // Only honor stops whose start was accepted for this receiver, so a
            // filtered-out sender's release cannot close a channel someone else
            // is still keying.
            if (!state.m_mKeyedSenders.Contains(senderPlayerId))
                return;

            state.m_mKeyedSenders.Remove(senderPlayerId);

            // Voice stops with the key, so close immediately instead of waiting
            // out the silence timeout; the tail-discard window swallows voice
            // frames that were still in flight behind this RPC.
            if (state.m_mKeyedSenders.Count() == 0)
            {
                state.m_bVoiceActive = false;
                state.m_fRpcClosedAtMs = nowMs;
                Close(state, frequency, nowMs);
            }
        }
    }

    //------------------------------------------------------------------------------------------------
    //! Voice packet on a tuned radio; the caller filters out own transmissions.
    void OnVoicePacket(int frequency, BaseTransceiver receiver)
    {
        float nowMs = GetGame().GetWorld().GetWorldTime();

        if (receiver)
        {
            BaseRadioComponent rxRadio = receiver.GetRadio();
            if (rxRadio)
                m_mLastRxMsByRadio.Set(rxRadio, nowMs);
        }

        // No squelch on a muted receiver (the engine still delivers packets to
        // muted transceivers - the spectator mod's radio-OFF toggle is a mute),
        // and none on another system's net (their mod curates that audio).
        if (receiver && (receiver.IsMuted() || EC29_CoexistenceGuard.EC29_IsSpecialNet(receiver)))
            return;

        EC29_RxChannelState state = GetOrCreateState(frequency);
        ExpireStuckKeys(state, nowMs);

        if (nowMs - state.m_fRpcClosedAtMs < VOICE_TAIL_DISCARD_MS)
            return;

        state.m_bVoiceActive = true;
        state.m_fLastVoiceMs = nowMs;
        Open(state, receiver, nowMs);
    }

    //------------------------------------------------------------------------------------------------
    //! Last world-time any voice packet arrived on this radio, -1 if never.
    float EC29_GetLastRadioRxMs(BaseRadioComponent radio)
    {
        float lastMs;
        if (radio && m_mLastRxMsByRadio.Find(radio, lastMs))
            return lastMs;

        return -1;
    }

    //------------------------------------------------------------------------------------------------
    //! Drop records whose radio was deleted (handles null) so the map cannot
    //! grow for the world lifetime. Rebuilds instead of removing in place -
    //! several deleted radios all read back as the same null key.
    void EC29_SweepDeadRadioRxRecords()
    {
        bool hasDead = false;
        foreach (BaseRadioComponent radio, float lastMs : m_mLastRxMsByRadio)
        {
            if (!radio)
            {
                hasDead = true;
                break;
            }
        }

        if (!hasDead)
            return;

        map<BaseRadioComponent, float> live = new map<BaseRadioComponent, float>();
        foreach (BaseRadioComponent radio, float lastMs : m_mLastRxMsByRadio)
        {
            if (radio)
                live.Set(radio, lastMs);
        }

        m_mLastRxMsByRadio = live;
    }

    //------------------------------------------------------------------------------------------------
    //! Advance timeouts; close voice-silent channels and drop idle state.
    //! \return true while any channel still needs ticking
    bool Tick(float nowMs)
    {
        array<int> idle = {};
        foreach (int frequency, EC29_RxChannelState state : m_mChannels)
        {
            ExpireStuckKeys(state, nowMs);

            if (state.m_bVoiceActive && nowMs - state.m_fLastVoiceMs > SILENCE_TIMEOUT_MS)
                state.m_bVoiceActive = false;

            if (state.m_bOpen && state.m_mKeyedSenders.Count() == 0 && !state.m_bVoiceActive)
                Close(state, frequency, nowMs);

            if (!state.m_bOpen && state.m_mKeyedSenders.Count() == 0 && !state.m_bVoiceActive && nowMs - state.m_fClosedAtMs > REOPEN_GRACE_MS)
                idle.Insert(frequency);
        }

        foreach (int frequency : idle)
            m_mChannels.Remove(frequency);

        return m_mChannels.Count() > 0;
    }

    //------------------------------------------------------------------------------------------------
    protected void Open(EC29_RxChannelState state, BaseTransceiver transceiver, float nowMs)
    {
        // Guard BEFORE logging: this runs per voice packet, logging belongs to
        // the state transition only.
        if (state.m_bOpen)
            return;

        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][RadioSquelch] Channel OPEN (squelch beep) t=%1", nowMs);

        state.m_bOpen = true;

        if (nowMs - state.m_fClosedAtMs < REOPEN_GRACE_MS)
            return;

        EC29_RadioBeepHelper.PlayRxOpen(transceiver);
    }

    //------------------------------------------------------------------------------------------------
    protected void Close(EC29_RxChannelState state, int frequency, float nowMs)
    {
        if (!state.m_bOpen)
            return;

        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][RadioSquelch] Channel CLOSE freq=%1 t=%2", frequency, nowMs);

        state.m_bOpen = false;
        state.m_fClosedAtMs = nowMs;

        BaseTransceiver transceiver = FindTunedTransceiver(frequency);
        if (transceiver)
            EC29_RadioBeepHelper.PlayRxClose(transceiver);
    }

    //------------------------------------------------------------------------------------------------
    protected void ExpireStuckKeys(EC29_RxChannelState state, float nowMs)
    {
        if (state.m_mKeyedSenders.Count() == 0)
            return;

        array<int> stale = {};
        foreach (int senderId, float lastKeyMs : state.m_mKeyedSenders)
        {
            if (nowMs - lastKeyMs > MAX_KEY_HOLD_MS)
                stale.Insert(senderId);
        }

        foreach (int senderId : stale)
            state.m_mKeyedSenders.Remove(senderId);
    }

    //------------------------------------------------------------------------------------------------
    protected EC29_RxChannelState GetOrCreateState(int frequency)
    {
        EC29_RxChannelState state;
        if (m_mChannels.Find(frequency, state))
            return state;

        state = new EC29_RxChannelState();
        m_mChannels.Set(frequency, state);
        return state;
    }

    //------------------------------------------------------------------------------------------------
    protected BaseTransceiver FindTunedTransceiver(int frequency)
    {
        PlayerController playerController = GetGame().GetPlayerController();
        if (!playerController)
            return null;

        SCR_VONController vonController = SCR_VONController.Cast(playerController.FindComponent(SCR_VONController));
        if (!vonController)
            return null;

        SCR_VONEntryRadio entry = vonController.EC29_FindRadioEntryByFrequency(frequency);
        if (!entry)
            return null;

        BaseTransceiver transceiver = entry.GetTransceiver();
        if (!transceiver)
            return null;

        // The engine only delivers voice to powered radios; mirror that gate so
        // a switched-off radio never squelches. Muted mirrors the same idea -
        // the player (or a spectator system's OFF toggle) asked for silence.
        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio || !radio.IsPowered() || transceiver.IsMuted())
            return null;

        return transceiver;
    }

    //------------------------------------------------------------------------------------------------
    protected bool IsReachable(int senderPlayerId, int frequency, float range, vector senderPos, PlayerController playerController)
    {
        if (senderPos == vector.Zero || range <= 0)
            return true;

        IEntity myEntity = playerController.GetControlledEntity();
        if (!myEntity)
            return true;

        vector myPos = myEntity.GetOrigin();
        if (vector.Distance(senderPos, myPos) > range)
            return false;

        if (EC29_RFPropagationNetworkComponent.IsRFPropagationEnabled())
        {
            // Cached per sender: shares the TTL entry with the voice-packet
            // path so a key-start followed by voice costs one raymarch, not two.
            float quality = EC29_RadioState.GetInstance().GetSignalQualityCached(senderPlayerId, senderPos, myPos, frequency);
            if (quality < MIN_SIGNAL_QUALITY)
                return false;
        }

        return true;
    }
}
