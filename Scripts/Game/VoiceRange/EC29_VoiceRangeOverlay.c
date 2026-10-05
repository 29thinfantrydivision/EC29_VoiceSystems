//------------------------------------------------------------------------------------------------
//! EC29 additions to the vanilla VoN transmission overlay.
//!
//! Admission (OnReceive): direct speech can be dropped by the mission's faction policy or
//! because the speaker is outside their tier's reach; spectators get enemy names.
//! Presentation (UpdateTransmission): outgoing radio frequency tint for the alternate channel,
//! real names, faction-coloured names, role text removal and the WHISPER / YELLING tag.
//!
//! Policy comes from EC29_VONSettingsComponent; without one the overlay stays vanilla apart from
//! the frequency tint, which belongs to the radio feature rather than the policy.
modded class SCR_VonDisplay
{
	protected static const string EC29_TAG_WHISPER = "#EC29-VON_Mode_Whisper";
	protected static const string EC29_TAG_YELL = "#EC29-VON_Mode_Yelling";

	//! The mission's own m_bShowEnemyNames, captured before EC29 ever touches the flag.
	protected bool m_bEC29_MissionEnemyNames;
	protected bool m_bEC29_MissionEnemyNamesCaptured;

	//------------------------------------------------------------------------------------------------
	//! Flags every live entry (outgoing and incoming) for a re-update so the mode tag follows a
	//! voice-mode change in the middle of a transmission. Called by SCR_VONController right after
	//! the local cycle, and by SCR_VoNComponent's replicated-mode callback on every client.
	void EC29_ForceRefreshAllTransmissions()
	{
		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][VonOverlay] voice mode changed - flagging transmission labels for refresh");

		if (m_OutTransmission)
			m_OutTransmission.m_bForceUpdate = true;

		foreach (int speakerId, TransmissionData entry : m_aTransmissionMap)
		{
			if (entry)
				entry.m_bForceUpdate = true;
		}
	}

	//------------------------------------------------------------------------------------------------
	override event void OnReceive(int playerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)
	{
		// Same early-out as vanilla, kept here so nothing below runs either.
		if (!m_wRoot || m_bIsVONUIDisabled)
			return;

		bool spectating = SCR_VoNComponent.EC29_IsSpectatingListener();
		EC29_TrackEnemyNamesForSpectator(spectating);

		if (EC29_RejectsDirect(playerId, isSenderEditor, receiver, spectating))
		{
			EC29_DropEntry(m_aTransmissionMap.Get(playerId));
			return;
		}

		super.OnReceive(playerId, isSenderEditor, receiver, frequency, quality);

		// Vanilla's incoming path only re-updates on device / frequency / activity changes and
		// never looks at m_bForceUpdate (only OnCapture does). Honour it here so a mode change
		// mid-sentence reaches the tag. A successful update clears the flag, so an entry vanilla
		// just updated is skipped.
		TransmissionData entry = m_aTransmissionMap.Get(playerId);
		if (!entry || !entry.m_bForceUpdate)
			return;

		if (!UpdateTransmission(entry, receiver, frequency, true))
			entry.HideTransmission();
	}

	//------------------------------------------------------------------------------------------------
	override protected bool UpdateTransmission(TransmissionData data, BaseTransceiver radioTransceiver, int frequency, bool IsReceiving)
	{
		if (!super.UpdateTransmission(data, radioTransceiver, frequency, IsReceiving))
			return false;

		if (!IsReceiving && radioTransceiver)
			EC29_TintOutgoingFrequency(data);

		if (data.m_bIsAdditional)
			return true;

		EC29_VONSettingsComponent policy = EC29_VONSettingsComponent.GetInstance();
		if (!policy)
			return true;

		if (IsReceiving)
			EC29_DecorateIncoming(data, policy);

		if (!radioTransceiver && policy.GetShowVoiceModeInOverlay())
			EC29_ApplyModeTag(data, IsReceiving);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Spectators must see enemy direct speech (vanilla discards it before any rename could run),
	//! living players must see exactly what the mission set. The flag therefore follows
	//! "mission value OR spectating" per packet - tracked, never latched - and the mission value is
	//! read once, before the first write.
	protected void EC29_TrackEnemyNamesForSpectator(bool spectating)
	{
		if (!m_bEC29_MissionEnemyNamesCaptured)
		{
			m_bEC29_MissionEnemyNames = m_bShowEnemyNames;
			m_bEC29_MissionEnemyNamesCaptured = true;
		}

		m_bShowEnemyNames = m_bEC29_MissionEnemyNames || spectating;
	}

	//------------------------------------------------------------------------------------------------
	//! True when an incoming DIRECT packet must not produce an overlay entry. Radio, GM/editor
	//! voices, an open local editor and spectating listeners are always let through.
	protected bool EC29_RejectsDirect(int playerId, bool isSenderEditor, BaseTransceiver receiver, bool spectating)
	{
		if (receiver || isSenderEditor || spectating)
			return false;

		if (EC29_IsLocalEditorOpen())
			return false;

		EC29_VONSettingsComponent policy = EC29_VONSettingsComponent.GetInstance();
		if (!policy)
			return false;

		if (EC29_FactionPolicyRejects(playerId, policy))
			return true;

		// Reach gate: not tied to any mission flag (the nametag flag does not switch it off).
		PlayerController localController = GetGame().GetPlayerController();
		if (!localController)
			return false;

		IEntity listener = localController.GetControlledEntity();
		if (!listener)
			return false;

		return !policy.IsAudibleForListener(playerId, listener);
	}

	//------------------------------------------------------------------------------------------------
	protected bool EC29_FactionPolicyRejects(int playerId, EC29_VONSettingsComponent policy)
	{
		bool hideHostile = policy.GetHideEnemyDirectIncoming();
		bool hideOthers = policy.GetHideFriendlyDirectIncoming();
		if (!hideHostile && !hideOthers)
			return false;

		// The server switched direct-speech UI off - nothing to sort by faction.
		if (m_bIsVONDirectDisabled)
			return true;

		SCR_FactionManager factions = SCR_FactionManager.Cast(GetGame().GetFactionManager());
		PlayerController localController = GetGame().GetPlayerController();
		if (!factions || !localController)
			return false;

		int localId = localController.GetPlayerId();
		if (localId == 0)
			return false;

		Faction ours = SCR_FactionManager.SGetPlayerFaction(localId);
		Faction theirs = SCR_FactionManager.SGetPlayerFaction(playerId);
		if (!ours || !theirs)
			return false;

		if (ours.IsFactionEnemy(theirs))
			return hideHostile;

		return hideOthers;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EC29_IsLocalEditorOpen()
	{
		SCR_EditorManagerEntity editor = SCR_EditorManagerEntity.GetInstance();
		return editor && editor.IsOpened();
	}

	//------------------------------------------------------------------------------------------------
	//! Takes an entry that is already on screen off it. Overflow ("+N") entries share one widget,
	//! so they are only marked gone and DisplayUpdate retires them and fixes the counter.
	protected void EC29_DropEntry(TransmissionData entry)
	{
		if (!entry)
			return;

		if (entry.m_bIsAdditional)
		{
			entry.m_bIsActive = false;
			entry.m_bIsAnimating = false;
			entry.m_bVisible = false;
			return;
		}

		entry.HideTransmission();
	}

	//------------------------------------------------------------------------------------------------
	//! Cyan while the alternate channel is keyed, white otherwise (the white write is what clears
	//! the cyan once the alternate push-to-talk is released). Nothing else in EC29 colours this
	//! widget. Steps aside completely, reset included, when a conflicting radio mod owns radios.
	protected void EC29_TintOutgoingFrequency(TransmissionData data)
	{
		if (EC29_CoexistenceGuard.ShouldYieldRadio())
			return;

		if (!data.m_Widgets || !data.m_Widgets.m_wFrequency)
			return;

		if (EC29_RadioState.GetInstance().EarSettings().IsTransmittingOnAlternate())
			data.m_Widgets.m_wFrequency.SetColor(Color.FromRGBA(0, 255, 255, 255));
		else
			data.m_Widgets.m_wFrequency.SetColor(Color.FromRGBA(255, 255, 255, 255));
	}

	//------------------------------------------------------------------------------------------------
	//! Policy-driven touches on another player's entry (radio or direct).
	protected void EC29_DecorateIncoming(TransmissionData data, EC29_VONSettingsComponent policy)
	{
		SCR_VoNOverlay_ElementWidgets widgets = data.m_Widgets;
		if (!widgets)
			return;

		if (policy.GetHideRoleInVonOverlay() && widgets.m_wRole)
		{
			widgets.m_wRole.SetText(string.Empty);
			widgets.m_wRole.SetVisible(false);
		}

		if (!widgets.m_wName)
			return;

		if (policy.GetAlwaysShowEnemyNames())
		{
			EC29_WriteSpeakerName(data, widgets.m_wName);
			widgets.m_wName.SetVisible(true);
		}

		if (policy.GetEnableVonFactionNameColoring())
		{
			SCR_Faction speakerFaction = SCR_Faction.Cast(data.m_Faction);
			if (speakerFaction)
				widgets.m_wName.SetColor(speakerFaction.GetFactionColor());
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Writes the speaker's name for every incoming entry, friend or foe. A game master possessing
	//! a character is shown under that character's identity; anyone else under their filtered
	//! player name (so platform name filtering still applies).
	protected void EC29_WriteSpeakerName(TransmissionData data, TextWidget nameWidget)
	{
		data.m_Entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(data.m_iPlayerID);

		SCR_PossessingManagerComponent possession = SCR_PossessingManagerComponent.GetInstance();
		if (data.m_Entity && possession && possession.IsPossessing(data.m_iPlayerID))
		{
			EC29_WritePossessedName(data, nameWidget);
			return;
		}

		nameWidget.SetText(SCR_PlayerNamesFilterCache.GetInstance().GetPlayerDisplayName(data.m_iPlayerID));
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_WritePossessedName(TransmissionData data, TextWidget nameWidget)
	{
		SCR_CharacterIdentityComponent scriptedIdentity = SCR_CharacterIdentityComponent.Cast(data.m_Entity.FindComponent(SCR_CharacterIdentityComponent));
		if (scriptedIdentity)
		{
			string format;
			array<string> args = {};
			scriptedIdentity.GetFormattedFullName(format, args);

			// The format takes up to three parameters; pad so a short list cannot index out of range.
			while (args.Count() < 3)
			{
				args.Insert(string.Empty);
			}

			nameWidget.SetTextFormat(format, args[0], args[1], args[2]);
			return;
		}

		CharacterIdentityComponent plainIdentity = CharacterIdentityComponent.Cast(data.m_Entity.FindComponent(CharacterIdentityComponent));
		if (plainIdentity && plainIdentity.GetIdentity())
		{
			nameWidget.SetText(plainIdentity.GetIdentity().GetName());
			return;
		}

		nameWidget.SetText(SCR_PlayerNamesFilterCache.GetInstance().GetPlayerDisplayName(data.m_iPlayerID));
	}

	//------------------------------------------------------------------------------------------------
	//! Direct-speech entries, both directions: WHISPER / YELLING in the channel slot, nothing for
	//! normal voice or an unresolvable speaker (the quiet common case, the way vanilla only tags
	//! special radio channels). The mode is read from the STOCK VoN component, which carries the
	//! replicated value - the transmitting component is a tier and carries none.
	protected void EC29_ApplyModeTag(TransmissionData data, bool incoming)
	{
		if (!data.m_Widgets)
			return;

		RichTextWidget tagText = data.m_Widgets.m_wChannelText;
		Widget tagFrame = data.m_Widgets.m_wChannelFrame;
		if (!tagText || !tagFrame)
			return;

		SCR_VoNComponent speakerVon;
		if (incoming)
		{
			speakerVon = SCR_VoNComponent.EC29_GetVoNForPlayer(data.m_iPlayerID);
		}
		else
		{
			PlayerController localController = GetGame().GetPlayerController();
			if (localController)
				speakerVon = EC29_VoiceTiers.StockVoN(localController.GetControlledEntity());
		}

		string tag;
		if (speakerVon)
		{
			EC29_EVoiceRange mode = speakerVon.EC29_GetVoiceRange();
			if (mode == EC29_EVoiceRange.WHISPER)
				tag = EC29_TAG_WHISPER;
			else if (mode == EC29_EVoiceRange.YELL)
				tag = EC29_TAG_YELL;
		}

		if (tag.IsEmpty())
		{
			tagFrame.SetVisible(false);
			return;
		}

		tagText.SetText(tag);
		tagFrame.SetVisible(true);
	}
}
