// Targets: which Pokémon an effect touches, and the numbers and tests that look at a Pokémon.
#include <algorithm>
#include <string>
#include <vector>

#include "effects/effect.h"
#include "game/game.h"

namespace {
// Which Pokémon in a pool qualify: (type T…), (stage S…), (name "A" …), basic, evolution, damaged, ex, not-self.
struct Filter {
    uint16_t types = 0;
    uint8_t stages = 0;
    bool damaged = false, ex = false, evolution = false, notSelf = false;
    std::vector<std::string> names;

    bool matches(const PokemonSlot& p, const EffectCtx& ctx) const {
        return p.card->hasType(types) && (!stages || (stages & stageBit(p.card->stage))) && (!damaged || p.damage > 0) &&
               (!ex || p.card->ex) && (!evolution || p.card->isEvolution()) && (!notSelf || p.uid != ctx.self) &&
               (names.empty() || std::find(names.begin(), names.end(), p.card->name) != names.end());
    }
};

Filter parseFilter(const SExpr& e, std::size_t from) {
    Filter f;
    for (std::size_t i = from; i < argCount(e); ++i) {
        const SExpr& a = arg(e, i);
        const std::string& w = a.list ? a.head() : a.atom;
        if (w == "damaged") f.damaged = true;
        else if (w == "ex") f.ex = true;
        else if (w == "evolution") f.evolution = true;
        else if (w == "not-self") f.notSelf = true;
        else if (w == "basic") f.stages |= stageBit(Stage::Basic);
        else if (w == "stage1") f.stages |= stageBit(Stage::Stage1);
        else if (w == "stage2") f.stages |= stageBit(Stage::Stage2);
        else if (w == "type" && a.list) f.types |= typeMask(a, 0);
        else if (w == "name" && a.list)
            for (std::size_t n = 0; n < argCount(a); ++n) f.names.push_back(arg(a, n).atom);
        else scriptFail("unknown filter '" + a.text() + "'", e);
    }
    return f;
}

// (choose|owner-chooses|random|each SIDE ZONE FILTER…): SIDE is my, opp or both; ZONE is active, bench or all.
// With choose the effect's controller picks; with owner-chooses the owner of those Pokémon does.
class Pool : public Target {
public:
    enum Mode { Choose, OwnerChooses, Random, Each };
    Pool(Mode mode, int side, int zone, Filter filter) : mode_(mode), side_(side), zone_(zone), filter_(std::move(filter)) {}

    void select(EffectCtx& ctx, Uids& out) const override {
        Uids pool;
        for (int k = 0; k < 2; ++k) {
            if ((side_ == 0 && k == 1) || (side_ == 1 && k == 0)) continue;
            const PlayerState& p = ctx.game.state().players[k == 0 ? ctx.player : 1 - ctx.player];
            if (zone_ != 1 && p.active.card && filter_.matches(p.active, ctx)) pool.push_back(p.active.uid);
            if (zone_ != 0)
                for (const PokemonSlot& b : p.bench)
                    if (filter_.matches(b, ctx)) pool.push_back(b.uid);
        }
        if (pool.empty()) return;
        if (mode_ == Each) {
            for (uint16_t uid : pool) out.push_back(uid);
            return;
        }
        int chooser = mode_ == OwnerChooses && side_ == 1 ? 1 - ctx.player : ctx.player;
        int i = mode_ == Random ? std::uniform_int_distribution<int>(0, int(pool.size()) - 1)(ctx.game.rng())
                                : ctx.game.choose(chooser, int(pool.size()));
        out.push_back(pool[i]);
        if (pool.size() > 1) ctx.game.say(mode_ == Random ? "  at random: " : "  chosen: ", ctx.game.find(pool[i])->card->name);
    }

private:
    Mode mode_;
    int side_, zone_;
    Filter filter_;
};

class Single : public Target {
public:
    enum Who { Self, Defending, MyActive, OppActive, Picked };
    explicit Single(Who who) : who_(who) {}
    void select(EffectCtx& ctx, Uids& out) const override {
        const auto& players = ctx.game.state().players;
        uint16_t uid = who_ == Self        ? ctx.self
                       : who_ == Defending ? ctx.defending
                       : who_ == Picked    ? ctx.picked
                       : who_ == MyActive  ? players[ctx.player].active.uid
                                           : players[1 - ctx.player].active.uid;
        if (ctx.game.find(uid)) out.push_back(uid);
    }

private:
    Who who_;
};

// The first Pokémon a target picks, for numbers and tests (they never ask for a choice).
const PokemonSlot* first(EffectCtx& ctx, const Target* t) {
    Uids u;
    t->select(ctx, u);
    return u.empty() ? nullptr : ctx.game.find(u[0]);
}

// Numbers about a Pokémon: (energy T [type]), (damage-on T), (retreat-cost T), and (count SIDE ZONE FILTER…).
class PokemonNum : public Num {
public:
    enum What { Energy, DamageOn, RetreatCost, Count };
    PokemonNum(What what, const Target* t, uint16_t types) : what_(what), t_(t), types_(types) {}
    int eval(EffectCtx& ctx) const override {
        if (what_ == Count) {
            Uids u;
            t_->select(ctx, u);
            return int(u.size());
        }
        int owner = 0;
        const PokemonSlot* p = first(ctx, t_);
        if (!p) return 0;
        ctx.game.find(p->uid, &owner);
        if (what_ == DamageOn) return p->damage;
        if (what_ == RetreatCost) return ctx.game.retreatCost(*p, owner);
        int n = 0;
        for (int t = 0; t < TypeCount; ++t)
            if (!types_ || (types_ & typeBit(Type(t)))) n += p->energy[t];
        return n;
    }

private:
    What what_;
    const Target* t_;
    uint16_t types_;
};

// Tests about a Pokémon.
class PokemonTest : public Cond {
public:
    enum What { HasDamage, HasStatus, HasTool, IsType, IsEx, IsBasic, IsEvolution, EnergyAtLeast, ExtraEnergy,
                MovedToActive, EvolvedThisTurn, Exists };
    PokemonTest(What what, const Target* t, int amount, uint16_t types) : what_(what), t_(t), amount_(amount), types_(types) {}

    bool test(EffectCtx& ctx) const override {
        if (what_ == Exists) {
            Uids u;
            t_->select(ctx, u);
            return !u.empty();
        }
        const PokemonSlot* p = first(ctx, t_);
        if (!p) return false;
        int turn = ctx.game.state().turn;
        switch (what_) {
            case HasDamage: return p->damage > 0;
            case HasStatus: return p->status & amount_;
            case HasTool: return p->tool != nullptr;
            case IsType: return p->card->hasType(types_);
            case IsEx: return p->card->ex;
            case IsBasic: return p->card->stage == Stage::Basic;
            case IsEvolution: return p->card->isEvolution();
            case MovedToActive: return p->becameActiveTurn == turn && p->enteredTurn != turn;
            case EvolvedThisTurn: return !p->below.empty() && p->enteredTurn == turn;
            case EnergyAtLeast: {
                int n = 0;
                for (int t = 0; t < TypeCount; ++t)
                    if (!types_ || (types_ & typeBit(Type(t)))) n += p->energy[t];
                return n >= amount_;
            }
            case ExtraEnergy: return extraEnergy(ctx, *p) >= amount_;
            default: return false;
        }
    }

private:
    // Energy of the given type left over after paying the attack's cost (other types pay Colorless first).
    int extraEnergy(EffectCtx& ctx, const PokemonSlot& p) const {
        if (!ctx.attackDef) return 0;
        const EnergyCounts& cost = ctx.attackDef->cost;
        int typed = 0, spareOther = 0, typedCost = 0;
        for (int t = 0; t < TypeCount; ++t) {
            if (t == int(Type::Colorless)) continue;
            if (types_ & typeBit(Type(t))) {
                typed += p.energy[t];
                typedCost += cost[t];
            } else {
                spareOther += std::max(0, p.energy[t] - cost[t]);
            }
        }
        int colorlessFromTyped = std::max(0, cost[int(Type::Colorless)] - spareOther);
        return typed - typedCost - colorlessFromTyped;
    }

    What what_;
    const Target* t_;
    int amount_;
    uint16_t types_;
};

uint8_t statusMask(const SExpr& e, std::size_t from) {
    uint8_t mask = 0;
    for (std::size_t i = from; i < argCount(e); ++i) {
        const std::string& s = arg(e, i).atom;
        if (s == "asleep") mask |= Asleep;
        else if (s == "paralyzed") mask |= Paralyzed;
        else if (s == "confused") mask |= Confused;
        else if (s == "poisoned") mask |= Poisoned;
        else if (s == "burned") mask |= Burned;
        else if (s == "any") mask |= 0xFF;
        else scriptFail("unknown Special Condition '" + s + "'", e);
    }
    return mask;
}
}  // namespace

uint8_t parseStatuses(const SExpr& e, std::size_t from) { return statusMask(e, from); }

void addTargetWords(Words& w) {
    const std::pair<const char*, Single::Who> singles[] = {{"self", Single::Self},
                                                           {"defending", Single::Defending},
                                                           {"my-active", Single::MyActive},
                                                           {"opp-active", Single::OppActive},
                                                           {"picked", Single::Picked}};
    for (auto [name, who] : singles) {
        auto whoCopy = who;
        w.target[name] = [whoCopy](const SExpr&, ScriptBuilder& b) { return b.make<Single>(whoCopy); };
    }
    const std::pair<const char*, Pool::Mode> pools[] = {
        {"choose", Pool::Choose}, {"owner-chooses", Pool::OwnerChooses}, {"random", Pool::Random}, {"each", Pool::Each}};
    for (auto [name, mode] : pools) {
        auto modeCopy = mode;
        w.target[name] = [modeCopy](const SExpr& e, ScriptBuilder& b) {
            const std::string& side = arg(e, 0).atom;
            const std::string& zone = arg(e, 1).atom;
            int s = side == "my" ? 0 : side == "opp" ? 1 : side == "both" ? 2 : -1;
            int z = zone == "active" ? 0 : zone == "bench" ? 1 : zone == "all" ? 2 : -1;
            if (s < 0 || z < 0) scriptFail("expected SIDE ZONE", e);
            return b.make<Pool>(modeCopy, s, z, parseFilter(e, 2));
        };
    }

    w.num["energy"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::Energy, b.target(arg(e, 0)), typeMask(e, 1));
    };
    w.num["damage-on"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::DamageOn, b.target(arg(e, 0)), 0);
    };
    w.num["retreat-cost"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::RetreatCost, b.target(arg(e, 0)), 0);
    };
    w.num["count"] = [](const SExpr& e, ScriptBuilder& b) {  // (count SIDE ZONE FILTER…)
        SExpr pool = e;
        pool.items[0].atom = "each";
        return b.make<PokemonNum>(PokemonNum::Count, b.target(pool), 0);
    };

    auto test = [&w](const char* name, PokemonTest::What what) {
        w.cond[name] = [what](const SExpr& e, ScriptBuilder& b) {
            return b.make<PokemonTest>(what, b.target(arg(e, 0)), 0, typeMask(e, 1));
        };
    };
    test("has-damage", PokemonTest::HasDamage);
    test("has-tool", PokemonTest::HasTool);
    test("is-type", PokemonTest::IsType);
    test("is-ex", PokemonTest::IsEx);
    test("is-basic", PokemonTest::IsBasic);
    test("is-evolution", PokemonTest::IsEvolution);
    test("moved-to-active", PokemonTest::MovedToActive);
    test("evolved-this-turn", PokemonTest::EvolvedThisTurn);
    w.cond["has-status"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::HasStatus, b.target(arg(e, 0)), statusMask(e, 1), 0);
    };
    // (energy-at-least T N [type…]) and (extra-energy T N type…)
    w.cond["energy-at-least"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::EnergyAtLeast, b.target(arg(e, 0)), intArg(arg(e, 1)), typeMask(e, 2));
    };
    w.cond["extra-energy"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::ExtraEnergy, b.target(arg(e, 0)), intArg(arg(e, 1)), typeMask(e, 2));
    };
    w.cond["exists"] = [](const SExpr& e, ScriptBuilder& b) {  // (exists SIDE ZONE FILTER…)
        SExpr pool = e;
        pool.items[0].atom = "each";
        return b.make<PokemonTest>(PokemonTest::Exists, b.target(pool), 0, 0);
    };
}
