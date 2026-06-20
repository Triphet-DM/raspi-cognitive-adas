#pragma once

// ============================================================
// L4 — NotificationManager  (effectful: thread + ALSA aplay)
//
//   notify(action, value)  [decision thread]  — non-blocking producer
//        └─ SpeedAudioMap: (action,value) -> "change_50.wav"
//        └─ เขียนทับ single slot (latest-wins) + ปลุก audio thread
//   audio thread (consumer): หยิบ latest -> aplay จนจบ -> วน
//
// pattern = double-buffer เดียวกับ CameraThread / AsyncDetectionWorker
//   (single producer + single consumer + 1 mutex + condvar + 1 pending slot)
//
// preempt(file): ตัด aplay ที่เล่นอยู่กลางคลิป (SIGTERM → 100ms → SIGKILL) แล้วเล่น file แทน —
//   ใช้เมื่อ Arbiter ตัดสิน PREEMPT (safety แทรก). submit(file) = ต่อคิว latest-wins (ไม่ตัด)
// process model: spawn aplay ด้วย posix_spawn (ถือ PID ไว้ → ฆ่าได้, ต่างจาก std::system) —
//   producer = คนสั่ง kill; audio thread = เจ้าเดียวที่ waitpid (reap กัน zombie). ทุก access
//   ของ child_pid_ อยู่ใต้ mutex_ → reap+clear atomic กับ kill (ปิดช่อง PID-reuse)
//
// enabled=false -> no-op ทั้งหมด ไม่ spawn thread (ใช้ตอนไม่เปิด --audio)
// ============================================================

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <sys/types.h>             // pid_t
#include <thread>

#include "audio/SpeedAudioMap.h"   // Action + filename()

class NotificationManager {
public:
    using Action = SpeedAudioMap::Action;

    //   audio_dir : โฟลเดอร์ .wav (join กับชื่อไฟล์จาก SpeedAudioMap)
    //   device    : ALSA device ของ `aplay -D` (เช่น "plughw:0,0")
    //   enabled   : false = ปิดทั้งหมด, ไม่สร้าง thread
    NotificationManager(std::string audio_dir, std::string device, bool enabled);
    ~NotificationManager();

    // ถือ thread + mutex -> ห้าม copy/move
    NotificationManager(const NotificationManager&)            = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    // generic L4 entry: เล่นไฟล์ชื่อนี้ (latest-wins). "" -> no-op.
    //   เป็นทางเข้ากลางที่ทั้ง 2 สมองใช้ร่วม (speed ผ่าน notify() ด้านล่าง, momentary ส่งตรง)
    void submit(const std::string& filename);

    // PREEMPT: ตัดคลิปที่กำลังเล่น (kill aplay) แล้วเล่น filename แทนทันที. "" -> no-op.
    //   เรียกเมื่อ Arbiter ตัดสิน Preempt เท่านั้น (Play ปกติ -> submit)
    void preempt(const std::string& filename);

    // speed convenience: (action,value) -> SpeedAudioMap -> submit(). SuppressX/ไม่รู้จัก -> เงียบ
    void notify(Action action, const std::string& value);

    // true = ช่องว่างจริง (ไม่มีคลิปเล่นอยู่ + ไม่มีคิวค้าง). main ใช้ขับ re-delivery.
    //   เช็คทั้ง child_pid_ และ has_pending_ → ปิดช่วง gap ระหว่าง submit กับ aplay เริ่มจริง
    bool is_idle();

private:
    using Clock = std::chrono::steady_clock;

    void run();                              // audio thread loop (consumer)
    void play_clip(const std::string& path); // spawn aplay + reap (เรียกนอก run()'s lock)

    const std::string audio_dir_;
    const std::string device_;
    const bool        enabled_;

    std::thread             thread_;
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::string             pending_;          // ชื่อไฟล์ถัดไป — single slot (latest-wins)
    bool                    has_pending_ = false;
    bool                    stop_        = false;

    // process ของ aplay ที่กำลังเล่น — ทุก access ใต้ mutex_
    pid_t             child_pid_    = -1;      // -1 = ไม่มีคลิปเล่นอยู่ (idle)
    bool              term_pending_ = false;   // ส่ง SIGTERM แล้ว รอ 100ms ค่อย SIGKILL
    Clock::time_point term_deadline_{};        // เส้นตายก่อน escalate เป็น SIGKILL
};
