#pragma once

#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "inference/OnnxModel.h"
#include "utils/Types.h"

class YoloDetector {
public:
    YoloDetector(
        const std::string& model_path,
        int imgsz,
        float conf_threshold,
        float iou_threshold,
        int threads
    );

    std::vector<Detection> detect(
        const cv::Mat& frame_rgb,
        double* preprocess_ms,
        double* infer_ms,
        double* postprocess_ms
    );

private:
    void preprocess_rgb_to_nchw(const cv::Mat& input_rgb);
    std::vector<Detection> postprocess(
        const float* output_data,
        const std::vector<int64_t>& output_shape,
        int original_width,
        int original_height
    );

private:
    std::unique_ptr<OnnxModel> model_;
    int imgsz_;
    float conf_threshold_;
    float iou_threshold_;

    LetterboxInfo last_letterbox_;
    cv::Mat letterboxed_rgb_;
    std::vector<float> input_buffer_;

    std::vector<std::string> class_names_ = {
        "Pedestrian_Warning_Sign",
        "Pedestrian_crossing",
        "School_Zone",
        "Traffic_sign",
        "curve_ahead",
        "no_parking",
        "no_passing",
        "no_stop",
        "no_u_turn",
        "sign_100",
        "sign_120",
        "sign_50",
        "sign_60",
        "sign_80",
        "sign_90",
        "sign_four_way"
    };
};
