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

struct StageTimes {
    double capture_ms = 0.0;
    double preprocess_ms = 0.0;
    double infer_ms = 0.0;
    double postprocess_ms = 0.0;
    double draw_ms = 0.0;
    double total_ms = 0.0;
};

struct AppConfig {
    std::string model_path = "../src/models/best512s.onnx";
    int imgsz = 512;
    float conf_threshold = 0.25f;
    float iou_threshold = 0.45f;
    int onnx_threads = 3;
    int camera_width = 960;
    int camera_height = 560;
    int camera_fps = 30;
    bool hide_window = false;
    bool no_draw = false;
    int avg_window = 50;
};
