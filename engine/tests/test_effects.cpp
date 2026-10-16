// Effect checks: the building blocks, modifiers, timed effects, Trainers and abilities, on hand-built
// positions. Scripts are written inline, so this doesn't need data/effects.tsv.
#include "effects/effect.h"
#include "test_support.h"

namespace {
std::vector<std::shared_ptr<const Node>> arena;

const Stmt* stmt(const char* script) {
    ScriptBuilder b(arena);
    return b.stmt(parseScript(script));
}

Modifier mod(const char* script, ModScope scope = ModScope::Self) {
    ScriptBuilder b(arena);
    return b.modifier(parseScript(script), scope);
}

CardDef trainer(TrainerKind kind, const char* name, const char* script = nullptr) {
    CardDef c;
    c.id = std::string("TR-") + name;
    c.name = name;
    c.category = Category::Trainer;
    c.trainer = kind;
    c.supported = true;
    if (script) c.play = stmt(script);
    return c;
}

// Mon0's attack becomes the given script (its damage stays 30).
void attackScript(Table& t, const char* script) { t.cards[0].attacks[0].script = stmt(script); }

int damageOn(Table& t, int player) { return t.game.state().players[player].active.damage; }

int handIndex(const PlayerState& p, const CardDef& card) {
    for (std::size_t i = 0; i < p.hand.size(); ++i)
        if (p.hand[i] == &card) return int(i);
    throw std::logic_error("card not in hand");
}
}  // namespace

static void bonusAndCancel() {
    Table t;
    attackScript(t, "(seq (bonus 20) (hit))");
    t.position();
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 50);

    Table c;
    attackScript(c, "(seq (cancel) (hit) (damage self 10))");  // "this attack does nothing": nothing after it either
    c.position();
    c.play(ActionKind::Attack);
    CHECK(damageOn(c, 1) == 0 && damageOn(c, 0) == 0);
}

static void recoilKnockOutScoresForTheOpponent() {
    Table t;
    attackScript(t, "(seq (hit) (damage self 60))");
    GameState& s = t.position();
    t.bench(0, t.cards[3]);
    t.play(ActionKind::Attack);
    CHECK(s.players[1].points == 1);  // P1's own Pokémon was knocked out
    CHECK(s.players[0].active.card == &t.cards[3]);
}

// Both Active Pokémon knocked out by one attack: the defending player picks a new Active first, then the
// attacking player, and only then comes Pokémon Checkup (App).
static void defenderPicksNewActiveFirst() {
    Table t;
    attackScript(t, "(seq (hit) (damage self 60))");
    GameState& s = t.position();
    t.bench(0, t.cards[3]);
    t.bench(0, t.cards[4]);
    t.bench(1, t.cards[5]);  // two on each Bench, so both players have a real choice
    s.players[1].active.damage = 30;
    std::vector<int> chosen;
    t.policy0.chosen = t.policy1.chosen = &chosen;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].points == 1 && s.players[1].points == 1);
    CHECK((chosen == std::vector<int>{1, 0}));  // P2 (defending), then P1 (attacking)
    CHECK(s.players[0].active.card && s.players[1].active.card && s.current == 1);
}

static void benchDamageHasNoWeakness() {
    Table t;
    t.cards[2].weakness = Type::Water;
    t.cards[1].weakness = Type::Water;
    attackScript(t, "(seq (hit) (damage (each opp bench) 20))");
    GameState& s = t.position();
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 30 + rules::WeaknessBonus);  // the Active: Weakness
    CHECK(s.players[1].bench[0].damage == 20);         // the Bench: none
}

// Official battle FAQ: base → the attacker's effects → Weakness → the defender's effects, floor at 0 only
// at the end. Mon0's 30 against a Mon1 that is weak to Water.
static void weaknessBeforeTheFloor() {
    auto hitFor = [](int attackerMod, int defenderMod) {
        Table t;
        t.cards[1].weakness = Type::Water;
        GameState& s = t.position();
        // On the attacker, like "the Defending Pokémon's attacks do −40"; on the defender, "takes −20".
        s.timed.push_back({{ModKind::DamageDealt, int16_t(attackerMod)}, 0, s.players[0].active.uid, s.turn});
        t.cards[1].passive.push_back({ModKind::DamageTaken, int16_t(defenderMod)});
        t.play(ActionKind::Attack);
        return damageOn(t, 1);
    };
    CHECK(hitFor(-40, 0) == 10);   // 30 − 40 + 20: Weakness still counts after a minus
    CHECK(hitFor(10, -20) == 40);  // 30 + 10 + 20 − 20
    CHECK(hitFor(0, -60) == 0);    // 30 + 20 − 60, floored once at the end
}

// Discarded energy goes to its owner's discard pile, also when it's the opponent's.
static void discardedEnergyGoesToItsOwnersPile() {
    Table t;
    attackScript(t, "(seq (hit) (discard-energy self any 1) (discard-energy opp-active any 1))");
    GameState& s = t.position();
    s.players[1].active.energy[int(Type::Fire)] = 1;
    t.play(ActionKind::Attack);
    CHECK(s.players[0].active.energyCount() == 0 && s.players[0].discardEnergy[int(Type::Water)] == 1);
    CHECK(s.players[1].active.energyCount() == 0 && s.players[1].discardEnergy[int(Type::Fire)] == 1);
}

static void statusesReplaceAndAddUp() {
    Table t;
    attackScript(t, "(seq (hit) (status opp-active poisoned asleep) (status opp-active paralyzed) (status (each opp bench) burned))");
    GameState& s = t.position();
    t.play(ActionKind::Attack);
    CHECK(s.players[1].active.status == (Poisoned | Paralyzed));  // Paralyzed replaced Asleep, Poisoned stays
    CHECK(s.players[1].bench[0].status == 0);                     // only an Active Pokémon gets Special Conditions
}

static void timedDamageReductionLastsOneOpponentTurn() {
    Table t;
    attackScript(t, "(seq (hit) (effect opp-next-turn self (damage-taken -20)))");
    GameState& s = t.position();
    s.players[1].active.energy[int(Type::Water)] = 1;
    t.play(ActionKind::Attack);  // P1 attacks; the effect lasts through P2's turn 6
    CHECK(s.timed.size() == 1 && s.timed[0].lastTurn == 6);
    t.play(ActionKind::Attack);  // P2's plain 30 hits P1's Mon0 for 10
    CHECK(damageOn(t, 0) == 10);
    CHECK(s.timed.empty());      // gone at the end of P2's turn
}

static void preventAllStopsDamageAndEffects() {
    Table t;
    t.cards[1].attacks[0].script = stmt("(seq (hit) (status opp-active poisoned))");
    attackScript(t, "(seq (hit) (effect opp-next-turn self (prevent-all)))");
    GameState& s = t.position();
    s.players[1].active.energy[int(Type::Water)] = 1;
    t.play(ActionKind::Attack);
    t.play(ActionKind::Attack);  // P2's attack does nothing to the protected Mon0
    CHECK(damageOn(t, 0) == 0 && s.players[0].active.status == 0);
}

static void defendingCantRetreat() {
    Table t;
    attackScript(t, "(seq (hit) (effect opp-next-turn defending (cant-retreat)))");
    t.position();
    t.play(ActionKind::Attack);
    CHECK(!t.can(ActionKind::Retreat));  // P2's turn: Mon1 can't retreat, though Mon2 is benched and retreat costs 0
}

static void supporterOncePerTurnItemsAnyNumber() {
    Table t;
    CardDef research = trainer(TrainerKind::Supporter, "Research", "(draw 2)");
    CardDef potion = trainer(TrainerKind::Item, "Potion", "(heal (choose my all damaged) 20)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    p.hand.push_back(&research);
    p.hand.push_back(&research);
    p.hand.push_back(&potion);
    p.hand.push_back(&potion);
    p.active.damage = 20;
    std::size_t deck = p.deck.size();
    t.play(ActionKind::PlayTrainer, 0);  // Research: draw 2
    CHECK(p.deck.size() == deck - 2 && p.hand.size() == 5);
    int supporters = 0;
    for (const Action& a : t.game.legalActions())
        if (a.kind == ActionKind::PlayTrainer && p.hand[a.index] == &research) ++supporters;
    CHECK(supporters == 0);  // one Supporter per turn
    t.play(ActionKind::PlayTrainer, handIndex(p, potion));
    CHECK(p.active.damage == 0);
    p.active.damage = 20;
    t.play(ActionKind::PlayTrainer, handIndex(p, potion));  // a second Potion is fine
    CHECK(p.active.damage == 0);
    CHECK(p.discard.size() == 3);
}

static void toolAndStadiumChangeHp() {
    Table t;
    CardDef cape = trainer(TrainerKind::Tool, "Cape");
    cape.passive.push_back(mod("(hp 20)"));
    CardDef plains = trainer(TrainerKind::Stadium, "Plains");
    plains.passive.push_back(mod("(hp 10 :stage basic)", ModScope::All));
    GameState& s = t.position();
    s.players[0].hand.clear();
    s.players[0].hand.push_back(&cape);
    s.players[0].hand.push_back(&plains);
    t.play(ActionKind::AttachTool, 0, -1);
    CHECK(t.game.maxHp(s.players[0].active, 0) == 80);
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(t.game.maxHp(s.players[0].active, 0) == 90);  // Tool + Stadium
    CHECK(t.game.maxHp(s.players[1].active, 1) == 70);  // the Stadium is for both players
    s.players[0].hand.push_back(&plains);
    CHECK(!t.can(ActionKind::PlayTrainer));  // one Stadium per turn, and not the same name
}

static void passiveAbilities() {
    Table t;
    t.cards[1].passive.push_back(mod("(damage-taken -20)"));       // Mon1 takes less
    t.cards[3].passive.push_back(mod("(damage-dealt 10 :scope my-all :type water)"));  // Mon3 boosts P1's Water Pokémon
    t.position();
    t.bench(0, t.cards[3]);
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 30 + 10 - 20);
}

static void needsNamedPokemonInPlay() {
    Table t;
    t.cards[1].passive.push_back(mod("(damage-taken -20 :needs \"Mon9\")"));
    GameState& s = t.position();
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 30);  // no Mon9 in play: no reduction
    s.players[1].active.damage = 0;
    t.bench(1, t.cards[9]);
    s.current = 0;
    s.players[0].active.energy[int(Type::Water)] = 1;
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 10);
}

static void activatedAbilityOncePerTurn() {
    Table t;
    t.cards[0].activated = stmt("(heal (each my all) 20)");
    GameState& s = t.position();
    s.players[0].active.damage = 40;
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(s.players[0].active.damage == 20);
    CHECK(!t.can(ActionKind::UseAbility));
}

// "Once during your turn" belongs to the card (App): after evolving, the new card may use its own ability.
static void evolvedCardUsesItsOwnAbility() {
    Table t;
    t.cards[0].activated = stmt("(heal (each my all) 10)");
    t.cards[10].activated = stmt("(heal (each my all) 10)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.active.damage = 40;
    p.hand.clear();
    p.hand.push_back(&t.cards[10]);  // Evo1, from Mon0
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(!t.can(ActionKind::UseAbility));
    t.play(ActionKind::Evolve, 0, -1);
    CHECK(t.can(ActionKind::UseAbility));
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(p.active.damage == 20);
    CHECK(!t.can(ActionKind::UseAbility));
}

static void reflectHitsTheAttacker() {
    Table t;
    t.cards[1].passive.push_back(mod("(reflect 20 :active)"));
    t.position();
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 30 && damageOn(t, 0) == 20);
}

static void switchInClearsConditions() {
    Table t;
    attackScript(t, "(seq (hit) (switch-in (owner-chooses opp bench)))");
    GameState& s = t.position();
    s.players[1].active.status = Poisoned;
    t.play(ActionKind::Attack);
    CHECK(s.players[1].active.card == &t.cards[2]);
    CHECK(s.players[1].bench[0].card == &t.cards[1] && s.players[1].bench[0].status == 0);
    CHECK(s.players[1].bench[0].damage == 30);  // the hit came first
}

static void poisonKnockOutAtCheckup() {
    Table t;
    GameState& s = t.position();
    s.players[1].active.damage = 55;
    s.players[1].active.status = Poisoned;
    t.play(ActionKind::EndTurn);
    CHECK(s.players[0].points == 1);
    CHECK(s.players[1].active.card == &t.cards[2]);
}

static void attackCostChanges() {
    Table t;
    GameState& s = t.position();
    TimedEffect te;
    te.mod = mod("(attack-cost 1)");
    te.uid = s.players[0].active.uid;
    te.lastTurn = 9;
    s.timed.push_back(te);
    CHECK(!t.can(ActionKind::Attack));  // Water + 1 Colorless now
    s.players[0].active.energy[int(Type::Water)] = 2;
    CHECK(t.can(ActionKind::Attack));
}

static void rareCandyAndOnEvolve() {
    Table t;
    CardDef candy = trainer(TrainerKind::Item, "Candy", "(rare-candy)");
    t.cards[11].evolvesFromBasic = "Mon0";
    t.cards[11].onEvolve = stmt("(draw 1)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    p.hand.push_back(&candy);
    p.hand.push_back(&t.cards[11]);  // Evo2, the Stage 2
    std::size_t deck = p.deck.size();
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(p.active.card == &t.cards[11] && p.active.below.size() == 1);
    CHECK(p.deck.size() == deck - 1);  // the on-evolve draw
}

static void confusedAttacksSometimesFail() {
    int hits = 0;
    for (int i = 0; i < 400; ++i) {
        Table t;
        GameState& s = t.position();
        s.players[0].active.status = Confused;
        t.play(ActionKind::Attack);
        hits += damageOn(t, 1) > 0;
    }
    CHECK(hits > 120 && hits < 280);  // about half
}

// A Trainer needs something to do before it can be played (App): no Potion without damage.
static void trainersNeedSomethingToDo() {
    Table t;
    CardDef potion = trainer(TrainerKind::Item, "Potion", "(heal (choose my all damaged) 20)");
    CardDef candy = trainer(TrainerKind::Item, "Candy", "(rare-candy)");
    CardDef generator = trainer(TrainerKind::Item, "Generator", "(seq (flip 1) (if heads (attach-energy (choose my bench) water 1)))");
    CardDef sabrina = trainer(TrainerKind::Supporter, "Sabrina", "(switch-in (owner-chooses opp bench))");
    CardDef research = trainer(TrainerKind::Supporter, "Research", "(draw 2)");
    CardDef giovanni = trainer(TrainerKind::Supporter, "Giovanni", "(effect this-turn me (damage-dealt 10 :scope my-all))");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    for (const CardDef* c : {&potion, &candy, &generator, &sabrina, &research, &giovanni}) p.hand.push_back(c);
    auto offered = [&](const CardDef& card) {
        for (const Action& a : t.game.legalActions())
            if (a.kind == ActionKind::PlayTrainer && p.hand[a.index] == &card) return true;
        return false;
    };
    CHECK(!offered(potion));     // nothing to heal
    CHECK(!offered(candy));      // no Stage 2 in the hand
    CHECK(!offered(generator));  // no Benched Pokémon to take the energy, whatever the coin says
    CHECK(offered(sabrina));     // P2 has a Benched Pokémon
    CHECK(offered(research));
    CHECK(offered(giovanni));  // an effect for this turn always does something
    p.active.damage = 10;
    t.bench(0, t.cards[3]);
    s.players[1].bench.clear();
    p.deck.clear();
    CHECK(offered(potion));
    CHECK(offered(generator));
    CHECK(!offered(sabrina));
    CHECK(!offered(research));  // nothing left to draw
}

static void moveEnergyAndSearch() {
    Table t;
    CardDef dawn = trainer(TrainerKind::Supporter, "Dawn", "(move-energy (choose my bench) my-active any 1)");
    CardDef ball = trainer(TrainerKind::Item, "Ball", "(search 1 hand basic)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    t.bench(0, t.cards[3]).energy[int(Type::Water)] = 2;
    p.hand.clear();
    p.hand.push_back(&dawn);
    p.hand.push_back(&ball);
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(p.active.energy[int(Type::Water)] == 2 && p.bench[0].energy[int(Type::Water)] == 1);
    std::size_t deck = p.deck.size();
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(p.deck.size() == deck - 1 && p.hand.size() == 1 && p.hand[0]->isBasic());
}

// "This attack does 20 damage for each Energy attached to all of your opponent's Pokémon" (Espeon):
// the energy on the Bench counts too.
static void energyOnAllPokemon() {
    Table t;
    attackScript(t, "(seq (set-damage (* (energy (each opp all)) 10)) (hit))");
    GameState& s = t.position();
    s.players[1].active.energy[int(Type::Water)] = 1;
    s.players[1].bench[0].energy[int(Type::Water)] = 2;
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 30);
}

// Lt. Surge: "Move all {L} Energy from your Benched Pokémon to …": from every Benched Pokémon.
static void moveEnergyFromTheWholeBench() {
    Table t;
    CardDef surge = trainer(TrainerKind::Supporter, "Surge", "(move-energy (each my bench) (choose my active (name \"Mon0\")) lightning all)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    t.bench(0, t.cards[3]).energy[int(Type::Lightning)] = 1;
    t.bench(0, t.cards[4]).energy[int(Type::Lightning)] = 2;
    p.bench[1].energy[int(Type::Water)] = 1;
    p.hand.clear();
    p.hand.push_back(&surge);
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(p.active.energy[int(Type::Lightning)] == 3);
    CHECK(p.bench[0].energyCount() == 0 && p.bench[1].energyCount() == 1);  // only Lightning moves
}

// Whitney: "it recovers from being Asleep, Paralyzed, and Confused": Poisoned stays.
static void cureOnlyTheNamedConditions() {
    Table t;
    CardDef whitney = trainer(TrainerKind::Supporter, "Whitney", "(cure my-active asleep paralyzed confused)");
    GameState& s = t.position();
    PlayerState& p = s.players[0];
    p.hand.clear();
    p.hand.push_back(&whitney);
    p.active.status = Poisoned;
    CHECK(!t.can(ActionKind::PlayTrainer));  // nothing it can cure
    p.active.status = Poisoned | Asleep;
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(p.active.status == Poisoned);
}

// Replacing a Stadium that gave +HP knocks out at once whatever now has too much damage.
static void replacedStadiumKnocksOut() {
    Table t;
    CardDef plains = trainer(TrainerKind::Stadium, "Plains");
    plains.passive.push_back(mod("(hp 20 :stage basic)", ModScope::All));
    CardDef area = trainer(TrainerKind::Stadium, "Area");
    GameState& s = t.position();
    s.stadium = &plains;
    s.stadiumOwner = 1;
    s.players[1].active.damage = 70;  // 60 HP + 20: still in play
    s.players[0].hand.clear();
    s.players[0].hand.push_back(&area);
    t.play(ActionKind::PlayTrainer, 0);
    CHECK(s.players[0].points == 1);
    CHECK(s.players[1].active.card == &t.cards[2]);  // P2's Benched Mon2 moved up before P1 goes on
    CHECK(s.players[1].discard.size() == 2);         // Mon1 and the old Stadium
}

// Golisopod's "if this Pokémon moved from your Bench to the Active Spot this turn" (Remco): yes when it
// evolved on the Bench and then moved up, or was benched this turn and then moved up; no when it moved up
// as Wimpod and evolved after.
static void movedToActiveBelongsToTheCard() {
    const Cond* moved = [] {
        ScriptBuilder b(arena);
        return b.cond(parseScript("(moved-to-active self)"));
    }();
    auto test = [&](Table& t) {
        EffectCtx ctx{t.game, 0};
        ctx.self = t.game.state().players[0].active.uid;
        return moved->test(ctx);
    };
    for (bool evolveFirst : {true, false}) {
        Table t;
        GameState& s = t.position();
        PlayerState& p = s.players[0];
        t.bench(0, t.cards[0]);  // a second Mon0, on the Bench since setup
        p.hand.clear();
        p.hand.push_back(&t.cards[10]);  // Evo1
        if (evolveFirst) {
            t.play(ActionKind::Evolve, 0, 0);
            t.play(ActionKind::Retreat, 0);
        } else {
            t.play(ActionKind::Retreat, 0);
            t.play(ActionKind::Evolve, 0, -1);
        }
        CHECK(p.active.card == &t.cards[10]);
        CHECK(test(t) == evolveFirst);
    }
    Table t;
    GameState& s = t.position();
    s.players[0].hand.clear();
    s.players[0].hand.push_back(&t.cards[3]);
    t.play(ActionKind::PlaceBench, 0);  // benched this turn
    CHECK(!test(t));
    t.play(ActionKind::Retreat, 0);
    CHECK(test(t));
}

// An ability with nothing to do can't be used (Remco), the same rule as for Trainers.
static void abilitiesNeedSomethingToDo() {
    Table t;
    t.cards[0].activated = stmt("(heal (each my all damaged) 20)");                         // Butterfree
    t.cards[3].activated = stmt("(attach-energy (choose my active (type psychic)) psychic 1)");  // Gardevoir
    GameState& s = t.position();
    t.bench(0, t.cards[3]);
    CHECK(!t.can(ActionKind::UseAbility));  // nothing damaged, and a Water Active
    s.players[0].bench[0].damage = 10;
    CHECK(t.can(ActionKind::UseAbility));
    t.play(ActionKind::UseAbility, 0, -1);
    CHECK(s.players[0].bench[0].damage == 0);
    CHECK(!t.can(ActionKind::UseAbility));
}

// Wugtrio ex, "chosen at random 3 times … do 50 damage to it" (Remco): the hits on one Pokémon add up
// first and are dealt as one, so all three on a Weak Active is 150 + 20, and Giovanni's +10 counts once.
static void randomHitsAddUpBeforeWeakness() {
    Table t;
    t.cards[1].hp = 300;
    t.cards[1].weakness = Type::Water;
    attackScript(t, "(seq (hit) (hits 3 (random opp active) 50))");
    t.cards[0].attacks[0].damage = 0;
    GameState& s = t.position();
    TimedEffect giovanni;
    giovanni.mod = mod("(damage-dealt 10 :scope my-all)");
    giovanni.player = 0;
    giovanni.lastTurn = s.turn;
    s.timed.push_back(giovanni);
    t.play(ActionKind::Attack);
    CHECK(damageOn(t, 1) == 150 + 10 + 20);
}

// A word with too many or too few arguments doesn't load, so nothing is silently ignored.
static void wrongArgumentCountsDontLoad() {
    for (const char* bad : {"(heal self 30 40)", "(heal self)", "(hit 5)", "(effect opp-next-turn self)"}) {
        bool failed = false;
        try {
            stmt(bad);
        } catch (const ScriptError&) {
            failed = true;
        }
        CHECK(failed);
    }
    stmt("(cure self asleep paralyzed)");  // a word that takes a list still loads
}

// The pieces coverage batch 1 added: card filters, the has-energy filter, (points-needed), (healed), :from-ex.
static void coverageBatchOneBlocks() {
    {   // Serena, Lisia, Team Rocket's Researcher, Cabbie: what a search may take
        Table t;
        t.cards[5].ex = t.cards[5].mega = true;
        t.cards[6].hp = 40;
        CardDef stadium = trainer(TrainerKind::Stadium, "Plaza");
        GameState& s = t.position();
        PlayerState& p = s.players[0];
        auto searchGets = [&](const char* script, std::vector<const CardDef*> deck) {
            p.deck.clear();
            p.hand.clear();
            for (const CardDef* c : deck) p.deck.push_back(c);
            EffectCtx ctx{t.game, 0};
            stmt(script)->run(ctx);
            return p.hand.size() == 1 ? p.hand[0] : nullptr;
        };
        CHECK(searchGets("(search 1 hand mega)", {&t.cards[4], &t.cards[5]}) == &t.cards[5]);
        CHECK(searchGets("(search 1 hand basic (max-hp 50))", {&t.cards[6], &t.cards[7]}) == &t.cards[6]);
        CHECK(searchGets("(search 1 hand pokemon (name-has \"on9\"))", {&t.cards[8], &t.cards[9]}) == &t.cards[9]);
        CHECK(searchGets("(search 1 hand stadium)", {&t.cards[8], &stadium}) == &stadium);
    }
    {   // Irida: only Pokémon with Water energy; Diantha: heal, and then discard because it healed
        Table t;
        GameState& s = t.position();
        PlayerState& p = s.players[0];
        t.bench(0, t.cards[3]).damage = 30;
        p.active.damage = 50;
        EffectCtx ctx{t.game, 0};
        stmt("(heal (each my all (has-energy 1 water) damaged) 40)")->run(ctx);
        CHECK(p.active.damage == 10 && p.bench[0].damage == 30);
        p.active.energy[int(Type::Water)] = 2;
        EffectCtx ctx2{t.game, 0};
        stmt("(pick (choose my all (has-energy 2 water) damaged) (seq (heal picked 90) (if (healed) (discard-energy picked water 2))))")->run(ctx2);
        CHECK(p.active.damage == 0 && p.active.energyCount() == 0);
    }
    {   // Mars: draw as many as the opponent still needs points
        Table t;
        GameState& s = t.position();
        s.players[1].points = 1;
        EffectCtx ctx{t.game, 0};
        stmt("(shuffle-hand-draw opp (points-needed opp))")->run(ctx);
        CHECK(s.players[1].hand.size() == 2);
    }
    {   // Cheren: −100 only against a Pokémon ex
        Table t;
        GameState& s = t.position();
        TimedEffect cheren;
        cheren.mod = mod("(damage-taken -100 :scope my-all :from-ex)", ModScope::MyAll);
        cheren.player = 0;
        cheren.lastTurn = s.turn + 1;
        s.timed.push_back(cheren);
        uint16_t attacker = s.players[1].active.uid, target = s.players[0].active.uid;
        CHECK(t.game.dealAttackDamage(1, attacker, target, 30) == 30);
        t.cards[1].ex = true;
        CHECK(t.game.dealAttackDamage(1, attacker, target, 30) == 0);
    }
}

int main() {
    bonusAndCancel();
    recoilKnockOutScoresForTheOpponent();
    defenderPicksNewActiveFirst();
    benchDamageHasNoWeakness();
    weaknessBeforeTheFloor();
    discardedEnergyGoesToItsOwnersPile();
    statusesReplaceAndAddUp();
    timedDamageReductionLastsOneOpponentTurn();
    preventAllStopsDamageAndEffects();
    defendingCantRetreat();
    supporterOncePerTurnItemsAnyNumber();
    toolAndStadiumChangeHp();
    passiveAbilities();
    needsNamedPokemonInPlay();
    activatedAbilityOncePerTurn();
    evolvedCardUsesItsOwnAbility();
    reflectHitsTheAttacker();
    switchInClearsConditions();
    poisonKnockOutAtCheckup();
    attackCostChanges();
    rareCandyAndOnEvolve();
    confusedAttacksSometimesFail();
    trainersNeedSomethingToDo();
    moveEnergyAndSearch();
    energyOnAllPokemon();
    moveEnergyFromTheWholeBench();
    cureOnlyTheNamedConditions();
    replacedStadiumKnocksOut();
    movedToActiveBelongsToTheCard();
    abilitiesNeedSomethingToDo();
    randomHitsAddUpBeforeWeakness();
    wrongArgumentCountsDontLoad();
    coverageBatchOneBlocks();
    return report("effects");
}
