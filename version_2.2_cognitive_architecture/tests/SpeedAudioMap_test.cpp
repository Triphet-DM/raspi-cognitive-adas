// ============================================================
// Unit test — SpeedAudioMap (pure, framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/audio/SpeedAudioMap.cpp
//       tests/SpeedAudioMap_test.cpp -o audiomap_test
//   ./audiomap_test          # exit 0 = ผ่านหมด
// ============================================================

#include "audio/SpeedAudioMap.h"

#include <iostream>

using Map = SpeedAudioMap;
using Act = AnnouncementPolicy::Action;

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// CHANGE -> change_XX.wav ครบทุกค่า
static void test_change_all_values() {
    CHECK(Map::filename(Act::Change, "sign_50")  == "change_50.wav");
    CHECK(Map::filename(Act::Change, "sign_60")  == "change_60.wav");
    CHECK(Map::filename(Act::Change, "sign_80")  == "change_80.wav");
    CHECK(Map::filename(Act::Change, "sign_90")  == "change_90.wav");
    CHECK(Map::filename(Act::Change, "sign_100") == "change_100.wav");  // 3 หลัก
}

// REMINDER -> reminder_XX.wav
static void test_reminder_all_values() {
    CHECK(Map::filename(Act::Reminder, "sign_50")  == "reminder_50.wav");
    CHECK(Map::filename(Act::Reminder, "sign_100") == "reminder_100.wav");
}

// SuppressX -> ไม่มีคลิป
static void test_suppress_no_clip() {
    CHECK(Map::filename(Act::SuppressCooldown,     "sign_50").empty());
    CHECK(Map::filename(Act::SuppressContinuation, "sign_50").empty());
}

// ค่าที่ไม่อยู่ใน known speed set -> ไม่มีคลิป (graceful, ไม่ใช่ filename มั่ว)
static void test_unknown_value_no_clip() {
    CHECK(Map::filename(Act::Change,   "no_parking").empty());
    CHECK(Map::filename(Act::Change,   "sign_70").empty());   // ไม่อยู่ใน set
    CHECK(Map::filename(Act::Change,   "").empty());
    CHECK(Map::filename(Act::Change,   "sign_").empty());
    CHECK(Map::filename(Act::Reminder, "50").empty());        // ไม่มี prefix sign_
}

int main() {
    test_change_all_values();
    test_reminder_all_values();
    test_suppress_no_clip();
    test_unknown_value_no_clip();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
