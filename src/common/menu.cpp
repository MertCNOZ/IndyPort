// The in-game settings menu, for playing without IndyPort (a TV, a controller, a front end such as EmulationStation):
// F1, or Back and Start together on a controller, opens it over the game; the arrow keys or the d-pad / left stick
// choose and change, B or Esc (or F1, or Back and Start again) close it. Everything applies at once and is saved to
// settings.toml when it closes, into the file as it is, so IndyPort shows the same settings.
//
// Opened while playing, it first opens the game's pause menu (a Start press), so nothing happens to Indy behind it.
// While it is open the game gets no input; after it closes, a button or key still held is kept from the game until
// it is let go (B or Esc would jump or pause again). Start and Back alone reach the game a moment late (150 ms), the
// time to see whether the other one follows.
//
// Drawn with ImGui's draw lists on the render thread (the runtime's GameInfo::ui); its own navigation, not ImGui's,
// so every controller the game sees moves it, not only XInput ones. Texts are the launcher's, in its language
// (launcher/lang.cpp).
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <toml++/toml.hpp>

#include "../../launcher/lang.h"
#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace game {

extern bool g_show_fps;            // fps.cpp
extern uint32_t g_draw_distance;   // game_info.cpp
extern uint32_t g_rumble;          // game_info.cpp
extern bool g_mouse_look;          // keyboard.cpp
extern uint32_t g_mouse_sens;
extern bool g_mouse_invert;
bool HudSpread();                  // keyboard.cpp: playing

namespace {

using Clock = std::chrono::steady_clock;
int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

constexpr uint16_t kUp = 0x1, kDown = 0x2, kLeft = 0x4, kRight = 0x8, kStart = 0x10, kBack = 0x20;

// colours as the launcher's
constexpr ImU32 kPanel = IM_COL32(20, 17, 15, 245), kLine = IM_COL32(64, 54, 46, 255), kText = IM_COL32(236, 230, 222, 255),
                kDim = IM_COL32(150, 140, 128, 255), kAccent = IM_COL32(236, 160, 44, 255),
                kSel = IM_COL32(58, 42, 32, 255);

ImFont* g_font = nullptr;   // Segoe UI
ImFont* g_title = nullptr;  // Georgia bold
float g_baked = 22;         // the size the fonts were made at

// ---- the items

struct Item {
    const char* label = nullptr;  // null: a heading (heading)
    const char* heading = nullptr;
    const char* desc = nullptr;
    enum Kind { Choice, Toggle, Slider } kind = Choice;
    std::vector<int> values;          // Choice: the values, names beside them
    std::vector<std::string> names;
    int lo = 0, hi = 0, step = 1;     // Slider
    const char* unit = "%";
    std::function<int()> get;
    std::function<void(int)> set;
    bool display = false;             // a change applies the display mode
};

std::mutex g_lock;
std::vector<Item> g_items;
int g_sel = 1;
bool g_open = false, g_dirty = false;
int64_t g_pause_until = 0;     // a Start press for the game's pause menu, while this opens
std::atomic<bool> g_open_flag{false};
std::deque<int> g_keys;        // keyboard navigation from the window thread: VK codes
std::atomic<bool> g_key_gate{false};  // after closing: keys kept from the game until let go

struct PortState {
    uint16_t prev = 0;             // directions and buttons last poll, for edges
    int64_t repeat_at = 0;         // the next repeat of a held direction
    uint16_t pending = 0;          // Start or Back held back, waiting for the other
    int64_t pending_since = 0;
    bool latched = false;          // let through after the wait, until let go
    uint16_t inject = 0;           // a tap let go during the wait, given late
    int64_t inject_until = 0;
    bool gate = false;             // after closing: kept from the game until neutral
};
PortState g_ports[4];

Item Heading(const char* text) {
    Item i;
    i.heading = text;
    return i;
}

Item Toggle(const char* label, const char* desc, std::function<bool()> get, std::function<void(bool)> set) {
    Item i;
    i.label = label;
    i.desc = desc;
    i.kind = Item::Toggle;
    i.get = [get] { return get() ? 1 : 0; };
    i.set = [set](int v) { set(v != 0); };
    return i;
}

Item Choice(const char* label, const char* desc, std::vector<int> values, std::vector<std::string> names,
            std::function<int()> get, std::function<void(int)> set) {
    Item i;
    i.label = label;
    i.desc = desc;
    i.kind = Item::Choice;
    i.values = std::move(values);
    i.names = std::move(names);
    i.get = std::move(get);
    i.set = std::move(set);
    return i;
}

Item SliderItem(const char* label, const char* desc, int lo, int hi, int step, const char* unit, std::function<int()> get,
                std::function<void(int)> set) {
    Item i;
    i.label = label;
    i.desc = desc;
    i.kind = Item::Slider;
    i.lo = lo;
    i.hi = hi;
    i.step = step;
    i.unit = unit;
    i.get = std::move(get);
    i.set = std::move(set);
    return i;
}

// the window's monitor's display modes, desktop first (0 x 0)
std::vector<std::pair<int, int>> Modes() {
    std::vector<std::pair<int, int>> out{{0, 0}};
    HMONITOR mon = MonitorFromWindow(static_cast<HWND>(app::Window()), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD n = 0; EnumDisplaySettingsW(mi.szDevice, n, &dm); n++) {
        std::pair<int, int> m{int(dm.dmPelsWidth), int(dm.dmPelsHeight)};
        if (m.first >= 1280 && m.second >= 720 && std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
    }
    std::sort(out.begin() + 1, out.end(), [](auto a, auto b) { return a.first * a.second > b.first * b.second; });
    return out;
}

void Build() {
    app::Settings& s = app::Get();
    std::vector<Item> v;
    v.push_back(Heading("WINDOW"));
    {
        Item i = Choice("Display mode",
                        "Fullscreen changes the monitor's mode; Borderless fills the screen with a window; Windowed opens "
                        "a window of the chosen resolution.",
                        {0, 1, 2}, {"Fullscreen", "Borderless", "Windowed"}, [&s] { return int(s.display_mode); },
                        [&s](int x) { s.display_mode = app::DisplayMode(x); });
        i.display = true;
        v.push_back(std::move(i));
    }
    {
        auto modes = Modes();
        std::vector<int> idx;
        std::vector<std::string> names;
        for (size_t n = 0; n < modes.size(); n++) {
            idx.push_back(int(n));
            names.push_back(modes[n].first ? std::to_string(modes[n].first) + " x " + std::to_string(modes[n].second)
                                           : "Desktop");
        }
        Item i = Choice(
            "Resolution", "The size of the game's picture. Desktop uses your monitor's current resolution.", idx, names,
            [&s, modes] {
                for (size_t n = 0; n < modes.size(); n++)
                    if (uint32_t(modes[n].first) == s.width && uint32_t(modes[n].second) == s.height) return int(n);
                return 0;
            },
            [&s, modes](int n) {
                s.width = uint32_t(modes[size_t(n)].first);
                s.height = uint32_t(modes[size_t(n)].second);
            });
        i.display = true;
        v.push_back(std::move(i));
    }
    v.push_back(Choice("Aspect ratio",
                       "16:9 shows a wider view of the game with the menus and HUD kept in proportion (on a wider monitor, "
                       "such as 21:9, it follows the monitor). 4:3 is the Xbox picture with bars at the sides; Stretched "
                       "pulls the 4:3 picture wide.",
                       {0, 1, 2}, {"16:9", "4:3", "Stretched"}, [&s] { return int(s.aspect); },
                       [&s](int x) { s.aspect = uint32_t(x); }));
    v.push_back(Choice("HUD position",
                       "Where the gauges in the HUD's top corners go while you play: in the 4:3 middle as on the Xbox, out "
                       "to a 16:9 area (for 21:9 and wider monitors), or out to the screen's edges. The item wheel, tips, "
                       "menus and subtitles stay in the middle.",
                       {0, 1, 2}, {"Middle (4:3)", "16:9", "Screen edges"}, [&s] { return int(s.hud_position); },
                       [&s](int x) { s.hud_position = uint32_t(x); }));
    v.push_back(Toggle("VSync", "Waits for the monitor's refresh: no tearing. Off gives the lowest latency.",
                       [&s] { return s.vsync; }, [&s](bool x) { s.vsync = x; }));

    v.push_back(Heading("RENDERING"));
    v.push_back(SliderItem("Render scale",
                           "Draws the game at this share of the resolution: above 100% is supersampling (sharper, slower), "
                           "below is faster.",
                           50, 200, 10, "%", [&s] { return int(s.render_percent); },
                           [&s](int x) { s.render_percent = uint32_t(x); }));
    v.push_back(Choice("Frame rate limit",
                       "The Xbox ran the game at 30 frames a second. The game keeps its speed at any frame rate.",
                       {30, 60, int(app::kFpsCapRefresh), 120, 144, 165, 240, 0},
                       {"30", "60", "Monitor refresh rate", "120", "144", "165", "240", "Unlimited"},
                       [&s] { return int(s.fps_cap); }, [&s](int x) { s.fps_cap = uint32_t(x); }));
    v.push_back(Choice("Draw distance",
                       "How far each level draws. The game cuts the view off at each level's own distance (Ceylon: 5555 "
                       "units); longer distances show more of large outdoor areas at a small cost. Applied when a level "
                       "loads.",
                       {100, 150, 200, 300, 400}, {"Original (100%)", "150%", "200%", "300%", "400%"},
                       [] { return int(g_draw_distance); }, [](int x) { g_draw_distance = uint32_t(x); }));
    v.push_back(SliderItem("Field of view",
                           "How wide the camera sees, as a share of the game's own view (100%). Wider shows more around "
                           "Indy.",
                           70, 120, 5, "%", [&s] { return int(s.fov); }, [&s](int x) { s.fov = uint32_t(x); }));
    v.push_back(Toggle("Anti-aliasing (FXAA)",
                       "Smooths jagged edges at almost no cost. It softens textures a little; sharpening makes up for it.",
                       [&s] { return s.fxaa; }, [&s](bool x) { s.fxaa = x; }));
    v.push_back(Toggle("Fill seams",
                       "Fills the hairline cracks between the level's surfaces that show at high resolutions as pale dots "
                       "along the edges of stones and boxes.",
                       [&s] { return s.seam_fill; }, [&s](bool x) { s.seam_fill = x; }));
    v.push_back(SliderItem("Sharpening",
                           "Contrast adaptive sharpening of the final picture: the game's low resolution textures look "
                           "crisper, without halos on edges. 0% turns it off.",
                           0, 100, 5, "%", [&s] { return int(s.sharpen); }, [&s](int x) { s.sharpen = uint32_t(x); }));
    v.push_back(Toggle("Sharper movies",
                       "The game's videos are 640 x 480. This scales them up with a sharper filter than the usual blur.",
                       [&s] { return s.video_filter; }, [&s](bool x) { s.video_filter = x; }));
    v.push_back(Toggle("Show FPS counter",
                       "The game's frames per second and frame time in the top left corner. F9 in the game turns it on "
                       "and off.",
                       [] { return g_show_fps; }, [](bool x) { g_show_fps = x; }));

    v.push_back(Heading("CONTROLS"));
    v.push_back(SliderItem("Vibration", "Strength of the controller's vibration; 0% turns it off.", 0, 100, 5, "%",
                           [] { return int(g_rumble); }, [](int x) { g_rumble = uint32_t(x); }));
    v.push_back(Toggle("Mouse look",
                       "The mouse turns the camera one to one, as far as it moves, in third and first person, and the "
                       "camera stays where you leave it; the right stick or the camera keys bring back the game's own "
                       "camera that swings behind Indy. The mouse counts only while playing: menus take keys. There is "
                       "no cursor in the game window.",
                       [] { return g_mouse_look; }, [](bool x) { g_mouse_look = x; }));
    v.push_back(SliderItem("Mouse sensitivity", nullptr, 1, 100, 1, "", [] { return int(g_mouse_sens); },
                           [](int x) { g_mouse_sens = uint32_t(x); }));
    v.push_back(Toggle("Invert vertical", nullptr, [] { return g_mouse_invert; }, [](bool x) { g_mouse_invert = x; }));
    g_items = std::move(v);
}

void Step(int dir) {
    int n = int(g_items.size());
    for (int k = 0; k < n; k++) {
        g_sel = (g_sel + dir + n) % n;
        if (g_items[size_t(g_sel)].label) return;
    }
}

// left or right on the chosen item (dir -1, 1), or A / Enter (0): toggles flip, choices go round, sliders step
void Change(int dir) {
    Item& i = g_items[size_t(g_sel)];
    if (!i.label) return;
    int v = i.get(), nv = v;
    if (i.kind == Item::Toggle) {
        nv = v ? 0 : 1;
    } else if (i.kind == Item::Choice) {
        int n = int(i.values.size()), at = 0;
        for (int k = 0; k < n; k++)
            if (i.values[size_t(k)] == v) at = k;
        at = dir < 0 ? (at + n - 1) % n : (at + 1) % n;
        nv = i.values[size_t(at)];
    } else {
        if (dir == 0) return;
        nv = std::clamp(v + dir * i.step, i.lo, i.hi);
    }
    if (nv == v) return;
    i.set(nv);
    g_dirty = true;
    if (i.display) app::ApplyDisplay();
}

void Open() {
    Build();
    g_sel = 0;
    Step(1);
    g_open = true;
    g_open_flag = true;
    g_dirty = false;
    g_keys.clear();
    // playing: the game's pause menu first, so the game stands still behind this
    g_pause_until = HudSpread() ? NowMs() + 120 : 0;
    LOGR("menu: opened");
}

void Close() {
    g_open = false;
    g_open_flag = false;
    for (PortState& p : g_ports) p.gate = true;
    g_key_gate = true;
    if (g_dirty) {
        app::Save();
        LOGR("menu: settings saved");
    }
    g_dirty = false;
}

bool Neutral(const input::Pad& pad) {
    if (pad.buttons) return false;
    for (uint8_t a : pad.analog)
        if (a > 40) return false;
    auto small = [](int16_t v) { return v > -12000 && v < 12000; };
    return small(pad.lx) && small(pad.ly) && small(pad.rx) && small(pad.ry);
}

void Clear(input::Pad& pad) {
    pad.buttons = 0;
    std::fill(std::begin(pad.analog), std::end(pad.analog), uint8_t(0));
    pad.lx = pad.ly = pad.rx = pad.ry = 0;
}

}  // namespace

bool MenuOpen() { return g_open_flag.load(); }

// keys held when the menu closed are kept from the game until let go
bool MenuKeysGated() {
    if (!g_key_gate) return false;
    const int keys[] = {VK_ESCAPE, VK_RETURN, VK_SPACE, VK_BACK, VK_F1, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
    for (int k : keys)
        if (GetAsyncKeyState(k) & 0x8000) return true;
    g_key_gate = false;
    return false;
}

// the game window is up: the menu's fonts, made for the monitor's height before the renderer builds the atlas
void MenuFonts(void* hwnd) {
    std::lock_guard ui(app::UiLock());
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(static_cast<HWND>(hwnd), MONITOR_DEFAULTTOPRIMARY), &mi);
    float h = float(mi.rcMonitor.bottom - mi.rcMonitor.top);
    g_baked = std::clamp(std::round(h / 1080.0f * 24.0f), 16.0f, 64.0f);
    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();  // stays the default font (the frame counter)
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0x2010, 0x2027, 0x2190, 0x21FF, 0};
    auto add = [&](const char* file, float size) -> ImFont* {
        std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
        return io.Fonts->AddFontFromFileTTF(path.c_str(), size, nullptr, ranges);
    };
    g_font = add("segoeui.ttf", g_baked);
    g_title = add("georgiab.ttf", g_baked * 1.5f);
}

// the window's messages: F1 opens and closes; while open, the keys that move the menu
bool MenuMessage(uint32_t msg, uintptr_t wp) {
    if (msg != WM_KEYDOWN) return false;
    if (wp == VK_F1) {
        std::lock_guard l(g_lock);
        g_keys.push_back(VK_F1);
        return true;
    }
    if (!g_open_flag) return false;
    switch (wp) {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT: case VK_RETURN: case VK_SPACE: case VK_ESCAPE:
        case VK_BACK: {
            std::lock_guard l(g_lock);
            g_keys.push_back(int(wp));
            return true;
        }
    }
    return false;
}

// each polled pad (after the gamepad mapping and the autopilot, before the keyboard): Back and Start together open
// the menu; while it is open its navigation, and nothing reaches the game; true: the keyboard is kept out too
bool MenuPad(int port, input::Pad& pad) {
    if (port < 0 || port > 3) return false;
    std::lock_guard l(g_lock);
    PortState& p = g_ports[port];
    int64_t now = NowMs();

    // keys from the window: F1 toggles, the rest move the menu
    if (port == 0) {
        while (!g_keys.empty()) {
            int k = g_keys.front();
            g_keys.pop_front();
            if (k == VK_F1) {
                if (g_open) Close();
                else Open();
                continue;
            }
            if (!g_open) continue;
            if (k == VK_UP) Step(-1);
            if (k == VK_DOWN) Step(1);
            if (k == VK_LEFT) Change(-1);
            if (k == VK_RIGHT) Change(1);
            if (k == VK_RETURN || k == VK_SPACE) Change(0);
            if (k == VK_ESCAPE || k == VK_BACK) Close();
        }
    }

    if (g_open) {
        // the controller moves the menu: directions repeat while held, A changes, B (or Back and Start) closes
        uint16_t dirs = pad.buttons & (kUp | kDown | kLeft | kRight);
        if (pad.ly > 16000) dirs |= kUp;
        if (pad.ly < -16000) dirs |= kDown;
        if (pad.lx < -16000) dirs |= kLeft;
        if (pad.lx > 16000) dirs |= kRight;
        uint16_t now_held = dirs | (pad.analog[0] > 128 ? 0x100 : 0) | (pad.analog[1] > 128 ? 0x200 : 0) |
                            (pad.buttons & (kStart | kBack));
        uint16_t pressed = now_held & ~p.prev;
        bool repeat = dirs && (dirs & p.prev) == dirs && now >= p.repeat_at;
        if (pressed & dirs) p.repeat_at = now + 380;
        if (repeat) p.repeat_at = now + 90;
        uint16_t act = (pressed & dirs) | (repeat ? dirs : 0);
        if (!p.gate) {
            if (act & kUp) Step(-1);
            if (act & kDown) Step(1);
            if (act & kLeft) Change(-1);
            if (act & kRight) Change(1);
            if (pressed & 0x100) Change(0);
            if ((pressed & 0x200) || ((now_held & (kStart | kBack)) == (kStart | kBack) && (pressed & (kStart | kBack))))
                Close();
        }
        p.prev = now_held;
        if (p.gate && Neutral(pad)) p.gate = false;
        Clear(pad);
        if (port == 0) {
            pad.connected = true;
            if (now < g_pause_until) pad.buttons |= kStart;
        }
        return g_open || port == 0;
    }

    // closed: a button still held from the menu stays out of the game until let go
    p.prev = 0;
    if (p.gate) {
        if (Neutral(pad)) p.gate = false;
        else {
            Clear(pad);
            return port == 0 && MenuKeysGated();
        }
    }

    // Back and Start together open it; either alone reaches the game after a short wait
    uint16_t sb = pad.buttons & (kStart | kBack);
    if (sb == (kStart | kBack)) {
        p.pending = 0;
        p.latched = false;
        Open();
        p.gate = true;
        Clear(pad);
        return true;
    }
    if (p.pending) {
        if (!sb) {
            p.inject = p.pending;  // let go before the wait ended: a short press, given now
            p.inject_until = now + 80;
            p.pending = 0;
        } else if (now - p.pending_since > 150) {
            p.pending = 0;
            p.latched = true;
        } else {
            pad.buttons &= uint16_t(~(kStart | kBack));
        }
    } else if (sb && !p.latched) {
        p.pending = sb;
        p.pending_since = now;
        pad.buttons &= uint16_t(~(kStart | kBack));
    }
    if (!sb) p.latched = false;
    if (p.inject && now < p.inject_until) pad.buttons |= p.inject;
    else p.inject = 0;
    return port == 0 && MenuKeysGated();
}

// each frame, inside the ImGui frame (render thread)
void MenuDraw() {
    std::lock_guard l(g_lock);
    if (!g_open || g_items.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    float W = io.DisplaySize.x, H = io.DisplaySize.y;
    float u = H / 1080.0f;  // a unit at this height
    ImFont* font = g_font ? g_font : ImGui::GetFont();
    ImFont* title = g_title ? g_title : font;
    float fs = 24 * u, ts = 36 * u;

    d->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 150));
    float pw = std::min(W - 80 * u, 1180 * u), ph = std::min(H - 80 * u, 900 * u);
    ImVec2 o((W - pw) * 0.5f, (H - ph) * 0.5f), q(o.x + pw, o.y + ph);
    d->AddRectFilled(o, q, kPanel, 14 * u);
    d->AddRect(o, q, kLine, 14 * u, 0, 1.5f * u);

    float pad = 40 * u, x0 = o.x + pad, x1 = q.x - pad;
    d->AddCircleFilled(ImVec2(x0 + 6 * u, o.y + 46 * u), 6 * u, kAccent);
    d->AddText(font, fs * 0.85f, ImVec2(x0 + 22 * u, o.y + 34 * u), kDim, "IndyPort");
    d->AddText(title, ts, ImVec2(x0, o.y + 62 * u), kText, T("SETTINGS"));

    // the rows, scrolled to keep the chosen one in view
    float top = o.y + 122 * u, desc_h = 110 * u, foot_h = 50 * u, bottom = q.y - desc_h - foot_h;
    float row = 46 * u, head = 58 * u;
    std::vector<float> ys;
    float y = 0;
    for (const Item& i : g_items) {
        ys.push_back(y);
        y += i.label ? row : head;
    }
    float view = bottom - top, sel_y = ys[size_t(g_sel)];
    static float scroll = 0;
    if (sel_y - scroll > view - row * 1.5f) scroll = sel_y - view + row * 1.5f;
    if (sel_y - scroll < row) scroll = std::max(0.0f, sel_y - row * 1.5f);
    scroll = std::clamp(scroll, 0.0f, std::max(0.0f, y - view));
    d->PushClipRect(ImVec2(o.x, top), ImVec2(q.x, bottom), true);
    float vx = x0 + (x1 - x0) * 0.55f;  // the values' column
    for (size_t n = 0; n < g_items.size(); n++) {
        const Item& i = g_items[n];
        float iy = top + ys[n] - scroll;
        if (iy > bottom || iy + head < top) continue;
        if (!i.label) {
            d->AddText(title, fs * 0.95f, ImVec2(x0, iy + 20 * u), kAccent, T(i.heading));
            d->AddLine(ImVec2(x0, iy + head - 4 * u), ImVec2(x1, iy + head - 4 * u), kLine, 1.2f * u);
            continue;
        }
        bool sel = int(n) == g_sel;
        if (sel) {
            d->AddRectFilled(ImVec2(x0 - 14 * u, iy + 3 * u), ImVec2(x1 + 14 * u, iy + row - 3 * u), kSel, 8 * u);
            d->AddRect(ImVec2(x0 - 14 * u, iy + 3 * u), ImVec2(x1 + 14 * u, iy + row - 3 * u), IM_COL32(236, 160, 44, 150),
                       8 * u, 0, 1.5f * u);
        }
        float ty = iy + (row - fs) * 0.5f;
        d->AddText(font, fs, ImVec2(x0, ty), sel ? kText : IM_COL32(214, 204, 192, 255), T(i.label));
        int v = i.get();
        if (i.kind == Item::Toggle) {
            float b = 26 * u, bx = vx, by = iy + (row - b) * 0.5f;
            d->AddRect(ImVec2(bx, by), ImVec2(bx + b, by + b), sel ? kAccent : kDim, 4 * u, 0, 1.5f * u);
            if (v) {
                d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + b, by + b), IM_COL32(236, 147, 61, 255), 4 * u);
                d->AddLine(ImVec2(bx + b * 0.25f, by + b * 0.52f), ImVec2(bx + b * 0.43f, by + b * 0.7f), kText, 2.2f * u);
                d->AddLine(ImVec2(bx + b * 0.43f, by + b * 0.7f), ImVec2(bx + b * 0.76f, by + b * 0.3f), kText, 2.2f * u);
            }
            d->AddText(font, fs, ImVec2(bx + b + 14 * u, ty), kDim, T(v ? "On" : "Off"));
        } else if (i.kind == Item::Choice) {
            std::string name;
            for (size_t k = 0; k < i.values.size(); k++)
                if (i.values[k] == v) name = T(i.names[k].c_str());
            if (name.empty() && !i.names.empty()) name = T(i.names[0].c_str());
            float w = x1 - vx;
            ImVec2 ns = font->CalcTextSizeA(fs, FLT_MAX, 0, name.c_str());
            d->AddText(font, fs, ImVec2(vx + (w - ns.x) * 0.5f, ty), kText, name.c_str());
            ImU32 ac = sel ? kAccent : IM_COL32(110, 100, 90, 255);
            d->AddText(font, fs, ImVec2(vx, ty), ac, "\xe2\x86\x90");
            d->AddText(font, fs, ImVec2(x1 - fs * 0.9f, ty), ac, "\xe2\x86\x92");
        } else {
            float w = x1 - vx - 90 * u, by = iy + row * 0.5f;
            float t = float(v - i.lo) / float(std::max(1, i.hi - i.lo));
            d->AddRectFilled(ImVec2(vx, by - 3 * u), ImVec2(vx + w, by + 3 * u), IM_COL32(52, 44, 38, 255), 3 * u);
            d->AddRectFilled(ImVec2(vx, by - 3 * u), ImVec2(vx + w * t, by + 3 * u), IM_COL32(236, 147, 61, 255), 3 * u);
            d->AddCircleFilled(ImVec2(vx + w * t, by), 9 * u, sel ? kAccent : IM_COL32(200, 150, 90, 255));
            std::string text = std::to_string(v) + i.unit;
            d->AddText(font, fs, ImVec2(x1 - 70 * u, ty), kText, text.c_str());
        }
    }
    d->PopClipRect();

    // the chosen item's description, and the keys
    d->AddLine(ImVec2(x0, bottom + 8 * u), ImVec2(x1, bottom + 8 * u), kLine, 1.2f * u);
    if (const char* desc = g_items[size_t(g_sel)].desc)
        d->AddText(font, fs * 0.82f, ImVec2(x0, bottom + 22 * u), kDim, T(desc), nullptr, x1 - x0);
    d->AddText(font, fs * 0.8f, ImVec2(x0, q.y - foot_h + 6 * u), IM_COL32(190, 178, 160, 255),
               T("Up / Down: choose   |   Left / Right, A or Enter: change   |   B, Esc or F1: close. "
                 "Changes apply at once and are saved."));
}

}  // namespace game
