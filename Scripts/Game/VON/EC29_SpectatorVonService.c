//------------------------------------------------------------------------------------------------
//! EC29_SpectatorVonService - spectator voice, owned by EC29.
//!
//! The spectator mod (29th_Spectator_V3) owns spectator LIFECYCLE and INPUT; this service owns
//! spectator VOICE. The state machine, the direct-speech concealment, the tier swap and the
//! push-to-talk pipeline all live here; the spectator mod's camera calls in through five
//! methods (EnterSpectate / RegisterSpectatorBody / SetTransmitting / SetReceiveEnabled /
//! ExitSpectate) and passes its own body, tier components and radio. The service is
//! component-agnostic on purpose: during the delegation phase the spectator mod registers its
//! own resources, and after its voice stack is deleted it registers the EC29-shipped ones
//! (EC29_VoNSpectator tiers + EC29_Radio_Spectator.et). No EC29 code references any spectator-mod
//! type - the dependency points the other way only.
//!
//! Every non-obvious rule in this file was learned in the field by the absorbed implementation
//! (29th_Spectator_V3, SPEC29_Camera.c voice section) and is carried over with its original
//! reasoning. The push-to-talk path in particular is under a VERBATIM-INVARIANT rule: the
//! CM_DIRECT fallback guard's root cause was never established, so "cleanup" of that path is
//! banned - see SetTransmitting.
//!
//! THE INTEROP ABI: the spectator net is a radio with encryption key "SPEC29_KEY" at 29000 kHz.
//! Privacy is the ENCRYPTION KEY, not the frequency. Those two values are shared identity
//! between this mod and the spectator mod - EC29_Radio_Spectator.et carries the same pair as
//! the spectator mod's Radio_spectator.et, and EC29's special-net heuristic
//! (EC29_CoexistenceGuard.EC29_IsSpecialNet: below 30000 kHz, or ranged past 10 km) exempts any
//! such radio from guard power-cycling, retune, alternate PTT, squelch and RF simulation.
//! Changing either value is a cross-mod breaking change.
//!
//! RANGE IS A TUNABLE, NOT ABI. It shipped at 50 km (map-wide) and was cut to 2 km on
//! 2026-09-05 as the first experiment against a server-side stall: with spectators keying the
//! net, the dedicated server's frame rate fell to ~2 FPS. EC29 runs no per-packet work on a
//! server (OnReceive exits with no local PlayerController), so the suspect is the engine's own
//! relay over a 50 km sphere that covers every transceiver on the map. The 29000 kHz floor
//! keeps the net special at any range, so this change is invisible to the heuristic.
//!
//! WHY STATE IS DERIVED, NOT LATCHED. SCR_VONController lives on the player controller, which
//! outlives every life - a session-lifetime boolean that gates voice behaviour permanently mutes
//! anyone whose exit path misses one restore (that bug class shipped once in the spectator mod's
//! own history). So the vanilla-action block re-derives "is the local player driving the
//! registered ghost" on every call and self-heals by construction: the body handle nulls when
//! the entity is deleted, and everything degrades to vanilla behaviour instead of a
//! session-long mute. The one unavoidable latch - the direct-speech usability kill, which
//! vanilla consults on its transmit path - is re-asserted per controlled-entity change while
//! spectating, unconditionally restored on exit, and covered by a self-healing auto-exit if a
//! live character shows up while the service still thinks it is spectating.
//!
//! Owned by EC29_RadioState (world-scoped, rebuilt on world change), so no state here survives
//! a scenario change. Client-side by nature: every path starts from the local PlayerController,
//! so on a dedicated server every method no-ops.
//------------------------------------------------------------------------------------------------
class EC29_SpectatorVonService
{
	protected bool m_bSpectating;
	protected bool m_bNetEnabled = true;
	protected bool m_bSubscribed;

	//! True from successful capture start to key-up. Local audible range follows the
	//! CONTROLLER'S ACTIVE COMPONENT, not the capturing one (field-observed: switching the
	//! controller back to normal "restored full audible range on a capture that was still
	//! running") - so while this is set, the deferred re-assert must never select the normal
	//! tier: a changed-handle registration on the same press (a late-streaming radio healing at
	//! key-down) queues one, and it would land one frame into the hold.
	protected bool m_bTransmitting;

	//! All four are plain engine handles, not owned refs - they auto-null when the entity or
	//! component is deleted, which is exactly the degradation the derived checks rely on.
	protected IEntity m_SpectatorBody;
	protected SCR_VoNComponent m_NormalTier;
	protected SCR_VoNComponent m_QuietTier;
	protected BaseRadioComponent m_Radio;

	//------------------------------------------------------------------------------------------------
	//! True while the local player is driving the REGISTERED spectator ghost. This is the gate the
	//! vanilla VON action blocks and the alternate-PTT poll consult - re-derived on every call, so a
	//! deleted body or a missed exit degrades to vanilla behaviour rather than a stuck block.
	static bool EC29_ShouldBlockVanillaVonActions()
	{
		return EC29_RadioState.GetInstance().SpectatorVon().IsBlockingVanillaVonActions();
	}

	//------------------------------------------------------------------------------------------------
	bool IsBlockingVanillaVonActions()
	{
		if (!m_bSpectating || !m_SpectatorBody)
			return false;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		return pc.GetControlledEntity() == m_SpectatorBody;
	}

	//------------------------------------------------------------------------------------------------
	bool IsSpectating()
	{
		return m_bSpectating;
	}

	//------------------------------------------------------------------------------------------------
	//! Spectate begins. Runs BEFORE any ghost body exists - the deploy menu is typically still
	//! open - so this only arms state, kills local direct speech and subscribes to
	//! controlled-entity changes. The direct-speech kill must land here rather than at body
	//! arrival because it must hold even if the body never lands: a dead player controlling a
	//! live entity could otherwise talk to the people they are watching.
	void EnterSpectate()
	{
		if (m_bSpectating)
			return;

		m_bSpectating = true;
		m_bNetEnabled = true; // per-entry default; the caller re-feeds a remembered preference via SetReceiveEnabled

		ApplyDirectSpeechLock(true);
		Subscribe();

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][SpecVon] EnterSpectate - direct speech locked, watching controlled-entity changes", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! The caller hands over the ghost and its voice components once the body actually lands
	//! (and again on every re-possess). The caller resolves the components because only it knows
	//! their concrete types; this service stores base-typed handles. Pass null body to
	//! unregister. Registration applies the receive-mute state immediately - receiving needs the
	//! radio unmuted from the moment the body exists, not from the first time someone presses
	//! the talk key - and queues the deferred tier/lock re-assert.
	void RegisterSpectatorBody(IEntity body, SCR_VoNComponent normalTier, SCR_VoNComponent quietTier, BaseRadioComponent radio)
	{
		if (!m_bSpectating)
		{
			if (body)
				Print("[EC29-DBG][SpecVon] RegisterSpectatorBody outside spectate - ignored (call EnterSpectate first)", LogLevel.WARNING);
			return;
		}

		// TRUST BOUNDARY TRIPWIRE. This service drives capture, transmit routing and mute state on
		// whatever radio it is handed, and the whole concealment design assumes that radio is the
		// spectator net (the special-net triple: sub-band frequency, super-physical range). The
		// caller resolves it by inventory scan, so a future ghost loadout carrying a second,
		// LIVING-net radio could hand us a transceiver the guard/squelch/RF stack actively manages -
		// and spectator speech would ride a real net. Refuse it loudly instead: a dead spectator
		// radio is diagnosable, a dead player talking on a living net is the failure this system
		// exists to prevent.
		if (radio && radio.TransceiversCount() > 0)
		{
			BaseTransceiver checkTrx = radio.GetTransceiver(0);
			if (checkTrx && !EC29_CoexistenceGuard.EC29_IsSpecialNet(checkTrx))
			{
				PrintFormat("[EC29-DBG][SpecVon] Registered radio is NOT a special net (freq %1 kHz, range %2 m) - refusing it; spectator radio stays dead rather than keying a living net", checkTrx.GetFrequency(), checkTrx.GetRange(), level: LogLevel.WARNING);
				radio = null;
			}
		}

		// Change detection gates the deferred re-assert. Callers re-register on every talk press
		// and net toggle (the per-use radio self-heal), and an unconditional queue here would land
		// a tier re-select one frame into a transmission that just swapped to the quiet tier. With
		// unchanged handles this call is mute-sync only; the re-assert queues only when something
		// actually changed (body arrival, a late-streamed radio or tier resolving).
		bool changed = (body != m_SpectatorBody || normalTier != m_NormalTier || quietTier != m_QuietTier || radio != m_Radio);

		m_SpectatorBody = body;
		m_NormalTier = normalTier;
		m_QuietTier = quietTier;
		m_Radio = radio;

		if (!body)
			return;

		if (changed && EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][SpecVon] Spectator body registered (normalTier=%1 quietTier=%2 radio=%3)", normalTier != null, quietTier != null, radio != null);

		ApplyMuteSync();

		if (changed)
			QueueReassert();
	}

	//------------------------------------------------------------------------------------------------
	//! SPECTATOR RADIO PUSH-TO-TALK. Direct speech is locked for the whole spectate session, so
	//! this is the ONLY way a spectator can be heard - by other spectators, never by the living.
	//! Drives SCR_VoNComponent DIRECTLY (SetTransmitRadio / SetCommMethod / SetCapture),
	//! bypassing SCR_VONEntryRadio and the VON menu entirely - a spectator has no selectable
	//! channel, so waiting for vanilla to offer one would leave them mute.
	//!
	//! VERBATIM-INVARIANT PATH (absorbed from the spectator mod, which learned each rule in the
	//! field): refuse-start-never-refuse-stop; stop capture on BOTH tiers at key-up and BEFORE
	//! the tier switch; swap to the quiet tier BEFORE capture starts; GetTransceiver(0) with NO
	//! typed cast; and the read-back guard that forces capture OFF if the engine would fall back
	//! to CM_DIRECT. The original CM_DIRECT cast-null root cause was never established, so
	//! "cleanup" of this path is banned.
	void SetTransmitting(bool talk)
	{
		if (!talk)
		{
			// Cleared FIRST, so the inline re-assert below is free to restore the normal tier.
			m_bTransmitting = false;

			// STOP CAPTURE ON EVERY TIER, not just the active one.
			//
			// Key-down captures on the QUIET tier, but by key-up the active component can have
			// been re-pointed at the NORMAL one, so stopping only that left the quiet component
			// transmitting forever - a stuck microphone. Switching the controller back to normal
			// then restored full audible range on a capture that was still running, which is why
			// a spectator was heard at range and could not stop.
			//
			// Both are stopped explicitly, and BEFORE the tier switch, so nothing is ever
			// capturing while the active component changes underneath it. Deliberately NOT gated
			// on who is controlled or whether spectate is still on: a hot mic must always be
			// closable, and stopping capture on a null handle is a no-op.
			if (m_QuietTier)
				m_QuietTier.SetCapture(false);

			if (m_NormalTier)
				m_NormalTier.SetCapture(false);

			// Back to the normal tier the moment the key is released, or the spectator stays
			// deaf. Unconditional: this must run even if the quiet tier was never selected.
			// NO-OPS DURING ExitSpectate, by design - the flag flips first there, so the capture
			// stop above still runs (the part that matters) while the tier restore is skipped:
			// there is no spectator left to be deaf, and the component it would select belongs
			// to a ghost that is about to be deleted. Vanilla re-resolves the VoN component on
			// the next controlled-entity change regardless.
			//
			// NO AUTO-EXIT FROM HERE. The synchronous stop path runs in half-armed windows - the
			// entry-time preference feed, a fast re-entry while the OLD ghost is still under
			// control - where "no registered body" is not evidence of a missed exit. Only the
			// deferred, entity-change-driven pass may auto-exit.
			ReassertSpectatorVoN(false);
			return;
		}

		// THE OFF-SWITCH REFUSES TO START, BUT NEVER REFUSES TO STOP. Guarding both directions
		// would mean toggling the net off mid-sentence could leave a capture running with no way
		// to end it - the stuck-microphone failure again by another route.
		if (!m_bNetEnabled)
			return;

		// Only ever transmits FROM the registered ghost. If control is anywhere else - the
		// corpse before the body lands, a real character after a respawn - this does nothing, so
		// the key cannot broadcast from a living player's radio.
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		if (!m_SpectatorBody || pc.GetControlledEntity() != m_SpectatorBody)
			return;

		SCR_VoNComponent von = m_NormalTier;
		if (!von)
			return;

		// SWAP TO THE SILENT TIER BEFORE TRANSMITTING.
		//
		// Talking on the radio also speaks ALOUD LOCALLY - vanilla behaviour, and the reason a
		// spectator was audible to living players standing near their body. It cannot be
		// refused, only made inaudible, so the speaking range is dropped for exactly the
		// duration of the key press. Doing it before capture starts means the very first packet
		// is already quiet.
		//
		// Absent quiet tier = old behaviour rather than a broken one: transmission still works,
		// it is simply audible locally.
		// STRICT: no quiet tier means NO TRANSMISSION, not a louder one. The old shape warned and
		// transmitted on the normal tier - full local audibility to 40 m, fading to 68 - which is
		// the exact outcome the quiet tier exists to prevent, handed out on the failure path. Same
		// trade already accepted for the strict tier resolve: dead radio beats audible spectator,
		// and the missing tier is warned at registration.
		if (!m_QuietTier)
		{
			Print("[EC29-DBG][SpecVon] transmit refused - no quiet tier registered, staying silent rather than speaking at audible range", LogLevel.WARNING);
			return;
		}

		SCR_VONController quietVon = EC29_GetLocalVonController();
		if (quietVon)
			quietVon.EC29_SelectVonComponent(m_QuietTier);
		else
			Print("[EC29-DBG][SpecVon] tier swap SKIPPED - no VON controller", LogLevel.WARNING);

		// Capture on the tier that is now active, so the two cannot disagree.
		von = m_QuietTier;

		if (!m_Radio || m_Radio.TransceiversCount() == 0)
			return;

		// NO CAST. The signature is SetTransmitRadio(BaseTransceiver), and
		// BaseRadioComponent.GetTransceiver already returns exactly that. The absorbed
		// implementation once used RadioTransceiver.Cast(...) here, which silently produced null
		// on this radio and left the engine complaining:
		//   "VoNComponent: CommMethod is CM_SQUAD_RADIO without any radio assigned. Fallbacking
		//    to CM_DIRECT"
		// - a fallback that would have made a spectator audible to LIVING players. WHY THE CAST
		// FAILED WAS NEVER ESTABLISHED (the prefab declares RadioTransceiver, so type mismatch
		// cannot have been it). Do not reintroduce the cast on the strength of the prefab
		// looking compatible - it looked compatible then too, and the call needs no cast either
		// way.
		//
		// NO FORCED UNMUTE HERE. An unconditional unmute would silently override the spectator's
		// own off-switch the moment they pressed talk - a toggle that undoes itself is worse
		// than no toggle. The mute state is owned by SetReceiveEnabled/ApplyMuteSync;
		// transmitting must respect it, not fight it.
		BaseTransceiver trx = m_Radio.GetTransceiver(0);
		if (!trx)
			return;

		von.SetTransmitRadio(trx);
		von.SetCommMethod(ECommMethod.SQUAD_RADIO);

		// VERIFIED BEFORE CAPTURING, and this is a safety property rather than tidiness.
		//
		// The engine's response to SQUAD_RADIO with no radio assigned is to FALL BACK TO DIRECT,
		// which would make a dead spectator audible to the living players standing around their
		// invisible body - the single worst outcome this system can produce, and one that looks
		// completely normal from the spectator's own side. There is no way to switch that
		// fallback off, so the guard is to never START. Read back through the engine's own
		// getters rather than trusting the setters, because the null-transceiver case reached
		// exactly this point once already.
		if (!von.GetTransmitRadio() || von.GetCommMethod() != ECommMethod.SQUAD_RADIO)
		{
			// Force capture OFF rather than merely declining to switch it on. Anything that
			// started a capture by another route - a stray vanilla bind, an earlier press that
			// half succeeded - would otherwise keep running on CM_DIRECT, which is a live
			// microphone audible to the players being spectated.
			von.SetCapture(false);

			Print("[EC29-DBG][SpecVon] radio transmit refused - no transceiver assigned, staying silent rather than falling back to direct", LogLevel.WARNING);
			return;
		}

		von.SetCapture(true);

		// Set only on the one successful exit, after capture actually opened - every refusal
		// above leaves it false, so the flag can never claim a transmission that was never
		// started.
		m_bTransmitting = true;
	}

	//------------------------------------------------------------------------------------------------
	//! The spectator net, both directions, in one switch. MUTE COVERS THE INCOMING HALF - it is
	//! vanilla's own "do not hear this channel" control (SCR_VONEntryRadio.ToggleMuteEntry walks
	//! SetMuteState behind the mute icon), so the HUD radio icon already shows the state for
	//! free. THE OUTGOING HALF IS NOT LEFT TO MUTE: SetTransmitting refuses to start while the
	//! net is off, in script, so transmission is blocked whether or not mute happens to stop it
	//! as well. That independence is deliberate - an earlier account credited transmit-blocking
	//! to mute on the strength of a field failure that was really the CM_DIRECT fallback. Do not
	//! rebuild the transmit block on top of mute.
	//!
	//! STOPS ANY TRANSMISSION IN PROGRESS FIRST when disabling, and that ordering is not
	//! incidental: muting a transceiver mid-capture is how a microphone gets stuck open with no
	//! key left to release it.
	void SetReceiveEnabled(bool enabled)
	{
		m_bNetEnabled = enabled;

		if (!enabled)
			SetTransmitting(false);

		ApplyMuteSync();
	}

	//------------------------------------------------------------------------------------------------
	bool IsReceiveEnabled()
	{
		return m_bNetEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! Spectate ends. Idempotent, safe from any teardown path, and the ORDER is the verified
	//! invariant order of the absorbed implementation:
	//!   1. flag flips FIRST, so no queued or re-entrant call re-applies spectator state;
	//!   2. stop capture on every tier (a capture left running on a body about to be deleted
	//!      would be a hot mic nobody can switch off) - the tier restore inside no-ops on the
	//!      already-flipped flag;
	//!   3. cancel the pending deferred re-assert, or the restore below is undone one frame
	//!      later (the re-assert re-applies the direct-speech kill, which it must while
	//!      spectating - but on the respawn exit path it lands on the far side of this teardown
	//!      with nothing left to restore speech afterwards);
	//!   4. give local speech back - NOT optional and NOT skippable on any exit path:
	//!      SCR_VONController lives on the player controller, which outlives this life, so a
	//!      spectator left locked stays mute for the rest of the session.
	void ExitSpectate()
	{
		if (!m_bSpectating)
			return;

		m_bSpectating = false;

		SetTransmitting(false);

		Unsubscribe();
		GetGame().GetCallqueue().Remove(ReassertSpectatorVoN);

		ApplyDirectSpeechLock(false);

		m_SpectatorBody = null;
		m_NormalTier = null;
		m_QuietTier = null;
		m_Radio = null;

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][SpecVon] ExitSpectate - direct speech restored, spectator voice state cleared", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! The service watches controlled-entity changes ITSELF rather than trusting the caller to
	//! forward them - the re-kill and the self-healing auto-exit must not depend on the caller
	//! being healthy, because a missed exit path in the caller is exactly the failure they cover.
	protected void Subscribe()
	{
		if (m_bSubscribed)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
		{
			Print("[EC29-DBG][SpecVon] No local player controller at EnterSpectate - controlled-entity watch unavailable", LogLevel.WARNING);
			return;
		}

		pc.m_OnControlledEntityChanged.Insert(OnControlledEntityChanged);
		m_bSubscribed = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Unsubscribe()
	{
		if (!m_bSubscribed)
			return;

		m_bSubscribed = false;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.m_OnControlledEntityChanged.Remove(OnControlledEntityChanged);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		if (!m_bSpectating)
			return;

		// Receiving needs the radio's mute state re-applied from the moment the ghost is (back)
		// under control, not from the first time someone presses the talk key.
		if (to && to == m_SpectatorBody)
			ApplyMuteSync();

		// NEXT FRAME, not now - SCR_VONController subscribes to this same invoker and re-resolves
		// its component itself:
		//   if (!m_VONComp || !m_VONComp.IsLocalActiveEditor())
		//       SetVONComponent(SCR_VoNComponent.Cast(to.FindComponent(SCR_VoNComponent)));
		// Subscriber order is not ours to control, so setting ours inline can simply be
		// overwritten a moment later. Worse, that FindComponent asks for the BASE type and
		// FindComponent returns DISABLED components - so vanilla can land on the inherited
		// component the body prefab switched off, and the spectator ends up on a dead component.
		// One frame later the controller has finished, and ours is the last word.
		QueueReassert();
	}

	//------------------------------------------------------------------------------------------------
	protected void QueueReassert()
	{
		GetGame().GetCallqueue().CallLater(ReassertSpectatorVoN, 0, false, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Deferred re-assert of everything vanilla's per-entity-change rebuild discards.
	//!
	//! DEFERRED CALL, SO IT MUST RE-CHECK THAT SPECTATE IS STILL ON, and that guard is the first
	//! line for a specific reason: on the respawn exit path the invoker queues this while
	//! spectate is still on, and the teardown then runs BEFORE the queued call lands - so a
	//! stale call used to re-apply the direct-speech kill after the restore, permanently,
	//! because SCR_VONController outlives the life. ExitSpectate also drops any queued call;
	//! this guard covers every other path that could ever queue one.
	//! allowAutoExit is true only from the deferred, entity-change-driven queue. The synchronous
	//! stop-path call passes false: it runs inside half-armed windows (entry-time preference
	//! feed, fast re-entry while the old ghost is still controlled) where a null registered body
	//! plus an ALIVE ghost would satisfy the exit test for the wrong reason - the ghost body IS
	//! ECharacterLifeState.ALIVE. Two field-found bugs shared exactly that shape.
	protected void ReassertSpectatorVoN(bool allowAutoExit)
	{
		if (!m_bSpectating)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();

		// SELF-HEALING AUTO-EXIT: a LIVE character under local control while this service still
		// thinks it is spectating means the caller's exit path was missed (crash teardown, a
		// future refactor dropping a Leave call). Running the spectator state forward from here
		// would re-lock direct speech on a living player - the session-long-mute failure this
		// architecture exists to rule out - so the service exits spectator voice instead and
		// says so. Corpses and null transitions are NOT exits: the direct-speech kill must hold
		// through them (a dead player's entity swaps several times on the way into spectate).
		if (allowAutoExit && controlled && controlled != m_SpectatorBody && IsAliveCharacter(controlled))
		{
			Print("[EC29-DBG][SpecVon] Live character under control while spectator voice active - auto-exiting spectator voice (missed ExitSpectate upstream?)", LogLevel.WARNING);
			ExitSpectate();
			return;
		}

		// RE-APPLY THE DIRECT-SPEECH KILL HERE, not only in EnterSpectate.
		// SCR_VONController.OnControlledEntityChanged calls ResetVON() and clears its encryption
		// key before re-resolving the component. EnterSpectate runs BEFORE the body exists, so
		// the lock applied there is set on state that this rebuild then discards - which would
		// leave a spectator able to talk on direct at full range, the exact leak this system
		// exists to prevent. Applied on the same deferred pass as the component swap, so it
		// lands after vanilla has finished rebuilding rather than racing it.
		ApplyDirectSpeechLock(true);

		if (!m_SpectatorBody || controlled != m_SpectatorBody)
			return;

		SCR_VONController von = EC29_GetLocalVonController();
		if (!von)
			return;

		// MID-TRANSMISSION, THE QUIET TIER IS THE ONLY LEGAL SELECTION. Local audible range
		// follows the controller's active component, not the capturing one - selecting the
		// normal tier here while a capture is running is full-volume local audio to 40 m for the
		// rest of the hold, aimed at exactly the living players the system hides from. A deferred
		// call CAN land mid-hold: a changed-handle registration at key-down (late-streaming radio
		// healing on the press) queues one. The direct-speech re-lock above still ran
		// unconditionally; only the tier choice branches.
		if (m_bTransmitting)
		{
			if (m_QuietTier)
				von.EC29_SelectVonComponent(m_QuietTier);
			return;
		}

		if (!m_NormalTier)
			return;

		// Via the modded controller - the underlying vanilla members are protected, and the
		// swap must be atomic. See EC29_SelectVonComponent.
		von.EC29_SelectVonComponent(m_NormalTier);
	}

	//------------------------------------------------------------------------------------------------
	//! EVERY TRANSCEIVER, not just index 0. The shipped radio carries one, but a radio with more
	//! would otherwise be half-muted, which is the sort of thing that reads as an intermittent
	//! bug. Only ever touches the REGISTERED ghost radio - never a living player's.
	protected void ApplyMuteSync()
	{
		if (!m_Radio)
			return;

		bool wantMuted = !m_bNetEnabled;

		int count = m_Radio.TransceiversCount();
		for (int i = 0; i < count; i++)
		{
			BaseTransceiver trx = m_Radio.GetTransceiver(i);
			if (trx && trx.IsMuted() != wantMuted)
				trx.SetMuteState(wantMuted);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyDirectSpeechLock(bool locked)
	{
		SCR_VONController von = EC29_GetLocalVonController();
		if (von)
			von.EC29_SetDirectSpeechTransmitLocked(locked);
	}

	//------------------------------------------------------------------------------------------------
	//! Alive test for the self-heal only. A corpse is a character too, so life state - not
	//! character-ness - is what separates "respawned without an exit" from the normal dead-entity
	//! shuffle on the way into spectate.
	protected bool IsAliveCharacter(IEntity ent)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(ent);
		if (!character)
			return false;

		CharacterControllerComponent cc = character.GetCharacterController();
		if (!cc)
			return false;

		return cc.GetLifeState() == ECharacterLifeState.ALIVE;
	}

	//------------------------------------------------------------------------------------------------
	//! Resolves the LOCAL player's VON controller - VoN is client-side, and this is the same
	//! route SCR_VoNComponent itself uses to find its controller. Returns null before a player
	//! controller exists; callers null-check.
	protected SCR_VONController EC29_GetLocalVonController()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;

		return SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
	}
}
