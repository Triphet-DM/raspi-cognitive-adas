# Session Report — GIL Deadlock, Double-Buffer Race, ROI Contamination

**Date:** 2026-06-06
**Project:** raspi_project v11 (version_2.2_cls_roi_debug)
**Branch:** `fix-gil`
**Target hardware:** Raspberry Pi 5 (ARM, 4× Cortex-A76, 8 GB)

---

## Objective

Continue the GIL investigation from 2026-06-05: get `--async-camera` mode running
without deadlock, then address the remaining critical pipeline issues (double-buffer
data race, ROI/voter class mismatch). Validate everything at runtime on the Pi.

---

## Problems Investigated

1. **Async-camera startup deadlock** — app hung immediately after
   `[CameraThread] started` / `[Classifier] loaded`, before producing any frame.
2. **Double-buffer data race (Bug #1)** — camera producer and main consumer could
   access the same buffer slot concurrently.
3. **ROI / voter class mismatch** — the classifier could receive an ROI belonging
   to a different class than the voter winner ("voter winner ≠ BestROI owner").

---

## Root Causes Found

### 1. GIL ownership deadlock (the startup hang)
The committed "GIL fix" (`53bf397`, 2026-06-05) added **only**
`PyGILState_Ensure/Release` inside `read()`. It never added the GIL *release* in the
constructor. Verified via `git log -S`: `PyEval_SaveThread` had **never** existed in
any committed version of `Picamera2Camera.cpp`.

Consequently `Py_Initialize()` acquired the GIL on the main thread and nothing ever
released it. The camera thread's `PyGILState_Ensure()` (first line of `read()`)
blocked forever waiting for a GIL that main holds → no frame ever produced →
`get_latest_frame()` returned false forever → main spun in `capture_frame_async`.

This was a *different* bug from the original violation, and exactly the deadlock the
2026-06-05 report predicted but incorrectly claimed was resolved.

### 2. Double-buffer race
`CameraThread` used an atomic index swap but no lock on the slot itself. With a fast
camera, the producer could begin writing `slot[i]` while the consumer was still
cloning `slot[i]` — a data race on `cv::Mat`.

### 3. ROI / voter class mismatch
`BestROI` tracked `max(yolo_conf)` across **all** speed-sign classes, while the voter
winner is `max(vote_count)`. These are independent maxima and can belong to different
classes, so the classifier could be handed a crop of class X labeled as winner Y.
The single global ROI was also updated *before* the cooldown-suppression check.

---

## Code Changes Made

### `src/camera/Picamera2Camera.h` (+1 member)
- Added `PyThreadState* save_ = nullptr;`

### `src/camera/Picamera2Camera.cpp` (+SaveThread / RestoreThread)
- Constructor: `save_ = PyEval_SaveThread();` as the last statement (after the
  `camera_` null-check) — releases the GIL acquired by `Py_Initialize()`.
- Destructor: `if (save_) PyEval_RestoreThread(save_);` as the first statement —
  re-acquires the GIL before `close()` / `Py_DECREF` / `Py_Finalize()`. The guard
  covers the case where the constructor threw before `SaveThread`.

### `src/camera/CameraThread.h` (per-slot mutex — Bug #1)
- Added `#include <mutex>` and a `std::mutex mtx` field to `CameraFrame`.
- `run()`: read into a local `temp` first (no lock around `camera_.read()`), then take
  the slot lock only for the `std::move`/metadata copy-in (~0.1 ms).
- `get_latest_frame()`: take the slot lock around the `valid` check + `clone()`.

### `src/main.cpp` (per-class ROI — "D1")
- Replaced the single `BestROI best_roi` with
  `std::map<std::string, BestROI> roi_by_class` (key = class name; ≤5 entries,
  hard-bounded by `speed_sign_group`).
- ROI update is now keyed per class **and** gated by `!cooldown.is_suppressed(...)`,
  so suppressed detections no longer pollute ROI state.
- At confirm, the classifier reads `roi_by_class[winner]` — guaranteeing the ROI's
  class matches the voter winner. Safe degradation if the winner's slot is absent.
- `roi_by_class.clear()` on confirm (mirrors `voter.reset()`).
- `run_decision()` signature + both call sites updated. Classifier / voter / cooldown
  logic untouched.

---

## Runtime Validation Results

Performed on Raspberry Pi 5 (this session):

| Check | Result |
|---|---|
| Build (`make -j4`) | ✅ passes |
| `--async-camera` startup | ✅ no deadlock |
| GIL SaveThread/RestoreThread | ✅ camera thread acquires GIL via `PyGILState_Ensure()` |
| Detection pipeline | ✅ runs normally, valid detections + confirmations |
| Classifier override | ✅ `voter=sign_80 → classifier=sign_50 → confirmed=sign_50`, matched the real 50 sign |

The example confirms two things at once: the async/GIL fix delivers live frames, and
Confirm-then-Classify correctly overrode a wrong YOLO-vote winner with the dedicated
classifier — the ROI it judged belonged to the winning class's own detections (D1
holding).

**Earlier perf baseline (sync/async, from `results.txt`):** ~7.4 FPS single-thread,
~9.3 FPS at `--threads 2` (the sweet spot; 3–4 threads regress to 6–7 FPS due to CPU
oversubscription on 4 cores). `--vulkan` gave no gain (YOLO11n too small for GPU
offload to pay off). Inference (~89–131 ms) is the bottleneck.

---

## Git Commits Created Today

- `1731c25` (2026-06-06) — *"fix: resolve Python GIL handling"* — added the
  `debug_reports/2026-06-05_gil_investigation.md` document only. **No code.**

All of today's *code* changes (GIL SaveThread, mutex, per-class ROI) are **still
uncommitted** in the working tree pending review/approval.

---

## Remaining Open Issues

1. **Bug #3 (LOW) — `classify_ms` metrics** in async mode. Not addressed this session;
   needs re-verification now that confirmations actually run.
2. **Bug #4 (LOW) — TemporalVoter tie-break** is alphabetical (via `std::map` order),
   not recency-based. Untouched.
3. **Stale design comments in `CameraThread.h`** — the header still says
   "ไม่มี blocking" / "ไม่ใช้ mutex lock" and `get_latest_frame`'s "ทำไมไม่ lock",
   which now contradict the per-slot mutex. Documentation only; code is correct.
4. **Minor stale naming in `main.cpp`** — `[CLS]` log label `"best_roi_F"` and one
   comment still reference `best_roi`. Cosmetic.
5. **SIGINT (Ctrl+C) shutdown** of the new destructor path (`RestoreThread` →
   `Py_Finalize`) not explicitly stress-tested for clean exit.
6. **Cross-class contamination deep test** — the rapid 50→60→80 transition scenario
   that D1 specifically targets has not been run end-to-end; the basic confirm path
   is validated, the adversarial one is recommended.

---

## Recommended Next Task

**Fix Bug #3 (`classify_ms` metrics)** — low effort, and now observable because the
classifier runs on confirmations. Then run the **50→60→80 rapid-transition test** with
`--save-roi-debug` to fully validate D1's class-consistency guarantee, and verify a
clean Ctrl+C shutdown. Address Bug #4 (tie-break) afterward.
