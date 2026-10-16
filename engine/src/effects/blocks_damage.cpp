// Damage and healing: the attack's own damage, damage to chosen Pokémon, healing, Special Conditions.
#include <algorithm>

#include "effects/effect.h"
#include "game/game.h"

namespace {
class Hit : public Stmt {  // (hit): the attack's damage, at this point in its text
public:
    void run(EffectCtx& ctx) const override { hit(ctx); }
};

// (bonus N), (set-damage N): change the attack's damage before (hit).
class AttackDamage : public Stmt {
public:
    AttackDamage(bool set, const Num* n) : set_(set), n_(n) {}
    void run(EffectCtx& ctx) const override {
        int n = n_->eval(ctx);
        ctx.damage = set_ ? n : ctx.damage + n;
    }

private:
    bool set_;
    const Num* n_;
};

class Cancel : public Stmt {  // "this attack does nothing": no damage and nothing after it
public:
    void run(EffectCtx& ctx) const override {
        ctx.stopped = true;
        ctx.game.say("  the attack does nothing");
    }
};

// (damage TARGET N): during an attack, damage to the opponent's Pokémon goes through the damage pipeline
// (Weakness only for the Active one); anything else is put on directly.
class Damage : public Stmt {
public:
    Damage(const Target* t, const Num* n) : t_(t), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        int n = n_->eval(ctx);
        for (uint16_t uid : targets) {
            int owner = 0;
            if (!ctx.game.find(uid, &owner) || n <= 0) continue;
            if (ctx.attack && owner != ctx.player)
                ctx.game.dealAttackDamage(ctx.player, ctx.self, uid, n);
            else
                ctx.game.putDamage(uid, n);
        }
    }

private:
    const Target* t_;
    const Num* n_;
};

class Heal : public Stmt {  // (heal TARGET N); N = 999 heals everything
public:
    Heal(const Target* t, const Num* n) : t_(t), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        int n = n_->eval(ctx);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || !p->damage) continue;
            p->damage = std::max(0, p->damage - n);
            if (ctx.game.logging()) ctx.game.say("  healed -> ", ctx.game.show(*p, owner));
        }
    }

private:
    const Target* t_;
    const Num* n_;
};

// (status TARGET S…): only an Active Pokémon can have Special Conditions. Asleep, Paralyzed and
// Confused replace each other; Poisoned and Burned add on.
class SetStatus : public Stmt {
public:
    SetStatus(const Target* t, uint8_t mask) : t_(t), mask_(mask) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || p != &ctx.game.state().players[owner].active || !affects(ctx, uid)) continue;
            if (mask_ & ReplacingStatuses) p->status &= ~ReplacingStatuses;
            p->status |= mask_;
            if (ctx.game.logging()) ctx.game.say("  -> ", ctx.game.show(*p, owner));
        }
    }

private:
    const Target* t_;
    uint8_t mask_;
};

class Cure : public Stmt {  // (cure TARGET): remove all Special Conditions
public:
    explicit Cure(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets)
            if (PokemonSlot* p = ctx.game.find(uid)) p->status = 0;
    }

private:
    const Target* t_;
};
}  // namespace

void addDamageWords(Words& w) {
    w.stmt["hit"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Hit>(); };
    w.stmt["bonus"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<AttackDamage>(false, b.num(arg(e, 0))); };
    w.stmt["set-damage"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<AttackDamage>(true, b.num(arg(e, 0))); };
    w.stmt["cancel"] = [](const SExpr&, ScriptBuilder& b) { return b.make<Cancel>(); };
    w.stmt["damage"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Damage>(b.target(arg(e, 0)), b.num(arg(e, 1))); };
    w.stmt["heal"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Heal>(b.target(arg(e, 0)), b.num(arg(e, 1))); };
    w.stmt["status"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<SetStatus>(b.target(arg(e, 0)), parseStatuses(e, 1)); };
    w.stmt["cure"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Cure>(b.target(arg(e, 0))); };
}
