#include "decision/NotificationArbiter.h"

#include "decision/MomentaryPolicy.h"   // INTERRUPT_THRESHOLD (single source of truth)

NotificationArbiter::NotificationArbiter(Millis nominal_clip)
    : nominal_clip_(nominal_clip) {}

void NotificationArbiter::take_channel(int rank, bool eligible, TimePoint now) {
    current_rank_     = rank;
    current_eligible_ = eligible;
    busy_until_       = now + nominal_clip_;
}

NotificationArbiter::Result
NotificationArbiter::submit(int rank, const std::string& filename, TimePoint now,
                            bool redeliver_eligible) {
    Result r;

    // ไม่มีคลิป → ไม่มีอะไรให้เล่น, ไม่ยึดช่อง (กัน "ยึดช่องเปล่า" บล็อก safety)
    if (filename.empty()) {
        r.decision = Decision::Drop;
        return r;
    }

    // SELECTION: ช่องว่าง → หยิบไปเล่น
    if (!busy(now)) {
        take_channel(rank, redeliver_eligible, now);
        r.decision = Decision::Play;
        r.filename = filename;
        return r;
    }

    // PREEMPTION: ช่องไม่ว่าง → แทรกได้เฉพาะ rank สูงกว่า "และ" ถึงเกณฑ์ safety
    if (rank > current_rank_ && rank >= MomentaryPolicy::INTERRUPT_THRESHOLD) {
        // คลิปเก่าที่กำลังถูกตัด ถ้าเป็น CHANGE (eligible) → จดไว้ให้ re-deliver เมื่อช่องว่าง
        if (current_eligible_) redeliver_owed_ = true;
        take_channel(rank, redeliver_eligible, now);
        r.decision = Decision::Preempt;
        r.filename = filename;
        return r;
    }

    // rank ต่ำกว่า/เท่ากัน หรือไม่ถึงเกณฑ์ interrupt → ทิ้ง (Law 4: ไม่มีคิว)
    r.decision = Decision::Drop;
    return r;
}

bool NotificationArbiter::poll(TimePoint now) {
    // L4 ว่างจริงแล้ว → sync ช่องเป็น idle ทันที (ไม่รอ scaffold busy_until_ หมดอายุ)
    current_rank_     = 0;
    current_eligible_ = false;
    busy_until_       = now;   // now >= busy_until_ → busy()==false → idle

    // มี CHANGE ถูก preempt ค้างไหม → คืน true + เคลียร์ธง (re-deliver ครั้งเดียว)
    if (redeliver_owed_) {
        redeliver_owed_ = false;
        return true;
    }
    return false;
}

void NotificationArbiter::reset() {
    current_rank_     = 0;
    current_eligible_ = false;
    redeliver_owed_   = false;
    busy_until_       = TimePoint{};
}
