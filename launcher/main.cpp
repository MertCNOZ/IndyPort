// IndyPort, the launcher of the PC port of Indiana Jones and the Emperor's Tomb: points the port at the player's own
// copy of the game (a disc image or the extracted files), edits the settings the port reads and starts it.
// Dear ImGui on Win32 and Direct3D 11 (SDL3 only for controller names and display modes), mouse, keyboard or
// controller. No game files are part of it.
//
// A sidebar with the pages (Home, Graphics, Game, Controls, Saves, About) and a bar along the bottom with Play.
// It lives beside IndyET.exe; settings.toml (with game_path, the copy chosen here) and the hdd folder (saves) are
// beside the exe. Without a chosen copy the port looks in an "assets" folder beside it or in any folder above.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <wincodec.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "0.3.0";
constexpr const wchar_t* kGameExe = L"IndyET.exe";
constexpr const char* kTitleId = "4c410007";

// colours: dark leather and the gold of the title
constexpr ImU32 kBg = IM_COL32(20, 17, 15, 255);
constexpr ImU32 kSidebar = IM_COL32(13, 11, 10, 255);
constexpr ImU32 kSelRow = IM_COL32(32, 26, 22, 255);
constexpr ImU32 kBar = IM_COL32(15, 13, 11, 255);
constexpr ImU32 kLine = IM_COL32(52, 44, 38, 255);
constexpr ImU32 kText = IM_COL32(236, 230, 222, 255);
constexpr ImU32 kDim = IM_COL32(150, 140, 128, 255);
constexpr ImU32 kAccent = IM_COL32(236, 160, 44, 255);
constexpr ImU32 kPlay = IM_COL32(176, 88, 20, 255);
constexpr ImU32 kGood = IM_COL32(130, 200, 110, 255);
constexpr ImU32 kBad = IM_COL32(236, 110, 90, 255);

std::atomic<int> g_wheel_seen{0};  // a wheel turn while a key binding waits (WndProc)
ImFont* g_font = nullptr;        // text
ImFont* g_font_head = nullptr;   // section headings
ImFont* g_font_nav = nullptr;    // sidebar pages
ImFont* g_font_title = nullptr;  // page titles
ImFont* g_font_logo = nullptr;
ImFont* g_font_play = nullptr;

// ---- paths

fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

// the port's own rule when no copy is chosen: an "assets" folder beside the exe or in any folder above it
fs::path FindAssets() {
    std::error_code ec;
    fs::path dir = ExeDir();
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), ' ');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L' ');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void OpenFolder(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// launcher.log beside the exe: what the launcher did, for bug reports
void Log(const char* fmt, ...) {
    static std::mutex m;
    std::lock_guard l(m);
    static FILE* f = _wfopen((ExeDir() / "launcher.log").c_str(), L"w");
    if (!f) return;
    va_list a;
    va_start(a, fmt);
    std::vfprintf(f, fmt, a);
    va_end(a);
    std::fputc('\n', f);
    std::fflush(f);
}

// ---- the game's files

bool IsIso(const fs::path& p) {
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".iso";
}

// an Xbox disc image: the XDVDFS volume descriptor ("MICROSOFT*XBOX*MEDIA" in sector 32) at the start of a game
// partition (xiso), or behind the video partition of a whole disc dump (XGD1, XGD2, XGD3)
bool IsXboxImage(const fs::path& p) {
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    const int64_t partitions[] = {0, 0x18300000, 0xFD90000, 0x2080000};
    bool found = false;
    for (int64_t at : partitions) {
        char magic[20] = {};
        if (_fseeki64(f, at + 0x10000, SEEK_SET) == 0 && std::fread(magic, 1, 20, f) == 20 &&
            std::memcmp(magic, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
            found = true;
            break;
        }
    }
    std::fclose(f);
    return found;
}

struct GameFiles {
    enum State { None, Ready, Problem } state = None;
    std::string what;    // "Disc image: name.iso", "Extracted files", or what is wrong
    std::string where;   // the folder or file, for the page
    bool chosen = false; // from game_path, not the assets folder
};

GameFiles CheckFolder(const fs::path& dir) {
    GameFiles g;
    std::error_code ec;
    g.where = Utf8(dir.wstring());
    if (fs::exists(dir / "default.xbe", ec)) {
        g.state = GameFiles::Ready;
        g.what = "Extracted game files";
        return g;
    }
    std::vector<fs::path> isos;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (IsIso(e.path()) && e.is_regular_file(ec)) isos.push_back(e.path());
    std::sort(isos.begin(), isos.end());
    for (auto& iso : isos)
        if (IsXboxImage(iso)) {
            g.state = GameFiles::Ready;
            g.what = "Disc image: " + Utf8(iso.filename().wstring());
            return g;
        }
    g.state = GameFiles::Problem;
    g.what = isos.empty() ? "This folder has no default.xbe and no disc image (.iso)."
                          : "The disc images in this folder are not Xbox discs.";
    return g;
}

GameFiles CheckGame(const std::string& game_path) {
    GameFiles g;
    std::error_code ec;
    if (!game_path.empty()) {
        fs::path p(Wide(game_path));
        if (fs::is_directory(p, ec)) {
            g = CheckFolder(p);
        } else if (fs::is_regular_file(p, ec)) {
            g.where = game_path;
            bool ok = IsXboxImage(p);
            g.state = ok ? GameFiles::Ready : GameFiles::Problem;
            g.what = ok ? "Disc image: " + Utf8(p.filename().wstring()) : "This file is not an Xbox disc image.";
        } else {
            g.where = game_path;
            g.state = GameFiles::Problem;
            g.what = "Not found. Was it moved, or is the drive unplugged?";
        }
        g.chosen = true;
        return g;
    }
    if (fs::path a = FindAssets(); !a.empty()) {
        GameFiles in = CheckFolder(a);
        if (in.state == GameFiles::Ready) return in;
    }
    g.what = "No game chosen yet.";
    return g;
}

// the Windows dialogs that choose the game
std::wstring PickIso(HWND owner) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Xbox disc image (*.iso)\0*.iso\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose the disc image of your copy of the game";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

std::wstring PickFolder(HWND owner) {
    std::wstring out;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return out;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dlg->SetTitle(L"Choose the folder of the extracted game (the one with default.xbe)");
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                out = path;
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dlg->Release();
    return out;
}

// ---- settings.toml beside the game

// the gamepad mapping: for each button of the Xbox controller the game reads, the button of a modern controller
// that presses it (the order of src/common/pad_map.cpp)
constexpr std::array<int, 16> kDefaultPadMap = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
// the game's buttons with what they do in its first controller layout (Options, Controls, Configuration 1)
const char* kGameButtons[16] = {"A: Attack 2",          "B: Jump / swim",        "X: Attack 1",
                                "Y: Action",            "Black: Draw / pack",    "White: Reload",
                                "LT: First person",     "RT: Guard mode",        "Up: Open / draw item",
                                "Down: Close / pack item", "Left: Previous item", "Right: Next item",
                                "Start: Pause",         "Back: Back",            "Left stick click",
                                "Right stick click"};
// keyboard and mouse bindings (src/common/keyboard.cpp): the 16 buttons, then the left and right stick directions;
// a Windows virtual key, 0x100 / 0x101 the wheel, 0 none
constexpr int kKeyActions = 24;
constexpr int kWheelUp = 0x100, kWheelDown = 0x101;
using KeyBindings = std::array<std::array<int, 2>, kKeyActions>;
constexpr KeyBindings kDefaultKeys = {{{'Q', VK_RETURN}, {VK_SPACE, VK_BACK}, {VK_LBUTTON, 0}, {'E', 0},
                                      {'G', 0}, {'R', 0}, {'F', 0}, {VK_RBUTTON, 0},
                                      {VK_UP, '1'}, {VK_DOWN, '2'}, {VK_LEFT, kWheelUp}, {VK_RIGHT, kWheelDown},
                                      {VK_ESCAPE, 0}, {VK_TAB, 0}, {'V', 0}, {VK_MBUTTON, 0},
                                      {'W', 0}, {'S', 0}, {'A', 0}, {'D', 0},
                                      {'I', 0}, {'K', 0}, {'J', 0}, {'L', 0}}};
const char* kStickActions[8] = {"Move forward", "Move back", "Move left", "Move right",
                                "Camera up",    "Camera down", "Camera left", "Camera right"};
const char* kControllerButtons[16] = {"A",       "B",          "X",          "Y",
                                      "LB",      "RB",         "LT",         "RT",
                                      "D-pad up", "D-pad down", "D-pad left", "D-pad right",
                                      "Menu",    "View",       "LS (click)", "RS (click)"};

struct Settings {
    int display_mode = 1;  // fullscreen, borderless, windowed
    int width = 0, height = 0;
    int render_percent = 100;
    int fps_cap = 60;
    bool vsync = true;
    int aniso = 16;
    int sharpen = 40;
    int draw_distance = 100;
    int aspect = 0;  // 0 widescreen (wider view), 1 4:3, 2 stretched
    bool full_width = true;
    int fov = 100;
    bool fxaa = false;
    int rumble = 100;
    bool custom_textures = true;
    bool dump_textures = false;
    int dump_format = 0;
    bool show_fps = false;
    bool ui_upscale = true;
    bool video_filter = true;
    std::string game_path;
    std::string texture_packs = "modern_buttons";  // built-in packs; modern_buttons: the modern Xbox icons
    std::array<int, 16> pad_map = kDefaultPadMap;
    KeyBindings keys = kDefaultKeys;
    bool mouse_look = true;
    int mouse_sensitivity = 50;
    bool mouse_invert_y = false;  // game button -> controller button, -1 none (pad_map.cpp)
    bool operator==(const Settings&) const = default;
};

const char* kModes[] = {"fullscreen", "borderless", "windowed"};

fs::path SettingsFile() { return ExeDir() / "settings.toml"; }

Settings LoadSettings() {
    Settings s;
    try {
        toml::table t = toml::parse_file(SettingsFile().string());
        std::string mode = t["display_mode"].value_or(std::string("borderless"));
        for (int i = 0; i < 3; i++)
            if (mode == kModes[i]) s.display_mode = i;
        s.width = int(t["width"].value_or(int64_t(0)));
        s.height = int(t["height"].value_or(int64_t(0)));
        s.render_percent = int(t["render_percent"].value_or(int64_t(100)));
        s.fps_cap = int(t["fps_cap"].value_or(int64_t(60)));
        s.vsync = t["vsync"].value_or(true);
        s.aniso = int(t["anisotropic_filtering"].value_or(int64_t(16)));
        s.sharpen = std::clamp(int(t["sharpen"].value_or(int64_t(40))), 0, 100);
        s.draw_distance = std::clamp(int(t["draw_distance"].value_or(int64_t(100))), 25, 1000);
        s.aspect = std::clamp(int(t["aspect"].value_or(int64_t(0))), 0, 2);
        s.full_width = t["full_width"].value_or(true);
        s.fov = std::clamp(int(t["fov"].value_or(int64_t(100))), 70, 140);
        s.fxaa = t["fxaa"].value_or(false);
        s.rumble = std::clamp(int(t["rumble"].value_or(int64_t(100))), 0, 100);
        s.custom_textures = t["custom_textures"].value_or(true);
        s.dump_textures = t["dump_textures"].value_or(false);
        s.dump_format = int(t["texture_dump_format"].value_or(int64_t(0)));
        s.show_fps = t["show_fps"].value_or(false);
        s.ui_upscale = t["ui_upscale"].value_or(true);
        s.video_filter = t["video_filter"].value_or(true);
        s.game_path = t["game_path"].value_or(std::string());
        s.texture_packs = t["texture_packs"].value_or(s.texture_packs);
        if (const toml::array* a = t["keys"].as_array(); a && a->size() == kKeyActions) {
            KeyBindings k{};
            bool ok = true;
            for (size_t i = 0; i < kKeyActions && ok; i++) {
                const toml::array* pair = (*a)[i].as_array();
                ok = pair && pair->size() == 2;
                for (size_t j = 0; ok && j < 2; j++) {
                    k[i][j] = int((*pair)[j].value_or(int64_t(-1)));
                    ok = k[i][j] >= 0 && k[i][j] <= kWheelDown;
                }
            }
            if (ok) s.keys = k;
        }
        s.mouse_look = t["mouse_look"].value_or(true);
        s.mouse_sensitivity = std::clamp(int(t["mouse_sensitivity"].value_or(int64_t(50))), 1, 100);
        s.mouse_invert_y = t["mouse_invert_y"].value_or(false);
        if (const toml::array* a = t["pad_map"].as_array(); a && a->size() == 16) {
            std::array<int, 16> m{};
            bool ok = true;
            for (size_t i = 0; i < 16; i++) {
                m[i] = int((*a)[i].value_or(int64_t(-2)));
                ok &= m[i] >= -1 && m[i] <= 15;
            }
            if (ok) s.pad_map = m;
        }
    } catch (...) {
    }
    return s;
}

// the keys the launcher owns are replaced, any other key in the file is kept
void SaveSettings(const Settings& s) {
    toml::table t;
    try {
        t = toml::parse_file(SettingsFile().string());
    } catch (...) {
    }
    t.insert_or_assign("display_mode", kModes[s.display_mode]);
    t.insert_or_assign("width", int64_t(s.width));
    t.insert_or_assign("height", int64_t(s.height));
    t.insert_or_assign("render_percent", int64_t(s.render_percent));
    t.insert_or_assign("fps_cap", int64_t(s.fps_cap));
    t.insert_or_assign("vsync", s.vsync);
    t.insert_or_assign("anisotropic_filtering", int64_t(s.aniso));
    t.insert_or_assign("sharpen", int64_t(s.sharpen));
    t.insert_or_assign("draw_distance", int64_t(s.draw_distance));
    t.insert_or_assign("aspect", int64_t(s.aspect));
    t.insert_or_assign("full_width", s.full_width);
    t.insert_or_assign("fov", int64_t(s.fov));
    t.insert_or_assign("fxaa", s.fxaa);
    t.insert_or_assign("rumble", int64_t(s.rumble));
    t.insert_or_assign("custom_textures", s.custom_textures);
    t.insert_or_assign("dump_textures", s.dump_textures);
    t.insert_or_assign("texture_dump_format", int64_t(s.dump_format));
    t.insert_or_assign("show_fps", s.show_fps);
    t.insert_or_assign("ui_upscale", s.ui_upscale);
    t.insert_or_assign("video_filter", s.video_filter);
    t.insert_or_assign("game_path", s.game_path);
    t.insert_or_assign("texture_packs", s.texture_packs);
    toml::array map;
    for (int v : s.pad_map) map.push_back(int64_t(v));
    t.insert_or_assign("pad_map", std::move(map));
    toml::array keys;
    for (const auto& pair : s.keys) keys.push_back(toml::array{int64_t(pair[0]), int64_t(pair[1])});
    t.insert_or_assign("keys", std::move(keys));
    t.insert_or_assign("mouse_look", s.mouse_look);
    t.insert_or_assign("mouse_sensitivity", int64_t(s.mouse_sensitivity));
    t.insert_or_assign("mouse_invert_y", s.mouse_invert_y);
    std::ofstream out(SettingsFile());
    out << t << "\n";
}

// ---- saves

struct Save {
    std::string name, folder, when;
};

fs::path SavesDir() { return ExeDir() / "hdd" / "E" / "UDATA" / kTitleId; }

std::vector<Save> ListSaves() {
    std::vector<Save> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(SavesDir(), ec)) {
        if (!e.is_directory()) continue;
        Save s;
        s.folder = Utf8(e.path().filename().wstring());
        s.name = s.folder;
        // SaveMeta.xbx: UTF-16 "Name=..."
        std::ifstream f(e.path() / "SaveMeta.xbx", std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(f)), {});
        std::wstring w;
        for (size_t i = 0; i + 1 < raw.size(); i += 2) w += wchar_t(uint8_t(raw[i]) | uint8_t(raw[i + 1]) << 8);
        if (size_t at = w.find(L"Name="); at != std::wstring::npos) {
            std::wstring n = w.substr(at + 5);
            n = n.substr(0, n.find_first_of(L"\r\n"));
            if (!n.empty()) s.name = Utf8(n);
        }
        auto t = fs::last_write_time(e.path(), ec);
        auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
        std::time_t tt = std::chrono::system_clock::to_time_t(sys);
        char buf[32];
        std::tm tm{};
        localtime_s(&tm, &tt);
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
        s.when = buf;
        out.push_back(s);
    }
    std::sort(out.begin(), out.end(), [](const Save& a, const Save& b) { return a.when > b.when; });
    return out;
}

std::string BackUpSaves() {
    std::error_code ec;
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
    fs::path dst = ExeDir() / "save backups" / stamp;
    fs::create_directories(dst, ec);
    fs::copy(SavesDir(), dst, fs::copy_options::recursive, ec);
    return ec ? "Backup failed: " + ec.message() : "Saved to save backups\\" + std::string(stamp);
}

// ---- running the game

std::atomic<bool> g_game_running{false};

// an IndyET.exe already running (started by hand, or a relaunch the launcher did not see)
bool GameAlreadyRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{sizeof(e)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
        found = _wcsicmp(e.szExeFile, kGameExe) == 0;
    CloseHandle(snap);
    return found;
}

// The game runs in a job: it restarts itself as a new process when it launches an Xbox image (the old one exits once
// the new one shows its first frames), and the game code compiler runs as child processes. The game has ended only
// when the job has no process left.
void Play(HWND window) {
    if (g_game_running || GameAlreadyRunning()) {
        Log("play: the game is already running");
        return;
    }
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    HANDLE port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT assoc{job, port};
    SetInformationJobObject(job, JobObjectAssociateCompletionPortInformation, &assoc, sizeof(assoc));

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    fs::path exe = ExeDir() / kGameExe;
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, ExeDir().c_str(),
                        &si, &pi)) {
        Log("play: cannot start %s (error %lu)", Utf8(exe.wstring()).c_str(), GetLastError());
        CloseHandle(job);
        CloseHandle(port);
        return;
    }
    if (!AssignProcessToJobObject(job, pi.hProcess)) Log("play: no job for the game (error %lu)", GetLastError());
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    Log("play: started the game, process %lu", pi.dwProcessId);
    g_game_running = true;
    ShowWindow(window, SW_MINIMIZE);
    std::thread([job, port, window] {
        DWORD msg;
        ULONG_PTR key;
        LPOVERLAPPED data;
        while (GetQueuedCompletionStatus(port, &msg, &key, &data, INFINITE)) {
            if (msg == JOB_OBJECT_MSG_NEW_PROCESS) Log("play: process %lu started", DWORD(uintptr_t(data)));
            if (msg == JOB_OBJECT_MSG_EXIT_PROCESS || msg == JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS)
                Log("play: process %lu exited", DWORD(uintptr_t(data)));
            if (msg == JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO) break;
        }
        Log("play: the game has ended");
        CloseHandle(job);
        CloseHandle(port);
        g_game_running = false;
        ShowWindow(window, SW_RESTORE);
        SetForegroundWindow(window);
    }).detach();
}

// ---- the banner: the jpg in the resources, decoded with WIC into a Direct3D 11 texture

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;

struct Image {
    ID3D11ShaderResourceView* srv = nullptr;
    int w = 0, h = 0;
};
Image g_banner;

Image LoadResourceImage(int id) {
    Image img;
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!res) return img;
    HGLOBAL mem = LoadResource(nullptr, res);
    auto* bytes = static_cast<const BYTE*>(LockResource(mem));
    IStream* stream = SHCreateMemStream(bytes, SizeofResource(nullptr, res));
    IWICImagingFactory* wic = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    if (stream && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom))) {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        std::vector<uint8_t> px(size_t(w) * h * 4);
        if (SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(px.size()), px.data()))) {
            D3D11_TEXTURE2D_DESC td{w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_IMMUTABLE,
                                    D3D11_BIND_SHADER_RESOURCE};
            D3D11_SUBRESOURCE_DATA init{px.data(), w * 4};
            ID3D11Texture2D* tex = nullptr;
            if (SUCCEEDED(g_dev->CreateTexture2D(&td, &init, &tex))) {
                g_dev->CreateShaderResourceView(tex, nullptr, &img.srv);
                tex->Release();
                img.w = int(w);
                img.h = int(h);
            }
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (wic) wic->Release();
    if (stream) stream->Release();
    return img;
}

// ---- widgets

// a settings row: the label on the left, the control in a column at the same place on each row, the description
// in dim text below
float g_col = 0;  // the controls' column, from the page's left

void Row(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(g_col);
}

void Desc(const char* text) {
    if (!text) return;
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - ImGui::GetFontSize());
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.35f));
}

bool Check(const char* id, bool* v) {
    float s = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(s, s));
    if (pressed) *v = !*v;
    ImDrawList* d = ImGui::GetWindowDrawList();
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    float in = s * 0.12f;
    d->AddRect(ImVec2(p.x + in, p.y + in), ImVec2(p.x + s - in, p.y + s - in), hot ? kAccent : kDim, 3, 0, 1.5f);
    if (*v) {
        d->AddRectFilled(ImVec2(p.x + in, p.y + in), ImVec2(p.x + s - in, p.y + s - in), kPlay, 3);
        ImVec2 a(p.x + s * 0.28f, p.y + s * 0.52f), b(p.x + s * 0.44f, p.y + s * 0.68f), c(p.x + s * 0.73f, p.y + s * 0.32f);
        d->AddLine(a, b, kText, 2.2f);
        d->AddLine(b, c, kText, 2.2f);
    }
    return pressed;
}

float ControlWidth() { return ImGui::GetFontSize() * 14; }

bool Combo(const char* id, int* current, const std::vector<std::string>& items, float width = 0) {
    ImGui::SetNextItemWidth(width > 0 ? width : ControlWidth());
    bool changed = false;
    const char* preview = *current >= 0 && *current < int(items.size()) ? items[size_t(*current)].c_str() : "";
    if (ImGui::BeginCombo(id, preview)) {
        for (int i = 0; i < int(items.size()); i++) {
            bool sel = i == *current;
            if (ImGui::Selectable(items[size_t(i)].c_str(), sel)) {
                *current = i;
                changed = true;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool Slider(const char* id, int* v, int lo, int hi, int step, const char* fmt, float width = 0) {
    ImGui::SetNextItemWidth(width > 0 ? width : ControlWidth());
    int s = *v / step;
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    std::string shown;  // the slider's format: its % doubled
    for (const char* c = buf; *c; c++) shown += *c == '%' ? "%%" : std::string(1, *c);
    std::snprintf(buf, sizeof(buf), "%s", shown.c_str());
    bool changed = ImGui::SliderInt(id, &s, lo / step, hi / step, buf);
    if (changed) *v = s * step;
    return changed;
}

bool Button(const char* label, float width = 0) {
    return ImGui::Button(label, ImVec2(width, ImGui::GetFrameHeight()));
}

void Heading(const char* text) {
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.5f));
    ImGui::PushFont(g_font_head);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y), kLine, 1.5f);
    ImGui::Dummy(ImVec2(w, ImGui::GetFontSize() * 0.4f));
}

void PageTitle(const char* title, const char* sub) {
    ImGui::PushFont(g_font_title);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();
}

// ---- pages

enum class Page { Home, Graphics, Game, Controls, Saves, About, Count };
const char* kPageNames[] = {"HOME", "GRAPHICS", "GAME", "CONTROLS", "SAVES", "ABOUT"};

struct Ui {
    Page page = Page::Home;
    int shown = -1;  // the page drawn last frame: a new one puts the controller's focus on its first control
    Settings s = LoadSettings();
    Settings saved = s;
    GameFiles game = CheckGame(s.game_path);
    std::vector<std::pair<int, int>> modes;  // display modes, desktop first as 0x0
    std::vector<Save> saves = ListSaves();
    std::string message;
    double message_until = 0;
    HWND window = nullptr;
    int capture = -1;  // the key binding slot waiting for a key (action * 2 + slot)
    std::array<bool, 256> capture_held{};
};

void Note(Ui& ui, std::string m) {
    ui.message = std::move(m);
    ui.message_until = ImGui::GetTime() + 4;
}

void LoadModes(Ui& ui) {
    ui.modes.clear();
    ui.modes.push_back({0, 0});
    int n = 0;
    SDL_DisplayMode** list = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &n);
    for (int i = 0; list && i < n; i++) {
        std::pair<int, int> m{list[i]->w, list[i]->h};
        if (m.first >= 640 && std::find(ui.modes.begin(), ui.modes.end(), m) == ui.modes.end()) ui.modes.push_back(m);
    }
    SDL_free(list);
}

void ChooseGame(Ui& ui, const std::wstring& path) {
    if (path.empty()) return;
    ui.s.game_path = Utf8(path);
    ui.game = CheckGame(ui.s.game_path);
    Log("game: chose %s: %s", ui.s.game_path.c_str(), ui.game.what.c_str());
    Note(ui, ui.game.state == GameFiles::Ready ? "The game is ready." : ui.game.what);
}

// the shared controls, on Home and on their pages
void DisplayModeControl(Ui& ui, float w = 0) { Combo("##mode", &ui.s.display_mode, {"Fullscreen", "Borderless", "Windowed"}, w); }

void ResolutionControl(Ui& ui, float w = 0) {
    std::vector<std::string> names;
    int cur = 0;
    for (size_t i = 0; i < ui.modes.size(); i++) {
        auto [mw, mh] = ui.modes[i];
        names.push_back(mw ? std::to_string(mw) + " x " + std::to_string(mh) : "Desktop");
        if (mw == ui.s.width && mh == ui.s.height) cur = int(i);
    }
    if (Combo("##res", &cur, names, w)) {
        ui.s.width = ui.modes[size_t(cur)].first;
        ui.s.height = ui.modes[size_t(cur)].second;
    }
}

void FpsControl(Ui& ui, float w = 0) {
    const int caps[] = {30, 60, 1, 120, 144, 165, 240, 0};
    std::vector<std::string> names = {"30", "60", "Monitor refresh rate", "120", "144", "165", "240", "Unlimited"};
    int cur = 1;
    for (int i = 0; i < 8; i++)
        if (caps[i] == ui.s.fps_cap) cur = i;
    if (Combo("##fps", &cur, names, w)) ui.s.fps_cap = caps[cur];
}

void AspectControl(Ui& ui, float w = 0) {
    Combo("##aspect", &ui.s.aspect, {"Widescreen (fill the screen)", "4:3 (original)", "Stretched"}, w);
}

void ScaleControl(Ui& ui, float w = 0) { Slider("##scale", &ui.s.render_percent, 50, 200, 10, "%d%%", w); }

void PageGraphics(Ui& ui) {
    Settings& s = ui.s;
    PageTitle("GRAPHICS", "How the game appears on your screen.");
    Heading("WINDOW");
    Row("Display mode");
    DisplayModeControl(ui);
    Desc("Fullscreen changes the monitor's mode; Borderless fills the screen with a window; Windowed opens a window "
         "of the chosen resolution.");
    Row("Resolution");
    ResolutionControl(ui);
    Desc("The size of the game's picture. Desktop uses your monitor's current resolution.");
    Row("Aspect ratio");
    AspectControl(ui);
    Desc("Widescreen shows a wider view of the game at your screen's shape (16:9, 21:9...) with the menus and HUD "
         "kept in proportion. 4:3 is the Xbox picture with bars at the sides; Stretched pulls the 4:3 picture wide.");
    if (s.aspect == 0) {
        Row("Full width detail");
        Check("##fullw", &s.full_width);
        Desc("Widescreen draws the game wider than the Xbox did, so the picture keeps full detail across the screen. "
             "Off draws it 4:3 and widens it, a little softer and a little faster.");
    }
    Row("VSync");
    Check("##vsync", &s.vsync);
    Desc("Waits for the monitor's refresh: no tearing. Off gives the lowest latency.");

    Heading("RENDERING");
    Row("Render scale");
    ScaleControl(ui);
    Desc("Draws the game at this share of the resolution: above 100% is supersampling (sharper, slower), below is "
         "faster.");
    Row("Frame rate limit");
    FpsControl(ui);
    Desc("The Xbox ran the game at 30 frames a second. The game keeps its speed at any frame rate.");
    Row("Anisotropic filtering");
    {
        const int levels[] = {0, 2, 4, 8, 16};
        std::vector<std::string> names = {"Game default", "2x", "4x", "8x", "16x"};
        int cur = 0;
        for (int i = 0; i < 5; i++)
            if (levels[i] == s.aniso) cur = i;
        if (Combo("##aniso", &cur, names)) s.aniso = levels[cur];
    }
    Desc("Sharper floors and walls seen at an angle, at little cost on most graphics cards. 16x is best.");
    Row("Draw distance");
    {
        const int values[] = {100, 150, 200, 300, 400};
        std::vector<std::string> names = {"Original (100%)", "150%", "200%", "300%", "400%"};
        int cur = 0;
        for (int i = 0; i < 5; i++)
            if (values[i] == s.draw_distance) cur = i;
        if (Combo("##dd", &cur, names)) s.draw_distance = values[cur];
    }
    Desc("How far each level draws. The game cuts the view off at each level's own distance (Ceylon: 5555 units); "
         "longer distances show more of large outdoor areas at a small cost. Applied when a level loads.");
    Row("Field of view");
    Slider("##fov", &s.fov, 70, 140, 5, "%d%%");
    Desc("How wide the camera sees, as a share of the game's own view (100%). Wider shows more around Indy.");
    Row("Anti-aliasing (FXAA)");
    Check("##fxaa", &s.fxaa);
    Desc("Smooths jagged edges at almost no cost. It softens textures a little; sharpening makes up for it.");
    Row("Sharpening");
    Slider("##sharpen", &s.sharpen, 0, 100, 5, "%d%%");
    Desc("Contrast adaptive sharpening of the final picture: the game's low resolution textures look crisper, "
         "without halos on edges. 0% turns it off.");

    Heading("TEXT AND HUD");
    Row("Sharp text and HUD");
    Check("##uiup", &s.ui_upscale);
    Desc("The game's fonts, menus, HUD and icons are made at up to four times their size when the game starts using "
         "them, with smooth outlines for text, so they stay sharp at high resolutions. Made on your PC from your copy "
         "of the game.");

    Row("Sharper movies");
    Check("##vidf", &s.video_filter);
    Desc("The game's videos are 640 x 480. This scales them up with a sharper filter than the usual blur.");

    Heading("OVERLAY");
    Row("Show FPS counter");
    Check("##showfps", &s.show_fps);
    Desc("The game's frames per second and frame time in the top left corner. F9 in the game turns it on and off.");

    Heading("TEXTURES");
    Row("Load custom textures");
    Check("##custom", &s.custom_textures);
    Desc("Images in the textures\\replacement folder replace the game's textures.");
    Row("Texture packs");
    if (Button("Open the textures folder")) OpenFolder(ExeDir() / "textures" / "replacement");
    Desc(nullptr);
    Row("Dump textures");
    Check("##dump", &s.dump_textures);
    Desc("Saves every texture the game uses into textures\\dumps, for making texture packs.");
    Row("Dump format");
    Combo("##dumpfmt", &s.dump_format, {"PNG", "DDS"});
    Desc("PNG to edit; DDS keeps the original compression and mipmaps.");
}

void PageGame(Ui& ui) {
    PageTitle("GAME", "Your copy of the game and where the port keeps its files.");
    Heading("GAME FILES");
    Row("Your game");
    {
        std::string shown = ui.game.where.empty() ? std::string("Nothing chosen") : ui.game.where;
        float w = ImGui::GetContentRegionAvail().x - ImGui::GetFontSize();
        ImGui::SetNextItemWidth(w);
        ImGui::BeginDisabled();
        ImGui::InputText("##where", shown.data(), shown.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    }
    ImGui::SetCursorPosX(g_col);
    ImU32 colour = ui.game.state == GameFiles::Ready ? kGood : ui.game.state == GameFiles::Problem ? kBad : kDim;
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::TextUnformatted(ui.game.what.c_str());
    ImGui::PopStyleColor();
    ImGui::SetCursorPosX(g_col);
    float bw = ImGui::GetFontSize() * 10;
    if (Button("Disc image (.iso)...", bw)) ChooseGame(ui, PickIso(ui.window));
    ImGui::SameLine();
    if (Button("Extracted folder...", bw)) ChooseGame(ui, PickFolder(ui.window));
    ImGui::SameLine();
    ImGui::BeginDisabled(ui.game.where.empty());
    if (Button("Open", bw * 0.6f)) {
        fs::path p(Wide(ui.game.where));
        std::error_code ec;
        OpenFolder(fs::is_directory(p, ec) ? p : p.parent_path());
    }
    ImGui::EndDisabled();
    Desc(nullptr);
    Desc("Indiana Jones and the Emperor's Tomb for the original Xbox (USA), from your own disc: a disc image (.iso; a "
         "whole disc dump or an xiso), or the extracted files (the folder with default.xbe). The port reads it where "
         "it is; nothing is copied and nothing is changed.");
    if (ui.game.chosen) {
        ImGui::SetCursorPosX(g_col);
        if (Button("Forget this choice", bw)) {
            ui.s.game_path.clear();
            ui.game = CheckGame(ui.s.game_path);
        }
        Desc("Without a choice the port looks for the game in an \"assets\" folder beside it.");
    }

    Heading("FOLDERS");
    Row("Port folder");
    if (Button("Open##port", bw)) OpenFolder(ExeDir());
    Desc("The port, settings.toml and the logs folder.");
    Row("Saves");
    if (Button("Open##saves", bw)) OpenFolder(SavesDir());
    Desc("The Xbox hard disk of the port: hdd\\E\\UDATA holds the saved games.");
}

// a modern controller's button as a small badge: A B X Y as coloured letters in a dark circle, the others as text in
// a rounded box
void ButtonGlyph(int button) {
    float s = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(s * 1.6f, s));
    if (button < 0) return;
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + s * 0.8f, p.y + s * 0.5f);
    const char* text = kControllerButtons[button];
    const char* shortname[16] = {"A",  "B",    "X",    "Y",     "LB",   "RB",   "LT", "RT",
                                 "UP", "DOWN", "LEFT", "RIGHT", "MENU", "VIEW", "LS", "RS"};
    if (button < 4) {
        const ImU32 colours[4] = {IM_COL32(110, 200, 70, 255), IM_COL32(230, 70, 60, 255), IM_COL32(70, 140, 235, 255),
                                  IM_COL32(240, 200, 50, 255)};
        d->AddCircleFilled(c, s * 0.42f, IM_COL32(36, 36, 38, 255), 24);
        d->AddCircle(c, s * 0.42f, IM_COL32(80, 80, 84, 255), 24, 1.2f);
        ImVec2 ts = ImGui::CalcTextSize(text);
        d->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), colours[button], text);
        return;
    }
    const char* t = shortname[button];
    ImVec2 ts = ImGui::CalcTextSize(t);
    float w = std::max(ts.x + s * 0.4f, s * 0.84f), h = s * 0.74f;
    d->AddRectFilled(ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f),
                     IM_COL32(36, 36, 38, 255), h * 0.3f);
    d->AddRect(ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), IM_COL32(80, 80, 84, 255),
               h * 0.3f, 0, 1.2f);
    d->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(230, 230, 232, 255), t);
}

// a binding's name: the mouse's own, else the keyboard layout's name for the key
std::string KeyName(int vk) {
    switch (vk) {
        case 0: return "-";
        case VK_LBUTTON: return "Left mouse";
        case VK_RBUTTON: return "Right mouse";
        case VK_MBUTTON: return "Middle mouse";
        case VK_XBUTTON1: return "Mouse 4";
        case VK_XBUTTON2: return "Mouse 5";
        case kWheelUp: return "Wheel up";
        case kWheelDown: return "Wheel down";
        default: break;
    }
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC);
    bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT || vk == VK_INSERT ||
                    vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                    vk == VK_RCONTROL || vk == VK_RMENU || vk == VK_DIVIDE;
    wchar_t name[64] = {};
    if (sc && GetKeyNameTextW(LONG(sc << 16 | (extended ? 1 << 24 : 0)), name, 64) > 0) return Utf8(name);
    char hex[16];
    std::snprintf(hex, sizeof(hex), "Key %02X", vk);
    return hex;
}

// waiting for a key for a binding slot: the keys held when it starts do not count (the click that started it)
void StartCapture(Ui& ui, int slot) {
    ui.capture = slot;
    for (int vk = 1; vk < 255; vk++) ui.capture_held[size_t(vk)] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    g_wheel_seen = 0;
}

void PollCapture(Ui& ui) {
    if (ui.capture < 0) return;
    int got = 0;
    if (int w = g_wheel_seen.exchange(0)) got = w > 0 ? kWheelUp : kWheelDown;
    for (int vk = 1; vk < 255 && !got; vk++) {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (!down) {
            ui.capture_held[size_t(vk)] = false;
            continue;
        }
        // the generic shift / control / alt codes come with their left or right one: take the side
        if (ui.capture_held[size_t(vk)] || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU) continue;
        got = vk;
    }
    if (!got) return;
    ui.s.keys[size_t(ui.capture / 2)][size_t(ui.capture % 2)] = got;
    ui.capture = -1;
}

void PageControls(Ui& ui) {
    PageTitle("CONTROLS", "Gamepad mapping, controllers and the keyboard.");
    Heading("BUTTON ICONS");
    Row("In-game button icons");
    {
        bool modern = ui.s.texture_packs.find("modern_buttons") != std::string::npos;
        int cur = modern ? 0 : 1;
        if (Combo("##icons", &cur, {"Modern Xbox", "Original Xbox"})) ui.s.texture_packs = cur == 0 ? "modern_buttons" : "";
    }
    Desc("Modern Xbox shows today's controller buttons (LB and RB for White and Black, LT, RT, LS, RS) in the "
         "game's prompts and menus, drawn sharp for high resolutions. Applied when the game starts.");
    Row("Vibration");
    Slider("##rumble", &ui.s.rumble, 0, 100, 5, "%d%%");
    Desc("Strength of the controller's vibration; 0% turns it off.");
    Heading("GAMEPAD MAPPING");
    Desc("Which button of your controller presses each button of the game's Xbox controller. The game's Black and "
         "White buttons are RB and LB by default. Applied when the game starts.");
    std::vector<std::string> names = {"None"};
    for (const char* n : kControllerButtons) names.push_back(n);
    for (int g = 0; g < 16; g++) {
        Row(kGameButtons[g]);
        int& m = ui.s.pad_map[size_t(g)];
        ButtonGlyph(m);
        ImGui::SameLine();
        int cur = m + 1;
        ImGui::PushID(g);
        if (Combo("##map", &cur, names)) {
            // a controller button presses one game button: the one it pressed before gets this one's old button
            int chosen = cur - 1;
            if (chosen >= 0)
                for (int& other : ui.s.pad_map)
                    if (&other != &m && other == chosen) other = m;
            m = chosen;
        }
        ImGui::PopID();
        if (m != kDefaultPadMap[size_t(g)]) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("changed");
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(g_col);
    ImGui::BeginDisabled(ui.s.pad_map == kDefaultPadMap);
    if (Button("Reset gamepad to defaults", ImGui::GetFontSize() * 14)) ui.s.pad_map = kDefaultPadMap;
    ImGui::EndDisabled();
    Desc(nullptr);
    Heading("CONTROLLERS");
    int n = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&n);
    if (n == 0) Desc("No controller connected. Xbox, PlayStation and Switch controllers work.");
    for (int i = 0; i < n; i++) {
        const char* name = SDL_GetGamepadNameForID(pads[i]);
        Row((std::string("Player ") + std::to_string(i + 1)).c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name ? name : "Controller");
    }
    SDL_free(pads);

    Heading("KEYBOARD AND MOUSE");
    Row("Mouse look");
    Check("##mlook", &ui.s.mouse_look);
    Desc("The mouse turns the camera. The cursor is hidden and kept inside the game window while it has focus.");
    if (ui.s.mouse_look) {
        Row("Mouse sensitivity");
        Slider("##msens", &ui.s.mouse_sensitivity, 1, 100, 1, "%d");
        Desc(nullptr);
        Row("Invert vertical");
        Check("##minv", &ui.s.mouse_invert_y);
        Desc(nullptr);
    }
    Desc("Click a slot, then press a key, a mouse button or turn the wheel. Right click a slot to clear it. "
         "Applied when the game starts. F9 in the game shows the frame counter.");
    for (int i = 0; i < kKeyActions; i++) {
        Row(i < 16 ? kGameButtons[i] : kStickActions[i - 16]);
        for (int j = 0; j < 2; j++) {
            ImGui::PushID(1000 + i * 2 + j);
            bool waiting = ui.capture == i * 2 + j;
            std::string label = waiting ? std::string("Press a key...") : KeyName(ui.s.keys[size_t(i)][size_t(j)]);
            if (waiting) ImGui::PushStyleColor(ImGuiCol_Button, kPlay);
            if (Button(label.c_str(), ImGui::GetFontSize() * 9)) StartCapture(ui, i * 2 + j);
            if (waiting) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) ui.s.keys[size_t(i)][size_t(j)] = 0;
            ImGui::PopID();
            if (j == 0) ImGui::SameLine();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::SetCursorPosX(g_col);
    ImGui::BeginDisabled(ui.s.keys == kDefaultKeys);
    if (Button("Reset keyboard and mouse", ImGui::GetFontSize() * 14)) ui.s.keys = kDefaultKeys;
    ImGui::EndDisabled();
    Desc(nullptr);
}

void PageSaves(Ui& ui) {
    PageTitle("SAVES", "The games saved on the port's Xbox hard disk.");
    Heading("SAVED GAMES");
    if (ui.saves.empty()) Desc("No saved games yet.");
    for (auto& s : ui.saves) {
        Row(s.name.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s.when.c_str());
        ImGui::PopStyleColor();
    }
    Heading("BACKUPS");
    float bw = ImGui::GetFontSize() * 10;
    Row("Saves folder");
    if (Button("Open##saves", bw)) OpenFolder(SavesDir());
    Desc(nullptr);
    Row("Back up");
    if (Button("Back up now", bw)) Note(ui, BackUpSaves());
    ImGui::SameLine();
    if (Button("Open backups", bw)) OpenFolder(ExeDir() / "save backups");
    Desc("Copies every save into the save backups folder beside the port, stamped with the date.");
}

void PageAbout(Ui&) {
    PageTitle("ABOUT", "IndyPort");
    Heading("THE PORT");
    ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - ImGui::GetFontSize());
    ImGui::TextUnformatted("An unofficial native PC port of the original Xbox version of Indiana Jones and the "
                           "Emperor's Tomb, made by statically recompiling the game's code.");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted("No game files are included. The port runs from your own legally acquired copy of the game.");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextUnformatted("Built with Xbox XBE Recomp by TGP482, Dear ImGui, SDL3 and toml++. Indiana Jones is a "
                           "trademark of Lucasfilm Ltd. This project is not affiliated with or endorsed by Lucasfilm, "
                           "LucasArts, Disney or Microsoft.");
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    Heading("VERSION");
    Row("IndyPort");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kVersion);
}

// the banner across the top of Home: the key art filling the width, faded into the page at the bottom and the left
void Banner(float w, float h) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (g_banner.srv) {
        float ia = float(g_banner.w) / float(g_banner.h), ba = w / h;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (ba > ia) {  // cut the top and bottom, keeping the hat's brim and the title
            float vh = ia / ba;
            uv0.y = 0.22f * (1 - vh);
            uv1.y = uv0.y + vh;
        } else {
            float uw = ba / ia;
            uv0.x = 0.3f * (1 - uw);
            uv1.x = uv0.x + uw;
        }
        d->AddImage(ImTextureID(g_banner.srv), p, ImVec2(p.x + w, p.y + h), uv0, uv1);
    } else {
        d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(60, 36, 16, 255));
    }
    ImU32 clear = kBg & 0x00FFFFFF;
    d->AddRectFilledMultiColor(ImVec2(p.x, p.y + h * 0.55f), ImVec2(p.x + w, p.y + h), clear, clear, kBg, kBg);
    d->AddRectFilledMultiColor(p, ImVec2(p.x + w * 0.06f, p.y + h), kBg, clear, clear, kBg);
    ImGui::Dummy(ImVec2(w, h));
}

void PageHome(Ui& ui, float banner_h) {
    ImVec2 start = ImGui::GetCursorPos();
    float pad = ImGui::GetFontSize() * 2;
    ImGui::SetCursorPos(ImVec2(0, 0));
    Banner(ImGui::GetWindowWidth(), banner_h);
    ImGui::SetCursorPos(ImVec2(start.x, banner_h - pad * 0.5f));

    ImGui::PushFont(g_font_head);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("Indiana Jones and the Emperor's Tomb  \xc2\xb7  Xbox (USA)");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImU32 colour = ui.game.state == GameFiles::Ready ? kDim : kBad;
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    if (ui.game.state == GameFiles::Ready) ImGui::Text("%s", ui.game.what.c_str());
    else ImGui::Text("%s Choose your copy of the game on the Game page.", ui.game.what.c_str());
    ImGui::PopStyleColor();

    Heading("QUICK SETTINGS");
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetFontSize()) * 0.5f;
    float label = ImGui::GetFontSize() * 9;
    float cw = std::min(ControlWidth() * 0.85f, half - label - ImGui::GetFontSize());
    float x0 = ImGui::GetCursorPosX(), x1 = x0 + half;
    auto cell = [&](float x, const char* name) {
        ImGui::SetCursorPosX(x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::SameLine(x + label);
    };
    cell(x0, "Display mode");
    DisplayModeControl(ui, cw);
    ImGui::SameLine();
    cell(x1, "Resolution");
    ResolutionControl(ui, cw);
    cell(x0, "Frame rate limit");
    FpsControl(ui, cw);
    ImGui::SameLine();
    cell(x1, "Render scale");
    ScaleControl(ui, cw);
    cell(x0, "Aspect ratio");
    AspectControl(ui, cw);
    ImGui::SameLine();
    cell(x1, "Draw distance");
    {
        const int values[] = {100, 150, 200, 300, 400};
        std::vector<std::string> names = {"Original", "150%", "200%", "300%", "400%"};
        int cur = 0;
        for (int i = 0; i < 5; i++)
            if (values[i] == ui.s.draw_distance) cur = i;
        if (Combo("##ddq", &cur, names, cw)) ui.s.draw_distance = values[cur];
    }
    cell(x0, "FPS counter");
    Check("##showfps", &ui.s.show_fps);
    ImGui::SameLine();
    cell(x1, "VSync");
    Check("##vsync", &ui.s.vsync);
}

void ApplyTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 9);
    st.FrameRounding = 2;
    st.PopupRounding = 2;
    st.GrabRounding = 2;
    st.GrabMinSize = 14;
    st.ScrollbarSize = 10;
    st.WindowBorderSize = 0;
    st.ChildBorderSize = 0;
    st.FrameBorderSize = 1;
    st.PopupBorderSize = 1;
    ImVec4* c = st.Colors;
    auto col = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    c[ImGuiCol_WindowBg] = col(kBg);
    c[ImGuiCol_ChildBg] = col(kBg);
    c[ImGuiCol_PopupBg] = col(IM_COL32(28, 24, 21, 255));
    c[ImGuiCol_Border] = col(kLine);
    c[ImGuiCol_Text] = col(kText);
    c[ImGuiCol_TextDisabled] = col(kDim);
    c[ImGuiCol_FrameBg] = col(IM_COL32(26, 22, 19, 255));
    c[ImGuiCol_FrameBgHovered] = col(IM_COL32(38, 32, 27, 255));
    c[ImGuiCol_FrameBgActive] = col(IM_COL32(46, 38, 32, 255));
    c[ImGuiCol_SliderGrab] = col(kAccent);
    c[ImGuiCol_SliderGrabActive] = col(IM_COL32(255, 190, 80, 255));
    c[ImGuiCol_Header] = col(IM_COL32(60, 44, 30, 255));
    c[ImGuiCol_HeaderHovered] = col(IM_COL32(80, 56, 34, 255));
    c[ImGuiCol_HeaderActive] = col(IM_COL32(96, 66, 38, 255));
    c[ImGuiCol_NavHighlight] = col(kAccent);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = col(IM_COL32(60, 52, 45, 255));
    c[ImGuiCol_Button] = col(IM_COL32(30, 26, 22, 255));
    c[ImGuiCol_ButtonHovered] = col(IM_COL32(48, 40, 33, 255));
    c[ImGuiCol_ButtonActive] = col(IM_COL32(62, 50, 40, 255));
    st.ScaleAllSizes(scale);
}

void LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    static const ImWchar ranges[] = {0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x2027, 0};
    auto add = [&](const char* file, float size) -> ImFont* {
        std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (fs::exists(path)) return io.Fonts->AddFontFromFileTTF(path.c_str(), size * scale, nullptr, ranges);
        ImFontConfig cfg;
        cfg.SizePixels = size * scale;
        return io.Fonts->AddFontDefault(&cfg);
    };
    g_font = add("segoeui.ttf", 18);
    g_font_head = add("georgiab.ttf", 20);
    g_font_nav = add("georgiab.ttf", 21);
    g_font_title = add("georgiab.ttf", 40);
    g_font_logo = add("georgiab.ttf", 38);
    g_font_play = add("georgiab.ttf", 30);
}

// the sidebar: the logo, the pages, a hint and the version
void Sidebar(Ui& ui, float width, float height) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(o, ImVec2(o.x + width, o.y + height), kSidebar);
    d->AddLine(ImVec2(o.x + width - 1, o.y), ImVec2(o.x + width - 1, o.y + height), kLine);
    float pad = ImGui::GetFontSize() * 1.8f;

    ImGui::SetCursorPos(ImVec2(pad, pad * 1.1f));
    ImGui::PushFont(g_font_logo);
    ImGui::TextUnformatted("Indy");
    ImGui::SameLine(0, 0);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("Port");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted("The Emperor's Tomb on PC");
    ImGui::PopStyleColor();

    ImGui::SetCursorPosY(pad * 4.6f);
    float row_h = ImGui::GetFontSize() * 3.2f;
    for (int i = 0; i < int(Page::Count); i++) {
        Page p = Page(i);
        bool sel = ui.page == p;
        ImGui::SetCursorPosX(0);
        ImVec2 at = ImGui::GetCursorScreenPos();
        if (sel) d->AddRectFilled(at, ImVec2(at.x + width - 1, at.y + row_h), kSelRow);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("page", ImVec2(width - 1, row_h))) ui.page = p;
        bool hot = ImGui::IsItemHovered();
        ImGui::PopID();
        ImGui::PushFont(g_font_nav);
        float th = ImGui::GetFontSize();
        d->AddText(ImVec2(at.x + pad, at.y + (row_h - th) * 0.5f), sel ? kAccent : hot ? kText : IM_COL32(205, 196, 184, 255),
                   kPageNames[i]);
        ImGui::PopFont();
    }

    float fs = ImGui::GetFontSize();
    ImGui::SetCursorPos(ImVec2(pad, height - fs * 5.4f));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushTextWrapPos(width - pad);
    ImGui::TextUnformatted("In the game: F9 shows the frame counter.");
    ImGui::PopTextWrapPos();
    ImGui::SetCursorPosX(pad);
    ImGui::Text("v%s", kVersion);
    ImGui::PopStyleColor();
}

// the bar along the bottom: what will run, and Play
void BottomBar(Ui& ui, float x, float y, float w, float h, bool focus_play) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + w, o.y + y + h), kBar);
    d->AddLine(ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + w, o.y + y), kLine);
    float pad = ImGui::GetFontSize() * 2;

    // the summary: resolution, scale, frame rate
    const Settings& s = ui.s;
    std::string res = s.width ? std::to_string(s.width) + " x " + std::to_string(s.height) : "Desktop resolution";
    std::string fps = s.fps_cap == 0 ? "unlimited FPS" : s.fps_cap == 1 ? "monitor refresh rate" : std::to_string(s.fps_cap) + " FPS";
    const char* modes[] = {"Fullscreen", "Borderless", "Windowed"};
    std::string summary = std::string(modes[s.display_mode]) + "  \xc2\xb7  " + res + "  \xc2\xb7  " +
                          std::to_string(s.render_percent) + "% scale  \xc2\xb7  " + fps;
    if (!ui.message.empty() && ImGui::GetTime() < ui.message_until) summary = ui.message;
    ImVec2 ts = ImGui::CalcTextSize(summary.c_str());
    d->AddText(ImVec2(o.x + x + pad, o.y + y + (h - ts.y) * 0.5f),
               summary == ui.message ? kAccent : kDim, summary.c_str());

    // Play, or the way to the game page while there is no game
    float bw = ImGui::GetFontSize() * 13, bh = h - ImGui::GetFontSize() * 1.2f;
    ImGui::SetCursorPos(ImVec2(x + w - bw - pad, y + (h - bh) * 0.5f));
    bool ready = ui.game.state == GameFiles::Ready;
    const char* label = g_game_running ? "RUNNING" : ready ? "PLAY" : "CHOOSE GAME";
    ImGui::PushStyleColor(ImGuiCol_Button, g_game_running ? IM_COL32(60, 52, 45, 255) : kPlay);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(206, 108, 28, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(150, 72, 16, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    ImGui::PushFont(ready ? g_font_play : g_font_nav);
    if (focus_play) ImGui::SetKeyboardFocusHere();
    if (ImGui::Button(label, ImVec2(bw, bh)) && !g_game_running) {
        if (ready) Play(ui.window);
        else ui.page = Page::Game;
    }
    ImGui::PopFont();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void CreateTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION,
                                               &sd, &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr))
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, want, 2, D3D11_SDK_VERSION, &sd,
                                           &g_swap, &g_dev, &fl, &g_ctx);
    if (FAILED(hr)) return false;
    CreateTarget();
    return true;
}

}  // namespace

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    switch (msg) {
        case WM_SIZE:
            if (wp != SIZE_MINIMIZED) {
                g_resize_w = LOWORD(lp);
                g_resize_h = HIWORD(lp);
            }
            return 0;
        case WM_MOUSEWHEEL:
            g_wheel_seen = GET_WHEEL_DELTA_WPARAM(wp);
            break;
        case WM_GETMINMAXINFO: {
            UINT dpi = GetDpiForWindow(hwnd);
            if (!dpi) dpi = 96;
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {LONG(960 * dpi / 96), LONG(640 * dpi / 96)};
            return 0;
        }
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // no Alt menu
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst};
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"IndyPort";
    RegisterClassExW(&wc);
    float scale = float(GetDpiForSystem()) / 96.0f;
    int w = int(1240 * scale), h = int(800 * scale);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    w = std::min<int>(w, wa.right - wa.left);
    h = std::min<int>(h, wa.bottom - wa.top);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"IndyPort (PC)", WS_OVERLAPPEDWINDOW,
                                wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                                nullptr, nullptr, inst, nullptr);
    // the dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE)
    BOOL dark = TRUE;
    using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
        if (auto set = reinterpret_cast<DwmSet>(GetProcAddress(dwm, "DwmSetWindowAttribute")))
            set(hwnd, 20, &dark, sizeof(dark));
    if (!CreateDevice(hwnd)) {
        MessageBoxW(hwnd, L"Direct3D 11 is not available.", L"IndyPort", MB_ICONERROR);
        return 1;
    }
    g_banner = LoadResourceImage(101);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    LoadFonts(scale);
    ApplyTheme(scale);
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    Ui ui;
    ui.window = hwnd;
    Log("IndyPort %s, game: %s (%s)", kVersion, ui.game.what.c_str(), ui.game.where.c_str());
    // INDY_LAUNCHER_PAGE=0..5 opens a page at once (screenshots)
    if (const char* pg = std::getenv("INDY_LAUNCHER_PAGE"); pg && *pg >= '0' && *pg <= '5') ui.page = Page(*pg - '0');
    LoadModes(ui);
    bool quit = false;
    double checked = 0;
    while (!quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;
        SDL_PumpEvents();
        if (IsIconic(hwnd)) {
            Sleep(50);
            continue;
        }
        if (g_resize_w && g_resize_h) {
            if (g_rtv) g_rtv->Release();
            g_rtv = nullptr;
            g_swap->ResizeBuffers(0, g_resize_w, g_resize_h, DXGI_FORMAT_UNKNOWN, 0);
            g_resize_w = g_resize_h = 0;
            CreateTarget();
        }

        // the controller only moves the launcher while it is the window in front (never while the game plays)
        bool front = GetForegroundWindow() == hwnd && !g_game_running;
        if (front) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // a key binding waiting for its key takes every key, Escape included
        bool capturing = ui.capture >= 0;
        PollCapture(ui);
        if (ui.page != Page::Controls) ui.capture = -1;
        // pages with LB / RB, Home with B or Escape, Play with Start
        bool popup = capturing || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
        int count = int(Page::Count), p = int(ui.page);
        if (!capturing && ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) ui.page = Page((p + count - 1) % count);
        if (!capturing && ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) ui.page = Page((p + 1) % count);
        if (!popup && (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape)))
            ui.page = Page::Home;
        if (front && !popup && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) &&
            ui.game.state == GameFiles::Ready)
            Play(hwnd);
        if (ImGui::GetTime() > checked + 2) {
            ui.saves = ListSaves();
            ui.game = CheckGame(ui.s.game_path);
            checked = ImGui::GetTime();
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushFont(g_font);
        float fs = ImGui::GetFontSize();
        float bar_h = fs * 4.4f;
        float side_w = fs * 15;
        Sidebar(ui, side_w, io.DisplaySize.y);

        // a new page puts the focus on its first control (Home: on Play), shown at once, so the first A or Enter
        // presses it
        bool new_page = int(ui.page) != ui.shown;
        ui.shown = int(ui.page);
        float page_w = io.DisplaySize.x - side_w, page_h = io.DisplaySize.y - bar_h;
        bool home = ui.page == Page::Home;
        float pad = fs * 2.2f;
        ImGui::SetCursorPos(ImVec2(side_w, 0));
        ImGui::BeginChild("page", ImVec2(page_w, page_h), ImGuiChildFlags_None,
                          home ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse : 0);
        if (new_page) {
            ImGui::SetScrollY(0);
            if (!home) {
                ImGui::SetWindowFocus();
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::GetCurrentContext()->NavDisableHighlight = false;
        }
        ImGui::Indent(pad);
        ImGui::SetCursorPosY(home ? 0 : pad * 0.8f);
        g_col = pad + fs * 13;
        ImGui::PushItemWidth(ControlWidth());
        switch (ui.page) {
            case Page::Home: PageHome(ui, std::min(page_h * 0.52f, page_w / 2.2f)); break;
            case Page::Graphics: PageGraphics(ui); break;
            case Page::Game: PageGame(ui); break;
            case Page::Controls: PageControls(ui); break;
            case Page::Saves: PageSaves(ui); break;
            case Page::About: PageAbout(ui); break;
            default: break;
        }
        ImGui::PopItemWidth();
        ImGui::Dummy(ImVec2(0, pad));
        ImGui::Unindent(pad);
        ImGui::EndChild();

        if (new_page && home) ImGui::SetWindowFocus();
        BottomBar(ui, side_w, page_h, page_w, bar_h, new_page && home);
        ImGui::PopFont();
        ImGui::End();

        // settings take effect when they change: the game reads the file at its start
        if (!(ui.s == ui.saved)) {
            SaveSettings(ui.s);
            ui.saved = ui.s;
        }

        ImGui::Render();
        const float clear[4] = {20 / 255.0f, 17 / 255.0f, 15 / 255.0f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_banner.srv) g_banner.srv->Release();
    if (g_rtv) g_rtv->Release();
    g_swap->Release();
    g_ctx->Release();
    g_dev->Release();
    DestroyWindow(hwnd);
    SDL_Quit();
    CoUninitialize();
    return 0;
}
