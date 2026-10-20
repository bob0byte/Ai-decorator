#pragma once

#include <Geode/Geode.hpp>
#include <array>
#include <string>

// Trigger presets. Settings are the most common ones found in the analyzed level pack
// (see results/07_triggers.md): e.g. Pulse hold 0.1s / fade out 0.3s, Move 30 units over 0.5s
// with Ease In Out, Scale 1.3x with Elastic Out, Shake strength 2 for 0.2s.
namespace triggers {
    struct Preset {
        char const* name;
        char const* info;   // short description shown on the button
    };

    inline constexpr std::array<Preset, 9> PRESETS = {{
        {"Pulse", "Flash white: hold 0.1s, fade 0.3s"},
        {"Fade In", "Hidden, then fades in over 0.5s"},
        {"Fade Out", "Fades to invisible over 0.5s"},
        {"Slide Up", "Moves up 1 block, 0.5s ease"},
        {"Slide Down", "Moves down 1 block, 0.5s ease"},
        {"Spin", "Rotates 90 degrees over 1s"},
        {"Pop", "Scales 1.3x (elastic) and back"},
        {"Shake", "Screen shake, strength 2, 0.2s"},
        {"Hide", "Toggles the structure off"},
    }};

    // Adds the preset's trigger(s) for the selected objects. Puts the selection in a group if
    // `group` is 0 (and returns the group used), otherwise reuses `group`.
    geode::Result<int> add(size_t preset, cocos2d::CCArray* selection, int group);
}
