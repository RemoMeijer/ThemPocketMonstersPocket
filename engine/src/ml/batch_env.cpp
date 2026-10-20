#include "ml/batch_env.h"

#include <algorithm>
#include <chrono>
#include <random>
#include <stdexcept>
#include <utility>

#include "game/game.h"
#include "players/heuristic.h"

namespace {
struct Stopped {};  // thrown inside a game when the env stops: unwinds it
}  // namespace

class BatchEnv::QueuePolicy : public Policy {
public:
    QueuePolicy(BatchEnv& env, int slot) : env_(env), slot_(slot) {}
    std::size_t choose(const PlayerView& view, const std::vector<Action>& actions) override {
        return actions.size() == 1 ? 0 : env_.wait(slot_, view, actions);
    }

private:
    BatchEnv& env_;
    int slot_;
};

BatchEnv::BatchEnv(const CardDb&, std::vector<Deck> decks, int threads, int gamesInFlight, Side external,
                   const std::string& opponent)
    : decks_(std::move(decks)), external_(external), opponent_(opponent), cpu_(std::max(1, threads)) {
    if (decks_.empty() || gamesInFlight < 1) throw std::invalid_argument("BatchEnv needs decks and a game in flight");
    for (const Deck& d : decks_) validateDeck(d);
    makePolicy(opponent_);  // fail early on an unknown name
    batch_.resize(std::size_t(gamesInFlight));
    for (int i = 0; i < gamesInFlight; ++i) slots_.push_back(std::make_unique<Slot>());
    for (int i = 0; i < gamesInFlight; ++i) threads_.emplace_back(&BatchEnv::worker, this, i);
}

void BatchEnv::worker(int slot) {
    QueuePolicy ext(*this, slot);
    std::unique_ptr<Policy> other = makePolicy(opponent_);
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(0, int(decks_.size()) - 1);
    cpu_.acquire();
    try {
        for (;;) {
            {
                std::lock_guard lock(m_);
                if (stopping_) break;
            }
            long id = nextGame_++;
            int a = 0, b = 1;  // deck A (the external side's) and deck B
            if (decks_.size() != 2) a = pick(rng), b = pick(rng);
            int seatA = int(id % 2);  // deck A sits in seat 0 every other game
            int deck[2] = {seatA ? b : a, seatA ? a : b};
            Policy* players[2] = {&ext, &ext};
            if (external_ == Side::Player0) players[1 - seatA] = other.get();
            slots_[std::size_t(slot)]->game = id;
            Game game(decks_[std::size_t(deck[0])], decks_[std::size_t(deck[1])], *players[0], *players[1]);
            Result r = game.run();
            std::lock_guard lock(m_);
            results_.push_back({id, {deck[0], deck[1]}, r.winner, r.turns, external_ == Side::Both ? -1 : seatA});
        }
    } catch (const Stopped&) {
    } catch (...) {
        std::lock_guard lock(m_);
        if (!failure_) failure_ = std::current_exception();
        stopping_ = true;
        cv_.notify_all();
    }
    cpu_.release();
}

std::size_t BatchEnv::wait(int slot, const PlayerView& view, const std::vector<Action>& actions) {
    Slot& s = *slots_[std::size_t(slot)];
    if (!enc::encode(view, actions, s.obs)) ++cutActions_;
    for (int n = int(actions.size()), m = maxActions_; n > m && !maxActions_.compare_exchange_weak(m, n);) {}
    s.seat = view.me();
    {
        std::lock_guard lock(m_);
        if (stopping_) throw Stopped{};
        s.waiting = true;
        pending_.push_back(slot);
    }
    cv_.notify_one();
    cpu_.release();
    s.ready.acquire();
    cpu_.acquire();
    if (s.answer < 0) throw Stopped{};
    return std::size_t(s.answer);
}

int BatchEnv::waitBatch(int minBatch, int timeoutMs) {
    if (!batchSlots_.empty()) throw std::logic_error("waitBatch: answer the last batch first");
    std::unique_lock lock(m_);
    std::size_t want = std::size_t(std::clamp(minBatch, 1, int(slots_.size())));
    cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return pending_.size() >= want || failure_ || stopping_; });
    if (failure_) std::rethrow_exception(failure_);
    batchSlots_.swap(pending_);
    lock.unlock();
    // The games in batchSlots_ sleep until answer(), so their slots hold still.
    batchGames_.resize(batchSlots_.size());
    batchSeats_.resize(batchSlots_.size());
    for (std::size_t i = 0; i < batchSlots_.size(); ++i) {
        const Slot& s = *slots_[std::size_t(batchSlots_[i])];
        batch_[i] = s.obs;
        batchGames_[i] = s.game;
        batchSeats_[i] = s.seat;
    }
    return int(batchSlots_.size());
}

void BatchEnv::answer(const std::vector<int>& actionIndices) {
    if (actionIndices.size() != batchSlots_.size()) throw std::invalid_argument("answer: one index per pending decision");
    std::lock_guard lock(m_);
    for (std::size_t i = 0; i < batchSlots_.size(); ++i) {
        Slot& s = *slots_[std::size_t(batchSlots_[i])];
        int a = actionIndices[i];
        if (a < 0 || a >= std::min(batch_[i].legalActions, enc::Actions)) a = 0, ++badAnswers_;
        if (!s.waiting) continue;  // stop() already woke it
        s.answer = a;
        s.waiting = false;
        s.ready.release();
    }
    decisions_ += long(batchSlots_.size());
    batchSlots_.clear();
}

std::vector<GameResult> BatchEnv::takeResults() {
    std::lock_guard lock(m_);
    return std::exchange(results_, {});
}

void BatchEnv::stop() {
    {
        std::lock_guard lock(m_);
        stopping_ = true;
        for (auto& s : slots_)
            if (s->waiting) s->waiting = false, s->answer = -1, s->ready.release();
    }
    cv_.notify_all();
    for (std::thread& t : threads_)
        if (t.joinable()) t.join();
    batchSlots_.clear();
}
