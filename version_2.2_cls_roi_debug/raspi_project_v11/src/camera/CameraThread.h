#pragma once

// ============================================================
// CameraThread — dedicated camera producer thread
//
// ทำไมต้องมี class นี้:
//   camera.read() block ~30ms ทุก frame
//   ถ้ารันใน main thread จะทำให้ main ไม่ว่างรับ detection result
//   ที่ detector thread ทำเสร็จแล้ว → res_wait สะสม
//
// Design: Double Buffer + Atomic Index Swap
//   - slot[0] และ slot[1] สลับกันเป็น write/read buffer
//   - camera thread เขียนลง slot ที่ไม่ได้ใช้เสมอ
//   - main thread อ่าน slot ที่ ready_index_ ชี้อยู่
//   - ไม่มี blocking ระหว่างสอง thread
//
// Frame Ownership:
//   write_slot = 1 - ready_index_  → owned by camera thread
//   read_slot  = ready_index_      → safe for main thread to clone
//
// Race condition ที่เหลือและ acceptable:
//   main อ่าน ready_index_ → camera swap → main clone slot เก่า
//   ผล: main ได้ frame ก่อนหน้า 1 frame (ยังถูกต้อง)
//   latest-frame overwrite policy ยังทำงานถูกต้อง
//
// สิ่งที่ TIDAK ทำ:
//   - ไม่ใช้ mutex lock ทั้ง frame (จะทำให้ camera block)
//   - ไม่ใช้ queue ใหญ่ (จะสะสม latency)
// ============================================================

#include <atomic>
#include <chrono>
#include <thread>

#include <opencv2/opencv.hpp>

#include "camera/Picamera2Camera.h"

using CamClock     = std::chrono::steady_clock;
using CamTimePoint = std::chrono::time_point<CamClock>;

struct CameraFrame {
    cv::Mat  frame_bgr;
    CamTimePoint captured_at;
    uint64_t seq = 0;       // sequence number เพิ่มทุก frame
    bool     valid = false;
};

class CameraThread {
public:
    explicit CameraThread(Picamera2Camera& camera)
        : camera_(camera)
    {
        // เริ่ม thread หลังจาก init สมาชิกทั้งหมด
        thread_ = std::thread(&CameraThread::run, this);
    }

    ~CameraThread() {
        stop_.store(true, std::memory_order_relaxed);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // ============================================================
    // get_latest_frame — เรียกจาก main thread เท่านั้น
    //
    // clone frame จาก ready slot ออกมา
    // คืน false ถ้ายังไม่มี frame พร้อมเลย (ช่วงแรก startup)
    //
    // ทำไม clone แทนที่จะส่ง pointer:
    //   ถ้าส่ง pointer กลับ main thread อาจถือ reference
    //   ขณะที่ camera thread swap slot ใหม่
    //   clone ทำให้ ownership ชัดเจน: main thread เป็นเจ้าของ copy นั้น
    //
    // ทำไมไม่ lock:
    //   camera thread ไม่เคยเขียนลง ready_slot ที่ ready_index_ ชี้อยู่
    //   มันเขียนลง write_slot = 1 - ready_index_ เสมอ
    //   ดังนั้น clone ที่ ready_slot ปลอดภัยโดยไม่ต้อง lock
    // ============================================================
    bool get_latest_frame(CameraFrame& out) {
        const int idx = ready_index_.load(std::memory_order_acquire);
        const CameraFrame& slot = slots_[idx];

        if (!slot.valid) return false;

        // clone frame ออกมา — main thread owns copy นี้
        out.frame_bgr    = slot.frame_bgr.clone();
        out.captured_at  = slot.captured_at;
        out.seq          = slot.seq;
        out.valid        = true;
        return true;
    }

    // สถิติสำหรับ instrumentation
    uint64_t total_captured() const {
        return total_captured_.load(std::memory_order_relaxed);
    }

    uint64_t total_errors() const {
        return total_errors_.load(std::memory_order_relaxed);
    }

    double capture_ms() const {
        return last_capture_ms_.load(std::memory_order_relaxed);
    }

private:
    // ============================================================
    // run — camera thread loop
    //
    // วน loop ตลอดเวลา:
    //   1. camera.read() → block จนได้ frame ใหม่ (~33ms)
    //   2. เขียนลง write_slot (slot ที่ไม่ใช่ ready_slot)
    //   3. atomic swap ready_index_ → main thread จะอ่าน slot ใหม่
    //
    // ทำไม write_slot = 1 - ready_index_:
    //   ready_index_ = 0 → camera เขียนลง slot[1]
    //   ready_index_ = 1 → camera เขียนลง slot[0]
    //   สลับกันตลอด ไม่มีทางเขียนทับ slot ที่ main กำลังอ่าน
    // ============================================================
    void run() {
        uint64_t seq = 0;
        while (!stop_.load(std::memory_order_relaxed)) {
            const auto t0 = CamClock::now();

            // write_slot คือ slot ที่ main ไม่ได้ใช้อยู่
            const int write_idx = 1 - ready_index_.load(std::memory_order_relaxed);
            CameraFrame& write_slot = slots_[write_idx];

            if (!camera_.read(write_slot.frame_bgr)) {
                total_errors_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            write_slot.captured_at = CamClock::now();
            write_slot.seq         = ++seq;
            write_slot.valid       = true;

            // atomic swap: main thread จะเห็น slot ใหม่ใน next get_latest_frame()
            ready_index_.store(write_idx, std::memory_order_release);

            const double ms = std::chrono::duration<double, std::milli>(
                write_slot.captured_at - t0
            ).count();
            last_capture_ms_.store(ms, std::memory_order_relaxed);
            total_captured_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    Picamera2Camera&  camera_;
    std::thread       thread_;
    std::atomic<bool> stop_{false};

    // Double buffer — slots_[0] และ slots_[1]
    CameraFrame       slots_[2];

    // ready_index_ บอกว่า slot ไหน ready สำหรับ main thread
    // camera thread เขียนลง 1 - ready_index_ เสมอ
    std::atomic<int>  ready_index_{0};

    // Instrumentation
    std::atomic<uint64_t> total_captured_{0};
    std::atomic<uint64_t> total_errors_{0};
    std::atomic<double>   last_capture_ms_{0.0};
};
