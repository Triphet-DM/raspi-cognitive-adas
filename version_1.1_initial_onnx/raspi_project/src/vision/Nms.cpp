// src/vision/Nms.cpp
#include "vision/Nms.h"

#include <algorithm>

static float iou_rect(const cv::Rect& a, const cv::Rect& b) {
    const int intersection = (a & b).area();
    const int union_area = a.area() + b.area() - intersection;

    if (union_area <= 0) {
        return 0.0f;
    }

    return static_cast<float>(intersection) / static_cast<float>(union_area);
}

std::vector<Detection> nms_detections(
    std::vector<Detection> detections,
    float iou_threshold
) {
    std::sort(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        }
    );

    std::vector<Detection> result;
    std::vector<bool> removed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (removed[i]) {
            continue;
        }

        result.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (removed[j]) {
                continue;
            }

            if (detections[i].class_id != detections[j].class_id) {
                continue;
            }

            if (iou_rect(detections[i].box, detections[j].box) > iou_threshold) {
                removed[j] = true;
            }
        }
    }

    return result;
}
