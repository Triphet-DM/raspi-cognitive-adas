# PROJECT STATUS

> Source of truth / high-level dashboard for raspi_project v11.
> Overwritten whenever a major architectural decision changes.
> Detailed history lives in the dated session reports in `debug_reports/`.

**Last updated:** 2026-06-09
**Branch:** `fix-gil`
**HEAD:** `2353d44 wip: shadow lifecycle before level1 redesign` (archival checkpoint)

---

## Current Status

Active work: rebuilding the speed-limit confirmation subsystem as a **three-layer
belief-state architecture (L1/L2/L3)**, replacing the superseded voter-winner shadow.

- Architecture is **frozen** as a v1 behavior contract (L1 Trigger Table, L2
  Transition Table, L3 Decision Table). See the 2026-06-09 session report.
- Implementation is underway, leaves-first: **L2 is implemented and unit-tested**
  (33/33, clean g++ build). L3 is next, then L1+facade+wiring, then bench validation.
- The whole new subsystem runs in **shadow mode (log-only, zero behavior change, no
  extra inference)** before any authority cutover.

No production behavior has changed. The superseded Step 2 voter-winner shadow is
archived in commit `2353d44` and will be *replaced* (not promoted).

---

## The Architecture (frozen v1)

"Current Speed Limit" is treated as a **belief state to be estimated**, not a
perception output. Four concerns are separated: detection / presence / value /
announcement.

**Authority split:** YOLO = presence + ROI localization (not value). CLS = value
authority, but only at confirm events (sparse, event-driven).

| Layer | Class | Responsibility |
|---|---|---|
| **L1** | SignEpisodeLifecycle | Armed/Confirmed/Releasing; presence debounce; re-arm; emits `EpisodeConfirmed{value, fresh}` |
| **L2** | CurrentSpeedLimitManager | UNKNOWN/ACTIVE belief; commit on CLS value change with K-hysteresis; no-forget |
| **L3** | AnnouncementPolicy | CHANGE / REMINDER / SUPPRESS |

Flow: `perception → L1 → L2 → L3`, owned by a thin **ShadowSpeedLimitPipeline**
facade. Three plain concrete classes + facade — **no interfaces, inheritance, event
bus, observer, DI, or generics.**

Frozen rules:
- **L1:** `fresh=TRUE` only on Armed→Confirmed (after a presence gap). Re-arm when
  `now - last_seen ≥ rearm_after`.
- **L2:** acquisition commits immediately (no K); a different value needs K
  consecutive confirms; same value reconfirm refreshes + clears pending; **no-forget**
  (no edge back to UNKNOWN); `age` is telemetry only. Default **K=1** (configurable).
- **L3:** CHANGE always fires (never blocked by cooldown); REMINDER only for a fresh
  episode of the *same* current value, gated by a **global** reminder cooldown;
  continuation → silent. CHANGE resets the reminder timer.
- **Gapless transitions** are handled by the value axis (no dedicated machinery).

---

## Stable / Verified

- **L2 CurrentSpeedLimitManager implemented + unit-tested** (33/33, clean
  `g++ -Wall -Wextra`). Pure logic, no dependencies, no I/O.
- **Threads = 2** is the best NCNN config (~10 FPS / ~93 ms infer).
- **FP16 arithmetic = no-op** (installed NCNN lacks active arm82 kernels); reverted.
- Prior fixes remain in place: async-camera GIL Save/RestoreThread, double-buffer
  per-slot mutex, per-class ROI ownership. (All Pi-verified on 2026-06-06.)

---

## Working Tree (Uncommitted)

- `src/decision/CurrentSpeedLimitManager.h` / `.cpp` — **L2 (new, done)**
- `tests/CurrentSpeedLimitManager_test.cpp` — L2 unit test (new)
- `debug_reports/2026-06-09_session_report.md` — new
- `debug_reports/PROJECT_STATUS.md` — this update

These do not touch existing app sources (CMake/main.cpp untouched until Step 3).

---

## Architecture Decisions (in effect)

- Belief-state model; identity and value are orthogonal.
- YOLO = presence/ROI authority; CLS = value authority at confirm.
- L2 no-forget; presence loss never changes the value.
- K-hysteresis on value change (CLS-side), default K=1, configurable, bench-measured.
- L3 CHANGE-always / REMINDER-on-fresh-episode; reminder cooldown global for v1.
- At cutover: remove voter-input suppression (L3 owns anti-spam); re-sample CLS on
  fresh episode or voter-class-change; skip same-class continuation.
- Two timers, two owners: `rearm_after` (L1) ≠ `reminder_cooldown` (L3).
- Object boundaries: 3 plain classes + thin facade, no abstraction machinery.

---

## Approved Direction (next steps)

Implementation roadmap (leaves-first):
1. L2 — **done.**
2. **L3 AnnouncementPolicy** — pure, returns `Action` enum, global cooldown, CHANGE
   bypasses cooldown; framework-free unit test from the 4-row decision table.
3. L1 refactor (strip SpeedSignLifecycle → presence state machine) +
   ShadowSpeedLimitPipeline facade + main.cpp wiring + `--shadow` flag + CMake — one
   coordinated step (L1 signature change forces the wiring change).
4. Bench validation: diff `[SHADOW][L3]` vs existing `[CONFIRMED]`.

Shadow wiring: log-only, zero behavior change, reuse existing CLS `output` (no extra
inference), compute class-agnostic presence from `detections`, behind `--shadow`.

---

## Cut / Deferred / Rejected

**Cut:**
- **STALE state** → replaced by **age-as-display**. `T_stale` is not bench-validatable
  (depends on road sign spacing); a parameter you can't validate is a liability.
- Gapless-specific machinery (value axis covers it); ROI-quality/proximity re-sample
  trigger; spatial tracking/identity layer.

**Deferred:**
- **L4 NotificationManager / Audio Manager** — until speaker hardware + real audio
  logs exist. Recorded conclusion: single-slot latest-wins (collapse-to-latest, NOT
  FIFO), own thread, non-preemptive within category, priority across categories.

**Rejected / superseded:**
- **Voter-winner identity** (Step 2 shadow, archived in `2353d44`) — episode identity
  tied to the unstable YOLO sub-class → YOLO flicker creates false value-change
  events. Replaced by L1/L2/L3.

---

## Validation Constraint (drives parameter choices)

Bench/controlled testing is available; large-scale road validation is not. Rule:
every behavioral parameter must be **bench-validatable, benign-default, or designed
away** — zero un-determinable parameters.

- `K` ← bench (CLS misread rate). `rearm_after` ← bench (YOLO dropout).
- `reminder_cooldown` ← benign default. `T_stale` ← designed away.
- gapless / co-visible frequency ← designed away (value axis + graceful degradation).

---

## Current Performance Baseline

| Config | FPS | infer |
|---|---|---|
| **Threads = 2** (default) | ~10 | ~93 ms |
| Threads = 3 (old) | ~8 | ~116 ms |

Inference-bound. FP16 arithmetic and Vulkan both give no gain.

---

## Known Issues

- **Repeat-confirmation bug** (the reason for L1/L2/L3) — fixed by construction in the
  new architecture; validated only on the bench so far.
- `classify_ms` metric needs re-verification once the lifecycle drives classification
  (Bug #3, LOW).
- TemporalVoter tie-break is alphabetical, not recency (Bug #4, LOW; keep orthogonal).
- Camera-only staleness can be confidently wrong after an unsigned turn (no map/GPS);
  accepted for v1, surfaced via age-as-display.
- **Cleanup pending a human decision:** `picture results/Screenshot 2026-06-06
  142354.png` was deleted inside `2353d44` — confirm intentional vs restore. Push
  status of `fix-gil` unknown.

---

## Next Immediate Tasks

1. **Implement L3 (AnnouncementPolicy)** + unit test (Step 2 of the roadmap).
2. Then L1 refactor + facade + wiring + CMake (Step 3, one coordinated step).
3. Bench-validate `[SHADOW]` vs `[CONFIRMED]`; measure `age` and `Pending` to confirm
   K=1 (or upgrade to K=2).

---

## Resume Point For Next Session

- Read **PROJECT_STATUS.md** (this file) then the **2026-06-09 session report**.
- **Finished:** architecture frozen (L1/L2/L3 + tables); L2 implemented + tested.
- **Next:** implement **L3 (AnnouncementPolicy)**, mirroring L2's pattern (pure
  concrete class, enum return, no internal logging, framework-free unit test).
- **Do NOT:** promote/commit the voter-winner shadow as authority; implement STALE;
  implement L4/audio; add interfaces/inheritance/event-bus/DI/generics; tune
  road-dependent params from assumptions; change L1's signature without updating
  main.cpp in the same step.
- **Decided already:** authority split, no-forget, K=1 default, CHANGE-always /
  REMINDER-on-fresh, STALE cut, L4 deferred, object boundaries, gapless-by-value-axis.
