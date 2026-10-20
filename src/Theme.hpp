#pragma once

#include <Geode/Geode.hpp>
#include <array>

struct Theme {
    char const* name;
    cocos2d::ccColor3B background;  // main window
    cocos2d::ccColor3B panel;       // preview area, input box
    cocos2d::ccColor3B accent;      // send button, highlights
    cocos2d::ccColor3B text;
    cocos2d::ccColor3B subtext;
};

inline constexpr std::array<Theme, 6> THEMES = {{
    {"Midnight", {18, 20, 28},   {30, 33, 45},   {108, 92, 231}, {240, 240, 250}, {150, 155, 175}},
    {"Neon",     {10, 10, 14},   {22, 22, 30},   {0, 230, 200},  {235, 255, 250}, {120, 170, 165}},
    {"Ocean",    {12, 30, 48},   {20, 46, 70},   {56, 170, 255}, {230, 244, 255}, {140, 175, 205}},
    {"Sunset",   {36, 18, 26},   {56, 28, 40},   {255, 120, 80}, {255, 238, 230}, {200, 150, 150}},
    {"Mint",     {16, 32, 26},   {26, 50, 40},   {80, 220, 140}, {232, 255, 242}, {140, 190, 165}},
    {"Light",    {236, 238, 244},{255, 255, 255},{70, 110, 240}, {30, 32, 40},    {110, 115, 130}},
}};

inline size_t currentThemeIndex() {
    auto i = geode::Mod::get()->getSavedValue<int64_t>("theme", 0);
    return (i < 0 || i >= static_cast<int64_t>(THEMES.size())) ? 0 : static_cast<size_t>(i);
}

inline Theme const& currentTheme() { return THEMES[currentThemeIndex()]; }

inline void setTheme(size_t i) { geode::Mod::get()->setSavedValue<int64_t>("theme", static_cast<int64_t>(i)); }
