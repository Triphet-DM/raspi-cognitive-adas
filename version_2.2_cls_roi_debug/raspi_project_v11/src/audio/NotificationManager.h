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
// v1 ข้อจำกัดที่ตั้งใจ: single category, ไม่มี priority/queue, non-preemptive
//   (aplay เล่นจนจบ); shutdown drain ≤ 1 คลิป (std::system block จน aplay จบ)
//
// enabled=false -> no-op ทั้งหมด ไม่ spawn thread (ใช้ตอนไม่เปิด --audio)
// ============================================================

#include <condition_variable>
#include <mutex>
#include <string>
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

    // speed convenience: (action,value) -> SpeedAudioMap -> submit(). SuppressX/ไม่รู้จัก -> เงียบ
    void notify(Action action, const std::string& value);

private:
    void run();                                    // audio thread loop (consumer)
    void play_blocking(const std::string& path);   // aplay จนจบ (เรียกนอก lock)

    const std::string audio_dir_;
    const std::string device_;
    const bool        enabled_;

    std::thread             thread_;
    std::mutex              mutex_;
    std::condition_variable cv_;
    std::string             pending_;          // ชื่อไฟล์ถัดไป — single slot
    bool                    has_pending_ = false;
    bool                    stop_        = false;
};
