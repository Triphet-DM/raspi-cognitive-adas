# Project Learnings

Record durable lessons learned from the user's real engineering project work. Keep entries concrete, evidence-backed, and reusable.

## 2026-06-05 - Classifier failures can originate from detector box decoding

- Context: The speed sign classifier received debug crops that were flat color fields with no visible traffic sign.
- Evidence: New ROI debug filenames showed `w1_h1`, proving the classifier was being given a 1x1 pixel crop. After changing YOLO box decoding from treating output as `x1,y1,x2,y2` to converting `cx,cy,w,h` into corners, ROI debug images showed real speed signs again.
- Lesson: Do not assume a classifier problem is caused by the classifier model. Validate the full data path first: detector output format, box scaling, ROI selection, crop, resize/pad, normalization, then classifier inference.
- Future use: When classification quality is poor, first inspect full-frame box overlays, raw ROI crops, and final classifier input images before changing the classifier model or training data.

## 2026-06-05 - Realtime async metrics need separate throughput and latency concepts

- Context: After enabling async detection, apparent FPS dropped and latency increased, which initially looked like a performance regression.
- Evidence: `cap_wait_ms` exposed real camera pacing, `out_fps` showed detector throughput remained stable, and `latency_ms` represented end-to-end pipeline delay rather than pure processing time.
- Lesson: In realtime systems, throughput FPS and end-to-end latency are different measurements. Async pipelines often reveal waiting, buffering, and result delay that synchronous loops hide.
- Future use: Keep measuring `output_fps`, `latency_ms`, `capture_wait_ms`, `queue_wait_ms`, inference time, result wait, dropped frames, and overwrites separately before changing queue policy or adding threads.

## 2026-06-05 - Latest-frame overwrite is a deliberate bounded-latency tradeoff

- Context: The camera can produce frames faster than YOLO/NCNN can process them on Raspberry Pi 5.
- Evidence: Dropped frame and overwrite counters increased while output FPS stayed usable and latency remained bounded.
- Lesson: Dropping stale frames can be correct for realtime perception when freshness matters more than processing every frame.
- Future use: Prefer a latest-frame slot for camera-to-detector handoff unless the task specifically requires complete frame coverage. Avoid increasing queue size without proving that added latency is acceptable.

## 2026-06-05 - Headless Raspberry Pi deployment changes frontend/debug assumptions

- Context: The app failed on a Raspberry Pi OS setup without GUI.
- Evidence: OpenCV window calls were not appropriate for the user's deployment environment, so default behavior needed to be headless.
- Lesson: Edge AI deployment constraints are part of the architecture, not an afterthought. Visualization and debug image saving should be optional and controlled by flags.
- Future use: Default Raspberry Pi realtime runs to no GUI, keep `--show-window` opt-in, and remember that image saving can distort realtime performance measurements.
