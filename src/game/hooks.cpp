// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <cstdint>

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
