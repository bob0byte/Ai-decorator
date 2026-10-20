#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "DecoratorPopup.hpp"

using namespace geode::prelude;

class $modify(AIDecoratorEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        auto menu = this->getChildByID("editor-buttons-menu");
        if (!menu) {
            log::warn("AI Decorator: editor-buttons-menu not found, button not added");
            return true;
        }

        auto spr = ButtonSprite::create("AI", 20, true, "bigFont.fnt", "GJ_button_04.png", 30.f, .6f);
        spr->setScale(.85f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(AIDecoratorEditorUI::onAIDecorate)
        );
        btn->setID("ai-decorate-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();

        return true;
    }

    void onAIDecorate(CCObject*) {
        auto selection = decorator::collectSelection(this);
        if (selection.empty()) {
            FLAlertLayer::create(
                "AI Decorator",
                "Select the structure you want decorated first, then press the <cy>AI</c> button.",
                "OK"
            )->show();
            return;
        }
        if (auto popup = DecoratorPopup::create(std::move(selection))) {
            popup->show();
        }
    }
};
