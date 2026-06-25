#include "decision/AnnouncementPolicy.h"

#include <algorithm>   // std::max

AnnouncementPolicy::AnnouncementPolicy(Millis reminder_cooldown)
    : cooldown_(std::max(Millis::zero(), reminder_cooldown)) {}   // cooldown >= 0

bool AnnouncementPolicy::cooldown_elapsed(TimePoint now) const {
    // ยังไม่เคยประกาศ = ถือว่าครบ (อนุญาตให้ย้ำได้)
    return !last_announce_at_ || (now - *last_announce_at_) >= cooldown_;
}

AnnouncementPolicy::Action
AnnouncementPolicy::decide(bool changed, bool fresh, TimePoint now) {
    // ── row 1: changed -> Change ── ประกาศเสมอ, เด้ง cooldown, รีเซ็ต timer
    if (changed) {
        last_announce_at_ = now;
        return Action::Change;
    }

    // ── row 4: ไม่ changed + ไม่ fresh (ต่อเนื่อง) -> เงียบ ── ไม่แตะ timer
    if (!fresh) {
        return Action::SuppressContinuation;
    }

    // ── row 2/3: ไม่ changed + fresh (เจอค่าเดิมซ้ำหลัง gap) ── gate ด้วย cooldown
    if (cooldown_elapsed(now)) {
        last_announce_at_ = now;          // ย้ำเตือนนับเป็นการประกาศ -> รีเซ็ต timer
        return Action::Reminder;
    }
    return Action::SuppressCooldown;
}

void AnnouncementPolicy::reset() {
    last_announce_at_.reset();
}
