#include "decision/ShadowSpeedLimitPipeline.h"

#include "inference/SpeedSignClassifier.h"   // speed_sign_group() — single source of truth
#include "audio/SpeedAudioMap.h"             // (action,value) -> ชื่อไฟล์ (เคยเรียกผ่าน nm_->notify)

#include <iostream>
#include <utility>   // std::move

ShadowSpeedLimitPipeline::ShadowSpeedLimitPipeline(int k,
                                                   Millis rearm_after,
                                                   Millis reminder_cooldown,
                                                   bool   verbose,
                                                   NotificationManager* nm,
                                                   NotificationArbiter* arb)
    : l1_(rearm_after),
      l2_(k),
      l3_(reminder_cooldown),
      verbose_(verbose),
      nm_(nm),
      arb_(arb) {}

namespace {
// Speed attention ranks (จาก attention scale รวม; ทั้งคู่ < INTERRUPT_THRESHOLD(20)
//   → speed ไม่มีวัน preempt ใคร: persistent state re-derive ได้). PROVISIONAL — bench-tune.
constexpr int SPEED_RANK_CHANGE   = 12;
constexpr int SPEED_RANK_REMINDER = 2;

int speed_rank(AnnouncementPolicy::Action a) {
    // มาถึงนี่เฉพาะ announce action (Change/Reminder); อื่น ๆ ไม่ส่งเสียงอยู่แล้ว
    return a == AnnouncementPolicy::Action::Change ? SPEED_RANK_CHANGE : SPEED_RANK_REMINDER;
}
}  // namespace

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
    //   route ผ่าน Arbiter (cross-brain) ก่อนถึง L4 → speed/momentary ตัดสินด้วย rank ร่วมกัน
    //   ไม่ใช่ last-wins. speed rank < 20 → Arbiter จะไม่ให้ speed preempt safety ที่เล่นอยู่
    //   (ถูก DROP) — ถูกต้องตามดีไซน์. no-op ถ้า --audio ปิด (nm_->enabled_=false).
    const char* arb_str = "-";   // ผล arbiter สำหรับ log (ช่วย diagnose reminder/preempt)
    if (announce && l2_.current()) {
        const std::string file = SpeedAudioMap::filename(action, *l2_.current());
        if (arb_) {
            // CHANGE = re-deliverable (ข้อมูลใหม่หายแล้วหายเลย); REMINDER = ไม่ (รู้อยู่แล้ว)
            const bool eligible = (action == AnnouncementPolicy::Action::Change);
            const auto ar = arb_->submit(speed_rank(action), file, now, eligible);
            arb_str = ar.decision == NotificationArbiter::Decision::Play    ? "PLAY"    :
                      ar.decision == NotificationArbiter::Decision::Preempt ? "PREEMPT" : "DROP";
            // Play = ต่อคิว, Preempt = ตัดคลิปเก่า. (speed rank < 20 → ปกติได้แค่ Play/Drop
            //   แต่ route ตาม decision ไว้เพื่อความถูกต้องเชิงโครงสร้าง)
            if (nm_) {
                if      (ar.decision == NotificationArbiter::Decision::Play)    nm_->submit(ar.filename);
                else if (ar.decision == NotificationArbiter::Decision::Preempt) nm_->preempt(ar.filename);
            }
        } else if (nm_) {
            arb_str = "DIRECT";
            nm_->submit(file);   // ไม่มี arbiter wired → ส่งตรง (พฤติกรรมเดิม)
        }
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
                  << " arb=" << arb_str
                  << " F" << frame_index << "\n" << std::flush;
    }
}

void ShadowSpeedLimitPipeline::redeliver(TimePoint now) {
    // re-deliver CHANGE ที่ถูก safety ตัดกลางคัน — Law 4: re-derive จาก belief ปัจจุบันของ L2
    //   (ไม่ replay ค่าเก่าที่เก็บไว้ → ถ้า belief ขยับ 60→80 ระหว่างรอ จะพูด 80 ที่ถูกต้อง)
    // NOTE: ไม่แตะ L3 — timer ถูก stamp ตอน CHANGE เดิมถูก "ตัดสิน" แล้ว (stamp-at-decision).
    //   ต่างจากที่เคยล็อกไว้ว่า stamp-at-completion แต่ห่างกัน ~1 คลิป (≈2s) เทียบ cooldown 180s
    //   = negligible → ยังไม่รื้อ L3 (ดู REVIEW). ถ้าต้องเป๊ะค่อยเพิ่มทีหลัง.
    if (!l2_.current()) return;                          // UNKNOWN → ไม่มีอะไรให้ redeliver
    const std::string file =
        SpeedAudioMap::filename(AnnouncementPolicy::Action::Change, *l2_.current());
    if (file.empty()) return;

    if (!arb_) { if (nm_) nm_->submit(file); return; }   // ไม่มี arbiter → ส่งตรง (พฤติกรรมเดิม)

    // eligible=true → ถ้า re-delivery เองถูก safety ตัดอีก ก็ค้างไว้ replay ต่อได้
    const auto ar = arb_->submit(SPEED_RANK_CHANGE, file, now, /*redeliver_eligible=*/true);
    if (nm_) {
        if      (ar.decision == NotificationArbiter::Decision::Play)    nm_->submit(ar.filename);
        else if (ar.decision == NotificationArbiter::Decision::Preempt) nm_->preempt(ar.filename);
    }
    if (verbose_) {
        std::cout << "[SHADOW][L3] REDELIVER CHANGE belief=" << *l2_.current()
                  << "\n" << std::flush;
    }
}
