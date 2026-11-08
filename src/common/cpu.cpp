// The game's threads all run on one host core, as on the single CPU Xbox. Its render lock (0x329FC0 takes it,
// 0x32A0A0 releases it) is a plain count with no atomic instructions: with the threads in parallel two updates can
// land at once and leave the count at 1 with no owner, which hung the game on a Loading screen. One modern core is
// many times the console's 733 MHz; the renderer, audio and other host threads keep the rest of the CPU.
#include <cstdint>

#include "core/log.h"
#include "kernel/kernel.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace game {

void PinGuestThreads() {
    DWORD_PTR process = 0, system = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system) || !process) return;
    // the second CPU the process may use, else the first: CPU 0 takes most of the system's interrupts
    DWORD_PTR first = process & (~process + 1);
    DWORD_PTR rest = process & ~first;
    DWORD_PTR pick = rest ? rest & (~rest + 1) : first;
    kernel::g_guest_affinity = uint64_t(pick);
    LOGR("cpu: game threads on CPU mask %llX", (unsigned long long)pick);
}

}  // namespace game
