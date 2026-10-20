#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// One object in the structure the user selected.
struct SelectedObject {
    int id;
    float x, y;
    float rotation;
    float scaleX, scaleY;
    bool solid;
    bool flipX = false, flipY = false;
    int type = 0;  // GameObjectType
};

// One decoration object to place.
struct DecoObject {
    int id = 1;
    float x = 0, y = 0;
    float rotation = 0;
    float scale = 1;
    float scaleX = 1, scaleY = 1;  // extra stretch on top of scale (GD 2.2 keys 128/129)
    bool flipX = false, flipY = false;
    int zLayer = 0;   // GD ZLayer value: -5,-3,-1,1,3 (B5..B1), 0 default, 5,7,9,11 (T1..T4)
    int zOrder = 0;
    int mainColor = 0;   // color channel ID, 0 = object default
    int detailColor = 0;
};

namespace decorator {
    // Reads the editor's current selection.
    std::vector<SelectedObject> collectSelection(EditorUI* ui);

    // Turns decoration objects into a GD level object string ("1,id,2,x,3,y,...;").
    std::string toObjectString(std::vector<DecoObject> const& objects);

    // Formats a float without trailing zeros, the way GD writes them.
    std::string fmtNum(float v);
}
