//------------------------------------------------------------------------------------------------
//! Two empty SCR_VoNComponent subclasses so the editor manager can carry extra VoN components (two
//! of one type cannot coexist on an entity, and FindComponent needs a distinct type). The ACP on
//! each prefab entry is what matters, and so does their ORDER. Field-measured 2026-09-05: the
//! engine treats the FIRST VoN component on the manager as the editor's ear, and it orders an
//! entity's components by CLASS NAME (every probe: Quiet < Tier < SCR_VoNComponent, whatever the
//! prefab listed), so the EAR class has to sort before every other VoN class on the entity:
//!   EC29_VoNSpectatorLoud  { Filename von.acp }         - the ear: connected, selected, full range
//!   EC29_VoNSpectatorQuiet { Filename ...VonQuiet.acp } - the transmit tier below
//! LOUD sorts before QUIET (L < Q) and before SCR_VoNComponent (E < S), which is the only reason
//! the ear works. ANY VoN class added to this manager later must sort AFTER Loud or it silently
//! becomes the ear and every spectator goes deaf. The inherited base stays untouched.
//!
//! The spectator TRANSMIT tier: what makes it quiet is the ACP set on the prefab entry:
//!   EC29_VoNSpectatorQuiet { Filename "{6BA89BD90EA9220F}Sounds/VON/EC29_SpectatorVonQuiet.acp" }
//! whose AmplitudeClass carries innerRange 0 / outerRange 0.0001.
//!
//! WHY IT EXISTS. Talking on a radio ALSO speaks aloud locally - vanilla behaviour, a side effect
//! of the transmission itself. The only lever is the speaking RANGE, which lives in the ACP, and
//! it cuts both ways (a 1 m range also means hearing nothing beyond 1 m). So this tier is selected
//! ONLY while the talk key is held; hearing stays on the ear component.
//!
//! THE HEARING CURVE lives on AmplitudeClass 200010 in von.acp, and the PARENT'S REAL VALUES are
//! worth writing down because they are not what an old note in this repo claimed: vanilla character
//! voice is curve "1/r", outerRange 40, slopeFactor 11 - silent at 40 m, not 68, and very steep.
//! Ours holds full volume to 40 and fades to 68, but inheriting slopeFactor 11 squeezed that whole
//! steep curve into a 28 m band and produced a cliff at 40 m (field-reported 2026-09-05). The node
//! now overrides slopeFactor; lower is gentler, and it is the knob to turn if the fade is wrong.
//!
//! THE EAR ALSO PROTECTS REAL GAME MASTERS. Vanilla's SCR_EditorManagerEntity.Open() wires editor
//! voice through FindComponent(SCR_VoNComponent), which returns that same first component - so
//! without the ear, an opened editor would run a GM's voice through the near-silent tier. The ear
//! carries the stock von.acp, so a GM's experience is what it always was.
//------------------------------------------------------------------------------------------------
class EC29_VoNSpectatorTierClass : SCR_VoNComponentClass
{
}

class EC29_VoNSpectatorTier : SCR_VoNComponent
{
}

class EC29_VoNSpectatorLoudClass : EC29_VoNSpectatorTierClass
{
}

class EC29_VoNSpectatorLoud : EC29_VoNSpectatorTier
{
}

class EC29_VoNSpectatorQuietClass : EC29_VoNSpectatorTierClass
{
}

class EC29_VoNSpectatorQuiet : EC29_VoNSpectatorTier
{
}
