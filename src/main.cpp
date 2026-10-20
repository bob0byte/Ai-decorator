#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "DecoratorPopup.hpp"

using namespace geode::prelude;

class $modify(AIDecoratorEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        // Square "AI" button, same size as the round editor buttons.
        auto bg = CCScale9Sprite::create("GJ_button_04.png");
        bg->setContentSize({40.f, 40.f});
        auto label = CCLabelBMFont::create("AI", "bigFont.fnt");
        label->setScale(.6f);
        bg->addChildAtPosition(label, Anchor::Center, ccp(0, 1));

        auto btn = CCMenuItemSpriteExtra::create(
            bg, this, menu_selector(AIDecoratorEditorUI::onAIDecorate)
        );
        btn->setID("ai-decorate-button"_spr);

        // Own menu, placed just right of the music playtest button.
        auto menu = CCMenu::create();
        menu->setID("ai-decorator-menu"_spr);
        menu->setContentSize({0, 0});
        menu->addChild(btn);
        this->addChild(menu);

        auto winSize = CCDirector::get()->getWinSize();
        CCPoint pos = ccp(95.f, winSize.height / 2 + 75.f);  // fallback
        if (auto playback = this->getChildByID("playback-menu")) {
            if (auto music = playback->getChildByID("music-playback-button")) {
                auto world = playback->convertToWorldSpace(music->getPosition());
                pos = ccp(world.x + music->getScaledContentWidth() / 2 + 32.f, world.y);
            }
        }
        menu->setPosition(pos);

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
