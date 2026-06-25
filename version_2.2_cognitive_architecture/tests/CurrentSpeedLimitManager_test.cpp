// ============================================================
// Unit test — L2 CurrentSpeedLimitManager  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/CurrentSpeedLimitManager.cpp
//       tests/CurrentSpeedLimitManager_test.cpp -o l2_test
//   ./l2_test          # exit 0 = ผ่านหมด
//
// แต่ละ test = แถวหนึ่งของ L2 Transition Table (+ K=1/K=2/edge)
// ============================================================

#include "decision/CurrentSpeedLimitManager.h"

#include <chrono>
#include <iostream>

using M     = CurrentSpeedLimitManager;
using Out   = M::Outcome;
using Clock = M::Clock;

// fake clock — สร้าง time_point จากวินาที (L2 ใช้แค่เก็บ/ลบเวลา ไม่ต้องเดินจริง)
static M::TimePoint t(int sec) {
    return Clock::time_point{} + std::chrono::seconds(sec);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// row 1 + row 2: acquisition แล้วยืนยันค่าเดิม
static void test_k1_acquire_and_reconfirm() {
    M m(1);
    CHECK(!m.current().has_value());                       // เริ่มที่ UNKNOWN
    CHECK(m.onValue("50", t(0)) == Out::Acquire);          // row 1
    CHECK(m.current().has_value() && *m.current() == "50");
    CHECK(m.onValue("50", t(1)) == Out::Reconfirm);        // row 2
    CHECK(*m.current() == "50");
}

// row 4 (K=1): ค่าต่าง commit ทันที
static void test_k1_change_immediate() {
    M m(1);
    m.onValue("50", t(0));
    CHECK(m.onValue("60", t(1)) == Out::Change);           // K=1 → commit ทันที
    CHECK(*m.current() == "60");
    CHECK(m.onValue("80", t(2)) == Out::Change);
    CHECK(*m.current() == "80");
}

static void test_is_change_flag() {
    CHECK(M::is_change(Out::Acquire));
    CHECK(M::is_change(Out::Change));
    CHECK(!M::is_change(Out::Reconfirm));
    CHECK(!M::is_change(Out::Pending));
}

// row 3 → row 4 (K=2): ต้องยืนยัน 2 ครั้งก่อน commit
static void test_k2_hysteresis() {
    M m(2);
    m.onValue("50", t(0));                                 // Acquire
    CHECK(m.onValue("60", t(1)) == Out::Pending);          // ครั้งที่ 1 ยังไม่ commit
    CHECK(*m.current() == "50");
    CHECK(m.onValue("60", t(2)) == Out::Change);           // ครั้งที่ 2 → commit
    CHECK(*m.current() == "60");
}

// row 2 ล้าง pending: ค่าแปลกปลอมครั้งเดียวถูกดูดซับ
static void test_k2_noise_absorbed() {
    M m(2);
    m.onValue("50", t(0));
    CHECK(m.onValue("60", t(1)) == Out::Pending);          // 60 แปลกปลอม
    CHECK(m.onValue("50", t(2)) == Out::Reconfirm);        // กลับมา 50 → clear pending
    CHECK(*m.current() == "50");
    CHECK(m.onValue("60", t(3)) == Out::Pending);          // นับใหม่จาก 1 (ไม่ใช่ 2)
    CHECK(*m.current() == "50");
}

// streak restart เมื่อเจอค่าที่สาม
static void test_k2_third_value_restart() {
    M m(2);
    m.onValue("50", t(0));
    CHECK(m.onValue("60", t(1)) == Out::Pending);          // pending=60, count=1
    CHECK(m.onValue("80", t(2)) == Out::Pending);          // restart pending=80, count=1
    CHECK(*m.current() == "50");
    CHECK(m.onValue("80", t(3)) == Out::Change);           // 80 ครบ 2 → commit
    CHECK(*m.current() == "80");
}

static void test_reset() {
    M m(1);
    m.onValue("50", t(0));
    m.reset();
    CHECK(!m.current().has_value());                       // กลับ UNKNOWN
    CHECK(m.onValue("60", t(1)) == Out::Acquire);          // acquisition ใหม่
}

static void test_k_clamped() {
    M m(0);                                                // K<=0 → clamp เป็น 1
    CHECK(m.k() == 1);
    m.onValue("50", t(0));
    CHECK(m.onValue("60", t(1)) == Out::Change);           // ทำตัวเหมือน K=1
}

// age: meaningful ตอน ACTIVE, refresh ทุก confirm ค่าเดิม
static void test_age_refresh() {
    M m(1);
    m.onValue("50", t(0));
    CHECK(m.age(t(5)) == std::chrono::milliseconds(5000)); // 5s นับจาก t(0)
    m.onValue("50", t(8));                                 // reconfirm → refresh
    CHECK(m.age(t(10)) == std::chrono::milliseconds(2000)); // 2s นับจาก t(8)
}

int main() {
    test_k1_acquire_and_reconfirm();
    test_k1_change_immediate();
    test_is_change_flag();
    test_k2_hysteresis();
    test_k2_noise_absorbed();
    test_k2_third_value_restart();
    test_reset();
    test_k_clamped();
    test_age_refresh();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
