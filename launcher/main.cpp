// IndyET launcher: sets up the game from the player's own disc image, edits the settings the port reads and starts it.
// Dear ImGui on Win32 and Direct3D 11 (SDL3 only for controller names and display modes), mouse, keyboard or controller. No game files are part of it.
//
// Pages: Home (status, Play, Settings), and Settings with Graphics, Controls, Saves and About in a sidebar.
// It lives beside IndyET.exe; the game's files are in an "assets" folder beside it or in any folder above, as the
// port looks for them; settings.toml and the hdd folder (saves) are beside the exe.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <d3d11.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <toml++/toml.hpp>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "0.1.0";
constexpr const wchar_t* kGameExe = L"IndyET.exe";
constexpr const char* kTitleId = "4c410007";

// colours of the dark theme
constexpr ImU32 kBg = IM_COL32(18, 20, 26, 255);
constexpr ImU32 kSidebar = IM_COL32(22, 25, 33, 255);
constexpr ImU32 kLine = IM_COL32(40, 44, 54, 255);
constexpr ImU32 kText = IM_COL32(232, 234, 238, 255);
constexpr ImU32 kDim = IM_COL32(140, 146, 158, 255);
constexpr ImU32 kAccent = IM_COL32(214, 160, 60, 255);  // the whip's leather
constexpr ImU32 kToggleOff = IM_COL32(52, 56, 66, 255);

ImFont* g_font = nullptr;
ImFont* g_font_big = nullptr;
ImFont* g_font_title = nullptr;
ImFont* g_font_caps = nullptr;

// ---- paths

fs::path ExeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

// the port's own rule: an "assets" folder beside the exe or in any folder above it
fs::path FindAssets() {
    std::error_code ec;
    for (fs::path dir = ExeDir(); !dir.empty(); dir = dir.parent_path()) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (dir == dir.parent_path()) break;
    }
    return {};
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

void OpenFolder(const fs::path& p) {
    std::error_code ec;
    fs::create_directories(p, ec);
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ---- the game's files

struct GameFiles {
    bool found = false;
    std::string what;  // "extracted files" or the disc image's name
};

GameFiles CheckGame() {
    GameFiles g;
    fs::path a = FindAssets();
    std::error_code ec;
    if (a.empty()) return g;
    if (fs::exists(a / "default.xbe", ec)) {
        g.found = true;
        g.what = "Extracted game files";
        return g;
    }
    for (auto& e : fs::directory_iterator(a, ec)) {
        std::wstring ext = e.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".iso") {
            g.found = true;
            g.what = Utf8(e.path().filename().wstring());
            return g;
        }
    }
    return g;
}

// adding a disc image: a hard link when it is on the same drive (instant), else a copy with progress
struct AddJob {
    std::atomic<bool> running{false}, done{false};
    std::atomic<uint64_t> copied{0}, total{0};
    std::mutex lock;
    std::string error;
};
AddJob g_add;

void CopyIso(fs::path src) {
    g_add.running = true;
    g_add.done = false;
    g_add.copied = 0;
    std::error_code ec;
    fs::path assets = ExeDir().parent_path().parent_path().parent_path() / "assets";  // out\build\release -> project
    if (fs::path a = FindAssets(); !a.empty()) assets = a;
    else if (!fs::exists(ExeDir() / "IndyET.dll", ec)) assets = ExeDir() / "assets";
    fs::create_directories(assets, ec);
    fs::path dst = assets / src.filename();
    g_add.total = fs::file_size(src, ec);
    if (!CreateHardLinkW(dst.c_str(), src.c_str(), nullptr)) {
        std::ifstream in(src, std::ios::binary);
        std::ofstream out(dst, std::ios::binary);
        std::vector<char> buf(8 << 20);
        while (in && out) {
            in.read(buf.data(), std::streamsize(buf.size()));
            std::streamsize n = in.gcount();
            if (n <= 0) break;
            out.write(buf.data(), n);
            g_add.copied += uint64_t(n);
        }
        if (!out) {
            std::lock_guard l(g_add.lock);
            g_add.error = "Copying the disc image failed (is there enough free space?)";
            out.close();
            fs::remove(dst, ec);
        }
    }
    g_add.copied = g_add.total.load();
    g_add.running = false;
    g_add.done = true;
}

// the disc image to add, from the Windows open dialog
std::wstring PickIso(HWND owner) {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Xbox disc image (*.iso)\0*.iso\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose your disc image of the game";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? file : L"";
}

// ---- settings.toml beside the game

struct Settings {
    int display_mode = 1;  // fullscreen, borderless, windowed
    int width = 0, height = 0;
    int render_percent = 100;
    int fps_cap = 60;
    bool vsync = true;
    int aniso = 0;
    bool custom_textures = true;
    bool dump_textures = false;
    int dump_format = 0;
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
        s.aniso = int(t["anisotropic_filtering"].value_or(int64_t(0)));
        s.custom_textures = t["custom_textures"].value_or(true);
        s.dump_textures = t["dump_textures"].value_or(false);
        s.dump_format = int(t["texture_dump_format"].value_or(int64_t(0)));
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
    t.insert_or_assign("custom_textures", s.custom_textures);
    t.insert_or_assign("dump_textures", s.dump_textures);
    t.insert_or_assign("texture_dump_format", int64_t(s.dump_format));
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

// ---- widgets

// a settings row: the label with an "i" that shows the help on hover, and the control right-aligned
void RowLabel(const char* label, const char* help) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) {
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float r = ImGui::GetFontSize() * 0.42f;
        ImVec2 c(p.x + r, p.y + ImGui::GetFrameHeight() * 0.5f);
        ImGui::GetWindowDrawList()->AddCircle(c, r, kDim, 16, 1.2f);
        ImGui::GetWindowDrawList()->AddText(ImVec2(c.x - ImGui::CalcTextSize("i").x * 0.5f, c.y - ImGui::GetFontSize() * 0.5f),
                                            kDim, "i");
        ImGui::Dummy(ImVec2(r * 2, ImGui::GetFrameHeight()));
        if (ImGui::IsItemHovered()) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
            ImGui::TextUnformatted(help);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
            ImGui::PopStyleVar();
        }
    }
}

void RowSeparator() {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize * 2;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 4), ImVec2(p.x + w, p.y + 4), kLine);
    ImGui::Dummy(ImVec2(w, 9));
}

void RightAlign(float width) {
    float x = ImGui::GetWindowContentRegionMax().x - width - ImGui::GetStyle().ScrollbarSize * 2;
    ImGui::SameLine(x);
}

bool Toggle(const char* id, bool* v) {
    float h = ImGui::GetFrameHeight() * 0.75f, w = h * 1.9f;
    RightAlign(w);
    ImVec2 p = ImGui::GetCursorScreenPos();
    p.y += (ImGui::GetFrameHeight() - h) * 0.5f;
    ImGui::SetCursorScreenPos(p);
    bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    if (pressed) *v = !*v;
    ImDrawList* d = ImGui::GetWindowDrawList();
    bool focus = ImGui::IsItemFocused() || ImGui::IsItemHovered();
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), *v ? kText : kToggleOff, h * 0.5f);
    if (focus) d->AddRect(ImVec2(p.x - 3, p.y - 3), ImVec2(p.x + w + 3, p.y + h + 3), kAccent, h * 0.5f + 3, 0, 2);
    float r = h * 0.5f - 3;
    ImVec2 knob(*v ? p.x + w - r - 3 : p.x + r + 3, p.y + h * 0.5f);
    d->AddCircleFilled(knob, r, *v ? kBg : IM_COL32(120, 126, 138, 255), 20);
    return pressed;
}

bool Combo(const char* id, int* current, const std::vector<std::string>& items) {
    float w = ImGui::GetFontSize() * 13;
    RightAlign(w);
    ImGui::SetNextItemWidth(w);
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

bool Slider(const char* id, int* v, int lo, int hi, int step, const char* fmt) {
    float w = ImGui::GetFontSize() * 13;
    RightAlign(w);
    ImGui::SetNextItemWidth(w);
    int s = *v / step;
    bool changed = ImGui::SliderInt(id, &s, lo / step, hi / step, "");
    if (changed) *v = s * step;
    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImGui::GetWindowDrawList()->AddText(ImVec2(r1.x - ts.x - 8, r0.y + (r1.y - r0.y - ts.y) * 0.5f), kText, buf);
    return changed;
}

void SectionTitle(const char* caps) {
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.6f));
    ImGui::PushFont(g_font_caps);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(caps);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    RowSeparator();
}

bool BigButton(const char* label, ImVec2 size, bool primary) {
    ImGui::PushStyleColor(ImGuiCol_Button, primary ? kAccent : IM_COL32(32, 36, 46, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, primary ? IM_COL32(232, 182, 84, 255) : IM_COL32(44, 49, 62, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, primary ? IM_COL32(190, 140, 50, 255) : IM_COL32(56, 62, 78, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, primary ? kBg : kText);
    ImGui::PushFont(g_font_caps);
    bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(4);
    return r;
}

// ---- pages

enum class Page { Home, Graphics, Controls, Saves, About };
const char* kPageNames[] = {"Home", "Graphics", "Controls", "Saves", "About"};

struct Ui {
    Page page = Page::Home;
    int shown = -1;  // the page drawn last frame: a new one puts the controller's focus on its first control
    Settings s = LoadSettings();
    Settings saved = s;
    GameFiles game = CheckGame();
    std::vector<std::pair<int, int>> modes;  // display modes, desktop first as 0x0
    std::vector<Save> saves = ListSaves();
    std::string message;
    double message_until = 0;
    HWND window = nullptr;
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

void PageGraphics(Ui& ui) {
    Settings& s = ui.s;
    SectionTitle("D I S P L A Y");
    RowLabel("Display mode", "Fullscreen changes the monitor's mode; Borderless fills the screen with a window; Windowed "
                             "opens a window of the chosen resolution.");
    Combo("##mode", &s.display_mode, {"Fullscreen", "Borderless", "Windowed"});
    RowSeparator();

    RowLabel("Resolution", "The size of the game's output. Desktop uses your monitor's current resolution.");
    {
        std::vector<std::string> names;
        int cur = 0;
        for (size_t i = 0; i < ui.modes.size(); i++) {
            auto [w, h] = ui.modes[i];
            names.push_back(w ? std::to_string(w) + " x " + std::to_string(h) : "Desktop");
            if (w == s.width && h == s.height) cur = int(i);
        }
        if (Combo("##res", &cur, names)) {
            s.width = ui.modes[size_t(cur)].first;
            s.height = ui.modes[size_t(cur)].second;
        }
    }
    RowSeparator();

    RowLabel("Render scale", "Draws the game at this share of the output height: above 100% is supersampling (sharper, "
                             "slower), below is faster.");
    Slider("##scale", &s.render_percent, 50, 200, 10, "%d%%");
    RowSeparator();

    RowLabel("Frame rate limit", "The Xbox ran at 30 or 60 frames a second. Higher limits make the game smoother where it "
                                 "keeps its speed.");
    {
        const int caps[] = {30, 60, 1, 120, 144, 0};
        std::vector<std::string> names = {"30", "60", "Monitor refresh rate", "120", "144", "Unlimited"};
        int cur = 1;
        for (int i = 0; i < 6; i++)
            if (caps[i] == s.fps_cap) cur = i;
        if (Combo("##fps", &cur, names)) s.fps_cap = caps[cur];
    }
    RowSeparator();

    RowLabel("V-Sync", "Waits for the monitor's refresh: no tearing.");
    Toggle("##vsync", &s.vsync);
    RowSeparator();

    SectionTitle("T E X T U R E S");
    RowLabel("Anisotropic filtering", "Keeps textures sharp at steep angles, such as floors and paths far away.");
    {
        const int levels[] = {0, 2, 4, 8, 16};
        std::vector<std::string> names = {"As the game asks", "2x", "4x", "8x", "16x"};
        int cur = 0;
        for (int i = 0; i < 5; i++)
            if (levels[i] == s.aniso) cur = i;
        if (Combo("##aniso", &cur, names)) s.aniso = levels[cur];
    }
    RowSeparator();

    RowLabel("Load custom textures", "Images in the textures\\replacement folder replace the game's textures.");
    Toggle("##custom", &s.custom_textures);
    RowSeparator();
    ImGui::Dummy(ImVec2(0, 6));
    if (BigButton("O P E N   T E X T U R E S   F O L D E R", ImVec2(0, ImGui::GetFrameHeight() * 1.6f), false))
        OpenFolder(ExeDir() / "textures" / "replacement");

    SectionTitle("A D V A N C E D");
    RowLabel("Dump textures", "Saves every texture the game uses into textures\\dumps, for texture packs.");
    Toggle("##dump", &s.dump_textures);
    RowSeparator();
    RowLabel("Dump format", "PNG to edit; DDS keeps the original compression and mipmaps.");
    Combo("##dumpfmt", &s.dump_format, {"PNG", "DDS"});
    RowSeparator();
}

void PageControls(Ui&) {
    SectionTitle("C O N T R O L L E R");
    int n = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&n);
    if (n == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("No controller connected. Xbox, PlayStation and Switch controllers work; the keyboard also "
                           "plays as the first controller.");
        ImGui::PopStyleColor();
    }
    for (int i = 0; i < n; i++) {
        const char* name = SDL_GetGamepadNameForID(pads[i]);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Player %d", i + 1);
        RightAlign(ImGui::CalcTextSize(name ? name : "Controller").x);
        ImGui::TextUnformatted(name ? name : "Controller");
        RowSeparator();
    }
    SDL_free(pads);

    SectionTitle("K E Y B O A R D");
    const char* rows[][2] = {{"Move", "W A S D"},          {"Camera", "I J K L"},       {"D-pad", "Arrow keys"},
                             {"A", "Space or Enter"},       {"B", "Q or Backspace"},     {"X", "E"},
                             {"Y", "R"},                    {"Start", "Enter"},          {"Back", "Tab"},
                             {"Left / right trigger", "Left Shift / Left Ctrl"},          {"Black / white", "C / Z"},
                             {"Left / right stick click", "V / B"}};
    for (auto& r : rows) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(r[0]);
        RightAlign(ImGui::CalcTextSize(r[1]).x);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted(r[1]);
        ImGui::PopStyleColor();
        RowSeparator();
    }
}

void PageSaves(Ui& ui) {
    SectionTitle("S A V E D   G A M E S");
    if (ui.saves.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted("No saved games yet.");
        ImGui::PopStyleColor();
        RowSeparator();
    }
    for (auto& s : ui.saves) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(s.name.c_str());
        RightAlign(ImGui::CalcTextSize(s.when.c_str()).x);
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextUnformatted(s.when.c_str());
        ImGui::PopStyleColor();
        RowSeparator();
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (BigButton("O P E N   S A V E S   F O L D E R", ImVec2(0, ImGui::GetFrameHeight() * 1.6f), false))
        OpenFolder(SavesDir());
    ImGui::SameLine();
    if (BigButton("B A C K   U P   S A V E S", ImVec2(0, ImGui::GetFrameHeight() * 1.6f), false)) Note(ui, BackUpSaves());
}

void PageAbout(Ui& ui) {
    SectionTitle("A B O U T");
    ImGui::TextWrapped("An unofficial native PC port of the original Xbox version of Indiana Jones and the Emperor's "
                       "Tomb, made by statically recompiling the game's code.");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("No game files are included. The game runs from your own legally acquired copy: its disc image "
                       "or extracted files in the assets folder.");
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextWrapped("Built with Xbox XBE Recomp by TGP482, Dear ImGui, SDL3 and toml++. Indiana Jones is a trademark "
                       "of Lucasfilm Ltd. This project is not affiliated with or endorsed by Lucasfilm, LucasArts or "
                       "Microsoft.");
    ImGui::PopStyleColor();
    RowSeparator();
    RowLabel("Version", nullptr);
    RightAlign(ImGui::CalcTextSize(kVersion).x);
    ImGui::TextUnformatted(kVersion);
    RowSeparator();
    RowLabel("Game files", nullptr);
    std::string what = ui.game.found ? ui.game.what : "Not found";
    RightAlign(ImGui::CalcTextSize(what.c_str()).x);
    ImGui::TextUnformatted(what.c_str());
    RowSeparator();
}

void PageHome(Ui& ui) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Dummy(ImVec2(0, avail.y * 0.18f));
    ImGui::PushFont(g_font_title);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("INDIANA JONES");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(g_font_big);
    ImGui::TextUnformatted("and the Emperor's Tomb");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));

    if (g_add.running) {
        float f = g_add.total ? float(double(g_add.copied) / double(g_add.total)) : 0.0f;
        ImGui::TextUnformatted("Adding your disc image...");
        ImGui::ProgressBar(f, ImVec2(ImGui::GetFontSize() * 24, 0));
        return;
    }
    if (g_add.done.exchange(false)) {
        ui.game = CheckGame();
        std::lock_guard l(g_add.lock);
        Note(ui, g_add.error.empty() ? "The game is ready." : g_add.error);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ui.game.found ? kDim : IM_COL32(230, 120, 100, 255));
    ImGui::TextUnformatted(ui.game.found ? ui.game.what.c_str() : "Game files not found");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize()));

    ImVec2 big(ImGui::GetFontSize() * 11, ImGui::GetFontSize() * 2.6f);
    if (ui.game.found) {
        if (g_game_running) {
            BigButton("R U N N I N G", big, false);
        } else {
            if (BigButton("P L A Y", big, true)) Play(ui.window);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kDim);
        ImGui::TextWrapped("Choose a disc image (.iso) of your own copy of the game (Xbox, USA). It is linked or copied "
                           "into the assets folder; extracted files work too.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 6));
        if (BigButton("A D D   D I S C   I M A G E", big, true)) {
            std::wstring iso = PickIso(ui.window);
            if (!iso.empty()) {
                g_add.running = true;
                std::thread(CopyIso, fs::path(iso)).detach();
            }
        }
    }
    ImGui::SameLine();
    if (BigButton("S E T T I N G S", big, false)) ui.page = Page::Graphics;
}

void ApplyTheme(float scale) {
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    ImGui::StyleColorsDark(&st);
    st.WindowPadding = ImVec2(0, 0);
    st.FramePadding = ImVec2(12, 8);
    st.ItemSpacing = ImVec2(10, 10);
    st.FrameRounding = 4;
    st.PopupRounding = 4;
    st.GrabRounding = 10;
    st.GrabMinSize = 18;
    st.ScrollbarSize = 8;
    st.WindowBorderSize = 0;
    st.PopupBorderSize = 1;
    ImVec4* c = st.Colors;
    c[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(kBg);
    c[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(kBg);
    c[ImGuiCol_PopupBg] = ImVec4(0.11f, 0.12f, 0.15f, 1);
    c[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(kLine);
    c[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(kText);
    c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.11f, 0.14f, 1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.16f, 0.20f, 1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.18f, 0.19f, 0.24f, 1);
    c[ImGuiCol_SliderGrab] = ImVec4(0.93f, 0.94f, 0.95f, 1);
    c[ImGuiCol_SliderGrabActive] = ImGui::ColorConvertU32ToFloat4(kAccent);
    c[ImGuiCol_Header] = ImVec4(0.20f, 0.22f, 0.28f, 1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.24f, 0.26f, 0.33f, 1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.28f, 0.30f, 0.38f, 1);
    c[ImGuiCol_NavHighlight] = ImGui::ColorConvertU32ToFloat4(kAccent);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(IM_COL32(32, 36, 46, 255));
    c[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(44, 49, 62, 255));
    c[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(IM_COL32(56, 62, 78, 255));
    st.ScaleAllSizes(scale);
}

void LoadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    auto add = [&](const char* file, float size) -> ImFont* {
        std::string path = std::string("C:\\Windows\\Fonts\\") + file;
        if (fs::exists(path)) return io.Fonts->AddFontFromFileTTF(path.c_str(), size * scale);
        ImFontConfig cfg;
        cfg.SizePixels = size * scale;
        return io.Fonts->AddFontDefault(&cfg);
    };
    g_font = add("segoeui.ttf", 19);
    g_font_caps = add("seguisb.ttf", 15);
    g_font_big = add("segoeui.ttf", 26);
    g_font_title = add("seguibl.ttf", 54);
}

// the sidebar: title, pages, version; Home has none
void Sidebar(Ui& ui, float width, float height) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(o, ImVec2(o.x + width, o.y + height), kSidebar);
    float pad = ImGui::GetFontSize() * 2;
    ImGui::SetCursorPos(ImVec2(pad, pad));
    ImGui::PushFont(g_font_caps);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    if (ImGui::Button("<  B A C K")) ui.page = Page::Home;
    ImGui::PopStyleColor(2);
    ImGui::PopFont();
    ImGui::SetCursorPos(ImVec2(pad, pad * 2.4f));
    ImGui::PushFont(g_font_big);
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::TextUnformatted("INDIANA JONES");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorPosX(pad);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted("and the Emperor's Tomb");
    ImGui::PopStyleColor();
    ImGui::SetCursorPosY(pad * 5.5f);
    for (int i = 1; i <= 4; i++) {
        Page p = Page(i);
        bool sel = ui.page == p;
        ImGui::SetCursorPosX(pad);
        ImVec2 at = ImGui::GetCursorScreenPos();
        ImGui::PushFont(g_font_big);
        ImGui::PushStyleColor(ImGuiCol_Text, sel ? kText : kDim);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 12));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
        if (ImGui::Button(kPageNames[i], ImVec2(width - pad * 1.5f, 0))) ui.page = p;
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::PopFont();
        if (sel) d->AddLine(ImVec2(o.x + pad * 0.55f, at.y + 4), ImVec2(o.x + pad * 0.55f, ImGui::GetItemRectMax().y - 4),
                            kAccent, 3);
        ImGui::Dummy(ImVec2(0, 4));
    }
    ImGui::SetCursorPos(ImVec2(pad, height - pad * 1.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::Text("Version %s", kVersion);
    ImGui::PopStyleColor();
}

// controller and key hints along the bottom
void Footer(float x, float y, float w, float h) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + w, o.y + y + h), IM_COL32(14, 16, 21, 255));
    d->AddLine(ImVec2(o.x + x, o.y + y), ImVec2(o.x + x + w, o.y + y), kLine);
    float cx = o.x + x + ImGui::GetFontSize() * 2;
    float cy = o.y + y + h * 0.5f;
    ImGui::PushFont(g_font_caps);
    auto hint = [&](const char* key, const char* label) {
        ImVec2 ks = ImGui::CalcTextSize(key);
        float bw = ks.x + 14, bh = ks.y + 8;
        d->AddRect(ImVec2(cx, cy - bh * 0.5f), ImVec2(cx + bw, cy + bh * 0.5f), kDim, 3);
        d->AddText(ImVec2(cx + 7, cy - ks.y * 0.5f), kText, key);
        cx += bw + 10;
        d->AddText(ImVec2(cx, cy - ks.y * 0.5f), kDim, label);
        cx += ImGui::CalcTextSize(label).x + ImGui::GetFontSize() * 2.2f;
    };
    hint("A", "E N T E R");
    hint("B", "B A C K");
    hint("LB RB", "P A G E S");
    ImGui::PopFont();
}

ID3D11Device* g_dev = nullptr;
ID3D11DeviceContext* g_ctx = nullptr;
IDXGISwapChain* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
UINT g_resize_w = 0, g_resize_h = 0;

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
        case WM_GETMINMAXINFO: {
            UINT dpi = GetDpiForWindow(hwnd);
            if (!dpi) dpi = 96;
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {LONG(800 * dpi / 96), LONG(560 * dpi / 96)};
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
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);

    WNDCLASSEXW wc{sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst};
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"IndyLauncher";
    RegisterClassExW(&wc);
    float scale = float(GetDpiForSystem()) / 96.0f;
    int w = int(1100 * scale), h = int(720 * scale);
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    w = std::min<int>(w, wa.right - wa.left);
    h = std::min<int>(h, wa.bottom - wa.top);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Indiana Jones and the Emperor's Tomb", WS_OVERLAPPEDWINDOW,
                                wa.left + (wa.right - wa.left - w) / 2, wa.top + (wa.bottom - wa.top - h) / 2, w, h,
                                nullptr, nullptr, inst, nullptr);
    // the dark title bar (DWMWA_USE_IMMERSIVE_DARK_MODE)
    BOOL dark = TRUE;
    using DwmSet = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll"))
        if (auto set = reinterpret_cast<DwmSet>(GetProcAddress(dwm, "DwmSetWindowAttribute")))
            set(hwnd, 20, &dark, sizeof(dark));
    if (!CreateDevice(hwnd)) {
        MessageBoxW(hwnd, L"Direct3D 11 is not available.", L"Launcher", MB_ICONERROR);
        return 1;
    }
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
    // INDY_LAUNCHER_PAGE=1..4 opens a settings page at once (screenshots)
    if (const char* pg = std::getenv("INDY_LAUNCHER_PAGE"); pg && *pg >= '1' && *pg <= '4') ui.page = Page(*pg - '0');
    LoadModes(ui);
    bool quit = false;
    double saves_checked = 0;
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
        if (GetForegroundWindow() == hwnd && !g_game_running) io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        else io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // pages with LB / RB, back with B or Escape
        bool in_settings = ui.page != Page::Home;
        if (in_settings) {
            int p = int(ui.page);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) ui.page = Page(p > 1 ? p - 1 : 4);
            if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) ui.page = Page(p < 4 ? p + 1 : 1);
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) &&
                (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape)))
                ui.page = Page::Home;
        }
        if (ImGui::GetTime() > saves_checked + 2) {
            ui.saves = ListSaves();
            if (!g_add.running) ui.game = CheckGame();
            saves_checked = ImGui::GetTime();
        }

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);
        float footer_h = ImGui::GetFontSize() * 2.8f;
        float side_w = in_settings ? ImGui::GetFontSize() * 15 : 0;
        if (in_settings) Sidebar(ui, side_w, io.DisplaySize.y - footer_h);

        float pad = ImGui::GetFontSize() * 2.6f;
        ImGui::SetCursorPos(ImVec2(side_w + pad, pad * 0.6f));
        ImGui::BeginChild("page", ImVec2(io.DisplaySize.x - side_w - pad * 1.6f, io.DisplaySize.y - footer_h - pad * 0.6f),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::PushFont(g_font);
        if (int(ui.page) != ui.shown) {
            ui.shown = int(ui.page);
            ImGui::SetWindowFocus();
            ImGui::SetKeyboardFocusHere();
            // the focus shows at once, so the first A or Enter presses it instead of only showing it
            ImGui::GetCurrentContext()->NavDisableHighlight = false;
        }
        switch (ui.page) {
            case Page::Home: PageHome(ui); break;
            case Page::Graphics: PageGraphics(ui); break;
            case Page::Controls: PageControls(ui); break;
            case Page::Saves: PageSaves(ui); break;
            case Page::About: PageAbout(ui); break;
        }
        ImGui::Dummy(ImVec2(0, pad));
        ImGui::PopFont();
        ImGui::EndChild();

        Footer(0, io.DisplaySize.y - footer_h, io.DisplaySize.x, footer_h);
        if (!ui.message.empty() && ImGui::GetTime() < ui.message_until) {
            ImVec2 ts = ImGui::CalcTextSize(ui.message.c_str());
            ImVec2 o = ImGui::GetWindowPos();
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(o.x + io.DisplaySize.x - ts.x - pad, o.y + io.DisplaySize.y - footer_h * 0.5f - ts.y * 0.5f), kAccent,
                ui.message.c_str());
        }
        ImGui::End();

        // settings take effect when they change: the game reads the file at its start
        if (std::memcmp(&ui.s, &ui.saved, sizeof(Settings)) != 0) {
            SaveSettings(ui.s);
            ui.saved = ui.s;
        }

        ImGui::Render();
        const float clear[4] = {18 / 255.0f, 20 / 255.0f, 26 / 255.0f, 1};
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    if (g_rtv) g_rtv->Release();
    g_swap->Release();
    g_ctx->Release();
    g_dev->Release();
    DestroyWindow(hwnd);
    SDL_Quit();
    return 0;
}
