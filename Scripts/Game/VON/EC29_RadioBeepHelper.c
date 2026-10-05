//! Radio beeps. TX beeps confirm the operator's own key-up and release; RX beeps mark squelch
//! opening and closing on someone else's transmission - deliberately lopsided, a subtle tail on
//! open and the prominent tone on close. The style comes from the radio's own setting.
//!
//! Live beeps are opt-in: they play only while the persisted RadioBeepsEnabled switch (Audio
//! tab) is on. The K-press preview ignores that switch on purpose - it is the feedback for a
//! deliberate press and doubles as a speaker test of the beep path.
//!
//! Every beep is routed like radio voice: the ear-routing and channel-volume audio variables are
//! written for that radio first, then the event fires as a 2D sound. Those variables are global
//! and the voice path rewrites them per packet as well; that is the existing design.
class EC29_RadioBeepHelper
{
	static const string BEEP_CONFIG = "{63926E92E2606681}Sounds/VON/EC29_beep.acp";
	static const string EAR_ROUTING_VARS = "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf";

	//! Event names inside EC29_beep.acp. Asset contract.
	protected static const string EVT_BEEP_HIGH = "EC29_BEEP_HIGH";
	protected static const string EVT_BEEP_LOW = "EC29_BEEP_LOW";
	protected static const string EVT_CLICK_OFF = "EC29_CLICK_OFF";
	protected static const string EVT_CLASSIC_START = "EC29_CLASSIC_START";
	protected static const string EVT_CLASSIC_END = "EC29_CLASSIC_END";
	protected static const string EVT_SQUELCH_TAIL = "EC29_SQUELCH_TAIL";

	//! Persisted switch, owned by the Audio-tab settings module. Read by name only.
	protected static const string SETTINGS_MODULE = "EC29_RadioSettings";
	protected static const string SETTINGS_FIELD = "RadioBeepsEnabled";

	//! The moments a beep can mark.
	protected static const int MOMENT_TX_START = 0;
	protected static const int MOMENT_TX_END = 1;
	protected static const int MOMENT_RX_OPEN = 2;
	protected static const int MOMENT_RX_CLOSE = 3;
	protected static const int MOMENT_PREVIEW = 4;

	//! The only static state in the radio layer, intentionally: warn once per game run when the
	//! settings module is missing rather than once per squelch event.
	protected static bool s_bWarnedModuleMissing;

	//------------------------------------------------------------------------------------------------
	//! The persisted master switch. A missing settings module counts as OFF, with one WARNING per
	//! run - otherwise the Audio-tab checkbox would silently do nothing.
	static bool EC29_AreBeepsEnabled()
	{
		BaseContainer module = null;
		UserSettings userSettings = GetGame().GetGameUserSettings();
		if (userSettings)
			module = userSettings.GetModule(SETTINGS_MODULE);

		if (!module)
		{
			if (!s_bWarnedModuleMissing)
			{
				s_bWarnedModuleMissing = true;
				Print("[EC29] EC29_RadioSettings module not found in game user settings - radio beeps forced OFF and the Audio-tab checkbox will not work", LogLevel.WARNING);
			}
			return false;
		}

		bool enabled = false;
		module.Get(SETTINGS_FIELD, enabled);
		return enabled;
	}

	//------------------------------------------------------------------------------------------------
	//! Key-up confirmation (controller, after the rate limit passed).
	static void PlayTxStart(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioBeep] TX start beep requested (%1 kHz)", transceiver.GetFrequency());

		PlayIfEnabled(transceiver, MOMENT_TX_START);
	}

	//------------------------------------------------------------------------------------------------
	//! Release confirmation (controller, before vanilla deactivation).
	static void PlayTxEnd(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioBeep] TX end beep requested (%1 kHz)", transceiver.GetFrequency());

		PlayIfEnabled(transceiver, MOMENT_TX_END);
	}

	//------------------------------------------------------------------------------------------------
	//! Squelch opened on an incoming transmission (EC29_RadioRxSquelch).
	static void PlayRxOpen(BaseTransceiver transceiver)
	{
		PlayIfEnabled(transceiver, MOMENT_RX_OPEN);
	}

	//------------------------------------------------------------------------------------------------
	//! Squelch closed after an incoming transmission (EC29_RadioRxSquelch).
	static void PlayRxClose(BaseTransceiver transceiver)
	{
		PlayIfEnabled(transceiver, MOMENT_RX_CLOSE);
	}

	//------------------------------------------------------------------------------------------------
	//! Sample of the radio's current style after a K press. Bypasses the master switch; OFF
	//! previews as silence, which is the right answer for OFF.
	static void PlayPreview(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return;

		PlayRouted(transceiver, EventFor(StyleOf(transceiver), MOMENT_PREVIEW));
	}

	//------------------------------------------------------------------------------------------------
	protected static void PlayIfEnabled(BaseTransceiver transceiver, int moment)
	{
		if (!transceiver)
			return;

		// The common path with default settings, and it runs per squelch event: silent outside
		// VERBOSE.
		if (!EC29_AreBeepsEnabled())
		{
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][RadioBeep] Beep suppressed - master switch off (moment %1, %2 kHz)", moment, transceiver.GetFrequency());
			return;
		}

		PlayRouted(transceiver, EventFor(StyleOf(transceiver), moment));
	}

	//------------------------------------------------------------------------------------------------
	protected static EC29_EBeepType StyleOf(BaseTransceiver transceiver)
	{
		return EC29_RadioState.GetInstance().EarSettings().GetBeepStyle(transceiver);
	}

	//------------------------------------------------------------------------------------------------
	//! Style x moment -> event. Empty = play nothing (OFF, or an unknown style).
	//!
	//!               HIGH            LOW             CLASSIC
	//!   TX start    BEEP_HIGH       BEEP_LOW        CLASSIC_START
	//!   TX end      CLICK_OFF       CLICK_OFF       CLASSIC_END
	//!   RX open     SQUELCH_TAIL    SQUELCH_TAIL    CLASSIC_START
	//!   RX close    BEEP_HIGH       BEEP_LOW        CLASSIC_END
	//!   preview     BEEP_HIGH       BEEP_LOW        CLASSIC_START
	protected static string EventFor(EC29_EBeepType style, int moment)
	{
		if (style == EC29_EBeepType.CLASSIC)
		{
			if (moment == MOMENT_TX_END || moment == MOMENT_RX_CLOSE)
				return EVT_CLASSIC_END;

			return EVT_CLASSIC_START;
		}

		if (style != EC29_EBeepType.HIGH && style != EC29_EBeepType.LOW)
			return string.Empty;

		if (moment == MOMENT_TX_END)
			return EVT_CLICK_OFF;

		if (moment == MOMENT_RX_OPEN)
			return EVT_SQUELCH_TAIL;

		// Key-up, squelch close and preview all carry the style's own tone.
		if (style == EC29_EBeepType.LOW)
			return EVT_BEEP_LOW;

		return EVT_BEEP_HIGH;
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the radio's routing and channel gain, then fires the event non-positionally.
	protected static void PlayRouted(notnull BaseTransceiver transceiver, string eventName)
	{
		if (eventName.IsEmpty())
			return;

		EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
		AudioSystem.SetVariableByName("EC29_EarRouting", settings.GetRouting(transceiver), EAR_ROUTING_VARS);
		AudioSystem.SetVariableByName("EC29_ChannelVolume", settings.GetVolumeGain(transceiver), EAR_ROUTING_VARS);

		vector transform[4];
		Math3D.MatrixIdentity4(transform);
		AudioSystem.PlayEvent(BEEP_CONFIG, eventName, transform);
	}
}
