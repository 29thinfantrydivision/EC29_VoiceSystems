modded class SCR_VoNComponent
{
	static const string EC29_VAR_NAME   = "EC29_VonRange";
	static const string EC29_VAR_CONFIG = "{33A27275C95E0302}Sounds/VON/EC29_LocalVariables_VON.conf";

	// Radio path: ear routing / signal quality / jamming / per-channel volume
	// audio variables. Boundary rule: EC29_VonRange gain applies to DIRECT speech
	// falloff, the radio variables apply to the RADIO path - the two sets never
	// touch the same audio variable.
	protected static const string EC29_EAR_ROUTING_CONFIG = "{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf";
	protected static bool s_bEC29RadioVarsChecked;
	protected static bool s_bEC29_EEarRoutingValid;
	protected static bool s_bEC29SignalQualityValid;
	protected static bool s_bEC29JamStrengthValid;
	protected static bool s_bEC29ChannelVolumeValid;

	protected static bool s_bEC29VarValid;
	protected static bool s_bEC29VarChecked;
	protected static ref map<int, SCR_VoNComponent> s_mEC29PlayerVon = new map<int, SCR_VoNComponent>();
	protected static ref map<int, IEntity> s_mEC29PlayerVonEntity = new map<int, IEntity>();

	// Debug: last gain logged per speaker so OnReceive logging doesn't spam every voice packet.
	protected static ref map<int, float> s_mEC29DbgLastGain = new map<int, float>();

	// One global gain variable serves every concurrently playing direct stream
	// (last-writer-wins). The loudest recently-active stream owns it: while a
	// nearby speaker is talking, a far speaker's near-floor writes - including
	// the proximity component every radio transmission produces - are held
	// back instead of chopping the nearby voice to silence. The hold window
	// bounds how long a stale louder value can linger once its speaker stops.
	protected static float s_fEC29ActiveGain;
	protected static float s_fEC29ActiveGainSetMs;
	protected static const int EC29_GAIN_HOLD_MS = 400;

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
		s_mEC29DbgLastGain.Clear();
		s_bEC29VarChecked = false;
		s_bEC29RadioVarsChecked = false;
		s_fEC29ActiveGain = 0;
		s_fEC29ActiveGainSetMs = 0;
	}

	//! Spawn default is WHISPER (issue #11): noise discipline out of the gate, F3
	//! cycles up when needed. The HUD seed in EC29_VON_VoiceRangeDisplay.c must
	//! match this initializer or the icon lies until the first F3 press.
	[RplProp(onRplName: "EC29_OnVoiceRangeReplicated")]
	protected EC29_EVoiceRange m_eEC29VoiceRange = EC29_EVoiceRange.WHISPER;

	//------------------------------------------------------------------------------------------------
	//! RplProp callback - fires on all clients when m_eEC29VoiceRange changes.
	//! Used to push the new mode into the VoN overlay so the WHISPER / YELLING label
	//! refreshes mid-transmission instead of only on the next new transmission.
	protected void EC29_OnVoiceRangeReplicated()
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VoN] CLIENT received replicated voice range: %1", typename.EnumToString(EC29_EVoiceRange, m_eEC29VoiceRange));
		// Can fire from JIP initial-state replication before the local PlayerController
		// exists; vanilla GetDisplay() dereferences GetPlayerController() unguarded.
		if (!GetGame().GetPlayerController())
			return;

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
	//! of re-doing FindComponent on every audio packet / frame. The cheap
	//! GetPlayerControlledEntity lookup runs every call; FindComponent only on entity change.
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

		SCR_VoNComponent fresh = SCR_VoNComponent.Cast(ent.FindComponent(SCR_VoNComponent));
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

		// Packet-type gate keeps the two systems from stomping each other's global
		// audio variables: direct packets (receiver == null) own EC29_VonRange,
		// radio packets own the ear-routing/quality/jam/volume set. Without the
		// gate, a far-away radio speaker drags the direct-falloff gain to the
		// floor mid-conversation, and a nearby direct speaker resets ear routing
		// to CENTER mid-radio-stream.
		if (!receiver)
		{
			// Editor and spectate senders (GM camera, spectator systems, deploy
			// screen) emit direct packets with no meaningful sender position.
			// The display layer already special-cases them; the gain path must
			// too, or their packets seize the shared falloff variable (org-mod
			// audit: Fort Meade hands every player the full GM editor, and both
			// spectate mods open editor VON for dead players).
			if (!isSenderEditor)
				EC29_ApplyRangeGain(playerId);
		}
		else
		{
			EC29_ApplyRadioAudioVars(playerId, receiver, frequency);
			EC29_TrackIncomingTransmission(receiver, frequency, playerId);
		}

		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);
	}

	//------------------------------------------------------------------------------------------------
	//! Direct-speech falloff gain (whisper/normal/yell). Writes EC29_VonRange only.
	protected void EC29_ApplyRangeGain(int playerId)
	{
		// One-time AudioSystem variable lookup so we can early-out cleanly when the conf isn't loaded.
		if (!s_bEC29VarChecked)
		{
			s_bEC29VarChecked = true;
			s_bEC29VarValid = (AudioSystem.GetVariableIDByName(EC29_VAR_NAME, EC29_VAR_CONFIG) != -1);

			if (!s_bEC29VarValid)
				PrintFormat("[EC29_VON] AudioSystem variable lookup FAILED: name='%1' config='%2' - audio modulation disabled", EC29_VAR_NAME, EC29_VAR_CONFIG, level: LogLevel.WARNING);
			else if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][VoN] AudioSystem variable '%1' resolved OK - von.acp override + local variables conf are loaded", EC29_VAR_NAME);
		}

		if (!s_bEC29VarValid)
			return;

		float volume = 1.0;

		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (settings)
		{
			// A speaker with no resolvable controlled entity (dead player on the
			// deploy screen, spectator mid-teardown) has no position to compute
			// falloff from; ComputeListenerVolume would return the full default,
			// and writing that seizes the shared variable at packet rate. Leave
			// the variable to the speakers that do resolve.
			if (!EC29_GetVoNForPlayer(playerId))
				return;

			PlayerController localPc = GetGame().GetPlayerController();
			IEntity listener;
			if (localPc)
				listener = localPc.GetControlledEntity();

			// Audio path applies the floor so the source stays alive in the engine.
			volume = settings.ComputeListenerVolume(playerId, listener, true);
		}
		else
		{
			// One log per session is enough; settings==null means the GameMode_Base override didn't apply.
			if (!s_mEC29DbgLastGain.Contains(-1))
			{
				s_mEC29DbgLastGain.Set(-1, 1.0);
				Print("[EC29-DBG][VoN] EC29_VONSettingsComponent.GetInstance() is NULL during OnReceive - GameMode prefab override not applied; gain stays 1.0", LogLevel.WARNING);
			}
		}

		// A quieter write only goes through once the current louder value has
		// gone stale (its speaker stopped or their packets paused); until then
		// this packet's gain is suppressed. Equal or louder always wins and
		// refreshes the hold, so a nearby speaker keeps ownership while talking
		// and a speaker walking away steps their own gain down at worst
		// EC29_GAIN_HOLD_MS late.
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			float nowMs = world.GetWorldTime();
			if (volume < s_fEC29ActiveGain && (nowMs - s_fEC29ActiveGainSetMs) <= EC29_GAIN_HOLD_MS)
				return;

			s_fEC29ActiveGain = volume;
			s_fEC29ActiveGainSetMs = nowMs;
		}

		// Throttled receive-path logging: only when this speaker's computed gain
		// changes noticeably. The throttle map only exists to feed this log, so
		// the whole block sits behind the debug gate.
		if (EC29_Debug.VERBOSE)
		{
			float lastGain;
			if (!s_mEC29DbgLastGain.Find(playerId, lastGain) || Math.AbsFloat(lastGain - volume) > 0.02)
			{
				s_mEC29DbgLastGain.Set(playerId, volume);
				PrintFormat("[EC29-DBG][VoN] OnReceive: speaker playerId=%1 computed gain=%2 (pushed to audio var '%3')", playerId, volume, EC29_VAR_NAME);
			}
		}

		AudioSystem.SetVariableByName(EC29_VAR_NAME, volume, EC29_VAR_CONFIG);
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

		vector receiverPos = vector.Zero;
		PlayerController playerController = GetGame().GetPlayerController();
		if (playerController)
		{
			IEntity receiverEntity = playerController.GetControlledEntity();
			if (receiverEntity)
				receiverPos = receiverEntity.GetOrigin();
		}

		if (s_bEC29SignalQualityValid)
		{
			float signalQuality = EC29_GetSignalQuality(playerId, frequency, receiverPos);
			AudioSystem.SetVariableByName("EC29_SignalQuality", signalQuality, EC29_EAR_ROUTING_CONFIG);
		}

		if (s_bEC29JamStrengthValid)
		{
			float jamStrength = EC29_GetJamStrength(receiverPos);
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
