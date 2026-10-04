# Clean-room spec: voice-range settings + HUD/UI

Scope: the VoN transmission overlay customisations, the HUD voice-range icon, the over-head
nametag speaking gate, the mission-header policy block, the replicated settings component, the
voice-range enum, and the local audio-variable config. This document describes behaviour and
contracts only. Where a name appears here it is part of an external interface (a class other
files or prefabs reference, a mission-header property existing scenarios already carry, a
widget the layout must contain, a vanilla member the engine owns, or a log prefix field-report
tooling greps for).

Related design context: `Docs/DirectSpeechTiers.md` (direct speech is transmitted through one of
three per-mode components whose ACP carries the range; nothing on the listener computes volume).

---

## 1. External contract

### 1.1 Files the rewrite replaces (same paths, so GUIDs and prefab references keep resolving)

| Path | GUID (from `.meta`) | Referenced by |
|---|---|---|
| `Scripts/Game/UI/HUD/EC29_VON_VonDisplay.c` | n/a | (modded class, no references) |
| `Scripts/Game/VON/EC29_VON_VoiceRangeDisplay.c` | n/a | `Prefabs/Characters/Core/DefaultPlayerController.et` (by class name) |
| `Scripts/Game/UI/Nametags/EC29_VON_NameTagData.c` | n/a | (modded class) |
| `Scripts/Game/Mission/EC29_VON_MissionHeader.c` | n/a | every mission header that carries the EC29 block |
| `Scripts/Game/GameMode/EC29_VON_SettingsComponent.c` | n/a | `Prefabs/MP/Modes/GameMode_Base.et` (by class name) |
| `Scripts/Game/VON/EC29_VON_EVoiceRange.c` | n/a | VON controller, VoN component, voice tiers |
| `Sounds/VON/EC29_LocalVariables_VON.conf` | `{33A27275C95E0302}` | `von.acp`, `EC29_VonWhisper/Normal/Yell.acp` (3 refs each), `EC29_VON_VoNComponent.c`, `EC29_SpectatorVonService.c` |
| `UI/Layouts/HUD/VON/EC29_VoiceRangeDisplay.layout` | `{46BDC60D3C6EC544}` | `DefaultPlayerController.et` (`m_LayoutPath`) |
| `UI/Imagesets/HUD/VON/VON_Icons.imageset` | `{8C4856BE7FC357C0}` | default value of the HUD display's imageset attribute only |

The imageset is referenced nowhere except as the HUD display's attribute default (no prefab
overrides it). Its two atlases (`VON_Icons-100_atlas.edds` `{6D33087BEE3E4827}`,
`VON_Icons-200_atlas.edds` `{957EBFEB1A10AB62}`) are referenced only by the imageset. Replacing
the icons with vanilla imagesets therefore only requires changing the attribute defaults
(imageset + three sprite names); the custom imageset and atlases can then be deleted.

### 1.2 Enum `EC29_EVoiceRange`

Three members, in this order, with implicit ordinals: `WHISPER` = 0, `NORMAL` = 1, `YELL` = 2.
The ordering is load-bearing:
- the value is replicated and sent over an RPC; the server rejects anything outside
  `WHISPER..YELL` by ordinal comparison;
- the cycle order on the keybind is WHISPER -> NORMAL -> YELL -> WHISPER;
- the spawn default (stock VoN component's field initializer) is `WHISPER` (issue #11).

Users outside this subsystem: `EC29_VON_VONController.c` (cycle, `EC29_ApplyVoiceTier`),
`EC29_VON_VoNComponent.c` (replicated field, getter `EC29_GetVoiceRange()`, request
`EC29_RequestSetVoiceRange`), `EC29_VoiceTiers.c` (`OuterRange`, `FindTier`).

### 1.3 Mission header

- `modded class SCR_MissionHeader` adds one member, `m_EC29_VON_Settings`, a strong ref to an
  `EC29_VON_Settings` object. Attribute: category `"EC29_VON"`, description states that the
  direct-speech ranges are fixed per tier in the ACPs (whisper 2/6 m, normal 15/20 m,
  yell 50/80 m) and are not mission-tunable.
- `EC29_VON_Settings` is a `[BaseContainerProps()]` class deriving `ScriptAndConfig`. Its seven
  public bool properties are the mission-facing ABI; **names must not change**:

| Property | Attribute default | Meaning |
|---|---|---|
| `m_bAlwaysShowEnemyNames` | true | Show the real sender name for incoming overlay entries (vanilla replaces enemy names with "unknown source"). |
| `m_bEnableVonFactionNameColoring` | true | Tint incoming sender names in the overlay with the sender's faction colour. |
| `m_bHideFriendlyDirectIncoming` | false (no explicit defvalue) | Suppress overlay entries for incoming DIRECT speech from friendly (non-enemy) speakers. |
| `m_bHideEnemyDirectIncoming` | true | Suppress overlay entries for incoming DIRECT speech from enemy speakers. |
| `m_bHideRoleInVonOverlay` | true | Remove the "(Role)" bracket text next to incoming sender names. |
| `m_bShowVoiceModeInOverlay` | true | Show WHISPER / YELLING in the overlay channel slot for direct speech (NORMAL shows nothing). |
| `m_bGateNameTagVonByRange` | true | Suppress the over-head nametag speaking icon for direct speech when the speaker is out of audible range. |

All seven are CheckBox attributes in category `"EC29_VON"`. The class constructor also assigns
the same seven defaults (so script-constructed instances match editor-created ones).
Formerly-present range/volume properties (`m_fWhisperRange`, `m_fYellVolume`, `m_fMinVolume`
and similar) were removed; headers still carrying them load with an unknown-property warning
and otherwise work. Do not reintroduce them.

### 1.4 Settings component

- `EC29_VONSettingsComponentClass : SCR_BaseGameModeComponentClass` (empty), with
  `ComponentEditorProps` category `"GameScripted/GameMode/Components"`, description
  "Replicates EC29_VON overlay and nametag policy from the mission header to all clients."
- `EC29_VONSettingsComponent : SCR_BaseGameModeComponent`. Attached to the game mode by the
  `Prefabs/MP/Modes/GameMode_Base.et` override (component instance GUID `{1B22E0DABA7D1419}`,
  no property overrides). No `[Attribute]`s.
- Public API (all called from outside this file):
  - `static EC29_VONSettingsComponent GetInstance()` - the live instance or null.
  - `bool GetAlwaysShowEnemyNames()`, `bool GetEnableVonFactionNameColoring()`,
    `bool GetHideFriendlyDirectIncoming()`, `bool GetHideEnemyDirectIncoming()`,
    `bool GetHideRoleInVonOverlay()`, `bool GetGateNameTagVonByRange()`,
    `bool GetShowVoiceModeInOverlay()` - one getter per policy flag above.
  - `bool IsAudibleForListener(int senderPlayerId, IEntity listener)` - the visual range gate
    (section 2.4). Callers: the overlay and the nametag.
- Seven replicated bools mirroring the header flags, with the same defaults as the header table.

### 1.5 VoN overlay (`modded class SCR_VonDisplay`)

- New public method: `void EC29_ForceRefreshAllTransmissions()`. Callers:
  `SCR_VONController` (after the local voice-mode cycle) and the stock `SCR_VoNComponent`'s
  replicated-mode callback (on every client when any player's mode changes).
- Overrides: `OnReceive(int playerId, bool isSenderEditor, BaseTransceiver receiver, int frequency, float quality)`
  and `UpdateTransmission(TransmissionData data, BaseTransceiver radioTransceiver, int frequency, bool IsReceiving)`.
- Vanilla members it reads or writes (engine/vanilla-owned names): `m_wRoot`,
  `m_bIsVONUIDisabled`, `m_bIsVONDirectDisabled`, `m_bShowEnemyNames`, `m_OutTransmission`,
  `m_aTransmissionMap`; on `TransmissionData`: `m_bForceUpdate`, `m_bIsAdditional`,
  `m_iPlayerID`, `m_Entity`, `m_Faction`, `HideTransmission()`, and widgets `m_wName`,
  `m_wRole`, `m_wFrequency`, `m_wChannelText`, `m_wChannelFrame`.
- Localised strings used: `#EC29-VON_Mode_Whisper` ("WHISPER"), `#EC29-VON_Mode_Yelling`
  ("YELLING"). `#EC29-VON_Mode_Normal` exists in the string tables but the overlay does not use
  it (NORMAL is intentionally blank).

### 1.6 Nametag (`modded class SCR_NameTagData`)

- Override: `OnReceivedVON(int playerId, BaseTransceiver receiver, int frequency, float quality)`.
  No new public API.

### 1.7 HUD voice-range icon

- `class EC29_VoiceRangeDisplay : SCR_InfoDisplay`.
- Registration: `Prefabs/Characters/Core/DefaultPlayerController.et` overrides the player
  controller's `SCR_HUDManagerComponent` and adds an `EC29_VoiceRangeDisplay` entry
  (instance GUID `{091E54BE96EE740D}`) to `InfoDisplays`, with
  `m_LayoutPath` = `{46BDC60D3C6EC544}` (the layout above) and `m_eLayer ALWAYS_TOP`. Living on
  the controller (not the character) is what keeps it alive inside vehicles.
- Overrides: `OnStartDraw(IEntity owner)`, `UpdateValues(IEntity owner, float timeSlice)`.
- `[Attribute]`s (names and defaults; prefab overrides could target them, none do today):

| Attribute | Type | Default | Notes |
|---|---|---|---|
| `m_sImageSet` | ResourceName (imageset picker) | `{8C4856BE7FC357C0}UI/Imagesets/HUD/VON/VON_Icons.imageset` | Will change to a vanilla imageset in the rewrite. |
| `m_sSpriteWhisper` | string | `EC29_VON_Whisper` | Sprite name for WHISPER. |
| `m_sSpriteNormal` | string | `EC29_VON_Normal` | Sprite name for NORMAL (and any unknown value). |
| `m_sSpriteYell` | string | `EC29_VON_Yelling` | Sprite name for YELL. |
| `m_iIconSize` | int | 48 | Square icon edge, pixels. |
| `m_iMarginLeft` | int | 18 | Pixels from the screen's left edge. |
| `m_iMarginBottom` | int | 360 | Pixels from the screen's bottom edge to the icon's top edge; chosen to sit just above the vanilla VoN transmission slot. |
| `m_iVisibleDurationMs` | int, range 0..inf step 100 | 3000 | Time the icon stays up after a mode change. 0 = permanently visible. |
| `m_fFadeInRate` | float, range 0.5..50 step 0.5 | 10 | Opacity animation rate on show (full fade ~ 1/rate s); equals vanilla's fast fade rate. |
| `m_fFadeOutRate` | float, range 0.5..50 step 0.5 | 1 | Opacity animation rate on hide; equals vanilla's slow fade rate. |

- Layout widget contract: the layout's root is a FrameWidget named `EC29_VoiceRangeRoot`
  containing an ImageWidget named `VoiceRangeIcon` (colour white, full alpha). Script looks up
  only `VoiceRangeIcon` (searched anywhere under the root). The root name is not looked up.

### 1.8 Audio local variables (`EC29_LocalVariables_VON.conf`)

Two external-source audio variables; names are referenced by ACPs and script:

| Variable | Default | Max | Status |
|---|---|---|---|
| `EC29_VonRange` | 1 | 3 | RETIRED as a range control. Still wired into the ACPs (it feeds the "bus ducking when many voices" input curve, which spans 0..3), so it must keep existing. Script (`EC29_VON_VoNComponent.c`) pins it to 1.0 once per world. |
| `EC29_SpectatorListening` | 0 | 1 | 0 = normal listener, 1 = local client is spectating. Selects the spectator hearing path in every VoN ACP. Written by `EC29_SpectatorVonService`. |

Minimum for both is the engine default (0). Nothing in this subsystem's script writes either
variable; the conf is part of this subsystem only as a shared resource.

### 1.9 Dependencies this subsystem consumes (outside scope; contract only)

- `SCR_VoNComponent.EC29_GetVoNForPlayer(int playerId)` - the player's STOCK VoN component on
  their CURRENT controlled entity (never a corpse, never a transmit tier), or null.
- `SCR_VoNComponent.EC29_IsSpectatingListener()` - true while the local player spectates.
- `SCR_VoNComponent::EC29_GetVoiceRange()` - the replicated mode.
- `EC29_VoiceTiers.StockVoN(IEntity)`, `EC29_VoiceTiers.OuterRange(EC29_EVoiceRange)`, and the
  constants `WHISPER_OUTER_M` (6), `NORMAL_OUTER_M` (20), `YELL_OUTER_M` (80).
- `EC29_CoexistenceGuard.ShouldYieldRadio()` - true when a known conflicting radio mod is loaded.
- `EC29_RadioState.GetInstance().EarSettings().IsTransmittingOnAlternate()` - true while the
  alternate radio channel is keyed.
- `EC29_Debug.VERBOSE` - compile-time switch for `[EC29-DBG]` trace lines.

---

## 2. Behaviour

### 2.1 Settings component lifecycle

- On post-init (every machine) the component registers itself as the singleton instance. On
  delete it clears the singleton only if it is still the registered one.
- Server only: it reads the current mission header. If the header is not an
  `SCR_MissionHeader` or carries no `m_EC29_VON_Settings` block, it logs a warning and keeps its
  built-in defaults (identical to the header defaults). Otherwise it copies all seven flags from
  the header block into its replicated fields and marks itself dirty for replication.
- Clients never read the header; they receive the seven flags by replication (late joiners get
  current values via initial state). No change callback is needed: consumers read the getters
  at the moment they need them.
- Values are set once at post-init; there is no runtime mutation path.
- Every consumer treats "no instance" as "apply no customisation" (overlay shows vanilla
  behaviour, nametag ungated, range gate not applied).

### 2.2 Visual range gate (`IsAudibleForListener`)

Answers "is this speaker inside the outer (silent) range of the tier they are transmitting on,
measured from the listener?" Used only by visuals; audio never consults it.
- Returns **true (show)** when: the listener is null; the speaker's stock VoN component cannot be
  resolved; the speaker has no controlled entity. Rationale: a packet did arrive, so hiding it
  would make a real talker invisible.
- Otherwise takes the speaker's replicated mode, looks up that tier's outer range
  (whisper 6 m, normal 20 m, yell 80 m; unknown values use normal), and returns true when the
  straight-line 3D distance between the speaker entity's origin and the listener entity's
  origin is less than or equal to that range (inclusive boundary).
- Accepted limitation: the replicated mode can lag the speaker's actual transmit tier by one
  replication round trip after a mode change; there is no listener-side signal for which tier a
  packet came from.

### 2.3 VoN overlay

**Incoming entry admission (per received packet, before vanilla processing):**
1. If the overlay root is missing or vanilla has the VoN UI disabled, do nothing at all (not even
   vanilla processing).
2. Spectator enemy-name handling: the first time a packet is received, the mission's own value of
   vanilla's "show enemy names" flag is captured. From then on, on every packet the effective
   flag is set to "captured mission value OR local player is currently spectating". So a
   spectator always sees enemy direct entries (vanilla would otherwise drop them before any name
   rewrite can run), and the mission's value is restored automatically the moment spectating
   ends. Living players always see exactly what the mission configured.
3. Filtering applies only to DIRECT speech (no receiving transceiver) where the sender is not an
   editor/GM voice, the local game-master editor is not open, and the local player is not
   spectating. Radio entries, GM voices, open-editor and spectator listeners are never filtered.
4. A direct entry is suppressed when EITHER:
   - **Faction filter** (only if at least one of the two hide flags is on): if vanilla's
     "direct VoN UI disabled" flag is set, suppress unconditionally. Otherwise resolve the local
     player's faction and the sender's faction through the faction manager (by player id); if
     the faction manager, local controller, a non-zero local player id, or either faction is
     missing, do not suppress. If the sender's faction is hostile to the local faction, suppress
     iff `HideEnemyDirectIncoming`; otherwise (friendly or neutral) suppress iff
     `HideFriendlyDirectIncoming`.
   - **Range filter**: the speaker is not audible per section 2.2, measured from the local
     player's currently controlled entity. This filter is NOT controlled by any mission flag;
     it applies whenever the settings instance exists and the local player controls an entity.
     (The nametag flag does not switch it off.)
5. On suppression: if an entry for that speaker already exists, hide it; then stop (vanilla does
   not process the packet).
6. Otherwise vanilla processes the packet. Afterwards, if the speaker's entry carries the
   force-update flag, re-run the per-entry update for it as an incoming entry; if that update
   rejects the entry, hide it. Reason: vanilla's incoming path only re-updates on device /
   frequency / active changes and ignores the force flag (only the outgoing path honours it), so
   without this a mid-sentence mode change would never refresh the label.

**Per-entry update additions (run after vanilla's update, only if vanilla accepted the entry):**
- **Outgoing radio frequency colour:** for an OUTGOING entry on a radio, unless the coexistence
  guard says to yield the radio feature, colour the frequency text CYAN while the alternate
  channel is being transmitted on and WHITE otherwise (the white reset un-cyans it after the
  alternate push-to-talk is released). EC29 colours this widget nowhere else, so this logic owns
  it. This runs before the "additional entry" check below, so it applies to any outgoing radio
  entry.
- Stop here for "additional" entries (vanilla's secondary entries) or when no settings instance
  exists.
- **Incoming entries only** (other people's audio, radio or direct):
  - `HideRoleInVonOverlay`: clear the role text and hide the role widget.
  - `AlwaysShowEnemyNames`: rewrite the sender name for every incoming entry (friendly included)
    and make the name widget visible. The entry's entity is refreshed to the sender's current
    controlled entity. If the sender is currently possessing an entity (game-master
    possession), show that character's identity name: the formatted full name (with its first
    three format parameters when available) from the scripted character identity, else the
    plain identity name, else the player display name. If not possessing, show the player
    display name from vanilla's filtered player-name cache (so platform name filtering is
    honoured).
  - `EnableVonFactionNameColoring`: if the entry has a resolvable scripted faction, tint the
    name text with that faction's colour (applies to friendly and enemy alike).
- **Voice-mode label** (`ShowVoiceModeInOverlay`, DIRECT entries only, BOTH directions):
  - Speaker resolution: incoming -> the sender's stock VoN by player id; outgoing -> the stock
    VoN on the local player's controlled entity (the stock component carries the mode; the
    transmitting component is a tier).
  - WHISPER -> channel slot text `#EC29-VON_Mode_Whisper`, channel frame visible.
    YELL -> `#EC29-VON_Mode_Yelling`, frame visible.
    NORMAL, or speaker unresolvable -> channel frame hidden (overlay stays quiet in the common
    case; this mirrors how vanilla shows e.g. PLATOON for a radio broadcast).
  - Skip silently if the entry lacks the channel text or channel frame widget.

**Force refresh (`EC29_ForceRefreshAllTransmissions`):** sets the force-update flag on the local
outgoing entry (if any) and on every entry in the incoming map, so labels re-evaluate on the next
update. Called (a) locally right after the player cycles mode, because the authority does not
always fire its own replication callback for its own writes, and (b) on every client when any
player's replicated mode changes.

### 2.4 Nametag speaking icon

Vanilla activates the speaking (VON) state on a speaker's over-head nametag whenever the local
player receives any transmission from them, which would reveal a whisperer 30 m away.
- Radio packets (receiving transceiver present) are never gated; radio reach is the radio's
  business.
- Direct packets: if a settings instance exists, `GateNameTagVonByRange` is on, the local
  player controls an entity, and the speaker is NOT audible per section 2.2, the packet is
  swallowed (vanilla never sees it, so the icon does not activate). In every other case vanilla
  runs unchanged.
- Note: unlike the overlay, the nametag gate does NOT exempt spectators or the open editor;
  only the flag and the presence of a controlled entity govern it.

### 2.5 HUD voice-range icon

- **Setup (on start draw):** after vanilla creates the layout, find `VoiceRangeIcon`. If the
  root or the icon is missing, log a warning and stay inert for the session. Otherwise position
  the icon absolutely: anchored at the parent's bottom-left corner, square of `m_iIconSize`
  pixels, offset `m_iMarginLeft` px to the right and `m_iMarginBottom` px upward (the offset is
  to the icon's top edge, so the icon hangs downward from that point). Load the WHISPER sprite and
  record WHISPER as the last-shown mode (must equal the spawn default, or the icon lies until the
  first cycle). If `m_iVisibleDurationMs` > 0, start the whole display (root) at opacity 0;
  if it is 0, leave it fully visible.
- **Per frame:** resolve the local player's stock VoN component by the local player id (the
  display is on the controller, the component is on the controlled character; uses the shared
  cached lookup). If there is no controller or no component (dead, spectating, no character),
  do nothing that frame (including not advancing the hide timer).
- When the mode differs from the last-shown mode: load the sprite for the new mode (WHISPER ->
  whisper sprite, YELL -> yell sprite, anything else -> normal sprite), remember it, and if
  auto-hide is enabled, animate the root's opacity to 1 at the fade-in rate, reset the visible
  timer, and arm the hide.
- While armed (auto-hide only), accumulate frame time; once it reaches `m_iVisibleDurationMs`,
  animate the root's opacity to 0 at the fade-out rate and disarm. Each further change restarts
  the cycle.
- Net effect with defaults: invisible at spawn; pops up for 3 s (fast fade in, slow ~1 s fade
  out) each time the local player's mode changes, including changes caused by respawn with a
  different mode than last shown. With duration 0: always visible, sprite swaps on change.
- The icon always reflects the REPLICATED mode, so it can trail the key press by one round trip.

---

## 3. HUD icon set

Current atlas: three white monochrome silhouettes on transparency, each 64x64 in a 72x204
(1x) / 136x396 (2x) atlas, drawn untinted (widget colour white). All three share one motif: a
soldier's head in a combat helmet, seen in profile facing left, with a gesture at the mouth.

| Sprite | Mode | Depicts |
|---|---|---|
| `EC29_VON_Whisper` | WHISPER (spawn default) | Helmeted head with a single raised index finger in front of the lips: the "shh / quiet" gesture. Meaning: speaking quietly, short range (~6 m). |
| `EC29_VON_Normal` | NORMAL | Helmeted head with a small speech bubble in front of the mouth. Meaning: ordinary talking voice (~20 m). |
| `EC29_VON_Yelling` | YELL | Helmeted head with an open hand raised beside the mouth, cupped as if shouting. Meaning: raised voice, long range (~80 m). |

There is no "off", "muted" or "transmitting" state; the icon only conveys which of the three
modes is selected. Replacement guidance: pick three vanilla Arma Reforger imageset sprites that
read as quiet / normal / loud speech (for example a low / medium / high volume-level glyph or a
speaker with 0-1 / 2 / 3 sound waves), keep them single-colour so the white widget colour
reads on any background, and point the four imageset/sprite attribute defaults at them.

Placement: one square icon, 48 px, at the screen's lower-left, 18 px in from the left edge and
with its top edge 360 px above the bottom edge, which lands just above the vanilla VoN
transmission list. Drawn on the always-on-top HUD layer.

---

## 4. Logging

Two families: `[EC29-DBG]...` trace lines only when `EC29_Debug.VERBOSE` is true; ungated lines
indicate a broken install or a lifecycle milestone and must always print.

| Prefix / gist | Level | Gate | When |
|---|---|---|---|
| `[EC29-DBG][VonOverlay]` refreshing transmission labels after voice-mode change | normal | VERBOSE | Each force refresh. |
| `[EC29_VON] EC29_VoiceRangeDisplay:` root null after start draw - layout failed to load | warning | always | HUD setup, layout missing. |
| `[EC29_VON] EC29_VoiceRangeDisplay:` VoiceRangeIcon widget not found in layout | warning | always | HUD setup, widget missing. |
| `[EC29-DBG][VoiceHUD]` voice-range display active (controller override applied), with the imageset path | normal | VERBOSE | HUD setup succeeded. |
| `[EC29-DBG][VoiceHUD]` mode changed -> icon '<sprite name>' | normal | VERBOSE | Each mode change seen by the HUD. |
| `[EC29-DBG][NameTag]` VON icon suppressed for player <id> (out of audible range) | normal | VERBOSE, and throttled | Only on the shown->suppressed transition per speaker (state remembered per player id; any un-suppressed packet resets it). This fires per voice packet otherwise; unthrottled it produced ~2,273 lines/minute in the field. |
| `[EC29-DBG][VONSettings]` component alive on game mode, isServer flag, plus the three outer ranges (whisper/normal/yell) | normal | VERBOSE | Post-init, every machine. |
| `[EC29] Voice systems initialized (server)` | normal | always, server only | Post-init on server; server-side counterpart of the client's spawn-in notice. |
| `[EC29_VON] Mission header has no EC29_VON_Settings; using component defaults.` | warning | always, server only | Header missing the block. |

---

## 5. Invariants and fixes that must survive

1. Spectator enemy names: TRACK, never latch. Capture the mission's "show enemy names" value once,
   before ever modifying it; effective value = mission value OR spectating, re-evaluated per
   packet. The spectator feature must never change what living players see.
2. Spectators, open GM editor, and GM/editor voices are exempt from the overlay's direct
   faction/range filters. Radio entries are exempt from every range gate (overlay and nametag).
3. Incoming entries must honour the force-update flag (vanilla's incoming path does not), and an
   entry the re-update rejects must be hidden, never left half-updated on screen.
4. The overlay's suppression must hide an already-visible entry for that speaker, not just skip
   the new packet.
5. Visual range gate fails OPEN: unresolvable speaker, missing speaker entity, or missing listener
   => shown. Boundary is inclusive.
6. Mode is always read from the speaker's STOCK VoN component on their CURRENT controlled entity
   (never a transmit tier, never a corpse). A corpse's frozen mode must not apply to the
   respawned player.
7. Visual gate ranges mirror the tier ACPs' outer ranges (6 / 20 / 80 m); change both together.
   Audio never reads the visual gate.
8. HUD initial sprite = WHISPER, matching the spawn default; if one changes the other must.
9. The HUD display must live on the player controller's HUD manager (survives vehicle entry) and
   locate the VoN component via the controlled entity each frame.
10. The outgoing radio frequency colour is owned exclusively by this logic and must yield
    entirely (no WHITE reset either) when the coexistence guard reports a conflicting radio mod.
11. NORMAL shows no overlay label (frame hidden), by design.
12. Local mode cycle must refresh the overlay immediately (the authority may not receive its own
    replication callback); remote clients refresh from the replicated-mode callback.
13. The nametag debug line must stay transition-throttled per speaker.
14. Mission-header property names in 1.3 and the component/HUD class names in 1.4/1.7 are
    referenced by existing scenarios and prefabs; keep them. Removed range/volume header
    properties stay removed.
15. `EC29_VonRange` stays defined (default 1, max 3) even though it no longer controls range; the
    ACPs still reference it and script pins it to unity.
