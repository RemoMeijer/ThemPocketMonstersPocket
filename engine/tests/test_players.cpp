// Player checks: HeuristicPolicy's rules on hand-built positions. The Table's player 0 (P1) is played by the
// heuristic; scripts are written inline, so this doesn't need data/effects.tsv.
#include "effects/effect.h"
#include "players/heuristic.h"
#include "test_support.h"

namespace {
std::vector<std::shared_ptr<const Node>> arena;

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

int main() {
    takesTheWinningAttack();
    gustsForTheWinAndPicksThePlannedTarget();
    healsTheDamagedActive();
    energyToTheActiveThatNeedsIt();
    newActiveIsTheReadyAttacker();
    return report("player");
}
