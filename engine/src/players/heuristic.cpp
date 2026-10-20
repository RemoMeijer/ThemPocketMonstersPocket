#include "players/heuristic.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
// What a card does, by words in its effect scripts. A card can be several.
enum Kind : uint16_t {
    Gust = 1,            // I pick which of their Benched Pokémon comes up
    GustOppChooses = 2,  // they pick (Sabrina)
    SwitchOwn = 4,
    Heal = 8,
    Boost = 16,  // more damage this turn
    RetreatHelp = 32,
    EnergyHelp = 64,
    Draw = 128,  // draws or searches
    RareCandy = 256,
};

uint16_t classify(const std::string& s) {
    auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
    uint16_t k = 0;
    if (has("(switch-in (choose opp")) k |= Gust;
    if (has("(switch-in (owner-chooses opp")) k |= GustOppChooses;
    if (has("(switch-in (choose my")) k |= SwitchOwn;
    if (has("(heal")) k |= Heal;
    if (has("damage-dealt") && has("this-turn")) k |= Boost;
    if (has("retreat-cost -") || has("no-retreat-cost")) k |= RetreatHelp;
    if (has("attach-energy") || has("move-energy") || has("attach-from-discard")) k |= EnergyHelp;
    if (has("(draw") || has("(search") || has("(dig") || has("shuffle-hand-draw")) k |= Draw;
    if (has("(rare-candy")) k |= RareCandy;
    return k;
}

using InPlay = FixedVec<const PokemonSlot*, 1 + rules::BenchSize>;

InPlay inPlay(const PlayerView& v, int player) {
    InPlay out;
    if (v.active(player).card) out.push_back(&v.active(player));
    for (const PokemonSlot& b : v.bench(player)) out.push_back(&b);
    return out;
}

const PokemonSlot* locate(const PlayerView& v, uint16_t uid, int* owner) {
    for (int p : {0, 1})
        for (const PokemonSlot* s : inPlay(v, p))
            if (s->uid == uid) return *owner = p, s;
    return nullptr;
}

// E: the printed damage plus Weakness, as if `def` were Active; effects ignored.
int expected(const PokemonSlot& att, const AttackDef* a, const PokemonSlot& def) {
    if (a->damage <= 0) return 0;
    return a->damage + (att.card->type && att.card->type == def.card->weakness ? rules::WeaknessBonus : 0);
}

// How many more Energy `have` needs for `cost`: the typed ones short, then Colorless from what's left over.
int missing(const EnergyCounts& have, const EnergyCounts& cost) {
    int shortBy = 0, spare = 0;
    for (int t = 0; t < TypeCount; ++t) {
        if (t == int(Type::Colorless)) continue;
        shortBy += std::max(0, cost[t] - have[t]);
        spare += std::max(0, have[t] - cost[t]);
    }
    return shortBy + std::max(0, cost[int(Type::Colorless)] - spare - have[int(Type::Colorless)]);
}

// The best E of p's attacks usable now against def, -1 when it has none.
int bestPayable(const PlayerView& v, const PokemonSlot& p, int owner, const PokemonSlot& def) {
    int best = -1;
    auto atk = v.attacks(p, owner);
    for (std::size_t i = 0; i < atk.size(); ++i)
        if (v.canAttack(p, owner, int(i))) best = std::max(best, expected(p, atk[i], def));
    return best;
}

// Its biggest attack (most printed damage, the first on a tie), -1 when it has none.
int biggest(const PlayerView& v, const PokemonSlot& p, int owner) {
    auto atk = v.attacks(p, owner);
    int big = -1;
    for (std::size_t i = 0; i < atk.size(); ++i)
        if (big < 0 || atk[i]->damage > atk[std::size_t(big)]->damage) big = int(i);
    return big;
}

int missingFor(const PlayerView& v, const PokemonSlot& p, int owner, int attack) {
    return missing(v.energy(p, owner), v.attackCost(p, owner, attack));
}

// Whether one more Energy (of `type`, or any type) brings p closer to its biggest attack.
bool needs(const PlayerView& v, const PokemonSlot& p, int owner, std::optional<Type> type = std::nullopt) {
    int big = biggest(v, p, owner);
    if (big < 0) return false;
    EnergyCounts have = v.energy(p, owner), cost = v.attackCost(p, owner, big);
    int before = missing(have, cost);
    if (!type) return before > 0;
    ++have[int(*type)];
    return missing(have, cost) < before;
}

// How good an attacker p is: the best of (damage / (1 + Energy still missing)) over its attacks.
double attackerScore(const PlayerView& v, const PokemonSlot& p, int owner) {
    double best = 0;
    auto atk = v.attacks(p, owner);
    for (std::size_t i = 0; i < atk.size(); ++i) best = std::max(best, atk[i]->damage / (1.0 + missingFor(v, p, owner, int(i))));
    return best;
}

// How much one `type` Energy matters to p's attacks: how many more Energy they would miss without it (adding: how
// many fewer with one more), its biggest attack weighing most; a type a cost names counts a little more.
int energyWorth(const PlayerView& v, const PokemonSlot& p, int owner, int type, bool adding) {
    auto atk = v.attacks(p, owner);
    int big = biggest(v, p, owner), worth = 0;
    EnergyCounts have = v.energy(p, owner), changed = have;
    if (adding) ++changed[type];
    else if (changed[type]) --changed[type];
    for (std::size_t i = 0; i < atk.size(); ++i) {
        EnergyCounts cost = v.attackCost(p, owner, int(i));
        worth += (int(i) == big ? 100 : 10) * std::abs(missing(changed, cost) - missing(have, cost)) + (cost[type] > 0);
    }
    return worth;
}

// My main attacker: -1 for the Active, else its Bench position. The Active wins a tie.
int mainAttacker(const PlayerView& v) {
    int me = v.me(), main = -1;
    double best = v.active(me).card ? attackerScore(v, v.active(me), me) : -1;  // empty while a new Active is due
    for (std::size_t i = 0; i < v.bench(me).size(); ++i)
        if (double s = attackerScore(v, v.bench(me)[i], me); s > best) best = s, main = int(i);
    return main;
}

// The most the opponent's Active could do to my Active next turn with one more Energy.
int threat(const PlayerView& v) {
    const PokemonSlot &o = v.active(v.opp()), &m = v.active(v.me());
    if (!o.card || !m.card) return 0;
    int best = 0;
    auto atk = v.attacks(o, v.opp());
    for (std::size_t i = 0; i < atk.size(); ++i)
        if (missingFor(v, o, v.opp(), int(i)) <= 1) best = std::max(best, expected(o, atk[i], m));
    return best;
}

// The extra damage a "this turn" boost gives my Active against theirs; 0 when its name or ex condition doesn't fit.
// shortcut: reads only :name and :vs-ex; type, stage and trait conditions count as met.
int boost(const CardDef* c, const PokemonSlot& mine, const PokemonSlot& theirs) {
    const std::string& s = c->scripts;
    std::size_t at = s.find("damage-dealt ");
    if (at == std::string::npos) return 0;
    if (s.find(":name") != std::string::npos && s.find('"' + mine.card->name + '"') == std::string::npos) return 0;
    if (s.find(":vs-ex") != std::string::npos && !theirs.card->ex) return 0;
    return std::atoi(s.c_str() + at + 13);
}

// The opening Active: its best attack payable soon (2 Energy or fewer) first, then its HP.
int openingScore(const CardDef* c) {
    int soon = 0;
    for (const AttackDef& a : c->attacks) {
        int cost = 0;
        for (auto n : a.cost) cost += n;
        if (cost <= 2) soon = std::max(soon, a.damage);
    }
    return soon * 1000 + c->hp;
}

// For picking cards to take (high first) or give up (low first): evolutions for my Pokémon in play, Basics while the
// Bench has room, Supporters, then the rest.
int cardValue(const PlayerView& v, const CardDef* c) {
    if (!c) return 0;
    if (c->isEvolution())
        for (const PokemonSlot* p : inPlay(v, v.me()))
            if (p->card->name == c->evolvesFrom) return 3;
    if (c->isBasic() && !v.bench(v.me()).full()) return 2;
    return c->isTrainer(TrainerKind::Supporter) ? 1 : 0;
}

template <class Score>
std::size_t argmax(std::size_t n, Score score) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < n; ++i)
        if (score(i) > score(best)) best = i;
    return best;
}

constexpr std::size_t None = std::size_t(-1);
}  // namespace

std::unique_ptr<Policy> makePolicy(const std::string& name) {
    if (name == "random") return std::make_unique<RandomPolicy>();
    if (name == "heuristic") return std::make_unique<HeuristicPolicy>();
    throw std::runtime_error("unknown player '" + name + "' (random or heuristic)");
}

uint16_t HeuristicPolicy::kinds(const CardDef* c) {
    auto [it, fresh] = kinds_.try_emplace(c, 0);
    if (fresh) it->second = classify(c->scripts);
    return it->second;
}

std::size_t HeuristicPolicy::choose(const PlayerView& v, const std::vector<Action>& actions) {
    if (!v.hasRules()) throw std::logic_error("HeuristicPolicy needs a view made by a Game");
    if (v.turn() != turn_) turn_ = v.turn(), moves_ = 0, plan_ = 0, last_ = 0;
    if (actions.size() == 1) return 0;
    if (actions[0].kind == ActionKind::Choose) return answer(v, actions);
    ++moves_;
    std::size_t i = act(v, actions);
    const Action& a = actions[i];
    if (a.kind == ActionKind::PlayTrainer) last_ = kinds(v.hand()[std::size_t(a.index)]);
    if (a.kind == ActionKind::UseAbility) last_ = kinds((a.target < 0 ? v.active(v.me()) : v.bench(v.me())[std::size_t(a.target)]).card);
    if (a.kind == ActionKind::Attack) last_ = kinds(v.active(v.me()).card);
    return i;
}

std::size_t HeuristicPolicy::act(const PlayerView& v, const std::vector<Action>& acts) {
    int me = v.me(), opp = v.opp();
    auto first = [&](auto pred) {
        for (std::size_t i = 0; i < acts.size(); ++i)
            if (pred(acts[i])) return i;
        return None;
    };
    auto card = [&](const Action& a) { return v.hand()[std::size_t(a.index)]; };
    auto trainer = [&](TrainerKind tk, uint16_t k) {  // a playable Trainer of that kind that does k
        return first([&](const Action& a) { return a.kind == ActionKind::PlayTrainer && card(a)->isTrainer(tk) && (kinds(card(a)) & k); });
    };
    std::size_t endTurn = acts.back().kind == ActionKind::EndTurn ? acts.size() - 1 : 0;

    // Setup: the best opening Active, then every other Basic onto the Bench.
    if (acts[0].kind == ActionKind::PlaceActive)
        return argmax(acts.size(), [&](std::size_t i) { return openingScore(card(acts[i])); });
    if (v.turn() == 0) {
        std::size_t i = first([](const Action& a) { return a.kind == ActionKind::PlaceBench; });
        return i != None ? i : endTurn;
    }

    const PokemonSlot &mine = v.active(me), &theirs = v.active(opp);
    auto atk = v.attacks(mine, me);
    int hpTheirs = v.hpLeft(theirs, opp);
    auto winsWith = [&](int points) { return v.points(me) + points >= rules::PointsToWin; };

    // 12. Attack: the knock-out worth most points, else the most E; never an attack that does nothing.
    auto attack = [&] {
        std::size_t best = None;
        int bestScore = 0;
        for (std::size_t k = 0; k < acts.size(); ++k) {
            if (acts[k].kind != ActionKind::Attack) continue;
            const AttackDef* a = atk[std::size_t(acts[k].index)];
            int e = expected(mine, a, theirs);
            int score = e >= hpTheirs ? 1000 + v.koPoints(theirs, opp) : e;
            if ((e > 0 || a->script) && (best == None || score > bestScore)) best = k, bestScore = score;
        }
        return best != None ? best : endTurn;
    };
    if (moves_ > 60) return attack();  // a stop for anything that could repeat forever

    // 1. Win now.
    std::size_t i = first([&](const Action& a) {
        return a.kind == ActionKind::Attack && expected(mine, atk[std::size_t(a.index)], theirs) >= hpTheirs &&
               (winsWith(v.koPoints(theirs, opp)) || v.bench(opp).empty());
    });
    if (i != None) return i;

    // 2. Gust for the win: bring up a Benched Pokémon my Active knocks out for the last points.
    auto koForWin = [&](const PokemonSlot& b) { return bestPayable(v, mine, me, b) >= v.hpLeft(b, opp) && winsWith(b.card->points); };
    for (i = 0; i < acts.size(); ++i) {
        if (acts[i].kind != ActionKind::PlayTrainer) continue;
        const CardDef* c = card(acts[i]);
        if (kinds(c) & Gust) {
            bool damagedOnly = c->scripts.find("(switch-in (choose opp bench damaged") != std::string::npos;  // Cyrus
            for (const PokemonSlot& b : v.bench(opp))
                if ((!damagedOnly || b.damage > 0) && koForWin(b)) {
                    plan_ = b.uid;
                    return i;
                }
        }
        if ((kinds(c) & GustOppChooses) && !v.bench(opp).empty() &&
            std::all_of(v.bench(opp).begin(), v.bench(opp).end(), koForWin))
            return i;
    }

    // 3. Evolve: the Active first, then the main attacker, then the rest. Rare Candy when it can be played.
    int main = mainAttacker(v);
    const PokemonSlot& mainSlot = main < 0 ? mine : v.bench(me)[std::size_t(main)];
    auto evolveRank = [&](const Action& a) { return a.target == -1 ? 3 : a.target == main ? 2 : 1; };
    std::size_t evolve = None;
    for (i = 0; i < acts.size(); ++i)
        if (acts[i].kind == ActionKind::Evolve && (evolve == None || evolveRank(acts[i]) > evolveRank(acts[evolve]))) evolve = i;
    if (evolve != None) return evolve;
    if ((i = trainer(TrainerKind::Item, RareCandy)) != None) return i;

    // 4. Every Basic onto the Bench while there's room.
    if ((i = first([](const Action& a) { return a.kind == ActionKind::PlaceBench; })) != None) return i;

    // 5. Items that draw or search.
    if ((i = trainer(TrainerKind::Item, Draw)) != None) return i;

    // 6. One Supporter, the first reason that fits.
    int myBest = bestPayable(v, mine, me, theirs);
    int threatens = threat(v);
    bool koThreat = threatens >= v.hpLeft(mine, me);
    for (i = 0; i < acts.size(); ++i) {
        const Action& a = acts[i];
        if (a.kind == ActionKind::PlayTrainer && card(a)->isTrainer(TrainerKind::Supporter) && (kinds(card(a)) & Boost) &&
            myBest >= 0 && myBest < hpTheirs && myBest + boost(card(a), mine, theirs) >= hpTheirs)
            return i;
    }
    if ((mine.damage >= 40 || mine.damage >= v.hpLeft(mine, me)) && (i = trainer(TrainerKind::Supporter, Heal)) != None) return i;
    if (koThreat && !v.bench(opp).empty() && (i = trainer(TrainerKind::Supporter, GustOppChooses)) != None) return i;
    if (needs(v, mainSlot, me) && (i = trainer(TrainerKind::Supporter, EnergyHelp)) != None) return i;
    if (v.hand().size() <= 5 && (i = trainer(TrainerKind::Supporter, Draw)) != None) return i;

    // 7. Tools: on the main attacker, else the Active.
    if ((i = first([&](const Action& a) { return a.kind == ActionKind::AttachTool && a.target == main; })) != None) return i;
    if ((i = first([&](const Action& a) { return a.kind == ActionKind::AttachTool && a.target == -1; })) != None) return i;

    // 8. Energy: to the Active when it needs it and is (as good as) the main attacker; else the main attacker; else
    // whoever needs it; else the Active.
    if (auto zone = v.zoneEnergy()) {
        auto attachTo = [&](int target) {
            return first([&](const Action& a) { return a.kind == ActionKind::AttachEnergy && a.target == target; });
        };
        int to = -1;
        bool activeNeeds = needs(v, mine, me, zone);
        if (!(activeNeeds && attackerScore(v, mine, me) >= attackerScore(v, mainSlot, me))) {
            if (needs(v, mainSlot, me, zone)) to = main;
            else if (!activeNeeds)
                for (std::size_t b = 0; b < v.bench(me).size() && to < 0; ++b)
                    if (needs(v, v.bench(me)[b], me, zone)) to = int(b);
        }
        if (to < 0 && mine.card->isFossil() && main >= 0) to = main;  // a Fossil has no attacks to pay for
        if ((i = attachTo(to)) != None || (i = attachTo(-1)) != None) return i;
    }

    // A Fossil in the Active Spot can't retreat: discard it from play once a Benched Pokémon can attack now.
    // Otherwise a Fossil is never discarded.
    int benchBest = -1, to = -1;
    for (std::size_t b = 0; b < v.bench(me).size(); ++b)
        if (int e = bestPayable(v, v.bench(me)[b], me, theirs); e > benchBest) benchBest = e, to = int(b);
    auto useAbility = [&](int target) {
        return first([&](const Action& a) { return a.kind == ActionKind::UseAbility && a.target == target; });
    };
    if (mine.card->isFossil() && benchBest >= 0 && (i = useAbility(-1)) != None) return i;

    // 9. Abilities, the Stadium's action, and a Stadium when none is in play or it's the opponent's.
    if (moves_ < 40) {
        if ((i = first([&](const Action& a) {
                 return (a.kind == ActionKind::UseAbility && !(a.target < 0 ? mine : v.bench(me)[std::size_t(a.target)]).card->isFossil()) ||
                        a.kind == ActionKind::UseStadium;
             })) != None)
            return i;
    }
    if ((!v.stadium() || v.stadiumOwner() != me) &&
        (i = first([&](const Action& a) { return a.kind == ActionKind::PlayTrainer && card(a)->isTrainer(TrainerKind::Stadium); })) != None)
        return i;

    // 10. Retreat or switch, to a Benched Pokémon that can attack now: when my Active can't attack and that one does
    // 10 more than the Active's biggest attack would, or when my Active is worth 2 points or more and could be
    // knocked out next turn.
    int big = biggest(v, mine, me);
    int activeBig = big < 0 ? 0 : expected(mine, atk[std::size_t(big)], theirs);
    bool move = benchBest >= 0 && ((myBest < 0 && benchBest >= activeBig + 10) || (koThreat && v.koPoints(mine, me) >= 2));
    if (move) {
        if ((i = trainer(TrainerKind::Item, SwitchOwn)) != None || (i = trainer(TrainerKind::Supporter, SwitchOwn)) != None) return i;
        if (v.retreatCost(mine, me) > 0 && (i = trainer(TrainerKind::Item, RetreatHelp)) != None) return i;
        if ((i = first([&](const Action& a) { return a.kind == ActionKind::Retreat && a.index == to; })) != None) return i;
    }

    // 11. Healing Items when my Active has 20 damage or more.
    if (mine.damage >= 20 && (i = trainer(TrainerKind::Item, Heal)) != None) return i;

    return attack();  // 12, then 13: end the turn
}

std::size_t HeuristicPolicy::answer(const PlayerView& v, const std::vector<Action>& opts) {
    int me = v.me(), opp = v.opp();
    for (std::size_t i = 0; i < opts.size(); ++i)
        if (plan_ && opts[i].uid == plan_) return i;
    const PokemonSlot &mine = v.active(me), &theirs = v.active(opp);
    if (opts[0].type >= 0) {  // an Energy type for Pokémon uid: Gain wants the type that matters most, Lose the least
        int owner = 0;
        if (const PokemonSlot* p = locate(v, opts[0].uid, &owner)) {
            bool gain = opts[0].choice == ChoiceKind::Gain;
            bool adding = gain == (owner == me);  // mine gains it, or theirs loses it
            return argmax(opts.size(), [&](std::size_t i) {
                int w = energyWorth(v, *p, owner, opts[i].type, adding);
                return gain ? w : -w;
            });
        }
    }
    auto slotOf = [&](std::size_t i) {
        int owner = 0;
        return locate(v, opts[i].uid, &owner);
    };
    switch (opts[0].choice) {
        case ChoiceKind::Target: {
            int owner = 0;
            if (!locate(v, opts[0].uid, &owner)) break;
            if (owner == opp)  // one my Active would knock out, else the lowest HP left; ties to ex
                return argmax(opts.size(), [&](std::size_t i) {
                    const PokemonSlot* p = slotOf(i);
                    if (!p) return -100000;
                    bool ko = mine.card && bestPayable(v, mine, me, *p) >= v.hpLeft(*p, opp);
                    return (ko ? 10000 : 0) - 2 * v.hpLeft(*p, opp) + p->card->ex;
                });
            if (last_ & Heal) return argmax(opts.size(), [&](std::size_t i) { return slotOf(i) ? slotOf(i)->damage : -1; });
            if ((last_ & SwitchOwn) && theirs.card)
                return argmax(opts.size(), [&](std::size_t i) { return slotOf(i) ? bestPayable(v, *slotOf(i), me, theirs) : -2; });
            int main = mainAttacker(v);
            const PokemonSlot* mainSlot = main < 0 ? &mine : &v.bench(me)[std::size_t(main)];
            return argmax(opts.size(), [&](std::size_t i) { return slotOf(i) == mainSlot; });
        }
        case ChoiceKind::NewActive:  // the best attacker ready now, a knock-out first; else the most HP left
            return argmax(opts.size(), [&](std::size_t i) {
                const PokemonSlot* p = slotOf(i);
                if (!p) return -1L;
                int e = theirs.card ? bestPayable(v, *p, me, theirs) : -1;
                long ready = e < 0 ? 0 : theirs.card && e >= v.hpLeft(theirs, opp) ? 2 : 1;
                return ready * 1000000L + std::max(e, 0) * 1000L + v.hpLeft(*p, me);
            });
        case ChoiceKind::Gain:
            return argmax(opts.size(), [&](std::size_t i) { return cardValue(v, opts[i].card); });
        case ChoiceKind::Lose:
            return argmax(opts.size(), [&](std::size_t i) { return -cardValue(v, opts[i].card); });
        case ChoiceKind::Other:
            break;
    }
    // shortcut: copy-attack, one-of and re-flip choices are random; they say nothing about their options yet
    return std::uniform_int_distribution<std::size_t>(0, opts.size() - 1)(rng_);
}
