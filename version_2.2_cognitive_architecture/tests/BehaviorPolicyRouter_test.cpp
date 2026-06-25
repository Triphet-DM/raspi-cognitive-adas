// ============================================================
// Unit test — BehaviorPolicyRouter  (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/MomentaryPolicy.cpp
//       src/decision/BehaviorPolicyRouter.cpp
//       tests/BehaviorPolicyRouter_test.cpp -o router_test
//   ./router_test      # exit 0 = ผ่านหมด
// ============================================================

#include "decision/BehaviorPolicyRouter.h"

#include <iostream>

using R     = BehaviorPolicyRouter;
using Brain = R::Brain;

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

static void test_speed_routes_to_speed() {
    CHECK(R::route("sign_50")  == Brain::Speed);
    CHECK(R::route("sign_60")  == Brain::Speed);
    CHECK(R::route("sign_80")  == Brain::Speed);
    CHECK(R::route("sign_90")  == Brain::Speed);
    CHECK(R::route("sign_100") == Brain::Speed);
}

static void test_momentary_routes_to_momentary() {
    CHECK(R::route("Pedestrian_crossing")     == Brain::Momentary);
    CHECK(R::route("Pedestrian_Warning_Sign") == Brain::Momentary);
    CHECK(R::route("School_Zone")             == Brain::Momentary);
    CHECK(R::route("curve_ahead")             == Brain::Momentary);
    CHECK(R::route("sign_four_way")           == Brain::Momentary);
    CHECK(R::route("Traffic_sign")            == Brain::Momentary);
    CHECK(R::route("no_parking")              == Brain::Momentary);
    CHECK(R::route("no_u_turn")               == Brain::Momentary);
    CHECK(R::route("no_stop")                 == Brain::Momentary);
    CHECK(R::route("no_passing")              == Brain::Momentary);
}

static void test_unknown_routes_to_none() {
    CHECK(R::route("garbage")  == Brain::None);
    CHECK(R::route("")         == Brain::None);
    CHECK(R::route("sign_70")  == Brain::None);   // ไม่อยู่ใน speed set
}

int main() {
    test_speed_routes_to_speed();
    test_momentary_routes_to_momentary();
    test_unknown_routes_to_none();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
