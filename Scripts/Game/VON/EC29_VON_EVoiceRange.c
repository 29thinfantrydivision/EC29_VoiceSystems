//------------------------------------------------------------------------------------------------
//! Voice transmission range mode for direct VoN.
//! Selects which transmit tier component the speaker captures through (EC29_VoiceTiers.c);
//! the tier's ACP carries the range. Replicated on the stock SCR_VoNComponent for labels and
//! the visual range gates.
enum EC29_EVoiceRange
{
	WHISPER,
	NORMAL,
	YELL
}
