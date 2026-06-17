#include "audio/MomentaryAudioMap.h"

#include <unordered_map>

// ============================================================
// ตาราง class -> wav (10 ป้าย non-speed). Diamond อัดเสียงให้ตรงชื่อไฟล์เหล่านี้
// แล้ววางใน assets/audio/ (โฟลเดอร์เดียวกับ speed wav). ไฟล์ที่ยังไม่มี -> เงียบ (ไม่ crash)
// ============================================================
namespace {
const std::unordered_map<std::string, std::string>& table() {
    static const std::unordered_map<std::string, std::string> t = {
        {"Pedestrian_crossing",     "Pedestrian_Crossing.wav"},
        {"Pedestrian_Warning_Sign", "Pedestrian_Warning.wav"},
        {"School_Zone",             "School_Zone.wav"},
        {"curve_ahead",             "curve_ahead.wav"},
        {"sign_four_way",           "sign_four_way.wav"},
        {"Traffic_sign",            "Traffic_sign.wav"},
        {"no_parking",              "no_parking.wav"},
        {"no_u_turn",               "no_u_turn.wav"},
        {"no_stop",                 "no_stop.wav"},
        {"no_passing",              "no_passing.wav"},
    };
    return t;
}
}  // namespace

std::string MomentaryAudioMap::filename(const std::string& cls) {
    const auto& t = table();
    auto it = t.find(cls);
    return it == t.end() ? std::string{} : it->second;
}
