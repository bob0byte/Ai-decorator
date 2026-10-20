#include "DecoratorPopup.hpp"

#include <Geode/utils/web.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/UndoObject.hpp>

using namespace geode::prelude;

namespace {
    constexpr std::array EXAMPLES = {
        "Glowing neon outlines, dark background",
        "Mossy stone ruins with vines",
        "Clean modern style with soft gradients",
        "Fire and lava, hot colors",
    };
}

DecoratorPopup* DecoratorPopup::create(std::vector<SelectedObject> selection) {
    auto ret = new DecoratorPopup();
    if (ret->init(std::move(selection))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool DecoratorPopup::init(std::vector<SelectedObject> selection) {
    if (!Popup::init(400.f, 250.f)) return false;
    m_selection = std::move(selection);

    this->setTitle("AI Decorator");

    auto info = CCLabelBMFont::create(
        fmt::format("{} object{} selected. Describe the look you want:",
            m_selection.size(), m_selection.size() == 1 ? "" : "s").c_str(),
        "chatFont.fnt"
    );
    info->setScale(.75f);
    m_mainLayer->addChildAtPosition(info, Anchor::Top, ccp(0, -48));

    m_input = TextInput::create(350.f, "e.g. spooky castle with purple glow", "chatFont.fnt");
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(400);
    m_mainLayer->addChildAtPosition(m_input, Anchor::Center, ccp(0, 22));

    // Quick example prompts.
    auto examples = CCMenu::create();
    examples->setContentSize({370.f, 50.f});
    examples->setLayout(RowLayout::create()->setGap(6.f)->setGrowCrossAxis(true)->setCrossAxisOverflow(false));
    for (size_t i = 0; i < EXAMPLES.size(); i++) {
        auto spr = ButtonSprite::create(EXAMPLES[i], "chatFont.fnt", "GJ_button_04.png", .6f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(DecoratorPopup::onExample));
        btn->setTag(static_cast<int>(i));
        examples->addChild(btn);
    }
    examples->updateLayout();
    m_mainLayer->addChildAtPosition(examples, Anchor::Center, ccp(0, -22));

    m_status = CCLabelBMFont::create("", "chatFont.fnt");
    m_status->setScale(.7f);
    m_mainLayer->addChildAtPosition(m_status, Anchor::Bottom, ccp(0, 58));

    auto genSpr = ButtonSprite::create("Generate", "goldFont.fnt", "GJ_button_01.png", .8f);
    m_generateBtn = CCMenuItemSpriteExtra::create(genSpr, this, menu_selector(DecoratorPopup::onGenerate));
    m_buttonMenu->addChildAtPosition(m_generateBtn, Anchor::Bottom, ccp(0, 28));

    return true;
}

void DecoratorPopup::onExample(CCObject* sender) {
    auto idx = static_cast<CCNode*>(sender)->getTag();
    if (idx >= 0 && idx < static_cast<int>(EXAMPLES.size())) {
        m_input->setString(EXAMPLES[idx]);
    }
}

void DecoratorPopup::setStatus(std::string const& text, ccColor3B color) {
    m_status->setString(text.c_str());
    m_status->setColor(color);
    m_status->limitLabelWidth(370.f, .7f, .3f);
}

void DecoratorPopup::setBusy(bool busy) {
    m_busy = busy;
    m_generateBtn->setEnabled(!busy);
    m_generateBtn->setOpacity(busy ? 120 : 255);
}

void DecoratorPopup::onGenerate(CCObject*) {
    if (m_busy) return;

    std::string prompt = m_input->getString();
    if (prompt.empty()) {
        setStatus("Type a description first.", {255, 120, 120});
        return;
    }

    auto apiKey = Mod::get()->getSettingValue<std::string>("api-key");
    if (apiKey.empty()) {
        setStatus("Add your Anthropic API key in the mod settings first.", {255, 120, 120});
        return;
    }

    setBusy(true);
    setStatus("Decorating... this can take up to a minute.", {255, 220, 120});

    auto body = decorator::buildRequest(m_selection, prompt);

    auto req = web::WebRequest()
        .header("x-api-key", apiKey)
        .header("anthropic-version", "2023-06-01")
        .header("content-type", "application/json")
        .timeout(std::chrono::seconds(180))
        .bodyJSON(body);

    // Keep the popup alive until the response lands so the callback can update it.
    async::spawn(
        req.post("https://api.anthropic.com/v1/messages"),
        [self = Ref<DecoratorPopup>(this)](web::WebResponse res) {
            self->onResponse(std::move(res));
        }
    );
}

void DecoratorPopup::onResponse(web::WebResponse res) {
    setBusy(false);

    auto json = res.json();
    if (!json) {
        setStatus(fmt::format("Request failed (HTTP {}).", res.code()), {255, 120, 120});
        log::error("AI Decorator: bad response ({}): {}", res.code(), res.string().unwrapOr(""));
        return;
    }

    auto parsed = decorator::parseResponse(json.unwrap());
    if (!parsed) {
        setStatus(parsed.unwrapErr(), {255, 120, 120});
        log::error("AI Decorator: {}", parsed.unwrapErr());
        return;
    }
    auto objects = parsed.unwrap();

    auto editor = LevelEditorLayer::get();
    if (!editor || !editor->m_editorUI) {
        // The user left the editor while we were waiting.
        return;
    }

    auto created = editor->createObjectsFromString(gd::string(decorator::toObjectString(objects)), true, true);
    if (!created || created->count() == 0) {
        setStatus("The AI's objects couldn't be placed.", {255, 120, 120});
        return;
    }

    // One undo step for the whole decoration, then select it so it's easy to tweak or delete.
    editor->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);
    editor->m_editorUI->deselectAll();
    editor->m_editorUI->selectObjects(created, true);
    editor->m_editorUI->updateButtons();

    auto msg = fmt::format("Placed {} decoration objects", created->count());
    Notification::create(msg, NotificationIcon::Success)->show();

    if (this->getParent()) {
        this->onClose(nullptr);
    }
}
