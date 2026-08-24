# Voice Multi Mod Master Sync — Design Plan

**Branch:** `voice-multi-mod-master-sync` · **Status:** PROPOSED — awaiting unit decisions (§8) · **Date:** 2026-08-24

## 1. Situation

Five confirmed cross-mod VON conflicts were found and patched during the August 2026 audit
(PRs #21–#23). Every patch is a *coexistence workaround*: EC29 infers what other mods are
doing (the `EC29_IsSpecialNet` frequency/range heuristic, `IsMuted` inference, editor-sender
gating) instead of being told. Inference breaks silently when any mod changes; the category
of bug survives even though every known instance is fixed.

The org runs three mods with VON behavior:

| Mod | VON footprint (verified against source) |
|---|---|
| **29th_Spectator_V3** | A complete parallel voice stack: ghost-body radio (`Radio_spectator.et` — 29000 kHz, 50 km, `SPEC29_KEY`, powered), two ACP audio-graph forks (normal 40/68 m hearing + near-zero "quiet" tier swapped in during PTT), a modded `SCR_VONController` (direct-speech transmit kill, atomic component swap, five vanilla VON action blocks — `SPEC29_VONController.c:43-220`), and a client PTT pipeline with a hot-mic safety guard (`SPEC29_Camera.c:4636-4963`). All client-side. |
| **29th_Game_Master** | Zero VON scripts. One voice artifact: `prefabs/SystemOverrides/DefaultPlayerController.et` setting `SCR_VonDisplay.m_bShowEnemyNames 0`, which GUID-collides with EC29's own override of `{6E2BB64764E3BE9B}` (load-order coin flip). |
| **29thIDSpectate2** | Retired (superseded by V3; fleet-delisting to be confirmed — §9). Opens the vanilla editor for dead players, which grants editor VON. |

## 2. Mission

EC29 becomes the **single VON authority** for the modset. Other mods declare what they need
(a net, a listener state, a policy) through an explicit EC29 API instead of implementing
voice behavior themselves. End state: V3 owns spectator *lifecycle and input*, EC29 owns
spectator *voice*; GM owns *nothing* voice-related; the heuristic survives only as a fallback
for third-party mods that will never call our API.

This plan was produced from a three-way independent design study (registry-only, full
absorption, phased hybrid), each proposal adversarially reviewed against vanilla 1.8 scripts
and all org mod sources. What follows keeps what survived review and drops what didn't.

## 3. What deliberately does NOT move

These boundaries are load-bearing. Each was a failure mode in at least one reviewed design.

1. **Input stays V3-owned, permanently.** V3 keeps its input contexts, action listeners, and
   its `chimeraInputCommon.conf` override. The two mods' same-GUID conf overrides
   (`{795184CF9AD764DB}`) coexist today because their action sets are *disjoint* — merging
   them passes through an untested engine behavior (same action defined in two same-GUID
   overrides). The disjoint-action rule becomes a documented contract instead. This also
   avoids reimplementing V3's event-driven PTT edges as a frame poll — EC29's controller
   Update poll early-returns behind `EC29_CoexistenceGuard.ShouldYieldRadio()`
   (`EC29_VON_VONController.c:399-400`), which would silently kill spectator PTT whenever a
   third-party radio mod is co-loaded. V3's listeners call the EC29 service; no polling.
2. **Ghost-body lifecycle stays V3-owned.** Body spawn/park/follow/reap is spectator
   machinery, not voice. The service takes the body (and its components) as arguments.
3. **Editor-gain classification stays per-packet.** `EC29_ApplyEditorGain` is selected by the
   `isSenderEditor` flag on direct packets where no transceiver exists — the net-policy
   object *cannot* key it. Two classifiers is the honest architecture: net policy for
   radio paths, packet flag for editor direct speech. Documented so nobody "consolidates"
   it later and regresses the GM-flutter fix.
4. **The heuristic never dies.** `EC29_IsSpecialNet` becomes the registry's internal
   fallback, kept permanently for third-party nets (ACRE-class mods). An unregistered
   special-looking net logs a one-shot WARNING — our migration tripwire and our third-party
   safety net in one.

## 4. Architecture — three seams

### Seam 1: Net policy registry (`EC29_VonNetRegistry`)

World-scoped beside the existing `EC29_RadioState` services, machine-local (never
replicated — policy is consumed where the transceiver lives; a prefab's descriptor
component re-registers on every machine that instantiates it, which covers JIP for free).

- `EC29_VonNetPolicy` — named flags mapping 1:1 onto today's *verified* exemption sites:
  `m_bBlockRetune` (`EC29_VON_VONController.c:509`), `m_bBlockAlternatePtt` (`:558/:579`),
  `m_bExemptPowerCycleGuard` (`EC29_RadioSystemGuard.c:163`), `m_bExemptSquelch`
  (`EC29_RadioRxSquelch.c:142`), `m_bCleanSignal` (`EC29_VON_VoNComponent.c:359` — one flag,
  one site; the reviewed designs' separate RF-skip flag mapped to nothing),
  `m_eEarRoutingOverride` (`EC29_EarRouting.c:67`), `m_bMuteMeansReceiveOff` (documents the
  `IsMuted` contract the squelch already honors).
- `EC29_VonNetDescriptorComponent` — placed on a radio-owning prefab; self-registers at
  `EOnInit`, deregisters on delete. Registration must never touch `PlayerController`
  (it runs on the dedicated server too) and must never call methods on
  `ChimeraWorld.GetRadioManager()` (standing rule — the 2026-08-21 CTD class).
- `ResolvePolicy(BaseTransceiver)` — replaces the heuristic at its seven consumer sites.
  Null-safe (the heuristic absorbs null today; the API must too — `:579` passes an
  unchecked `GetTransceiver()`). Resolution: registered → policy; else heuristic →
  canned `SPECIAL_LEGACY` policy (bit-identical to today) + one-shot WARNING; else null
  (normal net). **No editor built-in policy at these sites** — today only the guard's
  `:167` checks `IsEditorRadio()`, and adding it at the other six sites changes
  editor-radio behavior in the phase whose entire claim is zero change.

### Seam 2: Controller policy primitives (modded `SCR_VONController`, EC29-side)

Absorbing V3's protected-member machinery as contractual APIs — but **derived, not
latched**. V3's action blocks re-derive "am I controlling a spectator body" on every call
and self-heal by construction; a session-lifetime boolean on a controller that outlives
every life (`s_bIsInit`, vanilla `SCR_VONController.c:816`) permanently mutes anyone whose
exit path misses one restore. V3's own comments record that exact bug class shipping once.

- `EC29_SetDirectSpeechTransmitLocked(bool)` — wraps the `m_DirectSpeechEntry` usability
  kill. **Verified safe:** both EC29 usability re-sync sites cast to `SCR_VONEntryRadio`
  first (`EC29_VON_VONController.c:138`, `:328`), so they can never re-arm the plain-entry
  direct-speech kill. This closes V3's documented open question — deliver as a comment PR
  to V3 immediately (§7, PR-0).
- `EC29_SetVonActionsBlocked(bool)` + a **liveness check**: every consumer re-verifies the
  service's spectator state (body handle valid, context active) before honoring the flag —
  a stale latch degrades to vanilla behavior instead of a session-long mute. The same flag
  gates EC29's alternate-PTT frame poll, closing the documented action-block bypass at
  `EC29_VON_VONController.c:555-557` — standalone value, ships in Phase 0.
- `EC29_SelectVonComponent(SCR_VoNComponent)` — the atomic swap with the
  `m_bIsToggledDirect` direct-write latch repair, ported verbatim from
  `SPEC29_VONController.c:77-103`, with its took-verification return kept as the
  per-game-update tripwire (it depends on vanilla protected members surviving updates).

### Seam 3: Spectator voice service (`EC29_SpectatorVonService`, client singleton)

The state machine moves to EC29; the *triggers* stay in V3.

- `EnterSpectate(body, normalTier, quietTier, radio)` — component-agnostic: V3 passes its
  own components during migration, EC29's after cutover. Re-armed on **every**
  controlled-entity change (vanilla `OnControlledEntityChanged` re-points the VoN component,
  `bi-scripts SCR_VONController.c:674-687`; V3 survives via a per-change deferred re-assert
  — that per-change contract is part of the API spec, not an implementation detail).
- `ExitSpectate()` — teardown in V3's verified invariant order (stop capture on all tiers →
  cancel queued re-asserts → restore direct speech and unblock actions unconditionally),
  plus self-healing auto-exit if a live character appears while state says spectating.
- `SetTransmitting(bool)` — the PTT pipeline ported under a **verbatim-invariant rule**:
  refuse-start-never-refuse-stop; stop capture on both tiers at key-up; quiet-swap before
  capture; `GetTransceiver(0)` with NO typed cast; and the read-back guard that forces
  `SetCapture(false)` if the engine would fall back to `CM_DIRECT`. The original CM_DIRECT
  cast-null root cause was never established (`SPEC29_Camera.c:4764-4769`) — every one of
  these lines carries its source comment across, and "cleanup" of this path is banned.
- `SetReceiveEnabled(bool)` — `SetMuteState` walk (vanilla's own do-not-hear control, keeps
  the free HUD mute icon), independent of the transmit refusal.
- Spectator hearing profile — replicated setting `m_eSpectatorHearingProfile` on
  `EC29_VONSettingsComponent` (RplProp + mission-header seed + BumpMe, the proven pattern):
  `FLAT` (default — today's behavior, vanilla-fork ACP, whispers full-volume to 40 m) vs
  `MODULATED` (EC29-graph ACP consuming `EC29_VonRange`). Unit policy as a setting, not a
  hardcode. Profile selection is a component pick at spectate entry — no runtime ACP swap.

### GM absorption (completes in one phase)

GM's entire VON footprint is one prefab flag, so absorption = policy consolidation:

- New replicated enemy-names policy on `EC29_VONSettingsComponent`, applied **script-side**:
  EC29's modded `SCR_VonDisplay` writes vanilla's protected `m_bShowEnemyNames` at runtime
  (read per-transmission at `SCR_VonDisplay.c:266/:335` — verified viable), making the
  policy independent of which prefab wins the GUID coin flip.
- **Not a third toggle.** EC29 already ships `m_bAlwaysShowEnemyNames = true`
  (`EC29_VON_SettingsComponent.c:17`) whose force-reveal path
  (`EC29_VON_VonDisplay.c:117-118`) *already fights GM's flag today*. The work is a
  reconciliation of the existing fields into one coherent policy with one default — which
  requires the unit decision in §8.1. JIP-covered via `onRplName` callback (a JIP client
  must never sit on vanilla's show-names default waiting for an unspecified refresh).
- GM deletes its `DefaultPlayerController.et` only after the fleet runs the EC29 release
  carrying the script-side write. Both coin-flip outcomes are safe from that point on.

## 5. Phases

Every phase independently shippable; provider (EC29) always fleet-confirmed before any
consumer PR merges. Org controls all repos — this is process, not tooling.

| Phase | Contents | Repos | Gate / rollback |
|---|---|---|---|
| **0 — Groundwork** | Registry + policy object + `SPECIAL_LEGACY` fallback + descriptor component; 7 call-site conversions (behavior-identical by construction); alternate-PTT bypass close; PR-0 comment PRs (§7); identify GM's unresolved dependency `69D375C3CB13F8CD` | EC29 (+2 doc PRs) | WB compile + two-client smoke; revert = single-mod rollback |
| **1 — GM policy consolidation** | Enemy-names reconciliation (per §8.1 decision) + script-side write + JIP callback; then GM's one-file deletion PR | EC29, then GM | EC29 fleet-confirmed before GM PR; GM rollback = restore one file (kept on a branch as counter-rollback) |
| **2 — Spectator service, dormant** | Controller primitives + service + `EC29_Radio_Spectator.et` (same net identity: 29000 kHz / 50 km / `SPEC29_KEY` — the interop ABI) + tier components + flat/quiet ACPs + hearing-profile setting (FLAT only) | EC29 | Dormant-by-construction (no caller exists); two-client dormancy test vs old V3; revert freely |
| **3 — V3 cutover-by-delegation** | V3 keeps all its files; camera routes voice ops through the service (`Enter/Exit/SetTransmitting/SetReceiveEnabled` passing V3's own components); gproj gains the EC29 hard dependency (§8.2 decision point). Behavior bit-identical | V3 | Fleet EC29 ≥ Phase 2; full spectator regression list; soak **one full drill** before Phase 4; rollback = V3 workshop rollback (EC29 stays, service goes dormant again) |
| **4 — V3 deletion** | Delete `SPEC29_VONController.c`, tier classes, both ACPs, `Radio_spectator.et`; body prefab slots point at EC29 resources; prefs keep feeding `SetReceiveEnabled`; one-time prefs import | V3 | Only after Phase-3 soak is clean; rollback = pinned pre-dependency V3 release (§8.2) |
| **5 — Options + retirement** | MODULATED ACP pair live (two-client whisper field test is the gate — the one catastrophic-if-wrong branch: a context bug here makes whispers full-volume for living players); heuristic demoted to loud-warn permanent; `EC29_Debug.VERBOSE` off if not already | EC29 (+V3 if ACP lives there) | Log-driven (one op cycle of zero `SPECIAL_LEGACY` warnings for org nets) |

## 6. Mixed-version and droppability analysis

- **Old V3 + new EC29 (any phase)** — the load-bearing case, safe by construction: V3's
  radio trips *both* heuristic halves (29000 < 30000 kHz; 50 km > 10 km), so
  `SPECIAL_LEGACY` reproduces today's exemptions exactly; the service is dead code without
  a caller. Supported indefinitely.
- **New V3 + old EC29** — whole-project script compile failure (Enfusion compiles all loaded
  addons together): the server fails **loud at startup**, never silently. Prevented by
  provider-first ordering + V3's gproj dependency; workshop minimum-version enforcement is
  assumed absent.
- **Coin-flip window (Phase 1)** — both `DefaultPlayerController.et` overrides live; both
  outcomes acceptable once the script-side write ships (worst case = today's HUD-icon loss).
- **Droppability (the real cost, stated plainly):** from Phase 3 onward, delisting EC29
  kills spectator voice (V3 won't compile without it). Today "drop the broken mod" is the
  org's emergency lever (2026-08-21 lineage). Mitigations: the hard dependency lands only
  at Phase 3 (everything earlier stays droppable); a **pinned pre-dependency V3 release**
  is kept as the lockstep-rollback pair; and the decision itself is §8.2, not buried here.

## 7. Cross-repo PR list

| PR | Repo | Content | Sequenced after |
|---|---|---|---|
| PR-0a | 29th_Spectator_V3 | Comment-only: record the verified invariant that EC29's usability re-syncs cannot re-arm the direct-speech kill (closes their documented open question); document 29000 kHz/50 km/`SPEC29_KEY` as the interop ABI | — (immediate) |
| PR-0b | 29th_Game_Master | Comment/README: identify dependency `69D375C3CB13F8CD`; note the pending controller-prefab consolidation | — (immediate) |
| PR-1 | 29th_Game_Master | Delete `prefabs/SystemOverrides/DefaultPlayerController.et` (+ .meta) | EC29 Phase 1 fleet-confirmed |
| PR-2 | 29th_Spectator_V3 | Cutover-by-delegation (Phase 3): service calls + gproj dependency | EC29 Phase 2 fleet-confirmed |
| PR-3 | 29th_Spectator_V3 | Deletion (Phase 4): remove VON stack, re-point prefab slots | Phase-3 soak clean |

## 8. Unit decisions required before implementation

1. **Enemy VON names policy.** EC29 currently force-reveals (`m_bAlwaysShowEnemyNames=true`);
   GM's prefab hides. These contradict *today*, coin-flip resolved. One replicated setting
   with one default must win — recommend **hide** (GM's opsec intent) with mission-header
   override, but this is a leadership call, and whichever default ships is a fleet behavior
   change on non-GM servers that must be in the release notes.
2. **Accept the V3→EC29 hard dependency** (Phase 3+). Recommendation: accept — EC29 is
   already de facto mandatory voice infrastructure on every org server — with the pinned
   pre-dependency V3 release as the standing emergency pair.
3. **Spectator whisper policy** stays FLAT (today's behavior) as default; MODULATED ships
   later as a header-selectable option (Phase 5). Decision needed only on whether Phase 5
   is wanted at all.
4. **`EC29_Debug.VERBOSE`** flip timing (standing release-gate item; currently kept on
   deliberately for field debugging).

## 9. Open items feeding Phase 0

- Confirm 29thIDSpectate2 is not in any fleet server modlist (log evidence exists but was
  not re-verified; if ever relisted, its dead-player editor VON runs at editor gain).
- Identify GM's second gproj dependency `69D375C3CB13F8CD` (could hide a third
  `DefaultPlayerController` override).
- FortMeade regression case for anything touching editor policy: up to 40 concurrent
  editor senders — the strict-`<` equal-gain co-ownership in the 400 ms hold
  (`EC29_VON_VoNComponent.c:298`) is what keeps that correct and is untouchable.

## 10. Regression contract (fixes #13–#24)

Carried as the standing checklist for every phase PR: safe early-outs and the
no-PlayerController DS guard untouched; receiver guard decision tree, ready flag, and
`STABILIZATION_DELAY_MS` untouched (only the exemption *source* changes, identically);
usability re-sync sites untouched (radio-entry-only, verified); squelch `IsMuted` checks
verbatim; CH1-left/CH2-right routing untouched (special-net branch changes source of truth
only); 5 km RF cap keeps its perf role; editor steady-1.0 gain and the strict-`<` hold
byte-identical; no new code path ever calls methods on `ChimeraWorld.GetRadioManager()`.
