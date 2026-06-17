#include "decision/BehaviorPolicyRouter.h"

#include <unordered_set>

#include "decision/MomentaryPolicy.h"

namespace {
// mirror ของ SpeedSignClassifier::speed_sign_group() — เก็บไว้เองเพื่อให้ router เป็น pure
// (ไม่ดึง ncnn/opencv) → unit-test ได้ด้วย g++ ล้วน. speed มี 5 คลาสคงที่.
bool is_speed(const std::string& cls) {
    static const std::unordered_set<std::string> kSpeed = {
        "sign_50", "sign_60", "sign_80", "sign_90", "sign_100",
    };
    return kSpeed.count(cls) > 0;
}
}  // namespace

BehaviorPolicyRouter::Brain BehaviorPolicyRouter::route(const std::string& cls) {
    if (MomentaryPolicy::is_momentary(cls)) return Brain::Momentary;
    if (is_speed(cls))                      return Brain::Speed;
    return Brain::None;
}
