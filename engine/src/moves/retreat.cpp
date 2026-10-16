// Retreating: once per turn, pay the Retreat Cost in energy and swap the Active with a benched Pokémon.
#include <algorithm>

#include "game/game.h"
#include "moves/moves.h"

namespace {
class Retreat : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PlayerState& p = game.state().players[me];
        if (p.retreated || !game.canRetreat(p.active, me)) return;
        if (p.active.energyCount() < game.retreatCost(p.active, me)) return;
        for (std::size_t i = 0; i < p.bench.size(); ++i) out.push_back({ActionKind::Retreat, int8_t(i)});
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        // shortcut: retreat pays with the most plentiful energy type, no choice; make it a choice once multi-type decks matter.
        int cost = game.retreatCost(p.active, me);
        for (int i = 0; i < cost; ++i) {
            int t = int(std::max_element(p.active.energy.begin(), p.active.energy.end()) - p.active.energy.begin());
            --p.active.energy[t];
            ++p.discardEnergy[t];  // paid energy goes to the discard pile (App)
        }
        std::swap(p.active, p.bench[a.index]);
        p.retreated = true;
        game.clearConditions(p.bench[a.index]);  // moving to the Bench ends Special Conditions and effects
        game.movedToActive(me);
        game.say("P", me + 1, " retreats ", p.bench[a.index].card->name, " (cost ", cost, ") and moves ",
                 p.active.card->name, " to the Active spot");
    }
};
}  // namespace

const MoveRule& retreatRule() {
    static const Retreat rule;
    return rule;
}
