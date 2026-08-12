modded class SCR_AudioSettingsSubMenu
{
	//------------------------------------------------------------------------------------------------
	override void OnTabCreate(Widget menuRoot, ResourceName buttonsLayout, int index)
	{
		super.OnTabCreate(menuRoot, buttonsLayout, index);

		SCR_SettingBindingGameplay bind = new SCR_SettingBindingGameplay("EC29_EarplugSettings", "EarplugsVolume", "Earplugs");
		m_aSettingsBindings.Insert(bind);

		bind.LoadEntry(m_wScroll, false, true);
		bind.GetEntryChangedInvoker().Insert(OnMenuItemChanged);
	}
}

class EC29_EarplugSettings extends ModuleGameSettings
{
	[Attribute(defvalue: "80", uiwidget: UIWidgets.Slider, params: "0 100 1")]
	int EarplugsVolume;
}
