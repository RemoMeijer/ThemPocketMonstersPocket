// BatchEnv checks: an outside loop of waitBatch/answer with random legal indices plays complete games over many
// threads without hanging, the results make sense, and stop() ends games in flight. Uses every deck in decks/,
// so run it from the project folder. Prints one line of speed. test_batch_env [games]: games for the speed line (500).
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <random>
#include <set>

#include "ml/batch_env.h"
#include "test_support.h"

namespace {
using Clock = std::chrono::steady_clock;
double since(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Answers with random legal indices until `games` games finished; returns them. Fails a check after 60 s without
// a decision.
std::vector<GameResult> play(BatchEnv& env, long games, std::mt19937& rng, bool oneBadAnswer = false) {
    std::vector<GameResult> results;
    auto lastDecision = Clock::now();
    while (long(results.size()) < games) {
        int n = env.waitBatch(32, 100);
        if (!n && since(lastDecision) > 60) {
            std::cerr << "no decision for 60 s: a hang\n";
            ++failures;
            break;
        }
        std::vector<int> picks(std::size_t(n), 0);
        for (int i = 0; i < n; ++i) {
            const enc::Observation& o = env.batch()[std::size_t(i)];
            int legal = std::min(o.legalActions, enc::Actions);
            CHECK(legal >= 2 && std::count(o.actionMask.begin(), o.actionMask.end(), 1) == legal);
            CHECK(o.tokenMask[enc::TokGlobal] == 1 && env.batchSeats()[std::size_t(i)] >= 0 && env.batchSeats()[std::size_t(i)] <= 1);
            picks[std::size_t(i)] = std::uniform_int_distribution<int>(0, legal - 1)(rng);
        }
        if (oneBadAnswer && n) picks[0] = -1, oneBadAnswer = false;
        if (n) env.answer(picks), lastDecision = Clock::now();
        for (const GameResult& r : env.takeResults()) results.push_back(r);
    }
    return results;
}

void sane(const std::vector<GameResult>& results, int decks, bool both) {
    std::set<long> ids;
    for (const GameResult& r : results) {
        CHECK(r.winner >= -1 && r.winner <= 1 && r.turns >= 1 && r.turns <= rules::TurnCap);
        CHECK(r.decks[0] >= 0 && r.decks[0] < decks && r.decks[1] >= 0 && r.decks[1] < decks);
        CHECK(both ? r.external == -1 : r.external == int(r.game % 2));
        ids.insert(r.game);
    }
    CHECK(ids.size() == results.size());
}
}  // namespace

int main(int argc, char** argv) {
    long games = argc > 1 ? std::atol(argv[1]) : 500;
    const CardDb db = CardDb::load("data/cards.tsv", "data/effects.tsv");
    std::vector<Deck> decks;
    for (const auto& e : std::filesystem::directory_iterator("decks"))
        if (e.path().extension() == ".txt") decks.push_back(loadDeck(e.path().string(), db));
    std::mt19937 rng{std::random_device{}()};

    {  // the network plays both sides: the speed line
        BatchEnv env(db, decks, 8, 128, BatchEnv::Side::Both);
        auto start = Clock::now();
        std::vector<GameResult> results = play(env, games, rng);
        double seconds = since(start);
        long decisions = env.decisions();
        env.stop();
        sane(results, int(decks.size()), true);
        CHECK(env.badAnswers() == 0 && env.cutActions() == 0);
        std::printf("batch env, both sides external, 8 threads, 128 in flight: %zu games, %ld decisions in %.2f s: "
                    "%.0f decisions/s, %.0f games/s, max %d legal actions\n",
                    results.size(), decisions, seconds, decisions / seconds, results.size() / seconds, env.maxActions());
    }
    {  // one side against the heuristic, one bad answer, and stopped with games in flight
        BatchEnv env(db, decks, 8, 64, BatchEnv::Side::Player0, "heuristic");
        std::vector<GameResult> results = play(env, 100, rng, true);
        sane(results, int(decks.size()), false);
        CHECK(env.badAnswers() == 1);
        CHECK(env.waitBatch(1, 5000) > 0);  // left unanswered: stop() must still wake and join it
    }
    return report("batch env");
}
