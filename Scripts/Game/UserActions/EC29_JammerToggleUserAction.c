class EC29_JammerToggleUserAction : ScriptedUserAction
{
	protected EC29_JammerComponent m_JammerComponent;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);

		m_JammerComponent = EC29_JammerComponent.Cast(
			pOwnerEntity.FindComponent(EC29_JammerComponent)
		);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_JammerComponent != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return m_JammerComponent != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_JammerComponent)
		{
			outName = "Toggle Jammer";
			return true;
		}

		if (m_JammerComponent.IsJammerActive())
			outName = "Disable Jammer";
		else
			outName = "Enable Jammer";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);

		if (!m_JammerComponent)
			return;

		bool newState = !m_JammerComponent.IsJammerActive();
		m_JammerComponent.SetJammerActive(newState);
	}
}
