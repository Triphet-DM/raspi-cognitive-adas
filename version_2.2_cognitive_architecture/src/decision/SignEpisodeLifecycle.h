#pragma once

// ============================================================
// L1 — SignEpisodeLifecycle
//
// แบ่ง "episode" ของการเห็นป้าย speed จาก presence (YOLO = presence/ROI authority)
//   state: Armed / Confirmed / Releasing
//   - presence : เฟรมนี้เห็นป้าย speed ใด ๆ ไหม (class-agnostic, RAW — ไม่ผ่าน cooldown)
//   - confirm  : voter ยืนยันค่า speed แล้ว (facade gate is_speed มาก่อนเรียก)
//   - value    : ค่า CLS ที่แนบไปกับ confirm — L1 แค่ pass-through (value-blind)
//
//   fresh = TRUE เฉพาะ Armed->Confirmed (หลัง presence gap / re-arm) เท่านั้น
//   re-confirm ระหว่าง presence ต่อเนื่อง = fresh=FALSE (รวมถึง gapless value change:
//   L1 แค่รายงาน, L2 เป็นคนตัดสินว่า "เปลี่ยน")
//   re-arm เมื่อ now - last_seen >= rearm_after
//
// pure logic — ไม่มี classifier/voter dependency, ไม่มี I/O, ไม่มี log ข้างใน
//   (ต่างจาก SpeedSignLifecycle เดิมที่ identity = voter sub-class; อันนั้นถูกแทนแล้ว)
//   State / EpisodeConfirmed nest อยู่ในคลาส เพื่อไม่ชนกับ EpisodeState เดิม
// ============================================================

#include <chrono>
#include <string>

class SignEpisodeLifecycle {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Millis    = std::chrono::milliseconds;

    enum class State { Armed, Confirmed, Releasing };

    // ผลของ update() — ส่งต่อให้ L2 เมื่อ fired
    struct EpisodeConfirmed {
        bool        fired = false;   // เฟรมนี้มี confirm event ไหม
        std::string value;           // ค่า (CLS) ที่แนบมา — pass-through
        bool        fresh = false;   // TRUE เฉพาะ Armed->Confirmed (หลัง gap)
    };

    // rearm_after < 0 จะถูก clamp เป็น 0 (0 = absence เฟรมเดียวก็ re-arm ทันที)
    explicit SignEpisodeLifecycle(Millis rearm_after = Millis(600));

    EpisodeConfirmed update(bool presence, bool confirm,
                            const std::string& value, TimePoint now);

    State  state() const { return state_; }
    Millis rearm_after() const { return rearm_after_; }

    // hard restart -> Armed. ห้าม wire กับ presence loss (จะละเมิด no-forget ของ L2)
    void reset();

private:
    State     state_ = State::Armed;
    Millis    rearm_after_;
    TimePoint last_seen_{};   // เวลา presence ล่าสุด — ใช้คำนวณ re-arm
};
