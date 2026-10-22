#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

#include "DecoratorPopup.hpp"

using namespace geode::prelude;

class $modify(AIDecoratorEditorUI, EditorUI) {
    bool init(LevelEditorLayer* editorLayer) {
        if (!EditorUI::init(editorLayer)) return false;

        // Round "AI" button, drawn so it doesn't depend on any game sprite.
        constexpr float R = 20.f;
        auto bg = CCNode::create();
        bg->setContentSize({R * 2, R * 2});
        auto draw = CCDrawNode::create();
        draw->drawCircle({R, R}, R - 1.5f, {0.13f, 0.14f, 0.20f, 1.f}, 2.5f, {0.85f, 0.88f, 0.95f, 1.f}, 48);
        bg->addChild(draw);
        auto label = CCLabelBMFont::create("AI", "bigFont.fnt");
        label->setScale(.55f);
        label->setPosition({R, R + 1});
        bg->addChild(label);

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
        if (auto popup = DecoratorPopup::create(std::move(selection), this->getSelectedObjects())) {
            popup->show();
        }
    }
};
