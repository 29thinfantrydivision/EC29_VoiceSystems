//! Central world-scoped service for all EC29 radio runtime state.
//!
//! One lifecycle rule instead of four: the sub-services (ear/beep/volume
//! settings, RX squelch, jammer registry, RF propagation math) are plain
//! objects owned by this class, created together and discarded together when
//! the world changes. Script statics survive mission restarts and server hops
//! while world clocks, CallLater queues, and component pointers do not - so
//! the ONLY static in the radio runtime is the instance handle below, guarded
//! by a weak world reference that nulls when its world is destroyed.

//! One cached propagation result per transmitting player (see
//! EC29_RadioState.GetSignalQualityCached).
class EC29_SignalQualityCacheEntry
{
	float m_fQuality;
	float m_fComputedAtMs;
	float m_fFrequencyKHz;
}

class EC29_RadioState
{
	private static ref EC29_RadioState s_Instance;

	protected ref EC29_RadioEarSettings m_EarSettings;
	protected ref EC29_RadioRxSquelch m_Squelch;
	protected ref EC29_JammerRegistry m_Jammers;
	protected ref EC29_RFPropagationModel m_Propagation;
	protected ref EC29_RadioReceiverGuard m_ReceiverGuard;
	protected ref EC29_SpectatorVonService m_SpectatorVon;
	protected ref EC29_VonActivityService m_VonActivity;

	//! The propagation model raymarches up to 200 terrain samples per query and
	//! the query sits on the per-voice-packet hot path, so results are cached
	//! per sender with a short TTL. Terrain and positions cannot change enough
	//! inside the window to matter; a frequency retune invalidates immediately.
	protected static const float SIGNAL_QUALITY_TTL_MS = 250;
	protected ref map<int, ref EC29_SignalQualityCacheEntry> m_mSignalQualityBySender = new map<int, ref EC29_SignalQualityCacheEntry>();

	//! Weak (no ref) - nulls when its world unloads, forcing a rebuild.
	protected BaseWorld m_OwnerWorld;

	//------------------------------------------------------------------------------------------------
	static EC29_RadioState GetInstance()
	{
		BaseWorld currentWorld = GetGame().GetWorld();

		if (!s_Instance || s_Instance.m_OwnerWorld != currentWorld)
		{
			if (s_Instance && EC29_Debug.VERBOSE)
				Print("[EC29-DBG][RadioState] World changed - rebuilding radio runtime state", LogLevel.NORMAL);

			s_Instance = new EC29_RadioState();
			s_Instance.m_OwnerWorld = currentWorld;
		}

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	void EC29_RadioState()
	{
		m_EarSettings = new EC29_RadioEarSettings();
		m_Squelch = new EC29_RadioRxSquelch();
		m_Jammers = new EC29_JammerRegistry();
		m_Propagation = new EC29_RFPropagationModel();
		m_ReceiverGuard = new EC29_RadioReceiverGuard();
		m_SpectatorVon = new EC29_SpectatorVonService();
		m_VonActivity = new EC29_VonActivityService();
	}

	//------------------------------------------------------------------------------------------------
	//! Who is talking, as heard by the local client - the data feed behind spectator speaking
	//! indicators. Spectator-scoped at the feed; see EC29_VonActivityService.
	EC29_VonActivityService VonActivity()
	{
		return m_VonActivity;
	}

	//------------------------------------------------------------------------------------------------
	//! Spectator voice service - the EC29-owned half of the spectator mod's voice split (the
	//! spectator mod keeps lifecycle and input, this keeps voice). Sharing the world-scoped
	//! lifecycle here is what guarantees no spectator voice state survives a scenario change.
	EC29_SpectatorVonService SpectatorVon()
	{
		return m_SpectatorVon;
	}

	//------------------------------------------------------------------------------------------------
	EC29_RadioReceiverGuard ReceiverGuard()
	{
		return m_ReceiverGuard;
	}

	//------------------------------------------------------------------------------------------------
	EC29_RadioEarSettings EarSettings()
	{
		return m_EarSettings;
	}

	//------------------------------------------------------------------------------------------------
	EC29_RadioRxSquelch Squelch()
	{
		return m_Squelch;
	}

	//------------------------------------------------------------------------------------------------
	EC29_JammerRegistry Jammers()
	{
		return m_Jammers;
	}

	//------------------------------------------------------------------------------------------------
	//! RF propagation quality between two points, 0..1. Returns 1.0 when the
	//! propagation simulation is disabled server-side.
	//! Uncached - prefer GetSignalQualityCached on any per-packet path.
	float GetSignalQuality(vector transmitterPos, vector receiverPos, float frequencyKHz = 0)
	{
		return m_Propagation.CalculateSignalQuality(transmitterPos, receiverPos, frequencyKHz);
	}

	//------------------------------------------------------------------------------------------------
	//! Hot-path variant: one propagation result per transmitting player, reused
	//! for SIGNAL_QUALITY_TTL_MS. Voice packets arrive far faster than either
	//! party can move, so the raymarch runs at most ~4x/second per sender
	//! instead of per packet. A frequency change bypasses the cache.
	float GetSignalQualityCached(int senderPlayerId, vector transmitterPos, vector receiverPos, float frequencyKHz = 0)
	{
		// No world clock means no usable cache; fall through to a direct compute.
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return GetSignalQuality(transmitterPos, receiverPos, frequencyKHz);

		float nowMs = world.GetWorldTime();

		EC29_SignalQualityCacheEntry entry;
		if (m_mSignalQualityBySender.Find(senderPlayerId, entry))
		{
			float age = nowMs - entry.m_fComputedAtMs;
			if (age >= 0 && age < SIGNAL_QUALITY_TTL_MS && entry.m_fFrequencyKHz == frequencyKHz)
				return entry.m_fQuality;
		}
		else
		{
			entry = new EC29_SignalQualityCacheEntry();
			m_mSignalQualityBySender.Set(senderPlayerId, entry);
		}

		entry.m_fQuality = m_Propagation.CalculateSignalQuality(transmitterPos, receiverPos, frequencyKHz);
		entry.m_fComputedAtMs = nowMs;
		entry.m_fFrequencyKHz = frequencyKHz;
		return entry.m_fQuality;
	}

	//------------------------------------------------------------------------------------------------
	//! Worst jammer degradation at the receiver position: 0.0 = clean, 1.0 = fully jammed.
	float GetJammerStrength(vector receiverPos)
	{
		return m_Jammers.CalculateJammerDegradation(receiverPos);
	}
}

//! Token-bucket rate limiter for radio key-ups. Capacity tokens refill at a
//! fixed rate; each key consumes one; an empty bucket denies the key. Replaces
//! sliding-window timestamp arrays with two floats of state.
class EC29_TokenBucket
{
	protected float m_fCapacity;
	protected float m_fRefillPerMs;
	protected float m_fTokens;
	protected float m_fLastRefillMs;

	//------------------------------------------------------------------------------------------------
	void EC29_TokenBucket(float capacity, float windowMs)
	{
		m_fCapacity = capacity;
		m_fRefillPerMs = capacity / windowMs;
		m_fTokens = capacity;
		m_fLastRefillMs = -1;
	}

	//------------------------------------------------------------------------------------------------
	bool TryConsume(float nowMs)
	{
		if (m_fLastRefillMs >= 0)
		{
			// Clock went backwards = new world reused this bucket; reset full.
			if (nowMs < m_fLastRefillMs)
				m_fTokens = m_fCapacity;
			else
				m_fTokens = Math.Min(m_fCapacity, m_fTokens + (nowMs - m_fLastRefillMs) * m_fRefillPerMs);
		}

		m_fLastRefillMs = nowMs;

		if (m_fTokens < 1)
			return false;

		m_fTokens = m_fTokens - 1;
		return true;
	}
}
