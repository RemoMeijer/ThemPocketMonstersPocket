// tcgp-record: plays games between random deck pairs from the list (mirrors too), both sides by one player, and
// records every decision of both sides for behaviour cloning. Run from the project folder:
//   tcgp-record --games N [--threads T] --out DIR [--player heuristic|random] deck1.txt deck2.txt …
// DIR gets meta.json (what each file holds), one raw little-endian .bin file per array, row-major, and cards.bin,
// the static card table. A decision is a choice between two or more legal actions.
// Compact (record_format 2): floats as float16, ints in the smallest type that fits; tokens one row per decision,
// actions one row per legal action, with action_offsets (decisions + 1 entries) marking where each decision's
// actions start. The token mask is token_type != 0. tools/load_record.py reads it back into padded batches.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game/game.h"
#include "ml/encode.h"
#include "players/heuristic.h"

namespace {
using f16 = _Float16;  // GCC and Clang on x86-64; numpy's "<f2"
constexpr int RecordFormat = 2;

struct Column {
    const char* name;
    const char* dtype;
    std::vector<int> shape;  // per row
    bool perAction;          // rows: one per legal action, else one per decision
    std::size_t bytes;       // per row
};
enum : std::size_t { TokType, TokCard, TokTool, TokFloat, Legal, Chosen, Player, GameId, Outcome, ActKind, ActCard, ActPtr, ActPtr2, ActFloat };
const std::vector<Column> Columns = {
    {"token_type", "|i1", {enc::Tokens}, false, enc::Tokens},
    {"token_card", "<i2", {enc::Tokens}, false, 2 * enc::Tokens},
    {"token_tool", "<i2", {enc::Tokens}, false, 2 * enc::Tokens},
    {"token_float", "<f2", {enc::Tokens, enc::TokenFloats}, false, 2 * enc::Tokens * enc::TokenFloats},
    {"legal_actions", "<i2", {}, false, 2},  // before cutting at enc::Actions
    {"chosen", "|i1", {}, false, 1},         // index into the decision's actions
    {"player", "|i1", {}, false, 1},         // seat
    {"game", "<i4", {}, false, 4},           // game number
    {"result", "|i1", {}, false, 1},         // for that player: +1 win, -1 loss, 0 draw
    {"action_kind", "|i1", {}, true, 1},     // ActionKind + 1
    {"action_card", "<i2", {}, true, 2},
    {"action_ptr", "|i1", {}, true, 1},      // token, -1 none
    {"action_ptr2", "|i1", {}, true, 1},
    {"action_float", "<f2", {enc::ActionFloats}, true, 2 * enc::ActionFloats},
};

// Appends n values converted to To.
template <class To, class From>
void put(std::vector<char>& out, const From* v, std::size_t n) {
    std::size_t at = out.size();
    out.resize(at + n * sizeof(To));
    for (std::size_t i = 0; i < n; ++i) {
        To x = To(v[i]);
        std::memcpy(out.data() + at + i * sizeof(To), &x, sizeof(To));
    }
}
template <class To, class From>
void put(std::vector<char>& out, From v) { put<To>(out, &v, 1); }

// One game's decisions, compact, in one buffer per column; written in one piece when the game ends.
struct Buffers {
    std::vector<std::vector<char>> cols{Columns.size()};
    std::vector<int8_t> players;
    std::vector<int> actions;  // stored actions per decision
    void clear() {
        for (auto& c : cols) c.clear();
        players.clear();
        actions.clear();
    }
};

// Plays as `inner` and notes each decision.
class RecordingPolicy : public Policy {
public:
    RecordingPolicy(Policy& inner, Buffers& log, long& game) : inner_(inner), log_(log), game_(game) {}
    std::size_t choose(const PlayerView& view, const std::vector<Action>& actions) override {
        std::size_t pick = inner_.choose(view, actions);  // always asked, so the heuristic's memory stays the same
        if (actions.size() < 2) return pick;
        enc::encode(view, actions, obs_);
        auto& c = log_.cols;
        put<int8_t>(c[TokType], obs_.tokenType.data(), enc::Tokens);
        put<int16_t>(c[TokCard], obs_.tokenCard.data(), enc::Tokens);
        put<int16_t>(c[TokTool], obs_.tokenTool.data(), enc::Tokens);
        for (const auto& f : obs_.tokenFloat) put<f16>(c[TokFloat], f.data(), f.size());
        put<int16_t>(c[Legal], std::min(obs_.legalActions, 32767));
        put<int8_t>(c[Chosen], pick);
        put<int8_t>(c[Player], view.me());
        put<int32_t>(c[GameId], game_);
        int n = std::min(obs_.legalActions, enc::Actions);
        put<int8_t>(c[ActKind], obs_.actionKind.data(), std::size_t(n));
        put<int16_t>(c[ActCard], obs_.actionCard.data(), std::size_t(n));
        put<int8_t>(c[ActPtr], obs_.actionPtr.data(), std::size_t(n));
        put<int8_t>(c[ActPtr2], obs_.actionPtr2.data(), std::size_t(n));
        for (int i = 0; i < n; ++i) put<f16>(c[ActFloat], obs_.actionFloat[std::size_t(i)].data(), enc::ActionFloats);
        log_.players.push_back(int8_t(view.me()));
        log_.actions.push_back(n);
        return pick;
    }

private:
    Policy& inner_;
    Buffers& log_;
    long& game_;
    enc::Observation obs_;
};

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) out += c == '"' || c == '\\' ? std::string("\\") + c : std::string(1, c);
    return out + "\"";
}

std::string shapeJson(long rows, const std::vector<int>& shape) {
    std::string s = "[" + std::to_string(rows);
    for (int d : shape) s += ", " + std::to_string(d);
    return s + "]";
}
}  // namespace

int main(int argc, char** argv) {
    long games = 0;
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::string player = "heuristic", outDir;
    std::vector<std::string> paths;
    try {
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            bool hasValue = i + 1 < argc;
            if (a == "--games" && hasValue) games = std::stol(argv[++i]);
            else if (a == "--threads" && hasValue) threads = unsigned(std::stoul(argv[++i]));
            else if (a == "--out" && hasValue) outDir = argv[++i];
            else if (a == "--player" && hasValue) player = argv[++i];
            else if (a.starts_with("--")) throw std::runtime_error("unknown option " + a);
            else paths.push_back(a);
        }
        if (paths.empty() || games < 1 || threads < 1 || outDir.empty()) throw std::runtime_error("need --games, --out and decks");
        makePolicy(player);
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what()
                  << "\nusage: tcgp-record --games N [--threads T] --out DIR [--player heuristic|random] deck1.txt …\n";
        return 2;
    }
    try {
        const std::string effects = "data/effects.tsv";
        const CardDb db = CardDb::load("data/cards.tsv", std::filesystem::exists(effects) ? effects : "");
        if (db.all().size() >= 32767) throw std::runtime_error("too many cards for int16 card indices");
        std::vector<Deck> decks;
        for (const std::string& p : paths) decks.push_back(loadDeck(p, db));
        std::filesystem::create_directories(outDir);
        auto open = [&](const std::string& name) {
            std::ofstream f(std::filesystem::path(outDir) / (name + ".bin"), std::ios::binary);
            if (!f) throw std::runtime_error("cannot write " + outDir + "/" + name + ".bin");
            return f;
        };
        std::vector<std::ofstream> files;
        for (const Column& c : Columns) files.push_back(open(c.name));
        std::ofstream offsets = open("action_offsets");
        int64_t actionRows = 0;
        offsets.write(reinterpret_cast<const char*>(&actionRows), 8);

        // As in tcgp-run: threads take the next game number until all are played; each game's decisions are written
        // in one piece when it ends, so the files hold whole games, in the order they finished.
        std::mutex writeLock;
        std::atomic<long> next{0}, decisions{0};
        std::atomic<int> maxLegal{0};
        std::exception_ptr failure;
        auto work = [&] {
            try {
                std::unique_ptr<Policy> inner[2] = {makePolicy(player), makePolicy(player)};
                Buffers log;
                long k = 0;
                RecordingPolicy rec0(*inner[0], log, k), rec1(*inner[1], log, k);
                std::mt19937 rng{std::random_device{}()};
                std::uniform_int_distribution<std::size_t> pick(0, decks.size() - 1);
                while ((k = next++) < games) {
                    log.clear();
                    Game game(decks[pick(rng)], decks[pick(rng)], rec0, rec1);
                    int winner = game.run().winner;
                    for (int8_t p : log.players) put<int8_t>(log.cols[Outcome], winner < 0 ? 0 : winner == p ? 1 : -1);
                    std::lock_guard lock(writeLock);
                    for (std::size_t c = 0; c < Columns.size(); ++c) files[c].write(log.cols[c].data(), std::streamsize(log.cols[c].size()));
                    for (int n : log.actions) {
                        actionRows += n;
                        offsets.write(reinterpret_cast<const char*>(&actionRows), 8);
                        if (n > maxLegal) maxLegal = n;
                    }
                    decisions += long(log.players.size());
                }
            } catch (...) {
                std::lock_guard lock(writeLock);
                if (!failure) failure = std::current_exception();
                next = games;
            }
        };
        auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < threads; ++t) pool.emplace_back(work);
        for (std::thread& t : pool) t.join();
        if (failure) std::rethrow_exception(failure);
        for (std::ofstream& f : files)
            if (!f.flush()) throw std::runtime_error("write failed in " + outDir);
        if (!offsets.flush()) throw std::runtime_error("write failed in " + outDir);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

        enc::CardTable table = enc::staticFeatures(db);
        std::ofstream cards(std::filesystem::path(outDir) / "cards.bin", std::ios::binary);
        cards.write(reinterpret_cast<const char*>(table.values.data()), std::streamsize(table.values.size() * sizeof(float)));

        long n = decisions;
        std::size_t bytes = std::size_t(n + 1) * 8;  // action_offsets
        std::ofstream meta(std::filesystem::path(outDir) / "meta.json");
        meta << "{\n  \"record_format\": " << RecordFormat << ",\n  \"encoding_version\": " << enc::EncodingVersion
             << ",\n  \"games\": " << games << ",\n  \"decisions\": " << n << ",\n  \"actions\": " << actionRows
             << ",\n  \"player\": " << quoted(player) << ",\n  \"max_legal_actions\": " << maxLegal
             << ",\n  \"sizes\": {\"tokens\": " << enc::Tokens << ", \"token_floats\": " << enc::TokenFloats
             << ", \"token_types\": " << enc::TokenTypeCount << ", \"actions\": " << enc::Actions
             << ", \"action_floats\": " << enc::ActionFloats << ", \"action_kinds\": " << ActionKindCount + 1
             << ", \"card_fixed_features\": " << enc::CardFixedFeatures << "},\n  \"arrays\": {";
        for (const Column& c : Columns) {
            long rows = c.perAction ? long(actionRows) : n;
            meta << "\n    " << quoted(c.name) << ": {\"file\": " << quoted(std::string(c.name) + ".bin") << ", \"dtype\": " << quoted(c.dtype)
                 << ", \"rows\": " << (c.perAction ? "\"actions\"" : "\"decisions\"") << ", \"shape\": " << shapeJson(rows, c.shape) << "},";
            bytes += std::size_t(rows) * c.bytes;
        }
        meta << "\n    \"action_offsets\": {\"file\": \"action_offsets.bin\", \"dtype\": \"<i8\", \"rows\": \"decisions + 1\", \"shape\": ["
             << n + 1 << "]}\n  },\n  \"cards\": {\"file\": \"cards.bin\", \"dtype\": \"<f4\", \"shape\": [" << table.rows << ", "
             << table.cols << "]},\n  \"card_ids\": [";
        for (std::size_t i = 0; i < table.ids.size(); ++i) meta << (i ? ", " : "") << quoted(table.ids[i]);
        meta << "],\n  \"vocabulary\": [";
        for (std::size_t i = 0; i < table.words.size(); ++i) meta << (i ? ", " : "") << quoted(table.words[i]);
        meta << "]\n}\n";
        if (!meta || !cards) throw std::runtime_error("write failed in " + outDir);
        std::printf("%ld games, %ld decisions in %.2f s on %u threads: %.0f decisions/s, %.0f bytes per decision, max %d legal actions\n",
                    games, n, seconds, threads, double(n) / seconds, double(bytes) / double(n), int(maxLegal));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
