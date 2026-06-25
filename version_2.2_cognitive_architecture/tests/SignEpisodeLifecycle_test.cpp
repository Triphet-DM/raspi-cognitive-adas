// ============================================================
// Unit test — L1 SignEpisodeLifecycle  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/SignEpisodeLifecycle.cpp
//       tests/SignEpisodeLifecycle_test.cpp -o l1_test
//   ./l1_test          # exit 0 = ผ่านหมด
//
// แต่ละ test = แถวหนึ่งของ L1 Trigger Table (+ re-arm / fresh / edge)
// fake clock เป็น "มิลลิวินาที" เพราะ rearm_after วัดเป็น ms
// ============================================================

#include "decision/SignEpisodeLifecycle.h"

#include <chrono>
#include <iostream>

using L     = SignEpisodeLifecycle;
using State = L::State;
using Clock = L::Clock;
using Ms    = L::Millis;

// fake clock — time_point จากมิลลิวินาที
static L::TimePoint t(int ms) {
    return Clock::time_point{} + std::chrono::milliseconds(ms);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// Armed + confirm -> Confirmed, fresh=TRUE, value pass-through
static void test_acquire_fresh() {
    L l(Ms(600));
    CHECK(l.state() == State::Armed);
    auto ev = l.update(true, true, "50", t(0));
    CHECK(ev.fired && ev.fresh && ev.value == "50");
    CHECK(l.state() == State::Confirmed);
}

// Confirmed + confirm (ต่อเนื่อง) -> fired, fresh=FALSE
static void test_continuation_not_fresh() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed
    auto ev = l.update(true, true, "50", t(100));
    CHECK(ev.fired && !ev.fresh && ev.value == "50");
    CHECK(l.state() == State::Confirmed);
}

// Confirmed + presence (ไม่ confirm) -> ไม่ emit, ยัง Confirmed
static void test_presence_only_no_emit() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed
    auto ev = l.update(true, false, "", t(200));
    CHECK(!ev.fired);
    CHECK(l.state() == State::Confirmed);
}

// absence: Confirmed -> Releasing -> (>=rearm) Armed   (boundary ใช้ >=)
static void test_releasing_then_rearm() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed, last_seen=0
    CHECK(!l.update(false, false, "", t(100)).fired);
    CHECK(l.state() == State::Releasing);              // 100 < 600
    CHECK(l.state() == State::Releasing);
    l.update(false, false, "", t(500));
    CHECK(l.state() == State::Releasing);              // 500 < 600
    l.update(false, false, "", t(600));
    CHECK(l.state() == State::Armed);                  // 600 >= 600 -> re-arm
}

// re-arm แล้ว confirm ใหม่ = fresh=TRUE อีกครั้ง (วงรอบ gap -> fresh)
static void test_rearm_then_fresh_again() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));
    l.update(false, false, "", t(700));                // re-arm -> Armed
    CHECK(l.state() == State::Armed);
    auto ev = l.update(true, true, "50", t(800));
    CHECK(ev.fired && ev.fresh);                        // Armed->Confirmed = fresh
}

// re-seen จาก Releasing (ก่อนหมดเวลา) -> Confirmed, ไม่ emit;
// confirm ถัดไป fresh=FALSE (ไม่มี gap ครบ)
static void test_reseen_no_emit_not_fresh() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed
    l.update(false, false, "", t(100));                // Releasing
    auto ev = l.update(true, false, "", t(200));       // re-seen
    CHECK(!ev.fired);
    CHECK(l.state() == State::Confirmed);
    auto ev2 = l.update(true, true, "50", t(300));
    CHECK(ev2.fired && !ev2.fresh);                    // ไม่ fresh เพราะไม่ได้ re-arm
}

// presence ต่อเนื่อง refresh last_seen -> ไม่ re-arm แม้เวลารวมเกิน rearm_after
static void test_presence_prevents_rearm() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed
    l.update(true, false, "", t(500));
    l.update(true, false, "", t(1000));
    l.update(true, false, "", t(1700));                // เกิน 600 รวม แต่ presence ค้ำไว้
    CHECK(l.state() == State::Confirmed);
}

// Armed + presence ล้วน (ไม่ confirm) -> ไม่ emit, ยัง Armed (ยังไม่มี episode)
static void test_armed_presence_no_emit() {
    L l(Ms(600));
    CHECK(!l.update(true, false, "50", t(0)).fired);
    CHECK(!l.update(true, false, "50", t(100)).fired);
    CHECK(l.state() == State::Armed);
}

// gapless value change: confirm ค่าใหม่ระหว่าง presence ต่อเนื่อง -> fired, fresh=FALSE
// (L1 รายงานค่าใหม่; L2 เป็นคนตัดสินว่าเปลี่ยน)
static void test_gapless_value_change_not_fresh() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed(50)
    auto ev = l.update(true, true, "60", t(100));
    CHECK(ev.fired && !ev.fresh && ev.value == "60");
}

static void test_reset() {
    L l(Ms(600));
    l.update(true, true, "50", t(0));                  // Confirmed
    l.reset();
    CHECK(l.state() == State::Armed);
    auto ev = l.update(true, true, "60", t(100));
    CHECK(ev.fired && ev.fresh);                        // acquisition ใหม่ -> fresh
}

// rearm_after < 0 -> clamp 0; absence เฟรมเดียว re-arm ทันที
static void test_rearm_clamp_zero() {
    L l(Ms(-100));
    CHECK(l.rearm_after() == Ms(0));
    l.update(true, true, "50", t(0));                  // Confirmed, last_seen=0
    l.update(false, false, "", t(0));                  // 0-0=0 >= 0 -> re-arm
    CHECK(l.state() == State::Armed);
}

int main() {
    test_acquire_fresh();
    test_continuation_not_fresh();
    test_presence_only_no_emit();
    test_releasing_then_rearm();
    test_rearm_then_fresh_again();
    test_reseen_no_emit_not_fresh();
    test_presence_prevents_rearm();
    test_armed_presence_no_emit();
    test_gapless_value_change_not_fresh();
    test_reset();
    test_rearm_clamp_zero();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
