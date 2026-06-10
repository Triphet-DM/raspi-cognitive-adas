# 2026-06-09 Session Report

## Summary

This session was almost entirely **architecture design**, followed by the **first
implementation step**. We retired the superseded "Level 1 / voter-winner" shadow
direction and replaced it with a **three-layer belief-state architecture
(L1/L2/L3)**, froze it as a v1 behavior contract, then implemented and unit-tested
the first layer (**L2 CurrentSpeedLimitManager**).

No production behavior changed. No Pi build/runtime this session (design + one pure
logic module). L2 was built and tested on the Windows dev machine with g++.

Format note: this session ran as a design "meeting" (Diamond + Claude + GPT). Many
decisions below are the result of deliberately challenging assumptions.

---

# 1. Conceptual Reframe (the core shift)

The central realization that reorganized everything:

> **"Current Speed Limit" is a belief state to be *estimated*, not an output of the
> perception pipeline.**

The sign is *evidence* of the limit; the limit persists in space until contradicted.
The old design conflated "current limit" with "what perception sees this frame",
which is the root of the repeat-confirmation bug.

Consequences:
- Separate four concerns that were previously mixed: **detection / presence /
  value / announcement**.
- **Authority split:** YOLO = presence + ROI localization (NOT value). CLS = value
  authority, but only at confirm events (sparse, event-driven — *not* a continuous
  observer).
- A genuine limit change is detected by a **confident CLS value change**, which is
  independent of whether presence drops (identity and value are orthogonal).

---

# 2. The Three-Layer Architecture (frozen v1)

| Layer | Name | Responsibility | Type |
|---|---|---|---|
| **L1** | SignEpisodeLifecycle | Armed/Confirmed/Releasing, presence debounce, re-arm; emits `EpisodeConfirmed{value, fresh}` | event detection / episode segmentation |
| **L2** | CurrentSpeedLimitManager | UNKNOWN/ACTIVE belief; commit on CLS value change with K-hysteresis; no-forget | state estimation |
| **L3** | AnnouncementPolicy | CHANGE / REMINDER / SUPPRESS | notification policy |

Data flow: `perception → L1 → L2 → L3` (fast+noisy feeds slow+stable; no fast layer
writes the stable state directly). A thin **ShadowSpeedLimitPipeline** facade owns
and orchestrates L1→L2→L3.

### Key behavioral rules (frozen)
- **L1 fresh vs continuation:** `fresh=TRUE` only on `Armed→Confirmed` (i.e. after a
  presence gap / re-arm). Every re-confirm during continuous presence is
  `fresh=FALSE` (includes gapless value changes — L1 only reports, L2 decides).
- **L2 no-forget:** no edge back to UNKNOWN (camera-only cannot know "no limit").
  Value changes only on confirmed CLS value change; presence loss never touches the
  value. `age` is carried as telemetry only.
- **L2 K-hysteresis:** acquisition (UNKNOWN→first) commits immediately (no K).
  A *different* value must be confirmed K times consecutively before commit. Same
  value reconfirm refreshes + clears pending. Default **K=1** (degenerate: commits
  immediately). K is configurable.
- **L3 CHANGE always fires**, never blocked by reminder cooldown (safety: adopting a
  lower limit late is dangerous). **REMINDER** only for a fresh episode (presence
  gap) of the *same* current value, gated by a reminder cooldown. Continuation →
  silent.
- **Reminder cooldown = global single timestamp** for v1 (not per-value), to keep it
  simple. CHANGE also resets the reminder timer.

---

# 3. The Frozen Behavior Contract (v1 shadow spec)

Three tables were produced and frozen (full versions live in the chat history; the
behavior is summarized here):

**L1 Trigger Table** — Armed/Confirmed/Releasing × {confirm, presence, absence
timeout} → next state + emit. `fresh=TRUE` only Armed→Confirmed. Re-arm when
`now - last_seen ≥ rearm_after`.

**L2 Transition Table** — 4 outcomes:
- `Acquire` (UNKNOWN→ACTIVE, changed)
- `Reconfirm` (same value, refresh + clear pending, not changed)
- `Pending` (different value, streak < K, not changed)
- `Change` (different value, streak ≥ K → commit, changed)

Streak rule: if `V==pending_value` then `count++` else `pending_value=V; count=1`.

**L3 Decision Table** — `(changed, fresh, cooldown_elapsed)`:
1. `changed=TRUE` → CHANGE (always, bypass cooldown)
2. `!changed & fresh & elapsed` → REMINDER
3. `!changed & fresh & !elapsed` → SUPPRESS-CD
4. `!changed & !fresh` → SUPPRESS-CONT

End-to-end consistency was verified by hand on 4 scenarios: first sign, gapless
50→60, continuous repeater, and same-value re-encounter on a new road — all behave
as designed.

---

# 4. Decisions: Approved / Cut / Deferred / Parked

### Approved (kept)
- 3-layer separation; YOLO=presence/ROI, CLS=value.
- L1 fresh-vs-continuation + presence-based re-arm.
- L2 belief state, value-driven, no-forget, K (default 1, bench-measured).
- L3 CHANGE-always + REMINDER(fresh + presence-gap + cooldown).
- Gapless transitions handled by the value axis — **no dedicated machinery**.
- At cutover: remove voter-input suppression (L3 owns anti-spam); re-sample CLS on
  fresh episode OR voter-class-change; skip on same-class continuation.
- Two distinct timers, two owners: `rearm_after` (L1, presence debounce) ≠
  `reminder_cooldown` (L3, anti-spam).
- Object boundaries: **3 plain concrete classes + thin facade**. No interfaces, no
  inheritance, no event bus, no observer, no DI, no generics.

### Cut / simplified (and why)
- **STALE state → cut.** Replaced by **age-as-display** (human judges staleness).
  Reason: `T_stale` depends on road sign spacing, which is **not bench-validatable**
  camera-only. A parameter you cannot validate is a liability; design it away.
- Gapless-specific machinery → none (value axis covers it).
- ROI-quality / proximity / box-growth re-sample trigger → not used in v1
  (value-blind, over-triggers, smuggles in tracking). Box-growth kept only as a
  deferred phase-2 fallback if stuck-YOLO gapless misses are observed.
- Spatial tracking / identity layer (L2 identity) → not built; bench-test graceful
  degradation only.

### Deferred
- **L4 NotificationManager / Audio Manager** — audio queue, latest-wins delivery,
  speaker-busy state, priority, audio spacing. Deferred until the speaker hardware
  arrives and real audio logs exist (avoid designing hypothetical solutions).
  Design conclusions recorded for when it resumes: serialized slow resource →
  single-slot latest-wins overwrite (same pattern as the camera double-buffer),
  own thread (never block perception), non-preemptive within category, priority
  across categories, collapse-to-latest (NOT FIFO — state announcements must be
  fresh not complete).

### Rejected / superseded
- **Voter-winner identity** (the Step 2 shadow, archived in `2353d44`) — episode
  identity tied to the unstable YOLO sub-class → YOLO flicker (90→80→90 on one
  physical 50 sign) creates false value-change events. Superseded by L1/L2/L3.

---

# 5. Validation Constraint (shaped the architecture)

A hard project constraint was stated: **bench/controlled testing is available; large
scale real-road validation is not.** This became a design rule:

> Every behavioral parameter must be (a) bench-validatable, (b) benign-failure with a
> conservative default, or (c) designed away. Zero parameters whose correct value
> cannot be determined.

Mapping:
- `K` ← bench (CLS single-sample misread rate, measured by showing each printed sign
  repeatedly).
- `rearm_after` ← bench (measured YOLO dropout duration).
- `reminder_cooldown` ← benign default (failure = mild over/under reminding).
- `T_stale` ← designed away (age display).
- gapless / co-visible frequency ← designed away (value axis; graceful degradation).

Method for the genuinely un-benchable "is the voter fast enough for a transition?":
`detection_distance` (bench) ÷ `assumed_speed` (external knowledge) = dwell time,
compared against voter latency. Honest caveat recorded: printed-sign close-range
bench testing validates *logic* well but *absolute thresholds* only coarsely; stress
with degraded conditions and keep thresholds conservative.

---

# 6. Implementation: Step 1 — L2 CurrentSpeedLimitManager (DONE)

Files created (no existing code touched — per roadmap, existing files are touched
last at the wiring step):
- `src/decision/CurrentSpeedLimitManager.h`
- `src/decision/CurrentSpeedLimitManager.cpp`
- `tests/CurrentSpeedLimitManager_test.cpp` (framework-free, assert-based)

Design:
- `enum Outcome { Acquire, Reconfirm, Pending, Change }`. **Deviation from roadmap:**
  the roadmap said `onValue` returns `bool changed`; changed to an `Outcome` enum
  because the K-decision telemetry must count `Pending` separately from `Reconfirm`
  (a `bool` collapses both to false). `static is_change(Outcome)` lets the facade
  derive the `changed` flag for L3. **L2 stays pure — no logging inside; logging is
  the facade's job.**
- `current()` returns `optional<string>` (nullopt = UNKNOWN).
- `age(now)` is telemetry for the future STALE/display decision (not used to decide
  anything yet).
- K clamped to `>= 1` in the constructor.

### Build & test result (this machine, g++)
```
g++ -std=c++17 -Wall -Wextra -I src \
    src/decision/CurrentSpeedLimitManager.cpp \
    tests/CurrentSpeedLimitManager_test.cpp -o l2_test
=> BUILD CLEAN (no warnings)
=> 33/33 checks passed
=> exit 0
```
Covered: all 4 Transition-Table rows; K=1 immediate commit; K=2 hysteresis; noise
absorption (60 then back to 50 clears pending); third-value streak restart; reset;
K clamp; age refresh. Build artifact removed; only the 3 source files remain.

---

# 7. Implementation Roadmap (frozen)

Order chosen = leaves-first (pure, riskless, testable) before the migration/integration:

1. **L2** CurrentSpeedLimitManager — DONE (this session).
2. **L3** AnnouncementPolicy — next. Pure logic, decision table → unit test.
3. **L1** (refactor existing SpeedSignLifecycle → strip to presence state machine) +
   **ShadowSpeedLimitPipeline** facade + **main.cpp wiring** + Types.h `--shadow`
   flag + CMake — done as ONE coordinated step (changing L1's signature breaks the
   existing `lifecycle.update()` call in main.cpp, so they must move together to
   avoid a broken-build window).
4. **Bench validation** — diff `[SHADOW][L3]` against existing `[CONFIRMED]` on the
   bench scenarios.

L1 migration is mostly *deletion*: remove `classifier_` (and the CLS dependency),
`max_latch_`, `safety_refractory_`, `last_announce_`, `has_announced_`,
`suppressed_count_`, and `ActiveEpisode.{candidate_value, announced_value,
confirmed_at}`. `is_speed()` (presence helper) relocates to the facade/wiring.

---

# 8. Shadow Wiring Plan (frozen, for Step 3)

Log-only, zero behavior change, **no extra inference** (reuse the existing CLS
`output`). Minimal exposure: hoist the CLS-corrected `output` out of the confirm
block into `confirmed_value` (read-only). Per processed frame, compute class-agnostic
`presence` from the full `detections` vector and call
`pipeline.tick(presence, vote.confirmed, confirmed_value, frame_index, now)` at both
run_decision hook sites (sync + async). L1 ticks every frame; L2/L3 run only when L1
emits. Behind a `--shadow` flag (default off). Telemetry to log: `age` distribution
(STALE decision) and `Pending` event counts (K decision).

Exit criteria → authority cutover: shadow does NOT repeat-announce on flicker that
old `[CONFIRMED]` repeated; gapless/re-encounter/re-arm behave per tables; K=1
confirmed (or shown to need K=2); no FPS/latency regression.

---

# 9. Performance Baseline (unchanged this session)

| Config | FPS | infer |
|---|---|---|
| **Threads = 2** (default) | ~10 | ~93 ms |
| Threads = 3 (old) | ~8 | ~116 ms |

Inference-bound. FP16 arithmetic = no-op (NCNN build lacks active arm82 kernels).
Vulkan = no gain (model too small). No new perf work this session.

---

# 10. Git / Working-Tree Status

- Branch: `fix-gil`
- HEAD: `2353d44 wip: shadow lifecycle before level1 redesign` — archival checkpoint
  containing the **superseded** Step 2 voter-winner shadow + the 06-07 docs + a file
  deletion. Created during/around this session by the user.
- **Uncommitted (this session's work):**
  - `src/decision/CurrentSpeedLimitManager.h` (new, L2)
  - `src/decision/CurrentSpeedLimitManager.cpp` (new, L2)
  - `tests/CurrentSpeedLimitManager_test.cpp` (new)
  - `debug_reports/2026-06-09_session_report.md` (this file)
  - `debug_reports/PROJECT_STATUS.md` (updated this EOD)

### Cleanup item still needing a human decision
- `debug_reports/picture results/Screenshot 2026-06-06 142354.png` was **deleted**
  inside commit `2353d44`. Unknown whether intentional. It is likely 06-06 validation
  evidence (classifier override). Decide restore vs keep-deleted.
- Whether `fix-gil` is pushed to a remote is unknown (affects whether the WIP commit
  could be cleanly restructured).

---

# Known Issues

- **Repeat-confirmation bug** (the reason for L1/L2/L3): old cooldown can repeatedly
  confirm the same physical sign when YOLO class and CLS value disagree. The new
  architecture fixes it by construction (CLS-value comparison, not voter-winner).
- `classify_ms` metric needs re-verification once the lifecycle drives classification
  (Bug #3, LOW).
- TemporalVoter tie-break is alphabetical, not recency-based (Bug #4, LOW; keep
  orthogonal — do not fold into this work).
- Camera-only staleness: a no-forget belief can be confidently wrong after a turn
  onto an unsigned road. Mitigated by age-as-display honesty; fundamentally bounded
  by having no map/GPS (acknowledged, accepted for v1).

---

# Resume Point For Next Session

**What is finished:**
- Architecture frozen: L1/L2/L3 + thin facade, with frozen L1 Trigger / L2 Transition
  / L3 Decision tables.
- Validation rule fixed (bench-only → every parameter validatable/benign/designed-away).
- **L2 implemented + unit-tested (33/33, clean build).**

**What is in progress / next:**
- **Step 2 = implement L3 (AnnouncementPolicy).** Pure logic, returns an `Action`
  enum (`Change / Reminder / SuppressCooldown / SuppressContinuation`), global
  reminder-cooldown timestamp, CHANGE bypasses cooldown. Add a framework-free unit
  test mirroring the 4-row decision table. Mirror the L2 pattern (pure, no logging,
  enum return).
- Then **Step 3** = L1 refactor + facade + main.cpp wiring + CMake (one coordinated
  step), then bench validation.

**What should NOT be done:**
- Do NOT promote/commit the Step 2 voter-winner shadow as authority (archived in
  `2353d44`; it will be *replaced* by the L1 refactor).
- Do NOT implement STALE behavior (cut → age telemetry/display only).
- Do NOT implement L4 / audio (deferred until speaker hardware).
- Do NOT add interfaces / inheritance / event bus / observer / DI / generics to
  L1/L2/L3 (plain concrete classes + direct calls only).
- Do NOT tune road-dependent parameters from assumptions (bench-measure or use a
  benign default).
- Do NOT change L1's signature without updating main.cpp in the same step.

**Architectural decisions already made:** see sections 2–4 (authority split,
no-forget, K-hysteresis default 1, CHANGE-always / REMINDER-on-fresh, STALE cut,
L4 deferred, object boundaries, gapless-by-value-axis).
