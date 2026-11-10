#pragma once
#include <array>
#include <filesystem>

// keyboard and mouse bindings as the Controls page keeps them: 16 buttons, then the two sticks' directions; a
// Windows virtual key, 0x100 / 0x101 the wheel, 0 none
using KeyIconBindings = std::array<std::array<int, 2>, 24>;

// draws the keyboard_icons texture pack into dir (key_icons.cpp)
bool MakeKeyIcons(const std::filesystem::path& dir, const KeyIconBindings& keys, bool mouse_look);
