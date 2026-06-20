#include "decision/NotificationArbiter.h"

#include "decision/MomentaryPolicy.h"   // INTERRUPT_THRESHOLD (single source of truth)

NotificationArbiter::NotificationArbiter(Millis nominal_clip)
    : nominal_clip_(nominal_clip) {}

void NotificationArbiter::take_channel(int rank, TimePoint now) {
    current_rank_ = rank;
    busy_until_   = now + nominal_clip_;
}

NotificationArbiter::Result
NotificationArbiter::submit(int rank, const std::string& filename, TimePoint now) {
    Result r;

    // ไม่มีคลิป → ไม่มีอะไรให้เล่น, ไม่ยึดช่อง (กัน "ยึดช่องเปล่า" บล็อก safety)
    if (filename.empty()) {
        r.decision = Decision::Drop;
        return r;
    }

    // SELECTION: ช่องว่าง → หยิบไปเล่น
    if (!busy(now)) {
        take_channel(rank, now);
        r.decision = Decision::Play;
        r.filename = filename;
        return r;
    }

    // PREEMPTION: ช่องไม่ว่าง → แทรกได้เฉพาะ rank สูงกว่า "และ" ถึงเกณฑ์ safety
    if (rank > current_rank_ && rank >= MomentaryPolicy::INTERRUPT_THRESHOLD) {
        take_channel(rank, now);
        r.decision = Decision::Preempt;
        r.filename = filename;
        return r;
    }

    // rank ต่ำกว่า/เท่ากัน หรือไม่ถึงเกณฑ์ interrupt → ทิ้ง (Law 4: ไม่มีคิว)
    r.decision = Decision::Drop;
    return r;
}

void NotificationArbiter::reset() {
    current_rank_ = 0;
    busy_until_   = TimePoint{};
}
