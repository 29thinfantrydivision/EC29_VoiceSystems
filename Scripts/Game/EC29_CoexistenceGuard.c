//! Coexistence guard against known conflicting voice mods.
//!
//! Resource-level clobbering (same-GUID overrides of keyBindingMenu.conf,
//! chimeraInputCommon.conf, GameMode_Base.et, von.acp) is decided by load
//! order and CANNOT be prevented from script. What CAN be prevented is
//! double-firing behavior when a conflicting mod is co-loaded: both mods
//! polling the same default keys, double TX/RX beeps, double key-state RPC
//! traffic, double SFX-volume toggles. When a conflict is detected, the
//! overlapping EC29 feature set yields and logs a warning - the other mod
//! owns that feature for the session.
class EC29_CoexistenceGuard
{
	//! Workshop GUIDs of known conflicting voice mods (see CREDITS.md research note).
	protected static const string ADDON_CONFLICT_RADIO       = "672B0395726428B6";
	protected static const string ADDON_CONFLICT_VOICE_RANGE = "69333B6C7C8BE8AB";
	protected static const string ADDON_CONFLICT_EARPLUGS    = "612F512CD4CB21D5";

	//! Below the VHF player band: no unit net lives under 30 MHz, but special
	//! nets do (the spectator system's ghost radio sits at 29000 kHz).
	protected static const int EC29_SPECIAL_NET_BAND_FLOOR_KHZ = 30000;
	//! No physical radio in the modset reaches past ~6 km; a transceiver
	//! engineered far beyond that (spectator ghost: 50 km) is a magic net.
	protected static const float EC29_SPECIAL_NET_RANGE_M = 10000.0;

	//------------------------------------------------------------------------------------------------
	//! True for transceivers that belong to another system's net (spectator or
	//! admin ghost radios): tuned below the player band, or ranged beyond any
	//! physical radio. EC29's radio features leave those nets alone - no guard
	//! power-cycling, no retuning, no alternate PTT, no squelch/beeps, no RF
	//! simulation. Deliberately NOT keyed on encryption: a captured enemy
	//! radio carries a foreign key and must keep working like any radio.
	//! Frequency 0 (an unset channel) is NOT special - players may tune it.
	static bool EC29_IsSpecialNet(BaseTransceiver transceiver)
	{
		if (!transceiver)
			return false;

		int frequency = transceiver.GetFrequency();
		if (frequency > 0 && frequency < EC29_SPECIAL_NET_BAND_FLOOR_KHZ)
			return true;

		return transceiver.GetRange() > EC29_SPECIAL_NET_RANGE_M;
	}

	protected static bool s_bChecked;
	protected static bool s_bYieldRadio;
	protected static bool s_bYieldVoiceRange;
	protected static bool s_bYieldEarplugs;

	//------------------------------------------------------------------------------------------------
	//! True when a conflicting radio mod is loaded - EC29 radio features
	//! (radial-menu actions, beeps, key-state RPCs, squelch, alternate PTT) yield.
	static bool ShouldYieldRadio()
	{
		EC29_Check();
		return s_bYieldRadio;
	}

	//------------------------------------------------------------------------------------------------
	//! True when a conflicting voice-range mod is loaded - the EC29 F3 cycle yields
	//! (both actions default to F3; firing both would double-cycle).
	static bool ShouldYieldVoiceRange()
	{
		EC29_Check();
		return s_bYieldVoiceRange;
	}

	//------------------------------------------------------------------------------------------------
	//! True when a conflicting earplugs mod is loaded - the EC29 F2 toggle yields
	//! (both actions default to F2; firing both would cancel out).
	static bool ShouldYieldEarplugs()
	{
		EC29_Check();
		return s_bYieldEarplugs;
	}

	//------------------------------------------------------------------------------------------------
	//! Combined conflict text for the one-time chat notice, empty when clean.
	static string GetConflictNotice()
	{
		EC29_Check();

		array<string> conflicts = {};
		if (s_bYieldRadio)
			conflicts.Insert("radio");
		if (s_bYieldVoiceRange)
			conflicts.Insert("voice range");
		if (s_bYieldEarplugs)
			conflicts.Insert("earplugs");

		if (conflicts.IsEmpty())
			return string.Empty;

		string joined;
		foreach (int i, string name : conflicts)
		{
			if (i > 0)
				joined += ", ";
			joined += name;
		}

		return string.Format("EC29 Voice Systems: a conflicting voice mod is loaded - EC29 %1 features are disabled for this session. Unload the conflicting mod(s) to use the EC29 versions.", joined);
	}

	//------------------------------------------------------------------------------------------------
	protected static void EC29_Check()
	{
		if (s_bChecked)
			return;

		s_bChecked = true;

		array<string> addonGuids = {};
		GameProject.GetLoadedAddons(addonGuids);

		foreach (string guid : addonGuids)
		{
			if (guid == ADDON_CONFLICT_RADIO)
				s_bYieldRadio = true;
			else if (guid == ADDON_CONFLICT_VOICE_RANGE)
				s_bYieldVoiceRange = true;
			else if (guid == ADDON_CONFLICT_EARPLUGS)
				s_bYieldEarplugs = true;
		}

		if (s_bYieldRadio || s_bYieldVoiceRange || s_bYieldEarplugs)
			PrintFormat("[EC29-DBG][Coexist] Conflicting voice mod(s) co-loaded (radioYield=%1 vonRangeYield=%2 earplugsYield=%3) - matching EC29 features suppressed", s_bYieldRadio, s_bYieldVoiceRange, s_bYieldEarplugs, level: LogLevel.WARNING);
		else if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][Coexist] No conflicting voice mods co-loaded - all EC29 features active");
	}
}
