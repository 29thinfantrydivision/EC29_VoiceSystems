# Clean-room spec: Radio layer (ear routing, beeps, radial entry, frequency dialog, VON controller radio regions)

Audience: the engineer reimplementing the radio layer without access to the
current code or to the third-party mod it was ported from. This document
describes behavior and contracts only. Anything marked **[contract]** must be
kept exactly (names, values, signatures), because assets, configs, other EC29
subsystems, or vanilla's modded-class rules depend on it. Everything else may
change as long as the observable behavior matches.

Source of truth for this spec: branch `in-house-rework` at `fe32eb5`.

---

## 0. Scope

### 0.1 Whole files to rewrite

| File | Status | Notes |
|---|---|---|
| `Scripts/Game/VON/EC29_EarRouting.c` | **Rewrite** | Both enums and the settings class are derived. Default-routing classification (sec 3.1.2) and the VERBOSE logs are original 29th work but live in this file, so they are re-specified here and must be reimplemented. |
| `Scripts/Game/VON/EC29_RadioBeepHelper.c` | **Rewrite** | TX/RX beep selection and routed playback are derived. The master switch and the preview play are original 29th work, re-specified here. |
| `Scripts/Game/VON/EC29_VONEntryRadio.c` | **Rewrite** | Both members are derived. The "write the label before vanilla's update" ordering is an original 29th 1.8 fix (sec 4). |
| `Scripts/Game/VON/EC29_FrequencyDialog.c` | **Rewrite** | The dialog shell (vanilla configurable dialog) is 29th work, but the parse/clamp/snap/apply rule and the MHz display format are derived. Rewrite the whole file. |
| `Configs/Dialogs/EC29_Dialogs.conf` | **Rewrite allowed, keep contract** | Preset values in sec 2.7. |
| `UI/Layouts/Menus/Dialogs/EC29_FrequencyDialogContent.layout` | **Rewrite allowed, keep contract** | Widget name in sec 2.7. Keep the resource GUID (sec 2.7). |

### 0.2 `Scripts/Game/VON/EC29_VON_VONController.c` (modded `SCR_VONController`), method by method

I diffed the file against the third-party controller after normalizing the
prefix rename. **In scope** = derived (rewrite). **Mixed** = derived skeleton
with original 29th additions: rewrite the method, but the 29th requirements
listed for it in sec 3 must survive. **Original** = do not touch: no edits, no
moves, no renames.

| Member / method | Class | Notes |
|---|---|---|
| Sound constants: cycle, local-on, local-off, error **[contract paths, sec 2.5]** | In scope | Audio was already replaced in-house (`fe32eb5`), but the file names still match the third-party ones (see Ambiguities). |
| Audio handle members (cycle, local on/off, error), alternate-PTT latch, saved primary entry, keyed-frequency tracker, radio-check-played flag | In scope | Internal; names are free. |
| Key-rate bucket constants + bucket member | **Mixed** | The token bucket (`EC29_TokenBucket`, in `EC29_RadioState.c`) is original and replaced the third-party sliding window and lockout. Keep the class untouched. The controller's use of it is in scope. |
| Voice-range action name constant, pending-tier member | Original | |
| `Init` | Original | |
| `AddEntry` | Original | |
| `Cleanup` | Original | |
| `EC29_ActionVoiceRangeCycle` | Original | |
| `EC29_LocalStockVoN` | Original | |
| `EC29_ResolveTransmitTier` | Original | |
| `SetVONComponent` | Original | |
| `EC29_ApplyVoiceTier` | Original | |
| `EC29_ApplyPendingTier` | Original | Called by `DeactivateVON` (sec 4, I-3). |
| `OnControlledEntityChanged` | Original | |
| `OnPostInit` | In scope | |
| `PlayBeepStart`, `PlayBeepEnd` | In scope | Thin wrappers. May be inlined or renamed. |
| `SetVONBroadcast` | Original | |
| `SetActiveTransmit` | **Mixed** | Original additions: coexistence yield, voice-capture clear before key-up. |
| `EC29_IsKeySpamLocked` | In scope | Name and role are derived. The body is already 29th (bucket); rewrite it with the rest. |
| `EC29_PlayErrorBeep` | In scope | |
| `EC29_TryPlayRadioCheck` | **Mixed** | Derived one-shot first-spawn check. The original change: no sound, the conflict notice goes to chat, otherwise a log line. |
| `DeactivateVON` | **Mixed** | Original additions: coexistence yield, pending-tier apply after super. |
| `EC29_NotifyKeyStart`, `EC29_NotifyKeyStop` | In scope | |
| `RpcAsk_EC29_KeyState` **[contract]** | In scope | |
| `EC29_FindRadioEntryByFrequency` **[contract]** | In scope | |
| `EC29_SetDirectSpeechTransmitLocked` | Original | |
| `EC29_SelectVonComponent` | Original | |
| `ActionVONBroadcast` | Original | |
| `ActionVONLongRangeToggle` | Original | |
| `EC29_IsAlternateKeyClaimed` | Original | Reads `EC29_AlternateChannel` and the alternate frequency (contract in sec 2). |
| `ActionVONProximity` | Original | |
| `ActionVONProximityToggle` | **Mixed** | Original additions: spectator gate, forward to super when there is no VoN component, coexistence yield. |
| `ActionVONTransceiverCycle` | **Mixed** | Original additions: spectator gate, coexistence yield. |
| `Update` | **Mixed** | The radio-input polling is derived. Original: coexistence yield, the asymmetric spectator gating, reuse of the vanilla input-manager member. Gate order is in sec 4, I-6. |
| `OnEarRoutingToggle` | In scope | |
| `OnBeepTypeToggle` | **Mixed** | Original additions: preview tone, popup when the master switch is off. |
| `OnSetFrequencyPressed` | **Mixed** | Original addition: refuses special nets. |
| `OnVolumeAdjust` | In scope | |
| `OnAlternateChannelToggle` | **Mixed** | Original addition: refuses special nets. |
| `OnAlternatePTTStart` | **Mixed** | Original addition: refuses special nets, VERBOSE log. |
| `OnAlternatePTTEnd` | In scope | |
| `FindEntryByFrequency` (protected) | In scope | Internal. The public wrapper is the contract. |

What the third-party controller had that is already gone (do not reintroduce):
- the analog volume action poll (replaced in the 1.8 pass by discrete Up/Down)
- the self-managed frequency-input widget and its per-frame open/close polling
  (replaced by the vanilla dialog)
- the spawn-in roger beep and its chat banner

### 0.3 Explicitly NOT covered here

These files also have third-party counterparts but are outside this spec. Leave
them alone in this pass: `EC29_RadioRxSquelch.c`, `EC29_JammerComponent.c`,
`EC29_JammerRegistry.c`, `EC29_RFPropagationModel.c`, the radio audio-variable
section of `EC29_VON_VoNComponent.c`, `UI/HUD/EC29_VON_VonDisplay.c`. They call
into this layer through the contract in sec 2, which must stay stable.

---

## 1. Data model and lifecycle

- All per-radio settings (ear routing, beep style, channel volume) and the one
  global alternate channel live in **one plain object** (class
  `EC29_RadioEarSettings` **[contract]**). The world-scoped service
  `EC29_RadioState` (original, out of scope) owns it. That service builds it
  with a **parameterless constructor** **[contract]** and hands it out via
  `EC29_RadioState.GetInstance().EarSettings()`. The service throws it away
  and rebuilds it whenever the world changes.
- The settings object must hold **no statics**. Its lifetime is the world's.
- Per-radio settings are keyed by the **transceiver handle** (one entry per
  channel of a multi-channel radio). The alternate channel is a single
  **frequency** (int kHz), or "none" (negative).
- **Not persisted.** Routing, beep style, volume and alternate channel are
  session-scoped: a new world starts back at defaults. The only persisted value
  in this layer is the beep master switch (sec 2.4).

Defaults when a radio has no stored value:
- routing: the device-class default (sec 3.1.2)
- beep style: HIGH
- volume: 1.0 (100%)
- alternate: none

---

## 2. External contract

### 2.1 Enums **[contract - numeric values are consumed by audio assets]**

`EC29_EEarRouting`

| Name | Value | Meaning |
|---|---|---|
| `CENTER` | 0 | both ears |
| `RIGHT` | 1 | right ear |
| `LEFT` | 2 | left ear |

The numeric value is written straight into the audio variable `EC29_EarRouting`
(range 0..2, declared with max 2 in `RadioEarRouting.conf`). The voice ACPs
(`von.acp`, `EC29_VonNormal/Whisper/Yell.acp`), `EC29_beep.acp` and
`EC29_Routing.sig` branch on these numbers. The order is **not** the cycle
order. Do not renumber.

`EC29_EBeepType`

| Name | Value |
|---|---|
| `OFF` | 0 |
| `HIGH` | 1 |
| `LOW` | 2 |
| `CLASSIC` | 3 |

Keep the names and values. Nothing persists them, but out-of-scope code and
docs refer to them by name.

### 2.2 `EC29_RadioEarSettings` public surface

Called from code outside this spec, so these signatures are **[contract]**:

| Signature (Enforce types) | Out-of-scope callers |
|---|---|
| `EC29_EEarRouting GetRouting(BaseTransceiver)` | `EC29_VON_VoNComponent.c` (per voice packet) |
| `float GetVolume(BaseTransceiver)` | `EC29_VON_VoNComponent.c`, `EC29_SpectatorVonService.c` |
| `void SetVolume(BaseTransceiver, float)` | `EC29_SpectatorVonService.c` |
| `float AdjustVolume(BaseTransceiver, float delta)`, returns the clamped value now in force | `EC29_SpectatorVonService.c` |
| `int GetAlternateFrequency()` (negative = none) | `EC29_IsAlternateKeyClaimed` (original controller code) |
| `bool IsTransmittingOnAlternate()` | `UI/HUD/EC29_VON_VonDisplay.c` |

Only in-scope code uses everything else (cycle routing, cycle beep style,
display texts, volume percent, is-alternate, toggle alternate, set
transmitting-on-alternate, and so on). Names are free, but the behavior in
sec 3.1 must be available.

### 2.3 `EC29_RadioBeepHelper` (static class)

| Member | Status | Callers |
|---|---|---|
| `static const string BEEP_CONFIG` = `{63926E92E2606681}Sounds/VON/EC29_beep.acp` | **[contract]** name + value | controller `OnPostInit` (event-bank init) |
| `static void PlayRxOpen(BaseTransceiver)` | **[contract]** | `EC29_RadioRxSquelch.c` |
| `static void PlayRxClose(BaseTransceiver)` | **[contract]** | `EC29_RadioRxSquelch.c` |
| `static bool EC29_AreBeepsEnabled()` | keep (the controller popup uses it) | controller |
| TX start / TX end / preview entry points | free names | controller only |

The ear-routing variables resource is
`{3DA1A848EE00C426}Sounds/VON/RadioEarRouting.conf` **[contract]**.
`EC29_VON_VoNComponent.c` holds its own copy of that path.

### 2.4 Persisted setting **[contract - players' saved data]**

- Settings module class `EC29_RadioSettings`, field `RadioBeepsEnabled` (bool,
  default false). Both are defined in `Scripts/Game/EC29_Earplugs_AudioSettingsSubMenu.c`
  (out of scope) and bound to the Audio tab checkbox there.
- This layer only **reads** it, by module name `"EC29_RadioSettings"` and field
  name `"RadioBeepsEnabled"` through the game user settings.

### 2.5 Audio names **[contract]**

Audio variables, all in `RadioEarRouting.conf`. This layer writes two of them:

| Variable | Written by this layer? | Value |
|---|---|---|
| `EC29_EarRouting` | yes, before every beep | routing enum value (0/1/2) |
| `EC29_ChannelVolume` | yes, before every beep | volume^2.5 |
| `EC29_SignalQuality`, `EC29_JamStrength` | no (out-of-scope voice path) | |

Sound events in `EC29_beep.acp`. The names must match the ACP's sound nodes
exactly:

| Event | Used for |
|---|---|
| `EC29_BEEP_HIGH` | HIGH key-up, HIGH RX close, HIGH preview |
| `EC29_BEEP_LOW` | LOW key-up, LOW RX close, LOW preview |
| `EC29_CLICK_OFF` | HIGH/LOW key-release |
| `EC29_CLASSIC_START` | CLASSIC key-up, CLASSIC RX open, CLASSIC preview |
| `EC29_CLASSIC_END` | CLASSIC key-release, CLASSIC RX close |
| `EC29_SQUELCH_TAIL` | HIGH/LOW RX open |

Plain (non-event) UI sounds played by the controller:

| Purpose | Resource |
|---|---|
| transceiver-cycle click | `{19696BC8C5ECE170}Sounds/VON/EC29_FX/RadioCycle.wav` |
| direct-speech toggle ON | `{E21F58D501028C63}Sounds/VON/EC29_FX/RadioLocalOn.wav` |
| direct-speech toggle OFF | `{AFA775D58D24308A}Sounds/VON/EC29_FX/RadioLocalOff.wav` |
| key-up denied (rate limit) | `{7065D8DD8ADFA3DE}Sounds/EC29_Sound/errorbeep.wav` |

### 2.6 Input actions **[contract]**

Defined in `Configs/System/chimeraInputCommon.conf`. They are listed in
`keyBindingMenu.conf` (category `EC29`, separator "Radio") and in
`Configs/ControlHints/AvailableActions.conf` (tag `EC29_VON`). The names are the
keys players' rebinds are stored under.

| Action | Context | Default keys | Kind | Handler behavior |
|---|---|---|---|---|
| `EC29_AlternateChannel` | `VONContext` | hold LCtrl + Caps Lock; gamepad LB+RB | hold (value) | alternate push-to-talk (sec 3.6) |
| `EC29_VONRoutingAction` | `VONMenuContext` | T; gamepad RB | click | cycle ear routing of hovered radio |
| `EC29_SetFrequencyAction` | `VONMenuContext` | F; gamepad LB | click | open frequency dialog for hovered radio |
| `EC29_VONBeepTypeAction` | `VONMenuContext` | K; gamepad Y | click | cycle beep style of hovered radio |
| `EC29_VolumeUp` | `VONMenuContext` | `]`; gamepad LB + D-pad up | click | +10% volume on hovered radio |
| `EC29_VolumeDown` | `VONMenuContext` | `[`; gamepad LB + D-pad down | click | -10% volume on hovered radio |
| `EC29_AlternateChannelAction` | `VONMenuContext` | LCtrl + F; gamepad LB + X | click | toggle hovered radio as alternate |

`EC29_VONVoiceRangeCycle` (F3) and `EC29_ToggleEarplugs` live in the same confs
but belong to original / other subsystems.

Control-hint labels: "Set Frequency Manually", "Change Ear Routing", "Change
Beep Sound", "Change Channel Volume", "Set Alternate Channel", "Transmit
Alternate". Keybind-menu labels: "Set Frequency", "Change Ear Routing", "Change
Beep Sound", "Channel Volume Up", "Channel Volume Down", "Set Alternate
Channel", "Transmit Alternate".

### 2.7 Frequency dialog assets **[contract]**

- Preset config `{684601EE00000301}Configs/Dialogs/EC29_Dialogs.conf`. A vanilla
  configurable-dialog preset list with one preset:
  - inherits vanilla `{A1A24C0D7D3A7EF0}Configs/ConfigurableDialogs/DialogPrefabs/MessageOkCancel.conf`
  - tag `ec29_frequency`
  - title `Set Frequency`
  - content layout `{684601EE00000304}UI/Layouts/Menus/Dialogs/EC29_FrequencyDialogContent.layout`
- Content layout: a vertical layout holding one vanilla widget-library edit box
  (`WLib_EditBox.layout`) named **`EditBox`**, padded 8 on all sides. As far as
  I can tell, the vanilla edit-box dialog base finds its edit box by that name
  (inferred from the current setup working; I did not check the vanilla
  source).
- Dialog class `EC29_FrequencyDialog` extends vanilla `SCR_EditboxDialogUi`.
  Its open entry point is called only from the controller, so its name is free.

### 2.8 RPC **[contract]**

- `RpcAsk_EC29_KeyState(int frequency, float range, bool keyed)` on the modded
  VON controller. Reliable, receiver = server.
- Server side: resolve the owning player controller's player id and forward
  `(playerId, frequency, range, keyed)` to
  `EC29_RFPropagationNetworkComponent.GetInstance().EC29_RelayKeyState(...)`
  (out of scope). Do nothing if either side is missing.

### 2.9 Vanilla members/overrides relied on

Modded `SCR_VONEntryRadio`:
- must expose `void SetEntryFrequency(int freqKHz)` (the dialog uses it)
- overrides `Update()`
- uses vanilla's `m_iFrequency`, `m_sText`, `LABEL_FREQUENCY_UNITS`,
  `m_RadioTransceiver`, `m_EntryComponent` and the 1.8 field
  `m_sFrequencyTextOverwrite`, plus `SCR_VONEntryComponent.SetFrequencyColor`

Controller overrides: `OnPostInit`, `SetActiveTransmit`, `DeactivateVON`,
`ActionVONProximityToggle`, `ActionVONTransceiverCycle`, `Update`.
**Signatures must match vanilla exactly, including default arguments, or the
override is rejected at compile time.**

### 2.10 Original EC29 APIs this layer must call

- `EC29_CoexistenceGuard.ShouldYieldRadio()`: true when a known conflicting
  radio mod is loaded. EC29 radio behavior then stands down.
- `EC29_CoexistenceGuard.EC29_IsSpecialNet(BaseTransceiver)`: another system's
  dedicated net (the spectator net, admin nets).
- `EC29_CoexistenceGuard.GetConflictNotice()`: empty string when there is no
  conflict.
- `EC29_SpectatorVonService.EC29_ShouldBlockVanillaVonActions()`: derived per
  call, never cached.
- `EC29_RadioState.GetInstance().EarSettings()`
- `EC29_TokenBucket(capacity, windowMs)` with `TryConsume(nowMs)`.
- `EC29_RFPropagationSettings.GetInstance()`: touched at controller post-init
  so the settings singleton exists early.
- `EC29_Debug.VERBOSE`: the debug-log gate.

---

## 3. Behavior

### 3.1 Ear settings (`EC29_RadioEarSettings`)

#### 3.1.1 Routing lookup
- A null transceiver resolves to CENTER.
- A stored routing (manual cycle or memoized default) is returned as is.
- Otherwise the device-class default is resolved (3.1.2) and returned.

#### 3.1.2 Default routing (original 29th, Issue #7; see `Docs/EarRouting_Defaults.md`)

Resolved for a radio the first time anything asks:

- **Radio with 2+ channels** (1.8 dual-channel): the first channel (index 0,
  squad net) goes LEFT; every later channel (platoon net) goes RIGHT.
- **Single-channel handheld** (gadget type RADIO): LEFT.
- **Single-channel manpack** (gadget type RADIO_BACKPACK): RIGHT.
- **No radio gadget component** (vehicle sets, editor transceivers): CENTER.
- **Special nets:** CENTER. The owning system decides how that audio is
  presented.

Memoization:
- **When it happens:** the resolved default is stored. Every later lookup,
  including the per-voice-packet hot path, is then a single map hit.
- **When the radio is not resolvable yet:** if the transceiver has no radio
  component, or the radio component has no owner entity, return CENTER
  **without storing it**, so a later query can classify it properly.
- **Logging:** VERBOSE-only, one log line when a default is stored.

#### 3.1.3 Routing cycle (T in the radial menu)
- **Starting point:** the *effective* routing, so it is default-aware.
- **Order:** CENTER, then LEFT, then RIGHT, then back to CENTER. An
  unrecognized value goes to CENTER.
- **Effect:** the result is stored and overrides the default for that radio
  for the rest of the session.
- **Display letters:** LEFT "L", RIGHT "R", anything else "C".

#### 3.1.4 Beep style
- **Default:** HIGH, for a null or unset transceiver.
- **Cycle order (K):** OFF, then HIGH, then LOW, then CLASSIC, then back to
  OFF. An unrecognized value goes to LOW.
- **Long display text** (used in the popup): OFF "OFF", HIGH "HI", LOW "LO",
  CLASSIC "CLS", unrecognized "LO".

#### 3.1.5 Volume
- **Default and clamping:** default 1.0. Stored values are always clamped to
  [0.0, 1.0].
- **Adjust:** adds a signed delta to the current value, clamps, stores, and
  returns the clamped result.
- **Percent:** volume × 100, rounded to the nearest integer.
- **Audio gain:** volume raised to the power **2.5** for better low-end
  resolution. The out-of-scope voice path applies the same exponent; the two
  must agree.

#### 3.1.6 Alternate channel
- **What it is:** at most one per player, identified by **frequency**, not by
  radio.
- **Is-alternate check:** false when none is set; otherwise true when the
  radio's current frequency equals the stored one. Retuning the radio
  therefore drops its alternate status, and another radio on the same
  frequency counts as alternate.
- **Toggle on a radio:** if that radio is currently the alternate, clear the
  alternate. Otherwise make its current frequency the alternate, replacing any
  previous one. A null radio is a no-op.
- **Transmitting-on-alternate:** a separate bool, set while the alternate
  push-to-talk is held (3.6). The HUD reads it.
- **Logging:** VERBOSE-only, one line when cycle routing, cycle beep, adjust
  volume, or toggle alternate is invoked.

### 3.2 Beeps (`EC29_RadioBeepHelper`)

Concept: TX beeps are the operator's own key-up/release confirmation. RX beeps
are squelch open/close when receiving someone else. RX is deliberately
asymmetric: a subtle sound on open, a prominent one on close.

Style to event (OFF always plays nothing):

| Moment | HIGH | LOW | CLASSIC |
|---|---|---|---|
| TX start (key-up) | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_START` |
| TX end (release) | `EC29_CLICK_OFF` | `EC29_CLICK_OFF` | `EC29_CLASSIC_END` |
| RX open (squelch opens) | `EC29_SQUELCH_TAIL` | `EC29_SQUELCH_TAIL` | `EC29_CLASSIC_START` |
| RX close (squelch closes) | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_END` |
| Preview (style cycled) | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_START` |

The style is the one stored for **that radio**. Every entry point no-ops on a
null transceiver.

**Master switch.**
- **Gate:** TX start, TX end, RX open and RX close play only when the persisted
  `RadioBeepsEnabled` is true. The default is false: beeps are opt-in.
- **Missing settings module:** treated as OFF, with one WARNING per game run
  (text in sec 5). Otherwise the Audio-tab checkbox silently does nothing.
- **Suppressed beeps are silent in the log**, except one VERBOSE line. This is
  the common path with default settings, and it runs per squelch event.

**Preview bypasses the master switch.**
- **Why:** it is the audible feedback for a deliberate K press and doubles as
  a speaker test of the beep path.
- **OFF:** previews as silence, which is the correct feedback for OFF.

**Routed playback** (every beep, including preview):
1. Set `EC29_EarRouting` to the radio's effective routing value.
2. Set `EC29_ChannelVolume` to the radio's volume^2.5.
3. Fire the event from `EC29_beep.acp` as a non-positional 2D sound (identity
   transform).

Today the ACP only consumes the routing variable. The volume variable is set
so beeps follow channel volume once it is wired into the ACP. These variables
are global: the voice path overwrites them per packet as well. That is the
existing design; keep it.

**Who calls what:**
- **TX start:** the controller, at key-up.
- **TX end:** the controller, at release.
- **RX open/close:** the squelch tracker (out of scope). It already handles
  re-open grace and the power/mute gates.
- **Preview:** the K handler.

### 3.3 Radial-menu entry (modded `SCR_VONEntryRadio`)

**Set entry frequency (kHz):**
- Store the frequency on the entry.
- Rebuild the entry's base frequency text in vanilla's style: frequency in MHz
  rounded to the nearest 0.01 MHz, rendered with one decimal place, a space,
  then vanilla's frequency-units label.
- The dialog calls this after a retune so the label reflects the new frequency
  immediately.

**Update:**
1. If the entry has a transceiver, compose the label as
   `<base frequency text> <R>|<B>|<V>`:
   - `<R>`: the routing letter (L / R / C)
   - `<B>`: the short beep code: OFF "-", HIGH "BH", LOW "BL", CLASSIC "CLS",
     unrecognized "BH"
   - `<V>`: the volume percent as a bare integer, no % sign

   Example shape: `45.0 MHz L|BH|100`.
2. Write the label into vanilla's 1.8 frequency-text-overwrite field **before**
   running vanilla's update (invariant I-2).
3. Run vanilla's update.
4. After vanilla, if the entry component and transceiver exist and the radio is
   the current alternate (3.1.6), colour the entry's frequency text **cyan**.

Nothing here resets the colour for non-alternate entries; vanilla's own
rendering is relied on (see Ambiguities).

### 3.4 Frequency dialog (`EC29_FrequencyDialog`)

**Opening.** F in the radial menu, on a hovered radio that is not a special
net (the controller enforces the special-net refusal).
- Creates the dialog from the preset (sec 2.7). It inherits the standard frame,
  confirm/cancel, gamepad support and input blocking from vanilla.
- If the preset fails to load, log an ERROR and return nothing.

**On open:**
- Message line: `Range: <min> - <max> MHz` (the transceiver's band limits).
- Prefill the edit box with the transceiver's current frequency.
- Put the edit box straight into write mode so the player can type
  immediately.

**MHz display format** (message and prefill): whole MHz, a dot, then a
**single** digit for the hundreds-of-kHz place, **truncated**, not rounded. For
example, 45 125 kHz displays as `45.1` and 30 000 kHz as `30.0`.

**Confirm.** Applies, then closes via vanilla. Validation rules:
1. Parse the text as a decimal MHz number. If the result is not > 0 (empty,
   non-numeric, zero, negative), **do nothing**: no retune, no message, and the
   dialog just closes.
2. Convert to kHz (×1000, fractional kHz truncated).
3. **Clamp** into the transceiver's [min, max] band. Out-of-range input is
   corrected, not rejected.
4. If the transceiver reports a channel resolution > 0, **snap down** to a
   whole multiple of that resolution (multiples counted from 0 kHz, not from
   the band minimum).
5. Set the transceiver's frequency to the result.
6. If the dialog was opened from a radial entry, set that entry's frequency
   (3.3) and refresh the entry so the label updates at once.

**Cancel** changes nothing.

**Error feedback:** there is **none** today. Invalid input is silently ignored
and out-of-band input silently clamped. A reimplementation may add feedback,
but must not reject what is accepted today (see Ambiguities).

**Logging:** VERBOSE lines on open, on confirm (with the raw text), and on the
applied kHz value.

### 3.5 Radial-menu actions (controller)

All five act on the radio entry currently **hovered/selected** in the VON radial
menu. If there is no radial menu, the hovered entry is not a radio entry, or
the entry has no transceiver, they do nothing. After any change, refresh the
radial menu's entries so labels update.

- **Routing (T):** cycle routing (3.1.3).
- **Beep style (K):**
  - Cycle the style (3.1.4), then play the preview (3.2).
  - If the master switch is OFF, also show the vanilla popup notification:
    - title `Radio beep style: <long text>`
    - duration 4 s
    - subtitle `Radio beeps are OFF - enable them in Settings > Audio > 29th ID`
  - With the master ON there is no popup; the preview tone and label are the
    confirmation.
- **Set frequency (F):** refuse special nets; otherwise open the dialog (3.4).
- **Volume (`]` / `[`):** a positive input is +0.1, otherwise -0.1. That is one
  10% step per press, clamped 0..100%.
- **Alternate toggle (LCtrl+F):** refuse special nets (marking one would hand a
  transmit route around other mods' action blocks); otherwise toggle (3.1.6).

### 3.6 Alternate push-to-talk (controller)

Edge-detected from the **value** of `EC29_AlternateChannel` each frame, against
an internal "alternate PTT active" latch.

**Start edge** (value > 0, latch down). Only when the spectator block is not
active:
- **Preconditions:** an alternate frequency is set, a radio entry tuned to it
  exists, and that radio is not a special net (belt-and-braces with the toggle
  gate). Otherwise do nothing.
- **Then:**
  1. Raise the latch and the settings object's transmitting-on-alternate flag.
  2. Remember the current active entry.
  3. Make the alternate entry active.
  4. Activate a CHANNEL transmission through vanilla.
- The normal key-up hooks (rate limit, TX beep, key RPC) then apply through
  vanilla's activation path. Do not add separate beep or RPC calls here.
- **Logging:** VERBOSE line with the frequency.

**End edge** (value <= 0, latch up). **Always runs, never gated** (invariant
I-6):
1. Clear the flag and the latch.
2. Deactivate the CHANNEL transmission.
3. Restore the remembered primary entry, if any, and forget it.
- **Logging:** VERBOSE line.

Interplay with vanilla Caps Lock (original code, out of scope, for context
only): while the alternate key is held **and** an alternate is set, vanilla's
radio-PTT start and long-range toggle are swallowed. With no alternate set,
LCtrl+Caps stays vanilla's long-range toggle.

`EC29_FindRadioEntryByFrequency(int)` **[contract, public]**:
- **Returns:** the first radio entry among the controller's VON entries whose
  transceiver is currently on that frequency.
- **Returns null when:** the frequency is negative, or nothing matches.
- **Used by:** alternate PTT and the out-of-scope squelch tracker.

### 3.7 Key-up path (controller `SetActiveTransmit`, `DeactivateVON`)

**Key-up** (the entry being activated is a radio entry, and the coexistence
guard is not yielding). In this order:
1. **Rate limit.** Consume one token from the key bucket: capacity **4**,
   refilled over a **4000 ms** window (1 token per second steady state), clocked
   by world time.
   - If no world exists, allow.
   - If the bucket is empty: play the deny tone, a VERBOSE log line, and
     **return without calling vanilla**. No transmission, no TX beep, no key
     RPC.
2. **Capture clear.** Clear the player's voice capture (the current VoN
   component's capture off) before keying. This is the 1.8 wedge self-heal
   (invariant I-4).
3. **Beep and key RPC.** If the entry has a transceiver: TX start beep (3.2),
   then key-start notification (below).
4. Call vanilla.

Non-radio entries, or a yielding coexistence guard: straight to vanilla.

**Release (`DeactivateVON`):**
- If a transmission is active, the requested type is not DIRECT, and the
  coexistence guard is not yielding:
  1. Play the TX end beep for the active radio entry's transceiver, if any.
  2. Send the key-stop notification.
- Then call vanilla.
- Then run the original pending-tier apply (invariant I-3).

**Key-state notifications** (always paired):
- **Key-start for a frequency already keyed:** no-op.
- **Key-start while another frequency is keyed** (entry swap mid-key, e.g.
  alternate PTT): first send `keyed=false` for the old frequency (range 0).
  Then record the new frequency and send `keyed=true` with the transceiver's
  current range.
- **Key-stop:** only if something is keyed. Send `keyed=false` (range 0) for
  it and clear the record.
- **Purpose:** lets receivers squelch on a dead key even when no voice packets
  flow.

**Deny tone:** if the previous deny tone is still playing, stop it, then play a
fresh one. It restarts rather than stacking.

### 3.8 Other controller sound hooks

- **Post-init:** after vanilla, initialize the beep ACP for event playback and
  touch the RF propagation settings singleton.
- **Direct-speech toggle action** (spectator gate first: a blocked press makes
  no sound):
  - With no VoN component: still forward to vanilla (invariant I-1) and do
    nothing else.
  - Otherwise: note the toggle state, run vanilla, and stop if the coexistence
    guard yields.
  - If the toggle went off-to-on, play LocalOn; on-to-off, play LocalOff. Each
    restarts its own previous instance rather than stacking.
- **Transceiver-cycle action** (spectator gate first: a blocked press makes no
  sound):
  - On the DOWN trigger, unless the coexistence guard yields, play the cycle
    click (restart-not-stack).
  - Then always run vanilla.

### 3.9 First-spawn radio check (controller, mixed)

- **When it runs:** checked from `Update` until it has fired once per
  controller instance. The controller lives for one server session on the
  client, so it resets on reconnect and never replays on respawn.
- **When it fires:** once there is a local player controller whose controlled
  entity is a character. Mark it done.
- **Conflict notice:** if `GetConflictNotice()` is non-empty, show it in chat
  (via the player's chat component, if present) and stop.
- **Otherwise:** write one **unconditional** client log line (sec 5) that
  proves the mod loaded. No chat line, no sound.

### 3.10 Input rule: radio keys are frame-POLLED, not listener-driven (preserve)

Radio actions are read by polling in the controller's per-frame update:
- PTT by **action value**
- the menu actions by **triggered-this-frame**, and **only while the VON radial
  menu is open**

**Do not convert these to action listeners.** History (commit `940c076`
reverted `cba68ed`): the listener conversion made EC29's action contexts take
part in the engine's key arbitration:
- the menu context's T/F/K bindings shadowed or double-fired vanilla actions
  on the same physical keys
- the PTT action in the broadly active `VONContext` went live in far more
  situations than the value-poll gate

Polling keeps these keys inert in every other input situation, which is what
keeps them conflict-free. The 1.8 pass also moved volume off the mouse wheel
to `]`/`[`, because the 1.8 VON menu tunes frequency on the bare wheel and any
wheel source double-fired. Do not put volume back on the wheel.

Exception: the F3 voice-range cycle is listener-driven, which is original and
out of scope.

---

## 4. Invariants that must survive

- **I-1 Super-chain.** Every override forwards to vanilla, so other mods'
  overrides further down the modded chain keep running. The only exceptions
  are deliberate short-circuits:
  - the rate-limit deny in `SetActiveTransmit`
  - the spectator-block early returns (original)
  - the coexistence early return in `Update`, which comes *after* super

  `ActionVONProximityToggle` must forward to vanilla **even when there is no
  VoN component**. The third-party version returned early there, which is a
  fixed bug. `Update` calls vanilla **first**.
- **I-2 1.8 label ordering.** The composite radial label goes into vanilla's
  `m_sFrequencyTextOverwrite` **before** vanilla `Update` runs.
  - **Why:** vanilla then renders it natively, and native wheel-tuning flows
    into it.
  - **Do not** write the frequency text widget after vanilla instead. That
    stomps anything vanilla legitimately routes through the overwrite
    mechanism. The cyan colour is the only post-super touch.
- **I-3 DeactivateVON ordering.** Beep and key-stop run **before** vanilla's
  deactivation, while the active entry is still the keyed one. The original
  pending-tier apply runs **after** vanilla.
- **I-4 Capture clear before every radio key-up** that passes the rate limit
  (1.8 per-player capture wedge, field-reported first-joiner dead radio).
  Denied key-ups do not reach it.
- **I-5 Denied key-ups never reach vanilla.** No beep, no RPC, deny tone only.
- **I-6 `Update` gate order** (original structure, keep exactly):
  1. vanilla update
  2. first-spawn check
  3. return if the coexistence guard yields
  4. return if there is no input manager (reuse vanilla's resolved member; do
     not re-fetch it every frame)
  5. alternate-PTT edge detector: the START edge is gated by the spectator
     block; the END edge is **never** gated
  6. return if the spectator block is active
  7. if the radial menu is open, poll the six menu actions

  Why the asymmetry in step 5: the spectator gate is derived per frame and can
  flip true mid-hold (dying into spectate with the key down). A whole-tail
  return would strand the latch, the saved primary entry, and the cyan
  transmitting-on-alternate state for the entire spectate.
- **I-7 Special nets are never touched by this layer:**
  - never retuned (F refused)
  - never marked alternate (toggle refused)
  - never keyed by alternate PTT
  - default routing CENTER

  A changed frequency breaks that system until its owner rebuilds it.
- **I-8 Coexistence.** When `ShouldYieldRadio()` is true, the following stand
  down entirely and vanilla behavior is untouched:
  - key-up beep, RPC, rate limit and capture clear
  - release beep and RPC
  - toggle and cycle sounds
  - all polled radio input
- **I-9 Key RPC pairing.** Start and stop are always balanced per frequency,
  including mid-key entry swaps.
- **I-10 Enum numerics** (2.1) and **event/variable names** (2.5) are asset
  contracts.
- **I-11 Default routing is memoized only when classification succeeded**
  (3.1.2).
- **I-12 Master switch default OFF.** Preview is the sole bypass. A suppressed
  beep logs nothing outside VERBOSE.
- **I-13 One gain curve.** Volume^2.5 is used both here and in the voice path.
- **I-14 Lifecycle.** The settings object holds no statics and is
  world-scoped via `EC29_RadioState`. (The beep helper's one-per-run "module
  missing" warning flag is the only static here, and it is intentional.)
- **I-15 Vanilla override signatures** match exactly, including default
  arguments.

---

## 5. Logging summary

Tags in use: `[EC29]` (always-on user-relevant), `[EC29-DBG][<Area>]` (debug).
Most debug lines are gated by `EC29_Debug.VERBOSE`. Exceptions are noted.

**Always on (not VERBOSE-gated):**

| Where | Level | Content |
|---|---|---|
| Beep master switch read, settings module missing | WARNING, **once per game run** | `[EC29] EC29_RadioSettings module not found in game user settings - radio beeps forced OFF and the Audio-tab checkbox will not work` |
| Frequency dialog preset failed | ERROR | `[EC29-DBG][RadioFreq] ...preset failed to load - check EC29_Dialogs.conf` (debug-tagged but ungated) |
| First-spawn check, no conflict | NORMAL | `[EC29] Voice systems initialized (client, player <id>)`. Keep the wording: it is the per-client proof the mod loaded and is grepped in client RPTs. |

**VERBOSE only** (wording free, keep the area tags):

| Area tag | Lines |
|---|---|
| `RadioEar` | default routing applied (routing letter, kHz, whether a gadget was found); cycle routing / cycle beep / adjust volume (with delta) / toggle alternate pressed |
| `RadioBeep` | TX start requested; TX end requested; beep suppressed because the master switch is off |
| `RadioFreq` | dialog open; confirm with raw text; frequency set to N kHz |
| `RadioKey` | key-up denied, bucket empty |
| `RadioAlt` | alternate PTT start (frequency); alternate PTT end |

Original controller code (out of scope, unchanged) logs under `VONCtrl`,
`RadioTX`, and `[EC29_VON]`.

---

## Ambiguities / open questions

1. **Lineage left in assets.**
   - `Sounds/VON/EC29_beep.acp` still names its internal bank nodes
     `ACE_HighBeep`, `ACE_LowBeep`, `ACE_ClickOff`, `GRS_Start`, `GRS_End`,
     `Squelch Tail`, which are third-party naming.
   - The controller's plain sound files keep the third-party file names
     (`RadioCycle.wav`, `RadioLocalOn.wav`, `RadioLocalOff.wav`,
     `errorbeep.wav`), even though `fe32eb5` replaced their audio.

   None of these are script-visible contracts. Renaming them is an asset/meta
   change outside this spec; decide separately.
2. **Mixed methods.** For the "Mixed" controller methods, I've classed the
   whole method as rewrite-with-preserved-requirements, not "keep". If you want
   original lines kept verbatim instead, the implementer needs those exact
   blocks handed over, which the clean-room rule forbids. The requirements in
   sec 3 and 4 are written to make that unnecessary.
3. **`EC29_IsKeySpamLocked`.** The name and role are derived, but the body
   (bucket call) is already 29th. I listed it in scope because the rewrite is
   trivial. The bucket class itself stays.
4. **Frequency dialog quirks, preserved as-is unless you decide otherwise:**
   - the MHz display truncates to one decimal, so prefill can lose precision
     on 25/50 kHz channels
   - snapping is counted from 0 kHz, so after clamping, a band minimum that is
     not a multiple of the resolution could snap *below* min
   - there is no user-visible error for bad input

   Adding feedback is behavior-additive and probably welcome.
5. **Cyan colour reset.** The entry only ever sets cyan, never resets it. A
   radio that stops being alternate relies on vanilla's update to repaint the
   default colour. I did not verify that vanilla does so.
6. **Alternate PTT start while a primary transmission is already active.**
   Nothing guards this today. The behavior is whatever vanilla activation does
   with the swapped active entry, and the key-RPC pairing keeps the server
   side consistent. I'm not specifying it further.
7. **Vanilla activation path.** The claim that alternate PTT's vanilla
   activation runs through the overridden key-up hook (so rate limit, beep and
   RPC apply) is inferred from the key-RPC comment about "active-entry swaps
   mid-key (alternate channel PTT)". I did not check it against the vanilla
   source.
8. **Default branches.** Several "unrecognized value" defaults are
   inconsistent: beep default HIGH but cycle-fallback LOW, long text fallback
   "LO" but short code fallback "BH". They are unreachable with valid enums.
   The values are documented above; matching them exactly is optional.
9. **Not covered.** Other third-party-derived files are not covered by this
   spec (sec 0.3). A separate spec is needed if they are in the clean-room
   scope.
