# Brain 3 (Drowsiness) — Face / Landmark Model Shortlist

> Compiled 2026-06-25 for selecting a face-detection / eye-landmark model for the **driver-monitoring
> (drowsiness)** camera. Target: **Raspberry Pi 5 + USB webcam**, tested on **PC (Windows) first**.
> Preference: stay in the **NCNN + C++** ecosystem (to match the existing YOLO pipeline) where possible.
>
> ⚠️ **Honesty note:** model **sizes are approximate** and **speed is qualitative** — exact Pi-5 FPS
> must be measured on-device (your own past lesson: trust fp32 cross-platform, measure speed on Pi,
> and don't judge int8 on x86). Treat this as a shortlist to choose from, not final numbers.

---

## The drowsiness pipeline (so the model choice makes sense)

```
USB webcam → [1] FACE DETECTOR → [2] EYE LANDMARKS / EYE CROP → [3] EYE STATE
                                                                  (EAR ratio OR open/closed CNN)
                                                                        ↓
                                                  [4] TEMPORAL: eyes closed N frames → DROWSY
```
This mirrors your existing pattern (detect → classify value → temporal voting). You need a model for
**[1]** and **[2]** (or a combined one); **[3]** can be a math formula (EAR) or a tiny classifier.

---

## Table A — Face Detectors (stage [1])

| Model | Detects | Size (approx) | Speed | Ecosystem / Pi-5 fit | Credibility |
|---|---|---|---|---|---|
| **YuNet** (libfacedetection) | face box + **5 landmarks** (eyes/nose/mouth) | **~75–340 KB** (tiny) | very fast (CPU) | **built into OpenCV** (`cv::FaceDetectorYN`) — you already use OpenCV, **zero new runtime**. ONNX. | ★★★ OpenCV Zoo, maintained (Shiqi Yu) |
| **Ultra-Light-Fast-Generic-Face-Detector-1MB** (Linzaer) | face box only (no landmarks) | **~1 MB** | very fast | **official NCNN** demo + ONNX/MNN. Great NCNN fit. | ★★★ ~7k+ GitHub stars, widely ported |
| **RetinaFace** (MobileNet0.25) | face box + **5 landmarks** | **~1.7 MB** | fast | **in the official ncnn repo** (nihui example) + ONNX | ★★★ InsightFace paper, very common |
| **SCRFD** (InsightFace) | face box + **5 landmarks** | ~2.5 MB (0.5G) / smaller tiny variants | very efficient | ONNX (InsightFace) + community NCNN ports | ★★★ strong speed/accuracy paper |
| **BlazeFace** (Google/MediaPipe) | face box + 6 keypoints | **~few hundred KB** | extremely fast | native TFLite/MediaPipe; **community NCNN/ONNX ports** (use port to avoid MediaPipe/Bazel) | ★★★ Google, but native form = MediaPipe |

**Notes:** for drowsiness you want the **5 landmarks** (eye centers) — so YuNet / RetinaFace / SCRFD /
BlazeFace are better than Ultra-Light (box-only) **unless** you pair Ultra-Light with a separate
landmark model below.

---

## Table B — Facial Landmark / Mesh (stage [2], for EAR / eye contours)

| Model | Detects | Size (approx) | Speed | Ecosystem / Pi-5 fit | Credibility |
|---|---|---|---|---|---|
| **dlib 68-point** (`shape_predictor_68_face_landmarks`) | 68 landmarks (6 per eye → classic EAR) | **~99 MB** model file (big!) | landmark step fast; dlib's own face detector slow | dlib C++ lib (easy CMake) + Python | ★★★ **the classic EAR drowsiness path** (Soukupová & Čech 2016) — best for PC prototyping the LOGIC |
| **MediaPipe Face Mesh / Face Landmarker** | **468 dense 3D** landmarks (detailed eye contours, best for EAR/gaze) | ~2–3 MB | fast (mobile-optimized) | TFLite + **MediaPipe framework = heavy to integrate in C++/CMake (Bazel)**; Python easy | ★★★ Google, very high quality |
| **PFLD** (Practical Facial Landmark Detector) | 68 or 98 landmarks | ~1–5 MB (MobileNet) | fast, mobile-designed | ONNX + **NCNN ports** exist | ★★☆ known paper, decent |

---

## Table C — Direct Eye-State (alternative to EAR math, stage [3])

| Model | Detects | Size | Speed | Ecosystem | Credibility |
|---|---|---|---|---|---|
| **Open/Closed-eye CNN** (trained on **MRL Eye Dataset** or CEW) | eye open vs closed (binary, on eye crop) | **<1 MB** (tiny) | very fast | usually **train/convert yourself** (PyTorch/Keras → NCNN/TFLite) | ★★☆ dataset is credible; the model is yours to train |

**Trade-off:** EAR (geometry from landmarks) = no extra model, but threshold tuning needed.
Eye-state CNN = robust to head pose, but you train it + need an eye locator first.

---

## Recommended decision paths (pick tomorrow)

**Path 1 — Fastest PC prototype of the drowsiness LOGIC (recommended first step)**
`dlib face + 68-landmark → EAR → temporal` — tons of reference code, proves the *idea* on PC quickly.
(Heavy/slow for Pi, but you're prototyping the logic, not deploying yet.)

**Path 2 — Cleanest for your NCNN/C++ Pi stack (one ecosystem)**
`RetinaFace or SCRFD or Ultra-Light (NCNN) → PFLD landmarks (NCNN) → EAR` — all NCNN, matches YOLO,
no second runtime.

**Path 3 — Least new code on PC (you already have OpenCV)**
`YuNet via cv::FaceDetectorYN (face + 5 pts)` → use the 2 eye points + a small eye-crop EAR or a tiny
open/closed CNN. Zero new dependency to start.

**Path 4 — Densest eye info, accept integration cost**
`MediaPipe Face Mesh` (468 pts) — best eye contours, but C++ integration is the painful part.

---

## Quick credibility / source pointers (to verify tomorrow)
- **YuNet** → OpenCV Zoo (`opencv/opencv_zoo`), `cv::FaceDetectorYN`.
- **Ultra-Light-1MB** → GitHub `Linzaer/Ultra-Light-Fast-Generic-Face-Detector-1MB`.
- **RetinaFace / SCRFD** → `deepinsight/insightface`; RetinaFace also in `Tencent/ncnn` examples (nihui).
- **PFLD** → original paper + various ncnn ports.
- **MediaPipe Face Mesh** → Google `mediapipe` (Face Landmarker task).
- **dlib** → `dlib.net`, 68-landmark predictor (classic EAR tutorials, pyimagesearch).
- **MRL Eye Dataset** → MRL (eye open/closed) — for training a tiny eye-state CNN.

## Open questions for the final choice
1. EAR (math, no extra model) **vs** eye-state CNN (train it)? — affects which stage-[2] model you need.
2. Single-runtime (all NCNN) vs allow OpenCV-YuNet/TFLite as a second runtime on PC-prototype phase?
3. Glasses / low-light robustness (driver cabin) — worth checking on real webcam frames early.

---

## Decisions & approach (discussed 2026-06-25)

**Drowsiness signals chosen: EAR + Head pose** (standard DMS combo — complementary: EAR catches
eyes-closed/microsleep, head pose catches head-nodding + look-away; head pose also more robust in
glasses/low-light).

**Key implication → use a LANDMARK model, not an eye-state CNN.** Head pose = `cv::solvePnP` needs ~6
face points (nose tip, chin, eye corners, mouth corners). One landmark model (e.g. **PFLD / 68-pt**,
far lighter than FaceMesh's 468) feeds **both** EAR (eye points) and head pose (face points) from a
single inference — solvePnP cost ≈ 0. (Note: 468-pt FaceMesh is overkill, and you can't reclaim its
compute by using fewer outputs — pick a smaller model instead.)

**Roadmap (incremental — add ONE signal at a time, measure the delta before adopting; ablation, same
discipline as the project's A/B benchmarks):**
- **v1 = EAR + Head pose** — get the landmark pipeline working end-to-end first (the new skill).
- **v2 = + MAR (yawn)** — cheap add-on (same aspect-ratio formula on mouth points, ~3 lines).
  Yawn-vs-talk discriminator: **MAR high AND sustained ≥ ~4 s** (talking oscillates, won't sustain)
  + count yawns / time window.

**Temporal layer = the heart** (mirrors TemporalVoter): EAR → **PERCLOS** (proportion eyes >80% closed
over a window); head pose → nod-duration vs a calibrated neutral; per-person threshold calibration.

**Dev workflow:** prototype on **PC (Windows) first** (fp32 trusts cross-platform; measure speed on Pi).
Webcam via `cv::VideoCapture` (V4L2). Feeds `BehaviorPolicyRouter` as **Law 2 life-safety (top
priority)**; the thermal governor must never gate drowsiness.

**Tomorrow (2026-06-26) step 1:** read + understand the model differences (Tables A/B/C) → pick the
landmark model for v1.

---

## DECISIONS LOCKED — 2026-06-26 (design meeting: Diamond + Claude + GPT)

> Engineering-review format (GPT = hard reviewer, Claude = honesty-check on GPT). **No code.**
> Architecture for Brain 3 v1 locked except ONE action item (verify PFLD port). Numbers below are
> **starting points → bench-tune on Pi** (project discipline: don't lock from assumption).

### Hardware / cameras (2-camera design CONFIRMED)
- **Brain 1 + Brain 2 → IMX477** (road-facing, existing).
- **Brain 3 → Logitech Brio 100** (driver-facing, USB webcam). **Separate camera** → no shared
  capture thread, far less CPU/pipeline contention than the single-camera worry.
- **Resolution = 640×480** (face detection doesn't need 1080p; keeps USB decode overhead low).

### The v1 pipeline (LOCKED)
```
Brio 100 → YuNet (face box) → PFLD-NCNN (landmarks) → Landmark Quality Filter
        → EAR + PERCLOS + Head-Pose(solvePnP) → weighted Fatigue Score
        → Temporal/State engine → Tier-0 alert → Arbiter → Speaker
```
- **Detector = YuNet** (`cv::FaceDetectorYN`) — zero new dependency (already use OpenCV), fast, fine
  for a centred cabin face.
- **Landmark = PFLD (NCNN)** — matches the NCNN/YOLO stack (one runtime), mobile-first (~0.5M params),
  feeds BOTH EAR (eye-contour pts) and head-pose (solvePnP face pts) from one inference.
- **5-point detectors (YuNet/RetinaFace/SCRFD landmarks alone) RULED OUT for the value** — 5 pts give
  only eye *centres*, no eyelid contour → cannot compute EAR. Need the ≥68-pt landmark stage.
- **Signals = EAR + PERCLOS + Head Pose.** Combined as a **weighted Fatigue Score (fusion)**, *not* a
  conjunctive multi-signal AND (an AND-gate on a life-safety detector raises **false negatives** =
  misses real drowsiness; sustained eye-closure / PERCLOS spike must be able to fire ALONE, with head
  pose + blink adding confidence + redundancy when EAR is defeated by glasses / head turn).

### Prototype strategy — **Option B (single model, PC→Pi)**
- Prototype with **the same PFLD-NCNN model on PC from day 1** (fp32 trusts cross-platform), then
  deploy the *same* model on Pi. "What you benchmark is what you deploy."
- **dlib REJECTED for deploy** — reason = 99 MB memory footprint + outside the NCNN ecosystem (extra
  runtime/dependency), **NOT** "landmark compute is heavy" (dlib's landmark regressor is actually
  cheap; the slow part is dlib's *own face detector*, which we don't use). Option A (dlib-68 PC →
  PFLD Pi) dropped: its only advantage (iBUG-68 index reuse) likely breaks anyway since PFLD is
  natively **WFLW-98**, not iBUG-68.

### Scheduling — "Do not gate. Modulate. Never shut down."
- **Brain 3 is ALWAYS-ON. fps floor > 0 — never gated/shut off.**
- **REJECTED: gating Brain 3 on a Brain-2 risk condition** = safety anti-pattern (drowsiness PEAKS on
  empty monotonous highways at night = exactly when Brain 2 is silent).
- **Cadence controller, 2 inputs** (both *raise* sample rate, neither gates):
  1. **Environmental Risk Prior** — from signals we actually have: **L2 belief = 90/100**, sign-absence
     duration, IMX477 optical-flow magnitude, driving duration. *(NO "lane stable" input — we have no
     lane detector; NO GPS.)*
  2. **Early fatigue** — EAR starting to drop.
- **FPS hierarchy (bench-tune):** Normal 10 · Risk-prior high 20 · early-fatigue 30 · **CPU-critical
  floor = 5 (never 0).**
- "Run only on highway" kept as a *documented last-resort tier only* — and even then, "always-on at
  low fps" beats "highway-only at full fps" (a 1 s closure = 5 frames @5fps → still caught everywhere;
  micro-sleep also happens in traffic jams / city / red lights, not only highways).

### Arbiter priority
- **Tier 0 = driver-survival (drowsiness) = highest rank, preempts EVERYTHING** (above School_Zone 30).
- Tiers: **0** survival · **1** immediate safety signs · **2** warning signs · **3** persistent speed.
- **Top-rank alert is GATED by 3-stage confirmation** (prevents the worst cry-wolf = false positive at
  the highest rank, Law 7):
  - **A — Quality:** face/eyes visible, landmark confidence high → else drop frame, don't update state.
  - **B — Temporal:** signal sustained (e.g. EAR low ≥ ~1.2 s), not a single frame.
  - **C — Score fusion:** weighted fatigue score crosses critical threshold (NOT all-signals-AND).

### NEW behavioural category — Brain 3 = "persistent-but-must-escalate"
Not the same as the existing two brains:
- Brain 1 (speed) = persistent → **say once** (re-derivable).
- Brain 2 (momentary) = **no second chance**.
- **Brain 3 (drowsiness) = persistent-but-must-ESCALATE** — the goal is to *wake* the driver, not just
  inform → it must NOT copy Brain 1's "announce once then silent."
- **Fatigue State Machine:** Normal → Suspicious → Confirmed → Critical-Alert-Sent → **two exits:**
  (1) **recovery detected** (eyes reopened ~10 s + head stable + normal blink) → reset to Normal;
  (2) **fatigue persists / worsens → ESCALATE** (more frequent / stronger), never go silent.
- Need a **per-person calibration phase at drive start** (EAR neutral baseline).

### v1 scope guards
- **NO gaze estimation, NO yawning (MAR) in v1.** Yawn = v2 add-on. Microsleep = a *temporal-pattern*
  problem (state machine), not a model problem.

### ACTION ITEM (the only thing blocking a 100% lock)
1. **Verify the PFLD NCNN port** on PC before final lock — (a) scheme (68 vs 98 points), (b) port
   quality. Everything above assumes a usable port exists; if not → revisit fallback. *(dlib-68 is the
   theoretical fallback but heavy on memory + off-ecosystem.)*
2. Later: bench-tune ALL numbers (fps tiers, temporal thresholds, recovery window) on the Pi.
