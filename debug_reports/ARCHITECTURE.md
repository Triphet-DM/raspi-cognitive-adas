# ARCHITECTURE — raspi_project v11 (Traffic-Sign Speed-Limit Announcer)

> Complete technical walkthrough of the system **as it exists today**.
> Goal: a new engineer (or future-you) can debug and maintain the project from this
> document without reading the code first. It teaches the architecture, not just lists it.
>
> **Grounding:** branch `fix-gil`, HEAD `001ab58` (docs refresh on top of code commit
> `72bd460`), tree clean, in sync with `origin/fix-gil`. Source root:
> `version_2.2_cls_roi_debug/raspi_project_v11/src`. Language: **C++17** on Raspberry Pi 5
> (NCNN inference, OpenCV, embedded CPython only for the camera).
>
> **The one fact that frames everything:** the legacy voter/cooldown path is still
> **authority** and only prints `[CONFIRMED]` — it has *no audio*. The L1–L4 belief-state
> subsystem runs in **shadow** and is the **only thing that drives the speaker**
> (behind `--shadow --audio`). No authority cutover has happened.

---

## Table of Contents

1. System Overview Diagram
2. Thread Architecture
3. Data Flow (per object: create / own / modify / consume / die)
4. Source-Code Call Graph
5. Subsystem Deep Dive (Camera, YOLO, Voter, ROI, CLS, Legacy Authority, Shadow, L1, L2, L3, L4)
6. Design Philosophy
7. Failure Analysis
8. Current Status
9. Quick-Reference Tables & Debug Map

---

## 1. System Overview Diagram

The real end-to-end pipeline. Note the **fork** after `confirmed_value`: one copy feeds the
legacy authority (console only), one copy feeds the shadow pipeline (the speaker).

```
   photons
     │
     ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ CAMERA            Picamera2Camera.read()  (embedded CPython, GIL-bracketed)    │
│                   RGB888 bytes ─► cv::Mat ─► cvtColor(RGB→BGR) ─► frame_bgr     │
│                   [optional CameraThread double-buffer if --async-camera]      │
└──────────────────────────────────────────────────────────────────────────────┘
     │  cv::Mat frame_bgr (BGR, 560×960 default)
     ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ YOLO              YoloDetector::detect()                                       │
│                   letterbox → normalize(1/255) → NcnnModel::run(in0→out0)      │
│                   → postprocess(threshold 0.25) → NMS(IoU 0.45)                 │
│                   [optional AsyncDetectionWorker thread if --async-detect]     │
└──────────────────────────────────────────────────────────────────────────────┘
     │  std::vector<Detection> {box, class_id, class_name, confidence}
     ▼
==================== run_decision()  — MAIN THREAD ONLY ====================
     │
     ├─► top_class = argmax(confidence)
     │
     ├─► ROI LATCH   roi_by_class[top_class].update(frame, box, conf)   (per-class BestROI)
     │
     ├─► COOLDOWN    suppressed = cooldown.is_suppressed(top_class)
     │
     ├─► VOTER       voter.update(suppressed ? "" : top_class); vote = voter.evaluate()
     │
     └─► if vote.confirmed:
            ├─ CLS   SpeedSignClassifier::classify(best ROI of winner)   (Confirm-then-Classify)
            ├─ confirmed_value = CLS-corrected class
            │
            │     ┌─────────────────────────────┬──────────────────────────────────┐
            │     ▼ AUTHORITY (always)           ▼ SHADOW (only if --shadow)          │
            │  cooldown.activate(value)       pipeline.tick(presence, confirmed,      │
            │  print "[CONFIRMED] >>> value"     confirmed_value, frame, now)         │
            │  (console only — NO AUDIO)          │                                   │
            └─────────────────────────────────────┼───────────────────────────────────┘
                                                  ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│ SHADOW PIPELINE  ShadowSpeedLimitPipeline (facade, owns L1–L4)                 │
│                                                                                │
│   L1 SignEpisodeLifecycle     presence episode → EpisodeConfirmed{value,fresh} │
│        ▼                                                                        │
│   L2 CurrentSpeedLimitManager belief + K-hysteresis + no-forget → Outcome      │
│        ▼                                                                        │
│   L3 AnnouncementPolicy       CHANGE / REMINDER / SUPPRESS → Action            │
│        ▼  (only if is_announce)                                                │
│   L4 NotificationManager.notify(action, belief)                                │
│        └─ SpeedAudioMap: (action,value) → "change_50.wav"                       │
│        └─ single-slot latest-wins → wake AUDIO THREAD                           │
└──────────────────────────────────────────────────────────────────────────────┘
                                                  │
                                                  ▼  AUDIO THREAD
                                  aplay -q -D plughw:0,0 "change_50.wav"
                                                  │
                                                  ▼
                                            🔊  SPEAKER  (MAX98357A I2S amp + 3W)
```

Canonical chain in your shorthand (with the ROI correction):

```
Camera → Frame → YOLO → Detection → ROI(latch) → Voter → CLS → confirmed_value
                                                              ├─► [authority: cooldown + console]
                                                              └─► L1 → L2 → L3 → L4 → Speaker
```

ROI is **not** consumed by the voter; it accumulates in parallel and is harvested by CLS at
the confirm event. See §5 ROI Path.

---

## 2. Thread Architecture

Up to **four threads**. The defining rule: **all decision logic runs on the main thread.**
The other three threads do pure I/O and hand data across with the *same* single-slot
latest-wins pattern.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ MAIN THREAD  (always present)                                                 │
│   • main loop, frame acquisition                                              │
│   • run_decision():                                                           │
│       TemporalVoter  ·  CooldownManager  ·  BestROI latch                     │
│       SpeedSignClassifier::classify()      ← CLS RUNS HERE (not on worker)    │
│       SpeedSignLifecycle::update()         ← old shadow, [LC-SHADOW] telemetry │
│       ShadowSpeedLimitPipeline::tick() → L1 → L2 → L3 → L4.notify()           │
│   • render_output() (draw + optional imshow)                                  │
└─────────────────────────────────────────────────────────────────────────────┘
        ▲ frame_bgr (CLONE)                         │ notify() (non-blocking producer)
        │                                            ▼
┌───────────────────────────────┐      ┌───────────────────────────────────────┐
│ CAMERA THREAD (--async-camera)│      │ AUDIO THREAD (only when --audio)        │
│   camera_.read() (takes GIL)  │      │   wait on condvar                       │
│   double-buffer slot write    │      │   take latest filename (latest-wins)    │
│   atomic ready_index_ swap     │      │   play_blocking(): aplay BLOCKS to end  │
└───────────────────────────────┘      │   loop  (playback is OUTSIDE the lock)  │
        ▲                               └───────────────────────────────────────┘
        │ get_latest_frame() clones the ready slot
        │
┌─────────────────────────────────────────────┐
│ DETECTION WORKER THREAD (--async-detect)      │
│   run_detection() → YoloDetector::detect()    │
│   = YOLO INFERENCE ONLY.                       │
│   No voter, no CLS, no L1–L4.                  │
│   Returns std::vector<Detection> to main.      │
└─────────────────────────────────────────────┘
```

### Which objects live in which thread

| Object | Lives in / created by | Touched by thread |
|---|---|---|
| `Picamera2Camera` | `main()` | main (sync) **or** camera thread (async) — never both at once |
| `CameraThread` + `slots_[2]` | `main()` (if `--async-camera`) | camera thread writes, main reads (clone) |
| `YoloDetector` / `NcnnModel` (YOLO) | `main()` | worker (if async) else main — only one caller |
| `AsyncDetectionWorker` | `main()` (if `--async-detect`) | main (submit/take) + worker (run) |
| `TemporalVoter`, `CooldownManager`, `roi_by_class` | `main()` | **main only** |
| `SpeedSignClassifier` | owned by `YoloDetector` (RAII), called via `classifier_ptr` | **main only** (CLS is main-thread) |
| `SpeedSignLifecycle` (old shadow) | `main()` | main only |
| `ShadowSpeedLimitPipeline` (L1/L2/L3/L4) | `main()` | **main only** (producer side) |
| `NotificationManager` pending slot | inside facade | main writes, audio thread reads |

### How data crosses thread boundaries (all three use the same pattern)

```
single producer  ──►  [ 1 mutex + 1 condvar + 1 slot, latest-wins ]  ──►  single consumer
```

1. **Camera thread → main** (`CameraThread`): double buffer. Camera always writes
   `slots_[1 - ready_index_]` (never the slot main reads), locks that slot's mutex only for
   the ~0.1 ms data move, then `ready_index_.store(release)`. Main does `load(acquire)`,
   locks, **clones**, unlocks.
2. **main ↔ detection worker** (`AsyncDetectionWorker`): one `pending_` slot (main→worker),
   one `result_` slot (worker→main), each guarded by one mutex+condvar. Overwriting a full
   slot drops the stale frame (counted in `dropped_frames`).
3. **main → audio thread** (`NotificationManager`): one `pending_` filename slot. `notify()`
   overwrites it (drops the stale clip), signals condvar. Audio thread plays **outside** the
   lock.

### Why frame cloning exists

If main held a *pointer* into a camera slot, the camera thread could swap and overwrite that
slot mid-use → torn frame / race. **Clone makes ownership unambiguous:** after
`get_latest_frame()`, main owns its own `cv::Mat`, and the camera thread is free to keep
producing. The accepted cost is one `cv::Mat` deep copy (~0.1 ms). The accepted race (main
reads a slot one swap stale) is harmless under a latest-frame policy.

### Why result buffers exist

The worker computes YOLO asynchronously; main must not block waiting for it. The single
`result_` slot lets main poll (`try_take_result`) and proceed if nothing is ready, keeping
the main loop responsive (frame acquisition + render continue). The single-slot (not a queue)
choice is deliberate: a queue would accumulate **latency** (you'd process stale frames); a
single latest-wins slot always processes the freshest completed result.

### Consequence: L1–L4 need no locks

Because `run_decision()` — and therefore `pipeline.tick()` and every leaf — runs **only on
the main thread** (the worker does *only* `run_detection`/YOLO), the entire belief subsystem
is single-threaded. This was the main open risk (R5) from the L2 review; it is resolved by
construction. **Cutover invariant: `tick()` must stay single-threaded.**

---

## 3. Data Flow (per object)

For each major object: who **creates**, **owns**, **modifies**, **consumes** it, and when it
**dies**.

### `cv::Mat frame_bgr`
- **Create:** `Picamera2Camera::read()` — Python RGB888 buffer → `cv::Mat rgb` → `cvtColor`
  to BGR.
- **Own:** sync = main's `frame_bgr`; async-camera = `CameraThread` slots, main owns a
  **clone** in `cam_frame.frame_bgr`.
- **Modify:** not modified after creation (read-only downstream). A *clone* may be drawn on in
  `render_output` (`canvas_bgr`).
- **Consume:** `YoloDetector::detect` (read), `BestROI::update` (cloned into the latch),
  `SpeedSignClassifier::classify` (crops from the latched clone), renderer.
- **Die:** end of loop iteration (sync) / when overwritten in slot (async). A clone inside a
  `BestROI` lives until the next `roi_by_class.clear()` (next confirm).

### `Detection`  `{cv::Rect box, int class_id, std::string class_name, float confidence}`
- **Create:** `YoloDetector::postprocess` (one per surviving box), pruned by `nms_detections`.
- **Own:** the `std::vector<Detection>` — in async, moved through worker `pending_`→`result_`
  slots, then **moved** to main via `try_take_result`.
- **Modify:** NMS filters the vector; otherwise immutable.
- **Consume:** `run_decision` — argmax for `top_class`, presence scan, ROI latch.
- **Die:** end of `run_decision` for that frame.

### ROI — `BestROI`  `{cv::Mat frame_bgr, cv::Rect box, float yolo_conf, int frame_idx, bool valid}`
- **Create:** lazily in `roi_by_class[top_class]` (a `std::map<string,BestROI>` owned by
  `main`).
- **Own:** `main` (passed by reference into `run_decision`).
- **Modify:** `BestROI::update()` keeps only the **highest-YOLO-confidence** sample within the
  current voting window (clones the frame on improvement).
- **Consume:** `SpeedSignClassifier::classify` at the confirm event, reading
  `roi_by_class[winner]`.
- **Die:** `roi_by_class.clear()` on every confirm (synchronized with `voter.reset()`).

### `VoteResult`  `{winner, winner_count, history_size, confirmed, votes}`
- **Create:** `TemporalVoter::evaluate()` each frame.
- **Own:** `DecisionResult::vote` (a `run_decision` local).
- **Modify:** immutable snapshot.
- **Consume:** the confirm branch (`vote.confirmed`, `vote.winner`), logging, and passed to
  `pipeline.tick` as `voter_confirmed`.
- **Die:** end of `run_decision`. The underlying voter **history** (a deque) is separate
  state, cleared by `voter.reset()` on confirm.

### CLS output — `std::string` (+ `float cls_conf`)
- **Create:** `SpeedSignClassifier::classify()` — argmax over `{sign_100,50,60,80,90}`;
  returns `""` if `< min_conf` (0.70).
- **Own:** the `output` local in the confirm branch.
- **Modify:** overwrites the voter winner *iff* CLS is confident, producing `confirmed_value`.
- **Consume:** authority (`cooldown.activate`, `[CONFIRMED]` print) **and** shadow
  (`pipeline.tick`).
- **Die:** end of `run_decision`.

### `EpisodeConfirmed`  `{bool fired, std::string value, bool fresh}`
- **Create:** `SignEpisodeLifecycle::update()` (L1), every frame `tick` runs.
- **Own:** local in `ShadowSpeedLimitPipeline::tick`.
- **Modify:** immutable.
- **Consume:** if `fired`, its `value` → `l2_.onValue`, its `fresh` → `l3_.decide`. If not
  fired, `tick` returns immediately.
- **Die:** end of `tick`.

### L2 state — belief  `optional<string> current_` (+ `pending_value_/count_`, `last_confirmed_at_`)
- **Create:** first `Acquire` sets `current_`.
- **Own:** `CurrentSpeedLimitManager` (lives inside the facade, lifetime = process).
- **Modify:** `onValue()` only — `Acquire`/`Change` commit a value; `Reconfirm` refreshes
  time; `Pending` updates the challenge streak. **No transition back to UNKNOWN** (no-forget).
- **Consume:** facade reads `l2_.current()` for the audio value and the log `belief=` field;
  `is_change(outcome)` feeds L3.
- **Die:** never during a run (persists across frames); only `reset()` clears it.

### `AnnouncementAction` — `AnnouncementPolicy::Action` enum
- **Create:** `AnnouncementPolicy::decide(changed, fresh, now)` (L3).
- **Own:** local in `tick`.
- **Modify:** immutable.
- **Consume:** `is_announce()` gates both the `[SHADOW][L3]` log and `nm_.notify`.
- **Die:** end of `tick`. Side effects (timer reset, queued audio) outlive it.

### Audio clip — filename `std::string` → played WAV
- **Create:** `SpeedAudioMap::filename(action, value)` → e.g. `"change_50.wav"` (or `""`).
- **Own:** `NotificationManager::pending_` (single slot).
- **Modify:** overwritten by the next `notify` if the audio thread hasn't taken it yet
  (latest-wins).
- **Consume:** audio thread `run()` moves it out, `play_blocking` runs `aplay`.
- **Die:** after playback completes, or when overwritten before playback (dropped), or
  dropped at shutdown (`stop_`).

---

## 4. Source-Code Call Graph (real runtime path)

```
main()                                            [main.cpp]
 ├─ parse_args() → AppConfig
 ├─ YoloDetector(...)                              constructs NcnnModel (YOLO)
 ├─ Picamera2Camera(...)                           Py_Initialize + PyEval_SaveThread
 ├─ TemporalVoter(10,4), CooldownManager
 ├─ roi_by_class : map<string,BestROI>
 ├─ [if --cls] SpeedSignClassifier → detector.set_classifier(); classifier_ptr = get()
 ├─ SpeedSignLifecycle lifecycle(classifier_ptr)   (old shadow; telemetry only)
 ├─ ShadowSpeedLimitPipeline pipeline(k, rearm, reminder, verbose, audio, dir, device)
 │     └─ owns l1_ (SignEpisodeLifecycle), l2_ (CurrentSpeedLimitManager),
 │              l3_ (AnnouncementPolicy), nm_ (NotificationManager → audio thread if --audio)
 ├─ [if --async-camera] CameraThread(camera)       starts camera thread
 ├─ [if --async-detect] AsyncDetectionWorker(detector)   starts worker thread
 │
 └─ while (g_running):
      │
      ├─ FRAME
      │    [--async-camera] capture_frame_async() → CameraThread::get_latest_frame() [CLONE]
      │    [else]            capture_frame()       → Picamera2Camera::read()  (GIL)
      │
      ├─ DETECT + DECIDE
      │  [--async-detect]
      │     ├─ AsyncDetectionWorker::try_take_result()      (main takes finished YOLO)
      │     │     └─ if ready → run_decision(...) ; render_output() ; update_output_metrics()
      │     └─ AsyncDetectionWorker::submit_latest()        (wakes worker)
      │            └─ worker thread: run() → run_detection() → YoloDetector::detect()
      │                                                          ├─ letterbox_rgb()
      │                                                          ├─ NcnnModel::run(in0→out0)
      │                                                          ├─ postprocess()
      │                                                          └─ nms_detections()
      │  [else sync]
      │     ├─ run_detection() → YoloDetector::detect()    (on main thread)
      │     ├─ run_decision(...)
      │     └─ render_output()
      │
      └─ run_decision(detections, frame, cooldown, voter, classifier_ptr, roi_by_class,
                      lifecycle, pipeline, shadow_enabled, roi_debug_dir, frame_index, times)
           ├─ top_class = argmax(conf)
           ├─ roi_by_class[top_class].update(frame, box, conf, frame_index)   (BestROI)
           ├─ cooldown.is_suppressed(top_class) → result.suppressed
           ├─ voter.update(suppressed ? "" : top_class)
           ├─ result.vote = voter.evaluate()                  [TemporalVoter::VoteResult]
           ├─ if result.vote.confirmed:
           │     ├─ output = vote.winner ; voter.reset()
           │     ├─ if (classifier && speed_group(output) && roi.valid):
           │     │      └─ SpeedSignClassifier::classify(best_roi.frame, best_roi.box, ...)
           │     │            ├─ clamp_roi → make_classifier_input(224, gray-pad)
           │     │            └─ NcnnModel::run(in0→out0) → argmax → "" if < min_conf
           │     ├─ roi_by_class.clear()
           │     ├─ cooldown.activate(output)                  ◄── AUTHORITY
           │     ├─ print "[CONFIRMED] >>> output"             ◄── AUTHORITY (console only)
           │     └─ confirmed_value = output
           │
           ├─ lifecycle.update(top_class, suppressed, vote.confirmed, vote.winner, frame)
           │      └─ [LC-SHADOW] FIRE/RE-ARM/...   (return value DISCARDED)
           │
           └─ if shadow_enabled:
                 ├─ presence = any detection in speed_sign_group()   (RAW, class-agnostic)
                 └─ ShadowSpeedLimitPipeline::tick(presence, vote.confirmed,
                                                   confirmed_value, frame_index, now)
                      ├─ speed_confirmed = voter_confirmed && is_speed(confirmed_value)
                      ├─ ep = SignEpisodeLifecycle::update(presence, speed_confirmed, value, now)
                      ├─ if (!ep.fired) return
                      ├─ age_before = l2_.age(now)
                      ├─ outcome = CurrentSpeedLimitManager::onValue(ep.value, now)
                      ├─ changed = is_change(outcome)
                      ├─ action  = AnnouncementPolicy::decide(changed, ep.fresh, now)
                      ├─ if (is_announce(action) && l2_.current()):
                      │      └─ NotificationManager::notify(action, *l2_.current())
                      │            ├─ SpeedAudioMap::filename(action, value) → "change_50.wav"
                      │            ├─ pending_ = file ; has_pending_ = true   (latest-wins)
                      │            └─ cv_.notify_one()  ──► AUDIO THREAD
                      │                                      └─ run(): play_blocking()
                      │                                            └─ std::system("aplay -q -D ...")
                      └─ print "[SHADOW][L3] ACTION value=.. (L2=.., fresh=.., belief=.., age=..)"
```

> **Naming note vs. the request's example:** the real method names are
> `CurrentSpeedLimitManager::onValue()` (not `update()`) and
> `AnnouncementPolicy::decide()` (not `evaluate()`). The voter has `evaluate()`. The example
> call graph also implied the worker calls `run_decision`; it does **not** — the worker runs
> only YOLO, and `run_decision` always runs on main.

---

## 5. Subsystem Deep Dive

### 5.1 Camera  (`camera/`)

- **Purpose:** produce BGR frames from the Pi camera for the rest of the pipeline.
- **Inputs:** width/height/fps config; photons.
- **Outputs:** `cv::Mat` BGR (`frame_bgr`), plus `captured_at`/`seq` in async mode.
- **Files:** `Camera.h` (abstract `read()` interface), `Picamera2Camera.{h,cpp}` (embedded
  CPython wrapper), `CameraThread.h` (optional producer thread).
- **How it works:** the constructor `Py_Initialize()`s, runs an embedded `CppPicamera2` Python
  class (configures `RGB888`, starts, sleeps 1 s to settle), then **`PyEval_SaveThread()`** to
  release the GIL. `read()` brackets each Python call with
  `PyGILState_Ensure()/PyGILState_Release()`, parses the returned `(bytes, w, h)`, wraps it
  zero-copy as `cv::Mat`, and `cvtColor`s to BGR. Destructor `PyEval_RestoreThread()` before
  `close`/`Py_Finalize()` (guarded so a constructor throw before `SaveThread` is safe).
- **Why it exists / why this shape:** Picamera2 is Python-only; the rest is C++. Embedding the
  interpreter is simpler than a separate process + IPC. The GIL Save/Restore bracketing is the
  fix that lets a *worker* thread call `read()` without deadlock (the original GIL bug, fixed
  2026-06-06).
- **`CameraThread` (optional):** decouples the ~30 ms blocking `read()` from main using the
  double-buffer described in §2. Without it, main blocks on capture and can't pick up finished
  detections (→ `res_wait` accumulates). The per-slot mutex (not a whole-frame lock) keeps the
  camera from blocking on the consumer.

### 5.2 YOLO Detection  (`inference/YoloDetector`, `inference/NcnnModel`, `vision/`)

- **Purpose:** find traffic signs in a frame and localize them.
- **`Detection` structure:** `{cv::Rect box (original-frame px), int class_id,
  std::string class_name, float confidence}`. 15 classes (warning/crossing/school/curve/
  no-parking/.../`sign_50/60/80/90/100`/four-way).
- **Pipeline inside `detect()`:**
  1. `letterbox_rgb` → square `imgsz` (512) with aspect-preserving pad; records
     `LetterboxInfo{scale, pad_x, pad_y}`.
  2. `ncnn::Mat::from_pixels(PIXEL_BGR)` + `substract_mean_normalize(1/255)`.
  3. `NcnnModel::run()` creates a fresh `Extractor`, sets `in0`, extracts `out0` (~93 ms,
     Threads=2 best config).
  4. `postprocess()` decodes raw output (handles 2D/3D, channel-first/last), keeps boxes with
     score ≥ `conf_threshold` (0.25), and `scale_box_back()` un-letterboxes into original px.
  5. `nms_detections()` removes overlaps at IoU ≥ 0.45.
- **NMS — why:** one object yields many overlapping boxes; NMS keeps the highest-confidence
  box per cluster so downstream sees one detection per sign.
- **Confidence:** per-box max class score; used for the threshold, for `top_class` argmax, and
  for choosing the best ROI.
- **Important:** `detect()` **does not** call the classifier (it hard-sets `classify_ms = 0`).
  CLS was moved to the decision stage (Confirm-then-Classify). The `NcnnModel` is a thin RAII
  wrapper around `ncnn::Net` (lightmode on, threads/vulkan/packing configurable).

### 5.3 TemporalVoter  (`main.cpp`)

- **Why YOLO alone is insufficient:** a single frame is noisy — YOLO can misfire for one frame,
  flicker between sub-classes, or miss a real sign briefly. Acting on one frame ⇒ false
  confirms and jitter.
- **Problem solved:** require **temporal agreement** before declaring a sign "seen."
- **Internal buffer:** `std::deque<std::string>` of the last `max_frames = 10` per-frame
  top-classes (empty string when nothing/suppressed).
- **Confirmation logic (`evaluate`):** tally votes; pick the max (ties → most recent by walking
  history in reverse). **Confirm** when a class reaches `early_confirm_votes = 4`, **or** when
  the 10-frame window is full. On confirm returns `{winner, confirmed=true}`; else
  `winner=""`. `update()` pushes a new sample; `reset()` clears the window (done at confirm).
- **Known LOW bug #4:** the *standalone* tie-break is alphabetical rather than recency — kept
  orthogonal; doesn't affect the speed path materially.

### 5.4 ROI Path  (`Types.h::BestROI`, `run_decision`)

- **ROI extraction:** during the voting window, for the frame's `top_class`, if it is a speed
  sign **and not cooldown-suppressed**, `roi_by_class[top_class].update(frame, box, conf,
  frame_idx)` keeps only the **highest-YOLO-confidence** sample (it clones the full frame +
  stores the box).
- **Per-class ownership:** `roi_by_class` is a `map<class → BestROI>` so each class accumulates
  its *own* best crop; the classifier always reads the winning class's ROI, never a crop from a
  different class.
- **Why ROI is separate from the voter:** the voter decides *which class wins over time*; ROI
  decides *which single frame is the best crop to classify*. Coupling them would force you to
  classify every frame (slow + feeds noise back into voting) or classify whatever happens to be
  on screen at the confirm frame (often a poor crop). Keeping ROI as a parallel latch enables
  **Confirm-then-Classify**: classify once, on the sharpest crop, only after the voter commits.
- **Lifetime:** cleared on every confirm (`roi_by_class.clear()`), in lockstep with
  `voter.reset()`, starting a fresh window.

### 5.5 CLS — SpeedSignClassifier  (`inference/SpeedSignClassifier`)

- **Confirm-then-Classify architecture:** CLS runs **exactly once per confirm**, never per
  frame. The four gate conditions (all required): `vote.confirmed` **and** `classifier != null`
  **and** `output ∈ speed_sign_group()` (`sign_50/60/80/90/100`) **and** a valid best ROI for
  that class.
- **Why classification is sparse:** (1) cost — a 224×224 CNN every frame would compete with
  YOLO for the ~93 ms budget; (2) correctness — classifying every frame would feed CLS results
  back into the noise that the voter is trying to filter; (3) quality — the latched best ROI is
  a sharper crop than the confirm-frame crop.
- **What it does:** clamp ROI to frame → center-pad to a square (gray 114) → resize 224×224 →
  normalize 1/255 → NCNN (`in0→out0`) → argmax over `{sign_100, sign_50, sign_60, sign_80,
  sign_90}`. If best score `< min_conf` (0.70) it returns `""` (keep voter's class).
  Optionally writes debug crops with `--save-roi-debug`.
- **Authority split (YOLO vs CLS):** **YOLO = presence + ROI localization** (it found *a* speed
  sign and *where*). **CLS = value authority** (which number), but only *at confirm events*.
  The voter's sub-class is a hint; CLS overrides it when confident, abstains when not.
- **Logging gotcha:** `[CLS]` prints **only** on disagreement or low-conf; a confident
  agreement is silent. Combined with the `classify_ms` rolling-average dilution (Bug #3,
  one ~8 ms reading over a 50-frame window ≈ 0.0 ms displayed), "no `[CLS]` + `cls 0.0ms`" does
  **not** mean CLS was skipped.

### 5.6 Legacy Authority Path  (`CooldownManager`, the confirm branch)

- **`CooldownManager`:** per-class suppression timers (default 5 s/class). `is_suppressed`
  feeds the voter (suppressed → empty vote); `activate(class)` arms the timer at confirm.
- **Current production behavior (no flags):** detections → `top_class` → cooldown filter →
  voter → on confirm: CLS → `confirmed_value` → `cooldown.activate` + print
  `[CONFIRMED] >>> value | cooldown: 5.0s`. **That is the entire authority output — console +
  cooldown, no audio, no belief, no reminder logic.** This is the proven baseline kept until
  L1–L4 is bench-validated.
- **Its flaws (why L1–L4 exists):** identity = unstable YOLO sub-class, so flicker looks like a
  value change; cooldown is pure time, so a still-visible sign re-announces when the timer
  lapses; cooldown conflates "don't spam" with "presence."

### 5.7 Shadow Path  (`ShadowSpeedLimitPipeline` + L1–L4)

- **Why it exists:** to replace the legacy answer with a **belief-state** answer (separate
  identity from value) *without* a risky big-bang rewrite — validate it first.
- **How it runs in parallel:** `run_decision` computes `confirmed_value` once; the authority
  consumes it (cooldown + print) and, if `--shadow`, `pipeline.tick()` consumes the **same**
  value plus a freshly computed **raw class-agnostic presence** (any speed-sign detection this
  frame, ignoring cooldown and ignoring `top_class` — decision D1: presence is perception,
  cooldown is anti-spam).
- **Why it is currently non-authoritative:** `tick()` returns `void`; nothing feeds back into
  authority. The old `lifecycle.update()` return is discarded. Production-with-no-flags is
  unchanged. The audio you hear is the shadow's opinion — that's the validation-by-ear setup
  (decision D9). **Cutover** (future) means making L1–L4's decisions *be* the authority and
  removing voter-input suppression (L3 owns anti-spam then).
- **There are two "shadows":** the new L1–L4 (`[SHADOW][L3]`) and the *old* `SpeedSignLifecycle`
  (`[LC-SHADOW]`), kept only as a side-by-side comparison baseline, scheduled for deletion
  after validation. The old one ties episode identity to the voter sub-class and so re-fires on
  flicker — exactly the failure the new design removes.

### 5.8 L1 — SignEpisodeLifecycle  (`decision/SignEpisodeLifecycle`)

- **Question:** "Is this confirm a *new sighting* (sign went away and came back) or a
  continuation of one I'm already in?"
- **Inputs:** `update(presence, confirm, value, now)` — `presence` (raw class-agnostic),
  `confirm` (voter-confirmed *and* the facade already gated it to a speed value), `value` (CLS
  value, **pass-through only — L1 is value-blind**).
- **Output:** `EpisodeConfirmed{fired, value, fresh}`.
- **`fresh` logic:** `fresh = TRUE` **only** on `Armed → Confirmed`. A re-confirm during
  continuous presence is `fresh = FALSE`. This is the signal L3 uses to decide reminders.
- **Re-arm logic:** presence refreshes `last_seen`. With no confirm and absence, `Confirmed →
  Releasing`; once `now - last_seen ≥ rearm_after` (default 600 ms), `→ Armed`. A re-seen sign
  in `Releasing` goes back to `Confirmed`.
- **Hard rule (cutover R3):** `reset()` must **never** be wired to presence loss — that would
  break L2's no-forget.

**L1 state diagram:**
```
                 confirm (fresh=TRUE, fire)
        ┌──────────────────────────────────────────────┐
        ▼                                               │
   ┌─────────┐                                          │
   │  ARMED  │                                          │
   └─────────┘                                          │
        ▲                                               │
        │ now-last_seen ≥ rearm_after                   │
        │ (absence long enough)                         │
        │                                               │
   ┌──────────┐   absence (no confirm)   ┌───────────┐  │
   │RELEASING │◄─────────────────────────│ CONFIRMED │◄─┘
   └──────────┘                          └───────────┘
        │   presence again (re-seen)          ▲  │
        └─────────────────────────────────────┘  │ confirm again (fresh=FALSE, fire)
                                                  └──┘
   presence (any frame) → last_seen = now  [always, in every state]
```

### 5.9 L2 — CurrentSpeedLimitManager  (`decision/CurrentSpeedLimitManager`)

- **Question:** "Given a new confirmed value, what do I now *believe* the limit is?"
- **State:** `current_` (`optional<string>`; `nullopt` = **UNKNOWN**), `pending_value_/
  pending_count_` (the K-hysteresis challenge), `last_confirmed_at_`.
- **Input:** `onValue(value, now)` — called by the facade **once per L1 fire** (cutover R4:
  once per episode confirm, not per frame, or K's meaning collapses).
- **The four outcomes (transition table):**

  | Current | Incoming | Streak | Outcome | Belief after | changed? |
  |---|---|---|---|---|---|
  | UNKNOWN | V | — | **Acquire** | ACTIVE(V) (no K on first) | yes |
  | ACTIVE(V) | V | — | **Reconfirm** | ACTIVE(V), refresh, clear pending | no |
  | ACTIVE(V) | W≠V | count < K | **Pending** | ACTIVE(V) (challenge building) | no |
  | ACTIVE(V) | W≠V | count ≥ K | **Change** | ACTIVE(W), clear pending | yes |

- **K-hysteresis:** a *different* value must be confirmed K consecutive times before committing
  (default K=1 ⇒ commit on first differing confirm). Seeing the *same* value clears any pending
  challenge.
- **No-forget:** there is **no path back to UNKNOWN**. Presence loss never changes value; a
  camera-only system can't know "no limit here," so it keeps the last belief. Accepted v1
  weakness (wrong after a turn onto an unsigned road) — surfaced via `age`, not auto-cleared.
- **`age(now)`** = telemetry only (`now - last_confirmed_at_`); future age-as-display threshold
  (replaced the cut STALE state).

**L2 state diagram:**
```
        ┌───────────┐
        │  UNKNOWN  │   (current_ = nullopt)
        └───────────┘
              │ onValue(V): Acquire   (no K)
              ▼
        ┌──────────────┐  onValue(V same): Reconfirm (refresh, clear pending)
        │  ACTIVE(V)   │◄───────────────────────────────────────────────────┐
        └──────────────┘                                                     │
              │ onValue(W≠V)                                                 │
              ▼                                                              │
     ┌──────────────────────┐  same W again, count<K: Pending (stay ACTIVE(V))
     │ challenge: pending=W  │──────────────────────────────────────────────┘
     │ count toward K        │
     └──────────────────────┘
              │ count ≥ K  → Change: commit ACTIVE(W)
              ▼
        ┌──────────────┐
        │  ACTIVE(W)   │   (NO arrow back to UNKNOWN — no-forget)
        └──────────────┘
```

### 5.10 L3 — AnnouncementPolicy  (`decision/AnnouncementPolicy`)

- **Question:** "Should I speak, and is it a change or a reminder?"
- **Inputs:** `decide(changed, fresh, now)`. State: `cooldown_` (reminder cooldown, wired to
  180 s by default) and `last_announce_at_` (`optional`; `nullopt` = never announced = treated
  as elapsed).
- **Two policy rules:** **CHANGE always fires** (bypasses cooldown — a real limit change is
  safety-critical — and resets the timer). **REMINDER only on a fresh re-encounter of the same
  value, gated by the global cooldown.** Continuation (`!fresh`) is always silent.

**L3 decision table:**

| `changed` | `fresh` | cooldown elapsed? | Action | Speak? | Timer |
|---|---|---|---|---|---|
| true | — | — | **Change** | ✅ | reset |
| false | true | yes | **Reminder** | ✅ | reset |
| false | true | no | **SuppressCooldown** | ❌ | untouched |
| false | false | — | **SuppressContinuation** | ❌ | untouched |

- **`is_announce` = Change ∨ Reminder** — gates both the log and L4.
- **Two timers, two owners (don't confuse):** L1 `rearm_after` (600 ms, "new sighting?") vs L3
  `reminder_cooldown` (180 s, "how often to re-announce same limit?"). Their interaction is
  known-issue R6 (a >600 ms occlusion during a cooldown window re-arms L1, making the next
  confirm `fresh` → a possible REMINDER); treat as arguably-correct, measure at bench.

### 5.11 L4 — NotificationManager (+ SpeedAudioMap)  (`audio/`)

- **Split (decision D6):** pure `SpeedAudioMap` (unit-testable, holds its own speed set, no
  NCNN/OpenCV) + effectful `NotificationManager` (thread + `aplay`). L4 adds **no** bench-
  tunable behavior (mechanism, not policy).
- **`SpeedAudioMap::filename(action, value)`:** `Change→"change_"`, `Reminder→"reminder_"`,
  else `""`; value must be in `{sign_50/60/80/90/100}` → strip `"sign_"` → `"change_50.wav"`.
  Unknown/suppress → `""` (caller skips).
- **Audio thread + single-slot latest-wins:** `notify()` (main, non-blocking) maps to a
  filename and overwrites the single `pending_` slot, signaling the condvar. The audio thread
  takes the latest filename and `play_blocking()`s **outside the lock** (so the producer can
  overwrite mid-playback → perception never blocks on audio). Non-preemptive within the single
  category (a playing clip finishes; only the *next* selection is latest-wins).
  `enabled=false` → no thread at all.
- **Why FIFO was rejected:** a queue announces **stale state** — by the time a backlogged clip
  plays, the belief may have moved on, so the speaker would lie. Also unbounded latency.
- **Why latest-wins was chosen:** it always reflects the **freshest** decision; bursts collapse
  to "first + last" (verified on Pi: `50→60→80→100` plays "50" then "100"). And it never blocks
  perception (vs. synchronous audio, which blocks 1–2 s/clip).

**L4 latest-wins sequence:**
```
main thread (producer)                          audio thread (consumer)
─────────────────────                           ───────────────────────
notify(Change,50) → pending_="change_50"  ──►   wake → take "change_50" → aplay("change_50") ──┐
notify(Change,60) → pending_="change_60"                                                       │ (blocks
notify(Change,80) → pending_="change_80"        (80,90 overwritten while busy)                 │  to end)
notify(Change,100)→ pending_="change_100"                                                      │
                                                ◄──────────────────────────────────────────────┘
                                                wake → take "change_100" → aplay("change_100")
RESULT HEARD: "50" then "100"  (60/80 dropped — stale)
```

---

## 6. Design Philosophy

- **Why belief-state:** "current speed limit" is not something any single frame *perceives* —
  it must be *estimated* and *remembered* across noisy, intermittent observations. Modeling it
  as a belief (L2) makes memory, hysteresis, and staleness explicit and testable, instead of
  emergent side effects of cooldown timers.
- **Why perception and decision are separated:** perception (YOLO/CLS) is stateless and noisy;
  decision (L1–L4) is stateful and must be stable. Mixing them (the old design) means every
  perception wobble corrupts the decision. Separation lets each side be tuned and tested in
  isolation — perception on hardware, decision with bare-`g++` unit tests.
- **Why YOLO is not the speed authority:** YOLO's sub-class output is unstable frame-to-frame
  (flicker between `sign_50`/`sign_60`). It's excellent at *presence* and *localization*, poor
  at *exact value*. So YOLO owns presence + ROI only.
- **Why CLS is not the final authority:** CLS is more accurate on value but still a per-crop
  classifier that can be wrong or unsure (it returns `""` below 0.70). Letting CLS *directly*
  set the announced limit would re-introduce per-observation noise. Instead CLS feeds a *value
  candidate* into L2, which applies hysteresis and memory.
- **Why L2 owns the current speed limit:** exactly one place must hold the authoritative
  belief, apply K-hysteresis, and enforce no-forget. Centralizing it means there's one state to
  reason about, one transition table to test, and one source for "what do we believe right
  now?" (read by L3 for policy and by L4 for the spoken value).
- **Why plain classes + a facade (no interfaces/DI/event-bus):** the four concerns are small
  and pure; a facade wires them and is the *only* place with I/O, ordering, gating, and side
  effects. Abstraction machinery would add indirection without buying anything for a fixed,
  four-stage pipeline (explicit decision; YAGNI).

---

## 7. Failure Analysis

| Failure scenario | Layer that handles it | Why the architecture survives |
|---|---|---|
| **YOLO flicker** (`50↔60` jitter on one sign, single frames) | TemporalVoter + L2 K-hysteresis | Voter needs 4/10 agreement before any confirm; L2 needs K consecutive *differing* confirms to change belief. A 1-frame flip never commits. (Raise K if real flicker survives the voter.) |
| **Wrong CLS read** (one bad classification) | L2 (K) + CLS abstain | CLS returns `""` below 0.70 (keeps voter class). A single confident-but-wrong read is just one differing confirm; with K≥2 it won't commit, and the next correct confirm clears the pending challenge. |
| **Temporary occlusion** (sign hidden < rearm_after) | L1 Releasing + L2 no-forget | L1 goes `Confirmed→Releasing` but not `Armed` until 600 ms absence; belief is untouched (no-forget). When the sign reappears it's a continuation (`fresh=FALSE`), not a new announcement. |
| **Repeated signs** (same `sign_50` stays in view, confirms every few frames) | L1 `fresh` + L3 | After the first announcement, re-confirms are `fresh=FALSE` → L2 Reconfirm (`changed=false`) → L3 SuppressContinuation (silent). The *old authority* would re-`[CONFIRMED]` each time cooldown lapses. (Bench scenario #2 — the key proof.) |
| **Gapless 50→60** (one limit replaced by another, presence continuous) | L2 value axis | L1 stays continuous (`fresh=FALSE`) but L2 sees a different *value*; with K it commits `Change` → L3 CHANGE (always fires, bypasses cooldown) → announces "60". Value change is handled on the value axis, independent of episode identity. |
| **Audio spam** (decisions arrive faster than clips play, or bursty) | L4 latest-wins + L3 cooldown | L3 suppresses continuations and gates reminders behind the 180 s cooldown (only CHANGE bypasses). L4 collapses any residual burst to first+last and never queues stale clips. |

The unifying idea: each failure mode is absorbed by the layer whose *concern* it belongs to —
noise by the voter, value-uncertainty by L2's hysteresis, presence gaps by L1+no-forget, spam
by L3+L4 — so no single noisy observation can produce a wrong or annoying announcement.

---

## 8. Current Status

- **Production authority:** the **legacy voter/cooldown path** — `run_decision` → voter →
  CLS → `[CONFIRMED]` + cooldown. Console only, **no audio**. Unchanged when no flags are
  passed.
- **Shadow "authority":** the **L1–L4 belief-state pipeline** (`--shadow`). It produces
  `[SHADOW][L3]` decisions and, with `--audio`, **drives the speaker**. It does **not** feed
  back into the legacy path.
- **Verified on Raspberry Pi 5 (2026-06-10):**
  - Unit tests (framework-free, strict warnings): L1 26/26, L2 33/33, L3 23/23,
    SpeedAudioMap 14/14.
  - Full CMake `app` build PASS (shadow + audio sources compiled in).
  - `test_audio` PASS; **latest-wins verified** (`50→60→80→100` → "50" then "100"); unknown
    value silent; missing file warns without crashing.
  - MAX98357A I2S path PASS — all 10 Thai-voice WAVs intelligible through the 3W speaker.
  - End-to-end PASS: Camera → YOLO → CLS → L1 → L2 → L3 → L4 → Speaker, audio from shadow
    decisions while legacy `[CONFIRMED]` runs untouched.
  - Carried from 2026-06-06: GIL Save/RestoreThread fix, CameraThread double-buffer per-slot
    mutex, per-class ROI ownership.
  - Best config: Threads=2 → ~10 FPS / ~93 ms infer (inference-bound; FP16 arith = no-op,
    Vulkan = no gain).
- **What remains before cutover:**
  1. **Systematic bench validation** (printed signs + speaker): 50-stuck, flicker,
     gapless 50→60, re-arm, reminder; diff `[SHADOW][L3]` vs `[CONFIRMED]` (key check =
     scenario 2: legacy repeats while shadow stays silent). Lower `--shadow-reminder-sec` so
     REMINDER is observable.
  2. **Telemetry read:** `Pending` counts → decide K=1 vs K=2; `age` distribution →
     age-display threshold.
  3. **Cutover-readiness** decision honoring invariants R3–R5 (`reset()` not wired to presence
     loss; `onValue` once per episode confirm; `tick()` single-threaded).
  4. **After cutover:** remove the old `SpeedSignLifecycle` + its `[LC-SHADOW]` call; let L1–L4
     be authority; remove voter-input suppression (L3 owns anti-spam).
- **Do NOT (per decisions):** cut over before the bench passes; delete the old lifecycle yet;
  add interfaces/inheritance/event-bus/DI/generics; implement STALE; build cross-category audio
  priority; tune road-dependent params from assumptions.

**Run reference:**
```
./build/app <model/cls args> --shadow --audio [--shadow-reminder-sec 10] [--shadow-verbose]
  flags: --shadow-k <n>  --shadow-rearm-ms <ms>  --shadow-reminder-sec <s>
         --audio-dir <dir=../assets/audio>  --audio-device <dev=plughw:0,0>
```

---

## 9. Quick-Reference Tables & Debug Map

### State ownership

| State | Owner | Thread | Reset / lifetime |
|---|---|---|---|
| `frame_bgr` | main (sync) / CameraThread slots (async, main clones) | main | per iteration |
| `Detection` vector | created in YOLO, moved to main | main consumes | per frame |
| `roi_by_class` (BestROI/class) | main | main | cleared on confirm |
| Voter history | TemporalVoter (main) | main | `reset()` on confirm |
| Cooldown timers | CooldownManager (main) | main | per-class, time-based |
| L1 episode (`Armed/...`, `last_seen`) | SignEpisodeLifecycle (facade) | main | only `reset()` |
| L2 belief (`current_`, pending, `last_confirmed_at_`) | CurrentSpeedLimitManager (facade) | main | no-forget; only `reset()` |
| L3 reminder timer (`last_announce_at_`) | AnnouncementPolicy (facade) | main | reset by Change & Reminder |
| Audio pending slot | NotificationManager (facade) | main writes / audio reads | latest-wins |

### Flags

`--shadow`, `--shadow-k <n>`, `--shadow-rearm-ms <ms>`, `--shadow-reminder-sec <s>`,
`--shadow-verbose`, `--audio`, `--audio-dir <dir>`, `--audio-device <dev>`,
`--async-camera`, `--async-detect`, `--cls-param/--cls-bin/--cls-min-conf`,
`--save-roi-debug <dir>`.

### Symptom → where to look

- **No audio at all** → `--audio` **and** `--shadow` both set? (main warns if `--audio` alone.)
  `enabled_` true (else no thread)? WAV present in `--audio-dir`? Correct ALSA device
  (Pi 5 also has HDMI)?
- **Wrong number spoken** → CLS or L2 belief, not L1/L3. Check `[CLS]` disagreement lines;
  verify classifier `class_names_` order matches training; read the `[SHADOW][L3]` `belief=`
  field (audio uses `l2_.current()`); consider K.
- **Too many announcements** → L3. CHANGE (value flips → CLS/L2 flicker → raise K) vs REMINDER
  (fresh re-encounters → raise `--shadow-reminder-sec`). `action_str` in the log tells which.
- **Missing a real change** → L2 never returned `Change/Acquire`: K too high, CLS abstaining
  (`""`), or the facade `is_speed` gate rejected a non-speed value. CHANGE itself always fires.
- **Re-announces a still-visible sign** → that's old *authority* (`[CONFIRMED]`); shadow should
  be SUPPRESS-CONT. If shadow also repeats, presence is dropping (false re-arm) or
  `rearm_after` too small → check L1 `fresh`.
- **FPS dropped** → inference-bound (~93 ms @ Threads=2); audio is on its own thread and should
  be FPS-neutral. Inspect `infer`, `res_wait`, `cap_wait` in the perf line.
- **Camera crash/hang** → GIL: verify `PyEval_SaveThread/RestoreThread` bracketing and
  `PyGILState_Ensure/Release` in `read()`.
- **Suspected race in L1–L4** → shouldn't be possible: `run_decision`/`tick` are main-thread
  only. If you ever call `tick()` from the worker you've broken cutover invariant R5.

---

*End of document. Source of truth for high-level status remains `PROJECT_STATUS.md`; dated
session reports hold history. Update this file when the architecture changes (e.g., authority
cutover).*
