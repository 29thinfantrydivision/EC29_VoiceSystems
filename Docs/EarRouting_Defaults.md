# Default Ear Routing (Issue #7)

Squad net defaults to the **left ear**, platoon net to the **right ear**. Most of the unit
already runs this split by hand; new members now get it without setup.

## Behavior

| Device | Gadget type | Net | Default routing |
|---|---|---|---|
| Personal handheld (ANPRC68, R-148) | `EGadgetType.RADIO` | Squad | LEFT |
| Manpack (ANPRC77, R-107M) | `EGadgetType.RADIO_BACKPACK` | Platoon | RIGHT |
| Vehicle sets, editor transceivers | (no radio gadget) | — | CENTER (both ears) |

- The default applies **per transceiver** the first time anything asks for its routing
  (incoming voice packet, TX/RX beep, or the radial-menu label).
- Cycling routing in the radial menu (hover a radio, press the routing key) still works
  exactly as before and **overrides the default** for that radio. The cycle order is
  unchanged: CENTER → LEFT → RIGHT → CENTER.
- Routing choices are session-scoped (not persisted); a fresh session starts back at the
  squad-left / platoon-right defaults.

## Implementation

`EC29_RadioEarSettings.GetRouting()` (`Scripts/Game/VON/EC29_EarRouting.c`) resolved to
CENTER whenever a transceiver had no player-set entry. It now calls
`ApplyDefaultRouting()`, which classifies the owning device:

```
transceiver.GetRadio()                 -> BaseRadioComponent
  .GetOwner()                          -> radio entity
  .FindComponent(SCR_RadioComponent)   -> gadget component
  .GetType()                           -> EGadgetType.RADIO / RADIO_BACKPACK
```

This is the same classification vanilla uses for `SCR_VONEntryRadio.IsLongRange()`
(`m_GadgetComp.GetType() == EGadgetType.RADIO_BACKPACK`); vanilla's
`SCR_RadioComponent.GetType()` maps its `ERadioCategory` attribute (PERSONAL / MANPACK)
to those two gadget types, so the split holds for any radio prefab that sets its
category correctly, not just the four base-game models.

The result is memoized into the existing routing map, so the per-packet hot path
(`EC29_ApplyRadioAudioVars` → `EC29_GetEarRoutingForTransceiver`) stays a single map
lookup — same cost as before the change. If the radio entity cannot be resolved yet
(streaming edge case), the query returns CENTER **without** memoizing and classification
retries on the next call, so a wrong default is never frozen in.

No audio-graph changes: the routing enum value (CENTER=0, RIGHT=1, LEFT=2) feeds the
`EC29_EarRouting` audio variable exactly as before; only the unset-entry default moved.

## Debug logging

`EC29_Debug.VERBOSE` is **ON** in this build (integration weekend — every RPT captures
the full `[EC29-DBG]` trace chain). Relevant lines:

- `[EC29-DBG][RadioEar] Default routing L applied to transceiver at 42000 kHz (gadget=1)`
  — one line per transceiver, on first classification.
- Existing routing-cycle, beep, squelch, jammer, and packet traces all remain active.

Flip `Scripts/Game/EC29_Debug.c` back to `VERBOSE = false` for the wide release build.
