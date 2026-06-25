#pragma once

// ============================================================
// ShadowSpeedLimitPipeline — thin facade (Step 3)
//
// ถือ + เดินสาย L1 -> L2 -> L3 และทำ logging ([SHADOW][L3])
//   - L1 SignEpisodeLifecycle  : presence episode + fresh
//   - L2 CurrentSpeedLimitManager : belief + K-hysteresis + no-forget
//   - L3 AnnouncementPolicy    : CHANGE / REMINDER / SUPPRESS
//
// SHADOW ONLY: log อย่างเดียว ไม่มี authority, ไม่ทำ inference เพิ่ม
//   (ใช้ confirmed_value = CLS output ที่ run_decision คำนวณอยู่แล้ว)
//
// threading: tick() ต้องถูกเรียกจาก thread เดียว (main/decision thread).
//   run_decision รันบน main thread เสมอ (worker ทำแค่ run_detection)
//   -> L1/L2/L3 ไม่ต้องมี lock
//
// is_speed() ย้ายมาอยู่ที่ facade (gate ค่าที่ไม่ใช่ speed ออกก่อนป้อน L1.confirm)
// ============================================================

#include <chrono>
#include <string>

#include "decision/SignEpisodeLifecycle.h"
#include "decision/CurrentSpeedLimitManager.h"
#include "decision/AnnouncementPolicy.h"
#include "decision/NotificationArbiter.h"   // cross-brain attention scheduler (ก่อนถึง L4)
#include "audio/NotificationManager.h"      // L4: ส่งเสียงตาม announce action

class ShadowSpeedLimitPipeline {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Millis    = std::chrono::milliseconds;

    // nm = shared L4 (owned externally; one audio output for both brains via the Arbiter
    //      later). nullptr = no audio sink wired. The NotificationManager's own enabled_
    //      flag still decides no-op when --audio is off.
    //   arb = cross-brain Notification Arbiter (owned externally, shared with Brain 2).
    //         nullptr = ไม่ wire arbiter → speed ส่งตรงเข้า nm (พฤติกรรมเดิมก่อน Arbiter).
    ShadowSpeedLimitPipeline(int k,
                             Millis rearm_after,
                             Millis reminder_cooldown,
                             bool   verbose,
                             NotificationManager* nm,
                             NotificationArbiter* arb);

    // เรียกทุก processed frame (main thread เท่านั้น)
    //   presence        : เห็นป้าย speed ใด ๆ ในเฟรมนี้ไหม (RAW, class-agnostic)
    //   voter_confirmed : result.vote.confirmed
    //   confirmed_value : CLS-corrected output (อ่านเฉพาะตอน voter_confirmed)
    void tick(bool presence,
              bool voter_confirmed,
              const std::string& confirmed_value,
              int frame_index,
              TimePoint now);

    // re-deliver speed CHANGE ที่ถูก safety ตัดกลางคัน (เรียกจาก main เมื่อ Arbiter.poll คืน true).
    //   อ่าน belief ปัจจุบันจาก L2 → ประกาศ CHANGE ค่านั้น (Law 4: re-derive ไม่ replay ค่าเก่า)
    void redeliver(TimePoint now);

private:
    static bool is_speed(const std::string& cls);

    SignEpisodeLifecycle     l1_;
    CurrentSpeedLimitManager l2_;
    AnnouncementPolicy       l3_;
    bool                     verbose_;
    NotificationManager*     nm_;    // shared L4 — NOT owned (lives in main, fed by both brains)
    NotificationArbiter*     arb_;   // cross-brain scheduler — NOT owned (shared with Brain 2)
};
