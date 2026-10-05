//! Radial-menu radio entry: the frequency label carries this radio's EC29 settings, and the
//! alternate channel shows in cyan.
//!
//! Label shape: "<frequency> <routing>|<beep>|<volume>", e.g. "45.0 MHz L|BH|100".
modded class SCR_VONEntryRadio
{
	//! True while this entry's frequency text carries the alternate cyan, so the colour can be
	//! handed back the moment the radio stops being the alternate.
	protected bool m_bEC29_ShowsAlternate;

	//------------------------------------------------------------------------------------------------
	//! Retune the entry's own view of its frequency and rebuild the base label the way vanilla
	//! formats it (0.01 MHz rounding, one decimal shown). The frequency dialog calls this after a
	//! retune so the label is right before the transceiver getter catches up.
	void SetEntryFrequency(int freqKHz)
	{
		m_iFrequency = freqKHz;

		float megahertz = Math.Round(freqKHz * 0.1) * 0.01;
		m_sText = megahertz.ToString(3, 1) + " " + LABEL_FREQUENCY_UNITS;
	}

	//------------------------------------------------------------------------------------------------
	//! The composite label goes into vanilla's 1.8 frequency-text overwrite BEFORE vanilla's
	//! update, so vanilla renders it natively and its own wheel tuning flows into it. Writing the
	//! text widget after vanilla instead would stomp anything vanilla routes through the overwrite.
	//! The alternate colour is the only post-vanilla touch.
	override void Update()
	{
		if (m_RadioTransceiver)
			m_sFrequencyTextOverwrite = EC29_ComposeLabel();

		super.Update();

		SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);
		if (!entryComp || !m_RadioTransceiver)
			return;

		if (EC29_RadioState.GetInstance().EarSettings().IsAlternate(m_RadioTransceiver))
		{
			entryComp.SetFrequencyColor(Color.FromInt(Color.CYAN));
			m_bEC29_ShowsAlternate = true;
			return;
		}

		// No longer the alternate: put back the colour vanilla paints for this entry's state.
		if (m_bEC29_ShowsAlternate)
		{
			m_bEC29_ShowsAlternate = false;
			entryComp.SetFrequencyColor(EC29_VanillaFrequencyColor());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected string EC29_ComposeLabel()
	{
		EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();

		string routing = settings.GetRoutingLetter(settings.GetRouting(m_RadioTransceiver));
		string beep = settings.GetBeepStyleShortCode(settings.GetBeepStyle(m_RadioTransceiver));
		int volume = settings.GetVolumePercent(m_RadioTransceiver);

		return string.Format("%1 %2|%3|%4", m_sText, routing, beep, volume);
	}

	//------------------------------------------------------------------------------------------------
	//! Mirrors vanilla's frequency colour rule: hovered = bright orange, active = orange,
	//! otherwise white.
	protected Color EC29_VanillaFrequencyColor()
	{
		if (m_bIsSelected)
			return Color.FromInt(GUIColors.ORANGE_BRIGHT.PackToInt());

		if (m_bIsActive)
			return Color.FromInt(GUIColors.ORANGE.PackToInt());

		return Color.FromInt(Color.WHITE);
	}
}
