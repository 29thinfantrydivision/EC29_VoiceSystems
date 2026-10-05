//! Persisted radio preferences (game user settings, Audio tab -> 29th ID).
//!
//! The class name is the key the engine files players' saved values under,
//! and RadioBeepsEnabled is read by name from EC29_RadioBeepHelper. Do not
//! rename either, or every player's stored choice is silently dropped.
//! The engine discovers ModuleGameSettings subclasses by type - no conf
//! registration exists or is needed.
class EC29_RadioSettings : ModuleGameSettings
{
	//! Master switch for TX/RX radio beeps. Off by default: beeps are opt-in.
	[Attribute(defvalue: "0", uiwidget: UIWidgets.CheckBox, desc: "Play radio key-up and squelch beeps")]
	bool RadioBeepsEnabled;
}
