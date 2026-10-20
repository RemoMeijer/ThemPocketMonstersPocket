# Pokémon TCG Pocket simulator

A personal side project, not Odd.Bot work. A local git repo with no remote: commit each finished step. The generated `data/*.tsv` files are committed so data changes show up as diffs.

## Goal

Run many simulated games and see which decks come out on top, as ideas to try in the game. A fast C++ engine plays Pokémon TCG Pocket games between decks, many at a time in memory.

The current focus is a correct engine: the right cards and the right rules. Stats, deck search, a real player (bot) and maybe a small LLM come later.

## Where we are

- **Read first:** [PROGRESS.md](PROGRESS.md), the handoff file: status, coverage, what was done per batch, rules and open questions, shortcuts, weak spots and the next steps. Then [DESIGN.md](DESIGN.md) for how card effects are structured (§4) and every rule with its source (§10).
- **Done:**
  - the rules core: milestones 1–2, the cleanup, rule fixes R1–R4, Trainers and abilities that need something to do before they can be used, and the 4 bug fixes from the 2026-10-16 review;
  - the effect system;
  - card data from Limitless, every set up to B4b: all 2,319 cards are playable;
  - coverage batches 0–12 (batches 11–12: the one-offs, hand-written in `data/effects_manual.tsv`; Trainers: 144 of 152 now; all 13 Stadiums and all 26 Tools; memory of the last turn; knock-out triggers; about 70 Abilities; the long tail; Ultra Beast, Ancient and Future traits in `data/traits.tsv`).
- **Next** (PROGRESS.md, "Next"):
  - card coverage is done: every card plays;
  - player choices;
  - the simulation runner (milestone 3);
  - finding decks (milestone 4);
  - players and deck search with small learned models: [AI_PLAN.md](AI_PLAN.md).

## Layout

```
fetch_cards.py      Limitless → data/raw/limitless/  (two pages per set; re-run when a new set comes out)
export_cards.py     those pages → data/cards.tsv     (drops Star/Crown/Shiny alt arts, dedupes, Fossils as Pokémon, FIXES)
data/traits.tsv     Ultra Beast / Ancient / Future by card name, hand-made from Serebii (not in Limitless or deckgym)
check_cards.py      cards.tsv vs deckgym-core's database, a second source: prints every difference
check_all.sh        check_states over all decks in parallel (--row K: deck K against every deck)
compile_effects.py  card text → data/effects.tsv   (sentence patterns → effect scripts, has a self-test)
data/effects_manual.tsv  hand-written scripts for one-off cards; a card listed there skips the patterns
decks/*.txt         "energy: Water" + "2 A1-053" lines
engine/src/
  core/     rules.h (every rule number), state.h (plain, copyable game state), action.h, view.h
  cards/    card and deck loading, deck rules
  game/     turn flow, Pokémon Checkup, knock-outs, modifiers, damage pipeline (damage.cpp)
  moves/    one class per kind of move: its legal moves and what it does
  effects/  script parser, builder, blocks_*.cpp (one file per family of words)
  players/  Policy interface, RandomPolicy, HeuristicPolicy (rule-based; recognizes Trainers by their scripts)
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

Run the state checker over every deck, one process per deck on all cores (about 5 minutes for 31 decks; it prints the violations and their total; `--policy heuristic` on `check_states` plays the heuristic player instead of the random one):

```bash
./check_all.sh
```

The same with the sanitizer build (about 8× slower, so 200 games per pairing instead of 2,000):

```bash
GAMES=200 ./check_all.sh engine/build-debug/check_states
```

A few decks only, in one process:

```bash
engine/build/check_states decks/pikachu.txt decks/charizard.txt decks/abilities.txt
```

Play and print one game:

```bash
engine/build/tcgp decks/pikachu.txt decks/gardevoir.txt
```

Win rate of one deck against another, with a player for each (`random` or `heuristic`; the first player alternates):

```bash
engine/build/tcgp --match 2000 decks/pikachu.txt decks/charizard.txt heuristic heuristic
```

See which cards are supported and why the rest aren't (`--unsupported` lists each card left, with its reason):

```bash
engine/build/tcgp --coverage
```

For memory and undefined-behaviour checks, `engine/build-debug` is configured with `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer` (`-O1`: unoptimized it runs far slower).

## Working rules

- **Step by step:** plan each milestone, explain it, and wait for a go before writing code.
  - Exception: card-coverage batches run back to back, each with its own commit, and a progress report every two batches or so (2026-10-19). Rule questions are asked first. Big batches work best: survey many cards, ask every rule question at once, build with the quick checks, then run `./check_all.sh` and the sanitizer once and fix everything together.
- **No card is special-cased in C++:**
  - card behaviour comes from data and effect scripts;
  - a new mechanic is a new block or modifier, used by every card with that text;
  - rare one-off mechanics are skipped on purpose: they stay unsupported and show up in `--coverage`.
- **Rules:**
  - never change a game rule on a guess: ask first (rules can be checked in the app), and write the answer in DESIGN.md §10;
  - rule numbers live in `core/rules.h`;
  - DESIGN.md §10 lists every rule; anything still guessed is marked "assumed" there and is an open question in PROGRESS.md;
  - don't present guesses as facts.
- **Players:** the random player and the rule-based heuristic player. Learned players come later (AI_PLAN.md).
- **Ignore Star, Crown and Shiny cards.** They're alt-art reprints.
- **Card data:**
  - it comes from Limitless (pocket.limitlesstcg.com), cross-checked with `check_cards.py` against deckgym-core;
  - settle a difference with a third source (serebii.net/tcgpocket), and put real Limitless errors in `FIXES` in `export_cards.py`;
  - TCGdex stopped adding Pocket sets after B2a, and its data has errors, so we no longer use it;
  - the APK's assets are encrypted and we don't decrypt them.
- **Before calling something done:**
  - the tests pass;
  - `./check_all.sh` reports 0 violations, and the sanitizer build is clean on the new decks;
  - `python3 compile_effects.py --test` passes: the self-test and the golden test (`data/golden.tsv`, 487 reviewed real cards). No existing script in `data/effects.tsv` changed unless that was the point; after an intended change, review the diff and run `--accept-golden`;
  - after a data change, `check_cards.py` reports 0 differences;
  - when adding patterns, check every new script against its card text.
- **Keeping the context small** (a long session fills up fast):
  - one session per batch or milestone; PROGRESS.md is the handoff, so a fresh session starts from it, not from the chat;
  - print only what's needed: counts and the failing lines (`tail`, `grep`, `head`), never whole files or full logs;
  - hand bulk reading to a subagent that reports only the result, e.g. "check these 150 new scripts against their card text, list only the mismatches";
  - write longer edits as a script file in the scratchpad and run it, instead of repeating code in the chat;
  - keep reasoning to short notes.
- **Known shortcuts:** marked `shortcut:` in the code and listed in PROGRESS.md.
