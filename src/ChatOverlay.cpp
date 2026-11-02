#include "ChatOverlay.hpp"
#include "Brain.hpp"

using namespace geode::prelude;

static constexpr float kLineScale = 0.5f;
static constexpr float kPad = 4.f;
static constexpr char const* kFont = "chatFont.fnt";

std::string sanitizeForFont(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char ch : s) {
        if (ch == '\n' || ch == '\r' || ch == '\t') {
            out += ' ';
        } else if (ch >= 32 && ch < 127) {
            out += static_cast<char>(ch);
        }
        // anything else (emoji, accents) is dropped: the font has no glyphs for it
    }
    // collapse double spaces
    std::string res;
    for (char c : out) {
        if (c == ' ' && !res.empty() && res.back() == ' ') continue;
        res += c;
    }
    while (!res.empty() && res.back() == ' ') res.pop_back();
    if (res.size() > 200) res = res.substr(0, 200);
    return res;
}

ChatOverlay* ChatOverlay::create(float width, float height, int bgOpacity) {
    auto ret = new ChatOverlay();
    if (ret->init(width, height, bgOpacity)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ChatOverlay::init(float width, float height, int bgOpacity) {
    if (!CCNode::init()) return false;
    m_width = width;
    m_height = height;
    this->setContentSize({width, height});
    this->setAnchorPoint({0.f, 0.f});

    m_bg = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(std::clamp(bgOpacity, 0, 255))}, width, height);
    this->addChild(m_bg, 0);

    m_lineHolder = CCNode::create();
    this->addChild(m_lineHolder, 1);

    this->setID("chat-overlay"_spr);
    this->scheduleUpdate();
    return true;
}

void ChatOverlay::addMessage(std::string const& user, ccColor3B nameColor, std::string const& text, bool isMod) {
    auto cleanText = sanitizeForFont(text);
    if (cleanText.empty()) return;
    std::string prefix = (isMod ? "[MOD] " : "") + user;
    std::string full = prefix + ": " + cleanText;

    float wrapWidth = (m_width - 2 * kPad) / kLineScale;
    auto label = CCLabelBMFont::create(full.c_str(), kFont, wrapWidth, kCCTextAlignmentLeft);
    if (!label) return;
    label->setScale(kLineScale);
    label->setAnchorPoint({0.f, 0.f});

    // colour the username part. Each character is a child sprite tagged with its index
    // in the string; the username comes first and has no spaces, so wrapping can't shift it.
    size_t nameStart = isMod ? 6 : 0;
    for (size_t i = 0; i < prefix.size() + 1; i++) {
        if (auto spr = typeinfo_cast<CCSprite*>(label->getChildByTag(static_cast<int>(i)))) {
            if (isMod && i < 5) spr->setColor({0, 220, 90});
            else if (i >= nameStart) spr->setColor(nameColor);
        }
    }

    label->setOpacity(0);
    label->runAction(CCFadeIn::create(0.15f));
    m_lineHolder->addChild(label);
    m_lines.push_back(label);
    this->relayout();
}

void ChatOverlay::addSystem(std::string const& text) {
    this->addMessage("FakeChat", {150, 150, 150}, text, false);
}

void ChatOverlay::relayout() {
    float y = kPad;
    // newest at the bottom
    for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it) {
        auto node = *it;
        node->setPosition({kPad, y});
        y += node->getScaledContentSize().height + 1.f;
    }
    // drop lines that no longer fit
    while (!m_lines.empty()) {
        float total = kPad;
        for (auto n : m_lines) total += n->getScaledContentSize().height + 1.f;
        if (total <= m_height || m_lines.size() <= 1) break;
        m_lines.front()->removeFromParent();
        m_lines.pop_front();
    }
    if (!m_lines.empty()) {
        // re-run positions after trimming
        float yy = kPad;
        for (auto it = m_lines.rbegin(); it != m_lines.rend(); ++it) {
            (*it)->setPosition({kPad, yy});
            yy += (*it)->getScaledContentSize().height + 1.f;
        }
    }
}

void ChatOverlay::update(float dt) {
    Brain::get().update(dt, this);
}
