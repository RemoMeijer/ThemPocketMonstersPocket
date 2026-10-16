#include "game/game.h"

#include <algorithm>

#include "effects/effect.h"
#include "moves/moves.h"

namespace {
// Main-phase moves in the order they are offered. EndTurn comes last (RandomPolicy relies on it).
constexpr ActionKind MainMoves[] = {ActionKind::PlaceBench,   ActionKind::Evolve,     ActionKind::PlayTrainer,
                                    ActionKind::AttachTool,   ActionKind::AttachEnergy, ActionKind::UseAbility,
                                    ActionKind::Retreat,      ActionKind::Attack,     ActionKind::EndTurn};

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

void Game::apply(Action action) { moveRule(action.kind).apply(*this, action); }

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
    startTurn();
}

void Game::startTurn() {
    if (s_.turn == rules::TurnCap) return finish(-1);
    ++s_.turn;
    PlayerState& p = s_.players[s_.current];
    p.retreated = false;
    say("\n=== Turn ", s_.turn, ": P", s_.current + 1, "   points P1 ", s_.players[0].points, " - P2 ",
        s_.players[1].points, " ===");
    if (!firstTurnOfGame() || rules::FirstPlayerDrawsTurn1) draw(s_.current);
    // The player going first gets no energy on their first turn. Unused energy stays in the Energy Zone,
    // and then no new one is generated (Serebii, "Battle Mechanics").
    if (firstTurnOfGame()) {
    } else if (p.energy) {
        say("P", s_.current + 1, "'s unused ", typeName(*p.energy), " energy is still in the Energy Zone");
    } else {
        auto pick = std::uniform_int_distribution<std::size_t>(0, p.energyTypes.size() - 1)(rng_);
        p.energy = p.energyTypes[pick];
        say("P", s_.current + 1, " gets ", typeName(*p.energy), " energy from the Energy Zone");
    }
    showBoard();
}

void Game::endTurn() {
    int me = s_.current;
    // "At the end of your turn…" abilities of the player whose turn it was.
    Uids owners;
    if (s_.players[me].active.card) owners.push_back(s_.players[me].active.uid);
    for (const PokemonSlot& b : s_.players[me].bench) owners.push_back(b.uid);
    for (uint16_t uid : owners) {
        for (bool tool : {false, true}) {  // the Pokémon's ability, then its Tool ("Leftovers")
            PokemonSlot* p = find(uid);
            const CardDef* source = p ? (tool ? p->tool : p->card) : nullptr;
            if (!source || !source->endOfTurn || (source->endOfTurnWhileActive && p != &s_.players[me].active)) continue;
            say("P", me + 1, "'s ", source->name, ": ", source->abilities.empty() ? "end of turn" : source->abilities[0].name);
            EffectCtx ctx{*this, me};
            ctx.self = uid;
            source->endOfTurn->run(ctx);
        }
    }
    // Pokémon Checkup, for both Active Pokémon (Special Conditions are only ever on an Active Pokémon).
    for (int p : {me, 1 - me}) {
        PokemonSlot& a = s_.players[p].active;
        if (!a.card || !a.status) continue;
        if (a.status & Poisoned) {
            a.damage += rules::PoisonDamage;
            say(a.card->name, " takes ", rules::PoisonDamage, " Poison damage");
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
    auto& timed = s_.timed;
    for (std::size_t i = timed.size(); i-- > 0;)
        if (timed[i].lastTurn <= s_.turn) timed.take(i);
    s_.current = 1 - me;
    s_.phase = Phase::Main;
    startTurn();
}

void Game::knockOutCheck() {
    if (s_.phase == Phase::Over) return;
    for (int p = 0; p < 2; ++p) {
        PlayerState& ps = s_.players[p];
        if (ps.active.card && ps.active.damage >= maxHp(ps.active, p)) {
            knockOut(p, ps.active);
            ps.active = PokemonSlot{};
        }
        for (std::size_t i = ps.bench.size(); i-- > 0;) {
            if (ps.bench[i].damage >= maxHp(ps.bench[i], p)) {
                knockOut(p, ps.bench[i]);
                ps.bench.take(i);
            }
        }
    }
    // No Pokémon in play loses whatever the points; nobody left is a tie (Tips, DESIGN.md §10).
    auto out = [&](int p) { return !s_.players[p].active.card && s_.players[p].bench.empty(); };
    if (out(0) || out(1)) return finish(out(0) && out(1) ? -1 : out(0) ? 1 : 0);
    bool won[2];
    for (int p = 0; p < 2; ++p) won[p] = s_.players[p].points >= rules::PointsToWin;
    if (won[0] && won[1]) return finish(-1);  // both reach the points at once: a tie (Tips)
    if (won[0] || won[1]) return finish(won[0] ? 0 : 1);
    for (int p : {1 - s_.current, s_.current})
        if (!s_.players[p].active.card) promote(p);
}

void Game::knockOut(int player, PokemonSlot& slot) {
    PlayerState& owner = s_.players[player];
    PlayerState& scorer = s_.players[1 - player];
    scorer.points += slot.card->points;
    say(slot.card->name, " is knocked out. P", 2 - player, " scores ", slot.card->points, " (total ", scorer.points,
        ")");
    for (const CardDef* c : slot.below) owner.discard.push_back(c);
    owner.discard.push_back(slot.card);
    if (slot.tool) owner.discard.push_back(slot.tool);
    clearConditions(slot);
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
}

void Game::evolve(PokemonSlot& slot, const CardDef* card) {
    slot.below.push_back(slot.card);
    slot.card = card;
    slot.enteredTurn = s_.turn;
    slot.abilityTurn = -1;  // "once during your turn" belongs to the card: the new one may use its own (App)
    clearConditions(slot);  // evolving ends Special Conditions and effects on it
}

void Game::movedToActive(int player) { s_.players[player].active.becameActiveTurn = s_.turn; }

void Game::clearConditions(PokemonSlot& slot) {
    slot.status = 0;
    auto& timed = s_.timed;
    for (std::size_t i = timed.size(); i-- > 0;)
        if (timed[i].uid == slot.uid) timed.take(i);
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

int Game::maxHp(const PokemonSlot& p, int owner) const { return p.card->hp + sumMods(ModKind::Hp, p, owner); }

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
    EnergyCounts cost = p.card->attacks[attack].cost;
    int colorless = cost[int(Type::Colorless)] + sumMods(ModKind::AttackCost, p, owner);  // "cost 2 less {C}" stops at 0
    cost[int(Type::Colorless)] = uint8_t(std::max(0, colorless));
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
    if (slot.tool) s += " +" + slot.tool->name;
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
