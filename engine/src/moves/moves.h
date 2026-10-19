#pragma once
#include <vector>

#include "core/action.h"

class Game;

// One kind of move: which ones the player whose turn it is may make now, and what making one does.
// Each kind's rules live in one file here; the Game only runs the turns.
class MoveRule {
public:
    virtual ~MoveRule() = default;
    virtual void addMoves(Game& game, std::vector<Action>& out) const = 0;
    virtual void apply(Game& game, Action action) const = 0;
};

const MoveRule& moveRule(ActionKind kind);

// One per file in moves/.
const MoveRule& placeActiveRule();
const MoveRule& placeBenchRule();
const MoveRule& evolveRule();
const MoveRule& playTrainerRule();
const MoveRule& attachToolRule();
const MoveRule& attachEnergyRule();
const MoveRule& useAbilityRule();
const MoveRule& useStadiumRule();
const MoveRule& retreatRule();
const MoveRule& attackRule();
const MoveRule& endTurnRule();
