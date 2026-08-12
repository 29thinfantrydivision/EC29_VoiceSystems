//! Shared beep playback for radio transmissions, per-channel style aware.
//! TX beeps are the local key-up/release confirmation heard by the operator
//! (sidetone / talk-permit). RX beeps are the squelch open/close effects heard
//! when receiving someone else's transmission (squelch tail / roger beep).
//! RX is deliberately asymmetric: subtle click on open, prominent sound on close.
class EC29_RadioBeepHelper
{
    static const string BEEP_CONFIG = "{63926E92E2606681}Sounds/VON/EC29_beep.acp";
    static const string EAR_ROUTING_CONFIG = "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf";

    static const string EVENT_BEEP_HIGH = "EC29_BEEP_HIGH";
    static const string EVENT_BEEP_LOW = "EC29_BEEP_LOW";
    static const string EVENT_CLICK_OFF = "EC29_CLICK_OFF";
    static const string EVENT_GRS_START = "EC29_GRS_START";
    static const string EVENT_GRS_END = "EC29_GRS_END";
    //! Sound node must exist with this exact name in EC29_beep.acp
    static const string EVENT_SQUELCH_TAIL = "EC29_SQUELCH_TAIL";

    static void PlayTxStart(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioBeep] TX start beep requested");
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioEarSettings.GetInstance();
        EC29BeepType beepType = settings.GetBeepType(transceiver);

        string eventName;
        switch (beepType)
        {
            case EC29BeepType.ACE_HIGH: eventName = EVENT_BEEP_HIGH; break;
            case EC29BeepType.ACE_LOW: eventName = EVENT_BEEP_LOW; break;
            case EC29BeepType.GRS: eventName = EVENT_GRS_START; break;
            default: return;
        }

        PlayRouted(eventName, transceiver);
    }

    static void PlayTxEnd(BaseTransceiver transceiver)
    {
        Print("[EC29-DBG][RadioBeep] TX end beep requested");
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioEarSettings.GetInstance();
        EC29BeepType beepType = settings.GetBeepType(transceiver);

        string eventName;
        switch (beepType)
        {
            case EC29BeepType.ACE_HIGH:
            case EC29BeepType.ACE_LOW:
                eventName = EVENT_CLICK_OFF;
                break;
            case EC29BeepType.GRS:
                eventName = EVENT_GRS_END;
                break;
            default:
                return;
        }

        PlayRouted(eventName, transceiver);
    }

    static void PlayRxOpen(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioEarSettings.GetInstance();
        EC29BeepType beepType = settings.GetBeepType(transceiver);

        string eventName;
        switch (beepType)
        {
            case EC29BeepType.ACE_HIGH:
            case EC29BeepType.ACE_LOW:
                eventName = EVENT_SQUELCH_TAIL;
                break;
            case EC29BeepType.GRS:
                eventName = EVENT_GRS_START;
                break;
            default:
                return;
        }

        PlayRouted(eventName, transceiver);
    }

    static void PlayRxClose(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioEarSettings.GetInstance();
        EC29BeepType beepType = settings.GetBeepType(transceiver);

        string eventName;
        switch (beepType)
        {
            case EC29BeepType.ACE_HIGH: eventName = EVENT_BEEP_HIGH; break;
            case EC29BeepType.ACE_LOW: eventName = EVENT_BEEP_LOW; break;
            case EC29BeepType.GRS: eventName = EVENT_GRS_END; break;
            default: return;
        }

        PlayRouted(eventName, transceiver);
    }

    //! Master switch, persisted in game settings (Audio tab, 29th ID section).
    //! Default OFF - beeps are opt-in. The per-radio beep type (K in the radial
    //! menu) still selects the style once enabled.
    static bool EC29_AreBeepsEnabled()
    {
        BaseContainer radioSettings = GetGame().GetGameUserSettings().GetModule("EC29_RadioSettings");
        if (!radioSettings)
            return false;

        bool enabled;
        radioSettings.Get("RadioBeepsEnabled", enabled);
        return enabled;
    }

    protected static void PlayRouted(string eventName, BaseTransceiver transceiver)
    {
        if (!EC29_AreBeepsEnabled())
        {
            Print("[EC29-DBG][RadioBeep] Beep suppressed - RadioBeepsEnabled setting is off");
            return;
        }

        EC29_RadioEarSettings settings = EC29_RadioEarSettings.GetInstance();
        EC29EarRouting routing = settings.GetRouting(transceiver);

        AudioSystem.SetVariableByName("EC29_EarRouting", routing, EAR_ROUTING_CONFIG);

        // EC29_beep.acp only consumes EarRouting today; ChannelVolume is set so
        // beeps scale with per-channel volume once the variable is wired into the
        // audio project in Workbench.
        float volume = Math.Pow(settings.GetVolume(transceiver), 2.5);
        AudioSystem.SetVariableByName("EC29_ChannelVolume", volume, EAR_ROUTING_CONFIG);

        vector mat[4];
        Math3D.MatrixIdentity4(mat);

        AudioSystem.PlayEvent(BEEP_CONFIG, eventName, mat);
    }
}
