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
        super.Update();

        SCR_VONEntryComponent entryComp = SCR_VONEntryComponent.Cast(m_EntryComponent);
        if (!entryComp)
            return;

        if (!m_RadioTransceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        EC29_EEarRouting routing = settings.GetRouting(m_RadioTransceiver);
        EC29_EBeepType beepType = settings.GetBeepType(m_RadioTransceiver);
        int volume = settings.GetVolumePercent(m_RadioTransceiver);
        bool isAlternate = settings.IsAlternate(m_RadioTransceiver);

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

        string displayText = m_sText + " " + routingText + "|" + beepText + "|" + volume.ToString();
        entryComp.SetFrequencyText(displayText);

        if (isAlternate)
            entryComp.SetFrequencyColor(Color.FromInt(Color.CYAN));
    }
}
