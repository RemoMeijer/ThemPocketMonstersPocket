// Cards and the board: drawing, searching, hands and decks, switching, Tools, Rare Candy, timed effects.
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "effects/effect.h"
#include "game/game.h"

namespace {
int sidePlayer(const EffectCtx& ctx, bool opp) { return opp ? 1 - ctx.player : ctx.player; }

// Which cards in a deck qualify: pokemon, basic, stage1, stage2, mega, trainer, supporter, item, tool, stadium,
// (type T…), (name "A" …), (name-has "A"), (except "A"), (evolves-from "A"), (max-hp N), (min-retreat N).
struct CardFilter {
    bool pokemon = false, basic = false, trainer = false, mega = false;
    uint8_t stages = 0;
    TrainerKind kind = TrainerKind::None;
    uint16_t types = 0;
    std::vector<std::string> names;
    std::string nameHas, except, evolvesFrom, attack;  // attack: has an attack with that name (Puppy Pile)
    int maxHp = 0;
    int minRetreat = 0;  // (min-retreat 3): a Retreat Cost of 3 or more (Golurk)
    uint8_t traits = 0;  // (trait ancient)

    // Outside play a Fossil is an Item card, not a Pokémon: Poké Ball can't find it, Pachirisu counts it as an Item
    // (ruling). In play it's a Basic Pokémon.
    bool matches(const CardDef* c, bool inPlay = false) const {
        bool fossilItem = !inPlay && c->isFossil();
        bool item = c->isTrainer(TrainerKind::Item) || fossilItem, mon = c->isPokemon() && !fossilItem;
        return (!pokemon || mon) && (!basic || (mon && c->isBasic())) &&
               (!stages || (mon && (stages & stageBit(c->stage)))) && (!mega || c->mega) &&
               (!trainer || c->category == Category::Trainer || item) &&
               (kind == TrainerKind::None || c->isTrainer(kind) || (kind == TrainerKind::Item && item)) &&
               (!types || (mon && c->hasType(types))) &&
               (names.empty() || std::find(names.begin(), names.end(), c->name) != names.end()) &&
               (nameHas.empty() || c->name.find(nameHas) != std::string::npos) && (except.empty() || c->name != except) &&
               (evolvesFrom.empty() || c->evolvesFrom == evolvesFrom) && (!maxHp || (mon && c->hp <= maxHp)) &&
               (!minRetreat || (mon && c->retreat >= minRetreat)) &&
               (attack.empty() || std::any_of(c->attacks.begin(), c->attacks.end(), [&](const AttackDef& a) { return a.name == attack; })) &&
               (!traits || (traits & c->traits));
    }
};

CardFilter parseCardFilter(const SExpr& e, std::size_t from) {
    CardFilter f;
    for (std::size_t i = from; i < argCount(e); ++i) {
        const SExpr& a = arg(e, i);
        const std::string& w = a.list ? a.head() : a.atom;
        if (w == "pokemon") f.pokemon = true;
        else if (w == "basic") f.basic = true;
        else if (w == "stage1") f.stages |= stageBit(Stage::Stage1);
        else if (w == "stage2") f.stages |= stageBit(Stage::Stage2);
        else if (w == "trainer") f.trainer = true;
        else if (w == "supporter") f.kind = TrainerKind::Supporter;
        else if (w == "item") f.kind = TrainerKind::Item;
        else if (w == "tool") f.kind = TrainerKind::Tool;
        else if (w == "stadium") f.kind = TrainerKind::Stadium;
        else if (w == "mega") f.mega = true, f.pokemon = true;
        else if (w == "max-hp" && a.list) f.maxHp = intArg(arg(a, 0)), f.pokemon = true;
        else if (w == "min-retreat" && a.list) f.minRetreat = intArg(arg(a, 0)), f.pokemon = true;
        else if (w == "name-has" && a.list) f.nameHas = arg(a, 0).atom;
        else if (w == "except" && a.list) f.except = arg(a, 0).atom;
        else if (w == "type" && a.list) f.types = typeMask(a, 0), f.pokemon = true;
        else if (w == "name" && a.list)
            for (std::size_t n = 0; n < argCount(a); ++n) f.names.push_back(arg(a, n).atom);
        else if (w == "evolves-from" && a.list) f.evolvesFrom = arg(a, 0).atom;
        else if (w == "has-attack" && a.list) f.attack = arg(a, 0).atom, f.pokemon = true;
        else if (w == "trait" && a.list) f.traits = traitFromName(arg(a, 0).atom), f.pokemon = true;
        else scriptFail("unknown card filter '" + a.text() + "'", e);
    }
    return f;
}

class Draw : public Stmt {  // (draw N [my|opp])
public:
    Draw(const Num* n, bool opp) : n_(n), opp_(opp) {}
    void run(EffectCtx& ctx) const override {
        for (int i = n_->eval(ctx); i > 0; --i) ctx.game.draw(sidePlayer(ctx, opp_));
    }
    bool possible(EffectCtx& ctx) const override {
        return n_->eval(ctx) > 0 && !ctx.game.state().players[sidePlayer(ctx, opp_)].deck.empty();
    }

private:
    const Num* n_;
    bool opp_;
};

// (search N hand|bench FILTER…): N random matching cards from your deck into your hand, or (Basic
// Pokémon only) onto your Bench.
class Search : public Stmt {
public:
    Search(const Num* n, bool bench, CardFilter f) : n_(n), bench_(bench), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        for (int i = n_->eval(ctx); i > 0; --i) {
            std::vector<std::size_t> hits;
            for (std::size_t k = 0; k < p.deck.size(); ++k)
                // a card that names them may put Stage 1 cards straight onto the Bench (Silcoon, ruling)
                if (f_.matches(p.deck[k]) && (!bench_ || p.deck[k]->isBasic() || !f_.names.empty())) hits.push_back(k);
            if (hits.empty() || (bench_ ? p.bench.full() : p.hand.size() >= std::size_t(rules::HandLimit))) return;
            std::size_t k = hits[std::uniform_int_distribution<std::size_t>(0, hits.size() - 1)(ctx.game.rng())];
            const CardDef* card = p.deck.take(k);
            if (bench_) {
                ctx.game.addToBench(ctx.player, card);
                ctx.game.say("  ", card->name, " from the deck onto the Bench");
            } else {
                p.hand.push_back(card);
                ctx.game.say("  ", card->name, " from the deck into the hand");
            }
        }
    }
    bool possible(EffectCtx& ctx) const override {  // a matching card in the deck (playing the card made room)
        const PlayerState& p = ctx.game.state().players[ctx.player];
        if (bench_ && p.bench.full()) return false;
        for (const CardDef* c : p.deck)
            if (f_.matches(c) && (!bench_ || c->isBasic() || !f_.names.empty())) return true;
        return false;
    }

private:
    const Num* n_;
    bool bench_;
    CardFilter f_;
};

// Hands and decks: (mill SIDE N), (discard-hand-random SIDE N), (hand-to-deck-random SIDE N),
// (shuffle-hand-draw SIDE N|same) with SIDE my, opp or both. (hand-to-deck-random any N): "a card from among both
// players' hands is chosen at random" and shuffled into its owner's deck (Prank Spinner).
class Cards : public Stmt {
public:
    enum What { Mill, DiscardHandRandom, HandToDeckRandom, ShuffleHandDraw };
    Cards(What what, int side, const Num* n) : what_(what), side_(side), n_(n) {}
    bool possible(EffectCtx& ctx) const override {  // both hands (Prank Spinner): a card besides the one being played
        if (side_ != 3) return true;
        const auto& ps = ctx.game.state().players;
        return int(ps[0].hand.size() + ps[1].hand.size()) - ctx.trainerInHand > 0;
    }
    void run(EffectCtx& ctx) const override {
        if (side_ == 3) return fromBothHands(ctx);
        for (int k = 0; k < 2; ++k) {
            if ((side_ == 0 && k == 1) || (side_ == 1 && k == 0)) continue;
            int player = k == 0 ? ctx.player : 1 - ctx.player;
            PlayerState& p = ctx.game.state().players[player];
            auto& rng = ctx.game.rng();
            auto randomHand = [&] { return std::uniform_int_distribution<std::size_t>(0, p.hand.size() - 1)(rng); };
            int n = n_ ? n_->eval(ctx) : int(p.hand.size());  // no N: "draws that many cards"
            switch (what_) {
                case Mill:
                    for (; n > 0 && !p.deck.empty(); --n) p.discard.push_back(p.deck.pop_back());
                    break;
                case DiscardHandRandom:
                    for (; n > 0 && !p.hand.empty(); --n) p.discard.push_back(p.hand.take(randomHand()));
                    break;
                case HandToDeckRandom:
                    for (; n > 0 && !p.hand.empty(); --n) p.deck.push_back(p.hand.take(randomHand()));
                    std::shuffle(p.deck.begin(), p.deck.end(), rng);
                    break;
                case ShuffleHandDraw:
                    while (!p.hand.empty()) p.deck.push_back(p.hand.pop_back());
                    std::shuffle(p.deck.begin(), p.deck.end(), rng);
                    for (; n > 0; --n) ctx.game.draw(player);
                    break;
            }
            ctx.game.say("  P", player + 1, ": hand ", p.hand.size(), ", deck ", p.deck.size());
        }
    }

private:
    void fromBothHands(EffectCtx& ctx) const {
        auto& players = ctx.game.state().players;
        for (int n = n_->eval(ctx); n > 0; --n) {
            std::size_t total = players[0].hand.size() + players[1].hand.size();
            if (!total) return;
            std::size_t k = std::uniform_int_distribution<std::size_t>(0, total - 1)(ctx.game.rng());
            PlayerState& p = players[k < players[0].hand.size() ? 0 : 1];
            const CardDef* c = p.hand.take(k < players[0].hand.size() ? k : k - players[0].hand.size());
            p.deck.push_back(c);
            std::shuffle(p.deck.begin(), p.deck.end(), ctx.game.rng());
            ctx.game.say("  ", c->name, " is shuffled into its owner's deck");
        }
    }

    What what_;
    int side_;  // 0 my, 1 opp, 2 both, 3 any (one pool)
    const Num* n_;
};

// (switch-in TARGET): the chosen benched Pokémon swaps places with its owner's Active Pokémon.
class SwitchIn : public Stmt {
public:
    explicit SwitchIn(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids chosen;
        t_->select(ctx, chosen);
        if (chosen.empty()) return;
        int owner = 0;
        PokemonSlot* in = ctx.game.find(chosen[0], &owner);
        PlayerState& p = ctx.game.state().players[owner];
        if (!in || in == &p.active || !p.active.card) return;
        if (owner != ctx.player && !affects(ctx, p.active.uid)) return;
        std::swap(p.active, *in);
        in->spot = p.active.spot;  // it takes the Bench spot of the one coming up (App)
        ctx.game.clearConditions(*in);  // the one that went to the Bench
        ctx.game.movedToActive(owner);
        ctx.game.say("  P", owner + 1, "'s ", p.active.card->name, " is now Active, ", in->card->name, " goes to the Bench");
    }
    bool possible(EffectCtx& ctx) const override {  // a Benched Pokémon to switch in
        const auto& players = ctx.game.state().players;
        return anyCandidate(ctx, t_, [&](const PokemonSlot& p) { return &p != &players[0].active && &p != &players[1].active; });
    }

private:
    const Target* t_;
};

// (to-hand TARGET), (to-deck TARGET): the Pokémon, its evolutions and Tool back into its owner's hand or deck (Koga,
// Eldegoss). Not on an opponent's Pokémon whose attack effects are prevented.
class TakeBack : public Stmt {
public:
    TakeBack(const Target* t, bool toDeck) : t_(t), toDeck_(toDeck) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets)
            if (affects(ctx, uid)) ctx.game.takeBack(uid, toDeck_);
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot&) { return true; });
    }

private:
    const Target* t_;
    bool toDeck_;
};

// (tools-to-hand TARGET): their Tools back into their owners' hands (Elesa); (tools-to-deck TARGET): shuffled into
// their owners' decks (Hoopa).
class ToolsToHand : public Stmt {
public:
    explicit ToolsToHand(const Target* t, bool toDeck = false) : t_(t), toDeck_(toDeck) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || p->tools.empty() || !affects(ctx, uid)) continue;
            auto& deck = ctx.game.state().players[owner].deck;
            while (!p->tools.empty()) {
                const CardDef* tool = p->tools.pop_back();
                ctx.game.say("  ", tool->name, toDeck_ ? " is shuffled into the deck" : " goes back to the hand");
                toDeck_ ? deck.push_back(tool) : ctx.game.toHand(owner, tool);
            }
            if (toDeck_) std::shuffle(deck.begin(), deck.end(), ctx.game.rng());
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot& p) { return !p.tools.empty(); });
    }

private:
    const Target* t_;
    bool toDeck_;
};

class DiscardTools : public Stmt {  // (discard-tools TARGET)
public:
    explicit DiscardTools(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || p->tools.empty() || !affects(ctx, uid)) continue;
            while (!p->tools.empty()) {
                const CardDef* tool = p->tools.pop_back();
                ctx.game.say("  ", tool->name, " is discarded");
                ctx.game.state().players[owner].discard.push_back(tool);
            }
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot& p) { return !p.tools.empty(); });
    }

private:
    const Target* t_;
};

// (rare-candy): a Basic Pokémon in play evolves straight into a Stage 2 from the hand, skipping the
// Stage 1. Not on your first turn, and not on a Basic put into play this turn.
class RareCandy : public Stmt {
public:
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        auto options = optionsFor(ctx);
        if (options.empty()) return;
        auto [t, h] = options[ctx.game.choose(ctx.player, int(options.size()))];
        PokemonSlot& target = ctx.game.slot(ctx.player, t);
        ctx.game.evolve(target, p.hand.take(h));
        ctx.game.say("  ", target.below.back()->name, " evolves straight into ", target.card->name);
        if (target.card->onEvolve) {
            EffectCtx inner{ctx.game, ctx.player};
            inner.self = target.uid;
            target.card->onEvolve->run(inner);
        }
    }
    bool possible(EffectCtx& ctx) const override { return !optionsFor(ctx).empty(); }

private:
    // Every (target, hand position) it could evolve: not on your first turn, nor on a Basic put down this turn.
    static std::vector<std::pair<int, std::size_t>> optionsFor(EffectCtx& ctx) {
        const GameState& s = ctx.game.state();
        const PlayerState& p = s.players[ctx.player];
        std::vector<std::pair<int, std::size_t>> options;
        if (ctx.game.ownFirstTurn(ctx.player)) return options;
        for (int t = -1; t < int(p.bench.size()); ++t) {
            const PokemonSlot& basic = ctx.game.slot(ctx.player, t);
            if (!basic.card || basic.card->stage != Stage::Basic || basic.enteredTurn >= s.turn) continue;
            for (std::size_t h = 0; h < p.hand.size(); ++h)
                if (p.hand[h]->stage == Stage::Stage2 && p.hand[h]->evolvesFromBasic == basic.card->name)
                    options.push_back({t, h});
        }
        return options;
    }
};

// (effect DURATION WHO MODIFIER): a modifier for a while.
//   DURATION: this-turn, opp-next-turn (until the end of the opponent's next turn), my-next-turn.
//   WHO: self, defending, my-active, opp-active (that Pokémon), or me, opp (the whole player).
//   (cant-use-attack -1): "1 of … attacks is chosen at random" when it starts (Quagsire).
class Timed : public Stmt {
public:
    Timed(int turns, std::string who, Modifier mod) : turns_(turns), who_(std::move(who)), mod_(mod) {}
    void run(EffectCtx& ctx) const override {
        GameState& s = ctx.game.state();
        TimedEffect t;
        t.mod = mod_;
        t.lastTurn = s.turn + turns_;
        if (who_ == "me" || who_ == "opp") {
            t.player = int8_t(who_ == "me" ? ctx.player : 1 - ctx.player);
        } else {
            uint16_t uid = who_ == "self"        ? ctx.self
                           : who_ == "defending" ? ctx.defending
                           : who_ == "my-active" ? s.players[ctx.player].active.uid
                                                 : s.players[1 - ctx.player].active.uid;
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p) return;
            if (owner != ctx.player && !affects(ctx, uid)) return;
            // A copied "this Pokémon can't use X" doesn't bind the copier: it used its copying attack, not X (ruling).
            if (t.mod.kind == ModKind::CantUseAttack && ctx.copyDepth && uid == ctx.self) return;
            t.uid = uid;
            t.player = int8_t(owner);
            if (t.mod.kind == ModKind::CantUseAttack && t.mod.amount < 0) {
                std::size_t n = ctx.game.attacksOf(*p, owner).size();
                if (!n) return;
                t.mod.amount = int16_t(std::uniform_int_distribution<std::size_t>(0, n - 1)(ctx.game.rng()));
            }
        }
        if (s.timed.full()) s.timed.take(0);  // shortcut: drops the oldest effect past 16 at once; unseen so far
        s.timed.push_back(t);
    }

private:
    int turns_;
    std::string who_;
    Modifier mod_;
};

// (from-discard random|choose N SIDE hand|bench FILTER…): N matching cards from SIDE's discard pile, picked at
// random or by the player, into your hand, or (Basic Pokémon only) onto SIDE's Bench (Pokémon Flute).
// A card taken from the opponent becomes yours (assumed, DESIGN.md §10).
class FromDiscard : public Stmt {
public:
    FromDiscard(bool random, const Num* n, bool opp, bool bench, CardFilter f)
        : random_(random), n_(n), opp_(opp), bench_(bench), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& from = ctx.game.state().players[sidePlayer(ctx, opp_)];
        PlayerState& mine = ctx.game.state().players[ctx.player];
        for (int i = n_->eval(ctx); i > 0; --i) {
            if (bench_ ? from.bench.full() : mine.hand.size() >= std::size_t(rules::HandLimit)) return;
            std::vector<std::size_t> hits = matches(from);
            if (hits.empty()) return;
            std::size_t pick = random_ ? std::uniform_int_distribution<std::size_t>(0, hits.size() - 1)(ctx.game.rng())
                                       : std::size_t(ctx.game.choose(ctx.player, int(hits.size())));
            const CardDef* card = from.discard.take(hits[pick]);
            if (bench_) {
                ctx.game.addToBench(sidePlayer(ctx, opp_), card);
                ctx.game.say("  ", card->name, " from the discard pile onto the Bench");
            } else {
                mine.hand.push_back(card);
                ctx.game.say("  ", card->name, " from the discard pile into the hand");
            }
        }
    }
    bool possible(EffectCtx& ctx) const override {
        const PlayerState& from = ctx.game.state().players[sidePlayer(ctx, opp_)];
        return !(bench_ && from.bench.full()) && !matches(from).empty();
    }

private:
    std::vector<std::size_t> matches(const PlayerState& p) const {
        std::vector<std::size_t> hits;
        for (std::size_t k = 0; k < p.discard.size(); ++k)
            if (f_.matches(p.discard[k]) && (!bench_ || p.discard[k]->isBasic())) hits.push_back(k);
        return hits;
    }
    bool random_;
    const Num* n_;
    bool opp_, bench_;
    CardFilter f_;
};

class DiscardCount : public Num {  // (discard-count SIDE FILTER…), (deck-count SIDE FILTER…): matching cards in that pile
public:
    DiscardCount(bool opp, CardFilter f, bool deck = false) : opp_(opp), f_(std::move(f)), deck_(deck) {}
    int eval(EffectCtx& ctx) const override {
        const PlayerState& p = ctx.game.state().players[sidePlayer(ctx, opp_)];
        int n = 0;
        for (const CardDef* c : deck_ ? p.deck : p.discard) n += f_.matches(c);
        return n;
    }

private:
    bool opp_;
    CardFilter f_;
    bool deck_;
};

class PlayerNum : public Num {  // (points SIDE), (tools SIDE): Tools on all their Pokémon, (ko-count SIDE)
public:
    enum What { Points, Tools, KoCount };
    PlayerNum(What what, bool opp) : what_(what), opp_(opp) {}
    int eval(EffectCtx& ctx) const override {
        const PlayerState& p = ctx.game.state().players[sidePlayer(ctx, opp_)];
        if (what_ == Points) return p.points;
        if (what_ == KoCount) return p.koCount;
        int n = p.active.card ? int(p.active.tools.size()) : 0;
        for (const PokemonSlot& b : p.bench) n += int(b.tools.size());
        return n;
    }

private:
    What what_;
    bool opp_;
};

class WithAttack : public Num {  // Pokémon in play and cards in the hand with an attack of that name (Puppy Pile)
public:
    WithAttack(bool opp, CardFilter f) : opp_(opp), f_(std::move(f)) {}
    int eval(EffectCtx& ctx) const override {
        const PlayerState& p = ctx.game.state().players[sidePlayer(ctx, opp_)];
        int n = p.active.card && f_.matches(p.active.card, true);
        for (const PokemonSlot& b : p.bench) n += f_.matches(b.card, true);
        for (const CardDef* c : p.hand) n += f_.matches(c);
        return n;
    }

private:
    bool opp_;
    CardFilter f_;
};

// (copy-attack TARGET [:random] [:not-ex] [:energy]): choose (or pick at random) one attack of those Pokémon and use
// it as this attack, and "this Pokémon" is still the user. No Energy needed, unless :energy: "If this Pokémon doesn't
// have the necessary Energy to use that attack, this attack does nothing" (Ditto; the attack's printed cost, ruling).
// (copy-attack-cards SIDE): an attack of the Pokémon cards in that player's hand and deck, at random (Mew).
// A copied attack doesn't copy again.
class CopyAttack : public Stmt {
public:
    CopyAttack(const Target* t, bool random, bool notEx, bool energy, int cardsOf = -1)
        : t_(t), random_(random), notEx_(notEx), energy_(energy), cardsOf_(cardsOf) {}
    void run(EffectCtx& ctx) const override {
        if (ctx.copyDepth) return;
        std::vector<const AttackDef*> options = attacks(ctx);
        if (options.empty()) return;
        std::size_t k = random_ ? std::uniform_int_distribution<std::size_t>(0, options.size() - 1)(ctx.game.rng())
                                : std::size_t(ctx.game.choose(ctx.player, int(options.size())));
        const AttackDef* a = options[k];
        ctx.game.say("  uses ", a->name);
        const PokemonSlot* self = ctx.game.find(ctx.self);
        if (energy_ && (!self || !canPay(ctx.game.energyOf(*self, ctx.player), a->cost))) {
            ctx.stopped = true;
            return ctx.game.say("  not enough Energy: the attack does nothing");
        }
        EffectCtx copy = ctx;
        copy.copyDepth = 1;
        copy.attackDef = a;
        copy.damage = a->damage;
        copy.stopped = false;
        if (a->script) a->script->run(copy);
        else hit(copy);
        ctx.endsTurn |= copy.endsTurn;
        ctx.dealt += copy.dealt;
    }
    bool possible(EffectCtx& ctx) const override { return !attacks(ctx).empty(); }

private:
    std::vector<const AttackDef*> attacks(EffectCtx& ctx) const {
        std::vector<const AttackDef*> out;
        if (cardsOf_ >= 0) {
            const PlayerState& p = ctx.game.state().players[sidePlayer(ctx, cardsOf_ == 1)];
            for (const auto* pile : {&p.hand, &p.deck})
                for (const CardDef* c : *pile)
                    for (const AttackDef& a : c->attacks) out.push_back(&a);
            return out;
        }
        Uids from;
        t_->candidates(ctx, from);
        for (uint16_t uid : from)
            if (const PokemonSlot* p = ctx.game.find(uid); p && uid != ctx.self && !(notEx_ && p->card->ex))
                for (const AttackDef& a : p->card->attacks) out.push_back(&a);
        return out;
    }

    const Target* t_;
    bool random_, notEx_, energy_;
    int cardsOf_;  // copy-attack-cards: 0 my, 1 opp; -1 = from the Pokémon TARGET picks
};

// (opp-hand-to-deck FILTER…), (opp-hand-discard FILTER…): you choose a matching card in your opponent's hand; it's
// shuffled into their deck (Silver) or discarded (Absol). (opp-hand-to-bench FILTER…): a Basic Pokémon onto their
// Bench, without its "when you play this Pokémon" Abilities (Team Rocket's Boss, ruling).
class OppHandToDeck : public Stmt {
public:
    enum To { Deck, Discard, Bench };
    OppHandToDeck(CardFilter f, To to = Deck) : f_(std::move(f)), to_(to) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[1 - ctx.player];
        std::vector<std::size_t> hits = matches(p);
        if (hits.empty()) return;
        const CardDef* c = p.hand.take(hits[ctx.game.choose(ctx.player, int(hits.size()))]);
        if (to_ == Bench) {
            ctx.game.addToBench(1 - ctx.player, c);
            ctx.game.say("  ", c->name, " from the opponent's hand onto their Bench");
            return;
        }
        if (to_ == Discard) {
            p.discard.push_back(c);
            ctx.game.say("  ", c->name, " is discarded from the opponent's hand");
            return;
        }
        p.deck.push_back(c);
        std::shuffle(p.deck.begin(), p.deck.end(), ctx.game.rng());
        ctx.game.say("  ", c->name, " is shuffled into the opponent's deck");
    }
    bool possible(EffectCtx& ctx) const override {  // onto the Bench: a Basic and room for it
        const PlayerState& p = ctx.game.state().players[1 - ctx.player];
        return to_ != Bench || (!p.bench.full() && !matches(p).empty());
    }

private:
    std::vector<std::size_t> matches(const PlayerState& p) const {
        std::vector<std::size_t> hits;
        if (to_ == Bench && p.bench.full()) return hits;
        for (std::size_t i = 0; i < p.hand.size(); ++i)
            if (f_.matches(p.hand[i]) && (to_ != Bench || p.hand[i]->isBasic())) hits.push_back(i);
        return hits;
    }

    CardFilter f_;
    To to_;
};

// (top-card-if FILTER…): look at your top card; matching, it goes into your hand, else to the bottom (Mythical Slab).
class TopCardIf : public Stmt {
public:
    explicit TopCardIf(CardFilter f) : f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        if (p.deck.empty()) return;
        const CardDef* c = p.deck.pop_back();
        if (f_.matches(c)) {
            ctx.game.toHand(ctx.player, c);
            return;
        }
        p.deck.push_back(c);  // the last element is the top: move it to the front
        std::rotate(p.deck.begin(), p.deck.end() - 1, p.deck.end());
    }
    bool possible(EffectCtx& ctx) const override { return !ctx.game.state().players[ctx.player].deck.empty(); }

private:
    CardFilter f_;
};

// (discard-hand-random-of SIDE N FILTER…): N random matching cards from that hand to the discard pile.
class DiscardHandRandomOf : public Stmt {
public:
    DiscardHandRandomOf(bool opp, int n, CardFilter f) : opp_(opp), n_(n), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[sidePlayer(ctx, opp_)];
        for (int i = 0; i < n_; ++i) {
            std::vector<std::size_t> hits;
            for (std::size_t k = 0; k < p.hand.size(); ++k)
                if (f_.matches(p.hand[k])) hits.push_back(k);
            if (hits.empty()) return;
            p.discard.push_back(p.hand.take(hits[std::uniform_int_distribution<std::size_t>(0, hits.size() - 1)(ctx.game.rng())]));
        }
    }

private:
    bool opp_;
    int n_;
    CardFilter f_;
};

// (dig N FILTER…): look at the top N cards of your deck, put every matching one into your hand, and
// shuffle the rest back.
class Dig : public Stmt {
public:
    Dig(int n, CardFilter f) : n_(n), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        std::vector<const CardDef*> top;
        for (int i = 0; i < n_ && !p.deck.empty(); ++i) top.push_back(p.deck.pop_back());
        for (const CardDef* c : top) {
            if (f_.matches(c) && p.hand.size() < std::size_t(rules::HandLimit)) {
                p.hand.push_back(c);
                ctx.game.say("  ", c->name, " from the top of the deck into the hand");
            } else {
                p.deck.push_back(c);
            }
        }
        std::shuffle(p.deck.begin(), p.deck.end(), ctx.game.rng());
    }
    bool possible(EffectCtx& ctx) const override { return !ctx.game.state().players[ctx.player].deck.empty(); }

private:
    int n_;
    CardFilter f_;
};

// (swap-pokemon): a Pokémon from your hand (your choice) for a random Pokémon from your deck.
// (swap-for FILTER…): any card from your hand (your choice) for a random matching card from your deck (Kid's Room).
class SwapPokemon : public Stmt {
public:
    SwapPokemon(CardFilter hand, CardFilter deck) : hand_(std::move(hand)), deck_(std::move(deck)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        std::vector<std::size_t> inHand, inDeck;
        for (std::size_t i = 0; i < p.hand.size(); ++i)
            if (hand_.matches(p.hand[i])) inHand.push_back(i);
        for (std::size_t i = 0; i < p.deck.size(); ++i)
            if (deck_.matches(p.deck[i])) inDeck.push_back(i);
        if (inHand.empty() || inDeck.empty()) return;
        std::size_t give = inHand[ctx.game.choose(ctx.player, int(inHand.size()))];
        std::size_t take = inDeck[std::uniform_int_distribution<std::size_t>(0, inDeck.size() - 1)(ctx.game.rng())];
        const CardDef* got = p.deck.take(take);
        const CardDef* gave = p.hand.take(give);
        p.deck.push_back(gave);
        p.hand.push_back(got);
        std::shuffle(p.deck.begin(), p.deck.end(), ctx.game.rng());
        ctx.game.say("  ", gave->name, " into the deck, ", got->name, " into the hand");
    }
    bool possible(EffectCtx& ctx) const override {  // a card to give and one to get
        const PlayerState& p = ctx.game.state().players[ctx.player];
        auto give = [&](const CardDef* c) { return hand_.matches(c); };
        auto get = [&](const CardDef* c) { return deck_.matches(c); };
        return std::any_of(p.hand.begin(), p.hand.end(), give) && std::any_of(p.deck.begin(), p.deck.end(), get);
    }

private:
    CardFilter hand_, deck_;
};

class DrawUntil : public Stmt {  // (draw-until N): draw until your hand holds N cards (N may be (hand-size opp))
public:
    explicit DrawUntil(const Num* n) : n_(n) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        std::size_t n = std::size_t(std::max(0, n_->eval(ctx)));
        while (p.hand.size() < n && !p.deck.empty() && p.hand.size() < std::size_t(rules::HandLimit)) ctx.game.draw(ctx.player);
    }
    bool possible(EffectCtx& ctx) const override {
        const PlayerState& p = ctx.game.state().players[ctx.player];
        return int(p.hand.size()) < n_->eval(ctx) && !p.deck.empty();
    }

private:
    const Num* n_;
};

// (hand-to-deck FILTER…): shuffle a matching card from your hand (your choice) into your deck (Area Zero).
class HandToDeck : public Stmt {
public:
    explicit HandToDeck(CardFilter f) : f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        std::vector<std::size_t> hits;
        for (std::size_t i = 0; i < p.hand.size(); ++i)
            if (f_.matches(p.hand[i])) hits.push_back(i);
        if (hits.empty()) return;
        const CardDef* c = p.hand.take(hits[ctx.game.choose(ctx.player, int(hits.size()))]);
        p.deck.push_back(c);
        std::shuffle(p.deck.begin(), p.deck.end(), ctx.game.rng());
        ctx.game.say("  ", c->name, " is shuffled into the deck");
    }
    bool possible(EffectCtx& ctx) const override {
        const PlayerState& p = ctx.game.state().players[ctx.player];
        return std::any_of(p.hand.begin(), p.hand.end(), [&](const CardDef* c) { return f_.matches(c); });
    }

private:
    CardFilter f_;
};

// (discard-hand-to SIDE N): that player discards cards of their choice until they have N in their hand (Nasty Notice).
class DiscardHandTo : public Stmt {
public:
    DiscardHandTo(int side, int n) : side_(side), n_(n) {}
    void run(EffectCtx& ctx) const override {
        for (int player : players(ctx)) {
            PlayerState& p = ctx.game.state().players[player];
            while (p.hand.size() > std::size_t(n_)) {
                const CardDef* c = p.hand.take(ctx.game.choose(player, int(p.hand.size())));
                p.discard.push_back(c);
                ctx.game.say("  P", player + 1, " discards ", c->name);
            }
        }
    }
    bool possible(EffectCtx& ctx) const override {
        for (int player : players(ctx))
            if (ctx.game.state().players[player].hand.size() > std::size_t(n_)) return true;
        return false;
    }

private:
    std::vector<int> players(const EffectCtx& ctx) const {
        if (side_ == 2) return {ctx.player, 1 - ctx.player};
        return {side_ ? 1 - ctx.player : ctx.player};
    }
    int side_;  // 0 my, 1 opp, 2 both
    int n_;
};

class DiscardStadium : public Stmt {  // (discard-stadium): the Stadium in play goes to its owner's discard pile
public:
    void run(EffectCtx& ctx) const override {
        GameState& s = ctx.game.state();
        if (!s.stadium) return;
        ctx.game.say("  ", s.stadium->name, " is discarded");
        s.players[s.stadiumOwner].discard.push_back(s.stadium);
        s.stadium = nullptr;
        s.stadiumOwner = -1;
    }
    bool possible(EffectCtx& ctx) const override { return ctx.game.state().stadium != nullptr; }
};

class StadiumInPlay : public Cond {  // (stadium-in-play)
public:
    bool test(EffectCtx& ctx) const override { return ctx.game.state().stadium != nullptr; }
};

class MyTurn : public Cond {  // (my-turn): it's the effect's player's turn (at the end of a turn: theirs ended)
public:
    bool test(EffectCtx& ctx) const override { return ctx.game.state().current == ctx.player; }
};

class PlayedSupporter : public Cond {  // (played-supporter): you played a Supporter this turn
public:
    bool test(EffectCtx& ctx) const override {
        return ctx.game.state().players[ctx.player].supporterTurn == ctx.game.state().turn;
    }
};

// (hand-size my|opp). Asked before a Trainer is played, your hand still holds it: that card doesn't count (Maintenance).
class HandSize : public Num {
public:
    explicit HandSize(bool opp) : opp_(opp) {}
    int eval(EffectCtx& ctx) const override {
        return int(ctx.game.state().players[sidePlayer(ctx, opp_)].hand.size()) - (!opp_ && ctx.trainerInHand);
    }

private:
    bool opp_;
};

class PointsNeeded : public Num {  // (points-needed my|opp)
public:
    explicit PointsNeeded(bool opp) : opp_(opp) {}
    int eval(EffectCtx& ctx) const override {
        return std::max(0, rules::PointsToWin - ctx.game.state().players[sidePlayer(ctx, opp_)].points);
    }

private:
    bool opp_;
};

// (discard-from-hand N [FILTER…]): the player discards N (matching) cards of their choice from the hand (Garchomp;
// Slowking: Pokémon Tool cards).
class DiscardFromHand : public Stmt {
public:
    DiscardFromHand(int n, CardFilter f) : n_(n), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        for (int i = 0; i < n_; ++i) {
            std::vector<std::size_t> hits = matches(p);
            if (hits.empty()) return;
            const CardDef* c = p.hand.take(hits[std::size_t(ctx.game.choose(ctx.player, int(hits.size())))]);
            p.discard.push_back(c);
            ctx.game.say("  ", c->name, " is discarded from the hand");
        }
    }
    bool possible(EffectCtx& ctx) const override { return matches(ctx.game.state().players[ctx.player]).size() >= std::size_t(n_); }

private:
    std::vector<std::size_t> matches(const PlayerState& p) const {
        std::vector<std::size_t> hits;
        for (std::size_t k = 0; k < p.hand.size(); ++k)
            if (f_.matches(p.hand[k])) hits.push_back(k);
        return hits;
    }

    int n_;
    CardFilter f_;
};

// (move-damage FROM TO N|all): damage moves from one Pokémon onto another (Dusknoir, Brambleghast).
class MoveDamage : public Stmt {
public:
    MoveDamage(const Target* from, const Target* to, int n) : from_(from), to_(to), n_(n) {}
    void run(EffectCtx& ctx) const override {
        Uids f, t;
        from_->select(ctx, f);
        to_->select(ctx, t);
        PokemonSlot* a = f.empty() ? nullptr : ctx.game.find(f[0]);
        int owner = 0;
        PokemonSlot* b = t.empty() ? nullptr : ctx.game.find(t[0], &owner);
        if (!a || !b || a == b) return;
        int k = n_ < 0 ? a->damage : std::min(n_, a->damage);
        a->damage -= k;
        b->damage += k;
        if (ctx.game.logging()) ctx.game.say("  ", k, " damage moved -> ", ctx.game.show(*b, owner));
    }
    bool possible(EffectCtx& ctx) const override {
        Uids t;
        to_->candidates(ctx, t);
        return !t.empty() && anyCandidate(ctx, from_, [&](const PokemonSlot& p) { return p.damage > 0 && p.uid != t[0]; });
    }

private:
    const Target* from_;
    const Target* to_;
    int n_;
};

class DiscardPokemon : public Stmt {  // (discard-pokemon TARGET): "discard this Pokémon" from play (Klefki)
public:
    explicit DiscardPokemon(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets)
            if (affects(ctx, uid)) ctx.game.discardFromPlay(uid);
    }

private:
    const Target* t_;
};

// (evolve-from-deck TARGET): "put a random card from your deck that evolves from this Pokémon onto this Pokémon to
// evolve it" (Caterpie, Porygon2). An effect, so the usual timing limits don't apply; no "when you play it" Ability.
class EvolveFromDeck : public Stmt {
public:
    EvolveFromDeck(const Target* t, CardFilter f) : t_(t), f_(std::move(f)) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p) continue;
            PlayerState& ps = ctx.game.state().players[owner];
            std::vector<std::size_t> hits;
            for (std::size_t k = 0; k < ps.deck.size(); ++k)
                if (ps.deck[k]->isEvolution() && ps.deck[k]->evolvesFrom == p->card->name && f_.matches(ps.deck[k])) hits.push_back(k);
            if (hits.empty()) continue;
            const CardDef* card = ps.deck.take(hits[std::uniform_int_distribution<std::size_t>(0, hits.size() - 1)(ctx.game.rng())]);
            ctx.game.say("  ", p->card->name, " evolves into ", card->name, " from the deck");
            ctx.game.evolve(*p, card);
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [&](const PokemonSlot& p) {
            int owner = 0;
            ctx.game.find(p.uid, &owner);
            const auto& deck = ctx.game.state().players[owner].deck;
            return std::any_of(deck.begin(), deck.end(), [&](const CardDef* c) {
                return c->isEvolution() && c->evolvesFrom == p.card->name && f_.matches(c);
            });
        });
    }

private:
    const Target* t_;
    CardFilter f_;  // the evolution card must match (Quick-Grow Extract: a {G} Pokémon)
};

class FirstTurn : public Cond {  // (first-turn): it's the player's own first turn (Wimpod, Zeraora)
public:
    bool test(EffectCtx& ctx) const override { return ctx.game.ownFirstTurn(ctx.player); }
};

// Knock-out triggers (Game::knockOut reads the flags once the triggers have run).
class NoPoints : public Stmt {  // (no-points): "your opponent can't get any points for it"
public:
    void run(EffectCtx& ctx) const override { ctx.noPoints = true; }
};

class ToHandInstead : public Stmt {  // (to-hand-instead): the Pokémon and its evolutions go to the hand (Rescue Scarf)
public:
    void run(EffectCtx& ctx) const override { ctx.toHandInstead = true; }
};

class KnockOut : public Stmt {  // (knock-out TARGET): "the Attacking Pokémon is Knocked Out" (Galarian Cursola)
public:
    explicit KnockOut(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            if (PokemonSlot* p = ctx.game.find(uid, &owner)) {
                p->damage = std::max(p->damage, ctx.game.maxHp(*p, owner));
                p->koByEffect = true;
            }
        }
    }

private:
    const Target* t_;
};

class DiscardThis : public Stmt {  // (discard-this): a Tool's "discard this card", from the Pokémon it's on
public:
    void run(EffectCtx& ctx) const override {
        int owner = 0;
        PokemonSlot* p = ctx.game.find(ctx.self, &owner);
        if (!p) return;
        for (std::size_t i = 0; i < p->tools.size(); ++i)
            if (p->tools[i] == ctx.card) {
                ctx.game.say("  ", ctx.card->name, " is discarded");
                ctx.game.state().players[owner].discard.push_back(p->tools.take(i));
                return;
            }
    }
};

// Memory of earlier turns: the opponent's last turn is the one before this; your last turn is two back.
class KoLastTurn : public Cond {  // (ko-last-turn TYPE…): any of your (TYPE) Pokémon were Knocked Out by damage
public:                           // from an attack during your opponent's last turn
    explicit KoLastTurn(uint16_t types) : types_(types) {}
    bool test(EffectCtx& ctx) const override {
        const GameState& s = ctx.game.state();
        const PlayerState& p = s.players[ctx.player];
        return p.koTurn == s.turn - 1 && (!types_ || (p.koTypes & types_));
    }

private:
    uint16_t types_;
};

class UsedLastTurn : public Cond {  // (used-last-turn "A"): one of your Pokémon used the attack A during your last turn
public:
    explicit UsedLastTurn(std::string name) : name_(std::move(name)) {}
    bool test(EffectCtx& ctx) const override {
        const GameState& s = ctx.game.state();
        const PlayerState& p = s.players[ctx.player];
        return p.lastAttack && p.attackTurn == s.turn - 2 && p.lastAttack->name == name_;  // none yet: -1 is "turn 1 − 2"
    }

private:
    std::string name_;
};

class PointsLastTurn : public Num {  // (points-last-turn my|opp): points that player got during their last turn
public:
    explicit PointsLastTurn(bool opp) : opp_(opp) {}
    int eval(EffectCtx& ctx) const override {
        const GameState& s = ctx.game.state();
        int who = sidePlayer(ctx, opp_);
        int last = who == s.current ? s.turn - 2 : s.turn - 1;
        return s.players[who].scoredTurn == last ? s.players[who].scored : 0;
    }

private:
    bool opp_;
};

// (top-count N FILTER…): matching cards among the top N of your deck, looked at in place: "Reveal the top 3 cards
// of your deck. … for each Pokémon with a Retreat Cost of 3 or more you find there" (Golurk), or "Discard the top
// card of your deck. If that card is a {F} Pokémon…" (Dugtrio), asked before the mill.
class TopCount : public Num {
public:
    TopCount(int n, CardFilter f) : n_(n), f_(std::move(f)) {}
    int eval(EffectCtx& ctx) const override {
        const auto& deck = ctx.game.state().players[ctx.player].deck;
        int found = 0;
        for (std::size_t i = 0; i < std::size_t(n_) && i < deck.size(); ++i) found += f_.matches(deck[deck.size() - 1 - i]);
        return found;
    }

private:
    int n_;
    CardFilter f_;
};

class ShuffleDeck : public Stmt {  // (shuffle-deck my|opp): "Shuffle the revealed cards back into your deck"
public:
    explicit ShuffleDeck(bool opp) : opp_(opp) {}
    void run(EffectCtx& ctx) const override {
        auto& deck = ctx.game.state().players[sidePlayer(ctx, opp_)].deck;
        std::shuffle(deck.begin(), deck.end(), ctx.game.rng());
    }

private:
    bool opp_;
};

// (devolve TARGET): "devolve it by putting the highest Stage Evolution card on it into your opponent's hand" (Celebi).
// Damage, Energy and Tools stay; Special Conditions and effects end (ruling). Too much damage: the knock-out check.
class Devolve : public Stmt {
public:
    explicit Devolve(const Target* t) : t_(t) {}
    void run(EffectCtx& ctx) const override {
        Uids targets;
        t_->select(ctx, targets);
        for (uint16_t uid : targets) {
            int owner = 0;
            PokemonSlot* p = ctx.game.find(uid, &owner);
            if (!p || p->below.empty() || !affects(ctx, uid)) continue;
            const CardDef* top = p->card;
            p->card = p->below.pop_back();
            ctx.game.clearConditions(*p);
            ctx.game.toHand(owner, top);
            if (ctx.game.logging()) ctx.game.say("  ", top->name, " goes back to the hand -> ", ctx.game.show(*p, owner));
        }
    }
    bool possible(EffectCtx& ctx) const override {
        return anyCandidate(ctx, t_, [](const PokemonSlot& p) { return !p.below.empty(); });
    }

private:
    const Target* t_;
};

// (ko-check): knock-outs happen now, in the middle of the attack, and the opponent picks a new Active for the rest:
// "If the first attack Knocks Out your opponent's Active Pokémon, the second attack is used after your opponent
// chooses a new Active Pokémon" (Mega Kangaskhan ex). A knock-out that wins the game ends the attack (App).
class KoCheck : public Stmt {
public:
    void run(EffectCtx& ctx) const override {
        ctx.game.knockOutCheck(ctx.self);
        if (ctx.game.state().phase == Phase::Over) ctx.stopped = true;
    }
};

class AttackUses : public Num {  // (attack-uses SIDE "A"): times that player's Pokémon used attack A this game (Alcremie)
public:
    AttackUses(bool opp, std::string name) : opp_(opp), name_(std::move(name)) {}
    int eval(EffectCtx& ctx) const override {
        for (const AttackUse& u : ctx.game.state().players[sidePlayer(ctx, opp_)].attackUses)
            if (u.attack->name == name_) return u.count;
        return 0;
    }

private:
    bool opp_;
    std::string name_;
};

int sideOf(const SExpr& e) {
    if (e.atom == "both") return 2;
    return oppArg(e) ? 1 : 0;
}
}  // namespace

void addCardWords(Words& w) {
    w.stmt["draw"] = {1, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<Draw>(b.num(arg(e, 0)), argCount(e) > 1 && oppArg(arg(e, 1)));
    }};
    w.stmt["search"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        const std::string& dest = arg(e, 1).atom;
        if (dest != "hand" && dest != "bench") scriptFail("expected hand or bench", e);
        return b.make<Search>(b.num(arg(e, 0)), dest == "bench", parseCardFilter(e, 2));
    }};
    const std::pair<const char*, Cards::What> cards[] = {{"mill", Cards::Mill},
                                                         {"discard-hand-random", Cards::DiscardHandRandom},
                                                         {"hand-to-deck-random", Cards::HandToDeckRandom},
                                                         {"shuffle-hand-draw", Cards::ShuffleHandDraw}};
    for (auto [name, what] : cards) {
        auto whatCopy = what;
        w.stmt[name] = {1, 2, [whatCopy](const SExpr& e, ScriptBuilder& b) {
            bool same = argCount(e) > 1 && arg(e, 1).atom == "same";
            bool any = arg(e, 0).atom == "any";
            if (any && (whatCopy != Cards::HandToDeckRandom || argCount(e) < 2)) scriptFail("'any' only with hand-to-deck-random N", e);
            return b.make<Cards>(whatCopy, any ? 3 : sideOf(arg(e, 0)), argCount(e) > 1 && !same ? b.num(arg(e, 1)) : nullptr);
        }};
    }
    w.stmt["switch-in"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<SwitchIn>(b.target(arg(e, 0))); }};
    w.stmt["to-hand"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<TakeBack>(b.target(arg(e, 0)), false); }};
    w.stmt["to-deck"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<TakeBack>(b.target(arg(e, 0)), true); }};
    w.stmt["tools-to-hand"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<ToolsToHand>(b.target(arg(e, 0))); }};
    w.stmt["tools-to-deck"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<ToolsToHand>(b.target(arg(e, 0)), true); }};
    w.stmt["discard-tools"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<DiscardTools>(b.target(arg(e, 0))); }};
    w.stmt["rare-candy"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<RareCandy>(); }};
    w.stmt["swap-pokemon"] = {0, 0, [](const SExpr&, ScriptBuilder& b) {
        CardFilter pokemon;
        pokemon.pokemon = true;
        return b.make<SwapPokemon>(pokemon, pokemon);
    }};
    w.stmt["swap-for"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<SwapPokemon>(CardFilter{}, parseCardFilter(e, 0));
    }};
    w.stmt["draw-until"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<DrawUntil>(b.num(arg(e, 0))); }};
    w.stmt["hand-to-deck"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) {  // no FILTER: any card
        return b.make<HandToDeck>(parseCardFilter(e, 0));
    }};
    w.stmt["from-discard"] = {4, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        const std::string& mode = arg(e, 0).atom;
        const std::string& dest = arg(e, 3).atom;
        if (mode != "random" && mode != "choose") scriptFail("expected random or choose", e);
        if (dest != "hand" && dest != "bench") scriptFail("expected hand or bench", e);
        return b.make<FromDiscard>(mode == "random", b.num(arg(e, 1)), oppArg(arg(e, 2)), dest == "bench", parseCardFilter(e, 4));
    }};
    w.num["discard-count"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardCount>(oppArg(arg(e, 0)), parseCardFilter(e, 1));
    }};
    w.stmt["dig"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) { return b.make<Dig>(intArg(arg(e, 0)), parseCardFilter(e, 1)); }};
    w.stmt["effect"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        const std::string& d = arg(e, 0).atom;
        // while-active: "until this Pokémon leaves the Active Spot"; going to the Bench ends it (clearConditions)
        int turns = d == "this-turn" ? 0 : d == "opp-next-turn" ? 1 : d == "my-next-turn" ? 2 : d == "while-active" ? 1000000 : -1;
        if (turns < 0) scriptFail("unknown duration '" + d + "'", e);
        const std::string& who = arg(e, 1).atom;
        static const char* whos[] = {"self", "defending", "my-active", "opp-active", "me", "opp"};
        if (std::none_of(std::begin(whos), std::end(whos), [&](const char* x) { return who == x; }))
            scriptFail("unknown target '" + who + "'", e);
        return b.make<Timed>(turns, who, b.modifier(arg(e, 2), who == "me" || who == "opp" ? ModScope::MyAll : ModScope::Self));
    }};
    w.stmt["discard-hand-to"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardHandTo>(sideOf(arg(e, 0)), intArg(arg(e, 1)));
    }};
    w.stmt["discard-stadium"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<DiscardStadium>(); }};
    w.cond["stadium-in-play"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<StadiumInPlay>(); }};
    w.cond["my-turn"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<MyTurn>(); }};
    w.cond["played-supporter"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<PlayedSupporter>(); }};
    w.num["hand-size"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<HandSize>(oppArg(arg(e, 0))); }};
    w.stmt["discard-from-hand"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardFromHand>(intArg(arg(e, 0)), parseCardFilter(e, 1));
    }};
    w.stmt["move-damage"] = {3, 3, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<MoveDamage>(b.target(arg(e, 0)), b.target(arg(e, 1)), arg(e, 2).atom == "all" ? -1 : intArg(arg(e, 2)));
    }};
    w.stmt["discard-pokemon"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<DiscardPokemon>(b.target(arg(e, 0))); }};
    w.stmt["evolve-from-deck"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<EvolveFromDeck>(b.target(arg(e, 0)), parseCardFilter(e, 1));
    }};
    w.num["points"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<PlayerNum>(PlayerNum::Points, oppArg(arg(e, 0))); }};
    w.num["tools"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<PlayerNum>(PlayerNum::Tools, oppArg(arg(e, 0))); }};
    w.num["ko-count"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<PlayerNum>(PlayerNum::KoCount, oppArg(arg(e, 0))); }};
    w.num["deck-count"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardCount>(oppArg(arg(e, 0)), parseCardFilter(e, 1), true);
    }};
    w.num["with-attack"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) {  // (with-attack SIDE "A"): in play and in the hand
        CardFilter f;
        f.attack = arg(e, 1).atom;
        return b.make<WithAttack>(oppArg(arg(e, 0)), f);
    }};
    w.stmt["copy-attack"] = {1, 4, [](const SExpr& e, ScriptBuilder& b) {
        bool random = false, notEx = false, energy = false;
        for (std::size_t i = 1; i < argCount(e); ++i) {
            const std::string& o = arg(e, i).atom;
            if (o == ":random") random = true;
            else if (o == ":not-ex") notEx = true;
            else if (o == ":energy") energy = true;
            else scriptFail("unknown copy-attack option", e);
        }
        return b.make<CopyAttack>(b.target(arg(e, 0)), random, notEx, energy);
    }};
    w.stmt["copy-attack-cards"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<CopyAttack>(nullptr, true, false, false, oppArg(arg(e, 0)) ? 1 : 0);
    }};
    w.stmt["opp-hand-to-deck"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) { return b.make<OppHandToDeck>(parseCardFilter(e, 0)); }};
    w.stmt["opp-hand-discard"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<OppHandToDeck>(parseCardFilter(e, 0), OppHandToDeck::Discard);
    }};
    w.stmt["opp-hand-to-bench"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<OppHandToDeck>(parseCardFilter(e, 0), OppHandToDeck::Bench);
    }};
    w.stmt["top-card-if"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) { return b.make<TopCardIf>(parseCardFilter(e, 0)); }};
    w.stmt["discard-hand-random-of"] = {2, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<DiscardHandRandomOf>(oppArg(arg(e, 0)), intArg(arg(e, 1)), parseCardFilter(e, 2));
    }};
    w.cond["first-turn"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<FirstTurn>(); }};
    w.stmt["no-points"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<NoPoints>(); }};
    w.stmt["to-hand-instead"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<ToHandInstead>(); }};
    w.stmt["knock-out"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<KnockOut>(b.target(arg(e, 0))); }};
    w.stmt["discard-this"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<DiscardThis>(); }};
    w.cond["ko-last-turn"] = {0, AnyCount, [](const SExpr& e, ScriptBuilder& b) { return b.make<KoLastTurn>(typeMask(e, 0)); }};
    w.cond["used-last-turn"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<UsedLastTurn>(arg(e, 0).atom); }};
    w.num["points-last-turn"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<PointsLastTurn>(oppArg(arg(e, 0)));
    }};
    w.num["points-needed"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<PointsNeeded>(oppArg(arg(e, 0))); }};
    w.num["top-count"] = {1, AnyCount, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<TopCount>(intArg(arg(e, 0)), parseCardFilter(e, 1));
    }};
    w.stmt["shuffle-deck"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<ShuffleDeck>(oppArg(arg(e, 0))); }};
    w.stmt["devolve"] = {1, 1, [](const SExpr& e, ScriptBuilder& b) { return b.make<Devolve>(b.target(arg(e, 0))); }};
    w.stmt["ko-check"] = {0, 0, [](const SExpr&, ScriptBuilder& b) { return b.make<KoCheck>(); }};
    w.num["attack-uses"] = {2, 2, [](const SExpr& e, ScriptBuilder& b) {
        return b.make<AttackUses>(oppArg(arg(e, 0)), arg(e, 1).atom);
    }};
}
