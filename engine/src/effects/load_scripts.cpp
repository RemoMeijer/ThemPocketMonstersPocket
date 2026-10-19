// Attaching effect scripts (data/effects.tsv from compile_effects.py) to the cards.
//
// One row per piece of card text: id, part, script. Parts and their top-level forms:
//   attack:N   a statement, run when attack N is used; (hit) marks where its damage is done
//   ability:N  (passive MOD…)                          always on
//              (activated [:active] STMT)              "Once during your turn, you may…" ([:active]: only while Active)
//              (at-end-of-turn [:active] STMT)         "At the end of your turn…"
//              (on-attach [:type T…] [:active] STMT)   "Whenever you attach a {T} Energy from your Energy Zone to this Pokémon…"
//              (on-evolve STMT)                        "When you play this Pokémon from your hand to evolve…"
//              (on-damaged STMT)                       "If this Pokémon is in the Active Spot and is damaged by an attack…"
//              (at-end-of-each-turn STMT)              "At the end of each turn…" (self = this Pokémon, player = its owner)
//   card       (play STMT)     an Item or Supporter
//              (tool MOD…)     a Tool: modifiers for the Pokémon it is attached to
//              (at-end-of-turn [:active] STMT)  a Tool that does something at the end of your turn (self = its Pokémon)
//              (on-damaged STMT), (at-end-of-each-turn STMT)  as for abilities, on a Tool's Pokémon (several forms allowed)
//              (stadium MOD…)  a Stadium: modifiers for every Pokémon in play unless a :scope says otherwise
//              (stadium-action STMT)       a Stadium: "Once during each player's turn, that player may…" (player = them)
//              (stadium-end-of-turn STMT)  a Stadium: "At the end of each player's turn, that player…" (player = them)
//              (passive MOD…)  a Fossil played as a Pokémon
#include <fstream>
#include <stdexcept>
#include <string>

#include "cards/card.h"
#include "effects/effect.h"

namespace {
std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out(1);
    for (char c : line) {
        if (c == '\t')
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

// Options before the statement: :active and :type T…. Returns the position of the statement.
std::size_t options(const SExpr& e, bool& whileActive, uint16_t& types) {
    std::size_t i = 0;
    for (; i < argCount(e); ++i) {
        const SExpr& a = arg(e, i);
        if (a.list) break;
        if (a.atom == ":active") {
            whileActive = true;
        } else if (a.atom == ":type") {
            while (i + 1 < argCount(e) && !arg(e, i + 1).list && arg(e, i + 1).atom[0] != ':') types |= typeBit(typeArg(arg(e, ++i)));
        } else {
            scriptFail("unknown option '" + a.atom + "'", e);
        }
    }
    if (i + 1 != argCount(e)) scriptFail("expected one statement after the options", e);
    return i;
}

void modifiers(CardDef& c, const SExpr& e, ScriptBuilder& b, ModScope scope) {
    for (std::size_t i = 0; i < argCount(e); ++i) c.passive.push_back(b.modifier(arg(e, i), scope));
}

void readAbility(CardDef& c, const SExpr& e, ScriptBuilder& b) {
    const std::string& form = e.head();
    if (form == "passive") return modifiers(c, e, b, ModScope::Self);
    bool whileActive = false;
    uint16_t types = 0;
    std::size_t at = options(e, whileActive, types);
    const Stmt* stmt = b.stmt(arg(e, at));
    if (form == "activated") {
        c.activated = stmt;
        c.activatedWhileActive = whileActive;
    } else if (form == "at-end-of-turn") {
        c.endOfTurn = stmt;
        c.endOfTurnWhileActive = whileActive;
    } else if (form == "on-attach") {
        c.onAttach = stmt;
        c.onAttachTypes = types;
        c.onAttachWhileActive = whileActive;
    } else if (form == "on-evolve") {
        c.onEvolve = stmt;
    } else if (form == "on-damaged") {
        c.onDamaged = stmt;
    } else if (form == "at-end-of-each-turn") {
        c.endOfEachTurn = stmt;
    } else {
        scriptFail("unknown ability form '" + form + "'", e);
    }
}

void readCardText(CardDef& c, const SExpr& e, ScriptBuilder& b) {
    const std::string& form = e.head();
    if (form == "play" && (c.isTrainer(TrainerKind::Item) || c.isTrainer(TrainerKind::Supporter))) {
        if (argCount(e) != 1) scriptFail("expected one statement", e);
        c.play = b.stmt(arg(e, 0));
    }
    else if (form == "tool" && c.isTrainer(TrainerKind::Tool))
        modifiers(c, e, b, ModScope::Self);
    else if ((form == "at-end-of-turn" || form == "on-damaged" || form == "at-end-of-each-turn") && c.isTrainer(TrainerKind::Tool))
        readAbility(c, e, b);
    else if (form == "stadium" && c.isTrainer(TrainerKind::Stadium))
        modifiers(c, e, b, ModScope::All);
    else if ((form == "stadium-action" || form == "stadium-end-of-turn") && c.isTrainer(TrainerKind::Stadium)) {
        if (argCount(e) != 1) scriptFail("expected one statement", e);
        (form == "stadium-action" ? c.stadiumAction : c.endOfTurn) = b.stmt(arg(e, 0));
    }
    else if (form == "passive" && c.isPokemon())
        modifiers(c, e, b, ModScope::Self);
    else
        scriptFail("'" + form + "' doesn't fit this card", e);
}
}  // namespace

void CardDb::attachScripts(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    ScriptBuilder b(nodes_);
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        auto f = splitTabs(line);
        if (f.size() != 3) throw std::runtime_error("expected 3 columns in " + path + ": " + line);
        auto it = byId_.find(f[0]);
        if (it == byId_.end()) continue;  // a card no longer in cards.tsv
        CardDef& c = cards_[it->second];
        const std::string& part = f[1];
        try {
            SExpr e = parseScript(f[2]);
            if (!e.list) scriptFail("a script must be a list", e);
            if (part.starts_with("attack:")) {
                std::size_t n = std::stoul(part.substr(7));
                if (n >= c.attacks.size()) scriptFail("no attack " + part, e);
                c.attacks[n].script = b.stmt(e);
            } else if (part.starts_with("ability:")) {
                readAbility(c, e, b);
            } else if (part == "card") {
                readCardText(c, e, b);
            } else {
                throw ScriptError("unknown part '" + part + "'");
            }
        } catch (const ScriptError& err) {
            c.scriptError = part + ": " + err.what();
        }
    }
}
