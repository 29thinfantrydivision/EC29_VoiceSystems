# Spectator Voice Absorption

> **STATUS 2026-09-06.** Sections 1-4 below are the ORIGINAL August plan and are kept for the reasoning, not as a description of the code. Two things in them are now false: the "dormant resources" (the spectator radio prefab and the second ACP) were deleted rather than shipped dormant, and the consumer no longer has a spectator body at all. Read the dated sections at the bottom first - they supersede.

**Status:** EXECUTING — EC29 side implemented on this branch; spectator-mod delegation PR follows. **Supersedes** the full *Voice Multi Mod Master Sync* design (branch `voice-multi-mod-master-sync`, never merged) after a 2026-08-31 re-audit narrowed its scope.

## 1. The re-audit

The 2026-08-24 Master Sync plan proposed EC29 as single VON authority across three mods, via three seams and six phases. Re-evaluated against what the unit actually needs, only one part earns its complexity: **pulling spectator voice out of 29th_Spectator_V3 into EC29**. Everything else is dropped or deferred:

| Master Sync element | Disposition | Why |
|---|---|---|
| Seam 1 net-policy registry + 7 call-site conversions | **Dropped** | Its consumers were the other mods' nets. With spectator voice absorbed and Game Master untouched, no external consumer exists. The `EC29_IsSpecialNet` heuristic already covers the spectator net (29000 kHz < 30000; 50 km > 10 km) for both the old and new radio prefab, permanently, including third-party nets. A registry with zero registrants is dead weight. |
| Phase 1 Game Master enemy-names consolidation + GM PRs | **Dropped** | Out of scope by direction: leave GM alone. The GUID coin-flip on `DefaultPlayerController.et` stays the known, documented condition it is today. GM's mystery dependency `69D375C3CB13F8CD` was separately identified as 29th_ID_MapIcons (benign). |
| Seam 2 controller primitives | **Kept, refined** | Needed by the service. Refined per the plan's own rationale: the vanilla-action block is *derived per call*, not a settable latch — there is no `SetVonActionsBlocked` API at all, because a latch on a controller that outlives every life is the session-long-mute bug class. |
| Alternate-PTT bypass close | **Kept** | The frame-poll bypass documented in the audit is closed by the same derived gate. |
| Seam 3 spectator voice service | **Kept** | The point of the exercise. API adjusted to the spectator mod's real call flow (see §2). |
| EC29-owned spectator resources | **Kept, dormant** | Radio prefab + tier classes + two ACPs ship now so the spectator mod's later deletion PR needs no new EC29 release. Referenced by nothing until then. |
| Spectator hearing-profile setting (FLAT/MODULATED) | **Dropped** | Phase 5 (MODULATED whisper profile) is not wanted; a one-value setting is dead weight. FLAT behavior is inherent in the shipped ACPs. |
| V3 cutover two-step (delegate, then delete) | **Kept** | The delegation PR keeps the spectator mod's voice files as the rollback (workshop rollback → service goes dormant). The deletion PR comes only after a clean drill soak. |
| Unit decision §8.2 (V3→EC29 hard dependency) | **Accepted** | Implied by ordering this absorption. The pinned pre-dependency spectator release remains the emergency pair; from the delegation release onward, delisting EC29 kills spectator voice loudly (whole-project compile failure) rather than silently. |

## 2. Architecture as built

**Service:** `EC29_SpectatorVonService`, owned by `EC29_RadioState` (world-scoped, rebuilt on world change — no spectator voice state survives a scenario change). Client-side by construction: every path starts from the local `PlayerController`, so it no-ops on a dedicated server. Never touches `ChimeraWorld.GetRadioManager()`.

**API** (called by the spectator mod's camera; no EC29 code references any spectator-mod type):

- `EnterSpectate()` — arms state, locks direct speech, subscribes to controlled-entity changes. Split from body registration because the ghost lands asynchronously after entry and the direct-speech kill must hold even if it never lands.
- `RegisterSpectatorBody(body, normalTier, quietTier, radio)` — the caller resolves its own component types and hands over base-typed handles at body arrival (null body unregisters). Applies receive-mute immediately; queues the deferred re-assert.
- `SetTransmitting(bool)` — the push-to-talk pipeline, ported under the verbatim-invariant rule: refuse-start-never-refuse-stop; stop capture on both tiers before any tier switch; quiet-swap before capture; `GetTransceiver(0)` with no cast; read-back guard that forces capture off rather than let the engine fall back to CM_DIRECT (root cause of the original cast-null never established — cleanup of this path is banned).
- `SetReceiveEnabled(bool)` — the whole net on/off: stops any transmission first (mute-mid-capture is the stuck-mic route), then walks `SetMuteState` on every transceiver of the registered radio only.
- `ExitSpectate()` — teardown in the verified invariant order: flag first, stop capture on every tier, cancel the queued re-assert, restore direct speech unconditionally.

**Self-healing (the plan's liveness requirement, concretely):** the service subscribes to `m_OnControlledEntityChanged` itself rather than trusting the caller to forward events — a missed exit path in the caller is exactly the failure this covers. The deferred re-assert re-applies the direct-speech lock (vanilla's `ResetVON` discards it on every entity change) and re-selects the normal tier one frame later (vanilla's own subscriber re-resolves to the base-typed, possibly disabled component inline). If a *live* character is under local control while the service still thinks it is spectating, it auto-exits with a WARNING instead of re-locking a living player's speech.

**Controller primitives** (`modded SCR_VONController`): `EC29_SetDirectSpeechTransmitLocked` (usability kill; verified safe against EC29's own usability re-syncs, which cast `SCR_VONEntryRadio` first and so can never re-arm the plain direct entry — this closes the spectator mod's documented open question) and `EC29_SelectVonComponent` (the atomic tier swap with the `m_bIsToggledDirect` direct-write latch repair; its took-verification return is the per-game-update tripwire). All five vanilla VON actions short-circuit behind the derived gate, plus EC29's own polled surface (alternate PTT, radial-menu actions) and the F3 range cycle.

**Resources** (dormant until the spectator mod's deletion PR re-points at them):

| Resource | GUID | Notes |
|---|---|---|
| `Prefabs/Items/Core/EC29_Radio_Spectator.et` | `FD81CA0FE2BAED7C` | Byte-identical to the spectator mod's `Radio_spectator.et`: 29000 kHz / `SPEC29_KEY`, powered. Those two values are the **interop ABI** — changing either is a cross-mod breaking change. Range is a tunable: 50 km until 2026-09-05, then 2 km (server-FPS experiment; see `EC29_SpectatorVonService.c` header). |
| `Sounds/VON/EC29_SpectatorVon.acp` | `5C819950A693EB52` | Hearing tier: vanilla VON graph, innerRange 40 / outerRange 68. |
| `Sounds/VON/EC29_SpectatorVonQuiet.acp` | `6BA89BD90EA9220F` | Talk tier: innerRange 0 / outerRange 0.0001. |
| `Scripts/Game/VON/EC29_SpectatorVonTiers.c` | — | `EC29_VoNSpectator` / `EC29_VoNSpectatorQuiet` empty component subclasses. |

## 3. Rollout

1. **This PR (EC29):** service + primitives + dormant resources. Behavior-neutral until a caller exists; the blocks cannot engage without a registered body. Fleet-publish before step 2 merges.
2. **Spectator-mod delegation PR:** camera voice methods route through the service, passing the spectator mod's own components; gproj gains the EC29 dependency (`481849A4E0D88BEA`). Its voice files stay in place as the rollback. Mixed-version reality: new spectator mod + old EC29 fails compile **loudly at server start** — provider-first publish ordering is the guard; old spectator mod + new EC29 is fully supported indefinitely (the heuristic reproduces today's exemptions).
3. **Soak one full drill**, then the spectator-mod deletion PR: remove `SPEC29_VONController.c`, the tier classes, both ACPs and `Radio_spectator.et`; point the body prefab's tier components at the EC29 classes/ACPs and the wristwatch slot at `EC29_Radio_Spectator.et`.

## 4. Regression contract (carried from the Master Sync plan, still binding)

Receiver guard decision tree, ready flag and `STABILIZATION_DELAY_MS` untouched; usability re-sync sites untouched (radio-entry-only, verified); squelch `IsMuted` checks verbatim; CH1-left/CH2-right routing untouched; 5 km RF cap keeps its perf role; editor steady-1.0 gain and the strict-`<` hold byte-identical; safe early-outs and the no-PlayerController DS guards untouched; no new code path calls methods on `ChimeraWorld.GetRadioManager()`.

## 2026-09-05 - manager model (supersedes the ghost radio)

Spectator voice is anchored to the player's own **editor manager entity**, never opened: `EC29_EditorManager.et`
(routed to every player by `EC29_EditorSettingsEntity`) adds a 29000 kHz / 5 km `RadioTransceiver` and the quiet
transmit tier to the vanilla manager. The service wires it the way `SCR_EditorManagerEntity.Open()` wires voice.
The engine's active-editor question is answered from a flag replicated on the manager (`EC29_EditorManagerEntity`);
power, interpolation and position snaps are authored on the server (`EC29_SpectatorVoiceController`). Spectator
audio (clean radio voice, full-volume direct hearing to 40 m) is variable-gated in `von.acp` and applies only on a
spectating client. The ghost radio, `EC29_Radio_Spectator.et` and the normal-tier ACP are gone; the caller passes
its camera (`FollowCamera`) and nothing else. Field rules are in the service header.

### 2026-09-05 later - the ear (`EC29_VoNSpectatorLoud`)

An entity orders its components **by class name**, and the engine plays an incoming stream through
the **first** VoN component on the editor manager, gated by *that* component's ACP range. Nothing in
script overrides it: not `ConnectEditorToVoNSystem`, not `SetVONComponent`, not connect order, not
prefab listing order, not GUIDs (all measured, five runs). So `EC29_VoNSpectatorQuiet`
(`outerRange 0.0001`) sorted ahead of `SCR_VoNComponent` and became the ear - total deafness. The
pre-consolidation build escaped only because `SPEC29_VoNSpectatorQuiet` sorts *after* `SCR_`.

`EC29_VoNSpectatorLoud` (stock `von.acp`, full range) now owns the first slot: `L` < `Q`, `E` < `S`.
It is what the service connects and selects; the quiet tier is transmit-only. **Any VoN class added
to this manager later must sort after `Loud`.** This also protects real Game Masters, whose
`Open()` resolves the editor voice component the same way.
