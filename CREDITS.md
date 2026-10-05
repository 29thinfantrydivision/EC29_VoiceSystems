# EC29 Voice Systems — Credits

**Engineering:** Goldwep and Bae (29th Infantry Division)

All scripts, audio graphs and sound effects are 29th ID work. Sound effects are
synthesized from scratch by `Tools/generate_sounds.py`. Models, textures and icons are
stock Arma Reforger content by Bohemia Interactive. Provenance record for the in-house
rework: `Docs/CleanRoom/README.md`.

Thanks to the Arma Reforger modding community, whose public radio and voice mods showed
what the platform can do.

## Compatibility note
If another mod that modifies VON components or the same key bindings is loaded
alongside this one, the overlapping EC29 feature set disables itself for the
session and a chat notice explains it (see
`Scripts/Game/EC29_CoexistenceGuard.c`). Do not run this mod together with
other VON-modifying mods on a production server — engine resource overrides
are last-load-wins and cannot be deconflicted from script.
