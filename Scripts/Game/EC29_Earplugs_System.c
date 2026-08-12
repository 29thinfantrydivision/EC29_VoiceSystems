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
		Instance = this;

		EngineUserSettings = GetGame().GetEngineUserSettings();
		GameUserSettings = GetGame().GetGameUserSettings();
		Input = GetGame().GetInputManager();

		SFX_DefaultVolume = FetchDefaultSFXVolume();
		EarplugsVolume = FetchEarplugsVolume();

		GetGame().OnUserSettingsChangedInvoker().Insert(OnUserSettingsChanged);

		if (Input)
			Input.AddActionListener("EC29_ToggleEarplugs", EActionTrigger.DOWN, OnKeybindPressed);

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
		EngineUserSettings = GetGame().GetEngineUserSettings();
		ToggleSFXVolume();
	}

	//------------------------------------------------------------------------------------------------
	float FetchDefaultSFXVolume()
	{
		float volume = AudioSystem.GetMasterVolume(AudioSystem.SFX);
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
		BaseContainer earplugSettings = GameUserSettings.GetModule("EC29_EarplugSettings");
		if (earplugSettings)
		{
			earplugSettings.Get("EarplugsVolume", volume);
			volume = 1.0 - (volume * 0.01);
			volume *= SFX_DefaultVolume;
		}

		return volume;
	}

	//------------------------------------------------------------------------------------------------
	void ToggleSFXVolume()
	{
		bMuted = !bMuted;

		if (bMuted)
			SetVolume(EarplugsVolume);
		else
			SetVolume(SFX_DefaultVolume);

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
