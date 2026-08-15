modded class SCR_VONEntryRadio
{
    void SetEntryFrequency(int freqKHz)
    {
        m_iFrequency = freqKHz;

        float fFrequency = Math.Round(m_iFrequency * 0.1) * 0.01;
        m_sText = fFrequency.ToString(3, 1) + " " + LABEL_FREQUENCY_UNITS;
    }

    override void Update()
    {
        // 1.8 renders through m_sFrequencyTextOverwrite when set; feeding our
        // composite label into that field BEFORE super lets the vanilla Update
        // draw it natively instead of us stomping the widget afterwards (which
        // would also hide anything vanilla legitimately routes through the
        // overwrite mechanism).
        if (m_RadioTransceiver)
        {
            EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
            EC29_EEarRouting routing = settings.GetRouting(m_RadioTransceiver);
            EC29_EBeepType beepType = settings.GetBeepType(m_RadioTransceiver);
            int volume = settings.GetVolumePercent(m_RadioTransceiver);

            string routingText = settings.GetRoutingDisplayText(routing);

            string beepText;
            switch (beepType)
            {
                case EC29_EBeepType.OFF: beepText = "-"; break;
                case EC29_EBeepType.HIGH: beepText = "BH"; break;
                case EC29_EBeepType.LOW: beepText = "BL"; break;
                case EC29_EBeepType.CLASSIC: beepText = "CLS"; break;
                default: beepText = "BH";
            }

            m_sFrequencyTextOverwrite = m_sText + " " + routingText + "|" + beepText + "|" + volume.ToString();
        }

        super.Update();

        SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);
        if (!entryComp || !m_RadioTransceiver)
            return;

        EC29_RadioEarSettings earSettings = EC29_RadioState.GetInstance().EarSettings();
        if (earSettings.IsAlternate(m_RadioTransceiver))
            entryComp.SetFrequencyColor(Color.FromInt(Color.CYAN));
    }
}
