//! EC29_VonActivityService - "who is talking right now", as heard by the LOCAL client.
//!
//! Exists for consumers that draw their own speaking indicators and therefore need DATA, not
//! vanilla's UI: vanilla SCR_NametagDisplay rejects anyone whose faction cannot be resolved from
//! the controlled entity, which is every non-groupmate as seen from a faction-less spectator
//! ghost - so a spectator sees speaking indicators for their old squad and nobody else. The
//! spectator mod's own nameplates cover every faction but have no notion of who is transmitting;
//! EC29 sits on exactly that, per packet, per player id, in EC29_VON_VoNComponent.OnReceive.
//! This service is that feed, organized: an edge-triggered invoker plus two pull queries.
//!
//! FED UNGATED, SERVED SPECTATOR-SCOPED. The feed hooks OnReceive before every policy gate
//! (faction, nametag range gating, mute) - a packet that arrived is a player talking, whatever
//! policy does with the audio. But recording only happens while EC29_SpectatorVonService says the
//! local player is spectating: the range gate on living players' nametag VON icons exists to stop
//! position leaks between the living, and this service must not become a side door around it. A
//! dead spectator has no position to protect from; a living player gets nothing new. (If a
//! living-player consumer is ever wanted, that gate is the one deliberate switch to revisit.)
//!
//! DECAY, NOT A STOP EVENT. Voice packets simply stop arriving when the sender releases the key -
//! there is no end event on the receive path - so "talking" falls off on a silence timeout since
//! the last packet. 300 ms rides through packet jitter without visibly lagging the real release.
//!
//! DELIVERY CONSTRAINT (part of the API contract, not an implementation detail): this only ever
//! reports what the local client actually RECEIVES. Direct speech delivery is gated by the
//! sender's component range - it works for spectators because the ghost body is parked at the
//! camera, so the spectator is physically present at what they are watching. Radio traffic on
//! nets the local machine is not tuned to never arrives, and no client-side API can change that.
//!
//! Owned by EC29_RadioState (world-scoped, dies with the world). The ticker follows the squelch
//! singleton's self-stopping pattern: feeds call EnsureTicking, the loop reschedules itself only
//! while entries exist and exits cleanly when the world is torn down under it.
class EC29_VonActivityEntry
{
	float m_fLastPacketMs;
	bool m_bIsRadio;
}

class EC29_VonActivityService
{
	//! Silence timeout before a player stops counting as talking. Wide enough for packet
	//! jitter, tight enough that the indicator drops with the real key release.
	protected static const float DECAY_MS = 300;
	protected static const int TICK_INTERVAL_MS = 100;

	protected ref map<int, ref EC29_VonActivityEntry> m_mTransmitting = new map<int, ref EC29_VonActivityEntry>();
	protected ref ScriptInvoker m_OnVonActivityChanged = new ScriptInvoker();
	protected bool m_bTicking;

	//------------------------------------------------------------------------------------------------
	//! Fires (int playerId, bool talking, bool isRadio) on every transition - true when the first
	//! packet lands, false when the decay expires. isRadio distinguishes radio traffic
	//! (receiver != null on the packet) from direct speech; for a talking player it reflects the
	//! most recent packet, so a sender keying a radio mid-sentence flips the flag without an
	//! intermediate stop.
	ScriptInvoker GetOnVonActivityChanged()
	{
		return m_OnVonActivityChanged;
	}

	//------------------------------------------------------------------------------------------------
	bool IsPlayerTransmitting(int playerId)
	{
		return m_mTransmitting.Contains(playerId);
	}

	//------------------------------------------------------------------------------------------------
	void GetTransmittingPlayers(notnull out array<int> outIds)
	{
		outIds.Clear();
		foreach (int playerId, EC29_VonActivityEntry entry : m_mTransmitting)
			outIds.Insert(playerId);
	}

	//------------------------------------------------------------------------------------------------
	//! The feed. Called from EC29_VON_VoNComponent.OnReceive for every packet the local client
	//! receives, before any policy gate. Cheap when not spectating: one flag read and out.
	void EC29_RecordVonPacket(int playerId, bool isRadio)
	{
		// Unresolvable senders (deploy-screen dead players and similar) carry no usable id -
		// nothing a consumer could name or mark.
		if (playerId <= 0)
			return;

		// Spectator-scoped on purpose - see the header. Entries already live keep decaying via
		// the ticker regardless, so leaving spectate mid-transmission ends cleanly.
		if (!EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		float nowMs = world.GetWorldTime();

		EC29_VonActivityEntry entry;
		if (!m_mTransmitting.Find(playerId, entry))
		{
			entry = new EC29_VonActivityEntry();
			entry.m_fLastPacketMs = nowMs;
			entry.m_bIsRadio = isRadio;
			m_mTransmitting.Set(playerId, entry);
			m_OnVonActivityChanged.Invoke(playerId, true, isRadio);
		}
		else
		{
			entry.m_fLastPacketMs = nowMs;
			entry.m_bIsRadio = isRadio;
		}

		EnsureTicking();
	}

	//------------------------------------------------------------------------------------------------
	protected void EnsureTicking()
	{
		if (m_bTicking)
			return;

		m_bTicking = true;
		GetGame().GetCallqueue().CallLater(EC29_TickLoop, TICK_INTERVAL_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_TickLoop()
	{
		// The self-rescheduling ticker can fire into world teardown; stop cleanly so the stale
		// instance drops out of the call queue (same rule as the squelch ticker).
		BaseWorld world = GetGame().GetWorld();
		if (!world)
		{
			m_bTicking = false;
			return;
		}

		float nowMs = world.GetWorldTime();

		array<int> expired = {};
		foreach (int playerId, EC29_VonActivityEntry entry : m_mTransmitting)
		{
			// Clock went backwards = a new world reused this instance's map somehow; treat as
			// expired rather than waiting out a future timestamp.
			if (nowMs < entry.m_fLastPacketMs || nowMs - entry.m_fLastPacketMs >= DECAY_MS)
				expired.Insert(playerId);
		}

		foreach (int playerId : expired)
		{
			EC29_VonActivityEntry entry;
			if (m_mTransmitting.Find(playerId, entry))
			{
				m_mTransmitting.Remove(playerId);
				m_OnVonActivityChanged.Invoke(playerId, false, entry.m_bIsRadio);
			}
		}

		if (!m_mTransmitting.IsEmpty())
			GetGame().GetCallqueue().CallLater(EC29_TickLoop, TICK_INTERVAL_MS, false);
		else
			m_bTicking = false;
	}
}
