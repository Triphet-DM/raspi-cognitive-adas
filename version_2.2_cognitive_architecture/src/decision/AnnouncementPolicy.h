#pragma once

// ============================================================
// L3 — AnnouncementPolicy
//
// ตัดสิน "จะประกาศอะไร" จาก belief ของ L2 + episode ของ L1
//   อินพุต 3 ตัว (ตรงกับ L3 Decision Table):
//     - changed : L2 เปลี่ยน belief ไหม (= CurrentSpeedLimitManager::is_change)
//     - fresh   : L1 บอกว่าเป็น episode ใหม่หลัง presence-gap ไหม
//     - now     : เวลา (ใช้กับ reminder cooldown)
//
//   4 ผลลัพธ์ (4 แถวของตาราง):
//     1) changed                      -> Change               (เด้ง cooldown เสมอ)
//     2) !changed & fresh & elapsed   -> Reminder
//     3) !changed & fresh & !elapsed  -> SuppressCooldown
//     4) !changed & !fresh            -> SuppressContinuation  (ต่อเนื่อง = เงียบ)
//
//   reminder cooldown = global single timestamp (v1) — ทั้ง Change และ Reminder
//   นับเป็น "การประกาศล่าสุด" จึงรีเซ็ต timer ทั้งคู่ (Change ก็รีเซ็ตด้วย)
//
// pure logic — ไม่มี dependency, ไม่มี I/O, ไม่มี log ข้างใน (เป็นหน้าที่ facade)
// ============================================================

#include <chrono>
#include <optional>

class AnnouncementPolicy {
public:
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Millis    = std::chrono::milliseconds;

    // ผลของ decide() — ตรงกับ 4 แถวของ L3 Decision Table
    enum class Action {
        Change,                 // ประกาศลิมิตใหม่ (เด้ง cooldown เสมอ)
        Reminder,               // ค่าเดิม, episode ใหม่, cooldown ครบ -> ย้ำเตือน
        SuppressCooldown,       // ค่าเดิม, episode ใหม่, cooldown ยังไม่ครบ -> เงียบ
        SuppressContinuation    // ค่าเดิม, ต่อเนื่อง (ไม่ fresh) -> เงียบ
    };

    // reminder_cooldown < 0 จะถูก clamp เป็น 0 (0 = ครบเสมอ -> ย้ำทุก fresh episode)
    // ไม่มี default โดยตั้งใจ: ค่า benign ของ cooldown เป็นเรื่องของ wiring step
    // (ต่างจาก L2 ที่ K=1 เป็น degenerate default ที่มีความหมายในตัว; cooldown ไม่มี)
    explicit AnnouncementPolicy(Millis reminder_cooldown);

    // ตัดสิน action + อัปเดต cooldown timer (facade เรียกเฉพาะตอน L1 emit)
    Action decide(bool changed, bool fresh, TimePoint now);

    Millis cooldown() const { return cooldown_; }
    void   reset();   // ล้าง timer (เหมือนยังไม่เคยประกาศ)

    // action ไหนคือ "ประกาศจริง" (ใช้ฝั่ง facade เลือก log / ต่อ L4 ในอนาคต)
    static bool is_announce(Action a) {
        return a == Action::Change || a == Action::Reminder;
    }

private:
    bool cooldown_elapsed(TimePoint now) const;

    Millis                   cooldown_;
    std::optional<TimePoint> last_announce_at_;   // nullopt = ยังไม่เคยประกาศ (= ครบ)
};
