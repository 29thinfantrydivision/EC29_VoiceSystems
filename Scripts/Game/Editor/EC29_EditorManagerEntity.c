//------------------------------------------------------------------------------------------------
//! The spectator-voice flag lives on the manager entity: other players' controllers do not exist
//! on a client, but every manager does (RplComponent, streaming disabled). The server sets it
//! (EC29_SpectatorVoiceController); SCR_VoNComponent.IsEntityActiveEditor reads it on any machine.
//------------------------------------------------------------------------------------------------
modded class SCR_EditorManagerEntity
{
	[RplProp()]
	protected bool m_bEC29_SpectatorVoice;

	//------------------------------------------------------------------------------------------------
	bool EC29_IsSpectatorVoice()
	{
		return m_bEC29_SpectatorVoice;
	}

	//------------------------------------------------------------------------------------------------
	//! Server only.
	void EC29_SetSpectatorVoice(bool enable)
	{
		if (m_bEC29_SpectatorVoice == enable)
			return;

		m_bEC29_SpectatorVoice = enable;
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! A REAL GAME MASTER MUST NOT REACH THE SPECTATOR NET IN EITHER DIRECTION, and vanilla's own
	//! Open works against us on both counts.
	//!
	//! RECEIVE: their manager radio is powered while the editor is open, so the spectator
	//! transceiver would hear the net. Mute is a receiver-local gate.
	//!
	//! TRANSMIT: vanilla walks EVERY transceiver on the manager's radio and adds a VON entry for
	//! each (SCR_EditorManagerEntity.Open), so our added transceiver arrives in a GM's voice menu
	//! as a channel labelled "GM" that they can key. Muting does not stop that - mute is receive
	//! only - so the entry itself is removed. Our own transmit path never uses entries; it calls
	//! SetTransmitRadio directly, so a spectator is unaffected.
	//!
	//! Both are skipped while this player is spectating - the service owns those states then.
	override void Open(bool showErrorNotification = true)
	{
		super.Open(showErrorNotification);

		if (!IsOpened() || EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			return;

		EC29_SetSpectatorNetMuted(true);
		EC29_RemoveSpectatorNetEntry();
	}

	//------------------------------------------------------------------------------------------------
	//! Drops the VON entry vanilla just built for the spectator transceiver. Vanilla's own Close
	//! iterates the same array and skips nulls, so clearing the slot here needs no Close-side work.
	protected void EC29_RemoveSpectatorNetEntry()
	{
		if (!editorRadioEntries)
			return;

		BaseRadioComponent radio = BaseRadioComponent.Cast(FindComponent(BaseRadioComponent));
		BaseTransceiver net = EC29_SpectatorVonService.SpectatorTransceiver(radio);
		if (!net)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		SCR_VONController von = SCR_VONController.Cast(pc.FindComponent(SCR_VONController));
		if (!von)
			return;

		for (int i = 0, count = editorRadioEntries.Count(); i < count; i++)
		{
			SCR_VONEntryRadio entry = editorRadioEntries[i];
			if (!entry || entry.GetTransceiver() != net)
				continue;

			von.RemoveEntry(entry);
			editorRadioEntries[i] = null;
		}
	}

	//------------------------------------------------------------------------------------------------
	override void Close(bool showErrorNotification = true)
	{
		bool wasOpened = IsOpened();
		super.Close(showErrorNotification);

		if (wasOpened && !IsOpened() && !EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			EC29_SetSpectatorNetMuted(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_SetSpectatorNetMuted(bool muted)
	{
		BaseRadioComponent radio = BaseRadioComponent.Cast(FindComponent(BaseRadioComponent));
		BaseTransceiver net = EC29_SpectatorVonService.SpectatorTransceiver(radio);
		if (net && net.IsMuted() != muted)
			net.SetMuteState(muted);
	}
}
