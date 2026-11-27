//! RHS Status Quo's hearing model writes absolute values to the SFX master bus (limiter, passive
//! protection, the muffle after ringing). Every one of those writes goes through ApplyVolume, so
//! the earplugs take the value here and re-apply the plug factor in the same call. The bus never
//! holds an un-plugged value for a frame, and the earplug bus watchdog has nothing to correct.
//! Needs RHS Status Quo loaded (addon.gproj dependency 595F2BF2F44836FB).
modded class RHS_HearingManager
{
	//------------------------------------------------------------------------------------------------
	override protected void ApplyVolume(float volume)
	{
		// super is cache-gated: it only writes the bus when its applied value changes.
		float before = m_fAppliedVolume;
		super.ApplyVolume(volume);
		if (m_fAppliedVolume == before)
			return;

		EC29_Earplugs_System earplugs = EC29_Earplugs_System.Instance;
		if (earplugs)
			earplugs.OnExternalSFXWrite(volume);
	}
}
