#include "camera/Picamera2Camera.h"

#include <stdexcept>

Picamera2Camera::Picamera2Camera(int width, int height, int fps) {
    Py_Initialize();

    const char* py_code = R"PYCODE(
import time
import numpy as np
from picamera2 import Picamera2

class CppPicamera2:
    def __init__(self, width, height, fps):
        self.picam2 = Picamera2()
        config = self.picam2.create_video_configuration(
            main={"size": (int(width), int(height)), "format": "RGB888"},
            controls={"FrameRate": int(fps)}
        )
        self.picam2.configure(config)
        self.picam2.start()
        time.sleep(1.0)

    def read(self):
        frame = self.picam2.capture_array("main")
        if frame.ndim == 3 and frame.shape[2] > 3:
            frame = frame[:, :, :3]
        frame = np.ascontiguousarray(frame)
        h, w = frame.shape[:2]
        return frame.tobytes(), int(w), int(h)

    def close(self):
        self.picam2.stop()
)PYCODE";

    if (PyRun_SimpleString(py_code) != 0) {
        PyErr_Print();
        throw std::runtime_error("Failed to initialize embedded Picamera2 Python code");
    }

    PyObject* main_module = PyImport_AddModule("__main__");
    PyObject* globals = PyModule_GetDict(main_module);
    PyObject* cls = PyDict_GetItemString(globals, "CppPicamera2");

    if (!cls) {
        throw std::runtime_error("Cannot find CppPicamera2 class");
    }

    PyObject* args = Py_BuildValue("(iii)", width, height, fps);
    camera_ = PyObject_CallObject(cls, args);
    Py_DECREF(args);

    if (!camera_) {
        PyErr_Print();
        throw std::runtime_error("Failed to create Picamera2 object");
    }
}

Picamera2Camera::~Picamera2Camera() {
    if (camera_) {
        PyObject* result = PyObject_CallMethod(camera_, "close", nullptr);
        if (result) {
            Py_DECREF(result);
        }
        Py_DECREF(camera_);
        camera_ = nullptr;
    }

    if (Py_IsInitialized()) {
        Py_Finalize();
    }
}

bool Picamera2Camera::read(cv::Mat& frame_bgr) {
    PyObject* result = PyObject_CallMethod(camera_, "read", nullptr);
    if (!result) {
        PyErr_Print();
        return false;
    }

    const char* buffer = nullptr;
    Py_ssize_t buffer_size = 0;
    int width = 0;
    int height = 0;

    if (!PyArg_ParseTuple(result, "y#ii", &buffer, &buffer_size, &width, &height)) {
        PyErr_Print();
        Py_DECREF(result);
        return false;
    }

    const Py_ssize_t expected_size = static_cast<Py_ssize_t>(width) * height * 3;
    if (buffer_size != expected_size) {
        Py_DECREF(result);
        return false;
    }

    cv::Mat rgb(height, width, CV_8UC3, const_cast<char*>(buffer));
    cv::cvtColor(rgb, frame_bgr, cv::COLOR_RGB2BGR);

    Py_DECREF(result);
    return true;
}
