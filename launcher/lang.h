#pragma once
#include <string>

// The launcher's languages. Texts are written in English in the source and looked up by that text in
// lang_table.inc (tools/lang_table.py writes it); a text with no entry shows in English.
enum class Lang { English, Turkish, Russian };

// "auto" follows Windows' display language; "en", "tr", "ru" choose one
void SetLanguage(const std::string& setting);
Lang CurrentLanguage();

// the text in the current language; for an ImGui label "Text##id" the text part, the id kept
const char* T(const char* english);
