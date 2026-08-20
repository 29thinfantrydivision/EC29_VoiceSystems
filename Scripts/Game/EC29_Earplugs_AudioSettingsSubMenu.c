modded class SCR_AudioSettingsSubMenu
{
	//------------------------------------------------------------------------------------------------
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][EarplugsMenu] Audio settings tab created - inserting earplug reduction slider", LogLevel.NORMAL);
		SCR_SettingBindingGameplay bind = new SCR_SettingBindingGameplay("EC29_EarplugSettings", "EarplugsVolume", "Earplugs");
		m_aSettingsBindings.Insert(bind);

		bind.LoadEntry(m_wScroll, false, true);
		bind.GetEntryChangedInvoker().Insert(OnMenuItemChanged);

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][RadioMenu] Inserting radio beeps checkbox (default off)", LogLevel.NORMAL);
		SCR_SettingBindingGameplay beepBind = new SCR_SettingBindingGameplay("EC29_RadioSettings", "RadioBeepsEnabled", "RadioBeeps");
		m_aSettingsBindings.Insert(beepBind);

		beepBind.LoadEntry(m_wScroll, false, true);
		beepBind.GetEntryChangedInvoker().Insert(OnMenuItemChanged);

		// Not VERBOSE-gated: if either settings module failed to register, its
		// widget renders but never reads or writes the setting (the binding
		// ctor needs GetModule to succeed; vanilla LoadEntry then warns
		// "Settings item not found" and bails). Make that state unmissable in
		// the log the moment the Audio tab is opened.
		if (!GetGame().GetGameUserSettings().GetModule("EC29_RadioSettings"))
			Print("[EC29] Audio tab: EC29_RadioSettings module missing - Radio Beeps checkbox is inert", LogLevel.WARNING);
		if (!GetGame().GetGameUserSettings().GetModule("EC29_EarplugSettings"))
			Print("[EC29] Audio tab: EC29_EarplugSettings module missing - Earplugs slider is inert", LogLevel.WARNING);
	}
}

class EC29_RadioSettings extends ModuleGameSettings
{
	[Attribute(defvalue: "0", uiwidget: UIWidgets.CheckBox, desc: "Play radio TX/RX beeps (squelch, key-up confirmation)")]
	bool RadioBeepsEnabled;
}

class EC29_EarplugSettings extends ModuleGameSettings
{
	[Attribute(defvalue: "80", uiwidget: UIWidgets.Slider, params: "0 100 1")]
	int EarplugsVolume;
}
