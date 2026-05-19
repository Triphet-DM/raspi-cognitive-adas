// src/camera/Picamera2Camera.h
#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "camera/Camera.h"

class Picamera2Camera : public Camera {
public:
    Picamera2Camera(int width, int height, int fps = 30);
    ~Picamera2Camera() override;

    bool read(cv::Mat& frame) override;

private:
    PyObject* camera_ = nullptr;
};
