#pragma once

#include <vector>

#include "utils/Types.h"

std::vector<Detection> nms_detections(std::vector<Detection> detections, float iou_threshold);
