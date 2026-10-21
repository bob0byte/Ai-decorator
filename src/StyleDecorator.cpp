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
#include <optional>
#include <random>
#include <set>
#include <unordered_map>

using namespace geode::prelude;

namespace {
    // ------------------------------------------------------------------ learned data

    // One decoration rule: "on this side of this kind of hitbox, put this deco here".
    struct Item {
        int id;
        float share;           // how often this deco is the one used on this side
        float along, out;      // offset in the side's frame (out = away from the hitbox center)
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
        std::unordered_map<std::string, float> tags;
    };
    struct Data {
        std::vector<Style> groups, levels;
        std::unordered_map<int, std::string> category;  // known gameplay object id -> hitbox class
        bool ok = false;
        std::string error;
    };

    float fnum(matjson::Value const& v) { return static_cast<float>(v.asDouble().unwrapOr(0.0)); }
    int inum(matjson::Value const& v) { return static_cast<int>(std::lround(v.asDouble().unwrapOr(0.0))); }
    std::vector<matjson::Value> arr(matjson::Value const& v) { return v.asArray().unwrapOr(std::vector<matjson::Value>{}); }

    Style parseStyle(matjson::Value const& s) {
        Style st;
        st.name = s["name"].asString().unwrapOr("Style");
        if (s.contains("from")) st.levels = static_cast<int>(arr(s["from"]).size());
        for (auto const& p : arr(s["pal"])) st.pal.push_back({inum(p[0]), inum(p[1]), inum(p[2]), inum(p[3])});
        for (auto const& sl : arr(s["slots"])) {
            Slot slot;
            slot.anchor = sl["a"].asString().unwrapOr("");
            slot.side = sl["s"].asString().unwrapOr("");
            slot.rate = fnum(sl["rate"]);
            for (auto const& it : arr(sl["items"])) {
                slot.items.push_back({inum(it[0]), fnum(it[1]), fnum(it[2]), fnum(it[3]), fnum(it[4]), fnum(it[5]),
                                      fnum(it[6]), fnum(it[7]), inum(it[8]), inum(it[9]), inum(it[10])});
            }
            st.slots.push_back(std::move(slot));
        }
        st.sceneryRate = fnum(s["scn"]["rate"]);
        for (auto const& it : arr(s["scn"]["items"])) {
            st.scenery.push_back({inum(it[0]), fnum(it[1]), fnum(it[2]), fnum(it[3]), fnum(it[4]), inum(it[5]), inum(it[6]), fnum(it[7])});
        }
        if (s.contains("tags")) {
            for (auto const& [k, v] : s["tags"]) st.tags[k] = fnum(v);
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
            auto const root = parsed.unwrap();
            for (auto const& s : arr(root["styles"])) d.groups.push_back(parseStyle(s));
            for (auto const& s : arr(root["levels"])) d.levels.push_back(parseStyle(s));
            for (auto const& [cat, ids] : root["cats"]) {
                for (auto const& id : arr(ids)) d.category[inum(id)] = cat;
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

    // Only skip decorations that don't exist in this game version.
    // (Objects made with createWithKey all report type Solid until placed, so the type can't be checked here.)
    bool usable(int id) {
        static std::unordered_map<int, bool> cache;
        if (auto it = cache.find(id); it != cache.end()) return it->second;
        bool ok = GameObject::createWithKey(id) != nullptr;
        cache[id] = ok;
        return ok;
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

    // If a style has no rule for a hitbox class, borrow the closest one.
    std::vector<std::string> fallbacks(std::string const& cls) {
        if (cls == "ground_spike") return {"ground_spike", "spike"};
        if (cls == "spike_slope") return {"spike_slope", "slope", "spike"};
        if (cls == "platform") return {"platform", "block"};
        if (cls == "slope") return {"slope", "block"};
        if (cls == "saw") return {"saw", "orb"};
        if (cls == "orb" || cls == "pad" || cls == "portal" || cls == "speed" || cls == "collectible") return {cls, "saw"};
        return {cls, "block"};
    }

    Result<styles::Result> generateWith(Style const& style, std::vector<SelectedObject> const& selection) {
        auto mod = Mod::get();
        auto maxObjects = static_cast<size_t>(mod->getSettingValue<int64_t>("max-objects"));
        int chStart = static_cast<int>(mod->getSettingValue<int64_t>("color-channel-start"));
        int maxLayers = static_cast<int>(mod->getSettingValue<int64_t>("layers-per-side"));
        float sceneryAmount = static_cast<float>(mod->getSettingValue<double>("scenery-amount"));

        std::mt19937 rng(std::random_device{}());
        auto chance = [&](float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; };
        auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };

        // Sort the selection by hitbox.
        struct Anchor { SelectedObject const* o; std::string cls; };
        std::vector<Anchor> anchors;
        std::set<Cell> cells;
        for (auto const& o : selection) {
            auto cls = styles::hitboxClass(o);
            if (cls.empty()) continue;
            anchors.push_back({&o, cls});
            if (cls == "block" || cls == "platform" || cls == "slope") cells.insert(cellOf(o.x, o.y));
        }
        // Nothing with a hitbox selected: treat every selected object as a block so something happens.
        if (anchors.empty()) {
            for (auto const& o : selection) {
                anchors.push_back({&o, "block"});
                cells.insert(cellOf(o.x, o.y));
            }
        }

        // Slots by (class, side).
        std::map<std::pair<std::string, std::string>, Slot const*> slotMap;
        for (auto const& sl : style.slots) slotMap[{sl.anchor, sl.side}] = &sl;

        // For each slot, pick which decorations this structure uses (once, so the look is consistent).
        std::map<std::pair<std::string, std::string>, std::pair<float, std::vector<Item const*>>> picks;
        auto pickFor = [&](std::string const& cls, std::string const& side) -> std::pair<float, std::vector<Item const*>> const* {
            auto key = std::make_pair(cls, side);
            if (auto it = picks.find(key); it != picks.end()) return &it->second;
            Slot const* slot = nullptr;
            for (auto const& f : fallbacks(cls)) {
                if (auto it = slotMap.find({f, side}); it != slotMap.end()) { slot = it->second; break; }
            }
            std::pair<float, std::vector<Item const*>> pk{0.f, {}};
            if (slot) {
                std::vector<Item const*> pool; std::vector<float> weights;
                for (auto const& it : slot->items) {
                    if (!usable(it.id)) continue;
                    pool.push_back(&it); weights.push_back(std::max(it.share, 0.0001f));
                }
                int layers = std::clamp(static_cast<int>(std::lround(slot->rate)), 1, std::max(1, maxLayers));
                for (int l = 0; l < layers && !pool.empty(); l++) {
                    std::discrete_distribution<size_t> dist(weights.begin(), weights.end());
                    auto k = dist(rng);
                    pk.second.push_back(pool[k]);
                    pool.erase(pool.begin() + k); weights.erase(weights.begin() + k);
                }
                pk.first = std::min(slot->rate, 1.f);
            }
            return &(picks[key] = std::move(pk));
        };

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
        auto channelFor = [&](int pal) { return pal >= 0 && pal < static_cast<int>(style.pal.size()) ? chStart + pal : 0; };

        std::map<std::string, int> classCounts;
        for (auto const& a : anchors) {
            auto const& o = *a.o;
            classCounts[a.cls]++;
            bool gridded = a.cls == "block" || a.cls == "platform" || a.cls == "slope";
            auto [cx, cy] = cellOf(o.x, o.y);
            int quarter = ((static_cast<int>(std::lround(o.rotation / 90.f)) % 4) + 4) % 4;
            // Bigger/smaller hitboxes than a normal block: scale offsets with them.
            float sizeK = gridded && o.hbW > 1 && o.hbH > 1 ? std::clamp(std::max(o.hbW, o.hbH) / 30.f, .5f, 4.f) : 1.f;

            for (std::string side : {"center", "top", "bottom", "left", "right"}) {
                auto pk = pickFor(a.cls, side);
                if (!pk || pk->second.empty()) continue;

                // Skip faces that touch another hitbox (decorators leave those alone).
                if (gridded && side != "center") {
                    int dx = side == "right" ? 1 : side == "left" ? -1 : 0;
                    int dy = side == "top" ? 1 : side == "bottom" ? -1 : 0;
                    int wdx = dx, wdy = dy;  // rotate the side direction into the world (clockwise quarters)
                    for (int q = 0; q < quarter; q++) { int t = wdx; wdx = wdy; wdy = -t; }
                    if (cells.count({cx + wdx, cy + wdy})) continue;
                }
                if (!chance(pk->first)) continue;

                float alpha = sideAngle(side) * static_cast<float>(M_PI) / 180.f;
                float th = o.rotation * static_cast<float>(M_PI) / 180.f;
                for (auto it : pk->second) {
                    float along = it->along * sizeK, outD = it->out * sizeK;
                    // side frame -> object frame
                    float lx = along * std::cos(alpha) + outD * std::sin(alpha);
                    float ly = -along * std::sin(alpha) + outD * std::cos(alpha);
                    if (o.flipX) lx = -lx;
                    if (o.flipY) ly = -ly;
                    // object frame -> world (GD rotation is clockwise)
                    DecoObject deco;
                    deco.id = it->id;
                    deco.x = o.x + lx * std::cos(th) + ly * std::sin(th);
                    deco.y = o.y - lx * std::sin(th) + ly * std::cos(th);
                    deco.rotation = std::fmod(it->rot + o.rotation + sideAngle(side) + 720.f, 360.f);
                    deco.scaleX = it->sx * sizeK; deco.scaleY = it->sy * sizeK;
                    deco.flipX = chance(it->flipChance);
                    deco.zLayer = it->z; deco.zOrder = it->zo;
                    deco.mainColor = channelFor(it->pal);
                    deco.detailColor = deco.mainColor;
                    place(deco);
                }
            }
        }

        // Background scenery around (not on) the structure.
        if (!style.scenery.empty() && sceneryAmount > 0) {
            float minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
            for (auto const& a : anchors) {
                minX = std::min(minX, a.o->x); maxX = std::max(maxX, a.o->x);
                minY = std::min(minY, a.o->y); maxY = std::max(maxY, a.o->y);
            }
            std::vector<SceneryItem const*> pool; std::vector<float> w;
            for (auto const& s : style.scenery) if (usable(s.id)) { pool.push_back(&s); w.push_back(std::max(s.share, 0.0001f)); }
            if (!pool.empty()) {
                std::discrete_distribution<size_t> dist(w.begin(), w.end());
                int count = std::min(static_cast<int>(anchors.size() * std::min(style.sceneryRate, 3.f) * sceneryAmount), 400);
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

        if (out.empty()) {
            std::string found;
            for (auto const& [c, n] : classCounts) found += fmt::format("{}{} {}", found.empty() ? "" : ", ", n, c);
            return Err(fmt::format("Nothing to place for: {}. Try another style.", found));
        }

        for (size_t i = 0; i < style.pal.size(); i++) {
            auto const& p = style.pal[i];
            res.colors.push_back({chStart + static_cast<int>(i),
                {static_cast<GLubyte>(p[0]), static_cast<GLubyte>(p[1]), static_cast<GLubyte>(p[2])}, p[3] != 0});
        }
        std::string found;
        for (auto const& [c, n] : classCounts) {
            auto nice = c; std::replace(nice.begin(), nice.end(), '_', ' ');
            found += fmt::format("{}{} {}{}", found.empty() ? "" : ", ", n, nice, n == 1 ? "" : "s");
        }
        res.summary = found;
        return Ok(std::move(res));
    }

    // ------------------------------------------------------------------ prompt matching

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Everyday words -> the tags the styles were labelled with.
    std::unordered_map<std::string, std::vector<std::string>> const& synonyms() {
        static std::unordered_map<std::string, std::vector<std::string>> m = {
            {"fire", {"red", "orange", "glow"}}, {"lava", {"red", "orange", "glow"}}, {"hell", {"red", "dark", "spikes"}},
            {"blood", {"red", "dark"}}, {"ice", {"cyan", "azure", "white"}}, {"frozen", {"cyan", "azure", "white"}},
            {"snow", {"white", "azure"}}, {"winter", {"white", "azure"}}, {"ocean", {"blue", "cyan", "teal"}},
            {"water", {"blue", "cyan"}}, {"sea", {"blue", "teal"}}, {"nature", {"green", "lime"}}, {"forest", {"green"}},
            {"jungle", {"green", "lime"}}, {"toxic", {"lime", "green", "glow"}}, {"acid", {"lime", "glow"}},
            {"space", {"dark", "blue", "purple", "particles"}}, {"galaxy", {"purple", "blue", "particles"}},
            {"night", {"dark", "blue"}}, {"cosmic", {"purple", "particles"}}, {"gold", {"yellow", "orange"}},
            {"golden", {"yellow", "orange"}}, {"sun", {"yellow", "orange", "bright"}}, {"candy", {"pink", "bright"}},
            {"cute", {"pink", "bright"}}, {"love", {"pink", "red"}}, {"spooky", {"dark", "purple", "orange"}},
            {"halloween", {"orange", "purple", "dark"}}, {"horror", {"dark", "red"}}, {"scary", {"dark", "red"}},
            {"cyber", {"cyan", "glow", "grid"}}, {"tech", {"grid", "cyan", "lines"}}, {"futuristic", {"glow", "grid", "cyan"}},
            {"retro", {"pixel"}}, {"8bit", {"pixel"}}, {"glowing", {"glow"}}, {"glowy", {"glow"}}, {"shiny", {"glow", "bright"}},
            {"chain", {"chains"}}, {"clean", {"minimal"}}, {"simple", {"minimal"}}, {"busy", {"dense"}}, {"detailed", {"dense"}},
            {"crazy", {"dense", "particles"}}, {"particle", {"particles"}}, {"ring", {"rings"}}, {"orb", {"orbs"}},
            {"3d", {"3d"}}, {"depth", {"3d"}}, {"perspective", {"3d"}}, {"outline", {"outlines"}}, {"line", {"lines"}},
            {"purple", {"purple", "magenta"}}, {"violet", {"purple"}}, {"pink", {"pink", "magenta"}}, {"grey", {"mono"}},
            {"gray", {"mono"}}, {"black", {"dark", "black"}}, {"light", {"bright"}}, {"dark", {"dark"}},
        };
        return m;
    }

    // Color words recognized in a prompt, as RGB.
    std::optional<ccColor3B> colorWord(std::string const& w) {
        static std::unordered_map<std::string, ccColor3B> m = {
            {"red", {255, 40, 40}}, {"orange", {255, 140, 20}}, {"yellow", {255, 230, 40}}, {"gold", {255, 190, 40}},
            {"golden", {255, 190, 40}}, {"lime", {150, 255, 40}}, {"green", {40, 230, 90}}, {"teal", {20, 210, 180}},
            {"cyan", {40, 230, 255}}, {"aqua", {40, 230, 255}}, {"azure", {60, 160, 255}}, {"blue", {50, 90, 255}},
            {"purple", {160, 60, 255}}, {"violet", {160, 60, 255}}, {"magenta", {255, 40, 220}}, {"pink", {255, 100, 190}},
            {"white", {245, 245, 255}}, {"fire", {255, 110, 20}}, {"lava", {255, 80, 20}}, {"ice", {150, 230, 255}},
            {"toxic", {140, 255, 40}}, {"blood", {200, 0, 20}},
        };
        if (auto it = m.find(w); it != m.end()) return it->second;
        return std::nullopt;
    }

    std::vector<std::string> words(std::string const& prompt) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : lower(prompt) + " ") {
            if (std::isalnum(static_cast<unsigned char>(c))) cur += c;
            else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
        return out;
    }

    float score(Style const& s, std::vector<std::string> const& ws, std::string const& prompt) {
        float sc = 0;
        for (auto const& w : ws) {
            if (auto it = s.tags.find(w); it != s.tags.end()) sc += it->second;
            if (auto syn = synonyms().find(w); syn != synonyms().end()) {
                for (auto const& t : syn->second) if (auto it = s.tags.find(t); it != s.tags.end()) sc += it->second * .7f;
            }
        }
        // a level name typed in full counts a lot
        auto nm = lower(s.name);
        if (nm.size() > 2 && prompt.find(nm) != std::string::npos) sc += 20.f;
        return sc;
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
    std::array<int, 4> best = s->pal[0];
    int bestScore = -1;
    for (auto const& p : s->pal) {
        int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
        int score = (mx - mn) * 2 + mx;
        if (score > bestScore) { bestScore = score; best = p; }
    }
    return {static_cast<GLubyte>(best[0]), static_cast<GLubyte>(best[1]), static_cast<GLubyte>(best[2])};
}

std::string styles::hitboxClass(SelectedObject const& o) {
    auto type = static_cast<GameObjectType>(o.type);
    // Slopes, hazards and solids come straight from the object's hitbox type.
    if (type == GameObjectType::Slope) return "slope";
    if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
        if (o.radius > 0.5f) return "saw";
        float h = std::min(o.hbW, o.hbH);
        return (h > 0 && h <= 14.f) ? "ground_spike" : "spike";
    }
    if (type == GameObjectType::Solid || type == GameObjectType::Breakable) {
        auto const& cats = data().category;
        auto it = cats.find(o.id);
        // Solid type is also the default for plain decoration, so only trust it with a real hitbox.
        if (it == cats.end() && (o.hbW < 1 || o.hbH < 1)) return "";
        // Every gameplay object up to 2.1 (id 1911) is in the learned table, so older ids missing from it are decoration.
        if (it == cats.end() && o.id <= 1911) return "";
        if (it != cats.end() && it->second != "block" && it->second != "platform") return it->second;
        float mn = std::min(o.hbW, o.hbH), mx = std::max(o.hbW, o.hbH);
        if (mx > 0 && mn < mx * .6f) return "platform";
        return "block";
    }
    // Orbs, pads, portals...: use the learned table.
    auto const& cats = data().category;
    if (auto it = cats.find(o.id); it != cats.end()) return it->second;
    return "";
}

Result<styles::Result> styles::generate(StyleRef ref, std::vector<SelectedObject> const& selection) {
    auto const& d = data();
    if (!d.ok) return Err(d.error.empty() ? std::string("No style data.") : d.error);
    auto style = styleFor(ref);
    if (!style) return Err("Unknown style.");
    return generateWith(*style, selection);
}

Result<styles::Result> styles::generateFromPrompt(std::string const& prompt, std::vector<SelectedObject> const& selection) {
    auto const& d = data();
    if (!d.ok) return Err(d.error.empty() ? std::string("No style data.") : d.error);
    auto ws = words(prompt);
    auto lp = lower(prompt);

    // Weight every level by how well it matches. Every level always counts a little, so
    // the result draws on all the levels' decorations; matching ones dominate.
    std::vector<std::pair<float, Style const*>> scored;
    float best = 0;
    for (auto const& s : d.levels) {
        float sc = score(s, ws, lp);
        best = std::max(best, sc);
        scored.push_back({sc, &s});
    }
    std::sort(scored.begin(), scored.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    for (auto& [sc, _] : scored) sc = best > 0 ? 0.03f + sc / best : 1.f;

    // Merge: per (class, side), pool every level's items weighted by level weight.
    Style merged;
    merged.name = "Your description";
    std::map<std::pair<std::string, std::string>, std::pair<float, std::map<int, std::pair<float, Item>>>> pool;  // rate sum, items
    std::map<std::pair<std::string, std::string>, float> wsum;
    std::map<int, std::pair<float, SceneryItem>> scn;
    float scnRate = 0, totalW = 0;
    for (auto const& [w, s] : scored) {
        totalW += w;
        for (auto const& sl : s->slots) {
            auto key = std::make_pair(sl.anchor, sl.side);
            auto& p = pool[key];
            p.first += sl.rate * w; wsum[key] += w;
            for (auto const& it : sl.items) {
                auto& e = p.second[it.id];
                if (e.first == 0) e.second = it;
                e.first += it.share * w;
            }
        }
        scnRate += s->sceneryRate * w;
        for (auto const& it : s->scenery) {
            auto& e = scn[it.id];
            if (e.first == 0) e.second = it;
            e.first += it.share * w;
        }
    }
    for (auto& [key, p] : pool) {
        Slot sl; sl.anchor = key.first; sl.side = key.second; sl.rate = p.first / std::max(1e-6f, wsum[key]);
        std::vector<std::pair<float, Item>> items;
        for (auto& [id, e] : p.second) items.push_back(e);
        std::sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        float tot = 0;
        for (size_t i = 0; i < items.size() && i < 16; i++) tot += items[i].first;
        for (size_t i = 0; i < items.size() && i < 16; i++) {
            auto it = items[i].second; it.share = items[i].first / std::max(1e-6f, tot);
            sl.items.push_back(it);
        }
        merged.slots.push_back(std::move(sl));
    }
    merged.sceneryRate = scnRate / std::max(1e-6f, totalW);
    {
        std::vector<std::pair<float, SceneryItem>> items;
        for (auto& [id, e] : scn) items.push_back(e);
        std::sort(items.begin(), items.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
        for (size_t i = 0; i < items.size() && i < 20; i++) { auto it = items[i].second; it.share = items[i].first; merged.scenery.push_back(it); }
    }
    // Colors: the best-matching level's palette, tinted to any color the description names.
    merged.pal = scored.front().second->pal;
    std::optional<ccColor3B> tint;
    for (auto const& w : ws) if (auto c = colorWord(w)) { tint = c; break; }
    if (tint) {
        for (auto& p : merged.pal) {
            int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
            bool colorful = p[3] || (mx - mn) > 60;  // glows and vivid colors take the tint; black/white stay
            if (colorful) {
                float v = std::max(mx, 160) / 255.f;
                p = {static_cast<int>(tint->r * v), static_cast<int>(tint->g * v), static_cast<int>(tint->b * v), p[3]};
            }
        }
        bool any = false;
        for (auto const& p : merged.pal) any |= p[3] != 0;
        if (!any && !merged.pal.empty()) merged.pal.back() = {tint->r, tint->g, tint->b, 1};
    }

    auto res = generateWith(merged, selection);
    if (res) {
        auto r = res.unwrap();
        std::string top;
        if (best > 0) {
            for (size_t i = 0; i < scored.size() && i < 3; i++) top += (i ? ", " : "") + scored[i].second->name;
            r.summary = "mostly like " + top + "; " + r.summary;
        } else {
            r.summary = "all levels mixed; " + r.summary;
        }
        return Ok(std::move(r));
    }
    return res;
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
