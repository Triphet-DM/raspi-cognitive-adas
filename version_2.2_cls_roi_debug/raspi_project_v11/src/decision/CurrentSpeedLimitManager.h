#pragma once

// ============================================================
// L2 — CurrentSpeedLimitManager
//
// ถือ "belief" ของลิมิตความเร็วปัจจุบัน (state estimation)
//   - value authority = CLS output ที่ป้อนเข้ามาผ่าน onValue()
//   - UNKNOWN / ACTIVE(value)   (UNKNOWN = current_ == nullopt)
//   - no-forget: ไม่มีเส้นกลับ UNKNOWN (camera-only ไม่รู้ว่า "ไม่มีลิมิต")
//   - K-hysteresis: ค่าใหม่ที่ต่างต้องยืนยันติดกัน K ครั้งก่อน commit
//                   (acquisition ครั้งแรกไม่ใช้ K)
//
// pure logic — ไม่มี dependency, ไม่มี I/O, unit-testable ตรง ๆ
// การ log ([SHADOW][L2] ...) เป็นหน้าที่ของ facade (ดู Outcome)
// ============================================================

#include <chrono>
#include <optional>
#include <string>

class CurrentSpeedLimitManager {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Millis    = std::chrono::milliseconds;

    // ผลของ onValue() — ตรงกับ 4 แถวของ L2 Transition Table
    enum class Outcome {
        Acquire,    // UNKNOWN -> ACTIVE(V)                  (changed)
        Reconfirm,  // ACTIVE(V), V == current               (ไม่ changed)
        Pending,    // ACTIVE(V), V != current, ยังไม่ครบ K   (ไม่ changed)
        Change      // ACTIVE(V), V != current, ครบ K → commit (changed)
    };

    // K < 1 จะถูก clamp เป็น 1 (K<=0 ไม่มีความหมาย)
    explicit CurrentSpeedLimitManager(int k = 1);

    // ป้อนค่าจาก EpisodeConfirmed.value (CLS-authoritative)
    // precondition: value ต้องไม่ว่าง — facade รับประกัน (เรียกเฉพาะตอน L1 emit)
    Outcome onValue(const std::string& value, TimePoint now);

    // belief ปัจจุบัน — nullopt = UNKNOWN
    const std::optional<std::string>& current() const { return current_; }

    // อายุนับจาก confirm ล่าสุด (telemetry สำหรับ STALE ในอนาคต)
    // meaningful เฉพาะตอน ACTIVE (current() มีค่า)
    Millis age(TimePoint now) const;

    int  k() const { return k_; }
    void reset();   // กลับสู่ UNKNOWN + ล้าง pending (เริ่มใหม่)

    // outcome ไหนนับเป็น "limit changed" (ใช้ป้อน L3 + เลือก log)
    static bool is_change(Outcome o) {
        return o == Outcome::Acquire || o == Outcome::Change;
    }

private:
    void clear_pending();

    int k_;
    std::optional<std::string> current_;          // nullopt = UNKNOWN
    TimePoint                  last_confirmed_at_{};
    std::string                pending_value_;
    int                        pending_count_ = 0;
};
