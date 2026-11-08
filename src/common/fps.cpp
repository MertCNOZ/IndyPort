// The frame rate. The game limits itself with its "fpsLimit" variable (vars.cfg in TDATA, 30 on the Xbox): its frame
// function (0x9A4C0) spins on the time stamp counter until 1000 / fpsLimit ms have passed since the last frame, and 0
// turns that wait off. The simulation steps at a fixed 1/60 s (0x17D50), so drawing faster does not speed it up.
// vars.cfg is signed, an edited file is replaced with the defaults; so the variable is set in memory instead, from
// the settings' fps_cap: 30, 60, 120, 144 as they are, unlimited and the monitor's rate as 0 (the runtime's own
// limiter, app::Pace, then holds the cap). The game writes the value back when it reads vars.cfg, so it is kept set.
//
// INDY_FPS=1 also logs every 2 seconds how many frames the game presented a second and how many vertical blanks
// went by, from the counters in the game's own D3D device.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace game {

namespace {
constexpr uint32_t kSettingsPtr = 0x6B93A8;  // the game's settings object (0x9C6C3 stores it)
constexpr uint32_t kFpsLimit = 0x8C;         // its "fpsLimit" (registered at 0x9EF26)
constexpr uint32_t kDevicePtr = 0x4A9668;    // D3D_pDevice
constexpr uint32_t kSwapCount = 0x2010;      // CDevice_SwapCount
constexpr uint32_t kVBlankCount = 0x1980;    // CDevice_VBlankCount

uint32_t Read32(uint32_t va) {
    uint32_t v;
    std::memcpy(&v, kernel::g_base + va, 4);
    return v;
}
void Write32(uint32_t va, uint32_t v) { std::memcpy(kernel::g_base + va, &v, 4); }

// the game's limit for the settings' cap
uint32_t GameLimit() {
    uint32_t cap = app::Get().fps_cap;
    if (cap == 0 || cap == app::kFpsCapRefresh) return 0;
    return cap;
}

void Loop(bool log) {
    for (int i = 0; i < 600 && !kernel::g_base; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!kernel::g_base) return;
    uint32_t want = GameLimit(), told = 0xFFFFFFFF;
    uint32_t last_swaps = 0, last_blanks = 0;
    auto next_log = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (uint32_t settings = Read32(kSettingsPtr)) {
            if (Read32(settings + kFpsLimit) != want) {
                if (told != Read32(settings + kFpsLimit))
                    LOGR("fps: the game's limit %u -> %u", Read32(settings + kFpsLimit), want);
                Write32(settings + kFpsLimit, want);
                told = want;
            }
        }
        if (!log || std::chrono::steady_clock::now() < next_log) continue;
        next_log += std::chrono::seconds(2);
        uint32_t dev = Read32(kDevicePtr);
        if (!dev) continue;
        uint32_t swaps = Read32(dev + kSwapCount), blanks = Read32(dev + kVBlankCount);
        LOGR("fps: %.1f frames/s, %.1f blanks/s", (swaps - last_swaps) / 2.0, (blanks - last_blanks) / 2.0);
        last_swaps = swaps;
        last_blanks = blanks;
    }
}
}  // namespace

void StartFps() {
    const char* v = std::getenv("INDY_FPS");
    std::thread(Loop, v && *v == '1').detach();
}

}  // namespace game
