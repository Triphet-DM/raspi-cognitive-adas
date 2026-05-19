#pragma once

#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "inference/NcnnModel.h"
#include "utils/Types.h"

class YoloDetector {
public:
    YoloDetector(
        const std::string& param_path,
        const std::string& bin_path,
        int imgsz,
        float conf_threshold,
        float iou_threshold,
        int threads,
        const std::string& input_name,
        const std::string& output_name,
        bool use_vulkan,
        bool use_packing
    );

    std::vector<Detection> detect(
        const cv::Mat& frame_bgr,
        double* preprocess_ms,
        double* infer_ms,
        double* postprocess_ms
    );

private:
    ncnn::Mat preprocess_bgr_to_ncnn(const cv::Mat& input_bgr);
    std::vector<Detection> postprocess(
        const float* output_data,
        const std::vector<int64_t>& output_shape,
        int original_width,
        int original_height
    );

private:
    std::unique_ptr<NcnnModel> model_;
    std::string input_name_;
    std::string output_name_;
    int imgsz_;
    float conf_threshold_;
    float iou_threshold_;

    LetterboxInfo last_letterbox_;
    cv::Mat letterboxed_bgr_;
    bool output_shape_printed_ = false;

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
        "sign_50",
        "sign_60",
        "sign_80",
        "sign_90",
        "sign_four_way"
    };
};
