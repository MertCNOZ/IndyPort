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
extern uint32_t g_fov;            // game_info.cpp: fov in the settings, percent of the game's field of view
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
// The fov setting widens or narrows both half angles alike (their tangents scaled), on top of the widescreen.
X86_FUNC(sub_00323160) {
    float k = app::WideFactor();
    float f = float(std::clamp(game::g_fov, 70u, 120u)) / 100.0f;
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
