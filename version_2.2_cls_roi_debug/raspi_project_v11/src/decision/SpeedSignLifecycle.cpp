#include "decision/SpeedSignLifecycle.h"

#include "utils/Types.h"                     // BestROI, StageTimes (definitions)
#include "inference/SpeedSignClassifier.h"   // SpeedSignClassifier (definition)

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

// ── Step 1 stub ─────────────────────────────────────────────
// ยังไม่ทำงานจริง: ไม่อ่าน/ไม่เขียน state, ไม่เรียก classifier
// คืน {fired=false} เพื่อให้ behavior ของ pipeline ไม่เปลี่ยน
// (ตอนนี้ยังไม่มี caller — logic จริงจะใส่ใน Step 2)
SpeedSignLifecycle::Announcement SpeedSignLifecycle::update(
    [[maybe_unused]] const std::string& speed_candidate,
    [[maybe_unused]] const BestROI&     roi_for_candidate,
    [[maybe_unused]] int                frame_index,
    [[maybe_unused]] StageTimes&        times
) {
    return Announcement{};   // {fired=false, value=""}
}

void SpeedSignLifecycle::reset() {
    episode_       = ActiveEpisode{};
    has_announced_ = false;
}
