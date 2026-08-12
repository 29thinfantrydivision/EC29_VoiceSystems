modded class SCR_VoNComponent
{
	static const string EC29_VAR_NAME   = "EC29_VonRange";
	static const string EC29_VAR_CONFIG = "{33A27275C95E0302}Sounds/VON/EC29_LocalVariables_VON.conf";

	protected static bool s_bEC29VarValid;
	protected static bool s_bEC29VarChecked;
	protected static ref map<int, SCR_VoNComponent> s_mEC29PlayerVon = new map<int, SCR_VoNComponent>();
	protected static ref map<int, IEntity> s_mEC29PlayerVonEntity = new map<int, IEntity>();

	[RplProp(onRplName: "EC29_OnVoiceRangeReplicated")]
	protected EC29_EVoiceRange m_eEC29VoiceRange = EC29_EVoiceRange.NORMAL;

	//------------------------------------------------------------------------------------------------
	//! RplProp callback - fires on all clients when m_eEC29VoiceRange changes.
	//! Used to push the new mode into the VoN overlay so the WHISPER / YELLING label
	//! refreshes mid-transmission instead of only on the next new transmission.
	protected void EC29_OnVoiceRangeReplicated()
	{
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
			return;

		if (m_eEC29VoiceRange == range)
			return;

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
		// One-time AudioSystem variable lookup so we can early-out cleanly when the conf isn't loaded.
		if (!s_bEC29VarChecked)
		{
			s_bEC29VarChecked = true;
			s_bEC29VarValid = (AudioSystem.GetVariableIDByName(EC29_VAR_NAME, EC29_VAR_CONFIG) != -1);

			if (!s_bEC29VarValid)
				PrintFormat("[EC29_VON] AudioSystem variable lookup FAILED: name='%1' config='%2' - audio modulation disabled", EC29_VAR_NAME, EC29_VAR_CONFIG, level: LogLevel.WARNING);
		}

		if (!s_bEC29VarValid)
		{
			super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);
			return;
		}

		float volume = 1.0;

		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (settings)
		{
			PlayerController localPc = GetGame().GetPlayerController();
			IEntity listener;
			if (localPc)
				listener = localPc.GetControlledEntity();

			// Audio path applies the floor so the source stays alive in the engine.
			volume = settings.ComputeListenerVolume(playerId, listener, true);
		}

		AudioSystem.SetVariableByName(EC29_VAR_NAME, volume, EC29_VAR_CONFIG);

		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);
	}
}
