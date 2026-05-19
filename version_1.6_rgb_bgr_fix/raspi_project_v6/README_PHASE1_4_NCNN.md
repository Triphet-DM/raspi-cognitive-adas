# Phase 1.4 NCNN Test Notes

Goal: keep the Phase 1.3 camera/preprocess/postprocess pipeline mostly unchanged, and swap only the inference runtime from ONNX Runtime to NCNN.

## 1. Convert model to NCNN

Best option from the training/export machine:

```bash
yolo export model=best.pt format=ncnn imgsz=512 half=False dynamic=False
```

Then copy the generated `.param` and `.bin` files into:

```text
src/models/best512s_ncnn.param
src/models/best512s_ncnn.bin
```

If you use the default Ultralytics export names, the app defaults also support:

```text
src/models/model.ncnn.param
src/models/model.ncnn.bin
```

Alternative if you only have ONNX and NCNN tools installed:

```bash
onnx2ncnn best512s.onnx best512s_ncnn.param best512s_ncnn.bin
```

## 2. Build on Raspberry Pi 5

Install/build NCNN first so CMake can find `ncnnConfig.cmake`.

```bash
cd raspi_project_phase1_4_ncnn
mkdir -p build
cd build
cmake ..
make -j4
```

If CMake cannot find NCNN, pass `ncnn_DIR`:

```bash
cmake .. -Dncnn_DIR=/path/to/ncnn/lib/cmake/ncnn
```

## 3. First run: check load and output shape

```bash
./app --no-draw
```

The app prints the NCNN output shape once:

```text
NCNN output    : dims=... c=... h=... w=... elempack=...
```

If it fails at input/output blob, inspect names in the `.param` file and rerun:

```bash
./app --no-draw --input-name images --output-name output0
./app --no-draw --input-name input --output-name output
```

Common names from different export paths include:

```text
input:  in0, images, input
output: out0, output0, output
```

For this project, NCNN may print:

```text
NCNN output    : dims=2 c=1 h=20 w=5376 elempack=1
```

That is the expected raw YOLO layout for this 16-class model:

```text
20 attributes = 4 box values + 16 class scores
5376 boxes    = prediction candidates at imgsz 512
```

## 4. Test order

Keep the baseline fixed first:

```text
camera 960x560
imgsz 512
threads 3
no-draw on
vulkan off
packing on
```

Then test only one thing at a time:

```bash
./app --no-draw --threads 1
./app --no-draw --threads 2
./app --no-draw --threads 3
./app --no-draw --threads 4
```

Only after CPU mode works:

```bash
./app --no-draw --vulkan
```

Terminal output shows the top detections:

```text
det: 1 | top: sign_80 0.83 box:42x38
```

Meaning:

```text
sign_80 = predicted class
0.83    = model confidence
42x38   = detected box size in camera pixels after scaling back
```

Use `--top-k N` to show more detections:

```bash
./app --no-draw --top-k 5
```

## 5. Expected interpretation

If NCNN runs but detection is wrong, the likely causes are:

```text
wrong output blob
different output layout
model export includes built-in NMS
class/objectness format differs from the current postprocess
```

If detection is correct but FPS is similar, the bottleneck is still model compute and the next test should be NCNN packing/fp16/optimized export.
