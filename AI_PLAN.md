# AI plan: a player and a deck builder that learn together

Rewritten 2026-10-20, in the order the work is done. Built so far:
- every card plays, and golden tests guard the scripts;
- the heuristic player: about 99% wins against random, about 2% draws in a mirror;
- choice descriptors: every choice says what its options are;
- step 1: every Energy-type choice and the Fossil discard go to the player, and the runner `tcgp-run`;
- step 2: `deckgen.py` and `search_decks.py` (first run: Mega Sableye ex, Mimikyu ex, Hisuian Goodra, Mismagius ex).

## The goal

Two networks that get better together:
- **A player** that picks good moves from what it can see: its own hand, the board, and the discard piles.
- **A deck builder** that puts together the 20 cards the player wins most with.

The deck builder learns from how the player plays, and the player learns to play the decks the builder makes. So the
decks fit this player's style, the way a human builds around what they're good at.

**The output** is the ideas to try in the game:
- the top decks, with win rates and a matchup table;
- the mix of decks that no single deck beats (the Nash equilibrium);
- sample games showing how the player uses each deck.

## Hardware

| Machine | GPU | Use |
|---|---|---|
| Laptop | GTX 1050 (2–4 GB) | development and short training runs; the games themselves run on the 12 CPU threads |
| Desktop | GTX 1080 (8 GB) | long training runs, about 4× the laptop's GPU |

Both GPUs are fine for models of this size (up to a few million numbers). Two things to sort out:
- **PyTorch support:** Pascal-generation cards like these may need a slightly older PyTorch build. To check on each
  machine before step 3.
- **Getting the repo to the desktop:** it's a local repo with no remote. A private GitHub repo or a `git bundle` copy
  would do.

## How the pieces fit

```
             card features (HP, costs, damage, script words, learned id)  ── shared by both networks
                      │                                   │
   ┌──────────────────▼───────────┐       ┌───────────────▼──────────────┐
   │ PLAYER network               │       │ DECK BUILDER                 │
   │ tokens: hand cards, Pokémon  │       │ picks 20 cards, one by one,  │
   │ in play, global state        │       │ only legal choices           │
   │ → a score per legal action   │       │ → a deck                     │
   │ → "chance I win from here"   │       │ (+ a model that guesses a    │
   └──────────────▲───────────────┘       │   deck's win rate)           │
                  │ trained by self-play  └───────────────▲──────────────┘
                  │                                       │ trained on the win rates
          ┌───────┴───────────────────────────────────────┴──────┐
          │ C++ engine: many games at once on all CPU threads;   │
          │ decisions batched to the GPU, answers sent back      │
          └──────────────────────────────────────────────────────┘
```

## Step 1: the engine ready for players (CPU, done)

**1a. The last choices go to the player** (done 2026-10-20). The player picks every Energy type that is paid,
discarded, moved or taken from the discard pile, and may discard a Fossil from play. A learning player must make every
decision itself, or it can't learn them.

**1b. Runner** (done 2026-10-20). `tcgp-run` plays many games over all threads, about 28,000 heuristic games/s:
- one pairing, or a whole matrix of decks against decks;
- each side played by `random`, `heuristic` or, later, a network;
- results as win/draw/loss with 95% intervals, in CSV for the scripts in later steps.

Seeds are dropped.

## Step 2: the first deck search, with the heuristic player (CPU, done 2026-10-20)

This gives deck ideas early, with what exists now, and the generator also supplies the training decks for step 4.

**2a. Generator of legal, sensible decks:**
1. Pick 1–3 Pokémon lines.
2. Pick counts for each line, such as 2-2-1.
3. Set the Energy types from those lines.
4. Fill the rest with Trainers that fit.

It's used for the player's training mix and as the starting population.

**2b. Evolutionary search with the current player.** This gives the first deck ideas, and it needs only step 1 and the
heuristic player, long before the networks are ready.
- **Mutations:** swap a card, swap a line, change counts, change Energy types.
- **Crossover:** combine lines from two parents.
- **Successive halving:** many candidates get 100 games, the best get more, and so on.
- **Hall of fame:** the best decks of earlier generations stay in the opponent pool.

## Step 3: networks can play (C++ side done 2026-10-20; the Python side is set up on the PC)

**3a. Batch environment and Python module.**
- **The problem:** the engine asks a player for a decision deep inside a card effect (`Game::choose`), so a game can't
  simply pause and hand control to Python.
- **Built** (`engine/src/ml/batch_env.*`): each game runs on a C++ thread, with a few hundred games in flight. A
  network decision puts the encoded observation in a queue and waits. `waitBatch()` returns everything waiting;
  `answer()` wakes those games up. Games restart by themselves, and `takeResults()` drains the finished ones, each
  with its game id and seat.
- **Options:** the external side is player 0 or both; the opponent is `heuristic` or `random`. Two decks make a fixed
  pairing, more decks random pairs. Forced moves (a single legal action) are played without asking.
- **Speed:** about 220,000 decisions/s and 3,100 games/s from C++ (random answers, 8 threads, 128 in flight), and
  about 148,000 decisions/s through Python.
- **The Python module** (`engine/python/tcgp_py.cpp`, module `tcgp`) is built by CMake only when pybind11 is found.
  - **Exports:** `ENCODING_VERSION`, `SIZES`, `load_cards(cards=…, effects=…)`, `load_deck(path, db)`,
    `card_table(db)` → `{"values", "words", "ids"}`, and
    `BatchEnv(db, decks, threads, games_in_flight, side=tcgp.Side.PLAYER0|BOTH, opponent="heuristic")` with
    `wait_batch(min_batch, timeout_ms)` (a dict of numpy views), `answer(actions)`, `take_results()` and `stop()`.
  - **Tested:** a hand build played 500 games from Python.
  - **Untested:** the CMake path, a current pybind11, `torch.from_numpy` on the views, and the GIL under several
    Python threads. These are the first things to check on the PC.

**3b. Encoding** (`engine/src/ml/encode.*`, `EncodingVersion = 1`). It's built from `PlayerView` only, so a player
never sees the opponent's hand or either deck. Sizes: T=20 tokens × D=54 floats, A=64 actions × Da=24 floats, and
static card features F=369.

| Part | Layout |
|---|---|
| Token slots | 0 global · 1–10 my hand · 11 my Active, 12–14 my Bench spots 0–2 · 15 opp Active, 16–18 opp Bench spots 0–2 · 19 Stadium |
| Token ints | type (0 pad, 1 global, 2 hand, 3/4 my Active/Bench, 5/6 opp Active/Bench, 7 Stadium), card index, Tool card index |
| Pokémon floats | HP left/100 · damage/100 · Energy per type/5 (10) · status bits (5) · attack 0/1 payable · entered this turn · evolved this turn · Ability used · is Active |
| Global floats | turn/30 · points/3 (both) · hand/10 · deck/20 · discard/20 · discard Energy per type/10 (both) · Energy Zone now and next (one-hot) · Supporter played · retreated · no Energy left in the zone · own first turn · my turn |
| Stadium floats | mine · its action used |
| Action ints | kind+1 (0 pad), card index, pointer to a token, second pointer |
| Pointers by kind | Place / PlayTrainer: hand token · Evolve / AttachTool: hand token, then the target Pokémon · AttachEnergy / UseAbility: target · Retreat: the Pokémon coming in · Attack: my Active · UseStadium: Stadium · Choose: the Pokémon (by uid), or -1 with the option's card · EndTurn: -1 |
| Action floats | attack one-hot (8) · ChoiceKind one-hot (5) · Energy type one-hot (10) · option index/10 |
| Static card table | [cards+1 × 369]: category, stage, ex, Mega, HP, type, Weakness, retreat, points, both attacks' costs/damage/text kind, Ability, traits, and counts of 299 script words |

Card index 0 is padding. The vocabulary and the card ids in index order come with every recording (meta.json).

**3c. Recorder for behaviour cloning:** `tcgp-record --games N --threads T --out DIR deck…` plays random deck pairs
with the heuristic on both sides and saves every real decision: the observation, the chosen action, the seat, the
game, and the final result for that seat (+1/−1/0). Record format 2:
- floats are stored as float16 and actions ragged (only the legal ones, with `action_offsets`): about 2.5 KB per
  decision, so 2.5 million decisions take about 6.5 GB;
- it records about 70,000–400,000 decisions/s on 12 threads; the high end comes when the files are cached;
- `tools/load_record.py`: `load(dir)`, `batch(rec, indices)` (padded float32/int64 arrays with masks) and
  `check(rec)`.

## Step 4: the player network

**On the PC, first:**
1. Pull the repo.
2. Build the engine: `cmake -S engine -B engine/build && cmake --build engine/build -j`.
3. Run the tests: `engine/build/test_rules`, `test_effects`, `test_players`, `test_encode`, `test_batch_env`.
4. Run `ml/setup.sh`. It creates `.venv`, installs PyTorch (the CUDA 12.6 build, for Pascal), numpy, scipy and
   pybind11, builds the `tcgp` module, and runs `ml/check_gpu.py` (the GPU check plus the module import).

**4a. Copy the heuristic player first (behaviour cloning).**
- **Decks for the data:** the reference decks, `decks/found/`, and a few hundred generated ones
  (`python3 deckgen.py 300 ml/data/decks`).
- **Data:** `engine/build/tcgp-record --games 50000 --threads 12 --out ml/data/bc1 decks/*.txt decks/found/*.txt
  ml/data/decks/*.txt`. That's about 2.1 million decisions and 5–6 GB. Check it with `python3 tools/load_record.py ml/data/bc1`.
- **Model** (`ml/model.py`), small to start:
  - **Card vectors:** a learned embedding of the card index (64) plus a linear layer over the static card features
    (369 → 64), added together.
  - **Tokens:** an embedding of the token type, the card vector, the Tool card's vector, and a linear layer over the
    54 floats, into width 128.
  - **Encoder:** a 3-layer transformer (4 heads, padding masked).
  - **Actions:** an embedding of the kind, the card vector, the encoded tokens the two pointers gather, and a linear
    layer over the 24 floats. An MLP scores each action against the global token's state. Padding is masked, then a
    softmax over the legal actions.
  - **Value:** from the global token, tanh.
- **Training** (`ml/train_bc.py`):
  - loss: cross-entropy on the chosen action, plus 0.5 × the MSE of the value against the final result;
  - AdamW at lr 3e-4, batch 1024, about 5 epochs, with 5% of the games held out.
  - Watch the held-out accuracy: how often the network picks the heuristic's move.
- **Playing** (`ml/evaluate.py`): a `BatchEnv` with the network as player 0 against the heuristic, over the
  reference decks, 2,000 games or more, reporting the win rate with its interval.
- **Done when:** it wins about 50% against the heuristic, and the held-out accuracy is high (90%+ is plausible, since
  the heuristic is deterministic).

**4b. Get better than the teacher: self-play with PPO.**
- **Model:** a small transformer over the tokens, with an action head that scores each legal action (a pointer) and
 a value head ("how likely do I win from here"). 2–4 layers, 128–256 wide, about 0.5–2 million numbers.
- **Reward:** +1 for a win, −1 for a loss, 0 for a draw. Early on, maybe a small bonus per point scored, removed later.
- **League:** a light version of AlphaStar's idea. Opponents are:
 - 50% the current network;
 - 30% older saved versions;
 - 20% the heuristic.
 
 This stops it from forgetting how to beat old styles and from going round in circles. Among the older versions,
 the ones the current network loses to get picked more often (prioritized fictitious self-play, from AlphaStar), so
 training goes where the weaknesses are.
- **Why the reward stays plain win/loss and not Elo:** an Elo change per game is the same win/loss signal, scaled and
 shifted by the opponent's rating, and PPO's value head already does that shift. Elo would only add noise, because
 the ratings move during training. Elo is for measuring (the ladder) and for picking opponents, as above. Decks get
 no Elo: matchups aren't transitive, so decks are ranked by the matchup table and the Nash mix.
- **Decks during step 4:** a broad mix of generated decks (2a) and reference decks, so the player can play anything.
- **Done when:** it beats the heuristic clearly, 65%+ with the same decks on both sides, and keeps climbing the
 Elo ladder.

**4b, build notes:**
- **Self-play:** run self-play through a `BatchEnv` with both sides external. Each pending decision carries its game
  id and seat (`batchGames`, `batchSeats`), so its log probability and value are stored per (game, seat), and the
  final result from `take_results()` becomes that trajectory's reward.
- **Opponents from the league:** run several environments, or several model versions, and send each game's seat to
  the right network.
- **PPO settings:** γ = 1 (the game is short and only the end counts), GAE λ = 0.95, clip 0.2, entropy 0.01,
  rollouts of about 50,000 decisions per update, 4 epochs over them.
- **Start** from the 4a weights. Save a snapshot every few updates for the league. The Elo ladder uses `tcgp-run`
  for the C++ players and `evaluate.py` for the networks.

**4c. Search on top (later, optional):** at decision time, try each move in a few quick simulated games, with the
hidden cards dealt at random from what's unseen. The network guides which moves get tried. This is stronger but
slower, so it's for evaluation and as a teacher, not for the millions of training games.

**4d. A fast copy for the CPU (if needed):** if the transformer is too slow for big deck searches without the GPU,
train a small MLP to imitate it (*distillation*).

## Step 5: the deck networks

**5a. Deck value model:** a network that guesses a deck's win rate against the pool from its 20 cards (a set of card
embeddings). It trains on every deck 2b ever evaluated, and it screens mutations so only promising ones get
simulated.

**5b. The learned deck builder:**
- **What it is:** a network that builds a deck card by card, choosing only legal cards each step: at most 2 of a
 name, the evolution lines complete, the Energy types matching.
- **Training:** reinforcement learning on the win rate its decks reach when the current player network plays them.
 Cheap guesses come from the value model 5a, and real games confirm the promising ones.
- **Shared parts:** it uses the same card features as the player, so "what a card does" is learned once.

## Step 6: together, the actual goal

One cycle:
1. **Propose:** the builder proposes new decks, both its best guesses and some exploration. The population is these
   plus the hall of fame.
2. **Play:** the player network plays the population against itself (both sides network), on the GPU in batches.
3. **Learn:**
   - **the player** trains on those games (PPO), so it learns to play the decks the builder likes;
   - **the builder** trains on the decks' win rates, so it learns which decks this player wins with;
   - **the population** keeps the winners.
4. **Check:** on a fixed benchmark that the loop never trains on:
   - reference decks played by the heuristic and by each player version (the Elo ladder);
   - the matchup table of the current top decks, with its Nash mix.

**Stop** when the top decks and their ranking stop changing for several cycles, and the player no longer improves on
the benchmark.

## Risks, and what guards against them

| Risk | Guard |
|---|---|
| The AI finds an engine bug or shortcut and wins through it | golden tests and `check_states` stay mandatory; read the game logs of every top deck before trusting it |
| Rock-paper-scissors cycles: deck A beats B beats C beats A | hall of fame, league opponents, and the Nash mix instead of one "best" deck |
| The player overfits to its own decks | the fixed benchmark, the heuristic in the league, and generated decks in the training mix |
| Long games with a reward only at the end learn slowly | start from behaviour cloning (4a); the value head; optional small shaping early on |
| GPU or PyTorch trouble on Pascal | everything also runs on CPU, just slower; check the PyTorch version first |

## Size of each step

| Step | Size | Gives |
|---|---|---|
| 1 last player choices, runner | small | done: a player makes every decision; fast matchups and matrices |
| 2 deck generator and search with the heuristic player | medium | **the first deck ideas** |
| 3 Python bridge and encoding | large | the networks can play |
| 4a cloning | medium | a working network player |
| 4b self-play | large | a player better than the heuristic |
| 5 deck networks | medium | a learned deck builder |
| 6 the loop | medium (mostly running) | decks that fit the player, plus the Nash mix |

## Open questions

- **Reference decks:** should the benchmark and the opponent pool include current tournament decks (Limitless
  publishes decklists)? This anchors the results to the real meta.
- **Owned cards:** should the builder be limited to the cards in the collection, or search every card?
- **Match format:** single games, as in ranked, or best-of-3?
- **The desktop:** a private GitHub repo, or copies with `git bundle`?
