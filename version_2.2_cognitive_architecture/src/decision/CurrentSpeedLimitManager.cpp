#include "decision/CurrentSpeedLimitManager.h"

#include <algorithm>   // std::max

CurrentSpeedLimitManager::CurrentSpeedLimitManager(int k)
    : k_(std::max(1, k)) {}   // K อย่างน้อย 1

void CurrentSpeedLimitManager::clear_pending() {
    pending_value_.clear();
    pending_count_ = 0;
}

CurrentSpeedLimitManager::Outcome
CurrentSpeedLimitManager::onValue(const std::string& value, TimePoint now) {
    // ── row 1: UNKNOWN -> ACTIVE(V) ── acquisition, ไม่ใช้ K
    if (!current_) {
        current_           = value;
        last_confirmed_at_ = now;
        clear_pending();
        return Outcome::Acquire;
    }

    // ── row 2: ค่าเดิมยืนยันซ้ำ ── refresh + clear pending, ไม่ changed
    if (value == *current_) {
        last_confirmed_at_ = now;
        clear_pending();                 // เห็นค่าเดิม = ล้มล้าง challenge ที่ค้างอยู่
        return Outcome::Reconfirm;
    }

    // ── row 3/4: ค่าต่าง ── streak update ก่อน แล้วเช็ค K
    if (value == pending_value_) {
        ++pending_count_;                // ค่าเดิมที่ challenge อยู่ → นับต่อ
    } else {
        pending_value_ = value;          // ค่าใหม่ (รวมค่าที่สาม) → เริ่มนับใหม่
        pending_count_ = 1;
    }

    if (pending_count_ >= k_) {
        current_           = value;      // commit ค่าใหม่
        last_confirmed_at_ = now;
        clear_pending();
        return Outcome::Change;
    }
    return Outcome::Pending;             // ยัง challenge อยู่ ยังไม่เปลี่ยน belief
}

CurrentSpeedLimitManager::Millis
CurrentSpeedLimitManager::age(TimePoint now) const {
    return std::chrono::duration_cast<Millis>(now - last_confirmed_at_);
}

void CurrentSpeedLimitManager::reset() {
    current_.reset();
    last_confirmed_at_ = {};
    clear_pending();
}
