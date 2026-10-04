# Clean-room spec: radio jamming, RF propagation, receive squelch

Audience: the engineer reimplementing this subsystem without the current source.
Scope: jammer component + registry + toggle action, the RF propagation model, the RF settings
loader, the game-mode network component (settings replication + key-state relay), and the
receive-side squelch state machine.

Section 1 lists names and types. Those are the interface other files, prefabs and configs bind
to, so keep them. Sections 2 to 4 describe behavior only. Where this spec says "internal", you can
restructure, rename or merge that part however you like as long as the behavior stays the same.

---

## 1. External contract

### 1.1 `EC29_JammerComponent` (+ `EC29_JammerComponentClass`)

- `EC29_JammerComponentClass : ScriptComponentClass`, `EC29_JammerComponent : ScriptComponent`.
  Prefabs reference the class by name.
- `[Attribute]` fields that prefabs override by name. Names, types and defaults must stay the same:

| Field | Type | Default | Editor widget / range | Meaning |
|---|---|---|---|---|
| `m_fRangeConfig` | float | 500 | slider 100..2000, step 50 | jam radius, metres |
| `m_fConeAngleConfig` | float | 180 | slider 10..180, step 5 | full cone angle, degrees; 180 = omnidirectional |
| `m_bActiveConfig` | bool | true | checkbox | active on spawn |
| `m_vEmitterOffset` | vector | 0 0 0 | coords | emitter point offset in entity-local space |

- Replicated state (RplProp). Clients see exactly these three values:
  - active flag (bool), with an on-replication callback that logs (see section 3)
  - effective range (float)
  - effective cone angle (float)
  The field names of these RplProps are internal.
- Public methods that callers use:
  - `bool IsJammerActive()`
  - `void SetJammerActive(bool active)`: works only on the authority (see 2.1)
  - `float GetRange()`: returns the replicated range
  - `float GetConeAngle()`: returns the replicated cone angle
  - `vector GetPosition()`: world position of the emitter
  - `vector GetForwardVector()`: the owner's world forward axis (local +Z)
- The component needs an `RplComponent` on the same entity. Every prefab has one.

### 1.2 `EC29_JammerRegistry`

A plain object (not a component). There is exactly one, owned by `EC29_RadioState` and reached
through `EC29_RadioState.GetInstance().Jammers()`.
- `void RegisterJammer(EC29_JammerComponent jammer)`
- `void UnregisterJammer(EC29_JammerComponent jammer)`
- `float CalculateJammerDegradation(vector receiverPos)`: returns 0 for no effect and 1 for fully
  jammed. The only consumer is `EC29_RadioState.GetJammerStrength(vector)`.
- `int GetActiveJammerCount()`: public, but no current caller. Keep it.
- `static void DebugJammers()`: a Workbench console entry point (its doc comment says to call it
  from the console). Keep the name. The output format is internal.

### 1.3 `EC29_JammerToggleUserAction : ScriptedUserAction`

Prefabs reference it by class name under `additionalActions`, with `ParentContextList` =
`"JammerControlPanel"`. It overrides `Init`, `CanBeShownScript`, `CanBePerformedScript`,
`GetActionNameScript` and `PerformAction`. Nothing beyond the class name is referenced externally.

### 1.4 `EC29_RFPropagationModel`

A plain object owned by `EC29_RadioState` and created fresh each time that state is created.
- `float CalculateSignalQuality(vector txPos, vector rxPos, float frequencyKHz = 0)`: returns a
  value from 0 to 1. Called only from `EC29_RadioState.GetSignalQuality` and
  `EC29_RadioState.GetSignalQualityCached`.
- `static void DebugPropagation()`: a console entry point. Keep the name. The output is internal.
- Everything else in the class is internal: the constants, the helpers, and the scratch buffers.

### 1.5 `EC29_RFPropagationSettings`

- Declared as `[BaseContainerProps(configRoot: true)]`. `Configs/EC29_VONConfig.conf`
  (GUID `{C99D2868A888D3BC}`) has a root object of this exact class name. That root is currently
  empty, so every value comes from the attribute defaults.
- `[Attribute]` fields. The `.conf` binds to these names, so keep them:
  - `bool m_bRFPropagationEnabled`: default "0", category "RF Propagation", checkbox
  - `bool m_bDebugEnabled`: default "0", category "RF Propagation", checkbox
- Static API:
  - `static EC29_RFPropagationSettings GetInstance()`. Called for warm-up from
    `SCR_VONController` (modded) `OnPostInit` and from the VoN audio-variable probe in
    `SCR_VoNComponent` (modded).
  - `static bool IsRFPropagationEnabled()`
  - `static bool IsDebugEnabled()`
  - `static void SetRFPropagationEnabled(bool)` and `static void SetDebugEnabled(bool)`: public,
    no current callers. Keep them.
- The JSON file path is `$profile:EC29_RFPropagation.json`. The keys are `RFPropagationEnabled`
  and `DebugEnabled`, both bool. Server admins already have these files, so the path and key names
  are part of the contract.

### 1.6 `EC29_RFPropagationNetworkComponent` (+ `...Class`)

- `EC29_RFPropagationNetworkComponentClass : SCR_BaseGameModeComponentClass`, and
  `EC29_RFPropagationNetworkComponent : SCR_BaseGameModeComponent`.
- It is attached to the game mode by `Prefabs/MP/Modes/GameMode_Base.et` (a modded override of
  the vanilla game mode, entity ID `56B2B479C6B96951`). The component instance GUID there is
  `{684601EE43B5AC59}`. It must stay a game-mode component, because it relies on the
  `OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)` override.
- Public API:
  - `static EC29_RFPropagationNetworkComponent GetInstance()`: may return null.
  - `static bool IsRFPropagationEnabled()`: false when no instance exists.
  - `static bool IsDebugEnabled()`: false when no instance exists.
  - `void EC29_RelayKeyState(int senderPlayerId, int frequency, float range, bool keyed)`: server
    entry point. Its only caller is `SCR_VONController.RpcAsk_EC29_KeyState` (a client-to-server
    reliable RPC defined in the modded controller, outside this spec).
- Replicated state: two bool RplProps, "RF enabled" and "debug enabled". Each has an
  on-replication callback that logs (see section 3).
- Broadcast RPC (internal name, but it is part of the wire protocol): reliable, receiver =
  Broadcast. Payload is `(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)`.
- `EC29_PendingKeyStop` is a small helper class in the same file (a frequency int plus a queued-at
  ms float). It is internal.

### 1.7 `EC29_RadioRxSquelch`

A plain object owned by `EC29_RadioState` and reached through `EC29_RadioState.GetInstance().Squelch()`.
- `void EnsureTicking()`. Callers: the modded `SCR_VoNComponent` voice-packet path, and the
  broadcast RPC handler.
- `void OnRemoteKeyState(int senderPlayerId, int frequency, float range, bool keyed, vector senderPos)`.
  Caller: the broadcast RPC handler.
- `void OnVoicePacket(int frequency, BaseTransceiver receiver)`. Caller: the modded
  `SCR_VoNComponent`, for every incoming radio voice packet whose sender is not the local player.
- `float EC29_GetLastRadioRxMs(BaseRadioComponent radio)`: returns -1 if the radio has never
  received. Caller: `EC29_RadioReceiverGuard` heartbeat.
- `void EC29_SweepDeadRadioRxRecords()`. Caller: `EC29_RadioReceiverGuard` heartbeat, run before
  each scan.
- `bool Tick(float nowMs)` is public, but only the squelch's own ticker calls it. Treat it as
  internal.
- `EC29_RxChannelState` is the per-frequency state holder. It is internal.

### 1.8 Collaborators outside this spec (do not reimplement; call them)

- `EC29_RadioState`: world-scoped owner. It provides `GetInstance()`, `Squelch()`, `Jammers()`,
  `GetSignalQuality(tx, rx, kHz)`, `GetSignalQualityCached(senderId, tx, rx, kHz)` (per-sender
  cache with a 250 ms TTL, invalidated when the frequency changes), and
  `GetJammerStrength(rxPos)` (which returns the registry degradation).
- `EC29_TokenBucket(capacity, windowMs)` with `TryConsume(nowMs)`. It lives in the
  `EC29_RadioState` file. A full bucket refills linearly at capacity/window. If the clock goes
  backwards, it resets to full.
- `EC29_CoexistenceGuard.ShouldYieldRadio()` and `EC29_CoexistenceGuard.EC29_IsSpecialNet(BaseTransceiver)`.
- `EC29_RadioBeepHelper.PlayRxOpen(BaseTransceiver)` and `PlayRxClose(BaseTransceiver)`. Both are
  null-safe and pick the sound from the per-radio beep type.
- `SCR_VONController.EC29_FindRadioEntryByFrequency(int)` (modded) returns the local VON radio
  entry tuned to that frequency.
- `EC29_Debug.VERBOSE`: a static const bool and the master switch for `[EC29-DBG]` trace lines.
  It is currently `true`.

### 1.9 Downstream consumers of the numbers (contract on meaning)

For each incoming radio packet, the modded `SCR_VoNComponent` writes global audio variables that
the `von.acp` graph reads:
- `EC29_SignalQuality` = cached propagation quality, 0 to 1, where 1 is clean. It is forced to 1.0
  in any of these cases: RF is disabled (according to the network component), the sender has no
  controlled entity, the listener has no controlled entity, or the net is a special net.
- `EC29_JamStrength` = **1 − degradation**, so **1.0 means clean** and 0 means fully jammed. This
  inversion is deliberate. Neutral is 1.0, and it is written whenever the listener has no
  controlled entity or the net is a special net.

Jamming affects **only** this audio variable. It does not block transmission, it does not gate
squelch, and it does not feed the RF model.

---

## 2. Behavior

### 2.1 Jammer component

**Initialisation (post-init).** On the authority, or with replication not running
(offline/editor), copy the three config attributes into the replicated active/range/cone values
and mark them dirty for replication. Then, on every machine (server and every client), register
with the current world's `EC29_JammerRegistry`. In Workbench builds only, also enable the
per-frame event for debug drawing.

On a client, range and cone read 0 until replication delivers them. A range of 0 means no effect.

**Deletion.** Unregister from the current world's registry, then run base deletion.

**Activation.** `SetJammerActive` returns without doing anything when replication is running and
this machine is not the server. On the authority it sets the active flag and marks it dirty.
Clients have no request path. This is intentional (see section 4).

**Replication callback.** When the active flag arrives on a proxy, log a VERBOSE line with the
active, range and cone values.

**Emitter position.** With a zero offset, the emitter is the owner's world origin. Otherwise it is
the owner's origin plus the offset rotated into world space by the owner's orientation (local
X/Y/Z mapped to the owner's right/up/forward axes, with no scale). With no owner, it is the zero
vector.

**Forward.** The owner's world +Z axis. With no owner, it is world forward.

**Workbench debug draw.** Workbench builds only, every frame. It uses the **config** values, not
the replicated ones.
- Omnidirectional (cone ≥ 180): a translucent sphere of radius range at the emitter. It is red
  (alpha 0x40) when active and grey (alpha 0x40) when inactive.
- Directional: a blue arrow from the emitter along forward with length range, plus three
  concentric rings at 1/3, 2/3 and 3/3 of the half-angle. Each ring is 16 thin lines of length
  range. Lines are yellow when active and grey when inactive. All shapes are drawn once and ignore
  the z-buffer.

This draw has no gameplay effect.

### 2.2 Jammer registry and jam geometry

The registry is a de-duplicated list of jammer components. Registering one that is already listed
is a no-op, though it still logs. Unregistering removes the item.

**Degradation at a receiver position P**: the maximum single-jammer effect over all registered
jammers that are non-null and active. Effects do not add up. If no jammer has any effect, the
result is 0.

**Single-jammer effect**, with E = emitter position, R = replicated range and A = replicated cone
angle in degrees:
1. d = 3D Euclidean distance |P − E|. If d ≥ R, the effect is 0.
2. If A < 180: let u = normalize(P − E) and f = jammer forward (3D, so pitch counts). Then
   θ = acos(u·f) in degrees. If θ > A/2, the effect is 0. (θ equal to A/2 is still inside.)
3. Otherwise the effect is 1 − (d/R)². So it is 1.0 at the emitter and falls off quadratically to
   0 at the edge.

Terrain, occlusion and frequency play no part. Every frequency is jammed equally.

**Active count**: the number of registered jammers that are non-null and active.

**`DebugJammers()` (console)**: uses the local player's controlled entity position. It prints the
total registered and, for each jammer, its position, range, cone, active flag, distance, and
whether it is inactive, out of range, outside the cone (with the angle and half-angle), or jamming
(with the ratio, ratio² and degradation). It ends with the worst degradation and the final quality
(1 − worst). It prints an error and stops if there is no player controller or no controlled
entity, and a warning if no jammers are registered. These lines use their own plain-text prefixes,
not `[EC29-DBG]`, and are not gated.

### 2.3 Toggle user action

- On init, look up the `EC29_JammerComponent` on the owner entity and keep it.
- It is shown and performable whenever that component exists. There are no faction, permission,
  distance or rate checks beyond what the engine's action system already does.
- Action name: "Disable Jammer" while active, "Enable Jammer" while inactive, and
  "Toggle Jammer" if the component was not found.
- On perform: run the base behaviour. If replication is running and this machine is not the
  server, stop there. Without a component, stop. Otherwise compute the opposite of the current
  active state, log it (VERBOSE), and call `SetJammerActive(newState)`.

### 2.4 RF propagation model

Input: transmitter and receiver world positions, and a frequency in kHz. A value ≤ 0 means use the
default of **60000 kHz (60 MHz)**. Output: a quality from 0 to 1.

The model does **not** check whether RF is enabled. Callers do that.

Constants:
- speed of light c = 299792458 m/s
- antenna height above terrain 1.5 m
- sample spacing 50 m, at most 200 samples
- minimum distance 50 m, maximum modelled distance 5000 m
- at most 3 knife edges
- Earth radius 6371000 m, k-factor 1.333

Steps:
1. If there is no world, return 1.0.
2. Wavelength λ = c / (f_kHz × 1000). f_MHz = f_kHz / 1000.
3. D = horizontal (XZ) distance between tx and rx. If D < 50, return 1.0. If D > 5000, return 1.0
   (see section 4).
4. Antenna heights: h_tx = terrain surface Y at tx XZ + 1.5, and h_rx = terrain surface Y at rx
   XZ + 1.5. The real Y of the entities is **ignored**, so a player on a roof or in an aircraft
   counts as standing on the ground.
5. Slant distance S = √(D² + (h_rx − h_tx)²).
6. **Free-space path loss** (dB): 0 if S ≤ 1. Otherwise 20·log10(S) + 20·log10(f_MHz) − 27.55,
   clamped to ≥ 0.
7. **Terrain loss** (dB), using a profile along the horizontal tx-to-rx line:
   - N = floor(D / 50), capped at 200. If N < 1, the terrain loss is 0. Because D ≤ 5000, the cap
     never takes effect: N ≤ 100.
   - Sample points sit at d1 = 50·i for i = 1 … N−1, with d2 = D − d1. Neither endpoint is
     sampled.
   - At each sample:
     - terrain surface Y plus earth bulge b = d1·d2 / (2·6371000·1.333)
     - line-of-sight height h_los = h_tx + (h_rx − h_tx)·d1/D
     - first Fresnel radius r = √(λ·d1·d2 / D)
     - excess = (terrain + bulge) − h_los
   - **If excess > 0**, the sample is an obstruction candidate. Keep only the 3 tallest by excess,
     each with its d1 and d2. When excesses are equal, the earlier sample ranks higher.
   - **Otherwise**, it is a Fresnel candidate: clearance = −excess and intrusion = r − clearance.
     Track the single sample with the **largest positive intrusion in metres**, and keep that
     sample's r.
   - **Knife-edge loss**, for each kept obstruction (up to 3) using height h = its excess:
     v = h·√(2·D / (λ·d1·d2)). If d1 or d2 is ≤ 0, the loss is 0. If v < −0.78, the loss is 0.
     Otherwise loss = 6.9 + 20·log10(√((v−0.1)² + 1) + v − 0.1), clamped to [0, 40]. The losses
     add together.
   - **Fresnel loss**: only if the tracked intrusion is > 0 and its r is > 0. Clearance fraction
     c = 1 − intrusion/r. The loss is 3 dB if c < 0.2, 2 dB if c < 0.4, 1 dB if c < 0.6, and 0
     otherwise. It is added on top of the knife-edge losses, whether or not any obstruction
     exists.
8. Total loss L = path loss + terrain loss.
9. **Loss to quality** (piecewise linear):
   - L ≤ 75 gives 1.0
   - 75 < L ≤ 85: 1.0 down to 0.5, i.e. 1 − 0.5·(L−75)/10
   - 85 < L ≤ 92: 0.5 down to 0.2, i.e. 0.5 − 0.3·(L−85)/7
   - 92 < L ≤ 100: 0.2 down to 0, i.e. 0.2 − 0.2·(L−92)/8
   - L > 100 gives 0
10. If `EC29_RFPropagationNetworkComponent.IsDebugEnabled()` is true (the replicated server flag),
    print one line per evaluation showing distance, path loss, diffraction loss and total (rounded)
    plus the quality. See section 3.

Reference points at 60 MHz on flat ground: FSPL is about 68 dB at 1 km (quality 1.0) and about
82 dB at 5 km (quality about 0.65).

**Performance contract.** This function runs on the per-voice-packet path (behind the 250 ms
per-sender cache) and on every accepted key-start. It must not allocate per call. The current code
reuses three member scratch buffers, which is safe because script is single-threaded. Use any
equivalent approach.

**`DebugPropagation()` (console)**: uses a test target 1000 m ahead along the local controlled
entity's forward axis and the default 60 MHz. It prints the positions, terrain and antenna heights,
frequency, wavelength, distance and sample count, the k-factor and effective earth radius, and the
midpoint bulge. It then prints a per-sample profile, where each sample is labelled
BLOCKED (excess > 0), CLEAR (clearance ≥ 100% of r), GOOD (≥ 60%) or PARTIAL. After that come the
obstructions used (up to 3) with their per-edge loss, the Fresnel loss, the total loss and the
quality.

It differs from the live model in four ways, and these are acceptable for a debug tool:
- it skips the 50 m and 5000 m distance gates
- only PARTIAL samples compete for "worst Fresnel intrusion"
- it labels each obstruction with its sample index
- it uses the `[RF Debug]` prefix

It is not gated.

### 2.5 RF settings loader (`EC29_RFPropagationSettings`)

A process-lifetime singleton. It is **not** reset on world change.

- **Client** (replication running and not the server): on first call, create an instance with
  default values (both false) and log a VERBOSE line. Never read or write the profile JSON on a
  client. Clients never consume this instance's values. They read the replicated values on the
  network component instead.
- **Server / offline**, first call:
  1. Create an instance with both flags false.
  2. Try to load `$profile:EC29_RFPropagation.json`:
     - Parse succeeds: for each of `RFPropagationEnabled` and `DebugEnabled` that is present,
       overwrite the matching flag. A missing key leaves the default. Log "loaded from JSON".
       Done; the `.conf` is not consulted.
     - Parse fails **and the file exists**: log an ERROR saying the config exists but failed to
       parse, so fix or delete it, and that defaults are used this run. **Do not overwrite the
       file.** Fall through to the `.conf` step.
     - Parse fails **and the file is missing**: create it with exactly two keys,
       `RFPropagationEnabled: false` and `DebugEnabled: false`, as pretty-printed JSON with
       4-space indent and one key per line. Then fall through to the `.conf` step. If creating the
       file fails, log an ERROR. The current code creates the file in two steps: it opens in
       append mode and closes, then opens in write mode. Any method that reliably creates and
       writes the file is fine.
  3. `.conf` step: load `{C99D2868A888D3BC}Configs/EC29_VONConfig.conf`, instantiate its root as
     this class, and copy both flags. Log "loaded from .conf". If anything in the chain fails, log
     a WARNING that default settings (disabled) are in use.
  4. Only once per process, print a summary line with both flag values.
- `IsRFPropagationEnabled()` and `IsDebugEnabled()` return the flags from `GetInstance()`. They
  return false if there is no instance.
- `SetRFPropagationEnabled(b)` and `SetDebugEnabled(b)` set the flag on `GetInstance()` and log the
  new value. Neither persists to disk nor re-replicates. The network component's RplProps are only
  copied at its post-init.

### 2.6 Network component: settings replication

- On post-init, on every machine, record this component as the static instance.
- On the server, also read both flags from `EC29_RFPropagationSettings`, write them to the two
  RplProps, mark them dirty, and log a "server settings loaded" line.
- On clients, each RplProp callback logs "received server settings" with both values. With two
  props, the line can print twice per sync.
- The static accessors read the replicated values, which on the server are the values it loaded.
  So the server's JSON is what decides RF on/off and RF debug for everyone.

### 2.7 Network component: key-state relay (server)

Purpose: tell every receiver the moment a transmitter keys or unkeys a radio, so squelch works
even with no voice packets (a "dead key").

`EC29_RelayKeyState(senderPlayerId, frequency, range, keyed)`:
1. Does nothing unless this machine is the server.
2. **Validation**: reject (WARNING, not gated) any frequency below 1000 kHz or above
   1,000,000 kHz. This applies to both starts and stops. Clamp range to [0, 50000] m.
3. **Rate limit (key-starts only)**: each player has a token bucket of capacity **10** and window
   **4000 ms**, so it refills at 2.5 tokens/s. It is created on first use, and the clock is world
   time in ms. A start with no token available is dropped with a WARNING, which is not gated. Stops
   never consume tokens and are never rate-limited (see section 4).
   A re-key on the same frequency inside the debounce window still spends a token, because the
   check happens before the debounce check.
4. Log the accepted relay (VERBOSE).
5. **Key-start**:
   - If a stop for this player is pending, cancel it.
     - If the pending stop was for the **same** frequency, broadcast nothing and return. Receivers
       never saw a close, so the transmission continues seamlessly.
     - If it was for a **different** frequency, broadcast that old frequency's stop immediately,
       with range 0.
   - Record this player as keyed on this frequency, then broadcast the start with the supplied
     (clamped) range.
6. **Key-stop**: do not broadcast yet. Store or replace the player's pending stop (frequency plus
   the current world time) and schedule a flush **300 ms** later.
7. **Flush** (scheduled):
   - If there is no pending stop for the player, do nothing. It was cancelled.
   - If there is no world (teardown), do nothing.
   - If the pending stop was queued less than **250 ms** ago (300 ms minus a 50 ms jitter margin),
     do nothing. A newer stop replaced it and has its own flush scheduled.
   - Otherwise remove the pending stop, remove the keyed record, and broadcast the stop with
     range 0.
8. **Broadcast**: stamp the sender's position, which is the origin of the player's controlled
   entity, or the zero vector if there is none. Send the reliable broadcast RPC **and** run the
   handler locally, so a listen-server host is also served.
9. **Disconnect** (server, game-mode hook): if the player is recorded as keyed, relay a key-stop
   for that frequency through the same path (debounced; it costs no token). Always drop that
   player's rate-limit bucket.

Broadcast RPC handler (every machine that receives it, including the server's local call):
- If `EC29_CoexistenceGuard.ShouldYieldRadio()`, ignore it.
- Otherwise call `Squelch().OnRemoteKeyState(...)` on the current `EC29_RadioState`.
- Then, only if a local PlayerController exists, call `Squelch().EnsureTicking()`. A dedicated
  server never ticks.

Client side, for context only (it lives in the modded `SCR_VONController`): starts send the
transceiver's own range. A key-start on a new frequency while keyed on another first sends the old
frequency's stop. The client also has its own key bucket of capacity 4 per 4000 ms.

### 2.8 Receive squelch state machine (client-side; no-op without a local PlayerController)

Timing constants:

| Constant | Value |
|---|---|
| tick interval | 150 ms |
| voice silence timeout | 600 ms |
| reopen grace (no open beep if closed less than this long ago; also the idle-drop delay) | 500 ms |
| voice-tail discard after an RPC-driven close | 400 ms |
| max key hold (failsafe for lost stops) | 120000 ms |
| minimum RF quality for reachability | 0.05 |

All times are world time in ms.

State is kept **per frequency** (not per radio):
- the set of keyed senders, each with the time their key-start was accepted
- a voice-active flag and the last voice time
- an open flag and the closed-at time
- an RPC-closed-at time

A new channel starts with closed-at = 0 and RPC-closed-at = 0. So in the first 500 ms of world
time the first open is silent, and voice in the first 400 ms is discarded. Separately, the squelch
keeps a map from radio to the last time any voice packet arrived on it.

"**Tuned transceiver** for frequency F" means all of the following hold. If any fails, there is no
tuned transceiver.
- the local PlayerController's VON controller has a radio entry on F
- that entry has a transceiver
- the transceiver's radio exists and is powered
- the transceiver is not muted

"**Reachable**" for a key-start (sender id, F, range, sender pos):
- If the sender pos is the zero vector or range ≤ 0, it is reachable.
- If the local player has no controlled entity, it is reachable.
- If the 3D distance from sender pos to the local entity's origin is greater than range, it is not
  reachable.
- If `EC29_RFPropagationNetworkComponent.IsRFPropagationEnabled()` and
  `EC29_RadioState.GetSignalQualityCached(senderId, senderPos, myPos, F)` < 0.05, it is not
  reachable. This deliberately shares the per-sender cache entry with the voice path.
- Otherwise it is reachable.

**Remote key-state** (`OnRemoteKeyState`):
- With no local PlayerController, or when the sender is the local player, ignore it.
- **Start**:
  1. Require a tuned transceiver on F and that the sender is reachable. If either fails, ignore
     the start entirely and record nothing.
  2. Get or create the channel.
  3. Expire stuck keys: drop senders whose key time is more than 120000 ms old.
  4. Record the sender with the current time.
  5. **Open** the channel using that transceiver.
- **Stop**:
  1. If the channel does not exist, or the sender is not in its keyed set, ignore it. Only stops
     whose start was accepted here may release the channel.
  2. Remove the sender.
  3. If no keyed senders remain: clear voice-active, set RPC-closed-at = now, and **Close**
     immediately. Do not wait for the silence timeout.

**Voice packet** (`OnVoicePacket(F, receiver)`):
1. Record now as the receiver radio's last-RX time. This happens **before** any gate (see
   section 4).
2. If the receiver is muted or on a special net, stop. No squelch.
3. Get or create the channel and expire stuck keys.
4. If now − RPC-closed-at < 400 ms, drop the packet as an in-flight tail.
5. Otherwise set voice-active, set last voice = now, and **Open** using the receiver.

The voice path does not check power; the engine already only delivers to powered radios.

**Open**:
- If already open, do nothing (and log nothing).
- Otherwise log OPEN (VERBOSE) and mark the channel open.
- Play the RX-open sound on the given transceiver **unless** now − closed-at < 500 ms. A quick
  reopen is silent.

**Close**:
- If not open, do nothing.
- Otherwise log CLOSE (VERBOSE), mark the channel closed, and set closed-at = now.
- Play the RX-close sound on the **currently tuned** transceiver for F. If none is tuned (radio
  off, muted, or retuned), play no sound.

**Tick(now)** runs every 150 ms while needed. For each channel:
1. Expire stuck keys.
2. If voice-active and now − last voice > 600 ms, clear voice-active.
3. If open with no keyed senders and not voice-active, **Close**.
4. If closed with no keyed senders, not voice-active, and now − closed-at > 500 ms, delete the
   channel.

Tick returns true while any channel remains.

**Ticker** ("single-ticker rule"): `EnsureTicking()` does nothing if a tick is already scheduled.
Otherwise it schedules one 150 ms out. Each tick:
- If there is no world, stop and clear the ticking flag.
- Otherwise run Tick with the world time. If it returned true, reschedule for 150 ms. If not, stop
  and clear the flag.

There must be exactly one ticker per squelch instance, and the feed paths never run their own.

**Last-RX query**: returns the stored time, or −1 if the radio was never recorded or the radio
handle is null.

**Sweep**: removes every entry whose radio handle has gone null because the radio was deleted. Do
not delete those entries in place: several deleted radios all read back as the same null key, so
rebuild the map from the live entries instead.

### 2.9 World lifecycle

- `EC29_RadioState.GetInstance()` rebuilds itself whenever the current world differs from the one
  it was built for. That gives a new registry (empty), a new squelch (no channels, no RX records,
  not ticking) and a new propagation model. So jammers, channels and caches never survive a world
  change. Your implementations must hold no state in statics, apart from the two explicitly static
  singletons below.
- A stale squelch ticker from an old world stops either at world teardown (no world) or when its
  channels drain.
- `EC29_RFPropagationSettings` stays a process-lifetime static singleton, loaded once.
- `EC29_RFPropagationNetworkComponent` holds a static, non-owning instance handle, set at
  post-init. Its server-side maps (keyed frequency per player, pending stops, buckets) live as long
  as the component does.

---

## 3. Logging

### `[EC29-DBG]` lines

Most are gated by `EC29_Debug.VERBOSE`; the table notes the two that are not.

| Tag | When | Gated by VERBOSE? |
|---|---|---|
| `[EC29-DBG][Jammer]` | jammer registered (prints running total) | yes |
| `[EC29-DBG][Jammer]` | jammer unregistered (prints total before removal) | yes |
| `[EC29-DBG][Jammer]` | active flag replicated on a proxy (active, range, cone) | yes |
| `[EC29-DBG][Jammer]` | toggle action performed on the authority (new state) | yes |
| `[EC29-DBG][RadioNet]` | client created the default-valued RF settings instance (no JSON IO) | yes |
| `[EC29-DBG][RadioNet]` | server accepted a key-state relay (player, freq, range, keyed) | yes |
| `[EC29-DBG][RadioNet]` | server **rejected** a key-state for out-of-bounds frequency (WARNING level) | **no, always** |
| `[EC29-DBG][RadioNet]` | server **rate-limited** a key-start (WARNING level) | **no, always** |
| `[EC29-DBG][RadioSquelch]` | channel OPEN on a real transition (time) | yes |
| `[EC29-DBG][RadioSquelch]` | channel CLOSE on a real transition (freq, time) | yes |

The squelch OPEN line must be logged only on a closed-to-open transition, never per packet.

### Other lines

- `[EC29 RFPropagation]` prefix, never gated: settings loaded from JSON, default JSON created, JSON
  parse ERROR, JSON create ERROR, loaded from .conf, using defaults WARNING, the once-per-process
  summary, the set-flag lines, server settings loaded, and client received settings.
- `[RFPropagation]`: one line per model evaluation, printed only when the replicated RF debug flag
  is on. It is independent of VERBOSE.
- Console tools (`DebugJammers`, `DebugPropagation`) print their own plain-prefixed reports, not
  gated.

---

## 4. Invariants and past fixes to preserve

1. **Jammer state is authority-only.** No client-to-server toggle RPC exists. One used to exist; it
   was an unvalidated attack surface and was removed. If client-initiated toggling is ever added,
   it must be a server-validated RPC with permission and rate checks, never a blind setter.
2. **The toggle action changes state only on the authority.** User actions run on the server and
   on clients. If a client flipped its local copy, it could race the replicated value and flip the
   jammer back. Offline or editor (no replication) counts as authority.
3. **Settings JSON is touched only on the server or offline.** Clients must neither read nor create
   `$profile:EC29_RFPropagation.json`.
4. **Never overwrite a JSON file that exists but fails to parse.** It is an admin's hand edit. Use
   defaults for this run and log an ERROR.
5. **The 5000 m model cutoff returns clean quality (1.0).** Special nets (admin radios advertising
   about 50 km) must not march terrain samples across the map through unloaded cells. That load
   pattern caused the 2026-08-23 server stalls when spectators talked.
6. **The propagation hot path does not allocate** and sits behind the 250 ms per-sender cache.
   Reachability and the voice path share the same cache entry, so a key-start followed by voice
   costs one raymarch.
7. **Key-stops are never rate-limited.** Dropping a stop would leave every receiver's squelch stuck
   open until the 120 s failsafe. That includes the disconnect path, which goes through the same
   relay.
8. **Validate before amplifying.** Frequencies are bounds-checked and range is clamped server-side
   before anything is turned into reliable broadcasts to every player.
9. **Key-stop debounce of 300 ms** collapses push-to-talk spam into one continuous transmission:
   one open and one close beep, and no traffic for a cancelled stop. The flush must tolerate both
   world teardown and being superseded (the 250 ms age check).
10. **Disconnect releases the channel**, and per-player server maps must not grow for the life of
    the process (the bucket is removed on disconnect).
11. **Sender position is stamped on the server**, so receivers can range-gate without the sender
    entity, which may be outside their replication relevance.
12. **A broadcast is also handled locally on the server**, so the listen-server host gets squelch.
13. **The squelch key-start is gated, the key-stop is always processed**, but only for senders
    whose start was accepted. This avoids two failures: a receiver that walked out of range
    mid-transmission keeping a channel wedged open, and a filtered-out sender's stop closing a
    channel someone else is keying.
14. **An RPC stop closes immediately and discards the voice tail for 400 ms.** The window is
    deliberately shorter than the 500 ms reopen grace, so a genuine voice-only talker resumes
    silently instead of getting a second close beep.
15. **Squelch mirrors engine delivery.** No squelch on unpowered or muted radios, or on special
    nets.
16. **Last-RX is recorded before any policy gate.** Packet arrival proves the native receiver is
    registered. The receiver guard uses this to skip repairs on working radios and to flag radios
    that never receive (field case 2026-08-24: dead RX for a whole session).
17. **Single-ticker rule.** Only the squelch owns its tick loop, so the state machine is never
    double-ticked.
18. **The neutral jam value is 1.0 (clean)**, and it is written, not skipped, when the listener has
    no body. Otherwise a listener who died inside a jammer would keep hearing jammed static, because
    these audio variables are global and nothing else refreshes them.
19. **Component sort order** (project rule): a new VoN-family component class added to an entity
    must sort by class name after the ear component. This subsystem adds no VoN components, so the
    rule applies only if you restructure.

---

## 5. Jammer prefabs (gameplay parameters)

All of these use `EC29_JammerComponent` with active-on-spawn left at its default of true. Every
prefab is registered in `Configs/Editor/PlaceableEntities/Systems/Systems.conf`
(`SCR_PlaceableEntitiesRegistry`, appended to `m_Prefabs`), except the composition and the layer.

| Prefab (path) | GUID | Display name | Range m | Cone deg | Emitter offset | Budget | Mesh | Toggle action / context |
|---|---|---|---|---|---|---|---|---|
| `Prefabs/Editor/EffectsModuleEntities/EC29_JammerOmni500.et` | `{DAE4B75C9D640479}` | Jammer Omni 500 M | 500 (default) | 180 (explicit) | 0 0 0 | SYSTEMS 5 | none (invisible effect module; no physics) | none |
| `Prefabs/Editor/EffectsModuleEntities/EC29_JammerOmni1000.et` | `{5D28BAA97D8B4A1C}` | Jammer Omni 1000 M | 1000 | 180 (default) | 0 0 0 | SYSTEMS 5 | none | none |
| `Prefabs/Editor/EffectsModuleEntities/EC29_JammerOmni2000.et` | `{F8E6FEB3EDD56EEC}` | Jammer Omni 2000 M | 2000 | 180 (default) | 0 0 0 | SYSTEMS 5 | none | none |
| `Prefabs/Jammer500 omnidirectional.et` | `{54A26219AA6C9E43}` | Jammer OmniDirectional 500 M | 500 (default) | 180 (default) | 0 0.5 0 | empty budget entry (class defaults) | yes, small jammer mesh, rigid body (PropFireView) | yes; context "JammerControlPanel" at local offset (0, 0.9716, −0.0967) |
| `Prefabs/Jammer500 directional.et` | `{5F8DF577A8DD9544}` | Jammer Directional 500 M | 500 (default) | 90 | 0 0.5 0 | empty budget entry (class defaults) | yes, small jammer mesh, rigid body | yes; context "JammerControlPanel" at (0, 0.9716, −0.0967) |
| `Prefabs/Mega_JammerDir1000.et` | `{5A57220A058B8413}` | Mega Jammer (1000m) | 1000 | 90 | 0 12 3 | empty budget entry (class defaults) | yes, large jammer mesh, rigid body | yes; context "JammerControlPanel" (UI name "Power Box") at (−6.7011, 1.3539, −2.7624) |
| `Prefabs/Mega_JammerDir2000.et` | `{10EAEF91CAE18990}` | Mega Jammer (2000m) | 2000 | 90 | 0 12 3 | empty budget entry (class defaults) | yes, large jammer mesh, rigid body | yes; context "JammerControlPanel" (UI name "Power Box") at (−6.7011, 1.3539, −2.7624) |
| `PrefabsEditable/Auto/Compositions/Slotted/SlotFlatSmall/Radio Jammer.et` | `{8862A1F092D9290E}` | Radio Jammer (faction US) | 500 (default) | 180 (default) | 0 0 0 | CAMPAIGN 250; children SYSTEMS 5 + PROPS 8 | yes; inherits the vanilla small US antenna composition, and the jammer component sits on a child entity carrying the small jammer mesh (that child's radio/antenna-service components are disabled) | none (the composition's action manager is disabled), so it is always on |
| `Prefabs/Jammer_Layers/default.layer` | (layer file, no .meta) | n/a | 500 (default) | 180 (default) | 0 0 0 | n/a | small jammer mesh | none. A stray world-layer entity; nothing in the repo references it |

Notes:
- The four meshed placeables are editable `SYSTEM` entities with `PLACEABLE` flags and labels
  ENTITYTYPE_COMPOSITION + TRAIT_EFFECT_MODULE. The three Omni effect modules use
  ENTITYTYPE_SYSTEM + TRAIT_EFFECT_MODULE.
- On every action-bearing prefab, the toggle action's UI name and action title are "Toggle Jammer".
  The runtime name comes from section 2.3.
- The directional prefabs aim along the entity's +Z axis, and the cone is 3D, so tilting the
  entity tilts the cone. The Mega jammers' emitter sits 12 m up and 3 m forward (the top of the
  mast). Keep equivalent offsets when re-skinning onto vanilla meshes.
- `Prefabs/Characters/Core/DefaultPlayerController.et` does **not** reference any class in this
  subsystem; it only adds the voice-range HUD display. The key-state client RPC lives on the modded
  `SCR_VONController` class, not in a prefab.

---

## 6. Ambiguities found (decide explicitly; the current behavior is listed)

1. **Fresnel worst-sample choice.** The live model picks the sample with the largest intrusion
   in metres, then grades that one sample's clearance ratio. A sample with a worse ratio but a
   smaller absolute intrusion is ignored, and the debug tool ranks differently. Reproduce as is
   unless a fix is approved.
2. **Antenna heights ignore entity Y** (terrain + 1.5 m always). Players on buildings or in
   aircraft are modelled as standing on the ground.
3. **Cone check at the emitter point.** When the receiver sits exactly at the emitter, the
   direction is a zero vector, the dot product is 0 and θ = 90°, so a 90° cone (half-angle 45°)
   gives no jamming at distance 0. Also, a dot product slightly above 1 from float error could hit
   acos out of domain. Neither has been seen in practice. Clamping is an improvement, not a
   behaviour change.
4. **Comment drift.** `EC29_RadioState.GetSignalQuality` claims it returns 1.0 when RF is
   disabled, but the model never checks the flag; callers do. Keep the callers' gating.
5. **Jammer registration timing vs world rebuild.** Jammers register into whatever
   `EC29_RadioState` exists for the current world at their post-init. If that state were rebuilt
   later for the same load (world pointer changed after entities initialised), the jammers would
   drop out of the registry. This is not observed. A robust rewrite could rescan or re-register.
6. **Client RF settings instance.** The "is client" test relies on replication already running.
   A first `GetInstance()` call on a client before replication starts (for example in the main
   menu) would take the server branch and create or read the profile JSON. The settings singleton
   is never reset afterwards.
7. **Same-frequency re-key inside the debounce window** still spends a rate-limit token.
8. **Close beep after retune or power-off.** The close sound plays on whatever is tuned to F at
   close time. If the radio was switched off mid-transmission, there is no close beep.
9. **Early-world timing.** Channel timestamps start at 0, so the first open in the first 500 ms of
   world time is silent and voice in the first 400 ms is discarded.
10. **Mesh-less Omni modules have no toggle**, and the composition's action manager is disabled.
    Those jammers can only be removed, not switched off. `m_bActiveConfig` has no override anywhere.
11. **Budget entries** on the four meshed placeables are empty `SCR_EntityBudgetValue` objects,
    so they use engine class defaults. Confirm the intended budget when moving these prefabs.
