//! Vanilla code that writes an absolute value to the SFX master bus. Each write is reported to the
//! earplugs so the plug factor is re-applied in the same call (see EC29_Earplugs_System).

//------------------------------------------------------------------------------------------------
//! Deploy menu: mutes the world while open, restores it to 1 on close.
modded class SCR_DeployMenuBase
{
	//------------------------------------------------------------------------------------------------
	override void MuteSounds(bool mute = true)
	{
		// super writes nothing when the menu is not open
		bool wrote = IsOpen();
		super.MuteSounds(mute);
		if (!wrote)
			return;

		EC29_Earplugs_System earplugs = EC29_Earplugs_System.Instance;
		if (earplugs)
			earplugs.OnExternalSFXWrite(!mute);
	}
}

//------------------------------------------------------------------------------------------------
//! Loading screen: mutes every bus while shown, restores them to 1 when hidden.
modded class ArmaReforgerLoadingAnim
{
	//------------------------------------------------------------------------------------------------
	override void EnableSounds(bool enable)
	{
		super.EnableSounds(enable);

		EC29_Earplugs_System earplugs = EC29_Earplugs_System.Instance;
		if (earplugs)
			earplugs.OnExternalSFXWrite(enable);
	}
}
