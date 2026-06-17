# Multi-Model Core Budget & CPU Affinity — reference

> Created 2026-06-16. Reference for adding a 2nd model (face detection planned) on the Pi 5
> (4× Cortex-A76) without re-creating the async-style core contention. Companion to
> `FP32_SPEED_ENVELOPE.md` (the async→sync finding) and PROJECT_STATUS.

## The core principle (not threads — cores)
The finite resource is the **4 cores**, not the thread count. A blocked/sleeping thread costs
no core; only **CPU-bound runnable threads** demand cores.

- **full** = cores at ~100% but **≤1 CPU-bound thread per core** → ideal, no contention.
- **overflow** = **CPU-bound threads > cores** → time-slicing → every task slows, latency rises.
- The real question is **"is total CPU demand of simultaneously-runnable threads > 4?"**, NOT
  "are cores full?". Full is good; overflow is bad.

**CPU% is misleading.** A core at 100% serving 1 thread = that thread runs full speed. A core
at 100% serving 2 CPU-bound threads = each at half speed, latency doubles — *same 100% reading*.
This is exactly how async fooled us (CPU ~80% but infer 48→98 ms). **Measure latency + drops +
load average, not just CPU%.**

- Single best "overflow yet?" signal: **load average vs core count** (`uptime`/`htop`). 1-min
  load consistently > 4 on the 4-core Pi ⇒ threads queueing for a core ⇒ overflow.
- Plus: per-frame `infer_ms`/latency stable? frames dropping? `vcgencmd get_throttled` = 0x0?

## Current threading (sync mode, 2026-06-16)
| thread | count | CPU-bound? | notes |
|---|---|---|---|
| main loop | 1 | yes (during the cycle) | camera.read + letterbox + voting + L1–L4 + feed ncnn |
| ncnn OMP workers | 2 (`num_threads`) | **yes — the heavy part** | detector + classifier share these |
| camera | 0 in sync | — | CameraThread/double-buffer only exists with `--async-camera` |
| audio | 1 (if `--audio`) | no (blocked in `aplay`) | playback off the hot path; never blocks inference |

≈2 cores of real work today → CPU 50-60%, headroom for a 2nd model.

## Data movement (cache locality)
- **The biggest cross-thread copy is already gone:** async `CameraThread` did `frame.clone()` of
  the ~1.6 MB 960×560 frame across threads every iteration. Sync keeps the frame inside the main
  thread → no cross-thread frame copy. (One reason sync is leaner.)
- Remaining data-mover worth pinning = **the main thread**: it owns the big buffers (raw frame
  1.6 MB → letterbox 512² → ncnn input ~3 MB) and feeds ncnn every frame. Pinning it keeps those
  buffers **hot in that core's private L2** (no reload after a scheduler migration).
- Audio: tiny data — pin for **isolation** (don't preempt the inference cores), not for data.
- Pi 5 cache: each A76 has private L1+L2, all 4 share a 2 MB L3 (SLC), single cluster. So the
  win from pinning is avoiding L2 reload on migration; the main→ncnn handoff goes through shared
  L3 (cheap) — no cross-cluster penalty to worry about.

## Affinity difficulty (easy → hard)
1. **Measure only (EASIEST, do this first, no pinning):** `mpstat -P ALL 1` or `htop` per-core
   meters while the app runs → see which of the 4 cores is hot. Answers "real headroom per core"
   with zero code.
2. **Whole process:** `taskset -c 0-2 ./app` — trivial, but can't split per-thread.
3. **Our own threads (main/audio):** `pthread_setaffinity_np(handle, …)` — a few lines each.
4. **ncnn OMP workers (the fiddly one):** we don't own those threads → can't pthread-pin them.
   Use **env**: `GOMP_CPU_AFFINITY="0,1"` (or `OMP_PROC_BIND=close OMP_PLACES=cores`) or ncnn's
   own API. Coarse fencing is enough — no per-thread pinning needed.
   - Picamera2/libcamera also spawn their own threads (light, mostly blocked) — hard to control,
     usually fine to leave.

⚠️ **Pinning `main` alone is useless without fencing ncnn off main's core** — else the scheduler
drops ncnn OMP onto main's core and they contend again. Always pair: pin main to core X **and**
restrict ncnn to a disjoint set via `GOMP_CPU_AFFINITY`.

## Suggested 4-core layout (when the 2nd model arrives)
| core | work | why |
|---|---|---|
| 0-1 | ncnn inference (`GOMP_CPU_AFFINITY="0,1"`) | heaviest, 2 threads |
| 2 | main (camera read + letterbox + feed) | big buffers stay L2-hot, no migration |
| 3 | audio (+ light libcamera/Python helpers) | isolation, never preempts inference |

4 CPU-relevant threads on 4 cores = full, not overflow.

## Adding the face-detection model — the rule
Do **NOT** let CPU-bound threads exceed 4 simultaneously (that is exactly what async did).
Options, cheapest first:
- **Sequential / frame-alternating:** face model shares the existing inference budget (e.g. odd
  frames = sign detector, even = face) → no new always-on CPU-bound thread. Preferred.
- **Thread-budgeted concurrent:** detector 2 threads + face 2 threads but never peaking together
  (gated so only one runs per frame).
- If both must run concurrently and always-on → you need ≥ that many cores, or smaller models /
  lower imgsz / lower cadence. Budget against cores, not "it's just another thread."

**Decision metric when integrating:** does per-frame `infer_ms`/latency stay flat and no frames
drop, with load average ≤ 4 and no thermal throttle? If yes at 80-85% CPU → acceptable (tight);
aim steady-state ≤ 70-80% for a safety system. If latency rises → overflow, back off regardless
of the CPU% number.
