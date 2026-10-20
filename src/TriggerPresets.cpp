#include "TriggerPresets.hpp"
#include "Decorator.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/UndoObject.hpp>

using namespace geode::prelude;
using decorator::fmtNum;

namespace {
    std::string trig(int id, float x, float y, std::string const& props) {
        return fmt::format("1,{},2,{},3,{},{};", id, fmtNum(x), fmtNum(y), props);
    }
}

Result<int> triggers::add(size_t preset, CCArray* selection, int group) {
    auto editor = LevelEditorLayer::get();
    if (!editor || !editor->m_editorUI) return Err("Open the level editor first.");
    if (!selection || selection->count() == 0) return Err("Select the structure first.");
    if (preset >= PRESETS.size()) return Err("Unknown preset.");

    // Put the selection in its own group (once per popup).
    if (group <= 0) {
        group = editor->getNextFreeGroupID({});
        for (auto obj : CCArrayExt<GameObject*>(selection)) {
            if (obj->addToGroup(group) == 1) editor->addToGroup(obj, group, false);
        }
    }

    float minX = INFINITY, maxY = -INFINITY;
    for (auto obj : CCArrayExt<GameObject*>(selection)) {
        minX = std::min(minX, obj->getPositionX());
        maxY = std::max(maxY, obj->getPositionY());
    }
    // Triggers sit just left of the structure so they fire as the player reaches it.
    float x = minX - 45.f, y = maxY + 60.f + static_cast<float>(preset) * 30.f;
    auto g = fmt::format("51,{}", group);
    std::string s;
    switch (preset) {
        case 0: // Pulse (group): fade in 0, hold 0.1, fade out 0.3, white
            s = trig(1006, x, y, g + ",52,1,45,0,46,0.1,47,0.3,7,255,8,255,9,255"); break;
        case 1: // Fade In: hide early, then alpha 1 over 0.5s
            s = trig(1007, x - 300.f, y, g + ",10,0,35,0") + trig(1007, x, y, g + ",10,0.5,35,1"); break;
        case 2: // Fade Out
            s = trig(1007, x, y, g + ",10,0.5,35,0"); break;
        case 3: // Slide Up: move y +30 over 0.5s, Ease In Out (easing 1), rate 2
            s = trig(901, x, y, g + ",28,0,29,30,10,0.5,30,1,85,2"); break;
        case 4: // Slide Down
            s = trig(901, x, y, g + ",28,0,29,-30,10,0.5,30,1,85,2"); break;
        case 5: // Spin: rotate 90 degrees around its own group over 1s
            s = trig(1346, x, y, g + fmt::format(",71,{},68,90,69,0,10,1,30,1,85,2", group)); break;
        case 6: // Pop: scale 1.3x with Elastic Out (6), then divide back 0.5s later
            s = trig(2067, x, y, g + fmt::format(",71,{},150,1.3,151,1.3,10,0.3,30,6,85,2", group))
              + trig(2067, x + 150.f, y, g + fmt::format(",71,{},150,1.3,151,1.3,153,1,154,1,10,0.3,30,1,85,2", group)); break;
        case 7: // Shake (screen)
            s = trig(1520, x, y, "75,2,84,0.01,10,0.2"); break;
        case 8: // Hide: toggle the group off
            s = trig(1049, x, y, g + ",56,0"); break;
    }

    auto created = editor->createObjectsFromString(gd::string(s), true, true);
    if (!created || created->count() == 0) return Err("Couldn't place the trigger.");
    editor->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);
    return Ok(group);
}
