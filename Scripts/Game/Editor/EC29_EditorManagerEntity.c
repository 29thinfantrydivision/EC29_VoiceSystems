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
	//! A GAME MASTER GETS THE SPECTATOR TRANSCEIVER IN THEIR VOICE MENU whether we like it or not:
	//! vanilla's Open() walks every transceiver on the manager radio and builds a VON entry for each.
	//! That is harmless now - the transceiver is PARKED on 28000 unless this player is spectating
	//! (EC29_SpectatorVonService), so the entry is a channel with nobody on it.
	//!
	//! The mute stays as a second line, for the case where a park was missed. It is receive-side and
	//! restored on Close. Skipped while this player is spectating - the service owns that state then.
	override void Open(bool showErrorNotification = true)
	{
		super.Open(showErrorNotification);

		if (!IsOpened() || EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			return;

		EC29_SetSpectatorNetMuted(true);
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
