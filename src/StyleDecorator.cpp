#include "StyleDecorator.hpp"
#include "StyleData.hpp"
#include "NeuralData.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/ColorAction.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
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
        std::string name, type;
        std::vector<std::array<int, 4>> pal;   // r, g, b, blending
        std::vector<Slot> slots;
        float sceneryRate = 0;
        std::vector<SceneryItem> scenery;
        std::unordered_map<std::string, float> tags;
        float pulseGap = 123.f, fadeIn = 0, hold = 0, fadeOut = .3f;
        float colorDur = .15f, colorRate = 0;  // color change duration, changes per 1000 units
    };
    struct DecoType {
        std::string name;
        std::vector<size_t> levels;  // most typical first
    };
    struct Data {
        std::vector<DecoType> types;
        std::vector<Style> levels;
        std::unordered_map<int, std::string> category;  // known gameplay object id -> hitbox class
        std::unordered_map<int, std::string> family;    // deco id -> sprite family
        bool ok = false;
        std::string error;
    };

    float fnum(matjson::Value const& v) { return static_cast<float>(v.asDouble().unwrapOr(0.0)); }
    int inum(matjson::Value const& v) { return static_cast<int>(std::lround(v.asDouble().unwrapOr(0.0))); }
    std::vector<matjson::Value> arr(matjson::Value const& v) { return v.asArray().unwrapOr(std::vector<matjson::Value>{}); }

    Style parseStyle(matjson::Value const& s) {
        Style st;
        st.name = s["name"].asString().unwrapOr("Style");
        st.type = s.contains("type") ? s["type"].asString().unwrapOr("") : "";
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
            std::sort(slot.items.begin(), slot.items.end(), [](Item const& a, Item const& b) { return a.share > b.share; });
            st.slots.push_back(std::move(slot));
        }
        st.sceneryRate = fnum(s["scn"]["rate"]);
        for (auto const& it : arr(s["scn"]["items"])) {
            st.scenery.push_back({inum(it[0]), fnum(it[1]), fnum(it[2]), fnum(it[3]), fnum(it[4]), inum(it[5]), inum(it[6]), fnum(it[7])});
        }
        if (s.contains("tags")) {
            for (auto const& [k, v] : s["tags"]) st.tags[k] = fnum(v);
        }
        if (s.contains("eff")) {
            auto e = arr(s["eff"]);
            if (e.size() >= 4) { st.pulseGap = fnum(e[0]); st.fadeIn = fnum(e[1]); st.hold = fnum(e[2]); st.fadeOut = fnum(e[3]); }
            if (e.size() >= 7) { st.colorDur = fnum(e[5]); st.colorRate = fnum(e[6]); }
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
            for (auto const& s : arr(root["levels"])) d.levels.push_back(parseStyle(s));
            for (auto const& t : arr(root["types"])) {
                DecoType dt;
                dt.name = t["name"].asString().unwrapOr("");
                for (auto const& i : arr(t["levels"])) {
                    auto k = static_cast<size_t>(inum(i));
                    if (k < d.levels.size()) dt.levels.push_back(k);
                }
                if (!dt.levels.empty()) d.types.push_back(std::move(dt));
            }
            for (auto const& [id, f] : root["fams"]) d.family[std::atoi(id.c_str())] = f.asString().unwrapOr("");
            for (auto const& [cat, ids] : root["cats"]) {
                for (auto const& id : arr(ids)) d.category[inum(id)] = cat;
            }
            d.ok = !d.levels.empty();
            log::info("AI Decorator: loaded {} deco types and {} level styles", d.types.size(), d.levels.size());
            return d;
        }();
        return d;
    }

    // The level used for a style reference (for a type: its n-th most typical level).
    Style const* styleFor(styles::StyleRef ref, int variant = 0) {
        auto const& d = data();
        if (ref.level) return ref.index < d.levels.size() ? &d.levels[ref.index] : nullptr;
        if (ref.index >= d.types.size()) return nullptr;
        auto const& t = d.types[ref.index];
        return &d.levels[t.levels[static_cast<size_t>(std::max(variant, 0)) % t.levels.size()]];
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
    float snap(float v) { return std::floor(v / 30.f) * 30.f + 15.f; }

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

    // What a deco piece is for. Pieces with no clear role (random textured chunks, unknown 2.2 objects) aren't used.
    enum class Role { None, Body, Glow, Line, Particle, Chain, Persp };
    Role roleOf(int id) {
        auto const& fams = data().family;
        auto it = fams.find(id);
        if (it == fams.end()) return Role::None;
        auto const& f = it->second;
        auto has = [&](char const* k) { return f.find(k) != std::string::npos; };
        if (has("gradient") || has("glow")) return Role::Glow;
        if (has("persp")) return Role::Persp;
        if (has("chain")) return Role::Chain;
        if (has("lightsquare") || has("lighttriangle") || has("whiteblock") || f == "d_square" || f.rfind("square_", 0) == 0) return Role::Body;
        if (has("outline") || has("gridline") || has("d_bar") || has("smallbar") || has("d_link")) return Role::Line;
        if (has("ball") || has("scalefadering") || has("circle") || has("spark")) return Role::Particle;
        return Role::None;
    }

    std::string nice(std::string c) { std::replace(c.begin(), c.end(), '_', ' '); return c; }

    // ------------------------------------------------------------------ neural network
    // Probabilities of each decoration token for one side of one hitbox.
    // Inputs: hitbox class, side, rotation quarter, deco type, 7x7 neighbourhood of hitbox kinds (see tools/ml_extract.py).
    std::array<float, nn::V> const& neuralForward(int hb, int side, int quarter, int type, std::array<int8_t, (2 * nn::R + 1) * (2 * nn::R + 1)> const& nb) {
        static std::array<float, nn::H1> h1;
        static std::array<float, nn::H2> h2;
        static std::array<float, nn::V> out;
        std::vector<int> active;
        int o = 0;
        active.push_back(o + hb); o += nn::NHB;
        active.push_back(o + side); o += 5;
        active.push_back(o + quarter); o += 4;
        active.push_back(o + type); o += nn::NTYPES;
        for (size_t c = 0; c < nb.size(); c++) if (nb[c] > 0) active.push_back(o + static_cast<int>(c) * 4 + nb[c] - 1);
        // layer 1: inputs are one-hot, so just add the active rows
        for (int j = 0; j < nn::H1; j++) h1[j] = nn::B1[j];
        for (int i : active) { auto row = nn::W1 + static_cast<size_t>(i) * nn::H1; for (int j = 0; j < nn::H1; j++) h1[j] += row[j]; }
        for (auto& v : h1) v = std::max(v, 0.f);
        for (int j = 0; j < nn::H2; j++) h2[j] = nn::B2[j];
        for (int i = 0; i < nn::H1; i++) {
            if (h1[i] == 0.f) continue;
            auto row = nn::W2 + static_cast<size_t>(i) * nn::H2; float v = h1[i];
            for (int j = 0; j < nn::H2; j++) h2[j] += v * row[j];
        }
        for (auto& v : h2) v = std::max(v, 0.f);
        for (int j = 0; j < nn::V; j++) out[j] = nn::B3[j];
        for (int i = 0; i < nn::H2; i++) {
            if (h2[i] == 0.f) continue;
            auto row = nn::W3 + static_cast<size_t>(i) * nn::V; float v = h2[i];
            for (int j = 0; j < nn::V; j++) out[j] += v * row[j];
        }
        for (auto& v : out) v = 1.f / (1.f + std::exp(-std::clamp(v, -30.f, 30.f)));
        return out;
    }

    int neuralType(std::string const& type) {
        for (int i = 0; i < nn::NTYPES; i++) if (type == nn::TYPES[i]) return i;
        return 0;
    }
    int neuralHitbox(std::string const& cls) {
        for (int i = 0; i < nn::NHB; i++) if (cls == nn::HB[i]) return i;
        return nn::NHB - 1;  // "other"
    }

    // Decorate deterministically: every open side of every hitbox of the same kind gets the same
    // pieces, stacked in the source level's layer order. `variant` swaps in the next-most-used piece.
    Result<styles::Result> generateWith(Style const& style, std::vector<SelectedObject> const& selection, int variant) {
        auto mod = Mod::get();
        auto maxObjects = static_cast<size_t>(mod->getSettingValue<int64_t>("max-objects"));
        int chStart = static_cast<int>(mod->getSettingValue<int64_t>("color-channel-start"));
        int maxLayers = static_cast<int>(mod->getSettingValue<int64_t>("layers-per-side"));
        float sceneryAmount = 1.f;  // background on/off is an Options toggle

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
        if (anchors.empty()) return Err("No blocks, spikes or saws in the selection (decoration and triggers are skipped).");
        // Process hitboxes left to right, bottom to top, so the result is built in order.
        std::sort(anchors.begin(), anchors.end(), [](Anchor const& a, Anchor const& b) {
            return a.o->x != b.o->x ? a.o->x < b.o->x : a.o->y < b.o->y;
        });

        std::map<std::pair<std::string, std::string>, Slot const*> slotMap;
        for (auto const& sl : style.slots) slotMap[{sl.anchor, sl.side}] = &sl;

        // Which pieces each (hitbox kind, side) gets. Clean rules on top of what the level uses:
        //  - block/platform/slope center: one body piece (kept inside the hitbox) + one glow
        //  - open edges: one glow (or an outline/line if the level has no edge glow), hugging the edge
        //  - spikes, saws, orbs...: one glow or ring on the object itself; chains only under blocks in Hell styles
        bool is3D = style.type == "3D", isHell = style.type == "Hell";
        std::map<std::pair<std::string, std::string>, std::vector<Item const*>> picks;
        auto pickFor = [&](std::string const& cls, std::string const& side) -> std::vector<Item const*> const& {
            auto key = std::make_pair(cls, side);
            if (auto it = picks.find(key); it != picks.end()) return it->second;
            bool gridded = cls == "block" || cls == "platform" || cls == "slope";
            std::vector<Item const*> ranked;
            for (auto const& f : fallbacks(cls)) {
                if (auto it = slotMap.find({f, side}); it != slotMap.end() && it->second->rate >= 0.1f) {
                    for (auto const& item : it->second->items) if (usable(item.id)) ranked.push_back(&item);
                    break;
                }
            }
            // best piece of a role that sits sensibly; `skip` lets variations take the next one
            auto best = [&](Role role, float maxOut, int skip) -> Item const* {
                std::vector<Item const*> ok;
                for (auto it : ranked) {
                    auto r = roleOf(it->id);
                    if (r != role) continue;
                    if (std::abs(it->out) > maxOut || std::abs(it->along) > 8.f) continue;
                    if (std::max(it->sx, it->sy) > 2.5f || std::min(it->sx, it->sy) < .2f) continue;
                    ok.push_back(it);
                }
                if (ok.empty()) return nullptr;
                return ok[static_cast<size_t>(skip) % ok.size()];
            };
            std::vector<Item const*> chosen;
            auto const& op = styles::options();
            auto add = [&](Item const* it) {
                if (!it) return;
                auto r = roleOf(it->id);
                if (r == Role::Body && !op.body) return;
                if (r == Role::Glow && !(gridded ? op.glow : op.hazardGlow)) return;
                if ((r == Role::Line || r == Role::Chain) && !op.lines) return;
                if (r == Role::Particle && !(gridded ? op.particles : (op.particles || op.hazardGlow))) return;
                chosen.push_back(it);
            };
            if (side == "center") {
                if (gridded) add(best(Role::Body, 3.f, 0));
                if (gridded && is3D) add(best(Role::Persp, 3.f, variant));
                add(best(Role::Glow, 4.f, variant));
                if (!gridded && chosen.empty()) add(best(Role::Particle, 4.f, variant));
            } else if (gridded) {
                auto g = best(Role::Glow, 30.f, variant);
                if (g && std::abs(g->out) >= 6.f) add(g);
                else add(best(Role::Line, 18.f, variant));
                if (isHell && side == "bottom") add(best(Role::Chain, 40.f, 0));
            }
            std::sort(chosen.begin(), chosen.end(), [](Item const* a, Item const* b) {
                return a->z != b->z ? a->z < b->z : a->zo < b->zo;
            });
            return picks[key] = std::move(chosen);
        };

        styles::Result res;
        auto& out = res.objects;
        std::set<std::tuple<int, int, int, int>> placed;  // never place the same piece twice in one spot
        auto place = [&](DecoObject o) {
            if (out.size() >= maxObjects) return;
            auto key = std::make_tuple(o.id, static_cast<int>(std::lround(o.x)), static_cast<int>(std::lround(o.y)),
                                       static_cast<int>(std::lround(o.rotation)) % 360);
            if (!placed.insert(key).second) return;
            out.push_back(o);
        };
        // Channels by role: body, glow (blending), line/detail, particles.
        auto roleChannel = [&](int id) {
            switch (roleOf(id)) {
                case Role::Body: case Role::Persp: return chStart;
                case Role::Glow: return chStart + 1;
                case Role::Line: case Role::Chain: return chStart + 2;
                case Role::Particle: return chStart + 3;
                default: return chStart + 2;
            }
        };

        // Hitbox kinds per grid cell, for the network's view of the surroundings.
        std::map<Cell, int> occ;
        for (auto const& a : anchors) {
            int code = (a.cls == "block" || a.cls == "platform") ? 1 : a.cls == "slope" ? 2
                     : (a.cls == "spike" || a.cls == "ground_spike" || a.cls == "spike_slope" || a.cls == "saw") ? 3 : 4;
            auto key = cellOf(a.o->x, a.o->y);
            occ[key] = std::max(occ[key], code);
        }
        int aiType = neuralType(style.type);

        std::map<std::string, int> classCounts;
        for (auto const& a : anchors) {
            auto const& o = *a.o;
            classCounts[a.cls]++;
            bool gridded = a.cls == "block" || a.cls == "platform" || a.cls == "slope";
            auto [cx, cy] = cellOf(o.x, o.y);
            int quarter = ((static_cast<int>(std::lround(o.rotation / 90.f)) % 4) + 4) % 4;
            float sizeK = gridded && o.hbW > 1 && o.hbH > 1 ? std::clamp(std::max(o.hbW, o.hbH) / 30.f, .5f, 4.f) : 1.f;

            // AI network: what to put on each side of THIS hitbox, given its surroundings.
            std::array<std::vector<Item>, 5> aiItems;
            bool useAI = styles::options().useAI;
            if (useAI) {
                std::array<int8_t, (2 * nn::R + 1) * (2 * nn::R + 1)> nb{};
                int c = 0;
                for (int dy = -nn::R; dy <= nn::R; dy++)
                    for (int dx = -nn::R; dx <= nn::R; dx++, c++) {
                        if (dx == 0 && dy == 0) continue;
                        if (auto it = occ.find({cx + dx, cy + dy}); it != occ.end()) nb[c] = static_cast<int8_t>(it->second);
                    }
                float thr = styles::options().aiConfidence;
                int maxK = std::max(1, maxLayers);
                for (int si = 0; si < 5; si++) {
                    auto const& p = neuralForward(neuralHitbox(a.cls), si, quarter, aiType, nb);
                    std::array<int, nn::V> order;
                    for (int j = 0; j < nn::V; j++) order[j] = j;
                    std::partial_sort(order.begin(), order.begin() + 16, order.end(), [&](int x, int y) { return p[x] > p[y]; });
                    std::set<int> usedRoles;
                    for (int r = 0; r < 16 && static_cast<int>(aiItems[si].size()) < maxK; r++) {
                        int j = order[r];
                        if (p[j] < thr) break;
                        auto const& t = nn::TOKENS[j];
                        auto role = roleOf(t.id);
                        if (role == Role::None || usedRoles.count(static_cast<int>(role)) || !usable(t.id)) continue;
                        if (role == Role::Body && !styles::options().body) continue;
                        if (role == Role::Glow && !(gridded ? styles::options().glow : styles::options().hazardGlow)) continue;
                        if ((role == Role::Line || role == Role::Chain) && !styles::options().lines) continue;
                        if (role == Role::Particle && !styles::options().particles) continue;
                        usedRoles.insert(static_cast<int>(role));
                        aiItems[si].push_back({t.id, p[j], t.along, t.out, t.rot, t.sx, t.sy, 0.f, t.z, t.zo, 0});
                    }
                }
            }

            int sideIndex = -1;
            for (std::string side : {"center", "top", "bottom", "left", "right"}) {
                sideIndex++;
                std::vector<Item const*> aiPk;
                if (useAI) for (auto const& it : aiItems[sideIndex]) aiPk.push_back(&it);
                auto const& pk = useAI ? aiPk : pickFor(a.cls, side);
                if (pk.empty()) continue;
                if (gridded && side != "center") {
                    int dx = side == "right" ? 1 : side == "left" ? -1 : 0;
                    int dy = side == "top" ? 1 : side == "bottom" ? -1 : 0;
                    int wdx = dx, wdy = dy;
                    for (int q = 0; q < quarter; q++) { int t = wdx; wdx = wdy; wdy = -t; }
                    if (cells.count({cx + wdx, cy + wdy})) continue;  // face touches another hitbox
                }
                float alpha = sideAngle(side) * static_cast<float>(M_PI) / 180.f;
                float th = o.rotation * static_cast<float>(M_PI) / 180.f;
                for (auto it : pk) {
                    float along = it->along * sizeK, outD = it->out * sizeK;
                    float lx = along * std::cos(alpha) + outD * std::sin(alpha);
                    float ly = -along * std::sin(alpha) + outD * std::cos(alpha);
                    if (o.flipX) lx = -lx;
                    if (o.flipY) ly = -ly;
                    DecoObject deco;
                    deco.id = it->id;
                    deco.x = o.x + lx * std::cos(th) + ly * std::sin(th);
                    deco.y = o.y - lx * std::sin(th) + ly * std::cos(th);
                    deco.rotation = std::fmod(it->rot + o.rotation + sideAngle(side) + 720.f, 360.f);
                    float gk = roleOf(it->id) == Role::Glow ? styles::options().glowScale : 1.f;
                    deco.scaleX = it->sx * sizeK * gk; deco.scaleY = it->sy * sizeK * gk;
                    deco.flipX = it->flipChance > .5f;
                    deco.zLayer = it->z; deco.zOrder = it->zo;
                    if (roleOf(it->id) == Role::Body) { deco.zLayer = 3; deco.zOrder = -10; }  // B1: behind the block, as its fill
                    deco.mainColor = roleChannel(it->id);
                    deco.detailColor = deco.mainColor;
                    place(deco);
                }
            }
        }

        // Background: neat rows of the level's most used background pieces, snapped to the grid.
        if (styles::options().scenery && sceneryAmount <= 0) sceneryAmount = 1.f;
        if (!style.scenery.empty() && sceneryAmount > 0 && styles::options().scenery) {
            float minX = INFINITY, maxX = -INFINITY, minY = INFINITY, maxY = -INFINITY;
            for (auto const& a : anchors) {
                minX = std::min(minX, a.o->x); maxX = std::max(maxX, a.o->x);
                minY = std::min(minY, a.o->y); maxY = std::max(maxY, a.o->y);
            }
            std::vector<SceneryItem const*> top;
            for (auto const& s : style.scenery) {
                auto r = roleOf(s.id);
                if (usable(s.id) && (r == Role::Particle || r == Role::Glow) && top.size() < 2) top.push_back(&s);
            }
            int step = std::clamp(static_cast<int>(std::lround(2.f / std::max(.05f, style.sceneryRate * sceneryAmount))), 1, 8);
            for (size_t k = 0; k < top.size(); k++) {
                auto s = top[k];
                float d = snap(std::clamp(s->dist, 45.f, 180.f)) - 15.f;
                float y = k == 0 ? snap(maxY + d) : snap(minY - d);
                int i = 0;
                for (float x = snap(minX - 30); x <= maxX + 30; x += 30.f * step, i++) {
                    if (cells.count(cellOf(x, y))) continue;
                    DecoObject deco;
                    deco.id = s->id; deco.x = x; deco.y = y;
                    deco.rotation = s->rot; deco.scaleX = s->sx; deco.scaleY = s->sy;
                    deco.zLayer = s->z; deco.mainColor = roleChannel(s->id); deco.detailColor = deco.mainColor;
                    place(deco);
                }
            }
        }

        if (out.empty()) {
            std::string found;
            for (auto const& [c, n] : classCounts) found += fmt::format("{}{} {}", found.empty() ? "" : ", ", n, nice(c));
            return Err(fmt::format("Nothing to place for: {}. Try another style.", found));
        }

        // Back-to-front, then left to right.
        std::stable_sort(out.begin(), out.end(), [](DecoObject const& a, DecoObject const& b) {
            if (a.zLayer != b.zLayer) return a.zLayer < b.zLayer;
            if (a.zOrder != b.zOrder) return a.zOrder < b.zOrder;
            return a.x < b.x;
        });

        // Role colors from the level's palette: darkest = body, most colorful = glow (brightened),
        // lightest = lines/details, glow color again (solid) = particles.
        auto lum = [](std::array<int, 4> const& p) { return std::max({p[0], p[1], p[2]}); };
        auto sat = [](std::array<int, 4> const& p) { return std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}); };
        std::array<int, 4> body{8, 8, 12, 0}, glowC{80, 200, 255, 1}, detail{240, 240, 245, 0};
        if (!style.pal.empty()) {
            body = *std::min_element(style.pal.begin(), style.pal.end(), [&](auto const& a, auto const& b) { return lum(a) < lum(b); });
            glowC = *std::max_element(style.pal.begin(), style.pal.end(), [&](auto const& a, auto const& b) { return sat(a) * 2 + lum(a) < sat(b) * 2 + lum(b); });
            detail = *std::max_element(style.pal.begin(), style.pal.end(), [&](auto const& a, auto const& b) { return lum(a) < lum(b); });
        }
        if (sat(glowC) < 40) glowC = detail;  // a mono style glows white
        float gm = std::max(1, lum(glowC));
        auto up = [&](int c) { return static_cast<GLubyte>(std::min(255.f, c / gm * 235.f)); };
        ccColor3B glow3{up(glowC[0]), up(glowC[1]), up(glowC[2])};
        if (lum(detail) < 150) detail = {235, 235, 240, 0};
        auto c3 = [](std::array<int, 4> const& p) { return ccColor3B{static_cast<GLubyte>(p[0]), static_cast<GLubyte>(p[1]), static_cast<GLubyte>(p[2])}; };
        auto const& op = styles::options();
        // hue shift + brightness from the Options tab
        auto adjust = [&](ccColor3B c, float extraHue = 0.f) {
            float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
            float mx = std::max({r, g, b}), mn = std::min({r, g, b}), d = mx - mn, h = 0;
            if (d > 1e-4f) {
                if (mx == r) h = std::fmod((g - b) / d, 6.f); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
                h *= 60.f;
            }
            float sat = mx > 0 ? d / mx : 0, val = std::min(1.f, mx * op.brightness);
            h = std::fmod(h + op.hue + extraHue + 720.f, 360.f);
            float C = val * sat, X = C * (1 - std::abs(std::fmod(h / 60.f, 2.f) - 1)), m = val - C;
            float rr, gg, bb;
            if (h < 60) { rr = C; gg = X; bb = 0; } else if (h < 120) { rr = X; gg = C; bb = 0; } else if (h < 180) { rr = 0; gg = C; bb = X; }
            else if (h < 240) { rr = 0; gg = X; bb = C; } else if (h < 300) { rr = X; gg = 0; bb = C; } else { rr = C; gg = 0; bb = X; }
            return ccColor3B{static_cast<GLubyte>((rr + m) * 255), static_cast<GLubyte>((gg + m) * 255), static_cast<GLubyte>((bb + m) * 255)};
        };
        ccColor3B bodyC = adjust(c3(body)), detailC = adjust(c3(detail));
        glow3 = adjust(glow3);
        if (op.colorMode == 2) { bodyC = {6, 6, 8}; glow3 = {235, 235, 245}; detailC = {255, 255, 255}; }
        if (op.colorMode != 1) {  // mode 1 keeps the channels' current colors
            res.colors.push_back({chStart, bodyC, false, op.bodyOpacity});
            res.colors.push_back({chStart + 1, glow3, true, op.glowOpacity});
            res.colors.push_back({chStart + 2, detailC, false, 1.f});
            res.colors.push_back({chStart + 3, glow3, false, 1.f});
        }
        // Color shift: the glow changes to the style's other accent (or a 60 degree hue turn) mid-structure, then back.
        {
            ccColor3B alt = glow3;
            int bestSat = -1;
            for (auto const& p : style.pal) {
                int sv = sat(p);
                auto c = adjust(c3(p));
                if (sv > 40 && (std::abs(c.r - glow3.r) + std::abs(c.g - glow3.g) + std::abs(c.b - glow3.b)) > 120 && sv > bestSat) { bestSat = sv; alt = c; }
            }
            if (bestSat < 0) alt = adjust(glow3, 60.f - static_cast<float>(op.hue));  // glow3 is already hue-shifted
            res.shift = {chStart + 1, alt, glow3, std::clamp(style.colorDur > 0.01f ? style.colorDur : .2f, .1f, 2.f), op.colorShift};
        }
        res.pulse.channel = chStart + 1;
        res.pulse.color = {static_cast<GLubyte>(std::min(255, glow3.r / 2 + 128)), static_cast<GLubyte>(std::min(255, glow3.g / 2 + 128)),
                           static_cast<GLubyte>(std::min(255, glow3.b / 2 + 128))};
        res.pulse.gap = styles::options().pulseBlocks > 0 ? styles::options().pulseBlocks * 30.f : std::clamp(style.pulseGap, 60.f, 360.f);
        res.pulse.fadeIn = style.fadeIn; res.pulse.hold = style.hold; res.pulse.fadeOut = std::clamp(style.fadeOut, .05f, 1.5f);

        std::string found;
        for (auto const& [c, n] : classCounts) found += fmt::format("{}{} {}{}", found.empty() ? "" : ", ", n, nice(c), n == 1 ? "" : "s");
        res.summary = found;
        res.source = style.name;
        return Ok(std::move(res));
    }

    // ------------------------------------------------------------------ prompt matching

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    std::unordered_map<std::string, std::vector<std::string>> const& synonyms() {
        static std::unordered_map<std::string, std::vector<std::string>> m = {
            {"fire", {"red", "orange", "glow", "hell"}}, {"lava", {"red", "orange", "glow", "hell"}}, {"demon", {"hell", "dark"}},
            {"blood", {"red", "dark", "hell"}}, {"ice", {"cyan", "azure", "white"}}, {"frozen", {"cyan", "azure", "white"}},
            {"snow", {"white", "azure"}}, {"winter", {"white", "azure"}}, {"ocean", {"blue", "cyan", "teal"}},
            {"water", {"blue", "cyan"}}, {"sea", {"blue", "teal"}}, {"nature", {"green", "lime", "art"}}, {"forest", {"green", "art"}},
            {"jungle", {"green", "lime"}}, {"toxic", {"lime", "green", "glow"}}, {"acid", {"lime", "glow"}},
            {"space", {"dark", "blue", "purple", "particles"}}, {"galaxy", {"purple", "blue", "particles"}},
            {"night", {"dark", "blue"}}, {"cosmic", {"purple", "particles"}}, {"gold", {"yellow", "orange"}},
            {"golden", {"yellow", "orange"}}, {"sun", {"yellow", "orange", "bright"}}, {"candy", {"pink", "bright"}},
            {"cute", {"pink", "bright"}}, {"love", {"pink", "red"}}, {"spooky", {"dark", "purple", "orange"}},
            {"halloween", {"orange", "purple", "dark"}}, {"horror", {"dark", "red", "hell"}}, {"scary", {"dark", "red"}},
            {"cyber", {"cyan", "glow", "grid", "modern"}}, {"tech", {"grid", "cyan", "lines", "modern"}}, {"futuristic", {"glow", "modern"}},
            {"8bit", {"pixel", "retro"}}, {"old", {"classic"}}, {"oldschool", {"classic"}}, {"tilemap", {"classic"}},
            {"glowy", {"glow"}}, {"neon", {"glow"}}, {"shiny", {"glow", "bright"}}, {"chain", {"chains"}},
            {"nc", {"nine", "circles"}}, {"diamond", {"nine", "circles"}}, {"busy", {"dense"}}, {"crazy", {"dense", "particles", "effect"}},
            {"particle", {"particles"}}, {"ring", {"rings"}}, {"orb", {"orbs"}}, {"depth", {"3d"}}, {"perspective", {"3d"}},
            {"outline", {"outlines"}}, {"line", {"lines"}}, {"violet", {"purple"}}, {"grey", {"mono"}}, {"gray", {"mono"}},
            {"black", {"dark", "black"}}, {"flat", {"modern"}}, {"animated", {"effect"}}, {"moving", {"effect"}},
        };
        return m;
    }

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
        auto nm = lower(s.name);
        if (nm.size() > 2 && prompt.find(nm) != std::string::npos) sc += 40.f;
        return sc;
    }
}

styles::Options& styles::options() {
    static Options o = [] {
        Options o;
        auto m = Mod::get();
        auto b = [&](char const* k, bool d) { return m->getSavedValue<bool>(k, d); };
        auto f = [&](char const* k, float d) { return static_cast<float>(m->getSavedValue<double>(k, d)); };
        o.body = b("opt-body", true); o.glow = b("opt-glow", true); o.lines = b("opt-lines", true);
        o.hazardGlow = b("opt-hazard", true); o.particles = b("opt-particles", true); o.scenery = b("opt-scenery", false);
        o.pulses = b("opt-pulses", true); o.colorShift = b("opt-shift", false);
        o.glowScale = f("opt-glow-scale", 1.f); o.glowOpacity = f("opt-glow-opacity", 1.f); o.bodyOpacity = f("opt-body-opacity", 1.f);
        o.hue = static_cast<int>(m->getSavedValue<int64_t>("opt-hue", 0)); o.brightness = f("opt-brightness", 1.f);
        o.pulseBlocks = static_cast<int>(m->getSavedValue<int64_t>("opt-pulse-blocks", 0));
        o.colorMode = static_cast<int>(m->getSavedValue<int64_t>("opt-color-mode", 0));
        o.useAI = b("opt-ai", true); o.aiConfidence = f("opt-ai-conf", .12f);
        return o;
    }();
    return o;
}

void styles::saveOptions() {
    auto const& o = options();
    auto m = Mod::get();
    m->setSavedValue<bool>("opt-body", o.body); m->setSavedValue<bool>("opt-glow", o.glow); m->setSavedValue<bool>("opt-lines", o.lines);
    m->setSavedValue<bool>("opt-hazard", o.hazardGlow); m->setSavedValue<bool>("opt-particles", o.particles);
    m->setSavedValue<bool>("opt-scenery", o.scenery); m->setSavedValue<bool>("opt-pulses", o.pulses); m->setSavedValue<bool>("opt-shift", o.colorShift);
    m->setSavedValue<double>("opt-glow-scale", o.glowScale); m->setSavedValue<double>("opt-glow-opacity", o.glowOpacity);
    m->setSavedValue<double>("opt-body-opacity", o.bodyOpacity); m->setSavedValue<int64_t>("opt-hue", o.hue);
    m->setSavedValue<double>("opt-brightness", o.brightness); m->setSavedValue<int64_t>("opt-pulse-blocks", o.pulseBlocks);
    m->setSavedValue<int64_t>("opt-color-mode", o.colorMode);
    m->setSavedValue<bool>("opt-ai", o.useAI); m->setSavedValue<double>("opt-ai-conf", o.aiConfidence);
}

size_t styles::typeCount() { return data().types.size(); }
size_t styles::levelCount() { return data().levels.size(); }

std::string styles::name(StyleRef ref) {
    auto const& d = data();
    if (!ref.level) return ref.index < d.types.size() ? d.types[ref.index].name : "?";
    auto s = styleFor(ref);
    return s ? s->name : "?";
}

std::string styles::description(StyleRef ref) {
    auto const& d = data();
    if (!ref.level) {
        if (ref.index >= d.types.size()) return "";
        return fmt::format("Deco type, {} levels", d.types[ref.index].levels.size());
    }
    auto s = styleFor(ref);
    return s && !s->type.empty() ? s->type + " deco" : "Level style";
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
    if (o.isDeco) return "";  // existing decoration, triggers and no-touch objects are left alone
    auto type = static_cast<GameObjectType>(o.type);
    if (type == GameObjectType::Slope) return "slope";
    if (type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard) {
        if (o.radius > 0.5f) return "saw";
        float h = std::min(o.hbW, o.hbH);
        return (h > 0 && h <= 14.f) ? "ground_spike" : "spike";
    }
    auto const& cats = data().category;
    if (type == GameObjectType::Solid || type == GameObjectType::Breakable) {
        auto it = cats.find(o.id);
        if (it == cats.end() && (o.hbW < 1 || o.hbH < 1)) return "";
        // Every gameplay object up to 2.1 (id 1911) is in the learned table, so older ids missing from it are decoration.
        if (it == cats.end() && o.id <= 1911) return "";
        if (it != cats.end() && it->second != "block" && it->second != "platform") return it->second;
        float mn = std::min(o.hbW, o.hbH), mx = std::max(o.hbW, o.hbH);
        if (mx > 0 && mn < mx * .6f) return "platform";
        return "block";
    }
    if (auto it = cats.find(o.id); it != cats.end()) return it->second;
    return "";
}

Result<styles::Result> styles::generate(StyleRef ref, std::vector<SelectedObject> const& selection, int variant) {
    auto const& d = data();
    if (!d.ok) return Err(d.error.empty() ? std::string("No style data.") : d.error);
    // For a deco type, each press moves to the type's next most typical level.
    auto style = styleFor(ref, ref.level ? 0 : variant);
    if (!style) return Err("Unknown style.");
    return generateWith(*style, selection, ref.level ? variant : variant / std::max<int>(1, static_cast<int>(d.types[ref.index].levels.size())));
}

Result<styles::Result> styles::generateFromPrompt(std::string const& prompt, std::vector<SelectedObject> const& selection, int variant) {
    auto const& d = data();
    if (!d.ok) return Err(d.error.empty() ? std::string("No style data.") : d.error);
    auto ws = words(prompt);
    auto lp = lower(prompt);

    // Rank every level by how well it matches; each press uses the next best match.
    std::vector<std::pair<float, size_t>> ranked;
    for (size_t i = 0; i < d.levels.size(); i++) ranked.push_back({score(d.levels[i], ws, lp), i});
    std::stable_sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.first > b.first; });
    size_t pool = ranked.front().first > 0
        ? std::max<size_t>(1, std::count_if(ranked.begin(), ranked.end(), [&](auto const& r) { return r.first >= ranked.front().first * .6f; }))
        : d.levels.size();
    pool = std::min<size_t>(pool, 8);
    if (ranked.front().first <= 0) pool = d.levels.size();
    Style style = d.levels[ranked[static_cast<size_t>(std::max(variant, 0)) % pool].second];

    // Tint glows and vivid colors to a color named in the description; black and white stay.
    std::optional<ccColor3B> tint;
    for (auto const& w : ws) if (auto c = colorWord(w)) { tint = c; break; }
    if (tint) {
        for (auto& p : style.pal) {
            int mx = std::max({p[0], p[1], p[2]}), mn = std::min({p[0], p[1], p[2]});
            if (p[3] || (mx - mn) > 60) {
                float v = std::max(mx, 160) / 255.f;
                p = {static_cast<int>(tint->r * v), static_cast<int>(tint->g * v), static_cast<int>(tint->b * v), p[3]};
            }
        }
        bool any = false;
        for (auto const& p : style.pal) any |= p[3] != 0;
        if (!any && !style.pal.empty()) style.pal.back() = {tint->r, tint->g, tint->b, 1};
    }
    return generateWith(style, selection, variant / static_cast<int>(pool));
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
            float op = std::clamp(c.opacity, 0.f, 1.f);
            action->m_fromColor = c.color;
            action->m_color = c.color;
            action->m_blending = c.blending;
            action->m_fromOpacity = op;
            action->m_currentOpacity = op;
            action->m_copyID = 0;
        }
    }
}
