// src/vision/Letterbox.cpp
#include "vision/Letterbox.h"

#include <algorithm>
#include <cmath>

cv::Mat letterbox_image(const cv::Mat& image, int input_size, LetterboxInfo& info) {
    const int original_width = image.cols;
    const int original_height = image.rows;

    const float scale = std::min(
        static_cast<float>(input_size) / static_cast<float>(original_width),
        static_cast<float>(input_size) / static_cast<float>(original_height)
    );

    const int resized_width = static_cast<int>(std::round(original_width * scale));
    const int resized_height = static_cast<int>(std::round(original_height * scale));

    const int pad_x = (input_size - resized_width) / 2;
    const int pad_y = (input_size - resized_height) / 2;

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resized_width, resized_height));

    cv::Mat output(input_size, input_size, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(output(cv::Rect(pad_x, pad_y, resized_width, resized_height)));

    info.scale = scale;
    info.pad_x = pad_x;
    info.pad_y = pad_y;
    info.input_size = input_size;

    return output;
}

cv::Rect scale_box_back(
    float x1,
    float y1,
    float x2,
    float y2,
    const LetterboxInfo& info,
    int original_width,
    int original_height
) {
    x1 = (x1 - info.pad_x) / info.scale;
    y1 = (y1 - info.pad_y) / info.scale;
    x2 = (x2 - info.pad_x) / info.scale;
    y2 = (y2 - info.pad_y) / info.scale;

    x1 = std::clamp(x1, 0.0f, static_cast<float>(original_width - 1));
    y1 = std::clamp(y1, 0.0f, static_cast<float>(original_height - 1));
    x2 = std::clamp(x2, 0.0f, static_cast<float>(original_width - 1));
    y2 = std::clamp(y2, 0.0f, static_cast<float>(original_height - 1));

    const int left = static_cast<int>(std::round(x1));
    const int top = static_cast<int>(std::round(y1));
    const int right = static_cast<int>(std::round(x2));
    const int bottom = static_cast<int>(std::round(y2));

    return cv::Rect(
        left,
        top,
        std::max(1, right - left),
        std::max(1, bottom - top)
    );
}
