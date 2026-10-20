#include "StyleDecorator.hpp"

#include <Geode/binding/ObjectToolbox.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

using namespace geode::prelude;

namespace {
    // ------------------------------------------------------------------
    // Object library: built from the game's own object table at runtime,
    // so every ID we use is guaranteed to exist and be non-solid decoration.
    // ------------------------------------------------------------------
    struct LibObject {
        int id;
        std::string frame;  // lowercase sprite frame name
        float w, h;
    };

    struct Library {
        std::vector<LibObject> deco;
        int square = 0;               // plain decoration square, used for outlines/fills
        float squareScale = 1.f;      // scale that makes the square one block (30 units) wide
        size_t scanned = 0;
        std::vector<int> glows;       // edge glow pieces
    };

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    bool hasAny(std::string const& s, std::vector<char const*> const& words) {
        for (auto w : words) if (s.find(w) != std::string::npos) return true;
        return false;
    }

    Library const& library() {
        static Library lib = [] {
            Library l;
            auto toolbox = ObjectToolbox::sharedState();
            for (int id = 1; id <= 4600; id++) {
                auto frameC = toolbox->intKeyToFrame(id);
                auto obj = GameObject::createWithKey(id);
                if (!obj) continue;
                l.scanned++;
                bool deco = obj->m_objectType == GameObjectType::Decoration || obj->m_isDecoration;
                if (!deco) continue;
                auto size = obj->getContentSize();
                if (size.width < 1 || size.height < 1) continue;
                l.deco.push_back({id, lower(frameC ? frameC : ""), size.width, size.height});
            }

            // A plain square: decoration, ~30x30, frame name looks like a block/square.
            // Square-shaped decoration; size units vary by platform/texture quality, so match on shape.
            auto isSquare = [](LibObject const& o) {
                return o.w >= 8 && std::abs(o.w - o.h) / o.w < .08f;
            };
            auto pick = [&](LibObject const& o) { l.square = o.id; l.squareScale = 30.f / o.w; };
            for (auto const& o : l.deco) {
                if (isSquare(o) && hasAny(o.frame, {"square", "block"}) && !hasAny(o.frame, {"glow", "outline", "spike"})) {
                    pick(o); break;
                }
            }
            if (!l.square) {
                for (auto const& o : l.deco) {
                    if (isSquare(o) && !hasAny(o.frame, {"glow", "spike", "saw"})) { pick(o); break; }
                }
            }

            // Glow pieces about one block wide.
            for (auto const& o : l.deco) {
                if (o.frame.find("glow") != std::string::npos && o.h <= o.w) {
                    l.glows.push_back(o.id);
                }
            }

            log::info("AI Decorator: object library has {} decoration objects, square={}, {} glow pieces",
                l.deco.size(), l.square, l.glows.size());
            return l;
        }();
        return lib;
    }

    std::vector<int> findDetails(std::vector<char const*> const& words) {
        std::vector<int> out;
        for (auto const& o : library().deco) {
            if (o.w > 6 && o.w <= 90 && o.h > 6 && o.h <= 90 && hasAny(o.frame, words) && !hasAny(o.frame, {"glow"})) {
                out.push_back(o.id);
            }
        }
        return out;
    }

    // ------------------------------------------------------------------
    // Style definitions
    // ------------------------------------------------------------------
    struct StyleDef {
        ccColor3B outline, glow, fill, backdrop, detail;
        bool fillOverlay;       // cover blocks with a flat colored square
        bool glowEdges;
        float outlineThickness; // fraction of a block
        float backdropDensity;  // 0..1, chance per cell in the margin area
        float detailChance;     // chance per exposed top edge
        std::vector<char const*> detailWords;
    };

    StyleDef styleDef(size_t i) {
        switch (i) {
            default:
            case 0: return {{0, 255, 255}, {0, 200, 255}, {10, 12, 30}, {20, 0, 60}, {255, 0, 200},
                true, true, .08f, .10f, .25f, {"light", "spark", "particle", "ring"}};
            case 1: return {{255, 200, 60}, {255, 80, 0}, {40, 10, 5}, {90, 15, 0}, {255, 140, 0},
                true, true, .10f, .12f, .35f, {"fire", "flame", "spike", "thorn"}};
            case 2: return {{90, 80, 65}, {0, 0, 0}, {120, 105, 85}, {45, 40, 35}, {70, 120, 50},
                false, false, .12f, .18f, .55f, {"rock", "stone", "brick", "crack", "vine", "grass", "chain"}};
            case 3: return {{220, 245, 255}, {140, 210, 255}, {60, 110, 160}, {15, 35, 70}, {255, 255, 255},
                true, true, .07f, .10f, .35f, {"ice", "crystal", "snow", "spike"}};
            case 4: return {{40, 90, 40}, {120, 255, 120}, {70, 50, 30}, {20, 50, 25}, {80, 200, 80},
                false, false, .10f, .14f, .65f, {"grass", "bush", "plant", "leaf", "vine", "flower", "tree"}};
            case 5: return {{255, 255, 255}, {200, 200, 220}, {30, 30, 36}, {16, 16, 20}, {255, 255, 255},
                true, false, .05f, .0f, .0f, {}};
        }
    }

    using Cell = std::pair<int, int>;

    Cell cellOf(float x, float y) {
        return {static_cast<int>(std::floor(x / 30.f)), static_cast<int>(std::floor(y / 30.f))};
    }
    float cx(int c) { return c * 30.f + 15.f; }
}

Result<styles::Result> styles::generate(size_t styleIndex, std::vector<SelectedObject> const& selection) {
    auto const& lib = library();
    if (!lib.square) {
        return Err(fmt::format("No usable deco square found (scanned {}, deco {}). Send this to the dev!",
            lib.scanned, lib.deco.size()));
    }
    float sq = lib.squareScale;

    auto mod = Mod::get();
    auto maxObjects = static_cast<size_t>(mod->getSettingValue<int64_t>("max-objects"));
    int ch = static_cast<int>(mod->getSettingValue<int64_t>("color-channel-start"));
    int chOutline = ch, chGlow = ch + 1, chFill = ch + 2, chBackdrop = ch + 3, chDetail = ch + 4;

    auto s = styleDef(styleIndex);
    std::mt19937 rng(std::random_device{}());
    auto chance = [&](float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; };
    auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

    // Occupied grid cells from the selected solid objects.
    std::set<Cell> solid;
    for (auto const& o : selection) if (o.solid) solid.insert(cellOf(o.x, o.y));
    if (solid.empty()) {
        // Nothing solid selected (e.g. only deco); treat everything as the shape.
        for (auto const& o : selection) solid.insert(cellOf(o.x, o.y));
    }

    int minCX = INT_MAX, minCY = INT_MAX, maxCX = INT_MIN, maxCY = INT_MIN;
    for (auto [x, y] : solid) {
        minCX = std::min(minCX, x); maxCX = std::max(maxCX, x);
        minCY = std::min(minCY, y); maxCY = std::max(maxCY, y);
    }

    styles::Result res;
    auto& out = res.objects;
    auto add = [&](DecoObject o) { if (out.size() < maxObjects) out.push_back(o); };

    auto has = [&](int x, int y) { return solid.count({x, y}) > 0; };
    float thick = s.outlineThickness;

    for (auto [x, y] : solid) {
        // Flat fill over each block.
        if (s.fillOverlay) {
            DecoObject f;
            f.id = lib.square; f.x = cx(x); f.y = cx(y); f.scale = sq;
            f.zLayer = 3; f.zOrder = -5; f.mainColor = chFill; f.detailColor = chFill;
            add(f);
        }

        // Outlines on exposed edges: thin stretched squares.
        struct Edge { int dx, dy; };
        for (auto e : {Edge{0, 1}, Edge{0, -1}, Edge{-1, 0}, Edge{1, 0}}) {
            if (has(x + e.dx, y + e.dy)) continue;
            DecoObject l;
            l.id = lib.square;
            l.x = cx(x) + e.dx * (15.f - 15.f * thick);
            l.y = cx(y) + e.dy * (15.f - 15.f * thick);
            l.scale = sq;
            l.scaleX = e.dy != 0 ? 1.f : thick;
            l.scaleY = e.dy != 0 ? thick : 1.f;
            l.zLayer = 5; l.zOrder = 2; l.mainColor = chOutline; l.detailColor = chOutline;
            add(l);

            // Glow fading outward from the edge.
            if (s.glowEdges && !lib.glows.empty()) {
                DecoObject g;
                g.id = lib.glows[0];
                g.x = cx(x) + e.dx * 30.f;
                g.y = cx(y) + e.dy * 30.f;
                // Glow pieces fade upward by default: rotate so they fade away from the block.
                g.rotation = e.dy == 1 ? 0.f : e.dy == -1 ? 180.f : e.dx == -1 ? 270.f : 90.f;
                g.zLayer = 1; g.mainColor = chGlow; g.detailColor = chGlow;
                add(g);
            }
        }
    }

    // Details sitting on top surfaces.
    auto details = findDetails(s.detailWords);
    if (!details.empty() && s.detailChance > 0) {
        for (auto [x, y] : solid) {
            if (has(x, y + 1) || !chance(s.detailChance)) continue;
            DecoObject d;
            d.id = details[std::uniform_int_distribution<size_t>(0, details.size() - 1)(rng)];
            d.x = cx(x) + rnd(-8, 8);
            d.y = cx(y) + 15.f + 10.f;
            d.scale = rnd(.5f, 1.f);
            d.flipX = chance(.5f);
            d.zLayer = 3; d.zOrder = 1; d.mainColor = chDetail; d.detailColor = chDetail;
            add(d);
        }
    }

    // Background pieces around the structure (never on top of it).
    if (s.backdropDensity > 0) {
        for (int x = minCX - 3; x <= maxCX + 3; x++) {
            for (int y = minCY - 2; y <= maxCY + 3; y++) {
                if (has(x, y) || !chance(s.backdropDensity)) continue;
                DecoObject b;
                b.id = lib.square;
                b.x = cx(x); b.y = cx(y);
                b.scale = sq * rnd(.6f, 2.2f);
                b.rotation = styleIndex == 2 ? rnd(-8, 8) : 0.f;  // ruins look a bit crooked
                b.zLayer = -3; b.zOrder = -10; b.mainColor = chBackdrop; b.detailColor = chBackdrop;
                add(b);
            }
        }
    }

    if (out.empty()) return Err("This style had nothing to add to the selection.");

    res.colors = {
        {chOutline, s.outline, false},
        {chGlow, s.glow, true},
        {chFill, s.fill, false},
        {chBackdrop, s.backdrop, false},
        {chDetail, s.detail, false},
    };
    return Ok(std::move(res));
}

void styles::applyColors(std::vector<ChannelColor> const& colors) {
    auto editor = LevelEditorLayer::get();
    if (!editor) return;

    std::set<GJEffectManager*> managers;
    if (editor->m_levelSettings && editor->m_levelSettings->m_effectManager) {
        managers.insert(editor->m_levelSettings->m_effectManager);
    }
    if (editor->m_effectManager) managers.insert(editor->m_effectManager);

    for (auto mgr : managers) {
        for (auto const& c : colors) {
            auto action = mgr->getColorAction(c.channel);
            if (!action) continue;
            action->m_fromColor = c.color;
            action->m_color = c.color;
            action->m_blending = c.blending;
            action->m_fromOpacity = 1.f;
            action->m_currentOpacity = 1.f;
            action->m_copyID = 0;
        }
    }
}
