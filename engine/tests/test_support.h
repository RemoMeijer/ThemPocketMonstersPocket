// Shared by the engine tests: a CHECK macro, test cards, and a table to build positions on.
// The tests use their own cards, so they don't need data/cards.tsv.
#pragma once
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/game.h"

inline int failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

// Ten Basics "Mon0".."Mon9": 60 HP, Water, one attack "Splash" costing 1 Water for 30 damage.
// Then "Evo1" (Stage 1 from Mon0, 90 HP) and "Evo2" (Stage 2 from Evo1, 120 HP), same attack.
inline std::vector<CardDef> testCards() {
    std::vector<CardDef> cards(12);
    for (int i = 0; i < 12; ++i) {
        CardDef& c = cards[i];
        c.id = "T-" + std::to_string(i);
        c.name = "Mon" + std::to_string(i);
        c.hp = 60;
        c.type = Type::Water;
        c.supported = true;
        AttackDef a;
        a.name = "Splash";
        a.cost[int(Type::Water)] = 1;
        a.damage = 30;
        a.damageText = "30";
        c.attacks.push_back(a);
    }
    cards[10].name = "Evo1";
    cards[10].stage = Stage::Stage1;
    cards[10].evolvesFrom = "Mon0";
    cards[10].hp = 90;
    cards[11].name = "Evo2";
    cards[11].stage = Stage::Stage2;
    cards[11].evolvesFrom = "Evo1";
    cards[11].hp = 120;
    return cards;
}

// Mon0..Mon9, two of each.
inline Deck testDeck(const std::vector<CardDef>& cards) {
    Deck d;
    d.name = "test deck";
    d.energyTypes = {Type::Water};
    for (int i = 0; i < 10; ++i) d.cards.insert(d.cards.end(), 2, &cards[i]);
    return d;
}

// Mon0 -> Evo1 -> Evo2 line plus Mon1..Mon7, two of each.
inline Deck evolutionDeck(const std::vector<CardDef>& cards) {
    Deck d;
    d.name = "evolution deck";
    d.energyTypes = {Type::Water};
    for (int i : {0, 10, 11, 1, 2, 3, 4, 5, 6, 7}) d.cards.insert(d.cards.end(), 2, &cards[i]);
    return d;
}

// A game with everything around it, ready for a hand-built position.
struct Table {
    std::vector<CardDef> cards = testCards();
    Deck deck = testDeck(cards);
    RandomPolicy policy0, policy1;
    Game game{deck, deck, policy0, policy1};

    // P1 (player 0) to move on turn 5: Mon0 Active with one Water vs Mon1 Active, and P2 has Mon2 benched.
    GameState& position() {
        game.setup();
        GameState& s = game.state();
        for (PlayerState& p : s.players) {
            p.bench.clear();
            p.points = 0;
            p.energy.reset();
            p.retreated = false;
        }
        s.timed.clear();
        s.current = 0;
        s.first = 0;
        s.phase = Phase::Main;
        s.turn = 5;
        active(0, cards[0]).energy[int(Type::Water)] = 1;
        active(1, cards[1]);
        bench(1, cards[2]);
        return s;
    }

    PokemonSlot& active(int player, const CardDef& card) {
        PokemonSlot& slot = game.state().players[player].active;
        game.putIntoPlay(slot, &card);
        slot.enteredTurn = 0;
        return slot;
    }

    PokemonSlot& bench(int player, const CardDef& card) {
        auto& bench = game.state().players[player].bench;
        bench.push_back({});
        game.putIntoPlay(bench.back(), &card);
        bench.back().enteredTurn = 0;
        return bench.back();
    }

    bool can(ActionKind kind) {
        for (const Action& a : game.legalActions())
            if (a.kind == kind) return true;
        return false;
    }

    void play(ActionKind kind, int index = 0, int target = 0) {
        for (const Action& a : game.legalActions())
            if (a.kind == kind && a.index == index && a.target == target) return game.apply(a);
        throw std::logic_error("action not legal here");
    }
};

inline int report(const char* what) {
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all " << what << " tests passed\n";
    return 0;
}
