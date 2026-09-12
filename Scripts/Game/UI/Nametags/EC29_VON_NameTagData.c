//------------------------------------------------------------------------------------------------
//! Gates the over-head VON nametag icon by audible range.
//!
//! Vanilla SCR_NameTagData subscribes the LOCAL player's SCR_VoNComponent
//! m_OnReceivedVON invoker. When any incoming transmission arrives, it activates
//! ENameTagEntityState.VON on the speaker's nametag - which makes the orange
//! VON icon pop above their head.
//!
//! Without gating, an enemy whispering 30 m away (inaudible - the whisper tier's ACP stops at
//! 6 m) would still show the icon, which leaks their position. We override OnReceivedVON and
//! only let the state activation through when the speaker is inside the outer range of the
//! tier they transmit on (EC29_VONSettingsComponent.IsAudibleForListener).
//!
//! Radio transmissions (receiver != null) are never gated - radio reach is
//! defined by the radio's own range, not by direct VoN distance rules.
modded class SCR_NameTagData
{
	//! Debug print throttle: this fires per voice PACKET (dozens/second during
	//! speech), so log only on the suppressed<->shown transition per speaker -
	//! field logs showed the unthrottled print at 2,273 lines/minute.
	protected static ref map<int, bool> s_mEC29DbgSuppressed = new map<int, bool>();

	//------------------------------------------------------------------------------------------------
	override protected void OnReceivedVON(int playerId, BaseTransceiver receiver, int frequency, float quality)
	{
		// Only gate direct VoN. Radio always shows the icon regardless of distance.
		if (!receiver && EC29_ShouldGateNameTagVON(playerId))
		{
			if (EC29_Debug.VERBOSE)
			{
				bool wasSuppressed;
				if (!s_mEC29DbgSuppressed.Find(playerId, wasSuppressed) || !wasSuppressed)
				{
					s_mEC29DbgSuppressed.Set(playerId, true);
					PrintFormat("[EC29-DBG][NameTag] VON icon suppressed for player %1 (out of audible range)", playerId);
				}
			}
			return;
		}

		if (EC29_Debug.VERBOSE)
			s_mEC29DbgSuppressed.Set(playerId, false);

		super.OnReceivedVON(playerId, receiver, frequency, quality);
	}

	//------------------------------------------------------------------------------------------------
	//! Returns true if the speaker is outside the outer range of their transmit tier as seen
	//! from the local listener. Shared with the overlay so both visuals stop where the audio does.
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
