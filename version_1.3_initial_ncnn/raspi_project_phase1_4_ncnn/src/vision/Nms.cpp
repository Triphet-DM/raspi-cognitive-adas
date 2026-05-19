#include "vision/Nms.h"

#include <algorithm>

static float rect_iou(const cv::Rect& a, const cv::Rect& b) {
    const int inter = (a & b).area();
    const int uni = a.area() + b.area() - inter;
    if (uni <= 0) return 0.0f;
    return static_cast<float>(inter) / static_cast<float>(uni);
}

std::vector<Detection> nms_detections(std::vector<Detection> detections, float iou_threshold) {
    std::sort(detections.begin(), detections.end(), [](const Detection& a, const Detection& b) {
        return a.confidence > b.confidence;
    });

    std::vector<Detection> keep;
    std::vector<bool> removed(detections.size(), false);

    for (size_t i = 0; i < detections.size(); ++i) {
        if (removed[i]) continue;

        keep.push_back(detections[i]);

        for (size_t j = i + 1; j < detections.size(); ++j) {
            if (removed[j]) continue;
            if (detections[i].class_id != detections[j].class_id) continue;
            if (rect_iou(detections[i].box, detections[j].box) > iou_threshold) {
                removed[j] = true;
            }
        }
    }

    return keep;
}
