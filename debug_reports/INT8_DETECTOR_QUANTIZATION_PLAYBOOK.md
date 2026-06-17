# INT8 Detector Quantization — Playbook

> **Created:** 2026-06-16
> **Scope:** Quantize **ONLY the YOLO detector** to int8. **Classifier stays fp32** (value
> authority must stay accurate — see PROJECT_STATUS "YOLO = presence/ROI, CLS = value").
> **Status:** PLAN / NOT YET RUN. No project code changed yet.
> **Goal:** speed up the inference bottleneck (~93 ms, detector-dominated) without touching
> classifier accuracy. Decide *with measured A/B data* whether int8 detector is worth it
> (honors Q6: never fix road-dependent params from assumption).

---

## 0. Why detector-only (recap)

- Detector = heavy compute (the FPS bottleneck) + detection task tolerates int8 well.
- Classifier = tiny (runs only on confirm-frame ROI) + value decision is **error-sensitive**
  → quantizing it risks misreading 50/60/80… for ~0 speed gain. **Keep fp32.**
- Detector and classifier are **separate `ncnn::Net` instances** (`YoloDetector` vs
  `SpeedSignClassifier`) → opts/weights fully independent → mixed precision is trivial.

---

## 1. Assets (ready)

| Item | Path | State |
|---|---|---|
| Trained weight (deployed truth) | `D:\Project Version yolo 13 512\runs\detect\train\weights\best.pt` | ✅ confirmed by Diamond = same one exported to the Pi ncnn model |
| Calibration images (letterboxed 512, gray 114) | `D:\Project Version yolo 13 512\calib_int8\images_512\` (510 imgs) | ✅ visually checked |
| Calibration image list (abs paths) | `D:\Project Version yolo 13 512\calib_int8\imagelist.txt` | ✅ 510 lines |
| Letterbox prep script (port of `Letterbox.cpp`) | `D:\Project Version yolo 13 512\calib_int8\prep_calib_letterbox.py` | ✅ |

### Detector preprocessing recipe (from `YoloDetector.cpp` + `Letterbox.cpp`) — MUST match
- Letterbox: `scale=min(512/w,512/h)`, keep aspect, center, **pad (114,114,114) gray**.
  *(Already baked into `images_512/`; do NOT let ncnn2table resize-distort raw rect images.)*
- `pixel = BGR` (`PIXEL_BGR`)
- `mean = [0,0,0]` (code passes `nullptr`)
- `norm = [1/255, 1/255, 1/255] = [0.00392157, 0.00392157, 0.00392157]`
- input blob = **`in0`**, output blob = **`out0`**, imgsz = **512**

---

## 2. The three commands (run on the Pi, or any box with ncnn tools)

> Keep int8 as **separate files** (`yolo_int8.*`). **Do NOT overwrite** the live
> `src/models/detection/yolo11n/model.ncnn.*` — we need fp32 intact for the A/B test and
> for rollback.

```bash
# --- Step 1: export best.pt -> ncnn fp32 (same as the live model) ---
yolo export model=best.pt format=ncnn imgsz=512 half=False dynamic=False
#   -> produces best_ncnn_model/model.ncnn.param + model.ncnn.bin
#   rename for clarity:
cp best_ncnn_model/model.ncnn.param yolo_fp32.param
cp best_ncnn_model/model.ncnn.bin   yolo_fp32.bin

# (optional but recommended) fold/optimize graph before quantizing
ncnnoptimize yolo_fp32.param yolo_fp32.bin yolo_opt.param yolo_opt.bin 0

# --- Step 2: calibration -> activation range table (NO labels used) ---
ncnn2table yolo_opt.param yolo_opt.bin imagelist.txt yolo.table \
  mean=[0,0,0] \
  norm=[0.00392157,0.00392157,0.00392157] \
  shape=[512,512,3] \
  pixel=BGR \
  thread=4 \
  method=kl
#   shape=[512,512,3] is a no-op resize because images_512/ are already 512x512.
#   method=kl (KL-divergence) is the standard; can try method=aciq later.

# --- Step 3: quantize fp32 -> int8 using the table ---
ncnn2int8 yolo_opt.param yolo_opt.bin yolo_int8.param yolo_int8.bin yolo.table
```

Outputs to keep: `yolo_int8.param`, `yolo_int8.bin` (+ `yolo.table` for the record).

---

## 3. Code change required to actually RUN int8 (NOT YET APPLIED)

`NcnnModel` currently has **no int8 switch** — `opt.use_int8_inference` is never set. To
load `yolo_int8.*` as int8 on the detector only, a small change is needed (do at build time,
not now):

1. **`NcnnModel` ctor** — add `bool use_int8` param → `net_.opt.use_int8_inference = use_int8;`
   *(set it BEFORE `load_param`/`load_model`).*
2. **`YoloDetector`** — pass `use_int8 = true`; **`SpeedSignClassifier`** — pass `false`.
3. **`main.cpp` / `AppConfig`** — add a flag e.g. `--det-int8` + `--ncnn-param/--ncnn-bin`
   already exist to point at `yolo_int8.*`. Default OFF so production behavior is unchanged.

Gotcha: if `use_int8_inference=true` but the model file has **no** int8 calibration baked in,
ncnn silently falls back to fp32 (no error, no speedup) — so verify the speedup actually
appears, otherwise you're not really running int8.

---

## 4. A/B test plan (the real work — decide with data, not assumption)

Run the **same** input through fp32 vs int8 detector, classifier fp32 in both.

**Setup:** point the app at each detector model in turn (via `--ncnn-param/--ncnn-bin`),
classifier unchanged, `--threads 2` (the measured best), same conf/iou (0.25 / 0.45).

**Dataset for the measurement:** the held-out **`test/` set (1300 imgs) WITH labels** — here
labels ARE used (for scoring recall/precision), unlike calibration which used none.

| Metric | fp32 (baseline) | int8 | Pass condition |
|---|---|---|---|
| **Per-class recall** (esp. small/distant boxes) | — | — | int8 recall drop **≤ ~2–3%**, no class collapses |
| **Per-class precision** | — | — | no big rise in false positives (anti cry-wolf, Law 7) |
| **mAP@0.5** (overall) | — | — | within a few % of fp32 |
| **Inference time / FPS** on Pi 5 | ~93 ms / ~10 FPS | — | must be **meaningfully faster** to justify the accuracy cost |
| **Model size** | — | — | expect ~4× smaller (sanity that int8 took effect) |

**Decision rule:** adopt int8 detector **only if** FPS gain is real **AND** Safety-family
recall (Pedestrian_*, School_Zone) does not degrade — those gate "low suppression" downstream
(06-14/06-15 design). If a class collapses → revisit calibration (more/again diverse imgs,
try `method=aciq`), or keep that path fp32.

---

## 5. Gotchas / rollback

- **Do not overwrite** the live `src/models/detection/yolo11n/model.ncnn.*`. Test int8 via
  flags first; only swap in after A/B passes.
- **Letterbox pad must be gray 114, not black** — already handled by `prep_calib_letterbox.py`;
  never feed Roboflow "Fit (black edges)" images to calibration.
- **Street View UI chrome** is baked into all dataset imgs → calibration matches the *training*
  domain, but the Pi camera sees clean frames. Residual domain gap exists for both fp32 and
  int8; if accuracy is borderline, add real Pi-camera captures to the calibration set.
- **Classifier stays fp32** — never quantize it in this effort.
- Rollback = just keep pointing the app at the original fp32 model; int8 lives in separate files.

---

## 6. Checklist

- [x] `best.pt` confirmed = deployed model (✅ Diamond, by hand)
- [x] 510 calib imgs letterboxed 512 + visually verified (2026-06-16, Diamond OK)
- [x] export `best.pt` → `yolo_fp32.*` — **done 2026-06-16 on the Windows box**;
      **verified byte-identical (md5 `9fca6a5c…`) to the deployed
      `src/models/detection/yolo11n/model.ncnn.bin`** → quantizing the exact live model.
      Output: `D:\Project Version yolo 13 512\calib_int8\quant\yolo_fp32.{param,bin}`
- [x] `ncnn2table` with matching mean/norm/BGR → `yolo.table` — **done on Pi 2026-06-16**
      (510 imgs, KL). Use `run_quantize_on_pi.sh`.
- [x] `ncnn2int8` → `yolo_int8.*` — **done on Pi 2026-06-16**. int8 `.bin` = 2,705,772 B vs
      fp32 10,405,172 B (**~3.85× smaller → int8 took effect**). 81 conv + 7 depthwise quantized.
- [x] add `use_int8` to `NcnnModel` + `--det-int8` flag — **done 2026-06-16**
      (`NcnnModel`, `YoloDetector`, `Types.h` AppConfig, `main.cpp`). Default OFF → production
      unchanged. Classifier hard-wired fp32 (`SpeedSignClassifier` passes the NcnnModel default).
      **Not yet compiled on Pi.**
- [ ] A/B: recall/precision/mAP + FPS + size, fp32 vs int8
- [ ] decision: adopt int8 detector iff FPS↑ real AND Safety recall not degraded

### Code change detail (2026-06-16)
- `NcnnModel(... , bool use_int8=false)` → `net_.opt.use_int8_inference = use_int8;`
- `YoloDetector(... , bool use_int8=false)` → forwards to NcnnModel.
- `AppConfig.det_int8` + `--det-int8` flag → passed to `YoloDetector` in `main.cpp`;
  prints `[Detector] int8 inference ENABLED`. Point at int8 files via existing
  `--ncnn-param/--ncnn-bin`.
- Run int8: `./app --det-int8 --ncnn-param quant/yolo_int8.param --ncnn-bin quant/yolo_int8.bin ...`

### Handoff artifacts (ready 2026-06-16, in `D:\Project Version yolo 13 512\calib_int8\`)
- `quant\yolo_fp32.{param,bin}` — exported fp32 detector (= live model, md5-verified)
- `images_512\` (510) + `imagelist_pi.txt` (relative paths) — calibration input
- `run_quantize_on_pi.sh` — copy folder to Pi, `cd calib_int8`, `bash run_quantize_on_pi.sh`
  → produces `quant\yolo_int8.{param,bin}` (+ `yolo.table`). Live model NOT touched.
