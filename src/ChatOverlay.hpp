#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <string>

// The chat box drawn on top of the level. Messages stack from the bottom up,
// oldest ones scroll off the top.
class ChatOverlay : public cocos2d::CCNode {
public:
    static ChatOverlay* create(float width, float height, int bgOpacity);

    void addMessage(std::string const& user, cocos2d::ccColor3B nameColor, std::string const& text, bool isMod = false);
    void addSystem(std::string const& text);

    void update(float dt) override;

protected:
    bool init(float width, float height, int bgOpacity);
    void relayout();

    float m_width = 0.f;
    float m_height = 0.f;
    cocos2d::CCLayerColor* m_bg = nullptr;
    cocos2d::CCNode* m_lineHolder = nullptr;
    std::deque<cocos2d::CCNode*> m_lines;
};

// Strip characters the bitmap chat font can't draw (emoji etc.).
std::string sanitizeForFont(std::string const& s);
