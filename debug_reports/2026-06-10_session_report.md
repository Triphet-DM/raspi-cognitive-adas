# 2026-06-10 Session Report

## Session Summary

This session completed the **L1–L4 belief-state speed-limit subsystem** end-to-end and
brought it to **real audio output on the Pi**. We implemented the two remaining pure
leaves (L3, L1), performed the coordinated shadow integration (facade + `main.cpp`
wiring + CMake, Step 3), then designed and implemented the audio layer (L4) and wired
it behind `--audio` (Step 4). Everything was verified on Raspberry Pi 5 and committed
as `72bd460`, pushed to `origin/fix-gil`.

The subsystem still runs in **shadow** — the legacy voter/cooldown path remains the
authority; audio follows the shadow L1/L2/L3 decisions only. **No authority cutover
has occurred.**

Format note: ran as a design "meeting" (Diamond + Claude + GPT), reviewing each piece
before implementing — especially the effectful/production-touching steps.

> ⚠️ This report is also a **bookkeeping repair**: `72bd460` accidentally committed the
> *stale* L2-era `PROJECT_STATUS.md` (it was never refreshed during the L1→L4 work).
> Both this report and a rewritten `PROJECT_STATUS.md` were authored on 2026-06-10 to
> reconstruct the true state from the commit contents.

---

## Work Completed

Implementation order followed the frozen leaves-first roadmap.

1. **L2 re-verification** — reviewed the existing `CurrentSpeedLimitManager` against the
   frozen L2 Transition Table; built clean under strict warnings; 33/33 unit checks.
2. **L3 `AnnouncementPolicy`** (pure) — implemented from the 4-row Decision Table; 23/23.
3. **L1 `SignEpisodeLifecycle`** (pure) — implemented from the L1 Trigger Table; 26/26.
4. **Step 3 — shadow integration** (coordinated, one commit-worth of edits):
   - new `ShadowSpeedLimitPipeline` facade (owns L1→L2→L3, holds `is_speed()`, logs).
   - `main.cpp`: `run_decision()` hoists CLS-corrected `output` → `confirmed_value`;
     computes **raw class-agnostic** presence; `pipeline.tick(...)` behind `--shadow`.
   - flags `--shadow`, `--shadow-k`, `--shadow-rearm-ms`, `--shadow-reminder-sec`,
     `--shadow-verbose`; `--lc-verbose` repurposed → `--shadow-verbose`.
   - old `SpeedSignLifecycle` **kept** (still emits `[LC-SHADOW]`) for side-by-side
     comparison; its state enum did not collide because the new L1 nests `State`.
5. **L4 design** — agreed D6–D10 (below), then implemented:
   - `SpeedAudioMap` (pure): `(Action, value) → "change_50.wav"`; 14/14.
   - `NotificationManager` (effectful): dedicated audio thread, single-slot latest-wins,
     `aplay` playback; `enabled=false` → no thread.
   - `tests/test_audio.cpp` standalone hardware harness.
6. **Step 4 — audio integration**: facade owns `NotificationManager`; on `is_announce`
   → `notify(action, *l2_.current())`; flags `--audio`, `--audio-dir`, `--audio-device`;
   CMake += 2 audio sources.
7. **Audio assets**: 10 Thai-voice WAVs created and validated on hardware.
8. **CLS-call investigation** (see below).

---

## Problems Discovered + Root Cause Analysis

**"No `[CLS]` line + `cls: 0.0ms`" for a confirmed `sign_50` — is the classifier being
skipped?**
- Root cause: **misleading metric, not a skip.** `cls:` in the perf line is
  `avg.classify.value()` — a rolling average over `--avg-window` (default 50) frames.
  `YoloDetector::detect()` always sets `classify_ms = 0.0`; the only nonzero write is on
  the **confirm frame** (`run_decision`, Confirm-then-Classify). One ~8 ms reading over
  50 frames ≈ 0.16 ms → displays `0.0`. So `0.0ms` does **not** prove a skip. This is the
  pre-existing **Bug #3** (`classify_ms` metric).
- The missing `[CLS]` line is **by design**: `[CLS]` prints only on **disagreement**
  (`cls_result != voter_winner`) or **low-conf** (`cls_result == ""`). A high-confidence
  **agreement** (sign_50 → sign_50) is silent. Most likely explanation for the log shown.
- Honest caveat recorded: the observation (no `[CLS]` + `0.0ms`) **cannot distinguish**
  "ran + agreed" from "skipped." Disambiguators: the startup
  `[Classifier] ... loaded` banner (rules out null classifier), and `--save-roi-debug`
  (a saved crop proves CLS ran).
- CLS call conditions (current `run_decision`): `vote.confirmed` **and** `classifier`
  non-null **and** `output ∈ speed_sign_group()` **and** a valid `roi_by_class[output]`.
  There is **no** YOLO-confidence-based skip; the saving is "classify once per confirm,"
  not "skip when YOLO is sure." No per-value special-casing.

---

## Investigations Performed

- Traced the full `run_decision` flow (YOLO → voter → CLS gate → `confirmed_value` →
  `[CONFIRMED]`) with file/line references to answer the CLS question above.
- Verified **threading safety of the shadow path**: `run_decision()` (and therefore
  `pipeline.tick()`) runs on the **main thread only** — the `AsyncDetectionWorker`
  thread runs `run_detection()` (YOLO) exclusively. ⇒ L1/L2/L3/facade need **no locks**.
  This resolved the main open risk (R5) from the L2 review.

---

## Fixes / Changes Implemented

- L3, L1 leaves; `ShadowSpeedLimitPipeline` facade; L4 (`SpeedAudioMap`,
  `NotificationManager`); `main.cpp` wiring (Steps 3 & 4); `Types.h` config; CMake.
- `--lc-verbose` → `--shadow-verbose` (old lifecycle left at default-quiet but still
  emits `FIRE`/`RE-ARM` for comparison).
- Memory deviation noted: the L3 cooldown "never announced" state uses
  `std::optional<TimePoint>` (mirrors L2's `optional` for UNKNOWN) rather than an epoch
  trick — clock-independent and test-friendly.

---

## Validation Results / Build Status / Runtime Tests

**Unit tests (framework-free, clean under `-Wall -Wextra` + strict set), Pi-verified:**

| Component | Checks |
|---|---|
| L1 SignEpisodeLifecycle | 26/26 |
| L2 CurrentSpeedLimitManager | 33/33 |
| L3 AnnouncementPolicy | 23/23 |
| SpeedAudioMap | 14/14 |

- **Full CMake build: PASS on Pi** (app compiles 100%, shadow + audio sources included).
- **`test_audio`: PASS on Pi** — sequential clips correct; **latest-wins verified**
  (burst 50→60→80→100 plays "50" then "100" only); unknown value silent; missing-file
  warns without crashing.
- **MAX98357A I2S path: PASS** — all 10 WAV assets intelligible through the 3W speaker.
- **End-to-end PASS**: Camera → YOLO → CLS → L1 → L2 → L3 → L4 → Speaker — audio plays
  from shadow decisions while legacy `[CONFIRMED]` runs untouched.

**Not yet done:** the *systematic* bench validation scenarios (50-stuck / flicker /
gapless 50→60 / re-arm / reminder) comparing `[SHADOW][L3]` vs `[CONFIRMED]` and
measuring `Pending`/`age` telemetry. This is the next session's first task.

---

## Performance Benchmarks

Unchanged this session (no perf work): **Threads=2 → ~10 FPS / ~93 ms infer** (best);
Threads=3 → ~8 FPS / ~116 ms. FP16 arithmetic = no-op; Vulkan = no gain. Audio runs on a
dedicated thread (`play_blocking` outside the lock) so it should not affect perception
FPS — **to be confirmed by measurement at the bench**.

---

## Architecture Discussions + Approved Decisions

Carried decisions remain in effect (authority split, no-forget, K=1 default,
CHANGE-always/REMINDER-on-fresh, STALE cut, gapless-by-value-axis, 3-plain-classes +
facade / no abstraction machinery). New/confirmed this session:

- **D1 — presence = raw class-agnostic speed-sign presence** from the full `detections`
  vector (NOT cooldown-filtered, NOT `top_class`-only). Reason: presence is a perception
  fact; cooldown is anti-spam. Cooldown-filtered presence would spuriously re-arm a
  still-visible sign. (Improves on the old shadow.)
- **D2 — new class `SignEpisodeLifecycle`** (the old voter-winner `SpeedSignLifecycle` is
  a different concept). Old class **kept temporarily** for `[LC-SHADOW]` comparison;
  removal is a post-validation follow-up.
- **D3 — reminder cooldown default = 180 s** (NOT the 5 s anti-spam value). Reminder is a
  re-encounter reassurance cadence, and is the project's designated *benign-default*
  parameter (failure = mild over/under reminding; CHANGE always fires). Configurable.
- **D4 — `--lc-verbose` → `--shadow-verbose`.**
- **D6 — split L4** into pure `SpeedAudioMap` (unit-testable) + effectful
  `NotificationManager` (hardware-tested). L4 adds **no** bench-tunable behavioral params
  (mechanism, not policy).
- **D7 — v1 audio backend = `aplay` subprocess** via `std::system` on the audio thread
  (proven on hardware, zero new deps, no WAV/PCM code). No `AudioBackend` interface.
  Device specified explicitly (`-D plughw:0,0`) because Pi 5 also exposes HDMI audio.
- **D8 — single-slot latest-wins** (immediate→blocks perception; FIFO→stale; rejected).
  Cross-category priority **deferred** (v1 = one category).
- **D9 — L4 attaches to L3 output inside the facade, behind `--audio` (with `--shadow`),
  non-authoritative.** Audio = the new brain's decisions, for validation-by-ear; cutover
  is a separate later step.
- **D10 — config:** `--audio-dir` (default `../assets/audio`), `--audio-device`
  (default `plughw:0,0`).

---

## Rejected / Superseded

- Immediate (synchronous) audio playback — blocks perception 1–2 s/clip. Rejected.
- FIFO audio queue — stale state announcements. Rejected (latest-wins instead).
- `libasound` direct + `AudioBackend` abstraction for v1 — YAGNI / violates "no
  interfaces"; deferred to a possible v2 if gapless/preemption is ever needed.
- Voter-winner identity (`SpeedSignLifecycle`) — superseded by L1–L4; kept only as a
  shadow comparison baseline, scheduled for removal after validation.

---

## Known Issues

- **Repeat-confirmation bug** — fixed by construction in L1–L4; **bench proof still
  pending** (next session).
- **`classify_ms` (Bug #3, LOW)** — diagnosed: rolling-average dilution, not a real skip
  (see above). The metric remains misleading; consider reporting per-confirm instead.
- **TemporalVoter tie-break (Bug #4, LOW)** — alphabetical, not recency. Keep orthogonal.
- **`Pending` + `fresh` → `REMINDER` of the old value** (edge) — with K≥2 only; K=1
  default avoids it entirely. Parked for v1; revisit if bench shows K=2 is needed.
- **`rearm_after` (600 ms) vs cooldown (5 s) interaction (R6)** — a >600 ms occlusion
  during a cooldown window re-arms L1; the next confirm is `fresh=TRUE` → a possible
  REMINDER. Arguably correct (re-encounter); measure at bench, don't pre-tune.
- **Facade-contract invariants for any future cutover (R3–R5):** `reset()` must never be
  wired to presence loss (would break no-forget); `onValue` must be called once per
  episode confirm (not per frame, or K's meaning collapses); `tick()` must stay
  single-threaded.
- **Camera-only staleness** — a no-forget belief can be confidently wrong after a turn
  onto an unsigned road (no map/GPS). Accepted for v1; surfaced via age-as-display.
- **GIL violation + CameraThread double-buffer race** — both FIXED and Pi-verified
  2026-06-06 (carried; not reopened).

---

## Current Working-Tree Status

- Branch `fix-gil`, **HEAD = `72bd460`**, in sync with `origin/fix-gil` (pushed).
- `72bd460` ("feat(audio): complete L1-L4 shadow pipeline with speaker integration")
  bundled the entire L1–L4 working tree: 4 decision sources + facade + 2 audio sources +
  5 tests + 10 WAV assets + `main.cpp` (+83) + `Types.h` (+13) + CMake (+6), plus the
  2026-06-09 report and the (then-stale) PROJECT_STATUS.md.
- **Working tree clean** at session start of this repair. The only new files are this
  report and the rewritten `PROJECT_STATUS.md` (and `.claude` memory, outside the repo).
- Old `SpeedSignLifecycle.{h,cpp}` present and compiled (comparison baseline).

---

# Resume Point For Next Session

**What is finished**
- L1–L4 belief-state subsystem implemented, unit-tested, and Pi-verified end-to-end;
  audio plays from shadow decisions; committed + pushed (`72bd460` / `origin/fix-gil`).

**What is still in progress / next**
- **Systematic bench validation** with printed signs + speaker: run the 5 scenarios
  (50-stuck, flicker, gapless 50→60, re-arm, reminder); diff `[SHADOW][L3]` against
  legacy `[CONFIRMED]`; the key check is **scenario 2** — legacy repeats while shadow
  stays silent.
- **Read telemetry** to decide **K=1 vs K=2** (`Pending` event counts) and the
  age-as-display threshold (`age` distribution).
- **Evaluate authority cutover readiness** (see invariants R3–R5).
- For bench, lower the reminder cooldown (e.g. `--shadow-reminder-sec 10`) so REMINDER is
  observable without waiting 3 min.

**What should NOT be done**
- Do not cut over authority before the bench scenarios pass.
- Do not delete the old `SpeedSignLifecycle` until validation is complete.
- Do not add interfaces/inheritance/event-bus/DI/generics; do not implement STALE; do not
  tune road-dependent params from assumptions; do not build cross-category audio priority.

**Decisions already made**: authority split; no-forget; K=1 default (configurable);
CHANGE-always / REMINDER-on-fresh; reminder cooldown 180 s benign default; STALE cut;
gapless-by-value-axis; raw class-agnostic presence; single-slot latest-wins; `aplay` v1;
audio behind `--audio`+`--shadow`, non-authoritative; object boundaries (plain classes +
facade, no abstraction machinery).

**Run reference**
```
./build/app <model/cls args> --shadow --audio [--shadow-reminder-sec 10] [--shadow-verbose]
# flags: --shadow-k <n> --shadow-rearm-ms <ms> --shadow-reminder-sec <s>
#        --audio-dir <dir=../assets/audio> --audio-device <dev=plughw:0,0>
```
