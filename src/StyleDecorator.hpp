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
        float opacity = 1.f;
    };

    // What to place and how it looks. Saved between sessions; edited in the Options tab.
    struct Options {
        bool body = true, glow = true, lines = true, hazardGlow = true, particles = true, scenery = false, pulses = true, colorShift = false;
        float glowScale = 1.f;      // size of glow pieces
        float glowOpacity = 1.f;    // opacity of the glow channel
        float bodyOpacity = 1.f;    // opacity of the body channel
        int hue = 0;                // hue shift in degrees
        float brightness = 1.f;     // multiplies every style color
        int pulseBlocks = 0;        // pulse every N blocks (0 = like the source level)
        int colorMode = 0;          // 0 style colors, 1 keep my channel colors, 2 monochrome
        bool copyReal = true;       // copy the source level's real decoration around each hitbox (best quality)
        bool useAI = true;          // use the trained network to choose pieces (else the rule-based picks)
        float aiConfidence = .08f;   // how sure the network must be before placing a piece
    };
    Options& options();
    void saveOptions();

    // A color change on the glow during the structure (Color trigger), like levels that shift their palette.
    struct ColorShiftFx {
        int channel = 0;
        cocos2d::ccColor3B to{255, 255, 255}, back{255, 255, 255};
        float duration = .5f;
        bool enabled = false;
    };

    // Beat pulses on the glow color, timed like the source level.
    struct PulseFx {
        int channel = 0;
        cocos2d::ccColor3B color{255, 255, 255};
        float gap = 123.f, fadeIn = 0.f, hold = 0.f, fadeOut = .3f;
    };

    struct Result {
        std::vector<DecoObject> objects;     // sorted back-to-front (layer, then order, then x)
        std::vector<ChannelColor> colors;    // color channels to set when applying
        std::string summary;                 // what was used, for the status line
        std::string source;                  // which level the decoration came from
        PulseFx pulse;
        ColorShiftFx shift;
    };

    // A style is either a community deco type (Glow, Modern, Nine Circles...) or one single level.
    struct StyleRef {
        bool level = false;
        size_t index = 0;
    };

    size_t typeCount();
    inline size_t groupCount() { return typeCount(); }
    size_t levelCount();
    std::string name(StyleRef ref);
    std::string description(StyleRef ref);   // e.g. "25 levels" or the level's deco type
    cocos2d::ccColor3B swatch(StyleRef ref); // a representative accent color

    // Hitbox class of an object: "block", "platform", "slope", "spike", "ground_spike", "saw", "orb", ... or "".
    std::string hitboxClass(SelectedObject const& o);

    // Decorate in one style. `variant` picks the next level of the type / next alternative each time.
    geode::Result<Result> generate(StyleRef ref, std::vector<SelectedObject> const& selection, int variant);

    // Decorate from a description: all levels are ranked by how well they match the words
    // (deco type, colors, "chains", "3d", a level's name...); `variant` walks down the best matches.
    geode::Result<Result> generateFromPrompt(std::string const& prompt, std::vector<SelectedObject> const& selection, int variant);

    // Sets the color channels in the open level.
    void applyColors(std::vector<ChannelColor> const& colors);
}
