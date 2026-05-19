// src/utils/Timer.h
#pragma once

#include <chrono>

class Timer {
public:
    void tic() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    double toc_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};
