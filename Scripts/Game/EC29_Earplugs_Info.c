class EC29_Earplugs_Info extends SCR_InfoDisplay
{
	ImageWidget Image;

	//------------------------------------------------------------------------------------------------
	override void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);

		if (EC29_Debug.VERBOSE)
			Print("[EC29-DBG][EarplugsInfo] HUD display OnStartDraw (Character_Base override applied, character HUD active)", LogLevel.NORMAL);

		if (!EC29_Earplugs_System.Instance)
		{
			Print("[EC29-DBG][EarplugsInfo] EC29_Earplugs_System.Instance is NULL - system not created (ChimeraSystemsConfig override not applied?); icon will not react", LogLevel.WARNING);
			return;
		}

		if (!m_wRoot)
		{
			Print("[EC29-DBG][EarplugsInfo] Root widget missing - EarplugsOverlay.layout failed to load; icon disabled", LogLevel.WARNING);
			return;
		}

		EC29_Earplugs_System.ConnectEvent(EC29_Earplugs_System.Instance.OnEarplugsToggled, this.SetVisibility);

		Image = ImageWidget.Cast(m_wRoot.FindAnyWidget("Image0"));
		if (!Image)
			Print("[EC29-DBG][EarplugsInfo] 'Image0' widget not found in EarplugsOverlay.layout", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	override void OnStopDraw(IEntity owner)
	{
		super.OnStopDraw(owner);

		if (EC29_Earplugs_System.Instance)
			EC29_Earplugs_System.DisconnectEvents(EC29_Earplugs_System.Instance, this);
	}

	//------------------------------------------------------------------------------------------------
	[ReceiverAttribute()]
	void SetVisibility(bool state)
	{
		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][EarplugsInfo] Icon visibility -> %1", state);
		if (!Image)
			return;

		Image.SetOpacity(state);
	}
}
