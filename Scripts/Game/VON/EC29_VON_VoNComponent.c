modded class SCR_VoNComponent
{
	//! The old direct-speech gain variable. Since 2026-09-12 direct range is per SOURCE - the
	//! transmitting tier component's ACP (see EC29_VoiceTiers.c) - and this variable is no longer
	//! modulated. It still sits on the "Bus Ducking When Many Voices" input in von.acp, so it is
	//! pinned to unity once per world in case a stale value survived a scenario change.
	static const string EC29_VAR_NAME   = "EC29_VonRange";
	static const string EC29_VAR_CONFIG = "{33A27275C95E0302}Sounds/VON/EC29_LocalVariables_VON.conf";

	// Radio path: ear routing / signal quality / jamming / per-channel volume
	// audio variables, refreshed per incoming radio packet.
	protected static const string EC29_EAR_ROUTING_CONFIG = "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf";
	protected static bool s_bEC29RadioVarsChecked;
	protected static bool s_bEC29_EEarRoutingValid;
	protected static bool s_bEC29SignalQualityValid;
	protected static bool s_bEC29JamStrengthValid;
	protected static bool s_bEC29ChannelVolumeValid;

	protected static bool s_bEC29VarChecked;
	protected static ref map<int, SCR_VoNComponent> s_mEC29PlayerVon = new map<int, SCR_VoNComponent>();
	protected static ref map<int, IEntity> s_mEC29PlayerVonEntity = new map<int, IEntity>();

	// Server-side transmit trace throttle (see OnVoNUsed).
	protected static ref map<int, float> s_mEC29DbgLastVonUsedMs = new map<int, float>();
	protected static const int EC29_VONUSED_LOG_MS = 2000;

	// Receive-side dedupe. A character now carries four VoN components (the stock ear plus the
	// three transmit tiers, all this modded class). Whether the engine delivers OnReceive to the
	// first component only or to every one is not documented, so the per-packet bookkeeping
	// below runs once per (speaker, world-time) whichever it is. Vanilla's super still runs on
	// each call - its display update is idempotent.
	protected static ref map<int, float> s_mEC29LastPacketMs = new map<int, float>();
	protected static bool s_bEC29ReceiverTypeLogged;

	// World-lifecycle guard for the static caches above: playerIds and component
	// pointers are world-scoped, statics are not. Weak member nulls with its world;
	// a mismatch clears the caches and re-arms the one-shot audio-variable probes.
	protected static BaseWorld s_EC29OwnerWorld;

	protected static void EC29_CheckWorldReset()
	{
		BaseWorld currentWorld = GetGame().GetWorld();
		if (s_EC29OwnerWorld == currentWorld)
			return;

		if (s_EC29OwnerWorld && EC29_Debug.VERBOSE)
			Print("[EC29-DBG][VoN] World changed - clearing static player/VoN caches and re-arming audio-var probes", LogLevel.NORMAL);

		s_EC29OwnerWorld = currentWorld;
		s_mEC29PlayerVon.Clear();
		s_mEC29PlayerVonEntity.Clear();
		s_mEC29LastPacketMs.Clear();
		s_bEC29VarChecked = false;
		s_bEC29RadioVarsChecked = false;
		s_bEC29ReceiverTypeLogged = false;
	}

	//! Spawn default is WHISPER (issue #11): noise discipline out of the gate, F3
	//! cycles up when needed. The HUD seed in EC29_VON_VoiceRangeDisplay.c must
	//! match this initializer or the icon lies until the first F3 press.
	//!
	//! LIVES ON THE STOCK COMPONENT ONLY. The three tier components on a character are the same
	//! modded class and so carry this field too, but nothing reads or writes theirs: the mode is
	//! read from EC29_VoiceTiers.StockVoN(entity), and the transmit tier is chosen from it on
	//! the speaker's machine (SCR_VONController.EC29_ApplyVoiceTier). Listeners use it for the
	//! overlay label and the visual range gates only - never for audio.
	[RplProp(onRplName: "EC29_OnVoiceRangeReplicated")]
	protected EC29_EVoiceRange m_eEC29VoiceRange = EC29_EVoiceRange.WHISPER;

	//------------------------------------------------------------------------------------------------
	//! RplProp callback - fires on all clients when m_eEC29VoiceRange changes.
	//! Used to push the new mode into the VoN overlay so the WHISPER / YELLING label
	//! refreshes mid-transmission instead of only on the next new transmission.
	//!
	//! On the SPEAKER'S OWN client it is also the safety net for the transmit tier: F3 applied
	//! the tier locally before the request left, so this is normally a no-op, but a mode that
	//! arrives any other way (a server-side set, a rejected request leaving the old value) lands
	//! here and re-selects the tier to match.
	protected void EC29_OnVoiceRangeReplicated()
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoN] CLIENT received replicated voice range: %1", typename.EnumToString(EC29_EVoiceRange, m_eEC29VoiceRange));
		// Can fire from JIP initial-state replication before the local PlayerController
		// exists; vanilla GetDisplay() dereferences GetPlayerController() unguarded.
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		// Engine components expose no owner to script; "is this the local player's stock
		// component" is answered from the controlled entity's side instead.
		if (EC29_VoiceTiers.StockVoN(pc.GetControlledEntity()) == this)
		{
			SCR_VONController ctl = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
			if (ctl)
				ctl.EC29_ApplyVoiceTier(m_eEC29VoiceRange);
		}

		SCR_VonDisplay display = GetDisplay();
		if (display)
			display.EC29_ForceRefreshAllTransmissions();
	}

	//------------------------------------------------------------------------------------------------
	EC29_EVoiceRange EC29_GetVoiceRange()
	{
		return m_eEC29VoiceRange;
	}

	//------------------------------------------------------------------------------------------------
	void EC29_RequestSetVoiceRange(EC29_EVoiceRange range)
	{
		Rpc(EC29_RpcAsk_SetVoiceRange, range);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void EC29_RpcAsk_SetVoiceRange(EC29_EVoiceRange range)
	{
		// Reject out-of-enum values from a tampered client before storing/replicating.
		if (range < EC29_EVoiceRange.WHISPER || range > EC29_EVoiceRange.YELL)
		{
			PrintFormat("[EC29-DBG][VoN] SERVER rejected out-of-range voice mode value %1", range, level: LogLevel.WARNING);
			return;
		}

		if (m_eEC29VoiceRange == range)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoN] SERVER accepted voice range %1 -> replicating to clients", typename.EnumToString(EC29_EVoiceRange, range));
		m_eEC29VoiceRange = range;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Cached component lookup by playerId, validated against the player's CURRENT controlled
	//! entity: after death the corpse keeps its component (and frozen voice mode) alive until
	//! body cleanup, so mere existence of the cached component is not enough - a respawned
	//! player would keep resolving to the corpse and e.g. stay whisper-muted for listeners.
	//! Entries pointing at deleted entities/components null out automatically (weak refs).
	//!
	//! Public/static so the UI overlay (EC29_VonDisplay), the over-head nametag
	//! (EC29_NameTagData) and the HUD icon (EC29_VoiceRangeDisplay) share the cache instead
	//! of re-doing the component scan on every audio packet / frame. The cheap
	//! GetPlayerControlledEntity lookup runs every call; the scan only on entity change.
	//!
	//! Always the STOCK component (exact type), never a transmit tier: the tiers carry no
	//! replicated mode, and this lookup exists to read the mode.
	static SCR_VoNComponent EC29_GetVoNForPlayer(int playerId)
	{
		IEntity ent = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!ent)
		{
			s_mEC29PlayerVon.Remove(playerId);
			s_mEC29PlayerVonEntity.Remove(playerId);
			return null;
		}

		SCR_VoNComponent cached;
		IEntity cachedEnt;
		if (s_mEC29PlayerVon.Find(playerId, cached) && cached
			&& s_mEC29PlayerVonEntity.Find(playerId, cachedEnt) && cachedEnt == ent)
			return cached;

		SCR_VoNComponent fresh = EC29_VoiceTiers.StockVoN(ent);
		if (fresh)
		{
			s_mEC29PlayerVon.Set(playerId, fresh);
			s_mEC29PlayerVonEntity.Set(playerId, ent);
		}
		else
		{
			s_mEC29PlayerVon.Remove(playerId);
			s_mEC29PlayerVonEntity.Remove(playerId);
		}

		return fresh;
	}

	//------------------------------------------------------------------------------------------------
	override protected event void OnReceive(int playerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)
	{
		EC29_CheckWorldReset();

		// Audio variables are a playback concept: a machine with no local
		// player controller (dedicated server, JIP window) has nothing to
		// modulate, and the radio path's terrain raymarch is real CPU there -
		// with no local listener the receiver position degraded to world
		// origin, so every far speaker walked the model across the map per
		// packet. Super is skipped too: vanilla's OnReceive is display-only
		// and its GetDisplay() dereferences GetPlayerController() unguarded,
		// which is a VM exception per packet in exactly this state.
		if (!GetGame().GetPlayerController())
			return;

		if (EC29_IsDuplicatePacket(playerId))
		{
			super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);
			return;
		}

		// Feed the VON activity service BEFORE every policy gate below: a packet that arrived is
		// a player talking, no matter what faction filters, range gating or mute policy do with
		// the audio. The service itself is spectator-scoped and one flag read when it is not -
		// see EC29_VonActivityService.
		EC29_RadioState.GetInstance().VonActivity().EC29_RecordVonPacket(playerId, receiver != null);

		// Spectator audio gate: the variables conf is only resolvable once von.acp is live, which is
		// exactly now - one bool compare per packet after it sticks (see the service).
		EC29_RadioState.GetInstance().SpectatorVon().SyncListeningVar();

		// DIRECT packets (receiver == null) need NO per-packet work any more: the speaker's range
		// is applied by the engine from the transmitting tier's ACP (EC29_VoiceTiers.c). The one
		// thing left is pinning the retired gain variable to unity, once per world. Radio packets
		// still own the ear-routing/quality/jam/volume set.
		if (!receiver)
		{
			EC29_EnsureUnityRangeGain();
		}
		else
		{
			EC29_ApplyRadioAudioVars(playerId, receiver, frequency);
			EC29_TrackIncomingTransmission(receiver, frequency, playerId);
		}

		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);
	}

	//------------------------------------------------------------------------------------------------
	//! True when this speaker's packet was already processed at this world time by another VoN
	//! component on the local character (see s_mEC29LastPacketMs). Also logs, once per world and
	//! only in VERBOSE, which component class the engine delivered the first packet to - the
	//! field answer to "first component only, or all of them".
	protected bool EC29_IsDuplicatePacket(int playerId)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		if (!s_bEC29ReceiverTypeLogged)
		{
			s_bEC29ReceiverTypeLogged = true;
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][VoN] first incoming packet delivered to component class %1", Type());
		}

		float nowMs = world.GetWorldTime();
		float lastMs;
		if (s_mEC29LastPacketMs.Find(playerId, lastMs) && lastMs == nowMs)
			return true;

		s_mEC29LastPacketMs.Set(playerId, nowMs);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! True while the local player spectates through EC29_SpectatorVonService.
	static bool EC29_IsSpectatingListener()
	{
		return EC29_RadioState.GetInstance().SpectatorVon().IsSpectating();
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER-SIDE TRANSMIT TRACE, diagnostics only.
	//!
	//! The engine routes a spectator's voice from GetEditorWorldLocation(senderId) - the SERVER's
	//! copy of that player's editor manager, which for a client-owned manager moves ONLY through
	//! the owner's snap RPC. So the server's idea of where a spectator is speaking from can differ
	//! from the camera, and a receiver falls out of range with nothing to show for it. This prints
	//! the sender's server-side manager position and its distance to every other spectating
	//! manager, so an out-of-range receiver is visible rather than inferred.
	//!
	//! Throttled per sender: the engine raises this per voice packet.
	override protected event void OnVoNUsed(int senderId)
	{
		super.OnVoNUsed(senderId);

		if (!EC29_Debug.VERBOSE)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float now = world.GetWorldTime();
		float last;
		if (s_mEC29DbgLastVonUsedMs.Find(senderId, last) && now - last < EC29_VONUSED_LOG_MS)
			return;

		s_mEC29DbgLastVonUsedMs.Set(senderId, now);

		SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
		if (!core)
			return;

		SCR_EditorManagerEntity sender = core.GetEditorManager(senderId);
		if (!sender)
			return;

		vector from = sender.GetOrigin();

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		array<int> players = {};
		pm.GetPlayers(players);

		string others;
		foreach (int pid : players)
		{
			if (pid == senderId)
				continue;

			SCR_EditorManagerEntity other = core.GetEditorManager(pid);
			if (!other || !other.EC29_IsSpectatorVoice())
				continue;

			// Power is the SERVER-side receiver gate - an unpowered manager is filtered out before
			// anything is sent, so it explains a silent listener that mute (client-side) cannot.
			BaseRadioComponent otherRadio = BaseRadioComponent.Cast(other.FindComponent(BaseRadioComponent));
			// Frequency comes from the SERVER's copy, so it says whether a spectator's retune actually
			// replicated - a client that reads 29000 locally while the server still sees 28000 is on
			// a net of one.
			BaseTransceiver otherNet = EC29_SpectatorVonService.SpectatorTransceiver(otherRadio);
			int otherFreq = -1;
			if (otherNet)
				otherFreq = otherNet.GetFrequency();

			others = string.Format("%1 pid%2@%3(%4m,powered=%5,freq=%6)", others, pid, other.GetOrigin(), Math.Round(vector.Distance(from, other.GetOrigin())), otherRadio != null && otherRadio.IsPowered(), otherFreq);
		}

		PrintFormat("[EC29-DBG][SpecVon] server: VoN used by pid=%1 senderMgr=%2 spectators:%3", senderId, from, others);
	}

	//------------------------------------------------------------------------------------------------
	//! The engine asks this to decide whether an entity is an active editor - on every machine,
	//! for routing and for playback. Vanilla answers "the manager is opened"; a spectating player's
	//! manager is an active editor too, from the flag replicated on the manager itself (plus the
	//! local shortcut, which needs no replication round trip).
	override bool IsEntityActiveEditor(IEntity entity)
	{
		if (super.IsEntityActiveEditor(entity))
			return true;

		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.Cast(entity);
		if (!mgr)
			return false;

		if (mgr == SCR_EditorManagerEntity.GetInstance() && EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			return true;

		return mgr.EC29_IsSpectatorVoice();
	}

	//------------------------------------------------------------------------------------------------
	//! Pins the retired EC29_VonRange variable to 1.0 once per world. Its conf default is already
	//! 1, but the audio system's variables outlive a scenario, so a value written by a pre-tier
	//! build (or a stale scenario) would otherwise keep ducking every direct stream.
	protected void EC29_EnsureUnityRangeGain()
	{
		if (s_bEC29VarChecked)
			return;

		// The conf is only resolvable once von.acp has been loaded for a playing stream; keep
		// trying per packet until the lookup succeeds, then never again this world.
		if (AudioSystem.GetVariableIDByName(EC29_VAR_NAME, EC29_VAR_CONFIG) == -1)
			return;

		s_bEC29VarChecked = true;
		AudioSystem.SetVariableByName(EC29_VAR_NAME, 1.0, EC29_VAR_CONFIG);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoN] '%1' pinned to unity - direct range is per transmit tier now", EC29_VAR_NAME);
	}

	//------------------------------------------------------------------------------------------------
	//! Radio-path audio variables: ear routing, RF signal
	//! quality, jammer strength, per-channel volume. All are global external variables
	//! consumed by the merged von.acp graph; they must be refreshed per incoming packet.
	protected void EC29_ApplyRadioAudioVars(int playerId, BaseTransceiver receiver, int frequency)
	{
		if (EC29_CoexistenceGuard.ShouldYieldRadio())
			return;

		if (!s_bEC29RadioVarsChecked)
		{
			s_bEC29RadioVarsChecked = true;
			s_bEC29_EEarRoutingValid    = (AudioSystem.GetVariableIDByName("EC29_EarRouting", EC29_EAR_ROUTING_CONFIG) != -1);
			s_bEC29SignalQualityValid = (AudioSystem.GetVariableIDByName("EC29_SignalQuality", EC29_EAR_ROUTING_CONFIG) != -1);
			s_bEC29JamStrengthValid   = (AudioSystem.GetVariableIDByName("EC29_JamStrength", EC29_EAR_ROUTING_CONFIG) != -1);
			s_bEC29ChannelVolumeValid = (AudioSystem.GetVariableIDByName("EC29_ChannelVolume", EC29_EAR_ROUTING_CONFIG) != -1);
			EC29_RFPropagationSettings.GetInstance();

			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][Radio] audio var probe: earRouting=%1 signalQuality=%2 jamStrength=%3 channelVolume=%4",
					s_bEC29_EEarRoutingValid, s_bEC29SignalQualityValid, s_bEC29JamStrengthValid, s_bEC29ChannelVolumeValid);
		}

		if (s_bEC29_EEarRoutingValid)
		{
			float earRouting = EC29_GetEarRoutingForTransceiver(receiver);
			AudioSystem.SetVariableByName("EC29_EarRouting", earRouting, EC29_EAR_ROUTING_CONFIG);
		}

		// Both position-based variables need a real listener position; with no
		// controlled entity (dead, deploy screen) the old vector.Zero fallback
		// raymarched from every speaker to world origin per packet. They are
		// written NEUTRAL rather than skipped, or a listener who died inside a
		// jammer would keep jammed static on radio audio for as long as they
		// stay dead (the variables are global and nothing else refreshes them).
		vector receiverPos = vector.Zero;
		bool hasReceiverPos = false;
		PlayerController playerController = GetGame().GetPlayerController();
		if (playerController)
		{
			IEntity receiverEntity = playerController.GetControlledEntity();
			if (receiverEntity)
			{
				receiverPos = receiverEntity.GetOrigin();
				hasReceiverPos = true;
			}
		}

		// Special nets (the spectator net, admin-only nets) are engineered to be clean:
		// running terrain propagation or jammer degradation on them fights the
		// owning mod's audio design, so they get neutral values.
		bool specialNet = EC29_CoexistenceGuard.EC29_IsSpecialNet(receiver);

		if (s_bEC29SignalQualityValid)
		{
			float signalQuality = 1.0;
			if (hasReceiverPos && !specialNet)
				signalQuality = EC29_GetSignalQuality(playerId, frequency, receiverPos);
			AudioSystem.SetVariableByName("EC29_SignalQuality", signalQuality, EC29_EAR_ROUTING_CONFIG);
		}

		if (s_bEC29JamStrengthValid)
		{
			float jamStrength = 1.0;
			if (hasReceiverPos && !specialNet)
				jamStrength = EC29_GetJamStrength(receiverPos);
			AudioSystem.SetVariableByName("EC29_JamStrength", jamStrength, EC29_EAR_ROUTING_CONFIG);
		}

		if (s_bEC29ChannelVolumeValid)
		{
			float channelVolume = EC29_GetChannelVolumeForTransceiver(receiver);
			AudioSystem.SetVariableByName("EC29_ChannelVolume", channelVolume, EC29_EAR_ROUTING_CONFIG);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Voice packets feed EC29_RadioRxSquelch as the fallback squelch trigger (key-state
	//! RPCs are the primary). The squelch singleton owns its own 150ms ticker; both this
	//! path and the key-state RPC path just ensure it is running (single-ticker rule).
	protected void EC29_TrackIncomingTransmission(BaseTransceiver receiver, int frequency, int senderPlayerId)
	{
		if (EC29_CoexistenceGuard.ShouldYieldRadio())
			return;

		PlayerController playerController = GetGame().GetPlayerController();
		if (playerController && playerController.GetPlayerId() == senderPlayerId)
			return;

		EC29_RadioState.GetInstance().Squelch().OnVoicePacket(frequency, receiver);
		EC29_RadioState.GetInstance().Squelch().EnsureTicking();
	}

	protected float EC29_GetEarRoutingForTransceiver(BaseTransceiver transceiver)
	{
		EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
		EC29_EEarRouting routing = settings.GetRouting(transceiver);
		return routing;
	}

	protected float EC29_GetChannelVolumeForTransceiver(BaseTransceiver transceiver)
	{
		EC29_RadioEarSettings settings = EC29_RadioState.GetInstance().EarSettings();
		float volume = settings.GetVolume(transceiver);
		// Apply exponential curve for better volume sensitivity
		return Math.Pow(volume, 2.5);
	}

	protected float EC29_GetSignalQuality(int senderId, int frequencyKHz, vector receiverPos)
	{
		if (!EC29_RFPropagationNetworkComponent.IsRFPropagationEnabled())
			return 1.0;

		IEntity transmitter = GetGame().GetPlayerManager().GetPlayerControlledEntity(senderId);
		if (!transmitter)
			return 1.0;

		vector transmitterPos = transmitter.GetOrigin();

		// Cached per sender (short TTL): this runs per voice packet and the
		// uncached model raymarches up to 200 terrain samples per call.
		EC29_RadioState signalManager = EC29_RadioState.GetInstance();
		return signalManager.GetSignalQualityCached(senderId, transmitterPos, receiverPos, frequencyKHz);
	}

	protected float EC29_GetJamStrength(vector receiverPos)
	{
		EC29_RadioState signalManager = EC29_RadioState.GetInstance();
		float jammerDegradation = signalManager.GetJammerStrength(receiverPos);
		// CAREFUL THIS IS INVERTED!!!!
		return 1.0 - jammerDegradation;
	}
}
