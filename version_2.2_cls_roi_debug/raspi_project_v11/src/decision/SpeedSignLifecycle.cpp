#include "decision/SpeedSignLifecycle.h"

#include "inference/SpeedSignClassifier.h"   // SpeedSignClassifier (definition)

#include <iostream>

namespace {
// speed sign เท่านั้นที่เข้า lifecycle (sign_50/60/80/90/100)
bool is_speed(const std::string& cls) {
    return !cls.empty() &&
           SpeedSignClassifier::speed_sign_group().count(cls) > 0;
}
}  // namespace

SpeedSignLifecycle::SpeedSignLifecycle(
    SpeedSignClassifier* classifier,
    std::chrono::milliseconds rearm_after,
    std::chrono::milliseconds max_latch,
    std::chrono::milliseconds safety_refractory
)
    : classifier_(classifier),
      rearm_after_(rearm_after),
      max_latch_(max_latch),
      safety_refractory_(safety_refractory) {}

// ── Step 2: shadow state machine ────────────────────────────
// SHADOW ONLY: track Armed/Confirmed/Releasing + log [LC-SHADOW]
// ไม่เรียก classifier, ไม่กระทบการตัดสินใจ (caller ทิ้งค่า return)
SpeedSignLifecycle::Announcement SpeedSignLifecycle::update(
    const std::string& top_class,
    bool               detection_suppressed,
    bool               voter_confirmed,
    const std::string& voter_winner,
    int                frame_index
) {
    const auto now = std::chrono::steady_clock::now();
    Announcement ann;   // fired=false

    // presence signal: ให้สอดคล้องกับ input ของ voter ที่มีอยู่
    // (suppressed → ถือว่าไม่เห็น, เหมือน vote_input="")
    const std::string speed_candidate =
        (!detection_suppressed && is_speed(top_class)) ? top_class : std::string{};
    const bool speed_confirmed = voter_confirmed && is_speed(voter_winner);

    // ── (1) confirmation event ──────────────────────────────
    if (speed_confirmed) {
        const bool new_episode =
            (episode_.state != EpisodeState::Confirmed) ||
            (voter_winner != episode_.candidate_value);

        if (new_episode) {
            const std::string prev = episode_.candidate_value;
            const bool value_change =
                (episode_.state == EpisodeState::Confirmed) && (voter_winner != prev);

            if (value_change) {
                std::cout << "[LC-SHADOW] FIRE " << voter_winner
                          << " (value-change " << prev << "->" << voter_winner
                          << ", prev suppressed=" << suppressed_count_ << ")"
                          << " F" << frame_index << "\n" << std::flush;
            } else {
                std::cout << "[LC-SHADOW] FIRE " << voter_winner
                          << " (new episode)"
                          << " F" << frame_index << "\n" << std::flush;
            }

            episode_.state           = EpisodeState::Confirmed;
            episode_.candidate_value = voter_winner;
            episode_.announced_value = voter_winner;   // shadow: ไม่เรียก classifier
            episode_.confirmed_at    = now;
            episode_.last_seen       = now;
            last_announce_           = now;
            has_announced_           = true;
            suppressed_count_        = 0;              // reset สำหรับ episode ใหม่

            ann.fired = true;
            ann.value = voter_winner;
        } else {
            // candidate เดิมยัง Confirmed → ป้ายเดิม ไม่ประกาศซ้ำ
            episode_.last_seen = now;
            ++suppressed_count_;
            if (verbose_) {   // เงียบโดย default — เปิดด้วย --lc-verbose
                std::cout << "[LC-SHADOW] SUPPRESS repeat " << episode_.candidate_value
                          << " (#" << suppressed_count_ << ")"
                          << " F" << frame_index << "\n" << std::flush;
            }
        }
        return ann;
    }

    // ── (2) presence / absence (เฉพาะเมื่อมี episode) ────────
    if (episode_.state == EpisodeState::Confirmed ||
        episode_.state == EpisodeState::Releasing) {

        if (!speed_candidate.empty() && speed_candidate == episode_.candidate_value) {
            episode_.last_seen = now;                  // ยังเห็นป้ายเดิม
            if (episode_.state == EpisodeState::Releasing) {
                episode_.state = EpisodeState::Confirmed;
                std::cout << "[LC-SHADOW] RE-SEEN " << episode_.candidate_value
                          << " F" << frame_index << "\n" << std::flush;
            }
        } else if (speed_candidate.empty()) {
            if (episode_.state == EpisodeState::Confirmed) {
                episode_.state = EpisodeState::Releasing;
                std::cout << "[LC-SHADOW] RELEASING " << episode_.candidate_value
                          << " F" << frame_index << "\n" << std::flush;
            }
            if (now - episode_.last_seen >= rearm_after_) {
                const std::string prev = episode_.candidate_value;
                std::cout << "[LC-SHADOW] RE-ARM (was " << prev
                          << ", suppressed=" << suppressed_count_ << ")"
                          << " F" << frame_index << "\n" << std::flush;
                episode_          = ActiveEpisode{};   // re-arm → Armed
                suppressed_count_ = 0;
            }
        }
        // else: เห็นป้าย speed คนละ class แต่ voter ยังไม่ confirm → รอ value-change
    }

    return ann;
}

void SpeedSignLifecycle::reset() {
    episode_       = ActiveEpisode{};
    has_announced_ = false;
}
