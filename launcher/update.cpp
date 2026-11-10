// Updates from GitHub releases. LatestRelease asks the GitHub API for the latest release of the project (tag, notes,
// the IndyPort zip among its assets); InstallRelease downloads that zip with WinHTTP, unpacks it with Windows' own
// tar.exe into update\new beside the launcher and copies its files over the port's. A file in use (the launcher
// itself, the game's dll) is renamed to .old first, which Windows allows for a running exe, and removed at the next
// start. The zip holds only the port's files (release.bat); paths that belong to the player are skipped anyway.
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "update.h"

namespace fs = std::filesystem;

namespace {

std::wstring Wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// a GET: the body into out (or into a file when file is set); follows redirects (GitHub sends downloads elsewhere)
bool HttpGet(const std::string& url, std::string* out, const fs::path* file, std::string& error) {
    std::wstring wurl = Wide(url);
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    wchar_t extra[1024] = {};
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 1024;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        error = "bad address";
        return false;
    }
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET session = WinHttpOpen(L"IndyPort-Launcher", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        error = "no internet access (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 15000, 30000);
    HINTERNET conn = WinHttpConnect(session, host, uc.nPort, 0);
    std::wstring object = std::wstring(path) + extra;
    HINTERNET req = conn ? WinHttpOpenRequest(conn, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                              WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)
                         : nullptr;
    bool ok = false;
    if (req) {
        const wchar_t* headers = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(req, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            DWORD status = 0, size = sizeof(status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status == 200) {
                std::ofstream f;
                if (file) f.open(*file, std::ios::binary | std::ios::trunc);
                ok = !file || bool(f);
                std::vector<char> buf(1 << 16);
                for (DWORD got = 0; ok && WinHttpReadData(req, buf.data(), DWORD(buf.size()), &got) && got; ) {
                    if (file) f.write(buf.data(), got);
                    else out->append(buf.data(), got);
                }
                if (file && !f) ok = false;
                if (!ok) error = "could not write the download";
            } else {
                error = status == 404 ? "no release found" : "the server answered " + std::to_string(status);
            }
        } else {
            error = "no connection (" + std::to_string(GetLastError()) + ")";
        }
        WinHttpCloseHandle(req);
    } else if (error.empty()) {
        error = "no connection (" + std::to_string(GetLastError()) + ")";
    }
    if (conn) WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return ok;
}

// the string value of "key" in the JSON text at or after from (enough for the API's answer, no full parser)
std::string JsonString(const std::string& json, const std::string& key, size_t from = 0, size_t* end = nullptr) {
    std::string pat = "\"" + key + "\"";
    size_t k = json.find(pat, from);
    while (k != std::string::npos) {
        size_t c = json.find_first_not_of(" \t\r\n", k + pat.size());
        if (c != std::string::npos && json[c] == ':') {
            size_t q = json.find_first_not_of(" \t\r\n", c + 1);
            if (q == std::string::npos || json[q] != '"') return {};
            std::string v;
            size_t i = q + 1;
            for (; i < json.size() && json[i] != '"'; i++) {
                if (json[i] != '\\' || i + 1 >= json.size()) {
                    v += json[i];
                    continue;
                }
                char e = json[++i];
                if (e == 'n') v += '\n';
                else if (e == 't') v += '\t';
                else if (e == 'r') continue;
                else if (e == 'u' && i + 4 < json.size()) {
                    unsigned cp = unsigned(std::strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp < 0x80) v += char(cp);
                    else if (cp < 0x800) v += char(0xC0 | cp >> 6), v += char(0x80 | (cp & 0x3F));
                    else v += char(0xE0 | cp >> 12), v += char(0x80 | (cp >> 6 & 0x3F)), v += char(0x80 | (cp & 0x3F));
                } else v += e;
            }
            if (end) *end = i;
            return v;
        }
        k = json.find(pat, k + 1);
    }
    return {};
}

std::vector<int> Parts(const std::string& v) {
    std::vector<int> p;
    for (size_t i = 0; i < v.size();) {
        if (!isdigit(static_cast<unsigned char>(v[i]))) {
            i++;
            continue;
        }
        p.push_back(std::atoi(v.c_str() + i));
        while (i < v.size() && isdigit(static_cast<unsigned char>(v[i]))) i++;
    }
    return p;
}

// a path that belongs to the player, never written by an update
bool PlayersOwn(const fs::path& rel) {
    std::string first = rel.begin() != rel.end() ? rel.begin()->string() : "";
    for (const char* p : {"assets", "hdd", "logs", "save backups", "shadercache", "update", "CrashDumps"})
        if (_stricmp(first.c_str(), p) == 0) return true;
    std::string s = rel.generic_string();
    return _stricmp(s.c_str(), "settings.toml") == 0 || _strnicmp(s.c_str(), "textures/replacement", 20) == 0 ||
           _strnicmp(s.c_str(), "textures/dumps", 14) == 0;
}

bool Unzip(const fs::path& zip, const fs::path& to, std::string& error) {
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    fs::path tar = fs::path(sys) / L"tar.exe";
    std::wstring cmd = L"\"" + tar.wstring() + L"\" -xf \"" + zip.wstring() + L"\" -C \"" + to.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(tar.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        error = "cannot run tar.exe (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (code != 0) error = "the zip could not be unpacked";
    return code == 0;
}

}  // namespace

Release LatestRelease() {
    Release r;
    const char* env = std::getenv("INDYPORT_UPDATE_URL");
    std::string api = env && *env ? env : "https://api.github.com/repos/MertCNOZ/IndyPort/releases/latest";
    std::string body;
    if (!HttpGet(api, &body, nullptr, r.error)) return r;
    std::string tag = JsonString(body, "tag_name");
    if (tag.empty()) {
        r.error = "the answer has no version";
        return r;
    }
    r.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
    r.page = JsonString(body, "html_url");
    r.notes = JsonString(body, "body");
    for (size_t at = 0;;) {
        size_t end = 0;
        std::string url = JsonString(body, "browser_download_url", at, &end);
        if (url.empty()) break;
        std::string name = url.substr(url.find_last_of('/') + 1);
        if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".zip") == 0 &&
            _strnicmp(name.c_str(), "IndyPort", 8) == 0) {
            r.zip = url;
            break;
        }
        at = end;
    }
    if (r.zip.empty()) {
        r.error = "the release has no IndyPort zip";
        return r;
    }
    r.ok = true;
    return r;
}

bool NewerVersion(const std::string& a, const std::string& b) {
    std::vector<int> x = Parts(a), y = Parts(b);
    for (size_t i = 0; i < std::max(x.size(), y.size()); i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p > q;
    }
    return false;
}

bool InstallRelease(const Release& r, const fs::path& port_dir, std::string& error) {
    std::error_code ec;
    fs::path work = port_dir / "update", zip = work / "IndyPort.zip", fresh = work / "new";
    fs::remove_all(work, ec);
    fs::create_directories(fresh, ec);
    if (!HttpGet(r.zip, nullptr, &zip, error)) return false;
    if (!Unzip(zip, fresh, error)) return false;
    if (!fs::exists(fresh / "IndyPort.exe")) {
        error = "the zip has no IndyPort.exe";
        return false;
    }
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(fresh, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(fs::relative(it->path(), fresh, ec));
    for (const fs::path& rel : files) {
        if (PlayersOwn(rel)) continue;
        fs::path dst = port_dir / rel, old = dst;
        old += ".old";
        fs::create_directories(dst.parent_path(), ec);
        bool moved = false;
        if (fs::exists(dst, ec)) {
            fs::remove(old, ec);
            fs::rename(dst, old, ec);
            if (ec) {
                error = "cannot replace " + rel.string() + " (in use?)";
                return false;
            }
            moved = true;
        }
        fs::copy_file(fresh / rel, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            if (moved) fs::rename(old, dst, ec);
            error = "cannot write " + rel.string();
            return false;
        }
    }
    fs::remove_all(work, ec);
    return true;
}

void RemoveOldFiles(const fs::path& port_dir) {
    std::error_code ec;
    for (const fs::path& dir : {port_dir, port_dir / "compiler", port_dir / "textures" / "packs"}) {
        if (!fs::exists(dir, ec)) continue;
        auto it = dir == port_dir ? fs::recursive_directory_iterator() : fs::recursive_directory_iterator(dir, ec);
        if (dir == port_dir) {
            for (auto& e : fs::directory_iterator(dir, ec))
                if (e.path().extension() == ".old") fs::remove(e.path(), ec);
            continue;
        }
        for (; it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->path().extension() == ".old") fs::remove(it->path(), ec);
    }
}
