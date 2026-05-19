# Traffic Sign Edge AI (Raspberry Pi 5 + NCNN)

Real-time traffic sign detection system running on Raspberry Pi 5 using NCNN and OpenCV.

The system performs real-time inference with temporal voting and cooldown filtering for stable traffic sign recognition in edge environments.

---

# Features

- Real-time traffic sign detection
- NCNN optimized inference on Raspberry Pi 5
- Temporal voting stabilization
- Cooldown suppression system
- OpenCV visualization pipeline
- Telephoto lens support for long-range detection

---

# Hardware Setup

## Raspberry Pi HQ Camera + 16mm Telephoto Lens

![hardware_closeup](README_assets/hardware_closeup.jpg)

## Real-world Testing Setup

![testing_setup](README_assets/testing_setup.jpg)

---

# Hardware

- Raspberry Pi 5
- Raspberry Pi HQ Camera
- 16mm Telephoto Lens
- NCNN Runtime
- OpenCV

The telephoto lens was used to improve long-range traffic sign visibility during real-world testing.

---

# Demo Results

## Speed Limit 90 Detection

![demo_90](README_assets/demo_90.png)

## Traffic Light Detection

![demo_traffic_light](README_assets/demo_traffic_light.png)

## No U-Turn Detection

![demo_no_uturn](README_assets/demo_no_uturn.png)

## School Zone Detection

![demo_school_zone](README_assets/demo_school_zone.png)

## Pedestrian Warning Detection

![demo_pedestrian](README_assets/demo_pedestrian.png)

---

# Performance

Example runtime performance on Raspberry Pi 5:

- FPS: ~19 FPS
- Inference latency: ~47 ms
- Real-time edge inference using NCNN

---

# Tech Stack

- C++
- OpenCV
- NCNN
- CMake
- Raspberry Pi OS

---

# Future Plans

- Add multi-sign tracking support
- Optimize NCNN inference pipeline
- Add distance estimation
- Improve temporal voting stability
- Real-world road deployment testing
- Quantization and performance optimization
- GPS-assisted traffic sign localization for nighttime fallback detection
- Hybrid camera + GPS traffic sign awareness system
