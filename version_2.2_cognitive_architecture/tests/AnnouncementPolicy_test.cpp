// ============================================================
// Unit test — L3 AnnouncementPolicy  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/AnnouncementPolicy.cpp
//       tests/AnnouncementPolicy_test.cpp -o l3_test
//   ./l3_test          # exit 0 = ผ่านหมด
//
// แต่ละ test = แถวหนึ่งของ L3 Decision Table (+ cooldown / timer / edge)
// ============================================================

#include "decision/AnnouncementPolicy.h"

#include <chrono>
#include <iostream>

using P     = AnnouncementPolicy;
using Act   = P::Action;
using Clock = P::Clock;
using Ms    = P::Millis;

// fake clock — สร้าง time_point จากวินาที (L3 ใช้แค่ลบเวลา ไม่ต้องเดินจริง)
static P::TimePoint t(int sec) {
    return Clock::time_point{} + std::chrono::seconds(sec);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// row 1: changed -> Change เสมอ (ไม่ขึ้นกับ fresh, ไม่ขึ้นกับ cooldown)
static void test_change_always() {
    P p(Ms(10000));
    CHECK(p.decide(true, true,  t(0)) == Act::Change);
    CHECK(p.decide(true, false, t(1)) == Act::Change);   // ติดกัน, cooldown ยังไม่ครบ ก็ยัง Change
}

// Change เซ็ต/รีเซ็ต timer: fresh-same หลัง Change ภายใน cooldown -> SuppressCooldown
static void test_change_resets_timer() {
    P p(Ms(10000));
    CHECK(p.decide(true, false, t(0)) == Act::Change);            // timer = t(0)
    CHECK(p.decide(false, true, t(5)) == Act::SuppressCooldown);  // 5s < 10s
}

// row 2/3 + boundary: cooldown ใช้ >= (ครบพอดีถือว่าครบ)
static void test_reminder_boundary() {
    P p(Ms(10000));
    p.decide(true, false, t(0));                                  // seed timer t(0)
    CHECK(p.decide(false, true, t(9))  == Act::SuppressCooldown); // 9s < 10s
    CHECK(p.decide(false, true, t(10)) == Act::Reminder);         // 10s >= 10s (boundary)
}

// Reminder ก็รีเซ็ต timer: ย้ำครั้งถัดไปต้องรอ cooldown ใหม่
static void test_reminder_resets_timer() {
    P p(Ms(10000));
    p.decide(true, false, t(0));                                  // timer t(0)
    CHECK(p.decide(false, true, t(10)) == Act::Reminder);         // timer -> t(10)
    CHECK(p.decide(false, true, t(15)) == Act::SuppressCooldown); // 15-10=5 < 10
    CHECK(p.decide(false, true, t(20)) == Act::Reminder);         // 20-10=10 >= 10
}

// row 4: ไม่ fresh -> SuppressContinuation เสมอ และต้องไม่แตะ timer
static void test_suppress_continuation() {
    P p(Ms(10000));
    p.decide(true, false, t(0));                                  // timer t(0)
    CHECK(p.decide(false, false, t(100)) == Act::SuppressContinuation); // แม้ครบนานแล้วก็เงียบ
    CHECK(p.decide(false, true,  t(101)) == Act::Reminder);       // timer ยังเป็น t(0) -> 101>=10
}

// Change แทรกกลาง: เด้ง cooldown แล้ว reseed timer
static void test_change_after_suppress() {
    P p(Ms(10000));
    p.decide(true, false, t(0));                                  // change, timer t(0)
    CHECK(p.decide(false, true, t(3)) == Act::SuppressCooldown);  // ถูกกด
    CHECK(p.decide(true,  false, t(4)) == Act::Change);           // change เด้ง cooldown -> timer t(4)
    CHECK(p.decide(false, true, t(5)) == Act::SuppressCooldown);  // 5-4=1 < 10
}

static void test_is_announce_flag() {
    CHECK(P::is_announce(Act::Change));
    CHECK(P::is_announce(Act::Reminder));
    CHECK(!P::is_announce(Act::SuppressCooldown));
    CHECK(!P::is_announce(Act::SuppressContinuation));
}

static void test_reset() {
    P p(Ms(10000));
    p.decide(true, false, t(0));                                  // timer t(0)
    CHECK(p.decide(false, true, t(5)) == Act::SuppressCooldown);  // 5 < 10
    p.reset();                                                    // ล้าง timer
    CHECK(p.decide(false, true, t(6)) == Act::Reminder);          // เหมือนยังไม่เคยประกาศ -> ครบ
}

// ยังไม่เคยประกาศเลย = ถือว่า cooldown ครบ (nullopt = elapsed)
// flow จริง event แรกคือ Acquire->Change เสมอ; เทสนี้ทดสอบ leaf ตรง ๆ
static void test_initial_no_announce() {
    P p(Ms(10000));
    CHECK(p.decide(false, true, t(0)) == Act::Reminder);
}

static void test_cooldown_clamp_negative() {
    P p(Ms(-5000));                                               // < 0 -> clamp เป็น 0
    CHECK(p.cooldown() == Ms(0));
    p.decide(true, false, t(0));                                  // timer t(0)
    CHECK(p.decide(false, true, t(0)) == Act::Reminder);          // cooldown=0 -> ครบเสมอ
}

int main() {
    test_change_always();
    test_change_resets_timer();
    test_reminder_boundary();
    test_reminder_resets_timer();
    test_suppress_continuation();
    test_change_after_suppress();
    test_is_announce_flag();
    test_reset();
    test_initial_no_announce();
    test_cooldown_clamp_negative();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
