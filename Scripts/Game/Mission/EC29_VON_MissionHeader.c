modded class SCR_MissionHeader
{
	[Attribute(desc: "EC29 VON overlay and nametag policy. Direct-speech ranges are fixed per transmit tier in the mod's ACPs (whisper 2/6 m, normal 15/20 m, yell 50/80 m) and are not mission-tunable.", category: "EC29_VON")]
	ref EC29_VON_Settings m_EC29_VON_Settings;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class EC29_VON_Settings : ScriptAndConfig
{
	//------------------------------------------------------------------------------------------------
	void EC29_VON_Settings()
	{
		m_bAlwaysShowEnemyNames         = true;
		m_bEnableVonFactionNameColoring = true;
		m_bHideFriendlyDirectIncoming   = false;
		m_bHideEnemyDirectIncoming      = true;
		m_bHideRoleInVonOverlay         = true;
		m_bShowVoiceModeInOverlay       = true;
		m_bGateNameTagVonByRange        = true;

	}

	// VoN UI Settings
	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Always show enemy sender names in the VoN overlay (vanilla swaps to UNKNOWN SOURCE for enemies).", category: "EC29_VON")]
	bool m_bAlwaysShowEnemyNames;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Color the incoming sender's name in the VoN overlay by their faction color.", category: "EC29_VON")]
	bool m_bEnableVonFactionNameColoring;

	[Attribute(uiwidget: UIWidgets.CheckBox, desc: "Hide friendly direct (proximity) incoming VoN entries from the overlay.", category: "EC29_VON")]
	bool m_bHideFriendlyDirectIncoming;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Hide enemy direct (proximity) incoming VoN entries from the overlay.", category: "EC29_VON")]
	bool m_bHideEnemyDirectIncoming;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Completely hide the (Role) text in brackets next to player names in the VoN overlay.", category: "EC29_VON")]
	bool m_bHideRoleInVonOverlay;

	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Show WHISPER / YELLING label in the VoN overlay channel slot for incoming direct transmissions. NORMAL transmissions show no label (default vanilla behaviour).", category: "EC29_VON")]
	bool m_bShowVoiceModeInOverlay;

	// Nametag UI Settings
	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Suppress the over-head nametag VON icon for direct transmissions when the speaker is out of audible range, so whispers/distant talkers don't reveal themselves visually.", category: "EC29_VON")]
	bool m_bGateNameTagVonByRange;
}
