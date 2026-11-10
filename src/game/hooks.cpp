// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "app/app.h"
#include "core/log.h"

#include "xdk/hle.h"

namespace game {
extern uint32_t g_draw_distance;  // game_info.cpp: draw_distance in the settings, percent
}

// The level's far clip: 0x323A10 takes the distance (5555 in Ceylon), keeps it under 65536 at 0x6D7614 with its
// reciprocal at 0x6D7618 and rebuilds the projection (0x323160). The camera culls against the same far plane, so a
// larger distance draws more of the level's far geometry. Scaled here by draw_distance.
X86_EXTERN_FUNC(__imp__sub_00323A10);
X86_FUNC(sub_00323A10) {
    uint32_t percent = game::g_draw_distance;
    if (percent != 100 && percent >= 25 && percent <= 1000) {
        float far = X86_MEMF32(ctx.esp + 4);
        if (far > 0) X86_MEMF32(ctx.esp + 4) = far * float(percent) / 100.0f;
    }
    __imp__sub_00323A10(ctx, base);
}

// Widescreen without stretching (Hor+): 0x323160 builds the camera's projection and its culling planes from the
// horizontal and vertical half angles at 0x6D75F0 and 0x6D75F4 (0x323AE0 sets the horizontal one and derives the
// vertical one from the 640x480 viewport). While it runs, the horizontal half angle is widened by app::WideFactor
// (tan scaled), so the view is wider and nothing at the new edges is culled; the rest of the game keeps seeing the
// angle it set. The 4:3 frame is then shown at the screen's aspect, where the widened view has the right proportions.
X86_EXTERN_FUNC(__imp__sub_00323160);
// The fov setting widens or narrows both half angles alike (their tangents scaled), on top of the widescreen; the
// renderer scales the HUD by the same (d3d12.cpp), so the inventory wheel's 3D items stay in their rings.
X86_FUNC(sub_00323160) {
    float k = app::WideFactor();
    float f = float(std::clamp(app::Get().fov, 70u, 120u)) / 100.0f;
    float h = X86_MEMF32(0x6D75F0), v = X86_MEMF32(0x6D75F4);
    bool widen = (k > 1.01f || std::fabs(f - 1) > 0.001f) && h > 0.05f && h < 1.5f && v > 0.05f && v < 1.5f;
    static bool told = false;
    if (!told && widen) {
        told = true;
        LOGR("view: horizontal half angle %.4f rad -> %.4f (widescreen x%.3f, fov %.2f)", h,
             std::atan(std::tan(h) * k * f), k, f);
    }
    if (widen) {
        X86_MEMF32(0x6D75F0) = std::atan(std::tan(h) * k * f);
        X86_MEMF32(0x6D75F4) = std::atan(std::tan(v) * f);
    }
    __imp__sub_00323160(ctx, base);
    if (widen) {
        X86_MEMF32(0x6D75F0) = h;
        X86_MEMF32(0x6D75F4) = v;
    }
}


namespace game {
bool TakeTextKey(uint8_t& dik);  // keyboard.cpp: keys typed while a name box is up
void NoteTextBox();              // keyboard.cpp: a name box is up this frame
}

// Typing names (New Game's "Enter a name"). The game's text box class (vtable 0x58DD00) keeps its text at +0x2C,
// its cursor at +0x12C and its length limit at +0x13C, and 0x22D700 is its key handler: DirectInput key codes,
// letters and digits through the game's key table (0x64B520, made upper case), Backspace, Delete, the arrows. Only
// an Xbox keyboard ever fed it. 0x22DCE0 runs each frame while the box is up: after it, the keys typed on the PC
// keyboard are handed to the box's own handler on the game's thread, so the game edits the name itself.
X86_EXTERN_FUNC(__imp__sub_0022DCE0);
X86_EXTERN_FUNC(__imp__sub_0022D700);
X86_FUNC(sub_0022DCE0) {
    uint32_t self = ctx.ecx;
    __imp__sub_0022DCE0(ctx, base);
    if (!self || X86_MEM32(self) != 0x58DD00u) return;
    game::NoteTextBox();
    uint8_t dik;
    while (game::TakeTextKey(dik)) {
        X86Context saved = ctx;
        X86_PUSH32(uint32_t(dik));
        X86_PUSH32(0x0022DCE0u);  // a return address: the handler returns with ret 4
        ctx.ecx = self;
        __imp__sub_0022D700(ctx, base);
        ctx = saved;
    }
}

namespace game {
void TakeMouseCamera(float& yaw, float& pitch, bool& mouse_has_camera);  // keyboard.cpp: mouse movement, radians
void NoteCameraStick();                                                // keyboard.cpp: the stick moved the camera
}

// Mouse look, one to one. The third person camera's update (0x10B360, this = the camera) has no yaw angle: each
// frame it takes the flat vector from the camera to Indy (0x10B1A0: [this+0x100] target minus [this+0x130]
// position), turns it by its yaw speed ([this+0x7C], from the right stick through 0x1040B0, squared, with inertia,
// plus [this+0x84], the drift back behind Indy) and puts the camera there; the pitch is an angle at [this+0x68]
// (less [this+0x80], its speed, kept within limits). The mouse turns that vector and that angle directly, by the
// movement since the last frame, so the camera follows the mouse without the stick's speed curve and inertia.
// While the mouse rather than the stick moved the camera last, the camera input it reads is made a hair above zero
// (0x1040B0 out), which the camera takes as "the player is steering": it does not drift back behind Indy.
namespace {
float g_mouse_yaw = 0;
bool g_mouse_steers = false;
}

X86_EXTERN_FUNC(__imp__sub_0010B360);
X86_FUNC(sub_0010B360) {
    uint32_t self = ctx.ecx;
    float yaw = 0, pitch = 0;
    bool steers = false;
    game::TakeMouseCamera(yaw, pitch, steers);
    g_mouse_yaw = yaw;
    g_mouse_steers = steers;
    if (pitch != 0) {
        float p = X86_MEMF32(self + 0x68);
        if (std::isfinite(p)) X86_MEMF32(self + 0x68) = p + pitch;
    }
    __imp__sub_0010B360(ctx, base);
    g_mouse_yaw = 0;
    g_mouse_steers = false;
}

X86_EXTERN_FUNC(__imp__sub_0010B1A0);
X86_FUNC(sub_0010B1A0) {
    uint32_t ret = X86_MEM32(ctx.esp), out = X86_MEM32(ctx.esp + 4);
    __imp__sub_0010B1A0(ctx, base);
    if (ret != 0x10B513u || g_mouse_yaw == 0) return;
    float x = X86_MEMF32(out), z = X86_MEMF32(out + 8), c = std::cos(g_mouse_yaw), s = std::sin(g_mouse_yaw);
    X86_MEMF32(out) = x * c - z * s;
    X86_MEMF32(out + 8) = x * s + z * c;
}

X86_EXTERN_FUNC(__imp__sub_001040B0);
X86_FUNC(sub_001040B0) {
    uint32_t ret = X86_MEM32(ctx.esp), look = X86_MEM32(ctx.esp + 8);  // the camera's x and y
    __imp__sub_001040B0(ctx, base);
    if (ret != 0x10B3F9u) return;
    if (X86_MEMF32(look) != 0 || X86_MEMF32(look + 4) != 0) game::NoteCameraStick();
    else if (g_mouse_steers) X86_MEMF32(look) = 1e-4f;
}
