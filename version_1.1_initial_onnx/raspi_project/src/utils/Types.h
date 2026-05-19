// src/utils/Types.h
#pragma once

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

struct Detection {
    cv::Rect box;
    int class_id = -1;
    std::string class_name;
    float confidence = 0.0f;
};

struct LetterboxInfo {
    float scale = 1.0f;
    int pad_x = 0;
    int pad_y = 0;
    int input_size = 0;
};

struct AppConfig {
    std::string model_path = "../models/best640.onnx";

    int imgsz = 640;
    float conf_threshold = 0.25f;
    float iou_threshold = 0.45f;

    int camera_width = 1280;
    int camera_height = 720;

    int onnx_threads = 4;
    bool hide_window = false;
};
