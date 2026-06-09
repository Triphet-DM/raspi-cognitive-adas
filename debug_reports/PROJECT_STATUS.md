# PROJECT STATUS

> Source of truth / high-level dashboard for raspi_project v11.
> Overwritten whenever a major architectural decision changes.
> Detailed history lives in the dated session reports in `debug_reports/`.

**Last updated:** 2026-06-07
**Branch:** `fix-gil`
**Latest commit:** `bf3dd54 feat(lifecycle): add SpeedSignLifecycle scaffolding`

---

## Current Status

Active work: redesigning the speed-limit sign confirmation subsystem from
cooldown-driven logic to a lifecycle/state-machine architecture.

- Step 1 (lifecycle scaffolding) is committed and Pi-verified.
- Step 2 (shadow implementation) exists in the working tree but is **uncommitted
  and architecturally superseded** — it uses voter-winner identity, which the
  2026-06-07 review rejected.
- The approved path forward is the **Level 1** design (classifier = value
  authority, class-agnostic presence). Not yet implemented.

No production behavior changes are deployed beyond the committed Step 1 scaffold
(which is inert).

---

## Stable / Verified

- **Threads = 2** is the best-performing NCNN configuration on the Pi.
  (`--threads` controls NCNN intra-op parallelism only, not any pipeline worker
  pool.)
- **FP16 arithmetic experiment produced no measurable improvement** and was
  reverted.
- Installed NCNN appears to **lack active ARM82 (arm82) FP16 kernels**, so
  `use_fp16_arithmetic` silently no-ops (the Cortex-A76 CPU itself supports
  `asimdhp`; the limitation is the library build).
- **Step 1 SpeedSignLifecycle scaffolding committed** (`bf3dd54`).
- **Pi build successful** (`[100%] Built target app`).
- **Runtime smoke test successful** (app launches, detection pipeline normal, no
  regressions).
- Prior fixes remain in place: async-camera GIL deadlock, double-buffer per-slot
  mutex, per-class ROI ownership.

---

## Working Tree (Uncommitted)

- Step 2 **shadow** implementation present in 4 modified files:
  - `src/decision/SpeedSignLifecycle.cpp`
  - `src/decision/SpeedSignLifecycle.h`
  - `src/main.cpp`
  - `src/utils/Types.h`
- Shadow-only: it observes and logs `[LC-SHADOW]`, discards its return, and does
  **not** affect decisions.
- **Current Step 2 uses voter-winner identity.**
- Local header syntax check only (`g++ -fsyntax-only`); **not** Pi-built.
- ⚠️ **Do not commit Step 2 as-is.** It is superseded and to be replaced by
  Level 1.

---

## Architecture Decisions

- Speed-limit confirmation moves from cooldown-driven logic to a
  **lifecycle/state-machine** model.
- **Identity and value are separate concerns** answered by different mechanisms:
  - Identity / persistence — "Is this still the same physical sign?"
  - Value — "What speed limit is it?"
- Persistence and value-change are **orthogonal**: a genuine limit change is
  detected by a confident classifier-value change, independent of whether presence
  drops (so gapless 50→60→80 transitions are still caught).
- Cooldown is **not** an identity mechanism; it is demoted to a safety net.

---

## Approved Direction (Level 1)

- **Classifier is the value authority.**
- `announced_value` comes from **classifier output** (the only value exposed
  downstream / in `[LC-SHADOW] FIRE`).
- **FIRE is based on classifier-value change** (initial `"" -> V`, or
  `V_old -> V_new`), not on voter-winner change.
- Speed-sign **persistence is class-agnostic presence based** (any of the five
  speed classes present refreshes `last_seen`).
- `candidate_value` (voter winner) is demoted to trigger / ROI selector only.
- **Cooldown becomes a safety mechanism**, not an identity mechanism.
- **Shadow validation before authority cutover.**

---

## Rejected / Superseded Designs

- **Voter-winner identity** — episode identity tied to the unstable YOLO sub-class.
  YOLO flicker (sign_90 → sign_80 → sign_90 on one physical sign) creates false
  value-change events. (This is what the current uncommitted Step 2 code does.)
- **Cooldown keyed by YOLO class while the classifier overrides the final value** —
  the suppression gate keys on the YOLO/voter class but `activate()` keys on the
  classifier-corrected output; when they diverge, re-confirmation is never
  throttled.
- **Treating the YOLO sub-class as the authoritative speed-limit value** — YOLO
  digit recognition is unstable; the classifier is the authority.

---

## Current Performance Baseline

| Config | FPS | infer |
|---|---|---|
| **Threads = 2** (current default) | ~10 | ~93 ms |
| Threads = 3 (previous default) | ~8 | ~116 ms |

Inference dominates frame time. FP16 arithmetic gives no gain (see Stable /
Verified). Vulkan gives no gain (model too small to amortize GPU offload).

---

## Known Issues

- **Repeat-confirmation bug:** current cooldown logic can repeatedly confirm the
  same physical speed sign when the YOLO class and the classifier value disagree
  (the case the Level 1 lifecycle is designed to fix).
- `classify_ms` metric needs re-verification once the lifecycle drives
  classification.
- TemporalVoter tie-break is alphabetical, not recency-based (low priority;
  orthogonal to the lifecycle work — do not fold into it).

---

## Next Immediate Tasks

1. **Rework the shadow lifecycle toward the Level 1 architecture** (classifier
   authority, class-agnostic presence, FIRE on classifier-value change), replacing
   the superseded voter-winner Step 2 code.
2. **Add shadow instrumentation for classifier-value stability** (count
   single-sample disagreements and co-visible oscillation).
3. **Measure K=1 vs K=2 from data**, not assumptions (current lean: K=1, upgrade
   only if instrumentation shows confident single-sample disagreement / co-visible
   oscillation).
4. **Validate 50→60→80 transition scenarios** (plus override and dropout) by
   diffing `[LC-SHADOW]` against `[CONFIRMED]` on real footage.

---

## Resume Point For Next Session

- Read **PROJECT_STATUS.md** first (this file — source of truth).
- Read the latest **session report** second (`2026-06-07_session_report.md`).
- Continue from the **Level 1 lifecycle redesign**.
- **Do not** continue from the current voter-winner identity design (it is
  superseded; do not commit it as-is).
