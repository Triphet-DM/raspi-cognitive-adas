# Debugging Framework

## Primary Rule

Debug the whole system, not the symptom.

For computer vision and edge AI issues, always consider:

Sensor -> Camera Driver -> ISP -> Image Format -> Memory Transfer -> OpenCV -> Preprocessing -> Detection -> Tracking -> ROI Selection -> Classification -> Decision Logic -> Output

## Required Process

1. Restate the observed symptom.
2. Describe the current architecture.
3. Describe the data flow.
4. Identify where the symptom becomes visible.
5. List possible causes across the whole pipeline.
6. Rank causes by probability.
7. Explain the evidence for and against each cause.
8. Propose small experiments.
9. Evaluate the experiment results.
10. Identify root cause only when evidence is strong enough.
11. Suggest the smallest safe implementation change.
12. Explain tradeoffs and risks.

## Cause Ranking Template

Use a table or concise list:

- Cause.
- Probability.
- Evidence supporting it.
- Evidence against it.
- Experiment to confirm or reject it.
- Expected result if true.

## Experiment Design

Good experiments:

- Change one variable.
- Produce visible evidence.
- Use frame indices or timestamps.
- Save intermediate artifacts.
- Compare before and after.
- Avoid changing model, pipeline, and measurement at the same time.

For image pipeline bugs, prefer these artifacts:

- Full frame with bounding box.
- Raw ROI before resize.
- Final model input after preprocessing.
- Model output class/confidence.
- Frame index and detection box coordinates.

For realtime bugs, prefer these metrics:

- capture_wait_ms
- queue_wait_ms
- age_at_inference_start_ms
- infer_ms
- result_wait_ms
- latency_ms
- output_fps
- dropped_frames
- overwrites

## Symptom vs Cause Examples

Flat classifier crop:

- Symptom: saved classifier input is a solid color.
- Possible causes: ROI size is 1x1, wrong box format, stale frame, incorrect scale-back, invalid crop, wrong image buffer, padding-only resize.
- Good evidence: filename with `w1_h1`, full-frame box debug, raw ROI image.

Low FPS after async refactor:

- Symptom: displayed FPS drops.
- Possible causes: changed timing semantics, camera pacing exposed, main thread blocking, result polling order, condition variable wait, camera buffering.
- Good evidence: separate throughput FPS, latency, capture wait, inference time.

Wrong colors:

- Symptom: display or classifier input has color shift.
- Possible causes: RGB/BGR mismatch, ISP output format, OpenCV conversion, NCNN pixel format, camera configuration.
- Good evidence: known color test image, saved raw frame, saved preprocessed input, explicit format logs.

Good YOLO but bad classifier:

- Symptom: detector class looks plausible but classifier fails.
- Possible causes: crop wrong, classifier input color wrong, class mapping wrong, normalization mismatch, ROI too small, model trained on different domain.
- Good evidence: raw ROI and final classifier input.

## Root-Cause Discipline

Never jump directly from symptom to fix when multiple pipeline stages could cause the issue.

Use language carefully:

- "This suggests..." when evidence is partial.
- "The most likely cause is..." when ranked evidence is strong.
- "The root cause is..." only when the experiment clearly isolates the issue.

