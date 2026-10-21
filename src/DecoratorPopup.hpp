#pragma once

#include <Geode/Geode.hpp>
#include "Decorator.hpp"
#include "StyleDecorator.hpp"

class DecoratorPopup : public geode::Popup {
protected:
    std::vector<SelectedObject> m_selection;
    geode::Ref<cocos2d::CCArray> m_selectedNodes;   // the actual selected objects (for triggers)
    std::vector<DecoObject> m_result;
    std::vector<styles::ChannelColor> m_resultColors;
    styles::PulseFx m_resultPulse;
    int m_variant = 0;          // how many times the same style/description was used in a row
    std::string m_lastKey;      // which style/description that was
    float m_progressTau = .35f;  // how fast the progress estimate climbs
    bool m_hasResult = false;
    bool m_showingAfter = false;
    bool m_busy = false;
    bool m_closed = false;

    // Style browser
    styles::StyleRef m_style;
    size_t m_groupIndex = 0, m_levelIndex = 0;
    int m_triggerGroup = 0;
    bool m_triggersTab = false;

    // Themed nodes (recolored by applyTheme).
    cocos2d::extension::CCScale9Sprite* m_window = nullptr;
    cocos2d::extension::CCScale9Sprite* m_previewBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_closeBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_applyBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_beforeBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_afterBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_decorateBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_modeBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_prevBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_nextBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_tabDecoBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_tabTrigBg = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_subtitle = nullptr;
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_closeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_beforeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_afterLabel = nullptr;
    cocos2d::CCLabelBMFont* m_styleName = nullptr;
    cocos2d::CCLabelBMFont* m_styleDesc = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_prevLabel = nullptr;
    cocos2d::CCLabelBMFont* m_nextLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tabDecoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_tabTrigLabel = nullptr;
    cocos2d::CCDrawNode* m_swatch = nullptr;
    cocos2d::CCSprite* m_gear = nullptr;

    // Preview area.
    cocos2d::CCNode* m_previewContent = nullptr;
    cocos2d::CCClippingNode* m_clipper = nullptr;
    CCMenuItemSpriteExtra* m_applyBtn = nullptr;
    CCMenuItemSpriteExtra* m_beforeBtn = nullptr;
    CCMenuItemSpriteExtra* m_afterBtn = nullptr;

    // Description box (merges all levels' decorations).
    geode::TextInput* m_input = nullptr;
    cocos2d::extension::CCScale9Sprite* m_inputBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_sendBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_diceBg = nullptr;
    cocos2d::CCLabelBMFont* m_diceLabel = nullptr;
    cocos2d::CCMenu* m_inputRow = nullptr;

    // Decorate tab bottom row, and the triggers tab grid.
    cocos2d::CCMenu* m_decorateRow = nullptr;
    cocos2d::CCNode* m_decorateRowLabels = nullptr;
    cocos2d::CCMenu* m_triggerGrid = nullptr;
    std::vector<cocos2d::extension::CCScale9Sprite*> m_triggerBgs;
    std::vector<cocos2d::CCLabelBMFont*> m_triggerLabels;

    // Working overlay.
    cocos2d::CCLayerColor* m_overlay = nullptr;
    cocos2d::CCLayerColor* m_overlayGlow = nullptr;
    cocos2d::CCLabelBMFont* m_workingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_quoteLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    cocos2d::CCLayerColor* m_progressBar = nullptr;
    float m_elapsed = 0.f;
    float m_quoteTimer = 0.f;
    float m_progress = 0.f;
    size_t m_lastQuote = 0;

    // Nodes of the currently open overlay panel (theme picker).
    std::vector<geode::Ref<cocos2d::CCNode>> m_panelNodes;
    void closePanel();
    void addPanelClose(float x, float y);

    bool init(std::vector<SelectedObject> selection, cocos2d::CCArray* nodes);
    void onClose(cocos2d::CCObject*) override;

    void applyTheme();
    void buildPreview(bool after);
    void setStatus(std::string const& text, bool error = false);
    void refreshStyle();
    void setTab(bool triggers);

    void onDecorate(cocos2d::CCObject*);
    void onSend(cocos2d::CCObject*);
    void onDice(cocos2d::CCObject*);
    void runGeneration(geode::Result<styles::Result> res, std::string const& label);
    int nextVariant(std::string const& key);
    void onPrevStyle(cocos2d::CCObject*);
    void onNextStyle(cocos2d::CCObject*);
    void onToggleMode(cocos2d::CCObject*);
    void onTabDecorate(cocos2d::CCObject*);
    void onTabTriggers(cocos2d::CCObject*);
    void onTrigger(cocos2d::CCObject*);
    void showResult(std::string const& status);
    void onApply(cocos2d::CCObject*);
    void onBefore(cocos2d::CCObject*);
    void onAfter(cocos2d::CCObject*);
    void onGear(cocos2d::CCObject*);
    void onPickTheme(cocos2d::CCObject*);
    void onDismissPanel(cocos2d::CCObject*);

    void startWorking();
    void stopWorking(bool success);
    void updateWorking(float dt);

public:
    static DecoratorPopup* create(std::vector<SelectedObject> selection, cocos2d::CCArray* nodes);
};
