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
