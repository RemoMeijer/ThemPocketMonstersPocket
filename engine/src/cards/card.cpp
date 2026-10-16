#include "cards/card.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace {
constexpr const char* Names[TypeCount] = {"Grass",    "Fire",     "Water", "Lightning", "Psychic",
                                          "Fighting", "Darkness", "Metal", "Dragon",    "Colorless"};
constexpr const char* Letters = "GRWLPFDMNC";  // same order as Type

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out(1);
    for (char c : s) {
        if (c == sep)
            out.emplace_back();
        else
            out.back() += c;
    }
    return out;
}

bool isNumber(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// The number at the start of a damage text: 30 in "30", "30+" and "30×".
int leadingNumber(const std::string& s) {
    std::size_t n = 0;
    while (n < s.size() && std::isdigit(static_cast<unsigned char>(s[n]))) ++n;
    return n ? std::stoi(s.substr(0, n)) : 0;
}

Type requireType(char letter, const std::string& line) {
    auto t = typeFromLetter(letter);
    if (!t) throw std::runtime_error("unknown type letter '" + std::string(1, letter) + "' in: " + line);
    return *t;
}

void readStage(CardDef& c, const std::string& stage, const std::string& line) {
    if (c.category == Category::Pokemon) {
        if (stage == "Basic")
            c.stage = Stage::Basic;
        else if (stage == "Stage1")
            c.stage = Stage::Stage1;
        else if (stage == "Stage2")
            c.stage = Stage::Stage2;
        else
            throw std::runtime_error("unknown stage '" + stage + "' in: " + line);
        return;
    }
    if (stage == "Item")
        c.trainer = TrainerKind::Item;
    else if (stage == "Supporter")
        c.trainer = TrainerKind::Supporter;
    else if (stage == "Tool")
        c.trainer = TrainerKind::Tool;
    else if (stage == "Stadium")
        c.trainer = TrainerKind::Stadium;
    else
        throw std::runtime_error("unknown trainer kind '" + stage + "' in: " + line);
}

CardDef readCard(const std::string& line) {
    auto f = split(line, '\t');
    if (f.size() != 13) throw std::runtime_error("expected 13 columns in: " + line);
    CardDef c;
    c.id = f[0];
    c.name = f[1];
    if (f[2] == "Trainer")
        c.category = Category::Trainer;
    else if (f[2] != "Pokemon")
        throw std::runtime_error("unknown category '" + f[2] + "' in: " + line);
    readStage(c, f[3], line);
    c.hp = isNumber(f[4]) ? std::stoi(f[4]) : 0;
    if (!f[5].empty()) c.type = requireType(f[5][0], line);
    if (!f[6].empty()) c.weakness = requireType(f[6][0], line);
    c.retreat = isNumber(f[7]) ? std::stoi(f[7]) : 0;
    c.ex = f[8] == "EX";
    c.mega = c.ex && c.name.starts_with("Mega ");
    c.points = c.mega ? 3 : c.ex ? 2 : 1;
    c.evolvesFrom = f[9];
    if (!f[10].empty()) {
        for (const auto& text : split(f[10], '|')) {
            auto p = split(text, '~');
            if (p.size() != 4) throw std::runtime_error("bad attack '" + text + "' in: " + line);
            AttackDef a;
            a.name = p[0];
            for (char letter : p[1]) a.cost[int(requireType(letter, line))]++;
            a.damageText = p[2];
            a.damage = leadingNumber(p[2]);
            a.effect = p[3];
            c.attacks.push_back(std::move(a));
        }
    }
    if (!f[11].empty()) {
        for (const auto& text : split(f[11], '|')) {
            auto p = split(text, '~');
            if (p.size() != 2) throw std::runtime_error("bad ability '" + text + "' in: " + line);
            c.abilities.push_back({p[0], p[1]});
        }
    }
    c.effect = f[12];
    return c;
}

// Every piece of card text needs a script before the engine can play the card.
void markSupported(CardDef& c) {
    auto no = [&](const std::string& why) {
        c.supported = false;
        c.unsupported = why;
    };
    c.supported = true;
    if (!c.scriptError.empty()) return no(c.scriptError);
    for (const AttackDef& a : c.attacks) {
        if (!a.effect.empty() && !a.script) return no("attack text: " + a.effect);
        if (!a.script && !a.damageText.empty() && !isNumber(a.damageText))
            return no("attack damage '" + a.damageText + "' without a script");
    }
    bool abilityDone = c.activated || c.endOfTurn || c.onAttach || c.onEvolve || !c.passive.empty();
    if (!c.abilities.empty() && !abilityDone) return no("ability text: " + c.abilities[0].effect);
    if (c.category == Category::Trainer && !c.play && !c.endOfTurn && c.passive.empty()) return no("trainer text: " + c.effect);
    if (c.isPokemon() && !c.effect.empty() && c.passive.empty()) return no("card text: " + c.effect);  // Fossils
    if (c.isPokemon() && c.attacks.empty() && c.passive.empty()) return no("no attacks");
}
}  // namespace

std::optional<Type> typeFromLetter(char letter) {
    for (int i = 0; i < TypeCount; ++i)
        if (Letters[i] == letter) return Type(i);
    return std::nullopt;
}

std::optional<Type> typeFromName(const std::string& name) {
    for (int i = 0; i < TypeCount; ++i) {
        std::string n = Names[i];
        if (name == n) return Type(i);
        n[0] = char(std::tolower(static_cast<unsigned char>(n[0])));
        if (name == n) return Type(i);
    }
    return std::nullopt;
}

const char* typeName(Type t) { return Names[int(t)]; }
char typeLetter(Type t) { return Letters[int(t)]; }

bool canPay(const EnergyCounts& attached, const EnergyCounts& cost) {
    int spare = 0;
    for (int t = 0; t < TypeCount; ++t) {
        if (t == int(Type::Colorless)) continue;
        if (attached[t] < cost[t]) return false;
        spare += attached[t] - cost[t];
    }
    return spare + attached[int(Type::Colorless)] >= cost[int(Type::Colorless)];
}

CardDb CardDb::load(const std::string& cards, const std::string& scripts) {
    std::ifstream in(cards);
    if (!in) throw std::runtime_error("cannot open " + cards);
    CardDb db;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        CardDef c = readCard(line);
        db.byId_[c.id] = db.cards_.size();
        db.cards_.push_back(std::move(c));
    }
    // Rare Candy needs a Stage 2's Basic, two steps down.
    std::unordered_map<std::string, std::string> parentOf;
    for (const CardDef& c : db.cards_)
        if (c.stage == Stage::Stage1) parentOf.emplace(c.name, c.evolvesFrom);
    for (CardDef& c : db.cards_)
        if (c.stage == Stage::Stage2 && parentOf.count(c.evolvesFrom)) c.evolvesFromBasic = parentOf[c.evolvesFrom];
    if (!scripts.empty()) db.attachScripts(scripts);
    for (CardDef& c : db.cards_) markSupported(c);
    return db;
}

const CardDef* CardDb::find(const std::string& id) const {
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : &cards_[it->second];
}

std::size_t CardDb::supportedCount() const {
    return std::count_if(cards_.begin(), cards_.end(), [](const CardDef& c) { return c.supported; });
}
