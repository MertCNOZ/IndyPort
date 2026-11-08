// Autopilot: drives port 0's controller from a command file, so the game can be tested without its window in front
// or anyone at the keyboard. Off unless INDY_AUTOPILOT names a folder. A thread reads new lines appended to
// <folder>\cmd.txt every 50 ms and acknowledges each one in <folder>\ack.txt.
//
//   press <button> [seconds]           tap a button, 0.15 s by default
//   hold <button> <seconds>            hold it
//   stick <l|r> <x> <y> <seconds>      tilt a stick, x and y from -1 to 1, y up
//   capture                            save the next frame under XBE_CAPTURE_DIR (as F3 does)
//   # ...                              comment
//
// buttons: a b x y black white lt rt start back up down left right lthumb rthumb
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"

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
