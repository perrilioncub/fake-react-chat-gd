#include "Brain.hpp"
#include "ChatOverlay.hpp"

#include <algorithm>
#include <chrono>
#include <random>

using namespace geode::prelude;

namespace {

std::mt19937& rng() {
    static std::mt19937 r(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return r;
}

float frand(float a, float b) {
    return std::uniform_real_distribution<float>(a, b)(rng());
}

struct Activity {
    float minInterval; // seconds between AI calls
    float tickInterval; // seconds of quiet play before chat says something anyway
    float fillerGap;   // seconds of silence before a local filler message
    char const* count; // how many messages per call
};

Activity activity() {
    auto a = Mod::get()->getSettingValue<std::string>("activity");
    if (a == "Chill") return {12.f, 18.f, 9.f, "2 to 4"};
    if (a == "Hyper") return {4.f, 6.f, 2.5f, "5 to 9"};
    return {7.f, 11.f, 5.f, "3 to 6"};
}

std::string vibeRule() {
    auto t = Mod::get()->getSettingValue<std::string>("toxicity");
    if (t == "Wholesome") return "supportive and kind; gentle teasing at most";
    if (t == "Spicy") return "chaotic, roasts are harsher and chat argues a lot, but it stays playful";
    return "typical Geometry Dash stream chat: banter, light trash talk, memes, some genuine support";
}

std::string pct(float p) {
    return fmt::format("{}%", static_cast<int>(p));
}

} // namespace

Brain& Brain::get() {
    static Brain b;
    return b;
}

void Brain::startSession(LevelInfo info) {
    m_task.cancel();
    m_active = true;
    m_sessionId++;
    m_clock = 0.f;
    m_level = std::move(info);
    auto count = static_cast<int>(Mod::get()->getSettingValue<int64_t>("chatters"));
    m_roster = chatters::makeRoster(std::clamp(count, 3, 40), static_cast<unsigned>(rng()()));

    m_sessionAttempts = 0;
    m_currentAttempt = 0;
    m_sessionBest = 0.f;
    m_deathSpots.clear();
    m_hypeFiredThisAttempt = false;
    m_diedThisAttempt = false;
    m_lastProgress = 0.f;
    m_lastTickAt = 0.f;

    m_inFlight = false;
    m_pending.reset();
    m_lastCallAt = -1000.f;
    m_callTimes.clear();
    m_errorCount = 0;
    m_lastError.clear();

    m_queue.clear();
    m_history.clear();
    m_lastShownAt = 0.f;
    m_systemNotes.clear();

    if (Mod::get()->getSettingValue<std::string>("api-key").empty()) {
        m_systemNotes.push_back("No Claude API key set. Add it in the mod settings to wake chat up.");
    }
    this->queueEvent({EventKind::Start, "The streamer just opened this level. Chat is arriving and greeting the stream."});
}

void Brain::endSession() {
    m_active = false;
    m_task.cancel();
    m_inFlight = false;
    m_queue.clear();
}

void Brain::onAttemptStart(int attempt) {
    if (!m_active) return;
    m_currentAttempt = attempt;
    m_sessionAttempts++;
    m_hypeFiredThisAttempt = false;
    m_diedThisAttempt = false;
    m_lastProgress = 0.f;
    m_lastTickAt = m_clock;
}

void Brain::onDeath(float percent, bool practice) {
    if (!m_active || m_diedThisAttempt) return;
    m_diedThisAttempt = true;
    int p = static_cast<int>(percent);
    bool newBest = !practice && !m_level.platformer && p > m_level.bestPercent;
    if (!practice) m_deathSpots[p]++;
    m_sessionBest = std::max(m_sessionBest, percent);

    std::string detail;
    if (m_level.platformer) {
        detail = fmt::format("The streamer died (platformer level, attempt {}).", m_currentAttempt);
    } else if (newBest) {
        detail = fmt::format("NEW BEST! The streamer died at {} which beats their old best of {}. Chat goes crazy, mix of hype and 'so close'.", pct(percent), pct(m_level.bestPercent));
        m_level.bestPercent = p;
    } else {
        detail = fmt::format("The streamer died at {}{}.", pct(percent), practice ? " (in practice mode)" : "");
        if (p < 10) detail += " That's an early death.";
        else if (m_level.bestPercent > 0 && p >= m_level.bestPercent - 5) detail += " That's right near their best, painful.";
    }
    this->queueEvent({newBest ? EventKind::NewBest : EventKind::Death, detail});
}

void Brain::onComplete(bool practice) {
    if (!m_active) return;
    if (practice) {
        this->queueEvent({EventKind::Complete, "The streamer finished a practice mode run (not a real completion). Chat reacts, some say 'now do it in normal mode'."});
    } else {
        this->queueEvent({EventKind::Complete, fmt::format(
            "THE STREAMER JUST BEAT THE LEVEL! GG! This is a full completion on attempt {} of the session. Chat completely loses it: GG spam, POGGERS, W, emotional messages, clip it.",
            m_currentAttempt)});
        m_level.bestPercent = 100;
    }
}

void Brain::onProgress(float percent, bool practice) {
    if (!m_active || m_diedThisAttempt) return;
    m_lastProgress = percent;
    if (practice || m_level.platformer || m_hypeFiredThisAttempt) return;

    // chat starts chanting GO when the streamer is getting close to their best,
    // or deep into the level
    float best = static_cast<float>(m_level.bestPercent);
    float threshold = best >= 50.f ? std::clamp(best - 6.f, 50.f, 97.f) : 80.f;
    if (percent >= threshold) {
        m_hypeFiredThisAttempt = true;
        this->queueEvent({EventKind::Hype, fmt::format(
            "The streamer is ALIVE at {} right now (best is {}) and still going. Chat is getting nervous and hyped: GO GO GO, monkaS, don't choke.",
            pct(percent), pct(best))});
    }
}

void Brain::queueEvent(ChatEvent ev) {
    if (!m_pending || static_cast<int>(ev.kind) >= static_cast<int>(m_pending->kind)) {
        m_pending = std::move(ev);
    }
}

bool Brain::canCall() const {
    if (m_inFlight || !m_pending) return false;
    if (Mod::get()->getSettingValue<std::string>("api-key").empty()) return false;
    if (m_errorCount >= 5) return false; // something is wrong, stop spending
    auto maxPerMin = Mod::get()->getSettingValue<int64_t>("max-calls-per-minute");
    if (static_cast<int64_t>(m_callTimes.size()) >= maxPerMin) return false;
    bool urgent = m_pending->kind == EventKind::Complete || m_pending->kind == EventKind::NewBest || m_pending->kind == EventKind::Hype;
    float minGap = urgent ? 1.5f : activity().minInterval;
    return m_clock - m_lastCallAt >= minGap;
}

Chatter const* Brain::findChatter(std::string const& name) const {
    for (auto& c : m_roster) {
        if (c.name == name) return &c;
    }
    // be forgiving about case
    for (auto& c : m_roster) {
        if (std::equal(c.name.begin(), c.name.end(), name.begin(), name.end(),
                       [](char a, char b) { return std::tolower(a) == std::tolower(b); })) return &c;
    }
    return nullptr;
}

std::string Brain::buildSystemPrompt() const {
    std::string roster;
    for (auto& c : m_roster) {
        roster += fmt::format("- {} ({}{}): {}\n", c.name, c.persona, c.isMod ? ", channel moderator" : "", c.description);
    }
    return fmt::format(
        "You write the live chat of a Twitch stream. The streamer is playing Geometry Dash (GD). "
        "You control ALL of the chatters below and write what they type in reaction to what just happened in the game.\n\n"
        "CHATTERS:\n{}\n"
        "RULES:\n"
        "- Output ONLY a JSON array and nothing else: [{{\"u\":\"<chatter name>\",\"m\":\"<message>\",\"d\":<delay in ms, 0-6000>}}]\n"
        "- Write {} messages. Use a mix of different chatters, not always the same ones. Lurkers rarely talk.\n"
        "- Messages are short like real Twitch chat: usually 1-8 words, mostly lowercase, slang and typos are fine.\n"
        "- No emoji at all (the chat font cannot display them). Use emote words instead, e.g. KEKW, OMEGALUL, LULW, Pog, PogChamp, POGGERS, monkaS, Sadge, Copium, Clueless, 5Head, Aware, EZ, GIGACHAD, ICANT, Kappa, W, L, F, o7.\n"
        "- Chatters react to EACH OTHER, not only to the streamer: argue, agree, @mention, copy-spam. If someone hypes way too early (like GO GO GO at 5%), others flame them for it.\n"
        "- React to the real numbers you are given: an early death, a death near their best, a new best and a completion are all very different moods.\n"
        "- Keep every chatter in character.\n"
        "- Facts: only state facts about the level that appear in LEVEL INFO. Never invent verifiers, dates, list placements, records or stats. If unsure, chatters ask or joke instead.\n"
        "- Vibe: {}. Never use slurs, hate, sexual content, or insults about identity. Roast the gameplay, not the person.\n"
        "- Don't repeat earlier chat lines word for word.",
        roster, activity().count, vibeRule());
}

std::string Brain::buildUserPrompt(ChatEvent const& ev) const {
    auto& L = m_level;
    std::string info = fmt::format(
        "LEVEL INFO: \"{}\" by {} (level ID {}). Difficulty: {}{}. Length: {}. Likes: {}. Downloads: {}.\n",
        L.name, L.creator.empty() ? "RobTop" : L.creator, L.id, L.difficulty,
        L.stars > 0 ? fmt::format(", {} stars", L.stars) : "", L.length, L.likes, L.downloads);
    if (!L.platformer) {
        info += fmt::format("Streamer's all-time best on this level: {}. GD attempt counter for this level: {}.\n",
                            pct(L.bestPercent), L.totalAttempts);
    }

    std::string session = fmt::format("SESSION: current attempt #{}, {} attempts since opening the level", m_currentAttempt, m_sessionAttempts);
    if (!L.platformer && !m_deathSpots.empty()) {
        std::vector<std::pair<int, int>> spots(m_deathSpots.begin(), m_deathSpots.end());
        std::sort(spots.begin(), spots.end(), [](auto& a, auto& b) { return a.second > b.second; });
        session += ". Most common death spots:";
        for (size_t i = 0; i < std::min<size_t>(3, spots.size()); i++) {
            session += fmt::format(" {}% (x{})", spots[i].first, spots[i].second);
        }
    }
    session += ".\n";

    std::string chat = "RECENT CHAT (oldest first):\n";
    if (m_history.empty()) chat += "(empty, stream just started)\n";
    for (auto& line : m_history) chat += line + "\n";

    return info + session + "\n" + chat + "\nWHAT JUST HAPPENED: " + ev.detail + "\n\nWrite the next chat messages as a JSON array.";
}

void Brain::sendRequest(ChatEvent ev) {
    auto key = Mod::get()->getSettingValue<std::string>("api-key");
    auto model = Mod::get()->getSettingValue<std::string>("model");
    if (model.empty()) model = "claude-haiku-5-5";

    auto messages = matjson::Value::array();
    messages.push(matjson::makeObject({{"role", "user"}, {"content", this->buildUserPrompt(ev)}}));
    auto body = matjson::makeObject({
        {"model", model},
        {"max_tokens", 800},
        {"system", this->buildSystemPrompt()},
        {"messages", messages},
    });

    web::WebRequest req;
    req.header("x-api-key", key);
    req.header("anthropic-version", "2023-06-01");
    req.header("content-type", "application/json");
    req.timeout(std::chrono::seconds(30));
    req.bodyJSON(body);

    m_inFlight = true;
    m_lastCallAt = m_clock;
    m_callTimes.push_back(m_clock);
    int sid = m_sessionId;
    m_task.spawn(req.post("https://api.anthropic.com/v1/messages"), [this, sid](web::WebResponse resp) {
        this->handleResponse(std::move(resp), sid);
    });
}

void Brain::handleResponse(web::WebResponse resp, int sessionId) {
    if (sessionId != m_sessionId) return; // left the level meanwhile
    m_inFlight = false;

    if (!resp.ok()) {
        m_errorCount++;
        std::string msg = fmt::format("Claude API error {}", resp.code());
        auto j = resp.json();
        if (j.isOk()) {
            matjson::Value root = j.unwrap();
            auto e = root["error"]["message"].asString();
            if (e.isOk()) msg += ": " + e.unwrap();
        }
        if (resp.code() == 401) msg = "Claude rejected the API key (401). Check it in the mod settings.";
        if (msg != m_lastError) {
            m_systemNotes.push_back(msg);
            m_lastError = msg;
        }
        log::warn("{}", msg);
        return;
    }

    auto json = resp.json();
    if (json.isErr()) {
        m_errorCount++;
        return;
    }
    matjson::Value root = json.unwrap();
    std::string text;
    auto& content = root["content"];
    if (content.isArray()) {
        for (auto& block : content) {
            if (block["type"].asString().unwrapOr("") == "text") text += block["text"].asString().unwrapOr("");
        }
    }

    auto start = text.find('[');
    auto end = text.rfind(']');
    if (start == std::string::npos || end == std::string::npos || end <= start) {
        log::warn("Chat reply had no JSON array: {}", text);
        return;
    }
    auto parsed = matjson::parse(std::string_view(text).substr(start, end - start + 1));
    if (parsed.isErr()) {
        log::warn("Couldn't parse chat JSON: {}", text);
        return;
    }
    matjson::Value arr = parsed.unwrap();
    if (!arr.isArray()) return;
    m_errorCount = 0;

    float base = m_clock + 0.4f; // a little stream delay feels real
    float spread = 0.f;
    for (auto& item : arr) {
        auto user = item["u"].asString().unwrapOr("");
        auto msg = item["m"].asString().unwrapOr("");
        if (user.empty() || msg.empty()) continue;
        double d = item["d"].asDouble().unwrapOr(-1.0);
        float delay = d >= 0 ? static_cast<float>(std::clamp(d, 0.0, 8000.0) / 1000.0) : (spread += frand(0.3f, 1.2f));
        m_queue.push_back({base + delay, user, msg});
    }
    std::stable_sort(m_queue.begin(), m_queue.end(), [](auto& a, auto& b) { return a.showAt < b.showAt; });
}

void Brain::localFiller() {
    // cheap messages between AI bursts, so chat never looks dead
    static std::vector<char const*> const lines = {
        "KEKW", "monkaS", "Sadge", "o7", "lol", "W", "Pog", "catJAM", "real", "LULW",
        "Clueless", "this level is so clean", "hi chat", "first time here", "Copium",
    };
    std::vector<Chatter const*> quiet;
    for (auto& c : m_roster) {
        if (c.persona == "lurker" || c.persona == "chill regular" || c.persona == "new viewer") quiet.push_back(&c);
    }
    if (quiet.empty()) quiet.push_back(&m_roster[0]);
    auto who = quiet[std::uniform_int_distribution<size_t>(0, quiet.size() - 1)(rng())];
    auto what = lines[std::uniform_int_distribution<size_t>(0, lines.size() - 1)(rng())];
    m_queue.push_back({m_clock + frand(0.f, 0.8f), who->name, what});
}

void Brain::update(float dt, ChatOverlay* overlay) {
    m_clock += dt;
    if (!m_active || !overlay) return;

    for (auto& note : m_systemNotes) overlay->addSystem(note);
    m_systemNotes.clear();

    // slide out old call timestamps (rate limit window)
    while (!m_callTimes.empty() && m_clock - m_callTimes.front() > 60.f) m_callTimes.pop_front();

    // show messages that are due
    while (!m_queue.empty() && m_queue.front().showAt <= m_clock) {
        auto m = std::move(m_queue.front());
        m_queue.pop_front();
        auto c = this->findChatter(m.user);
        ccColor3B color = c ? c->color : ccColor3B{200, 200, 200};
        auto clean = sanitizeForFont(m.text);
        if (clean.empty()) continue;
        overlay->addMessage(m.user, color, clean, c && c->isMod);
        m_history.push_back(m.user + ": " + clean);
        while (m_history.size() > 25) m_history.pop_front();
        m_lastShownAt = m_clock;
    }

    auto act = activity();

    // nothing happened for a while but the streamer is playing: chat still talks
    bool alive = !m_diedThisAttempt && m_currentAttempt > 0;
    if (alive && !m_pending && m_clock - m_lastTickAt > act.tickInterval) {
        m_lastTickAt = m_clock;
        if (m_level.platformer) {
            this->queueEvent({EventKind::Tick, "The streamer is playing, nothing special happened. Chat chats: questions, jokes, side conversations, backseating."});
        } else {
            this->queueEvent({EventKind::Tick, fmt::format(
                "The streamer is currently alive at {} on this attempt. Nothing special yet. Chat chats: side conversations, questions, backseating.",
                pct(m_lastProgress))});
        }
    }

    if (this->canCall()) {
        auto ev = std::move(*m_pending);
        m_pending.reset();
        this->sendRequest(std::move(ev));
    }

    bool keyMissing = Mod::get()->getSettingValue<std::string>("api-key").empty();
    if (!keyMissing && m_queue.empty() && m_clock - m_lastShownAt > act.fillerGap * frand(1.f, 1.6f) && !m_roster.empty()) {
        this->localFiller();
        m_lastShownAt = m_clock;
    }
}
