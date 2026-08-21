class EC29_Earplugs_System extends WorldSystem
{
	static EC29_Earplugs_System Instance;

	InputManager Input;
	UserSettings EngineUserSettings;
	UserSettings GameUserSettings;
	float SFX_DefaultVolume;
	float EarplugsVolume;
	bool bMuted;

	//------------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
		outInfo
			.SetAbstract(false)
			.SetUnique(true)
			.SetLocation(WorldSystemLocation.Client);
	}

	//------------------------------------------------------------------------------------------------
	[EventAttribute()]
	void OnEarplugsToggled(bool bState);

	//------------------------------------------------------------------------------------------------
	override void OnInit()
	{
		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][Earplugs] System OnInit ENTER - system was instantiated (ChimeraSystemsConfig override applied)", LogLevel.NORMAL);
		Instance = this;

		EngineUserSettings = GetGame().GetEngineUserSettings();
		GameUserSettings = GetGame().GetGameUserSettings();
		Input = GetGame().GetInputManager();

		if (!EngineUserSettings)
			Print("[EC29-DBG][Earplugs] EngineUserSettings is NULL", LogLevel.WARNING);
		if (!GameUserSettings)
			Print("[EC29-DBG][Earplugs] GameUserSettings is NULL", LogLevel.WARNING);
		if (!Input)
			Print("[EC29-DBG][Earplugs] InputManager is NULL - keybind will NOT register", LogLevel.WARNING);

		SFX_DefaultVolume = FetchDefaultSFXVolume();
		EarplugsVolume = FetchEarplugsVolume();
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] Volumes resolved: defaultSFX=%1 pluggedSFX=%2", SFX_DefaultVolume, EarplugsVolume);

		GetGame().OnUserSettingsChangedInvoker().Insert(OnUserSettingsChanged);

		if (Input)
		{
			Input.AddActionListener("EC29_ToggleEarplugs", EActionTrigger.DOWN, OnKeybindPressed);
			if (EC29_Debug.VERBOSE)
				Print("[EC29-DBG][Earplugs] Action listener registered for 'EC29_ToggleEarplugs' (F2). If F2 does nothing and no keypress log appears, the input action did not load from chimeraInputCommon.conf", LogLevel.NORMAL);
		}

		if (GetVolume() < SFX_DefaultVolume)
			SetVolume(SFX_DefaultVolume);
	}

	//------------------------------------------------------------------------------------------------
	override void OnStopped()
	{
		if (Input)
			Input.RemoveActionListener("EC29_ToggleEarplugs", EActionTrigger.DOWN, OnKeybindPressed);

		GetGame().OnUserSettingsChangedInvoker().Remove(OnUserSettingsChanged);

		if (GetVolume() < SFX_DefaultVolume)
			SetVolume(SFX_DefaultVolume);
	}

	//------------------------------------------------------------------------------------------------
	void OnUserSettingsChanged()
	{
		SFX_DefaultVolume = FetchDefaultSFXVolume();
		EarplugsVolume = FetchEarplugsVolume();

		if (bMuted)
			SetVolume(EarplugsVolume);
	}

	//------------------------------------------------------------------------------------------------
	void OnKeybindPressed()
	{
		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][Earplugs] F2 keybind FIRED (EC29_ToggleEarplugs action works)", LogLevel.NORMAL);

		// Coexistence: a known conflicting mod also toggles SFX volume on F2; both firing would cancel out.
		if (EC29_CoexistenceGuard.ShouldYieldEarplugs())
			return;

		EngineUserSettings = GetGame().GetEngineUserSettings();
		ToggleSFXVolume();
	}

	//------------------------------------------------------------------------------------------------
	float FetchDefaultSFXVolume()
	{
		float volume = AudioSystem.GetMasterVolume(AudioSystem.SFX);
		if (!EngineUserSettings)
			return volume;

		BaseContainer audioSettings = EngineUserSettings.GetModule("AudioSettings");
		if (audioSettings)
		{
			audioSettings.Get("VolumeSfx", volume);
			volume *= 0.01;
		}

		return volume;
	}

	//------------------------------------------------------------------------------------------------
	float FetchEarplugsVolume()
	{
		float volume;
		BaseContainer earplugSettings;
		if (GameUserSettings)
			earplugSettings = GameUserSettings.GetModule("EC29_EarplugSettings");
		if (earplugSettings)
		{
			earplugSettings.Get("EarplugsVolume", volume);
			if (EC29_Debug.VERBOSE)
				PrintFormat("[EC29-DBG][Earplugs] Settings module found: reduction slider=%1%%", volume);
			volume = 1.0 - (volume * 0.01);
			volume *= SFX_DefaultVolume;
		}
		else
		{
			Print("[EC29-DBG][Earplugs] Settings module 'EC29_EarplugSettings' NOT found - plugged volume will be 0 (full mute)", LogLevel.WARNING);
		}

		return volume;
	}

	//------------------------------------------------------------------------------------------------
	void ToggleSFXVolume()
	{
		bMuted = !bMuted;

		float targetVolume = SFX_DefaultVolume;
		if (bMuted)
			targetVolume = EarplugsVolume;

		SetVolume(targetVolume);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] Toggled: muted=%1 -> SFX master volume set to %2 (readback=%3)", bMuted, targetVolume, GetVolume());
		ThrowEvent(this.OnEarplugsToggled, bMuted);
	}

	//------------------------------------------------------------------------------------------------
	void SetVolume(float volume)
	{
		AudioSystem.SetMasterVolume(AudioSystem.SFX, volume);
	}

	//------------------------------------------------------------------------------------------------
	float GetVolume()
	{
		return AudioSystem.GetMasterVolume(AudioSystem.SFX);
	}
}
