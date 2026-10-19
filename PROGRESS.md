# Progress

Updated 2026-10-19, after coverage batch 7.

This is the handoff file: what the project is, where it stands, what's open and what comes next. Read it first, then:
- [CLAUDE.md](CLAUDE.md) for the working rules and commands;
- [DESIGN.md](DESIGN.md) for how card effects work (§4) and every game rule with its source (§10).

## At a glance

A C++ simulator for Pokémon TCG Pocket. The goal is to play many games between decks and see which decks come out on top, as ideas for Remco to try in the game. It's Remco's personal side project, in a local git repo with no remote.

- **Engine:** most of the way there. It plays games by the rules Remco confirmed, for 85% of all cards, and an independent checker watches every move.
- **Around the engine:** not started yet. That means running many games at once, stats, finding decks and a better player.

| Part | State |
|---|---|
| Rules core: setup, turns, Energy Zone, attacking, retreating, evolving, Trainers, Tools, Stadiums with their actions (R5), abilities, Special Conditions, Checkup, damage, winning | Built and checked. Missing: the player picking which energy type to pay, discard or move (the engine picks; step 4 under Next) |
| Card data | Every set up to B4b: 2,319 unique cards from Limitless, cross-checked against deckgym-core |
| Card effects | 1,996 of 2,319 cards playable (86%). Trainers: 122 of 152 |
| Checks | 21 rule tests, 41 effect tests, 57 compiler self-test examples. `check_states`: 0 violations over 15 decks. Sanitizer build (memory and undefined behaviour) clean |
| Replayable games (seeds) | Not yet: every game seeds itself from `random_device` |
| Simulation runner (many games, threads, stats) | Not started |
| Finding decks (generate, play, rank) | Not started |
| Players | Random only. About 70% of random-vs-random games end in a draw at the turn cap; that's accepted for now |

- **Code size:** about 4,150 lines of C++ in the engine, 1,850 in tests, and 1,130 of Python (card data, plus the 900-line effect compiler).
- **Script language:** about 105 words.
- **Effect scripts:** 1,225 scripts in `data/effects.tsv`.

## Card coverage

`engine/build/tcgp --coverage`, 2026-10-19:

| Kind | Playable | At the 2026-10-16 review |
|---|---|---|
| Pokémon | 1,874 / 2,167 | 1,808 |
| Supporters | 68 / 82 | 40 |
| Items | 23 / 31 | 14 |
| Tools | 18 / 26 | 6 |
| Stadiums | 13 / 13 | 3 |
| **All** | **1,996 / 2,319 (86%)** | 1,871 (80%) |

**Trainers still missing (30):**
- **Knock-out triggers (6):** Rescue Scarf, Lucky Egg, Lucky Mittens, Electrical Cord, Hala, Iris.
- **Need a Pokémon trait the data doesn't mark (7):** Ultra Beasts (Beastite, Beast Wall, Lusamine), Ancient (Ancient Booster Energy Capsule, Professor Sada), Future (Future Booster Energy Capsule, Professor Turo). Neither Limitless nor deckgym-core says which Pokémon have these traits, so it would take a hand-made list.
- **One-offs (17):** Penny, Will, Drayden, May, Silver, Prank Spinner, Team Rocket's Boss, Quick-Grow Extract, Wallace, Acerola, Eevee Bag, Maintenance, Mythical Slab, Memory Light, Lucky Ice Pop, Puppy-Loving Girl, Clemont's Backpack.

**Pokémon still missing (293):** a long tail. The most common missing text is on 5 cards, and most are on 1 or 2. The biggest groups (`python3 compile_effects.py` lists them):
- **"Discard a card from your hand"** (5).
- **Puppy Pile** (5).
- **"This attack's damage isn't affected by Weakness / by effects on the Active"** (6).
- **Knock-out triggers on abilities:** Pyukumuku, Team Rocket's Electrode, Galarian Cursola.
- **Copying attacks:** "… and use it as this attack", as in Mew ex's Genome Hacking.
- **Choices like "choose either Poisoned or Confused"**.
- **Left out of batch 7:** Toxtricity (its bonus and its Paralysis sit in one clause, which can't run both before and after the damage), Archeops (a "+20 next turn" on the Bench), Cherubi and Revavroom (their Abilities: a cost cut with a Tool, and 2 Tools).

**Test decks:** `decks/coverage-a.txt` … `coverage-h.txt` hold the cards of each batch, so `check_states` plays them in random games. The other 7 decks (`pikachu`, `charizard`, `abilities`, …) are the older test decks.

## Checks: how to run them, and the last results

Run everything from the project folder (full list in CLAUDE.md):

```bash
cmake -S engine -B engine/build && cmake --build engine/build -j
engine/build/test_rules && engine/build/test_effects
engine/build/check_states decks/*.txt
python3 compile_effects.py          # regenerates data/effects.tsv and runs its self-test
engine/build/tcgp --coverage
```

The sanitizer build is `engine/build-debug`, configured with `-fsanitize=address,undefined`. It's about 8× slower, so run `check_states` on 2–4 decks there.

Last results, 2026-10-19 after batch 7:
- all tests pass;
- the self-test passes (57 examples);
- `check_states`: all 15 decks, 0 violations;
- sanitizer build: tests plus `check_states` on `coverage-h` and `coverage-g`, clean.

**Speed:** about 2,000 games/s on one thread with full state checking after every move (measured 2026-10-16). The engine alone is faster but hasn't been measured. deckgym-core quotes about 3,300 random games/s.

**What `check_states` verifies after every move** (among others):
- every card of both decks is in exactly one place, counted over both players because cards can change hands;
- a knocked-out Pokémon leaves play at once;
- hands stay at 10 or fewer;
- evolution stacks are correct;
- discard-pile energy plus attached energy never goes down;
- each kind of move was legal;
- the winner follows the win rules.

## What was done

**2026-10-16, foundation and rules:**
- **Baseline:** the engine, the effect-script system and the compiler.
- **Card data:** moved from TCGdex to Limitless, cross-checked with deckgym-core (see "Card data" below).
- **Rule fixes R1–R4:** no Pokémon in play loses; Weakness and the floor at 0; evolving resets "used its ability"; the Energy Zone with current and next energy, and energy in the discard pile.
- **Something to do:** Trainers can only be played when their effect has something to do.

**2026-10-16, code review.** It found 4 bugs. Each was confirmed with a test, then fixed in its own commit with a test that holds it:
- **"For each Energy attached to all of your (opponent's) Pokémon"** counted only the Active. Affected: Espeon, Zoroark, Eelektross, Mega Diancie ex.
- **Lt. Surge** moved energy from one Benched Pokémon instead of all of them. The compiler now reads an unnumbered plural as "all".
- **Whitney** also cured Poison and Burn: `cure` ignored its list.
- **Replacing an HP Stadium** left Pokémon with too much damage in play. Playing a Stadium now runs the knock-out check.

**2026-10-16, Remco's rule answers, built:**
- **Golisopod's "moved to the Active Spot this turn"** belongs to the card on top.
- **Abilities** need something to do, like Trainers.
- **Random multi-hit attacks** (Wugtrio ex) add up the hits on each Pokémon first, then deal them once, so Weakness and bonuses count once.

**2026-10-16 to 2026-10-19, coverage batches.** Each batch got:
- patterns in `compile_effects.py`;
- small engine blocks;
- an engine test and self-test examples;
- a review of every new line in `effects.tsv` against its card text;
- a test deck;
- `check_states` at 0 violations;
- a sanitizer run;
- its own commit.

No existing script changed in any batch unless the batch said so.

| Batch | Commit | Trainers made playable | New engine pieces | Pokémon unlocked along the way |
|---|---|---|---|---|
| 0 | `25068e1` | none | Every word declares how many arguments it takes; a script with too many or too few doesn't load | none |
| 1 | `14281b4` | 20: Mars, Lisia, Serena, Calem, Korrina, Cabbie, Juliana, Arven, Skyla, Irida, Diantha, Cheren, Order Pad, TR Goo-zooka, TR Researcher, TR Master Plan, Arena of Antiquity, Small Balloon, Elegant Cape, Deceptive Needle | Card filters (stadium, mega, max-hp, name-has), Pokémon filters (mega, has-energy), `points-needed`, `healed`, `:from-ex` | Ambipom |
| 2 | `33abb77` | 9: Lana, Piers, Juggler, Psychic, Kiawe, Big Malasada, Nasty Notice, Team Star Grunt, Field Blower | "You can use this card only if…" → `(if …)`; `end-turn`, `one-of`, `cure-random`, `discard-hand-to`, `discard-energy-among`, `discard-stadium`, `move-random-energy`, `stadium-in-play`, `has-attack`, `energy-types` | 12: Gyarados ex, Swampert, Ceruledge ex, Machop, Conkeldurr, Tornadus, Gallade, Grafaiai, 4 Castforms |
| 3 | `231e469` | 7: Celestic Town Elder, Fishing Net, Fisher, Flame Patch, Volkner, Pokémon Flute, TR Thieving Machine | `from-discard`, `attach-from-discard`, `discard-count`. "If you do, B" → `(then A B)`. Piles hold 40 cards (cards can change hands) | 12, among them Flareon ex, Delcatty, Galarian Perrserker, Chandelure, Houndstone, Hisuian Zoroark ex, Rotom ex |
| 4 (R5) | `962abbb` | 9 Stadiums: Mesagoza, Fragrant Forest, Area Zero, Kid's Room, Rainbow Cave, Arcade, Hiking Trail, Soothing Shore, Bounded Field. All 13 Stadiums now work | The Use Stadium move; the end-of-each-turn Stadium trigger; Weakness ×2; `draw-until`, `hand-to-deck`, `swap-for`, `cycle-energy` | none |
| 5 | `ec9ebe8` | 9 Tools: Lum Berry, Sitrus Berry, Poison Barb, Dark Pendant, Steel Apron, Clear Veil, Metal Core Barrier, Protective Poncho, Heavy Helmet | The `on-damaged` and `at-end-of-each-turn` triggers; `no-conditions`, `prevent-effects`, `prevent-ability-damage`; `:bench`, `:min-retreat`; `half-hp`, `my-turn`, `status-count` | 7: Regice, Arceus ex, Wartortle, Jellicent, Dragalge ex, Blissey, TR Magmar |
| 6 | `be4c2e5` | 5: Koga, Budding Expeditioner, Ilima, Parasol Lady, Elesa | `to-hand`, `to-deck`, `tools-to-hand`, `not-ex` | 3: Eldegoss, Dunsparce, Accelgor |
| 7 | see `git log` | none | Memory of the last turn in the state: `ko-last-turn`, `used-last-turn`, `damaged-last-turn`, `points-last-turn`. Compiler only: "takes +N" next turn, "this Pokémon's attack does +N" next turn (one-attack cards only), "your Pokémon do +N" next turn | 33: 9 "Knocked Out last turn" (Marshadow, Lapras, Zarude, …), 7 Sweets Relay, 9 "+N next turn" (Togekiss, Donphan, Crabominable ex, …), 3 "your Pokémon +N next turn" (Oricorio, Meloetta, Happiny), 3 "takes +N" (Kommo-o, Lucario, Hariyama), Wobbuffet, Hisuian Basculegion |

## Rules

DESIGN.md §10 lists every rule the engine follows, with its source:
- **App:** Remco checked it in the game;
- **Remco:** his reading of the card text;
- **FAQ, Tips, Card:** official sources;
- **Assumed:** no source.

**The rule for changes:** a game rule only changes after Remco has clarified it. Never change one on a guess; ask, then write his answer into §10.

**Answered by Remco and built** (newest first):
- "Knocked Out by damage from an attack during your opponent's last turn" counts Active and Benched Pokémon knocked out by that attack, not Poison or Burn at Checkup, nor Ability damage.
- A point scored at Checkup at the end of a player's turn counts as "during their last turn" (Hisuian Basculegion).
- Stadium effects "at the end of each player's turn" come last, after end-of-turn abilities and after Checkup.
- Rainbow Cave works like an extra turn for the Energy Zone.
- Bounded Field's ×2 takes the place of +20.
- A Pokémon put back into the hand takes its evolutions and Tool with it; its energy goes to the discard pile. With an empty Bench, that player loses (a way to concede).
- Random multi-hit attacks: hits add up per Pokémon, then Weakness and bonuses apply once.
- "Moved to the Active Spot this turn" belongs to the card on top.
- Abilities need something to do.
- When energy is moved or discarded, the player picks the type. Not built yet: step 4 under Next.
- Earlier (sources in §10): R1–R4; Trainers need something to do; effects that start something can always be played (Blaine, X Speed); after a double knock-out, the defending player picks a new Active first.

**Assumed, and still to confirm:**
- An attack that doesn't happen (Confused, or a coin-flip effect, tails) wasn't "used", so it doesn't count for Sweets Relay.
- Lum Berry and Sitrus Berry act at the end of each turn, before Checkup. That's Remco's best guess; he said he'd double-check.
- Which Energy Zone type comes up is random and even across the deck's types.
- Dealing an opening hand reshuffles until the top 5 cards hold a Basic.
- A defender's "takes −N damage" also applies on the Bench.
- Evolving ends other effects on the Pokémon.

**Open questions for Remco:**
- **Team Rocket's Thieving Machine:** once you play the Item it took, does it go to your discard pile (assumed) or back to your opponent's?
- **Back into a full hand:** when Koga or Elesa puts cards into a hand that already holds 10, the engine sends the extra cards to the discard pile (assumed).
- **Berries:** confirm "before Checkup".
- **Sweets Relay:** does an attack that didn't happen (Confused, tails) count as "used Sweets Relay"? Assumed: no.

## Shortcuts

These are marked `shortcut:` in the code:
- **Energy type when paying for a retreat or discarding:** the engine picks the most plentiful type. The player should pick.
- **Energy type when moving energy** (Elemental Switch, Dawn, "Move an Energy"): the engine takes the first matching type. Remco says the player picks.
- **Fossils:** a Fossil can't be discarded from play by choice.
- **"Look at the top cards and put them back in any order"** (Hiker, Morty) does nothing.
- **Clemont's Backpack:** its "+20 to your opponent's Pokémon" isn't supported, because damage bonuses only count against the Active. The card stays unsupported.
- **Timed effects:** at most 16 at once; past that, the oldest is dropped. This has never happened in a game.

## Known weak spots and risks

- **Card text → scripts is pattern-based.** A pattern that matches but means something slightly different makes a card play wrong without any check noticing. That's how three of the four review bugs got in.
  - Guards today: the `effects.tsv` diff reviewed per change, the self-test examples, the argument-count check, the engine tests and `check_states`.
  - Missing: real-card golden tests (step 2 under Next).
- **Numbers and tests look at the first Pokémon only.** Words like `has-damage` and `is-ex` read only the first Pokémon a target picks. With a `(choose …)` target, they would even ask for a choice in the middle of an evaluation. No script does that today.
- **Choices inside effects call the player directly** (`Game::choose()`). That's fine for random and simple players. A search player would need the pending choice stored in the game state.
- **"Something to do" is lenient for sequences.** `(seq A B)` counts as possible when any part is possible. `then` is the strict version, used for "If you do".
- **No seeds,** so a failure from `check_states` can't be replayed.
- **The random player favours simple decks,** and most of its games end in a draw at the turn cap.

## Next

In order. Each step gets a short plan, then tests, `check_states` at 0 violations, a sanitizer run and a commit. For coverage batches, Remco said (2026-10-19): commit freely, run batches back to back, and report every two batches or so. Rules still go to him first.

1. **More card coverage, in batches.** Candidates, in the order I'd suggest:
   - **Batch 8, knock-out triggers:** Rescue Scarf, Lucky Egg, Lucky Mittens, Electrical Cord, Hala, Iris, plus abilities such as Pyukumuku, Team Rocket's Electrode and Galarian Cursola. This needs a knock-out hook in `Game::knockOut`, and probably rule questions for Remco: the order of triggers, and the points for Iris.
   - **Batch 9, the long tail of simple conditions and numbers:** "has no damage on it", "has an Ability", "more remaining HP than this Pokémon", "Evolution Pokémon on your Bench", "exactly N cards in your hand", "isn't affected by Weakness / by effects", "discard a card from your hand".
   - **Traits:** a hand-made list of Ultra Beasts and Ancient/Future Pokémon, from Serebii, would unlock 7 Trainers plus some Pokémon. Ask Remco first, since it's card data.
   - **One-offs:** only if a deck needs them.
2. **Effect-script checks:** real-card golden tests, meaning card text plus its expected script, a few per sentence family; a review of the `effects.tsv` diff for each new set; and `data/effects_manual.tsv` for odd cards. Remco chose to do this after coverage.
3. **Seeds:** one seed per game, from which the shuffles, coins and random player all follow. Then failures can be replayed, and two decks can be compared on the same luck.
4. **Player choices the engine makes today:** which energy type to pay, discard or move (Remco: the player picks), and discarding a Fossil.
5. **The simulation runner (milestone 3):** many games over threads; win, draw and loss per pairing with a confidence interval; the first player alternates. About 2,400 games per pairing give ±2%.
6. **Finding decks (milestone 4):** generate legal decks around Pokémon lines, play them against each other, rank them, and list the top ones to try in the game.
7. **A minimal player** (attach energy, attack when possible), if the random player's draws and bias get in the way of step 6. No real bots yet (Remco).

Dropped: R6, source tags on each constant in `rules.h`. DESIGN.md §10 already gives each rule's source.

## Working agreements

- **Goal:** a correct engine first, meaning the right cards and the right rules. Stats, deck search and bots come later.
- **No card is special-cased in C++.**
  - A new mechanic becomes a new block or modifier, used by every card with that text.
  - Rare one-of-a-kind mechanics stay unsupported on purpose and show up in `--coverage`.
- **Rules:** never change one on a guess. Ask Remco; he checks in the app. His answers go into DESIGN.md §10.
- **Git:** a local repo with no remote; commit each finished step, with the attribution line from CLAUDE.md. The generated `data/*.tsv` files are committed so data changes show up as diffs.
- **Ignore Star, Crown and Shiny cards:** they're alt-art reprints.
- **The APK in `apkm/` and `x/` is unused,** and its assets are encrypted. We don't decrypt them.
- **Players:** the random player, or at most a minimal one, is enough for now.

## How the design holds up

The design works and needs no rework now. DESIGN.md §12 has the comparison with other engines.
- **Card effects as data:** card text compiles into small scripts built from shared blocks.
  - MTG Arena and Forge also write effects as per-card scripts. What's unusual here is that sentence patterns write the scripts from the card text.
  - deckgym-core, the closest Pocket simulator, writes Rust code per card instead and covers about 89%.
- **One damage pipeline in a fixed order** (base → attacker's effects → Weakness → defender's effects → floor at 0). Modifiers answer fixed questions, so every rule fix stayed small.
- **Plain, copyable game state:** ready for threads and, later, a search player. Pokémon are identified by a `uid`, so effects survive switching.
- **Hidden information:** players only see a `PlayerView`, so they can't peek.

## Card data

- **Source:** Limitless (pocket.limitlesstcg.com), every set up to B4b, cross-checked with `check_cards.py` against deckgym-core's `database.json` (0 unexplained differences).
- **Settling disagreements:** use Serebii. Real Limitless errors go into `FIXES` in `export_cards.py`; so far only Jigglypuff P-A-022's HP.
- **TCGdex** was dropped (2026-10-16): it stopped at B2a and had errors, among them wrong weaknesses, costs and damage, and missing " ex" on five cards.
- **Reprints** are merged into the original print.
- **A new set:**
  1. run `fetch_cards.py`, `export_cards.py` and `compile_effects.py`;
  2. run `check_cards.py` and expect 0 differences;
  3. review the `effects.tsv` diff.
