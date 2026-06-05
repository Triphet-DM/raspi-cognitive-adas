#include "inference/SpeedSignClassifier.h"

#include <algorithm>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

static std::string safe_label(const std::string& value) {
    if (value.empty()) return "unknown";

    std::string out = value;
    for (char& ch : out) {
        const bool ok =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-';
        if (!ok) ch = '_';
    }
    return out;
}

static cv::Rect clamp_roi(const cv::Mat& frame_bgr, const cv::Rect& roi) {
    if (frame_bgr.empty()) return cv::Rect{};
    return roi & cv::Rect(0, 0, frame_bgr.cols, frame_bgr.rows);
}

static cv::Mat make_classifier_input(const cv::Mat& frame_bgr, const cv::Rect& safe_roi, int img_size) {
    if (safe_roi.area() <= 0) {
        return cv::Mat(img_size, img_size, CV_8UC3, cv::Scalar(114, 114, 114));
    }

    cv::Mat crop = frame_bgr(safe_roi).clone();

    const int max_side = std::max(crop.cols, crop.rows);
    cv::Mat square(max_side, max_side, CV_8UC3, cv::Scalar(114, 114, 114));
    const int offset_x = (max_side - crop.cols) / 2;
    const int offset_y = (max_side - crop.rows) / 2;
    crop.copyTo(square(cv::Rect(offset_x, offset_y, crop.cols, crop.rows)));

    cv::Mat resized;
    cv::resize(square, resized, cv::Size(img_size, img_size), 0, 0, cv::INTER_LINEAR);
    return resized;
}

static void save_classifier_debug_images(
    const std::string& roi_debug_dir,
    const cv::Mat& frame_bgr,
    const cv::Rect& requested_roi,
    const cv::Rect& safe_roi,
    const cv::Mat& cls_input_bgr,
    const std::string& yolo_class,
    int frame_idx,
    float yolo_conf
) {
    if (roi_debug_dir.empty()) return;

    fs::create_directories(roi_debug_dir);

    const std::string prefix = cv::format(
        "F%04d_%s_conf_%.2f_x%d_y%d_w%d_h%d",
        frame_idx,
        safe_label(yolo_class).c_str(),
        yolo_conf,
        requested_roi.x,
        requested_roi.y,
        requested_roi.width,
        requested_roi.height
    );

    const fs::path dir(roi_debug_dir);

    if (!frame_bgr.empty()) {
        cv::Mat full = frame_bgr.clone();

        if (requested_roi.area() > 0) {
            cv::rectangle(full, requested_roi, cv::Scalar(0, 0, 255), 2);
        }
        if (safe_roi.area() > 0) {
            cv::rectangle(full, safe_roi, cv::Scalar(0, 255, 0), 2);
        }

        const std::string text = cv::format(
            "%s %.2f req=[%d,%d,%d,%d] safe=[%d,%d,%d,%d]",
            yolo_class.empty() ? "unknown" : yolo_class.c_str(),
            yolo_conf,
            requested_roi.x,
            requested_roi.y,
            requested_roi.width,
            requested_roi.height,
            safe_roi.x,
            safe_roi.y,
            safe_roi.width,
            safe_roi.height
        );

        cv::putText(
            full,
            text,
            cv::Point(12, 28),
            cv::FONT_HERSHEY_SIMPLEX,
            0.65,
            cv::Scalar(0, 255, 255),
            2
        );

        cv::imwrite((dir / (prefix + "_01_full_box.jpg")).string(), full);
    }

    if (safe_roi.area() > 0) {
        cv::Mat raw_roi = frame_bgr(safe_roi).clone();
        cv::imwrite((dir / (prefix + "_02_raw_roi.jpg")).string(), raw_roi);
    } else {
        cv::Mat invalid(224, 224, CV_8UC3, cv::Scalar(32, 32, 32));
        cv::putText(
            invalid,
            "INVALID ROI",
            cv::Point(28, 112),
            cv::FONT_HERSHEY_SIMPLEX,
            0.7,
            cv::Scalar(0, 0, 255),
            2
        );
        cv::imwrite((dir / (prefix + "_02_raw_roi_INVALID.jpg")).string(), invalid);
    }

    if (!cls_input_bgr.empty()) {
        cv::imwrite((dir / (prefix + "_03_cls_input_224.jpg")).string(), cls_input_bgr);
    }
}

SpeedSignClassifier::SpeedSignClassifier(
    const std::string& param_path,
    const std::string& bin_path,
    int threads,
    bool use_vulkan,
    bool use_packing,
    float min_confidence
) : min_confidence_(min_confidence) {
    model_ = std::make_unique<NcnnModel>(
        param_path, bin_path, threads, use_vulkan, use_packing
    );
}

cv::Mat SpeedSignClassifier::preprocess(
    const cv::Mat& frame_bgr,
    const cv::Rect& roi
) const {
    const cv::Rect safe_roi = clamp_roi(frame_bgr, roi);
    return make_classifier_input(frame_bgr, safe_roi, IMG_SIZE);
}

std::string SpeedSignClassifier::classify(
    const cv::Mat& frame_bgr,
    const cv::Rect& roi,
    float* out_confidence,
    const std::string& roi_debug_dir,
    const std::string& yolo_class,
    int frame_idx,
    float yolo_conf
) const {
    const cv::Rect safe_roi = clamp_roi(frame_bgr, roi);
    cv::Mat input_bgr = make_classifier_input(frame_bgr, safe_roi, IMG_SIZE);

    save_classifier_debug_images(
        roi_debug_dir,
        frame_bgr,
        roi,
        safe_roi,
        input_bgr,
        yolo_class,
        frame_idx,
        yolo_conf
    );

    ncnn::Mat input = ncnn::Mat::from_pixels(
        input_bgr.data,
        ncnn::Mat::PIXEL_BGR,
        input_bgr.cols,
        input_bgr.rows
    );
    const float norm_vals[3] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
    input.substract_mean_normalize(nullptr, norm_vals);

    ncnn::Mat output = model_->run(input, "in0", "out0");

    const float* scores = static_cast<const float*>(output.data);
    const int num_cls = static_cast<int>(class_names_.size());

    if (output.w < num_cls) {
        if (out_confidence) *out_confidence = 0.0f;
        return "";
    }

    int best_idx = 0;
    float best_score = scores[0];
    for (int i = 1; i < num_cls; ++i) {
        if (scores[i] > best_score) {
            best_score = scores[i];
            best_idx = i;
        }
    }

    if (out_confidence) *out_confidence = best_score;

    if (best_score < min_confidence_) return "";
    return class_names_[best_idx];
}
