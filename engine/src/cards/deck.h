#pragma once
#include <string>
#include <vector>

#include "cards/card.h"

struct Deck {
    std::string name;
    std::vector<const CardDef*> cards;
    std::vector<Type> energyTypes;  // what the Energy Zone can give
};

// Deck file: "energy: Water" (or "energy: Water, Fire"), then lines like "2 A1-053". "#" starts a comment.
Deck loadDeck(const std::string& path, const CardDb& db);

// Throws std::runtime_error naming the broken rule.
void validateDeck(const Deck& deck);
