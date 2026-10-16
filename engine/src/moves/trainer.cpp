// Playing Trainer cards from the hand: Items (any number), Supporters (one per turn), Stadiums (one per
// turn, replacing the one in play), and attaching Tools (one per Pokémon).
#include "effects/effect.h"
#include "game/game.h"
#include "moves/moves.h"

namespace {
class PlayTrainer : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        const GameState& s = game.state();
        int me = s.current;
        const PlayerState& p = s.players[me];
        bool items = !game.playerHas(ModKind::CantPlayItem, me);
        bool supporters = p.supporterTurn != s.turn && !game.playerHas(ModKind::CantPlaySupporter, me) &&
                          (rules::SupporterOnFirstTurn || !game.firstTurnOfGame());
        bool stadiums = p.stadiumTurn != s.turn;
        // shortcut: a Trainer is playable even when it would do nothing (Rare Candy without a target); check
        // its effect first once a smarter player would be misled by that.
        for (std::size_t h = 0; h < p.hand.size(); ++h) {
            const CardDef* c = p.hand[h];
            bool ok = (c->isTrainer(TrainerKind::Item) && items && c->play) ||
                      (c->isTrainer(TrainerKind::Supporter) && supporters && c->play) ||
                      (c->isTrainer(TrainerKind::Stadium) && stadiums && (!s.stadium || s.stadium->name != c->name));
            if (ok) out.push_back({ActionKind::PlayTrainer, int8_t(h)});
        }
    }

    void apply(Game& game, Action a) const override {
        GameState& s = game.state();
        int me = s.current;
        PlayerState& p = s.players[me];
        const CardDef* card = p.hand.take(a.index);
        game.say("P", me + 1, " plays ", card->name);
        if (card->isTrainer(TrainerKind::Stadium)) {
            if (s.stadium) s.players[s.stadiumOwner].discard.push_back(s.stadium);
            s.stadium = card;
            s.stadiumOwner = me;
            p.stadiumTurn = s.turn;
            return;
        }
        if (card->isTrainer(TrainerKind::Supporter)) p.supporterTurn = s.turn;
        EffectCtx ctx{game, me};
        card->play->run(ctx);
        p.discard.push_back(card);
        game.knockOutCheck();
    }
};

class AttachTool : public MoveRule {
public:
    void addMoves(Game& game, std::vector<Action>& out) const override {
        int me = game.state().current;
        const PlayerState& p = game.state().players[me];
        for (std::size_t h = 0; h < p.hand.size(); ++h) {
            if (!p.hand[h]->isTrainer(TrainerKind::Tool)) continue;
            for (int t = -1; t < int(p.bench.size()); ++t)
                if (!game.slot(me, t).tool) out.push_back({ActionKind::AttachTool, int8_t(h), int8_t(t)});
        }
    }

    void apply(Game& game, Action a) const override {
        int me = game.state().current;
        PlayerState& p = game.state().players[me];
        PokemonSlot& target = game.slot(me, a.target);
        target.tool = p.hand.take(a.index);
        game.say("P", me + 1, " attaches ", target.tool->name, " to ", target.card->name);
    }
};
}  // namespace

const MoveRule& playTrainerRule() {
    static const PlayTrainer rule;
    return rule;
}

const MoveRule& attachToolRule() {
    static const AttachTool rule;
    return rule;
}
