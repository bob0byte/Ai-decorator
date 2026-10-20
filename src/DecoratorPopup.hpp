#pragma once

#include <Geode/Geode.hpp>
#include "Decorator.hpp"
#include "StyleDecorator.hpp"

class DecoratorPopup : public geode::Popup {
protected:
    std::vector<SelectedObject> m_selection;
    std::vector<DecoObject> m_result;
    std::vector<styles::ChannelColor> m_resultColors;
    float m_progressTau = 16.f;  // how fast the estimated progress climbs
    bool m_hasResult = false;
    bool m_showingAfter = false;
    bool m_busy = false;
    bool m_closed = false;

    // Themed nodes (recolored by applyTheme).
    cocos2d::extension::CCScale9Sprite* m_window = nullptr;
    cocos2d::extension::CCScale9Sprite* m_previewBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_inputBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_sendBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_expandBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_closeBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_applyBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_beforeBg = nullptr;
    cocos2d::extension::CCScale9Sprite* m_afterBg = nullptr;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_subtitle = nullptr;
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_expandLabel = nullptr;
    cocos2d::CCLabelBMFont* m_closeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_beforeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_afterLabel = nullptr;
    cocos2d::CCSprite* m_gear = nullptr;

    // Preview area.
    cocos2d::CCNode* m_previewContent = nullptr;
    cocos2d::CCMenu* m_previewMenu = nullptr;
    CCMenuItemSpriteExtra* m_applyBtn = nullptr;
    CCMenuItemSpriteExtra* m_beforeBtn = nullptr;
    CCMenuItemSpriteExtra* m_afterBtn = nullptr;

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

    // Input row.
    geode::TextInput* m_input = nullptr;
    CCMenuItemSpriteExtra* m_sendBtn = nullptr;
    cocos2d::CCSprite* m_sendIcon = nullptr;

    // Nodes of the currently open overlay panel (expand / theme picker).
    std::vector<geode::Ref<cocos2d::CCNode>> m_panelNodes;
    void closePanel();
    void addPanelClose(float x, float y);

    bool init(std::vector<SelectedObject> selection);
    void onClose(cocos2d::CCObject*) override;

    void applyTheme();
    void buildPreview(bool after);
    void setStatus(std::string const& text, bool error = false);

    void onSend(cocos2d::CCObject*);
    void onStyle(cocos2d::CCObject*);
    void showResult(std::string const& status);
    void onApply(cocos2d::CCObject*);
    void onBefore(cocos2d::CCObject*);
    void onAfter(cocos2d::CCObject*);
    void onExpand(cocos2d::CCObject*);
    void onGear(cocos2d::CCObject*);
    void onPickTheme(cocos2d::CCObject*);
    void onDismissPanel(cocos2d::CCObject*);

    void startWorking();
    void stopWorking(bool success);
    void updateWorking(float dt);
    void onResponse(geode::utils::web::WebResponse res);

public:
    static DecoratorPopup* create(std::vector<SelectedObject> selection);
};
