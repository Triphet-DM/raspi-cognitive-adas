#pragma once

// ============================================================
// SpeedSignClassifier
//
// YOLO11n-cls model สำหรับแยก sign_50/60/80/90/100
// รับ crop จาก YOLO detection แล้วคืน class_name ที่แม่นกว่า
//
// Design decisions:
//   - ใช้ NcnnModel เดิมโดยตรง ไม่สร้าง abstraction ใหม่
//   - รัน synchronously ใน caller thread (detector thread)
//   - ไม่มี state ระหว่าง calls — thread-safe ถ้าเรียกจาก thread เดียว
//   - crop + resize ทำใน OpenCV ไม่ใช่ NCNN เพราะ simpler
//
// Input:  cv::Mat BGR crop จาก frame (ขนาดใดก็ได้)
// Output: class_name string, confidence float
//         ถ้า confidence < min_confidence คืน "" (ไม่แทนที่)
// ============================================================

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <opencv2/opencv.hpp>

#include "inference/NcnnModel.h"

class SpeedSignClassifier {
public:
    SpeedSignClassifier(
        const std::string& param_path,
        const std::string& bin_path,
        int threads,
        bool use_vulkan,
        bool use_packing,
        float min_confidence = 0.70f
    );

    // classify crop ที่ส่งมา
    // คืน class_name ถ้า confidence >= min_confidence
    // คืน "" ถ้าไม่มั่นใจพอ (ให้ YOLO result ชนะ)
    // classify + optional ROI save
    // roi_debug_dir: ถ้าไม่ว่าง → save crop ก่อน classify
    // yolo_class / frame_idx ใช้ใน filename เท่านั้น
    std::string classify(
        const cv::Mat& frame_bgr,
        const cv::Rect& roi,
        float* out_confidence = nullptr,
        const std::string& roi_debug_dir = "",
        const std::string& yolo_class = "",
        int frame_idx = 0,
        float yolo_conf = 0.0f
    ) const;

    // set ของ YOLO class names ที่ต้องส่งเข้า classifier
    // ถ้าไม่อยู่ใน set นี้ → bypass classifier ทันที
    static const std::unordered_set<std::string>& speed_sign_group() {
        static const std::unordered_set<std::string> group = {
            "sign_100", "sign_50", "sign_60", "sign_80", "sign_90"
        };
        return group;
    }

private:
    cv::Mat preprocess(const cv::Mat& frame_bgr, const cv::Rect& roi) const;

    std::unique_ptr<NcnnModel> model_;
    float min_confidence_;

    // class names ของ classifier — ต้องตรงกับ order ที่ train มา
    // จาก data.yaml: ['sign_100','sign_50','sign_60','sign_80','sign_90']
    // (alphabetical order ที่ Roboflow/YOLO ใช้)
    const std::vector<std::string> class_names_ = {
        "sign_100",
        "sign_50",
        "sign_60",
        "sign_80",
        "sign_90"
    };

    static constexpr int IMG_SIZE = 224;
};
