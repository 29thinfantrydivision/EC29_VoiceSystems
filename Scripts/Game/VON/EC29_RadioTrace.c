//! Radio / VON diagnostic trace, VERBOSE only.
//!
//! WHY. Two field reports share one shape: "British radios can't hear each other" and
//! "nobody could hear each other at drill start". Nothing in the RPT said what the radios
//! were actually tuned to, which transceiver received, who was expected to hear whom, or
//! whether a relay was even on the net - the existing [EC29-DBG] chain covers EC29's own
//! frequency dialog, tiers, squelch and key-state, but vanilla scroll tuning
//! (SCR_VONEntryRadio), the knob (SCR_RadioTuningUserAction) and the spawn group tuning
//! (SCR_GroupsManagerComponent.TunePlayersFrequency) were silent, as was every radio's
//! configuration. One known cause is already in hand: the British handheld (Radio_PRC349)
//! carries no "Frequency resolution" and so steps on the engine default 500 kHz while the
//! backpack (Radio_PRC351) steps 200 kHz - only whole-MHz frequencies land on both grids. The
//! GRID MISMATCH line below is that case made visible.
//!
//! WHAT. Every line is tagged [EC29-DBG][RadioTrace] and gated on EC29_Debug.VERBOSE at the
//! call site AND in here, so a release build (VERBOSE = false) evaluates nothing - every
//! entry point returns before any lookup or array is built. Per-packet callers are
//! rate-limited per speaker (the listener is always the local player, so per speaker is per
//! speaker-listener pair). The hooks live in modded classes next to the vanilla site they
//! observe:
//!   - SCR_RadioComponent (gadget): mode switch (spawn / re-kit / pickup), frequency, power and
//!     encryption-key change invokers -> state dump of the radio.
//!   - SCR_VONController: entry registration, key-up (TX line, engine view, capture health),
//!     controlled-entity change dump (EC29_VON_VONController.c).
//!   - SCR_VoNComponent: reception (ACTUAL hearers, listener modifiers), audio variables,
//!     server packet trace (EC29_VON_VoNComponent.c).
//!   - EC29_RFPropagationNetworkComponent: server radio key-up -> EXPECTED hearers; client
//!     key-state -> transmitter range for the BEYOND RANGE check.
//!   - SCR_GroupsManagerComponent: server-side group frequency tuning at spawn.
//!   - SCR_AntennaServicePointComponent: relay post-init / faction retune + relay registry.
//!   - SCR_BaseGameMode: RadioManagerEntity presence (EC29_RadioSystemGuard.c).
//!
//! EXPECTED vs ACTUAL. The server logs, per transmission start, who SHOULD hear it (players
//! inside the tier's outer range for direct speech; players holding a powered radio on the
//! same frequency and key, with distance against the transmitter's range and any relay on
//! that frequency, for radio). Every client logs what it ACTUALLY received. Diff the two.
//!
//! RADIO MANAGER. ChimeraWorld.GetRadioManager() can hand back a non-null native stub on a
//! manager-less world and calling anything on it is a CTD (EC29_RadioSystemGuard.c). The
//! engine-view line therefore never touches that getter: the manager is located ONCE per
//! world by a world entity query, and GetTransceiversInRange is called only on an entity the
//! query actually returned.
//!
//! NO NEW VoN COMPONENT CLASSES. Component order is by class name and the first VoN
//! component on an entity is the ear (EC29_VoiceTiers.c); this file adds only plain script
//! classes and modded non-VoN classes.

//------------------------------------------------------------------------------------------------
//! What a client last heard about a transmitter's radio key-up (from the server key-state
//! broadcast): the only place the TX transceiver's range reaches a listener.
class EC29_RadioTraceKeyedSender
{
	int m_iFrequency;
	float m_fRange;
	vector m_vPos;
	bool m_bKeyed;
}

class EC29_RadioTrace
{
	static const string TAG = "[EC29-DBG][RadioTrace]";

	//! Per-packet paths log once per speaker per second.
	protected static const int RX_LOG_INTERVAL_MS = 1000;
	//! A packet gap longer than this starts a new transmission (server expected-hearer trace).
	protected static const int TX_START_GAP_MS = 1000;
	//! A key-up on a frequency with no packet received for this long gets a full state dump.
	protected static const int QUIET_NET_MS = 60000;
	//! Direct-speech range check tolerance: the ACP radius and the mirror constant are not
	//! byte-identical, and positions are a frame apart.
	protected static const float DIRECT_RANGE_TOLERANCE_M = 2.0;
	//! Radius of the one-shot world query that locates the RadioManagerEntity.
	protected static const float MANAGER_QUERY_RADIUS_M = 100000;

	protected static ref map<int, float> s_mLastRxLogMs = new map<int, float>();
	protected static ref map<int, float> s_mLastVarLogMs = new map<int, float>();
	protected static ref map<int, float> s_mLastServerPacketMs = new map<int, float>();
	//! Last packet received per frequency on this client (every packet, no throttle).
	protected static ref map<int, float> s_mLastRxMsByFreq = new map<int, float>();
	protected static ref map<int, ref EC29_RadioTraceKeyedSender> s_mKeyedSenders = new map<int, ref EC29_RadioTraceKeyedSender>();

	//! Relay radios (RelayTransceiver carriers) that registered through the antenna component.
	//! Weak handles: a deleted antenna nulls out and is swept on the next use.
	protected static ref array<BaseRadioComponent> s_aRelays = {};

	//! RadioManagerEntity located by world query: 0 = not looked up yet, 1 = found, 2 = absent.
	protected static int s_iManagerLookupState;
	protected static RadioManagerEntity s_RadioManager;
	//! Set by the manager's own constructor. Weak handle, so a deleted manager nulls it; kept
	//! out of the world reset because the constructor can run before the new world is current.
	protected static RadioManagerEntity s_RegisteredManager;

	//! World-scoped reset for the static maps (world clocks restart, statics do not).
	protected static BaseWorld s_OwnerWorld;

	//------------------------------------------------------------------------------------------------
	protected static void CheckWorldReset()
	{
		BaseWorld currentWorld = GetGame().GetWorld();
		if (s_OwnerWorld == currentWorld)
			return;

		s_OwnerWorld = currentWorld;
		s_mLastRxLogMs.Clear();
		s_mLastVarLogMs.Clear();
		s_mLastServerPacketMs.Clear();
		s_mLastRxMsByFreq.Clear();
		s_mKeyedSenders.Clear();
		s_aRelays.Clear();
		s_iManagerLookupState = 0;
		s_RadioManager = null;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the per-key throttle allows a line now; stamps the key when it does.
	protected static bool Allow(notnull map<int, float> lastMs, int key, int intervalMs)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		CheckWorldReset();

		float nowMs = world.GetWorldTime();
		float last;
		if (lastMs.Find(key, last) && nowMs - last < intervalMs)
			return false;

		lastMs.Set(key, nowMs);
		return true;
	}

	//! ------------------------------------------------------------------------------------------
	//! Labels
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Prefab resource name of an entity, or a placeholder - never throws on a null entity or an
	//! entity spawned without prefab data.
	static string PrefabName(IEntity entity)
	{
		if (!entity)
			return "<null entity>";

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return "<no prefab data>";

		string name = prefabData.GetPrefabName();
		if (name.IsEmpty())
			return "<unnamed prefab>";

		return name;
	}

	//------------------------------------------------------------------------------------------------
	//! Player id owning a radio, resolved from the gadget's character owner. -1 when the radio is
	//! on the ground, on an AI, or on a non-gadget entity (relay, vehicle, editor manager).
	static int OwnerPlayerId(BaseRadioComponent radio)
	{
		if (!radio)
			return -1;

		IEntity owner = radio.GetOwner();
		if (!owner)
			return -1;

		SCR_GadgetComponent gadget = SCR_GadgetComponent.Cast(owner.FindComponent(SCR_GadgetComponent));
		if (!gadget)
			return -1;

		IEntity character = gadget.GetCharacterOwner();
		if (!character)
			return -1;

		return GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(character);
	}

	//------------------------------------------------------------------------------------------------
	//! True when the radio sits on the locally controlled entity.
	static bool IsLocalPlayerRadio(BaseRadioComponent radio)
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
			return false;

		return OwnerPlayerId(radio) == pc.GetPlayerId();
	}

	//------------------------------------------------------------------------------------------------
	static string PlayerLabel(int playerId)
	{
		if (playerId <= 0)
			return "pid=-1";

		return string.Format("pid=%1 '%2'", playerId, GetGame().GetPlayerManager().GetPlayerName(playerId));
	}

	//------------------------------------------------------------------------------------------------
	//! Local player's id and name, or "<no local player>" on a dedicated server.
	static string LocalPlayerLabel()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return "<no local player>";

		return PlayerLabel(pc.GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	//! Metres between two players' controlled entities, -1 when either is unknown here (a
	//! speaker outside replication relevance has no entity on this client).
	static float PlayerDistance(int playerIdA, int playerIdB)
	{
		PlayerManager pm = GetGame().GetPlayerManager();
		IEntity a = pm.GetPlayerControlledEntity(playerIdA);
		IEntity b = pm.GetPlayerControlledEntity(playerIdB);
		if (!a || !b)
			return -1;

		return vector.Distance(a.GetOrigin(), b.GetOrigin());
	}

	//------------------------------------------------------------------------------------------------
	static string DistanceLabel(float distance)
	{
		if (distance < 0)
			return "dist=?";

		return string.Format("dist=%1m", Math.Round(distance));
	}

	//------------------------------------------------------------------------------------------------
	//! Replicated voice tier of a player's stock VoN component; NORMAL when unresolvable.
	static EC29_EVoiceRange PlayerTier(int playerId, out bool resolved)
	{
		resolved = false;
		SCR_VoNComponent stock = SCR_VoNComponent.EC29_GetVoNForPlayer(playerId);
		if (!stock)
			return EC29_EVoiceRange.NORMAL;

		resolved = true;
		return stock.EC29_GetVoiceRange();
	}

	//------------------------------------------------------------------------------------------------
	static string TierLabel(int playerId)
	{
		bool resolved;
		EC29_EVoiceRange tier = PlayerTier(playerId, resolved);
		string label = typename.EnumToString(EC29_EVoiceRange, tier);
		if (!resolved)
			label = label + "?";

		return label;
	}

	//------------------------------------------------------------------------------------------------
	//! What kind of thing a speaker is talking from. The engine flags editor senders itself
	//! (isSenderEditor on OnReceive); EC29 splits those into GM and spectator voice from the
	//! manager's replicated flag. Characters are split by vehicle occupancy because a mounted
	//! character may be on a vehicle radio / intercom rather than a carried set.
	static string SourceTypeLabel(int playerId, bool isSenderEditor)
	{
		if (isSenderEditor)
		{
			SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
			if (core)
			{
				SCR_EditorManagerEntity mgr = core.GetEditorManager(playerId);
				if (mgr && mgr.EC29_IsSpectatorVoice())
					return "SPECTATOR_MANAGER";
			}

			return "GM_MANAGER";
		}

		IEntity entity = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!entity)
			return "NO_ENTITY";

		ChimeraCharacter character = ChimeraCharacter.Cast(entity);
		if (!character)
			return string.Format("OTHER(%1)", entity.Type());

		if (character.IsInVehicle())
			return "CHARACTER_IN_VEHICLE";

		return "CHARACTER";
	}

	//------------------------------------------------------------------------------------------------
	//! Source type of the LOCAL transmitter: the spectator service / GM manager when the local
	//! editor manager is the voice, otherwise the controlled entity.
	static string LocalSourceTypeLabel()
	{
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return "NO_LOCAL_PLAYER";

		if (EC29_RadioState.GetInstance().SpectatorVon().IsSpectating())
			return "SPECTATOR_MANAGER";

		SCR_EditorManagerEntity mgr = SCR_EditorManagerEntity.GetInstance();
		if (mgr && mgr.IsOpened())
			return "GM_MANAGER";

		return SourceTypeLabel(pc.GetPlayerId(), false);
	}

	//------------------------------------------------------------------------------------------------
	//! What entity hosts a radio: a carried gadget, a vehicle (vehicle radio / intercom), an
	//! editor manager, a relay antenna, or something else.
	static string RadioHostLabel(BaseRadioComponent radio)
	{
		if (!radio)
			return "NO_RADIO";

		IEntity owner = radio.GetOwner();
		if (!owner)
			return "NO_OWNER";

		if (owner.FindComponent(SCR_GadgetComponent))
			return "GADGET";

		if (Vehicle.Cast(owner))
			return "VEHICLE";

		if (SCR_EditorManagerEntity.Cast(owner))
			return "EDITOR_MANAGER";

		if (owner.FindComponent(SCR_AntennaServicePointComponent))
			return "RELAY";

		return string.Format("OTHER(%1)", owner.Type());
	}

	//------------------------------------------------------------------------------------------------
	//! Index of a transceiver inside its radio, -1 when not found.
	static int TransceiverIndex(BaseRadioComponent radio, BaseTransceiver transceiver)
	{
		if (!radio || !transceiver)
			return -1;

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			if (radio.GetTransceiver(i) == transceiver)
				return i;
		}

		return -1;
	}

	//------------------------------------------------------------------------------------------------
	//! First transceiver of a radio tuned to a frequency, or null.
	static BaseTransceiver FindTransceiverOnFrequency(BaseRadioComponent radio, int frequency)
	{
		if (!radio)
			return null;

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			BaseTransceiver tsv = radio.GetTransceiver(i);
			if (tsv && tsv.GetFrequency() == frequency)
				return tsv;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Gadget type label (RADIO / RADIO_BACKPACK) of the radio's owning entity, or "-" when the
	//! entity carries no gadget component (relays, vehicles, editor radios).
	static string GadgetTypeLabel(BaseRadioComponent radio)
	{
		if (!radio)
			return "-";

		IEntity owner = radio.GetOwner();
		if (!owner)
			return "-";

		SCR_GadgetComponent gadget = SCR_GadgetComponent.Cast(owner.FindComponent(SCR_GadgetComponent));
		if (!gadget)
			return "-";

		return typename.EnumToString(EGadgetType, gadget.GetType());
	}

	//------------------------------------------------------------------------------------------------
	//! One line per transceiver: index, class, frequency, tunable band, resolution, range, mute.
	static string TransceiverLine(BaseRadioComponent radio, int index)
	{
		BaseTransceiver tsv = radio.GetTransceiver(index);
		if (!tsv)
			return string.Format("tsv[%1]=<null>", index);

		return string.Format("tsv[%1] %2 freq=%3 min=%4 max=%5 res=%6 range=%7m muted=%8",
			index, tsv.Type(), tsv.GetFrequency(), tsv.GetMinFrequency(), tsv.GetMaxFrequency(),
			tsv.GetFrequencyResolution(), tsv.GetRange(), tsv.IsMuted());
	}

	//------------------------------------------------------------------------------------------------
	//! Hearing-side modifiers of the local listener at this moment: earplugs, life state, and
	//! for radio packets the receiving transceiver's mute, EC29 squelch state, ear routing and
	//! channel volume.
	static string ListenerModifiersLabel(BaseTransceiver receiver, int frequency)
	{
		string earplugs = "earplugs=<no system>";
		EC29_Earplugs_System plugs = EC29_Earplugs_System.Instance;
		if (plugs)
			earplugs = string.Format("earplugs=%1 sfxVol=%2", plugs.bMuted, plugs.GetVolume());

		string life = "life=<no character>";
		PlayerController pc = GetGame().GetPlayerController();
		if (pc)
		{
			ChimeraCharacter character = ChimeraCharacter.Cast(pc.GetControlledEntity());
			if (character)
			{
				CharacterControllerComponent controller = character.GetCharacterController();
				if (controller)
					life = string.Format("life=%1 unconscious=%2", typename.EnumToString(ECharacterLifeState, controller.GetLifeState()), controller.IsUnconscious());
			}
		}

		if (!receiver)
			return earplugs + " " + life;

		EC29_RadioState state = EC29_RadioState.GetInstance();
		EC29_RadioEarSettings ear = state.EarSettings();
		return string.Format("%1 %2 rxMuted=%3 squelch=%4 earRouting=%5 channelVol=%6",
			earplugs, life, receiver.IsMuted(), state.Squelch().EC29_GetChannelStateLabel(frequency),
			typename.EnumToString(EC29_EEarRouting, ear.GetRouting(receiver)), ear.GetVolume(receiver));
	}

	//! ------------------------------------------------------------------------------------------
	//! Radio state dumps
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Full state of one radio: owner, host, prefab, gadget type, power, key, then every
	//! transceiver. The owner id is resolved from the gadget when the caller passes -1.
	static void DumpRadio(string reason, BaseRadioComponent radio, int ownerPlayerId = -1)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		if (!radio)
		{
			PrintFormat("%1 radio state (%2): <null radio>", TAG, reason);
			return;
		}

		if (ownerPlayerId < 0)
			ownerPlayerId = OwnerPlayerId(radio);

		// string.Format / PrintFormat take at most nine placeholders - long lines are composed.
		string head = string.Format("%1 radio state (%2): owner %3 host=%4 prefab=%5 gadget=%6", TAG, reason, PlayerLabel(ownerPlayerId), RadioHostLabel(radio), PrefabName(radio.GetOwner()), GadgetTypeLabel(radio));
		PrintFormat("%1 powered=%2 key='%3' editorRadio=%4 transceivers=%5", head, radio.IsPowered(), radio.GetEncryptionKey(), radio.IsEditorRadio(), radio.TransceiversCount());

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			PrintFormat("%1   %2", TAG, TransceiverLine(radio, i));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Every radio (handheld + backpack) carried by an entity, from its gadget manager. Empty
	//! when the entity has no gadget manager.
	static void CollectRadios(IEntity entity, notnull array<BaseRadioComponent> outRadios)
	{
		if (!entity)
			return;

		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(entity);
		if (!gadgetManager)
			return;

		array<SCR_GadgetComponent> gadgets = {};
		array<SCR_GadgetComponent> found = gadgetManager.GetGadgetsByType(EGadgetType.RADIO);
		if (found)
			gadgets.InsertAll(found);

		found = gadgetManager.GetGadgetsByType(EGadgetType.RADIO_BACKPACK);
		if (found)
			gadgets.InsertAll(found);

		foreach (SCR_GadgetComponent gadget : gadgets)
		{
			if (!gadget)
				continue;

			IEntity gadgetEntity = gadget.GetOwner();
			if (!gadgetEntity)
				continue;

			BaseRadioComponent radio = BaseRadioComponent.Cast(gadgetEntity.FindComponent(BaseRadioComponent));
			if (radio && !outRadios.Contains(radio))
				outRadios.Insert(radio);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! State dump of every radio the local player carries plus the local tier and source type,
	//! followed by the tuning-grid check: two radios whose transceiver 0 resolutions differ can
	//! only meet on frequencies that are multiples of both (the British PRC349 500 kHz vs PRC351
	//! 200 kHz case).
	static void DumpLocalRadios(string reason)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity controlled = pc.GetControlledEntity();
		if (!controlled)
		{
			PrintFormat("%1 local radios (%2): %3 no controlled entity source=%4", TAG, reason, PlayerLabel(pc.GetPlayerId()), LocalSourceTypeLabel());
			return;
		}

		array<BaseRadioComponent> radios = {};
		CollectRadios(controlled, radios);

		PrintFormat("%1 local radios (%2): %3 entity=%4 source=%5 tier=%6 radios=%7", TAG, reason, PlayerLabel(pc.GetPlayerId()), PrefabName(controlled), LocalSourceTypeLabel(), TierLabel(pc.GetPlayerId()), radios.Count());

		foreach (BaseRadioComponent radio : radios)
		{
			DumpRadio(reason, radio, pc.GetPlayerId());
		}

		ReportGridMismatch(radios);
	}

	//------------------------------------------------------------------------------------------------
	//! Flags every pair of carried radios whose transceiver 0 tuning grids differ.
	static void ReportGridMismatch(notnull array<BaseRadioComponent> radios)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		for (int a = 0; a < radios.Count(); a++)
		{
			BaseRadioComponent radioA = radios[a];
			if (!radioA || radioA.TransceiversCount() == 0)
				continue;

			BaseTransceiver tsvA = radioA.GetTransceiver(0);
			if (!tsvA)
				continue;

			for (int b = a + 1; b < radios.Count(); b++)
			{
				BaseRadioComponent radioB = radios[b];
				if (!radioB || radioB.TransceiversCount() == 0)
					continue;

				BaseTransceiver tsvB = radioB.GetTransceiver(0);
				if (!tsvB)
					continue;

				int resA = tsvA.GetFrequencyResolution();
				int resB = tsvB.GetFrequencyResolution();
				if (resA == resB)
					continue;

				PrintFormat("%1 GRID MISMATCH res %2/%3 kHz between %4 (freq=%5) and %6 (freq=%7) - only frequencies on both grids can meet (e.g. whole MHz for 500/200)",
					TAG, resA, resB, PrefabName(radioA.GetOwner()), tsvA.GetFrequency(), PrefabName(radioB.GetOwner()), tsvB.GetFrequency());
			}
		}
	}

	//! ------------------------------------------------------------------------------------------
	//! Relays
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Registers a relay-carrying radio (client and server - antenna entities replicate).
	static void RegisterRelay(BaseRadioComponent radio)
	{
		if (!EC29_Debug.VERBOSE || !radio)
			return;

		CheckWorldReset();
		SweepRelays();

		if (!s_aRelays.Contains(radio))
			s_aRelays.Insert(radio);
	}

	//------------------------------------------------------------------------------------------------
	protected static void SweepRelays()
	{
		for (int i = s_aRelays.Count() - 1; i >= 0; i--)
		{
			BaseRadioComponent relay = s_aRelays[i];
			if (!relay || !relay.GetOwner() || relay.GetOwner().IsDeleted())
				s_aRelays.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Describes the relays on a frequency relative to a transmitter and a listener: for each,
	//! distance from both ends against the relay's range. Sets bridged when at least one relay
	//! on the frequency has both ends inside its range. Empty string when no relay is on the net.
	static string RelayReport(int frequency, vector txPos, bool hasTxPos, vector rxPos, bool hasRxPos, out bool bridged)
	{
		bridged = false;
		SweepRelays();

		string report;
		foreach (BaseRadioComponent relay : s_aRelays)
		{
			if (!relay.IsPowered())
				continue;

			BaseTransceiver tsv = FindTransceiverOnFrequency(relay, frequency);
			if (!tsv)
				continue;

			vector relayPos = relay.GetOwner().GetOrigin();
			float range = tsv.GetRange();
			float txDist = -1;
			float rxDist = -1;
			if (hasTxPos)
				txDist = vector.Distance(txPos, relayPos);
			if (hasRxPos)
				rxDist = vector.Distance(rxPos, relayPos);

			bool txIn = hasTxPos && txDist <= range;
			bool rxIn = hasRxPos && rxDist <= range;
			if (txIn && rxIn)
				bridged = true;

			report = string.Format("%1 relay %2 key='%3' range=%4m tx%5 rx%6 bridges=%7;", report, PrefabName(relay.GetOwner()), relay.GetEncryptionKey(), range, DistanceLabel(txDist), DistanceLabel(rxDist), txIn && rxIn);
		}

		return report;
	}

	//------------------------------------------------------------------------------------------------
	//! A relay radio (RelayTransceiver on an antenna / service point entity).
	static void LogRelay(string reason, IEntity owner, BaseRadioComponent radio)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		string factionKey = "<no faction component>";
		if (owner)
		{
			FactionAffiliationComponent affiliation = FactionAffiliationComponent.Cast(owner.FindComponent(FactionAffiliationComponent));
			if (affiliation)
			{
				Faction faction = affiliation.GetAffiliatedFaction();
				if (faction)
					factionKey = faction.GetFactionKey();
				else
					factionKey = "<none>";
			}
		}

		if (!radio)
		{
			PrintFormat("%1 relay (%2): prefab=%3 faction=%4 <no BaseRadioComponent>", TAG, reason, PrefabName(owner), factionKey);
			return;
		}

		PrintFormat("%1 relay (%2): prefab=%3 faction=%4 key='%5' powered=%6 pos=%7 transceivers=%8",
			TAG, reason, PrefabName(owner), factionKey, radio.GetEncryptionKey(), radio.IsPowered(), owner.GetOrigin(), radio.TransceiversCount());

		for (int i = 0, count = radio.TransceiversCount(); i < count; i++)
		{
			PrintFormat("%1   %2", TAG, TransceiverLine(radio, i));
		}
	}

	//! ------------------------------------------------------------------------------------------
	//! Client: key-state from the server, reception (ACTUAL hearers), audio variables
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Server key-state broadcast as received on this machine: remembers the transmitter's
	//! frequency, range and position so a later packet can be checked against the TX range.
	static void OnRemoteKeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		CheckWorldReset();

		EC29_RadioTraceKeyedSender sender;
		if (!s_mKeyedSenders.Find(senderPlayerId, sender))
		{
			sender = new EC29_RadioTraceKeyedSender();
			s_mKeyedSenders.Set(senderPlayerId, sender);
		}

		if (keyed)
		{
			sender.m_iFrequency = frequency;
			sender.m_fRange = range;
			sender.m_vPos = senderPos;
		}
		sender.m_bKeyed = keyed;
	}

	//------------------------------------------------------------------------------------------------
	//! Incoming voice packet as seen by the local ear - one ACTUAL hearer line, once per speaker
	//! per second. Direct packets are checked against the speaker's replicated tier range, radio
	//! packets against the transmitter's range (from the key-state broadcast) and the relays.
	static void LogReception(int speakerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		// Per-frequency last-RX stamp sees every radio packet (feeds the quiet-net key-up dump).
		CheckWorldReset();
		if (receiver)
			s_mLastRxMsByFreq.Set(frequency, world.GetWorldTime());

		if (!Allow(s_mLastRxLogMs, speakerId, RX_LOG_INTERVAL_MS))
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		int listenerId = pc.GetPlayerId();
		float distance = PlayerDistance(speakerId, listenerId);
		string sourceType = SourceTypeLabel(speakerId, isSenderEditor);

		if (!receiver)
		{
			bool tierResolved;
			EC29_EVoiceRange tier = PlayerTier(speakerId, tierResolved);
			float outer = EC29_VoiceTiers.OuterRange(tier);

			string directFlag;
			if (distance >= 0 && distance > outer + DIRECT_RANGE_TOLERANCE_M)
				directFlag = " OUT OF TIER RANGE";

			string directHead = string.Format("%1 RX direct: speaker %2 source=%3 listener %4 %5", TAG, PlayerLabel(speakerId), sourceType, PlayerLabel(listenerId), DistanceLabel(distance));
			PrintFormat("%1 tier=%2 outer=%3m quality=%4 listenerMods[%5]%6", directHead, TierLabel(speakerId), outer, quality, ListenerModifiersLabel(null, frequency), directFlag);
			return;
		}

		BaseRadioComponent radio = receiver.GetRadio();
		string radioPrefab = "<no radio>";
		string key = "";
		int index = -1;
		if (radio)
		{
			radioPrefab = PrefabName(radio.GetOwner());
			key = radio.GetEncryptionKey();
			index = TransceiverIndex(radio, receiver);
		}

		// Transmitter side, from the last key-state broadcast for this speaker.
		string txLabel = "txRange=? (no key-state seen)";
		float txRange = -1;
		vector txPos = vector.Zero;
		bool hasTxPos = false;
		EC29_RadioTraceKeyedSender sender;
		if (s_mKeyedSenders.Find(speakerId, sender))
		{
			txRange = sender.m_fRange;
			txPos = sender.m_vPos;
			hasTxPos = true;
			txLabel = string.Format("txFreq=%1 txRange=%2m txKeyed=%3", sender.m_iFrequency, sender.m_fRange, sender.m_bKeyed);
		}
		else
		{
			IEntity speaker = GetGame().GetPlayerManager().GetPlayerControlledEntity(speakerId);
			if (speaker)
			{
				txPos = speaker.GetOrigin();
				hasTxPos = true;
			}
		}

		vector rxPos = vector.Zero;
		bool hasRxPos = false;
		IEntity listener = pc.GetControlledEntity();
		if (listener)
		{
			rxPos = listener.GetOrigin();
			hasRxPos = true;
		}

		bool bridged;
		string relays = RelayReport(frequency, txPos, hasTxPos, rxPos, hasRxPos, bridged);
		if (relays.IsEmpty())
			relays = " no relay on freq";

		string flag;
		if (distance >= 0 && txRange >= 0 && distance > txRange && !bridged)
			flag = " RADIO BEYOND RANGE";

		string head = string.Format("%1 RX radio: speaker %2 source=%3 listener %4 %5 packetFreq=%6", TAG, PlayerLabel(speakerId), sourceType, PlayerLabel(listenerId), DistanceLabel(distance), frequency);
		string rx = string.Format("rx[host=%1 tsv=%2 freq=%3 res=%4 key='%5' range=%6m radio=%7]", RadioHostLabel(radio), index, receiver.GetFrequency(), receiver.GetFrequencyResolution(), key, receiver.GetRange(), radioPrefab);
		PrintFormat("%1 %2 %3 quality=%4 listenerMods[%5] relays:%6%7", head, rx, txLabel, quality, ListenerModifiersLabel(receiver, frequency), relays, flag);
	}

	//------------------------------------------------------------------------------------------------
	//! The global audio variables written for a radio packet, and which net drove them. These are
	//! GLOBAL (last writer wins across every playing stream), which is exactly what this line is
	//! for. Once per speaker per second; a value of -1 means that variable was not written
	//! (its conf lookup failed at the probe).
	static void LogAudioVars(int speakerId, BaseTransceiver receiver, float earRouting, float signalQuality, float jamStrength, float channelVolume)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		if (!Allow(s_mLastVarLogMs, speakerId, RX_LOG_INTERVAL_MS))
			return;

		string radioPrefab = "<no radio>";
		int frequency = -1;
		if (receiver)
		{
			frequency = receiver.GetFrequency();
			BaseRadioComponent radio = receiver.GetRadio();
			if (radio)
				radioPrefab = PrefabName(radio.GetOwner());
		}

		PrintFormat("%1 audio vars written for speaker %2 via freq=%3 radio=%4: EC29_EarRouting=%5 EC29_SignalQuality=%6 EC29_JamStrength=%7 EC29_ChannelVolume=%8 (global - last writer wins)",
			TAG, PlayerLabel(speakerId), frequency, radioPrefab, earRouting, signalQuality, jamStrength, channelVolume);
	}

	//! ------------------------------------------------------------------------------------------
	//! Transmit side
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! Key-up from the local controller: what is about to go out, from what, on which radio. For
	//! a radio key-up this also triggers the engine-view line and, on a net that has been silent
	//! for QUIET_NET_MS, a full local state dump.
	static void LogTransmit(SCR_VONEntry entry, EC29_EVoiceRange tier, EVONTransmitType transmitType)
	{
		if (!EC29_Debug.VERBOSE || !entry)
			return;

		SCR_VONEntryRadio radioEntry = SCR_VONEntryRadio.Cast(entry);
		if (!radioEntry)
		{
			PrintFormat("%1 TX key-up: speaker %2 source=%3 method=DIRECT type=%4 tier=%5 outer=%6m usable=%7",
				TAG, LocalPlayerLabel(), LocalSourceTypeLabel(), typename.EnumToString(EVONTransmitType, transmitType),
				typename.EnumToString(EC29_EVoiceRange, tier), EC29_VoiceTiers.OuterRange(tier), entry.IsUsable());
			return;
		}

		BaseTransceiver tsv = radioEntry.GetTransceiver();
		if (!tsv)
		{
			PrintFormat("%1 TX key-up: speaker %2 entry freq=%3 has no transceiver", TAG, LocalPlayerLabel(), radioEntry.GetEntryFrequency());
			return;
		}

		BaseRadioComponent radio = tsv.GetRadio();
		string radioPrefab = "<no radio>";
		string key = "";
		bool powered = false;
		int index = -1;
		if (radio)
		{
			radioPrefab = PrefabName(radio.GetOwner());
			key = radio.GetEncryptionKey();
			powered = radio.IsPowered();
			index = TransceiverIndex(radio, tsv);
		}

		int frequency = tsv.GetFrequency();
		string head = string.Format("%1 TX key-up: speaker %2 source=%3 method=RADIO entryFreq=%4 tsvFreq=%5 tsv=%6 host=%7 radio=%8", TAG, LocalPlayerLabel(), LocalSourceTypeLabel(), radioEntry.GetEntryFrequency(), frequency, index, RadioHostLabel(radio), radioPrefab);
		PrintFormat("%1 key='%2' powered=%3 usable=%4 type=%5 tier=%6 range=%7m", head, key, powered, radioEntry.IsUsable(),
			typename.EnumToString(EVONTransmitType, transmitType), typename.EnumToString(EC29_EVoiceRange, tier), tsv.GetRange());

		LogEngineView(frequency, tsv.GetRange());

		// Quiet net: nothing received on this frequency for a minute (or ever) and we are keying
		// it - the full local picture is worth having next to the key-up.
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		CheckWorldReset();
		float nowMs = world.GetWorldTime();
		float lastRxMs;
		string quiet;
		if (!s_mLastRxMsByFreq.Find(frequency, lastRxMs))
			quiet = "never";
		else if (nowMs - lastRxMs >= QUIET_NET_MS)
			quiet = string.Format("%1 ms ago", Math.Round(nowMs - lastRxMs));

		if (!quiet.IsEmpty())
			DumpLocalRadios(string.Format("key-up on quiet net freq=%1, last RX on it: %2", frequency, quiet));
	}

	//------------------------------------------------------------------------------------------------
	//! The ENGINE's view of a radio key-up: every powered transceiver the RadioManagerEntity
	//! reports within the transmitter's range of the local speaker, with owner, frequency and
	//! key. The manager is located by world query (see the header) - never through
	//! ChimeraWorld.GetRadioManager().
	static void LogEngineView(int frequency, float range)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return;

		IEntity speaker = pc.GetControlledEntity();
		if (!speaker)
			return;

		RadioManagerEntity manager = LocateRadioManager();
		if (!manager)
		{
			PrintFormat("%1 engine view: no RadioManagerEntity found by world query - GetTransceiversInRange skipped (manager-less CTD guard)", TAG);
			return;
		}

		array<BaseTransceiver> inRange = {};
		int count = manager.GetTransceiversInRange(speaker.GetOrigin(), range, inRange);

		int onFreq = 0;
		string list;
		foreach (BaseTransceiver tsv : inRange)
		{
			if (!tsv)
				continue;

			BaseRadioComponent radio = tsv.GetRadio();
			bool sameFreq = (tsv.GetFrequency() == frequency);
			if (sameFreq)
				onFreq++;

			string key = "";
			string ownerLabel = "pid=-1";
			string host = "NO_RADIO";
			if (radio)
			{
				key = radio.GetEncryptionKey();
				ownerLabel = PlayerLabel(OwnerPlayerId(radio));
				host = RadioHostLabel(radio);
			}

			// Only transceivers on the keyed frequency are listed in full; the rest are counted.
			if (sameFreq)
				list = string.Format("%1 {%2 host=%3 %4 freq=%5 key='%6' muted=%7}", list, ownerLabel, host, tsv.Type(), tsv.GetFrequency(), key, tsv.IsMuted());
		}

		if (list.IsEmpty())
			list = " <none on this frequency>";

		PrintFormat("%1 engine view: RadioManagerEntity.GetTransceiversInRange(pos=%2, range=%3m) -> %4 powered transceivers, %5 on freq=%6:%7",
			TAG, speaker.GetOrigin(), range, count, onFreq, frequency, list);
	}

	//------------------------------------------------------------------------------------------------
	//! One world query per world for the RadioManagerEntity. Cached weakly: a deleted manager
	//! nulls the handle and the lookup state says "found" - a null result then means it went
	//! away, and the caller logs that it skipped.
	protected static RadioManagerEntity LocateRadioManager()
	{
		CheckWorldReset();

		if (s_iManagerLookupState == 1 || (s_iManagerLookupState == 2 && !s_RegisteredManager))
			return s_RadioManager;

		// Registered by the entity itself (modded RadioManagerEntity below) - the sphere query
		// misses it, the manager has no bounds.
		if (s_RegisteredManager && s_RegisteredManager.GetWorld() == GetGame().GetWorld())
		{
			s_RadioManager = s_RegisteredManager;
			s_iManagerLookupState = 1;
			PrintFormat("%1 RadioManagerEntity located by self-registration: %2 at %3", TAG, PrefabName(s_RadioManager), s_RadioManager.GetOrigin());
			return s_RadioManager;
		}

		s_iManagerLookupState = 2;
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return null;

		world.QueryEntitiesBySphere(vector.Zero, MANAGER_QUERY_RADIUS_M, QueryRadioManager);

		if (s_RadioManager)
		{
			s_iManagerLookupState = 1;
			PrintFormat("%1 RadioManagerEntity located by world query: %2 at %3", TAG, PrefabName(s_RadioManager), s_RadioManager.GetOrigin());
		}
		else
		{
			PrintFormat("%1 RadioManagerEntity NOT found by world query (radius %2m) - engine-view lines will be skipped this world", TAG, MANAGER_QUERY_RADIUS_M);
		}

		return s_RadioManager;
	}

	//------------------------------------------------------------------------------------------------
	//! Called from the RadioManagerEntity constructor: a real entity, never the native stub.
	static void RegisterRadioManager(RadioManagerEntity manager)
	{
		s_RegisteredManager = manager;
		if (EC29_Debug.VERBOSE)
			PrintFormat("%1 RadioManagerEntity registered itself: %2", TAG, manager);
	}

	//------------------------------------------------------------------------------------------------
	//! Query callback: stop at the first RadioManagerEntity.
	protected static bool QueryRadioManager(IEntity entity)
	{
		RadioManagerEntity manager = RadioManagerEntity.Cast(entity);
		if (!manager)
			return true;

		s_RadioManager = manager;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Capture health of the component that captured for a key-up. Vanilla's IsTransmiting
	//! flags are set by OnCapture, which lands after the first captured frame, so the immediate
	//! check is informational and the deferred (~1 s) check is the verdict: still keyed and not
	//! transmitting = CAPTURE DEAD (the wedged-capture state SetActiveTransmit clears before
	//! each radio key).
	static void LogCaptureHealth(string when, SCR_VoNComponent comp, bool stillKeyed, EVONTransmitType transmitType, int keyedFrequency)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		if (!comp)
		{
			PrintFormat("%1 capture health (%2): no transmit component (m_VONComp null) keyed=%3", TAG, when, stillKeyed);
			return;
		}

		bool transmitting = comp.IsTransmiting();
		bool transmittingRadio = comp.IsTransmitingRadio();
		BaseTransceiver txRadio = comp.GetTransmitRadio();
		int txFreq = -1;
		if (txRadio)
			txFreq = txRadio.GetFrequency();

		string flag;
		if (stillKeyed && !transmitting && when != "at key-up")
			flag = " CAPTURE DEAD";

		string head = string.Format("%1 capture health (%2): speaker %3 comp=%4 keyed=%5 type=%6 keyedFreq=%7", TAG, when, LocalPlayerLabel(), comp.Type(), stillKeyed, typename.EnumToString(EVONTransmitType, transmitType), keyedFrequency);
		PrintFormat("%1 IsTransmiting=%2 IsTransmitingRadio=%3 commMethod=%4 transmitRadioFreq=%5%6", head, transmitting, transmittingRadio, typename.EnumToString(ECommMethod, comp.GetCommMethod()), txFreq, flag);
	}

	//! ------------------------------------------------------------------------------------------
	//! Server: EXPECTED hearers
	//! ------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	//! SERVER, radio key-up accepted: the EXPECTED hearers. Every other player holding a powered
	//! radio with a transceiver on the frequency, with key match, distance against the
	//! transmitter's range and the relays on that frequency.
	static void LogServerExpectedRadioHearers(int senderPlayerId, int frequency, float range)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		IEntity senderEntity = pm.GetPlayerControlledEntity(senderPlayerId);
		vector senderPos = vector.Zero;
		bool hasSenderPos = false;
		string senderKey = "";
		string senderRadio = "<no carried radio on freq>";
		if (senderEntity)
		{
			senderPos = senderEntity.GetOrigin();
			hasSenderPos = true;

			array<BaseRadioComponent> senderRadios = {};
			CollectRadios(senderEntity, senderRadios);
			foreach (BaseRadioComponent radio : senderRadios)
			{
				if (FindTransceiverOnFrequency(radio, frequency))
				{
					senderKey = radio.GetEncryptionKey();
					senderRadio = PrefabName(radio.GetOwner());
					break;
				}
			}
		}

		array<int> players = {};
		pm.GetPlayers(players);

		int expected = 0;
		array<string> hearers = {};
		foreach (int pid : players)
		{
			if (pid == senderPlayerId)
				continue;

			IEntity other = pm.GetPlayerControlledEntity(pid);
			if (!other)
				continue;

			array<BaseRadioComponent> radios = {};
			CollectRadios(other, radios);

			foreach (BaseRadioComponent radio : radios)
			{
				BaseTransceiver tsv = FindTransceiverOnFrequency(radio, frequency);
				if (!tsv)
					continue;

				float distance = -1;
				if (hasSenderPos)
					distance = vector.Distance(senderPos, other.GetOrigin());

				bool keyMatch = (radio.GetEncryptionKey() == senderKey);
				bool inRange = hasSenderPos && distance <= range;
				bool bridged;
				string relays = RelayReport(frequency, senderPos, hasSenderPos, other.GetOrigin(), true, bridged);

				bool hears = radio.IsPowered() && keyMatch && !tsv.IsMuted() && (inRange || bridged);
				if (hears)
					expected++;

				string who = string.Format("{%1 source=%2 radio=%3 powered=%4 key='%5' keyMatch=%6 muted=%7", PlayerLabel(pid), SourceTypeLabel(pid, false), PrefabName(radio.GetOwner()), radio.IsPowered(), radio.GetEncryptionKey(), keyMatch, tsv.IsMuted());
				hearers.Insert(string.Format("%1 %2 inTxRange=%3 relayBridged=%4 expectedToHear=%5%6}", who, DistanceLabel(distance), inRange, bridged, hears, relays));
			}
		}

		// Head plus one line per hearer: the console truncates a line at ~8.5 KB, which cut
		// off everyone past the twentieth hearer on a busy net.
		string head = string.Format("%1 server EXPECTED radio hearers: speaker %2 source=%3 radio=%4 freq=%5 key='%6'", TAG, PlayerLabel(senderPlayerId), SourceTypeLabel(senderPlayerId, false), senderRadio, frequency, senderKey);
		string none;
		if (hearers.IsEmpty())
			none = " <nobody holds a radio on this frequency>";
		PrintFormat("%1 txRange=%2m pos=%3 expected=%4 listed=%5%6", head, range, senderPos, expected, hearers.Count(), none);

		foreach (string hearer : hearers)
			PrintFormat("%1 server EXPECTED radio hearer: speaker pid=%2 freq=%3 %4", TAG, senderPlayerId, frequency, hearer);
	}

	//------------------------------------------------------------------------------------------------
	//! SERVER, per voice packet (OnVoNUsed): detects a transmission START (first packet after a
	//! gap) and, when the sender holds no radio key open, logs the EXPECTED direct hearers -
	//! every other player inside the sender's replicated tier outer range. A radio key-up was
	//! already reported from the key-state path, so a keyed sender only gets the start marker.
	static void LogServerVonUsed(int senderPlayerId)
	{
		if (!EC29_Debug.VERBOSE)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		CheckWorldReset();

		float nowMs = world.GetWorldTime();
		float lastMs;
		bool isStart = true;
		if (s_mLastServerPacketMs.Find(senderPlayerId, lastMs) && nowMs - lastMs < TX_START_GAP_MS)
			isStart = false;
		s_mLastServerPacketMs.Set(senderPlayerId, nowMs);

		if (!isStart)
			return;

		int keyedFrequency = -1;
		EC29_RFPropagationNetworkComponent net = EC29_RFPropagationNetworkComponent.GetInstance();
		if (net)
			keyedFrequency = net.EC29_GetKeyedFrequency(senderPlayerId);

		if (keyedFrequency >= 0)
		{
			PrintFormat("%1 server transmission START: speaker %2 method=RADIO keyedFreq=%3 (expected hearers listed at key-up)", TAG, PlayerLabel(senderPlayerId), keyedFrequency);
			return;
		}

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return;

		bool tierResolved;
		EC29_EVoiceRange tier = PlayerTier(senderPlayerId, tierResolved);
		float outer = EC29_VoiceTiers.OuterRange(tier);

		// A sender with no controlled entity is an editor-side voice (GM or spectator manager);
		// its position is the manager's, which the engine also uses for routing.
		IEntity senderEntity = pm.GetPlayerControlledEntity(senderPlayerId);
		string sourceType = SourceTypeLabel(senderPlayerId, false);
		if (!senderEntity)
		{
			SCR_EditorManagerCore core = SCR_EditorManagerCore.Cast(SCR_EditorManagerCore.GetInstance(SCR_EditorManagerCore));
			if (core)
			{
				SCR_EditorManagerEntity mgr = core.GetEditorManager(senderPlayerId);
				if (mgr)
				{
					senderEntity = mgr;
					sourceType = SourceTypeLabel(senderPlayerId, true);
				}
			}
		}

		if (!senderEntity)
		{
			PrintFormat("%1 server transmission START: speaker %2 source=%3 method=DIRECT tier=%4 outer=%5m - sender has no entity or editor manager", TAG, PlayerLabel(senderPlayerId), sourceType, TierLabel(senderPlayerId), outer);
			return;
		}

		vector senderPos = senderEntity.GetOrigin();

		array<int> players = {};
		pm.GetPlayers(players);

		int expected = 0;
		string hearers;
		array<string> chunks = {};
		foreach (int pid : players)
		{
			if (pid == senderPlayerId)
				continue;

			IEntity other = pm.GetPlayerControlledEntity(pid);
			if (!other)
				continue;

			float distance = vector.Distance(senderPos, other.GetOrigin());
			if (distance > outer + DIRECT_RANGE_TOLERANCE_M)
				continue;

			expected++;
			hearers = string.Format("%1 {%2 %3}", hearers, PlayerLabel(pid), DistanceLabel(distance));
			// Stay well under the console's ~8.5 KB line cut.
			if (hearers.Length() > 3000)
			{
				chunks.Insert(hearers);
				hearers = "";
			}
		}

		if (!hearers.IsEmpty())
			chunks.Insert(hearers);

		string first = " <nobody within range>";
		if (!chunks.IsEmpty())
			first = chunks[0];

		PrintFormat("%1 server transmission START: speaker %2 source=%3 method=DIRECT tier=%4 outer=%5m pos=%6 expected=%7:%8",
			TAG, PlayerLabel(senderPlayerId), sourceType, TierLabel(senderPlayerId), outer, senderPos, expected, first);

		for (int i = 1, count = chunks.Count(); i < count; i++)
			PrintFormat("%1 server transmission START (cont.): speaker pid=%2:%3", TAG, senderPlayerId, chunks[i]);
	}
}

//------------------------------------------------------------------------------------------------
//! Gadget-side hooks: a radio entering a character's possession, and the native change invokers.
modded class SCR_RadioComponent
{
	//------------------------------------------------------------------------------------------------
	//! Subscribes to the native change invokers once. The invokers fire on client and server;
	//! each callback filters down to radios that are the local player's (client) or any
	//! player's (server), so a crate of radios on the ground stays quiet.
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!EC29_Debug.VERBOSE || !m_BaseRadioComp)
			return;

		m_BaseRadioComp.m_OnTansceiverFrequencyChangedInvoker.Insert(EC29_OnTraceFrequencyChanged);
		m_BaseRadioComp.m_OnPowerChanged.Insert(EC29_OnTracePowerChanged);
		m_BaseRadioComp.m_OnEncryptionKeyChanged.Insert(EC29_OnTraceEncryptionKeyChanged);
	}

	//------------------------------------------------------------------------------------------------
	//! Spawn, re-kit, pickup and drop all pass through here with the new character owner.
	override void ModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		super.ModeSwitch(mode, charOwner);

		if (!EC29_Debug.VERBOSE || !m_BaseRadioComp)
			return;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc || !charOwner || pc.GetControlledEntity() != charOwner)
			return;

		EC29_RadioTrace.DumpLocalRadios(string.Format("gadget %1 -> %2", EC29_RadioTrace.PrefabName(GetOwner()), typename.EnumToString(EGadgetMode, mode)));
	}

	//------------------------------------------------------------------------------------------------
	//! True when a change on this radio is worth a line: the local player's radio on a client,
	//! any player's radio on the server.
	protected bool EC29_ShouldTraceRadio()
	{
		if (!m_BaseRadioComp)
			return false;

		if (EC29_RadioTrace.IsLocalPlayerRadio(m_BaseRadioComp))
			return true;

		return Replication.IsServer() && EC29_RadioTrace.OwnerPlayerId(m_BaseRadioComp) > 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_OnTraceFrequencyChanged(BaseTransceiver transceiver, int oldFreq, int newFreq)
	{
		if (!EC29_ShouldTraceRadio())
			return;

		int index = EC29_RadioTrace.TransceiverIndex(m_BaseRadioComp, transceiver);
		EC29_RadioTrace.DumpRadio(string.Format("frequency changed tsv=%1 %2 -> %3 kHz", index, oldFreq, newFreq), m_BaseRadioComp);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_OnTracePowerChanged(bool powered)
	{
		if (!EC29_ShouldTraceRadio())
			return;

		EC29_RadioTrace.DumpRadio(string.Format("power -> %1", powered), m_BaseRadioComp);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_OnTraceEncryptionKeyChanged(string newKey)
	{
		if (!EC29_ShouldTraceRadio())
			return;

		EC29_RadioTrace.DumpRadio(string.Format("encryption key -> '%1'", newKey), m_BaseRadioComp);
	}
}

//------------------------------------------------------------------------------------------------
//! Server-side group frequency tuning at spawn. Vanilla tunes transceiver 0 of the first RADIO
//! gadget (never the backpack) to the player's group frequency, silently; this says what it
//! found and what the radio reads afterwards.
modded class SCR_GroupsManagerComponent
{
	override void TunePlayersFrequency(int playerId, IEntity player)
	{
		super.TunePlayersFrequency(playerId, player);

		if (!EC29_Debug.VERBOSE)
			return;

		int groupId = -1;
		int groupFrequency = 0;
		PlayerController controller = GetGame().GetPlayerManager().GetPlayerController(playerId);
		if (controller)
		{
			SCR_PlayerControllerGroupComponent groupComponent = SCR_PlayerControllerGroupComponent.Cast(controller.FindComponent(SCR_PlayerControllerGroupComponent));
			if (groupComponent)
			{
				groupId = groupComponent.GetGroupID();
				groupFrequency = groupComponent.GetActualGroupFrequency();
			}
		}

		IEntity radioEntity;
		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(player);
		if (gadgetManager)
			radioEntity = gadgetManager.GetGadgetByType(EGadgetType.RADIO);

		if (!radioEntity)
		{
			PrintFormat("%1 server group tune: %2 group=%3 groupFreq=%4 - no RADIO gadget, nothing tuned", EC29_RadioTrace.TAG, EC29_RadioTrace.PlayerLabel(playerId), groupId, groupFrequency);
			return;
		}

		BaseRadioComponent radio = BaseRadioComponent.Cast(radioEntity.FindComponent(BaseRadioComponent));
		int tunedFrequency = -1;
		if (radio && radio.TransceiversCount() > 0)
		{
			BaseTransceiver tsv = radio.GetTransceiver(0);
			if (tsv)
				tunedFrequency = tsv.GetFrequency();
		}

		PrintFormat("%1 server group tune: %2 group=%3 groupFreq=%4 radio=%5 tsv=0 now=%6 kHz", EC29_RadioTrace.TAG, EC29_RadioTrace.PlayerLabel(playerId), groupId, groupFrequency, EC29_RadioTrace.PrefabName(radioEntity), tunedFrequency);
		EC29_RadioTrace.DumpRadio("server group tune", radio, playerId);
	}
}

//------------------------------------------------------------------------------------------------
//! Relay antennas: what the relay is set to when it initialises and whenever its faction retunes
//! it; registered with the trace so hearer checks can see which relays sit on a net.
modded class SCR_AntennaServicePointComponent
{
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (!EC29_Debug.VERBOSE)
			return;

		EC29_RadioTrace.RegisterRelay(m_RadioControl);
		EC29_RadioTrace.LogRelay("post-init", owner, m_RadioControl);
	}

	override void ChangeRadioSettings(notnull SCR_Faction faction)
	{
		super.ChangeRadioSettings(faction);

		if (!EC29_Debug.VERBOSE)
			return;

		EC29_RadioTrace.LogRelay(string.Format("faction -> %1 (faction freq=%2 key='%3')", faction.GetFactionKey(), faction.GetFactionRadioFrequency(), faction.GetFactionRadioEncryptionKey()), GetOwner(), m_RadioControl);
	}
}

//------------------------------------------------------------------------------------------------
//! Self-registration for the engine-view trace: the manager has no bounds, so a world sphere
//! query never returns it. Not a VoN component - no effect on ear selection.
modded class RadioManagerEntity
{
	void RadioManagerEntity(IEntitySource src, IEntity parent)
	{
		EC29_RadioTrace.RegisterRadioManager(this);
	}
}
