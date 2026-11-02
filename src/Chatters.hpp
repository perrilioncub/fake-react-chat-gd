#pragma once

#include <Geode/Geode.hpp>
#include <random>
#include <string>
#include <vector>

// The people in chat. Each one has a username, a name colour and a personality,
// and the AI is told to stay in character for each of them.

struct Chatter {
    std::string name;
    cocos2d::ccColor3B color;
    std::string persona;     // short label, e.g. "glazer"
    std::string description; // what the AI is told about them
    bool isMod = false;
};

namespace chatters {

struct Persona {
    char const* label;
    char const* description;
};

inline std::vector<Persona> const& personas() {
    static std::vector<Persona> const list = {
        {"glazer", "thinks the streamer is the GOAT, hypes every tiny thing"},
        {"hater", "says the level is overrated or the streamer is bad, but keeps watching anyway"},
        {"backseater", "gives unsolicited gameplay advice (click earlier, hold, use practice mode)"},
        {"go spammer", "spams GO GO GO and gets hyped way too early, often gets flamed for it"},
        {"lurker", "rarely talks, short messages, mostly emotes"},
        {"stats nerd", "knows GD history, demon list placements and creators; only states facts given in LEVEL FACTS"},
        {"new viewer", "just joined, asks basic questions like what level is this or how long has he been on it"},
        {"comedian", "makes jokes and memes about every death"},
        {"copium enjoyer", "always says next attempt is the one, Copium"},
        {"doomer", "predicts every attempt will fail, Sadge"},
        {"pro player", "acts like a top player, judges inputs and consistency"},
        {"chill regular", "friendly regular who talks to other chatters"},
    };
    return list;
}

inline std::vector<cocos2d::ccColor3B> const& palette() {
    // Twitch's default username colours
    static std::vector<cocos2d::ccColor3B> const list = {
        {255, 0, 0}, {0, 0, 255}, {0, 128, 0}, {178, 34, 34}, {255, 127, 80},
        {154, 205, 50}, {255, 69, 0}, {46, 139, 87}, {218, 165, 32}, {210, 105, 30},
        {95, 158, 160}, {30, 144, 255}, {255, 105, 180}, {138, 43, 226}, {0, 255, 127},
    };
    return list;
}

inline std::string makeName(std::mt19937& rng) {
    static std::vector<char const*> const first = {
        "spike", "wave", "orb", "demon", "cube", "ship", "ufo", "robot", "spider", "swing",
        "practice", "noclip", "ez", "copium", "kekw", "pog", "frame", "click", "dash", "bloodbath",
        "tartarus", "slaughter", "acu", "sonic", "zodiac", "grief", "nine", "circles", "theory", "stereo",
    };
    static std::vector<char const*> const second = {
        "enjoyer", "main", "god", "fan", "lord", "andy", "gamer", "guy", "kid", "fiend",
        "addict", "warrior", "slayer", "hater", "goblin", "merchant", "_tv", "_gd", "player", "master",
    };
    std::uniform_int_distribution<size_t> a(0, first.size() - 1), b(0, second.size() - 1);
    std::uniform_int_distribution<int> style(0, 5), num(1, 9999);
    std::string n = std::string(first[a(rng)]) + second[b(rng)];
    switch (style(rng)) {
        case 0: n += std::to_string(num(rng)); break;
        case 1: n = "xX" + n + "Xx"; break;
        case 2: n += "_"; n += std::to_string(num(rng) % 100); break;
        case 3: n[0] = static_cast<char>(std::toupper(n[0])); break;
        default: break;
    }
    return n;
}

inline std::vector<Chatter> makeRoster(int count, unsigned seed) {
    std::mt19937 rng(seed);
    auto const& ps = personas();
    auto const& pal = palette();
    std::vector<Chatter> out;
    std::uniform_int_distribution<size_t> pc(0, pal.size() - 1);
    for (int i = 0; i < count; i++) {
        Chatter c;
        // make names unique
        for (int tries = 0; tries < 10; tries++) {
            c.name = makeName(rng);
            bool dup = false;
            for (auto& o : out) dup |= (o.name == c.name);
            if (!dup) break;
        }
        // cycle personas so small chats still get a mix, then shuffle-ish
        auto const& p = ps[(i * 5 + seed) % ps.size()];
        c.persona = p.label;
        c.description = p.description;
        c.color = pal[pc(rng)];
        c.isMod = (i == 0);
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace chatters
