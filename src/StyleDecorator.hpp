#pragma once

#include "Decorator.hpp"
#include <string>
#include <vector>

// Offline decorator driven by styles learned from real levels (see StyleData.hpp).
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

    // A style is either one of the learned style groups, or the style of one single level.
    struct StyleRef {
        bool level = false;
        size_t index = 0;
    };

    size_t groupCount();
    size_t levelCount();
    std::string name(StyleRef ref);
    std::string description(StyleRef ref);   // e.g. "42 levels" or "from one level"
    cocos2d::ccColor3B swatch(StyleRef ref); // a representative accent color

    // Builds a decoration for the selection in the given style. Different every call.
    geode::Result<Result> generate(StyleRef ref, std::vector<SelectedObject> const& selection);

    // Sets the color channels in the open level.
    void applyColors(std::vector<ChannelColor> const& colors);
}
