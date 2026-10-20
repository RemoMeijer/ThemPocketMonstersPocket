#pragma once
#include <memory>
#include <string>
#include <unordered_map>

#include "players/policy.h"

// A rule-of-thumb player: each decision takes the first rule that applies (win now, gust for the win, evolve, bench,
// Trainers, Energy, retreat, attack). No look-ahead, so it stays fast enough for deck search. It asks rule questions
// through the view, so it needs a view a Game made.
class HeuristicPolicy : public Policy {
public:
    std::size_t choose(const PlayerView& view, const std::vector<Action>& actions) override;

private:
    std::size_t act(const PlayerView& v, const std::vector<Action>& actions);
    std::size_t answer(const PlayerView& v, const std::vector<Action>& options);  // a Choose inside an effect
    uint16_t kinds(const CardDef* c);  // what a card does, read from its scripts once

    // Memory of this turn only.
    int turn_ = -1;
    int moves_ = 0;      // decisions this turn: a stop for moves that could repeat forever
    uint16_t plan_ = 0;  // the opponent's Benched Pokémon a gust was played to bring up
    uint16_t last_ = 0;  // what the last card, ability or attack played does (kinds bits), for the choice after it
    std::unordered_map<const CardDef*, uint16_t> kinds_;
    std::mt19937 rng_{std::random_device{}()};
};

std::unique_ptr<Policy> makePolicy(const std::string& name);  // a player by name: "random" or "heuristic"
