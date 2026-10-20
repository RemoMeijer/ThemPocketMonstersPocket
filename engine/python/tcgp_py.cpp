// The Python module `tcgp` for training: cards, decks, the static card table and BatchEnv. Built by
// engine/CMakeLists.txt only where pybind11 is installed. Run Python from the project folder (data/, decks/).
//
//   import tcgp
//   db = tcgp.load_cards()                          # data/cards.tsv + data/effects.tsv
//   decks = [tcgp.load_deck(p, db) for p in paths]
//   env = tcgp.BatchEnv(db, decks, threads=8, games_in_flight=256, side=tcgp.Side.BOTH)
//   obs = env.wait_batch(64, 10)                    # dict of numpy arrays, one row per pending decision
//   env.answer(picks)                               # one index per row, into that row's legal actions
//
// The arrays wait_batch returns view the env's memory and are overwritten by the next wait_batch: copy what you
// keep (moving them to the GPU copies them).
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <cstddef>
#include <cstring>

#include "ml/batch_env.h"

namespace py = pybind11;

namespace {
// Rows 0..n of one Observation field across the batch, without copying: the batch is an array of Observations,
// so each row is sizeof(Observation) further on.
template <class T>
py::array field(py::object env, int n, std::size_t offset, std::vector<py::ssize_t> inner = {}) {
    const auto& batch = env.cast<BatchEnv&>().batch();
    std::vector<py::ssize_t> shape{n}, strides{py::ssize_t(sizeof(enc::Observation))};
    py::ssize_t step = sizeof(T);
    for (std::size_t i = 0; i < inner.size(); ++i) step *= inner[i];
    for (py::ssize_t d : inner) shape.push_back(d), strides.push_back(step /= d);
    const char* base = reinterpret_cast<const char*>(batch.data()) + offset;
    return py::array(py::dtype::of<T>(), shape, strides, base, env);
}
}  // namespace

PYBIND11_MODULE(tcgp, m) {
    m.attr("ENCODING_VERSION") = enc::EncodingVersion;
    py::dict sizes;
    sizes["tokens"] = enc::Tokens;
    sizes["token_floats"] = enc::TokenFloats;
    sizes["token_types"] = enc::TokenTypeCount;
    sizes["actions"] = enc::Actions;
    sizes["action_floats"] = enc::ActionFloats;
    sizes["action_kinds"] = ActionKindCount + 1;
    sizes["card_fixed_features"] = enc::CardFixedFeatures;
    m.attr("SIZES") = sizes;

    py::class_<CardDb, std::unique_ptr<CardDb>>(m, "CardDb").def("__len__", [](const CardDb& db) { return db.all().size(); });
    m.def("load_cards", [](const std::string& cards, const std::string& effects) { return std::make_unique<CardDb>(CardDb::load(cards, effects)); },
          py::arg("cards") = "data/cards.tsv", py::arg("effects") = "data/effects.tsv");

    py::class_<Deck>(m, "Deck").def_readonly("name", &Deck::name);
    m.def("load_deck", &loadDeck, py::arg("path"), py::arg("db"), py::keep_alive<0, 2>());  // a deck points into its db

    // {"values": float32 [cards+1, features], "words": script words, "ids": card id of row i+1}
    m.def("card_table", [](const CardDb& db) {
        enc::CardTable t = enc::staticFeatures(db);
        py::array_t<float> values({t.rows, t.cols});
        std::memcpy(values.mutable_data(), t.values.data(), t.values.size() * sizeof(float));
        py::dict d;
        d["values"] = values;
        d["words"] = t.words;
        d["ids"] = t.ids;
        return d;
    });

    py::class_<BatchEnv> env(m, "BatchEnv");
    py::enum_<BatchEnv::Side>(m, "Side").value("PLAYER0", BatchEnv::Side::Player0).value("BOTH", BatchEnv::Side::Both);
    env.def(py::init([](const CardDb& db, std::vector<Deck> decks, int threads, int gamesInFlight, BatchEnv::Side side,
                        const std::string& opponent) { return new BatchEnv(db, std::move(decks), threads, gamesInFlight, side, opponent); }),
            py::arg("db"), py::arg("decks"), py::arg("threads"), py::arg("games_in_flight"), py::arg("side"),
            py::arg("opponent") = "heuristic", py::keep_alive<1, 2>());
    env.def("wait_batch", [](py::object self, int minBatch, int timeoutMs) {
        BatchEnv& e = self.cast<BatchEnv&>();
        int n;
        {
            py::gil_scoped_release release;
            n = e.waitBatch(minBatch, timeoutMs);
        }
        using O = enc::Observation;
        py::dict d;
        d["token_type"] = field<int32_t>(self, n, offsetof(O, tokenType), {enc::Tokens});
        d["token_card"] = field<int32_t>(self, n, offsetof(O, tokenCard), {enc::Tokens});
        d["token_tool"] = field<int32_t>(self, n, offsetof(O, tokenTool), {enc::Tokens});
        d["token_float"] = field<float>(self, n, offsetof(O, tokenFloat), {enc::Tokens, enc::TokenFloats});
        d["token_mask"] = field<uint8_t>(self, n, offsetof(O, tokenMask), {enc::Tokens});
        d["action_kind"] = field<int32_t>(self, n, offsetof(O, actionKind), {enc::Actions});
        d["action_card"] = field<int32_t>(self, n, offsetof(O, actionCard), {enc::Actions});
        d["action_ptr"] = field<int32_t>(self, n, offsetof(O, actionPtr), {enc::Actions});
        d["action_ptr2"] = field<int32_t>(self, n, offsetof(O, actionPtr2), {enc::Actions});
        d["action_float"] = field<float>(self, n, offsetof(O, actionFloat), {enc::Actions, enc::ActionFloats});
        d["action_mask"] = field<uint8_t>(self, n, offsetof(O, actionMask), {enc::Actions});
        d["legal_actions"] = field<int32_t>(self, n, offsetof(O, legalActions));
        d["game"] = py::array_t<long>(n, e.batchGames().data());  // copies: small
        d["seat"] = py::array_t<int>(n, e.batchSeats().data());
        return d;
    }, py::arg("min_batch"), py::arg("timeout_ms"));
    env.def("answer", &BatchEnv::answer, py::arg("actions"), py::call_guard<py::gil_scoped_release>());
    env.def("take_results", [](BatchEnv& e) {
        py::list out;
        for (const GameResult& r : e.takeResults()) {
            py::dict d;
            d["game"] = r.game;
            d["decks"] = py::make_tuple(r.decks[0], r.decks[1]);
            d["winner"] = r.winner;
            d["turns"] = r.turns;
            d["external"] = r.external;
            out.append(d);
        }
        return out;
    });
    env.def("stop", &BatchEnv::stop, py::call_guard<py::gil_scoped_release>());
    env.def_property_readonly("decisions", &BatchEnv::decisions);
    env.def_property_readonly("bad_answers", &BatchEnv::badAnswers);
    env.def_property_readonly("max_actions", &BatchEnv::maxActions);
    env.def_property_readonly("cut_actions", &BatchEnv::cutActions);
}
