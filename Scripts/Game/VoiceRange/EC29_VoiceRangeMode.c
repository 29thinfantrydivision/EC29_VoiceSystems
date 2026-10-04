//------------------------------------------------------------------------------------------------
//! Direct-speech loudness the player has selected.
//!
//! The ordinals are part of the wire format: the value travels in a replicated property and in
//! the change request RPC, and the server bounds-checks it against the first and last member.
//! The keybind steps through the members in declaration order and wraps around, and the stock
//! VoN component spawns everyone on the first member (issue #11). Append nothing in between and
//! never reorder.
enum EC29_EVoiceRange
{
	WHISPER,
	NORMAL,
	YELL
}
