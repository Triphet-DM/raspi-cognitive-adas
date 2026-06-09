# 2026-06-07 Session Report

## Summary

This session focused on three major areas:

1. Inference performance investigation and optimization
2. Speed-sign classification authority validation
3. Lifecycle architecture redesign for speed-limit signs

No production behavior changes were deployed. Step 1 (lifecycle scaffolding) is
committed and Pi-verified. A Step 2 shadow implementation exists in the working
tree (uncommitted) but is **already considered architecturally superseded** by an
approved Level 1 redesign that is not yet implemented. See
"Implementation State at End of Session" for the precise state.

---

# 1. Inference Performance Investigation

## Problem

Observed FPS remained lower than expected on Raspberry Pi despite previous
optimizations.

Typical runtime:

* FPS ≈ 8–9
* NCNN inference dominated total frame time

Goal: determine whether thread configuration or FP16 settings were limiting
performance.

## Thread configuration

Tested multiple NCNN `num_threads` configurations. Note: `--threads` controls
**only** NCNN intra-op (OpenMP) parallelism inside the detector/classifier nets;
it does **not** size any pipeline worker pool (the async detector is a single
fixed thread).

### Threads = 3

* FPS ≈ 8.3
* infer ≈ 116 ms
* higher scheduling overhead (CPU oversubscription against camera + main + Python)

### Threads = 2

* FPS ≈ 10.2
* infer ≈ 93 ms
* more stable frame timing

Result: **Threads=2 consistently outperformed Threads=3** (~+23% FPS, ~-20%
infer). The previous default `threads=3` was past the sweet spot for this 4-core
async layout. Default changed to 2.

---

## FP16 Investigation

### Hypothesis

Enabling `net.opt.use_fp16_arithmetic = true` might further reduce inference time.
(`use_fp16_packed`/`use_fp16_storage` are already ON by NCNN default; only
arithmetic was off.)

### Result

Measured baseline vs FP16-arithmetic on the Pi:

* FPS ≈ 10.2 (both)
* infer ≈ 93 ms (both)

No measurable improvement. Most likely cause: the installed NCNN build does not
ship active ARMv8.2 (arm82) FP16 kernels, so the option silently no-ops at runtime
(the Cortex-A76 CPU itself supports `asimdhp`, so the limitation is the library
build, not the hardware).

Conclusion: FP16 arithmetic is a no-op for this deployment configuration. The
experiment was reverted and FP16 is considered closed for this project version.

---

# 2. Speed Sign Classification Validation

## Original Concern

YOLO frequently misclassified speed-limit signs:

* Real sign_50 detected as sign_90
* Real sign_60 detected as sign_80
* Real sign_60 detected as sign_90

This raised the question of whether YOLO should remain the authority for
speed-limit values.

## Real-World Validation

Multiple test images and runtime logs were evaluated.

Representative example:

* YOLO: sign_90
* Classifier: sign_50 (conf = 1.00)
* Physical sign: 50

Repeated testing showed:

* YOLO digit recognition unstable
* SpeedSignClassifier highly stable, confidence typically near 1.00
* Classifier consistently corrected YOLO mistakes

Conclusion — for speed-limit signs:

* YOLO = localization, ROI generation, trigger source (NOT final value authority)
* Classifier = final value authority

---

# 3. Cooldown / Repeat Announcement Issue

## Original Design

Cooldown was keyed by individual speed-sign classes
(sign_50 / sign_60 / sign_80 / sign_90 / sign_100).

## Problem discovered

The suppression gate keys on the **YOLO/voter** class, but `cooldown.activate()`
keys on the **classifier-corrected output**. When these diverge (the whole point
of the classifier), the class YOLO keeps emitting is never the class held in
cooldown, so re-confirmation is never throttled. Concretely, if YOLO oscillates

    sign_90 -> sign_80 -> sign_90

while the classifier consistently returns sign_50, the system can repeatedly
trigger confirmations/announcements for the same physical sign.

Cooldown was masking symptoms rather than solving the underlying **identity**
problem.

---

# 4. Lifecycle Architecture Review

A large design review was conducted around identity, persistence, value authority,
and re-announcement rules.

## Key realization

Two independent concerns were being conflated:

* **Identity / persistence** — "Is this still the same physical sign?"
* **Value** — "What speed limit does this sign represent?"

These must be answered by different mechanisms, in different namespaces, updating
at different rates (YOLO is per-frame; the classifier runs only at confirm).

## New direction (roles)

* **YOLO** — detect presence, provide ROI, trigger classification. Not the value
  authority.
* **Classifier** — determine the speed value; the value authority.
* **Lifecycle** — episode management, persistence, re-arm, announcement
  suppression.
* **Cooldown** — anti-spam safety net only. Not identity management.

## Identity vs. value are orthogonal

A genuine speed-limit change is detected by a **confident classifier-value
change**, which is independent of whether presence drops. This is why a gapless
50→60→80 transition is still caught even though class-agnostic presence never goes
absent: persistence and value-change are separate mechanisms.

---

# 5. SpeedSignLifecycle Implementation

## Step 1 — committed and verified

Implemented:

* `EpisodeState` enum (Armed / Confirmed / Releasing)
* `ActiveEpisode` struct
* `SpeedSignLifecycle` class shell
* `update()` stub, `reset()`

Characteristics: no runtime wiring, no behavior change, compile-only scaffolding.

Status: committed (`bf3dd54`), built successfully on the Pi, runtime smoke test
passed (application launches, detection pipeline operates normally, no
regressions).

### Timestamp design decision (Step 1)

`confirmed_at` and `last_seen` are intentionally **left default-constructed**, not
initialized to `Clock::now()`. The `state` field is the validity discriminator:
when `state == Armed`, the timestamps are intentionally unset. This makes misuse
surface loudly instead of silently, and matches existing codebase conventions
(`BestROI::valid`, cooldown map-absence). Invariant comments were added to both
fields.

## Step 2 — shadow implementation (uncommitted, SUPERSEDED)

After Step 1, a Step 2 **shadow** implementation was written and applied to the
working tree (not committed):

* `src/decision/SpeedSignLifecycle.cpp`
* `src/decision/SpeedSignLifecycle.h`
* `src/main.cpp`
* `src/utils/Types.h`

It implements a passive shadow state machine fed from existing voter/cooldown
output, logs `[LC-SHADOW]` (FIRE / RELEASING / RE-SEEN / RE-ARM by default;
per-event SUPPRESS behind `--lc-verbose`), and discards its return so decisions
are unaffected. It was header-syntax-checked locally (`g++ -fsyntax-only`); it has
**not** been built on the Pi.

**This Step 2 implementation uses voter-winner identity.** During today's
architecture review we concluded this is insufficient: YOLO class flicker
(sign_90 → sign_80 → sign_90 on one physical sign) creates **false value-change
events**, because episode identity is tied to the unstable YOLO sub-class. The
implementation is therefore **architecturally superseded** and is expected to be
**replaced** by the approved Level 1 design below — it should not be promoted to
authority, nor committed as-is without first being reworked.

## Approved Level 1 design (not yet implemented)

* Classifier = value authority.
* `announced_value` = classifier output (the only value exposed downstream / in
  `[LC-SHADOW] FIRE`).
* `candidate_value` (voter winner) demoted to trigger / ROI selector only — never
  identity, never the FIRE input.
* Class-agnostic speed-sign **presence** tracking drives persistence / re-arm
  (any of the five speed classes present refreshes `last_seen`).
* **FIRE on classifier-value change** (initial acquisition `"" -> V`, or
  `V_old -> V_new`), not on voter-winner change.
* Persistence and value-change are independent mechanisms (gapless transitions are
  caught by value-change).
* Cooldown becomes a safety mechanism only (anti-spam refractory + plausibility).
* Shadow instrumentation must precede any authority cutover.

---

# 6. Build Verification

* Step 1: built on the Pi — `[100%] Built target app`; runtime smoke test passed;
  no regressions. (`src/decision/SpeedSignLifecycle.cpp` was added to CMake in
  Step 1.)
* Step 2 (working tree): local header syntax check only (`g++ -std=c++17
  -fsyntax-only`). **Not** built on the Pi.

---

# 7. Git Status

* Branch: `fix-gil`
* Latest commit: `bf3dd54 feat(lifecycle): add SpeedSignLifecycle scaffolding`
  (Step 1)
* Uncommitted working-tree changes: Step 2 shadow (4 files listed above).

---

# Implementation State at End of Session

### Committed state

* `bf3dd54` — Step 1 lifecycle scaffolding. Pi-built, smoke-tested, no
  regressions.
* All prior fixes (GIL, double-buffer mutex, per-class ROI) remain in place.

### Working-tree state (uncommitted)

* Step 2 shadow implementation present in 4 modified files:
  `SpeedSignLifecycle.cpp`, `SpeedSignLifecycle.h`, `main.cpp`, `Types.h`.
* Shadow-only: observes and logs, does not affect decisions.
* Local header syntax check only; not Pi-built.

### Approved architecture direction (Level 1)

* Classifier = value authority; `announced_value` = classifier output.
* Class-agnostic presence for persistence / re-arm.
* FIRE on classifier-value change.
* Cooldown demoted to safety net.
* Shadow instrumentation before authority cutover.

### Known superseded design elements

* **Voter-winner identity** (current Step 2 working-tree code) — superseded;
  fails on YOLO class flicker (false value-change). To be replaced by Level 1
  (classifier authority + class-agnostic presence). Do not commit as-is without
  rework.

---

# Pending

* **Re-implement the shadow as Level 1** (classifier-authoritative
  `announced_value`, class-agnostic presence, FIRE on classifier-value change),
  replacing the superseded voter-winner Step 2 code.
* **K (value-change sustain) decision — measure, don't guess.** Agreed approach:
  1. instrument the shadow to count classifier-value instability (single-sample
     disagreements: `pending_count` reaches 1 then resets) and co-visible
     oscillation,
  2. collect shadow data on real driving footage,
  3. choose K from the measurements. Current lean: K=1 (the voter already gives
     trigger-side hysteresis), upgraded only if data shows confident single-sample
     classifier disagreements or co-visible oscillation.
* Build Step 2 / Level 1 shadow on the Pi and validate `[LC-SHADOW]` against
  `[CONFIRMED]` on the override, 50→60→80 transition, and dropout scenarios.
* Authority cutover (Level 1 becomes decision authority for speed signs) — only
  after shadow validation.
* Decide gapless-transition re-classification policy for authority mode
  (entry + bounded re-sample) — relevant once the classifier stops running on
  every confirm.

No known blockers at end of session.
