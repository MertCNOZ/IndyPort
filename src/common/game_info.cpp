#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace game {
void KeyboardPad(int port, input::Pad& pad);  // keyboard.cpp
void PinGuestThreads();                       // cpu.cpp
void StartAutopilot();                        // autopilot.cpp
void StartWatch();                            // watch.cpp
void StartFps();                              // fps.cpp
void FpsOverlay(double fps);                  // fps.cpp
extern bool g_show_fps;                       // fps.cpp
void AutopilotPad(int port, input::Pad& pad);

// F9 turns the frame counter on and off for this run (the launcher's setting is what it starts with)
bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t*) {
    if (msg == WM_KEYDOWN && wp == VK_F9 && !(lp & (1 << 30))) g_show_fps = !g_show_fps;
    return false;
}

// before the window and boot: guest threads on one core, the autopilot if INDY_AUTOPILOT is set
void Init() {
    PinGuestThreads();
    StartAutopilot();
    StartWatch();
    StartFps();
}

// the keyboard, then the autopilot's presses on top; a real controller plugging in or out is logged
void Pad(int port, input::Pad& pad) {
    static std::atomic<bool> real[4];
    if (port >= 0 && port < 4 && real[port].exchange(pad.connected) != pad.connected)
        LOGR("input: controller %s on port %d", pad.connected ? "connected" : "disconnected", port + 1);
    KeyboardPad(port, pad);
    AutopilotPad(port, pad);
}
}

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Indiana Jones and the Emperor's Tomb",  // window and splash
    .short_title = L"Emperor's Tomb",     // message boxes
    .name = L"IndyET",                    // window classes, crash dumps, handover event
    .log = "indyet",                      // log file prefix
    .settings = "settings.toml",          // settings file beside the exe
    .splash = L"splash",                  // optional splash.png or splash.bmp beside the exe
    .missing = L"The game was not found. Open IndyPort.exe and choose your disc image (.iso) or the folder of the "
               L"extracted game (with default.xbe), or put either in the assets folder beside the game.",
    .images = {{"default.xbe", "IndyET"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"show_fps", &game::g_show_fps, nullptr}},  // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = game::Init,                   // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = game::Message,             // window messages first; true takes one
    .ui = game::FpsOverlay,               // ImGui drawing each frame: the frame counter
    .pad = game::Pad,                     // changes a polled pad: keyboard and autopilot drive port 0
    .rumble = nullptr,                    // changes a rumble before it is set
};
