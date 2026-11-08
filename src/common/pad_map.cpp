// Gamepad button mapping (the launcher's Controls page): which button of a modern controller presses each button
// of the Xbox controller the game reads. settings.toml: pad_map = [16 numbers], indexed by the game's button (kGame
// order below), each a controller button (kController order) or -1 for none. Missing or wrong: the default, where
// each controller button presses the game button in its place (LB is White, RB is Black).
//
// The runtime fills the Xbox pad from the controller with that default mapping, so the hook reads each controller
// button back from its default place and writes the game's buttons from the chosen ones. The keyboard and the
// autopilot add their presses after this, unchanged.
#include <algorithm>
#include <array>
#include <cstdint>

#include <toml++/toml.hpp>

#include "core/input.h"
#include "core/log.h"

namespace game {

namespace {

// a button of the Xbox pad: an analog button (index into Pad::analog) or a digital one (a Pad::buttons mask)
struct Place {
    int analog;  // -1 for digital
    uint16_t mask;
};

// the game's buttons, in the settings' order: A B X Y Black White LT RT, d-pad up down left right, Start Back,
// left and right stick click
constexpr Place kGame[16] = {{0, 0},  {1, 0},  {2, 0},  {3, 0},  {4, 0},     {5, 0},     {6, 0},     {7, 0},
                             {-1, 1}, {-1, 2}, {-1, 4}, {-1, 8}, {-1, 0x10}, {-1, 0x20}, {-1, 0x40}, {-1, 0x80}};

// the controller's buttons, A B X Y LB RB LT RT, d-pad, Menu View, LS RS: where the runtime puts each one
constexpr int kDefaultGameButton[16] = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

std::array<int, 16> g_map = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};  // game button -> controller
bool g_default = true;

uint8_t Get(const input::Pad& pad, const Place& p) {
    return p.analog >= 0 ? pad.analog[p.analog] : (pad.buttons & p.mask) ? 0xFF : 0;
}

}  // namespace

void LoadPadMap(const void* table) {
    const auto& t = *static_cast<const toml::table*>(table);
    const toml::array* a = t["pad_map"].as_array();
    if (!a || a->size() != 16) return;
    std::array<int, 16> map{};
    for (size_t i = 0; i < 16; i++) {
        int v = int((*a)[i].value_or(int64_t(-2)));
        if (v < -1 || v > 15) return;
        map[i] = v;
    }
    g_map = map;
    g_default = true;
    for (int i = 0; i < 16; i++) g_default &= g_map[size_t(i)] == kDefaultGameButton[i];
    if (!g_default) LOGR("input: the gamepad buttons are mapped by the settings");
}

void SavePadMap(void* table) {
    toml::array a;
    for (int v : g_map) a.push_back(int64_t(v));
    static_cast<toml::table*>(table)->insert_or_assign("pad_map", std::move(a));
}

// the controller's presses moved to the chosen game buttons
void MapPad(int, input::Pad& pad) {
    if (g_default || !pad.connected) return;
    uint8_t controller[16];
    for (int c = 0; c < 16; c++) controller[c] = Get(pad, kGame[kDefaultGameButton[c]]);
    input::Pad out = pad;
    out.buttons = 0;
    for (auto& v : out.analog) v = 0;
    for (int g = 0; g < 16; g++) {
        int c = g_map[size_t(g)];
        uint8_t v = c >= 0 ? controller[c] : 0;
        const Place& p = kGame[g];
        if (p.analog >= 0) out.analog[p.analog] = std::max(out.analog[p.analog], v);
        else if (v >= 0x80) out.buttons |= p.mask;
    }
    pad = out;
}

}  // namespace game
