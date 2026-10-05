//! Binds the EC29 controls in the Audio settings tab ("29th ID" section of
//! UI/Layouts/Menus/SettingsMenu/AudioSettings.layout) to their game-settings
//! modules. Vanilla builds and loads its own bindings first; the EC29 ones are
//! appended to the same list so the tab's reload / apply / save path treats
//! them exactly like vanilla entries.
modded class SCR_AudioSettingsSubMenu
{
	//------------------------------------------------------------------------------------------------
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		if (!m_wScroll)
		{
			Print("[EC29] Audio settings tab has no scroll layout - 29th ID controls not bound", LogLevel.WARNING);
			return;
		}

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][EarplugsMenu] Binding earplugs reduction slider (Earplugs)");

		EC29_AddGameplayBinding("EC29_EarplugSettings", "EarplugsVolume", "Earplugs");

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][EarplugsMenu] Binding radio beeps checkbox (RadioBeeps)");

		EC29_AddGameplayBinding("EC29_RadioSettings", "RadioBeepsEnabled", "RadioBeeps");

		EC29_WarnIfModuleMissing("EC29_EarplugSettings", "earplugs reduction slider");
		EC29_WarnIfModuleMissing("EC29_RadioSettings", "radio beeps checkbox");
	}

	//------------------------------------------------------------------------------------------------
	//! Same steps vanilla LoadSettings performs per binding, for one extra entry.
	protected void EC29_AddGameplayBinding(string module, string property, string widgetName)
	{
		SCR_SettingBindingGameplay binding = new SCR_SettingBindingGameplay(module, property, widgetName);
		m_aSettingsBindings.Insert(binding);

		m_bLoadingSettings = true;
		binding.LoadEntry(m_wScroll);
		m_bLoadingSettings = false;

		binding.GetEntryChangedInvoker().Insert(OnMenuItemChanged);
	}

	//------------------------------------------------------------------------------------------------
	//! Not debug-gated: a missing module means the control silently does nothing.
	protected void EC29_WarnIfModuleMissing(string module, string control)
	{
		UserSettings gameSettings = GetGame().GetGameUserSettings();
		if (gameSettings && gameSettings.GetModule(module))
			return;

		PrintFormat("[EC29] %1 not found in game user settings - the Audio-tab %2 is inert", module, control, level: LogLevel.WARNING);
	}
}
