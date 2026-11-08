// Keyboard as the controller on port 0. Port 0 always reports a controller; keys are read while the game window
// has focus. Without a gamepad on port 0 the keyboard is that controller; with one, both drive it.
//
//   left stick   W A S D          right stick  I J K L
//   d-pad        arrow keys       A B X Y      Space, Q, E, R  (Enter is A too, Backspace is B too)
//   start        Enter            back         Tab
//   L / R trigger  Left Shift / Left Ctrl      black / white  C / Z
//   left / right thumb  V / B
#include <windows.h>

#include <cstdint>

#include "core/input.h"

namespace game {

namespace {

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool OurWindowFocused() {
    HWND w = GetForegroundWindow();
    DWORD pid = 0;
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}

int16_t Axis(int negative_vk, int positive_vk) {
    int v = (Down(positive_vk) ? 32767 : 0) - (Down(negative_vk) ? 32767 : 0);
    return int16_t(v);
}

}  // namespace

void KeyboardPad(int port, input::Pad& pad) {
    if (port != 0) return;
    // always plugged in, so the game sees a controller from the start even when its window opens in the
    // background; keys only count while the game window has focus
    pad.connected = true;
    if (!OurWindowFocused()) return;

    // console masks: dpad up 0x1, down 0x2, left 0x4, right 0x8, start 0x10, back 0x20, left thumb 0x40, right thumb 0x80
    struct Bit { int vk; uint16_t mask; };
    constexpr Bit kButtons[] = {{VK_UP, 0x1},     {VK_DOWN, 0x2}, {VK_LEFT, 0x4}, {VK_RIGHT, 0x8},
                                {VK_RETURN, 0x10}, {VK_TAB, 0x20}, {'V', 0x40},    {'B', 0x80}};
    for (const Bit& b : kButtons)
        if (Down(b.vk)) pad.buttons |= b.mask;

    // analog buttons: A B X Y black white left trigger right trigger
    struct Analog { int vk; int index; };
    constexpr Analog kAnalog[] = {{VK_SPACE, 0}, {VK_RETURN, 0}, {'Q', 1}, {VK_BACK, 1}, {'E', 2},
                                  {'R', 3},      {'C', 4},       {'Z', 5}, {VK_LSHIFT, 6}, {VK_LCONTROL, 7}};
    for (const Analog& a : kAnalog)
        if (Down(a.vk)) pad.analog[a.index] = 0xFF;

    // sticks have y up; keys add to a real stick and clamp
    auto add = [](int16_t& axis, int16_t keys) {
        if (keys) axis = int16_t(keys > 0 ? 32767 : -32767);
    };
    add(pad.lx, Axis('A', 'D'));
    add(pad.ly, Axis('S', 'W'));
    add(pad.rx, Axis('J', 'L'));
    add(pad.ry, Axis('K', 'I'));
}

}  // namespace game
