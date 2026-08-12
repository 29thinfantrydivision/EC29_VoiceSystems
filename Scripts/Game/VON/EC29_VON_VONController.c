modded class SCR_VONController
{
	protected const string EC29_ACTION_VOICE_RANGE_CYCLE = "EC29_VONVoiceRangeCycle";

	//------------------------------------------------------------------------------------------------
	override protected void Init(IEntity owner)
	{
		super.Init(owner);

		if (m_InputManager)
			m_InputManager.AddActionListener(EC29_ACTION_VOICE_RANGE_CYCLE, EActionTrigger.DOWN, EC29_ActionVoiceRangeCycle);
	}

	//------------------------------------------------------------------------------------------------
	override protected void Cleanup()
	{
		if (m_InputManager)
			m_InputManager.RemoveActionListener(EC29_ACTION_VOICE_RANGE_CYCLE, EActionTrigger.DOWN, EC29_ActionVoiceRangeCycle);

		super.Cleanup();
	}

	//------------------------------------------------------------------------------------------------
	protected void EC29_ActionVoiceRangeCycle(float value, EActionTrigger reason = EActionTrigger.UP)
	{
		if (!m_VONComp)
		{
			PrintFormat("[EC29_VON] Cycle pressed but no SCR_VoNComponent on controlled entity", level: LogLevel.WARNING);
			return;
		}

		EC29_EVoiceRange current = m_VONComp.EC29_GetVoiceRange();
		EC29_EVoiceRange next;
		switch (current)
		{
			case EC29_EVoiceRange.WHISPER: next = EC29_EVoiceRange.NORMAL;  break;
			case EC29_EVoiceRange.NORMAL:  next = EC29_EVoiceRange.YELL;    break;
			case EC29_EVoiceRange.YELL:    next = EC29_EVoiceRange.WHISPER; break;
			default:                      next = EC29_EVoiceRange.NORMAL;  break;
		}

		m_VONComp.EC29_RequestSetVoiceRange(next);

		// Refresh the VoN overlay label immediately for the local outgoing transmission.
		// The RplProp callback handles remote receivers, but the authority does not always
		// fire its own onRplName for its own writes - this guarantees local UI snaps to the
		// new mode the same frame the input is pressed.
		SCR_VonDisplay display = m_VONComp.GetDisplay();
		if (display)
			display.EC29_ForceRefreshAllTransmissions();
	}
}
