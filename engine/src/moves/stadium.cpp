// Using the Stadium in play: "Once during each player's turn, that player may…". Once per player per turn
// and per Stadium card; a new Stadium can be used again the same turn (App, DESIGN.md §10).
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
class UseStadium : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const GameState& s = game.state();
        const PlayerState& p = s.players[s.current];
        if (!s.stadium || !s.stadium->stadiumAction) return;
        if (p.stadiumUseTurn == s.turn && p.stadiumUsed == s.stadium) return;
        EffectCtx ctx{game, s.current};
        if (s.stadium->stadiumAction->possible(ctx)) out.push_back({ActionKind::UseStadium});
    }

    void apply(Game& game, Action) const override {
        GameState& s = game.state();
        int me = s.current;
        PlayerState& p = s.players[me];
        p.stadiumUseTurn = s.turn;
        p.stadiumUsed = s.stadium;
        game.say("P", me + 1, " uses ", s.stadium->name);
        EffectCtx ctx{game, me};
        s.stadium->stadiumAction->run(ctx);
        game.knockOutCheck();
    }
};
}  // namespace

const MoveRule& useStadiumRule() {
    static const UseStadium rule;
    return rule;
}
