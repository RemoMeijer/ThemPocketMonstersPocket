#pragma once
#include <atomic>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <semaphore>
#include <string>
#include <thread>
#include <vector>

#include "cards/deck.h"
#include "ml/encode.h"

struct GameResult {
    long game = 0;       // id, as in BatchEnv::batchGames()
    int decks[2] = {};   // index into the deck list, per seat
    int winner = -1;     // seat, -1 = draw
    int turns = 0;
    int external = -1;   // the seat the external player had; -1 = both
};

// Many games at once, each on its own thread, where an outside decision-maker (a network in Python) plays one side
// or both. A game that needs its decision encodes it into a pending slot and sleeps until answer(). Decisions with
// one legal action are made in place, so every pending decision has a choice. Two decks: always deck 0 against
// deck 1; more: a random pair (mirrors too). The external side plays the first deck of the pair, from seat 0 and
// seat 1 by turns. Not thread-safe: one caller thread drives waitBatch/answer.
class BatchEnv {
public:
    enum class Side { Player0, Both };  // Player0: one player, the other is `opponent` ("heuristic" or "random")

    BatchEnv(const CardDb& db, std::vector<Deck> decks, int threads, int gamesInFlight, Side external,
             const std::string& opponent = "heuristic");
    ~BatchEnv() { stop(); }

    // Waits until minBatch games wait for a decision (or timeoutMs passed); returns how many wait. Their
    // observations are batch()[0..n), valid until answer(). Throws what a game thread threw.
    int waitBatch(int minBatch, int timeoutMs);
    const std::vector<enc::Observation>& batch() const { return batch_; }
    const std::vector<long>& batchGames() const { return batchGames_; }  // game id per pending decision
    const std::vector<int>& batchSeats() const { return batchSeats_; }   // seat per pending decision
    // One index per pending decision, in order, into its legal actions. A bad one takes the first action.
    void answer(const std::vector<int>& actionIndices);
    std::vector<GameResult> takeResults();  // the games finished since the last call
    void stop();                            // abandons the games in progress and joins every thread

    long decisions() const { return decisions_; }
    long badAnswers() const { return badAnswers_; }
    int maxActions() const { return maxActions_; }  // most legal actions in one decision so far
    long cutActions() const { return cutActions_; } // decisions with more than enc::Actions legal actions

private:
    struct Slot {
        enc::Observation obs;
        long game = 0;
        int seat = 0, answer = 0;
        bool waiting = false;
        std::binary_semaphore ready{0};
    };
    class QueuePolicy;
    void worker(int slot);
    std::size_t wait(int slot, const PlayerView& view, const std::vector<Action>& actions);

    std::vector<Deck> decks_;
    Side external_;
    std::string opponent_;
    std::vector<std::unique_ptr<Slot>> slots_;
    std::vector<std::thread> threads_;
    std::counting_semaphore<> cpu_;  // games running at once: at most `threads`; a sleeping game gives its turn up

    std::mutex m_;  // guards pending_, results_, failure_, stopping_ and Slot::waiting
    std::condition_variable cv_;
    std::vector<int> pending_;
    std::vector<GameResult> results_;
    std::exception_ptr failure_;
    bool stopping_ = false;

    std::vector<int> batchSlots_;
    std::vector<enc::Observation> batch_;
    std::vector<long> batchGames_;
    std::vector<int> batchSeats_;

    std::atomic<long> nextGame_{0}, decisions_{0}, badAnswers_{0}, cutActions_{0};
    std::atomic<int> maxActions_{0};
};
