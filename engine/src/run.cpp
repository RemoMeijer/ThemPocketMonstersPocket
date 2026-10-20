// tcgp-run: many games between decks on all threads. Two decks are one pairing; more decks are every ordered pair
// (deck i as deck A against deck j), the full matrix. Run from the project folder:
//   tcgp-run [--games N] [--threads T] [--p0 random|heuristic] [--p1 random|heuristic] [--csv FILE] deck1.txt deck2.txt …
// --p0 plays deck A and --p1 deck B (heuristic by default). Seats swap every game, as in tcgp --match.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game/game.h"
#include "players/heuristic.h"

namespace {
struct Tally {  // deck A's results in one pairing; threads add to it
    std::atomic<long> wins{0}, draws{0};
};

struct Rate {
    double p, lo, hi;
};

Rate winRate(long wins, long games) {  // normal approximation of the 95% interval
    double p = double(wins) / games, half = 1.96 * std::sqrt(p * (1 - p) / games);
    return {p, std::max(0.0, p - half), std::min(1.0, p + half)};
}

std::string shortName(const Deck& d) { return std::filesystem::path(d.name).stem().string(); }
}  // namespace

int main(int argc, char** argv) {
    long games = 1000;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::string p0 = "heuristic", p1 = "heuristic", csv;
    std::vector<std::string> paths;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            bool hasValue = i + 1 < argc;
            if (a == "--games" && hasValue) games = std::stol(argv[++i]);
            else if (a == "--threads" && hasValue) threads = unsigned(std::stoul(argv[++i]));
            else if (a == "--p0" && hasValue) p0 = argv[++i];
            else if (a == "--p1" && hasValue) p1 = argv[++i];
            else if (a == "--csv" && hasValue) csv = argv[++i];
            else if (a.starts_with("--")) throw std::runtime_error("unknown option " + a);
            else paths.push_back(a);
        }
        if (paths.size() < 2 || games < 1 || threads < 1) throw std::runtime_error("need two decks or more");
        makePolicy(p0), makePolicy(p1);  // fail early on an unknown player name
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\nusage: tcgp-run [--games N] [--threads T] [--p0 random|heuristic] "
                  << "[--p1 random|heuristic] [--csv FILE] deck1.txt deck2.txt [deck3.txt …]\n";
        return 2;
    }
    try {
        const std::string effects = "data/effects.tsv";
        const CardDb db = CardDb::load("data/cards.tsv", std::filesystem::exists(effects) ? effects : "");
        std::vector<Deck> decks;
        for (const std::string& p : paths) decks.push_back(loadDeck(p, db));
        std::vector<std::pair<int, int>> pairings;  // (deck A, deck B)
        if (decks.size() == 2) pairings.push_back({0, 1});
        else
            for (int i = 0; i < int(decks.size()); ++i)
                for (int j = 0; j < int(decks.size()); ++j)
                    if (i != j) pairings.push_back({i, j});

        // Every game is one (pairing, game) number; threads take the next one until all are played. The CardDb and
        // decks are shared read-only; each thread has its own players, and each Game its own state and rng.
        std::vector<Tally> tallies(pairings.size());
        std::atomic<long> next{0};
        const long total = long(pairings.size()) * games;
        std::exception_ptr failure;
        std::mutex failureLock;
        auto work = [&] {
            try {
                std::unique_ptr<Policy> pa = makePolicy(p0), pb = makePolicy(p1);
                for (long k; (k = next++) < total;) {
                    auto [a, b] = pairings[std::size_t(k / games)];
                    bool aFirst = k % games % 2 == 0;  // deck A sits in seat 0 every other game
                    Game game(decks[aFirst ? a : b], decks[aFirst ? b : a], aFirst ? *pa : *pb, aFirst ? *pb : *pa);
                    int w = game.run().winner;
                    Tally& t = tallies[std::size_t(k / games)];
                    if (w < 0) ++t.draws;
                    else if ((w == 0) == aFirst) ++t.wins;
                }
            } catch (...) {
                std::lock_guard lock(failureLock);
                if (!failure) failure = std::current_exception();
                next = total;  // the others stop too
            }
        };
        auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work);
        for (std::thread& t : pool) t.join();
        if (failure) std::rethrow_exception(failure);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        std::ofstream out;
        if (!csv.empty()) {
            out.open(csv);
            if (!out) throw std::runtime_error("cannot write " + csv);
            out << "deckA,deckB,games,wins,draws,losses,winrate,lo,hi\n";
        }
        std::vector<std::vector<double>> rate(decks.size(), std::vector<double>(decks.size(), -1));
        for (std::size_t i = 0; i < pairings.size(); ++i) {
            auto [a, b] = pairings[i];
            long wins = tallies[i].wins, draws = tallies[i].draws;
            Rate r = winRate(wins, games);
            rate[std::size_t(a)][std::size_t(b)] = r.p;
            std::printf("%s vs %s: wins %ld, draws %ld, losses %ld; win rate %.1f%% (%.1f-%.1f%%)\n", shortName(decks[a]).c_str(),
                        shortName(decks[b]).c_str(), wins, draws, games - wins - draws, 100 * r.p, 100 * r.lo, 100 * r.hi);
            if (out.is_open()) out << decks[a].name << ',' << decks[b].name << ',' << games << ',' << wins << ',' << draws << ','
                         << games - wins - draws << ',' << r.p << ',' << r.lo << ',' << r.hi << '\n';
        }
        if (decks.size() > 2) {  // win rates in %, deck A down the side; avg: its mean over all opponents
            std::printf("\n%-16s avg ", "deck A \\ B");
            for (std::size_t j = 0; j < decks.size(); ++j) std::printf("%4zu", j + 1);
            std::printf("\n");
            for (std::size_t i = 0; i < decks.size(); ++i) {
                double sum = 0;
                for (std::size_t j = 0; j < decks.size(); ++j) sum += i == j ? 0 : rate[i][j];
                std::printf("%2zu %-13.13s %4.0f ", i + 1, shortName(decks[i]).c_str(), 100 * sum / double(decks.size() - 1));
                for (std::size_t j = 0; j < decks.size(); ++j)
                    i == j ? std::printf("   -") : std::printf("%4.0f", 100 * rate[i][j]);
                std::printf("\n");
            }
        }
        std::printf("\n%ld games in %.2f s on %u threads: %.0f games/s\n", total, seconds, threads, double(total) / seconds);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
