// tcgp <deck.txt> [opponent-deck.txt]   plays one game and prints it (a mirror match with one deck)
// tcgp --coverage                        how many cards the engine can play, and the commonest reasons it can't
// tcgp --unsupported                     every card the engine can't play, with its full reason
// tcgp --match N deckA deckB [random|heuristic] [random|heuristic]
//                                        N games, deckA's wins, draws and losses (players default to random)
// Run from the project folder: it reads data/cards.tsv and, when present, data/effects.tsv.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "game/game.h"
#include "players/heuristic.h"

namespace {
CardDb loadCards() {
    const std::string effects = "data/effects.tsv";
    return CardDb::load("data/cards.tsv", std::filesystem::exists(effects) ? effects : "");
}

const char* kindName(const CardDef& c) {
    if (c.isPokemon()) return "Pokemon";
    switch (c.trainer) {
        case TrainerKind::Item: return "Item";
        case TrainerKind::Supporter: return "Supporter";
        case TrainerKind::Tool: return "Tool";
        default: return "Stadium";
    }
}

void coverage(const CardDb& db) {
    std::map<std::string, std::pair<int, int>> byKind;  // supported, total
    std::map<std::string, int> reasons;
    for (const CardDef& c : db.all()) {
        auto& [ok, total] = byKind[kindName(c)];
        ++total;
        if (c.supported) {
            ++ok;
            continue;
        }
        std::string why = c.unsupported.substr(0, 110);
        for (char& ch : why)
            if (std::isdigit(static_cast<unsigned char>(ch))) ch = 'N';
        ++reasons[why];
    }
    int ok = 0, total = 0;
    for (const auto& [kind, counts] : byKind) {
        std::cout << kind << ": " << counts.first << " / " << counts.second << "\n";
        ok += counts.first;
        total += counts.second;
    }
    std::cout << "all: " << ok << " / " << total << " (" << 100 * ok / std::max(total, 1) << "%)\n\nmost common reasons:\n";
    std::vector<std::pair<int, std::string>> top;
    for (const auto& [why, n] : reasons) top.push_back({n, why});
    std::sort(top.rbegin(), top.rend());
    for (std::size_t i = 0; i < top.size() && i < 30; ++i) std::cout << "  " << top[i].first << "  " << top[i].second << "\n";
}

// N games of deckA against deckB, swapping seats every game (the coin still decides who goes first).
int match(const CardDb& db, int argc, char** argv) {
    int n = std::stoi(argv[2]);
    Deck a = loadDeck(argv[3], db), b = loadDeck(argv[4], db);
    std::unique_ptr<Policy> pa = makePolicy(argc > 5 ? argv[5] : "random"), pb = makePolicy(argc > 6 ? argv[6] : "random");
    long won = 0, drawn = 0;
    for (int g = 0; g < n; ++g) {
        bool aFirst = g % 2 == 0;
        Game game(aFirst ? a : b, aFirst ? b : a, aFirst ? *pa : *pb, aFirst ? *pb : *pa);
        int w = game.run().winner;
        if (w < 0) ++drawn;
        else if ((w == 0) == aFirst) ++won;
    }
    double p = double(won) / n, half = 1.96 * std::sqrt(p * (1 - p) / n);  // normal approximation
    std::cout << a.name << " vs " << b.name << ", " << n << " games: wins " << won << ", draws " << drawn << ", losses "
              << n - won - drawn << "; win rate " << 100 * p << "% +- " << 100 * half << "% (95%)\n";
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    bool isMatch = argc >= 5 && argc <= 7 && std::string(argv[1]) == "--match";
    if ((argc < 2 || argc > 3) && !isMatch) {
        std::cerr << "usage: tcgp <deck.txt> [opponent-deck.txt]\n       tcgp --coverage | --unsupported\n"
                     "       tcgp --match N deckA deckB [random|heuristic] [random|heuristic]\n";
        return 2;
    }
    try {
        CardDb db = loadCards();
        if (isMatch) return match(db, argc, argv);
        if (std::string(argv[1]) == "--unsupported") {
            for (const CardDef& c : db.all())
                if (!c.supported) std::cout << c.id << "\t" << c.name << "\t" << c.unsupported << "\n";
            return 0;
        }
        if (std::string(argv[1]) == "--coverage") {
            coverage(db);
            return 0;
        }
        std::cout << db.all().size() << " cards loaded, " << db.supportedCount() << " supported by the engine\n";
        Deck deck0 = loadDeck(argv[1], db);
        Deck deck1 = loadDeck(argc > 2 ? argv[2] : argv[1], db);
        std::cout << "P1: " << deck0.name << "   P2: " << deck1.name << "\n\n";
        RandomPolicy p1, p2;
        PrintLog log;
        Game game(deck0, deck1, p1, p2, &log);
        Result r = game.run();
        std::cout << "\nResult: " << (r.winner < 0 ? "draw" : "P" + std::to_string(r.winner + 1) + " wins")
                  << " after " << r.turns << " turns\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
