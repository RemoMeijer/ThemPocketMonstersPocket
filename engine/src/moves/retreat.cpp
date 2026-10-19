// Retreating: once per turn, pay the Retreat Cost in energy and swap the Active with a benched Pokémon.
#include <algorithm>
#include <numeric>

#include "game/game.h"
#include "moves/moves.h"

namespace {
class Retreat : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PlayerState& p = game.state().players[me];
        if (p.retreated || !game.canRetreat(p.active, me)) return;
        EnergyCounts counted = game.energyOf(p.active, me);
        if (std::accumulate(counted.begin(), counted.end(), 0) < game.retreatCost(p.active, me)) return;
        for (std::size_t i = 0; i < p.bench.size(); ++i) out.push_back({ActionKind::Retreat, int8_t(i)});
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        // shortcut: retreat pays with the most plentiful energy type, no choice; make it a choice once multi-type decks matter.
        int cost = game.retreatCost(p.active, me);
        EnergyCounts counted = game.energyOf(p.active, me);
        EnergyCounts real = p.active.energy;
        for (int left = cost; left > 0;) {
            int t = int(std::max_element(p.active.energy.begin(), p.active.energy.end()) - p.active.energy.begin());
            left -= counted[t] > real[t] ? 2 : 1;  // a doubled Energy pays 2 (Serperior)
            --p.active.energy[t];
            ++p.discardEnergy[t];  // paid energy goes to the discard pile (App)
        }
        std::swap(p.active, p.bench[a.index]);
        p.retreated = true;
        game.clearConditions(p.bench[a.index]);  // moving to the Bench ends Special Conditions and effects
        game.movedToActive(me);
        game.say("P", me + 1, " retreats ", p.bench[a.index].card->name, " (cost ", cost, ") and moves ",
                 p.active.card->name, " to the Active spot");
        game.knockOutCheck();  // HP from attached Energy may have gone with it (Reuniclus)
    }
};
}  // namespace

const MoveRule& retreatRule() {
    static const Retreat rule;
    return rule;
}
