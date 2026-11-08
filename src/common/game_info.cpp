#include <atomic>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace game {
void KeyboardPad(int port, input::Pad& pad);  // keyboard.cpp
void PinGuestThreads();                       // cpu.cpp
void StartAutopilot();                        // autopilot.cpp
void StartWatch();                            // watch.cpp
void AutopilotPad(int port, input::Pad& pad);

// before the window and boot: guest threads on one core, the autopilot if INDY_AUTOPILOT is set
void Init() {
    PinGuestThreads();
    StartAutopilot();
    StartWatch();
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
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "IndyET"}},  // each recompiled xbe and its exe, for title launches
    .options = {},                        // game settings kept in the settings file
    .load = nullptr,                      // reads more from the settings file's toml::table
    .save = nullptr,                      // writes more into it
    .init = game::Init,                   // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = nullptr,                    // the game window is up
    .message = nullptr,                   // window messages first; true takes one
    .ui = nullptr,                        // ImGui drawing each frame
    .pad = game::Pad,                     // changes a polled pad: keyboard and autopilot drive port 0
    .rumble = nullptr,                    // changes a rumble before it is set
};
