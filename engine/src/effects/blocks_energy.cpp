// Energy: extra energy from the Energy Zone, and discarding attached energy.
#include <algorithm>
#include <random>

#include "effects/effect.h"
#include "game/game.h"

namespace {
bool hasEnergy(const PokemonSlot& p, uint16_t types) {  // any energy of these types (0 = any type)
    for (int t = 0; t < TypeCount; ++t)
        if (p.energy[t] && (!types || (types & typeBit(Type(t))))) return true;
    return false;
}

// (attach-energy TARGET TYPE N): N new energy of TYPE from the Energy Zone, onto each target.
class AttachEnergy : public Stmt {
public:
    AttachEnergy(const Target* t, Type type, const Num* n) : t_(t), type_(type), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        int n = n_->eval(ctx);
        for (uint16_t uid : targets) ctx.game.attachFromZone(ctx.player, uid, type_, n);
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot&) { return true; });
    }

private:
    const Target* t_;
    Type type_;
    const Num* n_;
};

// (discard-energy TARGET TYPE|any N|all) and (discard-random-energy TARGET N).
class DiscardEnergy : public Stmt {
public:
    DiscardEnergy(const Target* t, uint16_t types, int count, bool random) : t_(t), types_(types), count_(count), random_(random) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || !affects(ctx, uid)) continue;
            int removed = 0;
            for (int i = 0; count_ < 0 || i < count_; ++i) {
                int t = pick(ctx, *p);
                if (t < 0) break;
                --p->energy[t];
                ++ctx.game.state().players[owner].discardEnergy[t];  // to its owner's discard pile
                ++removed;
            }
            if (removed && ctx.game.logging()) ctx.game.say("  ", removed, " energy discarded -> ", ctx.game.show(*p, owner));
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [this](const PokemonSlot& p) { return hasEnergy(p, types_); });
    }

private:
    // Which energy goes: a random one, or else the most plentiful matching type.
    // shortcut: the player doesn't choose which energy to discard; make it a choice once multi-type decks matter.
    int pick(EffectCtx& ctx, const PokemonSlot& p) const {
        int total = 0, best = -1;
        for (int t = 0; t < TypeCount; ++t) {
            if (types_ && !(types_ & typeBit(Type(t)))) continue;
            total += p.energy[t];
            if (p.energy[t] && (best < 0 || p.energy[t] > p.energy[best])) best = t;
        }
        if (!random_ || !total) return best;
        int k = std::uniform_int_distribution<int>(0, total - 1)(ctx.game.rng());
        for (int t = 0; t < TypeCount; ++t) {
            if (types_ && !(types_ & typeBit(Type(t)))) continue;
            if (k < p.energy[t]) return t;
            k -= p.energy[t];
        }
        return best;
    }

    const Target* t_;
    uint16_t types_;
    int count_;  // -1 = all
    bool random_;
};
// (move-energy FROM TO TYPE…|any N|all): move up to N energy (of those types) from each FROM Pokémon to TO.
class MoveEnergy : public Stmt {
public:
    MoveEnergy(const Target* from, const Target* to, uint16_t types, int count) : from_(from), to_(to), types_(types), count_(count) {}
    void run(EffectCtx& ctx) const override {
        Uids sources, dest;
        from_->select(ctx, sources);
        to_->select(ctx, dest);
        int owner = 0;
        PokemonSlot* to = dest.empty() ? nullptr : ctx.game.find(dest[0], &owner);
        if (!to) return;
        int moved = 0;
        for (uint16_t uid : sources) {
            PokemonSlot* from = ctx.game.find(uid);
            if (!from || from == to) continue;
            for (int t = 0; t < TypeCount && (count_ < 0 || moved < count_); ++t) {
                if (types_ && !(types_ & typeBit(Type(t)))) continue;
                while (from->energy[t] && (count_ < 0 || moved < count_)) {
                    --from->energy[t];
                    ++to->energy[t];
                    ++moved;
                }
            }
        }
        if (moved && ctx.game.logging()) ctx.game.say("  ", moved, " energy moved -> ", ctx.game.show(*to, owner));
    }
    bool possible(EffectCtx& ctx) const override {  // a source with matching energy, and somewhere else to put it
        Uids sources, dest;
        from_->candidates(ctx, sources);
        to_->candidates(ctx, dest);
        for (uint16_t s : sources) {
            const PokemonSlot* from = ctx.game.find(s);
            if (!from || !hasEnergy(*from, types_)) continue;
            for (uint16_t d : dest)
                if (d != s) return true;
        }
        return false;
    }

private:
    const Target* from_;
    const Target* to_;
    uint16_t types_;
    int count_;  // -1 = all
};
}  // namespace

void addEnergyWords(Words& w) {
    w.stmt["move-energy"] = [](const SExpr& e, ScriptBuilder& b) {
        const SExpr& count = arg(e, argCount(e) - 1);
        uint16_t types = 0;
        for (std::size_t i = 2; i + 1 < argCount(e); ++i)
            if (arg(e, i).atom != "any") types |= typeBit(typeArg(arg(e, i)));
        return b.make<MoveEnergy>(b.target(arg(e, 0)), b.target(arg(e, 1)), types, count.atom == "all" ? -1 : intArg(count));
    };
    w.stmt["attach-energy"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<AttachEnergy>(b.target(arg(e, 0)), typeArg(arg(e, 1)), argCount(e) > 2 ? b.num(arg(e, 2)) : b.num(atomExpr("1")));
    };
    w.stmt["discard-energy"] = [](const SExpr& e, ScriptBuilder& b) {
        const SExpr& type = arg(e, 1);
        const SExpr& count = arg(e, 2);
        uint16_t types = type.atom == "any" ? 0 : typeBit(typeArg(type));
        return b.make<DiscardEnergy>(b.target(arg(e, 0)), types, count.atom == "all" ? -1 : intArg(count), false);
    };
    w.stmt["discard-random-energy"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardEnergy>(b.target(arg(e, 0)), 0, argCount(e) > 1 ? intArg(arg(e, 1)) : 1, true);
    };
}
