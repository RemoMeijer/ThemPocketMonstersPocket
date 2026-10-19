// Targets: which Pokémon an effect touches, and the numbers and tests that look at a Pokémon.
#include <algorithm>
#include <bit>
#include <string>
#include <vector>

#include "effects/effect.h"
#include "game/game.h"

namespace {
// Which Pokémon in a pool qualify: (type T…), (name "A" …), (has-energy N T…), basic, stage1, stage2,
// evolution, damaged, ex, not-ex, mega, has-ability, tool (has a Tool attached), not-self.
struct Filter {
    uint16_t types = 0, energyTypes = 0;
    uint8_t stages = 0;
    int energy = 0;  // at least this much energy of energyTypes (0 = no test)
    bool damaged = false, ex = false, notEx = false, mega = false, evolution = false, notSelf = false, notPicked = false, ability = false, tool = false;
    std::vector<std::string> names;
    std::string otherAbility;  // (other-ability "A"): it has an Ability not named A (Unown)

    bool matches(const PokemonSlot& p, const EffectCtx& ctx) const {
        int n = 0, owner = 0;
        if (energy) {
            ctx.game.find(p.uid, &owner);
            EnergyCounts counted = ctx.game.energyOf(p, owner);
            for (int t = 0; t < TypeCount; ++t)
                if (!energyTypes || (energyTypes & typeBit(Type(t)))) n += counted[t];
        }
        return p.hasType(types) && (!stages || (stages & stageBit(p.card->stage))) && (!damaged || p.damage > 0) &&
               (!ex || p.card->ex) && (!notEx || !p.card->ex) && (!mega || p.card->mega) && (!evolution || p.card->isEvolution()) &&
               (!notSelf || p.uid != ctx.self) && (!notPicked || p.uid != ctx.picked) && n >= energy && (!ability || !p.card->abilities.empty()) &&
               (!tool || !p.tools.empty()) &&
               (otherAbility.empty() || (!p.card->abilities.empty() && p.card->abilities[0].name != otherAbility)) &&
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
        else if (w == "not-ex") f.notEx = true;
        else if (w == "mega") f.mega = true;
        else if (w == "has-ability") f.ability = true;
        else if (w == "tool") f.tool = true;
        else if (w == "evolution") f.evolution = true;
        else if (w == "not-self") f.notSelf = true;
        else if (w == "not-picked") f.notPicked = true;
        else if (w == "basic") f.stages |= stageBit(Stage::Basic);
        else if (w == "stage1") f.stages |= stageBit(Stage::Stage1);
        else if (w == "stage2") f.stages |= stageBit(Stage::Stage2);
        else if (w == "type" && a.list) f.types |= typeMask(a, 0);
        else if (w == "has-energy" && a.list) f.energy = intArg(arg(a, 0)), f.energyTypes = typeMask(a, 1);
        else if (w == "other-ability" && a.list) f.otherAbility = arg(a, 0).atom;
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

    void candidates(EffectCtx& ctx, Uids& pool) const override {
        for (int k = 0; k < 2; ++k) {
            if ((side_ == 0 && k == 1) || (side_ == 1 && k == 0)) continue;
            const PlayerState& p = ctx.game.state().players[k == 0 ? ctx.player : 1 - ctx.player];
            if (zone_ != 1 && p.active.card && filter_.matches(p.active, ctx)) pool.push_back(p.active.uid);
            if (zone_ != 0)
                for (const PokemonSlot& b : p.bench)
                    if (filter_.matches(b, ctx)) pool.push_back(b.uid);
        }
    }

    void select(EffectCtx& ctx, Uids& out) const override {
        Uids pool;
        candidates(ctx, pool);
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
    enum Who { Self, Defending, MyActive, OppActive, Picked, Attacker };
    explicit Single(Who who) : who_(who) {}
    void select(EffectCtx& ctx, Uids& out) const override {
        const auto& players = ctx.game.state().players;
        uint16_t uid = who_ == Self        ? ctx.self
                       : who_ == Defending ? ctx.defending
                       : who_ == Picked    ? ctx.picked
                       : who_ == Attacker  ? ctx.attacker
                       : who_ == MyActive  ? players[ctx.player].active.uid
                                           : players[1 - ctx.player].active.uid;
        if (ctx.game.find(uid)) out.push_back(uid);
    }
    void candidates(EffectCtx& ctx, Uids& out) const override { select(ctx, out); }  // no choice involved

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
// (energy …) adds up every Pokémon T picks: "for each Energy attached to all of your Pokémon".
class PokemonNum : public Num {
public:
    enum What { Energy, DamageOn, RetreatCost, Count, EnergyTypes, StatusCount, HpLeft };
    PokemonNum(What what, const Target* t, uint16_t types) : what_(what), t_(t), types_(types) {}
    int eval(EffectCtx& ctx) const override {
        Uids u;
        t_->select(ctx, u);
        if (what_ == Count) return int(u.size());
        if (what_ == EnergyTypes) {  // different types of energy on them together (Juggler)
            uint16_t seen = 0;
            for (uint16_t uid : u)
                if (const PokemonSlot* p = ctx.game.find(uid))
                    for (int t = 0; t < TypeCount; ++t)
                        if (p->energy[t]) seen |= typeBit(Type(t));
            return std::popcount(seen);
        }
        if (what_ == Energy) {
            int n = 0, owner = 0;
            for (uint16_t uid : u)
                if (const PokemonSlot* p = ctx.game.find(uid, &owner)) {
                    EnergyCounts counted = ctx.game.energyOf(*p, owner);
                    for (int t = 0; t < TypeCount; ++t)
                        if (!types_ || (types_ & typeBit(Type(t)))) n += counted[t];
                }
            return n;
        }
        int owner = 0;
        const PokemonSlot* p = u.empty() ? nullptr : ctx.game.find(u[0], &owner);
        if (!p) return 0;
        if (what_ == StatusCount) return std::popcount(unsigned(p->status));
        if (what_ == HpLeft) return ctx.game.maxHp(*p, owner) - p->damage;
        return what_ == DamageOn ? p->damage : ctx.game.retreatCost(*p, owner);
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
                MovedToActive, EvolvedThisTurn, DamagedLastTurn, IsActive, Exists, HalfHp };
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
            case HasTool: return !p->tools.empty();
            case IsType: return p->hasType(types_);
            case IsEx: return p->card->ex;
            case IsBasic: return p->card->stage == Stage::Basic;
            case IsEvolution: return p->card->isEvolution();
            case MovedToActive: return p->becameActiveTurn == turn;
            case EvolvedThisTurn: return !p->below.empty() && p->enteredTurn == turn;
            case DamagedLastTurn: return p->damagedTurn == turn - 1;
            case IsActive: {
                int owner = 0;
                ctx.game.find(p->uid, &owner);
                return p == &ctx.game.state().players[owner].active;
            }  // as the Active, during the opponent's last turn
            case HalfHp: {  // "has half of its maximum HP or less remaining"
                int owner = 0;
                ctx.game.find(p->uid, &owner);
                return 2 * p->damage >= ctx.game.maxHp(*p, owner);
            }
            case EnergyAtLeast: {
                int n = 0, owner = 0;
                ctx.game.find(p->uid, &owner);
                EnergyCounts counted = ctx.game.energyOf(*p, owner);
                for (int t = 0; t < TypeCount; ++t)
                    if (!types_ || (types_ & typeBit(Type(t)))) n += counted[t];
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
        int owner = 0;
        ctx.game.find(p.uid, &owner);
        EnergyCounts energy = ctx.game.energyOf(p, owner);
        int typed = 0, spareOther = 0, typedCost = 0;
        for (int t = 0; t < TypeCount; ++t) {
            if (t == int(Type::Colorless)) continue;
            if (types_ & typeBit(Type(t))) {
                typed += energy[t];
                typedCost += cost[t];
            } else {
                spareOther += std::max(0, energy[t] - cost[t]);
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

class HasAttack : public Cond {  // (has-attack T "A"): the Pokémon has an attack named A (Psychic, the Supporter)
public:
    HasAttack(const Target* t, std::string name) : t_(t), name_(std::move(name)) {}
    bool test(EffectCtx& ctx) const override {
        const PokemonSlot* p = first(ctx, t_);
        if (!p) return false;
        for (const AttackDef& a : p->card->attacks)
            if (a.name == name_) return true;
        return false;
    }

private:
    const Target* t_;
    std::string name_;
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
                                                           {"picked", Single::Picked},
                                                           {"attacker", Single::Attacker}};
    for (auto [name, who] : singles) {
        auto whoCopy = who;
        w.target[name] = {0, 0, [whoCopy](const SExpr&, ScriptBuilder& b) { return b.make<Single>(whoCopy); }};
    }
    const std::pair<const char*, Pool::Mode> pools[] = {
        {"choose", Pool::Choose}, {"owner-chooses", Pool::OwnerChooses}, {"random", Pool::Random}, {"each", Pool::Each}};
    for (auto [name, mode] : pools) {
        auto modeCopy = mode;
        w.target[name] = {2, AnyCount, [modeCopy](const SExpr& e, ScriptBuilder& b) {
            const std::string& side = arg(e, 0).atom;
            const std::string& zone = arg(e, 1).atom;
            int s = side == "my" ? 0 : side == "opp" ? 1 : side == "both" ? 2 : -1;
            int z = zone == "active" ? 0 : zone == "bench" ? 1 : zone == "all" ? 2 : -1;
            if (s < 0 || z < 0) scriptFail("expected SIDE ZONE", e);
            return b.make<Pool>(modeCopy, s, z, parseFilter(e, 2));
        }};
    }

    w.num["energy"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::Energy, b.target(arg(e, 0)), typeMask(e, 1));
    }};
    w.num["hp-left"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::HpLeft, b.target(arg(e, 0)), 0);
    }};
    w.num["damage-on"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::DamageOn, b.target(arg(e, 0)), 0);
    }};
    w.num["status-count"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::StatusCount, b.target(arg(e, 0)), 0);
    }};
    w.num["retreat-cost"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonNum>(PokemonNum::RetreatCost, b.target(arg(e, 0)), 0);
    }};
    w.num["count"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {  // (count SIDE ZONE FILTER…)
        SExpr pool = e;
        pool.items[0].atom = "each";
        return b.make<PokemonNum>(PokemonNum::Count, b.target(pool), 0);
    }};
    w.num["energy-types"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {  // (energy-types SIDE ZONE FILTER…)
        SExpr pool = e;
        pool.items[0].atom = "each";
        return b.make<PokemonNum>(PokemonNum::EnergyTypes, b.target(pool), 0);
    }};
    w.cond["has-attack"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<HasAttack>(b.target(arg(e, 0)), arg(e, 1).atom);
    }};

    auto test = [&w](const char* name, PokemonTest::What what, std::size_t min = 1, std::size_t max = 1) {
        w.cond[name] = {min, max, [what](const SExpr& e, ScriptBuilder& b) {
            return b.make<PokemonTest>(what, b.target(arg(e, 0)), 0, typeMask(e, 1));
        }};
    };
    test("has-damage", PokemonTest::HasDamage);
    test("has-tool", PokemonTest::HasTool);
    test("is-type", PokemonTest::IsType, 2, AnyCount);
    test("is-ex", PokemonTest::IsEx);
    test("is-basic", PokemonTest::IsBasic);
    test("is-evolution", PokemonTest::IsEvolution);
    test("moved-to-active", PokemonTest::MovedToActive);
    test("evolved-this-turn", PokemonTest::EvolvedThisTurn);
    test("damaged-last-turn", PokemonTest::DamagedLastTurn);
    test("is-active", PokemonTest::IsActive);
    test("half-hp", PokemonTest::HalfHp);
    w.cond["has-status"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::HasStatus, b.target(arg(e, 0)), statusMask(e, 1), 0);
    }};
    // (energy-at-least T N [type…]) and (extra-energy T N type…)
    w.cond["energy-at-least"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::EnergyAtLeast, b.target(arg(e, 0)), intArg(arg(e, 1)), typeMask(e, 2));
    }};
    w.cond["extra-energy"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PokemonTest>(PokemonTest::ExtraEnergy, b.target(arg(e, 0)), intArg(arg(e, 1)), typeMask(e, 2));
    }};
    w.cond["exists"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {  // (exists SIDE ZONE FILTER…)
        SExpr pool = e;
        pool.items[0].atom = "each";
        return b.make<PokemonTest>(PokemonTest::Exists, b.target(pool), 0, 0);
    }};
}
