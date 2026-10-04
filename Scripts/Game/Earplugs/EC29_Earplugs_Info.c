//! Character-HUD indicator for earplugs: one icon on the right screen edge,
//! fully opaque while plugged, fully transparent otherwise. Registered on the
//! SCR_BaseHUDComponent of Prefabs/Characters/Core/Character_Base.et with
//! UI/Layouts/HUD/Earplugs/EarplugsOverlay.layout.
//!
//! Visibility is driven by opacity only - the widget is never removed or
//! re-laid out. On draw start the icon is synced to the system's current
//! state, so respawning (which rebuilds the character HUD) while plugged keeps
//! the icon in step with the ducked SFX instead of waiting for the next toggle.
class EC29_Earplugs_Info : SCR_InfoDisplay
{
	//! Image widget name in EarplugsOverlay.layout - change both together.
	protected static const string EC29_INDICATOR_WIDGET = "Image0";

	protected ImageWidget m_wEC29_Indicator;
	//! Weak; the system outlives every HUD built during its world.
	protected EC29_Earplugs_System m_EC29_System;

	//------------------------------------------------------------------------------------------------
	override void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][EarplugsInfo] Draw start");

		EC29_Earplugs_System system = EC29_Earplugs_System.GetInstance();
		if (!system)
		{
			Print("[EC29-DBG][EarplugsInfo] EC29_Earplugs_System not running - indicator disabled (check ChimeraSystemsConfig.conf)", LogLevel.WARNING);
			return;
		}

		if (!m_wRoot)
		{
			Print("[EC29-DBG][EarplugsInfo] Overlay layout root missing after draw start - indicator disabled", LogLevel.WARNING);
			return;
		}

		m_EC29_System = system;
		m_EC29_System.GetOnToggled().Insert(EC29_OnToggled);

		m_wEC29_Indicator = ImageWidget.Cast(m_wRoot.FindAnyWidget(EC29_INDICATOR_WIDGET));
		if (!m_wEC29_Indicator)
		{
			PrintFormat("[EC29-DBG][EarplugsInfo] Image widget '%1' not found in overlay layout", EC29_INDICATOR_WIDGET, level: LogLevel.WARNING);
			return;
		}

		EC29_ShowIndicator(m_EC29_System.IsPlugged());
	}

	//------------------------------------------------------------------------------------------------
	override void OnStopDraw(IEntity owner)
	{
		if (m_EC29_System)
			m_EC29_System.GetOnToggled().Remove(EC29_OnToggled);

		m_EC29_System = null;
		m_wEC29_Indicator = null;

		super.OnStopDraw(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_OnToggled(bool plugged)
	{
		EC29_ShowIndicator(plugged);
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_ShowIndicator(bool plugged)
	{
		if (!m_wEC29_Indicator)
			return;

		float opacity = 0;
		if (plugged)
			opacity = 1;

		m_wEC29_Indicator.SetOpacity(opacity);

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][EarplugsInfo] Indicator opacity=%1 (plugged=%2)", opacity, plugged);
	}
}
