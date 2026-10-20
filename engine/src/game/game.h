#pragma once
#include <algorithm>
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
    // After any damage: knock-outs, points, winner, new Active Pokémon. attacker: right after its attack.
    void knockOutCheck(uint16_t attacker = 0);
    bool dropExtraTools();
    bool effectFlip(int player);  // a coin for the effect of an attack, Ability or Trainer card (Will)
    void finish(int winner);

    // --- Board ---
    PokemonSlot& slot(int player, int target) { return target < 0 ? s_.players[player].active : s_.players[player].bench[target]; }
    void putIntoPlay(PokemonSlot& slot, const CardDef* card);  // a new Pokémon: fresh identity
    PokemonSlot& addToBench(int player, const CardDef* card);  // a new Pokémon in the lowest free Bench spot
    void evolve(PokemonSlot& slot, const CardDef* card);       // puts card on top; damage, energy and Tool stay
    void movedToActive(int player);        // bookkeeping after a new Pokémon moved into the Active Spot
    void clearConditions(PokemonSlot& slot);  // its Special Conditions and timed effects end (moved to Bench, evolved, gone)
    PokemonSlot* find(uint16_t uid, int* owner = nullptr);  // a Pokémon in play by identity
    void draw(int player);
    // A Pokémon leaves play with its evolutions and Tool, into its owner's hand (Koga) or deck (Eldegoss); its
    // energy goes to the discard pile (ruling). knockOutCheck() then moves up a new Active, or ends the game.
    void takeBack(uint16_t uid, bool toDeck);
    void discardFromPlay(uint16_t uid);  // the Pokémon and all its cards to the discard pile; no points (Klefki)
    void toHand(int player, const CardDef* card);  // into the hand; a full hand sends it to the discard pile (assumed)
    void attachFromZone(int player, uint16_t uid, Type type, int count);  // new energy from the Energy Zone

    // --- Damage (game/damage.cpp) ---
    // An attack's damage to one of the opponent's Pokémon, in a fixed order: base → the attacker's
    // modifiers → Weakness → the target's modifiers → at least 0. The first two only count against the
    // Active Pokémon. Then "when damaged by an attack" effects run. Returns the damage done.
    int dealAttackDamage(int attacker, uint16_t attackerUid, uint16_t targetUid, int base, bool weakness = true,
                         bool ignoreDefender = false);
    void putDamage(uint16_t uid, int amount);  // straight onto a Pokémon: to itself, abilities, Checkup
    bool protectedFromAttacks(uint16_t uid);   // from the effects of attacks: PreventAll or PreventEffects

    // --- Questions the rules ask; modifiers from abilities, Tools, the Stadium and timed effects answer them ---
    int maxHp(const PokemonSlot& p, int owner) const;
    // Attached Energy as it counts, for costs and "for each Energy": a DoubleEnergy type counts twice (Serperior, ruling).
    EnergyCounts energyOf(const PokemonSlot& p, int owner) const;
    uint8_t blockedStatuses(const PokemonSlot& p, int owner) const;  // Special Conditions it can't have (NoConditions)
    int retreatCost(const PokemonSlot& p, int owner) const;
    bool canRetreat(const PokemonSlot& p, int owner) const;
    // "Basic Pokémon in play … have no Abilities" (Alolan Muk). Read straight from the cards in play and timed effects,
    // not through the modifiers, which ask this. Fossils have no Ability, so their rule stays.
    bool abilitiesOff(const PokemonSlot& p) const {
        if (p.card->abilities.empty()) return false;
        for (const TimedEffect& t : s_.timed)  // "the Defending Pokémon loses all Abilities" (Budew)
            if (t.mod.kind == ModKind::NoAbilities && t.uid == p.uid) return true;
        for (const PlayerState& ps : s_.players) {
            auto silences = [&](const PokemonSlot& src) {
                for (const Modifier& m : src.card->passive)
                    if (m.kind == ModKind::NoAbilities && &src != &p && (!m.stages || (m.stages & stageBit(p.card->stage))))
                        return true;
                return false;
            };
            if (ps.active.card && silences(ps.active)) return true;
            for (const PokemonSlot& b : ps.bench)
                if (silences(b)) return true;
        }
        return false;
    }
    bool toolRoom(const PokemonSlot& p, int owner) const {  // one Tool, more with ExtraTools (Revavroom)
        return !p.tools.full() && int(p.tools.size()) < 1 + sumMods(ModKind::ExtraTools, p, owner);
    }
    bool canUseAttack(const PokemonSlot& p, int owner, int attack) const;
    EnergyCounts attackCost(const PokemonSlot& p, int owner, int attack) const;
    // Its attacks, then those of the cards it evolved from when it may use them (Celebi, Memory Light).
    FixedVec<const AttackDef*, 8> attacksOf(const PokemonSlot& p, int owner) const {
        FixedVec<const AttackDef*, 8> out;
        for (const AttackDef& a : p.card->attacks) out.push_back(&a);
        if (!p.below.empty() && hasMod(ModKind::PreviousAttacks, p, owner))
            for (const CardDef* c : p.below)
                for (const AttackDef& a : c->attacks)
                    if (!out.full()) out.push_back(&a);
        return out;
    }
    int sumMods(ModKind kind, const PokemonSlot& subject, int owner) const;
    bool hasMod(ModKind kind, const PokemonSlot& subject, int owner) const;
    bool playerHas(ModKind kind, int player) const;  // player-wide modifiers, e.g. "can't play Supporters"
    template <class Fn>
    void forEachModOf(ModKind kind, const PokemonSlot& subject, int owner, Fn fn) const { forEachMod(kind, &subject, owner, fn); }

    // fn(card) for the Pokémon's own card, then each of its Tools; skips any that left it on the way.
    template <class Fn>
    void forEachSource(uint16_t uid, Fn fn) {
        const PokemonSlot* p = find(uid);
        if (!p) return;
        FixedVec<const CardDef*, 3> cards;
        if (!abilitiesOff(*p)) cards.push_back(p->card);
        for (const CardDef* t : p->tools) cards.push_back(t);
        for (const CardDef* c : cards)
            if ((p = find(uid)) && (c == p->card || std::find(p->tools.begin(), p->tools.end(), c) != p->tools.end())) fn(c);
    }

    // --- Decisions and chance ---
    // The player's policy picks one of `options` (0 when only one). uids[i] or cards[i], when given, is the Pokémon
    // or card option i stands for, and kind what the pick is for, so a player can tell the options apart.
    int choose(int player, int options, ChoiceKind kind = ChoiceKind::Other, const uint16_t* uids = nullptr,
               const CardDef* const* cards = nullptr);
    bool flip();                          // a coin: true = heads
    Type rollEnergy(const PlayerState& p);  // a random energy type from p's deck, for the Energy Zone
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
    void knockOut(uint16_t uid, uint16_t attacker);
    void resolveLater();  // the LaterEffects due at the end of this turn
    void runTriggers(int player, const Stmt* CardDef::*form, bool CardDef::*whileActive);  // its Pokémon's, Active first
    void survive(PokemonSlot& slot, int owner);
    void promote(int player);
    template <class Fn>
    void forEachMod(ModKind kind, const PokemonSlot* subject, int owner, Fn fn) const;
    static bool hasEnergy(const PokemonSlot& p, uint16_t types) {  // any Energy of these types (0 = any)
        for (int t = 0; t < TypeCount; ++t)
            if (p.energy[t] && (!types || (types & typeBit(Type(t))))) return true;
        return false;
    }
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
        if (m.whileBenched && (!source || source == &s_.players[sourceOwner].active)) return false;
        if (m.needs && !inPlay(sourceOwner, *m.needs)) return false;
        if (m.when) {  // asked about its source; the compiler never puts a choice in it
            EffectCtx ctx{const_cast<Game&>(*this), sourceOwner};
            ctx.self = source ? source->uid : 0;
            if (!m.when->test(ctx)) return false;
        }
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
        return subject->hasType(m.types) && (!m.stages || (m.stages & stageBit(subject->card->stage))) &&
               (!m.evolvesFrom || m.evolvesFrom->has(subject->card->evolvesFrom)) && (!m.traits || (m.traits & subject->card->traits)) &&
               (!m.withEnergy || hasEnergy(*subject, m.withEnergyTypes)) &&
               (!m.names || m.names->has(subject->card->name)) && (!m.notMega || !subject->card->mega) &&
               (!m.minRetreat || retreatCost(*subject, owner) >= m.minRetreat);
    };
    for (int p = 0; p < 2; ++p) {
        auto visit = [&](const PokemonSlot& source) {
            if (!abilitiesOff(source))
                for (const Modifier& m : source.card->passive)
                    if (applies(m, p, &source)) fn(m);
            for (const CardDef* tool : source.tools)
                for (const Modifier& m : tool->passive)
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
