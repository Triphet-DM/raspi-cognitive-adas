#include "inference/YoloDetector.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "vision/Letterbox.h"
#include "vision/Nms.h"

YoloDetector::YoloDetector(
    const std::string& param_path,
    const std::string& bin_path,
    int imgsz,
    float conf_threshold,
    float iou_threshold,
    int threads,
    const std::string& input_name,
    const std::string& output_name,
    bool use_vulkan,
    bool use_packing
) : input_name_(input_name),
    output_name_(output_name),
    imgsz_(imgsz),
    conf_threshold_(conf_threshold),
    iou_threshold_(iou_threshold) {
    model_ = std::make_unique<NcnnModel>(param_path, bin_path, threads, use_vulkan, use_packing);
}

ncnn::Mat YoloDetector::preprocess_rgb_to_ncnn(const cv::Mat& input_rgb) {
    ncnn::Mat input = ncnn::Mat::from_pixels(
        input_rgb.data,
        ncnn::Mat::PIXEL_RGB,
        input_rgb.cols,
        input_rgb.rows
    );

    const float norm_vals[3] = {
        1.0f / 255.0f,
        1.0f / 255.0f,
        1.0f / 255.0f
    };
    input.substract_mean_normalize(nullptr, norm_vals);

    return input;
}

std::vector<Detection> YoloDetector::detect(
    const cv::Mat& frame_rgb,
    double* preprocess_ms,
    double* infer_ms,
    double* postprocess_ms
) {
    auto t0 = std::chrono::high_resolution_clock::now();

    letterboxed_rgb_ = letterbox_rgb(frame_rgb, imgsz_, last_letterbox_);
    ncnn::Mat input = preprocess_rgb_to_ncnn(letterboxed_rgb_);

    auto t1 = std::chrono::high_resolution_clock::now();

    ncnn::Mat output = model_->run(input, input_name_, output_name_);

    auto t2 = std::chrono::high_resolution_clock::now();

    std::vector<int64_t> output_shape;
    if (output.dims == 3) {
        output_shape = {output.c, output.h, output.w};
    } else if (output.dims == 2) {
        output_shape = {output.h, output.w};
    } else if (output.dims == 1) {
        output_shape = {output.w};
    } else {
        throw std::runtime_error("Unsupported NCNN output dims");
    }

    if (!output_shape_printed_) {
        std::cout << "NCNN output    : dims=" << output.dims
                  << " c=" << output.c
                  << " h=" << output.h
                  << " w=" << output.w
                  << " elempack=" << output.elempack
                  << "\n";
        output_shape_printed_ = true;
    }

    std::vector<Detection> detections = postprocess(
        static_cast<const float*>(output.data),
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

    if (output_shape.size() == 2) {
        const int64_t dim1 = output_shape[0];
        const int64_t dim2 = output_shape[1];

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

    throw std::runtime_error("Unsupported YOLO output shape");
}
