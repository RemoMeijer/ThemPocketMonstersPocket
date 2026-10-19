// Putting Basic Pokémon into play: the Active one during setup, and onto the Bench.
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
void addBasics(const PlayerState& p, ActionKind kind, std::vector<Action>& out) {
    for (std::size_t i = 0; i < p.hand.size(); ++i)
        if (p.hand[i]->isBasic()) out.push_back({kind, int8_t(i)});
}

class PlaceActive : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const GameState& s = game.state();
        if (s.phase == Phase::Setup && !s.players[s.current].active.card)
            addBasics(s.players[s.current], ActionKind::PlaceActive, out);
    }
    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        game.putIntoPlay(p.active, p.hand.take(a.index));
        game.movedToActive(me);
        game.say("P", me + 1, " puts ", p.active.card->name, " in the Active spot");
    }
};

class PlaceBench : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const PlayerState& p = game.state().players[game.state().current];
        if (!p.bench.full()) addBasics(p, ActionKind::PlaceBench, out);
    }
    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        p.bench.push_back({});
        game.putIntoPlay(p.bench.back(), p.hand.take(a.index));
        game.say("P", me + 1, " benches ", p.bench.back().card->name);
        // "When you put this Pokémon from your hand onto your Bench, you may…" (not during setup)
        const PokemonSlot& placed = p.bench.back();
        if (game.state().phase == Phase::Main && placed.card->onBench && !game.abilitiesOff(placed)) {
            EffectCtx ctx{game, me};
            ctx.self = placed.uid;
            placed.card->onBench->run(ctx);
            game.knockOutCheck();
        }
    }
};
}  // namespace

const MoveRule& placeActiveRule() {
    static const PlaceActive rule;
    return rule;
}

const MoveRule& placeBenchRule() {
    static const PlaceBench rule;
    return rule;
}
