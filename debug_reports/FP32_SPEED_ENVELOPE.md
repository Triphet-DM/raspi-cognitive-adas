# fp32 Detector — Speed Envelope & Optimization Levers

> Created 2026-06-16. Purpose: map the speed limits of the CURRENT fp32 detector (no
> quantization — int8 ruled out, see `INT8_AB_RESULTS.md`) so Brain 2 / behavior
> architecture can be designed within a known latency budget.

## Optimization levers, ranked by impact

| Lever | Speed impact | Accuracy cost | Status / note |
|---|---|---|---|
| **imgsz** (512→416→384→320) | **largest** (~quadratic) | small, measured (table below) | re-exported + val'd ✅ |
| **fp16 storage/packed** | large (bench 99→46 ms) | ~none | ncnn DEFAULT = ON; the app already gets it (never sets fp16 opts) |
| **detection cadence** (run YOLO every Nth frame) | **large** | architectural | ⬅️ Brain 2 lever — see below |
| **threads** | medium (t2↔t4) | none | app uses t2; bench liked t4 — **must test in app** (camera-thread core contention) |
| **async pipeline** (`--async-detect --async-camera`) | medium (overlaps camera+infer) | none | raises throughput FPS, not infer time; untested in app |
| **winograd / sgemm / packing** | small–medium | none | already ON; bench confirms OFF = slower |
| **CPU governor `performance` + cooling** | small–medium | none | check Pi isn't thermal-throttling |
| fp16 **arithmetic** / Vulkan / int8 | no-op or slower | — | tried, don't help (A76 fp32 already efficient) |

## imgsz accuracy curve (yolo val, ncnn, test split 1300 imgs, conf .25 iou .45)

| imgsz | mAP50 | mAP50-95 | P | R | compute vs 512 |
|---|---:|---:|---:|---:|---:|
| 512 | 0.983 | 0.907 | 0.971 | 0.973 | 1.00× |
| 416 | 0.975 | 0.880 | 0.963 | 0.963 | 0.66× |
| 384 | 0.972 | 0.866 | 0.973 | 0.946 | 0.56× |
| 320 | 0.955 | 0.830 | 0.942 | 0.930 | 0.39× |

- **512→416**: −0.8% mAP50 for −34% compute → best value.
- **512→384**: −1.1% mAP50 for −44% compute.
- **320**: −2.8% mAP50 and mAP50-95 drops 0.907→0.830 (coarser localization) — only if speed is desperate.
- Note: val uses ultralytics' rectangular letterbox; the app square-letterboxes (separate item).
  Absolute mAP may shift on-device, but the **relative** imgsz trend holds.

## Pure-inference speed — MEASURED ON PI (2026-06-16, `bench_imgsz.py`, fp16=default)

| imgsz | t2 ms | t3 ms | t4 ms | FPS(t4) | mAP50 |
|---|---:|---:|---:|---:|---:|
| 320 | 16.8 | 15.4 | **13.8** | 72 | 0.955 |
| 384 | 24.0 | 21.9 | **19.8** | 50 | 0.972 |
| 416 | 29.1 | 26.9 | **24.1** | 42 | 0.975 |
| 512 | — | — | ~36 (est) | ~28 | 0.983 |

(512 row not run — no `ncnn_512/` on the Pi; estimate from area-scaling, matches the
`bench_int8_opts` `t4+fp16`=38.7 ms point. To fill it: copy the deployed
`src/models/detection/yolo11n/model.ncnn.*` into `ncnn_512/` and re-run.)

**Findings:** (1) **threads=4 is fastest at every imgsz** in pure inference — but the app uses
t2; the documented app baseline ~93 ms @512/t2 is ~2× the pure-inference number, i.e. the app
loop (camera contention / classifier / thermal / possibly a different system ncnn build) roughly
doubles per-frame cost. So **real app FPS ≈ half the table** — still a large gain. (2) **416 @ t4
is the sweet spot**: mAP50 0.975 (−0.8% vs 512) at ~24 ms pure (~33% faster than 512). (3) 320 =
72 FPS pure but mAP50-95 degrades (coarse localization).

**Must still verify in the real app:** does `--threads 4` (esp. with `--async-*` to free a core
for the camera) actually beat t2 in the full loop? The pure-inference t4 win may shrink or
reverse under camera-thread contention (the original "t2 best" measurement).

## ⭐⭐⭐ REAL APP measurement on Pi (2026-06-16, full pipeline, `bench_app_imgsz.sh`)

| imgsz | thr | FPS | infer ms | cap_wait ms | total ms |
|---|---|---:|---:|---:|---:|
| 512 | 2 | 19.3 | 47.4 | 1.9 | 51.7 |
| 512 | 4 | 20.6 | 46.0 | 1.8 | 50.5 |
| **416** | **4** | **30.9** | 27.4 | 5.3 | 34.2 |
| 416 | 2 | 30.1 | 29.6 | 2.1 | 33.1 |
| 384 | 4 | 31.2 | 21.7 | 9.9 | 33.2 |
| 320 | 4 | 31.5 | 15.2 | 17.0 | 33.3 |
| 416 | 4 +async | **10.9 (BROKEN)** | 89.4 | — | 110 (drop 25729) |

**Findings (these are the real, no-guess numbers):**
1. **The camera (30 FPS / ~33 ms/frame) is the ceiling for imgsz ≤ 416.** As infer drops
   (29→15 ms) `cap_wait` rises (2→17 ms) and FPS stays ~30-31 — the CPU idles waiting for the
   next frame. **Going below 416 buys ZERO FPS, only loses accuracy.**
2. **512 is inference-bound (~20 FPS); 416 hits the 30 FPS camera cap** at −0.8% mAP50 → **416
   @ t4 is the operating point.** ~6 ms CPU idle/frame = headroom for Brain 2.
3. **`--async-detect --async-camera` is BROKEN here** (10.9 FPS, infer 89 ms, 25729 dropped
   frames). Do NOT use async. (Confirms the PROJECT_STATUS caution.)
4. **threads=4 = small free win** (512: 19.3→20.6; 416: 30.1→30.9). Keep it; matters only when
   inference-bound (512).
5. Real current prod (512/t2) measured ~19 FPS / 47 ms infer here — *better* than the old
   "~10 FPS / 93 ms" note (older build/conditions). cls=0 because no signs were in frame;
   classifier adds a little only on confirm frames.

**To exceed 30 FPS** you'd have to raise `--camera-fps` (if the sensor supports it) — the model
already has spare time at 416. Otherwise 30 FPS @ 416 is the practical envelope.

**Brain 2 budget (concrete):** at 416/30 FPS the loop has ~33 ms/frame; detector uses ~27 ms,
leaving ~6 ms idle that already absorbs camera wait. Brain 2 arbitration is microseconds, so it
fits with room to spare. The real lever if more headroom is needed: drop imgsz (frees CPU but
not FPS) or lower detection cadence.

### Pending real-world check
All bench runs had `det:0` (camera not aimed at signs). dataset mAP = Street View domain ≠ Pi
camera. **TODO: live-detect real signs at 512 vs 416** (watch `det`/`conf` vs distance) to
confirm 416 doesn't lose far/small signs on the actual camera before locking it in.

## 🚨 ROOT CAUSE of "system only does ~10 FPS": async HALVES throughput (2026-06-16)
Diamond noted production @512 runs **9-10 FPS / 90-100 ms**, but the sync bench @512 = **19-20
FPS / 47 ms**. Resolved: **the slow path is `--async-detect --async-camera`.** Measured @416:

| 416 mode | FPS |
|---|---|
| **sync (no async)** | **28-30** |
| async, threads=2 | 13-15 |
| async, threads=4 | 10.9 |

async consistently gives **~half** the sync FPS on this Pi 5 — thread/core oversubscription
(camera thread + detect thread + N ncnn threads > 4 cores) inflates infer time and drops tens
of thousands of frames (latest-wins). Production's 9-10 FPS matches the async path; the bench's
19-20 FPS (sync) is correct. **The two numbers were different *modes*, not a measurement error.**

**Free ~2× win:** if production runs async, **turning async OFF roughly doubles FPS at zero
accuracy cost** (512: ~10→~19-20; 416: →~30). Verify by running the exact production command
minus the two async flags. The old PROJECT_STATUS "~10 FPS / 93 ms @ t2" was almost certainly
measured under async — re-baseline it sync.

**Open:** why was async enabled? If nothing depends on it (display smoothness, non-blocking
camera), drop it. If it was added for a reason, weigh that against the 2× throughput. This is
the single biggest speed lever found today — bigger than imgsz.

### DEFINITIVE A/B on Pi — same binary, same 512 model, threads=2 (2026-06-16)
| mode | infer ms | total ms | FPS | cap ms | drop |
|---|---:|---:|---:|---:|---:|
| **async** (`--async-detect --async-camera`) = production | **98** | 127 | **9.6** | 32 | 5574 |
| **sync** (no async flags) | **48** | 53 | **18.8** | 1.9 | 0 |

**async DOUBLES infer time (48→98 ms) and halves FPS.** Root cause visible in `cap`: async
spins a camera thread that burns ~32 ms/frame of CPU *continuously* (vs 1.9 ms to grab a ready
frame in sync), starving the 2-thread ncnn inference on the 4-core Pi 5. This fully explains
"production only does 9-10 FPS" while the sync bench did 19 — they were different modes, not a
measurement error. **Fix: run production in SYNC (drop `--async-detect --async-camera`) → ~2×
FPS at zero accuracy cost.** Note: async-camera was added 2026-06-06 for a GIL-correctness fix;
its throughput cost vs sync was apparently never A/B'd until now.

### Why async backfired here — and when it WOULD help
Intent (per `CameraThread.h`): `camera.read()` was assumed to block ~30 ms, so run it on a
separate thread to overlap with inference. **But that 30 ms wait doesn't actually happen in
this workload** — proof: sync `cap = 1.9 ms`, not 30 ms. Because **infer (48 ms) is SLOWER than
the camera interval (~33 ms @30fps), a fresh frame is always already waiting** when the loop
asks for it; the main thread just grabs it (1.9 ms). There is no wait to hide.

So async added a camera thread that (a) does real CPU work — Python/libcamera/pisp + clone of a
960×560 frame, not idle I/O — and (b) runs flat-out at camera rate producing frames that get
dropped (5574 wasted), continuously burning a core. On a 4-core Pi 5 that core is one inference
needs → ncnn threads get starved → infer doubles (48→98 ms). **Parallelism only speeds things up
with spare cores AND a real wait to overlap; here there were neither → pure contention loss.**

**async-camera will only pay off when ALL hold:** (1) `camera.read()` is a genuine idle wait,
(2) inference is *faster* than the camera interval (so the loop would otherwise wait on the
camera), and (3) there are spare cores. That regime appears if the detector is **moved to an
accelerator** (infer drops below the camera period) — re-evaluate async then, not before.

### sync vs async is capture-layer only — decision architecture is untouched (but cadence ↑)
`async`/`sync` lives at the **front of the pipeline** (how a frame gets from camera → detector).
It does NOT touch any decision logic: detector output, TemporalVoter, L1–L4 belief state, the
Brain 2 momentary engine, attention_rank, or audio are all identical. Verified on Pi: sync run
gave the same detections / logs / alerts as before. **Nothing in the designed/ to-be-designed
subsystems breaks.**

One real consequence for Brain 2 design: **frames now arrive at the decision layer ~2× faster
(9.6 → ~19 FPS).** This splits the temporal mechanisms by unit:
- **Frame-COUNT gates** (TemporalVoter K/N, Detection Stability Window): reach threshold in
  **half the wall-clock time** → faster confirmation (good), but any K tuned for ~10 FPS should
  be re-checked at the real ~19/30 FPS.
- **TIME-based gates** (L1 `rearm_after` 600 ms, L3 reminder 180 s, Brain 2 Notification
  Suppression Windows in seconds): **frame-rate-independent → unaffected.**

Design rule (reinforces the 2026-06-15 freeze): keep "how long to stay quiet" semantics
**time-based** (immune to FPS changes); only "how many consecutive frames confirm" stays
frame-count. Design Brain 2 against the real cadence (~19 FPS @512 / ~30 @416), not the old ~10.

**Soak test:** sync ran 5 min on Pi — all signs detected, logs/alerts identical, **CPU dropped
80% → 50-60%** (the async camera thread's wasted capture work is gone), leaving headroom for
Brain 2 / a future second-stage model. Longer soak (30 min / real drive) still advised before
locking sync as the production default, for GIL/thermal confidence.

### Updated lever priority (largest first)
1. **Drop async** → 512: 9.6→18.8 FPS (2×, free, no accuracy change). BIGGEST lever.
2. **imgsz 512→416** → ~30 FPS (camera cap), −0.8% mAP50.
3. **threads=4** → small free gain when inference-bound.
4. (below 416 = no FPS gain, camera-capped; async = never; int8/fp16-arith/vulkan = no help.)
Current production 9.6 FPS → sync 416 t4 ≈ 30 FPS is a realistic **~3× end-to-end** with
negligible accuracy loss, no new hardware.

## Pure-inference speed (Windows ncnn-python bench, 512, for shape only — NOT Pi truth)
From `bench_int8_opts.py`: fp32 base(fp16 off) 99 ms; fp16-on 46 ms; t4+fp16 **38.7 ms**.
The app's real number differs (different ncnn build + camera loop) — measure on Pi.

## TO MEASURE ON PI (assets ready)
1. Copy `ncnn_320/ 384/ 416/` (+ a 512) and `bench_imgsz.py` to the Pi.
2. `python bench_imgsz.py` → real fp32 ms per imgsz × threads {2,3,4}.
3. In the **app**: `--threads 4`, `--async-detect --async-camera` vs baseline → real FPS.
4. Confirm no thermal throttle (`vcgencmd measure_clock arm`, `measure_temp`).

## Framing for Brain 2 design (the key insight)
The detector cost (~one infer per detected frame) is the hard floor. Two architectural
levers that DON'T touch the model:
- **You need not run YOLO every frame.** TemporalVoter already integrates over frames; if
  detection runs every 2nd/3rd frame, the per-second compute drops proportionally and the
  freed time is budget for Brain 2. The cost is **reaction latency** (how fast a new sign is
  confirmed) — design the K/N voting window and suppression timings around the *actual*
  detection cadence, not an assumed 10 FPS.
- **Latency budget = detector infer_ms (at chosen imgsz) + classifier (confirm-frame only) +
  Brain 2 arbitration.** Brain 2's Notification Suppression Windows / attention arbitration
  run in microseconds — they are NOT the bottleneck. The detector sets the clock.

**Design rule of thumb:** pick imgsz from the curve for the mAP you need, measure its Pi
infer_ms, then set the detection cadence so total per-second detector load leaves headroom.
Everything in Brain 2 is cheap; the only real-time constraint is the YOLO cadence.

## Hardware pivot that removes the CPU constraint entirely
This whole envelope assumes **Pi 5 CPU** inference. Adding a **Pi AI HAT (Hailo NPU)** or
similar accelerator moves the detector off the CPU — int8 becomes the right choice there
(opposite of the CPU verdict) and the CPU is freed for Brain 2 / classifier / audio. See
`INT8_AB_RESULTS.md` → "Future / design pivot: accelerator". If that hardware is on the table,
the imgsz/cadence trade-offs above relax dramatically.
