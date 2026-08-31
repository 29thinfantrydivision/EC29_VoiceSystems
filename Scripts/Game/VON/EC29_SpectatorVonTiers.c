//------------------------------------------------------------------------------------------------
//! EC29 spectator voice tiers.
//!
//! EMPTY SUBCLASSES, and that is the entire point. They exist so a spectator ghost body can carry
//! a SECOND (and third) SCR_VoNComponent alongside the one it inherits from Character_Base.et -
//! two components of the same type cannot coexist on one entity, and FindComponent needs a
//! distinct type to ask for. This is exactly how ModularVoiceRange does it (B_VoNWhispering /
//! B_VoNNormal / B_VoNLoud are all empty SCR_VoNComponent subclasses); the only thing that
//! differs between instances is the ACP set on each one in the prefab.
//!
//! Absorbed from the 29th_Spectator_V3 voice stack (its SPEC29_VoNSpectator/-Quiet pair). During
//! the delegation phase the spectator mod still ships and registers ITS OWN tier components with
//! EC29_SpectatorVonService; these EC29 classes and their ACPs are the standing replacements the
//! spectator mod's body prefab points at once its voice stack is deleted. Until then they are
//! deliberately dormant - referenced by no prefab in this project.
//!
//! RANGE LIVES IN THE ACP, not in any of these classes. SCR_VoNComponent has a "Filename"
//! attribute pointing at an audio project, and that project's AmplitudeClass carries
//!   innerRange / outerRange - the distances in metres
//!   slopeFactor             - the falloff shape
//! so the ONLY difference between the two tiers below is which .acp their prefab entry names.
//!
//! PREFAB SIDE (done in the spectator mod's body prefab, in Workbench):
//!   EC29_VoNSpectator      { Filename "{5C819950A693EB52}Sounds/VON/EC29_SpectatorVon.acp" }
//!   EC29_VoNSpectatorQuiet { Filename "{6BA89BD90EA9220F}Sounds/VON/EC29_SpectatorVonQuiet.acp" }
//!   SCR_VoNComponent       { Enabled 0 }
//! The last line matters as much as the others: FindComponent RETURNS DISABLED COMPONENTS, so the
//! inherited base component being switched off does not stop it being found - which is why
//! consumers must resolve the tier TYPE strictly, and must NEVER fall back to the base type: the
//! fallback would return that disabled inherited component, EC29_SelectVonComponent would select
//! it and report success (it verifies the pointer took, not enabled-ness), and the spectator
//! would sit on a dead component with no warning anywhere. A missing tier must fail loud
//! (dead radio, warned at registration) instead.
//!
//! THE RANGE CUTS BOTH WAYS, and that measured fact is why there are two tiers rather than one.
//! The original spectator design was a SINGLE permanently-quiet component, on the assumption that
//! outerRange governs only the SENDER. It does not: at a 1 m range the spectator could only HEAR
//! others within the same 1 m, which is deafness rather than discretion (playback is routed
//! through the ACP on the receiver's VoNComponent). The answer is a second component swapped in
//! only while the talk key is held - see EC29_SpectatorVonService.SetTransmitting. Do not
//! collapse the two back into one.
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
//! Shared base for both tiers, so anything that must one day apply to every tier has a single
//! home. Mirrors LOR_VoiceRangeComponent, which is the same shape for the same reason.
//------------------------------------------------------------------------------------------------
class EC29_VoNSpectatorTierClass : SCR_VoNComponentClass
{
}

class EC29_VoNSpectatorTier : SCR_VoNComponent
{
}

class EC29_VoNSpectatorClass : EC29_VoNSpectatorTierClass
{
}

//! NORMAL tier. Points at Sounds/VON/EC29_SpectatorVon.acp. Active whenever the spectator is NOT
//! transmitting, so its AmplitudeClass is what shapes the spectator's HEARING.
//!
//! COMPRESSED FALLOFF: the .acp overrides innerRange 40 / outerRange 68. Vanilla (inherited from
//! Amplitude_-40LUFS_to_-35LUFS.conf - the .acp text never shows inherited values) is inner 1 /
//! outer 68, curve 1/r, slope 5. SAME TOP, SAME BOTTOM, STEEPER BETWEEN: the outer stays at
//! vanilla's 68 so the fade still reaches silence exactly where the sender's own gate stops
//! delivery; only inner moves, 1 -> 40, so the whole 0-40 m band is full volume and the entire
//! vanilla fade squeezes into 40-68 m. The intent is a spectator who hears a firefight at full
//! strength rather than straining at 30 m, without hearing anything they could not hear before -
//! the SENDER's component gates who receives at all, and every living player is on their stock
//! ACP. Revert to stock attenuation by deleting the two override lines (NOT by setting them to
//! 1/68 - an explicit override stops tracking the parent conf if BI ever retunes it).
class EC29_VoNSpectator : EC29_VoNSpectatorTier
{
}

class EC29_VoNSpectatorQuietClass : EC29_VoNSpectatorTierClass
{
}

//! The SILENT tier, selected ONLY while the spectator talk key is held.
//!
//! WHY IT HAS TO EXIST. Talking on the radio ALSO speaks aloud locally - that is vanilla
//! behaviour, not a bug, and it means blocking the direct-speech ENTRY achieves nothing: the
//! local audio is a side effect of the radio transmission itself, not a separate transmission
//! that can be refused. The only lever left is the speaking RANGE, which lives in the .acp.
//!
//! WHY IT IS ONLY ACTIVE WHILE TALKING. The range in an .acp cuts both ways - measured: with a
//! 1 m range the spectator could only HEAR others within the same 1 m. Permanently quiet
//! therefore means permanently deaf, which defeats the feature. Swapping to it only for the
//! duration of a key press costs nothing, because a spectator who is talking is not listening
//! anyway.
//!
//! The .acp is a duplicate of EC29_SpectatorVon.acp with, on its AmplitudeClass:
//!   innerRange 0
//!   outerRange 0.0001
//! Both are required - the audio compiler rejects an outer range below the inner one. Outer is
//! not simply 0 because a zero risks being read as "unset" and inheriting the parent project's
//! default, which would be the opposite of what this tier is for.
class EC29_VoNSpectatorQuiet : EC29_VoNSpectatorTier
{
}
