// Attacking: pay the cost with attached energy, run the attack's effect (its damage included), and the turn ends.
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
// "…for each time your Pokémon used Sweets Relay during this game" (Alcremie): counted by the attack's name.
void countUse(PlayerState& p, const AttackDef& attack) {
    for (AttackUse& u : p.attackUses)
        if (u.attack->name == attack.name) return void(++u.count);
    // shortcut: a 25th different attack name in one game isn't counted; raise the capacity if a deck gets there
    if (!p.attackUses.full()) p.attackUses.push_back({&attack, 1});
}

class Attack : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PokemonSlot& active = game.state().players[me].active;
        std::size_t n = game.attacksOf(active, me).size();
        for (std::size_t i = 0; i < n; ++i)
            if (game.canUseAttack(active, me, int(i)) && canPay(game.energyOf(active, me), game.attackCost(active, me, int(i))))
                out.push_back({ActionKind::Attack, int8_t(i)});
    }

    void apply(Game& game, Action a) const override {
        GameState& s = game.state();
        int me = s.current;
        const PokemonSlot& attacker = s.players[me].active;
        uint16_t attackerUid = attacker.uid;
        const AttackDef& attack = *game.attacksOf(attacker, me)[std::size_t(a.index)];
        game.say("P", me + 1, "'s ", attacker.card->name, " uses ", attack.name);
        // Confused, or an effect like "if the Defending Pokémon tries to use an attack, your opponent flips
        // a coin": tails, the attack doesn't happen (assumed for Confused, see DESIGN.md §10).
        // Confusion's coin isn't for an effect, so Will doesn't make it heads (ruling).
        bool confused = attacker.status & Confused;
        bool mustFlip = confused || game.hasMod(ModKind::AttackFlip, attacker, me);
        if (mustFlip && !(confused ? game.flip() : game.effectFlip(me))) {
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
            if (PokemonSlot* used = game.find(attackerUid)) used->attacked = true;  // "the first time … used an attack"
            countUse(s.players[me], attack);
        }
        game.knockOutCheck(attackerUid);
        if (s.phase == Phase::Main) game.endTurn();
    }
};
}  // namespace

const MoveRule& attackRule() {
    static const Attack rule;
    return rule;
}
