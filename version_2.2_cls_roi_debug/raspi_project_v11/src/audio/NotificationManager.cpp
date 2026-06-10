#include "audio/NotificationManager.h"

#include <cstdlib>      // std::system
#include <filesystem>
#include <iostream>
#include <utility>      // std::move

namespace fs = std::filesystem;

NotificationManager::NotificationManager(std::string audio_dir,
                                         std::string device,
                                         bool enabled)
    : audio_dir_(std::move(audio_dir)),
      device_(std::move(device)),
      enabled_(enabled) {
    if (enabled_) {
        thread_ = std::thread(&NotificationManager::run, this);
    }
}

NotificationManager::~NotificationManager() {
    if (!enabled_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void NotificationManager::notify(Action action, const std::string& value) {
    if (!enabled_) return;

    const std::string file = SpeedAudioMap::filename(action, value);
    if (file.empty()) return;   // SuppressX / ค่าไม่รู้จัก -> เงียบ

    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_     = file;     // latest-wins: ของเก่าที่ยังไม่เล่นถูกทิ้ง
        has_pending_ = true;
    }
    cv_.notify_one();
}

void NotificationManager::run() {
    while (true) {
        std::string file;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return stop_ || has_pending_; });
            if (stop_) return;          // shutdown: ทิ้ง pending ที่เหลือ ออกเลย
            file         = std::move(pending_);
            pending_.clear();
            has_pending_ = false;
        }
        // เล่นนอก lock — producer เขียน slot ใหม่ได้ระหว่างเล่น (perception ไม่ถูก block)
        play_blocking(audio_dir_ + "/" + file);
    }
}

void NotificationManager::play_blocking(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::cerr << "[AUDIO] missing file: " << path << " (skip)\n" << std::flush;
        return;
    }
    // aplay -q -D <device> "<path>" ; block จนเล่นจบ (อยู่บน audio thread เท่านั้น)
    const std::string cmd = "aplay -q -D " + device_ + " \"" + path + "\"";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::cerr << "[AUDIO] aplay rc=" << rc << " for " << path << "\n" << std::flush;
    }
}
