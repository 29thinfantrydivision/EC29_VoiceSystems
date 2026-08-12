# EC29 Voice Systems — Credits

**Engineering:** Goldwep (29th Infantry Division)

## Audio
- Sound effects from http://www.freesfx.co.uk
- Roger beep derived from "Walkie Talkie.wav" by CGEffex
  (https://freesound.org/people/CGEffex/sounds/97797/), licensed CC BY 4.0
  (https://creativecommons.org/licenses/by/4.0/) — clipped and volume-adjusted.
- Error tone via freesound.org, CC0 1.0 (public domain).
- Additional radio squelch/beep samples from community sound libraries.

## Compatibility note
If another mod that modifies VON components or the same key bindings is loaded
alongside this one, the overlapping EC29 feature set disables itself for the
session and a chat notice explains it (see
`Scripts/Game/EC29_CoexistenceGuard.c`). Do not run this mod together with
other VON-modifying mods on a production server — engine resource overrides
are last-load-wins and cannot be deconflicted from script.
