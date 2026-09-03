modded class SCR_MissionHeader
{
	[Attribute(desc: "EC29 VON voice range settings", category: "EC29_VON")]
	ref EC29_VON_Settings m_EC29_VON_Settings;
}

//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class EC29_VON_Settings : ScriptAndConfig
{
	//------------------------------------------------------------------------------------------------
	void EC29_VON_Settings()
	{
		m_fWhisperRange  = 3.0;
		m_fNormalRange   = 15.0;
		m_fNormalFalloffEnd = 20.0;
		m_fYellRange     = 50.0;
		m_fFalloffPower  = 4.0;
		m_fWhisperVolume = 1.0;
		m_fNormalVolume  = 1.0;
		m_fYellVolume    = 3.0;
		m_fMinVolume     = 0.05;

		m_bAlwaysShowEnemyNames         = true;
		m_bEnableVonFactionNameColoring = true;
		m_bHideFriendlyDirectIncoming   = false;
		m_bHideEnemyDirectIncoming      = true;
		m_bHideRoleInVonOverlay         = true;
		m_bShowVoiceModeInOverlay       = true;
		m_bGateNameTagVonByRange        = true;

		m_bReceiverRepairEnabled = true;
	}

	// VoN Range Setings
	[Attribute(defvalue: "3.0", uiwidget: UIWidgets.EditBox, desc: "Whisper characteristic range in meters. Beyond this distance, volume falls off as (R/dist)^FalloffPower.", params: "0 inf 0.5", category: "EC29_VON")]
	float m_fWhisperRange;

	[Attribute(defvalue: "15.0", uiwidget: UIWidgets.EditBox, desc: "Normal voice range in meters: full volume inside this distance, then a straight-line fade to silence at NormalFalloffEnd.", params: "0 inf 0.5", category: "EC29_VON")]
	float m_fNormalRange;

	[Attribute(defvalue: "20.0", uiwidget: UIWidgets.EditBox, desc: "Distance in meters at which NORMAL voice is fully silent. Must exceed NormalRange; the fade runs linearly between the two. Set equal to NormalRange for a hard cutoff.", params: "0 inf 0.5", category: "EC29_VON")]
	float m_fNormalFalloffEnd;

	[Attribute(defvalue: "50.0", uiwidget: UIWidgets.EditBox, desc: "Yell characteristic range in meters. Beyond this distance, volume falls off but base volume is high so it still carries.", params: "0 inf 0.5", category: "EC29_VON")]
	float m_fYellRange;

	[Attribute(defvalue: "4.0", uiwidget: UIWidgets.EditBox, desc: "Falloff exponent for the inverse-power decay outside characteristic range. 2.0 = inverse-square, 1.0 = inverse-linear (gentler).", params: "0.1 8 0.1", category: "EC29_VON")]
	float m_fFalloffPower;

	[Attribute(defvalue: "1.0", uiwidget: UIWidgets.EditBox, desc: "Whisper base volume multiplier (within range).", params: "0 3 0.05", category: "EC29_VON")]
	float m_fWhisperVolume;

	[Attribute(defvalue: "1.0", uiwidget: UIWidgets.EditBox, desc: "Normal base volume multiplier.", params: "0 3 0.05", category: "EC29_VON")]
	float m_fNormalVolume;

	[Attribute(defvalue: "3.0", uiwidget: UIWidgets.EditBox, desc: "Yell base volume multiplier (within range). Capped at 3 to match the EC29_VonRange audio variable's value_max; raise both together for louder yelling.", params: "0 3 0.05", category: "EC29_VON")]
	float m_fYellVolume;

	[Attribute(defvalue: "0.05", uiwidget: UIWidgets.EditBox, desc: "Minimum amplitude floor. Volumes below this are clamped up to keep the audio source alive in the engine (otherwise it culls and won't recover until the next transmission). 0.05 (~-26 dB) is inaudible in practice but enough to keep the source warm.", params: "0 1 0.01", category: "EC29_VON")]
	float m_fMinVolume;

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

	// Radio repair diagnostics (1.8 receiver-registration defect)
	[Attribute(defvalue: "1", uiwidget: UIWidgets.CheckBox, desc: "Run EC29 receiver-registration repair: a one-time silent power cycle per radio shortly after its VON entry registers (plus the spectator ghost radio). Turn OFF to measure whether a game update fixed the defect natively - with this off, the RX heartbeat WARNING under live traffic is the evidence.", category: "EC29_VON")]
	bool m_bReceiverRepairEnabled;
}
