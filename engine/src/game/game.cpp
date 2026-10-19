#include "game/game.h"

#include <algorithm>

#include "effects/effect.h"
#include "moves/moves.h"

namespace {
// Main-phase moves in the order they are offered. EndTurn comes last (RandomPolicy relies on it).
constexpr ActionKind MainMoves[] = {ActionKind::PlaceBench,   ActionKind::Evolve,     ActionKind::PlayTrainer,
                                    ActionKind::AttachTool,   ActionKind::AttachEnergy, ActionKind::UseAbility,
                                    ActionKind::UseStadium,   ActionKind::Retreat,    ActionKind::Attack,
                                    ActionKind::EndTurn};

constexpr uint8_t CantActStatuses = Asleep | Paralyzed;  // can't attack or retreat
}  // namespace

Game::Game(const Deck& deck0, const Deck& deck1, Policy& policy0, Policy& policy1, GameLog* log)
    : decks_{&deck0, &deck1}, policies_{&policy0, &policy1}, log_(log) {
    validateDeck(deck0);
    validateDeck(deck1);
}

Result Game::run() {
    setup();
    while (s_.phase != Phase::Over) step();
    return {s_.winner, s_.turn};
}

void Game::setup() {
    s_ = GameState{};
    deal(0);
    deal(1);
    while (s_.phase == Phase::Setup) step();
}

void Game::step() {
    int player = decider();
    const auto& actions = legalActions();
    apply(actions.at(policies_[player]->choose(PlayerView(s_, player), actions)));
}

const std::vector<Action>& Game::legalActions() {
    actions_.clear();
    if (s_.phase == Phase::Setup) {
        if (!s_.players[s_.current].active.card) {
            moveRule(ActionKind::PlaceActive).addMoves(*this, actions_);  // deal() guarantees a Basic in hand
        } else {
            moveRule(ActionKind::PlaceBench).addMoves(*this, actions_);
            actions_.push_back({ActionKind::EndTurn});
        }
    } else if (s_.phase == Phase::Main) {
        for (ActionKind kind : MainMoves) moveRule(kind).addMoves(*this, actions_);
    }
    return actions_;
}

void Game::apply(Action action) {
    moveRule(action.kind).apply(*this, action);
    // "recovers from all Special Conditions and can't be affected…" (Comfey, Steel Apron): as soon as it holds.
    for (int p = 0; p < 2; ++p)
        if (PokemonSlot& a = s_.players[p].active; a.card && a.status) a.status &= uint8_t(~blockedStatuses(a, p));
}

// --- Turn flow ---

void Game::deal(int player) {
    PlayerState& p = s_.players[player];
    for (const CardDef* c : decks_[player]->cards) p.deck.push_back(c);
    for (Type t : decks_[player]->energyTypes) p.energyTypes.push_back(t);
    // The opening hand always holds a Basic: reshuffle until the top cards have one (validateDeck checks there is one).
    auto isBasic = [](const CardDef* c) { return c->isBasic(); };
    do std::shuffle(p.deck.begin(), p.deck.end(), rng_);
    while (std::none_of(p.deck.end() - rules::OpeningHand, p.deck.end(), isBasic));
    for (int i = 0; i < rules::OpeningHand; ++i) p.hand.push_back(p.deck.pop_back());
    if (log_) {
        std::string names;
        for (const CardDef* c : p.hand) names += (names.empty() ? "" : ", ") + c->name;
        say("P", player + 1, " opening hand: ", names);
    }
}

void Game::beginPlay() {
    s_.first = std::uniform_int_distribution<int>(0, 1)(rng_);
    s_.current = s_.first;
    s_.phase = Phase::Main;
    say("Coin flip: P", s_.first + 1, " goes first");
    for (PlayerState& p : s_.players) p.nextEnergy = rollEnergy(p);
    startTurn();
}

Type Game::rollEnergy(const PlayerState& p) {
    // assumed: each of the deck's energy types is equally likely
    return p.energyTypes[std::uniform_int_distribution<std::size_t>(0, p.energyTypes.size() - 1)(rng_)];
}

void Game::startTurn() {
    if (s_.turn == rules::TurnCap) return finish(-1);
    ++s_.turn;
    PlayerState& p = s_.players[s_.current];
    p.retreated = false;
    say("\n=== Turn ", s_.turn, ": P", s_.current + 1, "   points P1 ", s_.players[0].points, " - P2 ",
        s_.players[1].points, " ===");
    if (!firstTurnOfGame() || rules::FirstPlayerDrawsTurn1) draw(s_.current);
    // The player going first gets no energy on their first turn. Otherwise the next energy comes up and a
    // new next one is rolled. (Unused energy went to the discard pile at the end of the last turn, App.)
    if (!firstTurnOfGame()) {
        p.energy = p.nextEnergy;
        p.nextEnergy = rollEnergy(p);
        say("P", s_.current + 1, " gets ", typeName(*p.energy), " energy from the Energy Zone (next: ",
            typeName(*p.nextEnergy), ")");
    }
    showBoard();
    runTriggers(s_.current, &CardDef::startOfTurn, &CardDef::startOfTurnWhileActive);  // after the draw (ruling)
    knockOutCheck();
}

void Game::endTurn() {
    int me = s_.current;
    // Unused Energy Zone energy goes to the discard pile (App; the exact moment in the turn is assumed).
    if (PlayerState& p = s_.players[me]; p.energy) {
        ++p.discardEnergy[int(*p.energy)];
        say("P", me + 1, "'s unused ", typeName(*p.energy), " energy goes to the discard pile");
        p.energy.reset();
    }
    // "At the end of your turn…" abilities of the player whose turn it was.
    Uids owners;
    if (s_.players[me].active.card) owners.push_back(s_.players[me].active.uid);
    for (const PokemonSlot& b : s_.players[me].bench) owners.push_back(b.uid);
    for (uint16_t uid : owners) {
        forEachSource(uid, [&](const CardDef* source) {  // the Pokémon's ability, then its Tools ("Leftovers")
            if (!source->endOfTurn || (source->endOfTurnWhileActive && find(uid) != &s_.players[me].active)) return;
            say("P", me + 1, "'s ", source->name, ": ", source->abilities.empty() ? "end of turn" : source->abilities[0].name);
            EffectCtx ctx{*this, me};
            ctx.self = uid;
            ctx.card = source;
            source->endOfTurn->run(ctx);
        });
    }
    // "At the end of each turn…" (Lum Berry, Sitrus Berry): both players' Pokémon, before Checkup (assumed, a best
    // guess: a Lum Berry cures Poison before it does damage).
    for (int owner : {me, 1 - me}) {
        Uids each;
        if (s_.players[owner].active.card) each.push_back(s_.players[owner].active.uid);
        for (const PokemonSlot& b : s_.players[owner].bench) each.push_back(b.uid);
        for (uint16_t uid : each)
            forEachSource(uid, [&](const CardDef* source) {
                if (!source->endOfEachTurn) return;
                say("P", owner + 1, "'s ", source->name, ": end of turn");
                EffectCtx ctx{*this, owner};
                ctx.self = uid;
                ctx.card = source;
                source->endOfEachTurn->run(ctx);
            });
    }
    // "During Pokémon Checkup…" Abilities come first, before Poison and Burn (ruling); then Pokémon Checkup for both
    // Active Pokémon (Special Conditions are only ever on an Active Pokémon).
    for (int p : {me, 1 - me}) runTriggers(p, &CardDef::duringCheckup, &CardDef::duringCheckupWhileActive);
    for (int p : {me, 1 - me}) {
        PokemonSlot& a = s_.players[p].active;
        if (!a.card || !a.status) continue;
        if (a.status & Poisoned) {
            int poison = rules::PoisonDamage + sumMods(ModKind::PoisonDamage, a, p);  // Nihilego: +10
            a.damage += poison;
            say(a.card->name, " takes ", poison, " Poison damage");
        }
        if (a.status & Burned) {
            a.damage += rules::BurnDamage;
            say(a.card->name, " takes ", rules::BurnDamage, " Burn damage");
            if (flip()) {
                a.status &= ~Burned;
                say(a.card->name, " is no longer Burned");
            }
        }
        if ((a.status & Asleep) && flip()) {
            a.status &= ~Asleep;
            say(a.card->name, " wakes up");
        }
        if ((a.status & Paralyzed) && p == me) {  // it wears off at the end of its owner's turn
            a.status &= ~Paralyzed;
            say(a.card->name, " is no longer Paralyzed");
        }
    }
    knockOutCheck();
    if (s_.phase == Phase::Over) return;
    // "At the end of each player's turn, that player…" Stadiums come last, after Checkup (ruling, DESIGN.md §10).
    if (s_.stadium && s_.stadium->endOfTurn) {
        say(s_.stadium->name, ": end of turn for P", me + 1);
        EffectCtx ctx{*this, me};
        s_.stadium->endOfTurn->run(ctx);
        knockOutCheck();
        if (s_.phase == Phase::Over) return;
    }
    auto& timed = s_.timed;
    for (std::size_t i = timed.size(); i-- > 0;)
        if (timed[i].lastTurn <= s_.turn) timed.take(i);
    s_.current = 1 - me;
    s_.phase = Phase::Main;
    startTurn();
}

void Game::knockOutCheck(uint16_t attacker) {
    if (s_.phase == Phase::Over) return;
    // Knock-out triggers can do damage (Pyukumuku), so look again until nobody is left at 0 HP; only the first round
    // comes from the attack. Every knock-out counts before anyone wins (ruling). The defender's go first.
    for (;; attacker = 0) {
        Uids out;
        for (int p : {1 - s_.current, s_.current}) {
            PlayerState& ps = s_.players[p];
            auto check = [&](PokemonSlot& slot) {
                if (!slot.card || slot.damage < maxHp(slot, p)) return;
                if (attacker && !slot.koByEffect) survive(slot, p);
                if (slot.damage >= maxHp(slot, p)) out.push_back(slot.uid);
            };
            check(ps.active);
            for (PokemonSlot& b : ps.bench) check(b);
        }
        if (out.empty()) break;
        for (uint16_t uid : out) knockOut(uid, attacker);
    }
    // No Pokémon in play loses whatever the points; nobody left is a tie (Tips, DESIGN.md §10).
    auto out = [&](int p) { return !s_.players[p].active.card && s_.players[p].bench.empty(); };
    if (out(0) || out(1)) return finish(out(0) && out(1) ? -1 : out(0) ? 1 : 0);
    bool won[2];
    for (int p = 0; p < 2; ++p) won[p] = s_.players[p].points >= rules::PointsToWin;
    if (won[0] && won[1]) return finish(-1);  // both reach the points at once: a tie (Tips)
    if (won[0] || won[1]) return finish(won[0] ? 0 : 1);
    // The defending player picks a new Active first, then the attacking player; Checkup comes after (App).
    for (int p : {1 - s_.current, s_.current})
        if (!s_.players[p].active.card) promote(p);
}

// "If this Pokémon would be Knocked Out by damage from an attack… it is not Knocked Out, and its remaining HP becomes
// 10" (Hala; Conkeldurr after a heads).
void Game::survive(PokemonSlot& slot, int owner) {
    int hp = -1;
    forEachMod(ModKind::SurviveAt, &slot, owner, [&](const Modifier& m) {
        if (hp < 0 && (!m.flip || flip())) hp = m.amount;
    });
    if (hp < 0) return;
    slot.damage = maxHp(slot, owner) - hp;
    say(slot.card->name, " is not Knocked Out (", hp, " HP left)");
}

void Game::knockOut(uint16_t uid, uint16_t attacker) {
    int player = 0;
    PokemonSlot* slot = find(uid, &player);
    if (!slot) return;
    bool byAttack = attacker && player != s_.current && !slot->koByEffect;  // by damage from an opponent's attack
    bool wasActive = slot == &s_.players[player].active;
    say(slot->card->name, " is knocked out");
    // "If this Pokémon is Knocked Out…": its Ability, then its Tools. Its cards are still in play meanwhile.
    EffectCtx ctx{*this, player};
    ctx.self = uid;
    ctx.attacker = byAttack ? attacker : 0;
    forEachSource(uid, [&](const CardDef* c) {
        if (!c->onKnockedOut || (c->onKnockedOutWhileActive && !wasActive) || (c->onKnockedOutByAttack && !byAttack)) return;
        say("  ", c->name, " reacts");
        ctx.card = c;
        c->onKnockedOut->run(ctx);
    });
    slot = find(uid, &player);
    if (!slot) return;  // no trigger takes it out of play today
    PlayerState& owner = s_.players[player];
    PlayerState& scorer = s_.players[1 - player];
    ++owner.koCount;
    PokemonSlot* by = byAttack ? find(attacker) : nullptr;
    int points = slot->card->points + (by && wasActive ? sumMods(ModKind::ExtraPoint, *by, 1 - player) : 0);
    if (ctx.noPoints) points = 0;  // "your opponent can't get any points for it"
    scorer.points += points;
    if (scorer.scoredTurn != s_.turn) scorer.scored = 0;
    scorer.scoredTurn = s_.turn;
    scorer.scored += points;
    if (byAttack) {  // "Knocked Out by damage from an attack during your opponent's last turn"
        if (owner.koTurn != s_.turn) owner.koTypes = 0;
        owner.koTurn = s_.turn;
        if (slot->card->type) owner.koTypes |= typeBit(*slot->card->type);
    }
    say("P", 2 - player, " scores ", points, " (total ", scorer.points, ")");
    // The attacker's "If your opponent's Pokémon is Knocked Out by damage from this Pokémon's attacks…"
    if (by)
        forEachSource(attacker, [&](const CardDef* c) {
            if (!c->onKnocksOut) return;
            say("  ", c->name, " reacts");
            EffectCtx mine{*this, 1 - player};
            mine.self = attacker;
            mine.card = c;
            c->onKnocksOut->run(mine);
        });
    // Its cards leave play: to the discard pile, or the Pokémon and its evolutions to the hand (Rescue Scarf, ruling).
    slot = find(uid, &player);
    std::vector<const CardDef*> pokemon{slot->card};
    pokemon.insert(pokemon.end(), slot->below.begin(), slot->below.end());
    for (const CardDef* c : pokemon) ctx.toHandInstead ? toHand(player, c) : owner.discard.push_back(c);
    for (const CardDef* t : slot->tools) owner.discard.push_back(t);
    for (int t = 0; t < TypeCount; ++t) owner.discardEnergy[t] += slot->energy[t];  // its energy too (App)
    clearConditions(*slot);
    if (slot == &owner.active)
        owner.active = PokemonSlot{};
    else
        owner.bench.take(std::size_t(slot - owner.bench.begin()));
}

void Game::promote(int player) {
    PlayerState& p = s_.players[player];
    p.active = p.bench.take(choose(player, int(p.bench.size())));
    movedToActive(player);
    say("P", player + 1, " moves ", p.active.card->name, " to the Active spot");
}

void Game::finish(int winner) {
    s_.winner = winner;
    s_.phase = Phase::Over;
    if (winner < 0)
        say("\nDraw");
    else
        say("\nP", winner + 1, " wins");
}

// --- Board ---

void Game::putIntoPlay(PokemonSlot& slot, const CardDef* card) {
    slot = PokemonSlot{};
    slot.card = card;
    slot.uid = s_.nextUid++;
    slot.enteredTurn = s_.turn;
    slot.baseStage = uint8_t(card->stage);
}

void Game::evolve(PokemonSlot& slot, const CardDef* card) {
    slot.below.push_back(slot.card);
    slot.card = card;
    slot.enteredTurn = s_.turn;
    slot.abilityTurn = -1;  // "once during your turn" belongs to the card: the new one may use its own (App)
    slot.becameActiveTurn = -1;  // so does "moved from your Bench to the Active Spot this turn" (ruling)
    clearConditions(slot);  // evolving ends Special Conditions and effects on it
}

void Game::movedToActive(int player) { s_.players[player].active.becameActiveTurn = s_.turn; }

void Game::clearConditions(PokemonSlot& slot) {
    slot.status = 0;
    auto& timed = s_.timed;
    for (std::size_t i = timed.size(); i-- > 0;)
        if (timed[i].uid == slot.uid) timed.take(i);
}

void Game::discardFromPlay(uint16_t uid) {
    int owner = 0;
    PokemonSlot* slot = find(uid, &owner);
    if (!slot) return;
    PlayerState& p = s_.players[owner];
    say(slot->card->name, " is discarded");
    p.discard.push_back(slot->card);
    for (const CardDef* c : slot->below) p.discard.push_back(c);
    for (const CardDef* t : slot->tools) p.discard.push_back(t);
    for (int t = 0; t < TypeCount; ++t) p.discardEnergy[t] += slot->energy[t];
    clearConditions(*slot);
    if (slot == &p.active)
        p.active = PokemonSlot{};
    else
        p.bench.take(std::size_t(slot - p.bench.begin()));
}

void Game::takeBack(uint16_t uid, bool toDeck) {
    int owner = 0;
    PokemonSlot* slot = find(uid, &owner);
    if (!slot) return;
    PlayerState& p = s_.players[owner];
    say(slot->card->name, toDeck ? " is shuffled into the deck" : " goes back to the hand");
    std::vector<const CardDef*> cards{slot->card};
    for (const CardDef* c : slot->below) cards.push_back(c);
    for (const CardDef* t : slot->tools) cards.push_back(t);
    for (const CardDef* c : cards) toDeck ? p.deck.push_back(c) : toHand(owner, c);
    for (int t = 0; t < TypeCount; ++t) p.discardEnergy[t] += slot->energy[t];  // energy isn't a card (ruling)
    clearConditions(*slot);
    if (slot == &p.active)
        p.active = PokemonSlot{};
    else
        p.bench.take(std::size_t(slot - p.bench.begin()));
    if (toDeck) std::shuffle(p.deck.begin(), p.deck.end(), rng_);
}

void Game::toHand(int player, const CardDef* card) {
    PlayerState& p = s_.players[player];
    if (p.hand.size() < std::size_t(rules::HandLimit))
        return p.hand.push_back(card);
    p.discard.push_back(card);  // assumed: a full hand takes no more (DESIGN.md §10)
    say("  P", player + 1, "'s hand is full: ", card->name, " goes to the discard pile");
}

void Game::draw(int player) {
    PlayerState& p = s_.players[player];
    if (p.deck.empty()) {
        say("P", player + 1, " has no cards left to draw");
    } else if (p.hand.size() >= std::size_t(rules::HandLimit)) {
        say("P", player + 1, " has a full hand and draws nothing");
    } else {
        p.hand.push_back(p.deck.pop_back());
        say("P", player + 1, " draws ", p.hand.back()->name);
    }
}

// --- Modifiers ---

int Game::sumMods(ModKind kind, const PokemonSlot& subject, int owner) const {
    int sum = 0;
    forEachMod(kind, &subject, owner, [&](const Modifier& m) { sum += m.amount; });
    return sum;
}

bool Game::hasMod(ModKind kind, const PokemonSlot& subject, int owner) const {
    bool found = false;
    forEachMod(kind, &subject, owner, [&](const Modifier&) { found = true; });
    return found;
}

bool Game::playerHas(ModKind kind, int player) const {
    bool found = false;
    forEachMod(kind, nullptr, player, [&](const Modifier&) { found = true; });
    return found;
}

int Game::maxHp(const PokemonSlot& p, int owner) const {
    int hp = p.card->hp;
    forEachMod(ModKind::Hp, &p, owner, [&](const Modifier& m) { hp += m.energy < 0 ? m.amount : m.amount * energyOf(p, owner)[m.energy]; });
    return hp;
}

EnergyCounts Game::energyOf(const PokemonSlot& p, int owner) const {
    EnergyCounts e = p.energy;
    int doubled = -1;  // "This effect doesn't stack"
    forEachMod(ModKind::DoubleEnergy, &p, owner, [&](const Modifier& m) { doubled = m.energy; });
    if (doubled >= 0) e[doubled] = uint8_t(2 * e[doubled]);
    return e;
}

uint8_t Game::blockedStatuses(const PokemonSlot& p, int owner) const {
    uint8_t blocked = 0;
    forEachMod(ModKind::NoConditions, &p, owner, [&](const Modifier& m) { blocked |= m.amount ? uint8_t(m.amount) : 0xFF; });
    return blocked;
}

// Runs one trigger form (during-checkup, at-start-of-turn) for each of a player's Pokémon and their Tools.
void Game::runTriggers(int player, const Stmt* CardDef::*form, bool CardDef::*whileActive) {
    Uids each;
    if (s_.players[player].active.card) each.push_back(s_.players[player].active.uid);
    for (const PokemonSlot& b : s_.players[player].bench) each.push_back(b.uid);
    for (uint16_t uid : each)
        forEachSource(uid, [&](const CardDef* c) {
            if (!(c->*form) || (whileActive && c->*whileActive && find(uid) != &s_.players[player].active)) return;
            say("P", player + 1, "'s ", c->name, c->abilities.empty() ? "" : ": " + c->abilities[0].name);
            EffectCtx ctx{*this, player};
            ctx.self = uid;
            ctx.card = c;
            (c->*form)->run(ctx);
        });
}

int Game::retreatCost(const PokemonSlot& p, int owner) const {
    if (hasMod(ModKind::NoRetreatCost, p, owner)) return 0;
    return std::max(0, p.card->retreat + sumMods(ModKind::RetreatCost, p, owner));
}

bool Game::canRetreat(const PokemonSlot& p, int owner) const {
    return !(p.status & CantActStatuses) && !hasMod(ModKind::CantRetreat, p, owner);
}

bool Game::canUseAttack(const PokemonSlot& p, int owner, int attack) const {
    if ((p.status & CantActStatuses) || hasMod(ModKind::CantAttack, p, owner)) return false;
    bool blocked = false;
    forEachMod(ModKind::CantUseAttack, &p, owner, [&](const Modifier& m) { blocked |= m.amount == attack; });
    return !blocked;
}

EnergyCounts Game::attackCost(const PokemonSlot& p, int owner, int attack) const {
    EnergyCounts cost = attacksOf(p, owner)[std::size_t(attack)]->cost;
    int counts[TypeCount];
    for (int t = 0; t < TypeCount; ++t) counts[t] = cost[t];
    forEachMod(ModKind::AttackCost, &p, owner, [&](const Modifier& m) { counts[m.energy < 0 ? int(Type::Colorless) : m.energy] += m.amount; });
    for (int t = 0; t < TypeCount; ++t) cost[t] = uint8_t(std::max(0, counts[t]));  // "cost 2 less {C}" stops at 0
    return cost;
}

// --- Decisions and chance ---

int Game::choose(int player, int options) {
    if (options <= 1) return 0;
    options_.clear();
    for (int i = 0; i < options; ++i) options_.push_back({ActionKind::Choose, int8_t(i)});
    std::size_t pick = policies_[player]->choose(PlayerView(s_, player), options_);
    if (pick >= options_.size()) throw std::out_of_range("policy chose option " + std::to_string(pick));
    return int(pick);
}

bool Game::flip() {
    bool heads = std::bernoulli_distribution(0.5)(rng_);
    say("  coin: ", heads ? "heads" : "tails");
    return heads;
}

// --- Log ---

std::string Game::show(const PokemonSlot& slot, int owner) const {
    if (!slot.card) return "-";
    std::string s = slot.card->name + " " + std::to_string(maxHp(slot, owner) - slot.damage) + "/" +
                    std::to_string(maxHp(slot, owner));
    std::string energy;
    for (int t = 0; t < TypeCount; ++t) energy.append(slot.energy[t], typeLetter(Type(t)));
    if (!energy.empty()) s += " [" + energy + "]";
    for (const CardDef* t : slot.tools) s += " +" + t->name;
    constexpr const char* Names[] = {"Asleep", "Paralyzed", "Confused", "Poisoned", "Burned"};
    for (int i = 0; i < 5; ++i)
        if (slot.status & (1 << i)) s += std::string(" (") + Names[i] + ")";
    return s;
}

void Game::showBoard() {
    if (!log_) return;
    for (int player : {s_.current, 1 - s_.current}) {
        const PlayerState& p = s_.players[player];
        std::string line = "  P" + std::to_string(player + 1) + "  Active: " + show(p.active, player) + "   Bench: ";
        if (p.bench.empty()) line += "-";
        for (std::size_t i = 0; i < p.bench.size(); ++i) line += (i ? ", " : "") + show(p.bench[i], player);
        line += "   (hand " + std::to_string(p.hand.size()) + ", deck " + std::to_string(p.deck.size()) + ")";
        log_->line(line);
    }
    if (s_.stadium) log_->line("  Stadium: " + s_.stadium->name);
    std::string hand;
    for (const CardDef* c : s_.players[s_.current].hand) hand += (hand.empty() ? "" : ", ") + c->name;
    log_->line("  P" + std::to_string(s_.current + 1) + " hand: " + (hand.empty() ? "-" : hand));
}
