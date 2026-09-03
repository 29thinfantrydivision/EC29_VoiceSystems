//------------------------------------------------------------------------------------------------
//! Registration probe - DIAGNOSTIC, OFF BY DEFAULT (mission header: m_bRegistrationProbe).
//!
//! First-party proof of whether a powered, carried radio is present in the native radio
//! system's transceiver registry. RadioManagerEntity.GetTransceiversInRange returns only
//! transceivers in power-ON state, so a radio that reports IsPowered() true but is absent from
//! the query result is powered-yet-unregistered: the 1.8 receiver-registration failure that
//! the repair cycles work around. Samples at +1/3/5/10 s after every controlled-entity change
//! (native setup continues after spawn - early and late samples catch both the broken window
//! and any native self-heal). Run it with the repair kill-switch OFF to measure what native
//! registration does on its own after a game update; [EC29-PROBE] lines carry the verdict.
//!
//! CRASH HAZARD - READ BEFORE ENABLING. ChimeraWorld.GetRadioManager() returns a NON-null
//! degraded handle on any world where a radio initialised before a RadioManagerEntity existed
//! (Game Master and custom worlds), and calling GetTransceiversInRange on that handle is a
//! native access violation - a guaranteed client crash (2026-08-21 field CTDs, every client).
//! No runtime signal distinguishes the degraded handle from a real manager. Enable ONLY on a
//! world known to contain a real RadioManagerEntity (vanilla Conflict worlds); never on the
//! fleet's GM servers. That is why this is a per-mission header switch, default OFF, and not
//! a code constant.
//!
//! Coverage: carried gadget radios only (RADIO + RADIO_BACKPACK); vehicle radios out of scope.
modded class SCR_PlayerController
{
	protected static const float EC29_PROBE_QUERY_RANGE_M = 1000.0;

	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		if (!to)
			return;

		EC29_VONSettingsComponent settings = EC29_VONSettingsComponent.GetInstance();
		if (!settings || !settings.EC29_IsRegistrationProbeEnabled())
			return;

		GetGame().GetCallqueue().CallLater(EC29_ProbeCheck, 1000, false, 1000);
		GetGame().GetCallqueue().CallLater(EC29_ProbeCheck, 3000, false, 3000);
		GetGame().GetCallqueue().CallLater(EC29_ProbeCheck, 5000, false, 5000);
		GetGame().GetCallqueue().CallLater(EC29_ProbeCheck, 10000, false, 10000);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_ProbeCheck(int checkDelayMs)
	{
		IEntity controlled = GetControlledEntity();
		if (!controlled)
			return;

		string side = "CLIENT";
		if (Replication.IsServer())
			side = "SERVER";

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return;

		RadioManagerEntity radioManager = world.GetRadioManager();
		if (!radioManager)
		{
			Print(string.Format("[EC29-PROBE] t=%1ms side=%2 NO RadioManagerEntity in world - radio VoN cannot function at all", checkDelayMs, side), LogLevel.ERROR);
			return;
		}

		SCR_GadgetManagerComponent gadgetManager = SCR_GadgetManagerComponent.GetGadgetManager(controlled);
		if (!gadgetManager)
		{
			Print(string.Format("[EC29-PROBE] t=%1ms side=%2 no gadget manager on controlled entity", checkDelayMs, side), LogLevel.WARNING);
			return;
		}

		array<SCR_GadgetComponent> radioGadgets = {};
		array<SCR_GadgetComponent> byType = gadgetManager.GetGadgetsByType(EGadgetType.RADIO);
		if (byType)
			radioGadgets.Copy(byType);
		byType = gadgetManager.GetGadgetsByType(EGadgetType.RADIO_BACKPACK);
		if (byType)
		{
			foreach (SCR_GadgetComponent gadget : byType)
				radioGadgets.Insert(gadget);
		}

		if (radioGadgets.IsEmpty())
		{
			Print(string.Format("[EC29-PROBE] t=%1ms side=%2 controlled entity carries no radios", checkDelayMs, side), LogLevel.NORMAL);
			return;
		}

		array<BaseTransceiver> nativeRegistry = {};
		int nativeCount = radioManager.GetTransceiversInRange(controlled.GetOrigin(), EC29_PROBE_QUERY_RANGE_M, nativeRegistry);

		foreach (SCR_GadgetComponent gadget : radioGadgets)
		{
			SCR_RadioComponent radioGadget = SCR_RadioComponent.Cast(gadget);
			if (!radioGadget)
				continue;

			BaseRadioComponent radio = radioGadget.GetRadioComponent();
			if (!radio)
			{
				Print(string.Format("[EC29-PROBE] t=%1ms side=%2 gadget without BaseRadioComponent", checkDelayMs, side), LogLevel.WARNING);
				continue;
			}

			EC29_ProbeReportRadio(radio, nativeRegistry, nativeCount, checkDelayMs, side);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_ProbeReportRadio(BaseRadioComponent radio, array<BaseTransceiver> nativeRegistry, int nativeCount, int checkDelayMs, string side)
	{
		string prefabName = "?";
		IEntity owner = radio.GetOwner();
		if (owner)
		{
			EntityPrefabData prefabData = owner.GetPrefabData();
			if (prefabData)
				prefabName = prefabData.GetPrefabName();
		}

		bool anyPoweredMissing = false;
		int tsvCount = radio.TransceiversCount();
		for (int i = 0; i < tsvCount; i++)
		{
			BaseTransceiver tsv = radio.GetTransceiver(i);
			if (!tsv)
				continue;

			bool inRegistry = nativeRegistry.Find(tsv) != -1;

			string verdict;
			if (!radio.IsPowered())
			{
				verdict = "SKIP (radio off - absence from registry is correct)";
			}
			else if (inRegistry)
			{
				verdict = "REGISTERED";
			}
			else
			{
				verdict = "MISSING <-- powered but not in native registry";
				anyPoweredMissing = true;
			}

			// string.Format caps at 9 placeholders - build the line in two halves.
			string head = string.Format("[EC29-PROBE] t=%1ms side=%2 radio=%3 tsv=%4/%5", checkDelayMs, side, prefabName, i + 1, tsvCount);
			string tail = string.Format("freq=%1kHz range=%2m powered=%3 muted=%4 registrySize=%5 -> %6", tsv.GetFrequency(), tsv.GetRange(), radio.IsPowered(), tsv.IsMuted(), nativeCount, verdict);
			Print(head + " " + tail, LogLevel.WARNING);
		}

		if (anyPoweredMissing)
			Print(string.Format("[EC29-PROBE] t=%1ms side=%2 VERDICT: receiver-registration failure CONFIRMED on %3", checkDelayMs, side, prefabName), LogLevel.ERROR);
	}
}
