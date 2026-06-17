// ============================================================
// Unit test — Brain 2 MomentaryEngine + MomentaryPolicy  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/MomentaryPolicy.cpp
//       src/decision/MomentaryEngine.cpp
//       tests/MomentaryEngine_test.cpp -o mom_test
//   ./mom_test         # exit 0 = ผ่านหมด
//
// ครอบคลุม: policy lookup/threshold · suppress window · suppress ไม่ refresh timer ·
//           per-class independence · reset
// ============================================================

#include "decision/MomentaryEngine.h"
#include "decision/MomentaryPolicy.h"

#include <chrono>
#include <iostream>

using E   = MomentaryEngine;
using Dec = E::Decision;

// fake clock — time_point จากวินาที (engine ใช้แค่เก็บ/ลบเวลา)
static E::TimePoint t(int sec) {
    return E::Clock::time_point{} + std::chrono::seconds(sec);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// ---- MomentaryPolicy ----------------------------------------------------

static void test_policy_lookup() {
    // speed / ไม่รู้จัก → ไม่ใช่ momentary
    CHECK(MomentaryPolicy::lookup("sign_50") == nullptr);
    CHECK(!MomentaryPolicy::is_momentary("sign_50"));
    CHECK(MomentaryPolicy::lookup("garbage") == nullptr);
    // momentary → มี policy
    CHECK(MomentaryPolicy::is_momentary("Pedestrian_crossing"));
    CHECK(MomentaryPolicy::is_momentary("no_parking"));
}

static void test_policy_ranks() {
    // re-ranked 2026-06-17: School Zone สูงสุด, Pedestrian_crossing = boundary
    CHECK(MomentaryPolicy::lookup("School_Zone")->attention_rank == 30);
    CHECK(MomentaryPolicy::lookup("Pedestrian_Warning_Sign")->attention_rank == 25);
    CHECK(MomentaryPolicy::lookup("Pedestrian_crossing")->attention_rank == 20);
    CHECK(MomentaryPolicy::lookup("curve_ahead")->attention_rank == 10);
    CHECK(MomentaryPolicy::lookup("no_stop")->attention_rank == 4);
    // Pedestrian_crossing = ต่ำสุดของ safety = Safety Boundary
    CHECK(MomentaryPolicy::lookup("Pedestrian_crossing")->attention_rank
          == MomentaryPolicy::INTERRUPT_THRESHOLD);
}

// safety interrupt-capable (>= threshold), อื่น ๆ ไม่ใช่
static void test_threshold_membership() {
    const int TH = MomentaryPolicy::INTERRUPT_THRESHOLD;
    CHECK(MomentaryPolicy::lookup("School_Zone")->attention_rank >= TH);
    CHECK(MomentaryPolicy::lookup("Pedestrian_Warning_Sign")->attention_rank >= TH);
    CHECK(MomentaryPolicy::lookup("Pedestrian_crossing")->attention_rank >= TH);   // = TH
    CHECK(MomentaryPolicy::lookup("curve_ahead")->attention_rank < TH);
    CHECK(MomentaryPolicy::lookup("no_parking")->attention_rank < TH);
}

// ---- MomentaryEngine ----------------------------------------------------

static void test_first_announce() {
    E e;
    auto r = e.onConfirmed("Pedestrian_crossing", t(0));
    CHECK(r.decision == Dec::Announce);
    CHECK(r.cls == "Pedestrian_crossing");
    CHECK(r.attention_rank == 20);
}

// ภายใน window → suppress (School_Zone window = 5s)
static void test_suppress_within_window() {
    E e;
    CHECK(e.onConfirmed("School_Zone", t(0)).decision == Dec::Announce);
    CHECK(e.onConfirmed("School_Zone", t(2)).decision == Dec::Suppress);   // 2s < 5s
    CHECK(e.onConfirmed("School_Zone", t(4)).decision == Dec::Suppress);   // 4s < 5s
}

// พ้น window → announce อีกครั้ง
static void test_announce_after_window() {
    E e;
    CHECK(e.onConfirmed("School_Zone", t(0)).decision == Dec::Announce);
    CHECK(e.onConfirmed("School_Zone", t(5)).decision == Dec::Announce);   // 5s >= 5s
}

// suppress ไม่ refresh timer: window วัดจากครั้งที่ "ประกาศ" ล่าสุด ไม่ใช่ครั้งที่เห็น
static void test_suppress_does_not_refresh() {
    E e;
    CHECK(e.onConfirmed("School_Zone", t(0)).decision == Dec::Announce);   // stamp t0
    CHECK(e.onConfirmed("School_Zone", t(3)).decision == Dec::Suppress);   // 3<5, ไม่ stamp
    // ถ้า suppress refresh timer ผิด ๆ → t6 จะวัดจาก t3 (3s<5 → suppress)
    // ที่ถูก: วัดจาก t0 → 6s>=5 → announce
    CHECK(e.onConfirmed("School_Zone", t(6)).decision == Dec::Announce);   // stamp t6
}

// คนละคลาส → window แยกกัน ไม่กวนกัน
static void test_per_class_independence() {
    E e;
    CHECK(e.onConfirmed("Pedestrian_crossing", t(0)).decision == Dec::Announce);
    CHECK(e.onConfirmed("School_Zone",         t(0)).decision == Dec::Announce);  // คนละ class
    CHECK(e.onConfirmed("Pedestrian_crossing", t(1)).decision == Dec::Suppress);  // ของตัวเอง
    CHECK(e.onConfirmed("no_parking",          t(1)).decision == Dec::Announce);  // class ใหม่
}

// non-momentary (speed) → suppress, rank 0 (กันพลาด)
static void test_non_momentary_guard() {
    E e;
    auto r = e.onConfirmed("sign_50", t(0));
    CHECK(r.decision == Dec::Suppress);
    CHECK(r.attention_rank == 0);
}

static void test_reset() {
    E e;
    CHECK(e.onConfirmed("no_parking", t(0)).decision == Dec::Announce);
    CHECK(e.onConfirmed("no_parking", t(5)).decision == Dec::Suppress);   // 5<30
    e.reset();
    CHECK(e.onConfirmed("no_parking", t(6)).decision == Dec::Announce);   // ความจำหาย → announce
}

int main() {
    test_policy_lookup();
    test_policy_ranks();
    test_threshold_membership();
    test_first_announce();
    test_suppress_within_window();
    test_announce_after_window();
    test_suppress_does_not_refresh();
    test_per_class_independence();
    test_non_momentary_guard();
    test_reset();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
