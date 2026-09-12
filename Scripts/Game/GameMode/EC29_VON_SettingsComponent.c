[ComponentEditorProps(category: "GameScripted/GameMode/Components", description: "Replicates EC29_VON overlay and nametag policy from the mission header to all clients.")]
class EC29_VONSettingsComponentClass : SCR_BaseGameModeComponentClass {}

//------------------------------------------------------------------------------------------------
//! Overlay / nametag policy, mission-header seeded and replicated. The direct-speech RANGES are
//! no longer here: since 2026-09-12 they are properties of the transmitting tier's ACP
//! (EC29_VoiceTiers.c) and cannot be changed by a mission header. The visual gates below read
//! the script-side mirrors of those ranges.
class EC29_VONSettingsComponent : SCR_BaseGameModeComponent
{
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
	bool GetAlwaysShowEnemyNames()         { return m_bAlwaysShowEnemyNames; }
	bool GetEnableVonFactionNameColoring() { return m_bEnableVonFactionNameColoring; }
	bool GetHideFriendlyDirectIncoming()   { return m_bHideFriendlyDirectIncoming; }
	bool GetHideEnemyDirectIncoming()      { return m_bHideEnemyDirectIncoming; }
	bool GetHideRoleInVonOverlay()         { return m_bHideRoleInVonOverlay; }
	bool GetGateNameTagVonByRange()        { return m_bGateNameTagVonByRange; }
	bool GetShowVoiceModeInOverlay()       { return m_bShowVoiceModeInOverlay; }

	//------------------------------------------------------------------------------------------------
	//! VISUAL gate only: is this speaker inside the outer range of the tier they transmit on, as
	//! seen from the listener? Used by the over-head nametag (EC29_NameTagData) and the VoN
	//! overlay (EC29_VonDisplay) so an icon never shows for a voice the engine is not playing.
	//! Audio never consults this - the engine attenuates per source from the tier's ACP.
	//!
	//! The answer depends on the speaker's replicated mode, which can lag their audio by one
	//! round trip after an F3 press. There is no listener-side signal that says which tier a
	//! packet came from, so a moment of icon lag after a mode change is the accepted cost.
	//!
	//! Unresolvable speaker (no entity, no stock component): shown, not hidden - a packet did
	//! arrive, and hiding it would make a real talker invisible.
	bool IsAudibleForListener(int senderPlayerId, IEntity listener)
	{
		if (!listener)
			return true;

		SCR_VoNComponent senderVon = SCR_VoNComponent.EC29_GetVoNForPlayer(senderPlayerId);
		if (!senderVon)
			return true;

		IEntity sender = GetGame().GetPlayerManager().GetPlayerControlledEntity(senderPlayerId);
		if (!sender)
			return true;

		float outer = EC29_VoiceTiers.OuterRange(senderVon.EC29_GetVoiceRange());
		return vector.DistanceSq(sender.GetOrigin(), listener.GetOrigin()) <= outer * outer;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_pInstance = this;
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VONSettings] Component alive on game mode (GameMode_Base override applied). isServer=%1 - direct ranges are per transmit tier (whisper %2m / normal %3m / yell %4m outer)",
				Replication.IsServer(), EC29_VoiceTiers.WHISPER_OUTER_M, EC29_VoiceTiers.NORMAL_OUTER_M, EC29_VoiceTiers.YELL_OUTER_M);

		if (!Replication.IsServer())
			return;

		SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
		if (!header || !header.m_EC29_VON_Settings)
		{
			Print("[EC29_VON] Mission header has no EC29_VON_Settings; using component defaults.", LogLevel.WARNING);
			return;
		}

		EC29_VON_Settings src = header.m_EC29_VON_Settings;

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
