# In-house rework — provenance record

Branch `in-house-rework` (2026-10-03). Goal: every script, graph, sound and art asset that
ships in EC29 Voice Systems is 29th ID work or stock Bohemia content, with a paper trail
that shows how it got that way.

## Method (two-room rewrite)

1. **Spec room.** Engineers who had read the earlier implementation wrote behavior-only
   specs (this folder: `Spec_*.md`). Specs carry the external contract — class names,
   action names, saved-setting names, GUIDs, audio variable names that other files or
   players' saved data depend on — plus behavior in prose and numbers. No code.
2. **Build room.** Separate engineers, given only a spec plus stock game files and the
   rest of the repo, wrote the new implementation. They were barred from opening the
   files being replaced, the earlier implementation's git history, and any reference
   material from other mods. Old files were deleted unread.
3. **Contract kept, expression replaced.** Interface names stay so missions, saved
   player settings and the spectator mod keep working; internals, structure, comments,
   wording, layouts, graphs and translations are new.

Logged exposures during the build room (both trivial, both already in the spec):
- Voice-range UI: one search printed two lines naming the
  `EarSettings().IsTransmittingOnAlternate()` call.
- Jamming/squelch: one search printed eight lines (comments plus mute/power checks)
  of the squelch file before it was deleted.

## What was replaced

| Area | Before | Now |
|---|---|---|
| Earplugs (system, HUD, settings tab) | ported | rewritten from `Spec_Earplugs.md` |
| Voice-range HUD, overlay, nametags, mission settings | ported | rewritten from `Spec_VoiceRange_UI.md` |
| Jamming, RF propagation, key-state relay, squelch | ported | rewritten from `Spec_Jamming_RF_Squelch.md` |
| Ear routing, beep helper, radial entry, frequency dialog, derived controller methods | ported | rewritten from `Spec_RadioLayer.md` |
| `von.acp` family, `.sig` graphs, beep `.acp`, routing conf | stock + third-party edits | stock + new graphs from `Spec_AudioGraphs.md` |
| All sound effects | third-party samples | synthesized by `Tools/generate_sounds.py` |
| Jammer models, textures, preview images | third-party art | stock Reforger meshes/previews (AN/GRC-160, RC-292) |
| Voice-range HUD icons, earplugs icon | third-party art | stock Reforger imagesets |
| Localization (13 languages, both tables) | ported | new wording and translations |
| Object IDs in override configs/prefabs | carried over | re-rolled (stock IDs kept where they are the override key) |
| Control-hint and keybind labels | carried over | reworded |

29th-original work (spectator voice, transmit tiers, coexistence guard, receiver guard,
alternate-PTT guard, editor manager voice) was out of scope and left as-is.

## Residual similarity (expected)

A line-overlap scan against the earlier reference material now only matches:
stock Bohemia graph content both sides inherit, the three-value voice-range enum,
component-registration lines in override prefabs, and input action names / key
bindings (functional contract). None of it is creative expression.

## Not covered by this branch

- Git history on this repo still contains the earlier implementation. It does not ship
  in the Workshop package; keep the repository private.
- Behavior is compile-verified only (PC + HEADLESS, Test1 chain). Field re-test list:
  see the PR description.
