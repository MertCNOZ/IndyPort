#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include "lang.h"

namespace {

struct Entry {
    const char* en;
    const char* tr;
    const char* ru;
};

const Entry kTable[] = {
#include "lang_table.inc"
};

Lang g_lang = Lang::English;

Lang SystemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_TURKISH: return Lang::Turkish;
        case LANG_RUSSIAN: return Lang::Russian;
        default: return Lang::English;
    }
}

const char* Lookup(const std::string& en) {
    static const std::unordered_map<std::string, const Entry*> index = [] {
        std::unordered_map<std::string, const Entry*> m;
        for (const Entry& e : kTable) m.emplace(e.en, &e);
        return m;
    }();
    auto it = index.find(en);
    if (it == index.end()) return nullptr;
    return g_lang == Lang::Turkish ? it->second->tr : it->second->ru;
}

}  // namespace

void SetLanguage(const std::string& setting) {
    g_lang = setting == "en"   ? Lang::English
             : setting == "tr" ? Lang::Turkish
             : setting == "ru" ? Lang::Russian
                               : SystemLanguage();
}

Lang CurrentLanguage() { return g_lang; }

const char* T(const char* english) {
    if (!english || g_lang == Lang::English) return english;
    const char* id = std::strstr(english, "##");
    if (!id) {
        const char* t = Lookup(english);
        return t ? t : english;
    }
    // "Text##id": the translated text with the same id, kept for the program's life (labels are few)
    static std::mutex lock;
    static std::unordered_map<std::string, std::string> labels[3];
    std::lock_guard l(lock);
    auto& cache = labels[int(g_lang)];
    auto it = cache.find(english);
    if (it != cache.end()) return it->second.c_str();
    const char* t = Lookup(std::string(english, id));
    std::string label = t ? std::string(t) + id : std::string(english);
    return cache.emplace(english, std::move(label)).first->second.c_str();
}
