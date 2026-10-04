//! Switches the owning entity's EC29_JammerComponent on or off.
//!
//! Prefabs attach it under additionalActions with ParentContextList "JammerControlPanel". Shown
//! and performable whenever the component exists - no faction, permission or rate checks beyond
//! the engine's own action rules.
//!
//! User actions execute on both the server and the performing client. Only the authority changes
//! state (offline/editor counts as authority); a client flipping its local copy would race the
//! replicated value and could switch the jammer straight back.
class EC29_JammerToggleUserAction : ScriptedUserAction
{
	protected EC29_JammerComponent m_EC29_Jammer;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		if (pOwnerEntity)
			m_EC29_Jammer = EC29_JammerComponent.Cast(pOwnerEntity.FindComponent(EC29_JammerComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_EC29_Jammer != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		return m_EC29_Jammer != null;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (!m_EC29_Jammer)
			outName = "Toggle Jammer";
		else if (m_EC29_Jammer.IsJammerActive())
			outName = "Disable Jammer";
		else
			outName = "Enable Jammer";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		super.PerformAction(pOwnerEntity, pUserEntity);

		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_EC29_Jammer)
			return;

		bool switchOn = !m_EC29_Jammer.IsJammerActive();

		if (EC29_Debug.VERBOSE)
			PrintFormat("[EC29-DBG][Jammer] Toggle action on authority - jammer now active=%1", switchOn);

		m_EC29_Jammer.SetJammerActive(switchOn);
	}
}
