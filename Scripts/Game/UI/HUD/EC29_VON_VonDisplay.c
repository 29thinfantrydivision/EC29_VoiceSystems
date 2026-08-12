//------------------------------------------------------------------------------------------------
//! Modded SCR_VonDisplay that
//!  - hides incoming direct VoN transmission entries based on faction (friendly /
//!    enemy filtering),
//!  - hides incoming direct entries based on the speaker's voice mode + distance
//!    so the UI stays consistent with audibility (whisper out of range -> no
//!    "INCOMING" indicator that would leak position),
//!  - optionally suppresses the (Role) text in brackets next to each transmission,
//!  - optionally rewrites enemy names / colors them by faction.
//!
//! All toggles are server-driven via EC29_VONSettingsComponent (which mirrors the
//! mission header). No per-prefab Attributes — single source of truth.
modded class SCR_VonDisplay
{
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	//! Mark every active transmission for re-update so UpdateTransmission re-runs and
	//! refreshes the WHISPER / YELLING channel label. Called when any player's voice
	//! mode changes (replicated via SCR_VoNComponent's RplProp).
	void EC29_ForceRefreshAllTransmissions()
	{
		Print("[EC29-DBG][VonOverlay] Refreshing transmission labels after voice-mode change", LogLevel.NORMAL);
		if (m_OutTransmission)
			m_OutTransmission.m_bForceUpdate = true;

		if (!m_aTransmissionMap)
			return;

		foreach (int playerId, TransmissionData data : m_aTransmissionMap)
		{
			if (data)
				data.m_bForceUpdate = true;
		}
	}

	//------------------------------------------------------------------------------------------------
	override event void OnReceive(int playerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)
	{
		if (!m_wRoot || m_bIsVONUIDisabled)
			return;

		// Only direct (proximity) transmissions get the faction / range filtering;
		// radio messages should display normally regardless of distance.
		if (!receiver && !isSenderEditor && !IsLocalEditorOpened_C())
		{
			if (ShouldHideDirectIncoming_C(playerId) || EC29_ShouldHideOutOfRange(playerId))
			{
				TransmissionData existing = m_aTransmissionMap.Get(playerId);
				if (existing)
					existing.HideTransmission();

				return;
			}
		}

		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);

		// Vanilla's incoming OnReceive only re-runs UpdateTransmission on device / freq /
		// active changes - it ignores m_bForceUpdate (only the outgoing path checks that).
		// We need force-update on incoming so the WHISPER / YELLING label refreshes when
		// the speaker cycles modes mid-transmission.
		if (m_aTransmissionMap)
		{
			TransmissionData data = m_aTransmissionMap.Get(playerId);
			if (data && data.m_bForceUpdate)
			{
				// Mirror vanilla OnReceive: a transmission UpdateTransmission filters out
				// must be hidden, not left half-updated on screen.
				if (!UpdateTransmission(data, receiver, frequency, true))
					data.HideTransmission();
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	override protected bool UpdateTransmission(TransmissionData data, BaseTransceiver radioTransceiver, int frequency, bool IsReceiving)
	{
		bool result = super.UpdateTransmission(data, radioTransceiver, frequency, IsReceiving);
		if (!result)
			return false;

		// Radio port (506th): outgoing entries show CYAN frequency while the alternate
		// channel is keyed, WHITE otherwise (the reset un-cyans after alt-PTT release).
		// EC29 never colors m_wFrequency, so this owns that widget exclusively.
		// Coexistence: when the real 506IRRU mod is co-loaded its own display mod
		// owns this widget - our WHITE reset would erase its CYAN indicator.
		if (!IsReceiving && radioTransceiver && !EC29_CoexistenceGuard.ShouldYieldRadio() && data && data.m_Widgets && data.m_Widgets.m_wFrequency)
		{
			EC29_RadioEarSettings earSettings = EC29_RadioEarSettings.GetInstance();
			if (earSettings.IsTransmittingOnAlternate())
				data.m_Widgets.m_wFrequency.SetColor(Color.FromInt(Color.CYAN));
			else
				data.m_Widgets.m_wFrequency.SetColor(Color.FromInt(Color.WHITE));
		}

		if (!data || data.m_bIsAdditional)
			return result;

		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (!settings)
			return result;

		// Customizations that only apply to incoming transmissions (other people's audio).
		if (IsReceiving)
		{
			// Hide the "(Role)" bracket text entirely if configured.
			if (settings.GetHideRoleInVonOverlay() && data.m_Widgets && data.m_Widgets.m_wRole)
			{
				data.m_Widgets.m_wRole.SetText(string.Empty);
				data.m_Widgets.m_wRole.SetVisible(false);
			}

			if (settings.GetAlwaysShowEnemyNames())
				UpdateTransmissionName_C(data);

			if (settings.GetEnableVonFactionNameColoring())
				ApplyFactionColorToName_C(data);
		}

		// Voice-mode label in the channel slot. Applies to BOTH directions when sending
		// direct (radioTransceiver == null): the local speaker sees their own mode the
		// same way they see PLATOON when broadcasting on platoon frequency, and listeners
		// see the speaker's mode for incoming direct audio.
		if (settings.GetShowVoiceModeInOverlay() && !radioTransceiver)
			ApplyVoiceModeLabel_C(data, IsReceiving);

		return result;
	}

	//------------------------------------------------------------------------------------------------
	//! Show the speaker's voice mode in the channel slot for direct (proximity) transmissions.
	//! NORMAL is intentionally blank to keep the overlay quiet for the common case.
	//! \param IsReceiving true for incoming - resolve speaker via data.m_iPlayerID;
	//!                    false for outgoing - resolve speaker via the local PlayerController.
	protected void ApplyVoiceModeLabel_C(TransmissionData data, bool IsReceiving)
	{
		if (!data || !data.m_Widgets || !data.m_Widgets.m_wChannelText || !data.m_Widgets.m_wChannelFrame)
			return;

		SCR_VoNComponent senderVon;
		if (IsReceiving)
		{
			senderVon = SCR_VoNComponent.EC29_GetVoNForPlayer(data.m_iPlayerID);
		}
		else
		{
			PlayerController localPc = GetGame().GetPlayerController();
			if (localPc)
			{
				IEntity controlled = localPc.GetControlledEntity();
				if (controlled)
					senderVon = SCR_VoNComponent.Cast(controlled.FindComponent(SCR_VoNComponent));
			}
		}

		if (!senderVon)
		{
			data.m_Widgets.m_wChannelFrame.SetVisible(false);
			return;
		}

		EC29_EVoiceRange mode = senderVon.EC29_GetVoiceRange();
		string label;
		switch (mode)
		{
			case EC29_EVoiceRange.WHISPER: label = "#EC29-VON_Mode_Whisper"; break;
			case EC29_EVoiceRange.YELL:    label = "#EC29-VON_Mode_Yelling"; break;
		}

		if (label.IsEmpty())
		{
			data.m_Widgets.m_wChannelFrame.SetVisible(false);
			return;
		}

		data.m_Widgets.m_wChannelText.SetText(label);
		data.m_Widgets.m_wChannelFrame.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! Hide the overlay entry when the speaker is below the listener's audibility
	//! threshold. Mirrors the audio path exactly through the shared helper.
	protected bool EC29_ShouldHideOutOfRange(int senderPlayerId)
	{
		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (!settings)
			return false;

		PlayerController localPc = GetGame().GetPlayerController();
		if (!localPc)
			return false;

		IEntity listener = localPc.GetControlledEntity();
		if (!listener)
			return false;

		return !settings.IsAudibleForListener(senderPlayerId, listener);
	}

	//------------------------------------------------------------------------------------------------
	protected bool ShouldHideDirectIncoming_C(int senderPlayerId)
	{
		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (!settings)
			return false;

		bool hideFriendly = settings.GetHideFriendlyDirectIncoming();
		bool hideEnemy    = settings.GetHideEnemyDirectIncoming();

		if (!hideFriendly && !hideEnemy)
			return false;

		if (m_bIsVONDirectDisabled)
			return true;

		SCR_FactionManager factionMgr = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		if (!factionMgr)
			return false;

		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return false;

		int controlledId = controller.GetPlayerId();
		if (!controlledId)
			return false;

		Faction localFaction = factionMgr.SGetPlayerFaction(controlledId);
		Faction senderFaction = factionMgr.SGetPlayerFaction(senderPlayerId);

		if (!localFaction || !senderFaction)
			return false;

		bool isEnemy = localFaction.IsFactionEnemy(senderFaction);

		if (isEnemy)
			return hideEnemy;

		return hideFriendly;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalEditorOpened_C()
	{
		SCR_EditorManagerCore editorCore = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
		if (!editorCore)
			return false;

		SCR_EditorManagerEntity editor = editorCore.GetEditorManager();
		if (!editor)
			return false;

		return editor.IsOpened();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFactionColorToName_C(TransmissionData data)
	{
		if (!data || !data.m_Widgets || !data.m_Widgets.m_wName)
			return;

		SCR_Faction faction = SCR_Faction.Cast(data.m_Faction);
		if (!faction)
			return;

		data.m_Widgets.m_wName.SetColor(Color.FromInt(faction.GetFactionColor().PackToInt()));
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateTransmissionName_C(TransmissionData data)
	{
		if (!data || !data.m_Widgets || !data.m_Widgets.m_wName)
			return;

		data.m_Entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(data.m_iPlayerID);

		SCR_PossessingManagerComponent possMgr = SCR_PossessingManagerComponent.GetInstance();
		if (data.m_Entity && possMgr && possMgr.IsPossessing(data.m_iPlayerID))
		{
			SCR_CharacterIdentityComponent scrCharIdentity = SCR_CharacterIdentityComponent.Cast(
				data.m_Entity.FindComponent(SCR_CharacterIdentityComponent)
			);

			if (scrCharIdentity)
			{
				string name;
				array<string> nameParams = {};
				scrCharIdentity.GetFormattedFullName(name, nameParams);

				if (nameParams && nameParams.Count() >= 3)
					data.m_Widgets.m_wName.SetTextFormat(name, nameParams[0], nameParams[1], nameParams[2]);
				else
					data.m_Widgets.m_wName.SetText(name);
			}
			else
			{
				CharacterIdentityComponent charIdentity = CharacterIdentityComponent.Cast(
					data.m_Entity.FindComponent(CharacterIdentityComponent)
				);

				if (charIdentity && charIdentity.GetIdentity())
					data.m_Widgets.m_wName.SetText(charIdentity.GetIdentity().GetName());
				else
					data.m_Widgets.m_wName.SetText(SCR_PlayerNamesFilterCache.GetInstance().GetPlayerDisplayName(data.m_iPlayerID));
			}
		}
		else
		{
			data.m_Widgets.m_wName.SetText(SCR_PlayerNamesFilterCache.GetInstance().GetPlayerDisplayName(data.m_iPlayerID));
		}

		data.m_Widgets.m_wName.SetVisible(true);
	}
}
