[ComponentEditorProps(category: "GameScripted/GameMode/Components", description: "Replicates EC29_VON voice range settings from the mission header to all clients.")]
class EC29_VONSettingsComponentClass : SCR_BaseGameModeComponentClass {}

//------------------------------------------------------------------------------------------------
class EC29_VONSettingsComponent : SCR_BaseGameModeComponent
{
	[RplProp()] protected float m_fWhisperRange  = 3.0;
	[RplProp()] protected float m_fNormalRange   = 15.0;
	[RplProp()] protected float m_fYellRange     = 50.0;
	[RplProp()] protected float m_fFalloffPower  = 4.0;
	[RplProp()] protected float m_fWhisperVolume = 1.0;
	[RplProp()] protected float m_fNormalVolume  = 1.0;
	[RplProp()] protected float m_fYellVolume    = 3.0;
	[RplProp()] protected float m_fMinVolume     = 0.05;

	[RplProp()] protected bool m_bAlwaysShowEnemyNames         = true;
	[RplProp()] protected bool m_bEnableVonFactionNameColoring = true;
	[RplProp()] protected bool m_bHideFriendlyDirectIncoming   = false;
	[RplProp()] protected bool m_bHideEnemyDirectIncoming      = true;
	[RplProp()] protected bool m_bHideRoleInVonOverlay         = true;
	[RplProp()] protected bool m_bGateNameTagVonByRange        = true;
	[RplProp()] protected bool m_bShowVoiceModeInOverlay       = true;

	protected static EC29_VONSettingsComponent s_pInstance;

	//------------------------------------------------------------------------------------------------
	static EC29_VONSettingsComponent GetInstance()
	{
		return s_pInstance;
	}

	//------------------------------------------------------------------------------------------------
	float GetRangeForMode(EC29_EVoiceRange mode)
	{
		switch (mode)
		{
			case EC29_EVoiceRange.WHISPER: return m_fWhisperRange;
			case EC29_EVoiceRange.YELL:    return m_fYellRange;
		}

		return m_fNormalRange;
	}

	//------------------------------------------------------------------------------------------------
	float GetVolumeForMode(EC29_EVoiceRange mode)
	{
		switch (mode)
		{
			case EC29_EVoiceRange.WHISPER: return m_fWhisperVolume;
			case EC29_EVoiceRange.YELL:    return m_fYellVolume;
		}

		return m_fNormalVolume;
	}

	//------------------------------------------------------------------------------------------------
	float GetFalloffPower()
	{
		return m_fFalloffPower;
	}

	//------------------------------------------------------------------------------------------------
	float GetMinVolume()
	{
		return m_fMinVolume;
	}

	//------------------------------------------------------------------------------------------------
	bool GetAlwaysShowEnemyNames()         { return m_bAlwaysShowEnemyNames; }
	bool GetEnableVonFactionNameColoring() { return m_bEnableVonFactionNameColoring; }
	bool GetHideFriendlyDirectIncoming()   { return m_bHideFriendlyDirectIncoming; }
	bool GetHideEnemyDirectIncoming()      { return m_bHideEnemyDirectIncoming; }
	bool GetHideRoleInVonOverlay()         { return m_bHideRoleInVonOverlay; }
	bool GetGateNameTagVonByRange()        { return m_bGateNameTagVonByRange; }
	bool GetShowVoiceModeInOverlay()       { return m_bShowVoiceModeInOverlay; }

	//------------------------------------------------------------------------------------------------
	//! Single source of truth for "what volume does this listener hear from this speaker
	//! based on the speaker's current voice mode and distance to the listener?"
	//!
	//! Used by the audio path (EC29_VoNComponent.OnReceive), the VoN overlay (EC29_VonDisplay),
	//! and the over-head nametag (EC29_NameTagData) to keep audio + UI in lockstep.
	//!
	//! \param senderPlayerId  Speaker's player id (from OnReceive's playerId).
	//! \param listener        Listener entity (typically the local player's controlled character).
	//! \param applyFloor      true = clamp result up to m_fMinVolume (audio path - keeps the
	//!                        audio source alive). false = return the raw computed value
	//!                        (UI / audibility checks - lets us tell "below threshold" cases).
	float ComputeListenerVolume(int senderPlayerId, IEntity listener, bool applyFloor)
	{
		if (!listener)
			return m_fNormalVolume;

		SCR_VoNComponent senderVon = SCR_VoNComponent.EC29_GetVoNForPlayer(senderPlayerId);
		if (!senderVon)
			return m_fNormalVolume;

		EC29_EVoiceRange mode = senderVon.EC29_GetVoiceRange();
		float volume = GetVolumeForMode(mode);

		// NORMAL: skip the distance math entirely - vanilla spatial falloff handles it.
		if (mode != EC29_EVoiceRange.NORMAL)
		{
			IEntity sender = GetGame().GetPlayerManager().GetPlayerControlledEntity(senderPlayerId);
			if (sender)
			{
				float distSq = vector.DistanceSqXZ(sender.GetOrigin(), listener.GetOrigin());
				float maxRange = GetRangeForMode(mode);

				if (distSq > maxRange * maxRange)
				{
					float dist = Math.Sqrt(distSq);
					float falloff = Math.Pow(maxRange / dist, m_fFalloffPower);
					volume *= falloff;
				}
			}
		}

		if (applyFloor && volume < m_fMinVolume)
			return m_fMinVolume;

		return volume;
	}

	//------------------------------------------------------------------------------------------------
	//! Returns true if the speaker is currently audible to the listener (computed volume
	//! is above the minimum-volume floor before clamping). Used for UI / nametag gating
	//! to suppress visual indicators when the audio is effectively silent.
	bool IsAudibleForListener(int senderPlayerId, IEntity listener)
	{
		return ComputeListenerVolume(senderPlayerId, listener, false) >= m_fMinVolume;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_pInstance = this;
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VONSettings] Component alive on game mode (GameMode_Base override applied). isServer=%1 whisper=%2m normal=%3m yell=%4m falloff=%5 yellVol=%6 minVol=%7",
				Replication.IsServer(), m_fWhisperRange, m_fNormalRange, m_fYellRange, m_fFalloffPower, m_fYellVolume, m_fMinVolume);

		if (!Replication.IsServer())
			return;

		SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
		if (!header || !header.m_EC29_VON_Settings)
		{
			Print("[EC29_VON] Mission header has no EC29_VON_Settings; using component defaults.", LogLevel.WARNING);
			return;
		}

		EC29_VON_Settings src = header.m_EC29_VON_Settings;
		m_fWhisperRange  = src.m_fWhisperRange;
		m_fNormalRange   = src.m_fNormalRange;
		m_fYellRange     = src.m_fYellRange;
		m_fFalloffPower  = src.m_fFalloffPower;
		m_fWhisperVolume = src.m_fWhisperVolume;
		m_fNormalVolume  = src.m_fNormalVolume;
		m_fYellVolume    = src.m_fYellVolume;
		m_fMinVolume     = src.m_fMinVolume;

		m_bAlwaysShowEnemyNames        = src.m_bAlwaysShowEnemyNames;
		m_bEnableVonFactionNameColoring = src.m_bEnableVonFactionNameColoring;
		m_bHideFriendlyDirectIncoming  = src.m_bHideFriendlyDirectIncoming;
		m_bHideEnemyDirectIncoming     = src.m_bHideEnemyDirectIncoming;
		m_bHideRoleInVonOverlay        = src.m_bHideRoleInVonOverlay;
		m_bGateNameTagVonByRange       = src.m_bGateNameTagVonByRange;
		m_bShowVoiceModeInOverlay      = src.m_bShowVoiceModeInOverlay;

		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (s_pInstance == this)
			s_pInstance = null;

		super.OnDelete(owner);
	}
}
