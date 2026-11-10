// Keyboard and mouse as the controller on port 0. Port 0 always reports a controller; keys and the mouse are read
// while the game window has focus. Without a gamepad on port 0 they are that controller; with one, both drive it.
//
// The bindings come from the settings (keys = 24 pairs of key codes, IndyPort's Controls page): the game's 16
// buttons in pad_map's order, then the left stick (up, down, left, right) and the right stick the same. A code is a
// Windows virtual key (the mouse buttons are virtual keys too), 0x100 / 0x101 the mouse wheel up / down, 0 none.
// The defaults follow the game's first controller layout: WASD moves, the mouse turns the camera, the left button
// is Attack 1, the right one Guard mode, Space jumps, E is Action, Q Attack 2, R Reload, G Draw / Pack, F First
// person, the wheel scrolls the items, Esc pauses; Enter is A and Start (the title screen wants Start), Backspace
// is B in the menus.
//
// Names: while a name box is up (New Game's "Enter a name", src/game/hooks.cpp) letters, digits, space, Backspace,
// Delete, Home, End and the left and right arrows type into it instead of pressing their bound buttons; up, down
// and Enter still move to OK and choose it, and Esc leaves the box (B).
//
// Mouse look: raw mouse movement turned into the right stick. The movement's speed (smoothed over only 8 ms, about
// a frame, so polls close together share it without lag) sets how far the stick is pushed, past the game's dead
// zone, so the camera turns as fast as the mouse moves. While the game has focus the cursor is hidden and kept inside the window.
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>

#include <toml++/toml.hpp>

#include "core/input.h"
#include "core/log.h"

namespace game {

namespace {

constexpr int kActions = 24;
constexpr uint32_t kWheelUp = 0x100, kWheelDown = 0x101;
using Bindings = std::array<std::array<uint32_t, 2>, kActions>;

// A B X Y Black White LT RT, d-pad up down left right, Start Back, LS RS, move U D L R, camera U D L R
constexpr Bindings kDefaults = {{{'Q', VK_RETURN},       {VK_SPACE, VK_BACK}, {VK_LBUTTON, 0}, {'E', 0},
                                 {'G', 0},               {'R', 0},            {'F', 0},        {VK_RBUTTON, 0},
                                 {VK_UP, '1'},           {VK_DOWN, '2'},      {VK_LEFT, kWheelUp}, {VK_RIGHT, kWheelDown},
                                 {VK_ESCAPE, VK_RETURN}, {VK_TAB, 0},         {'V', 0},        {VK_MBUTTON, 0},
                                 {'W', 0},               {'S', 0},            {'A', 0},        {'D', 0},
                                 {'I', 0},               {'K', 0},            {'J', 0},        {'L', 0}}};

// the game's buttons as Pad places: an analog index, or -1 and a digital mask (pad_map.cpp's order)
struct Place {
    int analog;
    uint16_t mask;
};
constexpr Place kButtons[16] = {{0, 0},  {1, 0},  {2, 0},  {3, 0},  {4, 0},     {5, 0},     {6, 0},     {7, 0},
                                {-1, 1}, {-1, 2}, {-1, 4}, {-1, 8}, {-1, 0x10}, {-1, 0x20}, {-1, 0x40}, {-1, 0x80}};

Bindings g_keys = kDefaults;
bool g_mouse_look = true;
uint32_t g_mouse_sens = 50;  // 1-100, 50 is 1x
bool g_mouse_invert = false;

HWND g_hwnd = nullptr;
std::atomic<int> g_dx{0}, g_dy{0};          // raw mouse movement not read yet
std::atomic<int> g_cam_dx{0}, g_cam_dy{0};  // the same for the camera hook (src/game/hooks.cpp)
std::atomic<int> g_wheel_up{0}, g_wheel_down{0};  // wheel notches not played yet

using Clock = std::chrono::steady_clock;

// a name box is up: the hook saw it in the last 200 ms
std::atomic<int64_t> g_textbox_seen{0};
std::mutex g_text_lock;
std::deque<uint8_t> g_text_keys;

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

bool Typing() { return NowMs() - g_textbox_seen.load() < 200; }

// the third person camera ran in the last 200 ms (its hook turns it by the mouse itself); the time the mouse last
// moved and the time the camera last had stick input (pad, or keys bound to the right stick)
std::atomic<int64_t> g_camera_seen{0}, g_mouse_moved{0}, g_stick_moved{0};

bool CameraHooked() { return NowMs() - g_camera_seen.load() < 200; }

// the keys that type into a name box, and their DirectInput codes (scan codes, extended ones with 0x80)
bool TextKey(uint32_t vk) {
    return (vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9') || (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) ||
           vk == VK_SPACE || vk == VK_BACK || vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME ||
           vk == VK_END;
}

uint8_t DirectInputCode(uint32_t vk) {
    bool extended = vk == VK_DELETE || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_HOME || vk == VK_END;
    uint32_t sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    return uint8_t((sc & 0x7F) | (extended ? 0x80 : 0));
}

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool OurWindowFocused() {
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

// a wheel notch is a short press: 90 ms down, 60 ms up, one after another
struct WheelPress {
    Clock::time_point until{};
    bool down = false;
    bool Step(std::atomic<int>& notches, Clock::time_point now) {
        if (now < until) return down;
        if (down) {
            down = false;
            until = now + std::chrono::milliseconds(60);
            return false;
        }
        if (notches.load() > 0) {
            notches--;
            down = true;
            until = now + std::chrono::milliseconds(90);
        }
        return down;
    }
};
WheelPress g_wheel[2];

bool Held(uint32_t code, const bool wheel[2]) {
    if (!code) return false;
    if (code < 0x100 && (TextKey(code) || code == VK_ESCAPE) && Typing()) return false;  // typing a name
    if (code == kWheelUp) return wheel[0];
    if (code == kWheelDown) return wheel[1];
    return code < 0x100 && Down(int(code));
}

// the stick push for a smoothed mouse speed in counts a second: past a dead zone, 1 at full
float StickFromSpeed(float v) {
    constexpr float kDeadZone = 0.22f;   // about the game's own stick dead zone
    float scale = (float(g_mouse_sens) / 50.0f) / 1500.0f;  // 1500 counts a second is full at 1x
    float m = std::fabs(v) * scale;
    if (m < 0.002f) return 0;
    m = std::min(kDeadZone + (1 - kDeadZone) * m, 1.0f);
    return v < 0 ? -m : m;
}

// the cursor kept inside the window while it has focus and mouse look is on
void HoldCursor(bool hold) {
    static bool held = false;
    if (hold && g_hwnd) {
        RECT r;
        GetClientRect(g_hwnd, &r);
        POINT a{r.left, r.top}, b{r.right, r.bottom};
        ClientToScreen(g_hwnd, &a);
        ClientToScreen(g_hwnd, &b);
        RECT s{a.x, a.y, b.x, b.y};
        ClipCursor(&s);
        held = true;
    } else if (held) {
        ClipCursor(nullptr);
        held = false;
    }
}

}  // namespace

void NoteTextBox() { g_textbox_seen = NowMs(); }

void TakeMouseCamera(float& yaw, float& pitch, bool& mouse_has_camera) {
    int64_t now = NowMs();
    g_camera_seen = now;
    int dx = g_cam_dx.exchange(0), dy = g_cam_dy.exchange(0);
    yaw = pitch = 0;
    mouse_has_camera = false;
    if (!g_mouse_look || !OurWindowFocused()) return;
    if (dx || dy) g_mouse_moved = now;
    // radians a count: 0.0022 (about 0.13 degrees) at sensitivity 50
    float k = float(g_mouse_sens) / 50.0f * 0.0022f;
    yaw = -float(dx) * k;  // the camera's turn is the other way round: the view turns the way the mouse goes
    pitch = float(dy) * k * (g_mouse_invert ? -1.0f : 1.0f);
    mouse_has_camera = g_mouse_moved.load() > g_stick_moved.load();
}

void NoteCameraStick() { g_stick_moved = NowMs(); }

bool TakeTextKey(uint8_t& dik) {
    std::lock_guard l(g_text_lock);
    if (g_text_keys.empty()) return false;
    dik = g_text_keys.front();
    g_text_keys.pop_front();
    return true;
}

void LoadKeys(const void* table) {
    const auto& t = *static_cast<const toml::table*>(table);
    if (const toml::array* a = t["keys"].as_array(); a && a->size() == kActions) {
        Bindings k{};
        bool ok = true;
        for (size_t i = 0; i < kActions && ok; i++) {
            const toml::array* pair = (*a)[i].as_array();
            ok = pair && pair->size() == 2;
            for (size_t j = 0; ok && j < 2; j++) {
                int64_t v = (*pair)[j].value_or(int64_t(-1));
                ok = v >= 0 && v <= 0x101;
                k[i][j] = uint32_t(v);
            }
        }
        if (ok) g_keys = k;
    }
    // keys_version 2: Enter presses Start too. A file from before gets it in Start's second slot when that is free
    if (!t.contains("keys_version") && !g_keys[12][1] && g_keys[12][0] != VK_RETURN) g_keys[12][1] = VK_RETURN;
    g_mouse_look = t["mouse_look"].value_or(g_mouse_look);
    g_mouse_sens = std::clamp(uint32_t(t["mouse_sensitivity"].value_or(int64_t(g_mouse_sens))), 1u, 100u);
    g_mouse_invert = t["mouse_invert_y"].value_or(g_mouse_invert);
}

void SaveKeys(void* table) {
    toml::array a;
    for (const auto& pair : g_keys) a.push_back(toml::array{int64_t(pair[0]), int64_t(pair[1])});
    auto& t = *static_cast<toml::table*>(table);
    t.insert_or_assign("keys", std::move(a));
    t.insert_or_assign("keys_version", int64_t(2));
    t.insert_or_assign("mouse_look", g_mouse_look);
    t.insert_or_assign("mouse_sensitivity", int64_t(g_mouse_sens));
    t.insert_or_assign("mouse_invert_y", g_mouse_invert);
}

// the game window is up: raw mouse input for mouse look
void MouseWindow(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE rid{0x01, 0x02, 0, g_hwnd};  // generic desktop, mouse
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) LOGR("input: raw mouse input unavailable (%lu)", GetLastError());
}

// the game window's messages: raw mouse movement, the wheel, no cursor over the window while mouse look holds it
bool MouseMessage(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    switch (msg) {
        case WM_KEYDOWN:
            if (Typing() && TextKey(uint32_t(wp))) {
                std::lock_guard l(g_text_lock);
                if (g_text_keys.size() < 64) g_text_keys.push_back(DirectInputCode(uint32_t(wp)));
            }
            return false;
        case WM_INPUT: {
            RAWINPUT ri;
            UINT size = sizeof(ri);
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) != UINT(-1) &&
                ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                g_dx += ri.data.mouse.lLastX;
                g_dy += ri.data.mouse.lLastY;
                g_cam_dx += ri.data.mouse.lLastX;
                g_cam_dy += ri.data.mouse.lLastY;
            }
            return false;  // DefWindowProc still cleans the input up
        }
        case WM_MOUSEWHEEL: {
            int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            if (notches > 0) g_wheel_up += notches;
            if (notches < 0) g_wheel_down -= notches;
            *result = 0;
            return true;
        }
        case WM_SETCURSOR:
            if (g_mouse_look && LOWORD(lp) == HTCLIENT && hwnd == g_hwnd) {
                SetCursor(nullptr);
                *result = TRUE;
                return true;
            }
            return false;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) HoldCursor(false);
            return false;
        default:
            return false;
    }
}

void KeyboardPad(int port, input::Pad& pad) {
    if (port != 0) return;
    // always plugged in, so the game sees a controller from the start even when its window opens in the
    // background; keys only count while the game window has focus
    pad.connected = true;
    bool focused = OurWindowFocused();
    HoldCursor(focused && g_mouse_look);
    auto now = Clock::now();
    bool wheel[2] = {g_wheel[0].Step(g_wheel_up, now), g_wheel[1].Step(g_wheel_down, now)};
    if (!focused) {
        g_dx = 0;
        g_dy = 0;
        g_wheel_up = 0;
        g_wheel_down = 0;
        return;
    }

    auto held = [&](int action) { return Held(g_keys[size_t(action)][0], wheel) || Held(g_keys[size_t(action)][1], wheel); };
    for (int b = 0; b < 16; b++) {
        if (!held(b)) continue;
        const Place& p = kButtons[b];
        if (p.analog >= 0) pad.analog[p.analog] = 0xFF;
        else pad.buttons |= p.mask;
    }

    // Esc leaves a name box, as B does (Space and Backspace, B's keys, are typing there)
    if (Typing() && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) pad.analog[1] = 0xFF;

    // sticks have y up; keys add to a real stick and clamp
    auto keys_axis = [&](int negative, int positive) { return (held(positive) ? 1 : 0) - (held(negative) ? 1 : 0); };
    auto push = [](int16_t& axis, float v) {
        if (v != 0) axis = int16_t(std::clamp(v, -1.0f, 1.0f) * 32767);
    };
    push(pad.lx, float(keys_axis(18, 19)));
    push(pad.ly, float(keys_axis(17, 16)));
    push(pad.rx, float(keys_axis(22, 23)));
    push(pad.ry, float(keys_axis(21, 20)));

    if (!g_mouse_look) return;
    // the mouse's speed, smoothed: polls close together share one movement instead of seeing it once and then zero
    static Clock::time_point last = now;
    static float vx = 0, vy = 0;
    float dt = std::clamp(std::chrono::duration<float>(now - last).count(), 1e-4f, 0.1f);
    last = now;
    float mx = float(g_dx.exchange(0)) / dt, my = float(g_dy.exchange(0)) / dt;
    if (CameraHooked()) {
        // the third person camera turns by the mouse itself (TakeMouseCamera): no stick
        vx = vy = 0;
        return;
    }
    g_cam_dx = 0;
    g_cam_dy = 0;
    float k = std::min(1.0f, dt / 0.008f);
    vx += (mx - vx) * k;
    vy += (my - vy) * k;
    float sx = StickFromSpeed(vx), sy = StickFromSpeed(vy) * (g_mouse_invert ? 1.0f : -1.0f);
    if (sx != 0) pad.rx = int16_t(std::clamp(float(pad.rx) / 32767 + sx, -1.0f, 1.0f) * 32767);
    if (sy != 0) pad.ry = int16_t(std::clamp(float(pad.ry) / 32767 + sy, -1.0f, 1.0f) * 32767);
}

}  // namespace game
