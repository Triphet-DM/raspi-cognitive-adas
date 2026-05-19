#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <opencv2/opencv.hpp>

#include "camera/Picamera2Camera.h"
#include "inference/YoloDetector.h"
#include "utils/Timer.h"
#include "utils/Types.h"
#include "vision/Draw.h"

static volatile std::sig_atomic_t g_running = 1;
void signal_handler(int) { g_running = 0; }

namespace fs = std::filesystem;
using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

// ============================================================
// CooldownManager
//
// เก็บ cooldown ต่อ class — แก้ตัวเลขใน class_cooldowns ได้เลย
// class ที่ไม่อยู่ใน table จะใช้ default_cooldown_sec
// ============================================================
struct CooldownManager {

    // ====== ตารางแก้ cooldown ต่อ class (หน่วย: วินาที) ======
    std::unordered_map<std::string, float> class_cooldowns = {
        {"Pedestrian_Warning_Sign",  3.0f},
        {"Pedestrian_crossing",      3.0f},
        {"School_Zone",              3.0f},
        {"Traffic_sign",             3.0f},
        {"curve_ahead",              3.0f},
        {"no_parking",               3.0f},
        {"no_passing",               3.0f},
        {"no_stop",                  3.0f},
        {"no_u_turn",                3.0f},
        {"sign_100",                 3.0f},
        {"sign_50",                  3.0f},
        {"sign_60",                  3.0f},
        {"sign_80",                  3.0f},
        {"sign_90",                  3.0f},
        {"sign_four_way",            3.0f},
    };
    // =========================================================

    // cooldown สำหรับ class ที่ไม่อยู่ใน table
    float default_cooldown_sec = 3.0f;

    // ตรวจสอบว่า class นี้อยู่ใน cooldown หรือเปล่า
    // คืน true = suppressed (ห้ามส่งเข้า voter)
    bool is_suppressed(const std::string& cls) const {
        auto it = cooldown_end_.find(cls);
        if (it == cooldown_end_.end()) return false;
        return Clock::now() < it->second;
    }

    // เวลา cooldown ที่เหลือ (วินาที), 0 ถ้าหมดแล้ว
    float remaining_sec(const std::string& cls) const {
        auto it = cooldown_end_.find(cls);
        if (it == cooldown_end_.end()) return 0.0f;
        auto now = Clock::now();
        if (now >= it->second) return 0.0f;
        return std::chrono::duration<float>(it->second - now).count();
    }

    // เปิด cooldown หลัง class ถูก confirm
    void activate(const std::string& cls) {
        float duration = default_cooldown_sec;
        auto it = class_cooldowns.find(cls);
        if (it != class_cooldowns.end()) {
            duration = it->second;
        }
        cooldown_end_[cls] =
    Clock::now() +
    std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<float>(duration)
    );
    }

private:
    std::unordered_map<std::string, TimePoint> cooldown_end_;
};

// ============================================================
// TemporalVoter (เหมือนเดิม)
// ============================================================
struct TemporalVoter {
    explicit TemporalVoter(int max_frames = 10, int early_confirm_votes = 4)
        : max_frames_(max_frames),
          early_confirm_votes_(early_confirm_votes) {}

    void update(const std::string& top_class) {
        history_.push_back(top_class);
        if (static_cast<int>(history_.size()) > max_frames_) {
            history_.pop_front();
        }
    }

    struct VoteResult {
        std::string winner;
        int winner_count = 0;
        int history_size = 0;
        bool confirmed   = false;
        std::map<std::string, int> votes;
    };

    VoteResult evaluate() const {
        VoteResult result;
        result.history_size = static_cast<int>(history_.size());

        for (const auto& cls : history_) {
            if (!cls.empty()) result.votes[cls]++;
        }

        if (result.votes.empty()) return result;

        std::string best_class;
        int best_count = 0;

        for (const auto& kv : result.votes) {
            if (kv.second > best_count) {
                best_count = kv.second;
                best_class = kv.first;
            } else if (kv.second == best_count) {
                // Tie → เลือก most recent
                for (auto it = history_.rbegin(); it != history_.rend(); ++it) {
                    if (*it == kv.first)  { best_class = kv.first; break; }
                    if (*it == best_class) { break; }
                }
            }
        }

        result.winner       = best_class;
        result.winner_count = best_count;

        // Early confirm
        if (best_count >= early_confirm_votes_) {
            result.confirmed = true;
            return result;
        }

        float ratio =
            static_cast<float>(best_count) /
            static_cast<float>(result.history_size);

        // Hard limit
        if (result.history_size >= max_frames_) {

            if (ratio >= 0.4f) {
                result.confirmed = true;
                return result;
            }

            result.winner = "";
            result.confirmed = false;
            return result;
        }
    return result;
    }

    void reset() { history_.clear(); }

    const std::deque<std::string>& history() const { return history_; }

private:
    int max_frames_;
    int early_confirm_votes_;
    std::deque<std::string> history_;
};

// ============================================================

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
    int max_count_;
    std::deque<double> values_;
    double sum_ = 0.0;
};

struct TimingAverages {
    explicit TimingAverages(int window)
        : fps(window), total(window), capture(window),
          preprocess(window), infer(window), postprocess(window), draw(window) {}

    void add(const StageTimes& times, double fps_value) {
        fps.add(fps_value);
        total.add(times.total_ms);
        capture.add(times.capture_ms);
        preprocess.add(times.preprocess_ms);
        infer.add(times.infer_ms);
        postprocess.add(times.postprocess_ms);
        draw.add(times.draw_ms);
    }

    RollingAverage fps, total, capture, preprocess, infer, postprocess, draw;
};

static std::string describe_detections(std::vector<Detection> detections, int top_k) {
    if (detections.empty()) return "none";
    std::sort(detections.begin(), detections.end(),
        [](const Detection& a, const Detection& b) {
            return a.confidence > b.confidence;
        });
    const int count = std::min(std::max(1, top_k), static_cast<int>(detections.size()));
    std::string text;
    for (int i = 0; i < count; ++i) {
        if (!text.empty()) text += " ; ";
        text += detections[i].class_name + " "
             + cv::format("%.2f", detections[i].confidence);
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
    const fs::path dir = fs::path(cfg.save_dir) / bucket;
    fs::create_directories(dir);

    std::string label = "none";
    float conf = 0.0f;
    if (!detections.empty()) {
        const auto best = std::max_element(detections.begin(), detections.end(),
            [](const Detection& a, const Detection& b) {
                return a.confidence < b.confidence;
            });
        label = best->class_name;
        conf  = best->confidence;
    }

    const std::string filename =
        cv::format("%06d_%s_%.2f.jpg", frame_index, label.c_str(), conf);
    cv::imwrite((dir / filename).string(), frame_bgr);
}

static AppConfig parse_args(int argc, char** argv) {
    AppConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        auto value = [&](const std::string& name) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + name);
            return argv[++i];
        };

        if      (key == "--model" || key == "--ncnn-param") cfg.model_path       = value(key);
        else if (key == "--ncnn-bin")        cfg.ncnn_bin_path    = value(key);
        else if (key == "--input-name")      cfg.ncnn_input_name  = value(key);
        else if (key == "--output-name")     cfg.ncnn_output_name = value(key);
        else if (key == "--imgsz")           cfg.imgsz            = std::stoi(value(key));
        else if (key == "--conf")            cfg.conf_threshold   = std::stof(value(key));
        else if (key == "--iou")             cfg.iou_threshold    = std::stof(value(key));
        else if (key == "--threads")         cfg.threads          = std::stoi(value(key));
        else if (key == "--camera-width")    cfg.camera_width     = std::stoi(value(key));
        else if (key == "--camera-height")   cfg.camera_height    = std::stoi(value(key));
        else if (key == "--camera-fps")      cfg.camera_fps       = std::stoi(value(key));
        else if (key == "--avg-window")      cfg.avg_window       = std::stoi(value(key));
        else if (key == "--top-k")           cfg.terminal_top_k   = std::stoi(value(key));
        else if (key == "--save-dir")        cfg.save_dir         = value(key);
        else if (key == "--save-every")      cfg.save_every       = std::stoi(value(key));
        else if (key == "--hide-window")     cfg.hide_window      = true;
        else if (key == "--no-draw")       { cfg.no_draw = true; cfg.hide_window = true; }
        else if (key == "--save-frames")     cfg.save_frames      = true;
        else if (key == "--vulkan")          cfg.use_vulkan       = true;
        else if (key == "--no-packing")      cfg.use_packing      = false;
        else if (key == "--help" || key == "-h") {
            std::cout << "Usage: ./app --ncnn-param <param> --ncnn-bin <bin> [options]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("Unknown argument: " + key);
        }
    }
    if (cfg.avg_window < 1)     throw std::runtime_error("--avg-window must be >= 1");
    if (cfg.terminal_top_k < 1) throw std::runtime_error("--top-k must be >= 1");
    if (cfg.save_every < 1)     throw std::runtime_error("--save-every must be >= 1");
    return cfg;
}

int main(int argc, char** argv) {
    try {
        std::signal(SIGINT, signal_handler);
        AppConfig cfg = parse_args(argc, argv);

        std::cout << "===== Traffic Sign Detection =====\n"
                  << "Pipeline: Camera -> YOLO -> Cooldown -> Voting -> Output\n"
                  << "Voting  : window=10, early_confirm=4\n\n";

        YoloDetector detector(
            cfg.model_path, cfg.ncnn_bin_path,
            cfg.imgsz, cfg.conf_threshold, cfg.iou_threshold,
            cfg.threads, cfg.ncnn_input_name, cfg.ncnn_output_name,
            cfg.use_vulkan, cfg.use_packing
        );

        Picamera2Camera camera(cfg.camera_width, cfg.camera_height, cfg.camera_fps);

        if (!cfg.hide_window) {
            cv::namedWindow("Detection", cv::WINDOW_NORMAL);
        }

        TimingAverages avg(cfg.avg_window);
        CooldownManager cooldown;
        TemporalVoter   voter(10, 4);

        cv::Mat frame_bgr, canvas_bgr;
        StageTimes times;
        int frame_index = 0;

        while (g_running) {
            ++frame_index;

            Timer total_timer;
            total_timer.tic();

            Timer cap_timer;
            cap_timer.tic();
            if (!camera.read(frame_bgr)) {
                std::cerr << "Failed to read frame\n";
                continue;
            }
            times.capture_ms = cap_timer.toc_ms();

            std::vector<Detection> detections = detector.detect(
                frame_bgr,
                &times.preprocess_ms,
                &times.infer_ms,
                &times.postprocess_ms
            );

            // ---- หา top class ของ frame นี้ ----
            std::string top_class = "";
            float top_conf = 0.0f;
            if (!detections.empty()) {
                const auto best = std::max_element(
                    detections.begin(), detections.end(),
                    [](const Detection& a, const Detection& b) {
                        return a.confidence < b.confidence;
                    });
                top_class = best->class_name;
                top_conf  = best->confidence;
            }

            // ---- Cooldown Suppression ----
            // ตรวจสอบก่อนส่งเข้า voter
            bool suppressed = false;
            if (!top_class.empty() && cooldown.is_suppressed(top_class)) {
                suppressed = true;
                // [DEBUG] แสดงเฉพาะตอน suppress เกิดขึ้น
                std::cout << "[SUPPRESS] " << top_class
                          << " cooldown remaining: "
                          << cv::format("%.1f", cooldown.remaining_sec(top_class))
                          << "s\n" << std::flush;
            }

            // ---- Temporal Voting ----
            // ส่งเข้า voter เฉพาะ class ที่ไม่ถูก suppress
            // ถ้า suppress → ส่ง "" เข้าไปแทน (ไม่นับ vote แต่ window เดิน)
            const std::string vote_input = suppressed ? "" : top_class;
            voter.update(vote_input);

            TemporalVoter::VoteResult vote = voter.evaluate();

            // [DEBUG] print เฉพาะเมื่อมี detection หรือ suppression
            if (!top_class.empty() && !suppressed) {
                std::cout << "[DET F" << frame_index << "]"
                          << " class=" << top_class
                          << " conf=" << cv::format("%.2f", top_conf)
                          << (suppressed ? " [SUPPRESSED]" : "")
                          << " | votes={";
                for (const auto& kv : vote.votes) {
                    std::cout << kv.first << ":" << kv.second << " ";
                }
                std::cout << "} buf=" << vote.history_size << "/10"
                          << " winner=" << (vote.winner.empty() ? "none" : vote.winner)
                          << " winner_count=" << vote.winner_count
                          << "\n" << std::flush;
            }

            // ---- Confirmed Output ----
            if (vote.confirmed) {
                const std::string output = vote.winner;
                voter.reset();

                // เปิด cooldown สำหรับ class ที่ confirm
                cooldown.activate(output);

                // [DEBUG] แสดงเสมอเมื่อ confirm
                float cd_dur = 0.0f;
                {
                    auto it = cooldown.class_cooldowns.find(output);
                    cd_dur = (it != cooldown.class_cooldowns.end())
                             ? it->second
                             : cooldown.default_cooldown_sec;
                }
                std::cout << "\n[CONFIRMED] >>> " << output
                          << " | cooldown activated: "
                          << cv::format("%.1f", cd_dur) << "s\n\n"
                          << std::flush;
            }

            // ---- Save frames ----
            if (cfg.save_frames && frame_index % cfg.save_every == 0) {
                save_debug_frame(frame_bgr, detections, cfg, frame_index);
            }

            // ---- Draw ----
            times.draw_ms = 0.0;
            if (!cfg.no_draw) {
                Timer draw_timer;
                draw_timer.tic();
                canvas_bgr = frame_bgr.clone();
                draw_phase1_overlay(
                    canvas_bgr, detections, times,
                    avg.fps.value(), cfg.imgsz, cfg.conf_threshold
                );
                times.draw_ms = draw_timer.toc_ms();
            }

            times.total_ms = total_timer.toc_ms();
            const double fps_val =
                times.total_ms > 0.0 ? 1000.0 / times.total_ms : 0.0;
            avg.add(times, fps_val);

            if (!cfg.hide_window && !cfg.no_draw) {
                cv::imshow("Detection", canvas_bgr);
                const int key = cv::waitKey(1) & 0xFF;
                if (key == 27 || key == 'q') break;
            }
        }

        std::cout << "Stopped.\n";
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "\nError: " << e.what() << "\n";
        return 1;
    }
}