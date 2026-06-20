#pragma once

// ============================================================
// NotificationArbiter — stateful cross-brain attention scheduler (FROZEN 2026-06-15)
//
//   ทรัพยากรที่แย่งกันคือ "ช่องความสนใจของคนขับ" (พูดได้ทีละเสียง) ไม่ใช่ CPU.
//   นั่งระหว่าง 2 สมอง (Speed PersistentState + Momentary) กับ shared L4:
//
//     submit(rank, filename, now):
//        idle (ช่องว่าง)                 → PLAY     (SELECTION: หยิบช่องไปเลย)
//        busy (มีเสียงเล่นอยู่):
//            rank > current AND rank >= INTERRUPT_THRESHOLD → PREEMPT
//            มิฉะนั้น                                       → DROP   (Law 4: ไม่มีคิว ทิ้งของเก่า)
//
//   - rank > current แบบ strict → เสียง rank เท่ากันไม่แทรกตัวเอง (กัน spam)
//   - rank >= INTERRUPT_THRESHOLD(=20, Pedestrian_crossing) → เฉพาะ safety แทรกได้
//       ⟺ "interrupt ได้" = "เป็น life-safety"  → re-derive Law 2
//   - speed ranks (Change 12 / Reminder 2) < 20 → speed ไม่มีวัน preempt ใคร (re-derivable)
//
// แกนเดียว (attention_rank) บังคับ selection-order = interrupt-order → ไม่มีทาง incoherent.
//
// pure logic: คืนแค่ {decision, filename} — ไม่แตะ L4 เอง. caller เป็นคนเรียก nm.submit
//   เมื่อ decision != Drop (ปรัชญาเดียวกับ MomentaryEngine: engine คิด, main ลงมือ)
//   → unit-test ด้วย g++ ล้วน, ไม่ผูก thread/aplay.
//
// busy_until_ = SCAFFOLD ชั่วคราว: ตอนนี้ยังไม่มี feedback จริงจาก L4 ว่า aplay เล่นจบเมื่อไหร่
//   (= refactor #3). interim ใช้ประมาณความยาวคลิป (nominal_clip). พอ #3 มา แทนด้วย event
//   "playback started/finished" จริง — กฎ PLAY/PREEMPT/DROP ด้านบน "ไม่เปลี่ยน".
//
// เรียกจาก main/decision thread เท่านั้น → ไม่ต้อง lock.
// ============================================================

#include <chrono>
#include <string>

class NotificationArbiter {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Millis    = std::chrono::milliseconds;

    enum class Decision { Play, Preempt, Drop };

    struct Result {
        Decision    decision = Decision::Drop;
        std::string filename;   // ส่งต่อให้ L4; ว่างเมื่อ Drop (หรือ filename ที่เข้ามาว่าง)
    };

    // nominal_clip = ความยาวคลิปโดยประมาณ (scaffold busy-window; bench-tunable, #3 จะแทนด้วย event จริง)
    explicit NotificationArbiter(Millis nominal_clip = Millis(2500));

    // ตัดสิน PLAY/PREEMPT/DROP. filename ว่าง → Drop ทันที ไม่แตะ state (ไม่มีคลิปก็ไม่ยึดช่อง)
    //   redeliver_eligible = true เฉพาะ speed CHANGE → ถ้าคลิปนี้ถูก preempt ภายหลัง จะถูกจดไว้
    //   ให้ re-deliver (replay) เมื่อช่องว่าง. อื่น ๆ (REMINDER / momentary) = false.
    Result submit(int rank, const std::string& filename, TimePoint now,
                  bool redeliver_eligible = false);

    // เรียกจาก main ทุกเฟรม "เมื่อ L4 ว่างจริง" (clip เล่นจบ/ถูกฆ่าแล้ว) → sync Arbiter เป็น idle.
    //   คืน true ถ้ามี CHANGE ค้างต้อง re-deliver (พร้อมเคลียร์ธง) → caller สั่ง pipeline.redeliver
    bool poll(TimePoint now);

    static bool plays(Decision d) { return d == Decision::Play || d == Decision::Preempt; }

    // telemetry / test helpers
    bool busy(TimePoint now) const { return now < busy_until_; }
    int  current_rank() const { return current_rank_; }
    bool redelivery_owed() const { return redeliver_owed_; }

    void reset();   // คืนสู่ idle (ลืม current + ธง re-deliver)

private:
    void take_channel(int rank, bool eligible, TimePoint now);  // ยึดช่อง: current_rank_/eligible/busy_until_

    const Millis nominal_clip_;
    int          current_rank_     = 0;
    bool         current_eligible_ = false;  // คลิปที่ครองช่องตอนนี้ re-deliver ได้ไหม (speed CHANGE)
    bool         redeliver_owed_   = false;  // มี CHANGE ถูก preempt ค้างรอ replay
    TimePoint    busy_until_{};   // idle เมื่อ now >= busy_until_ (ค่า default = epoch = idle ตั้งต้น)
};
