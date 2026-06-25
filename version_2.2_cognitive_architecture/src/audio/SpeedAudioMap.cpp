#include "audio/SpeedAudioMap.h"

#include <unordered_set>

std::string SpeedAudioMap::speed_number(const std::string& value) {
    static const std::unordered_set<std::string> known = {
        "sign_50", "sign_60", "sign_80", "sign_90", "sign_100"
    };
    if (known.count(value) == 0) return {};
    return value.substr(5);   // ตัด prefix "sign_" (5 ตัวอักษร)
}

std::string SpeedAudioMap::filename(Action action, const std::string& value) {
    std::string prefix;
    switch (action) {
        case Action::Change:   prefix = "change_";   break;
        case Action::Reminder: prefix = "reminder_"; break;
        default:               return {};            // SuppressX -> ไม่มีคลิป
    }

    const std::string num = speed_number(value);
    if (num.empty()) return {};                      // ค่าไม่รู้จัก -> ไม่มีคลิป

    return prefix + num + ".wav";
}
