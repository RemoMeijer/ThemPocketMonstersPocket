// Rule checks on hand-built positions: the core rules without card effects.
#include "test_support.h"

static void knockOutScoresAPoint() {
    Table t;
    GameState& s = t.position();
    s.players[1].active.damage = 30;  // 30 more knocks out the 60 HP Mon1
    s.players[1].active.energy[int(Type::Fire)] = 2;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].points == 1);
    CHECK(s.players[1].active.card == &t.cards[2]);  // the only benched Pokémon moved up
    CHECK(s.players[1].bench.empty());
    CHECK(s.players[1].discardEnergy[int(Type::Fire)] == 2);  // its energy went to the discard pile
    CHECK(s.current == 1 && s.turn == 6);
}

static void exKnockOutScoresTwo() {
    Table t;
    t.cards[1].points = 2;
    GameState& s = t.position();
    s.players[1].active.damage = 30;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].points == 2);
}

static void attackWithoutDamageKeepsGoing() {
    Table t;
    GameState& s = t.position();
    t.play(ActionKind::Attack);
    CHECK(s.players[1].active.damage == 30);
    CHECK(s.players[0].points == 0);
    CHECK(s.current == 1 && s.phase == Phase::Main);  // attacking ended P1's turn
}

static void unpaidCostBlocksAttack() {
    Table t;
    GameState& s = t.position();
    s.players[0].active.energy = {};
    CHECK(!t.can(ActionKind::Attack));
    s.players[0].active.energy[int(Type::Fire)] = 1;  // wrong type
    CHECK(!t.can(ActionKind::Attack));
    s.players[0].active.energy[int(Type::Water)] = 1;
    CHECK(t.can(ActionKind::Attack));

    EnergyCounts twoAny{};
    twoAny[int(Type::Colorless)] = 2;
    EnergyCounts fireWater{};
    fireWater[int(Type::Fire)] = 1;
    fireWater[int(Type::Water)] = 1;
    CHECK(canPay(fireWater, twoAny));  // Colorless takes any type
    fireWater[int(Type::Water)] = 0;
    CHECK(!canPay(fireWater, twoAny));
}

static void thirdPointWins() {
    Table t;
    GameState& s = t.position();
    s.players[0].points = 2;
    s.players[1].active.damage = 30;
    t.play(ActionKind::Attack);
    CHECK(s.phase == Phase::Over && s.winner == 0);
}

static void nothingLeftToPromoteLoses() {
    Table t;
    GameState& s = t.position();
    s.players[1].bench.clear();
    s.players[1].active.damage = 30;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].points == 1);
    CHECK(s.phase == Phase::Over && s.winner == 0);
}

// Both players at 2 points, both Active Pokémon Poisoned with 10 HP left: the Checkup knocks out both
// and takes both to 3 points. Returns the winner, given who still has a Benched Pokémon.
static int bothReachThreeAtOnce(bool bench0, bool bench1) {
    Table t;
    GameState& s = t.position();
    s.players[1].bench.clear();
    if (bench0) t.bench(0, t.cards[3]);
    if (bench1) t.bench(1, t.cards[2]);
    for (PlayerState& p : s.players) {
        p.points = 2;
        p.active.damage = 50;
        p.active.status = Poisoned;
    }
    t.play(ActionKind::EndTurn);
    CHECK(s.phase == Phase::Over && s.players[0].points == 3 && s.players[1].points == 3);
    return s.winner;
}

// Tips: "If a player doesn't have any Pokémon remaining in play, that player loses the battle regardless
// of the number of points each player has."
static void noPokemonLeftLosesWhateverThePoints() {
    CHECK(bothReachThreeAtOnce(true, false) == 0);
    CHECK(bothReachThreeAtOnce(false, true) == 1);
    CHECK(bothReachThreeAtOnce(false, false) == -1);  // nobody left: a tie
    CHECK(bothReachThreeAtOnce(true, true) == -1);    // both reach 3 points at once: a tie
}

static void firstPlayerGetsNoEnergyOnTurnOne() {
    Table t;
    t.game.setup();
    GameState& s = t.game.state();
    CHECK(s.turn == 1 && s.current == s.first);
    CHECK(!s.players[s.current].energy);
    CHECK(s.players[0].nextEnergy == Type::Water && s.players[1].nextEnergy == Type::Water);  // both shown from the start
    CHECK(!t.can(ActionKind::AttachEnergy));
    CHECK(s.players[s.current].hand.size() == 6 - s.players[s.current].bench.size() - 1);  // 5 + 1 draw, minus placed
    t.play(ActionKind::EndTurn);
    CHECK(s.turn == 2 && s.current != s.first);
    CHECK(s.players[s.current].energy == Type::Water);
}

// The next energy is known in advance; at the start of the turn it comes up and a new next one is rolled.
static void nextEnergyComesUp() {
    Table t;
    GameState& s = t.position();
    PlayerState& p = s.players[1];
    p.nextEnergy = Type::Fire;
    t.play(ActionKind::EndTurn);  // P2's turn starts
    CHECK(s.current == 1 && p.energy == Type::Fire);
    CHECK(p.nextEnergy == Type::Water);  // rolled from the deck's energy types
}

// Unused energy goes to the discard pile at the end of the turn; a new one comes next turn (App).
static void unusedEnergyGoesToTheDiscardPile() {
    Table t;
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.energy = Type::Water;
    p.nextEnergy = Type::Fire;
    t.play(ActionKind::EndTurn);
    CHECK(!p.energy && p.discardEnergy[int(Type::Water)] == 1);
    t.play(ActionKind::EndTurn);  // P2's turn ends, P1's starts
    CHECK(s.current == 0 && p.energy == Type::Fire);
}

static void benchHoldsThree() {
    Table t;
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    p.hand.push_back(&t.cards[5]);
    CHECK(t.can(ActionKind::PlaceBench));
    for (int i = 6; i < 9; ++i) t.bench(0, t.cards[i]);
    CHECK(!t.can(ActionKind::PlaceBench));
}

static void threeCopiesRejected() {
    std::vector<CardDef> cards = testCards();
    Deck d = testDeck(cards);
    validateDeck(d);  // the normal test deck is fine
    d.cards[1] = d.cards[0];
    d.cards[2] = d.cards[0];  // three Mon0, one Mon1
    bool rejected = false;
    try {
        validateDeck(d);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    CHECK(rejected);
}

static void weaknessAddsTwenty() {
    Table t;
    t.cards[0].type = Type::Lightning;
    t.cards[1].weakness = Type::Lightning;
    GameState& s = t.position();
    t.play(ActionKind::Attack);
    CHECK(s.players[1].active.damage == 30 + rules::WeaknessBonus);

    Table other;  // weak to something else: no bonus
    other.cards[1].weakness = Type::Fire;
    GameState& s2 = other.position();
    other.play(ActionKind::Attack);
    CHECK(s2.players[1].active.damage == 30);
}

static void retreatPaysAndSwaps() {
    Table t;
    t.cards[0].retreat = 1;
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    t.bench(0, t.cards[3]);
    t.play(ActionKind::Retreat, 0);
    CHECK(p.active.card == &t.cards[3]);
    CHECK(p.bench[0].card == &t.cards[0] && p.bench[0].energyCount() == 0);  // paid 1 Water
    CHECK(p.discardEnergy[int(Type::Water)] == 1);                            // into the discard pile
    CHECK(!t.can(ActionKind::Retreat));  // once per turn
    CHECK(t.can(ActionKind::EndTurn));   // the turn goes on after retreating

    Table pricey;
    pricey.cards[0].retreat = 2;  // only 1 energy attached
    pricey.position();
    pricey.bench(0, pricey.cards[3]);
    CHECK(!pricey.can(ActionKind::Retreat));

    Table costless;
    GameState& s3 = costless.position();  // retreat cost 0, works without energy
    s3.players[0].active.energy = {};
    CHECK(!costless.can(ActionKind::Retreat));  // nothing on the bench to swap with
    costless.bench(0, costless.cards[3]);
    CHECK(costless.can(ActionKind::Retreat));
}

static void evolutionKeepsDamageAndEnergy() {
    Table t;
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.active.damage = 20;
    p.hand.clear();
    p.hand.push_back(&t.cards[10]);  // Evo1
    p.hand.push_back(&t.cards[11]);  // Evo2
    t.play(ActionKind::Evolve, 0, -1);
    CHECK(p.active.card == &t.cards[10]);
    CHECK(p.active.damage == 20 && p.active.energy[int(Type::Water)] == 1);
    CHECK(p.active.below.size() == 1 && p.active.below[0] == &t.cards[0]);
    CHECK(!t.can(ActionKind::Evolve));  // Evo2 waits: Evo1 only just evolved

    p.active.enteredTurn = s.turn - 2;  // as if Evo1 evolved on P1's previous turn
    t.play(ActionKind::Evolve, 0, -1);
    CHECK(p.active.card == &t.cards[11] && p.active.below.size() == 2);
}

static void evolutionTiming() {
    Table t;
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    p.hand.push_back(&t.cards[10]);  // Evo1
    p.hand.push_back(&t.cards[0]);   // Mon0
    s.turn = 1;
    CHECK(!t.can(ActionKind::Evolve));  // nobody evolves on their own first turn: P1 went first...
    s.first = 1;
    s.turn = 2;
    CHECK(!t.can(ActionKind::Evolve));  // ...or second
    s.first = 0;
    s.turn = 5;
    t.play(ActionKind::Evolve, 0, -1);  // the Active Mon0 has been in play since setup
    p.hand.push_back(&t.cards[10]);     // a second Evo1
    t.play(ActionKind::PlaceBench, 0);  // Mon0 comes into play now
    CHECK(!t.can(ActionKind::Evolve));  // so it can't evolve this turn

    Table u;  // Evo1 only fits on Mon0
    GameState& s2 = u.position();
    u.active(0, u.cards[4]);
    s2.players[0].hand.clear();
    s2.players[0].hand.push_back(&u.cards[10]);
    CHECK(!u.can(ActionKind::Evolve));
}

static void knockedOutEvolutionDiscardsAllItsCards() {
    Table t;
    GameState& s = t.position();
    PokemonSlot& victim = s.players[1].active;
    victim.below.push_back(&t.cards[1]);
    victim.card = &t.cards[10];
    victim.damage = 60;  // 90 HP, one hit of 30 left
    std::size_t before = s.players[1].discard.size();
    t.play(ActionKind::Attack);
    CHECK(s.players[0].points == 1);
    CHECK(s.players[1].discard.size() == before + 2);
}

static void statusBlocksAttackAndRetreat() {
    Table t;
    GameState& s = t.position();
    t.bench(0, t.cards[3]);
    s.players[0].active.status = Asleep;
    CHECK(!t.can(ActionKind::Attack) && !t.can(ActionKind::Retreat));
    s.players[0].active.status = Paralyzed;
    CHECK(!t.can(ActionKind::Attack) && !t.can(ActionKind::Retreat));
    s.players[0].active.status = Poisoned | Confused;
    CHECK(t.can(ActionKind::Attack) && t.can(ActionKind::Retreat));
}

static void checkupPoisonBurnParalysis() {
    Table t;
    GameState& s = t.position();
    s.players[1].active.status = Poisoned | Paralyzed;
    s.players[0].active.status = Paralyzed;
    t.play(ActionKind::EndTurn);  // end of P1's turn: Checkup
    CHECK(s.players[1].active.damage == rules::PoisonDamage);
    CHECK(s.players[0].active.status == 0);  // P1's own Paralysis wore off at the end of P1's turn
    CHECK(s.players[1].active.status == (Poisoned | Paralyzed));
    t.play(ActionKind::EndTurn);  // end of P2's turn
    CHECK(s.players[1].active.damage == 2 * rules::PoisonDamage);
    CHECK(s.players[1].active.status == Poisoned);

    Table b;
    GameState& s2 = b.position();
    s2.players[1].active.status = Burned;
    b.play(ActionKind::EndTurn);
    CHECK(s2.players[1].active.damage == rules::BurnDamage);
}

static void retreatEndsSpecialConditions() {
    Table t;
    GameState& s = t.position();
    t.bench(0, t.cards[3]);
    s.players[0].active.status = Poisoned | Confused;
    t.play(ActionKind::Retreat, 0);
    CHECK(s.players[0].bench[0].status == 0);
}

static void randomGamesAlwaysEnd() {
    Table t;
    Deck evolutions = evolutionDeck(t.cards);
    Game mixed(t.deck, evolutions, t.policy0, t.policy1);
    for (int i = 0; i < 2000; ++i) {
        Game& g = i % 2 ? mixed : t.game;
        Result r = g.run();
        CHECK(r.turns >= 1 && r.turns <= rules::TurnCap);
        CHECK(g.state().phase == Phase::Over);
        if (r.winner >= 0) {
            const GameState& s = g.state();
            const PlayerState& loser = s.players[1 - r.winner];
            CHECK(s.players[r.winner].points >= rules::PointsToWin || (!loser.active.card && loser.bench.empty()));
        }
    }
}

int main() {
    knockOutScoresAPoint();
    exKnockOutScoresTwo();
    attackWithoutDamageKeepsGoing();
    unpaidCostBlocksAttack();
    thirdPointWins();
    nothingLeftToPromoteLoses();
    noPokemonLeftLosesWhateverThePoints();
    firstPlayerGetsNoEnergyOnTurnOne();
    nextEnergyComesUp();
    unusedEnergyGoesToTheDiscardPile();
    benchHoldsThree();
    threeCopiesRejected();
    weaknessAddsTwenty();
    retreatPaysAndSwaps();
    evolutionKeepsDamageAndEnergy();
    evolutionTiming();
    knockedOutEvolutionDiscardsAllItsCards();
    statusBlocksAttackAndRetreat();
    checkupPoisonBurnParalysis();
    retreatEndsSpecialConditions();
    randomGamesAlwaysEnd();
    return report("rules");
}
