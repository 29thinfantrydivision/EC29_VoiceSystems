[ComponentEditorProps(category: "GameScripted/GameMode/Components", description: "EC29 radio network glue: replicates the server's RF switches and relays radio key-up/key-down to every receiver.")]
class EC29_RFPropagationNetworkComponentClass : SCR_BaseGameModeComponentClass {}

//------------------------------------------------------------------------------------------------
//! A debounced key-stop waiting for its flush.
class EC29_PendingKeyStop
{
	int m_iFrequency;
	float m_fQueuedAtMs;
}

//------------------------------------------------------------------------------------------------
//! Game-mode component (attached by the Prefabs/MP/Modes/GameMode_Base.et override) with two jobs:
//!
//! 1. RF switches. The server copies EC29_RFPropagationSettings into two RplProps at post-init;
//!    every machine reads those through the static accessors, so the server's profile JSON decides
//!    RF on/off and RF debug for everyone.
//!
//! 2. Key-state relay. A transmitter's controller tells the server when it keys or unkeys
//!    (SCR_VONController.RpcAsk_EC29_KeyState -> EC29_RelayKeyState); the server validates,
//!    rate-limits starts, debounces stops by 300 ms, stamps the sender's position and broadcasts
//!    to everyone, so receivers open/close squelch even on a dead key with no voice packets.
//!    Stops are never rate-limited - a dropped stop would wedge every receiver open until the
//!    squelch's 120 s failsafe.
//!
//! It must stay a game-mode component: disconnect cleanup hangs off OnPlayerDisconnected.
class EC29_RFPropagationNetworkComponent : SCR_BaseGameModeComponent
{
	protected static const int MIN_FREQUENCY_KHZ = 1000;
	protected static const int MAX_FREQUENCY_KHZ = 1000000;
	protected static const float MAX_RANGE_M = 50000.0;
	protected static const float START_BUCKET_CAPACITY = 10;
	protected static const float START_BUCKET_WINDOW_MS = 4000;
	protected static const int STOP_DEBOUNCE_MS = 300;
	//! A flush only acts on a stop at least this old; a younger one was re-queued and has its
	//! own flush coming (300 ms debounce minus 50 ms scheduling jitter).
	protected static const float STOP_MIN_AGE_MS = 250;

	[RplProp(onRplName: "EC29_OnSwitchesReplicated")]
	protected bool m_bEC29_RFEnabled;

	[RplProp(onRplName: "EC29_OnSwitchesReplicated")]
	protected bool m_bEC29_DebugEnabled;

	//! Non-owning; set at post-init, cleared at delete.
	protected static EC29_RFPropagationNetworkComponent s_EC29_Instance;

	//! Server-only bookkeeping, keyed by player id. Lives as long as the component.
	protected ref map<int, int> m_mKeyedFrequencyByPlayer = new map<int, int>();
	protected ref map<int, ref EC29_PendingKeyStop> m_mPendingStopByPlayer = new map<int, ref EC29_PendingKeyStop>();
	protected ref map<int, ref EC29_TokenBucket> m_mStartBucketByPlayer = new map<int, ref EC29_TokenBucket>();

	//------------------------------------------------------------------------------------------------
	static EC29_RFPropagationNetworkComponent GetInstance()
	{
		return s_EC29_Instance;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsRFPropagationEnabled()
	{
		if (!s_EC29_Instance)
			return false;

		return s_EC29_Instance.m_bEC29_RFEnabled;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsDebugEnabled()
	{
		if (!s_EC29_Instance)
			return false;

		return s_EC29_Instance.m_bEC29_DebugEnabled;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool EC29_IsAuthority()
	{
		return !Replication.IsRunning() || Replication.IsServer();
	}

	//------------------------------------------------------------------------------------------------
	protected static float EC29_NowMs()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return 0;

		return world.GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_EC29_Instance = this;

		if (!EC29_IsAuthority())
			return;

		m_bEC29_RFEnabled = EC29_RFPropagationSettings.IsRFPropagationEnabled();
		m_bEC29_DebugEnabled = EC29_RFPropagationSettings.IsDebugEnabled();
		Replication.BumpMe();

		PrintFormat("[EC29 RFPropagation] Server settings loaded: RF propagation=%1 debug=%2", m_bEC29_RFEnabled, m_bEC29_DebugEnabled);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (s_EC29_Instance == this)
			s_EC29_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Shared by both props, so one sync can print twice.
	protected void EC29_OnSwitchesReplicated()
	{
		PrintFormat("[EC29 RFPropagation] Received server settings: RF propagation=%1 debug=%2", m_bEC29_RFEnabled, m_bEC29_DebugEnabled);
	}

	//------------------------------------------------------------------------------------------------
	//! Server entry point; sole caller is SCR_VONController.RpcAsk_EC29_KeyState.
	//! Validation happens before anything is amplified into reliable broadcasts to every player.
	void EC29_RelayKeyState(int senderPlayerId, int frequency, float range, bool keyed)
	{
		if (!EC29_IsAuthority())
			return;

		if (frequency < MIN_FREQUENCY_KHZ || frequency > MAX_FREQUENCY_KHZ)
		{
			PrintFormat("[EC29-DBG][RadioNet] Rejected key-state from player %1: frequency %2 kHz out of bounds", senderPlayerId, frequency, level: LogLevel.WARNING);
			return;
		}

		float safeRange = Math.Clamp(range, 0, MAX_RANGE_M);

		// Token check runs before the debounce check, so a same-frequency re-key inside the
		// debounce window still spends a token.
		if (keyed && !EC29_TakeStartToken(senderPlayerId))
		{
			PrintFormat("[EC29-DBG][RadioNet] Rate-limited key-start from player %1 on %2 kHz", senderPlayerId, frequency, level: LogLevel.WARNING);
			return;
		}

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioNet] Relay key-state: player=%1 freq=%2 range=%3 keyed=%4", senderPlayerId, frequency, safeRange, keyed);

		if (keyed)
			EC29_HandleKeyStart(senderPlayerId, frequency, safeRange);
		else
			EC29_QueueKeyStop(senderPlayerId, frequency);
	}

	//------------------------------------------------------------------------------------------------
	protected bool EC29_TakeStartToken(int playerId)
	{
		EC29_TokenBucket bucket;
		if (!m_mStartBucketByPlayer.Find(playerId, bucket))
		{
			bucket = new EC29_TokenBucket(START_BUCKET_CAPACITY, START_BUCKET_WINDOW_MS);
			m_mStartBucketByPlayer.Set(playerId, bucket);
		}

		return bucket.TryConsume(EC29_NowMs());
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_HandleKeyStart(int playerId, int frequency, float range)
	{
		EC29_PendingKeyStop pending;
		if (m_mPendingStopByPlayer.Find(playerId, pending))
		{
			int pendingFrequency = pending.m_iFrequency;
			m_mPendingStopByPlayer.Remove(playerId);

			// Re-key on the same net inside the debounce: receivers never saw a close, so the
			// transmission simply continues - nothing to send.
			if (pendingFrequency == frequency)
				return;

			// Moved to another net: release the old one now rather than at flush time.
			EC29_Broadcast(playerId, pendingFrequency, 0, false);
		}

		m_mKeyedFrequencyByPlayer.Set(playerId, frequency);
		EC29_Broadcast(playerId, frequency, range, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Stops are held back so push-to-talk spam collapses into one transmission (one open beep,
	//! one close beep). A newer stop replaces the queued one and schedules its own flush.
	protected void EC29_QueueKeyStop(int playerId, int frequency)
	{
		EC29_PendingKeyStop pending;
		if (!m_mPendingStopByPlayer.Find(playerId, pending))
		{
			pending = new EC29_PendingKeyStop();
			m_mPendingStopByPlayer.Set(playerId, pending);
		}

		pending.m_iFrequency = frequency;
		pending.m_fQueuedAtMs = EC29_NowMs();

		GetGame().GetCallqueue().CallLater(EC29_FlushKeyStop, STOP_DEBOUNCE_MS, false, playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_FlushKeyStop(int playerId)
	{
		EC29_PendingKeyStop pending;
		if (!m_mPendingStopByPlayer.Find(playerId, pending))
			return; // cancelled by a re-key

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return; // world teardown

		if (world.GetWorldTime() - pending.m_fQueuedAtMs < STOP_MIN_AGE_MS)
			return; // superseded; the newer stop's flush will handle it

		int frequency = pending.m_iFrequency;
		m_mPendingStopByPlayer.Remove(playerId);
		m_mKeyedFrequencyByPlayer.Remove(playerId);
		EC29_Broadcast(playerId, frequency, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! The sender's position is stamped here on the server, so receivers can range-gate without
	//! the sender entity (which may be outside their replication relevance). The handler also runs
	//! locally, because a broadcast does not reach the server itself - that serves a listen host.
	protected void EC29_Broadcast(int playerId, int frequency, float range, bool keyed)
	{
		vector senderPos = vector.Zero;
		IEntity sender = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (sender)
			senderPos = sender.GetOrigin();

		Rpc(EC29_RpcDo_RadioKeyState, playerId, frequency, range, keyed, senderPos);
		EC29_RpcDo_RadioKeyState(playerId, frequency, range, keyed, senderPos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void EC29_RpcDo_RadioKeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)
	{
		if (EC29_CoexistenceGuard.ShouldYieldRadio())
			return;

		EC29_RadioRxSquelch squelch = EC29_RadioState.GetInstance().Squelch();
		squelch.OnRemoteKeyState(senderPlayerId, frequency, range, keyed, senderPos);

		// Only a machine with a local player runs squelch; a dedicated server never ticks.
		if (GetGame().GetPlayerController())
			squelch.EnsureTicking();
	}

	//------------------------------------------------------------------------------------------------
	//! A player who leaves mid-transmission releases the channel through the normal (debounced,
	//! token-free) stop path; their rate-limit bucket goes so the map cannot grow for the life of
	//! the process.
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		if (!EC29_IsAuthority())
			return;

		int keyedFrequency;
		if (m_mKeyedFrequencyByPlayer.Find(playerId, keyedFrequency))
			EC29_RelayKeyState(playerId, keyedFrequency, 0, false);

		m_mStartBucketByPlayer.Remove(playerId);
	}
}
