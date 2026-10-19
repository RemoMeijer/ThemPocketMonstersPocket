# Pokémon TCG Pocket simulator

Remco's personal side project, not Odd.Bot work. A local git repo with no remote: commit each finished step. The generated `data/*.tsv` files are committed so data changes show up as diffs.

## Goal

Run many simulated games and see which decks come out on top, as ideas for Remco to try in the game. A fast C++ engine plays Pokémon TCG Pocket games between decks, many at a time in memory.

The current focus is a correct engine: the right cards and the right rules. Stats, deck search, a real player (bot) and maybe a small LLM come later.

## Where we are

- **Read first:** [PROGRESS.md](PROGRESS.md) for the current state, shortcuts and next options, and [DESIGN.md](DESIGN.md) for how card effects are structured.
- **Done:**
  - the rules core: milestones 1–2, the cleanup, rule fixes R1–R4, Trainers and abilities that need something to do before they can be used, and the 4 bug fixes from the 2026-10-16 review;
  - the effect system;
  - card data from Limitless, every set up to B4b: 1,930 of 2,319 cards (83%) are playable;
  - coverage batches 0–3 (argument-count check, 36 Trainers).
- **Next** (PROGRESS.md, "Next"):
  - Trainer coverage, batches 4–6 (batch 4 is R5, Stadium actions);
  - effect-script checks (golden tests);
  - seeds;
  - player choices;
  - the simulation runner (milestone 3);
  - finding decks (milestone 4).

## Layout

```
fetch_cards.py      Limitless → data/raw/limitless/  (two pages per set; re-run when a new set comes out)
export_cards.py     those pages → data/cards.tsv     (drops Star/Crown/Shiny alt arts, dedupes, Fossils as Pokémon, FIXES)
check_cards.py      cards.tsv vs deckgym-core's database, a second source: prints every difference
compile_effects.py  card text → data/effects.tsv   (sentence patterns → effect scripts, has a self-test)
decks/*.txt         "energy: Water" + "2 A1-053" lines
engine/src/
  core/     rules.h (every rule number), state.h (plain, copyable game state), action.h, view.h
  cards/    card and deck loading, deck rules
  game/     turn flow, Pokémon Checkup, knock-outs, modifiers, damage pipeline (damage.cpp)
  moves/    one class per kind of move: its legal moves and what it does
  effects/  script parser, builder, blocks_*.cpp (one file per family of words)
  players/  Policy interface, RandomPolicy
engine/tests/  test_rules, test_effects, check_states (state checker over random games)
apkm/, x/   the unpacked APK: unused
```

## Commands

Run everything from the project folder.

Rebuild the card data:

```bash
python3 fetch_cards.py && python3 export_cards.py && python3 compile_effects.py
```

Cross-check the card data against a second source (it should print 0 differences):

```bash
python3 check_cards.py
```

Build:

```bash
cmake -S engine -B engine/build && cmake --build engine/build -j
```

Run the tests:

```bash
engine/build/test_rules && engine/build/test_effects
```

Run the state checker:

```bash
engine/build/check_states decks/pikachu.txt decks/charizard.txt decks/abilities.txt
```

Play and print one game:

```bash
engine/build/tcgp decks/pikachu.txt decks/gardevoir.txt
```

See which cards are supported and why the rest aren't:

```bash
engine/build/tcgp --coverage
```

For memory and undefined-behaviour checks, `engine/build-debug` is configured with `-fsanitize=address,undefined`.

## Working rules

- **Step by step:** plan each milestone, explain it, and wait for a go before writing code.
- **No card is special-cased in C++:**
  - card behaviour comes from data and effect scripts;
  - a new mechanic is a new block or modifier, used by every card with that text;
  - rare one-off mechanics are skipped on purpose: they stay unsupported and show up in `--coverage`.
- **Rules:**
  - never change a game rule on a guess: ask Remco first, since he can check in the app, and write his answer in DESIGN.md §10;
  - rule numbers live in `core/rules.h`;
  - DESIGN.md §10 lists every rule; anything still guessed is marked "assumed" there and is an open question in PROGRESS.md;
  - don't present guesses as facts.
- **Players:** the random player, or at most a minimal one, is enough for now. No real bots yet: games only have to follow the rules and use the right actions.
- **Ignore Star, Crown and Shiny cards.** They're alt-art reprints.
- **Card data:**
  - it comes from Limitless (pocket.limitlesstcg.com), cross-checked with `check_cards.py` against deckgym-core;
  - settle a difference with a third source (serebii.net/tcgpocket), and put real Limitless errors in `FIXES` in `export_cards.py`;
  - TCGdex stopped adding Pocket sets after B2a, and its data has errors, so we no longer use it;
  - the APK's assets are encrypted and we don't decrypt them.
- **Before calling something done:**
  - the tests pass;
  - `check_states` reports 0 violations;
  - `compile_effects.py`'s self-test passes;
  - after a data change, `check_cards.py` reports 0 differences;
  - when adding patterns, compare a sample of compiled scripts with their card text.
- **Known shortcuts:** marked `shortcut:` in the code and listed in PROGRESS.md.
