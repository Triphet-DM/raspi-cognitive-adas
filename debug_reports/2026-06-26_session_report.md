# 2026-06-26 Session Report — Viewer fix (Brain 2/Arbiter) + Brain 3 architecture LOCKED

## Session Summary
Mixed session, **no production C++/decision code changed**. Work: (a) fixed the architecture
**viewer** which was still showing a pre-cutover, speed-only system; (b) refreshed the README
on-device detection images; (c) added a repo-specific git cheat-sheet; (d) ran the **Brain 3
(drowsiness) model-selection design meeting and LOCKED the architecture**; (e) resume defensibility
review (personal artifact, not in repo). All engineering substance was in docs/viewer + the Brain 3
design decisions.

---

## Work Completed

### 1. ARCHITECTURE_VIEWER.html — Board 0 + Board 10 fixed (committed)
**Problem discovered:** Board 0 (Super System Overview) and Board 10 (Live Simulator) were stuck in
the **pre-cutover, speed-only era** — they drew the async/double-buffer path as canonical and
**omitted Brain 2 (Momentary) + the Notification Arbiter entirely.** Board 1 was already correct
(untouched). User caught this himself ("why no Brain 2 in the overview?").

**Verified against source before editing** (`run_decision` in `main.cpp`, `NotificationArbiter.h`,
`ShadowSpeedLimitPipeline.cpp`):
- **BOTH brains route through the Arbiter before L4** — speed via `ShadowSpeedLimitPipeline.cpp:91`
  (`arb_->submit(speed_rank(action), file, now, eligible)`), momentary via `run_decision`. Speed
  ranks (CHANGE 12 / REMINDER 2) < `INTERRUPT_THRESHOLD` 20 → **speed never preempts** (re-derivable);
  momentary safety (School_Zone 30, Ped 20) can.
- async path (CameraThread/double-buffer/AsyncWorker) still exists behind `--async-*` flags but is
  **deprecated** (production = sync). double-buffer fixes the cv::Mat data-race; GIL handling is a
  *separate* fix (Python C-API from a non-main thread) — they came together with async-camera but
  solve different problems.

**Fixes:**
- **Board 0:** added `BehaviorPolicyRouter` / `MomentaryEngine` / `NotificationArbiter` nodes + edges
  (`confirmed → router → momentary → arbiter → shared L4`); relabeled speed region "Brain 1". (async
  was already honestly marked optional there.)
- **Board 10 (live sim):** marked CameraThread/Double-Buffer/AsyncWorker as the **OPTIONAL --async-***
  path (SYNC = production); added router/momentary/arbiter as a **fully animated** Brain-2 band — new
  stages (router split, momentary suppression-window, arbiter PLAY/PREEMPT/DROP + re-deliver-owed),
  plus `MODS`/`simFresh`/`simLines`/edges/WHY, momentary INPUT buttons, and **scenarios G** (School_Zone
  preempts a playing speed clip) and **H** (momentary suppression window).

**Validation:** `node --check` on the extracted JS = OK; hand-traced scenario G (PLAY speed → PREEMPT
by School_Zone → SUPPRESS repeat) and scenario C (50→60 both announce).
**Note (honest):** in-sim `NOMINAL_CLIP` was compressed to **300 ms** (real scaffold ≈ 2500 ms) so
back-to-back speed CHANGEs aren't DROPped under the sim's 100 ms/frame clock — documented in a code
comment.
**Commits:** `11440ba` (fix-gil) → brought to `main` `04e1d2c` (file was byte-identical across
branches pre-edit → clean `git checkout fix-gil -- ...` bring-over, no merge).

### 2. README on-device detection images refreshed (main, `6eabca4`)
Replaced the 4 "More on-device detections" screenshots with fresh Pi-5 captures (Pedestrian-crossing /
School-Zone / speed-100 / speed-60, each with the telemetry panel). Copied from `demo/` (gitignored)
→ `README_assets/` (tracked); updated captions to match the new signs; removed 3 orphaned old images.
README **differs across branches** (main has the hardware-photo commit) → **edited main directly**, did
NOT cross-branch checkout (would have reverted main's unique content).

### 3. GIT_CHEATSHEET.md (fix-gil, `e70c28f`)
Repo-specific Thai git cheat-sheet: everyday `add/commit/push` on fix-gil; the **publish-to-main 3a/3b
recipe** for the two **unrelated-history** branches; recovery commands; "what goes on which branch"
rules. (User struggles with git mainly because the repo's two-branch/unrelated-history setup is
genuinely advanced, not because of basics.)

---

## Architecture Discussions — Brain 3 (Drowsiness) Model Selection — LOCKED
Format: design meeting **Diamond + Claude + GPT** (engineering review, GPT hard-reviewer, Claude =
honesty-check on GPT). **Full decision log = `BRAIN3_FACE_MODELS_SHORTLIST.md` → "DECISIONS LOCKED —
2026-06-26".** Key points:

### Approved decisions
- **2 cameras:** IMX477 road-facing (Brain 1/2) + **Logitech Brio 100** driver-facing (Brain 3) @
  **640×480** — separate capture, ~no contention.
- **Pipeline:** **YuNet** (face detector, zero new dep — already use OpenCV) → **PFLD-NCNN** (landmarks,
  feeds both EAR + head-pose from one inference) → **EAR + PERCLOS + Head-Pose** combined as a
  **weighted Fatigue Score** (NOT a multi-signal AND) → Tier-0 alert → Arbiter.
- **Prototype = Option B** — same PFLD-NCNN model on PC→Pi from day 1 ("what you benchmark is what you
  deploy").
- **Scheduling: ALWAYS-ON, fps floor > 0, never gated.** Cadence raised by an **Environmental Risk
  Prior** (L2 belief 90/100 + sign-absence duration + IMX477 optical-flow + driving-duration; **no
  lane detector, no GPS**) + early EAR drop. FPS tiers (bench-tune): Normal 10 / risk-high 20 /
  early-fatigue 30 / **floor 5**.
- **Arbiter: Tier-0 drowsiness = highest rank, preempts everything** (above School_Zone 30); gated by
  3-stage confirmation **Quality → Temporal → weighted Score** (prevents the worst cry-wolf = false
  positive at the top rank).
- **New behavioural category: Brain 3 = "persistent-but-must-ESCALATE"** (≠ Brain 1 say-once, ≠ Brain 2
  no-2nd-chance). **Fatigue State Machine:** Normal → Suspicious → Confirmed → Alert-Sent → {recovery
  (eyes reopened ~10 s + head stable + normal blink) → reset · fatigue persists → **ESCALATE**, never
  silent}. Needs a per-person EAR-neutral calibration phase at drive start.
- **v1 scope:** NO gaze estimation, NO yawning (MAR = v2). 5-landmark detectors ruled out for the value
  (no eyelid contour → no EAR).

### Rejected decisions
- **dlib for deploy** — 99 MB memory + off-NCNN ecosystem (NOT "landmark compute heavy": dlib's
  regressor is cheap; the slow part is dlib's *own face detector*, which we don't use).
- **Option A (dlib-68 PC → PFLD Pi)** — PFLD is natively **WFLW-98**, so iBUG-68 index reuse breaks
  anyway → single-model Option B wins.
- **Gating Brain 3 on a Brain-2 risk condition** — safety anti-pattern (drowsiness PEAKS on monotonous
  empty roads = exactly when Brain 2 is silent). "Do not gate. Modulate. Never shut down."
- **Tracking-between-detections in v1** — premature optimization (YuNet ~8 ms is cheap; measure first).
- **Multi-signal AND** for the alert — raises **false negatives** (misses real drowsiness; head-droop
  lags eye-closure; glasses defeat EAR). Use weighted score where sustained eye-closure can fire alone.

### Honesty-check log (Claude ↔ GPT)
- Claude corrected GPT: "recalibrate everything" overclaim (it's a threshold re-tune, neutralized by
  per-person calibration); dlib compute-cost claim; tracking premature; 68-vs-98 scheme self-correct.
- GPT added (accepted): Environmental Risk Prior naming; **Recovery State Machine** (the missing piece);
  3-stage gating.
- Claude's final pushbacks (accepted): drop the "lane stable" input (no lane detector); Stage C = score
  not AND; recovery needs an **escalate** exit.

### ACTION ITEM (blocks 100% lock)
**Verify a usable PFLD NCNN port on PC** — (a) scheme (68 vs 98 points), (b) quality. Everything assumes
a good port exists; if none → fallback discussion (dlib is the heavy off-ecosystem fallback).

---

## Build / Runtime / Benchmarks
No build run (no production code touched). Viewer JS syntax-checked (`node --check` OK). No new Pi
benchmarks. Baseline unchanged: **~18 FPS @512, NCNN fp32 + fp16 storage/packed, threads=2**.

## Known Issues
- Unchanged from prior reports.
- **Brain 3 not built** — only architecture locked; **PFLD NCNN port unverified** (the single blocker).

## Current Working-Tree Status
Branch **fix-gil**. EOD commit includes: this report + the Brain 3 decision log
(`BRAIN3_FACE_MODELS_SHORTLIST.md`) + PROJECT_STATUS update. Resume PDFs
(`Triphet Rungreuang*.pdf`) left **untracked** in repo root by user (personal, intentionally not
committed). Public work (viewer, README images) already on `main`.

---

## Resume Point For Next Session
- **Finished:** viewer fix (Board 0 + Board 10) pushed to fix-gil + main; README detection images
  refreshed on main; git cheat-sheet; **Brain 3 architecture LOCKED** (decision log in the shortlist
  doc).
- **In progress:** nothing mid-edit.
- **Next:** (1) **verify the PFLD NCNN port** (the only Brain-3 blocker) → then prototype on PC
  (Option B). (2) optional: add the 2-camera Brain-3 band to viewer Board 0 someday. (3) Speed/bench
  leftovers still open (K=1 vs K=2 final measured choice; one re-delivery all-angle tick on a Pi soak).
- **Do NOT:** re-open the locked Brain-3 decisions (cameras / YuNet / PFLD / always-on / Tier-0 /
  escalate / weighted-score); gate Brain 3 on Brain-2 risk; use dlib for deploy; use multi-signal AND;
  build Brain 3 before verifying the PFLD port.
- **Decided already:** full Brain-3 lock — see `BRAIN3_FACE_MODELS_SHORTLIST.md` "DECISIONS LOCKED —
  2026-06-26".
