#pragma once
#include "core/state.h"

// What one player may see: their own hand, both boards and discard piles, and only the sizes of
// hands and decks. Policies get this instead of the whole state, so they can't peek.
class PlayerView {
public:
    PlayerView(const GameState& s, int player) : s_(s), me_(player) {}

    int me() const { return me_; }
    int opp() const { return 1 - me_; }
    int turn() const { return s_.turn; }

    const decltype(PlayerState::hand)& hand() const { return s_.players[me_].hand; }  // own hand only
    std::optional<Type> zoneEnergy() const { return s_.players[me_].energy; }

    const PokemonSlot& active(int player) const { return s_.players[player].active; }
    const decltype(PlayerState::bench)& bench(int player) const { return s_.players[player].bench; }
    const decltype(PlayerState::discard)& discard(int player) const { return s_.players[player].discard; }
    int points(int player) const { return s_.players[player].points; }
    std::size_t handSize(int player) const { return s_.players[player].hand.size(); }
    std::size_t deckSize(int player) const { return s_.players[player].deck.size(); }
    const CardDef* stadium() const { return s_.stadium; }

private:
    const GameState& s_;
    int me_;
};
