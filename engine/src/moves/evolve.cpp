// Evolving: a Stage 1 onto its Basic, a Stage 2 onto its Stage 1.
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
class Evolve : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const GameState& s = game.state();
        int me = s.current;
        const PlayerState& p = s.players[me];
        bool firstTurn = game.ownFirstTurn(me);  // nobody evolves on their own first turn…
        for (std::size_t h = 0; h < p.hand.size(); ++h) {
            const CardDef* evolution = p.hand[h];
            if (!evolution->isEvolution()) continue;
            // …nor a Pokémon that came into play or evolved this turn, unless it may evolve early (Eevee).
            auto fits = [&](const PokemonSlot& target) {
                bool early = game.hasMod(ModKind::EvolveEarly, target, me);
                bool line = target.card->name == evolution->evolvesFrom;
                game.forEachModOf(ModKind::EvolvesAs, target, me, [&](const Modifier& m) { line |= m.as && m.as->has(evolution->evolvesFrom); });
                return line && (early || (!firstTurn && target.enteredTurn < s.turn));
            };
            if (fits(p.active) && !game.playerHas(ModKind::CantEvolveActive, me)) out.push_back({ActionKind::Evolve, int8_t(h), -1});
            for (std::size_t i = 0; i < p.bench.size(); ++i)
                if (fits(p.bench[i])) out.push_back({ActionKind::Evolve, int8_t(h), int8_t(i)});
        }
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        PokemonSlot& target = game.slot(me, a.target);
        game.evolve(target, p.hand.take(a.index));
        game.say("P", me + 1, " evolves ", target.below.back()->name, a.target < 0 ? " (Active)" : " (bench)",
                 " into ", target.card->name);
        if (target.card->onEvolve && !game.abilitiesOff(target)) {
            EffectCtx ctx{game, me};
            ctx.self = target.uid;
            target.card->onEvolve->run(ctx);
        }
        game.knockOutCheck();  // e.g. a Basic that had Starting Plains' +20 HP evolves into less HP than its damage
    }
};
}  // namespace

const MoveRule& evolveRule() {
    static const Evolve rule;
    return rule;
}
