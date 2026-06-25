#pragma once

// ============================================================
// MomentaryAudioMap — pure: ชื่อคลาส momentary -> ชื่อไฟล์เสียง (class->wav)
//
//   "no_parking"          -> "no_parking.wav"
//   "Pedestrian_crossing" -> "ped_crossing.wav"
//   คลาสที่ไม่รู้จัก / ไม่ใช่ momentary -> ""   (caller ข้ามการเล่น)
//
//   ต่างจาก SpeedAudioMap: momentary มี 1 ไฟล์ต่อคลาส (ไม่มี change/reminder)
//   คืน "ชื่อไฟล์ล้วน" ไม่แตะ filesystem (NotificationManager join กับ audio-dir)
//   pure → unit-test ด้วย g++ ล้วน
// ============================================================

#include <string>

class MomentaryAudioMap {
public:
    // คืนชื่อไฟล์ .wav ของคลาส, หรือ "" ถ้าไม่มีคลิป
    static std::string filename(const std::string& cls);
};
