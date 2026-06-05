# Realtime Edge AI Principles

## Raspberry Pi 5 Reality

Raspberry Pi 5 is capable but constrained:

- CPU budget is limited.
- Memory bandwidth matters.
- Extra `cv::Mat` clones can matter.
- Image saving can distort measurements.
- Terminal logging can distort measurements.
- Camera buffering can hide or create latency.
- Thermal throttling may affect long runs.
- Headless deployment changes GUI assumptions.

## Throughput vs Latency

Throughput FPS:

- How many outputs per second the system produces.
- Useful for measuring detector work rate.

End-to-end latency:

- Time from frame capture to output decision.
- More important for realtime responsiveness.

Do not use one metric to explain the other.

## Queue Policy

Latest-frame overwrite:

- Best when freshness matters more than processing every frame.
- Keeps latency bounded.
- Drops frames intentionally.
- Works well when camera producer is faster than detector consumer.

FIFO queue:

- Best when every frame matters.
- Can accumulate latency if consumer is slower than producer.
- Harder to keep realtime freshness.

For traffic sign response on a constrained edge device, latest-frame overwrite is often the practical default.

## Thread Boundaries

Good thread boundaries:

- Camera capture thread when camera blocking hurts result handling.
- Detector worker when inference is the dominant bottleneck.
- Optional classifier worker only if classifier becomes expensive or blocks decision logic.

Avoid new threads when:

- The stage is cheap.
- The boundary requires excessive copying.
- Debugging becomes much harder.
- The bottleneck is not proven.

## Frame Ownership

Use clear ownership rules:

- If a frame crosses a thread boundary and may outlive the source buffer, clone it.
- If a frame is used only within the same synchronous stage, avoid unnecessary clone.
- If ROI uses a parent frame, ensure the parent frame remains alive.
- Save frame index and timestamp with frame data.

## Timing Metrics

Recommended metrics:

- capture_wait_ms: blocking wait for camera or latest frame.
- queue_wait_ms: time waiting before inference starts.
- age_at_inference_start_ms: frame age when detector begins.
- infer_ms: actual model compute.
- result_wait_ms: time completed result waits before main thread handles it.
- latency_ms: capture to output.
- output_fps: completed outputs per second.
- dropped_frames: frames intentionally skipped.
- overwrites: slot replacements.

## Practical Design Preference

Use a small, observable architecture:

Camera -> latest-frame slot -> detector worker -> latest-result slot -> decision/voting -> output

This is simple, bounded, debuggable, and realistic for Raspberry Pi 5.

Scale gradually:

- Add classifier after ROI correctness is proven.
- Add dual camera only after single-camera data ownership is stable.
- Add event-driven result handling only if polling creates measurable result_wait_ms.
- Optimize model only after pipeline correctness is proven.

