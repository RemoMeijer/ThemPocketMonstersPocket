# Progress

Updated 2026-10-20, after coverage batch 12 (every card playable).

This is the handoff file: what the project is, where it stands, what's open and what comes next. Read it first, then:
- [CLAUDE.md](CLAUDE.md) for the working rules and commands;
- [DESIGN.md](DESIGN.md) for how card effects work (§4) and every game rule with its source (§10).

## At a glance

A C++ simulator for Pokémon TCG Pocket. The goal is to play many games between decks and see which decks come out on top, as ideas to try in the game. It's a personal side project, in a local git repo with no remote.

- **Engine:** most of the way there. It plays games by the settled rules, for every card, and an independent checker watches every move.
- **Around the engine:** not started yet. That means running many games at once, stats, finding decks and a better player.

| Part | State |
|---|---|
| Rules core: setup, turns, Energy Zone, attacking, retreating, evolving, Trainers, Tools, Stadiums with their actions (R5), abilities, Special Conditions, Checkup, damage, winning | Built and checked. Missing: the player picking which energy type to pay, discard or move (the engine picks; step 4 under Next) |
| Card data | Every set up to B4b: 2,319 unique cards from Limitless, cross-checked against deckgym-core |
| Card effects | All 2,319 cards playable. Trainers: 152 of 152 |
| Checks | 21 rule tests, 45 effect tests, 86 compiler self-test examples. `check_states`: 0 violations over 30 decks. Sanitizer build (memory and undefined behaviour) clean |
| Replayable games (seeds) | Not yet: every game seeds itself from `random_device` |
| Simulation runner (many games, threads, stats) | Not started |
| Finding decks (generate, play, rank) | Not started |
| Players | Random, and a rule-based heuristic player (`players/heuristic.cpp`, 2026-10-20). Heuristic vs random wins about 99%; heuristic vs heuristic ends in a draw about 2% of the time (random vs random: about 70%), at about 5,300 games/s on one thread |

- **Code size:** about 4,550 lines of C++ in the engine, 2,125 in tests, and 1,130 of Python (card data, plus the 900-line effect compiler).
- **Script language:** about 200 words.
- **Effect scripts:** 1,553 scripts in `data/effects.tsv`, 49 cards of them hand-written in `data/effects_manual.tsv`. **Traits:** `data/traits.tsv` (Ultra Beast, Ancient, Future by card name, from Serebii).

## Card coverage

`engine/build/tcgp --coverage`, 2026-10-20 (`tcgp --unsupported` lists the cards left, with their reasons):

| Kind | Playable | At the 2026-10-16 review |
|---|---|---|
| Pokémon | 2,167 / 2,167 | 1,808 |
| Supporters | 82 / 82 | 40 |
| Items | 31 / 31 | 14 |
| Tools | 26 / 26 | 6 |
| Stadiums | 13 / 13 | 3 |
| **All** | **2,319 / 2,319 (100%)** | 1,871 (80%) |

**One-off cards** (batch 11) have hand-written scripts in `data/effects_manual.tsv`, not sentence patterns: a card listed
there skips the patterns entirely. Each card has a `#` note line.

**Test decks:** `decks/coverage-a.txt` … `coverage-x.txt` hold the cards of each batch, so `check_states` plays them in random games. The other 7 decks (`pikachu`, `charizard`, `abilities`, …) are the older test decks.

## Checks: how to run them, and the last results

Run everything from the project folder (full list in CLAUDE.md):

```bash
cmake -S engine -B engine/build && cmake --build engine/build -j
engine/build/test_rules && engine/build/test_effects
./check_all.sh                      # check_states over all decks, one process per deck, all cores
python3 compile_effects.py          # regenerates data/effects.tsv and runs its self-test
engine/build/tcgp --coverage
```

The sanitizer build is `engine/build-debug`, configured with `-O1 -fsanitize=address,undefined`. Run it at 200 games per
pairing: `GAMES=200 ./check_all.sh engine/build-debug/check_states` (about 13 minutes for all decks on 12 cores), or one
new deck's row: `engine/build-debug/check_states --games 200 --row K decks/*.txt` (about 2 minutes).

Last results, 2026-10-20 after batch 12:
- all tests pass;
- the self-test passes (86 examples), and `check_cards.py` reports 0 differences;
- `check_states` (`./check_all.sh`): all 31 decks, 151M moves, 0 violations;
- sanitizer build: tests pass; all 30 decks of batch 11 at 200 games per pairing, then the `coverage-x` row: no runtime
  errors, 0 violations.

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

**2026-10-16, rulings, built:**
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
| 7 | `c599c02` | none | Memory of the last turn in the state: `ko-last-turn`, `used-last-turn`, `damaged-last-turn`, `points-last-turn`. Compiler only: "takes +N" next turn, "this Pokémon's attack does +N" next turn (one-attack cards only), "your Pokémon do +N" next turn | 33: 9 "Knocked Out last turn" (Marshadow, Lapras, Zarude, …), 7 Sweets Relay, 9 "+N next turn" (Togekiss, Donphan, Crabominable ex, …), 3 "your Pokémon +N next turn" (Oricorio, Meloetta, Happiny), 3 "takes +N" (Kommo-o, Lucario, Hariyama), Wobbuffet, Hisuian Basculegion |
| 8 | `3dfc7a5` | 6: Rescue Scarf, Lucky Egg, Lucky Mittens, Electrical Cord, Hala, Iris | Knock-outs as a loop: triggers first, then points, then the win rules. `on-knocked-out` (`:active`, `:by-attack`), `on-knocks-out`, `no-points`, `to-hand-instead`, `knock-out`, `discard-this`, `not-picked`. Modifiers `survive-at` (`:flip`), `extra-point`, `extra-tools`, and the options `:if COND` and `:energy T`. A Pokémon holds a list of Tools (two for Revavroom). Fixed on the way: Sweets Relay read a missing "last attack" on turn 1 (batch 7), and evolving into less HP than the damage (after Starting Plains' +20) didn't knock out | 14: Team Rocket's Electrode, Spiritomb, Galarian Cursola, Passimian ex, Zoroark, Garchomp B4a, Dusknoir B1, Glimmora, Conkeldurr, Ursaluna, Bewear, Cherubi, Revavroom, Pyukumuku |
| 9 | `0ca479c` | 1: Memory Light | Ability forms `during-checkup`, `at-start-of-turn`, `on-bench`, `on-opp-attach`; activated `:bench` and `:repeat`. Words `move-damage`, `discard-from-hand`, `discard-pokemon`, `evolve-from-deck`, `first-turn`, `is-active`, `hp-left`, `other-ability`. Modifiers `cant-stadium`, `cant-evolve-active`, `no-healing`, `poison-damage`, `prevent-first-hit`, `evolve-early`, `evolves-as :as`, `no-abilities`, `extra-type`, `double-energy`, `previous-attacks`; options `:evolves-from`, `:with-energy`, `:status`, `:flip` on damage-taken, `:energy` per Energy on hp. `Game::energyOf` (Serperior), `attacksOf` (Celebi), `abilitiesOff` (Alolan Muk), `PokemonSlot::hasType` (Urshifu). Fixed on the way: retreat payment with a doubled Energy; "you have X in play" read "Arceus or Arceus ex" and "another Beldum" as one name; the checker's plain-board, evolve, retreat, repeat-Ability and draw checks | 71, among them Serperior ×2, Celebi, Alolan Muk, both Urshifu, Eevee and Eevee ex, Jolteon ex, Glaceon ex, Vaporeon, Sylveon, Garganacl, Flygon ex, Darkrai, Caterpie, Miraidon ex, Koraidon ex, Dusknoir, Klefki, Claydol, Mimikyu ex, Regigigas, the two Unown, Reuniclus, Comfey, Teal Mask Ogerpon ex, and five "discard a card from your hand" attackers |
| 10 | `16445a8` | 15: Beast Wall, Beastite, Lusamine, the Booster Energy Capsules, Professor Sada, Professor Turo, Silver, Acerola, Quick-Grow Extract, Wallace, Lucky Ice Pop, Eevee Bag, Mythical Slab, Puppy-Loving Girl | Traits (`data/traits.tsv`, `(trait …)`, `:trait`, `:per-point`); `(hit :no-weakness :ignore-effects)`; duration `while-active`; `copy-attack`, `set-hp-left`, `halve-hp`, `status-random`, `attach-random-type`, `change-energy-type`, `change-next-energy`, `opp-hand-to-deck`, `opp-hand-discard`, `top-card-if`, `discard-hand-random-of`, attach-from-discard `random` and `different`; numbers `points`, `tools`, `ko-count`, `deck-count`, `types-on`, `with-attack`, `dealt`; tests `evolved-from`, `shared-energy`, `at-zero`; filters `name-has`, `evolves-from`, `not-new`, `max-hp`, `trait`, `has-attack`; modifiers `no-weakness`, `cant-trainer`, `cant-evolve`, `:from-basic`. Stage 1 cards onto the Bench (Silcoon). Compiler: "If …," may split at any comma; joined multi-sentence rules | about 166, among them Mew ex, Ditto-like copiers, Hitmonchan ex, Starmie, Silcoon, Cascoon, Kingambit, Gholdengo ex, Iron Moth, the Puppy Pile family, Manaphy, Carbink |
| 11 | `0da8f4a` | 6: Will, Drayden, May, Prank Spinner, Team Rocket's Boss, Maintenance, Clemont's Backpack | `data/effects_manual.tsv` (hand-written scripts); `tcgp --unsupported`; `check_states --games N` (`GAMES=200 ./check_all.sh …` for the sanitizer); Fossils are Items outside play; a copied "can't use X" doesn't bind the copier; 3 fixed Bench spots (`PokemonSlot.spot`); damage or KO at the end of the opponent's next turn, by Pokémon or by spot; `poison TARGET N`; `devolve`; `ko-check` (an attack used twice); `copy-attack :energy`, `copy-attack-cards`; `top-count` and the card filter `min-retreat`; `attack-uses`; `first-attack`; `first-heads`; `tools-to-deck`; `opp-hand-to-bench`; `hand-to-deck-random any`; `shuffle-deck`; modifiers `extra-pick`, `asleep-on-attach`, `retreat-trap`, `no-abilities` (timed), `damage-dealt :any-target`; extra Tools fall off when a Pokémon may no longer hold them | 39 one-offs, from Ditto to Mega Kangaskhan ex |
| 12 | `3ca3fbd` | 1: Penny | `use-supporter` (Penny, Smeargle); the re-flip passives `reflip-attack` and `reflip-trainer` (Victini, Gholdengo), offered as a choice right after the coins | Smeargle, Victini, Gholdengo |

## Rules

DESIGN.md §10 lists every rule the engine follows, with its source:
- **App:** checked in the game;
- **Ruling:** how the card text is read here;
- **FAQ, Tips, Card:** official sources;
- **Assumed:** no source.

**The rule for changes:** a game rule only changes once it's settled. Never change one on a guess: ask, then write the answer into §10.

**Settled and built** (newest first):
- "This attack's damage isn't affected by any effects on your opponent's Active Pokémon" ignores all of the defender's effects (Abilities, Tools, attack effects). Silcoon and Cascoon go onto the Bench as Stage 1 Pokémon. Traits come from Serebii.
- "During Pokémon Checkup" Abilities act first in Checkup, before Poison and Burn. "At the end of each turn" / "of your opponent's turn" Abilities (Darkrai, Caterpie) act before Checkup. "At the beginning of your turn" (Meloetta) comes after the draw.
- Serperior's "each {G} Energy provides 2": literally 1 Energy = 2, for costs, retreat, "for each Energy attached" and HP per Energy.
- Urshifu's second type only counts for "{X} Pokémon" effects; Weakness always uses the printed type.
- Jolteon ex reacts to every Energy attached from the Energy Zone, by an effect too. Dusknoir may knock itself out by moving damage; the opponent scores.
- Knock-out triggers (Pyukumuku, Rescue Scarf, …) run first; then every knock-out of that moment counts, and only then the win rules: both at 3 or more points, a player with Pokémon left wins, and with Pokémon on both sides or neither it's a tie (4 points against 3 doesn't matter).
- Rescue Scarf: the Pokémon and its evolution cards go to the hand; the Scarf and the energy go to the discard pile.
- Lucky Mittens: one card per Pokémon knocked out.
- A named Pokémon means exactly that name: Hala's "Crabominable" doesn't cover Crabominable ex. A card that includes an ex names it.
- An attack that doesn't happen (Confused, or a coin-flip effect, tails) wasn't "used", so it doesn't count for Sweets Relay.
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
- "Its remaining HP is now N" never heals; "halve its remaining HP, rounded down" rounds to a whole 10; a copied attack needs no Energy and can't copy again.
- An effect that discards Energy discards real Energy: a doubled {G} (Serperior) goes as one.
- Alolan Muk turns off Basic Pokémon's Abilities, not their Tools and not a Fossil's rule. Under Claydol a Potion can't be played (nothing to do). Misdreavus's "reveal their hand" does nothing in the engine.
- Evolving from the deck by an effect (Caterpie, Porygon2, Magikarp) ignores the usual timing limits. A Pokémon whose HP drops below its damage because it lost Energy (Reuniclus retreating) is knocked out at once.
- "Is Knocked Out" by an effect (Bewear, Galarian Cursola) isn't "Knocked Out by damage from an attack": no Rescue Scarf, Hala or revenge bonus.
- Electrical Cord with one Benched Pokémon moves only 1 Energy ("1 Energy each to 2").
- Knock-out triggers of one moment run the defender's Pokémon first, each with its Ability and then its Tools; the attacker's "knocks out" triggers run after each knock-out.
- Lum Berry and Sitrus Berry act at the end of each turn, before Checkup. That's a best guess, still to double-check.
- Which Energy Zone type comes up is random and even across the deck's types.
- Dealing an opening hand reshuffles until the top 5 cards hold a Basic.
- A defender's "takes −N damage" also applies on the Bench.
- Evolving ends other effects on the Pokémon.

**Open questions:**
- **Necrozma:** Serebii lists Dawn Wings Necrozma, Dusk Mane Necrozma and Ultra Necrozma ex (P-A promos) as Ultra Beasts; `data/traits.tsv` follows it. Are they Ultra Beasts in the app?
- **Team Rocket's Thieving Machine:** once you play the Item it took, does it go to your discard pile (assumed) or back to your opponent's?
- **Will and "your opponent flips a coin" effects** (an attack that says "if the Defending Pokémon tries to use an attack, your opponent flips a coin"): when the Will player then attacks, built as the next effect coin, so it comes up heads. Confusion doesn't count. Confirm.
- **Back into a full hand:** when Koga or Elesa puts cards into a hand that already holds 10, the engine sends the extra cards to the discard pile (assumed).
- **Berries:** confirm "before Checkup".

## Shortcuts

These are marked `shortcut:` in the code:
- **Capacities (batch 11):** at most 8 delayed end-of-turn effects waiting at once, and 24 different attack names counted per player per game. Timed effects hold 16 and drop the oldest, so a Miltank or Mega Mawile ex used many turns in a row could push others out.
- **Energy type when paying for a retreat or discarding:** the engine picks the most plentiful type. The player should pick.
- **Energy type when moving energy** (Elemental Switch, Dawn, "Move an Energy"): the engine takes the first matching type; the player should pick.
- **Fossils:** a Fossil can't be discarded from play by choice.
- **"Look at the top cards and put them back in any order"** (Hiker, Morty) does nothing.
- **Clemont's Backpack:** its "+20 to your opponent's Pokémon" isn't supported, because damage bonuses only count against the Active. The card stays unsupported.
- **Timed effects:** at most 16 at once; past that, the oldest is dropped. This has never happened in a game.
- **Dragonair's "attach an Energy from your discard pile":** takes the most plentiful type there; the player should pick.
- **Professor Sada's "3 different types … in any way you like":** the types come at random; the player should pick which type goes where.
- **"During your next turn, this Pokémon's X attack does +N":** the bonus has no per-attack filter, so the compiler only accepts it on cards with one attack (all of them today).

## Known weak spots and risks

- **Card text → scripts is pattern-based.** A pattern that matches but means something slightly different makes a card play wrong without any check noticing. That's how three of the four review bugs got in.
  - Guards today: the `effects.tsv` diff reviewed per change, the self-test examples, the argument-count check, the engine tests and `check_states`.
  - Missing: real-card golden tests (step 2 under Next).
- **Alolan Muk's check scans the board** each time a modifier is read from a card with an Ability. Speed looked fine in the 21-deck check, but it hasn't been measured.
- **A modifier's `:if` condition is asked while the rules ask about modifiers.** A condition that asks a player to choose, or that asks about HP while an HP modifier has its own `:if`, would go wrong. The compiler only writes simple "this Pokémon has…" tests there today.
- **Numbers and tests look at the first Pokémon only.** Words like `has-damage` and `is-ex` read only the first Pokémon a target picks. With a `(choose …)` target, they would even ask for a choice in the middle of an evaluation. No script does that today.
- **Choices inside effects call the player directly** (`Game::choose()`). That's fine for random and simple players. A search player would need the pending choice stored in the game state.
- **"Something to do" is lenient for sequences.** `(seq A B)` counts as possible when any part is possible. `then` is the strict version, used for "If you do".
- **No seeds,** so a failure from `check_states` can't be replayed.
- **The random player favours simple decks,** and most of its games end in a draw at the turn cap.

## Next

In order. Each step gets a short plan, then tests, `check_states` at 0 violations, a sanitizer run and a commit. For coverage batches (2026-10-19): commit freely, run batches back to back, and report every two batches or so. Rule questions are asked first. **Big batches** work best: survey many cards, ask all rule questions at once, build them all with only the quick checks (tests, self-test), then run `./check_all.sh` and the sanitizer once and fix everything together.

1. **Card coverage is done** (batch 12): every card plays.
2. **Effect-script checks:** real-card golden tests, meaning card text plus its expected script, a few per sentence family; a review of the `effects.tsv` diff for each new set; and `data/effects_manual.tsv` for odd cards. This is next: golden tests over all cards, the hand-written ones included.
3. **Seeds:** one seed per game, from which the shuffles, coins and random player all follow. Then failures can be replayed, and two decks can be compared on the same luck.
4. **Player choices the engine makes today:** which energy type to pay, discard or move (the player should pick), and discarding a Fossil.
5. **The simulation runner (milestone 3, AI_PLAN.md M0):** many games over threads; win, draw and loss per pairing with a confidence interval; the first player alternates. About 2,400 games per pairing give ±2%.
6. **Finding decks (milestone 4, AI_PLAN.md M4):** generate legal decks around Pokémon lines, play them against each other, rank them, and list the top ones to try in the game.
7. **Players:** the heuristic player is built (AI_PLAN.md M1; `tcgp --match N deckA deckB heuristic random`). Next the small learned players and the deck search of AI_PLAN.md M2–M5. AI_PLAN.md has the plan and its open questions.

Dropped: R6, source tags on each constant in `rules.h`. DESIGN.md §10 already gives each rule's source.

## Working agreements

- **Goal:** a correct engine first, meaning the right cards and the right rules. Stats, deck search and bots come later.
- **No card is special-cased in C++.**
  - A new mechanic becomes a new block or modifier, used by every card with that text.
  - Rare one-of-a-kind mechanics stay unsupported on purpose and show up in `--coverage`.
- **Rules:** never change one on a guess. Ask first; rules get checked in the app. Answers go into DESIGN.md §10.
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
