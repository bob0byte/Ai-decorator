#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

// One object in the structure the user selected, as sent to the AI.
struct SelectedObject {
    int id;
    float x, y;
    float rotation;
    float scaleX, scaleY;
};

// One decoration object returned by the AI.
struct DecoObject {
    int id = 1;
    float x = 0, y = 0;
    float rotation = 0;
    float scale = 1;
    bool flipX = false, flipY = false;
    int zLayer = 0;   // GD ZLayer value: -5,-3,-1,1,3 (B5..B1), 0 default, 5,7,9,11 (T1..T4)
    int zOrder = 0;
    int mainColor = 0;   // color channel ID, 0 = object default
    int detailColor = 0;
};

namespace decorator {
    // Reads the editor's current selection.
    std::vector<SelectedObject> collectSelection(EditorUI* ui);

    // Builds the request body for the Gemini generateContent API.
    matjson::Value buildRequest(std::vector<SelectedObject> const& selection, std::string const& userPrompt);

    // Pulls the decoration list out of the API response.
    geode::Result<std::vector<DecoObject>> parseResponse(matjson::Value const& response);

    // Turns decoration objects into a GD level object string ("1,id,2,x,3,y,...;").
    std::string toObjectString(std::vector<DecoObject> const& objects);
}
