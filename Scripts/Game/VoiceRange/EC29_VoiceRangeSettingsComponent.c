//------------------------------------------------------------------------------------------------
[ComponentEditorProps(category: "GameScripted/GameMode/Components", description: "Replicates EC29_VON overlay and nametag policy from the mission header to all clients.")]
class EC29_VONSettingsComponentClass : SCR_BaseGameModeComponentClass
{
}

//------------------------------------------------------------------------------------------------
//! Carries the mission's EC29_VON_Settings block to every machine.
//!
//! Only the server looks at the mission header; it copies the seven flags once at post-init and
//! clients pick them up through replication (initial state covers late joiners). Nothing changes
//! them afterwards, so consumers simply read the getters when they need a value.
//!
//! Every consumer treats a missing instance as "leave vanilla alone".
//!
//! Lives on the game mode via the Prefabs/MP/Modes/GameMode_Base.et override.
class EC29_VONSettingsComponent : SCR_BaseGameModeComponent
{
	protected static EC29_VONSettingsComponent s_EC29_Active;

	// Field initializers match the header attribute defaults: a mission without the block
	// behaves exactly like one with an untouched block.
	[RplProp()]
	protected bool m_bEC29_AlwaysShowEnemyNames = true;

	[RplProp()]
	protected bool m_bEC29_FactionNameColoring = true;

	[RplProp()]
	protected bool m_bEC29_HideFriendlyDirect = false;

	[RplProp()]
	protected bool m_bEC29_HideEnemyDirect = true;

	[RplProp()]
	protected bool m_bEC29_HideRole = true;

	[RplProp()]
	protected bool m_bEC29_ShowVoiceMode = true;

	[RplProp()]
	protected bool m_bEC29_GateNameTag = true;

	//------------------------------------------------------------------------------------------------
	//! The live component, or null when the game mode does not carry one.
	static EC29_VONSettingsComponent GetInstance()
	{
		return s_EC29_Active;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_EC29_Active = this;

		bool isServer = Replication.IsServer();
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][VONSettings] component alive on game mode (isServer=%1) - visual outer ranges whisper=%2 m normal=%3 m yell=%4 m",
				isServer, EC29_VoiceTiers.WHISPER_OUTER_M, EC29_VoiceTiers.NORMAL_OUTER_M, EC29_VoiceTiers.YELL_OUTER_M);

		if (!isServer)
			return;

		Print("[EC29] Voice systems initialized (server)");

		EC29_LoadFromMission();
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (s_EC29_Active == this)
			s_EC29_Active = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Server: copy the header block into the replicated fields, or keep the built-in defaults
	//! when the mission does not carry one.
	protected void EC29_LoadFromMission()
	{
		EC29_VON_Settings block;
		SCR_MissionHeader header = SCR_MissionHeader.Cast(GetGame().GetMissionHeader());
		if (header)
			block = header.m_EC29_VON_Settings;

		if (!block)
		{
			Print("[EC29_VON] Mission header has no EC29_VON_Settings; using component defaults.", LogLevel.WARNING);
			return;
		}

		m_bEC29_AlwaysShowEnemyNames = block.m_bAlwaysShowEnemyNames;
		m_bEC29_FactionNameColoring = block.m_bEnableVonFactionNameColoring;
		m_bEC29_HideFriendlyDirect = block.m_bHideFriendlyDirectIncoming;
		m_bEC29_HideEnemyDirect = block.m_bHideEnemyDirectIncoming;
		m_bEC29_HideRole = block.m_bHideRoleInVonOverlay;
		m_bEC29_ShowVoiceMode = block.m_bShowVoiceModeInOverlay;
		m_bEC29_GateNameTag = block.m_bGateNameTagVonByRange;

		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	bool GetAlwaysShowEnemyNames()
	{
		return m_bEC29_AlwaysShowEnemyNames;
	}

	//------------------------------------------------------------------------------------------------
	bool GetEnableVonFactionNameColoring()
	{
		return m_bEC29_FactionNameColoring;
	}

	//------------------------------------------------------------------------------------------------
	bool GetHideFriendlyDirectIncoming()
	{
		return m_bEC29_HideFriendlyDirect;
	}

	//------------------------------------------------------------------------------------------------
	bool GetHideEnemyDirectIncoming()
	{
		return m_bEC29_HideEnemyDirect;
	}

	//------------------------------------------------------------------------------------------------
	bool GetHideRoleInVonOverlay()
	{
		return m_bEC29_HideRole;
	}

	//------------------------------------------------------------------------------------------------
	bool GetShowVoiceModeInOverlay()
	{
		return m_bEC29_ShowVoiceMode;
	}

	//------------------------------------------------------------------------------------------------
	bool GetGateNameTagVonByRange()
	{
		return m_bEC29_GateNameTag;
	}

	//------------------------------------------------------------------------------------------------
	//! VISUAL reach check: is the speaker within the outer (silent) radius of the tier they are
	//! on, measured from the listener? Feeds the overlay and the nametag only; audio never asks.
	//!
	//! Fails open. A packet did arrive, so whenever the answer cannot be worked out (no listener,
	//! no stock VoN for the speaker, speaker controls nothing) the speaker stays visible.
	//!
	//! The mode is the speaker's replicated one, so for one round trip after they switch it can
	//! disagree with the tier the packet actually came from - nothing on the listener's side says
	//! which tier transmitted.
	bool IsAudibleForListener(int senderPlayerId, IEntity listener)
	{
		if (!listener)
			return true;

		SCR_VoNComponent speakerVon = SCR_VoNComponent.EC29_GetVoNForPlayer(senderPlayerId);
		if (!speakerVon)
			return true;

		IEntity speaker = GetGame().GetPlayerManager().GetPlayerControlledEntity(senderPlayerId);
		if (!speaker)
			return true;

		float reach = EC29_VoiceTiers.OuterRange(speakerVon.EC29_GetVoiceRange());
		float distSq = vector.DistanceSq(speaker.GetOrigin(), listener.GetOrigin());

		// Inclusive edge: standing exactly on the outer radius still counts as heard.
		return distSq <= reach * reach;
	}
}
