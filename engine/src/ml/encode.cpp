#include "ml/encode.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <unordered_map>

namespace enc {
namespace {
// fn(word) for every unquoted atom of a script text that isn't a number: `heal`, `:scope`, `<=` … Quoted atoms are
// card names, and a bag of numbers says little.
template <class Fn>
void forEachWord(const std::string& s, Fn fn) {
    for (std::size_t i = 0; i < s.size();) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')') {
            ++i;
        } else if (c == '"') {
            std::size_t end = s.find('"', i + 1);
            i = end == std::string::npos ? s.size() : end + 1;
        } else {
            std::size_t start = i;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '(' && s[i] != ')') ++i;
            std::string w = s.substr(start, i - start);
            if (w.find_first_not_of("-0123456789") != std::string::npos) fn(w);
        }
    }
}
}  // namespace

CardTable staticFeatures(const CardDb& db) {
    CardTable t;
    for (const CardDef& c : db.all()) forEachWord(c.scripts, [&](const std::string& w) { t.words.push_back(w); });
    std::sort(t.words.begin(), t.words.end());
    t.words.erase(std::unique(t.words.begin(), t.words.end()), t.words.end());
    std::unordered_map<std::string, int> column;
    for (std::size_t i = 0; i < t.words.size(); ++i) column[t.words[i]] = CardFixedFeatures + int(i);

    t.rows = int(db.all().size()) + 1;
    t.cols = CardFixedFeatures + int(t.words.size());
    t.values.assign(std::size_t(t.rows) * std::size_t(t.cols), 0.f);
    for (const CardDef& c : db.all()) {
        t.ids.push_back(c.id);
        float* f = &t.values[std::size_t(cardIndex(&c)) * std::size_t(t.cols)];
        f[CatPokemon + (c.isPokemon() ? 0 : int(c.trainer))] = 1;  // TrainerKind: Item 1 … Stadium 4
        f[CatFossil] = c.isFossil();
        if (c.isPokemon()) {
            f[CatStage + int(c.stage)] = 1;
            f[CatPoints] = c.points / 3.f;
        }
        f[CatEx] = c.ex;
        f[CatMega] = c.mega;
        f[CatHp] = c.hp / 100.f;
        if (c.type) f[CatType + int(*c.type)] = 1;
        if (c.weakness) f[CatWeakness + int(*c.weakness)] = 1;
        f[CatRetreat] = c.retreat / 4.f;
        for (std::size_t i = 0; i < c.attacks.size() && i < 2; ++i) {
            const AttackDef& a = c.attacks[i];
            float* g = f + CatAttack + int(i) * AttackColumns;
            for (int e = 0; e < TypeCount; ++e) g[e] = a.cost[e] / 4.f;
            g[10] = a.damage / 100.f;
            if (!a.damageText.empty()) {
                char last = a.damageText.back();
                g[11 + (last == '+' ? 1 : last == 'x' ? 2 : last == '-' ? 3 : 0)] = 1;
            }
            g[15] = !a.effect.empty();
        }
        f[CatAbility] = !c.abilities.empty();
        for (int b = 0; b < 3; ++b) f[CatTraits + b] = (c.traits >> b) & 1;
        forEachWord(c.scripts, [&](const std::string& w) { f[column[w]] = std::min(1.f, f[column[w]] + 1 / 3.f); });
    }
    return t;
}

bool encode(const PlayerView& v, const std::vector<Action>& actions, Observation& o) {
    assert(v.hasRules());
    o = Observation{};
    const int me = v.me(), opp = v.opp(), turn = v.turn();
    auto token = [&](int t, TokenType type, const CardDef* card) {
        o.tokenType[t] = type;
        o.tokenCard[t] = cardIndex(card);
        o.tokenMask[t] = 1;
        return o.tokenFloat[t].data();
    };

    float* g = token(TokGlobal, TGlobal, nullptr);
    g[GTurn] = turn / float(rules::TurnCap);
    for (int k = 0; k < 2; ++k) {
        int p = k ? opp : me;
        g[GPoints + k] = v.points(p) / float(rules::PointsToWin);
        g[GHand + k] = v.handSize(p) / 10.f;
        g[GDeck + k] = v.deckSize(p) / 20.f;
        g[GDiscard + k] = v.discard(p).size() / 20.f;
        for (int e = 0; e < TypeCount; ++e) g[GDiscardEnergy + TypeCount * k + e] = v.discardEnergy(p)[e] / 10.f;
    }
    if (auto z = v.zoneEnergy()) g[GZone + int(*z)] = 1;
    if (auto z = v.nextEnergy()) g[GNext + int(*z)] = 1;
    g[GSupporterUsed] = v.supporterPlayed();
    g[GRetreated] = v.retreated();
    g[GNoEnergy] = !v.zoneEnergy();
    g[GOwnFirstTurn] = v.ownFirstTurn();
    g[GMyTurn] = v.current() == me;

    const auto& hand = v.hand();
    for (std::size_t i = 0; i < hand.size() && i < std::size_t(HandTokens); ++i) token(TokHand + int(i), THand, hand[i]);

    for (int k = 0; k < 2 && !(k && turn == 0); ++k) {  // during Setup the opponent's Pokémon are still face down
        int owner = k ? opp : me, base = k ? TokOpp : TokMine;
        auto pokemon = [&](int t, const PokemonSlot& p, bool active) {
            float* f = token(t, active ? (k ? TOppActive : TMyActive) : (k ? TOppBench : TMyBench), p.card);
            if (!p.tools.empty()) o.tokenTool[t] = cardIndex(p.tools[0]);  // shortcut: a second Tool (Revavroom) isn't shown
            f[PHpLeft] = v.hpLeft(p, owner) / 100.f;
            f[PDamage] = p.damage / 100.f;
            for (int e = 0; e < TypeCount; ++e) f[PEnergy + e] = p.energy[e] / 5.f;
            for (int b = 0; b < 5; ++b) f[PStatus + b] = (p.status >> b) & 1;
            std::size_t attacks = v.attacks(p, owner).size();
            for (std::size_t i = 0; i < attacks && i < 2; ++i) f[PCanAttack + int(i)] = v.canAttack(p, owner, int(i));
            f[PEntered] = p.enteredTurn == turn;
            f[PEvolved] = p.enteredTurn == turn && !p.below.empty();
            f[PAbilityUsed] = p.abilityTurn == turn;
            f[PIsActive] = active;
        };
        if (v.active(owner).card) pokemon(base, v.active(owner), true);
        for (const PokemonSlot& b : v.bench(owner)) pokemon(base + 1 + b.spot, b, false);
    }
    if (v.stadium()) {
        float* s = token(TokStadium, TStadium, v.stadium());
        s[SMine] = v.stadiumOwner() == me;
        s[SUsed] = v.stadiumUsed();
    }

    // Token of one of my Pokémon by action target (-1 = the Active, else a bench position), or of any Pokémon by uid.
    const auto& bench = v.bench(me);
    auto mine = [&](int target) { return target < 0 ? TokMine : TokMine + 1 + bench[std::size_t(target)].spot; };
    auto byUid = [&](uint16_t uid) {
        for (int k = 0; uid && k < 2; ++k) {
            int owner = k ? opp : me, base = k ? TokOpp : TokMine;
            if (v.active(owner).uid == uid && v.active(owner).card) return base;
            for (const PokemonSlot& b : v.bench(owner))
                if (b.uid == uid) return base + 1 + b.spot;
        }
        return -1;
    };
    o.legalActions = int(actions.size());
    for (int i = 0; i < std::min(Actions, o.legalActions); ++i) {
        const Action& a = actions[std::size_t(i)];
        float* f = o.actionFloat[i].data();
        o.actionKind[i] = int(a.kind) + 1;
        o.actionMask[i] = 1;
        o.actionPtr[i] = o.actionPtr2[i] = -1;
        auto handCard = [&] {
            o.actionCard[i] = cardIndex(hand[std::size_t(a.index)]);
            if (a.index < HandTokens) o.actionPtr[i] = TokHand + a.index;
        };
        switch (a.kind) {
            case ActionKind::PlaceActive:
            case ActionKind::PlaceBench:
            case ActionKind::PlayTrainer:
                handCard();
                break;
            case ActionKind::Evolve:
            case ActionKind::AttachTool:
                handCard();
                o.actionPtr2[i] = mine(a.target);
                break;
            case ActionKind::AttachEnergy:
            case ActionKind::UseAbility:
                o.actionPtr[i] = mine(a.target);
                break;
            case ActionKind::UseStadium:
                o.actionPtr[i] = TokStadium;
                break;
            case ActionKind::Retreat:
                o.actionPtr[i] = mine(a.index);
                break;
            case ActionKind::Attack:
                o.actionPtr[i] = TokMine;
                f[AAttack + std::min(int(a.index), 7)] = 1;
                break;
            case ActionKind::EndTurn:
                break;
            case ActionKind::Choose:
                o.actionCard[i] = cardIndex(a.card);
                o.actionPtr[i] = byUid(a.uid);
                f[AChoice + int(a.choice)] = 1;
                if (a.type >= 0) f[AEnergyType + a.type] = 1;
                f[AOption] = a.index / 10.f;
                break;
        }
    }
    return o.legalActions <= Actions;
}
}  // namespace enc
