//! Manual frequency entry built on the vanilla configurable-dialog framework:
//! standard dialog frame, confirm/cancel buttons, gamepad support and input
//! blocking come from SCR_EditboxDialogUi; this class only supplies the radio
//! context, prefill, validation, and apply-on-confirm.
class EC29_FrequencyDialog : SCR_EditboxDialogUi
{
	protected static const ResourceName DIALOGS_CONFIG = "{684601EE00000301}Configs/Dialogs/EC29_Dialogs.conf";
	protected static const string PRESET_TAG = "ec29_frequency";

	protected BaseTransceiver m_Transceiver;
	protected SCR_VONEntryRadio m_RadioEntry;

	//------------------------------------------------------------------------------------------------
	//! Open the dialog for one transceiver; returns null if the preset fails to load.
	static EC29_FrequencyDialog OpenFor(notnull BaseTransceiver transceiver, SCR_VONEntryRadio radioEntry)
	{
		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][RadioFreq] Frequency dialog OPEN");

		EC29_FrequencyDialog dialog = new EC29_FrequencyDialog();
		dialog.m_Transceiver = transceiver;
		dialog.m_RadioEntry = radioEntry;

		if (!SCR_ConfigurableDialogUi.CreateFromPreset(DIALOGS_CONFIG, PRESET_TAG, dialog))
		{
			Print("[EC29-DBG][RadioFreq] Dialog preset failed to load - check EC29_Dialogs.conf", LogLevel.ERROR);
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

		SetMessage(string.Format("Range: %1 - %2 MHz",
			EC29_FormatFrequency(m_Transceiver.GetMinFrequency()),
			EC29_FormatFrequency(m_Transceiver.GetMaxFrequency())));

		if (m_Editbox)
		{
			m_Editbox.SetValue(EC29_FormatFrequency(m_Transceiver.GetFrequency()));
			m_Editbox.ActivateWriteMode();
		}
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnConfirm()
	{
		if (m_Editbox && m_Transceiver)
			EC29_ApplyFrequency(m_Editbox.GetValue());

		super.OnConfirm();
	}

	//------------------------------------------------------------------------------------------------
	//! Parse MHz input, clamp to the transceiver band, snap to channel resolution.
	protected void EC29_ApplyFrequency(string input)
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioFreq] Frequency dialog CONFIRM: raw='%1'", input);

		float inputMHz = input.ToFloat();
		if (inputMHz <= 0)
			return;

		int freqKHz = inputMHz * 1000;
		freqKHz = Math.ClampInt(freqKHz, m_Transceiver.GetMinFrequency(), m_Transceiver.GetMaxFrequency());

		int resolution = m_Transceiver.GetFrequencyResolution();
		if (resolution > 0)
			freqKHz = (freqKHz / resolution) * resolution;

		m_Transceiver.SetFrequency(freqKHz);
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][RadioFreq] Frequency set to %1 kHz", freqKHz);

		if (m_RadioEntry)
		{
			m_RadioEntry.SetEntryFrequency(freqKHz);
			m_RadioEntry.Update();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected string EC29_FormatFrequency(int freqKHz)
	{
		int wholeMHz = freqKHz / 1000;
		int decimalKHz = (freqKHz % 1000) / 100;

		return string.Format("%1.%2", wholeMHz, decimalKHz);
	}
}
