#include "cards/deck.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>

#include "core/rules.h"

namespace {
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r");
    auto e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}
}  // namespace

Deck loadDeck(const std::string& path, const CardDb& db) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open " + path);
    Deck deck;
    deck.name = path;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        auto where = path + ":" + std::to_string(lineNo) + ": ";
        if (line.starts_with("energy:")) {
            std::istringstream types(line.substr(7));
            std::string name;
            while (std::getline(types, name, ',')) {
                auto t = typeFromName(trim(name));
                if (!t) throw std::runtime_error(where + "unknown energy type '" + trim(name) + "'");
                deck.energyTypes.push_back(*t);
            }
            continue;
        }
        std::istringstream parts(line);
        int count = 0;
        std::string id;
        if (!(parts >> count >> id) || count < 1) throw std::runtime_error(where + "expected '<count> <card id>'");
        const CardDef* card = db.find(id);
        if (!card) throw std::runtime_error(where + "unknown card id " + id);
        deck.cards.insert(deck.cards.end(), count, card);
    }
    validateDeck(deck);
    return deck;
}

void validateDeck(const Deck& deck) {
    auto fail = [&](const std::string& why) { throw std::runtime_error(deck.name + ": " + why); };
    if (deck.cards.size() != rules::DeckSize)
        fail("has " + std::to_string(deck.cards.size()) + " cards, needs " + std::to_string(rules::DeckSize));
    std::map<std::string, int> copies;
    for (const CardDef* c : deck.cards) {
        if (!c->supported) fail(c->name + " (" + c->id + ") isn't supported yet: " + c->unsupported);
        if (++copies[c->name] > rules::MaxCopies)
            fail("more than " + std::to_string(rules::MaxCopies) + " copies of " + c->name);
    }
    if (std::none_of(deck.cards.begin(), deck.cards.end(), [](const CardDef* c) { return c->isBasic(); }))
        fail("has no Basic Pokémon");
    auto& types = deck.energyTypes;
    if (types.empty() || types.size() > rules::MaxEnergyTypes)
        fail("needs 1 to " + std::to_string(rules::MaxEnergyTypes) + " energy types");
    for (std::size_t i = 0; i < types.size(); ++i) {
        if (types[i] == Type::Colorless || types[i] == Type::Dragon)
            fail(std::string(typeName(types[i])) + " can't be an energy type");
        if (std::find(types.begin(), types.begin() + i, types[i]) != types.begin() + i)
            fail(std::string("energy type ") + typeName(types[i]) + " listed twice");
    }
}
