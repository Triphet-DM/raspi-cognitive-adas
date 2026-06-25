#pragma once

// ============================================================
// MomentaryEngine — Brain 2 core (Momentary / non-speed signs)
//
//   NO episode lifecycle. NO L1/L2/L3. Human Memory Suppression Model:
//
//     onConfirmed(class, now):
//        now − last_notified[class] ≥ suppression_window[class] ?
//            NO  → SUPPRESS (เงียบ)              ← ไม่ stamp (วัดจาก "ครั้งที่พูดล่าสุด")
//            YES → ANNOUNCE · last_notified[class] = now
//
//   - window วัดจาก "ครั้งที่ประกาศล่าสุด" ไม่ใช่ "ครั้งที่เห็นล่าสุด"
//       (มนุษย์: "เพิ่งบอกไปไหม" ไม่ใช่ "เพิ่งเห็นป้ายไหม") → suppress ไม่ refresh timer
//   - class memory ไม่ใช่ instance (ผูก last_notified[class])
//   - ป้ายบังแวบแล้วโผล่ใหม่ = อยู่ใน window → suppress อัตโนมัติ
//       → คำถาม "episode เดิม/ใหม่" ไม่ต้องตอบ (timestamp กลืนไปหมด)
//
//   Engine ไม่รู้เรื่อง arbitration: คืนแค่ {decision, class, rank}
//       Notification Arbiter (ทีหลัง) เป็นคนเลือก/preempt จาก rank
//
// pure logic — ไม่มี I/O, ไม่มี log, ไม่มี dependency นอกจาก MomentaryPolicy
//   tick/onConfirmed เรียกจาก main/decision thread เท่านั้น → ไม่ต้อง lock
// ============================================================

#include <chrono>
#include <string>
#include <unordered_map>

#include "decision/MomentaryPolicy.h"

class MomentaryEngine {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    enum class Decision { Suppress, Announce };

    struct Result {
        Decision    decision = Decision::Suppress;
        std::string cls;                 // คลาสที่ trigger
        int         attention_rank = 0;  // จาก policy (ใช้โดย Arbiter ตอน Announce)
    };

    // เรียกเมื่อมี confirmed momentary sign (BehaviorPolicyRouter gate: เฉพาะ non-speed)
    //   ถ้า cls ไม่ใช่ momentary (ไม่มี policy) → Suppress, rank 0 (กันพลาด; router ควรกันมาแล้ว)
    Result onConfirmed(const std::string& cls, TimePoint now);

    static bool is_announce(Decision d) { return d == Decision::Announce; }

    void reset() { last_notified_.clear(); }   // ล้างความจำทั้งหมด (เริ่มใหม่)

private:
    std::unordered_map<std::string, TimePoint> last_notified_;
};
