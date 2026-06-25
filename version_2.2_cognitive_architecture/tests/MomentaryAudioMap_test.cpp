// ============================================================
// Unit test — MomentaryAudioMap (class->wav)  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/audio/MomentaryAudioMap.cpp
//       tests/MomentaryAudioMap_test.cpp -o momaudio_test
//   ./momaudio_test
// ============================================================

#include "audio/MomentaryAudioMap.h"

#include <iostream>

using A = MomentaryAudioMap;

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

static void test_known_classes() {
    CHECK(A::filename("no_parking")          == "no_parking.wav");
    CHECK(A::filename("Pedestrian_crossing") == "Pedestrian_Crossing.wav");
    CHECK(A::filename("School_Zone")         == "School_Zone.wav");
    CHECK(A::filename("sign_four_way")       == "sign_four_way.wav");
    CHECK(A::filename("Traffic_sign")        == "Traffic_sign.wav");
}

static void test_unknown_and_speed_empty() {
    CHECK(A::filename("sign_50").empty());   // speed ไม่ใช่ momentary
    CHECK(A::filename("garbage").empty());
    CHECK(A::filename("").empty());
}

int main() {
    test_known_classes();
    test_unknown_and_speed_empty();
    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
