// Ending the turn, or during setup: done placing Pokémon.
#include "game/game.h"
#include "moves/moves.h"

namespace {
class EndTurn : public MoveRule {
public:
    void addMoves(Game&, std::vector<Action>& out) const override { out.push_back({ActionKind::EndTurn}); }

    void apply(Game& game, Action) const override {
        GameState& s = game.state();
        if (s.phase == Phase::Setup) {
            if (s.current == 0)
                s.current = 1;
            else
                game.beginPlay();
            return;
        }
        game.say("P", s.current + 1, " ends the turn");
        game.endTurn();
    }
};
}  // namespace

const MoveRule& endTurnRule() {
    static const EndTurn rule;
    return rule;
}
