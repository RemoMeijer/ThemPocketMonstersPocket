# AI plan: playing well and building decks

Written 2026-10-20, after card coverage reached 100%. Built so far: the heuristic player (M1) and the choice descriptors (M0).

The goal stays the same as in PROGRESS.md: find decks that come out on top, as ideas to try in the game. That needs two
things, and they depend on each other:

- **Game strategy:** a player that makes good choices. A deck is only as good as the way it's played, and the random
  player makes every deck look like a coin toss (about 70% of its games end in a draw at the turn cap).
- **Deck building:** searching the space of legal 20-card decks for the ones with the best win rates.

So the order is: first a decent player, then deck search with that player, then improve both in turns.

## What we have, and what's missing

- **Have:**
  - an engine that plays every card by the settled rules;
  - `PlayerView`, which already hides the opponent's hand and both decks, so a player can't cheat;
  - one `Policy` interface (`choose(view, legal actions)`);
  - about 2,000 games/s per thread *with* the state checker on. The engine alone hasn't been measured; expect several
    times that.
- **Missing, all already on PROGRESS.md's Next list:**
  - the runner (many games over threads, with stats);
  - player choices. Today a choice inside an effect reaches the player as "option 0…n" with no meaning attached.
    A learned player needs to know *what* each option is (a Pokémon, a card, an Energy type), so every choice must
    carry a short description: the kind of choice, plus the uid or card id of each option.

## Milestones

Each is small enough to check on its own: a test, numbers from the runner, and a commit.

### M0. Foundations (Next steps 3–5)

- **Runner:** `tcgp-run deckA deckB --games N --threads T`. It reports win/draw/loss with a 95% interval and
  alternates who goes first. Seeds (replayable games, the same luck for both decks) were dropped for now (2026-10-20):
  training wants fresh randomness anyway, and the gain for comparisons is modest.
- **Choice descriptors (built 2026-10-20):** every `Game::choose` call passes what it's choosing (`ChoiceKind`) and what each option is (a Pokémon's uid or a card). The random
  player ignores them; later players use them.
- **Speed:** measure games/s with the checker off. That sets the budget for everything below.

### M1. Baseline players and a ladder (no ML)

- **Heuristic player** (built 2026-10-20, `players/heuristic.cpp`; it wins about 99% against random and draws about 2% against itself):
  - evolve when possible;
  - attach Energy to the Pokémon closest to attacking;
  - play draw and search Trainers;
  - attack with the strongest affordable attack;
  - retreat a Pokémon about to be knocked out, when there's something better on the Bench.
  
  It's a sane opponent, and it cuts the draws.
- **Rollout player** (flat Monte Carlo): for each legal action, play K quick games to the end with the heuristic
  player, after first dealing the hidden cards at random from what's still unseen (*determinizing*). Pick the action
  that wins most often. It's slow, hundreds of games per decision, but strong and needs no training. It's the
  **teacher** for M3.
- **Ladder:**
  - a fixed pool of reference decks;
  - every player gets an Elo rating against the others: random, heuristic, rollout, and later each trained version;
  - a new player must beat the old ones on this ladder before it's used for deck search.

### M2. State and action features (C++)

- **State, one fixed-size vector per decision, seen from `PlayerView` only:**
  - for each of the 8 Pokémon spots (both Actives, both Benches of 3): HP left, damage, Energy per type, Tool,
    Special Conditions, stage, ex/Mega, retreat cost, and whether each attack is payable;
  - your own hand as card counts;
  - the opponent's hand size;
  - deck sizes, discard counts, points, turn number, the Energy Zone, the Stadium, and whether the Supporter or
    retreat was already used this turn.
- **Cards:** described by their numbers (HP, damage, costs, type) plus a small learned embedding of the card id,
  16 numbers per card. The numbers help the model handle cards it has rarely seen.
- **Actions:** legal actions differ every turn, so the model scores each action separately from features of that
  action: its kind, the card or Pokémon it involves, and the choice descriptor from M0. It then takes the best
  score (a softmax over the legal actions). No fixed action space is needed.
- **Data export:** `(state, legal actions, chosen action, final result)` per decision, to a binary file.

### M3. Small learned players (Python training, C++ playing)

- **Training:** in Python with PyTorch, on CPU. These are small MLPs: 2–3 layers of 128–256 units, about 100k
  numbers in all.
- **Playing:** in C++, from exported weights in a plain file, with a hand-written forward pass of about 60 lines and
  no new dependency. One decision should take tens of microseconds, so a game takes a few milliseconds.
- **Steps:**
  1. **Value network:** from the final results of many heuristic-vs-heuristic games, learn "how likely am I to win
     from here". Use it as a one-step-lookahead player: try each legal action and keep the one that leads to the
     best value. This is cheap and already much better than random.
  2. **Policy network by imitation:** learn to pick what the rollout player (M1) picks. Expensive search becomes
     a cheap, fast player.
  3. **Expert iteration:** a search (the rollouts, later a small Monte Carlo tree search over determinized deals)
     guided by the current networks makes better choices. The networks learn from those choices, and the loop
     repeats. This is AlphaZero's idea, scaled down, and it handles hidden cards by determinizing. Pure
     reinforcement learning from scratch (PPO and the like) is possible later, but games are long and the reward
     comes only at the end, so it learns slowly. Bootstrapping from search is the safer start.
- **Many decks:** train on a broad mix of decks, both generated (M4) and reference decks, so one player works for
  any deck. Optionally add a summary of its own deck list as input.
- **Accepting a new version:** it goes on the ladder (M1) and must beat the previous version clearly, for example
  55%+ over 2,000 mirrored games.

### M4. Deck building

- **Legal decks:**
  - 20 cards, at most 2 of a name, at least one Basic Pokémon;
  - Energy types chosen to match the Pokémon;
  - Star, Crown and Shiny cards ignored.
- **Generator, from Pokémon lines:**
  1. Pick 1–3 lines (a Basic and what evolves from it, or a Basic ex).
  2. Pick counts for each line, such as 2-2-1 or 2-2-2.
  3. Set the Energy types from those lines.
  4. Fill the rest with Trainers. Draw and search Trainers are likely; Trainers that only help one line go in only
     with that line.
  
  Every generated deck is legal and makes some sense.
- **Fitness:** win rate against an opponent pool, played by the current best player on both sides. To save games,
  use successive halving:
  1. Many candidates play 100 games each.
  2. The top half play twice as many.
  3. Repeat until a few remain with tight intervals (about 2,400 games for ±2%).
- **Search:** an evolutionary loop.
  - **Mutations:** swap a card, swap a whole line, change a line's counts, change an Energy type.
  - **Crossover:** combine lines from two parents.
  - **Selection:** by fitness.
  - **Hall of fame:** keep the best decks of earlier generations in the opponent pool. Otherwise the search chases
    whatever beats the current top deck and goes round in rock-paper-scissors circles.
- **Surrogate model (small ML for decks):** a network that predicts a deck's win rate from its list (the deck as a
  bag of card embeddings), trained on every deck already simulated. It screens mutations cheaply, and only promising
  ones get simulated. It also shows which cards tend to lift a deck.
- **Output:** for the top decks:
  - the list;
  - the win rate against the pool with its interval;
  - the matchup table;
  - a mix of decks that no single deck beats (the Nash equilibrium of the matchup table). This shows what's strong
    overall, not just against one opponent.

### M5. Both together

Repeat this loop:
1. Search decks with the current player.
2. Add the top decks to the training mix.
3. Train the next player version and check it on the ladder.
4. Search again.

Keep a fixed held-out deck pool and the older players on the ladder, so overfitting to one meta shows up.

## Rough compute budget

Assuming the engine runs about 10,000 games/s per thread without the checker (to be measured in M0):

| Player | Games/s on 12 threads | Use |
|---|---|---|
| Random or heuristic | ~100,000 | baselines, value-network data |
| Small network | ~3,000–10,000 | deck search, ladder |
| Rollout teacher | a few | imitation labels only |

One deck search with 200 decks per generation and 50 generations, after halving, takes about 10 million games:
roughly half an hour to a few hours with the network player. That fits on this machine; no GPU is needed for
networks this small.

## Order of work

1. Golden tests for the effect scripts (Next step 2). A player can only learn the right game if the cards are right.
2. M0, then M1 (the heuristic player alone already gives useful first deck rankings), then M4 with the heuristic
   player, then M2–M3, then M4 again with the learned player, then M5.

Deck search can start early with the heuristic player, so there are deck ideas before any ML is trained.

## Open questions

- **Python for training:** is PyTorch on this machine fine? Is there a GPU? CPU is enough.
- **Reference decks:** should the opponent pool include current tournament decks (Limitless publishes decklists),
  or only decks the search finds?
- **Owned cards:** should deck search be limited to the cards in the collection, or search everything?
- **Match format:** single games (as in ranked) or best-of-3?
