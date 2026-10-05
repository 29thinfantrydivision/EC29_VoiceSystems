//! Per-radio listening preferences: which ear a radio plays in, its beep style, its channel
//! volume, plus the player's one alternate channel.
//!
//! Lifetime is the world's. EC29_RadioState builds this with the parameterless constructor and
//! rebuilds it on a world change, so nothing here is static and nothing is persisted - a new
//! world starts back at the defaults. Per-radio values are keyed by transceiver (one entry per
//! channel of a multi-channel radio); the alternate channel is a frequency, not a radio.

//! Written straight into the EC29_EarRouting audio variable (0..2). The voice ACPs,
//! EC29_beep.acp and EC29_Routing.sig branch on these numbers, so they never move. The numeric
//! order is NOT the cycle order.
enum EC29_EEarRouting
{
	CENTER = 0,
	RIGHT = 1,
	LEFT = 2
}

enum EC29_EBeepType
{
	OFF = 0,
	HIGH = 1,
	LOW = 2,
	CLASSIC = 3
}

class EC29_RadioEarSettings
{
	//! Volume is stored linear 0..1; the audio gain is volume^GAIN_EXPONENT for finer control
	//! at the quiet end. The voice path (EC29_VON_VoNComponent) applies the same exponent.
	protected const float GAIN_EXPONENT = 2.5;
	protected const float DEFAULT_VOLUME = 1.0;

	//! Manual routing choices and memoized device defaults, both. A hit here is final.
	protected ref map<BaseTransceiver, int> m_mRouting = new map<BaseTransceiver, int>();
	protected ref map<BaseTransceiver, int> m_mBeepStyle = new map<BaseTransceiver, int>();
	protected ref map<BaseTransceiver, float> m_mVolume = new map<BaseTransceiver, float>();

	//! Frequency (kHz) of the alternate channel; negative = none.
	protected int m_iAlternateFrequency = -1;
	//! Up while the alternate push-to-talk is held; the HUD paints the TX overlay from it.
	protected bool m_bTransmittingOnAlternate;

	//------------------------------------------------------------------------------------------------
	void EC29_RadioEarSettings()
	{
	}

	//------------------------------------------------------------------------------------------------
	// Ear routing
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Effective routing: the stored value if there is one, otherwise the device-class default.
	//! Runs per voice packet, so after the first query for a radio it is a single map hit.
	EC29_EEarRouting GetRouting(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return EC29_EEarRouting.CENTER;

		int stored;
		if (m_mRouting.Find(transceiver, stored))
			return stored;

		return ResolveDeviceDefault(transceiver);
	}

	//------------------------------------------------------------------------------------------------
	//! Squad net in the left ear, platoon net in the right (Issue #7, Docs/EarRouting_Defaults.md).
	//! Multi-channel personal radios split by channel index; single-channel radios go by device
	//! class; anything without a radio gadget (vehicle sets, editor transceivers) plays in both
	//! ears. A radio whose owning entity cannot be reached yet answers CENTER WITHOUT storing it,
	//! so the next query gets another chance to classify it properly.
	protected EC29_EEarRouting ResolveDeviceDefault(notnull BaseTransceiver transceiver)
	{
		// Another system's net: that system decides how its audio is presented. Not stored -
		// whether a transceiver is special depends on its tuning, which its owner may still be
		// setting up.
		if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
			return EC29_EEarRouting.CENTER;

		BaseRadioComponent radio = transceiver.GetRadio();
		if (!radio)
			return EC29_EEarRouting.CENTER;

		IEntity device = radio.GetOwner();
		if (!device)
			return EC29_EEarRouting.CENTER;

		EC29_EEarRouting routing = EC29_EEarRouting.CENTER;
		SCR_RadioComponent gadget = SCR_RadioComponent.Cast(device.FindComponent(SCR_RadioComponent));
		if (gadget)
		{
			if (radio.TransceiversCount() >= 2)
			{
				int channel = ChannelIndexOf(radio, transceiver);
				if (channel < 0)
					return EC29_EEarRouting.CENTER;

				if (channel == 0)
					routing = EC29_EEarRouting.LEFT;
				else
					routing = EC29_EEarRouting.RIGHT;
			}
			else
			{
				EGadgetType deviceClass = gadget.GetType();
				if (deviceClass == EGadgetType.RADIO)
					routing = EC29_EEarRouting.LEFT;
				else if (deviceClass == EGadgetType.RADIO_BACKPACK)
					routing = EC29_EEarRouting.RIGHT;
			}
		}

		m_mRouting.Set(transceiver, routing);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioEar] Default routing %1 stored for transceiver at %2 kHz (gadget=%3)", GetRoutingLetter(routing), transceiver.GetFrequency(), gadget != null);

		return routing;
	}

	//------------------------------------------------------------------------------------------------
	protected int ChannelIndexOf(notnull BaseRadioComponent radio, notnull BaseTransceiver transceiver)
	{
		int count = radio.TransceiversCount();
		for (int i = 0; i < count; i++)
		{
			if (radio.GetTransceiver(i) == transceiver)
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! T in the radial menu: CENTER -> LEFT -> RIGHT -> CENTER, starting from the effective
	//! routing so the first press moves on from the default. The result overrides the default
	//! for the rest of the session.
	EC29_EEarRouting CycleRouting(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return EC29_EEarRouting.CENTER;

		EC29_EEarRouting next;
		switch (GetRouting(transceiver))
		{
			case EC29_EEarRouting.CENTER: next = EC29_EEarRouting.LEFT;   break;
			case EC29_EEarRouting.LEFT:   next = EC29_EEarRouting.RIGHT;  break;
			case EC29_EEarRouting.RIGHT:  next = EC29_EEarRouting.CENTER; break;
			default:                      next = EC29_EEarRouting.CENTER; break;
		}

		m_mRouting.Set(transceiver, next);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioEar] Routing cycled to %1 (%2 kHz)", GetRoutingLetter(next), transceiver.GetFrequency());

		return next;
	}

	//------------------------------------------------------------------------------------------------
	string GetRoutingLetter(EC29_EEarRouting routing)
	{
		if (routing == EC29_EEarRouting.LEFT)
			return "L";

		if (routing == EC29_EEarRouting.RIGHT)
			return "R";

		return "C";
	}

	//------------------------------------------------------------------------------------------------
	// Beep style
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	EC29_EBeepType GetBeepStyle(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return EC29_EBeepType.HIGH;

		int stored;
		if (m_mBeepStyle.Find(transceiver, stored))
			return stored;

		return EC29_EBeepType.HIGH;
	}

	//------------------------------------------------------------------------------------------------
	//! K in the radial menu: OFF -> HIGH -> LOW -> CLASSIC -> OFF.
	EC29_EBeepType CycleBeepType(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return EC29_EBeepType.HIGH;

		EC29_EBeepType next;
		switch (GetBeepStyle(transceiver))
		{
			case EC29_EBeepType.OFF:     next = EC29_EBeepType.HIGH;    break;
			case EC29_EBeepType.HIGH:    next = EC29_EBeepType.LOW;     break;
			case EC29_EBeepType.LOW:     next = EC29_EBeepType.CLASSIC; break;
			case EC29_EBeepType.CLASSIC: next = EC29_EBeepType.OFF;     break;
			default:                     next = EC29_EBeepType.HIGH;    break;
		}

		m_mBeepStyle.Set(transceiver, next);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioEar] Beep style cycled to %1 (%2 kHz)", GetBeepStyleLongText(next), transceiver.GetFrequency());

		return next;
	}

	//------------------------------------------------------------------------------------------------
	//! Popup wording.
	string GetBeepStyleLongText(EC29_EBeepType style)
	{
		switch (style)
		{
			case EC29_EBeepType.OFF:     return "OFF";
			case EC29_EBeepType.LOW:     return "LO";
			case EC29_EBeepType.CLASSIC: return "CLS";
		}

		return "HI";
	}

	//------------------------------------------------------------------------------------------------
	//! Radial-label code.
	string GetBeepStyleShortCode(EC29_EBeepType style)
	{
		switch (style)
		{
			case EC29_EBeepType.OFF:     return "-";
			case EC29_EBeepType.LOW:     return "BL";
			case EC29_EBeepType.CLASSIC: return "CLS";
		}

		return "BH";
	}

	//------------------------------------------------------------------------------------------------
	// Channel volume
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	float GetVolume(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return DEFAULT_VOLUME;

		float stored;
		if (m_mVolume.Find(transceiver, stored))
			return stored;

		return DEFAULT_VOLUME;
	}

	//------------------------------------------------------------------------------------------------
	void SetVolume(BaseTransceiver transceiver, float volume)
	{
		if (!transceiver)
			return;

		m_mVolume.Set(transceiver, Math.Clamp(volume, 0.0, 1.0));
	}

	//------------------------------------------------------------------------------------------------
	//! Steps the volume by a signed delta and returns the clamped value now in force.
	float AdjustVolume(BaseTransceiver transceiver, float delta)
	{
		if (!transceiver)
			return DEFAULT_VOLUME;

		SetVolume(transceiver, GetVolume(transceiver) + delta);
		float applied = GetVolume(transceiver);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioEar] Volume adjusted by %1 -> %2 (%3 kHz)", delta, applied, transceiver.GetFrequency());

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	int GetVolumePercent(BaseTransceiver transceiver)
	{
		return Math.Round(GetVolume(transceiver) * 100);
	}

	//------------------------------------------------------------------------------------------------
	//! Value for the EC29_ChannelVolume audio variable.
	float GetVolumeGain(BaseTransceiver transceiver)
	{
		return Math.Pow(GetVolume(transceiver), GAIN_EXPONENT);
	}

	//------------------------------------------------------------------------------------------------
	// Alternate channel
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Negative = no alternate channel set.
	int GetAlternateFrequency()
	{
		return m_iAlternateFrequency;
	}

	//------------------------------------------------------------------------------------------------
	//! Matched by frequency: retuning a radio drops its alternate status, and any radio sitting
	//! on the alternate frequency counts.
	bool IsAlternate(BaseTransceiver transceiver)
	{
		if (!transceiver || m_iAlternateFrequency < 0)
			return false;

		return transceiver.GetFrequency() == m_iAlternateFrequency;
	}

	//------------------------------------------------------------------------------------------------
	//! Clears the alternate if this radio is it, otherwise makes this radio's frequency the
	//! alternate (replacing any previous one).
	void ToggleAlternate(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return;

		if (IsAlternate(transceiver))
			m_iAlternateFrequency = -1;
		else
			m_iAlternateFrequency = transceiver.GetFrequency();

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioEar] Alternate toggled on %1 kHz -> alternate now %2", transceiver.GetFrequency(), m_iAlternateFrequency);
	}

	//------------------------------------------------------------------------------------------------
	void SetTransmittingOnAlternate(bool transmitting)
	{
		m_bTransmittingOnAlternate = transmitting;
	}

	//------------------------------------------------------------------------------------------------
	bool IsTransmittingOnAlternate()
	{
		return m_bTransmittingOnAlternate;
	}
}
