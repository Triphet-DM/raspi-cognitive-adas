// src/inference/YoloDetector.cpp
#include "inference/YoloDetector.h"

#include <chrono>
#include <cstring>
#include <stdexcept>

#include "vision/Letterbox.h"
#include "vision/Nms.h"

YoloDetector::YoloDetector(
    const std::string& model_path,
    int imgsz,
    float conf_threshold,
    float iou_threshold,
    int threads
) : imgsz_(imgsz),
    conf_threshold_(conf_threshold),
    iou_threshold_(iou_threshold) {
    model_ = std::make_unique<OnnxModel>(model_path, threads);
}

std::vector<float> YoloDetector::preprocess(const cv::Mat& image) {
    input_image_ = letterbox_image(image, imgsz_, last_letterbox_);

    cv::Mat rgb;
    cv::cvtColor(input_image_, rgb, cv::COLOR_BGR2RGB);
    rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

    std::vector<cv::Mat> channels(3);
    cv::split(rgb, channels);

    const int channel_size = imgsz_ * imgsz_;
    std::vector<float> blob(3 * channel_size);

    for (int c = 0; c < 3; ++c) {
        std::memcpy(
            blob.data() + c * channel_size,
            channels[c].data,
            channel_size * sizeof(float)
        );
    }

    return blob;
}

std::vector<Detection> YoloDetector::detect(const cv::Mat& frame, double* infer_ms) {
    std::vector<float> input = preprocess(frame);

    std::vector<int64_t> input_shape = {
        1,
        3,
        imgsz_,
        imgsz_
    };

    const auto t1 = std::chrono::high_resolution_clock::now();

    std::vector<Ort::Value> outputs = model_->run(input, input_shape);

    const auto t2 = std::chrono::high_resolution_clock::now();

    if (infer_ms) {
        *infer_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    }

    float* output_data = outputs[0].GetTensorMutableData<float>();

    std::vector<int64_t> output_shape = outputs[0]
        .GetTensorTypeAndShapeInfo()
        .GetShape();

    std::vector<Detection> detections = postprocess(
        output_data,
        output_shape,
        last_letterbox_,
        frame.cols,
        frame.rows
    );

    return nms_detections(detections, iou_threshold_);
}

std::vector<Detection> YoloDetector::postprocess(
    float* output_data,
    const std::vector<int64_t>& output_shape,
    const LetterboxInfo& letterbox_info,
    int original_width,
    int original_height
) {
    std::vector<Detection> detections;

    if (output_shape.size() == 3) {
        const int64_t dim1 = output_shape[1];
        const int64_t dim2 = output_shape[2];

        if (dim2 == 6 && dim1 > 6) {
            const int64_t num_boxes = dim1;

            for (int64_t i = 0; i < num_boxes; ++i) {
                const float* row = output_data + i * 6;

                const float x1 = row[0];
                const float y1 = row[1];
                const float x2 = row[2];
                const float y2 = row[3];
                const float score = row[4];
                const int class_id = static_cast<int>(row[5]);

                if (score < conf_threshold_) {
                    continue;
                }

                Detection det;
                det.box = scale_box_back(
                    x1,
                    y1,
                    x2,
                    y2,
                    letterbox_info,
                    original_width,
                    original_height
                );

                det.class_id = class_id;
                det.confidence = score;
                det.class_name = class_id >= 0 && class_id < static_cast<int>(class_names_.size())
                    ? class_names_[class_id]
                    : std::to_string(class_id);

                detections.push_back(det);
            }

            return detections;
        }

        const bool channel_first = dim1 < dim2;
        const int64_t num_attrs = channel_first ? dim1 : dim2;
        const int64_t num_boxes = channel_first ? dim2 : dim1;

        if (num_attrs < 6) {
            throw std::runtime_error("Unsupported YOLO output: num_attrs < 6");
        }

        const int num_classes = static_cast<int>(num_attrs - 4);

        for (int64_t i = 0; i < num_boxes; ++i) {
            auto at = [&](int64_t attr) -> float {
                if (channel_first) {
                    return output_data[attr * num_boxes + i];
                }

                return output_data[i * num_attrs + attr];
            };

            const float cx = at(0);
            const float cy = at(1);
            const float w = at(2);
            const float h = at(3);

            int best_class = -1;
            float best_score = 0.0f;

            for (int c = 0; c < num_classes; ++c) {
                const float score = at(4 + c);

                if (score > best_score) {
                    best_score = score;
                    best_class = c;
                }
            }

            if (best_score < conf_threshold_) {
                continue;
            }

            const float x1 = cx - w * 0.5f;
            const float y1 = cy - h * 0.5f;
            const float x2 = cx + w * 0.5f;
            const float y2 = cy + h * 0.5f;

            Detection det;
            det.box = scale_box_back(
                x1,
                y1,
                x2,
                y2,
                letterbox_info,
                original_width,
                original_height
            );

            det.class_id = best_class;
            det.confidence = best_score;
            det.class_name = best_class >= 0 && best_class < static_cast<int>(class_names_.size())
                ? class_names_[best_class]
                : std::to_string(best_class);

            detections.push_back(det);
        }

        return detections;
    }

    if (output_shape.size() == 2 && output_shape[1] == 6) {
        const int64_t num_boxes = output_shape[0];

        for (int64_t i = 0; i < num_boxes; ++i) {
            const float* row = output_data + i * 6;

            const float x1 = row[0];
            const float y1 = row[1];
            const float x2 = row[2];
            const float y2 = row[3];
            const float score = row[4];
            const int class_id = static_cast<int>(row[5]);

            if (score < conf_threshold_) {
                continue;
            }

            Detection det;
            det.box = scale_box_back(
                x1,
                y1,
                x2,
                y2,
                letterbox_info,
                original_width,
                original_height
            );

            det.class_id = class_id;
            det.confidence = score;
            det.class_name = class_id >= 0 && class_id < static_cast<int>(class_names_.size())
                ? class_names_[class_id]
                : std::to_string(class_id);

            detections.push_back(det);
        }

        return detections;
    }

    throw std::runtime_error("Unsupported YOLO output shape");
}
