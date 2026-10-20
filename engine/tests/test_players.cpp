// Player checks: HeuristicPolicy's rules on hand-built positions. The Table's player 0 (P1) is played by the
// heuristic; scripts are written inline, so this doesn't need data/effects.tsv.
#include "effects/effect.h"
#include "players/heuristic.h"
#include "test_support.h"

namespace {
std::vector<std::shared_ptr<const Node>> arena;

const Stmt* stmt(const std::string& script) {
    ScriptBuilder b(arena);
    return b.stmt(parseScript(script));
}

// A Fossil as effects.tsv gives it: a 40-HP Basic that can't retreat and may be discarded from play.
CardDef fossil() {
    CardDef c;
    c.id = "T-fossil";
    c.name = "Test Fossil";
    c.hp = 40;
    c.type = Type::Colorless;
    c.effect = "Play this card as if it were a 40-HP Basic {C} Pokemon.";
    c.supported = true;
    ScriptBuilder b(arena);
    c.passive.push_back(b.modifier(parseScript("(cant-retreat)"), ModScope::Self));
    c.activated = b.stmt(parseScript("(discard-pokemon self)"));
    c.activatedRepeat = true;
    return c;
}

int energy(const PokemonSlot& p, Type t) { return p.energy[int(t)]; }

// A Trainer as effects.tsv gives it: its play statement, and the script text the heuristic reads.
CardDef trainer(TrainerKind kind, const char* name, const std::string& play) {
    CardDef c;
    c.id = std::string("TR-") + name;
    c.name = name;
    c.category = Category::Trainer;
    c.trainer = kind;
    c.supported = true;
    ScriptBuilder b(arena);
    c.play = b.stmt(parseScript(play));
    c.scripts = "(play " + play + ")";
    return c;
}

// The position from Table::position(), with P1 played by `h` and P1's hand holding only `hand`.
GameState& heuristicPosition(Table& t, HeuristicPolicy& h, std::vector<const CardDef*> hand = {}) {
    GameState& s = t.position();
    t.policy0.delegate = &h;
    s.players[0].hand.clear();
    for (const CardDef* c : hand) s.players[0].hand.push_back(c);
    return s;
}
}  // namespace

static void takesTheWinningAttack() {
    Table t;
    HeuristicPolicy h;
    GameState& s = heuristicPosition(t, h, {&t.cards[5], &t.cards[6]});  // Basics it could bench first
    s.players[0].points = 2;
    s.players[1].active.damage = 30;  // Splash's 30 knocks out Mon1
    t.game.step();
    CHECK(s.phase == Phase::Over && s.winner == 0);
}

static void gustsForTheWinAndPicksThePlannedTarget() {
    Table t;
    HeuristicPolicy h;
    CardDef cyrus = trainer(TrainerKind::Supporter, "Cyrus", "(switch-in (choose opp bench damaged))");
    CardDef exMon = t.cards[3];
    exMon.ex = true;
    exMon.points = 2;
    GameState& s = heuristicPosition(t, h, {&cyrus});
    s.players[0].points = 1;
    s.players[1].bench[0].damage = 50;  // Mon2, 10 HP left: knocked out, but 1 point doesn't win
    t.bench(1, exMon).damage = 40;      // 20 HP left and worth 2: the win
    t.game.step();
    CHECK(s.players[0].hand.empty() && s.players[1].active.card == &exMon);
    t.game.step();
    CHECK(s.phase == Phase::Over && s.winner == 0);
}

static void healsTheDamagedActive() {
    Table t;
    HeuristicPolicy h;
    CardDef potion = trainer(TrainerKind::Item, "Potion", "(heal (choose my all damaged) 20)");
    GameState& s = heuristicPosition(t, h, {&potion});
    s.players[0].active.damage = 30;
    t.bench(0, t.cards[2]).damage = 10;
    t.game.step();
    CHECK(s.players[0].active.damage == 10 && s.players[0].bench[0].damage == 10);
}

static void energyToTheActiveThatNeedsIt() {
    Table t;
    HeuristicPolicy h;
    GameState& s = heuristicPosition(t, h);
    s.players[0].active.energy = {};
    s.players[0].energy = Type::Water;
    t.bench(0, t.cards[2]).energy[int(Type::Water)] = 1;  // ready already: the better attacker, but it needs nothing
    t.game.step();
    CHECK(s.players[0].active.energy[int(Type::Water)] == 1 && s.players[0].bench[0].energy[int(Type::Water)] == 1);
}

static void newActiveIsTheReadyAttacker() {
    Table t;
    HeuristicPolicy h;
    GameState& s = heuristicPosition(t, h);
    s.current = 1;
    s.players[1].active.energy[int(Type::Water)] = 1;
    s.players[0].active.damage = 30;  // P2's Splash knocks it out
    t.bench(0, t.cards[2]);
    t.bench(0, t.cards[3]).energy[int(Type::Water)] = 1;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].active.card == &t.cards[3]);
}

// The Energy-type choices go to the player. Each position is one where the engine's old fixed pick (the most plentiful
// type, or the first matching one) differs from the type the heuristic should pick.
static void retreatPaysWithTheUnneededEnergy() {
    Table t;
    HeuristicPolicy h;
    t.cards[0].retreat = 1;
    GameState& s = heuristicPosition(t, h);
    s.players[0].active.energy = {};
    s.players[0].active.energy[int(Type::Water)] = 2;  // Splash needs 1 Water
    s.players[0].active.energy[int(Type::Fire)] = 1;
    t.bench(0, t.cards[2]);
    t.play(ActionKind::Retreat, 0);
    const PokemonSlot& out = s.players[0].bench[0];
    CHECK(energy(out, Type::Water) == 2 && energy(out, Type::Fire) == 0 && s.players[0].discardEnergy[int(Type::Fire)] == 1);
}

static void discardEnergyKeepsTheNeededType() {
    Table t;
    HeuristicPolicy h;
    t.cards[0].attacks[0].script = stmt("(seq (hit) (discard-energy self any 1))");
    GameState& s = heuristicPosition(t, h);
    s.players[0].active.energy[int(Type::Water)] = 2;
    s.players[0].active.energy[int(Type::Fire)] = 1;
    t.play(ActionKind::Attack);
    CHECK(energy(s.players[0].active, Type::Water) == 2 && energy(s.players[0].active, Type::Fire) == 0);
}

static void moveEnergyTakesTheNeededType() {
    Table t;
    HeuristicPolicy h;
    CardDef item = trainer(TrainerKind::Item, "Mover", "(move-energy (choose my bench (has-energy 1)) my-active any 1)");
    GameState& s = heuristicPosition(t, h, {&item});
    s.players[0].active.energy = {};
    PokemonSlot& from = t.bench(0, t.cards[2]);
    from.energy[int(Type::Water)] = 1;
    from.energy[int(Type::Fire)] = 2;
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(energy(s.players[0].active, Type::Water) == 1 && energy(s.players[0].bench[0], Type::Fire) == 2);
}

static void attachFromDiscardTakesTheNeededType() {
    Table t;
    HeuristicPolicy h;
    CardDef item = trainer(TrainerKind::Item, "Recycler", "(attach-from-discard my-active any 1)");
    GameState& s = heuristicPosition(t, h, {&item});
    s.players[0].active.energy = {};
    s.players[0].discardEnergy[int(Type::Fire)] = 3;
    s.players[0].discardEnergy[int(Type::Water)] = 1;
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(energy(s.players[0].active, Type::Water) == 1 && s.players[0].discardEnergy[int(Type::Fire)] == 3);
}

static void differentTypesStartWithTheNeededOne() {  // Professor Sada
    Table t;
    HeuristicPolicy h;
    CardDef sada = trainer(TrainerKind::Supporter, "Sada", "(attach-from-discard (choose my all) different 2)");
    GameState& s = heuristicPosition(t, h, {&sada});
    s.players[0].active.energy = {};
    for (Type type : {Type::Grass, Type::Fire, Type::Water, Type::Lightning, Type::Psychic}) s.players[0].discardEnergy[int(type)] = 1;
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(energy(s.players[0].active, Type::Water) == 1 && s.players[0].active.energyCount() == 2);
}

// "At any time during your turn, you may discard this card from play": no points, and not an Ability.
static void fossilsCanBeDiscardedFromPlay() {
    Table t;
    CardDef f = fossil();
    GameState& s = t.position();
    PokemonSlot& benched = t.bench(0, f);
    s.timed.push_back({Modifier{ModKind::NoAbilities}, 0, benched.uid, 99});  // "loses all Abilities" (Budew): no matter
    CHECK(t.can(ActionKind::UseAbility));
    t.play(ActionKind::UseAbility, 0, 0);
    CHECK(s.players[0].bench.empty() && s.players[0].discard.back() == &f && s.players[1].points == 0);

    t.active(0, f);  // from the Active Spot: a new Active comes up
    t.bench(0, t.cards[2]);
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(s.players[0].active.card == &t.cards[2] && s.players[1].points == 0 && s.phase == Phase::Main);

    t.active(0, f);  // with no other Pokémon in play, its owner loses
    s.players[0].bench.clear();
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(s.phase == Phase::Over && s.winner == 1);
}

static void heuristicDiscardsOnlyAStuckActiveFossil() {
    Table t;
    HeuristicPolicy h;
    CardDef f = fossil();
    GameState& s = heuristicPosition(t, h);
    t.bench(0, f);
    t.game.step();  // the Bench Fossil stays; Mon0 attacks
    CHECK(s.players[0].bench.size() == 1 && s.current == 1);

    Table u;
    HeuristicPolicy h2;
    GameState& su = heuristicPosition(u, h2);
    u.active(0, f);
    u.bench(0, u.cards[2]).energy[int(Type::Water)] = 1;  // ready to attack
    u.game.step();
    CHECK(su.players[0].active.card == &u.cards[2] && su.players[1].points == 0);
}

int main() {
    takesTheWinningAttack();
    gustsForTheWinAndPicksThePlannedTarget();
    healsTheDamagedActive();
    energyToTheActiveThatNeedsIt();
    newActiveIsTheReadyAttacker();
    retreatPaysWithTheUnneededEnergy();
    discardEnergyKeepsTheNeededType();
    moveEnergyTakesTheNeededType();
    attachFromDiscardTakesTheNeededType();
    differentTypesStartWithTheNeededOne();
    fossilsCanBeDiscardedFromPlay();
    heuristicDiscardsOnlyAStuckActiveFossil();
    return report("player");
}
