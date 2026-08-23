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

    protected ref map<BaseTransceiver, EC29_EEarRouting> m_mRoutingByTransceiver = new map<BaseTransceiver, EC29_EEarRouting>();
    protected ref map<BaseTransceiver, EC29_EBeepType> m_mBeepTypeByTransceiver = new map<BaseTransceiver, EC29_EBeepType>();
    protected ref map<BaseTransceiver, float> m_mVolumeByTransceiver = new map<BaseTransceiver, float>();
    protected int m_iAlternateFrequency = -1;
    protected bool m_bTransmittingOnAlternate = false;


    //! Issue #7: squad net defaults to the left ear, platoon net to the right.
    //! A radio the player has not routed manually resolves to a default by
    //! device class on first query; the K-menu cycle still overrides per radio.
    EC29_EEarRouting GetRouting(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return EC29_EEarRouting.CENTER;

        EC29_EEarRouting routing;
        if (m_mRoutingByTransceiver.Find(transceiver, routing))
            return routing;

        return ApplyDefaultRouting(transceiver);
    }

    //! Stores a transceiver's default routing. Personal radios with multiple
    //! channels route by channel - CH1 (squad net) -> LEFT, every later
    //! channel (platoon net) -> RIGHT - because 1.8 dual-channel radios carry
    //! both nets on one device, where a device-class rule would put every
    //! channel in the same ear. Single-channel personal radios keep the
    //! device rule: handheld (EGadgetType.RADIO) -> LEFT, manpack
    //! (EGadgetType.RADIO_BACKPACK) -> RIGHT. Anything without a radio gadget
    //! (vehicle sets, editor transceivers) -> CENTER.
    //! The result is memoized in the routing map so the per-packet hot path
    //! stays a single map lookup. A transceiver whose owning entity cannot be
    //! resolved yet returns CENTER WITHOUT memoizing, so classification
    //! retries on the next query instead of freezing a wrong default.
    protected EC29_EEarRouting ApplyDefaultRouting(BaseTransceiver transceiver)
    {
        BaseRadioComponent radio = transceiver.GetRadio();
        if (!radio)
            return EC29_EEarRouting.CENTER;

        IEntity radioEntity = radio.GetOwner();
        if (!radioEntity)
            return EC29_EEarRouting.CENTER;

        SCR_RadioComponent gadget = SCR_RadioComponent.Cast(radioEntity.FindComponent(SCR_RadioComponent));
        EC29_EEarRouting routing = EC29_EEarRouting.CENTER;
        if (gadget)
        {
            if (radio.TransceiversCount() >= 2)
            {
                routing = EC29_EEarRouting.RIGHT;
                if (radio.GetTransceiver(0) == transceiver)
                    routing = EC29_EEarRouting.LEFT;
            }
            else
            {
                EGadgetType gadgetType = gadget.GetType();
                if (gadgetType == EGadgetType.RADIO)
                    routing = EC29_EEarRouting.LEFT;
                else if (gadgetType == EGadgetType.RADIO_BACKPACK)
                    routing = EC29_EEarRouting.RIGHT;
            }
        }

        m_mRoutingByTransceiver.Set(transceiver, routing);

        if (EC29_Debug.VERBOSE)
        {
            bool hasGadget = false;
            if (gadget)
                hasGadget = true;
            PrintFormat("[EC29-DBG][RadioEar] Default routing %1 applied to transceiver at %2 kHz (gadget=%3)",
                GetRoutingDisplayText(routing), transceiver.GetFrequency(), hasGadget);
        }

        return routing;
    }

    void SetRouting(BaseTransceiver transceiver, EC29_EEarRouting routing)
    {
        if (!transceiver)
            return;

        m_mRoutingByTransceiver.Set(transceiver, routing);
    }

    EC29_EEarRouting CycleRouting(BaseTransceiver transceiver)
    {
        if (EC29_Debug.VERBOSE)
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
        if (EC29_Debug.VERBOSE)
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
        if (EC29_Debug.VERBOSE)
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
        if (EC29_Debug.VERBOSE)
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
