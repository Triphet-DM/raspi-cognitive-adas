// src/vision/Draw.cpp
#include "vision/Draw.h"

#include <string>

static cv::Scalar color_for_class(int class_id) {
    static const std::vector<cv::Scalar> colors = {
        cv::Scalar(0, 255, 255),
        cv::Scalar(0, 255, 0),
        cv::Scalar(255, 0, 0),
        cv::Scalar(255, 0, 255),
        cv::Scalar(255, 128, 0),
        cv::Scalar(0, 128, 255)
    };

    return colors[class_id % colors.size()];
}

static void draw_label(
    cv::Mat& frame,
    const std::string& text,
    int x,
    int y,
    const cv::Scalar& color
) {
    int base = 0;
    const double scale = 0.55;
    const int thickness = 2;

    cv::Size text_size = cv::getTextSize(
        text,
        cv::FONT_HERSHEY_SIMPLEX,
        scale,
        thickness,
        &base
    );

    y = std::max(y, text_size.height + 8);

    cv::rectangle(
        frame,
        cv::Rect(
            x,
            y - text_size.height - 8,
            text_size.width + 10,
            text_size.height + base + 8
        ),
        color,
        -1
    );

    cv::putText(
        frame,
        text,
        cv::Point(x + 5, y - 5),
        cv::FONT_HERSHEY_SIMPLEX,
        scale,
        cv::Scalar(0, 0, 0),
        thickness
    );
}

void draw_phase1_overlay(
    cv::Mat& frame,
    const std::vector<Detection>& detections,
    double fps,
    double infer_ms,
    int imgsz,
    float conf_threshold
) {
    for (const Detection& det : detections) {
        cv::Scalar color = color_for_class(det.class_id);

        cv::rectangle(frame, det.box, color, 2);

        const std::string label = det.class_name + " " + cv::format("%.2f", det.confidence);

        draw_label(
            frame,
            label,
            det.box.x,
            std::max(20, det.box.y),
            color
        );
    }

    cv::rectangle(
        frame,
        cv::Rect(0, 0, frame.cols, 92),
        cv::Scalar(20, 20, 20),
        -1
    );

    cv::putText(
        frame,
        "Phase 1: Picamera2 -> YOLO ONNX -> Draw",
        cv::Point(20, 28),
        cv::FONT_HERSHEY_SIMPLEX,
        0.65,
        cv::Scalar(0, 255, 0),
        2
    );

    cv::putText(
        frame,
        "FPS: " + cv::format("%.2f", fps) +
            " | infer: " + cv::format("%.1f", infer_ms) +
            " ms | det: " + std::to_string(detections.size()),
        cv::Point(20, 58),
        cv::FONT_HERSHEY_SIMPLEX,
        0.65,
        cv::Scalar(0, 255, 0),
        2
    );

    cv::putText(
        frame,
        "imgsz: " + std::to_string(imgsz) +
            " | conf: " + cv::format("%.2f", conf_threshold),
        cv::Point(20, 86),
        cv::FONT_HERSHEY_SIMPLEX,
        0.65,
        cv::Scalar(0, 255, 0),
        2
    );
}
