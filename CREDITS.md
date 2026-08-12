# EC29 Voice Systems — Credits & Attribution

EC29 Voice Systems is a 29th Infantry Division standalone merge of two
community mods, adapted with permission/license and renamed to the EC29
namespace so it cannot conflict with the originals.

## Absorbed works

### WCS_VON + WCS_Earplugs (What Could Go Wrong / WCS team)
- Voice range system (whisper / normal / yell with server-replicated falloff)
  and the earplugs SFX toggle.
- Used with the WCS team's verbal permission (obtained by Nathan/Goldwep prior
  to the port). Keep this note in any submission to 29th chief of engineering.

### 506IRRU — Enhanced Radio v1.0.31 (Fedora12, 506th IRRU)
- Ear routing, manual frequency entry, TX/RX radio beeps, per-channel volume,
  alternate channel PTT, radio jammers / EWar, RF propagation + squelch model.
- License: **Arma Public License Share Alike (APL-SA)** — adapted with
  attribution; this mod remains APL-SA-compatible.
- Original credits carried forward from the source mod:
  - GRS: sound files
  - ACE (and ACRE2): sound files
  - Sound effects from http://www.freesfx.co.uk
  - Roger beep derived from "Walkie Talkie.wav" by CGEffex
    (https://freesound.org/people/CGEffex/sounds/97797/), licensed CC BY 4.0
    (https://creativecommons.org/licenses/by/4.0/) — clipped and volume-adjusted.
  - Error tone via freesound.org, CC0 1.0 (public domain).

## Coexistence behavior
If the original 506IRRU Enhanced Radio, WCS_VON, or WCS_Earplugs mods are
loaded alongside this mod, the matching EC29 feature set disables itself for
the session and a chat notice explains it (see
`Scripts/Game/EC29_CoexistenceGuard.c`). Resource overrides (key bindings,
input actions, GameMode prefab, VON audio project) are last-load-wins by
engine design and cannot be deconflicted from script — do not run this mod
together with the originals on a production server.
