#include "vision/Draw.h"

#include <string>
#include <vector>

static cv::Scalar color_for_class(int class_id) {
    static const std::vector<cv::Scalar> palette = {
        cv::Scalar(255, 255, 0),
        cv::Scalar(0, 255, 0),
        cv::Scalar(0, 128, 255),
        cv::Scalar(255, 0, 255),
        cv::Scalar(0, 255, 255),
        cv::Scalar(255, 128, 0)
    };

    return palette[class_id % palette.size()];
}

static void draw_label(cv::Mat& image, const std::string& text, int x, int y, const cv::Scalar& color) {
    int base = 0;
    const double scale = 0.55;
    const int thickness = 2;
    cv::Size size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, thickness, &base);

    y = std::max(y, size.height + 10);

    cv::rectangle(image, cv::Rect(x, y - size.height - 10, size.width + 10, size.height + base + 8), color, -1);
    cv::putText(image, text, cv::Point(x + 5, y - 5), cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0, 0, 0), thickness);
}

void draw_phase1_overlay(
    cv::Mat& canvas_bgr,
    const std::vector<Detection>& detections,
    const StageTimes& times,
    double fps_avg,
    int imgsz,
    float conf_threshold
) {
    for (const auto& det : detections) {
        const cv::Scalar color = color_for_class(det.class_id);
        cv::rectangle(canvas_bgr, det.box, color, 2);

        const std::string label = det.class_name + " " + cv::format("%.2f", det.confidence);
        draw_label(canvas_bgr, label, det.box.x, std::max(20, det.box.y), color);
    }

    cv::rectangle(canvas_bgr, cv::Rect(0, 0, canvas_bgr.cols, 145), cv::Scalar(18, 18, 18), -1);

    cv::putText(canvas_bgr, "Phase 1: Camera -> YOLO -> Draw", cv::Point(20, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

    cv::putText(canvas_bgr,
                "FPS: " + cv::format("%.2f", fps_avg) +
                " | total: " + cv::format("%.1f", times.total_ms) + " ms" +
                " | det: " + std::to_string(detections.size()),
                cv::Point(20, 56), cv::FONT_HERSHEY_SIMPLEX, 0.62, cv::Scalar(0, 255, 0), 2);

    cv::putText(canvas_bgr,
                "cap: " + cv::format("%.1f", times.capture_ms) +
                " | pre: " + cv::format("%.1f", times.preprocess_ms) +
                " | infer: " + cv::format("%.1f", times.infer_ms),
                cv::Point(20, 86), cv::FONT_HERSHEY_SIMPLEX, 0.62, cv::Scalar(0, 255, 255), 2);

    cv::putText(canvas_bgr,
                "post: " + cv::format("%.1f", times.postprocess_ms) +
                " | draw: " + cv::format("%.1f", times.draw_ms) +
                " | imgsz: " + std::to_string(imgsz) +
                " | conf: " + cv::format("%.2f", conf_threshold),
                cv::Point(20, 116), cv::FONT_HERSHEY_SIMPLEX, 0.62, cv::Scalar(0, 255, 255), 2);
}
