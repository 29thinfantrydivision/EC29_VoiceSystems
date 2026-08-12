//! Coexistence handshake for the originals this mod absorbed.
//!
//! Resource-level clobbering (same-GUID overrides of keyBindingMenu.conf,
//! chimeraInputCommon.conf, GameMode_Base.et, von.acp) is decided by load
//! order and CANNOT be prevented from script. What CAN be prevented is
//! double-firing behavior when an original mod is co-loaded: both mods
//! polling the same default keys, double TX/RX beeps, double key-state RPC
//! traffic, double SFX-volume toggles. When an original is detected, the
//! EC29 copy of that feature set yields and logs a warning - the original
//! mod owns the feature for that session.
class EC29_CoexistenceGuard
{
	//! Workshop GUIDs of the absorbed originals.
	protected static const string ADDON_506_IRRU_RADIO = "672B0395726428B6"; // 506IRRU - Enhanced Radio
	protected static const string ADDON_WCS_VON        = "69333B6C7C8BE8AB"; // WCS_VON
	protected static const string ADDON_WCS_EARPLUGS   = "612F512CD4CB21D5"; // WCS_Earplugs

	protected static bool s_bChecked;
	protected static bool s_bYieldRadio;
	protected static bool s_bYieldVoiceRange;
	protected static bool s_bYieldEarplugs;

	//------------------------------------------------------------------------------------------------
	//! True when the real 506IRRU Enhanced Radio is loaded - EC29 radio features
	//! (radial-menu actions, beeps, key-state RPCs, squelch, alternate PTT) yield.
	static bool ShouldYieldRadio()
	{
		EC29_Check();
		return s_bYieldRadio;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the real WCS_VON is loaded - the EC29 F3 voice-range cycle yields
	//! (both actions default to F3; firing both would double-cycle).
	static bool ShouldYieldVoiceRange()
	{
		EC29_Check();
		return s_bYieldVoiceRange;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the real WCS_Earplugs is loaded - the EC29 F2 earplug toggle yields
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
			conflicts.Insert("506IRRU Enhanced Radio");
		if (s_bYieldVoiceRange)
			conflicts.Insert("WCS_VON");
		if (s_bYieldEarplugs)
			conflicts.Insert("WCS_Earplugs");

		if (conflicts.IsEmpty())
			return string.Empty;

		string joined;
		foreach (int i, string name : conflicts)
		{
			if (i > 0)
				joined += ", ";
			joined += name;
		}

		return string.Format("EC29 Voice Systems: %1 detected - the EC29 copy of those features is disabled for this session. Unload the original mod(s) to use the EC29 versions.", joined);
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
			if (guid == ADDON_506_IRRU_RADIO)
				s_bYieldRadio = true;
			else if (guid == ADDON_WCS_VON)
				s_bYieldVoiceRange = true;
			else if (guid == ADDON_WCS_EARPLUGS)
				s_bYieldEarplugs = true;
		}

		if (s_bYieldRadio || s_bYieldVoiceRange || s_bYieldEarplugs)
			PrintFormat("[EC29-DBG][Coexist] Original mod(s) co-loaded (radioYield=%1 vonRangeYield=%2 earplugsYield=%3) - matching EC29 features suppressed", s_bYieldRadio, s_bYieldVoiceRange, s_bYieldEarplugs, level: LogLevel.WARNING);
		else
			Print("[EC29-DBG][Coexist] No absorbed originals co-loaded - all EC29 features active");
	}
}
