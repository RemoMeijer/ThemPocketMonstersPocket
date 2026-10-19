# Progress

Updated 2026-10-16.

## Where we are

The **game engine** is most of the way there. It plays Pocket games by the rules Remco confirmed, for 84% of all cards, and an independent checker watches every move. The **simulator around it** hasn't started yet: playing many games at once, stats, finding decks and a better player.

| Part | State |
|---|---|
| Rules core: setup, turns, Energy Zone, attacking, retreating, evolving, Trainers, Tools, Stadiums (always-on effects), abilities, Special Conditions, Checkup, damage, winning | Built and checked, Stadium actions (R5) included. Missing: the player choosing which energy to pay, discard or move |
| Card data | Every set up to B4b: 2,319 unique cards from Limitless, cross-checked against deckgym-core |
| Card effects | 1,939 of 2,319 cards (84%) playable. Trainers lag behind and are being added in batches (table below, Next step 1) |
| Checks | 21 rule tests, 38 effect tests, 45 compiler self-test examples, `check_states` (24.2M moves over all 12 decks, 0 violations), and the memory and undefined-behaviour checker build is clean after coverage batch 3 (the tests plus `check_states` on 3 coverage decks) |
| Replayable games (seeds) | Not yet: every game seeds itself from `random_device` |
| Simulation runner (many games, threads, stats) | Not started |
| Finding decks (generate, play, rank) | Not started |
| Players | Random only. About 70% of random-vs-random games end in a draw at the turn cap |

Code size: about 4,500 lines of C++ (engine and tests) and 900 lines of Python (card data and the effect compiler).

**Speed:** `check_states` plays about 2,000 games/s on one thread while checking the whole state after every move. The engine on its own is faster but hasn't been measured. For comparison, deckgym-core quotes about 3,300 random games/s.

### Card coverage

`engine/build/tcgp --coverage`:

| Kind | Playable |
|---|---|
| Pokémon | 1,831 / 2,167 |
| Supporters | 63 / 82 |
| Items | 23 / 31 |
| Tools | 9 / 26 |
| Stadiums | 13 / 13 |

Trainers matter most for deck finding because they go into most decks. They're being added in batches (Next, step 1). `decks/coverage-a.txt` and `coverage-b.txt` hold the batch 1 Trainers, `coverage-c.txt` the batch 2 ones, `coverage-d.txt` the batch 3 ones and `coverage-e.txt` the Stadiums, so `check_states` plays them.

The rest are mostly one-of-a-kind mechanics we chose to skip for now:
- memory of last turn (Sweets Relay, "knocked out last turn");
- knock-out triggers, copying attacks, cards back from the discard pile, returning a Pokémon to the hand;
- Energy Zone tricks, "you can use this card only if…", Ultra Beasts, "choose 1:" cards.

## How the design holds up

The short version: the design works and needs no rework now. Details, and how other engines do it, are in DESIGN.md §12.

- **Card effects as data.** Card text compiles into small scripts built from about 60 blocks, and no card is handled by name in C++. That gave 80% coverage with a 700-line compiler. deckgym-core, the closest existing Pocket simulator, writes Rust code per card instead (a 7,000-line file of attack handlers) and covers about 89%.
  - Building effects from shared blocks is normal: MTG Arena writes each card's abilities as small scripts in a rules language, and Forge uses a text script per card. What's unusual is that sentence patterns write our scripts from card text.
  - The risk: a pattern that matches but means something slightly different makes a card play wrong without anyone noticing. Against that we have the git diff of `effects.tsv`, the compiler's self-test, the coverage report and the block tests. Proposed: real-card golden tests, a diff review for each new set, and `effects_manual.tsv` for odd cards (DESIGN.md §4, "How fragile is this?").
  - The long tail gets harder as the patterns grow.
- **One damage pipeline, and modifiers that answer fixed questions.** This matches the official damage order and kept every rule fix in this round small: R1–R4 each touched a few places.
- **Plain, copyable game state.** It's ready for threads and for a search player later.
- **Choices inside effects call the player directly** (`choose()`), as Forge and XMage do. That's fine for random and simple players. A search player needs the pending choice stored in the state instead, as deckgym-core and SabberStone do. Only 8 places call `choose()`, so this can wait until a search player is planned.
- **Hidden information:** players only see a `PlayerView`, so a smarter player can't cheat. deckgym's search players do see hidden cards.
- **Weak spots:**
  - games can't be replayed (no seeds);
  - the engine still picks which energy to pay or discard, which is the player's choice in the game (see Shortcuts below);
  - the random player's results are biased toward simple decks (DESIGN.md §12).

## Card data (2026-10-16)

- **Why the switch:** TCGdex stopped adding Pocket sets after B2a (Paldean Wonders), which is about 8 months and 1,700 cards behind the game. Missing: A4b, B2b, B3, B3a, B3b, B4, B4a, B4b, Promo-B and Promo-A 101–117.
- **Sources compared:**
  - Limitless: every set, clean text, one page per set;
  - deckgym-core's `database.json`: also every set, scraped from Limitless, with some text glitches;
  - TCGdex: sets up to B2a only;
  - Serebii, to settle disagreements.
- **Result:** the three sources disagree on 32 cards. Limitless is with the majority on all but one: Jigglypuff P-A-022, where Serebii, deckgym and TCGdex all say 50 HP and Limitless says 60. That one is fixed in `FIXES`.
- **TCGdex errors that are now fixed:**
  - 20 Pokémon, mostly babies and Dragon types, had a Colorless weakness (they have none);
  - Blastoise A1-055 cost WC (it's WWC), Seadra A1-071 cost WWC (it's WWW), Tandemaus B2a-081 cost CC (it's C);
  - Pincurchin A1-112 did 40 damage (it does 30) and Flittle B2a-050 did 10 (it does 20);
  - five B2a ex cards lost " ex" from their names, so they scored 1 point instead of 2: Meowscarada, Armarouge, Chien-Pao, Bellibolt and Gholdengo;
  - text: Pachirisu A2b-025 (Benched {L} only), Koraidon B2a-063 (discards {F}, not {R}), Mega Steelix ex B1a-052 (the "−20" was missing), Unown GUARD/POWER (the ability name was missing).
- **Effect scripts:** the only card that stopped compiling was Brock, whose text says "attach it to Golem or Onix" without "your"; the pattern now accepts both. Three scripts changed, and all three are corrections: Pachirisu, Koraidon and Cetitan.
- Reprints are now merged into the original print, so some old Promo-A ids (e.g. P-A-029 Blastoise) are gone. The decks don't use them.

## Rules

DESIGN.md §10 lists every rule the engine follows. Remco confirmed the open ones in the app on 2026-10-16. The rule for changes: **a game rule only changes after Remco has clarified it.**

Done on 2026-10-16, each with a test and its own commit:
- **R1:** a player with no Pokémon in play loses, whatever the points.
- **R2:** Weakness counts whenever the attack does damage; the floor at 0 comes only at the end.
- **R3:** evolving resets "used its ability this turn", because the use belongs to the card.
- **R4:** the Energy Zone has a current and a next energy. Unused energy, energy paid for a retreat, discarded energy and energy on knocked-out Pokémon go to the discard pile, counted per type.

Also answered on 2026-10-16:
- A Trainer can only be played when its effect has something to do. Each effect block now checks this.
- Energy paid for a retreat or discarded by an effect goes to the discard pile.
- When one attack knocks out both Active Pokémon, the defending player picks a new Active first, then the attacking player, then Pokémon Checkup. The engine already did this; a test now holds it.
- An effect that starts something can always be played, even if it can never pay off. That covers Blaine without Ninetales, and X Speed after you've already retreated: the Retreat Cost is still 1 less. The engine already did this.
- "Moved from your Bench to the Active Spot this turn" (Golisopod) belongs to the card on top (Remco, from the card text). Evolved on the Bench and then moved up: yes. Benched this turn and then moved up: yes. Moved up as Wimpod and evolved after: no. The engine used to say no in all three cases; evolving now resets the mark, like R3.
- Abilities follow the Trainer rule: an ability with nothing to do can't be used (Remco).
- When a Pokémon has two types of energy and one is moved or discarded, the player picks the type (Remco). That's step 4 under Next.

- Random multi-hit attacks (Wugtrio ex): the hits on one Pokémon add up first and are dealt as one attack, so Weakness (+20) and bonuses count once per Pokémon (Remco). The engine used to apply them on every hit.

**Open questions:**
- **Team Rocket's Thieving Machine:** the Item it takes from your opponent's discard pile, once you play it, goes to your discard pile, so it's yours from then on. Is that right, or does it go back to your opponent's discard pile? The engine assumes yours.

## Review (2026-10-16)

A read-through of the engine and every compiled Trainer found 4 bugs. Each was confirmed with a test against the engine, then fixed in its own commit with a test that holds it:
- **"For each Energy attached to all of your (opponent's) Pokémon"** counted the Active Pokémon only. Affected cards: Espeon, Zoroark, Eelektross and Mega Diancie ex. The fix is in the `(energy …)` number.
- **Lt. Surge** moved energy from 1 Benched Pokémon instead of all of them. The compiler now reads a plural with no number in front as "all". That was the only script that changed.
- **Whitney** also cured Poison and Burn, because `cure` ignored its list of conditions. The fix is in the block.
- **Replacing an HP Stadium** (Starting Plains) left Pokémon with too much damage in play. Playing a Stadium now runs the knock-out check.

The scripts of all 63 playable Trainers now match their text, apart from the shortcuts listed below.

Weak spots the review found, not fixed:
- **No argument-count checks:** blocks don't check how many arguments they get, so an extra one is silently ignored (that was the Whitney bug). A check in the builder would make such a script fail to load, and the card would show up in `--coverage`.
- **Numbers and tests look at one Pokémon only:** `has-damage`, `is-ex` and the like read only the first Pokémon of a target. They would even ask for a choice if a script gave them `(choose …)`. No script does that today.
- **`move-energy` picks the energy type itself:** Elemental Switch and Dawn work like the discard shortcut below. Remco says the player picks, so this belongs to step 4 under Next; it's now marked `shortcut:` and listed below. (Fixed since: abilities now need something to do, like Trainers; words check their argument counts.)
- **Why these slipped through:** the compiler's self-test uses example sentences, not the real cards' scripts. Real-card golden tests (Next, step 1) are what would catch this kind of bug.

## Shortcuts (marked `shortcut:` in the code)

- Which energy gets discarded (retreat, "discard an Energy") is picked automatically, not chosen by the player.
- Which energy type gets moved (Elemental Switch, Dawn, "Move an Energy") is picked automatically too; Remco says the player picks (Next, step 4).
- A Fossil can't be discarded from play by choice.
- Effects that look at the deck and put the cards back in any order do nothing.
- Clemont's Backpack's "+20 to your opponent's Pokémon" isn't supported (our damage bonuses only count against the Active).
- At most 16 timed effects at once; past that the oldest is dropped. Never seen in a game so far.

## Next

In order. Each step gets a plan first, then tests, `check_states` at 0 violations and a commit. Remco chose coverage first and golden tests later (2026-10-16).

1. **Card coverage, Trainers first**, in batches. The rule answers batches 4–6 need are in DESIGN.md §10.
   - Done 2026-10-16:
     - **Batch 0:** effect words check how many arguments they get.
     - **Batch 1:** 20 Trainers from new patterns and small filters: Mars, Lisia, Serena, Calem, Korrina, Cabbie, Juliana, Arven, Skyla, Irida, Diantha, Cheren, Order Pad, Team Rocket's Goo-zooka, Researcher and Master Plan, Arena of Antiquity, Small Balloon, Elegant Cape, Deceptive Needle.
   - **Batch 2**, done 2026-10-16: 9 Trainers.
     - Cards: Lana, Piers, Juggler, Psychic, Kiawe, Big Malasada, Nasty Notice, Team Star Grunt, Field Blower.
     - How: "You can use this card only if…" compiles to `(if …)`. New blocks: `end-turn`, `one-of`, `cure-random`, `discard-hand-to`, `discard-energy-among`, `discard-stadium`, `move-random-energy`. New tests: `stadium-in-play`, `has-attack`, `energy-types`.
     - Also unlocked 12 Pokémon attacks, among them Gyarados ex, Swampert, Ceruledge ex, Gallade, Tornadus and the Castforms.
   - **Batch 3**, done 2026-10-19: 7 Trainers.
     - Cards: Celestic Town Elder, Fishing Net, Fisher, Flame Patch, Volkner, Pokémon Flute, Team Rocket's Thieving Machine.
     - New blocks: `from-discard`, `attach-from-discard`, `discard-count`.
     - "If you do, B" is now `(then A B)`: B only follows when A had something to do.
     - Also unlocked 12 Pokémon, among them Flareon ex, Delcatty, Galarian Perrserker, Chandelure, Houndstone, Hisuian Zoroark ex and Rotom ex.
     - Cards can now change hands, so each player's piles hold up to 40 cards. `check_states` counts cards over both players together, and checks that energy in the discard pile plus energy attached never goes down.
   - **Batch 4 (R5)**, done 2026-10-19: all 13 Stadiums are playable, which completes the rules milestone.
     - Cards: Mesagoza, Fragrant Forest, Area Zero, Kid's Room, Rainbow Cave, Arcade, Hiking Trail, Soothing Shore, Bounded Field.
     - The Use Stadium move: once per player per turn and per Stadium card.
     - The end-of-each-turn Stadium trigger, which runs last.
     - Weakness ×2 (`weakness-x2 :not-mega`).
     - New blocks: `draw-until`, `hand-to-deck`, `swap-for`, `cycle-energy`.
     - The compiler reads a Stadium's "that player … their" as "you … your".
   - **Batch 5:** Tools that react to something: Lum Berry, Sitrus Berry, Poison Barb, Dark Pendant, Steel Apron, Clear Veil, Metal Core Barrier, Protective Poncho, Heavy Helmet.
   - **Batch 6:** back to the hand: Koga, Budding Expeditioner, Ilima, Parasol Lady, Elesa.
   - **After batch 6:** 122 of 152 Trainers. Left out:
     - knock-out triggers;
     - Ultra Beast, Ancient and Future cards: the data doesn't mark those traits;
     - one-offs: Penny, Will, Drayden and others.
   - **Then:** the most common missing attack texts from `tcgp --coverage`.
2. **Effect-script checks:** real-card golden tests (card text plus its expected script, a few per sentence family), a review of the `effects.tsv` diff for each new set, and `data/effects_manual.tsv` for odd cards.
3. **Seeds:** one seed per game, from which the shuffles, coins and random player all follow. Then a failure found by `check_states` can be replayed, and two decks can be compared on the same luck.
4. **Player choices the engine makes today:** which energy to pay for a retreat, to discard or to move (the player picks the type, Remco), and discarding a Fossil.
5. **The simulation runner (milestone 3):** many games over threads; win, draw and loss per pairing, with a confidence interval; the first player alternates. About 2,400 games per pairing give ±2%.
6. **Finding decks (milestone 4):** generate legal decks around Pokémon lines, play them against each other, rank them, and list the top ones to try in the game.
7. **A minimal player:** attach energy and attack when possible, if the random player's draws and bias get in the way of step 6. Remco said not to build real bots yet.

Dropped: R6, source tags on each constant in `rules.h`. DESIGN.md §10 already says where each rule comes from, and that's enough.
