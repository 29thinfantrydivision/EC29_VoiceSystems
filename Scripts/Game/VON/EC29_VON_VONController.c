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

    //! A voice mode chosen while a push-to-talk was held. -1 = nothing pending. Applied by
    //! DeactivateVON once the key is up, so a transmission keeps the range it started with
    //! (Nathan, 2026-09-12: "for mid press keying I'm fine with keeping the first volume").
    protected int m_iEC29_PendingTier = -1;

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
    //! Every radio VON entry passes through here on client and server; hand the
    //! radio to the receive-health tracker (RX heartbeat - see
    //! EC29_RadioSystemGuard.c).
    override void AddEntry(SCR_VONEntry entry)
    {
        super.AddEntry(entry);

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (!transceiver)
            return;

        EC29_RadioState.GetInstance().ReceiverGuard().OnRadioEntryAdded(transceiver);
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

        // Spectator block: a spectator's direct speech is locked, so cycling its range writes
        // replicated VoN state for nothing - pointless at best, confusing telemetry at worst.
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;
        // The mode lives on the STOCK component, not on whichever tier is transmitting.
        SCR_VoNComponent stock = EC29_LocalStockVoN();
        if (!stock)
        {
            PrintFormat("[EC29_VON] Cycle pressed but no SCR_VoNComponent on controlled entity", level: LogLevel.WARNING);
            return;
        }

        EC29_EVoiceRange current = stock.EC29_GetVoiceRange();
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
        stock.EC29_RequestSetVoiceRange(next);

        // THE TRANSMIT TIER SWITCHES HERE, LOCALLY, before the request has even reached the
        // server: the speaking range is a property of which component captures, decided on
        // this machine. The replicated enum only feeds labels and icons.
        EC29_ApplyVoiceTier(next);

        // Refresh the VoN overlay label immediately for the local outgoing transmission.
        // The RplProp callback handles remote receivers, but the authority does not always
        // fire its own onRplName for its own writes - this guarantees local UI snaps to the
        // new mode the same frame the input is pressed.
        SCR_VonDisplay display = stock.GetDisplay();
        if (display)
            display.EC29_ForceRefreshAllTransmissions();
    }

    //! ------------------------------------------------------------------------------------------
    //! DIRECT-SPEECH TIERS (see EC29_VoiceTiers.c for the why).
    //!
    //! Every assignment of a transmit component funnels through SetVONComponent - vanilla's
    //! controlled-entity change, AssignVONComponent, the editor manager handing the character
    //! back on close, and EC29's own spectator service. Overriding it is the one choke point
    //! that guarantees a character never transmits direct speech from its stock component: the
    //! stock one is the ear, and its ACP carries the full 40 m hearing range.
    //! ------------------------------------------------------------------------------------------

    //! The stock (exact-type) VoN component of the locally controlled entity, or null.
    protected SCR_VoNComponent EC29_LocalStockVoN()
    {
        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return null;

        return EC29_VoiceTiers.StockVoN(pc.GetControlledEntity());
    }

    //! The controlled character's STOCK component becomes the tier for its current voice mode.
    //! Anything else - a tier already, an editor manager's component, a spectator ear, some
    //! other entity's component, null - passes through untouched, so every other system's
    //! selection is exactly what it asked for. Engine components expose no owner to script, so
    //! "the controlled character's stock component" is matched from the entity's side; that is
    //! also the only entity a transmit tier is ever wanted for.
    protected SCR_VoNComponent EC29_ResolveTransmitTier(SCR_VoNComponent comp)
    {
        if (!comp || comp.Type() != SCR_VoNComponent)
            return comp;

        PlayerController pc = PlayerController.Cast(GetOwner());
        if (!pc)
            return comp;

        IEntity owner = pc.GetControlledEntity();
        if (!ChimeraCharacter.Cast(owner) || EC29_VoiceTiers.StockVoN(owner) != comp)
            return comp;

        SCR_VoNComponent tier = EC29_VoiceTiers.FindTier(owner, comp.EC29_GetVoiceRange());
        if (!tier)
        {
            // A character prefab outside EC29's Character_Base override: no tiers, so the stock
            // component transmits at its own (full) range - the pre-tier behaviour, minus the
            // gain variable. Logged once per such entity class would be nicer; VERBOSE will do.
            if (EC29_Debug.VERBOSE)
                PrintFormat("[EC29-DBG][VONCtrl] %1 carries no direct-speech tiers - transmitting from the stock component", owner.Type());
            return comp;
        }

        return tier;
    }

    override void SetVONComponent(SCR_VoNComponent VONComp)
    {
        super.SetVONComponent(EC29_ResolveTransmitTier(VONComp));
    }

    //! Re-points transmission at the tier for a mode. Immediate when nothing is keyed, or when
    //! direct-speech TOGGLE is on (EC29_SelectVonComponent stops the toggle's capture, swaps, and
    //! re-arms it - a gap of one call, and the toggle can stay on for minutes so waiting is not an
    //! option). Deferred to key-up during a HELD transmission of any kind, so the range a
    //! sentence started with is the range it ends with.
    void EC29_ApplyVoiceTier(EC29_EVoiceRange mode)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        PlayerController pc = GetGame().GetPlayerController();
        if (!pc)
            return;

        SCR_VoNComponent tier = EC29_VoiceTiers.FindTier(pc.GetControlledEntity(), mode);
        if (!tier)
            return;

        if (m_bIsActive && !m_bIsToggledDirect)
        {
            m_iEC29_PendingTier = mode;
            if (EC29_Debug.VERBOSE)
                PrintFormat("[EC29-DBG][VONCtrl] tier %1 pending until key-up (transmission in progress keeps its range)", typename.EnumToString(EC29_EVoiceRange, mode));
            return;
        }

        m_iEC29_PendingTier = -1;
        if (!EC29_SelectVonComponent(tier))
            PrintFormat("[EC29_VON] transmit tier %1 did not take - vanilla's protected VON members changed?", tier.Type(), level: LogLevel.WARNING);
        else if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][VONCtrl] transmit tier -> %1", tier.Type());
    }

    //! Applies a tier deferred by EC29_ApplyVoiceTier once the transmission that deferred it has
    //! ended. Runs after vanilla's deactivation so m_bIsActive is already down - unless the direct
    //! toggle re-armed capture inside super, in which case the swap happens through the toggle
    //! path (EC29_SelectVonComponent re-arms it).
    protected void EC29_ApplyPendingTier()
    {
        if (m_iEC29_PendingTier < 0)
            return;

        EC29_EVoiceRange mode = m_iEC29_PendingTier;
        m_iEC29_PendingTier = -1;
        EC29_ApplyVoiceTier(mode);
    }

    //! Vanilla re-resolves the transmit component here (through SetVONComponent, so the tier
    //! substitution still applies); a tier deferred for the previous body is meaningless now.
    //! After vanilla is done the tier is applied once more from the new body's mode: a no-op
    //! when SetVONComponent already resolved it, and the correction if vanilla's
    //! FindComponent(SCR_VoNComponent) ever hands back a tier instead of the stock component
    //! (component order is by class name and the tiers sort after - see EC29_VoiceTiers.c - but
    //! this does not have to trust that). Nothing is keyed right after a body change, so it
    //! applies immediately; while spectating it defers to the service, which re-asserts.
    override protected void OnControlledEntityChanged(IEntity from, IEntity to)
    {
        m_iEC29_PendingTier = -1;
        super.OnControlledEntityChanged(from, to);

        SCR_VoNComponent stock = EC29_VoiceTiers.StockVoN(to);
        if (stock)
            EC29_ApplyVoiceTier(stock.EC29_GetVoiceRange());
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

    //! Vanilla's transmit gate silently reroutes to direct speech when the
    //! active entry is flagged unusable. Entry usability is a snapshot of the
    //! radio's power state taken at entry init or menu refresh - a refresh
    //! landing while the radio was momentarily off latches the entry unusable
    //! after it is powered again and receiving: dead TX, working RX, no
    //! feedback (2026-08-23 field case, then caused by the since-removed repair
    //! cycle; a manual off/on with a menu refresh in between produces the same
    //! stale snapshot). Re-sync the flag from the actual power state at the
    //! moment of use, so a stale snapshot can never eat a key-up.
    override protected void SetVONBroadcast(bool activate, EVONTransmitType transmitType = EVONTransmitType.CHANNEL)
    {
        if (activate && m_ActiveEntry && !m_ActiveEntry.IsUsable() && !EC29_CoexistenceGuard.ShouldYieldRadio())
        {
            SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(m_ActiveEntry);
            if (radioEntry)
            {
                BaseTransceiver transceiver = radioEntry.GetTransceiver();
                if (transceiver)
                {
                    BaseRadioComponent radio = transceiver.GetRadio();
                    if (radio && radio.IsPowered())
                    {
                        radioEntry.SetUsable(true);
                        PrintFormat("[EC29-DBG][RadioTX] Active entry (freq %1 kHz) was flagged unusable while its radio is powered - re-synced so this key-up transmits (stale usability snapshot)", transceiver.GetFrequency(), level: LogLevel.WARNING);
                    }
                    else if (EC29_Debug.VERBOSE)
                    {
                        PrintFormat("[EC29-DBG][RadioTX] Key-up falling back to direct speech: active entry (freq %1 kHz) unusable and radio is unpowered", transceiver.GetFrequency());
                    }
                }
            }
        }

        super.SetVONBroadcast(activate, transmitType);
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

            // 1.8 can wedge the player's voice capture so every radio transmit
            // is silently dead until it clears - per-player, survives switching
            // radios (community-documented; the Exilados fix mod carries the
            // same guard). Clearing before keying makes a wedged state
            // self-heal on the next PTT press.
            if (m_VONComp)
                m_VONComp.SetCapture(false);

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
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        float nowMs = world.GetWorldTime();

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

        EC29_ApplyPendingTier();
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

    //! ------------------------------------------------------------------------------------------
    //! Spectator voice primitives (consumed by EC29_SpectatorVonService).
    //!
    //! Both exist because the vanilla members they touch are PROTECTED with no public accessor -
    //! a modded class inherits access, ordinary script does not. Absorbed from the spectator
    //! mod's controller layer together with their reasoning.
    //! ------------------------------------------------------------------------------------------

    //! True restores normal local speech; false makes local direct-speech transmission
    //! IMPOSSIBLE rather than merely quiet. Vanilla gates both direct-speech transmit paths
    //! (SetVONProximity / SetVONProximityToggle) on m_DirectSpeechEntry.IsUsable(), so clearing
    //! usability beats a zero speech range: no attenuation curve to tune, no distance at which
    //! it leaks anyway. Receiving is untouched - usability is consulted only on the transmit
    //! path - which is what leaves a spectator able to hear everything while saying nothing.
    //!
    //! SAFE FOR EC29'S OWN USABILITY MACHINERY, verified: EC29's usability re-sync site
    //! (SetVONBroadcast above) casts to SCR_VONEntryRadio before touching usability, so it can
    //! never re-arm the plain direct-speech entry this locks.
    //!
    //! MUST BE RESTORED ON THE WAY OUT. This controller lives on the player controller, which
    //! outlives any single life, so a spectator who is never re-enabled stays mute for the rest
    //! of the session. EC29_SpectatorVonService.ExitSpectate owns that restore, and its
    //! self-healing auto-exit covers a missed exit path. Safe to call repeatedly and safe before
    //! the entry exists - a controller mid-initialisation simply has nothing to set yet.
    void EC29_SetDirectSpeechTransmitLocked(bool locked)
    {
        if (m_DirectSpeechEntry)
            m_DirectSpeechEntry.SetUsable(!locked);
    }

    //! Selects which SCR_VoNComponent the controller transmits through - the spectator body's
    //! near-silent tier, in the service's case. LIVES HERE BECAUSE THE SEQUENCE IS PROTECTED,
    //! and because the switch must be ATOMIC.
    //!
    //! The obvious implementation - DeactivateVON(); SetVONProximityToggle(false);
    //! SetVONComponent() - does NOT reliably work (community-documented across several
    //! structurally different attempts: the audible range "kept sticking to whichever tier's
    //! component was first on the entity, regardless of which one actually captured"). The
    //! reason is visible in the vanilla source:
    //!   SetVONProximityToggle(bool activate)
    //!     if (!m_VONComp) return;
    //!     if (!m_DirectSpeechEntry.IsUsable()) return;   <- deliberately false while spectating
    //!     if (m_bIsToggledDirect == activate) return;
    //! Every early-return leaves m_bIsToggledDirect stale, and the middle one is guaranteed to
    //! fire while the direct-speech lock is on. So the setter cannot be used to clear the latch -
    //! the field is written directly instead, which a modded class may do and outside script may
    //! not.
    //!
    //! A character's stock component handed in here resolves to its transmit tier first (same
    //! rule as SetVONComponent), and the success check compares against that - so the spectator
    //! service's "give the character its voice back" lands on the right tier and still reports
    //! true.
    bool EC29_SelectVonComponent(SCR_VoNComponent comp)
    {
        comp = EC29_ResolveTransmitTier(comp);
        if (!comp)
            return false;

        if (m_VONComp == comp)
            return true;

        // Captured BEFORE anything is torn down, so a transmission in progress can be resumed
        // on the new component rather than silently dropped.
        bool wasDirectActive = (m_bIsActive && m_eVONType == EVONTransmitType.DIRECT);
        bool wasDirectToggled = m_bIsToggledDirect;

        m_bIsToggledDirect = false;

        if (m_bIsActive)
            DeactivateVON(m_eVONType);

        SetVONComponent(comp);

        if (wasDirectActive && wasDirectToggled)
            SetVONProximityToggle(true);

        // Reports whether it actually took, rather than assuming - the whole point of this
        // method is that the naive version silently did not. A false return after a game update
        // is the tripwire that vanilla's protected members changed underneath us.
        return m_VONComp == comp;
    }

    //! ------------------------------------------------------------------------------------------
    //! VANILLA RADIO/DIRECT TRANSMIT IS REMOVED WHILE SPECTATING - the
    //! capability, not one key binding. A spectator has the service's own push-to-talk, which
    //! drives capture directly; vanilla's VON actions are a SECOND, parallel route to the same
    //! microphone and radio that the spectator system never asked for - it transmits on whatever
    //! entry vanilla picked, and double-tap could cycle radio nets straight past everything the
    //! service set up. ALL FIVE vanilla VON actions are blocked, not just the transmit ones: the
    //! two direct-speech blocks are belt and braces over the usability lock (concealment must
    //! not DEPEND on one mechanism holding), and the cycle/long-range blocks close the
    //! net-change route.
    //!
    //! The gate is DERIVED PER CALL from the service's own spectating state, never latched -
    //! see EC29_SpectatorVonService. It does not depend on what the player controls, which is
    //! nothing at all once a spectator system takes their entity away. For everyone else this is one check and
    //! then straight into vanilla; the blocks only ever SHORT-CIRCUIT, so whatever the rest of
    //! the modded chain does still happens exactly as it would have for living players.
    //! Signatures must match vanilla EXACTLY, including the default argument, or the override is
    //! rejected at compile time.
    //!
    //! NOTE the asymmetry with the service's own machinery: EC29_SelectVonComponent calls
    //! SetVONProximityToggle, which is a DIFFERENT method from ActionVONProximityToggle - the
    //! action is the input handler, the setter is the state change. Blocking the actions does
    //! not disturb the tier swap.
    //! ------------------------------------------------------------------------------------------
    override protected void ActionVONBroadcast(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        super.ActionVONBroadcast(value, reason);
    }

    override protected void ActionVONLongRangeToggle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        super.ActionVONLongRangeToggle(value, reason);
    }

    override protected void ActionVONProximity(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        super.ActionVONProximity(value, reason);
    }

    override protected void ActionVONProximityToggle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        // Spectator block first - before the toggle beeps below, so a blocked press makes no
        // sound at all instead of beeping over a refused action.
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

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
        // Spectator block first - before the cycle sound, so a blocked press does not play
        // feedback for an action that will not happen.
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

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

        // Spectator block placement is asymmetric ON PURPOSE. The alternate-PTT poll is an edge
        // detector over a latched state (m_bAlternatePTTActive), not a stateless action - and the
        // spectator gate, unlike the session-constant coexistence yield above, is derived per
        // frame and can flip TRUE mid-hold when a player enters spectate with the key down.
        // A whole-tail early-return here would strand the latch: the release edge would never be
        // seen, the saved primary entry never restored, and the transmitting-on-alternate flag
        // (and its CYAN HUD state) stuck for the entire spectate. So only the START edge is
        // gated - closing the documented action-block bypass - while the END edge always runs,
        // which is exactly the pre-absorption behavior (the poll ran through death and cleaned
        // the latch on release).
        float altValue = inputMgr.GetActionValue("EC29_AlternateChannel");
        if (altValue > 0 && !m_bAlternatePTTActive)
        {
            if (!EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
                OnAlternatePTTStart();
        }
        else if (altValue <= 0 && m_bAlternatePTTActive)
        {
            OnAlternatePTTEnd();
        }

        // The radial-menu actions are stateless per-press handlers, so the blanket gate is safe
        // here - and while spectating they are all meaningless at best.
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

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
        EC29_EBeepType next = settings.CycleBeepType(transceiver);

        // With the master switch ON, the preview tone plus the radial label are
        // the confirmation - no popup. Master OFF previews as silence, so the
        // popup carries the state and points at the switch (the original
        // "K does nothing" fail-safe, now only where it is still needed).
        EC29_RadioBeepHelper.PlayPreview(transceiver);

        if (!EC29_RadioBeepHelper.EC29_AreBeepsEnabled())
        {
            string styleText = settings.GetBeepTypeDisplayText(next);
            SCR_PopUpNotification.GetInstance().PopupMsg("Radio beep style: " + styleText, 4, "Radio beeps are OFF - enable them in Settings > Audio > 29th ID");
        }

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

        // Never retune another system's net (the spectator net, an admin-only net) - a changed
        // frequency breaks that system until its owner rebuilds it.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
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

        // The alternate-PTT poll bypasses other mods' action-level transmit
        // blocks, so marking a special net as alternate would hand spectators
        // a transmit route their own mod deliberately removed.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
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

        // Belt-and-braces with the toggle-side gate: never key a special net.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(altEntry.GetTransceiver()))
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
