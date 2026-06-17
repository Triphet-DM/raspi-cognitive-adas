#include "decision/ShadowSpeedLimitPipeline.h"

#include "inference/SpeedSignClassifier.h"   // speed_sign_group() — single source of truth

#include <iostream>
#include <utility>   // std::move

ShadowSpeedLimitPipeline::ShadowSpeedLimitPipeline(int k,
                                                   Millis rearm_after,
                                                   Millis reminder_cooldown,
                                                   bool   verbose,
                                                   NotificationManager* nm)
    : l1_(rearm_after),
      l2_(k),
      l3_(reminder_cooldown),
      verbose_(verbose),
      nm_(nm) {}

bool ShadowSpeedLimitPipeline::is_speed(const std::string& cls) {
    return !cls.empty() &&
           SpeedSignClassifier::speed_sign_group().count(cls) > 0;
}

namespace {
const char* outcome_str(CurrentSpeedLimitManager::Outcome o) {
    using O = CurrentSpeedLimitManager::Outcome;
    switch (o) {
        case O::Acquire:   return "Acquire";
        case O::Reconfirm: return "Reconfirm";
        case O::Pending:   return "Pending";
        case O::Change:    return "Change";
    }
    return "?";
}
const char* action_str(AnnouncementPolicy::Action a) {
    using A = AnnouncementPolicy::Action;
    switch (a) {
        case A::Change:               return "CHANGE";
        case A::Reminder:             return "REMINDER";
        case A::SuppressCooldown:     return "SUPPRESS-CD";
        case A::SuppressContinuation: return "SUPPRESS-CONT";
    }
    return "?";
}
}  // namespace

void ShadowSpeedLimitPipeline::tick(bool presence,
                                    bool voter_confirmed,
                                    const std::string& confirmed_value,
                                    int frame_index,
                                    TimePoint now) {
    // gate: เฉพาะ voter confirm ที่เป็นค่า speed เท่านั้นที่นับเป็น L1.confirm
    const bool speed_confirmed = voter_confirmed && is_speed(confirmed_value);

    const auto ep = l1_.update(presence, speed_confirmed, confirmed_value, now);
    if (!ep.fired) return;   // L1 ticks ทุกเฟรม แต่ L2/L3 รันเฉพาะตอน emit

    // age ก่อน onValue = ความเก่าของ belief ตอนหลักฐานใหม่มาถึง (telemetry STALE)
    const Millis age_before = l2_.current() ? l2_.age(now) : Millis(0);

    const auto outcome = l2_.onValue(ep.value, now);
    const bool changed = CurrentSpeedLimitManager::is_change(outcome);
    const auto action  = l3_.decide(changed, ep.fresh, now);
    const bool announce = AnnouncementPolicy::is_announce(action);

    // L4 (shared, external): ส่งเสียงเฉพาะ announce action (Change/Reminder); Suppress -> เงียบ
    //   no-op ถ้า --audio ปิด (nm_->enabled_=false) หรือไม่มี sink (nm_==nullptr).
    //   value = belief ปัจจุบันของ L2
    if (announce && l2_.current() && nm_) {
        nm_->notify(action, *l2_.current());
    }

    // ประกาศจริง (CHANGE/REMINDER) log เสมอ; SUPPRESS log เฉพาะ --shadow-verbose
    if (announce || verbose_) {
        const std::string belief = l2_.current() ? *l2_.current() : "UNKNOWN";
        std::cout << "[SHADOW][L3] " << action_str(action)
                  << " value=" << ep.value
                  << " (L2=" << outcome_str(outcome)
                  << ", fresh=" << (ep.fresh ? "T" : "F")
                  << ", belief=" << belief
                  << ", age=" << age_before.count() << "ms)"
                  << " F" << frame_index << "\n" << std::flush;
    }
}
