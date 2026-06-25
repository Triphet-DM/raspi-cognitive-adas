#include "vision/Letterbox.h"

#include <algorithm>
#include <cmath>

cv::Mat letterbox_rgb(const cv::Mat& src_rgb, int input_size, LetterboxInfo& info) {
    const int src_w = src_rgb.cols;
    const int src_h = src_rgb.rows;

    const float scale = std::min(
        static_cast<float>(input_size) / static_cast<float>(src_w),
        static_cast<float>(input_size) / static_cast<float>(src_h)
    );

    const int new_w = static_cast<int>(std::round(src_w * scale));
    const int new_h = static_cast<int>(std::round(src_h * scale));

    const int pad_x = (input_size - new_w) / 2;
    const int pad_y = (input_size - new_h) / 2;

    cv::Mat resized;
    cv::resize(src_rgb, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

    cv::Mat output(input_size, input_size, CV_8UC3, cv::Scalar(114, 114, 114));
    resized.copyTo(output(cv::Rect(pad_x, pad_y, new_w, new_h)));

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

    return cv::Rect(left, top, std::max(1, right - left), std::max(1, bottom - top));
}
