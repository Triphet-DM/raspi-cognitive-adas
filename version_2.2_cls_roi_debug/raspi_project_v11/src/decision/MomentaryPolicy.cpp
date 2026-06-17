#include "decision/MomentaryPolicy.h"

#include <unordered_map>

// ============================================================
// ตาราง policy ของ 10 ป้าย non-speed (rank ตาม Board 1 · FROZEN structure)
//
//   tier        | rank | window | สมาชิก
//   ------------|------|--------|---------------------------------------
//   Safety      | 30   |  5 s   | School_Zone  (เสี่ยงสูงสุด: เด็ก+มุมอับรถรับส่ง)
//   (interrupt) | 25   |  5 s   | Pedestrian_Warning_Sign
//               | 20   |  5 s   | Pedestrian_crossing  ← = INTERRUPT_THRESHOLD (ต่ำสุดของ safety)
//   ------------|------|--------|---------------------------------------
//   re-ranked 2026-06-17 (Diamond): school zone เสี่ยงสูงสุดตามกฎหมาย/วิจัยไทย
//   Warning     | 10   | 15 s   | curve_ahead
//               |  8   | 15 s   | sign_four_way (crossroad), Traffic_sign (สัญญาณไฟข้างหน้า)
//   ------------|------|--------|---------------------------------------
//   Restriction |  4   | 30 s   | no_parking, no_u_turn, no_stop, no_passing  (suppress หนัก)
//
//   Safety window สั้นสุด: ตั้งใจให้ re-announce ได้บ่อย (เขตชุมชนมีทางข้ามถี่ ๆ)
//   Restriction window ยาวสุด: ข้อมูลซ้ำได้ค่า ~0 → suppress aggressive
// ============================================================
namespace {
using Ms = std::chrono::milliseconds;

const std::unordered_map<std::string, MomentaryPolicy>& table() {
    static const std::unordered_map<std::string, MomentaryPolicy> t = {
        // Safety (interrupt-capable: rank >= INTERRUPT_THRESHOLD)
        {"School_Zone",             {Ms(5000),  30}},
        {"Pedestrian_Warning_Sign", {Ms(5000),  25}},
        {"Pedestrian_crossing",     {Ms(5000),  20}},
        // Warning
        {"curve_ahead",             {Ms(15000), 10}},
        {"sign_four_way",           {Ms(15000),  8}},
        {"Traffic_sign",            {Ms(15000),  8}},
        // Restriction
        {"no_parking",              {Ms(30000),  4}},
        {"no_u_turn",               {Ms(30000),  4}},
        {"no_stop",                 {Ms(30000),  4}},
        {"no_passing",              {Ms(30000),  4}},
    };
    return t;
}
}  // namespace

const MomentaryPolicy* MomentaryPolicy::lookup(const std::string& cls) {
    const auto& t = table();
    auto it = t.find(cls);
    return it == t.end() ? nullptr : &it->second;
}
