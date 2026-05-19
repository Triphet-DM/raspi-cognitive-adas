// src/vision/Draw.h
#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

#include "utils/Types.h"

void draw_phase1_overlay(
    cv::Mat& frame,
    const std::vector<Detection>& detections,
    double fps,
    double infer_ms,
    int imgsz,
    float conf_threshold
);
