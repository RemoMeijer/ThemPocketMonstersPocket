// Cards and the board: drawing, searching, hands and decks, switching, Tools, Rare Candy, timed effects.
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "effects/effect.h"
#include "game/game.h"

namespace {
int sidePlayer(const EffectCtx& ctx, bool opp) { return opp ? 1 - ctx.player : ctx.player; }

// Which cards in a deck qualify: pokemon, basic, stage1, stage2, trainer, supporter, item, tool, (type T…),
// (name "A" …), (evolves-from "A").
struct CardFilter {
    bool pokemon = false, basic = false, trainer = false;
    uint8_t stages = 0;
    TrainerKind kind = TrainerKind::None;
    uint16_t types = 0;
    std::vector<std::string> names;
    std::string evolvesFrom;

    bool matches(const CardDef* c) const {
        return (!pokemon || c->isPokemon()) && (!basic || c->isBasic()) &&
               (!stages || (c->isPokemon() && (stages & stageBit(c->stage)))) &&
               (!trainer || c->category == Category::Trainer) && (kind == TrainerKind::None || c->isTrainer(kind)) &&
               (!types || (c->isPokemon() && c->hasType(types))) &&
               (names.empty() || std::find(names.begin(), names.end(), c->name) != names.end()) &&
               (evolvesFrom.empty() || c->evolvesFrom == evolvesFrom);
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
        else if (w == "type" && a.list) f.types = typeMask(a, 0), f.pokemon = true;
        else if (w == "name" && a.list)
            for (std::size_t n = 0; n < argCount(a); ++n) f.names.push_back(arg(a, n).atom);
        else if (w == "evolves-from" && a.list) f.evolvesFrom = arg(a, 0).atom;
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
                if (f_.matches(p.deck[k]) && (!bench_ || p.deck[k]->isBasic())) hits.push_back(k);
            if (hits.empty() || (bench_ ? p.bench.full() : p.hand.size() >= std::size_t(rules::HandLimit))) return;
            std::size_t k = hits[std::uniform_int_distribution<std::size_t>(0, hits.size() - 1)(ctx.game.rng())];
            const CardDef* card = p.deck.take(k);
            if (bench_) {
                p.bench.push_back({});
                ctx.game.putIntoPlay(p.bench.back(), card);
                ctx.game.say("  ", card->name, " from the deck onto the Bench");
            } else {
                p.hand.push_back(card);
                ctx.game.say("  ", card->name, " from the deck into the hand");
            }
        }
    }

private:
    const Num* n_;
    bool bench_;
    CardFilter f_;
};

// Hands and decks: (mill SIDE N), (discard-hand-random SIDE N), (hand-to-deck-random SIDE N),
// (shuffle-hand-draw SIDE N|same) with SIDE my, opp or both.
class Cards : public Stmt {
public:
    enum What { Mill, DiscardHandRandom, HandToDeckRandom, ShuffleHandDraw };
    Cards(What what, int side, const Num* n) : what_(what), side_(side), n_(n) {}
    void run(EffectCtx& ctx) const override {
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
    What what_;
    int side_;  // 0 my, 1 opp, 2 both
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
        ctx.game.clearConditions(*in);  // the one that went to the Bench
        ctx.game.movedToActive(owner);
        ctx.game.say("  P", owner + 1, "'s ", p.active.card->name, " is now Active, ", in->card->name, " goes to the Bench");
    }

private:
    const Target* t_;
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
            if (!p || !p->tool || !affects(ctx, uid)) continue;
            ctx.game.say("  ", p->tool->name, " is discarded");
            ctx.game.state().players[owner].discard.push_back(p->tool);
            p->tool = nullptr;
        }
    }

private:
    const Target* t_;
};

// (rare-candy): a Basic Pokémon in play evolves straight into a Stage 2 from the hand, skipping the
// Stage 1. Not on your first turn, and not on a Basic put into play this turn.
class RareCandy : public Stmt {
public:
    void run(EffectCtx& ctx) const override {
        GameState& s = ctx.game.state();
        PlayerState& p = s.players[ctx.player];
        if (ctx.game.ownFirstTurn(ctx.player)) return;
        std::vector<std::pair<int, std::size_t>> options;  // (target, hand position)
        for (int t = -1; t < int(p.bench.size()); ++t) {
            const PokemonSlot& basic = ctx.game.slot(ctx.player, t);
            if (!basic.card || basic.card->stage != Stage::Basic || basic.enteredTurn >= s.turn) continue;
            for (std::size_t h = 0; h < p.hand.size(); ++h)
                if (p.hand[h]->stage == Stage::Stage2 && p.hand[h]->evolvesFromBasic == basic.card->name)
                    options.push_back({t, h});
        }
        if (options.empty()) return;
        auto [t, h] = options[ctx.game.choose(ctx.player, int(options.size()))];
        PokemonSlot& target = ctx.game.slot(ctx.player, t);
        target.below.push_back(target.card);
        target.card = p.hand.take(h);
        target.enteredTurn = s.turn;
        ctx.game.clearConditions(target);
        ctx.game.say("  ", target.below.back()->name, " evolves straight into ", target.card->name);
        if (target.card->onEvolve) {
            EffectCtx inner{ctx.game, ctx.player};
            inner.self = target.uid;
            target.card->onEvolve->run(inner);
        }
    }
};

// (effect DURATION WHO MODIFIER): a modifier for a while.
//   DURATION: this-turn, opp-next-turn (until the end of the opponent's next turn), my-next-turn.
//   WHO: self, defending, my-active, opp-active (that Pokémon), or me, opp (the whole player).
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
            if (!ctx.game.find(uid, &owner)) return;
            if (owner != ctx.player && !affects(ctx, uid)) return;
            t.uid = uid;
            t.player = int8_t(owner);
        }
        if (s.timed.full()) s.timed.take(0);  // shortcut: drops the oldest effect past 16 at once; unseen so far
        s.timed.push_back(t);
    }

private:
    int turns_;
    std::string who_;
    Modifier mod_;
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

private:
    int n_;
    CardFilter f_;
};

// (swap-pokemon): a Pokémon from your hand (your choice) for a random Pokémon from your deck.
class SwapPokemon : public Stmt {
public:
    void run(EffectCtx& ctx) const override {
        PlayerState& p = ctx.game.state().players[ctx.player];
        std::vector<std::size_t> inHand, inDeck;
        for (std::size_t i = 0; i < p.hand.size(); ++i)
            if (p.hand[i]->isPokemon()) inHand.push_back(i);
        for (std::size_t i = 0; i < p.deck.size(); ++i)
            if (p.deck[i]->isPokemon()) inDeck.push_back(i);
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
};

class PlayedSupporter : public Cond {  // (played-supporter): you played a Supporter this turn
public:
    bool test(EffectCtx& ctx) const override {
        return ctx.game.state().players[ctx.player].supporterTurn == ctx.game.state().turn;
    }
};

class HandSize : public Num {  // (hand-size my|opp)
public:
    explicit HandSize(bool opp) : opp_(opp) {}
    int eval(EffectCtx& ctx) const override { return int(ctx.game.state().players[sidePlayer(ctx, opp_)].hand.size()); }

private:
    bool opp_;
};

int sideOf(const SExpr& e) {
    if (e.atom == "both") return 2;
    return oppArg(e) ? 1 : 0;
}
}  // namespace

void addCardWords(Words& w) {
    w.stmt["draw"] = [](const SExpr& e, ScriptBuilder& b) {
        return b.make<Draw>(b.num(arg(e, 0)), argCount(e) > 1 && oppArg(arg(e, 1)));
    };
    w.stmt["search"] = [](const SExpr& e, ScriptBuilder& b) {
        const std::string& dest = arg(e, 1).atom;
        if (dest != "hand" && dest != "bench") scriptFail("expected hand or bench", e);
        return b.make<Search>(b.num(arg(e, 0)), dest == "bench", parseCardFilter(e, 2));
    };
    const std::pair<const char*, Cards::What> cards[] = {{"mill", Cards::Mill},
                                                         {"discard-hand-random", Cards::DiscardHandRandom},
                                                         {"hand-to-deck-random", Cards::HandToDeckRandom},
                                                         {"shuffle-hand-draw", Cards::ShuffleHandDraw}};
    for (auto [name, what] : cards) {
        auto whatCopy = what;
        w.stmt[name] = [whatCopy](const SExpr& e, ScriptBuilder& b) {
            bool same = argCount(e) > 1 && arg(e, 1).atom == "same";
            return b.make<Cards>(whatCopy, sideOf(arg(e, 0)), argCount(e) > 1 && !same ? b.num(arg(e, 1)) : nullptr);
        };
    }
    w.stmt["switch-in"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<SwitchIn>(b.target(arg(e, 0))); };
    w.stmt["discard-tools"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<DiscardTools>(b.target(arg(e, 0))); };
    w.stmt["rare-candy"] = [](const SExpr&, ScriptBuilder& b) { return b.make<RareCandy>(); };
    w.stmt["swap-pokemon"] = [](const SExpr&, ScriptBuilder& b) { return b.make<SwapPokemon>(); };
    w.stmt["dig"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<Dig>(intArg(arg(e, 0)), parseCardFilter(e, 1)); };
    w.stmt["effect"] = [](const SExpr& e, ScriptBuilder& b) {
        const std::string& d = arg(e, 0).atom;
        int turns = d == "this-turn" ? 0 : d == "opp-next-turn" ? 1 : d == "my-next-turn" ? 2 : -1;
        if (turns < 0) scriptFail("unknown duration '" + d + "'", e);
        const std::string& who = arg(e, 1).atom;
        static const char* whos[] = {"self", "defending", "my-active", "opp-active", "me", "opp"};
        if (std::none_of(std::begin(whos), std::end(whos), [&](const char* x) { return who == x; }))
            scriptFail("unknown target '" + who + "'", e);
        return b.make<Timed>(turns, who, b.modifier(arg(e, 2), who == "me" || who == "opp" ? ModScope::MyAll : ModScope::Self));
    };
    w.cond["played-supporter"] = [](const SExpr&, ScriptBuilder& b) { return b.make<PlayedSupporter>(); };
    w.num["hand-size"] = [](const SExpr& e, ScriptBuilder& b) { return b.make<HandSize>(oppArg(arg(e, 0))); };
}
