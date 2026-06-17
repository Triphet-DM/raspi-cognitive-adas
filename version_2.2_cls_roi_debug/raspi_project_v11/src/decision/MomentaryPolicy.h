#pragma once

// ============================================================
// MomentaryPolicy — per-class policy ของ Brain 2 (Momentary / non-speed)
//
//   { suppression_window (TIME, ต่อคลาส) , attention_rank (แกนเดียว) }
//
//   attention_rank : ยุบ priority + interrupt_level เหลือแกนเดียว (FROZEN 2026-06-15)
//   INTERRUPT_THRESHOLD = Safety Boundary = rank ต่ำสุดของกลุ่ม life-safety (School Zone)
//       → "interrupt ได้" ⟺ "เป็น life-safety" (re-derive Law 2)
//
//   class memory ไม่ใช่ instance: window ผูกกับ "ชนิดป้าย" (camera-only, ไม่มี tracking/GPS)
//
// ตัวเลขทั้งหมด PROVISIONAL — โครงสร้าง (สเกล/threshold/เว้นช่วง) คือสิ่งที่ frozen
//   ค่า window/rank จริงไป tune ที่ bench (Q6: อย่าฟันค่าที่ขึ้นกับถนนจากสมมติฐาน)
//
// pure: ถือ known momentary set ไว้ในตัวเอง (ไม่ผูก ncnn/opencv) → unit-test ด้วย g++ ล้วน
//   เหมือน SpeedAudioMap / L1-L3
// ============================================================

#include <chrono>
#include <string>

struct MomentaryPolicy {
    std::chrono::milliseconds suppression_window;
    int                       attention_rank;

    // Safety Boundary = School Zone rank. ป้าย rank >= นี้ คือ life-safety → interrupt ได้
    static constexpr int INTERRUPT_THRESHOLD = 20;

    // คืน policy ของคลาส momentary, หรือ nullptr ถ้าไม่ใช่ momentary (speed / ไม่รู้จัก)
    static const MomentaryPolicy* lookup(const std::string& cls);

    // ป้ายนี้อยู่ในกลุ่ม Brain 2 (non-speed) ไหม
    static bool is_momentary(const std::string& cls) { return lookup(cls) != nullptr; }
};
