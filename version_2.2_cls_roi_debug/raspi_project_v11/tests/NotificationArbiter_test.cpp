// ============================================================
// Unit test — NotificationArbiter (framework-free)
//
// build & run (จาก raspi_project_v11/):
//   g++ -std=c++17 -I src src/decision/NotificationArbiter.cpp
//       tests/NotificationArbiter_test.cpp -o arb_test
//   ./arb_test          # exit 0 = ผ่านหมด
//
// ครอบคลุม: SELECTION (idle->play) · empty-file drop · busy-drop (rank ต่ำ/เท่า) ·
//           below-threshold ไม่แทรก (speed) · PREEMPTION (safety) · safety ไม่แทรก safety สูงกว่า ·
//           busy-window หมดอายุ -> idle · preempt รีเซ็ต window · reset · custom nominal_clip
//
//   ranks อ้างจาก MomentaryPolicy จริง: School_Zone 30 · Ped_Warning 25 · Ped_crossing 20(=TH)
//                                       curve 10 · no_parking 4 · speed Change 12 · speed Reminder 2
// ============================================================

#include "decision/NotificationArbiter.h"
#include "decision/MomentaryPolicy.h"

#include <chrono>
#include <iostream>

using A   = NotificationArbiter;
using Dec = A::Decision;

// fake clock — time_point จาก "มิลลิวินาที" (arbiter ใช้แค่เทียบ now < busy_until)
static A::TimePoint t(int ms) {
    return A::Clock::time_point{} + std::chrono::milliseconds(ms);
}

static int g_run = 0, g_pass = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_run;                                                          \
        if (cond) { ++g_pass; }                                           \
        else { std::cout << "FAIL line " << __LINE__ << ": " #cond "\n"; }\
    } while (0)

// ทุกเทสต์ใช้ nominal_clip = 1000ms เพื่อให้เลขเวลาลงตัวที่ขอบวินาที (ยกเว้นเทสต์ default ท้ายสุด)
static A make() { return A(A::Millis(1000)); }

// ---- SELECTION (idle) ---------------------------------------------------

static void test_idle_plays() {
    A a = make();
    auto r = a.submit(20, "ped_crossing.wav", t(0));
    CHECK(r.decision == Dec::Play);
    CHECK(r.filename == "ped_crossing.wav");   // filename ส่งต่อให้ L4
    CHECK(a.busy(t(0)));                        // หลังรับงาน → ช่องไม่ว่าง
    CHECK(a.current_rank() == 20);
}

// filename ว่าง → Drop, ไม่ยึดช่อง (ไม่มีคลิปก็ไม่บล็อก safety ที่จะตามมา)
static void test_empty_filename_drop() {
    A a = make();
    auto r = a.submit(30, "", t(0));
    CHECK(r.decision == Dec::Drop);
    CHECK(r.filename.empty());
    CHECK(!a.busy(t(0)));   // ยังว่างอยู่
}

// ---- busy → DROP --------------------------------------------------------

// rank ต่ำกว่าที่กำลังเล่น → ทิ้ง (Law 4: ไม่มีคิว)
static void test_busy_lower_rank_drop() {
    A a = make();
    a.submit(30, "school.wav", t(0));                       // School_Zone กำลังเล่น
    auto r = a.submit(20, "ped_crossing.wav", t(200));      // ภายใน window
    CHECK(r.decision == Dec::Drop);
    CHECK(a.current_rank() == 30);                          // ของเดิมยังครองช่อง
}

// rank เท่ากัน → ไม่แทรกตัวเอง (strict >) → ทิ้ง = กัน spam ป้ายชนิดเดียวกันรัว ๆ
static void test_busy_equal_rank_drop() {
    A a = make();
    a.submit(20, "ped_crossing.wav", t(0));
    auto r = a.submit(20, "ped_crossing.wav", t(300));
    CHECK(r.decision == Dec::Drop);
}

// rank สูงกว่า แต่ "ต่ำกว่า threshold" → ห้ามแทรก (speed change 12 แทรก speed reminder 2 ไม่ได้)
//   → speed ไม่มีวัน preempt: persistent state re-derive ได้ ไม่ต้อง interrupt
static void test_busy_higher_but_below_threshold_drop() {
    A a = make();
    a.submit(2,  "reminder_50.wav", t(0));    // speed Reminder rank 2
    auto r = a.submit(12, "change_60.wav", t(300));  // speed Change rank 12 (>2 แต่ <20)
    CHECK(r.decision == Dec::Drop);
    CHECK(a.current_rank() == 2);
}

// speed (12) แทรก momentary ที่ไม่ใช่ safety (no_parking 4) ไม่ได้เช่นกัน — 12<20
static void test_speed_cannot_preempt_nonsafety() {
    A a = make();
    a.submit(4, "no_parking.wav", t(0));      // no_parking rank 4 กำลังเล่น
    auto r = a.submit(12, "change_60.wav", t(300));
    CHECK(r.decision == Dec::Drop);           // 12<20 → ไม่แทรก แม้ rank สูงกว่า
}

// ---- busy → PREEMPT (safety เท่านั้น) -----------------------------------

// safety (20) แทรกของที่ไม่ใช่ safety ได้ (เช่น speed change 12)
static void test_safety_preempts_speed() {
    A a = make();
    a.submit(12, "change_60.wav", t(0));               // speed Change กำลังเล่น
    auto r = a.submit(20, "ped_crossing.wav", t(300));  // Ped crossing = threshold
    CHECK(r.decision == Dec::Preempt);
    CHECK(r.filename == "ped_crossing.wav");
    CHECK(a.current_rank() == 20);                      // ช่องเปลี่ยนเจ้าของ
}

// safety สูงกว่า แทรก safety ต่ำกว่าได้ (School 30 แทรก Ped crossing 20)
static void test_higher_safety_preempts_lower_safety() {
    A a = make();
    a.submit(20, "ped_crossing.wav", t(0));
    auto r = a.submit(30, "school.wav", t(300));
    CHECK(r.decision == Dec::Preempt);
    CHECK(a.current_rank() == 30);
}

// safety ต่ำกว่า แทรก safety สูงกว่า "ไม่ได้" (Ped warning 25 แทรก School 30 ไม่ได้)
static void test_lower_safety_cannot_preempt_higher() {
    A a = make();
    a.submit(30, "school.wav", t(0));
    auto r = a.submit(25, "ped_warning.wav", t(300));   // 25<30 → ถึง threshold แต่ rank ไม่สูงพอ
    CHECK(r.decision == Dec::Drop);
    CHECK(a.current_rank() == 30);
}

// ---- busy-window อายุ ----------------------------------------------------

// พ้น window (>= nominal_clip) → ช่องว่างอีกครั้ง → submit ถัดไป "Play" แม้ rank ต่ำ
static void test_window_expires_then_play() {
    A a = make();   // nominal_clip = 1000ms
    a.submit(30, "school.wav", t(0));              // busy ถึง t(1000)
    CHECK(a.submit(4, "no_parking.wav", t(999)).decision == Dec::Drop);  // ยัง busy
    auto r = a.submit(4, "no_parking.wav", t(1000));  // 1000 >= 1000 → ว่างแล้ว
    CHECK(r.decision == Dec::Play);                // SELECTION อีกรอบ — rank ต่ำก็เล่นได้เพราะช่องว่าง
    CHECK(a.current_rank() == 4);
}

// preempt รีเซ็ต busy-window จากเวลาที่แทรก (ไม่ใช่ของคลิปเดิม)
static void test_preempt_resets_window() {
    A a = make();
    a.submit(12, "change.wav", t(0));              // busy เดิมถึง t(1000)
    a.submit(20, "ped_crossing.wav", t(500));      // preempt → busy ใหม่ถึง t(1500)
    CHECK(a.busy(t(1200)));                         // ถ้าไม่รีเซ็ต t(1200) จะว่างแล้ว (>1000)
    CHECK(!a.busy(t(1500)));                        // ว่างที่ 1500
}

// ---- reset / helpers / default ------------------------------------------

static void test_reset() {
    A a = make();
    a.submit(30, "school.wav", t(0));
    CHECK(a.busy(t(100)));
    a.reset();
    CHECK(!a.busy(t(100)));                         // กลับเป็น idle
    CHECK(a.current_rank() == 0);
    CHECK(a.submit(4, "no_parking.wav", t(100)).decision == Dec::Play);
}

static void test_plays_helper() {
    CHECK(A::plays(Dec::Play));
    CHECK(A::plays(Dec::Preempt));
    CHECK(!A::plays(Dec::Drop));
}

// default nominal_clip = 2500ms
static void test_default_nominal_clip() {
    A a;   // default ctor
    a.submit(30, "school.wav", t(0));
    CHECK(a.busy(t(2499)));
    CHECK(!a.busy(t(2500)));
}

// sanity: threshold ใน arbiter = ตัวเดียวกับ MomentaryPolicy (single source of truth)
static void test_threshold_is_shared() {
    CHECK(MomentaryPolicy::INTERRUPT_THRESHOLD == 20);
}

int main() {
    test_idle_plays();
    test_empty_filename_drop();
    test_busy_lower_rank_drop();
    test_busy_equal_rank_drop();
    test_busy_higher_but_below_threshold_drop();
    test_speed_cannot_preempt_nonsafety();
    test_safety_preempts_speed();
    test_higher_safety_preempts_lower_safety();
    test_lower_safety_cannot_preempt_higher();
    test_window_expires_then_play();
    test_preempt_resets_window();
    test_reset();
    test_plays_helper();
    test_default_nominal_clip();
    test_threshold_is_shared();

    std::cout << g_pass << "/" << g_run << " checks passed\n";
    return (g_pass == g_run) ? 0 : 1;
}
