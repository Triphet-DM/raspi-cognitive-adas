#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

#include "utils/Types.h"

void draw_phase1_overlay(
    cv::Mat& canvas_bgr,
    const std::vector<Detection>& detections,
    const StageTimes& times,
    double fps_avg,
    int imgsz,
    float conf_threshold
);
