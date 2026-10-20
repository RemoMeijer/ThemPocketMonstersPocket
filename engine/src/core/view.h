#pragma once
#include "core/state.h"

class Game;

// What one player may see: their own hand, both boards and discard piles, and only the sizes of
// hands and decks. Policies get this instead of the whole state, so they can't peek.
class PlayerView {
public:
    PlayerView(const GameState& s, int player, const Game* game = nullptr) : s_(s), me_(player), game_(game) {}

    int me() const { return me_; }
    int opp() const { return 1 - me_; }
    int turn() const { return s_.turn; }

    const decltype(PlayerState::hand)& hand() const { return s_.players[me_].hand; }  // own hand only
    std::optional<Type> zoneEnergy() const { return s_.players[me_].energy; }
    std::optional<Type> nextEnergy() const { return s_.players[me_].nextEnergy; }

    const PokemonSlot& active(int player) const { return s_.players[player].active; }
    const decltype(PlayerState::bench)& bench(int player) const { return s_.players[player].bench; }
    const decltype(PlayerState::discard)& discard(int player) const { return s_.players[player].discard; }
    const EnergyCounts& discardEnergy(int player) const { return s_.players[player].discardEnergy; }
    int points(int player) const { return s_.players[player].points; }
    std::size_t handSize(int player) const { return s_.players[player].hand.size(); }
    std::size_t deckSize(int player) const { return s_.players[player].deck.size(); }
    const CardDef* stadium() const { return s_.stadium; }
    int stadiumOwner() const { return s_.stadiumOwner; }

    // Rule questions about Pokémon in play (public anyway), with every modifier counted (game/game.cpp). Only for a
    // view a Game made, and p must be a slot read from this view: the game tells the Active apart by its address.
    bool hasRules() const { return game_ != nullptr; }
    int hpLeft(const PokemonSlot& p, int owner) const;
    FixedVec<const AttackDef*, 8> attacks(const PokemonSlot& p, int owner) const;
    EnergyCounts attackCost(const PokemonSlot& p, int owner, int attack) const;
    bool canAttack(const PokemonSlot& p, int owner, int attack) const;  // allowed and paid for, if it were Active
    EnergyCounts energy(const PokemonSlot& p, int owner) const;          // as it counts for costs
    int retreatCost(const PokemonSlot& p, int owner) const;
    int koPoints(const PokemonSlot& p, int owner) const;  // what knocking it out with an attack scores

private:
    const GameState& s_;
    int me_;
    const Game* game_;
};
