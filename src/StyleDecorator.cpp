#include "StyleDecorator.hpp"
#include "StyleData.hpp"

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
#include <unordered_map>

using namespace geode::prelude;

namespace {
    // ------------------------------------------------------------------ learned data

    // One decoration rule: "on this side of this kind of object, put this deco here".
    struct Item {
        int id;
        float share;           // how often this deco is the one used on this side
        float along, out;      // offset in the side's frame (out = away from the object's center)
        float rot;             // rotation relative to the side
        float sx, sy;
        float flipChance;
        int z, zo;
        int pal;               // palette index
    };
    struct Slot {
        std::string anchor, side;
        float rate;            // decorations per side
        std::vector<Item> items;
    };
    struct SceneryItem {
        int id; float share, sx, sy, rot; int z, pal; float dist;
    };
    struct Style {
        std::string name;
        int levels = 1;
        std::vector<std::array<int, 4>> pal;   // r, g, b, blending
        std::vector<Slot> slots;
        float sceneryRate = 0;
        std::vector<SceneryItem> scenery;
    };
    struct Data {
        std::vector<Style> groups, levels;
        std::unordered_map<int, std::string> category;  // gameplay object id -> category
        bool ok = false;
        std::string error;
    };

    float fnum(matjson::Value const& v) { return static_cast<float>(v.asDouble().unwrapOr(0.0)); }
    int inum(matjson::Value const& v) { return static_cast<int>(std::lround(v.asDouble().unwrapOr(0.0))); }

    Style parseStyle(matjson::Value const& s) {
        Style st;
        st.name = s["name"].asString().unwrapOr("Style");
        if (s.contains("from")) st.levels = static_cast<int>(s["from"].asArray().map([](auto const& a) { return a.size(); }).unwrapOr(1));
        for (auto const& p : s["pal"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            st.pal.push_back({inum(p[0]), inum(p[1]), inum(p[2]), inum(p[3])});
        }
        for (auto const& sl : s["slots"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            Slot slot;
            slot.anchor = sl["a"].asString().unwrapOr("");
            slot.side = sl["s"].asString().unwrapOr("");
            slot.rate = fnum(sl["rate"]);
            for (auto const& it : sl["items"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
                slot.items.push_back({inum(it[0]), fnum(it[1]), fnum(it[2]), fnum(it[3]), fnum(it[4]), fnum(it[5]),
                                      fnum(it[6]), fnum(it[7]), inum(it[8]), inum(it[9]), inum(it[10])});
            }
            st.slots.push_back(std::move(slot));
        }
        st.sceneryRate = fnum(s["scn"]["rate"]);
        for (auto const& it : s["scn"]["items"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
            st.scenery.push_back({inum(it[0]), fnum(it[1]), fnum(it[2]), fnum(it[3]), fnum(it[4]), inum(it[5]), inum(it[6]), fnum(it[7])});
        }
        return st;
    }

    Data const& data() {
        static Data d = [] {
            Data d;
            auto parsed = matjson::parse(STYLE_DATA_JSON);
            if (!parsed) {
                d.error = "Style data is corrupted: " + parsed.unwrapErr().message;
                return d;
            }
            auto root = parsed.unwrap();
            for (auto const& s : root["styles"].asArray().unwrapOr(std::vector<matjson::Value>{})) d.groups.push_back(parseStyle(s));
            for (auto const& s : root["levels"].asArray().unwrapOr(std::vector<matjson::Value>{})) d.levels.push_back(parseStyle(s));
            for (auto const& [cat, ids] : root["cats"]) {
                for (auto const& id : ids.asArray().unwrapOr(std::vector<matjson::Value>{})) d.category[inum(id)] = cat;
            }
            d.ok = !d.groups.empty();
            log::info("AI Decorator: loaded {} style groups and {} level styles", d.groups.size(), d.levels.size());
            return d;
        }();
        return d;
    }

    Style const* styleFor(styles::StyleRef ref) {
        auto const& d = data();
        auto const& list = ref.level ? d.levels : d.groups;
        return ref.index < list.size() ? &list[ref.index] : nullptr;
    }

    // Only use deco objects that exist in this game version and aren't hazards/solids.
    bool usable(int id) {
        static std::unordered_map<int, bool> cache;
        if (auto it = cache.find(id); it != cache.end()) return it->second;
        bool ok = false;
        if (auto obj = GameObject::createWithKey(id)) {
            ok = obj->m_objectType != GameObjectType::Hazard && obj->m_objectType != GameObjectType::Solid;
        }
        cache[id] = ok;
        return ok;
    }

    std::string categoryOf(SelectedObject const& o) {
        auto const& cats = data().category;
        if (auto it = cats.find(o.id); it != cats.end()) return it->second;
        // Newer objects not in the learned table: fall back to the game's own type.
        switch (static_cast<GameObjectType>(o.type)) {
            case GameObjectType::Solid: return "block";
            case GameObjectType::Hazard: return "spike";
            default: return "";
        }
    }

    using Cell = std::pair<int, int>;
    Cell cellOf(float x, float y) {
        return {static_cast<int>(std::floor(x / 30.f)), static_cast<int>(std::floor(y / 30.f))};
    }

    // Side frame: angle that turns the "top" frame onto this side (counter-clockwise, degrees).
    float sideAngle(std::string const& side) {
        if (side == "right") return 90.f;
        if (side == "bottom") return 180.f;
        if (side == "left") return 270.f;
        return 0.f;
    }
}

size_t styles::groupCount() { return data().groups.size(); }
size_t styles::levelCount() { return data().levels.size(); }

std::string styles::name(StyleRef ref) {
    auto s = styleFor(ref);
    return s ? s->name : "?";
}

std::string styles::description(StyleRef ref) {
    auto s = styleFor(ref);
    if (!s) return "";
    if (ref.level) return "Style of this level";
    return fmt::format("Learned from {} level{}", s->levels, s->levels == 1 ? "" : "s");
}

ccColor3B styles::swatch(StyleRef ref) {
    auto s = styleFor(ref);
    if (!s || s->pal.empty()) return {200, 200, 200};
    // the most colorful palette entry
    std::array<int, 4> best = s->pal[0];
    int bestScore = -1;
    for (auto const& p : s->pal) {
        int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
        int score = (mx - mn) * 2 + mx;
        if (score > bestScore) { bestScore = score; best = p; }
    }
    return {static_cast<GLubyte>(best[0]), static_cast<GLubyte>(best[1]), static_cast<GLubyte>(best[2])};
}

Result<styles::Result> styles::generate(StyleRef ref, std::vector<SelectedObject> const& selection) {
    auto const& d = data();
    if (!d.ok) return Err(d.error.empty() ? std::string("No style data.") : d.error);
    auto style = styleFor(ref);
    if (!style) return Err("Unknown style.");

    auto mod = Mod::get();
    auto maxObjects = static_cast<size_t>(mod->getSettingValue<int64_t>("max-objects"));
    int chStart = static_cast<int>(mod->getSettingValue<int64_t>("color-channel-start"));
    int maxLayers = static_cast<int>(mod->getSettingValue<int64_t>("layers-per-side"));
    float sceneryAmount = static_cast<float>(mod->getSettingValue<double>("scenery-amount"));

    std::mt19937 rng(std::random_device{}());
    auto chance = [&](float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; };
    auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

    // Gameplay objects in the selection, and which grid cells hold blocks.
    struct Anchor { SelectedObject const* o; std::string cat; };
    std::vector<Anchor> anchors;
    std::set<Cell> cells;
    for (auto const& o : selection) {
        auto cat = categoryOf(o);
        if (cat.empty()) continue;
        anchors.push_back({&o, cat});
        if (cat == "block" || cat == "platform" || cat == "slope") cells.insert(cellOf(o.x, o.y));
    }
    if (anchors.empty()) return Err("Select some gameplay objects (blocks, spikes, saws...) to decorate.");

    // For each slot, pick which decorations this structure uses (once, so the look is consistent).
    std::map<std::pair<std::string, std::string>, std::pair<float, std::vector<Item const*>>> picks;
    for (auto const& slot : style->slots) {
        std::vector<Item const*> pool;
        std::vector<float> weights;
        for (auto const& it : slot.items) {
            if (!usable(it.id)) continue;
            pool.push_back(&it); weights.push_back(it.share);
        }
        if (pool.empty()) continue;
        int layers = std::clamp(static_cast<int>(std::lround(slot.rate)), 1, std::max(1, maxLayers));
        std::vector<Item const*> chosen;
        for (int l = 0; l < layers && !pool.empty(); l++) {
            std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
            auto k = dist(rng);
            chosen.push_back(pool[k]);
            pool.erase(pool.begin() + k); weights.erase(weights.begin() + k);
        }
        picks[{slot.anchor, slot.side}] = {std::min(slot.rate, 1.f), std::move(chosen)};
    }

    styles::Result res;
    auto& out = res.objects;
    std::set<std::tuple<int, int, int, int>> placed;  // de-duplicate identical placements
    auto place = [&](DecoObject o) {
        if (out.size() >= maxObjects) return;
        auto key = std::make_tuple(o.id, static_cast<int>(std::lround(o.x)), static_cast<int>(std::lround(o.y)),
                                   static_cast<int>(std::lround(o.rotation)) % 360);
        if (!placed.insert(key).second) return;
        out.push_back(o);
    };
    auto channelFor = [&](int pal) { return pal >= 0 && pal < static_cast<int>(style->pal.size()) ? chStart + pal : 0; };

    for (auto const& a : anchors) {
        auto const& o = *a.o;
        bool gridded = a.cat == "block" || a.cat == "platform" || a.cat == "slope";
        auto [cx, cy] = cellOf(o.x, o.y);
        int quarter = ((static_cast<int>(std::lround(o.rotation / 90.f)) % 4) + 4) % 4;

        for (std::string side : {"center", "top", "bottom", "left", "right"}) {
            auto pk = picks.find({a.cat, side});
            if (pk == picks.end()) continue;

            // Skip faces that touch another block (decorators leave those alone).
            if (gridded && side != "center") {
                int dx = side == "right" ? 1 : side == "left" ? -1 : 0;
                int dy = side == "top" ? 1 : side == "bottom" ? -1 : 0;
                int wdx = dx, wdy = dy;  // rotate the side direction into the world (clockwise quarters)
                for (int q = 0; q < quarter; q++) { int t = wdx; wdx = wdy; wdy = -t; }
                if (cells.count({cx + wdx, cy + wdy})) continue;
            }
            if (!chance(pk->second.first)) continue;

            float alpha = sideAngle(side) * static_cast<float>(M_PI) / 180.f;
            float th = o.rotation * static_cast<float>(M_PI) / 180.f;
            for (auto it : pk->second.second) {
                // side frame -> object frame
                float lx = it->along * std::cos(alpha) + it->out * std::sin(alpha);
                float ly = -it->along * std::sin(alpha) + it->out * std::cos(alpha);
                if (o.flipX) lx = -lx;
                if (o.flipY) ly = -ly;
                // object frame -> world (GD rotation is clockwise)
                DecoObject deco;
                deco.id = it->id;
                deco.x = o.x + lx * std::cos(th) + ly * std::sin(th);
                deco.y = o.y - lx * std::sin(th) + ly * std::cos(th);
                deco.rotation = std::fmod(it->rot + o.rotation + sideAngle(side) + 720.f, 360.f);
                deco.scaleX = it->sx; deco.scaleY = it->sy;
                deco.flipX = chance(it->flipChance);
                deco.zLayer = it->z; deco.zOrder = it->zo;
                deco.mainColor = channelFor(it->pal);
                deco.detailColor = deco.mainColor;
                place(deco);
            }
        }
    }

    // Background scenery around (not on) the structure.
    if (!style->scenery.empty() && sceneryAmount > 0) {
        float minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
        for (auto const& a : anchors) {
            minX = std::min(minX, a.o->x); maxX = std::max(maxX, a.o->x);
            minY = std::min(minY, a.o->y); maxY = std::max(maxY, a.o->y);
        }
        std::vector<SceneryItem const*> pool; std::vector<float> w;
        for (auto const& s : style->scenery) if (usable(s.id)) { pool.push_back(&s); w.push_back(s.share); }
        if (!pool.empty()) {
            std::discrete_distribution<size_t> dist(w.begin(), w.end());
            int count = std::min(static_cast<int>(anchors.size() * std::min(style->sceneryRate, 3.f) * sceneryAmount), 400);
            for (int i = 0; i < count; i++) {
                auto s = pool[dist(rng)];
                float r = std::clamp(s->dist, 45.f, 240.f) * rnd(.7f, 1.3f);
                float x = rnd(minX - 60, maxX + 60);
                float y = chance(.5f) ? maxY + r : minY - r;
                if (cells.count(cellOf(x, y))) continue;
                DecoObject deco;
                deco.id = s->id; deco.x = x; deco.y = y;
                deco.rotation = s->rot; deco.scaleX = s->sx; deco.scaleY = s->sy;
                deco.zLayer = s->z; deco.mainColor = channelFor(s->pal); deco.detailColor = deco.mainColor;
                place(deco);
            }
        }
    }

    if (out.empty()) return Err("This style has no rules for the objects you selected.");

    for (size_t i = 0; i < style->pal.size(); i++) {
        auto const& p = style->pal[i];
        res.colors.push_back({chStart + static_cast<int>(i),
            {static_cast<GLubyte>(p[0]), static_cast<GLubyte>(p[1]), static_cast<GLubyte>(p[2])}, p[3] != 0});
    }
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
