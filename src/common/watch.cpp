// Write watch for debugging: INDY_WATCH=<hex guest address>,<hex length> logs the host code that writes there.
// The guest page is made read only; a write faults, the handler counts the faulting host address (an address in the
// game code DLL maps to its recompiled function with tools/watch_map.py), lets that one instruction run with the
// trap flag, then protects the page again. Every distinct writer is logged on its first hit, and all of them with
// their counts every 5 seconds. Writes to the rest of the page pass the same way without being counted.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "core/log.h"
#include "kernel/kernel.h"

namespace game {

namespace {

uint8_t* g_page = nullptr;  // host address of the watched page
uintptr_t g_lo = 0, g_hi = 0;  // watched host range
std::mutex g_lock;
std::map<uintptr_t, std::pair<uint64_t, uint32_t>> g_hits;  // host rip -> count, first guest address written
thread_local bool t_stepping = false;

uintptr_t ModuleBase(uintptr_t rip, char* name, size_t n) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(rip), &m) ||
        !m) {
        name[0] = 0;
        return 0;
    }
    char path[MAX_PATH] = {};
    GetModuleFileNameA(m, path, MAX_PATH);
    const char* base = std::strrchr(path, '\\');
    strncpy_s(name, n, base ? base + 1 : path, _TRUNCATE);
    return reinterpret_cast<uintptr_t>(m);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2 && r->ExceptionInformation[0] == 1 &&
        g_page) {
        uintptr_t at = r->ExceptionInformation[1];
        uintptr_t page = reinterpret_cast<uintptr_t>(g_page);
        if (at < page || at >= page + 0x1000) return EXCEPTION_CONTINUE_SEARCH;
        uintptr_t rip = e->ContextRecord->Rip;
        if (at >= g_lo && at < g_hi) {
            std::lock_guard lock(g_lock);
            auto [it, fresh] = g_hits.try_emplace(rip, 0, uint32_t(at - reinterpret_cast<uintptr_t>(kernel::g_base)));
            it->second.first++;
            if (fresh) {
                char mod[64];
                uintptr_t base = ModuleBase(rip, mod, sizeof(mod));
                LOGR("watch: new writer %s+%llX writes %08X", mod, (unsigned long long)(rip - base), it->second.second);
                // the guest calls that led here, on this guest thread
                if (kernel::Current()) kernel::LogGuestStack(kernel::CurrentContext(), 96);
            }
        }
        DWORD old;
        VirtualProtect(g_page, 0x1000, PAGE_READWRITE, &old);
        e->ContextRecord->EFlags |= 0x100;  // trap flag: back here after this one instruction
        t_stepping = true;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && t_stepping) {
        t_stepping = false;
        DWORD old;
        VirtualProtect(g_page, 0x1000, PAGE_READONLY, &old);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void Loop(uint32_t guest, uint32_t len) {
    // the guest memory exists once the image boots
    for (int i = 0; i < 600 && !kernel::g_base; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::this_thread::sleep_for(std::chrono::seconds(5));
    uint8_t* host = kernel::g_base + guest;
    g_page = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(host) & ~uintptr_t(0xFFF));
    g_lo = reinterpret_cast<uintptr_t>(host);
    g_hi = g_lo + len;
    AddVectoredExceptionHandler(1, Handler);
    DWORD old;
    if (!VirtualProtect(g_page, 0x1000, PAGE_READONLY, &old)) {
        LOGR("watch: cannot protect %08X (%lu)", guest & ~0xFFFu, GetLastError());
        return;
    }
    LOGR("watch: %08X..%08X", guest, guest + len);
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::lock_guard lock(g_lock);
        for (auto& [rip, v] : g_hits) {
            char mod[64];
            uintptr_t base = ModuleBase(rip, mod, sizeof(mod));
            LOGR("watch: %s+%llX %llu writes, first %08X", mod, (unsigned long long)(rip - base),
                 (unsigned long long)v.first, v.second);
        }
    }
}

}  // namespace

void StartWatch() {
    const char* v = std::getenv("INDY_WATCH");
    if (!v || !*v) return;
    char* end = nullptr;
    uint32_t guest = uint32_t(std::strtoul(v, &end, 16));
    uint32_t len = end && *end == ',' ? uint32_t(std::strtoul(end + 1, nullptr, 16)) : 0x100;
    std::thread(Loop, guest, len).detach();
}

}  // namespace game
