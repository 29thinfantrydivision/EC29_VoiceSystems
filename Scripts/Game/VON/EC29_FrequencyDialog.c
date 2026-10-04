//! Manual frequency entry for one radio (F in the VON radial menu).
//!
//! Built on vanilla's configurable edit-box dialog, which supplies the frame, confirm/cancel,
//! gamepad support and input blocking. The preset lives in Configs/Dialogs/EC29_Dialogs.conf;
//! its content layout holds one edit box named "EditBox", which is the name the vanilla base
//! looks it up by.
//!
//! Input is decimal MHz. Confirm clamps it into the radio's band and snaps it down to the
//! radio's channel resolution - out-of-band input is corrected, not refused. Input that does
//! not parse to a positive number changes nothing and sounds the deny tone. Cancel changes
//! nothing.
class EC29_FrequencyDialog : SCR_EditboxDialogUi
{
	protected static const ResourceName PRESETS = "{684601EE00000301}Configs/Dialogs/EC29_Dialogs.conf";
	protected static const string PRESET_TAG = "ec29_frequency";

	protected BaseTransceiver m_Transceiver;
	//! The radial entry the dialog was opened from, if any; its label is refreshed on apply.
	protected SCR_VONEntryRadio m_SourceEntry;

	//------------------------------------------------------------------------------------------------
	//! Opens the dialog for a transceiver. The controller has already refused special nets.
	static EC29_FrequencyDialog OpenForTransceiver(BaseTransceiver transceiver, SCR_VONEntryRadio sourceEntry = null)
	{
		if (!transceiver)
			return null;

		// Vanilla's CreateFromPreset dereferences the loaded resource unchecked; check it first so
		// a broken preset path is a log line, not a script crash.
		Resource presets = BaseContainerTools.LoadContainer(PRESETS);
		if (!presets || !presets.IsValid())
		{
			Print("[EC29-DBG][RadioFreq] Frequency dialog preset failed to load - check EC29_Dialogs.conf", LogLevel.ERROR);
			return null;
		}

		// The target must be set before creation: vanilla fires OnMenuOpen inside CreateFromPreset.
		EC29_FrequencyDialog dialog = new EC29_FrequencyDialog();
		dialog.m_Transceiver = transceiver;
		dialog.m_SourceEntry = sourceEntry;

		if (!SCR_ConfigurableDialogUi.CreateFromPreset(PRESETS, PRESET_TAG, dialog))
		{
			Print("[EC29-DBG][RadioFreq] Frequency dialog preset failed to load - check EC29_Dialogs.conf", LogLevel.ERROR);
			return null;
		}

		return dialog;
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnMenuOpen(SCR_ConfigurableDialogUiPreset preset)
	{
		super.OnMenuOpen(preset);

		if (!m_Transceiver)
			return;

		SetMessage(string.Format("Range: %1 - %2 MHz", FormatMegahertz(m_Transceiver.GetMinFrequency()), FormatMegahertz(m_Transceiver.GetMaxFrequency())));

		if (m_Editbox)
		{
			m_Editbox.SetValue(FormatMegahertz(m_Transceiver.GetFrequency()));
			// Straight into typing - no extra click or confirm press to start editing.
			m_Editbox.ActivateWriteMode();
		}

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioFreq] Dialog open for %1 kHz (band %2-%3)", m_Transceiver.GetFrequency(), m_Transceiver.GetMinFrequency(), m_Transceiver.GetMaxFrequency());
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnConfirm()
	{
		ApplyEnteredFrequency();
		super.OnConfirm();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyEnteredFrequency()
	{
		if (!m_Transceiver || !m_Editbox)
			return;

		string entered = m_Editbox.GetValue();
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioFreq] Confirm with input '%1'", entered);

		float megahertz = entered.ToFloat();
		if (megahertz <= 0)
		{
			PlayDenyTone();
			return;
		}

		// Truncate fractional kHz. The small bias keeps float error from turning a typed 45.1
		// into 45099 kHz.
		int kilohertz = Math.Floor(megahertz * 1000 + 0.001);
		kilohertz = FitToBand(kilohertz);

		m_Transceiver.SetFrequency(kilohertz);

		if (m_SourceEntry)
		{
			m_SourceEntry.SetEntryFrequency(kilohertz);
			m_SourceEntry.Update();
		}

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioFreq] Frequency set to %1 kHz", kilohertz);
	}

	//------------------------------------------------------------------------------------------------
	//! Clamp into [min, max], then snap down to a whole multiple of the channel resolution
	//! (counted from 0 kHz). A snap that lands under the band floor steps up one channel, and the
	//! result is clamped again so it can never leave the band.
	protected int FitToBand(int kilohertz)
	{
		int minimum = m_Transceiver.GetMinFrequency();
		int maximum = m_Transceiver.GetMaxFrequency();
		kilohertz = Math.ClampInt(kilohertz, minimum, maximum);

		int resolution = m_Transceiver.GetFrequencyResolution();
		if (resolution <= 0)
			return kilohertz;

		int snapped = (kilohertz / resolution) * resolution;
		if (snapped < minimum)
			snapped = snapped + resolution;

		return Math.ClampInt(snapped, minimum, maximum);
	}

	//------------------------------------------------------------------------------------------------
	//! Whole MHz, a dot, then the hundreds-of-kHz digit, truncated: 45125 -> "45.1", 30000 -> "30.0".
	protected static string FormatMegahertz(int kilohertz)
	{
		int whole = kilohertz / 1000;
		int tenths = (kilohertz % 1000) / 100;
		return string.Format("%1.%2", whole, tenths);
	}

	//------------------------------------------------------------------------------------------------
	//! Same deny tone as a refused key-up, played through the local VON controller.
	protected void PlayDenyTone()
	{
		PlayerController playerController = GetGame().GetPlayerController();
		if (!playerController)
			return;

		SCR_VONController controller = SCR_VONController.Cast(playerController.FindComponent(SCR_VONController));
		if (controller)
			controller.EC29_PlayErrorBeep();
	}
}
