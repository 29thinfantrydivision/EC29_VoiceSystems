enum EC29EarRouting
{
    CENTER = 0,
    RIGHT = 1,
    LEFT = 2
}

enum EC29BeepType
{
    OFF = 0,
    ACE_HIGH = 1,
    ACE_LOW = 2,
    GRS = 3
}

class EC29_RadioEarSettings
{
    private static ref EC29_RadioEarSettings s_Instance;

    protected ref map<BaseTransceiver, EC29EarRouting> m_mRoutingByTransceiver = new map<BaseTransceiver, EC29EarRouting>();
    protected ref map<BaseTransceiver, EC29BeepType> m_mBeepTypeByTransceiver = new map<BaseTransceiver, EC29BeepType>();
    protected ref map<BaseTransceiver, float> m_mVolumeByTransceiver = new map<BaseTransceiver, float>();
    protected int m_iAlternateFrequency = -1;
    protected bool m_bTransmittingOnAlternate = false;

    //! World-lifecycle guard: the maps key raw BaseTransceiver pointers, which are
    //! only meaningful within one world; statics survive mission restart / server
    //! hop, so a stale instance would leak dead keys (and could mis-apply settings
    //! if the engine recycles component addresses). Weak member nulls with its
    //! world; mismatch rebuilds the singleton empty.
    protected BaseWorld m_OwnerWorld;

    static EC29_RadioEarSettings GetInstance()
    {
        BaseWorld currentWorld = GetGame().GetWorld();

        if (!s_Instance || s_Instance.m_OwnerWorld != currentWorld)
        {
            if (s_Instance)
                Print("[EC29-DBG][RadioEar] World changed - resetting ear/beep/volume settings singleton", LogLevel.NORMAL);

            s_Instance = new EC29_RadioEarSettings();
            s_Instance.m_OwnerWorld = currentWorld;
        }

        return s_Instance;
    }

    EC29EarRouting GetRouting(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return EC29EarRouting.CENTER;

        if (!m_mRoutingByTransceiver.Contains(transceiver))
            return EC29EarRouting.CENTER;

        return m_mRoutingByTransceiver.Get(transceiver);
    }

    void SetRouting(BaseTransceiver transceiver, EC29EarRouting routing)
    {
        if (!transceiver)
            return;

        m_mRoutingByTransceiver.Set(transceiver, routing);
    }

    EC29EarRouting CycleRouting(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioEar] CycleRouting pressed");
        if (!transceiver)
            return EC29EarRouting.CENTER;

        EC29EarRouting current = GetRouting(transceiver);
        EC29EarRouting next;

        switch (current)
        {
            case EC29EarRouting.CENTER:
                next = EC29EarRouting.LEFT;
                break;
            case EC29EarRouting.LEFT:
                next = EC29EarRouting.RIGHT;
                break;
            case EC29EarRouting.RIGHT:
                next = EC29EarRouting.CENTER;
                break;
            default:
                next = EC29EarRouting.CENTER;
        }

        SetRouting(transceiver, next);
        return next;
    }

    string GetRoutingDisplayText(EC29EarRouting routing)
    {
        switch (routing)
        {
            case EC29EarRouting.LEFT:
                return "L";
            case EC29EarRouting.RIGHT:
                return "R";
            default:
                return "C";
        }

        return "C";
    }

    EC29BeepType GetBeepType(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return EC29BeepType.ACE_HIGH;

        if (!m_mBeepTypeByTransceiver.Contains(transceiver))
            return EC29BeepType.ACE_HIGH;

        return m_mBeepTypeByTransceiver.Get(transceiver);
    }

    void SetBeepType(BaseTransceiver transceiver, EC29BeepType beepType)
    {
        if (!transceiver)
            return;

        m_mBeepTypeByTransceiver.Set(transceiver, beepType);
    }

    EC29BeepType CycleBeepType(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioEar] CycleBeepType pressed");
        if (!transceiver)
            return EC29BeepType.ACE_HIGH;

        EC29BeepType current = GetBeepType(transceiver);
        EC29BeepType next;

        switch (current)
        {
            case EC29BeepType.OFF:
                next = EC29BeepType.ACE_HIGH;
                break;
            case EC29BeepType.ACE_HIGH:
                next = EC29BeepType.ACE_LOW;
                break;
            case EC29BeepType.ACE_LOW:
                next = EC29BeepType.GRS;
                break;
            case EC29BeepType.GRS:
                next = EC29BeepType.OFF;
                break;
            default:
                next = EC29BeepType.ACE_LOW;
        }

        SetBeepType(transceiver, next);
        return next;
    }

    string GetBeepTypeDisplayText(EC29BeepType beepType)
    {
        switch (beepType)
        {
            case EC29BeepType.OFF:
                return "OFF";
            case EC29BeepType.ACE_HIGH:
                return "ACE-H";
            case EC29BeepType.ACE_LOW:
                return "ACE-L";
            case EC29BeepType.GRS:
                return "GRS";
            default:
                return "ACE-L";
        }

        return "ACE-H";
    }

    float GetVolume(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return 1.0;

        if (!m_mVolumeByTransceiver.Contains(transceiver))
            return 1.0;

        return m_mVolumeByTransceiver.Get(transceiver);
    }

    void SetVolume(BaseTransceiver transceiver, float volume)
    {
        if (!transceiver)
            return;

        m_mVolumeByTransceiver.Set(transceiver, Math.Clamp(volume, 0.0, 1.0));
    }

    float AdjustVolume(BaseTransceiver transceiver, float delta)
    {
        PrintFormat("[EC29-DBG][RadioEar] AdjustVolume delta=%1", delta);
        float current = GetVolume(transceiver);
        float newVolume = Math.Clamp(current + delta, 0.0, 1.0);
        SetVolume(transceiver, newVolume);
        return newVolume;
    }

    int GetVolumePercent(BaseTransceiver transceiver)
    {
        return Math.Round(GetVolume(transceiver) * 100);
    }

    string GetVolumeDisplayText(BaseTransceiver transceiver)
    {
        int percent = GetVolumePercent(transceiver);
        return percent.ToString() + "%";
    }

    int GetAlternateFrequency()
    {
        return m_iAlternateFrequency;
    }

    bool IsAlternate(BaseTransceiver transceiver)
    {
        if (!transceiver || m_iAlternateFrequency < 0)
            return false;

        return transceiver.GetFrequency() == m_iAlternateFrequency;
    }

    void SetAlternateFrequency(int frequency)
    {
        m_iAlternateFrequency = frequency;
    }

    void ClearAlternateFrequency()
    {
        m_iAlternateFrequency = -1;
    }

    bool ToggleAlternate(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioEar] ToggleAlternate pressed");
        if (!transceiver)
            return false;

        if (IsAlternate(transceiver))
        {
            ClearAlternateFrequency();
            return false;
        }
        else
        {
            SetAlternateFrequency(transceiver.GetFrequency());
            return true;
        }
    }

    bool IsTransmittingOnAlternate()
    {
        return m_bTransmittingOnAlternate;
    }

    void SetTransmittingOnAlternate(bool transmitting)
    {
        m_bTransmittingOnAlternate = transmitting;
    }
}
