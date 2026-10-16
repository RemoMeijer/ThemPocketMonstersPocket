# Pokémon TCG Pocket simulator

Remco's personal side project, not Odd.Bot work. Not a git repo.

## Goal

Find decks that work surprisingly well. A fast C++ engine plays Pokémon TCG Pocket games between decks, many at a time in memory. Later we add stats, deck search, and a small LLM that suggests what to try next.

## Where we are

- **Read first:** [PROGRESS.md](PROGRESS.md) for the current state, shortcuts and next options, and [DESIGN.md](DESIGN.md) for how card effects are structured.
- **Done:**
  - base rules (milestones 1–2);
  - core cleanup;
  - the effect system, with 85% of cards playable.
- **Next:** milestone 3 (simulation runner: threads, seeds, stats), or more card coverage.

## Layout

```
fetch_cards.py      TCGdex API → data/cards.json   (re-run when a new set comes out)
export_cards.py     cards.json → data/cards.tsv    (drops Star/Crown/Shiny alt arts, dedupes, Fossils as Pokémon)
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
  - rule numbers live in `core/rules.h`;
  - guesses are marked "unconfirmed" or "assumed" there and listed in DESIGN.md §10;
  - don't present them as facts.
- **Players:** keep the random player for now. Remco said not to add player types or bots yet.
- **Ignore Star, Crown and Shiny cards.** They're alt-art reprints.
- **Card data:** only from TCGdex. The APK's assets are encrypted and we don't decrypt them.
- **Before calling something done:**
  - the tests pass;
  - `check_states` reports 0 violations;
  - `compile_effects.py`'s self-test passes;
  - when adding patterns, compare a sample of compiled scripts with their card text.
- **Known shortcuts:** marked `shortcut:` in the code and listed in PROGRESS.md.
