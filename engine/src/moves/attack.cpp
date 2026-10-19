// Attacking: pay the cost with attached energy, run the attack's effect (its damage included), and the turn ends.
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
class Attack : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PokemonSlot& active = game.state().players[me].active;
        for (std::size_t i = 0; i < active.card->attacks.size(); ++i)
            if (game.canUseAttack(active, me, int(i)) && canPay(active.energy, game.attackCost(active, me, int(i))))
                out.push_back({ActionKind::Attack, int8_t(i)});
    }

    void apply(Game& game, Action a) const override {
        GameState& s = game.state();
        int me = s.current;
        const PokemonSlot& attacker = s.players[me].active;
        const AttackDef& attack = attacker.card->attacks[a.index];
        game.say("P", me + 1, "'s ", attacker.card->name, " uses ", attack.name);
        // Confused, or an effect like "if the Defending Pokémon tries to use an attack, your opponent flips
        // a coin": tails, the attack doesn't happen (assumed for Confused, see DESIGN.md §10).
        bool mustFlip = (attacker.status & Confused) || game.hasMod(ModKind::AttackFlip, attacker, me);
        if (mustFlip && !game.flip()) {
            game.say("  the attack doesn't happen");
        } else {
            EffectCtx ctx{game, me};
            ctx.self = attacker.uid;
            ctx.defending = s.players[1 - me].active.uid;
            ctx.attack = true;
            ctx.attackDef = &attack;
            ctx.damage = attack.damage;
            if (attack.script)
                attack.script->run(ctx);
            else
                hit(ctx);
            s.players[me].attackTurn = s.turn;  // after the attack, which may ask "used … during your last turn"
            s.players[me].lastAttack = &attack;
        }
        game.knockOutCheck(true);
        if (s.phase == Phase::Main) game.endTurn();
    }
};
}  // namespace

const MoveRule& attackRule() {
    static const Attack rule;
    return rule;
}
