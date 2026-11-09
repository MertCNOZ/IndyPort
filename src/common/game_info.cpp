#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>

#include <toml++/toml.hpp>

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
uint32_t g_draw_distance = 100;               // percent of each level's far clip (src/game/hooks.cpp)
void AutopilotPad(int port, input::Pad& pad);
void LoadPadMap(const void* table);  // pad_map.cpp
void SavePadMap(void* table);
void LoadKeys(const void* table);  // keyboard.cpp
void SaveKeys(void* table);
void MouseWindow(void* hwnd);
bool MouseMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result);
void MapPad(int port, input::Pad& pad);

// the settings file's extra keys: the gamepad mapping, and the modern button icons on unless the file says otherwise
void Load(const void* table) {
    LoadPadMap(table);
    LoadKeys(table);
    if (!static_cast<const toml::table*>(table)->contains("texture_packs")) app::Get().texture_packs = "modern_buttons";
}

void Save(void* table) {
    SavePadMap(table);
    SaveKeys(table);
}

// F9 turns the frame counter on and off for this run (the launcher's setting is what it starts with)
bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    if (msg == WM_KEYDOWN && wp == VK_F9 && !(lp & (1 << 30))) g_show_fps = !g_show_fps;
    return MouseMessage(hwnd, msg, wp, lp, result);
}

// before the window and boot: guest threads on one core, the autopilot if INDY_AUTOPILOT is set
void Init() {
    PinGuestThreads();
    StartAutopilot();
    StartWatch();
    StartFps();
}

// the controller's buttons as mapped in the settings, the keyboard, then the autopilot's presses on top; a real controller plugging in or out is logged
void Pad(int port, input::Pad& pad) {
    static std::atomic<bool> real[4];
    if (port >= 0 && port < 4 && real[port].exchange(pad.connected) != pad.connected)
        LOGR("input: controller %s on port %d", pad.connected ? "connected" : "disconnected", port + 1);
    MapPad(port, pad);
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
    .options = {{"show_fps", &game::g_show_fps, nullptr}, {"draw_distance", nullptr, &game::g_draw_distance}},  // game settings kept in the settings file
    .load = game::Load,                   // reads more from the settings file's toml::table: pad_map
    .save = game::Save,                   // writes more into it: pad_map, keys, mouse
    .init = game::Init,                   // after the settings load, before the window and boot
    .loaded = nullptr,                    // image in memory, before its entry point runs
    .window = game::MouseWindow,          // the game window is up: raw mouse input
    .message = game::Message,             // window messages first; true takes one
    .ui = game::FpsOverlay,               // ImGui drawing each frame: the frame counter
    .pad = game::Pad,                     // changes a polled pad: keyboard and autopilot drive port 0
    .rumble = nullptr,                    // changes a rumble before it is set
};
