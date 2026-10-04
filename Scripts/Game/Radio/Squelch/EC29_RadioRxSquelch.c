//! Squelch bookkeeping for one frequency (shared by every local radio tuned to it).
class EC29_RxChannelState
{
	//! Senders whose key-start was accepted here -> world ms of that start.
	ref map<int, float> m_mKeyedAtMs = new map<int, float>();
	bool m_bVoiceActive;
	float m_fLastVoiceMs;
	bool m_bOpen;
	float m_fClosedAtMs;
	float m_fRpcClosedAtMs;
}

//------------------------------------------------------------------------------------------------
//! Receive-side squelch: decides when a local radio "opens" (RX-open sound) and "closes"
//! (RX-close sound) for incoming traffic.
//!
//! Two feeds drive it:
//!   - key-state broadcasts from EC29_RFPropagationNetworkComponent (primary; works on a dead key)
//!   - incoming radio voice packets from the modded SCR_VoNComponent (fallback)
//! It owns exactly one 150 ms ticker; both feeds only call EnsureTicking (single-ticker rule).
//!
//! Policy:
//!   - a remote key-start is honoured only if a local radio is tuned, powered and unmuted on the
//!     net and the sender is reachable (range, then RF quality when RF is on). A filtered start
//!     records nothing, so its stop is ignored too - an out-of-range sender can never close a
//!     channel someone else is keying, and a receiver that walks out mid-transmission still gets
//!     the stop for the start it accepted.
//!   - the last accepted stop closes the channel at once and discards in-flight voice for 400 ms.
//!     That window is shorter than the 500 ms reopen grace on purpose: a genuine voice-only talker
//!     resumes silently instead of drawing a second close beep.
//!   - mirrors engine delivery: no squelch on muted receivers or special nets.
//!
//! Plain object owned by EC29_RadioState: a world change discards all channels, RX records and
//! the ticker flag. All times are world time in ms; a fresh channel starts with closed-at and
//! RPC-closed-at at 0, so the first open in the first 500 ms of a world is silent and voice in the
//! first 400 ms is dropped.
class EC29_RadioRxSquelch
{
	protected static const int TICK_MS = 150;
	protected static const float VOICE_SILENCE_MS = 600;
	//! No open sound if the channel closed less than this long ago; also the idle-drop delay.
	protected static const float REOPEN_GRACE_MS = 500;
	protected static const float RPC_TAIL_DISCARD_MS = 400;
	//! Failsafe for lost stops.
	protected static const float MAX_KEY_HOLD_MS = 120000;
	protected static const float MIN_REACHABLE_QUALITY = 0.05;

	protected ref map<int, ref EC29_RxChannelState> m_mChannels = new map<int, ref EC29_RxChannelState>();
	//! Weak keys: a deleted radio reads back as null (see EC29_SweepDeadRadioRxRecords).
	protected ref map<BaseRadioComponent, float> m_mLastRxMsByRadio = new map<BaseRadioComponent, float>();
	protected bool m_bTickScheduled;

	//! Reused removal lists so the 150 ms tick does not allocate.
	protected ref array<int> m_aScratchExpired = {};
	protected ref array<int> m_aScratchIdle = {};

	//------------------------------------------------------------------------------------------------
	protected static float EC29_NowMs()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	void EnsureTicking()
	{
		if (m_bTickScheduled)
			return;

		m_bTickScheduled = true;
		GetGame().GetCallqueue().CallLater(EC29_RunScheduledTick, TICK_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Stops by itself on world teardown or when no channel is left; a stale instance from an old
	//! world therefore dies out on its own.
	protected void EC29_RunScheduledTick()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || !Tick(world.GetWorldTime()))
		{
			m_bTickScheduled = false;
			return;
		}

		GetGame().GetCallqueue().CallLater(EC29_RunScheduledTick, TICK_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Internal: only the squelch's own ticker calls this. Returns true while any channel remains.
	bool Tick(float nowMs)
	{
		m_aScratchIdle.Clear();

		foreach (int frequency, EC29_RxChannelState channel : m_mChannels)
		{
			EC29_DropStuckKeys(channel, nowMs);
			bool anyKeyed = !channel.m_mKeyedAtMs.IsEmpty();

			if (channel.m_bVoiceActive && nowMs - channel.m_fLastVoiceMs > VOICE_SILENCE_MS)
				channel.m_bVoiceActive = false;

			if (channel.m_bOpen && !anyKeyed && !channel.m_bVoiceActive)
				EC29_Close(channel, frequency, nowMs);

			if (!channel.m_bOpen && !anyKeyed && !channel.m_bVoiceActive && nowMs - channel.m_fClosedAtMs > REOPEN_GRACE_MS)
				m_aScratchIdle.Insert(frequency);
		}

		foreach (int idleFrequency : m_aScratchIdle)
		{
			m_mChannels.Remove(idleFrequency);
		}

		return !m_mChannels.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	//! Key-state broadcast handler (via EC29_RFPropagationNetworkComponent).
	void OnRemoteKeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller || controller.GetPlayerId() == senderPlayerId)
			return;

		float nowMs = EC29_NowMs();

		if (keyed)
		{
			BaseTransceiver tuned = EC29_FindTunedTransceiver(frequency);
			if (!tuned || !EC29_IsReachable(controller, senderPlayerId, frequency, range, senderPos))
				return;

			EC29_RxChannelState channel = EC29_GetOrCreateChannel(frequency);
			EC29_DropStuckKeys(channel, nowMs);
			channel.m_mKeyedAtMs.Set(senderPlayerId, nowMs);
			EC29_Open(channel, frequency, tuned, nowMs);
			return;
		}

		EC29_RxChannelState stopped;
		if (!m_mChannels.Find(frequency, stopped) || !stopped.m_mKeyedAtMs.Contains(senderPlayerId))
			return;

		stopped.m_mKeyedAtMs.Remove(senderPlayerId);
		if (!stopped.m_mKeyedAtMs.IsEmpty())
			return;

		// Last key released: close now instead of waiting for voice silence, and mark the
		// moment so in-flight voice packets do not reopen it.
		stopped.m_bVoiceActive = false;
		stopped.m_fRpcClosedAtMs = nowMs;
		EC29_Close(stopped, frequency, nowMs);
	}

	//------------------------------------------------------------------------------------------------
	//! Every incoming radio voice packet whose sender is not the local player.
	void OnVoicePacket(int frequency, BaseTransceiver receiver)
	{
		float nowMs = EC29_NowMs();

		// Arrival proves the native receiver is registered; the receiver guard relies on this, so
		// it is recorded before any squelch policy can bail out.
		if (receiver)
		{
			BaseRadioComponent radio = receiver.GetRadio();
			if (radio)
				m_mLastRxMsByRadio.Set(radio, nowMs);

			if (receiver.IsMuted() || EC29_CoexistenceGuard.EC29_IsSpecialNet(receiver))
				return;
		}

		// No power check: the engine only delivers voice to powered radios.
		EC29_RxChannelState channel = EC29_GetOrCreateChannel(frequency);
		EC29_DropStuckKeys(channel, nowMs);

		if (nowMs - channel.m_fRpcClosedAtMs < RPC_TAIL_DISCARD_MS)
			return; // tail of a transmission that a key-stop already closed

		channel.m_bVoiceActive = true;
		channel.m_fLastVoiceMs = nowMs;
		EC29_Open(channel, frequency, receiver, nowMs);
	}

	//------------------------------------------------------------------------------------------------
	//! World ms of the last voice packet on this radio, or -1 if none was ever recorded.
	float EC29_GetLastRadioRxMs(BaseRadioComponent radio)
	{
		if (!radio)
			return -1;

		float lastMs;
		if (m_mLastRxMsByRadio.Find(radio, lastMs))
			return lastMs;

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! Drops records of deleted radios. Several deleted radios all collapse onto the same null key,
	//! so the map is rebuilt from the live entries rather than edited in place.
	void EC29_SweepDeadRadioRxRecords()
	{
		map<BaseRadioComponent, float> live = new map<BaseRadioComponent, float>();
		foreach (BaseRadioComponent radio, float lastMs : m_mLastRxMsByRadio)
		{
			if (radio)
				live.Set(radio, lastMs);
		}

		m_mLastRxMsByRadio = live;
	}

	//------------------------------------------------------------------------------------------------
	protected EC29_RxChannelState EC29_GetOrCreateChannel(int frequency)
	{
		EC29_RxChannelState channel;
		if (!m_mChannels.Find(frequency, channel))
		{
			channel = new EC29_RxChannelState();
			m_mChannels.Set(frequency, channel);
		}

		return channel;
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_DropStuckKeys(EC29_RxChannelState channel, float nowMs)
	{
		m_aScratchExpired.Clear();

		foreach (int senderId, float keyedAtMs : channel.m_mKeyedAtMs)
		{
			if (nowMs - keyedAtMs > MAX_KEY_HOLD_MS)
				m_aScratchExpired.Insert(senderId);
		}

		foreach (int expiredId : m_aScratchExpired)
		{
			channel.m_mKeyedAtMs.Remove(expiredId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Logs and plays only on a real closed->open transition, never per packet.
	protected void EC29_Open(EC29_RxChannelState channel, int frequency, BaseTransceiver transceiver, float nowMs)
	{
		if (channel.m_bOpen)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioSquelch] OPEN freq=%1 t=%2", frequency, nowMs);

		channel.m_bOpen = true;

		if (nowMs - channel.m_fClosedAtMs >= REOPEN_GRACE_MS)
			EC29_RadioBeepHelper.PlayRxOpen(transceiver);
	}

	//------------------------------------------------------------------------------------------------
	//! The close sound goes to whatever is tuned to the net right now; a radio switched off, muted
	//! or retuned mid-transmission gets no close sound.
	protected void EC29_Close(EC29_RxChannelState channel, int frequency, float nowMs)
	{
		if (!channel.m_bOpen)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioSquelch] CLOSE freq=%1 t=%2", frequency, nowMs);

		channel.m_bOpen = false;
		channel.m_fClosedAtMs = nowMs;

		EC29_RadioBeepHelper.PlayRxClose(EC29_FindTunedTransceiver(frequency));
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's transceiver on this net, provided its radio is powered and it is unmuted.
	protected BaseTransceiver EC29_FindTunedTransceiver(int frequency)
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return null;

		SCR_VONController von = SCR_VONController.Cast(controller.FindComponent(SCR_VONController));
		if (!von)
			return null;

		SCR_VONEntryRadio entry = von.EC29_FindRadioEntryByFrequency(frequency);
		if (!entry)
			return null;

		BaseTransceiver transceiver = entry.GetTransceiver();
		if (!transceiver)
			return null;

		BaseRadioComponent radio = transceiver.GetRadio();
		if (!radio || !radio.IsPowered() || transceiver.IsMuted())
			return null;

		return transceiver;
	}

	//------------------------------------------------------------------------------------------------
	//! Unknown position or range means "assume reachable"; so does having no body. RF quality uses
	//! the same per-sender cache entry as the voice path, so a key-start followed by voice costs a
	//! single raymarch.
	protected bool EC29_IsReachable(PlayerController controller, int senderPlayerId, int frequency, float range, vector senderPos)
	{
		if (senderPos == vector.Zero || range <= 0)
			return true;

		IEntity body = controller.GetControlledEntity();
		if (!body)
			return true;

		vector listenerPos = body.GetOrigin();
		if (vector.Distance(senderPos, listenerPos) > range)
			return false;

		if (!EC29_RFPropagationNetworkComponent.IsRFPropagationEnabled())
			return true;

		float quality = EC29_RadioState.GetInstance().GetSignalQualityCached(senderPlayerId, senderPos, listenerPos, frequency);
		return quality >= MIN_REACHABLE_QUALITY;
	}
}
