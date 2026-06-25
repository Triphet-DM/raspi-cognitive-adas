#include "audio/NotificationManager.h"

#include <spawn.h>      // posix_spawnp
#include <sys/wait.h>   // waitpid, WNOHANG
#include <csignal>      // kill, SIGTERM, SIGKILL
#include <filesystem>
#include <iostream>
#include <utility>      // std::move

extern char** environ;  // ส่งต่อ environment ปัจจุบันให้ aplay (posix_spawnp ต้องการ envp)

namespace fs = std::filesystem;

namespace {
constexpr int kTermGraceMs = 100;   // SIGTERM แล้วรอเท่านี้ ถ้ายังไม่ตาย → SIGKILL (กันค้าง)
constexpr int kReapPollMs  = 20;    // ความถี่ที่ audio thread ตื่นมาเช็คว่าคลิปจบ / ถึงเวลา SIGKILL
}  // namespace

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

void NotificationManager::submit(const std::string& filename) {
    if (!enabled_) return;
    if (filename.empty()) return;   // ไม่มีคลิป -> เงียบ
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_     = filename;     // latest-wins: ของเก่าที่ยังไม่เล่นถูกทิ้ง (ไม่แตะตัวที่เล่นอยู่)
        has_pending_ = true;
    }
    cv_.notify_one();
}

void NotificationManager::preempt(const std::string& filename) {
    if (!enabled_) return;
    if (filename.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_     = filename;     // ตัวถัดไปที่จะเล่นหลังตัด (latest-wins)
        has_pending_ = true;
        if (child_pid_ > 0) {        // มีคลิปเล่นอยู่ → เคาะไหล่ให้หยุด (SIGTERM = ปิด ALSA นุ่มนวล)
            ::kill(child_pid_, SIGTERM);
            term_pending_  = true;
            term_deadline_ = Clock::now() + std::chrono::milliseconds(kTermGraceMs);
        }
    }
    cv_.notify_one();   // ปลุก audio thread ให้รีบ reap ตัวเก่า + เล่นตัวใหม่
}

void NotificationManager::notify(Action action, const std::string& value) {
    submit(SpeedAudioMap::filename(action, value));
}

bool NotificationManager::is_idle() {
    if (!enabled_) return true;     // ไม่มี audio → ไม่มีอะไรให้รอ
    std::lock_guard<std::mutex> lock(mutex_);
    return child_pid_ <= 0 && !has_pending_;   // ไม่มีคลิปเล่นอยู่ + ไม่มีคิวค้าง
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
        // เล่นนอก lock — producer เขียน slot ใหม่ / สั่ง preempt ได้ระหว่างเล่น
        play_clip(audio_dir_ + "/" + file);
    }
}

void NotificationManager::play_clip(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::cerr << "[AUDIO] missing file: " << path << " (skip)\n" << std::flush;
        return;
    }

    // spawn aplay เป็น process ลูกที่เรา "ถือ PID" → preempt() ฆ่าได้กลางคลิป.
    //   posix_spawn ไม่ clone address space ทั้งก้อน (โมเดล/บัฟเฟอร์หลายร้อย MB) และ
    //   ปลอดภัยกับ multithread (เลี่ยง fork-in-threaded deadlock) — เบากว่า fork บน Pi.
    char* argv[] = {
        const_cast<char*>("aplay"),
        const_cast<char*>("-q"),
        const_cast<char*>("-D"),
        const_cast<char*>(device_.c_str()),
        const_cast<char*>(path.c_str()),
        nullptr
    };
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, "aplay", nullptr, nullptr, argv, environ);
    if (rc != 0) {
        std::cerr << "[AUDIO] spawn aplay failed rc=" << rc << " for " << path << "\n" << std::flush;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        child_pid_    = pid;     // จากนี้ producer มองเห็น → preempt() kill ได้
        term_pending_ = false;
    }

    // reap loop — audio thread เป็นเจ้าเดียวที่ waitpid (กัน zombie + กัน double-reap).
    //   waitpid(WNOHANG) + clear child_pid_ ทำใต้ mutex เดียวกับที่ preempt() ใช้ kill →
    //   reap กับ clear เป็น atomic: producer ไม่มีทาง kill PID ที่ reap ไปแล้ว (ปิด PID-reuse).
    int status = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (stop_) {                              // shutdown: ตัดทันที แล้ว block reap ให้จบ
            ::kill(pid, SIGTERM);
            waitpid(pid, &status, 0);
            child_pid_ = -1;
            return;
        }

        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || r == -1) {                // คลิปจบเอง / ถูกฆ่าแล้ว → reap+clear (atomic)
            child_pid_ = -1;
            return;
        }

        // r == 0 → ยังเล่นอยู่
        if (term_pending_ && Clock::now() >= term_deadline_) {
            ::kill(pid, SIGKILL);                 // SIGTERM ไม่ยอมตายใน 100ms → กระชาก
            term_pending_ = false;                // escalate ครั้งเดียวพอ
        }
        cv_.wait_for(lock, std::chrono::milliseconds(kReapPollMs), [&] { return stop_; });
    }
}
