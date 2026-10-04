//------------------------------------------------------------------------------------------------
//! Mission-facing policy for how direct speech is presented on screen (VoN overlay entries and
//! the over-head nametag speaking icon). Audio is not configured here.
//!
//! The member names below are stored in existing mission headers - renaming one silently drops
//! that mission's setting. Range and volume knobs used to live here too; they were removed when
//! the ranges moved into the per-tier ACPs and must not come back (old headers that still carry
//! them only log an unknown-property warning).
[BaseContainerProps()]
class EC29_VON_Settings : ScriptAndConfig
{
	[Attribute("1", UIWidgets.CheckBox, "Name the speaker on every incoming overlay entry, enemies included (vanilla shows enemies as an unknown source).", category: "EC29_VON")]
	bool m_bAlwaysShowEnemyNames;

	[Attribute("1", UIWidgets.CheckBox, "Colour incoming speaker names in the overlay with the speaker's faction colour.", category: "EC29_VON")]
	bool m_bEnableVonFactionNameColoring;

	[Attribute(uiwidget: UIWidgets.CheckBox, desc: "Drop overlay entries for direct speech coming from non-hostile players.", category: "EC29_VON")]
	bool m_bHideFriendlyDirectIncoming;

	[Attribute("1", UIWidgets.CheckBox, "Drop overlay entries for direct speech coming from hostile players.", category: "EC29_VON")]
	bool m_bHideEnemyDirectIncoming;

	[Attribute("1", UIWidgets.CheckBox, "Strip the bracketed role text shown beside incoming speaker names.", category: "EC29_VON")]
	bool m_bHideRoleInVonOverlay;

	[Attribute("1", UIWidgets.CheckBox, "Put WHISPER / YELLING in the overlay's channel slot for direct speech. Normal voice shows nothing.", category: "EC29_VON")]
	bool m_bShowVoiceModeInOverlay;

	[Attribute("1", UIWidgets.CheckBox, "Keep the nametag speaking icon off while a direct speaker is beyond the reach of the voice tier they are using.", category: "EC29_VON")]
	bool m_bGateNameTagVonByRange;

	//------------------------------------------------------------------------------------------------
	//! Mirrors the attribute defaults so a block built from script matches one placed in the editor.
	void EC29_VON_Settings()
	{
		m_bAlwaysShowEnemyNames = true;
		m_bEnableVonFactionNameColoring = true;
		m_bHideFriendlyDirectIncoming = false;
		m_bHideEnemyDirectIncoming = true;
		m_bHideRoleInVonOverlay = true;
		m_bShowVoiceModeInOverlay = true;
		m_bGateNameTagVonByRange = true;
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_MissionHeader
{
	[Attribute(desc: "EC29 voice presentation policy. Direct-speech reach is NOT set here: it is fixed per tier in the ACPs (whisper 2/6 m, normal 15/20 m, yell 50/80 m, inner/outer).", category: "EC29_VON")]
	ref EC29_VON_Settings m_EC29_VON_Settings;
}
