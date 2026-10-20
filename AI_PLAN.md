# AI plan: a player and a deck builder that learn together

Rewritten 2026-10-20. Built so far:
- every card plays, and golden tests guard the scripts;
- the heuristic player: about 99% wins against random, about 2% draws in a mirror;
- choice descriptors: every choice says what its options are;
- A0 (every Energy-type choice and the Fossil discard go to the player) and A1 (`tcgp-run`).

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
  machine before step A2.
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

## Phase A: infrastructure (CPU, on the laptop)

**A0. The last choices go to the player** (PROGRESS.md Next step 4). Today the engine picks which Energy type to
pay, discard or move, and when to discard a Fossil. A learning player must make every decision itself, or it can't
learn them.

**A1. Runner.** `tcgp-run` plays many games over all threads:
- one pairing, or a whole matrix of decks against decks;
- each side played by `random`, `heuristic` or, later, a network;
- results as win/draw/loss with 95% intervals, in CSV for the scripts in later phases.

Seeds are dropped.

**A2. A Python bridge with batched decisions.** This is the key piece of engineering.
- **Why it's needed:** the engine asks a player for a decision deep inside a card effect (`Game::choose`), so a game
  can't simply pause and hand control to Python.
- **The approach:** each game runs on its own C++ thread, and a few hundred games are in flight at once. When a
  network decision is needed, the game thread puts the observation in a queue and waits.
- **In Python:** `env.wait_batch()` returns all waiting observations as arrays. PyTorch runs them through the network
  on the GPU in one batch, and `env.answer(actions)` wakes the games up again.
- **The tool:** pybind11. No engine rewrite is needed, and the heuristic player stays in C++ as an opponent.
- **Done when:** a dummy network in Python plays complete games, and the batch throughput is measured.

**A3. Encoding:** what the network sees, from `PlayerView` only.
- **Card features**, a fixed table computed once:
  - the printed numbers: HP, type, Weakness, Retreat Cost, stage, ex or Mega, and each attack's cost and damage;
  - the words in its effect scripts (about 200 possible words, so `heal`, `switch-in`, `damage-dealt` … become
    features);
  - a small learned embedding per card.
  
  The script words let it handle rare cards from what they *do*.
- **Tokens:**
  - each card in your own hand;
  - each Pokémon in play, with its damage, Energy, Tool and Special Conditions;
  - the opponent's hand size, deck sizes and discard piles as counts;
  - a global token: turn, points, the Energy Zone, the Stadium, and whether this turn's Supporter, retreat and Energy
    were already used.
- **Actions:** each legal action is a token. It carries its kind and points at the card or Pokémon it involves, using
  the choice descriptors.

## Phase B: the player network

**B1. Copy the heuristic player first (behaviour cloning).**
- Record millions of heuristic decisions and train the network to predict them.
- That's fast, and it proves the whole pipeline: encoding, bridge, GPU, playing.
- **Done when:** the network wins about 50% against the heuristic it copied.

**B2. Get better than the teacher: self-play with PPO.**
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
- **Decks during B:** a broad mix of generated decks (C1) and reference decks, so the player can play anything.
- **Done when:** it beats the heuristic clearly, 65%+ with the same decks on both sides, and keeps climbing the
  Elo ladder.

**B3. Search on top (later, optional):** at decision time, try each move in a few quick simulated games, with the
hidden cards dealt at random from what's unseen. The network guides which moves get tried. This is stronger but
slower, so it's for evaluation and as a teacher, not for the millions of training games.

**B4. A fast copy for the CPU (if needed):** if the transformer is too slow for big deck searches without the GPU,
train a small MLP to imitate it (*distillation*).

## Phase C: the deck builder

**C1. Generator of legal, sensible decks:**
1. Pick 1–3 Pokémon lines.
2. Pick counts for each line, such as 2-2-1.
3. Set the Energy types from those lines.
4. Fill the rest with Trainers that fit.

It's used for the player's training mix and as the starting population.

**C2. Evolutionary search with the current player.** This gives the first deck ideas, and it can start right after
A1 with the heuristic player, long before the networks are ready.
- **Mutations:** swap a card, swap a line, change counts, change Energy types.
- **Crossover:** combine lines from two parents.
- **Successive halving:** many candidates get 100 games, the best get more, and so on.
- **Hall of fame:** the best decks of earlier generations stay in the opponent pool.

**C3. Deck value model:** a network that guesses a deck's win rate against the pool from its 20 cards (a set of card
embeddings). It trains on every deck C2 ever evaluated, and it screens mutations so only promising ones get
simulated.

**C4. The learned deck builder:**
- **What it is:** a network that builds a deck card by card, choosing only legal cards each step: at most 2 of a
  name, the evolution lines complete, the Energy types matching.
- **Training:** reinforcement learning on the win rate its decks reach when the current player network plays them.
  Cheap guesses come from the value model C3, and real games confirm the promising ones.
- **Shared parts:** it uses the same card features as the player, so "what a card does" is learned once.

## Phase D: together, the actual goal

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
| Long games with a reward only at the end learn slowly | start from behaviour cloning (B1); the value head; optional small shaping early on |
| GPU or PyTorch trouble on Pascal | everything also runs on CPU, just slower; check the PyTorch version first |

## Order and rough size

| Step | Size | Gives |
|---|---|---|
| A0 last player choices | small | a player can make every decision |
| A1 runner | small | fast matchups and matrices |
| C1 + C2 deck search with the heuristic player | medium | **the first deck ideas** |
| A2 bridge + A3 encoding | large | the networks can play |
| B1 cloning | medium | a working network player |
| B2 self-play | large | a player better than the heuristic |
| C3 + C4 deck networks | medium | a learned deck builder |
| D the loop | medium (mostly running) | decks that fit the player, plus the Nash mix |

The early steps already give something to use: after A1 + C2 there are deck rankings with the heuristic player.

## Open questions

- **Reference decks:** should the benchmark and the opponent pool include current tournament decks (Limitless
  publishes decklists)? This anchors the results to the real meta.
- **Owned cards:** should the builder be limited to the cards in the collection, or search every card?
- **Match format:** single games, as in ranked, or best-of-3?
- **The desktop:** a private GitHub repo, or copies with `git bundle`?
