//------------------------------------------------------------------------------------------------
//! Keeps the over-head speaking icon honest about direct-speech reach.
//!
//! Vanilla lights a speaker's nametag VON state for ANY packet the local player receives from
//! them - including one from a whisperer 30 m away who is inaudible there. With the mission's
//! m_bGateNameTagVonByRange on, a direct packet from a speaker beyond their tier's outer radius
//! is swallowed before vanilla sees it. Radio packets are never gated (reach is the radio's job).
//!
//! Unlike the overlay this gate has no spectator / open-editor exemption: only the flag and a
//! controlled local entity decide it.
modded class SCR_NameTagData
{
	//! Speakers whose icon is currently being held back, for the transition-only debug line.
	//! Per-packet logging here ran at ~2,273 lines/minute in the field - keep it edge-triggered.
	protected static ref map<int, bool> s_mEC29_HeldBack = new map<int, bool>();

	//------------------------------------------------------------------------------------------------
	override protected void OnReceivedVON(int playerId, BaseTransceiver receiver, int frequency, float quality)
	{
		if (!receiver && EC29_IsBeyondEarshot(playerId))
		{
			if (EC29_Debug.VERBOSE && !s_mEC29_HeldBack.Contains(playerId))
			{
				s_mEC29_HeldBack.Set(playerId, true);
				PrintFormat("[EC29-DBG][NameTag] VON icon suppressed for player %1 (out of audible range)", playerId);
			}

			return;
		}

		if (EC29_Debug.VERBOSE)
			s_mEC29_HeldBack.Remove(playerId);

		super.OnReceivedVON(playerId, receiver, frequency, quality);
	}

	//------------------------------------------------------------------------------------------------
	protected bool EC29_IsBeyondEarshot(int playerId)
	{
		EC29_VONSettingsComponent policy = EC29_VONSettingsComponent.GetInstance();
		if (!policy || !policy.GetGateNameTagVonByRange())
			return false;

		PlayerController localController = GetGame().GetPlayerController();
		if (!localController)
			return false;

		IEntity listener = localController.GetControlledEntity();
		if (!listener)
			return false;

		return !policy.IsAudibleForListener(playerId, listener);
	}
}
