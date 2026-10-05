# Clean-room spec: Earplugs subsystem

Audience: the engineer reimplementing earplugs without access to the current
code. This document describes behavior and contracts only. Where something is
marked **[contract]** it must be kept byte-for-byte, because other assets,
players' saved settings, or the other EC29 subsystems depend on it. Everything
else is free to change as long as the observable behavior matches.

Source of truth for this spec: branch `in-house-rework` at `fe32eb5`.

---

## 1. External contract

### 1.1 Script classes

| Name | Kind | Why it is a contract |
|---|---|---|
| `EC29_Earplugs_System` **[contract]** | A world system (engine `WorldSystem` family), client-only, unique per world | Registered by name in `Configs/Systems/ChimeraSystemsConfig.conf`; renaming it breaks registration |
| `EC29_Earplugs_Info` **[contract]** | A HUD info display (vanilla `SCR_InfoDisplay` family) | Registered by name inside `Prefabs/Characters/Core/Character_Base.et` |
| `EC29_EarplugSettings` **[contract]** | A game user-settings module (vanilla `ModuleGameSettings` family) | Class name is the settings-module key players' saved settings are filed under |
| `EC29_RadioSettings` **[contract, not earplugs but co-located]** | A game user-settings module | Lives in the same script file today; read by `Scripts/Game/VON/EC29_RadioBeepHelper.c`. If the file is replaced it must be carried over unchanged (see 3.3) |
| Modded `SCR_AudioSettingsSubMenu` | `modded` extension of the vanilla Audio settings tab | Hooks the vanilla tab-creation step to bind the EC29 controls |

The system must expose a static way for the HUD display to find the live
instance, and an event ("earplugs toggled", carrying the new plugged state as a
bool) that the HUD display subscribes to. The event mechanism and accessor name
are internal (only the two EC29 classes use them; no other caller exists - a
repo-wide search for the event name and system class outside these files finds
only the systems config).

### 1.2 System registration

- `Configs/Systems/ChimeraSystemsConfig.conf` (meta GUID `{86E953538A28A98D}`,
  inheriting vanilla `{45C53F06BA17238D}configs/Systems/SystemsConfig.conf`)
  adds one entry: the earplugs system, location **Client**, system points 0.
  This conf is shared with other EC29 systems in future; keep the entry.
- The system's own declared info must agree: non-abstract, unique, client
  location.

### 1.3 Input action

- Action name **[contract]**: `EC29_ToggleEarplugs`. Referenced by the input
  conf, the keybinding menu conf, and the system's listener.
- Defined in `Configs/System/chimeraInputCommon.conf` (meta GUID
  `{795184CF9AD764DB}`, a vanilla-GUID override) as a top-level action with a
  sum of two sources:
  - Keyboard **F2**, "toggle" filter preset, click filter.
  - Gamepad combo: **left shoulder** held + **D-pad down** (gamepad "toggle"
    preset, click filter on the D-pad).
- Added to the vanilla `GlobalContext` by appending to its action-refs list
  (append, not replace - vanilla refs must survive).
- The system listens for the action's **down** trigger.

### 1.4 Keybinding menu

`Configs/System/keyBindingMenu.conf` (meta GUID `{4EE7794C9A3F11EF}`) declares
an EC29 category (name `EC29`, display key `#EC29-KeybindCategory_29th`) whose
"General" section (separator key `#EC29-KeybindSeparator_General`) lists, in
order, the voice-range cycle entry and then the earplugs entry:
action `EC29_ToggleEarplugs`, display key `#EC29-Keybind_EC29_Earplugs`,
preset `toggle`. The category is shared with the radio/VoN subsystems; only the
earplugs entry is in scope here.

### 1.5 Persisted settings **[contract - players' saved settings]**

| Module class | Property | Type | Default | Range / step | Meaning |
|---|---|---|---|---|---|
| `EC29_EarplugSettings` | `EarplugsVolume` | int | 80 | 0..100, step 1 | Percent **reduction** applied to SFX while plugged (despite the property name, higher = quieter) |
| `EC29_RadioSettings` | `RadioBeepsEnabled` | bool | false (0) | checkbox | Radio TX/RX beeps; out of scope, carry over unchanged |

Both are stored in the game user settings (not the engine settings). The
property name, its type, and the module class name must not change or every
player's saved value is lost. No conf file in the repo registers these
modules; registration relies on the class deriving from the vanilla game
settings module base.

The system also **reads** (never writes) one vanilla engine setting: module
`AudioSettings`, property `VolumeSfx` (0..100), from the engine user settings.

### 1.6 Localization keys

From `Language/EC29_earplugs_localization.<lang>.conf` (13 languages ship; most
entries are still English, some languages, e.g. de_de, translate the slider
label):

| Key | en_us text | Used by |
|---|---|---|
| `EC29-Keybind_EC29_Earplugs` | Toggle Earplugs | keybinding menu entry |
| `EC29-Settings_EarplugsVolume` | Earplugs volume reduction | Audio-tab slider label |
| `EC29-Settings_Title_29th` | 29th ID | Audio-tab section header |
| `EC29-KeybindCategory_29th` | 29th ID | keybinding category |
| `EC29-KeybindSeparator_General` | General | keybinding separator |

The last three are duplicated in `EC29_von_localization.*` as well. The
Radio Beeps checkbox label key `EC29-Settings_RadioBeeps` lives only in the
VoN string table.

### 1.7 HUD info registration

`Prefabs/Characters/Core/Character_Base.et` (meta GUID `{37578B1666981FCE}`,
vanilla-GUID override) adds to the character's base HUD component an info
display of class `EC29_Earplugs_Info` with: layout
`{C3E88B05F8FF93CF}UI/layouts/HUD/Earplugs/EarplugsOverlay.layout`, visible at
parent, layer LOW, shown when created. (The same prefab override also carries
the VoN direct-speech tier components; leave those untouched.)

### 1.8 Widget names looked up by script **[contract]**

- HUD overlay layout: the display finds an image widget named **`Image0`**
  anywhere under its root. A rewrite may rename it only if script and layout
  change together.
- Audio settings layout: the settings bindings locate controls by widget name
  **`Earplugs`** (slider) and **`RadioBeeps`** (checkbox). These names are the
  third argument of the vanilla gameplay setting binding and must match the
  layout exactly.

---

## 2. Behavior

### 2.1 Model

Two volumes are tracked, both on the 0..1 scale of the engine's SFX master bus:

- **Unplugged (default) volume** = the player's vanilla SFX slider value
  (`AudioSettings.VolumeSfx` / 100). If engine settings are unavailable, fall
  back to whatever the SFX master bus currently reads.
- **Plugged volume** = unplugged volume x (1 - reduction / 100), where
  reduction is `EC29_EarplugSettings.EarplugsVolume`. With the default 80, the
  plugged level is 20 % of the player's normal SFX level. Reduction 0 = no
  change; 100 = silence.
- If the EC29 settings module cannot be found, the plugged volume is **0**
  (full mute) and a warning is logged.

The only audio control touched is the engine's **SFX master volume**
(`AudioSystem` SFX bus). Music, dialogue, VoIP, and master are never changed.
Voice comms therefore stay at full level while plugged - that is the point of
the feature.

A single in-memory flag records plugged / unplugged. It starts **unplugged**
for every new world and is **not persisted**.

### 2.2 Toggle

On the `EC29_ToggleEarplugs` down trigger:

1. If the coexistence guard reports that the conflicting earplugs mod is
   loaded (see 2.5), do nothing at all: no state change, no volume change, no
   event.
2. Otherwise flip the plugged flag, set the SFX master bus to the plugged or
   unplugged volume accordingly, and raise the "earplugs toggled" event with
   the new plugged state.

There is no hold mode; every press flips. No sound cue plays on toggle.

### 2.3 Settings changes while running

The system subscribes to the game's "user settings changed" notification.
Whenever it fires, both the unplugged and plugged volumes are recomputed from
the current settings. If currently plugged, the new plugged volume is applied
immediately (so moving the reduction slider, or the vanilla SFX slider, while
plugged takes effect live). If unplugged, nothing is applied by EC29 - vanilla
already applies its own SFX slider.

### 2.4 World lifecycle

- **System init** (each world load, client only): resolve the engine settings,
  game settings and input manager (warn on each that is missing; without the
  input manager the keybind cannot register); compute both volumes; subscribe
  to settings changes; register the action listener. Then **self-heal**: if
  the SFX master bus currently reads below the unplugged volume (for example a
  previous world ended while plugged), raise it back to the unplugged volume.
- **System stop**: unregister the action listener and the settings
  subscription, and apply the same self-heal (if SFX is below the unplugged
  volume, restore it). Leaving a world while plugged therefore never leaves the
  player's SFX quiet in menus or the next session.
- The self-heal only ever raises the bus; it never lowers a bus that is
  above the computed default.
- Dedicated servers do not get the system (client location).

### 2.5 Coexistence guard interaction

`EC29_CoexistenceGuard` (separate file, not part of this rewrite) exposes a
yes/no query "should earplugs yield". It answers yes when the loaded addon
list contains the workshop GUID of the known conflicting earplugs mod (the
mod this code was originally ported from; it also binds F2 and also scales
SFX, so both firing would cancel out). The guard caches its answer once per
process and owns its own warning log and the player-facing conflict notice.

Earplugs consults the guard **at keypress time only**. When yielding, init,
settings-change handling, and the stop-time self-heal still run; only the
toggle is suppressed. The HUD icon therefore never shows while yielding.

### 2.6 HUD indicator

- Hidden by default (the overlay image starts fully transparent).
- When drawing starts for the character HUD: if the system instance is missing
  or the layout root failed to load, warn and do nothing further. Otherwise
  subscribe to the "earplugs toggled" event and locate the indicator image
  (warn if absent).
- On each toggle event: indicator fully visible (opacity 1) when plugged,
  fully hidden (opacity 0) when unplugged.
- When drawing stops: unsubscribe from the system's events.
- Known gap in the current implementation (do not treat as a requirement): the
  indicator is not synced to the current state when the HUD is (re)created, so
  respawning while plugged leaves SFX reduced with the icon hidden until the
  next toggle. The rewrite **should** initialize the icon from the current
  plugged state on draw start; flag it if you choose not to.

---

## 3. Settings menu additions (Audio tab)

### 3.1 Layout override

`UI/Layouts/Menus/SettingsMenu/AudioSettings.layout` (meta GUID
`{16228186C675CDD9}`) is a same-GUID override of the vanilla Audio settings
layout. Comparing it to the vanilla file read from the game data:

Vanilla content of the scroll area, in order: "Volume" header; Master, Music,
SFX, Dialogue, VOIP sliders; "General" header; stereo processing mode
spinbox; dynamic range slider; tinnitus spinbox; DualSense speaker spinbox;
"Game related" header; HQ announcer spinbox. Plus a separate description pane.

**Added by EC29** (three new children of the scroll area's content list):

1. A section header named `Title29th`, using the vanilla settings-title
   sub-layout, label key `#EC29-Settings_Title_29th` ("29th ID").
2. A slider named `Earplugs` **[contract]**, using the vanilla widget-library
   slider, label key `#EC29-Settings_EarplugsVolume`, vanilla settings label
   sub-layout, value shown as a whole-number percent (format "N%", rounded),
   4 px padding on all sides (same as vanilla sliders). Range 0..100, step 1,
   default 80 - these come from the settings-module attribute, not the layout.
3. A checkbox named `RadioBeeps` **[contract]**, using the vanilla widget-
   library checkbox, label key `#EC29-Settings_RadioBeeps`, vanilla settings
   label sub-layout, 4 px padding. (Radio subsystem; keep it.)

**Modified vanilla widget**: the vanilla "General" header (`TitleGeneral`) gets
its top padding raised from 4 to 30 (both on its label component and on its
inner text slot) so it separates visually from the EC29 block above it.

Intended placement: the EC29 block sits after the vanilla volume sliders
(after VOIP) and immediately before the "General" header. See Ambiguities.

Nothing else in the vanilla layout is changed; the override lists vanilla
widgets only by prefab reference.

### 3.2 Script side

When the vanilla Audio tab is created, after vanilla setup:

- If the tab's scroll widget is missing, log a warning and add nothing.
- Bind the `Earplugs` widget to `EC29_EarplugSettings.EarplugsVolume`, and
  the `RadioBeeps` widget to `EC29_RadioSettings.RadioBeepsEnabled`, both as
  vanilla gameplay setting bindings. Add both to the tab's binding list and
  route their change notifications into the tab's normal "item changed"
  handling so the vanilla apply/save path persists them.
- Then, unconditionally (not debug-gated), warn once per tab open for each of
  the two modules that cannot be found in the game user settings, stating that
  the corresponding control is inert.

### 3.3 Shared-file note

The radio-beeps module and its binding are not earplugs features, but today
they live in the earplugs settings file. A rewrite that replaces that file must
keep `EC29_RadioSettings.RadioBeepsEnabled` (bool, default off, checkbox) and
its binding, or move them to the radio subsystem in the same change.

---

## 4. HUD overlay

- What it depicts: an "earplugs in" status icon - a pictogram meaning
  "hearing protection is active / ears muffled". The rewrite will use a vanilla
  imageset icon; pick one that reads as ear/hearing-protection or muted
  ambient sound, **not** a muted-microphone glyph (players would read that as
  "my voice is muted", which is false - only SFX is reduced).
- Where: a 50 x 50 px square anchored to the **vertical middle of the right
  screen edge**, inset so its right edge sits about 25 px from the screen edge.
- Rendering: the image fills the square, additive blend, starts at opacity 0.
  Visibility is controlled solely by opacity (0 or 1); the widget is never
  removed or re-laid out.
- HUD layer LOW, shown on the character HUD (so it is present only while the
  local player controls a character; not in spectator/menus).
- Asset `{1A95414B2C82DB8C}UI/Textures/HUD/Earplugs/Earplugs_UI.edds` is
  referenced only by this layout; once the layout uses an imageset icon, the
  texture has no other references.

---

## 5. Logging and invariants

### 5.1 Logging

All EC29 diagnostic output is gated by the project-wide verbose switch
(`EC29_Debug.VERBOSE`, currently on for field testing), except warnings that
indicate a broken install, which are always printed. Tags in use:
`[EC29-DBG][Earplugs]` (system), `[EC29-DBG][EarplugsInfo]` (HUD),
`[EC29-DBG][EarplugsMenu]` (settings tab trace), `[EC29]` (ungated audio-tab
warnings).

Always printed (warning level):
- Engine settings, game settings, or input manager unavailable at system init.
- Earplug settings module not found when computing the plugged volume (plugged
  will be full mute).
- HUD: system instance missing; overlay layout root missing; indicator image
  not found.
- Audio tab: scroll widget missing; either EC29 settings module missing.

Verbose only (normal level):
- System init entered; resolved default and plugged volumes.
- Settings module found, with the raw reduction percent.
- Action listener registered.
- Keypress received (logged before the coexistence check, so it also appears
  when yielding).
- Toggle result: new state, target volume, and a readback of the SFX bus.
- HUD draw start; each indicator visibility change.
- Audio tab: inserting the earplug slider and the radio-beeps checkbox.

The rewrite may reword messages but should keep the tag prefixes (log filters
and the field-test checklist search on them) and keep the always-on / verbose
split.

### 5.2 Invariants

1. Only the SFX master bus is ever written. Never touch VoIP, dialogue, music,
   or master.
2. Plugged volume is always a fraction of the player's own SFX setting, never
   an absolute level, and never above it.
3. The player's vanilla `VolumeSfx` setting is never written; earplugs are a
   runtime override of the bus, not a settings change.
4. Leaving a world (system stop) or entering one (system init) never leaves the
   SFX bus below the player's configured level.
5. Plugged state is per-world, in memory, starts unplugged.
6. When the coexistence guard says yield, a keypress has no side effects.
7. The HUD icon is visible if and only if the last toggle event said plugged.
8. Persisted names (`EC29_EarplugSettings`, `EarplugsVolume`,
   `EC29_RadioSettings`, `RadioBeepsEnabled`), the action name, the widget
   names `Earplugs` / `RadioBeeps` / `Image0`, the class names registered in
   confs/prefabs, and the localization keys are frozen.
9. Client-only. Nothing here replicates or runs on a dedicated server.

---

## Ambiguities / open questions

- **Placement of added Audio-tab widgets.** The override lists the three new
  widgets before the reference to the vanilla "General" header, and bumps that
  header's top padding, which implies they render between VOIP and "General".
  Whether the engine's layout-inheritance merge actually inserts new children
  at that position (versus appending at the end of the list) was not verified
  in game.
- **Vanilla GUID of AudioSettings.layout.** The vanilla file was read from the
  game pak, but the asset index did not report its GUID; the "same-GUID
  override" claim rests on the EC29 meta file carrying `{16228186C675CDD9}`.
- **Settings-module registration.** No conf in the repo registers the two
  settings modules; it is assumed the engine discovers `ModuleGameSettings`
  subclasses by class. The ungated "module missing" warnings exist because
  this has failed before or was feared to.
- **Event ordering on settings change.** If the player moves the vanilla SFX
  slider while plugged, the final bus level depends on vanilla applying its
  value before EC29's settings-changed handler re-applies the plugged level.
  Not verified.
- **Respawn icon desync** (2.6) is a current defect, not a spec requirement.
- **"Default SFX" refresh on keypress.** The current system refreshes its
  engine-settings handle on keypress but does not recompute volumes there;
  recompute happens only on init and settings-changed. Behaviorally equivalent
  unless settings change without the notification firing.
