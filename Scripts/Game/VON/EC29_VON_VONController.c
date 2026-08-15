modded class SCR_VONController
{
    const string EC29_SOUND_CYCLE = "{19696BC8C5ECE170}Sounds/VON/EC29_FX/RadioCycle.wav";
    const string EC29_SOUND_LOCAL_OFF = "{AFA775D58D24308A}Sounds/VON/EC29_FX/RadioLocalOff.wav";
    const string EC29_SOUND_LOCAL_ON = "{E21F58D501028C63}Sounds/VON/EC29_FX/RadioLocalOn.wav";
    const string EC29_SOUND_ERROR = "{7065D8DD8ADFA3DE}Sounds/EC29_Sound/errorbeep.wav";

    //! Key-up rate limit: a token bucket sized for normal PTT traffic; an empty
    //! bucket refuses transmission and answers with the deny tone - like a
    //! trunked system rejecting the channel until it drains.
    protected static const float EC29_KEY_BUCKET_CAPACITY = 4;
    protected static const float EC29_KEY_BUCKET_WINDOW_MS = 4000;

    protected AudioHandle m_AudioHandleCycle;
    protected AudioHandle m_AudioHandleLocalOn;
    protected AudioHandle m_AudioHandleLocalOff;
    protected bool m_bAlternatePTTActive = false;
    protected SCR_VONEntry m_SavedPrimaryEntry;
    protected int m_iEC29_KeyedFrequency = -1;
    protected ref EC29_TokenBucket m_EC29_KeyBucket = new EC29_TokenBucket(EC29_KEY_BUCKET_CAPACITY, EC29_KEY_BUCKET_WINDOW_MS);
    protected AudioHandle m_AudioHandleError;
    protected bool m_bEC29_RadioCheckPlayed = false;

    protected const string EC29_ACTION_VOICE_RANGE_CYCLE = "EC29_VONVoiceRangeCycle";

    //------------------------------------------------------------------------------------------------
    override protected void Init(IEntity owner)
    {
        super.Init(owner);

        if (m_InputManager)
        {
            m_InputManager.AddActionListener(EC29_ACTION_VOICE_RANGE_CYCLE, EActionTrigger.DOWN, EC29_ActionVoiceRangeCycle);
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][VONCtrl] Listener registered for 'EC29_VONVoiceRangeCycle' (F3). Radio actions are frame-polled (original bind behavior).", LogLevel.NORMAL);
        }
        else
        {
            Print("[EC29-DBG][VONCtrl] m_InputManager NULL at Init - voice/radio keys will not work", LogLevel.WARNING);
        }
    }

    //------------------------------------------------------------------------------------------------
    override protected void Cleanup()
    {
        if (m_InputManager)
            m_InputManager.RemoveActionListener(EC29_ACTION_VOICE_RANGE_CYCLE, EActionTrigger.DOWN, EC29_ActionVoiceRangeCycle);

        super.Cleanup();
    }

    //------------------------------------------------------------------------------------------------
    protected void EC29_ActionVoiceRangeCycle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_Debug.VERBOSE)
            Print("[EC29-DBG][VONCtrl] F3 keybind FIRED (EC29_VONVoiceRangeCycle action works)", LogLevel.NORMAL);

        // Coexistence: a known conflicting mod also cycles voice range on F3; firing both would double-cycle.
        if (EC29_CoexistenceGuard.ShouldYieldVoiceRange())
            return;
        if (!m_VONComp)
        {
            PrintFormat("[EC29_VON] Cycle pressed but no SCR_VoNComponent on controlled entity", level: LogLevel.WARNING);
            return;
        }

        EC29_EVoiceRange current = m_VONComp.EC29_GetVoiceRange();
        EC29_EVoiceRange next;
        switch (current)
        {
            case EC29_EVoiceRange.WHISPER: next = EC29_EVoiceRange.NORMAL;  break;
            case EC29_EVoiceRange.NORMAL:  next = EC29_EVoiceRange.YELL;    break;
            case EC29_EVoiceRange.YELL:    next = EC29_EVoiceRange.WHISPER; break;
            default:                      next = EC29_EVoiceRange.NORMAL;  break;
        }

        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][VONCtrl] Requesting voice range change: %1 -> %2", typename.EnumToString(EC29_EVoiceRange, current), typename.EnumToString(EC29_EVoiceRange, next));
        m_VONComp.EC29_RequestSetVoiceRange(next);

        // Refresh the VoN overlay label immediately for the local outgoing transmission.
        // The RplProp callback handles remote receivers, but the authority does not always
        // fire its own onRplName for its own writes - this guarantees local UI snaps to the
        // new mode the same frame the input is pressed.
        SCR_VonDisplay display = m_VONComp.GetDisplay();
        if (display)
            display.EC29_ForceRefreshAllTransmissions();
    }

    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);
        AudioSystem.PlayEventInitialize(EC29_RadioBeepHelper.BEEP_CONFIG);
        EC29_RFPropagationSettings.GetInstance();
    }

    protected void PlayBeepStart(BaseTransceiver transceiver)
    {
        EC29_RadioBeepHelper.PlayTxStart(transceiver);
    }

    protected void PlayBeepEnd(BaseTransceiver transceiver)
    {
        EC29_RadioBeepHelper.PlayTxEnd(transceiver);
    }

    override void SetActiveTransmit(notnull SCR_VONEntry entry)
    {
        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
        if (radioEntry && !EC29_CoexistenceGuard.ShouldYieldRadio())
        {
            // Denied key-ups never reach super, so no transmission starts, no
            // TX beep plays and no key RPC is sent - just the deny tone.
            if (EC29_IsKeySpamLocked())
            {
                EC29_PlayErrorBeep();
                return;
            }

            BaseTransceiver transceiver = radioEntry.GetTransceiver();
            if (transceiver)
            {
                PlayBeepStart(transceiver);
                EC29_NotifyKeyStart(transceiver);
            }
        }

        super.SetActiveTransmit(entry);
    }

    //! Consumes one key-up token; an empty bucket means the lockout is engaged.
    protected bool EC29_IsKeySpamLocked()
    {
        float nowMs = GetGame().GetWorld().GetWorldTime();

        if (m_EC29_KeyBucket.TryConsume(nowMs))
            return false;

        if (EC29_Debug.VERBOSE)
            Print("[EC29-DBG][RadioKey] Key-up denied - rate bucket empty", LogLevel.NORMAL);
        return true;
    }

    protected void EC29_PlayErrorBeep()
    {
        if (m_AudioHandleError != 0 && AudioSystem.IsSoundPlayed(m_AudioHandleError))
            AudioSystem.TerminateSound(m_AudioHandleError);

        m_AudioHandleError = AudioSystem.PlaySound(EC29_SOUND_ERROR);
    }

    //! One-time radio check on first spawn: confirms the voice systems are up
    //! with a roger beep + chat line. The VON controller instance lives exactly one
    //! server session on the client, so the flag resets naturally on reconnect
    //! and never replays on respawn.
    protected void EC29_TryPlayRadioCheck()
    {
        PlayerController playerController = GetGame().GetPlayerController();
        if (!playerController)
            return;

        IEntity controlledEntity = playerController.GetControlledEntity();
        if (!controlledEntity)
            return;

        if (!SCR_ChimeraCharacter.Cast(controlledEntity))
            return;

        m_bEC29_RadioCheckPlayed = true;

        SCR_ChatComponent chatComponent = SCR_ChatComponent.Cast(playerController.FindComponent(SCR_ChatComponent));

        string conflictNotice = EC29_CoexistenceGuard.GetConflictNotice();
        if (!conflictNotice.IsEmpty())
        {
            if (chatComponent)
                chatComponent.ShowMessage(conflictNotice);
            return;
        }

        // Spawn-in confirmation is chat-only; the audible roger beep on every
        // spawn was noise (removed by request).
        if (chatComponent)
            chatComponent.ShowMessage("***EC29 VOICE SYSTEMS INITIALIZED***");
    }

    override void DeactivateVON(EVONTransmitType transmitType = EVONTransmitType.NONE)
    {
        if (m_bIsActive && transmitType != EVONTransmitType.DIRECT && !EC29_CoexistenceGuard.ShouldYieldRadio())
        {
            SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(m_ActiveEntry);
            if (radioEntry)
            {
                BaseTransceiver transceiver = radioEntry.GetTransceiver();
                if (transceiver)
                    PlayBeepEnd(transceiver);
            }

            EC29_NotifyKeyStop();
        }

        super.DeactivateVON(transmitType);
    }

    //! Tell the server this client keyed a radio so receivers can squelch even
    //! when no voice packets flow (dead key). Tracks the keyed frequency
    //! locally so start/stop RPCs always pair up, including active-entry swaps
    //! mid-key (alternate channel PTT).
    protected void EC29_NotifyKeyStart(BaseTransceiver transceiver)
    {
        int frequency = transceiver.GetFrequency();
        if (m_iEC29_KeyedFrequency == frequency)
            return;

        if (m_iEC29_KeyedFrequency >= 0)
            Rpc(RpcAsk_EC29_KeyState, m_iEC29_KeyedFrequency, 0.0, false);

        m_iEC29_KeyedFrequency = frequency;
        Rpc(RpcAsk_EC29_KeyState, frequency, transceiver.GetRange(), true);
    }

    protected void EC29_NotifyKeyStop()
    {
        if (m_iEC29_KeyedFrequency < 0)
            return;

        Rpc(RpcAsk_EC29_KeyState, m_iEC29_KeyedFrequency, 0.0, false);
        m_iEC29_KeyedFrequency = -1;
    }

    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_EC29_KeyState(int frequency, float range, bool keyed)
    {
        PlayerController playerController = PlayerController.Cast(GetOwner());
        if (!playerController)
            return;

        EC29_RFPropagationNetworkComponent net = EC29_RFPropagationNetworkComponent.GetInstance();
        if (net)
            net.EC29_RelayKeyState(playerController.GetPlayerId(), frequency, range, keyed);
    }

    SCR_VONEntryRadio EC29_FindRadioEntryByFrequency(int frequency)
    {
        return FindEntryByFrequency(frequency);
    }

    override protected void ActionVONProximityToggle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        // Chain hygiene: still forward to super when we have nothing to do, so a
        // third mod's override further down the modded chain keeps running.
        if (!m_VONComp)
        {
            super.ActionVONProximityToggle(value, reason);
            return;
        }

        bool wasToggled = m_bIsToggledDirect;

        super.ActionVONProximityToggle(value, reason);

        if (EC29_CoexistenceGuard.ShouldYieldRadio())
            return;

        if (m_bIsToggledDirect && !wasToggled)
        {
            if (m_AudioHandleLocalOn != 0 && AudioSystem.IsSoundPlayed(m_AudioHandleLocalOn))
                AudioSystem.TerminateSound(m_AudioHandleLocalOn);

            m_AudioHandleLocalOn = AudioSystem.PlaySound(EC29_SOUND_LOCAL_ON);
        }
        else if (!m_bIsToggledDirect && wasToggled)
        {
            if (m_AudioHandleLocalOff != 0 && AudioSystem.IsSoundPlayed(m_AudioHandleLocalOff))
                AudioSystem.TerminateSound(m_AudioHandleLocalOff);

            m_AudioHandleLocalOff = AudioSystem.PlaySound(EC29_SOUND_LOCAL_OFF);
        }
    }

    override protected void ActionVONTransceiverCycle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (reason == EActionTrigger.DOWN && !EC29_CoexistenceGuard.ShouldYieldRadio())
        {
            if (m_AudioHandleCycle != 0 && AudioSystem.IsSoundPlayed(m_AudioHandleCycle))
                AudioSystem.TerminateSound(m_AudioHandleCycle);

            m_AudioHandleCycle = AudioSystem.PlaySound(EC29_SOUND_CYCLE);
        }

        super.ActionVONTransceiverCycle(value, reason);
    }

    override void Update(float timeSlice)
    {
        super.Update(timeSlice);

        if (!m_bEC29_RadioCheckPlayed)
            EC29_TryPlayRadioCheck();

        // Coexistence: a known conflicting VON mod polls the same default keys; ours yields.
        if (EC29_CoexistenceGuard.ShouldYieldRadio())
            return;

        // Original bind behavior: radio actions are frame-polled exactly as the
        // absorbed implementation did - PTT by action value, menu actions only
        // while the radial menu is open. Polling keeps these keys inert in every
        // other input situation, which is what keeps them conflict-free against
        // vanilla uses of the same physical keys.
        // Uses the member vanilla Init already resolved instead of re-fetching
        // the manager from the game every frame.
        InputManager inputMgr = m_InputManager;
        if (!inputMgr)
            return;

        float altValue = inputMgr.GetActionValue("EC29_AlternateChannel");
        if (altValue > 0 && !m_bAlternatePTTActive)
            OnAlternatePTTStart();
        else if (altValue <= 0 && m_bAlternatePTTActive)
            OnAlternatePTTEnd();

        if (m_VONMenu && m_VONMenu.GetRadialMenu() && m_VONMenu.GetRadialMenu().IsOpened())
        {
            if (inputMgr.GetActionTriggered("EC29_VONRoutingAction"))
                OnEarRoutingToggle();

            if (inputMgr.GetActionTriggered("EC29_SetFrequencyAction"))
                OnSetFrequencyPressed();

            if (inputMgr.GetActionTriggered("EC29_VONBeepTypeAction"))
                OnBeepTypeToggle();

            if (inputMgr.GetActionTriggered("EC29_VolumeUp"))
                OnVolumeAdjust(1);

            if (inputMgr.GetActionTriggered("EC29_VolumeDown"))
                OnVolumeAdjust(-1);

            if (inputMgr.GetActionTriggered("EC29_AlternateChannelAction"))
                OnAlternateChannelToggle();
        }
    }

    protected void OnEarRoutingToggle()
    {
        SCR_RadialMenu radialMenu = m_VONMenu.GetRadialMenu();
        if (!radialMenu)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radialMenu.GetSelectionEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        settings.CycleRouting(transceiver);

        radialMenu.UpdateEntries();
    }

    protected void OnBeepTypeToggle()
    {
        SCR_RadialMenu radialMenu = m_VONMenu.GetRadialMenu();
        if (!radialMenu)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radialMenu.GetSelectionEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        settings.CycleBeepType(transceiver);

        radialMenu.UpdateEntries();
    }

    protected void OnSetFrequencyPressed()
    {
        SCR_RadialMenu radialMenu = m_VONMenu.GetRadialMenu();
        if (!radialMenu)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radialMenu.GetSelectionEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_FrequencyDialog.OpenFor(transceiver, radioEntry);
    }

    protected void OnVolumeAdjust(float value)
    {
        SCR_RadialMenu radialMenu = m_VONMenu.GetRadialMenu();
        if (!radialMenu)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radialMenu.GetSelectionEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();

        float delta;
        if (value > 0)
            delta = 0.1;
        else
            delta = -0.1;

        settings.AdjustVolume(transceiver, delta);
        radialMenu.UpdateEntries();
    }

    protected void OnAlternateChannelToggle()
    {
        SCR_RadialMenu radialMenu = m_VONMenu.GetRadialMenu();
        if (!radialMenu)
            return;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radialMenu.GetSelectionEntry());
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        settings.ToggleAlternate(transceiver);
        radialMenu.UpdateEntries();
    }

    protected void OnAlternatePTTStart()
    {
        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        int altFrequency = settings.GetAlternateFrequency();

        if (altFrequency < 0)
            return;

        SCR_VONEntryRadio altEntry = FindEntryByFrequency(altFrequency);
        if (!altEntry)
            return;

        m_bAlternatePTTActive = true;
        settings.SetTransmittingOnAlternate(true);
        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][RadioAlt] Alternate PTT START on freq %1 (primary entry saved)", altFrequency);

        m_SavedPrimaryEntry = m_ActiveEntry;
        m_ActiveEntry = altEntry;
        ActivateVON(EVONTransmitType.CHANNEL);
    }

    protected void OnAlternatePTTEnd()
    {
        if (!m_bAlternatePTTActive)
            return;

        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        settings.SetTransmittingOnAlternate(false);
        m_bAlternatePTTActive = false;
        if (EC29_Debug.VERBOSE)
            Print("[EC29-DBG][RadioAlt] Alternate PTT END (primary entry restored)");

        DeactivateVON(EVONTransmitType.CHANNEL);

        if (m_SavedPrimaryEntry)
        {
            m_ActiveEntry = m_SavedPrimaryEntry;
            m_SavedPrimaryEntry = null;
        }
    }

    protected SCR_VONEntryRadio FindEntryByFrequency(int frequency)
    {
        if (frequency < 0)
            return null;

        array<ref SCR_VONEntry> entries = {};
        GetVONEntries(entries);

        foreach (SCR_VONEntry entry : entries)
        {
            SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
            if (radioEntry)
            {
                BaseTransceiver transceiver = radioEntry.GetTransceiver();
                if (transceiver && transceiver.GetFrequency() == frequency)
                    return radioEntry;
            }
        }

        return null;
    }
}
