//------------------------------------------------------------------------------------------------
class EC29_PendingKeyStop
{
	int m_iFrequency;
	float m_fQueuedAtMs;
}

//------------------------------------------------------------------------------------------------
class EC29_RFPropagationNetworkComponentClass : SCR_BaseGameModeComponentClass
{
};

class EC29_RFPropagationNetworkComponent : SCR_BaseGameModeComponent
{
	protected static EC29_RFPropagationNetworkComponent s_Instance;

	[RplProp(onRplName: "OnSettingsReceived")]
	protected bool m_bRFPropagationEnabled;

	[RplProp(onRplName: "OnSettingsReceived")]
	protected bool m_bDebugEnabled;

	//! Server-only: which frequency each currently-keyed player is holding open
	protected ref map<int, int> m_mEC29_KeyedFreqByPlayer = new map<int, int>();

	//! Server-only: key-stops held briefly before broadcast; a re-key inside the
	//! window cancels the stop so PTT spam reads as one continuous transmission
	//! for receivers instead of a close beep per cycle (and generates no traffic).
	protected ref map<int, ref EC29_PendingKeyStop> m_mEC29_PendingStops = new map<int, ref EC29_PendingKeyStop>();
	protected static const int EC29_KEY_STOP_DEBOUNCE_MS = 300;

	//! Server-side sanity bounds for client-supplied key-state RPCs: a tampered
	//! client must not amplify garbage into reliable broadcasts to every player.
	protected static const int EC29_MIN_FREQUENCY_KHZ = 1000;
	protected static const int EC29_MAX_FREQUENCY_KHZ = 1000000;
	protected static const float EC29_MAX_RANGE_M = 50000;
	protected static const float EC29_RELAY_BUCKET_CAPACITY = 10;
	protected static const float EC29_RELAY_BUCKET_WINDOW_MS = 4000;
	protected ref map<int, ref EC29_TokenBucket> m_mEC29_RelayBucketByPlayer = new map<int, ref EC29_TokenBucket>();


	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;

		if (Replication.IsServer())
		{
			EC29_RFPropagationSettings settings = EC29_RFPropagationSettings.GetInstance();
			m_bRFPropagationEnabled = settings.IsRFPropagationEnabled();
			m_bDebugEnabled = settings.IsDebugEnabled();

			Replication.BumpMe();

			Print(string.Format("[EC29 RFPropagation] Server settings loaded - RF: %1 | Debug: %2",
				m_bRFPropagationEnabled, m_bDebugEnabled));
		}
	}

	//------------------------------------------------------------------------------------------------
	static EC29_RFPropagationNetworkComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnSettingsReceived()
	{
		Print(string.Format("[EC29 RFPropagation] Received server settings - RF: %1 | Debug: %2",
			m_bRFPropagationEnabled, m_bDebugEnabled));
	}

	//------------------------------------------------------------------------------------------------
	static bool IsRFPropagationEnabled()
	{
		if (!s_Instance)
			return false;

		return s_Instance.m_bRFPropagationEnabled;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsDebugEnabled()
	{
		if (!s_Instance)
			return false;

		return s_Instance.m_bDebugEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! Server: relay a transmitter's key state to every client so receivers can
	//! play squelch open/close instantly, dead keys included. Sender position is
	//! stamped here so receivers can range-gate without knowing the transmitter
	//! entity (which may be outside their replication relevance).
	void EC29_RelayKeyState(int senderPlayerId, int frequency, float range, bool keyed)
	{
		if (!Replication.IsServer())
			return;

		// Validate client-supplied values before amplifying them into broadcasts.
		if (frequency < EC29_MIN_FREQUENCY_KHZ || frequency > EC29_MAX_FREQUENCY_KHZ)
		{
			PrintFormat("[EC29-DBG][RadioNet] SERVER rejected key-state from player %1: frequency %2 out of bounds", senderPlayerId, frequency, level: LogLevel.WARNING);
			return;
		}

		range = Math.Clamp(range, 0, EC29_MAX_RANGE_M);

		if (!EC29_CheckRelayRate(senderPlayerId))
			return;

		PrintFormat("[EC29-DBG][RadioNet] SERVER relay key-state: player=%1 freq=%2 range=%3 keyed=%4", senderPlayerId, frequency, range, keyed);

		if (keyed)
		{
			EC29_PendingKeyStop pending;
			if (m_mEC29_PendingStops.Find(senderPlayerId, pending))
			{
				m_mEC29_PendingStops.Remove(senderPlayerId);

				// Re-key of the same frequency inside the debounce window:
				// receivers never saw the close, so there is nothing to send.
				if (pending.m_iFrequency == frequency)
					return;

				EC29_BroadcastKeyState(senderPlayerId, pending.m_iFrequency, 0.0, false);
			}

			m_mEC29_KeyedFreqByPlayer.Set(senderPlayerId, frequency);
			EC29_BroadcastKeyState(senderPlayerId, frequency, range, true);
		}
		else
		{
			EC29_PendingKeyStop pending = new EC29_PendingKeyStop();
			pending.m_iFrequency = frequency;
			pending.m_fQueuedAtMs = GetGame().GetWorld().GetWorldTime();
			m_mEC29_PendingStops.Set(senderPlayerId, pending);
			GetGame().GetCallqueue().CallLater(EC29_FlushPendingStop, EC29_KEY_STOP_DEBOUNCE_MS, false, senderPlayerId);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server-side key-rate limiter (token bucket per player) mirroring the
	//! client-side bucket; a compliant client drains its own first.
	protected bool EC29_CheckRelayRate(int playerId)
	{
		float nowMs = GetGame().GetWorld().GetWorldTime();

		EC29_TokenBucket bucket;
		if (!m_mEC29_RelayBucketByPlayer.Find(playerId, bucket))
		{
			bucket = new EC29_TokenBucket(EC29_RELAY_BUCKET_CAPACITY, EC29_RELAY_BUCKET_WINDOW_MS);
			m_mEC29_RelayBucketByPlayer.Set(playerId, bucket);
		}

		if (!bucket.TryConsume(nowMs))
		{
			PrintFormat("[EC29-DBG][RadioNet] SERVER rate-limited key-state relay from player %1", playerId, level: LogLevel.WARNING);
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_FlushPendingStop(int playerId)
	{
		EC29_PendingKeyStop pending;
		if (!m_mEC29_PendingStops.Find(playerId, pending))
			return;

		// A newer stop queued during our delay has its own flush call coming;
		// the 50ms margin absorbs CallLater frame jitter on our own entry.
		float nowMs = GetGame().GetWorld().GetWorldTime();
		if (nowMs - pending.m_fQueuedAtMs < EC29_KEY_STOP_DEBOUNCE_MS - 50)
			return;

		m_mEC29_PendingStops.Remove(playerId);
		m_mEC29_KeyedFreqByPlayer.Remove(playerId);
		EC29_BroadcastKeyState(playerId, pending.m_iFrequency, 0.0, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_BroadcastKeyState(int senderPlayerId, int frequency, float range, bool keyed)
	{
		vector senderPos = vector.Zero;
		IEntity senderEntity = GetGame().GetPlayerManager().GetPlayerControlledEntity(senderPlayerId);
		if (senderEntity)
			senderPos = senderEntity.GetOrigin();

		Rpc(RpcDo_EC29_KeyState, senderPlayerId, frequency, range, keyed, senderPos);
		RpcDo_EC29_KeyState(senderPlayerId, frequency, range, keyed, senderPos);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_EC29_KeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)
	{
		// Coexistence: a known conflicting VON mod runs its own key-state/squelch system.
		if (EC29_CoexistenceGuard.ShouldYieldRadio())
			return;

		EC29_RadioState.GetInstance().Squelch().OnRemoteKeyState(senderPlayerId, frequency, range, keyed, senderPos);

		// Dead-key channels produce no voice packets, so the voice-packet feed path
		// may never run; keep timeouts advancing from here as well. The squelch
		// singleton owns the ticker (single-ticker rule).
		if (GetGame().GetPlayerController())
			EC29_RadioState.GetInstance().Squelch().EnsureTicking();
	}

	//------------------------------------------------------------------------------------------------
	//! A player who disconnects mid-key never sends the key-stop RPC; release
	//! their channel for everyone.
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		if (!Replication.IsServer())
			return;

		int frequency;
		if (m_mEC29_KeyedFreqByPlayer.Find(playerId, frequency))
			EC29_RelayKeyState(playerId, frequency, 0.0, false);
	}
}
