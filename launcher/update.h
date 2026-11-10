#pragma once
#include <filesystem>
#include <string>

// Updates from the project's GitHub releases (update.cpp).
constexpr const char* kRepoPage = "https://github.com/MertCNOZ/IndyPort";
constexpr const char* kReleasesPage = "https://github.com/MertCNOZ/IndyPort/releases";

struct Release {
    bool ok = false;
    std::string error;    // when not ok
    std::string version;  // the tag without its "v"
    std::string notes;    // the release's description
    std::string page;     // its web page
    std::string zip;      // the IndyPort zip to download
};

// the latest release (blocking; the API address can be replaced with INDYPORT_UPDATE_URL for testing)
Release LatestRelease();

// whether version a ("1.2.3") is newer than b
bool NewerVersion(const std::string& a, const std::string& b);

// downloads the release's zip and puts its files over the port's own beside the launcher (the running launcher is
// renamed to .old first); the player's files (saves, settings, game files, own textures) are never touched
bool InstallRelease(const Release& r, const std::filesystem::path& port_dir, std::string& error);

// removes the .old files an update left behind
void RemoveOldFiles(const std::filesystem::path& port_dir);
