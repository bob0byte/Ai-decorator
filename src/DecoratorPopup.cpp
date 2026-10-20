#include "DecoratorPopup.hpp"
#include "Theme.hpp"
#include "Quotes.hpp"

#include <Geode/utils/web.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/UndoObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <cmath>

using namespace geode::prelude;

namespace {
    constexpr float W = 440.f, H = 300.f;
    // Preview area (in main layer coordinates).
    constexpr float PX = 16.f, PY = 98.f, PW = 408.f, PH = 156.f;
    constexpr float FONT_UP = 2.f;  // labels inside half-scaled rounded sprites are scaled up by this

    // Rounded rectangle. Built at 2x and scaled down so corners stay small and crisp.
    constexpr int BORDER_TAG = 7301;

    // Rounded rectangle with a border. Built at 2x and scaled down so corners stay small and crisp.
    // The border is a slightly bigger copy drawn behind (negative z).
    CCScale9Sprite* rounded(float w, float h, float border = 1.5f) {
        auto spr = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
        spr->setContentSize({w * 2, h * 2});
        spr->setScale(.5f);
        if (border > 0) {
            auto b = CCScale9Sprite::create("square02b_001.png", {0, 0, 80, 80});
            b->setContentSize({w * 2 + border * 4, h * 2 + border * 4});
            b->setPosition({w, h});
            b->setColor({90, 90, 110});
            b->setTag(BORDER_TAG);
            spr->addChild(b, -1);
        }
        return spr;
    }

    void setBorder(CCNode* spr, ccColor3B c) {
        if (auto b = static_cast<CCScale9Sprite*>(spr->getChildByTag(BORDER_TAG))) b->setColor(c);
    }

    CCLabelBMFont* text(char const* str, float scale) {
        auto lbl = CCLabelBMFont::create(str, "chatFont.fnt");
        lbl->setScale(scale);
        return lbl;
    }

    // A rounded button with a centered label. Returns the item; bg and label via out params.
    CCMenuItemSpriteExtra* roundedButton(
        float w, float h, char const* label, float labelScale, CCObject* target, SEL_MenuHandler sel,
        CCScale9Sprite** outBg, CCLabelBMFont** outLabel = nullptr
    ) {
        auto bg = rounded(w, h);
        if (label) {
            auto lbl = text(label, labelScale * FONT_UP);
            lbl->setPosition(bg->getContentSize() / 2);
            bg->addChild(lbl);
            if (outLabel) *outLabel = lbl;
        }
        auto btn = CCMenuItemSpriteExtra::create(bg, target, sel);
        btn->m_scaleMultiplier = 1.06f;
        if (outBg) *outBg = bg;
        return btn;
    }

    ccColor3B mix(ccColor3B a, ccColor3B b, float t) {
        return {
            static_cast<GLubyte>(a.r + (b.r - a.r) * t),
            static_cast<GLubyte>(a.g + (b.g - a.g) * t),
            static_cast<GLubyte>(a.b + (b.b - a.b) * t),
        };
    }
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
    if (!Popup::init(W, H)) return false;
    m_selection = std::move(selection);

    // Replace GD's default popup look with our own window.
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_closeBtn->setEnabled(false);

    m_window = rounded(W, H, 2.5f);
    m_window->setPosition({W / 2, H / 2});
    m_mainLayer->addChild(m_window, -10);

    // ---- Top bar ----
    m_title = text("AI Decorator", .85f);
    m_title->setAnchorPoint({0, .5f});
    m_title->setPosition({PX + 2, H - 22});
    m_mainLayer->addChild(m_title);

    m_subtitle = text(fmt::format("{} object{} selected",
        m_selection.size(), m_selection.size() == 1 ? "" : "s").c_str(), .5f);
    m_subtitle->setAnchorPoint({0, .5f});
    m_subtitle->setPosition({PX + 4 + m_title->getScaledContentWidth() + 8, H - 23});
    m_mainLayer->addChild(m_subtitle);

    m_gear = CCSprite::createWithSpriteFrameName("accountBtn_settings_001.png");
    if (!m_gear) m_gear = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    m_gear->setScale(.42f);
    m_gear->setOpacity(110);
    auto gearBtn = CCMenuItemSpriteExtra::create(m_gear, this, menu_selector(DecoratorPopup::onGear));
    gearBtn->setPosition({W - 54, H - 22});
    m_buttonMenu->addChild(gearBtn);

    auto closeBtn = roundedButton(24, 24, "X", .6f, this, menu_selector(DecoratorPopup::onClose), &m_closeBg, &m_closeLabel);
    closeBtn->setPosition({W - 24, H - 22});
    m_buttonMenu->addChild(closeBtn);

    // ---- Preview area ----
    m_previewBg = rounded(PW, PH);
    m_previewBg->setPosition({PX + PW / 2, PY + PH / 2});
    m_mainLayer->addChild(m_previewBg);

    auto stencil = CCLayerColor::create({255, 255, 255, 255}, PW - 6, PH - 6);
    auto clipper = CCClippingNode::create(stencil);
    clipper->setPosition({PX + 3, PY + 3});
    m_mainLayer->addChild(clipper);

    m_previewContent = CCNode::create();
    m_previewContent->setPosition({(PW - 6) / 2, (PH - 6) / 2});
    clipper->addChild(m_previewContent);

    m_previewHint = text("Your structure", .45f);
    m_previewHint->setAnchorPoint({1, 0});
    m_previewHint->setPosition({PX + PW - 10, PY + 8});
    m_mainLayer->addChild(m_previewHint, 2);

    // Before / After tabs and Apply (shown once there is a result).
    m_beforeBtn = roundedButton(60, 20, "Before", .45f, this, menu_selector(DecoratorPopup::onBefore), &m_beforeBg, &m_beforeLabel);
    m_beforeBtn->setPosition({PX + 38, PY + PH - 16});
    m_afterBtn = roundedButton(60, 20, "After", .45f, this, menu_selector(DecoratorPopup::onAfter), &m_afterBg, &m_afterLabel);
    m_afterBtn->setPosition({PX + 102, PY + PH - 16});
    m_applyBtn = roundedButton(84, 24, "Apply", .55f, this, menu_selector(DecoratorPopup::onApply), &m_applyBg);
    m_applyBtn->setPosition({PX + PW - 50, PY + PH - 18});
    for (auto b : {m_beforeBtn, m_afterBtn, m_applyBtn}) {
        b->setVisible(false);
        m_buttonMenu->addChild(b, 5);
    }

    // Working overlay: fades to black and cycles colors while the AI runs.
    m_overlay = CCLayerColor::create({0, 0, 0, 0}, PW - 6, PH - 6);
    clipper->addChild(m_overlay, 10);
    m_overlayGlow = CCLayerColor::create({255, 255, 255, 0}, PW - 6, PH - 6);
    clipper->addChild(m_overlayGlow, 11);

    m_workingLabel = text("Working...", 1.1f);
    m_workingLabel->setPosition({(PW - 6) / 2, (PH - 6) / 2 + 22});
    m_quoteLabel = text("", .55f);
    m_quoteLabel->setPosition({(PW - 6) / 2, (PH - 6) / 2 - 4});
    m_percentLabel = text("0%", .6f);
    m_percentLabel->setPosition({(PW - 6) / 2, (PH - 6) / 2 - 30});
    m_progressBar = CCLayerColor::create({255, 255, 255, 220}, 0, 3);
    m_progressBar->setPosition({(PW - 6) / 2 - 100, (PH - 6) / 2 - 44});
    for (CCNode* n : {static_cast<CCNode*>(m_workingLabel), static_cast<CCNode*>(m_quoteLabel),
                      static_cast<CCNode*>(m_percentLabel), static_cast<CCNode*>(m_progressBar)}) {
        n->setVisible(false);
        clipper->addChild(n, 12);
    }

    // ---- Style row (offline, no AI) ----
    {
        constexpr float pillW = 62.f, gap = 4.f, rowY2 = 76.f;
        auto n = styles::STYLES.size();
        float startX = W / 2 - (n * pillW + (n - 1) * gap) / 2 + pillW / 2;
        for (size_t i = 0; i < n; i++) {
            CCScale9Sprite* pillBg = nullptr;
            CCLabelBMFont* pillLbl = nullptr;
            auto pill = roundedButton(pillW, 22, styles::STYLES[i].name, .45f, this,
                menu_selector(DecoratorPopup::onStyle), &pillBg, &pillLbl);
            pillBg->setColor(mix({30, 30, 36}, styles::STYLES[i].swatch, .35f));
            pillLbl->setColor(mix({255, 255, 255}, styles::STYLES[i].swatch, .35f));
            setBorder(pillBg, styles::STYLES[i].swatch);
            pill->setTag(static_cast<int>(i));
            pill->setPosition({startX + i * (pillW + gap), rowY2});
            m_buttonMenu->addChild(pill);
        }
    }

    // ---- Input row ----
    constexpr float rowY = 34.f, inputW = 316.f;
    m_inputBg = rounded(inputW, 36);
    m_inputBg->setPosition({PX + inputW / 2, rowY});
    m_mainLayer->addChild(m_inputBg);

    m_input = TextInput::create(inputW - 16, "Or describe a look for the AI...", "chatFont.fnt");
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(600);
    m_input->setPosition({PX + inputW / 2, rowY});
    m_mainLayer->addChild(m_input);

    auto expandBtn = roundedButton(36, 36, "...", .7f, this, menu_selector(DecoratorPopup::onExpand), &m_expandBg, &m_expandLabel);
    expandBtn->setPosition({PX + inputW + 8 + 18, rowY});
    m_buttonMenu->addChild(expandBtn);

    m_sendBg = rounded(40, 36);
    m_sendIcon = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    m_sendIcon->setFlipX(true);
    m_sendIcon->setScale(.55f * FONT_UP);
    m_sendIcon->setPosition(m_sendBg->getContentSize() / 2);
    m_sendBg->addChild(m_sendIcon);
    m_sendBtn = CCMenuItemSpriteExtra::create(m_sendBg, this, menu_selector(DecoratorPopup::onSend));
    m_sendBtn->m_scaleMultiplier = 1.08f;
    m_sendBtn->setPosition({W - PX - 20, rowY});
    m_buttonMenu->addChild(m_sendBtn);

    m_status = text("", .45f);
    m_status->setAnchorPoint({0, 0});
    m_status->setPosition({PX + 10, PY + 8});
    m_mainLayer->addChild(m_status, 2);

    applyTheme();
    buildPreview(false);
    this->schedule(schedule_selector(DecoratorPopup::updateWorking));
    return true;
}

void DecoratorPopup::applyTheme() {
    auto const& t = currentTheme();
    m_window->setColor(t.background);
    auto border = mix(t.panel, t.text, .25f);
    setBorder(m_window, mix(t.background, t.accent, .6f));
    for (CCNode* n : {static_cast<CCNode*>(m_previewBg), static_cast<CCNode*>(m_inputBg),
                      static_cast<CCNode*>(m_expandBg), static_cast<CCNode*>(m_closeBg),
                      static_cast<CCNode*>(m_beforeBg), static_cast<CCNode*>(m_afterBg)}) {
        setBorder(n, border);
    }
    setBorder(m_sendBg, mix(t.accent, {255, 255, 255}, .35f));
    setBorder(m_applyBg, mix(t.accent, {255, 255, 255}, .35f));
    m_previewBg->setColor(t.panel);
    m_inputBg->setColor(t.panel);
    m_expandBg->setColor(t.panel);
    m_closeBg->setColor(t.panel);
    m_sendBg->setColor(t.accent);
    m_applyBg->setColor(t.accent);
    m_title->setColor(t.text);
    m_subtitle->setColor(t.subtext);
    m_previewHint->setColor(t.subtext);
    m_expandLabel->setColor(t.text);
    m_closeLabel->setColor(t.text);
    m_gear->setColor(t.text);

    m_beforeBg->setColor(m_showingAfter ? t.panel : t.accent);
    m_afterBg->setColor(m_showingAfter ? t.accent : t.panel);
    m_beforeLabel->setColor(t.text);
    m_afterLabel->setColor(t.text);
    if (!m_busy) m_status->setColor(t.subtext);
}

void DecoratorPopup::buildPreview(bool after) {
    m_showingAfter = after;
    m_previewContent->removeAllChildren();

    struct P { int id; float x, y, rot, sx, sy; bool fx, fy; int z; };
    std::vector<P> items;
    for (auto const& o : m_selection) items.push_back({o.id, o.x, o.y, o.rotation, o.scaleX, o.scaleY, false, false, 0});
    if (after) {
        for (auto const& d : m_result) {
            items.push_back({d.id, d.x, d.y, d.rotation, d.scale * d.scaleX, d.scale * d.scaleY, d.flipX, d.flipY, d.zLayer * 1000 + d.zOrder});
        }
    }
    if (items.empty()) return;

    float minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
    for (auto const& p : items) {
        minX = std::min(minX, p.x - 15); maxX = std::max(maxX, p.x + 15);
        minY = std::min(minY, p.y - 15); maxY = std::max(maxY, p.y + 15);
    }
    float cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
    float scale = std::min((PW - 40) / (maxX - minX), (PH - 50) / (maxY - minY));
    scale = std::min(scale, 1.5f);

    for (auto const& p : items) {
        auto obj = GameObject::createWithKey(p.id);
        if (!obj) continue;
        obj->setPosition({(p.x - cx) * scale, (p.y - cy) * scale});
        obj->setRotation(p.rot);
        obj->setScaleX(p.sx * scale);
        obj->setScaleY(p.sy * scale);
        obj->setFlipX(p.fx);
        obj->setFlipY(p.fy);
        m_previewContent->addChild(obj, p.z);
    }

    m_previewHint->setString(after ? "After" : "Your structure");

    auto const& t = currentTheme();
    m_beforeBg->setColor(after ? t.panel : t.accent);
    m_afterBg->setColor(after ? t.accent : t.panel);
}

void DecoratorPopup::setStatus(std::string const& str, bool error) {
    m_status->setString(str.c_str());
    m_status->setColor(error ? ccColor3B{255, 110, 110} : currentTheme().subtext);
    m_status->limitLabelWidth(PW - 140, .45f, .25f);
}

// ---------------- Panels ----------------

void DecoratorPopup::closePanel() {
    for (auto& n : m_panelNodes) n->removeFromParent();
    m_panelNodes.clear();
    m_input->setEnabled(!m_busy);
}

void DecoratorPopup::onDismissPanel(CCObject*) { closePanel(); }

void DecoratorPopup::onExpand(CCObject*) {
    closePanel();
    m_input->setEnabled(false);
    auto const& t = currentTheme();

    // Full-window catcher so tapping anywhere closes the panel.
    auto catcher = CCMenuItem::create(this, menu_selector(DecoratorPopup::onDismissPanel));
    catcher->setContentSize({W, H});
    catcher->setPosition({W / 2, H / 2});
    m_buttonMenu->addChild(catcher, 50);
    m_panelNodes.push_back(catcher);

    auto bg = rounded(W - 40, H - 60);
    bg->setColor(t.panel);
    bg->setPosition({W / 2, H / 2});
    m_buttonMenu->addChild(bg, 51);
    m_panelNodes.push_back(bg);

    auto header = text("Your description", .6f);
    header->setColor(t.text);
    header->setPosition({W / 2, H - 50});
    m_buttonMenu->addChild(header, 52);
    m_panelNodes.push_back(header);

    std::string str = m_input->getString();
    if (str.empty()) str = "(nothing typed yet)";
    auto body = CCLabelBMFont::create(str.c_str(), "chatFont.fnt", (W - 80) / .55f, kCCTextAlignmentLeft);
    body->setScale(.55f);
    body->setColor(t.subtext);
    body->setAnchorPoint({.5f, 1});
    body->setPosition({W / 2, H - 66});
    if (body->getScaledContentHeight() > H - 110) body->setScale(.55f * (H - 110) / body->getScaledContentHeight());
    m_buttonMenu->addChild(body, 52);
    m_panelNodes.push_back(body);

    auto hint = text("Tap anywhere to close", .4f);
    hint->setColor(t.subtext);
    hint->setPosition({W / 2, 44});
    m_buttonMenu->addChild(hint, 52);
    m_panelNodes.push_back(hint);
}

void DecoratorPopup::onGear(CCObject*) {
    closePanel();
    m_input->setEnabled(false);
    auto const& t = currentTheme();

    auto catcher = CCMenuItem::create(this, menu_selector(DecoratorPopup::onDismissPanel));
    catcher->setContentSize({W, H});
    catcher->setPosition({W / 2, H / 2});
    m_buttonMenu->addChild(catcher, 50);
    m_panelNodes.push_back(catcher);

    auto bg = rounded(300, 150);
    bg->setColor(t.panel);
    bg->setPosition({W / 2, H / 2});
    m_buttonMenu->addChild(bg, 51);
    m_panelNodes.push_back(bg);

    auto header = text("Theme", .7f);
    header->setColor(t.text);
    header->setPosition({W / 2, H / 2 + 52});
    m_buttonMenu->addChild(header, 52);
    m_panelNodes.push_back(header);

    for (size_t i = 0; i < THEMES.size(); i++) {
        auto const& th = THEMES[i];
        CCScale9Sprite* swatchBg = nullptr;
        CCLabelBMFont* lbl = nullptr;
        auto btn = roundedButton(84, 40, th.name, .5f, this, menu_selector(DecoratorPopup::onPickTheme), &swatchBg, &lbl);
        swatchBg->setColor(th.background);
        lbl->setColor(th.text);
        lbl->setPositionY(lbl->getPositionY() + 10);

        // Accent stripe so each theme is recognizable at a glance.
        auto stripe = rounded(56, 6, 0);
        stripe->setColor(th.accent);
        stripe->setScale(1.f);  // parent is already half scale
        stripe->setPosition({swatchBg->getContentSize().width / 2, 18});
        swatchBg->addChild(stripe);

        if (i == currentThemeIndex()) {
            auto ring = rounded(90, 46, 0);
            ring->setColor(t.accent);
            ring->setPosition({W / 2 - 92 + (i % 3) * 92.f, H / 2 + 14 - (i / 3) * 52.f});
            m_buttonMenu->addChild(ring, 52);
            m_panelNodes.push_back(ring);
        }

        btn->setTag(static_cast<int>(i));
        btn->setPosition({W / 2 - 92 + (i % 3) * 92.f, H / 2 + 14 - (i / 3) * 52.f});
        m_buttonMenu->addChild(btn, 53);
        m_panelNodes.push_back(btn);
    }
}

void DecoratorPopup::onPickTheme(CCObject* sender) {
    setTheme(static_cast<size_t>(static_cast<CCNode*>(sender)->getTag()));
    applyTheme();
    onGear(nullptr);  // rebuild picker with new highlight/colors
}

// ---------------- Tabs / apply ----------------

void DecoratorPopup::onBefore(CCObject*) { if (!m_busy) buildPreview(false); }
void DecoratorPopup::onAfter(CCObject*) { if (!m_busy && m_hasResult) buildPreview(true); }

void DecoratorPopup::onApply(CCObject*) {
    if (m_busy || !m_hasResult) return;

    auto editor = LevelEditorLayer::get();
    if (!editor || !editor->m_editorUI) return;

    auto created = editor->createObjectsFromString(gd::string(decorator::toObjectString(m_result)), true, true);
    if (!created || created->count() == 0) {
        setStatus("The objects couldn't be placed.", true);
        return;
    }

    if (!m_resultColors.empty()) styles::applyColors(m_resultColors);

    // One undo step for the whole decoration, then select it so it's easy to tweak or delete.
    editor->addToUndoList(UndoObject::createWithArray(created, UndoCommand::Paste), false);
    editor->m_editorUI->deselectAll();
    editor->m_editorUI->selectObjects(created, true);
    editor->m_editorUI->updateButtons();

    auto msg = fmt::format("Placed {} decoration objects", created->count());
    Notification::create(msg, NotificationIcon::Success)->show();
    this->onClose(nullptr);
}

void DecoratorPopup::onClose(CCObject* sender) {
    m_closed = true;
    Popup::onClose(sender);
}

// ---------------- Generation ----------------

void DecoratorPopup::onSend(CCObject*) {
    if (m_busy) return;
    closePanel();

    std::string prompt = m_input->getString();
    if (prompt.empty()) {
        setStatus("Type a description first.", true);
        return;
    }
    auto apiKey = Mod::get()->getSettingValue<std::string>("api-key");
    if (apiKey.empty()) {
        setStatus("Typing needs a free Gemini key (mod settings). Tap a style above to decorate offline!", true);
        return;
    }
    auto model = Mod::get()->getSettingValue<std::string>("model");

    m_progressTau = 16.f;
    startWorking();

    auto req = web::WebRequest()
        .header("x-goog-api-key", apiKey)
        .header("content-type", "application/json")
        .timeout(std::chrono::seconds(180))
        .bodyJSON(decorator::buildRequest(m_selection, prompt));

    // Keep the popup alive until the response lands so the callback can update it.
    async::spawn(
        req.post(fmt::format("https://generativelanguage.googleapis.com/v1beta/models/{}:generateContent", model)),
        [self = Ref<DecoratorPopup>(this)](web::WebResponse res) {
            if (!self->m_closed) self->onResponse(std::move(res));
        }
    );
}

void DecoratorPopup::startWorking() {
    m_busy = true;
    m_elapsed = 0.f;
    m_quoteTimer = 0.f;
    m_progress = 0.f;
    m_input->setEnabled(false);
    m_sendBtn->setEnabled(false);
    m_sendBg->setOpacity(140);
    setStatus("");
    for (auto b : {m_beforeBtn, m_afterBtn, m_applyBtn}) b->setVisible(false);

    auto const& t = currentTheme();
    m_overlay->stopAllActions();
    m_overlay->runAction(CCFadeTo::create(.6f, 240));

    // Slow color cycle over the black.
    m_overlayGlow->stopAllActions();
    m_overlayGlow->setColor(t.accent);
    m_overlayGlow->runAction(CCFadeTo::create(.6f, 60));
    m_overlayGlow->runAction(CCRepeatForever::create(CCSequence::create(
        CCTintTo::create(1.4f, 255, 70, 160),
        CCTintTo::create(1.4f, 60, 200, 255),
        CCTintTo::create(1.4f, 255, 200, 60),
        CCTintTo::create(1.4f, t.accent.r, t.accent.g, t.accent.b),
        nullptr
    )));

    m_workingLabel->stopAllActions();
    m_workingLabel->setColor({255, 255, 255});
    m_workingLabel->runAction(CCRepeatForever::create(CCSequence::create(
        CCTintTo::create(1.4f, 255, 150, 210),
        CCTintTo::create(1.4f, 150, 225, 255),
        CCTintTo::create(1.4f, 255, 230, 150),
        CCTintTo::create(1.4f, 255, 255, 255),
        nullptr
    )));

    m_lastQuote = static_cast<size_t>(rand()) % WORKING_QUOTES.size();
    m_quoteLabel->setString(WORKING_QUOTES[m_lastQuote]);
    m_quoteLabel->setColor({200, 200, 210});
    m_quoteLabel->setOpacity(255);
    m_percentLabel->setString("0%");
    m_percentLabel->setColor({255, 255, 255});
    m_progressBar->setContentSize({0, 3});

    for (CCNode* n : {static_cast<CCNode*>(m_workingLabel), static_cast<CCNode*>(m_quoteLabel),
                      static_cast<CCNode*>(m_percentLabel), static_cast<CCNode*>(m_progressBar)}) {
        n->setVisible(true);
    }
}

void DecoratorPopup::updateWorking(float dt) {
    if (!m_busy) return;
    m_elapsed += dt;

    // The API doesn't report progress, so this is an estimate that eases toward 95%
    // and only reaches 100% when the response actually arrives.
    m_progress = std::max(m_progress, 95.f * (1.f - std::exp(-m_elapsed / m_progressTau)));
    m_percentLabel->setString(fmt::format("{}%", static_cast<int>(m_progress)).c_str());
    m_progressBar->setContentSize({200.f * m_progress / 100.f, 3});

    m_quoteTimer += dt;
    if (m_quoteTimer >= 2.6f) {
        m_quoteTimer = 0.f;
        size_t next = static_cast<size_t>(rand()) % WORKING_QUOTES.size();
        if (next == m_lastQuote) next = (next + 1) % WORKING_QUOTES.size();
        m_lastQuote = next;
        m_quoteLabel->stopAllActions();
        m_quoteLabel->runAction(CCSequence::create(
            CCFadeTo::create(.2f, 0),
            CallFuncExt::create([lbl = m_quoteLabel, next] { lbl->setString(WORKING_QUOTES[next]); }),
            CCFadeTo::create(.2f, 255),
            nullptr
        ));
    }
}

void DecoratorPopup::stopWorking(bool success) {
    m_busy = false;
    m_input->setEnabled(true);
    m_sendBtn->setEnabled(true);
    m_sendBg->setOpacity(255);

    if (success) {
        m_percentLabel->setString("100%");
        m_progressBar->setContentSize({200, 3});
        m_workingLabel->stopAllActions();
        m_workingLabel->setString("Done!");
    }

    auto hide = [this] {
        for (CCNode* n : {static_cast<CCNode*>(m_workingLabel), static_cast<CCNode*>(m_quoteLabel),
                          static_cast<CCNode*>(m_percentLabel), static_cast<CCNode*>(m_progressBar)}) {
            n->setVisible(false);
        }
        m_workingLabel->setString("Working...");
        m_overlayGlow->stopAllActions();
        m_overlayGlow->runAction(CCFadeTo::create(.4f, 0));
        m_overlay->stopAllActions();
        m_overlay->runAction(CCFadeTo::create(.4f, 0));
    };

    if (success) {
        this->runAction(CCSequence::create(CCDelayTime::create(.6f), CallFuncExt::create(hide), nullptr));
    } else {
        hide();
    }
}

void DecoratorPopup::onResponse(web::WebResponse res) {
    if (res.code() == 429) {
        stopWorking(false);
        setStatus("Free-tier limit reached. Wait a bit and try again.", true);
        return;
    }

    auto json = res.json();
    if (!json) {
        stopWorking(false);
        setStatus(fmt::format("Request failed (HTTP {}).", res.code()), true);
        log::error("AI Decorator: bad response ({}): {}", res.code(), res.string().unwrapOr(""));
        return;
    }

    auto parsed = decorator::parseResponse(json.unwrap());
    if (!parsed) {
        stopWorking(false);
        setStatus(parsed.unwrapErr(), true);
        log::error("AI Decorator: {}", parsed.unwrapErr());
        return;
    }

    m_result = std::move(parsed).unwrap();
    m_resultColors.clear();
    showResult(fmt::format("{} objects ready. Press Apply to place them, or send again to retry.", m_result.size()));
}

void DecoratorPopup::showResult(std::string const& status) {
    m_hasResult = true;
    stopWorking(true);
    buildPreview(true);
    for (auto b : {m_beforeBtn, m_afterBtn, m_applyBtn}) b->setVisible(true);
    setStatus(status);
}

void DecoratorPopup::onStyle(CCObject* sender) {
    if (m_busy) return;
    closePanel();
    auto idx = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());

    auto res = styles::generate(idx, m_selection);
    if (!res) {
        setStatus(res.unwrapErr(), true);
        return;
    }

    // Offline styles are instant; play a short version of the working animation anyway.
    m_progressTau = .35f;
    startWorking();
    auto r = std::move(res).unwrap();
    auto name = styles::STYLES[idx].name;
    this->runAction(CCSequence::create(
        CCDelayTime::create(1.1f),
        CallFuncExt::create([self = Ref<DecoratorPopup>(this), r = std::move(r), name]() mutable {
            if (self->m_closed) return;
            self->m_result = std::move(r.objects);
            self->m_resultColors = std::move(r.colors);
            self->showResult(fmt::format("{} style: {} objects. Tap the style again for a new variation.", name, self->m_result.size()));
        }),
        nullptr
    ));
}
