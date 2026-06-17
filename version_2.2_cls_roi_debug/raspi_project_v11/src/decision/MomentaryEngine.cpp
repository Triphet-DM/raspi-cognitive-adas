#include "decision/MomentaryEngine.h"

MomentaryEngine::Result
MomentaryEngine::onConfirmed(const std::string& cls, TimePoint now) {
    const MomentaryPolicy* pol = MomentaryPolicy::lookup(cls);
    if (!pol) {
        // ไม่ใช่ momentary class — router ควรกันมาแล้ว; กันพลาดด้วยการเงียบ
        return {Decision::Suppress, cls, 0};
    }

    auto it = last_notified_.find(cls);
    if (it != last_notified_.end() && (now - it->second) < pol->suppression_window) {
        // ยังอยู่ใน suppression window → เงียบ, ไม่ stamp (วัดจากครั้งที่ประกาศล่าสุด)
        return {Decision::Suppress, cls, pol->attention_rank};
    }

    last_notified_[cls] = now;   // ประกาศ → จำเวลาที่ "บอกคนขับ"
    return {Decision::Announce, cls, pol->attention_rank};
}
