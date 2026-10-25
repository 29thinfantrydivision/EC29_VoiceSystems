# Per-stream radio ear routing and channel volume

Design for making left/right/both routing and per-net volume correct when two nets talk at
once. Research prototype lives on branch `ear-routing-research`; nothing here has run in game.
Evidence is the Ghidra dump `C:\tools\ghidra_out_von` (ArmaReforgerSteamDiag.exe) plus vanilla
data read from the paks. Function addresses are Ghidra names.

## The problem, restated from the engine

- One `VonStream` per SPEAKER on the listener's receiving VoN component (`FUN_1402f22d0`), each
  with its own compiled instance of von.acp (`FUN_1408cb700`).
- `AudioSystem.SetVariableByName` writes are config-wide: `EC29_EarRouting` / `EC29_ChannelVolume`
  are one value for every stream, so the last packet processed wins for everybody.
- The only per-stream inputs a stream has are six named graph inputs, bound by name at stream
  creation (`FUN_1408cb700` -> `FUN_140b22c90(template+0x28+0x1f0, name)`): `TransmissionQuality`,
  `distance`, `Interior`, `RoomSize`, `UnderPlayerControl`, `IsInPlayerVehicle`. The name list is
  hardcoded in the engine; the ACP decides whether a .sig exposes an input with that name. A custom
  input name is never bound.
- Every packet, `FUN_1408cc3b0` sets `TransmissionQuality` from the packet and copies the other
  four from the SPEAKER's entity: `FUN_1401f2030(playerManager, playerId)` -> controlled entity
  (the local proxy), `FindComponent(0x51f4424d)` = SignalsManagerComponent (same type hash used by
  the native Interior updater `FUN_1405ad680` next to `FindSignal("Interior")`), then
  `FUN_14018b380(name)` = FindSignal by NAME and `FUN_14018b600(idx)` = GetSignalValue. If the
  graph has no input with that name the set is a no-op (`FUN_140b450f0` bounds-checks the index).
- `distance` is written per frame from stream position (`FUN_1402f4ed0` -> `FUN_1408cc580`). The
  per-frame per-stream update `FUN_1408cc610` only ticks the 2 s timeout; no signal copy per frame.

## Recommended mechanism: hijack the speaker's `RoomSize` signal

On the listener's machine, in `OnReceive`, write an encoded value into the SPEAKER proxy's
SignalsManagerComponent signal `RoomSize`. The next packet copies it into THAT speaker's stream
only. A new signal graph in von.acp, `EC29_StreamRouting.sig`, has one input named `RoomSize` and
decodes it into LeftVol / RightVol / ChannelVol for the radio chain.

Encoding (script `EC29_EncodeStreamSignal`): `value = 10*ear + 9*volume`, ear 1 = both, 2 = right,
3 = left, volume 0..1. Written range is [10, 39]. 0 means "not encoded" (direct speech packets write
0) and decodes to both ears at full volume.

### Why RoomSize and not the other three

| Signal | Native writer on a REMOTE proxy | Vanilla consumers on a character | Verdict |
|---|---|---|---|
| `Interior` | `FUN_1405ad680` (async raycast job `FUN_1405ada70`) writes `Interior` on ONE entity at job+0x38; strrefs show no other entity-level writer. Which entity that is was not proven - likely the listener's [unverified]. | Character_Occlusion.sig, Weapon_Occlusion.sig, Reverb_Base/Reverb_Character.sig - all interpolate it 0..1 and `Max` it with GInterior | Semantically loaded; values outside 0..1 change occlusion. Rejected. |
| `UnderPlayerControl` | Only for the locally controlled character: `FUN_140425c50` (controller/possession change, byte +0x1e7 bit 3) and `FUN_140424dd0` (vehicle enter/leave, compares against the component's own entity at +0xf0); by index 0x1c in `FUN_140422910` / `FUN_140422be0`. Proxies stay at default. | Occlusion sigs use `1 - UnderPlayerControl` as a gate; Weapon_UnderPlayerControl_1p_3p_Cam.sig | Any non-zero value flips the proxy's own foley/weapon graphs into "this is the player" mode. Rejected. |
| `IsInPlayerVehicle` | Same local-only setters (`FUN_1407117f0`), plus a per-frame by-index write at idx 0x1a in `FUN_14080aca0` (which component that is was not resolved). | Occlusion + Reverb_Character gates (`1 - IsInPlayerVehicle`) | Same gate problem, plus a possible per-frame clobber. Rejected. |
| `RoomSize` | None found: no by-name writer (DecompStrRefs over `RoomSize`: only the registration `FUN_1402e8e90`, `FUN_1408cb700`, `FUN_1408cc3b0`) and no by-index writer (idx 0xb absent from all 32 callers of `FUN_1402e9c40`). Sound-instance level "Roomsize" writes (`FUN_1402dc900`, `FUN_141410900`, `FUN_14164c300`) target sound inputs, not the entity signal. | Character_Occlusion / Weapon_Occlusion: `Condition GRoomSize != RoomSize`; Reverb_Base / Reverb_Character: `Condition GRoomSize == RoomSize` plus m3 interpolations starting at "Small Start" 100. | Values in [2, 99] are indistinguishable from 0 to every vanilla consumer: below 100 m3 every interpolation clamps to its X-min output, and GRoomSize is an External 0..1 variable (GlobalVariables.conf) so equality never happens. Chosen. |

Remaining caveat on RoomSize: writers that call `FUN_14018ca80` (SetSignalValue) directly with a
cached index were not enumerated. The VERBOSE readback line in the prototype catches a clobber in
the field (see test plan).

### Where it branches radio vs direct

`VON_RADIO` (sound 27657) is fed by Shader Radio 46098 AND Shader Direct 22546; `VON_DIRECT`
(49161) by Shader Direct only. The new signal's outputs go only into the radio chain:
Routing Bus 94215 con:2/con:3 (LeftVol/RightVol) and Channel Control Volume 107527 con:0, both
under Shader Radio. Shader Direct's occlusion (Signal Occlusion 66573, frequency 78869) also reads
`RoomSize` because the engine binds by name, but by the table above our values read as 0 there.
Direct speech from the same speaker (packet type 0, `FUN_1408cbc60`) therefore keeps vanilla
behaviour, and so does the direct component of a nearby radio talker.

### Timing

`Rpc_ReceiveVoiceData` (`FUN_1402f2e70`) calls `FUN_1408cbc60` (play + `FUN_1408cc3b0` signal copy)
and only then `FUN_1402f43f0`, which dispatches the script event at slot `DAT_1422d5474` =
`"OnReceive"` (registered in `FUN_1402f04c0`). So a write in `OnReceive` for packet N is heard from
packet N+1. The proxy keeps the value between transmissions, so the one-packet lag only matters when
the same speaker changes net or the listener changes routing/volume mid-transmission.

### Script hook

`Scripts/Game/VON/EC29_VON_VoNComponent.c`:
- `OnReceive`: radio packets call `EC29_WriteStreamSignal(playerId, EC29_EncodeStreamSignal(receiver))`;
  direct packets write 0.
- `EC29_WriteStreamSignal` caches SignalsManagerComponent + signal index per playerId (re-resolved on
  entity change, like `EC29_GetVoNForPlayer`), uses `AddOrFindSignal("RoomSize", 0)` so a proxy
  without the signal still works, skips unchanged values, logs on VERBOSE.
- Caches are cleared in `EC29_CheckWorldReset`.
The old `EC29_EarRouting` / `EC29_ChannelVolume` variables are still written; they are now unwired
in von.acp and can be deleted once the field test passes. `EC29_SignalQuality` and `EC29_JamStrength`
stay global (unchanged behaviour, still last-writer-wins).

### ACP / graph changes

- New `Sounds/VON/EC29_StreamRouting.sig` (+ .meta with a hand-made GUID `5E29A1C4D7B30F11`;
  Workbench may want to re-import it). Input `RoomSize`; Convertors decode the tens digit into
  LeftVol / RightVol (Default 1) and an `EarBase` (Default -9), `Sub` leaves 9*volume,
  Interpolate X 0..9 -> 0..1, Pow 2.5 (same curve as `EC29_GetChannelVolumeForTransceiver`).
- `von.acp`: SignalClass 200020 for the new sig; Routing Bus 94215 con:2/3 now from 200020:30/31
  instead of 95245:22/2; Channel Control Volume 107527 con:0 from 200020:32 instead of variable
  108574. Signal 95245 and variables 98334/108574 left in place, unconnected.

## Alternatives looked at

- `TransmissionQuality`: per stream, but set server-side per recipient transceiver
  (`FUN_1402f3280` -> transceiver virtual via `FUN_14067eaa0`, cached per sender/radio/transceiver)
  and sent in the packet. Not script-writable on the client. Could carry EC29's propagation model
  only if the server mod could override the native transceiver quality - no hook found.
- Second hijacked signal for `EC29_SignalQuality` / `EC29_JamStrength`: possible later with the same
  trick if a second safe signal exists; `Interior` is the only remaining candidate and it is not
  safe. Could be folded into the RoomSize encoding (more digits) at the cost of graph complexity.
- Per-component ACP on the listener: playback is always the first VoN component found by type on
  the recipient (`FUN_1402f3280`, 0x7363d874), so no per-net component choice exists.
- `distance`: engine-owned per frame; moving the stream position is not available to script.

## Known limits that stay

- One speaker heard on two of your radios is still one stream (one value).
- First packet of a transmission uses the previous value (one-packet lag).
- VON_RADIO's direct component shares the stream; nothing changes there.

## In-game test plan (VERBOSE on)

Setup: listener L with two radios, squad net routed LEFT, platoon net routed RIGHT, platoon volume
at 30 %. Speakers A (squad net) and B (platoon net) at least 100 m away so no direct component.

1. A keys alone. Expect log `[EC29-DBG][StreamSig] speaker <A> signal 'RoomSize' idx=<n> current=0`
   once, then `speaker <A> 'RoomSize' <- 39 (readback 39)`. Audio: left ear only, full volume.
   If `current` is non-zero or a later readback differs from the written value, something else
   writes RoomSize on proxies - record it, the mechanism is then unsafe.
2. B keys alone. Expect `<- 22.7` (right, 30 % -> 10*2 + 9*0.3). Audio: right ear, quiet.
3. A and B key together for 10 s. Expect NO further StreamSig writes (values unchanged) and both
   streams correct at once: A left/full, B right/quiet. This is the case the variables could not do.
4. While both talk, cycle platoon routing to BOTH. Expect one `<- 12.7` line for B only; B moves to
   both ears within one packet; A untouched.
5. A walks to 5 m and talks on the radio, then direct. Expect `<- 0` on the first direct packet.
   Direct voice must sound as before (occlusion, reverb); the radio stream's direct component
   also unchanged. Compare against a stock build if in doubt.
6. Regression: footsteps / weapon sounds of A and B at 5-20 m, indoors and outdoors, with L inside
   and outside a building. No audible change versus before (RoomSize encoded value must be
   invisible to Character/Weapon occlusion and reverb).
7. Spectator net and GM speech (`isSenderEditor`): same as before; the value written is whatever
   the receiver's routing says, and special nets already resolve to CENTER.
8. Dedicated server: no StreamSig lines at all (OnReceive returns before the write when there is no
   local PlayerController).

## Compile status

`wb_validate_scripts` could not run: the Workbench lease was held by another session
(`registered:428-4136b6`). Script changes are unvalidated. ACP and .sig edits were written by hand
and need a Workbench open to confirm the resource loads (watch for
`VoN AudioSourceTemplate compilation failed` in the log, `FUN_1408cb700`).
