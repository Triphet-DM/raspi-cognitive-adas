// src/camera/Camera.h
#pragma once

#include <opencv2/opencv.hpp>

class Camera {
public:
    virtual ~Camera() = default;
    virtual bool read(cv::Mat& frame) = 0;
};
