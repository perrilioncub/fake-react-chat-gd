#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include "Chatters.hpp"

#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

class ChatOverlay;

// What we know about the level being played (all from GD itself).
struct LevelInfo {
    int id = 0;
    std::string name;
    std::string creator;
    int stars = 0;
    std::string difficulty; // "Extreme Demon", "Harder", ...
    std::string length;     // "Tiny" .. "XL", "Platformer"
    int likes = 0;
    int downloads = 0;
    int bestPercent = 0;    // all-time best before this session
    int totalAttempts = 0;  // GD's stored attempt count
    bool platformer = false;
    bool rated = false;
};

// Things that happen in the level and that chat reacts to.
enum class EventKind { Start = 0, Tick = 1, Death = 2, Hype = 3, NewBest = 4, Complete = 5 };

struct ChatEvent {
    EventKind kind;
    std::string detail;
};

struct PendingMessage {
    float showAt; // session clock time
    std::string user;
    std::string text;
};

// Talks to Claude and schedules chat messages.
class Brain {
public:
    static Brain& get();

    void startSession(LevelInfo info);
    void endSession();

    // gameplay hooks
    void onAttemptStart(int attempt);
    void onDeath(float percent, bool practice);
    void onComplete(bool practice);
    void onProgress(float percent, bool practice); // called every frame while alive

    void update(float dt, ChatOverlay* overlay);

private:
    void queueEvent(ChatEvent ev);
    bool canCall() const;
    void sendRequest(ChatEvent ev);
    void handleResponse(geode::utils::web::WebResponse resp, int sessionId);
    std::string buildSystemPrompt() const;
    std::string buildUserPrompt(ChatEvent const& ev) const;
    Chatter const* findChatter(std::string const& name) const;
    void localFiller();

    bool m_active = false;
    int m_sessionId = 0;
    float m_clock = 0.f;
    LevelInfo m_level;
    std::vector<Chatter> m_roster;

    // session stats
    int m_sessionAttempts = 0;
    int m_currentAttempt = 0;
    float m_sessionBest = 0.f;
    std::map<int, int> m_deathSpots; // percent -> count
    bool m_hypeFiredThisAttempt = false;
    bool m_diedThisAttempt = false;
    float m_lastProgress = 0.f;
    float m_lastTickAt = 0.f;

    // AI plumbing
    bool m_inFlight = false;
    std::optional<ChatEvent> m_pending;
    float m_lastCallAt = -1000.f;
    std::deque<float> m_callTimes;
    int m_errorCount = 0;
    std::string m_lastError;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_task;

    // chat state
    std::deque<PendingMessage> m_queue;
    std::deque<std::string> m_history; // "user: text" lines, newest last
    float m_lastShownAt = 0.f;
    std::vector<std::string> m_systemNotes; // shown once in the overlay
};
