class EC29_Earplugs_System extends WorldSystem
{
	static EC29_Earplugs_System Instance;

	InputManager Input;
	UserSettings EngineUserSettings;
	UserSettings GameUserSettings;
	float SFX_DefaultVolume;
	float EarplugsVolume;
	bool bMuted;

	// --- Bus watchdog -----------------------------------------------------------------------
	// The SFX master bus is a single global knob. Other mods (RHS Status Quo's RHS_HearingManager
	// during its tinnitus/muffle effect) and vanilla (deploy menu, loading screen) write ABSOLUTE
	// values to it, silently undoing the plugged level. While plugged we watch the bus each frame:
	// a value we did not write is treated as the new "open" base and re-attenuated by our factor.
	// RHS gates its own writes on its cached last value and never reads the bus back, so this
	// converges in one correction per external change instead of ping-ponging.
	protected float m_fPlugFactor = 1.0;		//!< 1 - reduction%, applied multiplicatively to the base
	protected float m_fBaseVolume = 1.0;		//!< inferred "open" level (user slider, or last external write)
	protected float m_fLastWritten = -1.0;		//!< last value WE put on the bus; -1 = nothing yet
	protected float m_fEpisodeQuietTimer;		//!< seconds since last correction (episode bookkeeping)
	protected int m_iEpisodeCorrections;		//!< corrections in the current episode
	protected float m_fRateWindow;				//!< rolling 1 s window for the ping-pong breaker
	protected int m_iRateCount;
	protected float m_fBackoffTimer;			//!< > 0: watchdog suspended (another writer is fighting back)

	protected static const float WATCHDOG_EPSILON = 0.002;
	protected static const int WATCHDOG_MAX_PER_SECOND = 30;	//!< RHS muffle ramp writes ~1/frame; a real fight is far above this
	protected static const float WATCHDOG_BACKOFF_SECONDS = 5.0;
	protected static const float WATCHDOG_EPISODE_QUIET = 1.0;

	//------------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		super.InitInfo(outInfo);
		outInfo
			.SetAbstract(false)
			.SetUnique(true)
			.SetLocation(WorldSystemLocation.Client)
			.AddPoint(WorldSystemPoint.Frame);
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
	//! Per-frame bus watchdog. Only active while plugged.
	override void OnUpdatePoint(WorldUpdatePointArgs args)
	{
		if (!bMuted)
			return;

		float dt = args.GetTimeSliceSeconds();

		if (m_fBackoffTimer > 0)
		{
			m_fBackoffTimer -= dt;
			if (m_fBackoffTimer <= 0)
			{
				m_iRateCount = 0;
				m_fRateWindow = 0;
				// Re-arm against whatever is on the bus now; one more correction is cheap.
				m_fLastWritten = -1.0;
			}
			return;
		}

		// Rolling rate window
		m_fRateWindow += dt;
		if (m_fRateWindow >= 1.0)
		{
			m_fRateWindow = 0;
			m_iRateCount = 0;
		}

		// Episode bookkeeping: report once when a burst of corrections has gone quiet
		if (m_iEpisodeCorrections > 0)
		{
			m_fEpisodeQuietTimer += dt;
			if (m_fEpisodeQuietTimer >= WATCHDOG_EPISODE_QUIET)
			{
				Print(string.Format("[EC29][Earplugs] Bus watchdog: external SFX writes ended, %1 correction(s) applied, bus now %2 (base %3 x factor %4)", m_iEpisodeCorrections, GetVolume(), m_fBaseVolume, m_fPlugFactor), LogLevel.WARNING);
				m_iEpisodeCorrections = 0;
				m_fEpisodeQuietTimer = 0;
			}
		}

		float bus = GetVolume();
		if (m_fLastWritten >= 0 && Math.AbsFloat(bus - m_fLastWritten) < WATCHDOG_EPSILON)
			return;

		// Someone else wrote the bus. Treat their value as the new open level and re-plug it.
		m_fBaseVolume = bus;
		float target = m_fBaseVolume * m_fPlugFactor;

		if (Math.AbsFloat(target - bus) < WATCHDOG_EPSILON)
		{
			// Nothing to do (e.g. factor 1.0); just adopt the value as ours.
			m_fLastWritten = bus;
			return;
		}

		m_iRateCount++;
		if (m_iRateCount > WATCHDOG_MAX_PER_SECOND)
		{
			Print(string.Format("[EC29][Earplugs] Bus watchdog: >%1 corrections/s - another mod is re-asserting the SFX bus. Backing off for %2 s. Earplugs may sound open until it stops.", WATCHDOG_MAX_PER_SECOND, WATCHDOG_BACKOFF_SECONDS), LogLevel.WARNING);
			m_fBackoffTimer = WATCHDOG_BACKOFF_SECONDS;
			return;
		}

		if (m_iEpisodeCorrections == 0)
			Print(string.Format("[EC29][Earplugs] Bus watchdog: external SFX write detected (bus=%1, ours was %2) - re-applying plug factor %3 -> %4", bus, m_fLastWritten, m_fPlugFactor, target), LogLevel.WARNING);

		m_iEpisodeCorrections++;
		m_fEpisodeQuietTimer = 0;
		SetVolume(target);
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
		bMuted = !bMuted;

		// Plug/unplug relative to the inferred open level, so unplugging mid-RHS-muffle hands the bus
		// back at whatever the other writer last wanted rather than slamming it to the slider value.
		float targetVolume = m_fBaseVolume;
		if (bMuted)
			targetVolume = m_fBaseVolume * m_fPlugFactor;

		SetVolume(targetVolume);
		m_iEpisodeCorrections = 0;
		m_fEpisodeQuietTimer = 0;

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
