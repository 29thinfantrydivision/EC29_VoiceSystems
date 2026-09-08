//------------------------------------------------------------------------------------------------
//! EC29_SpectatorVonService - spectator voice, owned by EC29.
//!
//! Whatever runs the spectating (a spectator mod, any camera-driven observer mode) owns LIFECYCLE,
//! INPUT and CAMERA; this service owns VOICE. Voice is anchored to the player's own editor manager
//! entity WITHOUT opening the editor: every player has one, and it already carries a radio, a VoN
//! component and network movement. EC29_EditorManager.et (routed to every player by
//! EC29_EditorSettingsEntity) adds the spectator transceiver, the ear and the quiet transmit tier.
//! The caller uses six methods - EnterSpectate, FollowCamera, SetTransmitting, SetReceiveEnabled,
//! ExitSpectate, IsSpectating - and passes nothing but its camera. No character is involved here.
//!
//! FIELD RULES (2026-09-05, Workbench host + Peer Tool client; each one cost a run):
//!  - The engine plays an incoming stream through the FIRST VoN component on the manager, and an
//!    entity orders its components BY CLASS NAME. That first component's own audible range gates
//!    delivery, so a near-silent tier sitting in front of the real one makes the spectator deaf no
//!    matter which component is connected, selected or listed first in the prefab (cost: a full
//!    session of runs, 2026-09-05). EC29_VoNSpectatorLoud exists to own that slot - see
//!    EC29_SpectatorVonTiers.c. It must also be ConnectEditorToVoNSystem'd on the receiving client.
//!  - A component connected as an editor gets SQUAD_RADIO rewritten to GAME_MASTER_RADIO by the
//!    engine; both are radio paths. Transmit goes through the QUIET tier so the local emission
//!    every radio transmission carries stays inaudible to the living.
//!  - On this path a NON-blank encryption key reaches NOBODY (not even a same-keyed receiver);
//!    a blank key reaches every powered receiver on the frequency. Privacy is the frequency (no
//!    player radio tunes below 30000 kHz) plus receivers: a living player's manager radio is
//!    unpowered, a Game Master's spectator transceiver is muted (EC29_EditorManagerEntity).
//!  - Radio state on a client-owned manager is owner-authoritative as the server sees it: key
//!    and range come from the prefab, power is set by the SERVER (EC29_SpectatorVoiceController).
//!  - The server copy of the manager moves only through the movement interpolator at a capped
//!    speed (a teleport left it 13 km behind; interpolation off parks it at the origin), so the
//!    owner snaps it through the server per 25 m of camera travel.
//!  - The engine's "is this an active editor" question is answered from a flag replicated on the
//!    manager itself (EC29_EditorManagerEntity) - remote controllers do not exist on clients.
//!  - The editor channel (frequency 0) ignores keys and frequency and is shared with real GMs;
//!    it and the faction channels are muted for spectators. The spectator net is NOT one of them:
//!    it is an ordinary RadioTransceiver we add to the manager's radio, and SpectatorTransceiver
//!    finds it by skipping every EditorTransceiver and EditorFactionTransceiver. Range is 5000 m,
//!    vanilla's own maximum (the large transmitter tower); no player radio exceeds 2000 m.
//!
//! Owned by EC29_RadioState (world-scoped, rebuilt on world change): no state here survives a
//! scenario change or a Workbench game reload. Client-side by nature.
//------------------------------------------------------------------------------------------------
class EC29_SpectatorVonService
{
	//! THE NET IS SEPARATED BY FREQUENCY, NOT BY MUTING. The transceiver ships PARKED on 28000 and is
	//! tuned to 29000 only while spectating, then parked again on the way out. Parked is the prefab
	//! default, so a player who never spectates - and a Game Master, whose editor Open() builds a VON
	//! entry for every transceiver whether we like it or not - is off the net with no code having to
	//! run. Both ends of the band sit below 30000, so no player radio reaches either and EC29's own
	//! special-net rules still apply to both. SetTransceiverFrequency is the one radio setter the
	//! engine documents as syncing to the server (range and encryption key demonstrably do not).
	static const int EC29_SPECTATOR_NET_KHZ = 29000;
	static const int EC29_SPECTATOR_PARK_KHZ = 28000;
	protected static const string EC29_LISTENING_VAR  = "EC29_SpectatorListening";
	protected static const string EC29_LISTENING_CONF = "{33A27275C95E0302}Sounds/VON/EC29_LocalVariables_VON.conf";

	protected static const float SNAP_DIST_M = 25;
	protected static const float SNAP_MIN_MS = 200;
	protected static const float SNAP_LOG_MS = 2000;
	protected static const int TUNE_VERIFY_MS = 1000;
	protected static const int POWER_RETRY_MS = 1000;
	protected static const int POWER_RETRY_MAX = 5;

	protected bool m_bSpectating;
	protected bool m_bNetEnabled = true;
	protected bool m_bSubscribed;
	protected bool m_bTransmitting;
	protected bool m_bListeningVarSet;

	protected vector m_vLastSnapPos;
	protected bool m_bHasSnapPos;
	protected float m_fLastSnapMs;
	protected float m_fLastSnapLogMs;

	//------------------------------------------------------------------------------------------------
	//! Gate for the vanilla VON action blocks in EC29_VON_VONController: a spectator has no vanilla
	//! voice, radio or direct.
	static bool EC29_ShouldBlockVanillaVonActions()
	{
		return EC29_RadioState.GetInstance().SpectatorVon().IsBlockingVanillaVonActions();
	}

	//------------------------------------------------------------------------------------------------
	bool IsBlockingVanillaVonActions()
	{
		return m_bSpectating;
	}

	//------------------------------------------------------------------------------------------------
	bool IsSpectating()
	{
		return m_bSpectating;
	}

	//------------------------------------------------------------------------------------------------
	bool IsReceiveEnabled()
	{
		return m_bNetEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! The spectator transceiver on a manager radio: the one that is neither the editor channel
	//! nor a faction channel. Null for a vanilla manager or a null radio.
	static BaseTransceiver SpectatorTransceiver(BaseRadioComponent radio)
	{
		if (!radio)
			return null;

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			BaseTransceiver trx = radio.GetTransceiver(i);
			if (!trx || trx.IsInherited(EditorTransceiver) || trx.IsInherited(EditorFactionTransceiver))
				continue;

			return trx;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Wires the manager the way SCR_EditorManagerEntity.Open() wires it for voice, in Open()'s
	//! order, minus the editor: gadget init, connect, select, lock direct speech, movement sync.
	void EnterSpectate()
	{
		if (m_bSpectating)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		SCR_VONController ctl = EC29_GetLocalVonController();
		if (!pc || !mgr || !ctl)
		{
			Print("[EC29-DBG][SpecVon] EnterSpectate: no player controller, editor manager or VON controller - spectator voice unavailable", LogLevel.WARNING);
			return;
		}

		SCR_VoNComponent hearing = HearingComponent(mgr);
		if (!hearing)
		{
			Print("[EC29-DBG][SpecVon] EnterSpectate: editor manager has no SCR_VoNComponent - spectator voice unavailable", LogLevel.WARNING);
			return;
		}

		SCR_VoNComponent quiet = QuietTier(mgr);
		if (!quiet)
			Print("[EC29-DBG][SpecVon] EnterSpectate: manager has no quiet transmit tier - EC29_EditorManager.et not in use? Transmit would be audible to the living", LogLevel.WARNING);

		SCR_RadioComponent gadget = SCR_RadioComponent.Cast(mgr.FindComponent(SCR_RadioComponent));
		if (gadget)
			gadget.OnPostInit(mgr);

		BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));
		BaseTransceiver net = SpectatorTransceiver(radio);
		if (!net)
			Print("[EC29-DBG][SpecVon] EnterSpectate: manager radio has no spectator transceiver - EC29_EditorManager.et not in use? Push-to-talk will be dead", LogLevel.WARNING);

		EC29_TuneNet(radio, net, EC29_SPECTATOR_NET_KHZ);

		// Spectators hear ONLY the spectator net: the editor channel is GM chatter, the faction
		// channels are the living. Both muted for the session, restored on exit.
		SetOtherChannelsMuted(radio, net, true);

		if (radio && !radio.IsPowered())
			radio.SetPower(true);

		pc.EC29_AskSpectatorVoice(true);

		int pid = pc.GetPlayerId();
		hearing.ConnectEditorToVoNSystem(pid);
		if (quiet)
			quiet.ConnectEditorToVoNSystem(pid);

		ctl.EC29_SelectVonComponent(hearing);
		ctl.EC29_SetDirectSpeechTransmitLocked(true);
		mgr.EnableCameraNwkSimulation(true);

		m_bSpectating = true;
		m_bNetEnabled = true; // per-entry default; the caller re-feeds a remembered preference via SetReceiveEnabled
		m_bTransmitting = false;
		m_bHasSnapPos = false;

		Subscribe();
		GetGame().GetCallqueue().CallLater(RetryPower, POWER_RETRY_MS, false, 0);

		SyncListeningVar();

		ApplyMuteSync();

		if (EC29_Debug.VERBOSE)
		{
			int freq = -1;
			float range = -1;
			if (net)
			{
				freq = net.GetFrequency();
				range = net.GetRange();
			}
			// The manager id is here so a reconnecting player's stale-vs-fresh manager is visible
			// against the server's own "pid N spectator-voice" line.
			PrintFormat("[EC29-DBG][SpecVon] EnterSpectate pid=%1 mgr=%2 ear=%3 quietTier=%4 net(freq=%5 range=%6 muted=%7) powered=%8", pid, EC29_RplIdOf(mgr), hearing.Type(), quiet != null, freq, range, net != null && net.IsMuted(), radio != null && radio.IsPowered());
			PrintFormat("[EC29-DBG][SpecVon] manager radio transceivers:%1", EC29_DumpTransceivers(radio));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Per frame from the caller's camera tick: what SCR_CameraEditorComponent.EOnFrame does for a
	//! Game Master, plus the server-side snap the movement interpolator cannot provide.
	void FollowCamera(IEntity camera)
	{
		if (!m_bSpectating || !camera)
			return;

		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (!mgr)
			return;

		vector mat[4];
		camera.GetWorldTransform(mat);
		mgr.SetWorldTransform(mat);

		if (m_bHasSnapPos && vector.DistanceSq(mat[3], m_vLastSnapPos) < SNAP_DIST_M * SNAP_DIST_M)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();
		if (m_bHasSnapPos && now - m_fLastSnapMs < SNAP_MIN_MS)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

		pc.EC29_AskManagerSnap(mat[3]);
		m_vLastSnapPos = mat[3];
		m_bHasSnapPos = true;
		m_fLastSnapMs = now;

		if (EC29_Debug.VERBOSE && now - m_fLastSnapLogMs >= SNAP_LOG_MS)
		{
			m_fLastSnapLogMs = now;
			PrintFormat("[EC29-DBG][SpecVon] snap sent cam=%1 mgrLocal=%2", mat[3], mgr.GetOrigin());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! SPECTATOR PUSH-TO-TALK. Refuse-start-never-refuse-stop; stop capture on BOTH tiers before any
	//! tier switch; swap to the quiet tier BEFORE capture starts; read back through the engine's
	//! own getters and never capture on a CM_DIRECT fallback (that would be a dead spectator
	//! audible to the living players around their camera).
	void SetTransmitting(bool talk)
	{
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		SCR_VONController ctl = EC29_GetLocalVonController();
		SCR_VoNComponent hearing;
		SCR_VoNComponent quiet;
		if (mgr)
		{
			hearing = HearingComponent(mgr);
			quiet = QuietTier(mgr);
		}

		if (!talk)
		{
			m_bTransmitting = false;

			if (quiet)
				quiet.SetCapture(false);

			if (hearing)
				hearing.SetCapture(false);

			if (m_bSpectating && ctl && hearing)
				ctl.EC29_SelectVonComponent(hearing);

			return;
		}

		if (!m_bSpectating || !m_bNetEnabled || m_bTransmitting || !hearing || !ctl)
		{
			// The one refusal with no trace of its own: a key-up that never became a key-down looks
			// exactly like a listener who heard nothing, so say which condition swallowed it.
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][SpecVon] TX refused - spectating=%1 netEnabled=%2 alreadyTx=%3 ear=%4 ctl=%5", m_bSpectating, m_bNetEnabled, m_bTransmitting, hearing != null, ctl != null);

			return;
		}

		BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));
		BaseTransceiver net = SpectatorTransceiver(radio);
		if (!net)
		{
			Print("[EC29-DBG][SpecVon] transmit refused - no spectator transceiver on the manager radio", LogLevel.WARNING);
			return;
		}

		SCR_VoNComponent von = hearing;
		if (quiet)
		{
			ctl.EC29_SelectVonComponent(quiet);
			von = quiet;
		}

		von.SetCommMethod(ECommMethod.SQUAD_RADIO);
		von.SetTransmitRadio(net);

		ECommMethod method = von.GetCommMethod();
		if (!von.GetTransmitRadio() || (method != ECommMethod.SQUAD_RADIO && method != ECommMethod.GAME_MASTER_RADIO))
		{
			von.SetCapture(false);
			ctl.EC29_SelectVonComponent(hearing);
			PrintFormat("[EC29-DBG][SpecVon] transmit refused - read-back failed (radio=%1 method=%2), staying silent rather than falling back to direct", von.GetTransmitRadio() != null, method, level: LogLevel.WARNING);
			return;
		}

		m_bTransmitting = von.SetCapture(true);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][SpecVon] TX start capture=%1 freq=%2 muted=%3 powered=%4", m_bTransmitting, net.GetFrequency(), net.IsMuted(), radio != null && radio.IsPowered());
	}

	//------------------------------------------------------------------------------------------------
	//! The spectator net, both directions. Stops any transmission in progress BEFORE muting: a
	//! transceiver muted mid-capture is a microphone stuck open with no key left to release it.
	void SetReceiveEnabled(bool enabled)
	{
		m_bNetEnabled = enabled;

		if (!enabled)
			SetTransmitting(false);

		ApplyMuteSync();
	}

	//------------------------------------------------------------------------------------------------
	//! Mirrors Close(): stop capture, disconnect, hand the controller back to the character, and
	//! give local speech back UNCONDITIONALLY - SCR_VONController outlives this life.
	void ExitSpectate()
	{
		if (!m_bSpectating)
			return;

		m_bSpectating = false;
		SetTransmitting(false);
		Unsubscribe();
		GetGame().GetCallqueue().Remove(Reassert);
		GetGame().GetCallqueue().Remove(RetryPower);

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		SCR_VONController ctl = EC29_GetLocalVonController();

		if (mgr)
		{
			mgr.EnableCameraNwkSimulation(false);

			SCR_VoNComponent hearing = HearingComponent(mgr);
			if (hearing)
				hearing.DisconnectEditorFromVoNSystem();

			SCR_VoNComponent quiet = QuietTier(mgr);
			if (quiet)
				quiet.DisconnectEditorFromVoNSystem();

			BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));

			// Session mutes off (a Game Master's editor channels come back), then three independent
			// ways of being off the net: parked back on 28000, muted, unpowered. Only the park is
			// load-bearing; the other two cover a park that never landed. A living player audible to
			// - or hearing - the dead is the one failure this system exists to prevent, so it does
			// not rest on a single call surviving a crash teardown or a lost round trip. Power is
			// left alone while a real editor is open: vanilla owns it then. Re-entry re-derives all
			// three.
			SetOtherChannelsMuted(radio, null, false);
			BaseTransceiver net = SpectatorTransceiver(radio);
			EC29_TuneNet(radio, net, EC29_SPECTATOR_PARK_KHZ);

			// The mute stays as a second line: a missed park (crash teardown, a lost RPC) would
			// otherwise leave a living player sitting on the net.
			if (net && !net.IsMuted())
				net.SetMuteState(true);

			if (radio && radio.IsPowered() && !mgr.IsOpened())
				radio.SetPower(false);
		}

		if (ctl)
		{
			ctl.EC29_SetDirectSpeechTransmitLocked(false);

			IEntity ent;
			if (pc)
				ent = pc.GetControlledEntity();

			if (ent)
			{
				SCR_VoNComponent charVon = SCR_VoNComponent.Cast(ent.FindComponent(SCR_VoNComponent));
				if (charVon)
					ctl.EC29_SelectVonComponent(charVon);
			}
		}

		if (pc)
			pc.EC29_AskSpectatorVoice(false);

		SyncListeningVar();

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][SpecVon] ExitSpectate mgr=%1 - direct speech restored, manager voice disconnected, net muted and local radio unpowered", EC29_RplIdOf(mgr));
	}

	//------------------------------------------------------------------------------------------------
	//! SPECTATOR NET LOUDNESS, 0..1, on THIS client only.
	//!
	//! Routed through EC29's own per-channel radio volume rather than a second mechanism: the net
	//! is a radio channel, EC29_RadioEarSettings already keeps a volume per transceiver, and the
	//! Channel Control Volume bus that applies it already sits on the path the net takes. So this
	//! is a lookup and a clamp, and the audio graph does the work.
	//!
	//! Purely local, like every other listening preference here - nobody else's mix changes, and
	//! nothing is replicated.
	float GetNetVolume()
	{
		BaseTransceiver net = LocalNetTransceiver();
		if (!net)
			return 1.0;

		return EC29_RadioState.GetInstance().EarSettings().GetVolume(net);
	}

	//------------------------------------------------------------------------------------------------
	//! Returns the volume actually in force afterwards, which is the clamped value - so a caller
	//! stepping past either end can report the real number rather than its own running total.
	float AdjustNetVolume(float delta)
	{
		BaseTransceiver net = LocalNetTransceiver();
		if (!net)
			return 1.0;

		float applied = EC29_RadioState.GetInstance().EarSettings().AdjustVolume(net, delta);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][SpecVon] net volume %1", applied);

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	float SetNetVolume(float volume)
	{
		BaseTransceiver net = LocalNetTransceiver();
		if (!net)
			return 1.0;

		EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
		settings.SetVolume(net, volume);
		return settings.GetVolume(net);
	}

	//------------------------------------------------------------------------------------------------
	//! The local player's own spectator transceiver. Null off a manager, which every caller treats
	//! as "no net to adjust" rather than an error - a player who is not spectating has none.
	protected BaseTransceiver LocalNetTransceiver()
	{
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (!mgr)
			return null;

		return SpectatorTransceiver(BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent)));
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the EC29_SpectatorListening audio variable equal to the spectating state. The variables
	//! conf only exists once the audio system has loaded von.acp for a playing stream, so the set can
	//! fail at EnterSpectate; SCR_VoNComponent.OnReceive calls this again per packet until it sticks.
	void SyncListeningVar()
	{
		if (m_bSpectating == m_bListeningVarSet)
			return;

		float value = 0;
		if (m_bSpectating)
			value = 1;

		if (AudioSystem.SetVariableByName(EC29_LISTENING_VAR, value, EC29_LISTENING_CONF))
		{
			m_bListeningVarSet = m_bSpectating;
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][SpecVon] EC29_SpectatorListening = %1", value);
		}
		else if (!m_bSpectating)
		{
			// conf never loaded during this spectate - nothing to reset
			m_bListeningVarSet = false;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! SCR_VONController re-resolves its component to the controlled entity on this same invoker,
	//! so ours is re-selected one frame later - the last word.
	protected void Subscribe()
	{
		if (m_bSubscribed)
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (!pc)
			return;

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

		// SELF-HEALING AUTO-EXIT. A LIVE character arriving under local control while this service
		// still thinks it is spectating means the caller's exit path was missed (crash teardown, a
		// refactor dropping a Leave call). Running spectator state forward from here would re-lock
		// direct speech on a living player - SCR_VONController outlives the life, so that is a
		// session-long mute. Unambiguous now that no ghost body exists: a spectator controls
		// nothing, and everything on the way in is a corpse or null. Corpses and null are NOT exits.
		if (to && IsAliveCharacter(to))
		{
			Print("[EC29-DBG][SpecVon] Live character under control while spectator voice active - auto-exiting spectator voice (missed ExitSpectate upstream?)", LogLevel.WARNING);
			ExitSpectate();
			return;
		}

		GetGame().GetCallqueue().CallLater(Reassert, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Alive test for the self-heal only: a corpse is a character too, so life state - not
	//! character-ness - is what separates a respawn from the dead-entity shuffle on the way in.
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
	//! Mid-transmission the quiet tier is the only legal selection: local audibility follows the
	//! controller's active component, not the capturing one.
	protected void Reassert()
	{
		if (!m_bSpectating)
			return;

		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		SCR_VONController ctl = EC29_GetLocalVonController();
		if (!mgr || !ctl)
			return;

		SCR_VoNComponent tier;
		if (m_bTransmitting)
			tier = QuietTier(mgr);
		if (!tier)
			tier = HearingComponent(mgr);

		if (tier)
			ctl.EC29_SelectVonComponent(tier);

		ctl.EC29_SetDirectSpeechTransmitLocked(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Editor radio power is gated natively on the owner counting as an active editor, and that
	//! answer depends on the replicated flag - which lands AFTER EnterSpectate on a real client.
	protected void RetryPower(int attempt)
	{
		if (!m_bSpectating)
			return;

		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (!mgr)
			return;

		BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));
		if (!radio)
			return;

		if (!radio.IsPowered())
			radio.SetPower(true);

		if ((!radio.IsPowered() || !mgr.EC29_IsSpectatorVoice()) && attempt < POWER_RETRY_MAX)
		{
			GetGame().GetCallqueue().CallLater(RetryPower, POWER_RETRY_MS, false, attempt + 1);
			return;
		}

		if (!radio.IsPowered())
			Print("[EC29-DBG][SpecVon] manager radio never powered - spectator net dead this session", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Diagnostics: what the manager radio ACTUALLY carries at runtime. The question this answers is
	//! whether declaring Transceivers in a derived prefab appends to the inherited array or replaces
	//! it - if the editor and faction channels are missing here, EC29_EditorManager.et has silently
	//! taken a real Game Master's radio away from them, which no spectator symptom would reveal.
	protected string EC29_DumpTransceivers(BaseRadioComponent radio)
	{
		if (!radio)
			return " <no radio>";

		string dump;
		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			BaseTransceiver trx = radio.GetTransceiver(i);
			if (!trx)
			{
				dump = string.Format("%1 [%2]<null>", dump, i);
				continue;
			}

			string kind = "OTHER";
			if (trx.IsInherited(EditorTransceiver))
				kind = "EDITOR";
			else if (trx.IsInherited(EditorFactionTransceiver))
				kind = "FACTION";

			dump = string.Format("%1 [%2]%3 freq=%4 range=%5 muted=%6", dump, i, kind, trx.GetFrequency(), trx.GetRange(), trx.IsMuted());
		}

		return dump;
	}

	//------------------------------------------------------------------------------------------------
	//! No-op when already there - the setter replicates, so re-sending it every entry is traffic for
	//! nothing. Frequency is read back rather than assumed: on a client-owned manager it is the only
	//! radio property that has ever been observed to stick, and if that stops being true this net
	//! silently stops existing.
	protected void EC29_TuneNet(BaseRadioComponent radio, BaseTransceiver net, int khz)
	{
		if (!radio || !net || net.GetFrequency() == khz)
			return;

		// BaseTransceiver.SetFrequency, NOT BaseRadioComponent.SetTransceiverFrequency. The latter is
		// documented as "set frequency and sync with server" - an owner-to-server request, and on a
		// listen server that request has nothing to loop back to, so it silently no-ops on the host
		// while working fine from a remote client (field-measured 2026-09-07: peer STUCK at 29000,
		// host REFUSED, same code path). SetFrequency is the one the engine documents as supporting
		// "proxies and server".
		net.SetFrequency(khz);

		// The immediate read-back means little either way - the deferred check below is the answer.
		if (EC29_Debug.VERBOSE)
		{
			PrintFormat("[EC29-DBG][SpecVon] net tune asked %1 kHz (immediate read back %2)", khz, net.GetFrequency());
			GetGame().GetCallqueue().CallLater(EC29_VerifyTune, TUNE_VERIFY_MS, false, khz);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_VerifyTune(int khz)
	{
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (!mgr)
			return;

		BaseTransceiver net = SpectatorTransceiver(BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent)));
		if (!net)
			return;

		int now = net.GetFrequency();
		if (now == khz)
			PrintFormat("[EC29-DBG][SpecVon] net tune STUCK at %1 kHz", now, level: LogLevel.NORMAL);
		else
			PrintFormat("[EC29-DBG][SpecVon] net tune REFUSED - asked %1 kHz, still %2 kHz", khz, now, level: LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	//! Receive-mute on the spectator transceiver only; the other channels are the session mutes.
	protected void ApplyMuteSync()
	{
		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (!mgr)
			return;

		BaseRadioComponent radio = BaseRadioComponent.Cast(mgr.FindComponent(BaseRadioComponent));
		BaseTransceiver net = SpectatorTransceiver(radio);
		if (net && net.IsMuted() == m_bNetEnabled)
			net.SetMuteState(!m_bNetEnabled);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetOtherChannelsMuted(BaseRadioComponent radio, BaseTransceiver net, bool muted)
	{
		if (!radio)
			return;

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			BaseTransceiver trx = radio.GetTransceiver(i);
			if (trx && trx != net && trx.IsMuted() != muted)
				trx.SetMuteState(muted);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! The manager's BASE component: the one the engine plays incoming streams through.
	//! The ear: EC29_VoNSpectatorLoud, which sorts first among the manager's VoN components (see
	//! EC29_SpectatorVonTiers.c). Falls back to the exact vanilla base on a manager without it.
	protected SCR_VoNComponent HearingComponent(notnull SCR_EditorManagerEntity mgr)
	{
		SCR_VoNComponent ear = EC29_VoNSpectatorLoud.Cast(mgr.FindComponent(EC29_VoNSpectatorLoud));
		if (ear)
			return ear;

		array<Managed> comps = {};
		mgr.FindComponents(SCR_VoNComponent, comps);
		foreach (Managed comp : comps)
		{
			if (comp.Type() == SCR_VoNComponent)
				return SCR_VoNComponent.Cast(comp);
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_VoNComponent QuietTier(notnull SCR_EditorManagerEntity mgr)
	{
		return EC29_VoNSpectatorQuiet.Cast(mgr.FindComponent(EC29_VoNSpectatorQuiet));
	}

	//------------------------------------------------------------------------------------------------
	//! Diagnostics only - "none" for an unreplicated or null entity.
	protected string EC29_RplIdOf(IEntity ent)
	{
		if (!ent)
			return "none";

		RplComponent rpl = RplComponent.Cast(ent.FindComponent(RplComponent));
		if (!rpl)
			return "none";

		return rpl.Id().AsString();
	}

	//------------------------------------------------------------------------------------------------
	protected SCR_VONController EC29_GetLocalVonController()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return null;

		return SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
	}
}
