#pragma once

#include <opencv2/opencv.hpp>

#include "utils/Types.h"

cv::Mat letterbox_rgb(const cv::Mat& src_rgb, int input_size, LetterboxInfo& info);

cv::Rect scale_box_back(
    float x1,
    float y1,
    float x2,
    float y2,
    const LetterboxInfo& info,
    int original_width,
    int original_height
);
