#pragma once

#include <Geode/Geode.hpp>
#include "Decorator.hpp"

class DecoratorPopup : public geode::Popup {
protected:
    std::vector<SelectedObject> m_selection;
    geode::TextInput* m_input = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_generateBtn = nullptr;
    bool m_busy = false;

    bool init(std::vector<SelectedObject> selection);
    void onGenerate(cocos2d::CCObject*);
    void onExample(cocos2d::CCObject*);
    void setStatus(std::string const& text, cocos2d::ccColor3B color = {255, 255, 255});
    void setBusy(bool busy);
    void onResponse(geode::utils::web::WebResponse res);

public:
    static DecoratorPopup* create(std::vector<SelectedObject> selection);
};
