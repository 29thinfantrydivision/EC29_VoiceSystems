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
    private static ref EC29_RadioEarSettings s_Instance;

    protected ref map<BaseTransceiver, EC29_EEarRouting> m_mRoutingByTransceiver = new map<BaseTransceiver, EC29_EEarRouting>();
    protected ref map<BaseTransceiver, EC29_EBeepType> m_mBeepTypeByTransceiver = new map<BaseTransceiver, EC29_EBeepType>();
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

    EC29_EEarRouting GetRouting(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return EC29_EEarRouting.CENTER;

        if (!m_mRoutingByTransceiver.Contains(transceiver))
            return EC29_EEarRouting.CENTER;

        return m_mRoutingByTransceiver.Get(transceiver);
    }

    void SetRouting(BaseTransceiver transceiver, EC29_EEarRouting routing)
    {
        if (!transceiver)
            return;

        m_mRoutingByTransceiver.Set(transceiver, routing);
    }

    EC29_EEarRouting CycleRouting(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioEar] CycleRouting pressed");
        if (!transceiver)
            return EC29_EEarRouting.CENTER;

        EC29_EEarRouting current = GetRouting(transceiver);
        EC29_EEarRouting next;

        switch (current)
        {
            case EC29_EEarRouting.CENTER:
                next = EC29_EEarRouting.LEFT;
                break;
            case EC29_EEarRouting.LEFT:
                next = EC29_EEarRouting.RIGHT;
                break;
            case EC29_EEarRouting.RIGHT:
                next = EC29_EEarRouting.CENTER;
                break;
            default:
                next = EC29_EEarRouting.CENTER;
        }

        SetRouting(transceiver, next);
        return next;
    }

    string GetRoutingDisplayText(EC29_EEarRouting routing)
    {
        switch (routing)
        {
            case EC29_EEarRouting.LEFT:
                return "L";
            case EC29_EEarRouting.RIGHT:
                return "R";
            default:
                return "C";
        }

        return "C";
    }

    EC29_EBeepType GetBeepType(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return EC29_EBeepType.HIGH;

        if (!m_mBeepTypeByTransceiver.Contains(transceiver))
            return EC29_EBeepType.HIGH;

        return m_mBeepTypeByTransceiver.Get(transceiver);
    }

    void SetBeepType(BaseTransceiver transceiver, EC29_EBeepType beepType)
    {
        if (!transceiver)
            return;

        m_mBeepTypeByTransceiver.Set(transceiver, beepType);
    }

    EC29_EBeepType CycleBeepType(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioEar] CycleBeepType pressed");
        if (!transceiver)
            return EC29_EBeepType.HIGH;

        EC29_EBeepType current = GetBeepType(transceiver);
        EC29_EBeepType next;

        switch (current)
        {
            case EC29_EBeepType.OFF:
                next = EC29_EBeepType.HIGH;
                break;
            case EC29_EBeepType.HIGH:
                next = EC29_EBeepType.LOW;
                break;
            case EC29_EBeepType.LOW:
                next = EC29_EBeepType.CLASSIC;
                break;
            case EC29_EBeepType.CLASSIC:
                next = EC29_EBeepType.OFF;
                break;
            default:
                next = EC29_EBeepType.LOW;
        }

        SetBeepType(transceiver, next);
        return next;
    }

    string GetBeepTypeDisplayText(EC29_EBeepType beepType)
    {
        switch (beepType)
        {
            case EC29_EBeepType.OFF:
                return "OFF";
            case EC29_EBeepType.HIGH:
                return "HI";
            case EC29_EBeepType.LOW:
                return "LO";
            case EC29_EBeepType.CLASSIC:
                return "CLS";
            default:
                return "LO";
        }

        return "HI";
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
