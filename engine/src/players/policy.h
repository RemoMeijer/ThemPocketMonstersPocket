#pragma once
#include <random>
#include <vector>

#include "core/action.h"
#include "core/view.h"

// Decides for one player. The game passes the legal actions; EndTurn, when present, is always last.
class Policy {
public:
    virtual ~Policy() = default;
    virtual std::size_t choose(const PlayerView& view, const std::vector<Action>& actions) = 0;
};

// Picks at random, but never ends the turn while something else is possible, so games don't stall.
class RandomPolicy : public Policy {
public:
    std::size_t choose(const PlayerView&, const std::vector<Action>& actions) override {
        std::size_t n = actions.size();
        if (n > 1 && actions.back().kind == ActionKind::EndTurn) --n;
        return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng_);
    }

private:
    std::mt19937 rng_{std::random_device{}()};
};
