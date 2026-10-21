#pragma once

#include "Decorator.hpp"
#include <string>
#include <vector>

// Offline decorator driven by styles learned from real levels (see StyleData.hpp).
// Decoration is placed around *hitboxes*: every selected object is sorted by its hitbox
// (full block, thin platform, slope, spike, saw, ...) and decorated with what real levels put there.
namespace styles {
    struct ChannelColor {
        int channel;
        cocos2d::ccColor3B color;
        bool blending;
    };

    struct Result {
        std::vector<DecoObject> objects;
        std::vector<ChannelColor> colors;  // color channels to set when applying
        std::string summary;               // what was used, for the status line
    };

    // A style is either one of the learned style groups, or the style of one single level.
    struct StyleRef {
        bool level = false;
        size_t index = 0;
    };

    size_t groupCount();
    size_t levelCount();
    std::string name(StyleRef ref);
    std::string description(StyleRef ref);   // e.g. "Learned from 42 levels"
    cocos2d::ccColor3B swatch(StyleRef ref); // a representative accent color

    // Hitbox class of an object: "block", "platform", "slope", "spike", "ground_spike", "saw", "orb", ... or "".
    std::string hitboxClass(SelectedObject const& o);

    // Decorate in one style.
    geode::Result<Result> generate(StyleRef ref, std::vector<SelectedObject> const& selection);

    // Decorate from a description: every level's decorations are merged, weighted by how well
    // each level matches the words (colors, "glow", "chains", "3d", a level's name...).
    geode::Result<Result> generateFromPrompt(std::string const& prompt, std::vector<SelectedObject> const& selection);

    // Sets the color channels in the open level.
    void applyColors(std::vector<ChannelColor> const& colors);
}
