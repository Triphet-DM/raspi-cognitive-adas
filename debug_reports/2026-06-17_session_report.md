# 2026-06-17 Session Report — Speed Cutover + Brain 2 Bring-Up (Momentary Engine)

## Session Summary

A heavy **implementation** day (contrast with the 06-14/06-15 design meetings). Two big
arcs landed and were Pi-verified:

1. **Track A closed — SPEED AUTHORITY CUTOVER.** The legacy voter-input cooldown +
   `[CONFIRMED]` + `SpeedSignLifecycle` path was removed; the L1–L4 belief-state pipeline is
   now the speed authority by default. Resolved the K≥2↔cooldown coupling discovered during
   bench. K=2 made the default.
2. **Brain 2 brought up end-to-end with audio.** Built `MomentaryEngine` + `MomentaryPolicy`
   (pure), `BehaviorPolicyRouter`, genericized L4 (`NotificationManager::submit`), added
   `MomentaryAudioMap` (class→wav), recorded 10 momentary WAVs, and re-ranked the Safety
   tier. **Heard real momentary speech on the Pi.**

Five commits, all pushed to `origin/fix-gil`. Integration refactors **#1 (shared L4)** and
**#2 (two wav maps)** are DONE. Only the **Notification Arbiter** + refactor **#3**
(kill-aplay / interrupt-aware / re-delivery) remain for Brain 2.

---

## Work Completed (chronological)

1. **B-track decisions (perf sign-off):** B1 soak = Diamond runs/watches logs himself;
   **B2 real-drive soak deferred** (can't do yet); **B3 live-detect-416 CUT** — 512 @ ~19 FPS
   sync is sufficient for real-time → **imgsz locked at 512**; B4 systemd parked. Net: the
   only perf change that ships is "drop async" (already the code default).
2. **Track A bench validation:** all **5 shadow scenarios PASS** (50-stuck, flicker, gapless
   50→60, re-arm, reminder).
3. **K≥2↔cooldown coupling diagnosed** (see Root Cause).
4. **SPEED CUTOVER** implemented + Pi-verified (commit `4cc4165`).
5. **K=2 made default** (commit `50e994b`).
6. **Extract shared L4** — integration refactor #1 (commit `0c49ec5`), Pi-verified audio
   identical.
7. **MomentaryEngine + MomentaryPolicy** — Brain 2 core, pure, 36/36 tests (commit `2c06d59`).
8. **BehaviorPolicyRouter + momentary audio + re-rank** — refactor #2 + wiring (commit
   `4693470`), Pi-verified real speech.

---

## Problems Discovered

### K≥2 blocked by the legacy voter-input cooldown (bench-confirmed, NOT a bug)
With `--shadow-k 2`, a value change did **not** announce on first sight — it waited out the
5 s per-class cooldown, even for a brand-new value. K=1 hid the symptom.

## Root Cause Analysis

One **shared** `TemporalVoter` was fed cooldown-gated input:
`main.cpp:465` did `vote_input = result.suppressed ? "" : top_class`. On the 1st confirm,
`cooldown.activate(output)` (`main.cpp:532`) suppressed that class for 5 s → its vote input
became `""` → the voter could not reach ≥4/10 again → L2 never received the **2nd consecutive
confirm** that K=2 requires → the change stalled until the cooldown lapsed.

Key insight: the shadow pipeline's **`presence` was already clean** (RAW, class-agnostic,
`main.cpp:556-562`); only the **`confirmed`/`value`** signal was contaminated. The fix was
the long-documented cutover step — "remove voter-input suppression; **L3 owns anti-spam**" —
not a change to L1–L4 logic. It cannot be done in isolation because the same voter feeds the
legacy `[CONFIRMED]` path, which relies on the cooldown for its own anti-spam → authority had
to move to the shadow pipeline (with L3 anti-spam) at the same time.

## Fixes Implemented

### 1. Speed authority cutover (`main.cpp`, `Types.h`) — commit `4cc4165`
- Removed `struct CooldownManager` + instance.
- Voter now fed raw `top_class` always (dropped the `suppressed ? ""` zeroing).
- Dropped the cooldown gate on `BestROI` update.
- Removed `cooldown.activate()` + the `[CONFIRMED]` print (kept CLS correction + `confirmed_value`).
- Removed the `SpeedSignLifecycle` `[LC-SHADOW]` call + `#include`.
- `cfg.shadow` default `false`→**`true`** → L1–L4 is the authority; `--shadow` is now a no-op.
- Net `−131/+58`. Anti-spam now entirely L3: CHANGE-always / REMINDER-on-fresh@180 s /
  **SuppressContinuation** (the `!changed & !fresh` case that replaces the 5 s cooldown).
- **Pi-verified:** detection/alerts normal; **K=2 changes value on the 2nd confirm with no
  cooldown wait**; no audio spam.

### 2. K=2 default (`Types.h`) — commit `50e994b`
`shadow_k` 1→2. Now that the coupling is gone, K=2 gives flicker hysteresis against false
value-changes as designed. Acquisition (UNKNOWN→first value) still ignores K (commits on the
1st confirm); K only gates a *change* of an already-held belief.

### 3. Extract shared L4 — integration refactor #1 (`ShadowSpeedLimitPipeline.*`, `main.cpp`) — commit `0c49ec5`
`NotificationManager` moved out of the pipeline; owned in `main()`, passed by pointer
(`NotificationManager* nm_`). One audio output (one thread + one latest-wins slot) that both
brains will feed. **API unchanged → `test_audio` untouched; audio Pi-verified identical.**

### 4. MomentaryEngine + MomentaryPolicy — Brain 2 core (pure) — commit `2c06d59`
- `MomentaryEngine`: Human Memory Suppression Model — `onConfirmed(class, now)`:
  `now − last_notified[class] ≥ window ? Announce + stamp : Suppress`. **No episode
  lifecycle.** Suppress does **not** refresh the timer (window measured from last *announce*,
  not last *sighting*).
- `MomentaryPolicy`: per-class `{suppression_window, attention_rank}` table for the 10
  non-speed classes + `INTERRUPT_THRESHOLD`. Pure (holds its own known set, no ncnn) →
  g++-testable.
- **36/36** unit checks under `-Wall -Wextra`.

### 5. BehaviorPolicyRouter + momentary audio + re-rank — refactor #2 + wiring — commit `4693470`
- `BehaviorPolicyRouter::route(class)` → `Speed` / `Momentary` / `None` (pure, mirrors the
  speed set internally). **18/18** tests.
- L4 **genericized**: `NotificationManager::submit(filename)` is the generic entry; `notify`
  is now a speed convenience wrapper. `MomentaryAudioMap` (class→wav, **8/8** tests).
- Wired into `run_decision`: confirmed non-speed sign → `momentary.onConfirmed` → on
  Announce, `[MOMENTARY] ANNOUNCE` + `notifier.submit(MomentaryAudioMap::filename)`.
- Added `MomentaryPolicy.cpp` / `MomentaryEngine.cpp` / `BehaviorPolicyRouter.cpp` /
  `MomentaryAudioMap.cpp` to CMake.
- **10 momentary WAVs** recorded + placed in `assets/audio/`; `MomentaryAudioMap` filenames
  fixed to match Diamond's actual file names (5 differed). All 10 mapped files verified to
  exist on disk.
- **Pi-verified: real momentary speech plays** (e.g. School Zone, no_parking) and suppresses
  while the sign stays in frame; speed audio unchanged.

---

## Validation / Build / Runtime

- **Pure unit tests (g++ -Wall -Wextra on host):** MomentaryEngine **36/36**, BehaviorPolicyRouter
  **18/18**, MomentaryAudioMap **8/8**. SpeedAudioMap / L1 / L2 / L3 unchanged (still green).
- **Pi build + runtime:** full CMake `app` builds; cutover Pi-verified (detection/alerts
  normal, K=2 correct, no spam); shared-L4 audio Pi-verified identical; Brain 2 momentary
  audio Pi-verified audible end-to-end.
- **WAV asset check:** all 10 momentary class→filename entries resolve to existing files.

## Performance Benchmarks
No new perf runs today. Operating point reaffirmed from 2026-06-16: **sync, 512, threads=2,
~19 FPS / ~48 ms infer, CPU 50-60%.** imgsz locked at 512 (416 path cut). Decision logic
(L1–L4, Brain 2) runs in microseconds — not the bottleneck.

## Architecture Discussions / Decisions

### Approved
- **L1–L4 = speed authority** (cutover). Anti-spam owned solely by L3.
- **K=2 default** for the L2 hysteresis.
- **imgsz locked at 512** (B3 live-detect-416 cut; 19 FPS sufficient).
- **Brain 2 = MomentaryEngine** (timestamp suppression, no lifecycle); `BehaviorPolicyRouter`
  dispatch; `MomentaryAudioMap` (class→wav); shared generic L4 (`submit(filename)`).
- **Safety tier RE-RANKED (Diamond, Thai road-safety research):**
  **School Zone 30 > Pedestrian Warning 25 > Pedestrian Crossing 20**. School zones carry the
  highest systemic risk (unpredictable children + sight-line occlusion from pick-up/drop-off
  vehicles). `INTERRUPT_THRESHOLD` stays **20** but now sits at **Pedestrian Crossing** (the
  new lowest safety rank); all three remain interrupt-capable. **Only numbers changed —
  structure (single axis, threshold = lowest safety rank, sparse scale) untouched**, exactly
  as the frozen design anticipated.

### Interim (to be replaced)
- **Momentary audio submits directly to the shared L4** (no Notification Arbiter yet) →
  speed and momentary race on the single latest-wins slot (last submit wins). Acceptable for
  hearing one sign at a time; rank-based selection/preemption arrives with the Arbiter.

### Considered / nuance noted
- Counter-view on the re-rank: Pedestrian Crossing = "someone crossing **now**" (imminent
  event) could outrank School Zone (area warning). Diamond's research-backed ordering kept;
  trivially reversible (one number) if bench/road testing disagrees.

## Rejected / Superseded
- Legacy `CooldownManager` voter-input suppression — **removed** (redundant + blocked K≥2).
- Legacy `[CONFIRMED]` print path + `SpeedSignLifecycle` `[LC-SHADOW]` call — **removed**.
- The earlier provisional rank "School Zone = 20 = threshold" — **superseded** by the re-rank.

## Known Issues / Limitations
- **No Notification Arbiter yet** → momentary↔speed share the L4 slot with no rank
  arbitration (interim direct submit). No cross-brain preemption / re-delivery yet.
- **`SpeedSignLifecycle.{h,cpp}` still compiled (orphaned)** in CMake — A4 housekeeping
  (delete files + CMake entry); harmless dead weight for now.
- `MomentaryPolicy` numbers (windows 5/15/30 s, ranks) are **provisional** — bench-tune.
- Per-class precision of the Safety family still **unmeasured** (gates "low suppression").
- Carry-over speed items (LOW): classify_ms rolling-average dilution; TemporalVoter
  alphabetical tie-break; camera-only no-forget staleness.

## Current Working-Tree Status
**CLEAN.** All work committed and pushed to `origin/fix-gil`.
Today's commits (oldest→newest):
- `4cc4165` cutover speed authority to L1-L4, remove legacy cooldown
- `50e994b` make L2 K-hysteresis default 2
- `0c49ec5` extract shared L4 out of the speed pipeline
- `2c06d59` add Brain 2 MomentaryEngine + MomentaryPolicy
- `4693470` wire Brain 2 momentary engine end-to-end with audio
(Plus `d4b858a` earlier: docs snapshot.) **HEAD = `4693470`, in sync with remote.**

---

## Resume Point For Next Session

- **What is finished:** Track A (speed cutover + bench + K decision) DONE & Pi-verified;
  integration refactors **#1 (shared L4)** and **#2 (class→wav + generic submit)** DONE;
  Brain 2 core (MomentaryEngine + MomentaryPolicy + BehaviorPolicyRouter) DONE & wired;
  momentary audio audible on Pi; safety re-ranked. All committed + pushed.
- **What is in progress / next:** **Notification Arbiter** — the stateful cross-brain
  selector/preemptor (SELECT highest rank; PREEMPT iff `incoming.rank > current.rank AND
  incoming.rank ≥ INTERRUPT_THRESHOLD`). Then refactor **#3** = kill-aplay preempt +
  interrupt-awareness + Delivery Completeness re-delivery.
- **What should be done next, concretely:** (1) design/build the Arbiter sitting between both
  brains and the shared L4; route BOTH speed and momentary through it (replace the interim
  direct submit); (2) only then add kill-aplay + the Arbiter→engine "clip preempted" feedback
  edge for re-delivery.
- **What should NOT be done:** don't re-open `attention_rank`/threshold structure (settled —
  only numbers tune); don't add an episode lifecycle to momentary; don't touch L1 re-arm for
  re-delivery (it's an L3-level re-announce); don't fix `MomentaryPolicy` numbers from
  assumption (bench); don't wire `reset()` to presence loss; keep `tick()` single-threaded.
- **Decisions already made:** cutover (L1–L4 authority, L3 anti-spam); K=2 default; imgsz 512
  locked; sync (no async); Brain 2 timestamp model; single `attention_rank` axis;
  INTERRUPT_THRESHOLD = lowest safety rank (= Pedestrian Crossing after re-rank); School Zone
  30 > Ped Warning 25 > Ped Crossing 20; shared generic L4.
- **Optional housekeeping:** A4 — delete orphaned `SpeedSignLifecycle.{h,cpp}` from CMake.
