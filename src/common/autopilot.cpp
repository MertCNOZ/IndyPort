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
//   # ...                              comment
//
// buttons: a b x y black white lt rt start back up down left right lthumb rthumb
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
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
