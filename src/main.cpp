#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Brain.hpp"
#include "ChatOverlay.hpp"

using namespace geode::prelude;

namespace {

std::string difficultyName(GJGameLevel* level) {
    if (level->m_demon.value() > 0) {
        switch (level->m_demonDifficulty) {
            case 3: return "Easy Demon";
            case 4: return "Medium Demon";
            case 5: return "Insane Demon";
            case 6: return "Extreme Demon";
            default: return "Hard Demon";
        }
    }
    switch (level->m_stars.value()) {
        case 0: return "Unrated";
        case 1: return "Auto";
        case 2: return "Easy";
        case 3: return "Normal";
        case 4: case 5: return "Hard";
        case 6: case 7: return "Harder";
        case 8: case 9: return "Insane";
        default: return "Demon";
    }
}

std::string lengthName(int len) {
    switch (len) {
        case 0: return "Tiny";
        case 1: return "Short";
        case 2: return "Medium";
        case 3: return "Long";
        case 4: return "XL";
        case 5: return "Platformer";
        default: return "Unknown";
    }
}

} // namespace

class $modify(ChatPlayLayer, PlayLayer) {
    struct Fields {
        Ref<ChatOverlay> overlay;
        bool deathReported = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        if (!Mod::get()->getSettingValue<bool>("enabled") || !level) return true;

        LevelInfo info;
        info.id = level->m_levelID.value();
        info.name = level->m_levelName;
        info.creator = level->m_creatorName;
        info.stars = level->m_stars.value();
        info.difficulty = difficultyName(level);
        info.length = lengthName(level->m_levelLength);
        info.likes = level->m_likes;
        info.downloads = level->m_downloads;
        info.bestPercent = level->m_normalPercent.value();
        info.totalAttempts = level->m_attempts.value();
        info.platformer = this->m_isPlatformer;
        info.rated = info.stars > 0;
        Brain::get().startSession(std::move(info));

        auto win = CCDirector::get()->getWinSize();
        float w = win.width * static_cast<float>(Mod::get()->getSettingValue<double>("chat-width"));
        float h = win.height * 0.6f;
        auto opacity = static_cast<int>(Mod::get()->getSettingValue<int64_t>("chat-opacity"));
        auto overlay = ChatOverlay::create(w, h, opacity);
        overlay->setPosition({win.width - w - 4.f, 28.f});
        this->addChild(overlay, 10000);
        m_fields->overlay = overlay;
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        if (!m_fields->overlay) return;
        m_fields->deathReported = false;
        Brain::get().onAttemptStart(this->m_attempts);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // GD "kills" the player with a hidden anticheat spike right at the start; ignore it
        if (!m_fields->overlay || object == this->m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        float percent = this->getCurrentPercent();
        PlayLayer::destroyPlayer(player, object);
        // noclip and similar keep the player alive: only real deaths count
        if (player && player->m_isDead && !m_fields->deathReported) {
            m_fields->deathReported = true;
            Brain::get().onDeath(percent, this->m_isPracticeMode);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!m_fields->overlay) return;
        Brain::get().onComplete(this->m_isPracticeMode);
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!m_fields->overlay || !this->m_player1) return;
        if (!this->m_player1->m_isDead && !this->m_hasCompletedLevel) {
            Brain::get().onProgress(this->getCurrentPercent(), this->m_isPracticeMode);
        }
    }

    void onQuit() {
        if (m_fields->overlay) Brain::get().endSession();
        PlayLayer::onQuit();
    }
};
