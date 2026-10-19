// Plays random games between every pair of the given decks and checks the whole game state after
// every move, using its own reading of the rules rather than the engine's code. Run from the project folder:
//   engine/build/check_states decks/water.txt decks/lightning.txt decks/fighting.txt
//
// The exact damage and cost checks only apply on a "plain" board (no abilities, Tools, Stadium or timed
// effects in play, and an attack without a script); card effects are covered by tests/test_effects.cpp.
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "game/game.h"

namespace {

constexpr int GamesPerPairing = 2000;

long violations = 0;
std::string where;  // which game and turn, for the error messages

#define EXPECT(cond)                                                              \
    do {                                                                          \
        if (!(cond) && ++violations <= 25) std::cerr << where << ": " #cond "\n"; \
    } while (0)

struct Stats {
    long moves = 0, evolutions[3] = {}, retreats = 0, weaknessHits = 0, knockOuts = 0, exKnockOuts = 0;
    long energyDiscarded = 0, trainers = 0, supporters = 0, tools = 0, abilities = 0, scriptedAttacks = 0;
};

int total(const EnergyCounts& e) {
    int n = 0;
    for (auto x : e) n += x;
    return n;
}

// Picks any legal action, ending the turn early too, to reach more kinds of states than RandomPolicy.
class AnyPolicy : public Policy {
public:
    std::size_t choose(const PlayerView&, const std::vector<Action>& actions) override {
        return std::uniform_int_distribution<std::size_t>(0, actions.size() - 1)(rng_);
    }

private:
    std::mt19937 rng_{std::random_device{}()};
};

int stageOf(const CardDef* c) { return int(c->stage); }

const PokemonSlot& slotOf(const PlayerState& p, int target) { return target < 0 ? p.active : p.bench[target]; }

std::vector<const PokemonSlot*> inPlay(const PlayerState& p) {
    std::vector<const PokemonSlot*> out;
    if (p.active.card) out.push_back(&p.active);
    for (const PokemonSlot& s : p.bench) out.push_back(&s);
    return out;
}

const Modifier* passiveOf(const CardDef& c, ModKind kind) {
    auto m = std::find_if(c.passive.begin(), c.passive.end(), [&](const Modifier& x) { return x.kind == kind; });
    return m == c.passive.end() ? nullptr : &*m;
}

// One Pokémon Tool each, two with an Ability that says so (Revavroom).
bool allowsTwoTools(const CardDef& c) { return passiveOf(c, ModKind::ExtraTools); }

// No abilities, Tools, Stadium or timed effects in play: only the base rules apply.
bool plainBoard(const GameState& s) {
    if (s.stadium || !s.timed.empty()) return false;
    for (const PlayerState& p : s.players)
        for (const PokemonSlot* slot : inPlay(p)) {
            const CardDef& c = *slot->card;
            if (!slot->tools.empty() || !c.passive.empty() || !c.abilities.empty() || c.extraTypes) return false;
        }
    return true;
}

void checkState(const Game& game, const Deck* decks[2]) {
    const GameState& s = game.state();
    // Every card of both decks is in exactly one place. Counted over both players together, because cards can
    // change hands (Team Rocket's Thieving Machine).
    std::multiset<const CardDef*> seen, dealt;
    for (int i = 0; i < 2; ++i) {
        const PlayerState& p = s.players[i];
        seen.insert(p.deck.begin(), p.deck.end());
        seen.insert(p.hand.begin(), p.hand.end());
        seen.insert(p.discard.begin(), p.discard.end());
        for (const PokemonSlot* slot : inPlay(p)) {
            seen.insert(slot->card);
            seen.insert(slot->below.begin(), slot->below.end());
            seen.insert(slot->tools.begin(), slot->tools.end());
        }
        dealt.insert(decks[i]->cards.begin(), decks[i]->cards.end());
    }
    if (s.stadium) seen.insert(s.stadium);
    EXPECT(seen == dealt);
    for (int i = 0; i < 2; ++i) {
        const PlayerState& p = s.players[i];
        EXPECT(p.hand.size() <= std::size_t(rules::HandLimit));
        EXPECT(s.phase == Phase::Over || p.points < rules::PointsToWin);
        for (const PokemonSlot* slot : inPlay(p)) {
            EXPECT(slot->damage >= 0 && slot->damage < game.maxHp(*slot, i));  // knocked-out Pokémon leave play at once
            EXPECT(slot->enteredTurn <= s.turn);
            EXPECT(slot->uid != 0);
            for (const CardDef* t : slot->tools) EXPECT(t->isTrainer(TrainerKind::Tool));
            EXPECT(slot->tools.size() <= (allowsTwoTools(*slot->card) ? 2u : 1u));
            if (slot != &p.active) EXPECT(slot->status == 0);  // Special Conditions are only on the Active Pokémon
            // An evolved Pokémon sits on its line, Basic first, each card naming the one below it. Rare Candy
            // puts a Stage 2 straight onto its Basic.
            bool rareCandy = slot->card->stage == Stage::Stage2 && slot->below.size() == 1 &&
                             slot->card->evolvesFromBasic == slot->below[0]->name;
            EXPECT(int(slot->below.size()) == stageOf(slot->card) - slot->baseStage || rareCandy);  // Silcoon: base 1
            for (std::size_t b = 0; b < slot->below.size() && !rareCandy; ++b) {
                const CardDef* above = b + 1 < slot->below.size() ? slot->below[b + 1] : slot->card;
                const Modifier* as = passiveOf(*slot->below[b], ModKind::EvolvesAs);  // Eevee ex
                EXPECT(above->evolvesFrom == slot->below[b]->name || (as && as->as->has(above->evolvesFrom)));
                EXPECT(stageOf(slot->below[b]) == int(b) + slot->baseStage);
            }
        }
    }
    switch (s.phase) {
        case Phase::Setup:
            EXPECT(s.phase != Phase::Setup);  // setup() has finished
            break;
        case Phase::Main:
            EXPECT(s.players[0].active.card && s.players[1].active.card);
            break;
        case Phase::Over: {
            // No Pokémon in play loses whatever the points, nobody left is a tie; else 3 points wins and
            // both at once is a tie; else it's the turn cap.
            auto out = [&](int p) { return !s.players[p].active.card && s.players[p].bench.empty(); };
            auto reached = [&](int p) { return s.players[p].points >= rules::PointsToWin; };
            int expected = -1;
            if (out(0) != out(1))
                expected = out(0) ? 1 : 0;
            else if (!out(0) && reached(0) != reached(1))
                expected = reached(0) ? 0 : 1;
            EXPECT(s.winner == expected);
            if (!out(0) && !out(1) && !reached(0) && !reached(1)) EXPECT(s.turn == rules::TurnCap);
            break;
        }
    }
}

void checkOpening(const GameState& s) {
    EXPECT(s.phase == Phase::Main && s.turn == 1 && s.current == s.first);
    const PlayerState& first = s.players[s.current];
    const PlayerState& second = s.players[1 - s.current];
    EXPECT(!first.energy && !second.energy);  // no energy on the first turn of the game
    EXPECT(first.nextEnergy && second.nextEnergy);  // both Energy Zones show their next energy from the start
    // Five cards dealt (always with a Basic, so an Active exists), plus the first turn's draw.
    EXPECT(first.hand.size() + inPlay(first).size() ==
           std::size_t(rules::OpeningHand + (rules::FirstPlayerDrawsTurn1 ? 1 : 0)));
    EXPECT(second.hand.size() + inPlay(second).size() == std::size_t(rules::OpeningHand));
}

// The legal moves in a turn, worked out from the rules. Placing Basics and evolving are compared exactly;
// for the rest, which modifiers can change, every move on offer must at least be allowed by the base rules.
void checkActions(const GameState& s, const std::vector<Action>& actions) {
    if (s.phase != Phase::Main) return;
    const PlayerState& p = s.players[s.current];
    using Move = std::tuple<int, int, int>;
    std::multiset<Move> expected, offered;
    int bench = int(p.bench.size());
    // Abilities that change evolving (Eevee, Eevee ex, Aerodactyl ex) or turn Abilities off (Alolan Muk): then every
    // Evolve on offer must only fit the looser rule, not match the base rules exactly.
    bool evolveRules = false;
    for (const PlayerState& ps : s.players)
        for (const PokemonSlot* slot : inPlay(ps))
            for (ModKind k : {ModKind::EvolveEarly, ModKind::EvolvesAs, ModKind::CantEvolveActive, ModKind::NoAbilities})
                evolveRules |= passiveOf(*slot->card, k) != nullptr;
    for (int h = 0; h < int(p.hand.size()); ++h) {
        const CardDef* c = p.hand[h];
        if (c->isBasic() && bench < rules::BenchSize) expected.insert({int(ActionKind::PlaceBench), h, 0});
        // Evolve from turn 3 on (each player's own first turn is out), onto the Pokémon the card names,
        // if that Pokémon has been in play since before this turn.
        if (!c->evolvesFrom.empty() && s.turn >= 3 && !evolveRules)
            for (int t = -1; t < bench; ++t) {
                const PokemonSlot& target = slotOf(p, t);
                if (target.card->name == c->evolvesFrom && target.enteredTurn < s.turn)
                    expected.insert({int(ActionKind::Evolve), h, t});
            }
    }
    bool endTurn = false;
    bool cantAct = p.active.status & (Asleep | Paralyzed);
    for (const Action& a : actions) {
        switch (a.kind) {
            case ActionKind::Evolve:
                if (evolveRules) {
                    const PokemonSlot& target = slotOf(p, a.target);
                    const CardDef* c = p.hand[a.index];
                    const Modifier* as = passiveOf(*target.card, ModKind::EvolvesAs);
                    bool early = passiveOf(*target.card, ModKind::EvolveEarly) && a.target < 0;
                    EXPECT(target.card->name == c->evolvesFrom || (as && as->as->has(c->evolvesFrom)));
                    EXPECT(early || (s.turn >= 3 && target.enteredTurn < s.turn));
                    break;
                }
                [[fallthrough]];
            case ActionKind::PlaceBench:
                offered.insert({int(a.kind), a.index, a.target});
                break;
            case ActionKind::EndTurn:
                endTurn = true;
                break;
            case ActionKind::AttachEnergy:
                EXPECT(p.energy.has_value());
                break;
            case ActionKind::Retreat:
                EXPECT(!p.retreated && !cantAct && a.index < bench);
                break;
            case ActionKind::Attack:
                EXPECT(!cantAct);
                break;
            case ActionKind::PlayTrainer: {
                const CardDef* c = p.hand[a.index];
                EXPECT(c->category == Category::Trainer && !c->isTrainer(TrainerKind::Tool));
                if (c->isTrainer(TrainerKind::Supporter)) EXPECT(p.supporterTurn != s.turn);  // one per turn
                if (c->isTrainer(TrainerKind::Stadium))
                    EXPECT(p.stadiumTurn != s.turn && (!s.stadium || s.stadium->name != c->name));
                break;
            }
            case ActionKind::AttachTool:
                EXPECT(p.hand[a.index]->isTrainer(TrainerKind::Tool) &&
                       slotOf(p, a.target).tools.size() < (allowsTwoTools(*slotOf(p, a.target).card) ? 2u : 1u));
                break;
            case ActionKind::UseStadium:  // once per player per turn and per Stadium card
                EXPECT(s.stadium && s.stadium->stadiumAction && !(p.stadiumUseTurn == s.turn && p.stadiumUsed == s.stadium));
                break;
            case ActionKind::UseAbility:
                EXPECT(slotOf(p, a.target).card->activated &&
                       (slotOf(p, a.target).abilityTurn != s.turn || slotOf(p, a.target).card->activatedRepeat));  // Vaporeon
                break;
            default:
                EXPECT(!"unexpected move");
        }
    }
    EXPECT(endTurn && actions.back().kind == ActionKind::EndTurn);
    EXPECT(expected == offered);
}

void checkMove(const GameState& before, Action a, const GameState& after, Stats& st) {
    int me = before.current;
    const PlayerState& pb = before.players[me];
    const PlayerState& pa = after.players[me];
    bool plain = plainBoard(before);
    switch (a.kind) {
        case ActionKind::Evolve: {
            const PokemonSlot& from = slotOf(pb, a.target);
            bool early = passiveOf(*from.card, ModKind::EvolveEarly);  // Eevee, while Active
            EXPECT(early || (before.turn >= 3 && from.enteredTurn < before.turn));
            const Modifier* as = passiveOf(*from.card, ModKind::EvolvesAs);  // Eevee ex
            EXPECT(pb.hand[a.index]->evolvesFrom == from.card->name || (as && as->as->has(pb.hand[a.index]->evolvesFrom)));
            auto still = inPlay(pa);
            if (std::none_of(still.begin(), still.end(), [&](const PokemonSlot* x) { return x->uid == from.uid; })) {
                EXPECT(from.damage >= pb.hand[a.index]->hp);  // knocked out at once: its damage is more than its new HP
                break;
            }
            const PokemonSlot& to = slotOf(pa, a.target);
            EXPECT(to.card == pb.hand[a.index]);
            EXPECT(to.below.size() == from.below.size() + 1 && to.below.back() == from.card);
            if (!to.card->onEvolve) EXPECT(to.damage == from.damage && to.energy == from.energy);  // damage and energy stay
            EXPECT(to.enteredTurn == before.turn && to.status == 0);
            EXPECT(to.abilityTurn != before.turn);  // a new card: its "once during your turn" is unused (App)
            st.evolutions[stageOf(to.card)]++;
            break;
        }
        case ActionKind::Retreat: {
            const PokemonSlot& in = pb.bench[a.index];
            EXPECT(!pb.retreated && pa.retreated);
            EXPECT(pa.active.card == in.card && pa.active.damage == in.damage && pa.active.energy == in.energy);
            if (std::size_t(a.index) >= pa.bench.size() || pa.bench[a.index].uid != pb.active.uid) {
                EXPECT(pb.active.damage >= pb.active.card->hp);  // knocked out: its HP went with the Energy (Reuniclus)
                break;
            }
            const PokemonSlot& out = pa.bench[a.index];
            EXPECT(out.card == pb.active.card && out.damage == pb.active.damage && out.status == 0);
            EXPECT(out.energyCount() <= pb.active.energyCount());
            if (plain) EXPECT(out.energyCount() == pb.active.energyCount() - pb.active.card->retreat);
            // the energy paid goes to the discard pile
            EXPECT(total(pa.discardEnergy) == total(pb.discardEnergy) + pb.active.energyCount() - out.energyCount());
            st.retreats++;
            break;
        }
        case ActionKind::AttachEnergy: {
            EXPECT(pb.energy && !pa.energy);
            if (plain) {
                int t = int(*pb.energy);
                EXPECT(slotOf(pa, a.target).energy[t] == slotOf(pb, a.target).energy[t] + 1);
            }
            break;
        }
        case ActionKind::PlayTrainer: {
            const CardDef* c = pb.hand[a.index];
            st.trainers++;
            if (c->isTrainer(TrainerKind::Supporter)) {
                EXPECT(pb.supporterTurn != before.turn && pa.supporterTurn == before.turn);
                st.supporters++;
            }
            if (c->isTrainer(TrainerKind::Stadium)) EXPECT(after.stadium == c && after.stadiumOwner == me);
            break;
        }
        case ActionKind::AttachTool:
            EXPECT(slotOf(pa, a.target).tools.back() == pb.hand[a.index]);
            st.tools++;
            break;
        case ActionKind::UseAbility:
            EXPECT(slotOf(pb, a.target).abilityTurn != before.turn || slotOf(pb, a.target).card->activatedRepeat);
            st.abilities++;
            break;
        case ActionKind::Attack: {
            if (std::size_t(a.index) >= pb.active.card->attacks.size()) break;  // an earlier evolution's attack (Celebi)
            const AttackDef& atk = pb.active.card->attacks[a.index];
            if (atk.script) st.scriptedAttacks++;
            if (!plain || atk.script || pb.active.status) break;  // effects: see test_effects
            const PlayerState& db = before.players[1 - me];
            const PlayerState& da = after.players[1 - me];
            const CardDef* target = db.active.card;
            bool weak = atk.damage > 0 && target->weakness && pb.active.card->type == target->weakness;
            int damage = atk.damage + (weak ? 20 : 0);
            st.weaknessHits += weak;
            if (db.active.damage + damage >= target->hp) {
                EXPECT(pa.points >= pb.points + target->points);
                EXPECT(total(da.discardEnergy) == total(db.discardEnergy) + db.active.energyCount());  // its energy too
                st.knockOuts++;
                st.exKnockOuts += target->points > 1;
            } else if (!db.active.status) {
                EXPECT(da.active.card == target && da.active.damage == db.active.damage + damage);
                EXPECT(pa.points == pb.points);
                EXPECT(after.phase == Phase::Over || after.turn == before.turn + 1);  // attacking ends the turn
            }
            break;
        }
        default:
            break;
    }
    // Energy never leaves the game: per player and type, the discard pile plus what's attached to their Pokémon
    // only grows (new energy from the Energy Zone). Flame Patch and the like move it from the pile onto a Pokémon.
    auto held = [](const PlayerState& p, int t) {
        int n = p.discardEnergy[t];
        for (const PokemonSlot* slot : inPlay(p)) n += slot->energy[t];
        return n;
    };
    for (int p = 0; p < 2; ++p)
        for (int t = 0; t < TypeCount; ++t)
            EXPECT(held(after.players[p], t) >= held(before.players[p], t));
    // The turn passed to the other player, after End turn or an attack.
    if (before.phase == Phase::Main && after.phase == Phase::Main && after.turn == before.turn + 1) {
        // The player whose turn ended: unused energy went to the discard pile (App).
        const PlayerState& eb = before.players[me];
        const PlayerState& ea = after.players[me];
        EXPECT(!ea.energy);
        if (eb.energy) {
            EXPECT(ea.discardEnergy[int(*eb.energy)] > eb.discardEnergy[int(*eb.energy)]);
            st.energyDiscarded++;
        }
        // The new player: their next energy came up, and a new one was rolled from their deck's types.
        const PlayerState& nb = before.players[after.current];
        const PlayerState& na = after.players[after.current];
        EXPECT(!na.retreated && na.energy && na.energy == nb.nextEnergy);
        EXPECT(na.nextEnergy && std::find(na.energyTypes.begin(), na.energyTypes.end(), *na.nextEnergy) != na.energyTypes.end());
        // They draw if they can. (After an attack, its effects may have changed that player's hand.)
        bool canDraw = !nb.deck.empty() && nb.hand.size() < std::size_t(rules::HandLimit);
        // …unless one of their Pokémon evolved from the deck at the end of the turn (Caterpie), maybe with the last card.
        bool evolvedFromDeck = false;
        for (const PokemonSlot* x : inPlay(na))
            for (const PokemonSlot* y : inPlay(nb)) evolvedFromDeck |= x->uid == y->uid && x->below.size() > y->below.size();
        if (a.kind == ActionKind::EndTurn && !evolvedFromDeck) EXPECT(na.hand.size() == nb.hand.size() + (canDraw ? 1 : 0));
    }
}

}  // namespace

int main(int argc, char** argv) {
    int row = -1;  // --row K: only deck K against every deck, so check_all.sh can run the rows in parallel
    if (argc > 2 && std::string(argv[1]) == "--row") {
        row = std::stoi(argv[2]);
        argv += 2;
        argc -= 2;
    }
    if (argc < 2) {
        std::cerr << "usage: check_states [--row K] <deck.txt>...\n";
        return 2;
    }
    try {
        const std::string effects = "data/effects.tsv";
        CardDb db = CardDb::load("data/cards.tsv", std::filesystem::exists(effects) ? effects : "");
        std::vector<Deck> decks;
        for (int i = 1; i < argc; ++i) decks.push_back(loadDeck(argv[i], db));
        RandomPolicy random0, random1;
        AnyPolicy any0, any1;
        Stats st;
        for (std::size_t i = 0; i < decks.size(); ++i) {
            if (row >= 0 && int(i) != row) continue;
            const Deck& d0 = decks[i];
            for (const Deck& d1 : decks) {
                const Deck* pair[2] = {&d0, &d1};
                long wins[2] = {}, draws[2] = {}, turns = 0;  // draws: random player, any-move player
                for (int g = 0; g < GamesPerPairing; ++g) {
                    Policy* policies[2] = {&random0, &random1};
                    if (g % 2) {
                        policies[0] = &any0;
                        policies[1] = &any1;
                    }
                    Game game(d0, d1, *policies[0], *policies[1]);
                    const GameState& s = game.state();
                    where = d0.name + " vs " + d1.name + ", game " + std::to_string(g) + ", setup";
                    game.setup();
                    checkOpening(s);
                    checkState(game, pair);
                    while (s.phase != Phase::Over) {
                        where = d0.name + " vs " + d1.name + ", game " + std::to_string(g) + ", turn " +
                                std::to_string(s.turn);
                        GameState before = s;
                        int player = game.decider();
                        std::vector<Action> actions = game.legalActions();
                        checkActions(before, actions);
                        Action a = actions.at(policies[player]->choose(PlayerView(before, player), actions));
                        where += ", move kind " + std::to_string(int(a.kind));
                        game.apply(a);
                        checkMove(before, a, s, st);
                        checkState(game, pair);
                        st.moves++;
                    }
                    if (s.winner < 0)
                        draws[g % 2]++;
                    else
                        wins[s.winner]++;
                    turns += s.turn;
                }
                std::cout << d0.name << " vs " << d1.name << ": P1 " << wins[0] << ", P2 " << wins[1] << ", draws "
                          << draws[0] + draws[1] << " (" << draws[0] << " random, " << draws[1]
                          << " any-move), avg " << double(turns) / GamesPerPairing << " turns\n";
            }
        }
        std::cout << "\nmoves checked: " << st.moves << "\nevolutions: " << st.evolutions[1] << " to Stage 1, "
                  << st.evolutions[2] << " to Stage 2\nretreats: " << st.retreats << "\nTrainers played: " << st.trainers
                  << " (Supporters " << st.supporters << "), Tools attached: " << st.tools
                  << ", abilities used: " << st.abilities << "\nattacks with effects: " << st.scriptedAttacks
                  << "\nweakness hits (plain board): " << st.weaknessHits << "\nknock-outs (plain board): " << st.knockOuts
                  << " (ex: " << st.exKnockOuts << ")\nturns ending with unused energy discarded: " << st.energyDiscarded
                  << "\nrule violations: " << violations << '\n';
        return violations ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 2;
    }
}
