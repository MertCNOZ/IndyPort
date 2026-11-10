// Autopilot: drives port 0's controller from a command file, so the game can be tested without its window in front
// or anyone at the keyboard. Off unless INDY_AUTOPILOT names a folder. A thread reads new lines appended to
// <folder>\cmd.txt every 50 ms and acknowledges each one in <folder>\ack.txt.
//
//   press <button> [seconds]           tap a button, 0.15 s by default
//   hold <button> <seconds>            hold it
//   stick <l|r> <x> <y> <seconds>      tilt a stick, x and y from -1 to 1, y up
//   capture                            save the next frame under XBE_CAPTURE_DIR (as F3 does)
//   find <text>                        log where the text is in guest memory, as ASCII and UTF-16
//   peek <hex address> [bytes]         log guest memory there
//   poke <hex address> <float>         write a float there
//   scan start                         every float in guest memory becomes a candidate
//   scan changed|same|inc|dec          keep the candidates whose float did that since the last scan
//   scan eq <value> [tolerance]        keep the candidates now at the value (0.001 by default)
//   scan mark                          remember the candidates' values now
//   scan back [tolerance]              keep the candidates back at their marked values
//   scan list [n]                      log the candidates (the first 60, or n up to 1000)
//   watch <hex address> [w|rw]         a hardware breakpoint there on every thread: which code writes (or reads)
//   watch list                         log the code that hit it, as function names from the PDB, with counts
//   watch off                          remove it
//   # ...                              comment
//
// buttons: a b x y black white lt rt start back up down left right lthumb rthumb
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>
#pragma comment(lib, "dbghelp.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"
#include "kernel/kernel.h"

namespace game {

namespace {

using Clock = std::chrono::steady_clock;

struct Action {
    int kind;  // 0 digital mask, 1 analog index, 2 stick
    uint16_t mask = 0;
    int index = 0;
    float x = 0, y = 0;
    Clock::time_point until;
};

std::mutex g_lock;
std::vector<Action> g_actions;
std::string g_dir;

bool Button(const std::string& name, Action& a) {
    struct Digital { const char* name; uint16_t mask; };
    constexpr Digital kDigital[] = {{"up", 0x1},    {"down", 0x2},   {"left", 0x4},    {"right", 0x8},
                                    {"start", 0x10}, {"back", 0x20}, {"lthumb", 0x40}, {"rthumb", 0x80}};
    for (const Digital& d : kDigital)
        if (name == d.name) {
            a.kind = 0;
            a.mask = d.mask;
            return true;
        }
    constexpr const char* kAnalog[] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
    for (int i = 0; i < 8; i++)
        if (name == kAnalog[i]) {
            a.kind = 1;
            a.index = i;
            return true;
        }
    return false;
}

// committed guest memory only: the 64 MB has reserved holes
bool Readable(uint32_t addr) {
    MEMORY_BASIC_INFORMATION mi{};
    if (!VirtualQuery(kernel::g_base + addr, &mi, sizeof(mi))) return false;
    return mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD));
}

// every place the text is, as ASCII and as UTF-16, logged (the first 60)
int FindText(const std::string& text) {
    if (text.empty() || !kernel::g_base) return 0;
    std::string wide;
    for (char ch : text) {
        wide += ch;
        wide += '\0';
    }
    int found = 0;
    for (uint32_t page = 0x10000; page < 0x4000000 && found < 60; page += 0x1000) {
        if (!Readable(page)) continue;
        const uint8_t* p = kernel::g_base + page;
        for (const std::string* t : {&text, static_cast<const std::string*>(&wide)})
            for (uint32_t o = 0; o + t->size() <= 0x1000 && found < 60; o++)
                if (std::memcmp(p + o, t->data(), t->size()) == 0) {
                    LOGR("find: %08X %s", page + o, t == &text ? "ascii" : "utf-16");
                    found++;
                }
    }
    return found;
}

void Peek(uint32_t addr, uint32_t n) {
    if (!kernel::g_base || !Readable(addr) || !Readable(addr + n - 1)) {
        LOGR("peek: %08X not readable", addr);
        return;
    }
    std::string line;
    char buf[8];
    for (uint32_t i = 0; i < n; i++) {
        if (i % 16 == 0) {
            if (!line.empty()) LOGR("peek: %s", line.c_str());
            std::snprintf(buf, sizeof(buf), "%06X", addr + i);
            line = std::string(buf) + ":";
        }
        std::snprintf(buf, sizeof(buf), " %02X", kernel::g_base[addr + i]);
        line += buf;
    }
    LOGR("peek: %s", line.c_str());
}

// a search for a value in guest memory by how it changes (as a memory scanner does): a copy of the 64 MB and a
// candidate bit per float
std::vector<uint8_t> g_scan_copy;
std::vector<uint64_t> g_scan_bits;
std::vector<uint8_t> g_scan_mark;
size_t g_scan_count = 0;
constexpr uint32_t kScanLo = 0x10000, kScanHi = 0x4000000;

bool Candidate(uint32_t addr) {
    uint32_t i = addr >> 2;
    return (g_scan_bits[i >> 6] >> (i & 63)) & 1;
}

void Drop(uint32_t addr) {
    uint32_t i = addr >> 2;
    g_scan_bits[i >> 6] &= ~(uint64_t(1) << (i & 63));
    g_scan_count--;
}

void ScanList(int most = 60) {
    int n = 0;
    for (uint32_t a = kScanLo; a < kScanHi && n < most; a += 4)
        if (Candidate(a)) {
            float v;
            std::memcpy(&v, kernel::g_base + a, 4);
            LOGR("scan: %08X = %g", a, v);
            n++;
        }
}

std::string Scan(const std::string& mode, std::istringstream& in) {
    if (!kernel::g_base) return "error: no guest memory";
    if (mode == "start") {
        g_scan_copy.assign(kScanHi, 0);
        g_scan_bits.assign(kScanHi / 4 / 64, 0);
        g_scan_count = 0;
        for (uint32_t page = kScanLo; page < kScanHi; page += 0x1000) {
            if (!Readable(page)) continue;
            std::memcpy(&g_scan_copy[page], kernel::g_base + page, 0x1000);
            for (uint32_t a = page; a < page + 0x1000; a += 4) {
                float v;
                std::memcpy(&v, &g_scan_copy[a], 4);
                if (!std::isfinite(v) || std::fabs(v) > 1e6f || (v != 0 && std::fabs(v) < 1e-6f)) continue;
                uint32_t i = a >> 2;
                g_scan_bits[i >> 6] |= uint64_t(1) << (i & 63);
                g_scan_count++;
            }
        }
        return "ok scan " + std::to_string(g_scan_count) + " candidates";
    }
    if (g_scan_bits.empty()) return "error: scan start first";
    if (mode == "list") {
        int most = 60;
        in >> most;
        ScanList(std::clamp(most, 1, 1000));
        return "ok scan list (in the log)";
    }
    if (mode == "mark") {
        g_scan_mark.assign(kScanHi, 0);
        for (uint32_t page = kScanLo; page < kScanHi; page += 0x1000)
            if (Readable(page)) std::memcpy(&g_scan_mark[page], kernel::g_base + page, 0x1000);
        return "ok scan mark";
    }
    float value = 0, tol = 0.001f;
    int kind = mode == "changed" ? 0 : mode == "same" ? 1 : mode == "inc" ? 2 : mode == "dec" ? 3 : mode == "eq" ? 4
             : mode == "back" ? 5 : -1;
    if (kind < 0) return "error: scan changed|same|inc|dec|eq|mark|back|list|start";
    if (kind == 4) in >> value >> tol;
    if (kind == 5) {
        if (g_scan_mark.empty()) return "error: scan mark first";
        in >> tol;
    }
    for (uint32_t page = kScanLo; page < kScanHi; page += 0x1000) {
        bool any = false;
        uint32_t first = page >> 2;
        for (uint32_t w = first >> 6; w < (first + 0x400) >> 6; w++) any |= g_scan_bits[w] != 0;
        if (!any) continue;
        bool readable = Readable(page);
        for (uint32_t a = page; a < page + 0x1000; a += 4) {
            if (!Candidate(a)) continue;
            if (!readable) {
                Drop(a);
                continue;
            }
            float was, now;
            std::memcpy(&was, &g_scan_copy[a], 4);
            std::memcpy(&now, kernel::g_base + a, 4);
            float marked = 0;
            if (kind == 5) std::memcpy(&marked, &g_scan_mark[a], 4);
            bool keep = std::isfinite(now) && (kind == 0   ? now != was
                                               : kind == 1 ? now == was
                                               : kind == 2 ? now > was
                                               : kind == 3 ? now < was
                                               : kind == 4 ? std::fabs(now - value) <= tol
                                                           : std::fabs(now - marked) <= tol);
            if (!keep) Drop(a);
            else std::memcpy(&g_scan_copy[a], &now, 4);
        }
    }
    if (g_scan_count <= 60) ScanList();
    return "ok scan " + std::to_string(g_scan_count) + " candidates";
}

// the watch: where the code that touched the address is (the instruction after it), counted
struct Hit {
    std::atomic<uint64_t> rip{0};
    std::atomic<uint32_t> count{0};
};
Hit g_hits[256];
PVOID g_watch_handler = nullptr;

LONG CALLBACK WatchHandler(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(e->ContextRecord->Dr6 & 1))
        return EXCEPTION_CONTINUE_SEARCH;
    uint64_t rip = e->ContextRecord->Rip;
    for (Hit& h : g_hits) {
        uint64_t cur = h.rip.load();
        if (cur == 0) {
            if (h.rip.compare_exchange_strong(cur, rip)) cur = rip;
        }
        if (cur == rip) {
            h.count++;
            break;
        }
    }
    e->ContextRecord->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

// the debug registers of every other thread of the process: dr0 = host, dr7 = its enable bits (0 removes it)
int SetWatch(uint64_t host, uint64_t dr7) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{sizeof(te)};
    DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != DWORD(-1)) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                c.Dr0 = host;
                c.Dr7 = (c.Dr7 & ~uint64_t(0xF0003)) | dr7;
                if (SetThreadContext(t, &c)) n++;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    return n;
}

// the export of a module nearest below an address: the recompiled game's functions are all exported by its code DLL,
// which has no PDB
bool NearestExport(uint64_t addr, std::string& name, uint64_t& off) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod))
        return false;
    auto* b = reinterpret_cast<const uint8_t*>(mod);
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress) return false;
    auto* ex = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(b + dir.VirtualAddress);
    auto* names = reinterpret_cast<const uint32_t*>(b + ex->AddressOfNames);
    auto* ords = reinterpret_cast<const uint16_t*>(b + ex->AddressOfNameOrdinals);
    auto* funcs = reinterpret_cast<const uint32_t*>(b + ex->AddressOfFunctions);
    uint64_t rva = addr - uint64_t(b), best = 0;
    const char* best_name = nullptr;
    for (uint32_t i = 0; i < ex->NumberOfNames; i++) {
        uint64_t f = funcs[ords[i]];
        if (f <= rva && f >= best) {
            best = f;
            best_name = reinterpret_cast<const char*>(b + names[i]);
        }
    }
    if (!best_name) return false;
    name = best_name;
    off = rva - best;
    return true;
}

std::string Watch(const std::string& arg, std::istringstream& in) {
    if (arg == "off") {
        int n = SetWatch(0, 0);
        return "ok watch off on " + std::to_string(n) + " threads";
    }
    if (arg == "list") {
        static bool sym = false;
        HANDLE proc = GetCurrentProcess();
        if (!sym) {
            SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            sym = SymInitialize(proc, nullptr, TRUE) != FALSE;
        }
        int n = 0;
        for (Hit& h : g_hits) {
            uint64_t rip = h.rip.load();
            if (!rip) break;
            alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256] = {};
            auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = 255;
            DWORD64 off = 0;
            std::string name;
            uint64_t eoff = 0;
            if (NearestExport(rip, name, eoff) && name.rfind("__imp__sub_", 0) == 0) off = eoff;
            else name = sym && SymFromAddr(proc, rip, &off, si) ? si->Name : "?";
            LOGR("watch: %u x %s+0x%llX", h.count.load(), name.c_str(), (unsigned long long)off);
            n++;
        }
        return "ok watch list " + std::to_string(n) + " places (in the log)";
    }
    uint32_t addr = uint32_t(std::strtoul(arg.c_str(), nullptr, 16));
    std::string mode = "w";
    in >> mode;
    if (!kernel::g_base || !Readable(addr) || (addr & 3)) return "error: not a readable aligned address";
    for (Hit& h : g_hits) {
        h.rip = 0;
        h.count = 0;
    }
    if (!g_watch_handler) g_watch_handler = AddVectoredExceptionHandler(1, WatchHandler);
    // L0, RW0 = 01 write or 11 read/write, LEN0 = 11 four bytes
    uint64_t dr7 = 1 | (uint64_t(mode == "rw" ? 3 : 1) << 16) | (uint64_t(3) << 18);
    int n = SetWatch(uint64_t(kernel::g_base + addr), dr7);
    return "ok watch " + arg + " " + mode + " on " + std::to_string(n) + " threads";
}

std::string Run(const std::string& line) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    if (cmd.empty() || cmd[0] == '#') return "";
    Action a;
    if (cmd == "press" || cmd == "hold") {
        std::string b;
        float seconds = 0.15f;
        in >> b >> seconds;
        if (!Button(b, a)) return "error: unknown button " + b;
        a.until = Clock::now() + std::chrono::milliseconds(int(seconds * 1000));
    } else if (cmd == "stick") {
        std::string which;
        float seconds = 0;
        in >> which >> a.x >> a.y >> seconds;
        a.kind = 2;
        a.index = which == "r" ? 1 : 0;
        a.x = std::clamp(a.x, -1.0f, 1.0f);
        a.y = std::clamp(a.y, -1.0f, 1.0f);
        a.until = Clock::now() + std::chrono::milliseconds(int(seconds * 1000));
    } else if (cmd == "capture") {
        gpu::g_capture = true;
        return "ok capture";
    } else if (cmd == "find") {
        std::string text;
        std::getline(in >> std::ws, text);
        return "ok find " + std::to_string(FindText(text)) + " places (in the log)";
    } else if (cmd == "peek") {
        uint32_t addr = 0, n = 64;
        in >> std::hex >> addr >> std::dec >> n;
        Peek(addr, std::min(n, 512u));
        return "ok peek (in the log)";
    } else if (cmd == "poke") {
        uint32_t addr = 0;
        float v = 0;
        in >> std::hex >> addr >> std::dec >> v;
        if (!kernel::g_base || !Readable(addr)) return "error: not readable";
        std::memcpy(kernel::g_base + addr, &v, 4);
        return "ok " + line;
    } else if (cmd == "watch") {
        std::string arg;
        in >> arg;
        return Watch(arg, in);
    } else if (cmd == "scan") {
        std::string mode;
        in >> mode;
        return Scan(mode, in);
    } else {
        return "error: unknown command " + cmd;
    }
    std::lock_guard lock(g_lock);
    g_actions.push_back(a);
    return "ok " + line;
}

void Loop() {
    std::string cmd_path = g_dir + "\\cmd.txt", ack_path = g_dir + "\\ack.txt";
    std::ofstream(ack_path, std::ios::trunc) << "autopilot ready\n";
    std::streamoff done = 0;
    {
        // commands already in the file before the game started are skipped
        std::ifstream f(cmd_path, std::ios::ate);
        if (f) done = f.tellg();
    }
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ifstream f(cmd_path);
        if (!f) continue;
        f.seekg(0, std::ios::end);
        std::streamoff size = f.tellg();
        if (size < done) done = 0;  // the file was replaced
        if (size == done) continue;
        f.seekg(done);
        std::string line, last;
        std::ofstream ack(ack_path, std::ios::app);
        while (std::getline(f, line)) {
            if (f.eof()) break;  // a line still being written
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string r = Run(line);
            if (!r.empty()) ack << r << "\n";
            done = f.tellg();
        }
    }
}

}  // namespace

void StartAutopilot() {
    const char* dir = std::getenv("INDY_AUTOPILOT");
    if (!dir || !*dir) return;
    g_dir = dir;
    std::thread(Loop).detach();
    LOGR("autopilot: reading %s\\cmd.txt", dir);
}

void AutopilotPad(int port, input::Pad& pad) {
    if (port != 0 || g_dir.empty()) return;
    pad.connected = true;
    auto now = Clock::now();
    std::lock_guard lock(g_lock);
    g_actions.erase(std::remove_if(g_actions.begin(), g_actions.end(), [&](const Action& a) { return a.until <= now; }),
                    g_actions.end());
    for (const Action& a : g_actions) {
        if (a.kind == 0) pad.buttons |= a.mask;
        if (a.kind == 1) pad.analog[a.index] = 0xFF;
        if (a.kind == 2) {
            int16_t x = int16_t(a.x * 32767), y = int16_t(a.y * 32767);
            if (a.index == 0) pad.lx = x, pad.ly = y;
            else pad.rx = x, pad.ry = y;
        }
    }
}

}  // namespace game
