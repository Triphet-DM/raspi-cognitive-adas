#pragma once

#include <opencv2/opencv.hpp>
#include "camera/Camera.h"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

class Picamera2Camera : public Camera {
public:
    Picamera2Camera(int width, int height, int fps = 30);
    ~Picamera2Camera() override;

    bool read(cv::Mat& frame_bgr) override;

private:
    PyObject*      camera_ = nullptr;
    PyThreadState* save_   = nullptr;
};
