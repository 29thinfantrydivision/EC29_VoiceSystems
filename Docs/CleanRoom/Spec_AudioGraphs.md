# Clean-room spec: VON audio graphs (von.acp family, sigs, beep ACP, variable confs)

Audience: the engineer who rebuilds the non-vanilla parts of the voice audio graphs,
starting from Bohemia's vanilla `Sounds/VON/von.acp` and `Sounds/VON/Von.sig`, without
access to the current EC29 graphs or the third-party sources they were built from.

This document describes behavior and contracts. Items marked **[contract]** must be kept
exactly (names, GUIDs, value meanings), because scripts, prefabs or other graphs depend on
them. Everything else is free as long as the observable audio matches.

Source of truth: branch `in-house-rework` at `fe32eb5`.

Labels used per part:

- **VANILLA**: identical to the vanilla extraction. Start from vanilla and leave it alone.
- **KEEP**: 29th-original work. Carry over as described (the engineer may re-create it from
  this description; nothing in it is third-party).
- **REBUILD**: derived from the third-party radio mod. Re-implement from the behavior in
  section 3. Do not try to reproduce its structure.
- **DECIDE**: derived from a second third-party source (the WCS voice mod), which this
  clean room was not briefed on. Behavior is given so it can be rebuilt. Whether to rebuild
  or keep it is Nathan's call (see section 5).

---

## 1. Contract

### 1.1 Audio variables

All of these are global external audio variables (`source External`), set from script with
`AudioSystem.SetVariableByName(name, value, confResource)` and read by the graphs.

| Variable **[contract]** | Declared in | Range / default | Written by | Meaning |
|---|---|---|---|---|
| `EC29_EarRouting` | `RadioEarRouting.conf` | 0..2, default 0 (`value_max 2`) | `EC29_VON_VoNComponent.c` per incoming radio packet; `EC29_RadioBeepHelper.c` before every beep | Enum `EC29_EEarRouting`: **0 = CENTER, 1 = RIGHT, 2 = LEFT** |
| `EC29_JamStrength` | `RadioEarRouting.conf` | 0..1, default 1 | `EC29_VON_VoNComponent.c` per radio packet | **Inverted**: 1 = clean, 0 = fully jammed (script sends `1 - jammerDegradation`). Neutral 1.0 when there is no listener position or the net is a special net |
| `EC29_SignalQuality` | `RadioEarRouting.conf` | 0..1, default 1 | `EC29_VON_VoNComponent.c` per radio packet | RF terrain-propagation quality. 1.0 when RF propagation is disabled server-side, no listener, or special net |
| `EC29_ChannelVolume` | `RadioEarRouting.conf` | 0..1, default 1 | `EC29_VON_VoNComponent.c` per radio packet; `EC29_RadioBeepHelper.c` before every beep | Linear gain. Script sends `v^2.5`, where `v` is the per-radio volume 0..1 (default 1.0, adjusted with `]` / `[`) |
| `EC29_VonRange` | `EC29_LocalVariables_VON.conf` | 0..3, default 1 (`value_max 3`) | `EC29_VON_VoNComponent.c`, pinned to 1.0 once per world | Retired direct-speech gain. Script still looks it up and pins it, so it must still resolve |
| `EC29_SpectatorListening` | `EC29_LocalVariables_VON.conf` | 0..1, default 0 (`value_max 1`) | `EC29_SpectatorVonService.c` (`SyncListeningVar`) | 1 only on a client that is currently spectating; 0 for everyone else |

Script probes the four radio variables once with `AudioSystem.GetVariableIDByName` against
`{3DA1A848EE00C426}` and skips any that does not resolve, so a misspelt name fails
**silently** (that already happened once in this mod's history: the whole radio chain went
dead). Keep the names byte-exact and keep them declared as bare identifiers in the conf.

Vanilla variables the graphs read (do not redeclare):

| Variable | Resource | Used for |
|---|---|---|
| `VON_Radio` | `{A60F08955792B575}Sounds/_SharedData/Variables/GlobalVariables.conf` | event variable on `VON_RADIO` (vanilla) |
| `VON_Direct` | same | event variable on `VON_RAW` (vanilla); vanilla also drives the many-voices ducking curve from it (see 2.1, DECIDE) |
| `VONAmplitude` (local) | `{27F15E80D6E36E07}Sounds/_SharedData/LocalVariables/LocalVariables_VON.conf` | written by the Direct Processing bus; drives mouth animation. Vanilla, must stay on the voice path |

### 1.2 Signal inputs

| Signal input | Where | Note |
|---|---|---|
| `TransmissionQuality` **[contract]** | the Von.sig override | Vanilla input name. Must stay. In the current mod it is fed from `EC29_JamStrength` (see 3.2) |
| Ear-routing input, signal-quality input | the two EC29 sigs | Internal. Names free |

### 1.3 Events (sound names) triggered from script or engine

| Event **[contract]** | ACP | Triggered by |
|---|---|---|
| `VON_DIRECT`, `VON_RADIO`, `VON_RAW`, `SOUND_ROGER_BEEP` | von.acp family | Engine (vanilla names, vanilla meaning) |
| `EC29_BEEP_HIGH` | EC29_beep.acp | `EC29_RadioBeepHelper.c` |
| `EC29_BEEP_LOW` | EC29_beep.acp | same |
| `EC29_CLICK_OFF` | EC29_beep.acp | same |
| `EC29_CLASSIC_START` | EC29_beep.acp | same |
| `EC29_CLASSIC_END` | EC29_beep.acp | same |
| `EC29_SQUELCH_TAIL` | EC29_beep.acp | same (the script comment says this node must exist with this exact name) |

`EC29_VON_VONController.c` also calls `AudioSystem.PlayEventInitialize` on the beep ACP, so
it must load standalone.

Which event plays when (this is script, not graph, but the graph must supply all six):

| Moment | HIGH style | LOW style | CLASSIC style | OFF |
|---|---|---|---|---|
| TX key-down | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_START` | nothing |
| TX key-up | `EC29_CLICK_OFF` | `EC29_CLICK_OFF` | `EC29_CLASSIC_END` | nothing |
| RX squelch open | `EC29_SQUELCH_TAIL` | `EC29_SQUELCH_TAIL` | `EC29_CLASSIC_START` | nothing |
| RX squelch close | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_END` | nothing |
| Style-cycle preview (K) | `EC29_BEEP_HIGH` | `EC29_BEEP_LOW` | `EC29_CLASSIC_START` | silence |

All beeps except the preview are gated by the `RadioBeepsEnabled` user setting (default OFF).
Every beep is played with an identity transform (non-positional) right after script sets
`EC29_EarRouting` (and `EC29_ChannelVolume`) for that radio.

### 1.4 Resource GUIDs **[contract]**

Mod resources (referenced by prefabs, scripts or other graphs):

| GUID | Path | Referenced by |
|---|---|---|
| `{B4C3941EC8B2AD20}` | `Sounds/VON/von.acp` | **Vanilla GUID** (override). Vanilla character / editor prefabs, `Prefabs/Editor/EC29_EditorManager.et` (the `EC29_VoNSpectatorLoud` ear) |
| `{2B1221C55BB515C7}` | `Sounds/VON/EC29_VonWhisper.acp` | `Prefabs/Characters/Core/Character_Base.et` |
| `{451463DBCFB85305}` | `Sounds/VON/EC29_VonNormal.acp` | same |
| `{86558FBD68BF466A}` | `Sounds/VON/EC29_VonYell.acp` | same |
| `{6BA89BD90EA9220F}` | `Sounds/VON/EC29_SpectatorVonQuiet.acp` | `Prefabs/Editor/EC29_EditorManager.et`, comment in `EC29_SpectatorVonTiers.c` |
| `{63926E92E2606681}` | `Sounds/VON/EC29_beep.acp` | `EC29_RadioBeepHelper.BEEP_CONFIG` |
| `{CF1F9C4B120DE5F8}` | `Sounds/VON/EC29_Von.sig` | **Vanilla GUID of `Sounds/VON/Von.sig`** (override). Every ACP that references vanilla Von.sig gets this file, including `EC29_SpectatorVonQuiet.acp` (see 4.6) |
| `{4D6CC8A4D1FED38F}` | `Sounds/VON/EC29_Routing.sig` | von.acp family, beep ACP. Internal: can be renamed or re-GUIDed if every graph is updated together |
| `{D59B06686BE2B58F}` | `Sounds/VON/EC29_SignalQuality.sig` | von.acp family. Internal, same note |
| `{3DA1A848EE00C426}` | `Sounds/VON/RadioEarRouting.conf` | `EC29_VON_VoNComponent.c`, `EC29_RadioBeepHelper.c`, graphs |
| `{33A27275C95E0302}` | `Sounds/VON/EC29_LocalVariables_VON.conf` | `EC29_VON_VoNComponent.c`, `EC29_SpectatorVonService.c`, graphs |

Sample files used by the beep ACP. All ten shipped wavs were regenerated in-house by
`Tools/generate_sounds.py` at `fe32eb5` with GUIDs unchanged, so these are **KEEP**:

| GUID | Path | Event |
|---|---|---|
| `{1ADFC1E8AF584F50}` | `Sounds/EC29_Beeps/beep_high.wav` | `EC29_BEEP_HIGH` |
| `{A08EB2587E7B515E}` | `Sounds/EC29_Beeps/beep_low.wav` | `EC29_BEEP_LOW` |
| `{E43AE4FF96A1BFE6}` | `Sounds/EC29_Beeps/click_off.wav` | `EC29_CLICK_OFF` |
| `{FFCC0497F888301F}` | `Sounds/VON/EC29_FX/StartRadioTransmissionBeep.wav` | `EC29_CLASSIC_START` |
| `{A5C6D2BF6A2C871D}` | `Sounds/VON/EC29_FX/EndRadioTransmission2.wav` | `EC29_CLASSIC_END` |
| `{A79CA9FE93FE6EAB}` | `Sounds/EC29_Sound/squelch_tail_01.wav` | `EC29_SQUELCH_TAIL` |

Vanilla resources the graphs reference (all already present in the vanilla von.acp unless
noted): `{CF1F9C4B120DE5F8}` Von.sig, `{2DE6099BBD60C620}` Character_Occlusion.sig,
`{76CBB40EF7F227B6}` Reverb_Base.sig, `{B764D803219C775E}` FinalMix.afm,
`{9F8145144171C64B}` Amplitude_-40LUFS_to_-35LUFS.conf (vanilla direct amplitude),
`{299E65568A14273A}` Amplitude_-45LUFS_to_-30LUFS_Character.conf (**not** in vanilla von.acp;
used by the current direct amplitude and the spectator amplitude, see 2.1),
`{85F92D98658CB0F5}` Frequency_Character_Voice.conf, the six vanilla roger-beep wavs,
`{9F3AF591CD6DFE1A}` listener window model.

---

## 2. Per-file breakdown

### 2.1 `Sounds/VON/von.acp` (vanilla GUID override)

**VANILLA, unchanged (start from vanilla and leave alone):**

- Direct voice path: Stream (26641), Bus Direct Processing (52231: -5 dB, writes
  `VONAmplitude`), its filter chain 53271 (HP 54294 220 Hz, LP 58390 8 kHz, low shelf 62486
  150 Hz -6 dB, compressor 39958), Shader Direct (22546) with Spatiality 48156, Frequency
  78869 fed by Signal Occlusion 66573.
- `VON_RAW` (56329) and Shader RAW (57362).
- Reverb send: Signal Reverb 77837, AuxOuts 81949 / 82973 / 83997, OutputState mixer 13323.
- Radio filter chain DSP settings: Bitcrusher 33814 (mask and static settings), HP 63510
  (1200 Hz, Q 2, +2 dB), plus the static Q/gain of LP 64534 (Q 1.8, +2 dB) and the clipper
  type of 88086 (hard clipper). Their *modulation* is REBUILD (below).
- Brownian noise generator 36869 (its static level -14 dB).
- Roger-beep banks 40964 / 41988 and their samples.
- `VON_DIRECT` (49161), `VON_RADIO` (27657) and `SOUND_ROGER_BEEP` (45065) as events and
  their output routing.

**KEEP (29th-original), the spectator gate around `EC29_SpectatorListening`:**

- Two gate controls from `EC29_SpectatorListening` (L): an "off" gain of `1 - L` and an "on"
  gain of `L` (linear, L in 0..1).
- Radio path for a spectating client: the whole degraded-radio sound (voice through the
  radio filter chain plus the noise bed) is multiplied by `1 - L`. In parallel a
  **clean-voice** branch, multiplied by `L`, takes the raw decoded stream through the
  vanilla *direct* filter chain (53271: the same HP / LP / shelf / compressor) at -10 dB, with
  no radio chain, no noise and no distance attenuation. Both branches then go through the ear
  routing and channel volume stages, so a spectator-net voice still honours ear routing and
  per-channel volume.
- Direct path: Shader Direct's output is multiplied by `1 - L` before the vanilla ducking
  bus. A second direct shader ("Spectator Shader Direct": same spatiality and frequency nodes
  as Shader Direct, but its own AmplitudeClass **200010**, parent
  `Amplitude_-45LUFS_to_-30LUFS_Character`, innerRange 40 / outerRange 68) is multiplied by
  `L` and feeds the same ducking bus. `VON_DIRECT` takes both shaders.
- Net effect: for everyone but a spectating client, L = 0 and the graph sounds exactly like
  the non-spectator graph. A spectating client hears direct speech at full volume to 40 m
  fading to 68 m, and spectator-net radio as clean speech.
- The comment strings on these nodes are 29th prose and can be kept.

**REBUILD (506th-derived), radio path. Behavior in 3.1 to 3.4:**

- Swapping the Signal 1 sig to the EC29 Von.sig, wiring `EC29_JamStrength` into its
  `TransmissionQuality` input, and the extra outputs that sig provides.
- Splitting vanilla's "Bus Voice + Noise" into a voice-only path and a noise-only path, the
  extra pink-noise generator, and the voice-path gain law.
- RF signal-quality modulation of the radio clipper drive, radio low-pass cutoff and the
  `VON_RADIO` event gain.
- Per-channel volume stage and stereo ear-routing stage on the radio path, and Shader Radio
  feeding them.
- `SOUND_ROGER_BEEP` attenuated to -60 dB (the vanilla roger beep is effectively muted; the
  mod's own beeps replace it).
- Incidental differences that came along with the derived file and that have no
  established purpose: the radio filter chain declares 5 ports (vanilla 4, still only 4
  filters), Shader Radio carries an explicit `Enable 1` plus an extra parameter flag, and the
  default (preview) input values of Signal Occlusion (first input 1 -> 0) and Signal Reverb
  (second input 1 -> 0) differ from vanilla. Rebuild from the vanilla values unless an A/B
  shows a difference (see 5).

**DECIDE (WCS-derived), direct path:**

- Vanilla's "Bus Ducking When Many Voices" (84999) is driven by `VON_Direct` through a curve
  1 -> 1.0, 2 -> 0.73 (exponential, modifier 3), 5 -> 0.501, i.e. vanilla ducks direct speech
  when several voices play at once. The current graph replaces that control with
  `EC29_VonRange` through an identity curve (0 -> 0, 3 -> 3). Since `EC29_VonRange` is pinned to
  1.0, **the current behavior is a constant unity gain on that bus: no many-voices ducking.**
- AmplitudeClass **23571** (fed by Shader Direct 22546) has its parent swapped from vanilla
  `Amplitude_-40LUFS_to_-35LUFS` to vanilla `Amplitude_-45LUFS_to_-30LUFS_Character`, with no
  overrides in von.acp itself. Per the 2026-09-12 graph reading in the brain notes and
  `EC29_SpectatorVonTiers.c`, that parent gives a 1/r curve, outerRange 40, slopeFactor 11
  [not re-read from the conf in this session]. That inherited 40 m is the direct-speech wall
  a living listener hears through the stock (ear) component.
- `EC29_VonRange` declared in `EC29_LocalVariables_VON.conf`.

### 2.2 `EC29_VonWhisper.acp`, `EC29_VonNormal.acp`, `EC29_VonYell.acp`

Each is a full copy of the current von.acp (so they inherit every VANILLA / KEEP / REBUILD /
DECIDE part above and must be regenerated from the rebuilt von.acp). Verified by diff in this
session: the **only** differences from von.acp are:

| File | AmplitudeClass 23571 | Other |
|---|---|---|
| Whisper | innerRange 2, outerRange 6 | none |
| Normal | innerRange 15, outerRange 20 | none |
| Yell | innerRange 50, outerRange 80 | `volume_dB 6` on the `VON_DIRECT` event |

These overrides are **KEEP** (29th-original, `Docs/DirectSpeechTiers.md`). AmplitudeClass
200010 is deliberately left alone in all three, so spectators hear every tier at full volume.

### 2.3 `EC29_SpectatorVonQuiet.acp` (KEEP)

Diffed against the vanilla von.acp (not against EC29's): it is vanilla plus three 29th
changes:

- Vanilla's "Bus Voice + Noise" loses its radio filter-chain input (radio voice is
  unprocessed).
- The Brownian noise generator is at -100 dB (no noise bed).
- AmplitudeClass 23571: innerRange 0, outerRange 0.0001 (inaudible in the world).

It references vanilla Von.sig by GUID `{CF1F9C4B120DE5F8}`, which resolves to EC29's
override. No other changes needed.

### 2.4 `EC29_Von.sig` (overrides vanilla Von.sig GUID `{CF1F9C4B120DE5F8}`)

Vanilla structure: one input `TransmissionQuality` (default 1) and three outputs,
`Quality_W`, `Noise_V`, `Radio_V`.

- **VANILLA:** the input and the three output names and their roles.
- **REBUILD:** changed curve endpoints on the three vanilla outputs, and three added outputs
  (a voice-path gain, plus a low-pass and a high-pass cutoff curve that nothing consumes).
  Behavior in 3.2.

### 2.5 `EC29_SignalQuality.sig` (REBUILD)

Entirely derived. One input (RF signal quality) and three outputs: radio clipper drive, radio
low-pass cutoff, radio event gain. Behavior in 3.3.

### 2.6 `EC29_Routing.sig` (REBUILD)

Entirely derived. One input (ear-routing enum), two outputs (left-channel gain, right-channel
gain). Behavior in 3.4. Used by von.acp family and by the beep ACP.

### 2.7 `EC29_beep.acp` (REBUILD graph; KEEP samples and event names)

The graph is derived. Event names (1.3) and sample files (1.4) are contract / in-house.
Behavior in 3.5.

### 2.8 `RadioEarRouting.conf` (REBUILD, trivial)

Derived, but it is only a declaration of the four radio variables in 1.1 with their defaults.
Re-declare them as in 1.1. The GUID and path are contract.

### 2.9 `EC29_LocalVariables_VON.conf`

- `EC29_SpectatorListening` (0..1, default 0): **KEEP**.
- `EC29_VonRange` (0..3, default 1): **DECIDE** (WCS-derived). Script still resolves and pins
  it, so if it is dropped from the graph it should still be declared, or the script lookup
  must go with it.

---

## 3. Behavior of the REBUILD parts

Notation: q = a 0..1 control, curves are linear and clamped at their endpoints unless noted.
"Gain" means a linear amplitude multiplier.

### 3.1 Radio path, end to end

Order from source to output (the derived graph's order, which is what produces today's
sound):

1. The decoded voice stream goes through the vanilla Direct Processing bus (-5 dB and the
   vanilla direct EQ / compressor chain). Vanilla also does this.
2. It then splits into two parallel sub-paths that both run through the vanilla radio filter
   chain (bitcrusher, HP 1200 Hz, hard clipper, LP):
   - **Voice sub-path**: the processed voice, gain = voice gain (3.2).
   - **Noise sub-path**: two noise generators, Brownian (vanilla) and Pink (added), each with a
     static level of -14 dB (linear 0.2) and each scaled by `Noise_V` (3.2). The whole noise
     sub-path is scaled by `Radio_V` (3.2).
   In vanilla the voice and the Brownian noise share one bus scaled by `Radio_V`. The derived
   version gives the voice its own gain law and adds a pink-noise layer at the same level.
3. (29th KEEP gate: both sub-paths x `1 - L`; the clean spectator branch x `L` joins here.)
4. **Ear routing stage**: forced stereo (2 channels), left / right gains from 3.4.
5. **Channel volume stage**: gain = `EC29_ChannelVolume` directly (no curve in the graph; the
   2.5 power law is applied in script). dB equivalents for the per-radio setting v:
   v = 1.0 -> 0 dB, 0.75 -> -6.2 dB, 0.5 -> -15 dB, 0.25 -> -30 dB, 0.1 -> -50 dB, 0 -> silent.
6. Shader Radio (no distance attenuation, as vanilla) -> `VON_RADIO` event, whose gain is the
   RF "radio event gain" from 3.3. `VON_RADIO` still also takes the vanilla Shader Direct input,
   as in vanilla.

Radio filter chain parameters that move (both sub-paths share the chain):

| Parameter | Vanilla (static) | Driven by | Range |
|---|---|---|---|
| Bitcrusher wet amount | from `Quality_W` | `TransmissionQuality` | see 3.2 |
| Hard-clipper drive | 20 | `EC29_SignalQuality` | 80 at q=0 -> 20 at q=1 |
| Low-pass cutoff | 3600 Hz | `EC29_SignalQuality` | 1000 Hz at q=0 -> 3600 Hz at q=1 |

The HP 1200 Hz filter is not modulated.

### 3.2 Jamming: `EC29_JamStrength` -> `TransmissionQuality` (the Von.sig override)

The jammer variable is wired into the Von.sig `TransmissionQuality` input, so in the radio
graph TQ = `EC29_JamStrength` (1 = clean, 0 = fully jammed).

| Output | Vanilla | Required (current) behavior |
|---|---|---|
| `Quality_W` (bitcrusher wet amount) | 0.85 at TQ=0 -> 0.2 at TQ=1 | **1.0** at TQ=0 -> 0.2 at TQ=1, linear |
| `Noise_V` (noise generator level) | 1.0 at TQ=0 -> -60 dB (0.001) at TQ>=0.8, "power of 1/3" fade shape | **1.8** at TQ=0 -> -60 dB (0.001) at TQ>=0.8, same fade shape |
| `Radio_V` (noise sub-path gain) | -12 dB (0.25) at TQ=0 -> 1.0 at TQ=1 | **-10 dB (0.316)** at TQ=0 -> 1.0 at TQ=1, linear |
| voice gain (new output) | n/a (voice used `Radio_V`) | 0 at TQ=0 -> 1.0 at TQ=1, linear |
| low-pass and high-pass cutoff curves (new outputs) | n/a | 1000 -> 3600 Hz and 3400 -> 1200 Hz over TQ 0..1. **Not connected to anything** in any graph. Omit unless wanted |

Audible result: at TQ = 1 (no jammer) voice is at full gain, the noise bed is about -60 dB
under its static level (inaudible), and the bitcrusher is at 20 % wet. As jamming increases
the voice fades linearly to silence, the noise bed rises steeply (cube-root shape) to 1.8x its
static level, and the bitcrusher goes fully wet. A fully jammed channel is noise only.

The vanilla curve values (the "Vanilla" column) were read from the extracted vanilla Von.sig.
The interpolator's min/max inputs are taken to be the Y endpoints, which is consistent with
both the vanilla and the current files [inferred, not documented by the engine].

### 3.3 RF propagation: `EC29_SignalQuality`

| Control | q = 0 | q = 1 | Shape |
|---|---|---|---|
| Radio hard-clipper drive | 80 | 20 | linear |
| Radio low-pass cutoff | 1000 Hz | 3600 Hz | linear |
| `VON_RADIO` event gain | 0 | 1.0 (reached at q = 0.3) | linear over 0..0.3, flat 1.0 above |

Audible result: good links (q >= 0.3) are at full level and only gain grit and a duller top
as q falls; below 0.3 the whole received radio sound (voice and noise) fades to silence at
q = 0, while getting harsher (drive up to 4x vanilla) and band-limited to 1 kHz.

### 3.4 Ear routing: `EC29_EarRouting`

| Value | Meaning | Left channel | Right channel |
|---|---|---|---|
| 0 | CENTER | pass | pass |
| 1 | RIGHT | muted | pass |
| 2 | LEFT | pass | muted |

- Hard routing. There is no pan law: the active ear gets the same gain whether the radio is
  routed to one ear or centered.
- The "pass" value the derived sig emits is 2.0, and "muted" is 0 [the mute value is
  inferred from the converter behavior; see 5]. If the channel-volume input is not clamped,
  2.0 is a +6 dB boost per ear. An equivalent rebuild must match the current loudness, so A/B
  the centered level against the current build.
- The same routing stage (same mapping) is applied to every beep event in 3.5.
- Known engine limitation, accepted: the variable is global and last-writer-wins per packet,
  so two nets transmitting at once can flicker between ears (`Docs/EarRouting_Defaults.md`).

### 3.5 Beep ACP `EC29_beep.acp`

Six events, each one sample -> stereo ear-routing stage (3.4, from `EC29_EarRouting`) ->
default output. Non-positional. No noise, no filtering, no jamming or RF modulation. The
graph does **not** consume `EC29_ChannelVolume` (script sets it anyway; the script comment
says wiring it in was intended but never done). The events have no explicit output-state
assignment in the current graph.

| Event | Sample | Event level |
|---|---|---|
| `EC29_BEEP_HIGH` | beep_high.wav | -12 dB |
| `EC29_BEEP_LOW` | beep_low.wav | -12 dB |
| `EC29_CLICK_OFF` | click_off.wav | -15 dB |
| `EC29_CLASSIC_START` | StartRadioTransmissionBeep.wav | **+6 dB** |
| `EC29_CLASSIC_END` | EndRadioTransmission2.wav | -3 dB |
| `EC29_SQUELCH_TAIL` | squelch_tail_01.wav | -12 dB |

No volume or pitch variance and no silence padding on any bank. Bank labels in the current
file still carry legacy third-party names; those are internal and should not be carried over.

---

## 4. Invariants a rebuild must not break

1. **Class-name / first-VoN-component rule.** An entity orders its components by class name,
   and the engine plays an incoming stream through the **first** VoN component on the entity,
   gated by that component's ACP range. On the editor manager the ear is
   `EC29_VoNSpectatorLoud` (stock `von.acp`, `{B4C3941EC8B2AD20}`); any new VoN class on the
   manager must sort after it. On characters the stock `SCR_VoNComponent` (stock von.acp) is
   the ear; the tiers are named `SCR_VoNComponent_EC29Whisper/Normal/Yell` so they sort after
   it, and must never take an `EC29_` prefix. Consequence for graphs: **von.acp is what every
   living listener and every spectator hears through.** The tier ACPs only shape what a
   speaker transmits.
2. **Shader Direct 22546 feeds AmplitudeClass 23571.** That amplitude is the per-speaker
   direct-speech range, and it is the only node the tier ACPs override (2/6, 15/20, 50/80 m).
   AmplitudeClass 200010 belongs only to the spectator shader and the tiers must leave it alone.
   Do not re-number, merge or re-route these. The yell `volume_dB 6` sits on the `VON_DIRECT`
   event, not on the amplitude.
3. **Tier ACPs are copies of von.acp.** Any change to von.acp has to be repeated in all
   three, which differ only by 2.2. The script mirrors of the outer ranges
   (`EC29_VoiceTiers.c`: `WHISPER_OUTER_M 6`, `NORMAL_OUTER_M 20`, `YELL_OUTER_M 80`) must
   change together with the ACPs.
4. **Spectator gate is neutral at L = 0.** With `EC29_SpectatorListening` = 0 the graph must
   sound exactly like the non-spectator graph, because that is every non-spectating client.
   Spectator direct = full volume to 40 m, fade to 68 m; spectator-net radio = clean voice at
   -10 dB through the direct EQ chain, still ear-routed and channel-volume scaled.
5. **Vanilla GUID overrides stay on vanilla GUIDs.** `{B4C3941EC8B2AD20}` (von.acp) and
   `{CF1F9C4B120DE5F8}` (Von.sig). Re-GUIDing an override silently kills it; this happened
   once in this mod's history.
6. **The Von.sig override is shared.** It replaces vanilla Von.sig for every ACP that
   references it, including `EC29_SpectatorVonQuiet.acp` and possibly other vanilla ACPs
   [other vanilla consumers not checked in this session]. Keep the vanilla input name and the
   three vanilla outputs with their vanilla roles. Added outputs are fine.
7. **Variable names are resolved by string and fail silently.** See 1.1. Declare them as bare
   identifiers in the confs. A past rename pass missed unquoted conf class names and the
   radio chain went dead.
8. **`VONAmplitude` must stay on the direct path** (Bus Direct Processing). Mouth animation
   for every tier depends on it (`Docs/DirectSpeechTiers.md` open item 4).
9. **Ear-routing enum is CENTER = 0, RIGHT = 1, LEFT = 2**, and the beep ACP uses the same
   routing as voice, so a radio's beeps land in the same ear as its traffic.
10. **Neutral values mean clean.** `EC29_JamStrength` = 1 and `EC29_SignalQuality` = 1 must
    give an undegraded radio (script writes these for special nets, the spectator net, dead
    listeners and RF-disabled servers). `EC29_ChannelVolume` = 1 must be 0 dB.
11. **`SOUND_ROGER_BEEP` stays effectively silent** (-60 dB). Otherwise players hear vanilla's
    roger beep on top of (or instead of) the opt-in EC29 beeps.
12. von.acp was byte-identical between Reforger 1.7 and 1.8. The override strategy relies on
    vanilla's node ids staying stable. Keep vanilla ids for vanilla nodes so the next
    game-update hash diff stays meaningful.

---

## 5. Ambiguities and open decisions

1. **WCS-derived parts (DECIDE).** The direct-path ducking replacement (2.1), the 23571
   parent choice, and `EC29_VonRange` came from the WCS voice mod (commit `74fe43b`, verified:
   they are present in the first commit and absent from both the vanilla and the 506th files).
   This clean room was briefed on 506th only. Options: rebuild them from 2.1 (constant unity
   ducking bus, Character -45/-30 parent), or restore vanilla (many-voices ducking back,
   -40/-35 parent). Restoring vanilla changes how direct speech sounds and may move the 40 m
   wall and the tier curve shape, so the tier ranges would need a field re-check.
2. **Ear-routing gain value.** The derived routing sig emits 2.0 on the "pass" channel. It is
   not known whether the bus clamps channel gain to 1.0. Match by A/B, not by number.
3. **Interpolator and converter port semantics** are inferred from file reading (Y endpoints
   on the min/max inputs; converter = 0 inside its interval, default value outside). The
   vanilla Von.sig behaves sensibly under the same reading, which supports it.
4. **Does wiring `EC29_JamStrength` into `TransmissionQuality` replace the engine's own value?**
   In vanilla nothing is wired to that input, so presumably the engine feeds it. The current
   graph overrides it with the variable. Reproduce the override for equivalence. Whether the
   engine's range-based quality is being discarded is untested.
5. **Incidental derived differences** (extra empty filter-chain port, Shader Radio
   `Enable 1` and extra flag, occlusion and reverb preview defaults 1 -> 0). No known purpose.
   Rebuild with vanilla values and A/B. The occlusion and reverb inputs are probably
   engine-driven at runtime.
6. **Unused outputs.** The low-pass and high-pass cutoff curves in the Von.sig override are
   computed but never connected. They can be omitted.
7. **Beep channel volume.** `EC29_ChannelVolume` is set before every beep but the beep graph
   ignores it. To stay equivalent, leave it unwired. Wiring it in is a behavior change for
   Nathan to decide.
8. Spectator direct hearing beyond 40 m is an open field question
   (`EC29_SpectatorVonTiers.c` header): it is not known whether the engine stops delivering
   direct packets near 40 m. Do not tune 200010 as part of the rebuild.
