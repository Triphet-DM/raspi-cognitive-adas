# INT8 Detector — A/B Test Results

> Started 2026-06-16. Detector-only int8 vs fp32. Classifier fp32 in both.
> Accuracy measured with `yolo val` (ultralytics 8.4.33) on the **test split (1300 imgs,
> 1398 boxes, labels)** of `data.yaml`. Speed must be measured **on the Pi** with the app.

## Accuracy — fp32 baseline (DONE, on dev box)

Command:
```
yolo val task=detect model=best_ncnn_model imgsz=512 \
  data="D:/Project Version yolo 13 512/data.yaml" split=test conf=0.25 iou=0.45
```
Model = `quant/best_ncnn_model` = byte-identical (md5 `9fca6a5c…`) to the deployed
`src/models/detection/yolo11n/model.ncnn.*`.

| Class | Inst | P | R | mAP50 | mAP50-95 |
|---|---:|---:|---:|---:|---:|
| **all** | 1398 | 0.971 | 0.973 | **0.983** | **0.907** |
| Pedestrian_Warning_Sign | 96 | 0.981 | 0.969 | 0.978 | 0.920 |
| Pedestrian_crossing | 91 | 0.957 | 0.972 | 0.986 | 0.874 |
| School_Zone | 92 | 0.948 | 0.989 | 0.990 | 0.915 |
| Traffic_sign | 95 | 0.993 | 0.979 | 0.989 | 0.927 |
| curve_ahead | 94 | 0.979 | 0.987 | 0.992 | 0.909 |
| no_parking | 98 | 0.968 | 0.969 | 0.972 | 0.900 |
| no_passing | 94 | 1.000 | 0.986 | 0.995 | 0.912 |
| no_stop | 93 | 0.982 | 0.989 | 0.994 | 0.918 |
| no_u_turn | 94 | 0.989 | 0.968 | 0.984 | 0.879 |
| sign_100 | 91 | 0.963 | 0.989 | 0.990 | 0.908 |
| sign_50 | 91 | 0.989 | 0.978 | 0.988 | 0.911 |
| sign_60 | 95 | 1.000 | 0.935 | 0.978 | 0.911 |
| sign_80 | 95 | 0.906 | 0.947 | 0.965 | 0.887 |
| sign_90 | 91 | 0.946 | 0.954 | 0.966 | 0.916 |
| sign_four_way | 88 | 0.967 | 0.984 | 0.985 | 0.922 |

Baseline is strong and class-balanced (weakest mAP50 = sign_80 0.965). This is the bar int8
must stay close to.

## Accuracy — int8 (DONE 2026-06-16) — ❌ int8 model is NON-FUNCTIONAL

### What was tried
- **`yolo val` on int8 → INVALID.** Gave mAP 0 everywhere. Root cause: ultralytics' NCNN
  backend does **not** set `opt.use_int8_inference=true`, so ncnn misreads the int8 weights →
  garbage. `yolo val` cannot measure an int8 ncnn model. (Confirmed: fp32 via same path = fine.)
- **Faithful harness** `eval_ncnn_map.py` (ncnn-python, `use_int8_inference=True`, decode+NMS
  mirroring `YoloDetector.cpp`, **square 512×512 letterbox pad114 like the Pi app**). Ran fp32
  and int8 with identical code. Operating point conf=0.25, IoU-match 0.5.

### Result (identical harness, fair delta)
| | TP | FP | FN | Recall | Precision |
|---|---:|---:|---:|---:|---:|
| fp32 | 475 | 185 | 923 | 0.340 | 0.720 |
| **int8** | **0** | **1310** | 1398 | **0.000** | **0.000** |

**int8 = 0 true positives across all 15 classes**, with 1310 spurious boxes. The model still
emits boxes but **none localize within IoU 0.5** → int8 destroyed box regression. Corroborated
by raw scores: same gantry image fp32 max-class 0.869 vs int8 0.0035; 15-img sweep int8 fired
only on a few large signs, dead on small ones.

### Root cause (hypothesis)
Naive **full-graph** int8 quantization quantized the **detection head / DFL / output convs**,
which are highly precision-sensitive. Red flag in the calib table: `conv_80` (final head)
scale = **1977** (threshold 0.064) → logits clamped to a tiny range → output collapses.

### Remediation options (for a future attempt — NOT done)
1. **Partial / mixed int8** — quantize backbone only; keep the detection head (and usually the
   first conv) in fp32. Done by removing those layers from `yolo.table` before `ncnn2int8`, or
   per-layer exclusion. Most likely fix.
2. Better calibration (more/again-diverse imgs, `method=aciq`), though unlikely to save a head
   that shouldn't be quantized at all.
3. Accept that yolo11n→ncnn full int8 is not viable here; pursue speed via other routes
   (smaller imgsz, lighter model) per the original int8 playbook §4.

### ⚠️ Separate finding (affects fp32 too, independent of int8)
Debugging revealed **`YoloDetector.cpp` letterboxes to a square 512×512**, but ultralytics
val/predict uses a **rectangular** stride-padded letterbox. On odd-aspect test images the square
path can miss signs the rectangular path catches (e.g. a 413×512 portrait: ultralytics fp32
conf 0.956, our square-letterbox path 0.0). This is why the harness fp32 recall (0.340) is far
below ultralytics' 0.973 — the harness faithfully reproduces the *deployed* square preprocessing.
Real camera frames are landscape (≈960×560) so the gap is smaller in production, but **the
square-vs-rectangular letterbox is worth a look** as a possible fp32 accuracy win on its own.

## Speed — fp32 vs int8 (PENDING, MUST be on Pi)

Build on Pi, then read the app's own `infer_ms` / FPS telemetry (threads=2, the measured best):
```
# fp32 (baseline, current production)
./app --no-draw            # uses default yolo11n fp32 model
# int8
./app --no-draw --det-int8 --ncnn-param <…>/yolo_int8.param --ncnn-bin <…>/yolo_int8.bin
```
Expect int8 faster than the ~93 ms / ~10 FPS fp32 baseline. Record here when measured.

| Config | infer_ms | FPS | model .bin |
|---|---:|---:|---:|
| fp32 (Pi) | ~93 (prior) | ~10 | 10,405,172 B |
| int8 (Pi) | — | — | 2,705,772 B |

## ⭐ PI GROUND-TRUTH (2026-06-16, Diamond) — supersedes the Windows harness above
Ran on the actual Pi 5 (`./app --det-int8 --ncnn-param …/yolo_int8.param --ncnn-bin …`):
- **int8 DETECTS NORMALLY on Pi** — both fp32 and int8 detect fine. The int8 model is **NOT
  broken.** The Windows `eval_ncnn_map.py` "0 TP" result was a **platform artifact of the x86
  ncnn-python int8 path** (ncnn's int8 kernels are tuned/validated on ARM; the x86 wheel
  produced garbage int8 output). **Disregard the Windows int8 numbers above** — Pi is the
  deployment target and the ground truth. (The square-vs-rectangular letterbox note still
  stands as a separate fp32 item.)
- **int8 is SLOWER than fp32 on Pi by ~1–3 FPS.** The whole point of int8 was speed; it makes
  things *slower* here.

### Why int8 is slower on Cortex-A76 (expected)
1. A76 fp32 NEON is already fast — lowering precision gives little (mirrors the prior
   "FP16 arithmetic = no-op" finding).
2. quantize/dequantize conversions between layers cost more than the int8 compute saves.
3. int8 may skip winograd and fall back to a slower path.

## ⭐⭐ OPT SWEEP on Pi (2026-06-16) — closes the case with data
Pure-inference median ms on Pi (`bench_int8_opts.py`, ncnn-python ARM = correct int8):

| config | fp32 ms | int8 ms | int8/fp32 |
|---|---:|---:|---:|
| base (t2,pack,wino,sgemm, **fp16 OFF**) | 99.22 | 72.14 | 0.73 |
| t3 | 91.96 | 67.27 | 0.73 |
| t4 | 89.91 | 67.17 | 0.75 |
| t1 | 147.88 | 95.82 | 0.65 |
| no-winograd | 118.60 | 71.73 | 0.60 |
| no-sgemm | 96.53 | 78.65 | 0.81 |
| no-pack | 109.55 | 83.55 | 0.76 |
| **fp16-on** | **46.20** | 49.18 | **1.06** |
| **t4+fp16** | **38.73** | 45.47 | **1.17** |
| t4+no-wino | 97.47 | 67.45 | 0.69 |

**Reconciliation of the earlier contradictions:** int8 beats fp32 *only when fp16 is forced
OFF*. But `NcnnModel.cpp` never touches the fp16 opts → ncnn **defaults `use_fp16_storage`/
`use_fp16_packed` = ON**, so the real app's fp32 already runs the fast fp16 path (~46 ms
region), and int8 is slower than that. That is exactly why the app showed int8 **1–3 FPS
slower**. The "base" bench row (fp16 off) is not the app's real baseline.

**fp16 nuance:** the win is from fp16 **storage/packed** (memory bandwidth), NOT fp16
**arithmetic** — so the prior PROJECT_STATUS note "FP16 arithmetic = no-op" still holds; they
are different opts.

## 🔭 Future / design pivot: int8 BECOMES the right choice WITH an accelerator (Pi AI HAT / NPU)

**Scope of today's "int8 not worth it" verdict: it applies ONLY to Pi 5 *CPU* inference.**
The reason int8 lost is CPU-specific — Cortex-A76 already runs fp32+fp16 NEON efficiently, and
the int8 quantize/dequantize overhead on CPU ate the gains. That logic **inverts** on a
dedicated accelerator:

- **NPUs are built around int8.** A Pi AI HAT (Hailo-8 / Hailo-8L), Coral Edge TPU, etc. are
  int8 MAC arrays — int8 (or the vendor's quantized format) is their *native, preferred,
  often only* precision. There is no "fp32 is already fast" competitor on those chips; int8 is
  how you use them at all.
- **It offloads the CPU entirely.** Detection moves off the A76 → the whole CPU is freed for
  the camera thread, the classifier, audio (`aplay`), and **Brain 2 arbitration**. That can
  mean *higher imgsz AND higher FPS at the same time*, plus thermal headroom — a much bigger
  win than shaving CPU ms.
- **Today's int8 work is a prerequisite, not wasted.** The calibration image set
  (`calib_int8/images_512/`, stratified + letterboxed), the preprocessing recipe (pad114, BGR,
  norm 1/255), and the accuracy-eval method all transfer.

**Caveat — toolchain differs per accelerator (the ncnn int8 model does NOT port directly):**
- **Hailo (Pi AI HAT):** uses the **Hailo Dataflow Compiler** — feed it ONNX (`yolo export
  format=onnx`) + a calibration image set; Hailo does *its own* quantization to `.hef`. Our
  ncnn `yolo.table`/`yolo_int8.*` is not reused, but the **same calibration images** are.
- **Coral Edge TPU:** needs **TFLite int8** (`yolo export format=tflite int8=True`) then
  `edgetpu_compiler`.
- Either way: re-quantize with the vendor tool; keep the architecture's classifier-vs-detector
  split decision (the detector is the heavy part worth offloading).

**Recommendation if hardware budget allows:** evaluate a **Pi AI HAT (Hailo-8L)** for the
detector. It is the path where int8 pays off — re-run the accuracy/speed A/B with the Hailo
toolchain before committing. Logged here so the int8 effort is picked up, not repeated.

## Decision (2026-06-16): ❌ DO NOT adopt int8 on Pi 5 CPU — and DO NOT pursue partial quantization (CPU)
- int8 detects fine but is **slower** on Pi 5 → it fails its only purpose (speed).
- **Partial quantization is also dropped:** mixed precision adds *more* quant/dequant
  boundaries → even more overhead → cannot beat fp32 if full int8 already can't.
- Speed should be pursued elsewhere: **`--async-detect --async-camera`** (pipeline overlap,
  helps fp32 FPS), **lower imgsz** (512→416/384), lighter model, thread tuning.
- Keep: `--det-int8` switch stays in the code (harmless, default OFF) as documentation of this
  experiment; calibration assets archived. No production change.
