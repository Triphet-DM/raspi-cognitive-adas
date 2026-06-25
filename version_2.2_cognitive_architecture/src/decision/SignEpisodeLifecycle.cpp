#include "decision/SignEpisodeLifecycle.h"

#include <algorithm>   // std::max

SignEpisodeLifecycle::SignEpisodeLifecycle(Millis rearm_after)
    : rearm_after_(std::max(Millis::zero(), rearm_after)) {}   // rearm_after >= 0

SignEpisodeLifecycle::EpisodeConfirmed
SignEpisodeLifecycle::update(bool presence, bool confirm,
                             const std::string& value, TimePoint now) {
    EpisodeConfirmed ev;   // fired=false

    if (presence) last_seen_ = now;   // เห็นป้าย speed ใด ๆ -> refresh presence (RAW)

    // ── confirm event (voter ยืนยันค่า speed; facade gate is_speed แล้ว) ──
    if (confirm) {
        ev.fresh   = (state_ == State::Armed);   // fresh เฉพาะ Armed->Confirmed
        ev.fired   = true;
        ev.value   = value;                      // pass-through (value-blind)
        state_     = State::Confirmed;
        last_seen_ = now;                        // confirm = เพิ่งเห็นล่าสุด
        return ev;
    }

    // ── ไม่มี confirm: จัดการ presence/absence เพื่อ re-arm ──
    if (state_ == State::Confirmed || state_ == State::Releasing) {
        if (presence) {
            if (state_ == State::Releasing)
                state_ = State::Confirmed;        // re-seen ป้ายเดิมก่อนหมดเวลา
        } else {
            if (state_ == State::Confirmed)
                state_ = State::Releasing;        // เริ่มหายจากเฟรม
            if (now - last_seen_ >= rearm_after_)
                state_ = State::Armed;            // หายนานพอ -> re-arm
        }
    }
    return ev;
}

void SignEpisodeLifecycle::reset() {
    state_     = State::Armed;
    last_seen_ = {};
}
