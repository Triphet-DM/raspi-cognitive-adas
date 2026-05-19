#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "camera/Picamera2Camera.h"
#include "inference/YoloDetector.h"
#include "utils/Timer.h"
#include "utils/Types.h"
#include "vision/Draw.h"

static volatile std::sig_atomic_t g_running = 1;

void signal_handler(int) {
    g_running = 0;
}

namespace fs = std::filesystem;

struct RollingAverage {
    explicit RollingAverage(int max_count) : max_count_(std::max(1, max_count)) {}

    void add(double value) {
        values_.push_back(value);
        sum_ += value;

        if (static_cast<int>(values_.size()) > max_count_) {
            sum_ -= values_.front();
            values_.pop_front();
        }
    }

    double value() const {
        if (values_.empty()) return 0.0;
        return sum_ / static_cast<double>(values_.size());
    }

private:
    int max_count_ = 50;
    std::deque<double> values_;
    double sum_ = 0.0;
};

struct TimingAverages {
    explicit TimingAverages(int window)
        : fps(window),
          total(window),
          capture(window),
          preprocess(window),
          infer(window),
          postprocess(window),
          draw(window) {}

    void add(const StageTimes& times, double fps_value) {
        fps.add(fps_value);
        total.add(times.total_ms);
        capture.add(times.capture_ms);
        preprocess.add(times.preprocess_ms);
        infer.add(times.infer_ms);
        postprocess.add(times.postprocess_ms);
        draw.add(times.draw_ms);
    }

    RollingAverage fps;
    RollingAverage total;
    RollingAverage capture;
    RollingAverage preprocess;
    RollingAverage infer;
    RollingAverage postprocess;
    RollingAverage draw;
};

static std::string describe_detections(std::vector<Detection> detections, int top_k) {
    if (detections.empty()) {
        return "none";
    }

    std::sort(
        detections.begin(),
        detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        }
    );

    const int count = std::min(std::max(1, top_k), static_cast<int>(detections.size()));
    std::string text;

    for (int i = 0; i < count; ++i) {
        const Detection& det = detections[i];
        if (!text.empty()) text += " ; ";

        text += det.class_name;
        text += " ";
        text += cv::format("%.2f", det.confidence);
        text += " box:";
        text += std::to_string(det.box.width);
        text += "x";
        text += std::to_string(det.box.height);
    }

    return text;
}

static void save_debug_frame(
    const cv::Mat& frame_bgr,
    const std::vector<Detection>& detections,
    const AppConfig& cfg,
    int frame_index
) {
    const std::string bucket = detections.empty() ? "miss" : "hit";
    const fs::path output_dir = fs::path(cfg.save_dir) / bucket;
    fs::create_directories(output_dir);

    std::string label = "none";
    float confidence = 0.0f;

    if (!detections.empty()) {
        const auto best = std::max_element(
            detections.begin(),
            detections.end(),
            [](const Detection& a, const Detection& b) {
                return a.confidence < b.confidence;
            }
        );
        label = best->class_name;
        confidence = best->confidence;
    }

    const std::string filename =
        cv::format("%06d_%s_%.2f.jpg", frame_index, label.c_str(), confidence);
    const fs::path output_path = output_dir / filename;

    if (!cv::imwrite(output_path.string(), frame_bgr)) {
        std::cerr << "\nFailed to save frame: " << output_path.string() << "\n";
    }
}

static AppConfig parse_args(int argc, char** argv) {
    AppConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];

        auto value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Missing value for " + name);
            }
            return argv[++i];
        };

        if (key == "--model" || key == "--ncnn-param") cfg.model_path = value(key);
        else if (key == "--ncnn-bin") cfg.ncnn_bin_path = value(key);
        else if (key == "--input-name") cfg.ncnn_input_name = value(key);
        else if (key == "--output-name") cfg.ncnn_output_name = value(key);
        else if (key == "--imgsz") cfg.imgsz = std::stoi(value(key));
        else if (key == "--conf") cfg.conf_threshold = std::stof(value(key));
        else if (key == "--iou") cfg.iou_threshold = std::stof(value(key));
        else if (key == "--threads") cfg.threads = std::stoi(value(key));
        else if (key == "--camera-width") cfg.camera_width = std::stoi(value(key));
        else if (key == "--camera-height") cfg.camera_height = std::stoi(value(key));
        else if (key == "--camera-fps") cfg.camera_fps = std::stoi(value(key));
        else if (key == "--avg-window") cfg.avg_window = std::stoi(value(key));
        else if (key == "--top-k") cfg.terminal_top_k = std::stoi(value(key));
        else if (key == "--save-dir") cfg.save_dir = value(key);
        else if (key == "--save-every") cfg.save_every = std::stoi(value(key));
        else if (key == "--hide-window") cfg.hide_window = true;
        else if (key == "--no-draw") {
            cfg.no_draw = true;
            cfg.hide_window = true;
        } else if (key == "--save-frames") {
            cfg.save_frames = true;
        } else if (key == "--vulkan") {
            cfg.use_vulkan = true;
        } else if (key == "--no-packing") {
            cfg.use_packing = false;
        } else if (key == "--help" || key == "-h") {
            std::cout
                << "Usage:\n"
                << "  ./app --ncnn-param ../src/models/model.ncnn.param "
                << "--ncnn-bin ../src/models/model.ncnn.bin --imgsz 512 --threads 3 "
                << "--camera-width 960 --camera-height 560 --conf 0.25 --no-draw\n\n"
                << "Options:\n"
                << "  --input-name NAME   NCNN input blob name, default in0\n"
                << "  --output-name NAME  NCNN output blob name, default out0\n"
                << "  --vulkan            Enable NCNN Vulkan compute, default off\n"
                << "  --no-packing        Disable NCNN packing layout for debugging\n"
                << "  --hide-window       Do not show OpenCV preview window\n"
                << "  --no-draw           Skip cvtColor and overlay drawing for timing tests\n"
                << "  --avg-window N      Rolling average window for terminal output, default 50\n"
                << "  --top-k N           Show top N detections in terminal, default 3\n"
                << "  --save-frames       Save raw camera frames for dataset/debug review\n"
                << "  --save-dir DIR      Save frame directory, default pi_debug_frames\n"
                << "  --save-every N      Save every N frames, default 30\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + key);
        }
    }

    if (cfg.avg_window < 1) {
        throw std::runtime_error("--avg-window must be >= 1");
    }
    if (cfg.terminal_top_k < 1) {
        throw std::runtime_error("--top-k must be >= 1");
    }
    if (cfg.save_every < 1) {
        throw std::runtime_error("--save-every must be >= 1");
    }

    return cfg;
}

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, signal_handler);

        AppConfig cfg = parse_args(argc, argv);

        std::cout << "===== Phase 1.5 NCNN Color Fix =====\n";
        std::cout << "Pipeline     : Camera -> YOLO NCNN -> Draw/NoDraw\n";
        std::cout << "Param        : " << cfg.model_path << "\n";
        std::cout << "Bin          : " << cfg.ncnn_bin_path << "\n";
        std::cout << "Input blob   : " << cfg.ncnn_input_name << "\n";
        std::cout << "Output blob  : " << cfg.ncnn_output_name << "\n";
        std::cout << "Image size   : " << cfg.imgsz << "\n";
        std::cout << "Confidence   : " << cfg.conf_threshold << "\n";
        std::cout << "IoU          : " << cfg.iou_threshold << "\n";
        std::cout << "Threads      : " << cfg.threads << "\n";
        std::cout << "Vulkan       : " << (cfg.use_vulkan ? "on" : "off") << "\n";
        std::cout << "Packing      : " << (cfg.use_packing ? "on" : "off") << "\n";
        std::cout << "Camera       : " << cfg.camera_width << "x" << cfg.camera_height
                  << " @" << cfg.camera_fps << " fps\n";
        std::cout << "Window       : " << (cfg.hide_window ? "hidden" : "shown") << "\n";
        std::cout << "Draw         : " << (cfg.no_draw ? "off" : "on") << "\n";
        std::cout << "Avg window   : " << cfg.avg_window << " frames\n";
        std::cout << "Save frames  : " << (cfg.save_frames ? "on" : "off") << "\n";
        if (cfg.save_frames) {
            std::cout << "Save dir     : " << cfg.save_dir << "\n";
            std::cout << "Save every   : " << cfg.save_every << " frames\n";
        }
        std::cout << "Stop         : Ctrl+C";
        if (!cfg.hide_window) std::cout << " or q/Esc";
        std::cout << "\n\n";

        YoloDetector detector(
            cfg.model_path,
            cfg.ncnn_bin_path,
            cfg.imgsz,
            cfg.conf_threshold,
            cfg.iou_threshold,
            cfg.threads,
            cfg.ncnn_input_name,
            cfg.ncnn_output_name,
            cfg.use_vulkan,
            cfg.use_packing
        );

        Picamera2Camera camera(cfg.camera_width, cfg.camera_height, cfg.camera_fps);

        if (!cfg.hide_window) {
        cv::namedWindow("Phase 1.5 - NCNN", cv::WINDOW_NORMAL);
        }

        TimingAverages avg(cfg.avg_window);

        cv::Mat frame_bgr;
        cv::Mat canvas_bgr;
        StageTimes times;
        int frame_index = 0;

        while (g_running) {
            ++frame_index;

            Timer total_timer;
            total_timer.tic();

            Timer capture_timer;
            capture_timer.tic();
            if (!camera.read(frame_bgr)) {
                std::cerr << "Failed to read frame\n";
                continue;
            }
            times.capture_ms = capture_timer.toc_ms();

            std::vector<Detection> detections = detector.detect(
                frame_bgr,
                &times.preprocess_ms,
                &times.infer_ms,
                &times.postprocess_ms
            );

            if (cfg.save_frames && frame_index % cfg.save_every == 0) {
                save_debug_frame(frame_bgr, detections, cfg, frame_index);
            }

            times.draw_ms = 0.0;
            if (!cfg.no_draw) {
                Timer draw_timer;
                draw_timer.tic();

                canvas_bgr = frame_bgr.clone();
                draw_phase1_overlay(
                    canvas_bgr,
                    detections,
                    times,
                    avg.fps.value(),
                    cfg.imgsz,
                    cfg.conf_threshold
                );

                times.draw_ms = draw_timer.toc_ms();
            }

            times.total_ms = total_timer.toc_ms();

            const double fps = times.total_ms > 0.0 ? 1000.0 / times.total_ms : 0.0;
            avg.add(times, fps);

            if (!cfg.hide_window && !cfg.no_draw) {
                cv::imshow("Phase 1.5 - NCNN", canvas_bgr);
                const int key = cv::waitKey(1) & 0xFF;
                if (key == 27 || key == 'q') {
                    break;
                }
            } else {
                std::cout
                    << "\rFPS(avg): " << cv::format("%.2f", avg.fps.value())
                    << " | total: " << cv::format("%.1f", avg.total.value())
                    << " ms | cap: " << cv::format("%.1f", avg.capture.value())
                    << " | pre: " << cv::format("%.1f", avg.preprocess.value())
                    << " | infer: " << cv::format("%.1f", avg.infer.value())
                    << " | post: " << cv::format("%.1f", avg.postprocess.value())
                    << " | draw: " << cv::format("%.1f", avg.draw.value())
                    << " | det: " << detections.size()
                    << " | top: " << describe_detections(detections, cfg.terminal_top_k)
                    << "          "
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
