// Attaching the energy waiting in the Energy Zone to one of your Pokémon.
#include "game/game.h"
#include "moves/moves.h"

namespace {
class AttachEnergy : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PlayerState& p = game.state().players[me];
        if (!p.energy) return;
        if (!game.playerHas(ModKind::CantAttachToActive, me)) out.push_back({ActionKind::AttachEnergy, 0, -1});
        for (std::size_t i = 0; i < p.bench.size(); ++i) out.push_back({ActionKind::AttachEnergy, 0, int8_t(i)});
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        Type type = *p.energy;
        p.energy.reset();
        game.attachFromZone(me, game.slot(me, a.target).uid, type, 1);
        game.knockOutCheck();  // an on-attach ability can do damage
    }
};
}  // namespace

const MoveRule& attachEnergyRule() {
    static const AttachEnergy rule;
    return rule;
}
