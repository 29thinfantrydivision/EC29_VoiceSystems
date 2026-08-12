class EC29_Earplugs_Info extends SCR_InfoDisplay
{
	ImageWidget Image;

	//------------------------------------------------------------------------------------------------
	override void OnStartDraw(IEntity owner)
	{
		super.OnStartDraw(owner);

		EC29_Earplugs_System.ConnectEvent(EC29_Earplugs_System.Instance.OnEarplugsToggled, this.SetVisibility);

		Image = ImageWidget.Cast(m_wRoot.FindAnyWidget("Image0"));
	}

	//------------------------------------------------------------------------------------------------
	override void OnStopDraw(IEntity owner)
	{
		super.OnStopDraw(owner);

		EC29_Earplugs_System.DisconnectEvents(EC29_Earplugs_System.Instance, this);
	}

	//------------------------------------------------------------------------------------------------
	[ReceiverAttribute()]
	void SetVisibility(bool state)
	{
		if (!Image)
			return;

		Image.SetOpacity(state);
	}
}
