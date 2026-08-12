//! Central world-scoped service for all EC29 radio runtime state.
//!
//! One lifecycle rule instead of four: the sub-services (ear/beep/volume
//! settings, RX squelch, jammer registry, RF propagation math) are plain
//! objects owned by this class, created together and discarded together when
//! the world changes. Script statics survive mission restarts and server hops
//! while world clocks, CallLater queues, and component pointers do not - so
//! the ONLY static in the radio runtime is the instance handle below, guarded
//! by a weak world reference that nulls when its world is destroyed.
class EC29_RadioState
{
	private static ref EC29_RadioState s_Instance;

	protected ref EC29_RadioEarSettings m_EarSettings;
	protected ref EC29_RadioRxSquelch m_Squelch;
	protected ref EC29_JammerRegistry m_Jammers;
	protected ref EC29_RFPropagationModel m_Propagation;

	//! Weak (no ref) - nulls when its world unloads, forcing a rebuild.
	protected BaseWorld m_OwnerWorld;

	//------------------------------------------------------------------------------------------------
	static EC29_RadioState GetInstance()
	{
		BaseWorld currentWorld = GetGame().GetWorld();

		if (!s_Instance || s_Instance.m_OwnerWorld != currentWorld)
		{
			if (s_Instance)
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
	float GetSignalQuality(vector transmitterPos, vector receiverPos, float frequencyKHz = 0)
	{
		return m_Propagation.CalculateSignalQuality(transmitterPos, receiverPos, frequencyKHz);
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
