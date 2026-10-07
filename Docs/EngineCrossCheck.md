# Engine cross-check: EC29 bug history vs native VON

Each row is a bug or quirk we hit where the cause sat in native code, what we did about it,
and the question the Ghidra dump (`C:\tools\ghidra_out_von`) has to answer. "Answer" is filled
in from decompiled code only, with the function address cited.

| # | Bug / quirk (where recorded) | What we did | Question for the engine | Answer |
|---|---|---|---|---|
| 1 | Radio ear routing / net volume / signal quality / jam flicker when two nets talk at once (brain 08-12, "506th author hit the same") | Documented as engine limit | Are audio variables global per config, or per sound instance? Does `VonStream` own a sound instance the graph could read per-stream values from? | |
| 2 | Whisper heard at 10-20 m; shared direct gain owned by the loudest stream (#20, #21, #35) | Moved range to the speaker: three VoN tiers with their own ACP | Confirms which component's ACP plays an incoming direct stream (sender's or listener's)? Is that why tiers work? | |
| 3 | Direct hearing stops at 40 m whatever the curve (brain 09-06, "40 m mystery") | Traced to AmplitudeClass 23571's inherited parent | Is there also a native distance cull on direct packets (send/receive side) independent of the ACP? | |
| 4 | Engine plays incoming streams through the FIRST VoN component, ordered by class name (Bae, #34; tier naming in #35) | Naming rule: new VoN classes must sort after the ear | How does the engine pick the playback component, and is "first by class name" the real rule or an artifact of component iteration order? | |
| 5 | 1.8 dead RX / working TX, receiver registration lost (#10, #17, #25, #29-#31; 1.8.0.13 "radio would not work sometimes") | Repair cycle, later removed after 1.8.0.13 | How does a transceiver register as a receiver with RadioManagerEntity, and what can drop it? | |
| 6 | Client CTD: native write access violation in `RadioManagerEntity.GetTransceiversInRange` on worlds without a real RadioManagerEntity (#13) | Guard skips the call on manager-less worlds | What does `GetTransceiversInRange` dereference when no manager entity exists? Is the non-null `GetRadioManager()` handle a stub? | |
| 7 | Voice capture wedges so every radio transmit is silent (#17, the SetCapture(false) self-heal) | Clear capture before each key-up | What state makes `SetCapture(true)` a no-op? | |
| 8 | Server FPS collapses while spectators key the net (#22, #33) | Ghost radio cut to 2 km, then retired (#34) | Cost of radio relay per packet: does the server iterate every transceiver in range per voice packet? Scaling with transmit range? | |
| 9 | 1.8 relays: static map antennas disabled, radios only work near relays (issue #4) | Explained (BI change), no code | Confirm relay hop logic and range checks in the native relay path. | |
| 10 | Packets delivered to more than one VoN component on a character (#35 dedupe per speaker per world-time) | Dedupe in OnReceive | Does the engine call OnReceive once per component, once per transceiver, or both? | |
| 11 | Phantom key-starts on radio selection (#38) | Key logic moved into ActivateVON | None (script-side) - listed for completeness. | n/a |
| 12 | Server high-CPU restart 2026-10-04 (Erickson report) | Unattributed | Only if the server log points into VON; otherwise out of scope. | |

## Goal
Find out whether any per-stream or per-transceiver audio parameter is reachable from script or
ACP config. If yes, rows 1 and 2 get a real fix. If no, the dump documents the limit so we stop
chasing it.

## Findings (Ghidra, ArmaReforgerSteamDiag.exe, 2026-10-07)

Addresses are Ghidra function names from the dump in `C:\tools\ghidra_out_von`.

1. **One sound per speaker, not per radio.** `FUN_1402f22d0` keeps a list of `VonStream`s keyed by the
   speaker's player ID on the receiving VoN component and creates one on first packet. Each stream
   compiles its own instance of that component's sound graph (`FUN_1408cb700`). Two people talking on
   two nets = two instances; one person heard on two of your radios = one instance.
2. **Per-stream signals exist, but only six, and the engine owns them.** The stream binds
   `TransmissionQuality`, `distance`, `Interior`, `RoomSize`, `UnderPlayerControl`,
   `IsInPlayerVehicle` (`FUN_1408cb700`). Every packet, `FUN_1408cc3b0` sets `TransmissionQuality`
   from the packet and copies the four environment signals from the SPEAKER's entity signals.
   Script-set audio variables (`EC29_EarRouting`, `EC29_ChannelVolume`, `EC29_SignalQuality`,
   `EC29_JamStrength`) are config-wide, one value for every stream - so row 1 is confirmed as an
   engine limit: last writer wins.
3. **Direct vs radio is per packet on the same sound.** `FUN_1408cbc60` plays `VON_DIRECT` or
   `VON_RADIO` from the packet's type byte; when a speaker's type changes the old sound is stopped
   and a new one started. Someone alternating between direct speech and radio restarts their sound.
4. **Radio streams sit at the listener, direct streams at the speaker** (`FUN_1402f2e70`, position
   source chosen by packet type; `FUN_1402f4ed0` re-positions every stream each frame).
5. **Which component decodes:** `FUN_1402f2e70` (registered as `VoNComponent::Rpc_ReceiveVoiceData`
   in `FUN_1402f0e80`) only decodes on a component whose owner entity is the locally controlled
   entity, or that has a flag set at `+0x71`. Streams then use THAT component's sound graph (`+0x80`).
   This points to the LISTENER's component playing everyone. Not yet proven: which component
   instance the server routes the RPC to. If it is the listener's, the whisper/normal/yell tier
   ACPs only shape what the listener hears while the listener is in that tier, not the speaker's
   range - which would contradict the #35 design. Verify with the existing VERBOSE line
   "first incoming packet delivered to component class" before trusting the tiers.
6. **Capture** (`FUN_1402f4ed0`, per-frame component update): `SetCapture` only sets a desired flag;
   the update starts/stops capture when it differs from last frame. Radio comm method with no
   radio assigned falls back to direct with a log line ("CommMethod is CM_SQUAD_RADIO without any
   radio assigned"). EC29's SetCapture(false)-then-true in one call nets to no change, so the
   1.8 wedge self-heal works only if the wedge is a stale flag, not a native capture fault.

## What this means for EC29
- No per-stream gain or pan is reachable from script. Ear routing and per-net volume cannot be
  made correct for two nets talking at once by writing variables.
- The only per-stream inputs the graph gets are the six signals above. Four come from the
  speaker's entity signals. Untested idea: on the listener's machine, set one of those signals on
  the speaker's (non-replicated, local) entity to encode which radio received the stream, and
  have the radio graph route/scale on it. It would hijack a vanilla signal's meaning, so test
  carefully first.
- Item 5 needs a field check before the tiers are relied on.
