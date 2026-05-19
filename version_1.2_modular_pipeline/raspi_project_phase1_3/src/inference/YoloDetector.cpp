#include "inference/YoloDetector.h"

#include <chrono>
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
    input_buffer_.resize(static_cast<size_t>(3) * imgsz_ * imgsz_);
}

void YoloDetector::preprocess_rgb_to_nchw(const cv::Mat& input_rgb) {
    const int h = input_rgb.rows;
    const int w = input_rgb.cols;
    const int hw = h * w;

    const unsigned char* data = input_rgb.ptr<unsigned char>(0);

    float* dst_r = input_buffer_.data();
    float* dst_g = input_buffer_.data() + hw;
    float* dst_b = input_buffer_.data() + 2 * hw;

    for (int i = 0; i < hw; ++i) {
        const int idx = i * 3;
        dst_r[i] = data[idx + 0] * (1.0f / 255.0f);
        dst_g[i] = data[idx + 1] * (1.0f / 255.0f);
        dst_b[i] = data[idx + 2] * (1.0f / 255.0f);
    }
}

std::vector<Detection> YoloDetector::detect(
    const cv::Mat& frame_rgb,
    double* preprocess_ms,
    double* infer_ms,
    double* postprocess_ms
) {
    auto t0 = std::chrono::high_resolution_clock::now();

    letterboxed_rgb_ = letterbox_rgb(frame_rgb, imgsz_, last_letterbox_);
    preprocess_rgb_to_nchw(letterboxed_rgb_);

    auto t1 = std::chrono::high_resolution_clock::now();

    std::vector<int64_t> input_shape = {1, 3, imgsz_, imgsz_};
    std::vector<Ort::Value> outputs = model_->run(
        input_buffer_.data(),
        input_buffer_.size(),
        input_shape
    );

    auto t2 = std::chrono::high_resolution_clock::now();

    const float* output_data = outputs[0].GetTensorData<float>();
    const std::vector<int64_t> output_shape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();

    std::vector<Detection> detections = postprocess(
        output_data,
        output_shape,
        frame_rgb.cols,
        frame_rgb.rows
    );

    detections = nms_detections(std::move(detections), iou_threshold_);

    auto t3 = std::chrono::high_resolution_clock::now();

    if (preprocess_ms) {
        *preprocess_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    }
    if (infer_ms) {
        *infer_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    }
    if (postprocess_ms) {
        *postprocess_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    }

    return detections;
}

std::vector<Detection> YoloDetector::postprocess(
    const float* output_data,
    const std::vector<int64_t>& output_shape,
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

                if (score < conf_threshold_) continue;

                Detection det;
                det.box = scale_box_back(x1, y1, x2, y2, last_letterbox_, original_width, original_height);
                det.class_id = class_id;
                det.class_name = (class_id >= 0 && class_id < static_cast<int>(class_names_.size()))
                    ? class_names_[class_id]
                    : std::to_string(class_id);
                det.confidence = score;
                detections.push_back(det);
            }

            return detections;
        }

        const bool channel_first = dim1 < dim2;
        const int64_t num_attrs = channel_first ? dim1 : dim2;
        const int64_t num_boxes = channel_first ? dim2 : dim1;

        if (num_attrs < 6) {
            throw std::runtime_error("Unsupported YOLO output shape");
        }

        const int num_classes = static_cast<int>(num_attrs - 4);

        for (int64_t i = 0; i < num_boxes; ++i) {
            auto at = [&](int64_t attr) -> float {
                if (channel_first) return output_data[attr * num_boxes + i];
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

            if (best_score < conf_threshold_) continue;

            const float x1 = cx - w * 0.5f;
            const float y1 = cy - h * 0.5f;
            const float x2 = cx + w * 0.5f;
            const float y2 = cy + h * 0.5f;

            Detection det;
            det.box = scale_box_back(x1, y1, x2, y2, last_letterbox_, original_width, original_height);
            det.class_id = best_class;
            det.class_name = (best_class >= 0 && best_class < static_cast<int>(class_names_.size()))
                ? class_names_[best_class]
                : std::to_string(best_class);
            det.confidence = best_score;
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

            if (score < conf_threshold_) continue;

            Detection det;
            det.box = scale_box_back(x1, y1, x2, y2, last_letterbox_, original_width, original_height);
            det.class_id = class_id;
            det.class_name = (class_id >= 0 && class_id < static_cast<int>(class_names_.size()))
                ? class_names_[class_id]
                : std::to_string(class_id);
            det.confidence = score;
            detections.push_back(det);
        }

        return detections;
    }

    throw std::runtime_error("Unsupported YOLO output shape");
}
