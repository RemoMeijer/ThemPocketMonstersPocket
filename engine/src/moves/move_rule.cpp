#include <stdexcept>

#include "moves/moves.h"

namespace {
// Choices inside effects are answered through Game::choose(), never offered as a move of their own.
class ChooseRule : public MoveRule {
public:
    void addMoves(Game&, std::vector<Action>&) const override {}
    void apply(Game&, Action) const override { throw std::logic_error("Choose is only an answer to Game::choose()"); }
};
}  // namespace

const MoveRule& moveRule(ActionKind kind) {
    static const ChooseRule choose;
    static const MoveRule* const rules[ActionKindCount] = {
        &placeActiveRule(),  &placeBenchRule(), &evolveRule(),  &playTrainerRule(),
        &attachToolRule(),   &attachEnergyRule(), &useAbilityRule(), &useStadiumRule(),
        &retreatRule(),      &attackRule(),     &endTurnRule(),    &choose,
    };
    return *rules[int(kind)];
}
