// Turning script text into blocks, plus the helpers the blocks share.
#include <algorithm>

#include "effects/effect.h"
#include "game/game.h"

namespace {
const Words& words() {
    static const Words all = [] {
        Words w;
        addFlowWords(w);
        addTargetWords(w);
        addDamageWords(w);
        addEnergyWords(w);
        addCardWords(w);
        return w;
    }();
    return all;
}

bool isInteger(const std::string& s) {
    std::size_t start = !s.empty() && s[0] == '-' ? 1 : 0;
    return s.size() > start && std::all_of(s.begin() + start, s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

template <class T>
const T* build(const std::unordered_map<std::string, Entry<T>>& map, const SExpr& e, ScriptBuilder& b, const char* kind) {
    const std::string& word = e.list ? e.head() : e.atom;
    auto it = map.find(word);
    if (it == map.end()) scriptFail(std::string("unknown ") + kind + " '" + word + "'", e);
    std::size_t n = argCount(e);
    if (n < it->second.min || n > it->second.max) scriptFail("wrong number of arguments for '" + word + "'", e);
    return it->second.build(e, b);
}

class Literal : public Num {
public:
    explicit Literal(int v) : v_(v) {}
    int eval(EffectCtx&) const override { return v_; }

private:
    int v_;
};
}  // namespace

const Stmt* ScriptBuilder::stmt(const SExpr& e) { return build(words().stmt, e, *this, "statement"); }
const Cond* ScriptBuilder::cond(const SExpr& e) { return build(words().cond, e, *this, "condition"); }
const Target* ScriptBuilder::target(const SExpr& e) { return build(words().target, e, *this, "target"); }

const Num* ScriptBuilder::num(const SExpr& e) {
    if (!e.list && isInteger(e.atom)) return make<Literal>(std::stoi(e.atom));
    return build(words().num, e, *this, "number");
}

// (KIND [amount] [:active] [:bench] [:min-retreat N] [:scope S] [:type T…] [:from T…] [:stage S…] [:name "A"…] [:needs "A"…] [:vs-ex] [:from-ex] [:not-mega]
//  [:any-target] [:attack "A"…])
Modifier ScriptBuilder::modifier(const SExpr& e, ModScope defaultScope) {
    static const std::unordered_map<std::string, ModKind> kinds = {
        {"damage-taken", ModKind::DamageTaken},     {"damage-dealt", ModKind::DamageDealt},
        {"prevent-damage", ModKind::PreventDamage}, {"prevent-all", ModKind::PreventAll},
        {"prevent-flip", ModKind::PreventFlip},     {"reflect", ModKind::Reflect},
        {"hp", ModKind::Hp},                        {"retreat-cost", ModKind::RetreatCost},
        {"no-retreat-cost", ModKind::NoRetreatCost}, {"cant-retreat", ModKind::CantRetreat},
        {"cant-attack", ModKind::CantAttack},       {"cant-use-attack", ModKind::CantUseAttack},
        {"attack-cost", ModKind::AttackCost},       {"attack-flip", ModKind::AttackFlip},
        {"cant-supporter", ModKind::CantPlaySupporter}, {"cant-item", ModKind::CantPlayItem},
        {"cant-attach-to-active", ModKind::CantAttachToActive}, {"weakness-x2", ModKind::WeaknessTimesTwo},
        {"no-conditions", ModKind::NoConditions},   {"prevent-effects", ModKind::PreventEffects},
        {"prevent-ability-damage", ModKind::PreventAbilityDamage},
        {"extra-tools", ModKind::ExtraTools}, {"reflip-attack", ModKind::ReflipAttack}, {"reflip-trainer", ModKind::ReflipTrainer},       {"survive-at", ModKind::SurviveAt},
        {"extra-point", ModKind::ExtraPoint},       {"cant-stadium", ModKind::CantPlayStadium},
        {"cant-evolve-active", ModKind::CantEvolveActive}, {"no-healing", ModKind::NoHealing},
        {"poison-damage", ModKind::PoisonDamage},   {"prevent-first-hit", ModKind::PreventFirstHit},
        {"evolve-early", ModKind::EvolveEarly},     {"evolves-as", ModKind::EvolvesAs},
        {"no-abilities", ModKind::NoAbilities},     {"extra-type", ModKind::ExtraType},
        {"double-energy", ModKind::DoubleEnergy},   {"previous-attacks", ModKind::PreviousAttacks},
        {"no-weakness", ModKind::NoWeakness},       {"cant-trainer", ModKind::CantPlayTrainer},
        {"cant-evolve", ModKind::CantEvolve},       {"extra-pick", ModKind::ExtraPick},
        {"asleep-on-attach", ModKind::AsleepOnAttach}, {"retreat-trap", ModKind::RetreatTrap},
    };
    static const std::unordered_map<std::string, ModScope> scopes = {
        {"self", ModScope::Self},           {"my-active", ModScope::MyActive}, {"my-all", ModScope::MyAll},
        {"opp-active", ModScope::OppActive}, {"opp-all", ModScope::OppAll},   {"all", ModScope::All},
    };
    static const std::unordered_map<std::string, Stage> stages = {
        {"basic", Stage::Basic}, {"stage1", Stage::Stage1}, {"stage2", Stage::Stage2}};
    const std::string& word = e.list ? e.head() : e.atom;
    auto kind = kinds.find(word);
    if (kind == kinds.end()) scriptFail("unknown modifier '" + word + "'", e);
    Modifier m;
    m.kind = kind->second;
    m.scope = playerWide(m.kind) ? ModScope::MyAll : defaultScope;
    for (std::size_t i = 0; i < argCount(e); ++i) {
        const SExpr& a = arg(e, i);
        auto values = [&](auto fn) {  // the atoms after a keyword, up to the next keyword
            while (i + 1 < argCount(e) && !arg(e, i + 1).list && arg(e, i + 1).atom[0] != ':') fn(arg(e, ++i));
        };
        if (!a.list && isInteger(a.atom)) {
            m.amount = int16_t(std::stoi(a.atom));
        } else if (a.atom == ":active") {
            m.whileActive = true;
        } else if (a.atom == ":bench") {
            m.whileBenched = true;
        } else if (a.atom == ":min-retreat" && i + 1 < argCount(e)) {
            m.minRetreat = uint8_t(intArg(arg(e, ++i)));
        } else if (a.atom == ":vs-ex") {
            m.vsEx = true;
        } else if (a.atom == ":from-ex") {
            m.fromEx = true;
        } else if (a.atom == ":from-basic") {
            m.fromBasic = true;
        } else if (a.atom == ":per-point") {
            m.perPoint = true;
        } else if (a.atom == ":trait" && i + 1 < argCount(e)) {
            m.traits |= traitFromName(arg(e, ++i).atom);
            if (!m.traits) scriptFail("unknown trait", e);
        } else if (a.atom == ":not-mega") {
            m.notMega = true;
        } else if (a.atom == ":flip") {
            m.flip = true;
        } else if (a.atom == ":any-target") {
            m.anyTarget = true;
        } else if (a.atom == ":energy" && i + 1 < argCount(e)) {
            m.energy = int8_t(typeArg(arg(e, ++i)));
        } else if (a.atom == ":if" && i + 1 < argCount(e)) {
            m.when = cond(arg(e, ++i));
        } else if (a.atom == ":name" || a.atom == ":needs" || a.atom == ":evolves-from" || a.atom == ":as" || a.atom == ":attack") {
            auto list = std::make_shared<NameList>();
            values([&](const SExpr& n) { list->names.push_back(n.atom); });
            arena_.push_back(list);
            (a.atom == ":name" ? m.names : a.atom == ":needs" ? m.needs : a.atom == ":as" ? m.as
                                 : a.atom == ":attack" ? m.attacks : m.evolvesFrom) = list.get();
        } else if (a.atom == ":with-energy") {
            m.withEnergy = true;
            values([&](const SExpr& t) { m.withEnergyTypes |= typeBit(typeArg(t)); });
        } else if (a.atom == ":status") {  // NoConditions: only these Special Conditions (Hoothoot)
            values([&](const SExpr& t) {
                static const std::unordered_map<std::string, int> bits = {
                    {"asleep", Asleep}, {"paralyzed", Paralyzed}, {"confused", Confused}, {"poisoned", Poisoned}, {"burned", Burned}};
                auto bit = bits.find(t.atom);
                if (bit == bits.end()) scriptFail("unknown Special Condition", e);
                m.amount = int16_t(m.amount | bit->second);
            });
        } else if (a.atom == ":scope" && i + 1 < argCount(e)) {
            auto s = scopes.find(arg(e, ++i).atom);
            if (s == scopes.end()) scriptFail("unknown scope", e);
            m.scope = s->second;
        } else if (a.atom == ":type") {
            values([&](const SExpr& t) { m.types |= typeBit(typeArg(t)); });
        } else if (a.atom == ":from") {
            values([&](const SExpr& t) { m.fromTypes |= typeBit(typeArg(t)); });
        } else if (a.atom == ":stage") {
            values([&](const SExpr& t) {
                auto s = stages.find(t.atom);
                if (s == stages.end()) scriptFail("unknown stage", e);
                m.stages |= stageBit(s->second);
            });
        } else {
            scriptFail("unknown modifier option '" + a.text() + "'", e);
        }
    }
    return m;
}

void scriptFail(const std::string& why, const SExpr& e) { throw ScriptError(why + " in " + e.text()); }

const SExpr& arg(const SExpr& e, std::size_t i) {
    if (!e.list || i + 1 >= e.items.size()) scriptFail("missing argument " + std::to_string(i + 1), e);
    return e.items[i + 1];
}

std::size_t argCount(const SExpr& e) { return e.list ? e.items.size() - 1 : 0; }

int intArg(const SExpr& e) {
    if (e.list || !isInteger(e.atom)) scriptFail("expected a number", e);
    return std::stoi(e.atom);
}

Type typeArg(const SExpr& e) {
    auto t = e.list ? std::nullopt : typeFromName(e.atom);
    if (!t) scriptFail("expected an energy type", e);
    return *t;
}

uint16_t typeMask(const SExpr& e, std::size_t from) {
    uint16_t mask = 0;
    for (std::size_t i = from; i < argCount(e); ++i) mask |= typeBit(typeArg(arg(e, i)));
    return mask;
}

bool oppArg(const SExpr& e) {
    if (e.list || (e.atom != "my" && e.atom != "opp")) scriptFail("expected my or opp", e);
    return e.atom == "opp";
}

void hit(EffectCtx& ctx, bool weakness, bool ignoreDefender) {
    if (ctx.stopped || ctx.damage <= 0) return;
    const PokemonSlot& defender = ctx.game.state().players[1 - ctx.player].active;
    if (defender.card) ctx.dealt += ctx.game.dealAttackDamage(ctx.player, ctx.self, defender.uid, ctx.damage, weakness, ignoreDefender);
}

bool affects(EffectCtx& ctx, uint16_t uid) {
    int owner = -1;
    if (!ctx.game.find(uid, &owner)) return false;
    if (ctx.attack && owner != ctx.player && ctx.game.protectedFromAttacks(uid)) {
        ctx.game.say("  the effect is prevented");
        return false;
    }
    return true;
}

bool anyCandidate(EffectCtx& ctx, const Target* t, const std::function<bool(const PokemonSlot&)>& test) {
    Uids pool;
    t->candidates(ctx, pool);
    for (uint16_t uid : pool)
        if (const PokemonSlot* p = ctx.game.find(uid); p && test(*p)) return true;
    return false;
}
