// Encoder checks: a hand-built position encodes to the expected tokens and actions, and nothing hidden shows.
// The static card table is checked on data/cards.tsv when run from the project folder.
#include <algorithm>
#include <filesystem>

#include "ml/encode.h"
#include "test_support.h"

using namespace enc;

namespace {
// The Table's position, with indices as if the test cards were a CardDb: P1 holds Mon3 and Evo1, P2 holds Mon7..9,
// P1's Active (Mon0) has 20 damage and Mon5 on the Bench, P2's Mon2 sits in Bench spot 2, and a Water Energy waits.
GameState& position(Table& t) {
    for (std::size_t i = 0; i < t.cards.size(); ++i) t.cards[i].index = int(i);
    GameState& s = t.position();
    s.players[0].hand.clear();
    s.players[0].hand.push_back(&t.cards[3]);
    s.players[0].hand.push_back(&t.cards[10]);
    s.players[1].hand.clear();
    for (int i : {7, 8, 9}) s.players[1].hand.push_back(&t.cards[std::size_t(i)]);
    s.players[0].active.damage = 20;
    s.players[0].energy = Type::Water;
    t.bench(0, t.cards[5]);
    s.players[1].bench[0].spot = 2;
    return s;
}

int find(const Observation& o, ActionKind kind, int ptr) {
    for (int i = 0; i < Actions; ++i)
        if (o.actionKind[i] == int(kind) + 1 && o.actionPtr[i] == ptr) return i;
    return -1;
}

bool zeroFloats(const float* f, int n) { return std::all_of(f, f + n, [](float x) { return x == 0; }); }
}  // namespace

static void tokensOfAPosition() {
    Table t;
    GameState& s = position(t);
    const auto& actions = t.game.legalActions();
    Observation o;
    CHECK(encode(PlayerView(s, 0, &t.game), actions, o));

    int used[] = {TokGlobal, TokHand, TokHand + 1, TokMine, TokMine + 1, TokOpp, TokOpp + 3};
    for (int i = 0; i < Tokens; ++i) {
        bool on = std::find(std::begin(used), std::end(used), i) != std::end(used);
        CHECK(o.tokenMask[i] == on);
        if (!on) CHECK(o.tokenType[i] == TNone && o.tokenCard[i] == 0 && zeroFloats(o.tokenFloat[i].data(), TokenFloats));
    }
    CHECK(o.tokenType[TokHand + 1] == THand && o.tokenCard[TokHand + 1] == cardIndex(&t.cards[10]));
    CHECK(o.tokenType[TokMine] == TMyActive && o.tokenCard[TokMine] == cardIndex(&t.cards[0]));
    CHECK(o.tokenType[TokMine + 1] == TMyBench && o.tokenCard[TokMine + 1] == cardIndex(&t.cards[5]));
    CHECK(o.tokenType[TokOpp + 3] == TOppBench && o.tokenCard[TokOpp + 3] == cardIndex(&t.cards[2]));

    const float* mine = o.tokenFloat[TokMine].data();
    CHECK(mine[PHpLeft] == 0.4f && mine[PDamage] == 0.2f);
    CHECK(mine[PEnergy + int(Type::Water)] == 0.2f && mine[PCanAttack] == 1 && mine[PIsActive] == 1);
    CHECK(o.tokenFloat[TokOpp][PCanAttack] == 0);  // no Energy on P2's Active

    const float* g = o.tokenFloat[TokGlobal].data();
    CHECK(g[GHand] == 0.2f && g[GHand + 1] == 0.3f);  // hand sizes: 2 mine, 3 theirs
    CHECK(g[GZone + int(Type::Water)] == 1 && g[GNoEnergy] == 0 && g[GMyTurn] == 1 && g[GOwnFirstTurn] == 0);
    CHECK(g[GTurn] == 5.f / rules::TurnCap);

    // The opponent's hand (Mon7..9) appears nowhere.
    for (int c : {7, 8, 9}) {
        int idx = cardIndex(&t.cards[std::size_t(c)]);
        CHECK(std::count(o.tokenCard.begin(), o.tokenCard.end(), idx) == 0);
        CHECK(std::count(o.actionCard.begin(), o.actionCard.end(), idx) == 0);
    }
}

static void actionsPointAtTheirTokens() {
    Table t;
    GameState& s = position(t);
    const auto& actions = t.game.legalActions();
    Observation o;
    encode(PlayerView(s, 0, &t.game), actions, o);
    CHECK(o.legalActions == int(actions.size()));
    for (int i = 0; i < Actions; ++i) {
        bool on = i < int(actions.size());
        CHECK(o.actionMask[i] == on);
        if (!on) CHECK(o.actionKind[i] == 0 && zeroFloats(o.actionFloat[i].data(), ActionFloats));
    }
    int attack = find(o, ActionKind::Attack, TokMine);
    CHECK(attack >= 0 && o.actionFloat[std::size_t(attack)][AAttack] == 1);
    int evolve = find(o, ActionKind::Evolve, TokHand + 1);  // Evo1 onto the Active Mon0
    CHECK(evolve >= 0 && o.actionPtr2[std::size_t(evolve)] == TokMine && o.actionCard[std::size_t(evolve)] == cardIndex(&t.cards[10]));
    CHECK(find(o, ActionKind::PlaceBench, TokHand) >= 0);
    CHECK(find(o, ActionKind::AttachEnergy, TokMine) >= 0 && find(o, ActionKind::AttachEnergy, TokMine + 1) >= 0);
    CHECK(find(o, ActionKind::Retreat, TokMine + 1) >= 0);
    CHECK(find(o, ActionKind::EndTurn, -1) == int(actions.size()) - 1);
}

static void chooseOverPokemonPointsAtThem() {
    Table t;
    GameState& s = position(t);
    // As Game::choose builds them: P2's Active and Benched Mon2.
    std::vector<Action> options = {{ActionKind::Choose, 0, 0, s.players[1].active.uid, nullptr, ChoiceKind::Target},
                                   {ActionKind::Choose, 1, 0, s.players[1].bench[0].uid, nullptr, ChoiceKind::Target},
                                   {ActionKind::Choose, 2, 0, s.players[0].active.uid, nullptr, ChoiceKind::Gain, int8_t(Type::Fire)}};
    Observation o;
    encode(PlayerView(s, 0, &t.game), options, o);
    CHECK(o.actionPtr[0] == TokOpp && o.actionPtr[1] == TokOpp + 3 && o.actionPtr[2] == TokMine);
    CHECK(o.actionFloat[1][AChoice + int(ChoiceKind::Target)] == 1 && o.actionFloat[1][AOption] == 0.1f);
    CHECK(o.actionFloat[2][AChoice + int(ChoiceKind::Gain)] == 1 && o.actionFloat[2][AEnergyType + int(Type::Fire)] == 1);
}

static void setupHidesTheOpponentsBoard() {
    Table t;
    GameState& s = position(t);
    s.turn = 0;
    Observation o;
    encode(PlayerView(s, 1, &t.game), {{ActionKind::EndTurn}}, o);
    CHECK(o.tokenMask[TokOpp] == 0 && o.tokenMask[TokOpp + 3] == 0 && o.tokenMask[TokMine] == 1);
}

static void tooManyActionsAreCut() {
    Table t;
    GameState& s = position(t);
    Observation o;
    std::vector<Action> many(70, Action{ActionKind::EndTurn});
    CHECK(!encode(PlayerView(s, 0, &t.game), many, o));
    CHECK(o.legalActions == 70 && std::count(o.actionMask.begin(), o.actionMask.end(), 1) == Actions);
}

static void staticCardTable() {
    if (!std::filesystem::exists("data/cards.tsv")) return void(std::cout << "skipped the card table: no data/cards.tsv here\n");
    CardDb db = CardDb::load("data/cards.tsv", "data/effects.tsv");
    CardTable t = staticFeatures(db);
    CHECK(t.rows == int(db.all().size()) + 1 && t.cols == CardFixedFeatures + int(t.words.size()));
    CHECK(int(t.values.size()) == t.rows * t.cols && zeroFloats(t.values.data(), t.cols));  // row 0: no card
    CHECK(std::is_sorted(t.words.begin(), t.words.end()) && std::adjacent_find(t.words.begin(), t.words.end()) == t.words.end());
    CHECK(t.words.size() > 100 && std::count(t.words.begin(), t.words.end(), "heal") == 1);

    const CardDef* bulbasaur = db.find("A1-001");  // 70 HP Grass, Weakness Fire, Vine Whip {G}{C} 40
    CHECK(bulbasaur && t.ids[std::size_t(cardIndex(bulbasaur)) - 1] == "A1-001");
    const float* f = &t.values[std::size_t(cardIndex(bulbasaur) * t.cols)];
    CHECK(f[CatPokemon] == 1 && f[CatStage] == 1 && f[CatHp] == 0.7f);
    CHECK(f[CatType + int(Type::Grass)] == 1 && f[CatWeakness + int(Type::Fire)] == 1);
    CHECK(f[CatAttack + int(Type::Grass)] == 0.25f && f[CatAttack + int(Type::Colorless)] == 0.25f && f[CatAttack + 10] == 0.4f);
    CHECK(f[CatAttack + 11] == 1 && f[CatAttack + AttackColumns + 10] == 0);  // plain damage; no second attack

    const CardDef* heals = db.find("A1-003");  // an attack script with (heal self 30)
    int heal = CardFixedFeatures + int(std::find(t.words.begin(), t.words.end(), "heal") - t.words.begin());
    CHECK(heals && t.values[std::size_t(cardIndex(heals) * t.cols + heal)] > 0);
}

int main() {
    tokensOfAPosition();
    actionsPointAtTheirTokens();
    chooseOverPokemonPointsAtThem();
    setupHidesTheOpponentsBoard();
    tooManyActionsAreCut();
    staticCardTable();
    return report("encode");
}
