// Using a Pokémon's "once during your turn, you may…" ability.
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
class UseAbility : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const GameState& s = game.state();
        int me = s.current;
        for (int t = -1; t < int(s.players[me].bench.size()); ++t) {
            const PokemonSlot& p = game.slot(me, t);
            if (!p.card->activated || p.abilityTurn == s.turn || (p.card->activatedWhileActive && t >= 0)) continue;
            // Like a Trainer, an ability needs something to do: no Psy Shadow without a Psychic Active (Remco).
            EffectCtx ctx{game, me};
            ctx.self = p.uid;
            if (p.card->activated->possible(ctx)) out.push_back({ActionKind::UseAbility, 0, int8_t(t)});
        }
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PokemonSlot& p = game.slot(me, a.target);
        p.abilityTurn = game.state().turn;
        game.say("P", me + 1, "'s ", p.card->name, " uses ", p.card->abilities.empty() ? "its ability" : p.card->abilities[0].name);
        EffectCtx ctx{game, me};
        ctx.self = p.uid;
        p.card->activated->run(ctx);
        game.knockOutCheck();
        if (ctx.endsTurn && game.state().phase == Phase::Main) game.endTurn();
    }
};
}  // namespace

const MoveRule& useAbilityRule() {
    static const UseAbility rule;
    return rule;
}
