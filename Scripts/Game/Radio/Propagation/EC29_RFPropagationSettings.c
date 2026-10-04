//! Server-side source of the two RF switches (propagation on/off, per-evaluation debug lines).
//!
//! Resolution order on the authority (dedicated/listen server, or offline play), once per process:
//!   1. $profile:EC29_RFPropagation.json - admins already have these files, so the path and the
//!      key names (RFPropagationEnabled, DebugEnabled) are a fixed contract. Missing keys keep the
//!      default. A file that exists but does not parse is an admin's hand edit: log an ERROR, use
//!      defaults for this run and NEVER overwrite it. A missing file is created with both keys off.
//!   2. Configs/EC29_VONConfig.conf, whose root is this class (attribute defaults today).
//!   3. Built-in defaults: both off.
//!
//! The values only matter on the server: EC29_RFPropagationNetworkComponent copies them into
//! replicated props at its post-init and every machine reads those. Nothing here is persisted or
//! re-replicated by the setters.
//!
//! A non-authoritative machine never reads or creates the profile JSON. It gets a separate
//! defaults-only instance, and that instance does not latch the authority slot - a process that
//! first asked from the main menu or as a joined client still loads the JSON properly if it later
//! hosts or plays offline.
[BaseContainerProps(configRoot: true)]
class EC29_RFPropagationSettings
{
	[Attribute("0", UIWidgets.CheckBox, "Terrain-aware RF propagation affects radio audio quality", category: "RF Propagation")]
	bool m_bRFPropagationEnabled;

	[Attribute("0", UIWidgets.CheckBox, "Print one [RFPropagation] line per propagation evaluation", category: "RF Propagation")]
	bool m_bDebugEnabled;

	protected static const string PROFILE_JSON = "$profile:EC29_RFPropagation.json";
	protected static const string JSON_KEY_RF = "RFPropagationEnabled";
	protected static const string JSON_KEY_DEBUG = "DebugEnabled";
	protected static const ResourceName CONF_RESOURCE = "{C99D2868A888D3BC}Configs/EC29_VONConfig.conf";

	//! Process-lifetime: loaded once on the authority, deliberately not reset on world change.
	protected static ref EC29_RFPropagationSettings s_EC29_AuthorityInstance;
	//! Defaults-only stand-in for machines that must not touch the profile file.
	protected static ref EC29_RFPropagationSettings s_EC29_RemoteInstance;

	//------------------------------------------------------------------------------------------------
	static EC29_RFPropagationSettings GetInstance()
	{
		if (!EC29_MayUseProfileFile())
		{
			if (!s_EC29_RemoteInstance)
			{
				s_EC29_RemoteInstance = new EC29_RFPropagationSettings();
				if (EC29_Debug.VERBOSE)
					Print("[EC29-DBG][RadioNet] Not the authority - RF settings stay at defaults, profile JSON untouched (server values arrive through EC29_RFPropagationNetworkComponent)");
			}

			return s_EC29_RemoteInstance;
		}

		if (!s_EC29_AuthorityInstance)
			s_EC29_AuthorityInstance = EC29_LoadForAuthority();

		return s_EC29_AuthorityInstance;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsRFPropagationEnabled()
	{
		EC29_RFPropagationSettings settings = GetInstance();
		if (!settings)
			return false;

		return settings.m_bRFPropagationEnabled;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsDebugEnabled()
	{
		EC29_RFPropagationSettings settings = GetInstance();
		if (!settings)
			return false;

		return settings.m_bDebugEnabled;
	}

	//------------------------------------------------------------------------------------------------
	//! In-memory only; not written to disk and not pushed to the network component.
	static void SetRFPropagationEnabled(bool enabled)
	{
		EC29_RFPropagationSettings settings = GetInstance();
		if (!settings)
			return;

		settings.m_bRFPropagationEnabled = enabled;
		PrintFormat("[EC29 RFPropagation] RF propagation set to %1", enabled);
	}

	//------------------------------------------------------------------------------------------------
	//! In-memory only; not written to disk and not pushed to the network component.
	static void SetDebugEnabled(bool enabled)
	{
		EC29_RFPropagationSettings settings = GetInstance();
		if (!settings)
			return;

		settings.m_bDebugEnabled = enabled;
		PrintFormat("[EC29 RFPropagation] RF debug set to %1", enabled);
	}

	//------------------------------------------------------------------------------------------------
	//! True only where reading/creating the server's profile JSON is legitimate. Any doubt answers
	//! false: no game, a client replication session, a running session where this is not the
	//! server, no world, or the main menu (a player's machine before it hosts or joins anything).
	protected static bool EC29_MayUseProfileFile()
	{
		ArmaReforgerScripted game = GetGame();
		if (!game)
			return false;

		if (RplSession.Mode() == RplMode.Client)
			return false;

		if (Replication.IsRunning())
			return Replication.IsServer();

		if (game.m_bIsMainMenuOpen)
			return false;

		return game.GetWorld() != null;
	}

	//------------------------------------------------------------------------------------------------
	protected static EC29_RFPropagationSettings EC29_LoadForAuthority()
	{
		EC29_RFPropagationSettings settings = new EC29_RFPropagationSettings();

		if (!settings.EC29_ReadProfileJson())
			settings.EC29_ReadConf();

		PrintFormat("[EC29 RFPropagation] Active settings: RF propagation=%1 debug=%2", settings.m_bRFPropagationEnabled, settings.m_bDebugEnabled);
		return settings;
	}

	//------------------------------------------------------------------------------------------------
	//! \return true when the JSON was parsed and applied (the .conf is then not consulted)
	protected bool EC29_ReadProfileJson()
	{
		SCR_JsonLoadContext reader = new SCR_JsonLoadContext();
		if (reader.LoadFromFile(PROFILE_JSON))
		{
			bool value;
			if (reader.DoesKeyExist(JSON_KEY_RF) && reader.ReadValue(JSON_KEY_RF, value))
				m_bRFPropagationEnabled = value;

			if (reader.DoesKeyExist(JSON_KEY_DEBUG) && reader.ReadValue(JSON_KEY_DEBUG, value))
				m_bDebugEnabled = value;

			PrintFormat("[EC29 RFPropagation] Settings loaded from JSON: %1", PROFILE_JSON);
			return true;
		}

		if (FileIO.FileExists(PROFILE_JSON))
		{
			PrintFormat("[EC29 RFPropagation] %1 exists but failed to parse - fix or delete it. Using defaults for this run; the file was left untouched.", PROFILE_JSON, level: LogLevel.ERROR);
			return false;
		}

		EC29_WriteDefaultProfileJson();
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Only ever called when the file does not exist. Pretty-printed, 4-space indent, one key per line.
	protected static void EC29_WriteDefaultProfileJson()
	{
		FileHandle file = FileIO.OpenFile(PROFILE_JSON, FileMode.WRITE);
		if (!file)
		{
			PrintFormat("[EC29 RFPropagation] Could not create default settings file %1", PROFILE_JSON, level: LogLevel.ERROR);
			return;
		}

		file.WriteLine("{");
		file.WriteLine("    \"" + JSON_KEY_RF + "\": false,");
		file.WriteLine("    \"" + JSON_KEY_DEBUG + "\": false");
		file.WriteLine("}");
		file.Close();

		PrintFormat("[EC29 RFPropagation] Created default settings file %1 (both switches off)", PROFILE_JSON);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_ReadConf()
	{
		Resource resource = BaseContainerTools.LoadContainer(CONF_RESOURCE);
		if (!resource || !resource.IsValid())
		{
			Print("[EC29 RFPropagation] Could not load EC29_VONConfig.conf - using default settings (disabled)", LogLevel.WARNING);
			return;
		}

		BaseResourceObject resourceObject = resource.GetResource();
		if (!resourceObject)
		{
			Print("[EC29 RFPropagation] EC29_VONConfig.conf has no resource object - using default settings (disabled)", LogLevel.WARNING);
			return;
		}

		EC29_RFPropagationSettings fromConf = EC29_RFPropagationSettings.Cast(BaseContainerTools.CreateInstanceFromContainer(resourceObject.ToBaseContainer()));
		if (!fromConf)
		{
			Print("[EC29 RFPropagation] EC29_VONConfig.conf root is not EC29_RFPropagationSettings - using default settings (disabled)", LogLevel.WARNING);
			return;
		}

		m_bRFPropagationEnabled = fromConf.m_bRFPropagationEnabled;
		m_bDebugEnabled = fromConf.m_bDebugEnabled;
		Print("[EC29 RFPropagation] Settings loaded from .conf");
	}
}
