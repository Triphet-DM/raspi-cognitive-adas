#pragma once

// ============================================================
// BehaviorPolicyRouter — นั่งหลัง TemporalVoter confirm (Brain 2 design FROZEN 2026-06-15)
//
//   ตอบ: "ป้ายที่ confirm นี้ควรใช้ behavioral policy แบบไหน → ไปสมองไหน"
//   (generalize ของ is_speed() gate เดิม)
//
//     Speed     → Brain 1 (PersistentState: L1→L2→L3 → shared L4)
//     Momentary → Brain 2 (MomentaryEngine: timestamp suppression)
//     None      → ไม่รู้จัก → ไม่ทำอะไร
//
// route() บนชื่อคลาส (voter winner) — CLS ปรับแค่ "ค่า" ภายใน speed ไม่เปลี่ยน speed↔non-speed
//
// pure: ถือ known speed set เอง(mirror SpeedSignClassifier::speed_sign_group) เพื่อไม่ผูก
//   ncnn/opencv → unit-test ด้วย g++ ล้วน (ปรัชญาเดียวกับ SpeedAudioMap / MomentaryPolicy)
// ============================================================

#include <string>

class BehaviorPolicyRouter {
public:
    enum class Brain { Speed, Momentary, None };

    static Brain route(const std::string& cls);
};
