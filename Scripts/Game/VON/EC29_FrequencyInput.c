class EC29_FrequencyInput
{
    protected static const ResourceName LAYOUT_PATH = "{618CC2BF24440A1C}UI/layouts/EC29_FrequencyInput.layout";
    
    protected BaseTransceiver m_Transceiver;
    protected SCR_VONEntryRadio m_RadioEntry;
    
    protected Widget m_wRoot;
    protected EditBoxWidget m_wFrequencyEdit;
    protected TextWidget m_wRangeText;
    protected TextWidget m_wTitle;
    protected TextWidget m_wHintText;
    
    protected bool m_bIsOpen;
    
    //------------------------------------------------------------------------------------------------
    void Open(BaseTransceiver transceiver, SCR_VONEntryRadio radioEntry)
    {		
        if (m_bIsOpen)
            return;
        
        m_Transceiver = transceiver;
        m_RadioEntry = radioEntry;
        
        m_wRoot = GetGame().GetWorkspace().CreateWidgets(LAYOUT_PATH);
        if (!m_wRoot)
        {
            Print("[EC29] Failed to load frequency input layout!", LogLevel.ERROR);
            return;
        }
        
        m_wFrequencyEdit = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("FrequencyEdit"));
        m_wRangeText = TextWidget.Cast(m_wRoot.FindAnyWidget("RangeText"));
        m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("Title"));
        m_wHintText = TextWidget.Cast(m_wRoot.FindAnyWidget("HintText"));
        
        if (!m_wFrequencyEdit)
        {
            Print("[EC29] FrequencyEdit widget not found!", LogLevel.ERROR);
            Close(false);
            return;
        }
        
        if (m_wRangeText && m_Transceiver)
        {
            int minFreq = m_Transceiver.GetMinFrequency();
            int maxFreq = m_Transceiver.GetMaxFrequency();
            string rangeStr = string.Format("Range: %1 - %2", FormatFrequency(minFreq), FormatFrequency(maxFreq));
            m_wRangeText.SetText(rangeStr);
        }
        
        if (m_wFrequencyEdit && m_Transceiver)
        {
            int currentFreq = m_Transceiver.GetFrequency();
            m_wFrequencyEdit.SetText(FormatFrequency(currentFreq));
        }
        
        GetGame().GetWorkspace().SetFocusedWidget(m_wFrequencyEdit);
        m_wFrequencyEdit.ActivateWriteMode();
        
        m_bIsOpen = true;
    }
    
    //------------------------------------------------------------------------------------------------
    void Close(bool submit = true)
    {
        if (!m_bIsOpen && !m_wRoot)
            return;
        
        if (submit && m_wFrequencyEdit && m_Transceiver)
        {
            string input = m_wFrequencyEdit.GetText();
            Submit(input);
        }
        
        if (m_wRoot)
        {
            m_wRoot.RemoveFromHierarchy();
            m_wRoot = null;
        }
        
        m_wFrequencyEdit = null;
        m_wRangeText = null;
        m_wTitle = null;
        m_wHintText = null;
        m_Transceiver = null;
        m_RadioEntry = null;
        m_bIsOpen = false;
    }
    
    //------------------------------------------------------------------------------------------------
    protected void Submit(string input)
    {
        if (!m_Transceiver)
            return;
        
        float inputMHz = input.ToFloat();
        if (inputMHz <= 0)
            return;
        
        int freqKHz = (int)(inputMHz * 1000);
        
        int minFreq = m_Transceiver.GetMinFrequency();
        int maxFreq = m_Transceiver.GetMaxFrequency();
        int resolution = m_Transceiver.GetFrequencyResolution();
        
        freqKHz = Math.ClampInt(freqKHz, minFreq, maxFreq);
        
        if (resolution > 0)
            freqKHz = (freqKHz / resolution) * resolution;
        
        m_Transceiver.SetFrequency(freqKHz);
        
        if (m_RadioEntry)
        {
            m_RadioEntry.SetEntryFrequency(freqKHz);
            m_RadioEntry.Update();
        }
    }
    
    //------------------------------------------------------------------------------------------------
    protected string FormatFrequency(int freqKHz)
    {
        int wholeMHz = freqKHz / 1000;
        int decimalKHz = (freqKHz % 1000) / 100;
        
        return string.Format("%1.%2", wholeMHz, decimalKHz);
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsOpen()
    {
        return m_bIsOpen;
    }
    
    //------------------------------------------------------------------------------------------------
    bool IsInWriteMode()
    {
        if (!m_wFrequencyEdit)
            return false;
        
        return m_wFrequencyEdit.IsInWriteMode();
    }
}