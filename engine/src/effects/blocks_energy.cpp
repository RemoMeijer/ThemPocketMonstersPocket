// Energy: extra energy from the Energy Zone, and discarding attached energy.
#include <algorithm>
#include <random>

#include "effects/effect.h"
#include "game/game.h"

namespace {
int matching(const PokemonSlot& p, uint16_t types) {  // energy of these types attached (0 = any type)
    int n = 0;
    for (int t = 0; t < TypeCount; ++t)
        if (!types || (types & typeBit(Type(t)))) n += p.energy[t];
    return n;
}

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

// (attach-from-discard TARGET TYPE N): up to N energy of TYPE from your discard pile onto each target. Not from
// the Energy Zone, so "whenever you attach … from your Energy Zone" abilities don't trigger.
class AttachFromDiscard : public Stmt {
public:
    AttachFromDiscard(const Target* t, int type, int n) : t_(t), type_(type), n_(n) {}  // type -1: any (Dragonair)
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        EnergyCounts& piles = ctx.game.state().players[ctx.player].discardEnergy;
        // shortcut: "an Energy" takes the most plentiful type in the discard pile; the player should pick.
        int type = type_ >= 0 ? type_ : int(std::max_element(piles.begin(), piles.end()) - piles.begin());
        auto& pile = piles[type];
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            int k = std::min<int>(n_, pile);
            if (!p || !k) continue;
            pile -= k;
            p->energy[type] += k;
            if (ctx.game.logging()) ctx.game.say("  ", k, " ", typeName(Type(type)), " energy from the discard pile -> ", ctx.game.show(*p, owner));
        }
    }
    bool possible(EffectCtx& ctx) const override {
        const EnergyCounts& piles = ctx.game.state().players[ctx.player].discardEnergy;
        return (type_ >= 0 ? piles[type_] > 0 : std::any_of(piles.begin(), piles.end(), [](uint8_t n) { return n > 0; })) &&
               anyCandidate(ctx, t_, [](const PokemonSlot&) { return true; });
    }

private:
    const Target* t_;
    int type_;
    int n_;
};

// (cycle-energy): the energy waiting in your Energy Zone goes to the discard pile, the shown next one comes up
// and a new next one is rolled, like an extra turn for the zone (Rainbow Cave, Remco).
class CycleEnergy : public Stmt {
public:
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        if (!p.energy) return;
        ++p.discardEnergy[int(*p.energy)];
        p.energy = p.nextEnergy;
        p.nextEnergy = ctx.game.rollEnergy(p);
        ctx.game.say("  Energy Zone: ", typeName(*p.energy), " (next: ", typeName(*p.nextEnergy), ")");
    }
    bool possible(EffectCtx& ctx) const override { return ctx.game.state().players[ctx.player].energy.has_value(); }
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
        return random_ && total ? randomEnergy(ctx, p, types_) : best;
    }

    const Target* t_;
    uint16_t types_;
    int count_;  // -1 = all
    bool random_;
};
// (move-energy FROM TO TYPE…|any N|all): move up to N energy (of those types) from the FROM Pokémon to TO.
// (move-random-energy FROM TO N): the same with random energy.
// shortcut: move-energy takes the first matching type, not the player's pick (Remco: the player picks); see PROGRESS.md step 4.
class MoveEnergy : public Stmt {
public:
    MoveEnergy(const Target* from, const Target* to, uint16_t types, int count, bool random = false)
        : from_(from), to_(to), types_(types), count_(count), random_(random) {}
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
            while (count_ < 0 || moved < count_) {
                int t = random_ ? randomEnergy(ctx, *from, types_) : -1;
                for (int k = 0; !random_ && k < TypeCount && t < 0; ++k)
                    if (from->energy[k] && (!types_ || (types_ & typeBit(Type(k))))) t = k;
                if (t < 0) break;
                --from->energy[t];
                ++to->energy[t];
                ++moved;
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
    bool random_;
};

// (discard-energy-among TARGET N): N times, a random energy from among all the energy on the TARGET Pokémon
// together, to its owner's discard pile (Team Star Grunt).
class DiscardEnergyAmong : public Stmt {
public:
    DiscardEnergyAmong(const Target* t, int count) : t_(t), count_(count) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (int i = 0; i < count_; ++i) {
            std::vector<std::pair<uint16_t, int>> pool;  // Pokémon and how much energy it has
            int total = 0;
            for (uint16_t uid : targets)
                if (const PokemonSlot* p = ctx.game.find(uid); p && matching(*p, 0) && affects(ctx, uid)) {
                    pool.push_back({uid, matching(*p, 0)});
                    total += pool.back().second;
                }
            if (!total) return;
            int k = std::uniform_int_distribution<int>(0, total - 1)(ctx.game.rng());
            std::size_t j = 0;
            for (; k >= pool[j].second; ++j) k -= pool[j].second;
            int owner = 0;
            PokemonSlot* p = ctx.game.find(pool[j].first, &owner);
            int t = randomEnergy(ctx, *p, 0);
            --p->energy[t];
            ++ctx.game.state().players[owner].discardEnergy[t];
            if (ctx.game.logging()) ctx.game.say("  1 energy discarded -> ", ctx.game.show(*p, owner));
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot& p) { return hasEnergy(p, 0); });
    }

private:
    const Target* t_;
    int count_;
};
}  // namespace

int randomEnergy(EffectCtx& ctx, const PokemonSlot& p, uint16_t types) {
    int total = matching(p, types);
    if (!total) return -1;
    int k = std::uniform_int_distribution<int>(0, total - 1)(ctx.game.rng());
    for (int t = 0; t < TypeCount; ++t) {
        if (types && !(types & typeBit(Type(t)))) continue;
        if (k < p.energy[t]) return t;
        k -= p.energy[t];
    }
    return -1;
}

void addEnergyWords(Words& w) {
    w.stmt["move-energy"] = {4, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        const SExpr& count = arg(e, argCount(e) - 1);
        uint16_t types = 0;
        for (std::size_t i = 2; i + 1 < argCount(e); ++i)
            if (arg(e, i).atom != "any") types |= typeBit(typeArg(arg(e, i)));
        return b.make<MoveEnergy>(b.target(arg(e, 0)), b.target(arg(e, 1)), types, count.atom == "all" ? -1 : intArg(count));
    }};
    w.stmt["move-random-energy"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<MoveEnergy>(b.target(arg(e, 0)), b.target(arg(e, 1)), 0, intArg(arg(e, 2)), true);
    }};
    w.stmt["discard-energy-among"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardEnergyAmong>(b.target(arg(e, 0)), intArg(arg(e, 1)));
    }};
    w.stmt["cycle-energy"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<CycleEnergy>(); }};
    w.stmt["attach-from-discard"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<AttachFromDiscard>(b.target(arg(e, 0)), arg(e, 1).atom == "any" ? -1 : int(typeArg(arg(e, 1))), intArg(arg(e, 2)));
    }};
    w.stmt["attach-energy"] = {2, 3, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<AttachEnergy>(b.target(arg(e, 0)), typeArg(arg(e, 1)), argCount(e) > 2 ? b.num(arg(e, 2)) : b.num(atomExpr("1")));
    }};
    w.stmt["discard-energy"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        const SExpr& type = arg(e, 1);
        const SExpr& count = arg(e, 2);
        uint16_t types = type.atom == "any" ? 0 : typeBit(typeArg(type));
        return b.make<DiscardEnergy>(b.target(arg(e, 0)), types, count.atom == "all" ? -1 : intArg(count), false);
    }};
    w.stmt["discard-random-energy"] = {1, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardEnergy>(b.target(arg(e, 0)), 0, argCount(e) > 1 ? intArg(arg(e, 1)) : 1, true);
    }};
}
