//------------------------------------------------------------------------------------------------
//! Gates the over-head VON nametag icon by audible range.
//!
//! Vanilla SCR_NameTagData subscribes the LOCAL player's SCR_VoNComponent
//! m_OnReceivedVON invoker. When any incoming transmission arrives, it activates
//! ENameTagEntityState.VON on the speaker's nametag - which makes the orange
//! VON icon pop above their head.
//!
//! Without gating, an enemy whispering 30 m away (audio is silenced by our
//! range check) would still show the icon, which leaks their position.
//! We override OnReceivedVON to apply the same audibility check used by the
//! audio path before letting the state activation through.
//!
//! Radio transmissions (receiver != null) are never gated - radio reach is
//! defined by the radio's own range, not by direct VoN distance rules.
modded class SCR_NameTagData
{
	//------------------------------------------------------------------------------------------------
	override protected void OnReceivedVON(int playerId, BaseTransceiver receiver, int frequency, float quality)
	{
		// Only gate direct VoN. Radio always shows the icon regardless of distance.
		if (!receiver && EC29_ShouldGateNameTagVON(playerId))
		{
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][NameTag] VON icon suppressed for player %1 (out of audible range)", playerId);
			return;
		}

		super.OnReceivedVON(playerId, receiver, frequency, quality);
	}

	//------------------------------------------------------------------------------------------------
	//! Returns true if the speaker is out of audible range for the local listener
	//! according to current voice mode + EC29_VONSettingsComponent thresholds.
	//! Uses the shared audibility helper so audio + UI + nametag stay in lockstep.
	protected bool EC29_ShouldGateNameTagVON(int senderPlayerId)
	{
		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (!settings || !settings.GetGateNameTagVonByRange())
			return false;

		PlayerController localPc = GetGame().GetPlayerController();
		if (!localPc)
			return false;

		IEntity listener = localPc.GetControlledEntity();
		if (!listener)
			return false;

		return !settings.IsAudibleForListener(senderPlayerId, listener);
	}
}
