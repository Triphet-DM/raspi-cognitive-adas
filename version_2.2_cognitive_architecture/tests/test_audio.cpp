// ============================================================
// test_audio — manual hardware harness สำหรับ NotificationManager (บน Pi)
//
// build (Pi):
//   g++ -std=c++17 -pthread -I src
//       src/audio/SpeedAudioMap.cpp src/audio/NotificationManager.cpp
//       tests/test_audio.cpp -o test_audio
//   (พิมพ์ต่อบรรทัดเดียว หรือใส่ \ ต่อท้ายเองตอนรันใน shell)
//
// run:
//   ./test_audio [audio_dir] [device]
//   เช่น  ./test_audio assets/audio plughw:0,0
//
// ไม่ใช่ unit test — ต้อง "ฟัง" ลำโพง ว่าผลตรงกับที่พิมพ์บนจอ
//   missing-file path: ลองชี้ audio_dir ผิด เช่น ./test_audio /nope plughw:0,0
//   shutdown drain   : กด Ctrl-C ระหว่างคลิปกำลังเล่น
// ============================================================

#include "audio/NotificationManager.h"

#include <chrono>
#include <iostream>
#include <thread>

using Act = NotificationManager::Action;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
    const std::string dir    = (argc > 1) ? argv[1] : "assets/audio";
    const std::string device = (argc > 2) ? argv[2] : "plughw:0,0";
    std::cout << "audio_dir=" << dir << "  device=" << device << "\n";

    NotificationManager nm(dir, device, /*enabled=*/true);

    // [1] sequential CHANGE — ควรได้ยิน "พื้นที่จำกัดความเร็ว XX" ครบ 5 ค่า ตามลำดับ
    std::cout << "[1] sequential CHANGE 50,60,80,90,100\n";
    for (const char* v : {"sign_50", "sign_60", "sign_80", "sign_90", "sign_100"}) {
        nm.notify(Act::Change, v);
        std::this_thread::sleep_for(2500ms);   // เผื่อคลิป (~1-2s) เล่นจบก่อนตัวถัดไป
    }

    // [2] REMINDER — ควรได้ยิน "จำกัดความเร็ว XX"
    std::cout << "[2] REMINDER 50,60\n";
    nm.notify(Act::Reminder, "sign_50");  std::this_thread::sleep_for(2500ms);
    nm.notify(Act::Reminder, "sign_60");  std::this_thread::sleep_for(2500ms);

    // [3] latest-wins burst — คาดว่าได้ยิน "50" (กำลังเล่น) แล้วตามด้วย "100" (ตัวสุดท้ายใน slot)
    //     ไม่ใช่ 50->60->80->100 ครบทุกตัว
    std::cout << "[3] latest-wins burst -> expect 50 then 100 only\n";
    nm.notify(Act::Change, "sign_50");          // เริ่มเล่น 50
    std::this_thread::sleep_for(200ms);         // ระหว่าง 50 ยังเล่นอยู่
    nm.notify(Act::Change, "sign_60");          // ถูกเขียนทับ
    nm.notify(Act::Change, "sign_80");          // ถูกเขียนทับ
    nm.notify(Act::Change, "sign_100");         // ค้างใน slot
    std::this_thread::sleep_for(3500ms);

    // [4] ค่าไม่รู้จัก -> เงียบ (no-op), ไม่ crash
    std::cout << "[4] unknown value sign_70 -> silent (no clip)\n";
    nm.notify(Act::Change, "sign_70");
    std::this_thread::sleep_for(800ms);

    std::cout << "done.\n";
    return 0;
}
