#include "DecoratorPopup.hpp"
#include "Theme.hpp"
#include "Quotes.hpp"
#include "TriggerPresets.hpp"

#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/UndoObject.hpp>
#include <Geode/binding/GameObject.hpp>
#include <cmath>
#include <map>
#include <functional>
#include <array>
#include <algorithm>

using namespace geode::prelude;

namespace {
    constexpr float W = 440.f, H = 300.f;
    // Preview area (in main layer coordinates).
    constexpr float PX = 16.f, PY = 102.f, PW = 408.f, PH = 152.f;
    constexpr float FONT_UP = 2.f;  // labels inside half-scaled rounded sprites are scaled up by this
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

DecoratorPopup* DecoratorPopup::create(std::vector<SelectedObject> selection, CCArray* nodes) {
    auto ret = new DecoratorPopup();
    if (ret->init(std::move(selection), nodes)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool DecoratorPopup::init(std::vector<SelectedObject> selection, CCArray* nodes) {
    if (!Popup::init(W, H)) return false;
    m_selection = std::move(selection);
    m_selectedNodes = CCArray::create();
    if (nodes) m_selectedNodes->addObjectsFromArray(nodes);

    // remember the last style the user picked
    m_groupIndex = static_cast<size_t>(Mod::get()->getSavedValue<int64_t>("style-group", 0));
    m_levelIndex = static_cast<size_t>(Mod::get()->getSavedValue<int64_t>("style-level", 0));
    m_style.level = Mod::get()->getSavedValue<bool>("style-is-level", false);
    if (m_groupIndex >= styles::groupCount()) m_groupIndex = 0;
    if (m_levelIndex >= styles::levelCount()) m_levelIndex = 0;
    m_style.index = m_style.level ? m_levelIndex : m_groupIndex;

    // Replace GD's default popup look with our own window.
    m_bgSprite->setVisible(false);
    m_closeBtn->setVisible(false);
    m_closeBtn->setEnabled(false);

    m_window = rounded(W, H, 2.5f);
    m_window->setPosition({W / 2, H / 2});
    m_mainLayer->addChild(m_window, -10);

    // ---- Top bar ----
    m_title = text("AI Decorator", .8f);
    m_title->setAnchorPoint({0, .5f});
    m_title->setPosition({PX + 2, H - 22});
    m_mainLayer->addChild(m_title);

    {
        std::map<std::string, int> counts;
        for (auto const& o : m_selection) {
            auto c = styles::hitboxClass(o);
            counts[c.empty() ? std::string("deco") : c]++;
        }
        std::string sub;
        for (auto const& [c, n] : counts) {
            auto nice = c; std::replace(nice.begin(), nice.end(), '_', ' ');
            sub += fmt::format("{}{} {}", sub.empty() ? "" : ", ", n, nice);
        }
        m_subtitle = text(sub.c_str(), .4f);
    }
    m_subtitle->setAnchorPoint({0, .5f});
    m_subtitle->setPosition({PX + 4 + m_title->getScaledContentWidth() + 6, H - 23});
    m_subtitle->limitLabelWidth(186 - (PX + 4 + m_title->getScaledContentWidth() + 6), .4f, .15f);
    m_mainLayer->addChild(m_subtitle);

    // Tabs
    auto tabDeco = roundedButton(64, 22, "Decorate", .42f, this, menu_selector(DecoratorPopup::onTabDecorate), &m_tabDecoBg, &m_tabDecoLabel);
    tabDeco->setPosition({220, H - 22});
    m_buttonMenu->addChild(tabDeco);
    auto tabTrig = roundedButton(64, 22, "Triggers", .42f, this, menu_selector(DecoratorPopup::onTabTriggers), &m_tabTrigBg, &m_tabTrigLabel);
    tabTrig->setPosition({288, H - 22});
    m_buttonMenu->addChild(tabTrig);
    auto tabOpt = roundedButton(64, 22, "Options", .42f, this, menu_selector(DecoratorPopup::onTabOptions), &m_tabOptBg, &m_tabOptLabel);
    tabOpt->setPosition({356, H - 22});
    m_buttonMenu->addChild(tabOpt);

    m_gear = CCSprite::createWithSpriteFrameName("accountBtn_settings_001.png");
    if (!m_gear) m_gear = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
    m_gear->setScale(.42f);
    m_gear->setOpacity(110);
    m_gear->setVisible(false);  // themes live in the Options tab now
    m_mainLayer->addChild(m_gear);

    auto closeBtn = roundedButton(24, 24, "X", .6f, this, menu_selector(DecoratorPopup::onClose), &m_closeBg, &m_closeLabel);
    closeBtn->setPosition({W - 24, H - 22});
    m_buttonMenu->addChild(closeBtn);

    // ---- Preview area ----
    m_previewBg = rounded(PW, PH);
    m_previewBg->setPosition({PX + PW / 2, PY + PH / 2});
    m_mainLayer->addChild(m_previewBg);

    auto stencil = CCLayerColor::create({255, 255, 255, 255}, PW - 6, PH - 6);
    m_clipper = CCClippingNode::create(stencil);
    m_clipper->setPosition({PX + 3, PY + 3});
    m_mainLayer->addChild(m_clipper);

    m_previewContent = CCNode::create();
    m_previewContent->setPosition({(PW - 6) / 2, (PH - 6) / 2});
    m_clipper->addChild(m_previewContent);

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

    // Working overlay: fades to black and cycles colors while decorating.
    m_overlay = CCLayerColor::create({0, 0, 0, 0}, PW - 6, PH - 6);
    m_clipper->addChild(m_overlay, 10);
    m_overlayGlow = CCLayerColor::create({255, 255, 255, 0}, PW - 6, PH - 6);
    m_clipper->addChild(m_overlayGlow, 11);

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
        m_clipper->addChild(n, 12);
    }

    // ---- Decorate tab: style browser row ----
    constexpr float rowY = 78.f;
    m_decorateRow = CCMenu::create();
    m_decorateRow->setPosition({0, 0});
    m_decorateRow->setContentSize({W, 60});
    m_mainLayer->addChild(m_decorateRow, 3);
    m_decorateRowLabels = CCNode::create();
    m_mainLayer->addChild(m_decorateRowLabels, 4);

    auto modeBtn = roundedButton(64, 34, "Types", .45f, this, menu_selector(DecoratorPopup::onToggleMode), &m_modeBg, &m_modeLabel);
    modeBtn->setPosition({PX + 32, rowY});
    m_decorateRow->addChild(modeBtn);

    auto prevBtn = roundedButton(26, 34, "<", .7f, this, menu_selector(DecoratorPopup::onPrevStyle), &m_prevBg, &m_prevLabel);
    prevBtn->setPosition({PX + 64 + 6 + 13, rowY});
    m_decorateRow->addChild(prevBtn);

    auto nextBtn = roundedButton(26, 34, ">", .7f, this, menu_selector(DecoratorPopup::onNextStyle), &m_nextBg, &m_nextLabel);
    nextBtn->setPosition({W - PX - 58 - 4 - 28 - 6 - 13, rowY});
    m_decorateRow->addChild(nextBtn);

    float nameLeft = PX + 64 + 6 + 26 + 8;
    m_swatch = CCDrawNode::create();
    m_swatch->setPosition({nameLeft + 6, rowY});
    m_decorateRowLabels->addChild(m_swatch);
    m_styleName = text("", .55f);
    m_styleName->setAnchorPoint({0, .5f});
    m_styleName->setPosition({nameLeft + 16, rowY + 7});
    m_decorateRowLabels->addChild(m_styleName);
    m_styleDesc = text("", .38f);
    m_styleDesc->setAnchorPoint({0, .5f});
    m_styleDesc->setPosition({nameLeft + 16, rowY - 8});
    m_decorateRowLabels->addChild(m_styleDesc);

    auto decoBtn = roundedButton(58, 34, "Use", .55f, this, menu_selector(DecoratorPopup::onDecorate), &m_decorateBg);
    decoBtn->setPosition({W - PX - 29, rowY});
    m_decorateRow->addChild(decoBtn);
    auto diceBtn = roundedButton(28, 34, "?", .7f, this, menu_selector(DecoratorPopup::onDice), &m_diceBg, &m_diceLabel);
    diceBtn->setPosition({W - PX - 58 - 4 - 14, rowY});
    m_decorateRow->addChild(diceBtn);

    // ---- Description row: merges every level's decorations, weighted by the words ----
    constexpr float inY = 32.f;
    m_inputRow = CCMenu::create();
    m_inputRow->setPosition({0, 0});
    m_mainLayer->addChild(m_inputRow, 3);
    float inW = W - 2 * PX - 52 - 6;
    m_inputBg = rounded(inW, 36);
    m_inputBg->setPosition({PX + inW / 2, inY});
    m_mainLayer->addChild(m_inputBg, 2);
    m_input = TextInput::create(inW - 16, "Describe it: dark red glow with chains, icy 3d, like Acheron...", "chatFont.fnt");
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->setMaxCharCount(200);
    m_input->setPosition({PX + inW / 2, inY});
    m_input->setString(Mod::get()->getSavedValue<std::string>("last-prompt", ""));
    m_mainLayer->addChild(m_input, 3);
    m_sendBg = rounded(52, 36);
    auto sendLbl = text("Go", .6f * FONT_UP);
    sendLbl->setPosition(m_sendBg->getContentSize() / 2);
    m_sendBg->addChild(sendLbl);
    auto sendBtn = CCMenuItemSpriteExtra::create(m_sendBg, this, menu_selector(DecoratorPopup::onSend));
    sendBtn->m_scaleMultiplier = 1.08f;
    sendBtn->setPosition({W - PX - 26, inY});
    m_inputRow->addChild(sendBtn);

    // ---- Triggers tab: 3x3 grid of presets over the preview area ----
    m_triggerGrid = CCMenu::create();
    m_triggerGrid->setPosition({0, 0});
    m_mainLayer->addChild(m_triggerGrid, 6);
    {
        constexpr float cw = 128.f, ch = 44.f, gap = 5.f;
        float x0 = PX + (PW - (3 * cw + 2 * gap)) / 2 + cw / 2;
        float y0 = PY + PH + 8 - ch / 2;
        for (size_t i = 0; i < triggers::PRESETS.size(); i++) {
            auto const& p = triggers::PRESETS[i];
            CCScale9Sprite* bg = nullptr;
            CCLabelBMFont* lbl = nullptr;
            auto btn = roundedButton(cw, ch, p.name, .55f, this, menu_selector(DecoratorPopup::onTrigger), &bg, &lbl);
            lbl->setPositionY(lbl->getPositionY() + 14);
            auto info = CCLabelBMFont::create(p.info, "chatFont.fnt", (cw - 10) * 2 / (.38f * FONT_UP), kCCTextAlignmentCenter);
            info->setScale(.38f * FONT_UP);
            info->setPosition({bg->getContentSize().width / 2, bg->getContentSize().height / 2 - 14});
            bg->addChild(info);
            btn->setTag(static_cast<int>(i));
            btn->setPosition({x0 + (i % 3) * (cw + gap), y0 - (i / 3) * (ch + gap)});
            m_triggerGrid->addChild(btn);
            m_triggerBgs.push_back(bg);
            m_triggerLabels.push_back(lbl);
            m_triggerLabels.push_back(info);
        }
    }

    m_status = text("", .45f);
    m_status->setAnchorPoint({0, 0});
    m_status->setPosition({PX + 10, PY + 8});
    m_mainLayer->addChild(m_status, 7);

    buildOptions();
    applyTheme();
    refreshStyle();
    setTab(false);
    buildPreview(false);
    this->schedule(schedule_selector(DecoratorPopup::updateWorking));
    return true;
}

void DecoratorPopup::applyTheme() {
    auto const& t = currentTheme();
    m_window->setColor(t.background);
    auto border = mix(t.panel, t.text, .25f);
    setBorder(m_window, mix(t.background, t.accent, .6f));
    for (CCNode* n : {static_cast<CCNode*>(m_previewBg), static_cast<CCNode*>(m_closeBg),
                      static_cast<CCNode*>(m_beforeBg), static_cast<CCNode*>(m_afterBg),
                      static_cast<CCNode*>(m_modeBg), static_cast<CCNode*>(m_prevBg), static_cast<CCNode*>(m_nextBg)}) {
        setBorder(n, border);
    }
    setBorder(m_applyBg, mix(t.accent, {255, 255, 255}, .35f));
    setBorder(m_decorateBg, mix(t.accent, {255, 255, 255}, .35f));
    setBorder(m_sendBg, mix(t.accent, {255, 255, 255}, .35f));
    m_sendBg->setColor(t.accent);
    m_inputBg->setColor(t.panel); setBorder(m_inputBg, border);
    m_diceBg->setColor(t.panel); setBorder(m_diceBg, border); m_diceLabel->setColor(t.text);
    m_previewBg->setColor(t.panel);
    m_closeBg->setColor(t.panel);
    m_modeBg->setColor(t.panel);
    m_prevBg->setColor(t.panel);
    m_nextBg->setColor(t.panel);
    m_applyBg->setColor(t.accent);
    m_decorateBg->setColor(t.accent);
    m_title->setColor(t.text);
    m_subtitle->setColor(t.subtext);
    m_previewHint->setColor(t.subtext);
    m_closeLabel->setColor(t.text);
    m_modeLabel->setColor(t.text);
    m_prevLabel->setColor(t.text);
    m_nextLabel->setColor(t.text);
    m_styleName->setColor(t.text);
    m_styleDesc->setColor(t.subtext);
    m_gear->setColor(t.text);
    for (auto bg : m_triggerBgs) { bg->setColor(t.panel); setBorder(bg, border); }
    for (size_t i = 0; i < m_triggerLabels.size(); i++) m_triggerLabels[i]->setColor(i % 2 ? t.subtext : t.text);

    m_beforeBg->setColor(m_showingAfter ? t.panel : t.accent);
    m_afterBg->setColor(m_showingAfter ? t.accent : t.panel);
    m_beforeLabel->setColor(t.text);
    m_afterLabel->setColor(t.text);
    m_tabDecoBg->setColor(m_tab == 0 ? t.accent : t.panel);
    m_tabTrigBg->setColor(m_tab == 1 ? t.accent : t.panel);
    m_tabOptBg->setColor(m_tab == 2 ? t.accent : t.panel);
    setBorder(m_tabOptBg, border);
    m_tabOptLabel->setColor(t.text);
    for (auto bg : m_optPanels) { bg->setColor(t.panel); setBorder(bg, border); }
    for (auto l : m_optTextLabels) l->setColor(t.text);
    for (auto l : m_optValueLabels) l->setColor(t.text);
    refreshOptions();
    setBorder(m_tabDecoBg, border);
    setBorder(m_tabTrigBg, border);
    m_tabDecoLabel->setColor(t.text);
    m_tabTrigLabel->setColor(t.text);
    if (!m_busy) m_status->setColor(t.subtext);
}

void DecoratorPopup::setTab(bool trig) { setTabIndex(trig ? 1 : 0); }

void DecoratorPopup::setTabIndex(int tab) {
    if (m_busy) return;
    m_tab = tab;
    m_triggersTab = tab == 1;
    bool deco = tab == 0, trig = tab == 1, opt = tab == 2;
    m_triggerGrid->setVisible(trig);
    m_triggerGrid->setEnabled(trig);
    m_optMenu->setVisible(opt);
    m_optMenu->setEnabled(opt);
    m_optLabels->setVisible(opt);
    m_decorateRow->setVisible(deco);
    m_decorateRow->setEnabled(deco);
    m_decorateRowLabels->setVisible(deco);
    m_inputRow->setVisible(deco);
    m_inputRow->setEnabled(deco);
    m_input->setVisible(deco);
    m_inputBg->setVisible(deco);
    m_previewBg->setVisible(!opt);
    m_clipper->setVisible(!opt);
    m_previewContent->setVisible(deco);
    m_previewHint->setVisible(deco);
    for (auto b : {m_beforeBtn, m_afterBtn, m_applyBtn}) b->setVisible(deco && m_hasResult);
    setStatus(trig ? "Adds triggers for the selected structure (it gets its own group)." :
              opt ? "" : "");
    m_status->setVisible(!opt);
    applyTheme();
}

void DecoratorPopup::onTabDecorate(CCObject*) { closePanel(); setTabIndex(0); }
void DecoratorPopup::onTabTriggers(CCObject*) { closePanel(); setTabIndex(1); }
void DecoratorPopup::onTabOptions(CCObject*) { closePanel(); setTabIndex(2); }

// ---------------- Options tab ----------------
// Toggles (tags 0-7), steppers (tags 100+ = minus, 200+ = plus), cycles for color mode / theme.
namespace {
    struct ToggleDef { char const* name; bool styles::Options::* field; };
    constexpr std::array<ToggleDef, 10> TOGGLES = {{
        {"Copy real deco", &styles::Options::copyReal},
        {"AI network", &styles::Options::useAI},
        {"Body fill", &styles::Options::body}, {"Edge glow", &styles::Options::glow}, {"Outlines", &styles::Options::lines},
        {"Spike/saw glow", &styles::Options::hazardGlow}, {"Particles", &styles::Options::particles},
        {"Background", &styles::Options::scenery}, {"Beat pulses", &styles::Options::pulses}, {"Color shift", &styles::Options::colorShift},
    }};
    constexpr std::array<char const*, 9> STEPPERS = {"Glow size", "Glow opacity", "Body opacity", "Hue shift", "Brightness", "Pulse every", "Colors", "Theme", "AI confidence"};
    constexpr std::array<char const*, 3> COLOR_MODES = {"Style", "Keep mine", "Mono"};
}

void DecoratorPopup::buildOptions() {
    m_optMenu = CCMenu::create();
    m_optMenu->setPosition({0, 0});
    m_mainLayer->addChild(m_optMenu, 8);
    m_optLabels = CCNode::create();
    m_mainLayer->addChild(m_optLabels, 9);

    // toggles: 4 x 2
    constexpr float tw = 78.f, th = 26.f, tg = 4.f;
    float x0 = W / 2 - (5 * tw + 4 * tg) / 2 + tw / 2;
    for (size_t i = 0; i < TOGGLES.size(); i++) {
        CCScale9Sprite* bg = nullptr;
        CCLabelBMFont* lbl = nullptr;
        auto btn = roundedButton(tw, th, TOGGLES[i].name, .36f, this, menu_selector(DecoratorPopup::onOptToggle), &bg, &lbl);
        btn->setTag(static_cast<int>(i));
        btn->setPosition({x0 + (i % 5) * (tw + tg), H - 58 - (i / 5) * (th + tg)});
        m_optMenu->addChild(btn);
        m_optToggleBgs.push_back(bg);
        m_optTextLabels.push_back(lbl);
    }
    // steppers: 2 columns x 4 rows: [label | - value +]
    constexpr float rowH = 26.f, colW = 200.f;
    for (size_t i = 0; i < STEPPERS.size(); i++) {
        float cx = W / 2 - colW / 2 - 4 + (i % 2) * (colW + 8);
        float cy = H - 126 - (i / 2) * (rowH + 4);
        auto panel = rounded(colW, rowH);
        panel->setPosition({cx, cy});
        m_optLabels->addChild(panel, -1);
        m_optPanels.push_back(panel);
        auto name = text(STEPPERS[i], .42f);
        name->setAnchorPoint({0, .5f});
        name->setPosition({cx - colW / 2 + 10, cy});
        m_optLabels->addChild(name);
        m_optTextLabels.push_back(name);
        auto val = text("", .42f);
        val->setPosition({cx + colW / 2 - 52, cy});
        m_optLabels->addChild(val);
        m_optValueLabels.push_back(val);
        CCScale9Sprite* mb = nullptr; CCScale9Sprite* pb = nullptr;
        CCLabelBMFont* ml = nullptr; CCLabelBMFont* pl = nullptr;
        auto minus = roundedButton(22, 22, "-", .6f, this, menu_selector(DecoratorPopup::onOptStep), &mb, &ml);
        minus->setTag(100 + static_cast<int>(i));
        minus->setPosition({cx + colW / 2 - 90, cy});
        m_optMenu->addChild(minus);
        auto plus = roundedButton(22, 22, "+", .6f, this, menu_selector(DecoratorPopup::onOptStep), &pb, &pl);
        plus->setTag(200 + static_cast<int>(i));
        plus->setPosition({cx + colW / 2 - 14, cy});
        m_optMenu->addChild(plus);
        m_optPanels.push_back(mb); m_optPanels.push_back(pb);
        m_optTextLabels.push_back(ml); m_optTextLabels.push_back(pl);
    }
    CCScale9Sprite* rb = nullptr; CCLabelBMFont* rl = nullptr;
    auto reset = roundedButton(110, 24, "Reset options", .42f, this, menu_selector(DecoratorPopup::onOptReset), &rb, &rl);
    reset->setPosition({W / 2 + 104, H - 126 - 4 * 30.f});
    m_optMenu->addChild(reset);
    m_optPanels.push_back(rb); m_optTextLabels.push_back(rl);
    m_optMenu->setVisible(false);
    m_optMenu->setEnabled(false);
    m_optLabels->setVisible(false);
}

void DecoratorPopup::refreshOptions() {
    if (!m_optMenu) return;
    auto const& o = styles::options();
    auto const& t = currentTheme();
    for (size_t i = 0; i < TOGGLES.size() && i < m_optToggleBgs.size(); i++) {
        bool on = o.*(TOGGLES[i].field);
        m_optToggleBgs[i]->setColor(on ? t.accent : t.panel);
        setBorder(m_optToggleBgs[i], on ? mix(t.accent, {255, 255, 255}, .35f) : mix(t.panel, t.text, .25f));
        m_optTextLabels[i]->setOpacity(on ? 255 : 140);
    }
    std::array<std::string, 9> v = {
        fmt::format("{:.0f}%", o.glowScale * 100), fmt::format("{:.0f}%", o.glowOpacity * 100), fmt::format("{:.0f}%", o.bodyOpacity * 100),
        fmt::format("{}{}", o.hue > 0 ? "+" : "", o.hue), fmt::format("{:.0f}%", o.brightness * 100),
        o.pulseBlocks == 0 ? std::string("Auto") : fmt::format("{} blocks", o.pulseBlocks), COLOR_MODES[o.colorMode % 3], currentTheme().name,
        fmt::format("{:.0f}%", o.aiConfidence * 100)};
    for (size_t i = 0; i < v.size() && i < m_optValueLabels.size(); i++) {
        m_optValueLabels[i]->setString(v[i].c_str());
        m_optValueLabels[i]->limitLabelWidth(52.f, .42f, .2f);
    }
}

void DecoratorPopup::optionsChanged() {
    styles::saveOptions();
    refreshOptions();
    // Rebuild the current result with the new options so the preview stays up to date.
    if (m_hasResult && m_lastGen) {
        auto res = m_lastGen();
        if (res) {
            auto r = std::move(res).unwrap();
            m_result = std::move(r.objects);
            m_resultColors = std::move(r.colors);
            m_resultPulse = r.pulse;
            m_resultShift = r.shift;
        }
    }
}

void DecoratorPopup::onOptToggle(CCObject* sender) {
    auto i = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
    if (i >= TOGGLES.size()) return;
    auto& o = styles::options();
    o.*(TOGGLES[i].field) = !(o.*(TOGGLES[i].field));
    optionsChanged();
}

void DecoratorPopup::onOptStep(CCObject* sender) {
    int tag = static_cast<CCNode*>(sender)->getTag();
    int dir = tag >= 200 ? 1 : -1;
    int i = tag % 100;
    auto& o = styles::options();
    auto step = [&](float& v, float d, float lo, float hi) { v = std::clamp(std::round((v + d * dir) * 100.f) / 100.f, lo, hi); };
    switch (i) {
        case 0: step(o.glowScale, .1f, .3f, 2.5f); break;
        case 1: step(o.glowOpacity, .1f, .1f, 1.f); break;
        case 2: step(o.bodyOpacity, .1f, .1f, 1.f); break;
        case 3: o.hue = ((o.hue + dir * 30 + 180) % 360 + 360) % 360 - 180; break;
        case 4: step(o.brightness, .1f, .3f, 1.5f); break;
        case 5: o.pulseBlocks = std::clamp(o.pulseBlocks + dir, 0, 16); break;
        case 6: o.colorMode = (o.colorMode + dir + 3) % 3; break;
        case 7: setTheme((currentThemeIndex() + THEMES.size() + dir) % THEMES.size()); applyTheme(); break;
        case 8: step(o.aiConfidence, .05f, .05f, .9f); break;
    }
    optionsChanged();
}

void DecoratorPopup::onOptReset(CCObject*) {
    styles::options() = styles::Options{};
    optionsChanged();
}

void DecoratorPopup::refreshStyle() {
    m_style.index = m_style.level ? m_levelIndex : m_groupIndex;
    m_modeLabel->setString(m_style.level ? "Levels" : "Types");
    auto total = m_style.level ? styles::levelCount() : styles::groupCount();
    m_styleName->setString(styles::name(m_style).c_str());
    m_styleName->limitLabelWidth(160.f, .55f, .2f);
    m_styleDesc->setString(fmt::format("{}  ({}/{})", styles::description(m_style), m_style.index + 1, total).c_str());
    m_styleDesc->limitLabelWidth(160.f, .38f, .2f);
    auto c = styles::swatch(m_style);
    m_swatch->clear();
    m_swatch->drawDot({0, 0}, 5.f, {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f});

    auto mod = Mod::get();
    mod->setSavedValue<int64_t>("style-group", static_cast<int64_t>(m_groupIndex));
    mod->setSavedValue<int64_t>("style-level", static_cast<int64_t>(m_levelIndex));
    mod->setSavedValue<bool>("style-is-level", m_style.level);
}

void DecoratorPopup::onPrevStyle(CCObject*) {
    if (m_busy) return;
    auto& idx = m_style.level ? m_levelIndex : m_groupIndex;
    auto total = m_style.level ? styles::levelCount() : styles::groupCount();
    if (total == 0) return;
    idx = (idx + total - 1) % total;
    refreshStyle();
}

void DecoratorPopup::onNextStyle(CCObject*) {
    if (m_busy) return;
    auto& idx = m_style.level ? m_levelIndex : m_groupIndex;
    auto total = m_style.level ? styles::levelCount() : styles::groupCount();
    if (total == 0) return;
    idx = (idx + 1) % total;
    refreshStyle();
}

void DecoratorPopup::onToggleMode(CCObject*) {
    if (m_busy) return;
    m_style.level = !m_style.level;
    refreshStyle();
}

void DecoratorPopup::buildPreview(bool after) {
    m_showingAfter = after;
    m_previewContent->removeAllChildren();

    struct P { int id; float x, y, rot, sx, sy; bool fx, fy; int z; };
    std::vector<P> items;
    for (auto const& o : m_selection) items.push_back({o.id, o.x, o.y, o.rotation, o.scaleX, o.scaleY, o.flipX, o.flipY, 0});
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

    // Show the style's colors on the preview too.
    std::map<int, ccColor3B> chColors;
    for (auto const& c : m_resultColors) chColors[c.channel] = c.color;

    for (size_t i = 0; i < items.size(); i++) {
        auto const& p = items[i];
        auto obj = GameObject::createWithKey(p.id);
        if (!obj) continue;
        obj->setPosition({(p.x - cx) * scale, (p.y - cy) * scale});
        obj->setRotation(p.rot);
        obj->setScaleX(p.sx * scale * (p.fx ? -1 : 1));
        obj->setScaleY(p.sy * scale * (p.fy ? -1 : 1));
        if (after && i >= m_selection.size()) {
            auto const& d = m_result[i - m_selection.size()];
            if (auto it = chColors.find(d.mainColor); it != chColors.end()) obj->setColor(it->second);
        }
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
}

void DecoratorPopup::onDismissPanel(CCObject*) { closePanel(); }

void DecoratorPopup::addPanelClose(float x, float y) {
    auto const& t = currentTheme();
    CCScale9Sprite* bg = nullptr;
    CCLabelBMFont* lbl = nullptr;
    auto btn = roundedButton(22, 22, "X", .55f, this, menu_selector(DecoratorPopup::onDismissPanel), &bg, &lbl);
    bg->setColor(t.background);
    lbl->setColor(t.text);
    btn->setPosition({x, y});
    m_buttonMenu->addChild(btn, 60);
    m_panelNodes.push_back(btn);
}

void DecoratorPopup::onGear(CCObject* sender) {
    bool wasOpen = !m_panelNodes.empty();
    closePanel();
    if (wasOpen && sender) return;  // gear toggles the picker
    auto const& t = currentTheme();

    auto bg = rounded(300, 150);
    bg->setColor(t.panel);
    bg->setPosition({W / 2, H / 2});
    m_buttonMenu->addChild(bg, 51);
    m_panelNodes.push_back(bg);

    addPanelClose(W / 2 + 150 - 18, H / 2 + 75 - 16);

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

    // Beat pulses on the glow color across the structure, timed like the source level.
    if (styles::options().pulses && m_resultPulse.channel > 0) {
        float minX = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (auto const& d : m_result) { minX = std::min(minX, d.x); maxX = std::max(maxX, d.x); maxY = std::max(maxY, d.y); }
        auto const& p = m_resultPulse;
        std::string trig;
        // At most 16 pulses; spread them out on long selections.
        float gap = std::max(p.gap, (maxX - minX) / 16.f);
        for (float x = minX - 30.f; x <= maxX; x += gap) {
            trig += fmt::format("1,1006,2,{},3,{},51,{},52,0,45,{},46,{},47,{},7,{},8,{},9,{};",
                decorator::fmtNum(x), decorator::fmtNum(maxY + 90.f), p.channel,
                decorator::fmtNum(p.fadeIn), decorator::fmtNum(p.hold), decorator::fmtNum(p.fadeOut),
                static_cast<int>(p.color.r), static_cast<int>(p.color.g), static_cast<int>(p.color.b));
        }
        if (!trig.empty()) {
            if (auto pulses = editor->createObjectsFromString(gd::string(trig), true, true)) {
                created->addObjectsFromArray(pulses);
            }
        }
    }

    // Color shift: the glow changes color in the middle of the structure and back at the end.
    if (m_resultShift.enabled && m_resultShift.channel > 0) {
        float minX = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (auto const& d : m_result) { minX = std::min(minX, d.x); maxX = std::max(maxX, d.x); maxY = std::max(maxY, d.y); }
        auto const& c = m_resultShift;
        auto colorTrig = [&](float x, ccColor3B col) {
            return fmt::format("1,899,2,{},3,{},23,{},7,{},8,{},9,{},10,{},17,1,35,1;", decorator::fmtNum(x), decorator::fmtNum(maxY + 120.f),
                c.channel, static_cast<int>(col.r), static_cast<int>(col.g), static_cast<int>(col.b), decorator::fmtNum(c.duration));
        };
        auto trig = colorTrig((minX + maxX) / 2, c.to) + colorTrig(maxX + 30.f, c.back);
        if (auto shifts = editor->createObjectsFromString(gd::string(trig), true, true)) created->addObjectsFromArray(shifts);
    }

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

// ---------------- Triggers ----------------

void DecoratorPopup::onTrigger(CCObject* sender) {
    if (m_busy) return;
    auto idx = static_cast<size_t>(static_cast<CCNode*>(sender)->getTag());
    auto res = triggers::add(idx, m_selectedNodes, m_triggerGroup);
    if (!res) {
        setStatus(res.unwrapErr(), true);
        return;
    }
    m_triggerGroup = res.unwrap();
    setStatus(fmt::format("Added {} (group {}). Triggers are just left of your structure.",
        triggers::PRESETS[idx].name, m_triggerGroup));
}

// ---------------- Working animation ----------------

void DecoratorPopup::startWorking() {
    m_busy = true;
    m_elapsed = 0.f;
    m_quoteTimer = 0.f;
    m_progress = 0.f;
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

void DecoratorPopup::showResult(std::string const& status) {
    m_hasResult = true;
    stopWorking(true);
    buildPreview(true);
    for (auto b : {m_beforeBtn, m_afterBtn, m_applyBtn}) b->setVisible(!m_triggersTab);
    setStatus(status);
}

void DecoratorPopup::runGeneration(Result<styles::Result> res, std::string const& label) {
    if (!res) {
        setStatus(res.unwrapErr(), true);
        return;
    }
    // Generation is instant; play a short version of the working animation anyway.
    m_progressTau = .35f;
    startWorking();
    auto r = std::move(res).unwrap();
    this->runAction(CCSequence::create(
        CCDelayTime::create(1.1f),
        CallFuncExt::create([self = Ref<DecoratorPopup>(this), r = std::move(r), label]() mutable {
            if (self->m_closed) return;
            self->m_result = std::move(r.objects);
            self->m_resultColors = std::move(r.colors);
            self->m_resultPulse = r.pulse;
            self->m_resultShift = r.shift;
            self->showResult(fmt::format("{} (from {}): {} objects on {}. Press again for the next variation.",
                label, r.source, self->m_result.size(), r.summary));
        }),
        nullptr
    ));
}

int DecoratorPopup::nextVariant(std::string const& key) {
    if (key != m_lastKey) { m_lastKey = key; m_variant = 0; }
    else m_variant++;
    return m_variant;
}

void DecoratorPopup::onDecorate(CCObject*) {
    if (m_busy) return;
    closePanel();
    auto key = fmt::format("style:{}:{}", m_style.level, m_style.index);
    int v = nextVariant(key);
    auto ref = m_style;
    m_lastGen = [this, ref, v] { return styles::generate(ref, m_selection, v); };
    runGeneration(m_lastGen(), styles::name(m_style));
}

void DecoratorPopup::onSend(CCObject*) {
    if (m_busy) return;
    closePanel();
    std::string prompt = m_input->getString();
    Mod::get()->setSavedValue<std::string>("last-prompt", prompt);
    int v = nextVariant("prompt:" + prompt);
    m_lastGen = [this, prompt, v] { return styles::generateFromPrompt(prompt, m_selection, v); };
    runGeneration(m_lastGen(), prompt.empty() ? std::string("All levels") : std::string("Your description"));
}

void DecoratorPopup::onDice(CCObject*) {
    if (m_busy) return;
    auto total = m_style.level ? styles::levelCount() : styles::groupCount();
    if (total == 0) return;
    auto& idx = m_style.level ? m_levelIndex : m_groupIndex;
    idx = static_cast<size_t>(rand()) % total;
    refreshStyle();
    onDecorate(nullptr);
}
