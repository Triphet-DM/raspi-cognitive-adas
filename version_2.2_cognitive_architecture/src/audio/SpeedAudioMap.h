#pragma once

// ============================================================
// SpeedAudioMap — pure: (L3 Action, belief value) -> ชื่อไฟล์เสียง
//
//   Change   + sign_50  -> "change_50.wav"
//   Reminder + sign_60  -> "reminder_60.wav"
//   SuppressX / ค่าที่ไม่รู้จัก -> ""   (ไม่มีคลิป → caller ข้ามการเล่น)
//
// คืน "ชื่อไฟล์ล้วน" ไม่แตะ filesystem (NotificationManager เป็นคน join กับ audio-dir)
// ถือ known speed set ไว้ในตัวเอง เพื่อไม่ผูกกับ SpeedSignClassifier (ncnn/opencv)
//   -> unit-test ได้ด้วย g++ ล้วน เหมือน L1/L2/L3
// ============================================================

#include <string>

#include "decision/AnnouncementPolicy.h"   // Action (pure header — chrono/optional เท่านั้น)

class SpeedAudioMap {
public:
    using Action = AnnouncementPolicy::Action;

    // คืน "change_50.wav" / "reminder_60.wav" หรือ "" ถ้าไม่มีคลิปสำหรับเคสนี้
    static std::string filename(Action action, const std::string& value);

private:
    // "sign_50" -> "50" ถ้าอยู่ใน known speed set, ไม่งั้นคืน ""
    static std::string speed_number(const std::string& value);
};
