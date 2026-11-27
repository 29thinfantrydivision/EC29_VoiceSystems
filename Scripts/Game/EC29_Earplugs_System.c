class EC29_Earplugs_System extends WorldSystem
{
	static EC29_Earplugs_System Instance;

	InputManager Input;
	UserSettings EngineUserSettings;
	UserSettings GameUserSettings;
	float SFX_DefaultVolume;
	float EarplugsVolume;
	bool bMuted;

	// --- SFX bus ownership --------------------------------------------------------------------
	// The SFX master bus is a single global knob, and other code writes ABSOLUTE values to it,
	// which would silently undo the plugged level. Nothing polls the bus. Each known writer is
	// hooked at its one write point and reports the value through OnExternalSFXWrite, which takes
	// it as the new "open" level and re-applies the plug factor in the same call:
	//   RHS Status Quo hearing model   EC29_RHS_HearingHook.c   (RHS_HearingManager.ApplyVolume)
	//   vanilla deploy menu            EC29_SFXBusHooks.c       (SCR_DeployMenuBase.MuteSounds)
	//   vanilla loading screen         EC29_SFXBusHooks.c       (ArmaReforgerLoadingAnim.EnableSounds)
	// A writer with no hook (another mod, a scenario framework volume action) is picked up at the
	// next plug toggle, which reads the bus back before it acts.
	protected float m_fPlugFactor = 1.0;		//!< 1 - reduction%, applied multiplicatively to the base
	protected float m_fBaseVolume = 1.0;		//!< "open" level (user slider, or the last external write)
	protected float m_fLastWritten = -1.0;		//!< last value WE put on the bus; -1 = nothing yet

	protected static const float BUS_EPSILON = 0.002;

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

		m_fBaseVolume = SFX_DefaultVolume;
		if (GetVolume() < SFX_DefaultVolume)
			SetVolume(SFX_DefaultVolume);
	}

	//------------------------------------------------------------------------------------------------
	//! A hooked writer reports that it has just put this value on the SFX bus (RHS hearing model,
	//! deploy menu, loading screen). Take it as the open level and, while plugged, re-attenuate it
	//! in the same call so the bus never holds an un-plugged value.
	void OnExternalSFXWrite(float volume)
	{
		m_fBaseVolume = volume;
		if (!bMuted)
			return;

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] External SFX write reported (%1) - bus set to %2", volume, volume * m_fPlugFactor);

		SetVolume(volume * m_fPlugFactor);
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
		m_fBaseVolume = SFX_DefaultVolume;

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
			m_fPlugFactor = volume;
			volume *= SFX_DefaultVolume;
		}
		else
		{
			Print("[EC29-DBG][Earplugs] Settings module 'EC29_EarplugSettings' NOT found - plugged volume will be 0 (full mute)", LogLevel.WARNING);
			m_fPlugFactor = 0;
		}

		return volume;
	}

	//------------------------------------------------------------------------------------------------
	void ToggleSFXVolume()
	{
		// Pick up a write from a writer that has no hook.
		float bus = GetVolume();
		if (m_fLastWritten < 0 || Math.AbsFloat(bus - m_fLastWritten) >= BUS_EPSILON)
			m_fBaseVolume = bus;

		bMuted = !bMuted;

		// Plug/unplug relative to the inferred open level, so unplugging mid-RHS-muffle hands the bus
		// back at whatever the other writer last wanted rather than slamming it to the slider value.
		float targetVolume = m_fBaseVolume;
		if (bMuted)
			targetVolume = m_fBaseVolume * m_fPlugFactor;

		SetVolume(targetVolume);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Earplugs] Toggled: muted=%1 -> SFX master volume set to %2 (readback=%3)", bMuted, targetVolume, GetVolume());
		ThrowEvent(this.OnEarplugsToggled, bMuted);
	}

	//------------------------------------------------------------------------------------------------
	void SetVolume(float volume)
	{
		m_fLastWritten = volume;
		AudioSystem.SetMasterVolume(AudioSystem.SFX, volume);
	}

	//------------------------------------------------------------------------------------------------
	float GetVolume()
	{
		return AudioSystem.GetMasterVolume(AudioSystem.SFX);
	}
}
