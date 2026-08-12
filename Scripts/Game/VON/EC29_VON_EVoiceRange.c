//------------------------------------------------------------------------------------------------
//! Voice transmission range mode for direct VoN.
//! Maps to a multiplier on the speaker's audio variable that the direct bus
//! in von.acp reads to scale playback volume per-listener.
enum EC29_EVoiceRange
{
	WHISPER,
	NORMAL,
	YELL
}
