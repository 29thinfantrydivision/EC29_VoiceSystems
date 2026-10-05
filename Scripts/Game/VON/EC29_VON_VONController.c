modded class SCR_VONController
{
    //! Plain (non-event) UI sounds. The resource paths are asset contracts.
    const string EC29_SOUND_CYCLE = "{19696BC8C5ECE170}Sounds/VON/EC29_FX/RadioCycle.wav";
    const string EC29_SOUND_LOCAL_ON = "{E21F58D501028C63}Sounds/VON/EC29_FX/RadioLocalOn.wav";
    const string EC29_SOUND_LOCAL_OFF = "{AFA775D58D24308A}Sounds/VON/EC29_FX/RadioLocalOff.wav";
    const string EC29_SOUND_ERROR = "{7065D8DD8ADFA3DE}Sounds/EC29_Sound/errorbeep.wav";

    //! Radio key-up limiter. A burst of four key-ups is always available; the bucket refills
    //! over four seconds, so steady traffic gets one key-up per second. An empty bucket turns
    //! the key-up away with the deny tone and never reaches vanilla.
    protected static const float EC29_KEY_BURST = 4;
    protected static const float EC29_KEY_REFILL_WINDOW_MS = 4000;
    protected ref EC29_TokenBucket m_EC29_KeyLimiter = new EC29_TokenBucket(EC29_KEY_BURST, EC29_KEY_REFILL_WINDOW_MS);

    //! One handle per UI sound so each one restarts itself instead of stacking.
    protected AudioHandle m_hEC29_CycleSound = AudioHandle.Invalid;
    protected AudioHandle m_hEC29_LocalOnSound = AudioHandle.Invalid;
    protected AudioHandle m_hEC29_LocalOffSound = AudioHandle.Invalid;
    protected AudioHandle m_hEC29_DenySound = AudioHandle.Invalid;

    //! Alternate push-to-talk state: the edge latch, and the entry to hand back on release.
    protected bool m_bEC29_AltHeld;
    protected SCR_VONEntry m_EC29_PrimaryBeforeAlt;

    //! Frequency the server was last told is keyed by us; -1 = nothing keyed.
    protected int m_iEC29_KeyedFreq = -1;

    //! First-spawn check runs once per controller instance (one server session on a client).
    protected bool m_bEC29_SpawnCheckDone;

    //! Set only while ActivateVON is keying a radio - see SetActiveTransmit for why.
    protected bool m_bEC29_KeyingRadio;

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

    //------------------------------------------------------------------------------------------------
    //! After vanilla init: warm the beep ACP so the first event plays without a load hitch, and
    //! create the RF propagation settings singleton early.
    override void OnPostInit(IEntity owner)
    {
        super.OnPostInit(owner);

        AudioSystem.PlayEventInitialize(EC29_RadioBeepHelper.BEEP_CONFIG);
        EC29_RFPropagationSettings.GetInstance();
    }

    //------------------------------------------------------------------------------------------------
    //! Stops the previous instance of a UI sound if it is still audible, then plays a fresh one.
    protected AudioHandle EC29_RestartSound(AudioHandle previous, string resource)
    {
        if (AudioSystem.IsSoundPlayed(previous))
            AudioSystem.TerminateSound(previous);

        return AudioSystem.PlaySound(resource);
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

    //------------------------------------------------------------------------------------------------
    //! The one place a radio key-up actually starts. The rate limit lives here, before vanilla,
    //! so a refused key-up returns false with capture untouched - refusing inside
    //! SetActiveTransmit still let vanilla open the mic afterwards.
    override protected bool ActivateVON(notnull SCR_VONEntry entry, EVONTransmitType transmitType = EVONTransmitType.NONE)
    {
        bool radioKey = SCR_VONEntryRadio.Cast(entry) && transmitType != EVONTransmitType.DIRECT && !EC29_CoexistenceGuard.ShouldYieldRadio();
        if (radioKey && EC29_IsKeySpamLocked())
        {
            EC29_PlayErrorBeep();
            if (EC29_Debug.VERBOSE)
                Print("[EC29-DBG][RadioKey] Key-up refused - key bucket empty", LogLevel.NORMAL);
            return false;
        }

        m_bEC29_KeyingRadio = radioKey;
        bool activated = super.ActivateVON(entry, transmitType);
        m_bEC29_KeyingRadio = false;
        return activated;
    }

    //------------------------------------------------------------------------------------------------
    //! Vanilla calls this for TWO jobs: keying a radio (from ActivateVON) and merely selecting
    //! one (SetVONLongRange, on Ctrl+Caps or whenever the active radio swaps between a short- and
    //! a long-range set). Only the first is a key-up: clear the voice capture (1.8 per-player
    //! capture wedge self-heal), TX beep and key-state notification, then vanilla. Treating a
    //! selection as a key sent a key-start with no stop, leaving a phantom dead key open on the
    //! net. The flag is consumed on first use so a nested vanilla re-entry can never key twice.
    override void SetActiveTransmit(notnull SCR_VONEntry entry)
    {
        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
        if (!radioEntry || !m_bEC29_KeyingRadio)
        {
            super.SetActiveTransmit(entry);
            return;
        }

        m_bEC29_KeyingRadio = false;

        if (m_VONComp)
            m_VONComp.SetCapture(false);

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (transceiver)
        {
            EC29_RadioBeepHelper.PlayTxStart(transceiver);
            EC29_NotifyKeyStart(transceiver);
        }

        super.SetActiveTransmit(entry);
    }

    //------------------------------------------------------------------------------------------------
    //! True when this key-up must be refused. Takes a token from the key bucket, clocked by world
    //! time; with no world there is no clock and the key-up is allowed.
    protected bool EC29_IsKeySpamLocked()
    {
        BaseWorld world = GetGame().GetWorld();
        if (!world)
            return false;

        return !m_EC29_KeyLimiter.TryConsume(world.GetWorldTime());
    }

    //------------------------------------------------------------------------------------------------
    //! The deny tone. Public so the frequency dialog can reuse it for rejected input.
    void EC29_PlayErrorBeep()
    {
        m_hEC29_DenySound = EC29_RestartSound(m_hEC29_DenySound, EC29_SOUND_ERROR);
    }

    //------------------------------------------------------------------------------------------------
    //! Runs from Update until a local player controls a character, then never again for this
    //! controller. No sound, no chat banner: a coexistence conflict goes to chat, otherwise one
    //! always-on log line proves the mod loaded on this client (grepped in client RPTs).
    protected void EC29_TryPlayRadioCheck()
    {
        if (m_bEC29_SpawnCheckDone)
            return;

        PlayerController playerController = GetGame().GetPlayerController();
        if (!playerController)
            return;

        if (!ChimeraCharacter.Cast(playerController.GetControlledEntity()))
            return;

        m_bEC29_SpawnCheckDone = true;

        string conflict = EC29_CoexistenceGuard.GetConflictNotice();
        if (!conflict.IsEmpty())
        {
            SCR_ChatComponent chat = SCR_ChatComponent.Cast(playerController.FindComponent(SCR_ChatComponent));
            if (chat)
                chat.ShowMessage(conflict);
            return;
        }

        PrintFormat("[EC29] Voice systems initialized (client, player %1)", playerController.GetPlayerId());
    }

    //------------------------------------------------------------------------------------------------
    //! Release. The TX end beep and key-stop run BEFORE vanilla, while the active entry is still
    //! the one that was keyed; the deferred voice tier is applied AFTER vanilla.
    override void DeactivateVON(EVONTransmitType transmitType = EVONTransmitType.NONE)
    {
        if (m_bIsActive && transmitType != EVONTransmitType.DIRECT && !EC29_CoexistenceGuard.ShouldYieldRadio())
        {
            SCR_VONEntryRadio keyedEntry = SCR_VONEntryRadio.Cast(m_ActiveEntry);
            if (keyedEntry && keyedEntry.GetTransceiver())
                EC29_RadioBeepHelper.PlayTxEnd(keyedEntry.GetTransceiver());

            EC29_NotifyKeyStop();
        }

        super.DeactivateVON(transmitType);

        EC29_ApplyPendingTier();
    }

    //------------------------------------------------------------------------------------------------
    //! Tells the server this player keyed a frequency, so receivers can squelch on a dead key
    //! even when no voice packets flow. Starts and stops stay paired per frequency: an entry
    //! swap mid-key (alternate PTT) closes the old frequency before opening the new one.
    protected void EC29_NotifyKeyStart(BaseTransceiver transceiver)
    {
        if (!transceiver)
            return;

        int frequency = transceiver.GetFrequency();
        if (frequency == m_iEC29_KeyedFreq)
            return;

        if (m_iEC29_KeyedFreq >= 0)
            Rpc(RpcAsk_EC29_KeyState, m_iEC29_KeyedFreq, 0.0, false);

        m_iEC29_KeyedFreq = frequency;
        Rpc(RpcAsk_EC29_KeyState, frequency, transceiver.GetRange(), true);
    }

    //------------------------------------------------------------------------------------------------
    protected void EC29_NotifyKeyStop()
    {
        if (m_iEC29_KeyedFreq < 0)
            return;

        Rpc(RpcAsk_EC29_KeyState, m_iEC29_KeyedFreq, 0.0, false);
        m_iEC29_KeyedFreq = -1;
    }

    //------------------------------------------------------------------------------------------------
    //! Server side of the key-state notification: stamp the sender's player id and hand it to
    //! the RF relay.
    [RplRpc(RplChannel.Reliable, RplRcver.Server)]
    protected void RpcAsk_EC29_KeyState(int frequency, float range, bool keyed)
    {
        PlayerController sender = PlayerController.Cast(GetOwner());
        if (!sender)
            return;

        EC29_RFPropagationNetworkComponent relay = EC29_RFPropagationNetworkComponent.GetInstance();
        if (!relay)
            return;

        relay.EC29_RelayKeyState(sender.GetPlayerId(), frequency, range, keyed);
    }

    //------------------------------------------------------------------------------------------------
    //! First radio entry whose transceiver is currently tuned to the given frequency, or null.
    //! Used by alternate PTT and by the RX squelch tracker.
    SCR_VONEntryRadio EC29_FindRadioEntryByFrequency(int frequency)
    {
        if (frequency < 0)
            return null;

        foreach (SCR_VONEntry entry : m_aEntries)
        {
            SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
            if (!radioEntry)
                continue;

            BaseTransceiver transceiver = radioEntry.GetTransceiver();
            if (transceiver && transceiver.GetFrequency() == frequency)
                return radioEntry;
        }

        return null;
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

        // The alternate-channel key is Left Ctrl + Caps Lock by default, and Caps Lock alone is
        // vanilla's radio push-to-talk - so the combo also raises this action. Only the START is
        // swallowed (the alternate poll in Update keys the alternate net instead); the release
        // always reaches vanilla, which never refuses to stop a transmission.
        if (reason != EActionTrigger.UP && EC29_IsAlternateKeyClaimed())
            return;

        super.ActionVONBroadcast(value, reason);
    }

    override protected void ActionVONLongRangeToggle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        // Vanilla binds the long-range toggle to the same Left Ctrl + Caps Lock. While a player
        // has an alternate channel set, the combo belongs to alternate transmit; with none set it
        // is vanilla's toggle exactly as before.
        if (EC29_IsAlternateKeyClaimed())
            return;

        super.ActionVONLongRangeToggle(value, reason);
    }

    //! True while the alternate-transmit input is held AND there is an alternate channel for it
    //! to key. Derived from the action, not from raw keys, so it follows a player's rebind: move
    //! alternate transmit off Ctrl+Caps in the keybind menu and vanilla gets both keys back.
    protected bool EC29_IsAlternateKeyClaimed()
    {
        if (EC29_CoexistenceGuard.ShouldYieldRadio() || !m_InputManager)
            return false;

        if (m_InputManager.GetActionValue("EC29_AlternateChannel") <= 0)
            return false;

        return EC29_RadioState.GetInstance().EarSettings().GetAlternateFrequency() >= 0;
    }

    override protected void ActionVONProximity(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        super.ActionVONProximity(value, reason);
    }

    //------------------------------------------------------------------------------------------------
    //! Direct-speech toggle with an audible on/off cue. A spectator-blocked press makes no sound.
    //! With no VoN component there is nothing to cue, but vanilla still runs so the rest of the
    //! modded chain does.
    override protected void ActionVONProximityToggle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        if (!m_VONComp)
        {
            super.ActionVONProximityToggle(value, reason);
            return;
        }

        bool toggledBefore = m_bIsToggledDirect;
        super.ActionVONProximityToggle(value, reason);

        if (EC29_CoexistenceGuard.ShouldYieldRadio())
            return;

        if (!toggledBefore && m_bIsToggledDirect)
            m_hEC29_LocalOnSound = EC29_RestartSound(m_hEC29_LocalOnSound, EC29_SOUND_LOCAL_ON);
        else if (toggledBefore && !m_bIsToggledDirect)
            m_hEC29_LocalOffSound = EC29_RestartSound(m_hEC29_LocalOffSound, EC29_SOUND_LOCAL_OFF);
    }

    //------------------------------------------------------------------------------------------------
    //! Transceiver cycle with a click on the press. A spectator-blocked press makes no sound.
    override protected void ActionVONTransceiverCycle(float value, EActionTrigger reason = EActionTrigger.UP)
    {
        if (EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions())
            return;

        if (reason == EActionTrigger.DOWN && !EC29_CoexistenceGuard.ShouldYieldRadio())
            m_hEC29_CycleSound = EC29_RestartSound(m_hEC29_CycleSound, EC29_SOUND_CYCLE);

        super.ActionVONTransceiverCycle(value, reason);
    }

    //------------------------------------------------------------------------------------------------
    //! Radio input is POLLED here every frame, never listener-driven: PTT by action value, the
    //! radial-menu actions by triggered-this-frame and only while the VON radial menu is open.
    //! Listeners pulled EC29's contexts into the engine's key arbitration (shadowed and
    //! double-fired vanilla T/F/K, PTT live far outside the VON situation - 940c076 reverted
    //! cba68ed). Do not convert.
    //!
    //! Gate order is load-bearing. The spectator gate is derived per frame and can flip on
    //! mid-hold (dying into spectate with the alternate key down), so it gates only the START
    //! edge; the release edge always runs, or the latch, the saved primary entry and the HUD's
    //! transmitting-on-alternate state would be stranded for the whole spectate.
    override void Update(float timeSlice)
    {
        super.Update(timeSlice);

        EC29_TryPlayRadioCheck();

        if (EC29_CoexistenceGuard.ShouldYieldRadio())
            return;

        // Vanilla resolved this at Init; no per-frame refetch.
        if (!m_InputManager)
            return;

        bool spectatorBlocked = EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions();

        bool altKeyDown = m_InputManager.GetActionValue("EC29_AlternateChannel") > 0;
        if (altKeyDown && !m_bEC29_AltHeld)
        {
            if (!spectatorBlocked)
                OnAlternatePTTStart();
        }
        else if (!altKeyDown && m_bEC29_AltHeld)
        {
            OnAlternatePTTEnd();
        }

        if (spectatorBlocked)
            return;

        if (!m_VONMenu)
            return;

        SCR_RadialMenu radial = m_VONMenu.GetRadialMenu();
        if (!radial || !radial.IsOpened())
            return;

        if (m_InputManager.GetActionTriggered("EC29_VONRoutingAction"))
            OnEarRoutingToggle();

        if (m_InputManager.GetActionTriggered("EC29_SetFrequencyAction"))
            OnSetFrequencyPressed();

        if (m_InputManager.GetActionTriggered("EC29_VONBeepTypeAction"))
            OnBeepTypeToggle();

        if (m_InputManager.GetActionTriggered("EC29_VolumeUp"))
            OnVolumeAdjust(1);

        if (m_InputManager.GetActionTriggered("EC29_VolumeDown"))
            OnVolumeAdjust(-1);

        if (m_InputManager.GetActionTriggered("EC29_AlternateChannelAction"))
            OnAlternateChannelToggle();
    }

    //------------------------------------------------------------------------------------------------
    //! The radio entry hovered in the open VON radial menu, provided it has a transceiver.
    protected SCR_VONEntryRadio EC29_HoveredRadioEntry()
    {
        if (!m_VONMenu)
            return null;

        SCR_RadialMenu radial = m_VONMenu.GetRadialMenu();
        if (!radial)
            return null;

        SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(radial.GetSelectionEntry());
        if (!radioEntry || !radioEntry.GetTransceiver())
            return null;

        return radioEntry;
    }

    //------------------------------------------------------------------------------------------------
    //! Repaint the radial menu so the entry labels pick up a change straight away.
    protected void EC29_RefreshRadial()
    {
        if (!m_VONMenu)
            return;

        SCR_RadialMenu radial = m_VONMenu.GetRadialMenu();
        if (radial)
            radial.UpdateEntries();
    }

    //------------------------------------------------------------------------------------------------
    //! T: cycle the hovered radio's ear routing.
    protected void OnEarRoutingToggle()
    {
        SCR_VONEntryRadio radioEntry = EC29_HoveredRadioEntry();
        if (!radioEntry)
            return;

        EC29_RadioState.GetInstance().EarSettings().CycleRouting(radioEntry.GetTransceiver());
        EC29_RefreshRadial();
    }

    //------------------------------------------------------------------------------------------------
    //! K: cycle the hovered radio's beep style and preview it. The preview ignores the master
    //! switch; when the switch is off a popup explains why live beeps stay silent.
    protected void OnBeepTypeToggle()
    {
        SCR_VONEntryRadio radioEntry = EC29_HoveredRadioEntry();
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        EC29_EBeepType style = settings.CycleBeepType(transceiver);

        EC29_RadioBeepHelper.PlayPreview(transceiver);

        if (!EC29_RadioBeepHelper.EC29_AreBeepsEnabled())
        {
            SCR_PopUpNotification popup = SCR_PopUpNotification.GetInstance();
            if (popup)
                popup.PopupMsg("Radio beep style: " + settings.GetBeepStyleLongText(style), 4, "Radio beeps are OFF - enable them in Settings > Audio > 29th ID");
        }

        EC29_RefreshRadial();
    }

    //------------------------------------------------------------------------------------------------
    //! F: open the frequency dialog for the hovered radio. Special nets are never retuned.
    protected void OnSetFrequencyPressed()
    {
        SCR_VONEntryRadio radioEntry = EC29_HoveredRadioEntry();
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
        {
            if (EC29_Debug.VERBOSE)
                PrintFormat("[EC29-DBG][RadioFreq] Refused: %1 kHz is a special net", transceiver.GetFrequency());
            return;
        }

        EC29_FrequencyDialog.OpenForTransceiver(transceiver, radioEntry);
    }

    //------------------------------------------------------------------------------------------------
    //! ] / [: one 10% step on the hovered radio's channel volume.
    protected void OnVolumeAdjust(float value)
    {
        SCR_VONEntryRadio radioEntry = EC29_HoveredRadioEntry();
        if (!radioEntry)
            return;

        float step = -0.1;
        if (value > 0)
            step = 0.1;

        EC29_RadioState.GetInstance().EarSettings().AdjustVolume(radioEntry.GetTransceiver(), step);
        EC29_RefreshRadial();
    }

    //------------------------------------------------------------------------------------------------
    //! LCtrl+F: mark or unmark the hovered radio as the alternate channel. Special nets are
    //! refused - marking one would hand alternate PTT a transmit route around other mods'
    //! action blocks.
    protected void OnAlternateChannelToggle()
    {
        SCR_VONEntryRadio radioEntry = EC29_HoveredRadioEntry();
        if (!radioEntry)
            return;

        BaseTransceiver transceiver = radioEntry.GetTransceiver();
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(transceiver))
        {
            if (EC29_Debug.VERBOSE)
                PrintFormat("[EC29-DBG][RadioAlt] Refused: %1 kHz is a special net", transceiver.GetFrequency());
            return;
        }

        EC29_RadioState.GetInstance().EarSettings().ToggleAlternate(transceiver);
        EC29_RefreshRadial();
    }

    //------------------------------------------------------------------------------------------------
    //! Alternate PTT pressed: swap the alternate entry in and key it through vanilla. Vanilla's
    //! activation runs SetActiveTransmit, so the rate limit, TX beep and key RPC apply without
    //! being called here.
    protected void OnAlternatePTTStart()
    {
        EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
        int frequency = settings.GetAlternateFrequency();
        if (frequency < 0)
            return;

        SCR_VONEntryRadio altEntry = EC29_FindRadioEntryByFrequency(frequency);
        if (!altEntry)
            return;

        // The toggle already refuses special nets; this covers a net that became special after.
        if (EC29_CoexistenceGuard.EC29_IsSpecialNet(altEntry.GetTransceiver()))
            return;

        m_bEC29_AltHeld = true;
        settings.SetTransmittingOnAlternate(true);

        m_EC29_PrimaryBeforeAlt = m_ActiveEntry;
        SetEntryActive(altEntry);
        ActivateVON(EVONTransmitType.CHANNEL);

        if (EC29_Debug.VERBOSE)
            PrintFormat("[EC29-DBG][RadioAlt] Alternate PTT start on %1 kHz", frequency);
    }

    //------------------------------------------------------------------------------------------------
    //! Alternate PTT released. Never gated: always unkeys and hands the primary entry back.
    protected void OnAlternatePTTEnd()
    {
        EC29_RadioState.GetInstance().EarSettings().SetTransmittingOnAlternate(false);
        m_bEC29_AltHeld = false;

        DeactivateVON(EVONTransmitType.CHANNEL);

        // The primary may have been removed while the alternate was held (radio dropped).
        if (m_EC29_PrimaryBeforeAlt && m_aEntries.Find(m_EC29_PrimaryBeforeAlt) >= 0)
            SetEntryActive(m_EC29_PrimaryBeforeAlt);

        m_EC29_PrimaryBeforeAlt = null;

        if (EC29_Debug.VERBOSE)
            Print("[EC29-DBG][RadioAlt] Alternate PTT end", LogLevel.NORMAL);
    }
}
