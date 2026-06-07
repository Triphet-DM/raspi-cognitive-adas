#pragma once

// ============================================================
// SpeedSignLifecycle — Step 1 (โครงเปล่า ยังไม่ wire เข้า pipeline)
//
// เป้าหมาย Step 1: เพิ่ม data structures + คอมไพล์ผ่าน
//   - ไม่กระทบ run_decision / voter / cooldown
//   - update() เป็น stub (no-op) — คืน {fired=false} เสมอ
//   - state machine จริง + การถือครอง voter จะมาใน Step 2
//
// หมายเหตุ: ตั้งใจไม่อ้างถึง TemporalVoter / CooldownManager / Clock
// เพราะปัจจุบัน type เหล่านั้นนิยามอยู่ใน main.cpp (ไม่ใช่ header)
// การดึงออกมาเป็น header จะทำใน step ถัดไป
// ============================================================

#include <chrono>
#include <string>

// forward declarations — เลี่ยง include หนักใน header
struct BestROI;
struct StageTimes;
class  SpeedSignClassifier;

enum class EpisodeState {
    Armed,       // ยังไม่มี episode — รอสะสมหลักฐานเพื่อ confirm ครั้งแรก
    Confirmed,   // ยืนยันป้ายแล้ว ถือว่ายังเห็นอยู่ — ไม่ประกาศซ้ำ
    Releasing    // ป้ายหายจากเฟรม — กำลังนับ absence เพื่อ re-arm
};

struct ActiveEpisode {
    EpisodeState state = EpisodeState::Armed;

    std::string candidate_value;   // class จาก YOLO/voter = identity ของ episode
    std::string announced_value;   // ผลจาก classifier = label ที่ประกาศจริง

    std::chrono::steady_clock::time_point confirmed_at;  // valid only when state != Armed; set on entry edge
    std::chrono::steady_clock::time_point last_seen;     // valid only when state != Armed; set per detection
};

class SpeedSignLifecycle {
public:
    struct Announcement {
        bool        fired = false;   // true = มีการประกาศป้ายใหม่ในเฟรมนี้
        std::string value;           // ค่าที่ประกาศ (= announced_value)
    };

    explicit SpeedSignLifecycle(
        SpeedSignClassifier* classifier,
        std::chrono::milliseconds rearm_after        = std::chrono::milliseconds(600),
        std::chrono::milliseconds max_latch          = std::chrono::milliseconds(45000),
        std::chrono::milliseconds safety_refractory  = std::chrono::milliseconds(1000)
    );

    // Step 1: stub — คืน {fired=false} เสมอ, ไม่แตะ state, ไม่เรียก classifier
    // Step 2: implement state machine จริง แล้วเรียกแบบ shadow mode
    Announcement update(
        const std::string& speed_candidate,    // class ป้าย speed ที่ conf สูงสุดในเฟรม ("" = ไม่มี)
        const BestROI&     roi_for_candidate,  // best ROI ของ candidate (จาก roi_by_class)
        int                frame_index,
        StageTimes&        times
    );

    void reset();   // กลับสู่ Armed (ใช้ตอน re-arm / เริ่มใหม่)

    const ActiveEpisode& episode() const { return episode_; }

private:
    ActiveEpisode        episode_;
    SpeedSignClassifier* classifier_ = nullptr;   // borrow — ไม่ถือครอง (main เป็นเจ้าของ)

    std::chrono::milliseconds rearm_after_;
    std::chrono::milliseconds max_latch_;
    std::chrono::milliseconds safety_refractory_;

    std::chrono::steady_clock::time_point last_announce_;   // ใช้กับ safety_refractory (Step 2)
    bool has_announced_ = false;
};
