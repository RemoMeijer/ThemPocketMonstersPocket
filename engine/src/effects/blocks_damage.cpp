// Damage and healing: the attack's own damage, damage to chosen Pokémon, healing, Special Conditions.
#include <algorithm>
#include <array>

#include "effects/effect.h"
#include "game/game.h"

namespace {
class Hit : public Stmt {  // (hit [:no-weakness] [:ignore-effects]): the attack's damage, at this point in its text
public:
    Hit(bool weakness, bool ignore) : weakness_(weakness), ignore_(ignore) {}
    void run(EffectCtx& ctx) const override { hit(ctx, weakness_, ignore_); }

private:
    bool weakness_, ignore_;
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

// During an attack, damage to the opponent's Pokémon goes through the damage pipeline (Weakness only for
// the Active one); anything else is put on directly.
void damageOne(EffectCtx& ctx, uint16_t uid, int n) {
    int owner = 0;
    if (!ctx.game.find(uid, &owner) || n <= 0) return;
    if (ctx.attack && owner != ctx.player)
        return void(ctx.game.dealAttackDamage(ctx.player, ctx.self, uid, n));
    // A Pokémon's own effect on the opponent's Pokémon outside an attack: an Ability (Protective Poncho stops it).
    if (ctx.self && owner != ctx.player && ctx.game.hasMod(ModKind::PreventAbilityDamage, *ctx.game.find(uid), owner))
        return ctx.game.say("  the damage is prevented");
    ctx.game.putDamage(uid, n);
}

class Damage : public Stmt {  // (damage TARGET N)
public:
    Damage(const Target* t, const Num* n) : t_(t), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        int n = n_->eval(ctx);
        for (uint16_t uid : targets) damageOne(ctx, uid, n);
    }

private:
    const Target* t_;
    const Num* n_;
};

// (hits TIMES TARGET N): pick TARGET TIMES times, add up N per pick, then damage each picked Pokémon once,
// so Weakness and "+N damage" count once per Pokémon, not per pick (ruling: "chosen at random 3 times").
class Hits : public Stmt {
public:
    Hits(const Num* times, const Target* t, const Num* n) : times_(times), t_(t), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids hit;
        std::array<int, 2 * (1 + rules::BenchSize)> total{};  // damage per entry of `hit`
        int n = n_->eval(ctx);
        for (int i = times_->eval(ctx); i > 0; --i) {
            Uids picked;
            t_->select(ctx, picked);
            for (uint16_t uid : picked) {
                std::size_t k = std::find(hit.begin(), hit.end(), uid) - hit.begin();
                if (k == hit.size()) hit.push_back(uid);
                total[k] += n;
            }
        }
        for (std::size_t k = 0; k < hit.size(); ++k) damageOne(ctx, hit[k], total[k]);
    }

private:
    const Num* times_;
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
            if (!p || !p->damage || n <= 0 || ctx.game.hasMod(ModKind::NoHealing, *p, owner)) continue;  // Claydol
            p->damage = std::max(0, p->damage - n);
            ctx.healed = true;
            if (ctx.game.logging()) ctx.game.say("  healed -> ", ctx.game.show(*p, owner));
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [&](const PokemonSlot& p) {
            int owner = 0;
            ctx.game.find(p.uid, &owner);
            return p.damage > 0 && !ctx.game.hasMod(ModKind::NoHealing, p, owner);
        });
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
            uint8_t add = mask_ & uint8_t(~ctx.game.blockedStatuses(*p, owner));  // "can't be affected by…" (Hoothoot: Asleep)
            if (!add) continue;
            if (add & ReplacingStatuses) p->status &= ~ReplacingStatuses;
            p->status |= add;
            if (ctx.game.logging()) ctx.game.say("  -> ", ctx.game.show(*p, owner));
        }
    }

private:
    const Target* t_;
    uint8_t mask_;
};

// (set-hp-left TARGET N): "its remaining HP is now N" (never heals: assumed); (halve-hp TARGET): "halve its remaining
// HP, rounded down" to a whole 10 (assumed).
class SetHpLeft : public Stmt {
public:
    SetHpLeft(const Target* t, int n) : t_(t), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || !affects(ctx, uid)) continue;
            int hp = ctx.game.maxHp(*p, owner), left = hp - p->damage;
            int now = n_ >= 0 ? n_ : left / 2 / 10 * 10;
            if (now < left) p->damage = hp - now;
            if (ctx.game.logging()) ctx.game.say("  -> ", ctx.game.show(*p, owner));
        }
    }

private:
    const Target* t_;
    int n_;  // -1: half
};

// (status-random TARGET S…): one of these Special Conditions at random, not one it already has (Dustox).
class StatusRandom : public Stmt {
public:
    StatusRandom(const Target* t, uint8_t mask) : t_(t), mask_(mask) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || p != &ctx.game.state().players[owner].active || !affects(ctx, uid)) continue;
            std::vector<uint8_t> left;
            for (uint8_t bit = 1; bit && bit <= Burned; bit = uint8_t(bit << 1))
                if ((mask_ & bit) && !(p->status & bit)) left.push_back(bit);
            if (left.empty()) continue;
            uint8_t s = left[std::uniform_int_distribution<std::size_t>(0, left.size() - 1)(ctx.game.rng())];
            if (s & uint8_t(~ctx.game.blockedStatuses(*p, owner))) {
                if (s & ReplacingStatuses) p->status &= ~ReplacingStatuses;
                p->status |= s;
            }
            if (ctx.game.logging()) ctx.game.say("  -> ", ctx.game.show(*p, owner));
        }
    }

private:
    const Target* t_;
    uint8_t mask_;
};

class Dealt : public Num {  // (dealt): the damage (hit) did to the opponent's Active Pokémon
public:
    int eval(EffectCtx& ctx) const override { return ctx.dealt; }
};

class Healed : public Cond {  // (healed)
public:
    bool test(EffectCtx& ctx) const override { return ctx.healed; }
};

// (cure TARGET [S…]): remove those Special Conditions, or all of them. (cure-random TARGET): remove one at random.
class Cure : public Stmt {
public:
    Cure(const Target* t, uint8_t mask, bool random = false) : t_(t), mask_(mask), random_(random) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            PokemonSlot* p = ctx.game.find(uid);
            if (!p) continue;
            if (!random_) {
                p->status &= ~mask_;
                continue;
            }
            std::vector<uint8_t> has;
            for (uint8_t bit = 1; bit && bit <= Burned; bit <<= 1)
                if (p->status & bit) has.push_back(bit);
            if (!has.empty()) p->status &= ~has[std::uniform_int_distribution<std::size_t>(0, has.size() - 1)(ctx.game.rng())];
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [this](const PokemonSlot& p) { return (p.status & mask_) != 0; });
    }

private:
    const Target* t_;
    uint8_t mask_;
    bool random_;
};
}  // namespace

void addDamageWords(Words& w) {
    w.stmt["hit"] = {0, 2, [](const SExpr& e, ScriptBuilder& b) {
        bool weakness = true, ignore = false;
        for (std::size_t i = 0; i < argCount(e); ++i) {
            const std::string& o = arg(e, i).atom;
            if (o == ":no-weakness") weakness = false;
            else if (o == ":ignore-effects") ignore = true;
            else scriptFail("unknown hit option", e);
        }
        return b.make<Hit>(weakness, ignore);
    }};
    w.stmt["bonus"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<AttackDamage>(false, b.num(arg(e, 0))); }};
    w.stmt["set-damage"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<AttackDamage>(true, b.num(arg(e, 0))); }};
    w.stmt["cancel"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<Cancel>(); }};
    w.stmt["damage"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) { return b.make<Damage>(b.target(arg(e, 0)), b.num(arg(e, 1))); }};
    w.stmt["set-hp-left"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) { return b.make<SetHpLeft>(b.target(arg(e, 0)), intArg(arg(e, 1))); }};
    w.stmt["halve-hp"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<SetHpLeft>(b.target(arg(e, 0)), -1); }};
    w.stmt["status-random"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<StatusRandom>(b.target(arg(e, 0)), parseStatuses(e, 1));
    }};
    w.num["dealt"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<Dealt>(); }};
    w.stmt["hits"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<Hits>(b.num(arg(e, 0)), b.target(arg(e, 1)), b.num(arg(e, 2)));
    }};
    w.stmt["heal"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) { return b.make<Heal>(b.target(arg(e, 0)), b.num(arg(e, 1))); }};
    w.stmt["status"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) { return b.make<SetStatus>(b.target(arg(e, 0)), parseStatuses(e, 1)); }};
    w.cond["healed"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<Healed>(); }};
    w.stmt["cure-random"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<Cure>(b.target(arg(e, 0)), uint8_t(0xFF), true); }};
    w.stmt["cure"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<Cure>(b.target(arg(e, 0)), argCount(e) > 1 ? parseStatuses(e, 1) : uint8_t(0xFF));
    }};
}
