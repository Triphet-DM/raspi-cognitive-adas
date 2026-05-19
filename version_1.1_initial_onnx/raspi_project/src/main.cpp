// src/main.cpp
#include <iostream>
#include <stdexcept>
#include <string>

#include <opencv2/opencv.hpp>
#include <Python.h>
#include "camera/Picamera2Camera.h"
#include "inference/YoloDetector.h"
#include "utils/Timer.h"
#include "utils/Types.h"
#include "vision/Draw.h"

static AppConfig parse_args(int argc, char** argv) {
    AppConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        auto value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }

            return argv[++i];
        };

        if (key == "--model") {
            config.model_path = value(key);
        } else if (key == "--imgsz") {
            config.imgsz = std::stoi(value(key));
        } else if (key == "--conf") {
            config.conf_threshold = std::stof(value(key));
        } else if (key == "--iou") {
            config.iou_threshold = std::stof(value(key));
        } else if (key == "--threads") {
            config.onnx_threads = std::stoi(value(key));
        } else if (key == "--camera-width") {
            config.camera_width = std::stoi(value(key));
        } else if (key == "--camera-height") {
            config.camera_height = std::stoi(value(key));
        } else if (key == "--hide-window") {
            config.hide_window = true;
        } else if (key == "--help" || key == "-h") {
            std::cout
                << "Phase 1: Picamera2 -> YOLO ONNX -> Draw\n\n"
                << "Usage:\n"
                << "  ./app --model ../models/best.onnx --imgsz 640 --conf 0.25 --threads 4\n\n"
                << "Options:\n"
                << "  --model PATH\n"
                << "  --imgsz INT\n"
                << "  --conf FLOAT\n"
                << "  --iou FLOAT\n"
                << "  --threads INT\n"
                << "  --camera-width INT\n"
                << "  --camera-height INT\n"
                << "  --hide-window\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + key);
        }
    }

    return config;
}

int main(int argc, char** argv) {
    try {
        AppConfig config = parse_args(argc, argv);

        std::cout << "===== Phase 1 =====\n";
        std::cout << "Pipeline     : Picamera2 -> YOLO ONNX -> Draw\n";
        std::cout << "Model        : " << config.model_path << "\n";
        std::cout << "Image size   : " << config.imgsz << "\n";
        std::cout << "Confidence   : " << config.conf_threshold << "\n";
        std::cout << "IoU          : " << config.iou_threshold << "\n";
        std::cout << "Threads      : " << config.onnx_threads << "\n";
        std::cout << "Camera       : " << config.camera_width << "x" << config.camera_height << "\n";

        Picamera2Camera camera(
            config.camera_width,
            config.camera_height,
            30
        );

        YoloDetector detector(
            config.model_path,
            config.imgsz,
            config.conf_threshold,
            config.iou_threshold,
            config.onnx_threads
        );

        if (!config.hide_window) {
            cv::namedWindow("Phase 1 - YOLO ONNX", cv::WINDOW_NORMAL);
        }

        double fps_avg = 0.0;
        const double fps_alpha = 0.08;

        while (true) {
            if (PyErr_CheckSignals() != 0) {
                std::cout << "Interrupted by Ctrl+C\n";
                break;
            }

            Timer total_timer;
            total_timer.tic();

            cv::Mat frame;

            if (!camera.read(frame)) {
                std::cerr << "Failed to read frame from Picamera2\n";
                continue;
            }

            double infer_ms = 0.0;

            std::vector<Detection> detections = detector.detect(
                frame,
                &infer_ms
            );

            const double total_ms = total_timer.toc_ms();
            const double fps = total_ms > 0.0 ? 1000.0 / total_ms : 0.0;

            if (fps_avg <= 0.0) {
                fps_avg = fps;
            } else {
                fps_avg = (1.0 - fps_alpha) * fps_avg + fps_alpha * fps;
            }

            draw_phase1_overlay(
                frame,
                detections,
                fps_avg,
                infer_ms,
                config.imgsz,
                config.conf_threshold
            );

            if (!config.hide_window) {
                cv::imshow("Phase 1 - YOLO ONNX", frame);

                const int key = cv::waitKey(1) & 0xFF;

                if (key == 27 || key == 'q') {
                    break;
                }
            } else {
                std::cout
                    << "\rFPS: " << cv::format("%.2f", fps_avg)
                    << " | infer: " << cv::format("%.1f", infer_ms)
                    << " ms | det: " << detections.size()
                    << std::flush;
            }
        }

        std::cout << "\nStopped.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}
