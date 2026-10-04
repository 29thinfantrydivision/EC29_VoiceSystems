//! Persisted earplug preference (game user settings, Audio tab -> 29th ID).
//!
//! The class name and the EarplugsVolume property are the keys players'
//! saved values live under - frozen. Despite the name the value is the
//! percentage REMOVED from SFX while plugged: 80 leaves 20 % of the
//! player's normal SFX level, 0 changes nothing, 100 silences SFX.
class EC29_EarplugSettings : ModuleGameSettings
{
	[Attribute(defvalue: "80", uiwidget: UIWidgets.Slider, params: "0 100 1", desc: "Percent of SFX volume removed while earplugs are in")]
	int EarplugsVolume;
}

//! Client-side earplugs: a keybind that ducks the SFX master bus to a
//! fraction of the player's own SFX setting, leaving voice, radio, music and
//! dialogue untouched. Registered by class name in
//! Configs/Systems/ChimeraSystemsConfig.conf (Client location), so dedicated
//! servers never instantiate it.
//!
//! State is a single in-memory flag, unplugged at every world start and never
//! persisted. The player's VolumeSfx setting is only ever read; earplugs are a
//! runtime override of the bus, and both world start and world stop pull the
//! bus back up to the configured level if a previous session left it low.
class EC29_Earplugs_System : GameSystem
{
	//! Input action name - shared with chimeraInputCommon.conf and keyBindingMenu.conf.
	static const string EC29_ACTION_TOGGLE = "EC29_ToggleEarplugs";

	protected static const string EC29_GAME_MODULE = "EC29_EarplugSettings";
	protected static const string EC29_GAME_PROPERTY = "EarplugsVolume";
	protected static const string EC29_ENGINE_MODULE = "AudioSettings";
	protected static const string EC29_ENGINE_PROPERTY = "VolumeSfx";

	//! Tolerance for "bus is below the configured level" so float noise never
	//! triggers a pointless restore.
	protected static const float EC29_RESTORE_EPSILON = 0.0005;

	//! Weak handle to the live system for the HUD display; cleared on shutdown.
	protected static EC29_Earplugs_System s_EC29_Active;

	protected ref ScriptInvokerBool m_EC29_OnToggled;
	protected InputManager m_EC29_Input;

	protected bool m_bEC29_Plugged;
	protected bool m_bEC29_Listening;
	protected bool m_bEC29_SettingsHooked;

	//! Bus level with earplugs out (the player's own SFX slider, 0..1).
	protected float m_fEC29_OpenLevel = 1;
	//! Bus level with earplugs in (fraction of m_fEC29_OpenLevel).
	protected float m_fEC29_PluggedLevel;

	//------------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		outInfo
			.SetAbstract(false)
			.SetUnique(true)
			.SetLocation(WorldSystemLocation.Client);
	}

	//------------------------------------------------------------------------------------------------
	//! Live instance for the current world, or null when none is running.
	static EC29_Earplugs_System GetInstance()
	{
		if (s_EC29_Active)
			return s_EC29_Active;

		ArmaReforgerScripted game = GetGame();
		if (!game)
			return null;

		World world = game.GetWorld();
		if (!world)
			return null;

		return EC29_Earplugs_System.Cast(world.FindSystem(EC29_Earplugs_System));
	}

	//------------------------------------------------------------------------------------------------
	//! Fired after every successful toggle with the new plugged state.
	ScriptInvokerBool GetOnToggled()
	{
		if (!m_EC29_OnToggled)
			m_EC29_OnToggled = new ScriptInvokerBool();

		return m_EC29_OnToggled;
	}

	//------------------------------------------------------------------------------------------------
	bool IsPlugged()
	{
		return m_bEC29_Plugged;
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnInit()
	{
		super.OnInit();

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][Earplugs] System init");

		s_EC29_Active = this;
		m_bEC29_Plugged = false;

		ArmaReforgerScripted game = GetGame();
		if (!game)
		{
			Print("[EC29-DBG][Earplugs] Game instance unavailable at init - earplugs disabled", LogLevel.WARNING);
			return;
		}

		if (!game.GetEngineUserSettings())
			Print("[EC29-DBG][Earplugs] Engine user settings unavailable - unplugged level falls back to the current SFX bus", LogLevel.WARNING);

		if (!game.GetGameUserSettings())
			Print("[EC29-DBG][Earplugs] Game user settings unavailable - earplug reduction setting cannot be read", LogLevel.WARNING);

		m_EC29_Input = game.GetInputManager();
		if (!m_EC29_Input)
			Print("[EC29-DBG][Earplugs] Input manager unavailable - earplugs keybind will not register", LogLevel.WARNING);

		EC29_RefreshLevels();

		game.OnUserSettingsChangedInvoker().Insert(EC29_OnUserSettingsChanged);
		m_bEC29_SettingsHooked = true;

		if (m_EC29_Input)
		{
			m_EC29_Input.AddActionListener(EC29_ACTION_TOGGLE, EActionTrigger.DOWN, EC29_OnToggleAction);
			m_bEC29_Listening = true;

			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][Earplugs] Listening for %1", EC29_ACTION_TOGGLE);
		}

		EC29_RaiseBusToOpenLevel();
	}

	//------------------------------------------------------------------------------------------------
	override protected void OnStopped()
	{
		EC29_Shutdown();
		super.OnStopped();
	}

	//------------------------------------------------------------------------------------------------
	//! Second line of defence: some teardown paths destroy the system without
	//! a separate stop. EC29_Shutdown is idempotent.
	override protected void OnCleanup()
	{
		EC29_Shutdown();
		super.OnCleanup();
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_Shutdown()
	{
		if (m_bEC29_Listening && m_EC29_Input)
			m_EC29_Input.RemoveActionListener(EC29_ACTION_TOGGLE, EActionTrigger.DOWN, EC29_OnToggleAction);

		m_bEC29_Listening = false;

		ArmaReforgerScripted game = GetGame();
		if (m_bEC29_SettingsHooked && game)
			game.OnUserSettingsChangedInvoker().Remove(EC29_OnUserSettingsChanged);

		m_bEC29_SettingsHooked = false;

		// Never leave menus or the next session with ducked SFX.
		EC29_RaiseBusToOpenLevel();
		m_bEC29_Plugged = false;

		if (s_EC29_Active == this)
			s_EC29_Active = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_OnToggleAction(float value, EActionTrigger reason = EActionTrigger.DOWN)
	{
		// Logged ahead of the guard on purpose: a yielding keypress still shows in the RPT.
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] %1 pressed (currently plugged=%2)", EC29_ACTION_TOGGLE, m_bEC29_Plugged);

		// A co-loaded earplugs mod owns F2 too; toggling both would cancel out.
		if (EC29_CoexistenceGuard.ShouldYieldEarplugs())
			return;

		m_bEC29_Plugged = !m_bEC29_Plugged;

		float level = m_fEC29_OpenLevel;
		if (m_bEC29_Plugged)
			level = m_fEC29_PluggedLevel;

		AudioSystem.SetMasterVolume(AudioSystem.SFX, level);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] plugged=%1 target=%2 bus readback=%3", m_bEC29_Plugged, level, AudioSystem.GetMasterVolume(AudioSystem.SFX));

		if (m_EC29_OnToggled)
			m_EC29_OnToggled.Invoke(m_bEC29_Plugged);
	}

	//------------------------------------------------------------------------------------------------
	//! Either slider (vanilla SFX or the EC29 reduction) may have moved. While
	//! unplugged vanilla already drives the bus, so only the plugged case needs
	//! re-applying.
	protected void EC29_OnUserSettingsChanged()
	{
		EC29_RefreshLevels();

		if (m_bEC29_Plugged)
			AudioSystem.SetMasterVolume(AudioSystem.SFX, m_fEC29_PluggedLevel);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_RefreshLevels()
	{
		m_fEC29_OpenLevel = EC29_ReadOpenLevel();
		m_fEC29_PluggedLevel = m_fEC29_OpenLevel * (1 - EC29_ReadReductionFraction());

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] Levels: unplugged=%1 plugged=%2", m_fEC29_OpenLevel, m_fEC29_PluggedLevel);
	}

	//------------------------------------------------------------------------------------------------
	//! Player's own SFX setting on the 0..1 bus scale; the live bus value
	//! stands in when the engine settings cannot be read.
	protected float EC29_ReadOpenLevel()
	{
		ArmaReforgerScripted game = GetGame();
		if (game)
		{
			UserSettings engineSettings = game.GetEngineUserSettings();
			if (engineSettings)
			{
				BaseContainer audio = engineSettings.GetModule(EC29_ENGINE_MODULE);
				if (audio)
				{
					float percent;
					if (audio.Get(EC29_ENGINE_PROPERTY, percent))
						return Math.Clamp(percent / 100, 0, 1);
				}
			}
		}

		return AudioSystem.GetMasterVolume(AudioSystem.SFX);
	}

	//------------------------------------------------------------------------------------------------
	//! Share of the open level removed while plugged (0..1). A missing module
	//! means full mute - better an obviously-working earplug than a dead key.
	protected float EC29_ReadReductionFraction()
	{
		BaseContainer module;
		ArmaReforgerScripted game = GetGame();
		if (game)
		{
			UserSettings gameSettings = game.GetGameUserSettings();
			if (gameSettings)
				module = gameSettings.GetModule(EC29_GAME_MODULE);
		}

		if (!module)
		{
			PrintFormat("[EC29-DBG][Earplugs] %1 module not found in game user settings - plugged SFX will be fully muted", EC29_GAME_MODULE, level: LogLevel.WARNING);
			return 1;
		}

		int percent;
		module.Get(EC29_GAME_PROPERTY, percent);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] %1 found, reduction=%2 percent", EC29_GAME_MODULE, percent);

		return Math.Clamp(percent, 0, 100) / 100.0;
	}

	//------------------------------------------------------------------------------------------------
	//! Self-heal: only ever raises the bus, never lowers one above the open level.
	protected void EC29_RaiseBusToOpenLevel()
	{
		float current = AudioSystem.GetMasterVolume(AudioSystem.SFX);
		if (current >= m_fEC29_OpenLevel - EC29_RESTORE_EPSILON)
			return;

		AudioSystem.SetMasterVolume(AudioSystem.SFX, m_fEC29_OpenLevel);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] SFX bus was %1, restored to %2", current, m_fEC29_OpenLevel);
	}
}
