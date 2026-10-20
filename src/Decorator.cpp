#include "Decorator.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

using namespace geode::prelude;

namespace {
    constexpr char const* SYSTEM_PROMPT = R"(You are a level decorator for Geometry Dash (version 2.2).
The user has selected a gameplay structure in the level editor. Your job is to add decoration objects around and on top of it so it matches the look the user describes.

Coordinate system:
- Units are editor units. One grid block is 30 units wide and tall.
- x grows to the right, y grows upward. Positions are object centers.
- A block sitting on the grid has its center at a multiple of 30 plus 15.
- The ground line in a fresh level is at y = 90 (first block row center is y = 105).

Rules:
- Keep the gameplay intact: never put solid blocks or hazards where the player has to fly or jump. Decoration should frame, outline and fill in, not block paths.
- Stay inside or near the bounding box of the selection (a margin of about 4 blocks is fine).
- Prefer object IDs that appear in the user's selection or palette. Only use other object IDs you are confident exist in Geometry Dash 2.2 and are decorative (non-solid).
- Use zLayer to layer things: -5,-3,-1,1,3 are background layers B5..B1, 0 is default, 5,7,9,11 are foreground T1..T4.
- Use mainColor/detailColor channel IDs (1-999) to group colors so the user can recolor them later; 0 keeps the object's default color.
- Rotation is in degrees, clockwise. Scale 1 is normal size.

Respond with ONLY a JSON object, no prose and no markdown fences, in exactly this shape:
{"objects":[{"id":1,"x":105,"y":105,"rotation":0,"scale":1,"flipX":false,"flipY":false,"zLayer":0,"zOrder":0,"mainColor":0,"detailColor":0}]}
Fields other than id, x and y may be omitted.)";

    float num(matjson::Value const& v, std::string_view key, float def) {
        if (!v.contains(key)) return def;
        return static_cast<float>(v[key].asDouble().unwrapOr(def));
    }

    int integer(matjson::Value const& v, std::string_view key, int def) {
        if (!v.contains(key)) return def;
        if (auto d = v[key].asDouble(); d.isOk()) {
            return static_cast<int>(std::lround(d.unwrap()));
        }
        return def;
    }

    bool flag(matjson::Value const& v, std::string_view key) {
        if (!v.contains(key)) return false;
        return v[key].asBool().unwrapOr(false);
    }

    // Formats a float without trailing zeros, the way GD writes them.
    std::string fmtNum(float v) {
        auto s = fmt::format("{:.3f}", v);
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        if (s == "-0") s = "0";
        return s;
    }
}

std::vector<SelectedObject> decorator::collectSelection(EditorUI* ui) {
    std::vector<SelectedObject> out;
    auto arr = ui->getSelectedObjects();
    if (!arr) return out;

    for (auto obj : CCArrayExt<GameObject*>(arr)) {
        auto pos = obj->getPosition();
        out.push_back({
            obj->m_objectID,
            pos.x, pos.y,
            obj->getRotation(),
            obj->m_scaleX, obj->m_scaleY,
        });
    }
    return out;
}

matjson::Value decorator::buildRequest(std::vector<SelectedObject> const& selection, std::string const& userPrompt) {
    auto mod = Mod::get();
    auto maxObjects = mod->getSettingValue<int64_t>("max-objects");
    auto palette = mod->getSettingValue<std::string>("palette");

    float minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    auto objs = matjson::Value::array();
    for (auto const& o : selection) {
        minX = std::min(minX, o.x); maxX = std::max(maxX, o.x);
        minY = std::min(minY, o.y); maxY = std::max(maxY, o.y);
        objs.push(matjson::makeObject({
            {"id", o.id},
            {"x", static_cast<double>(o.x)},
            {"y", static_cast<double>(o.y)},
            {"rotation", static_cast<double>(o.rotation)},
            {"scaleX", static_cast<double>(o.scaleX)},
            {"scaleY", static_cast<double>(o.scaleY)},
        }));
    }

    auto userMessage = fmt::format(
        "Requested look: {}\n\n"
        "Selection bounding box (object centers): x {} to {}, y {} to {}.\n"
        "Place at most {} objects.\n"
        "{}"
        "Selected objects:\n{}",
        userPrompt,
        fmtNum(minX), fmtNum(maxX), fmtNum(minY), fmtNum(maxY),
        maxObjects,
        palette.empty() ? "" : fmt::format("Preferred palette (id:description): {}\n", palette),
        objs.dump(matjson::NO_INDENTATION)
    );

    // Gemini generateContent request body.
    auto sysParts = matjson::Value::array();
    sysParts.push(matjson::makeObject({{"text", SYSTEM_PROMPT}}));

    auto userParts = matjson::Value::array();
    userParts.push(matjson::makeObject({{"text", userMessage}}));

    auto contents = matjson::Value::array();
    contents.push(matjson::makeObject({
        {"role", "user"},
        {"parts", userParts},
    }));

    return matjson::makeObject({
        {"systemInstruction", matjson::makeObject({{"parts", sysParts}})},
        {"contents", contents},
        {"generationConfig", matjson::makeObject({
            {"responseMimeType", "application/json"},
            {"maxOutputTokens", 32000},
        })},
    });
}

Result<std::vector<DecoObject>> decorator::parseResponse(matjson::Value const& response) {
    if (response.contains("error")) {
        auto msg = response["error"].contains("message")
            ? response["error"]["message"].asString().unwrapOr("unknown error")
            : std::string("unknown error");
        return Err("API error: " + msg);
    }

    // Join the text parts of the first candidate (skipping any "thought" parts).
    std::string text;
    if (auto cands = response["candidates"].asArray(); cands.isOk() && !cands.unwrap().empty()) {
        auto const& cand = cands.unwrap()[0];
        if (auto parts = cand["content"]["parts"].asArray(); parts.isOk()) {
            for (auto const& part : parts.unwrap()) {
                if (part.contains("thought") && part["thought"].asBool().unwrapOr(false)) continue;
                if (part.contains("text")) text += part["text"].asString().unwrapOr("");
            }
        }
        if (text.empty() && cand.contains("finishReason")) {
            return Err("The AI stopped early (" + cand["finishReason"].asString().unwrapOr("unknown") + "). Try a lower Max Objects.");
        }
    }
    if (text.empty()) return Err("The AI returned an empty response.");

    // Tolerate stray prose or code fences around the JSON.
    auto start = text.find('{');
    auto end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end < start) {
        return Err("Couldn't find JSON in the AI's response.");
    }

    auto parsed = matjson::parse(std::string_view(text).substr(start, end - start + 1));
    if (!parsed) {
        return Err("Couldn't parse the AI's JSON: " + parsed.unwrapErr().message);
    }
    auto root = parsed.unwrap();

    auto list = root["objects"].asArray();
    if (!list) return Err("The AI's JSON has no \"objects\" list.");

    auto maxObjects = static_cast<size_t>(Mod::get()->getSettingValue<int64_t>("max-objects"));

    std::vector<DecoObject> out;
    for (auto const& v : list.unwrap()) {
        if (out.size() >= maxObjects) break;
        if (!v.contains("id") || !v.contains("x") || !v.contains("y")) continue;

        DecoObject o;
        o.id = integer(v, "id", 0);
        if (o.id <= 0 || o.id > 5000) continue;
        o.x = num(v, "x", 0);
        o.y = num(v, "y", 0);
        o.rotation = num(v, "rotation", 0);
        o.scale = std::clamp(num(v, "scale", 1), 0.05f, 10.f);
        o.flipX = flag(v, "flipX");
        o.flipY = flag(v, "flipY");
        o.zLayer = integer(v, "zLayer", 0);
        o.zOrder = std::clamp(integer(v, "zOrder", 0), -100, 100);
        o.mainColor = std::clamp(integer(v, "mainColor", 0), 0, 1101);
        o.detailColor = std::clamp(integer(v, "detailColor", 0), 0, 1101);
        out.push_back(o);
    }

    if (out.empty()) return Err("The AI didn't return any usable objects.");
    return Ok(std::move(out));
}

std::string decorator::toObjectString(std::vector<DecoObject> const& objects) {
    // GD object string keys: 1 id, 2 x, 3 y, 4 flipX, 5 flipY, 6 rotation,
    // 21 main color, 22 detail color, 24 z layer, 25 z order, 32 scale.
    std::string out;
    for (auto const& o : objects) {
        out += fmt::format("1,{},2,{},3,{}", o.id, fmtNum(o.x), fmtNum(o.y));
        if (o.flipX) out += ",4,1";
        if (o.flipY) out += ",5,1";
        if (o.rotation != 0) out += ",6," + fmtNum(o.rotation);
        if (o.mainColor > 0) out += fmt::format(",21,{}", o.mainColor);
        if (o.detailColor > 0) out += fmt::format(",22,{}", o.detailColor);
        if (o.zLayer != 0) out += fmt::format(",24,{}", o.zLayer);
        if (o.zOrder != 0) out += fmt::format(",25,{}", o.zOrder);
        if (o.scale != 1) out += ",32," + fmtNum(o.scale);
        out += ";";
    }
    return out;
}
