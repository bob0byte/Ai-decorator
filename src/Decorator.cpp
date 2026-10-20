#include "Decorator.hpp"
#include <Geode/binding/GameObject.hpp>

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

using namespace geode::prelude;

std::string decorator::fmtNum(float v) {
    auto s = fmt::format("{:.3f}", v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
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
            obj->m_objectType == GameObjectType::Solid,
            obj->m_isFlipX, obj->m_isFlipY,
            static_cast<int>(obj->m_objectType),
        });
    }
    return out;
}

std::string decorator::toObjectString(std::vector<DecoObject> const& objects) {
    // GD object string keys: 1 id, 2 x, 3 y, 4 flipX, 5 flipY, 6 rotation,
    // 21 main color, 22 detail color, 24 z layer, 25 z order, 32 scale, 128/129 scale x/y.
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
        if (o.scaleX * o.scale != 1 || o.scaleY * o.scale != 1) {
            out += ",128," + fmtNum(o.scaleX * o.scale) + ",129," + fmtNum(o.scaleY * o.scale);
        }
        out += ";";
    }
    return out;
}
