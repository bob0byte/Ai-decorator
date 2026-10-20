#pragma once

#include "Decorator.hpp"
#include <array>

// Offline, rule-based decorator: no AI, no internet.
namespace styles {
    struct ChannelColor {
        int channel;
        cocos2d::ccColor3B color;
        bool blending;
    };

    struct Result {
        std::vector<DecoObject> objects;
        std::vector<ChannelColor> colors;  // color channels to set when applying
    };

    struct StyleInfo {
        char const* name;
        cocos2d::ccColor3B swatch;
    };

    inline constexpr std::array<StyleInfo, 6> STYLES = {{
        {"Neon",    {0, 230, 255}},
        {"Lava",    {255, 90, 30}},
        {"Ruins",   {150, 130, 100}},
        {"Ice",     {170, 225, 255}},
        {"Nature",  {90, 200, 90}},
        {"Minimal", {230, 230, 235}},
    }};

    // Builds a decoration for the selection in the given style. Different every call.
    geode::Result<Result> generate(size_t styleIndex, std::vector<SelectedObject> const& selection);

    // Sets the color channels in the open level.
    void applyColors(std::vector<ChannelColor> const& colors);
}
