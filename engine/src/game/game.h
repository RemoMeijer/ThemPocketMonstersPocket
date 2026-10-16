#pragma once
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cards/deck.h"
#include "core/action.h"
#include "core/state.h"
#include "core/view.h"
#include "effects/effect.h"
#include "game/log.h"
#include "players/policy.h"

struct Result {
    int winner = -1;  // 0 or 1, or -1 for a draw
    int turns = 0;
};

// One game between two decks: the turn flow, plus the services move rules (moves/) and card
// effects (effects/) use. The decks, policies and log must outlive the Game.
class Game {
public:
    Game(const Deck& deck0, const Deck& deck1, Policy& policy0, Policy& policy1, GameLog* log = nullptr);

    Result run();  // setup(), then step() until the game is over
    void setup();  // deal, both players place their Pokémon, coin flip, start turn 1
    void step();   // the player whose turn it is picks one legal action

    int decider() const { return s_.current; }
    const std::vector<Action>& legalActions();
    void apply(Action action);  // action must come from legalActions()

    GameState& state() { return s_; }  // tests set up positions through this
    const GameState& state() const { return s_; }

    // --- Named rules ---
    bool ownFirstTurn(int player) const { return s_.turn == (player == s_.first ? 1 : 2); }
    bool firstTurnOfGame() const { return s_.turn == 1; }

    // --- Turn flow ---
    void beginPlay();      // after setup: coin flip, turn 1
    void endTurn();        // end-of-turn effects, Pokémon Checkup, then the next player's turn
    void knockOutCheck();  // after any damage: knock-outs, points, winner, new Active Pokémon
    void finish(int winner);

    // --- Board ---
    PokemonSlot& slot(int player, int target) { return target < 0 ? s_.players[player].active : s_.players[player].bench[target]; }
    void putIntoPlay(PokemonSlot& slot, const CardDef* card);  // a new Pokémon: fresh identity
    void evolve(PokemonSlot& slot, const CardDef* card);       // puts card on top; damage, energy and Tool stay
    void movedToActive(int player);        // bookkeeping after a new Pokémon moved into the Active Spot
    void clearConditions(PokemonSlot& slot);  // its Special Conditions and timed effects end (moved to Bench, evolved, gone)
    PokemonSlot* find(uint16_t uid, int* owner = nullptr);  // a Pokémon in play by identity
    void draw(int player);
    void attachFromZone(int player, uint16_t uid, Type type, int count);  // new energy from the Energy Zone

    // --- Damage (game/damage.cpp) ---
    // An attack's damage to one of the opponent's Pokémon, in a fixed order: base → the attacker's
    // modifiers → Weakness → the target's modifiers → at least 0. The first two only count against the
    // Active Pokémon. Then "when damaged by an attack" effects run. Returns the damage done.
    int dealAttackDamage(int attacker, uint16_t attackerUid, uint16_t targetUid, int base, bool weakness = true);
    void putDamage(uint16_t uid, int amount);  // straight onto a Pokémon: to itself, abilities, Checkup
    bool protectedFromAttacks(uint16_t uid);   // "prevent all damage from — and effects of — attacks"

    // --- Questions the rules ask; modifiers from abilities, Tools, the Stadium and timed effects answer them ---
    int maxHp(const PokemonSlot& p, int owner) const;
    int retreatCost(const PokemonSlot& p, int owner) const;
    bool canRetreat(const PokemonSlot& p, int owner) const;
    bool canUseAttack(const PokemonSlot& p, int owner, int attack) const;
    EnergyCounts attackCost(const PokemonSlot& p, int owner, int attack) const;
    int sumMods(ModKind kind, const PokemonSlot& subject, int owner) const;
    bool hasMod(ModKind kind, const PokemonSlot& subject, int owner) const;
    bool playerHas(ModKind kind, int player) const;  // player-wide modifiers, e.g. "can't play Supporters"

    // --- Decisions and chance ---
    int choose(int player, int options);  // the player's policy picks one of `options` (0 when only one)
    bool flip();                          // a coin: true = heads
    std::mt19937& rng() { return rng_; }

    // --- Log ---
    bool logging() const { return log_ != nullptr; }
    template <class... Args>
    void say(const Args&... args) {
        if (!log_) return;
        std::ostringstream out;
        (out << ... << args);
        log_->line(out.str());
    }
    std::string show(const PokemonSlot& slot, int owner) const;

private:
    void deal(int player);
    void startTurn();
    void showBoard();
    void knockOut(int player, PokemonSlot& slot);
    void promote(int player);
    template <class Fn>
    void forEachMod(ModKind kind, const PokemonSlot* subject, int owner, Fn fn) const;
    bool inPlay(int player, const NameList& names) const {
        const PlayerState& p = s_.players[player];
        if (p.active.card && names.has(p.active.card->name)) return true;
        for (const PokemonSlot& b : p.bench)
            if (names.has(b.card->name)) return true;
        return false;
    }

    const Deck* decks_[2];
    Policy* policies_[2];
    GameLog* log_;
    GameState s_;
    std::vector<Action> actions_;  // reused between decisions
    std::vector<Action> options_;  // reused by choose()
    std::mt19937 rng_{std::random_device{}()};
};

// Calls fn(modifier) for every modifier of `kind` that applies to `subject`, a Pokémon of player `owner`.
// subject == nullptr asks a player-wide question about player `owner`.
template <class Fn>
void Game::forEachMod(ModKind kind, const PokemonSlot* subject, int owner, Fn fn) const {
    auto applies = [&](const Modifier& m, int sourceOwner, const PokemonSlot* source) {
        if (m.kind != kind) return false;
        if (m.whileActive && source != &s_.players[sourceOwner].active) return false;
        if (m.needs && !inPlay(sourceOwner, *m.needs)) return false;
        bool mine = owner == sourceOwner;
        if (!subject) {
            bool forOpponent = m.scope == ModScope::OppActive || m.scope == ModScope::OppAll;
            return m.scope == ModScope::All || mine != forOpponent;
        }
        bool active = subject == &s_.players[owner].active;
        switch (m.scope) {
            case ModScope::Self:
                if (source != subject) return false;
                break;
            case ModScope::MyActive:
                if (!mine || !active) return false;
                break;
            case ModScope::MyAll:
                if (!mine) return false;
                break;
            case ModScope::OppActive:
                if (mine || !active) return false;
                break;
            case ModScope::OppAll:
                if (mine) return false;
                break;
            case ModScope::All:
                break;
        }
        return subject->card->hasType(m.types) && (!m.stages || (m.stages & stageBit(subject->card->stage))) &&
               (!m.names || m.names->has(subject->card->name));
    };
    for (int p = 0; p < 2; ++p) {
        auto visit = [&](const PokemonSlot& source) {
            for (const Modifier& m : source.card->passive)
                if (applies(m, p, &source)) fn(m);
            if (source.tool)
                for (const Modifier& m : source.tool->passive)
                    if (applies(m, p, &source)) fn(m);
        };
        if (s_.players[p].active.card) visit(s_.players[p].active);
        for (const PokemonSlot& b : s_.players[p].bench) visit(b);
    }
    if (s_.stadium)
        for (const Modifier& m : s_.stadium->passive)
            if (applies(m, s_.stadiumOwner, nullptr)) fn(m);
    for (const TimedEffect& t : s_.timed) {
        if (t.mod.kind != kind) continue;
        if (t.uid ? subject && subject->uid == t.uid : applies(t.mod, t.player, nullptr)) fn(t.mod);
    }
}
