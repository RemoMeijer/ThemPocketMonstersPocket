# Progress

Updated 2026-10-16. DESIGN.md's foundation is built: card effects work through scripts, with no card handled by name in C++. The card data now comes from Limitless and covers every set up to B4b (see "Card data" below).

## Where things stand

- **Card coverage:** `engine/build/tcgp --coverage` reports 1,871 of 2,319 cards (80%) fully playable. Before the newer sets were added it was 1,449 of 1,708 (85%); the new sets bring new mechanics.

  | Kind | Playable |
  |---|---|
  | Pokémon | 1,808 / 2,167 |
  | Supporters | 40 / 82 |
  | Items | 14 / 31 |
  | Tools | 6 / 26 |
  | Stadiums | 3 / 13 |

  The rest are mostly one-of-a-kind mechanics we chose to skip:
  - memory of last turn (Sweets Relay, "knocked out last turn");
  - knock-out triggers, copying attacks, cards back from the discard pile, returning a Pokémon to the hand;
  - Energy Zone tricks, "you can use this card only if…", Ultra Beasts, "choose 1:" cards.
- **Pipeline:**
  - `fetch_cards.py` → `data/raw/limitless/` (text and list view of every set);
  - `export_cards.py` → `data/cards.tsv`;
  - `check_cards.py` compares `cards.tsv` with deckgym-core's database: 0 differences;
  - `compile_effects.py` → `data/effects.tsv` (sentence patterns, self-test of 27 examples);
  - the engine loads both TSV files.
- **Engine:**
  - `core/`, `cards/`, `game/` (turn flow, Checkup, damage pipeline, modifiers);
  - `moves/` (one class per move kind);
  - `effects/` (script parser, builder, blocks for flow, targets, damage, energy, cards);
  - `players/`.
- **Checks:**
  - `test_rules` (21 tests) and `test_effects` (22 tests) pass.
  - `check_states` ran 8.3M moves over all 7 decks with 0 violations, including 934k Trainers, 111k Tools, 383k abilities and 165k attacks with effects (on the Limitless data, after R1–R4). Its own reading of the rules includes the Energy Zone and discard-pile energy; breaking either on purpose gives thousands of violations.
  - Clean under the memory and undefined-behaviour checkers: the tests, plus `check_states` on 4 decks, after R1–R3.
- **Decks:** `decks/` holds water, lightning, fighting (plain), and pikachu, charizard, gardevoir, abilities (with Trainers and effects).

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

## Shortcuts (marked `shortcut:` in the code)

- Which energy gets discarded (retreat, "discard an Energy") is picked automatically, not chosen by the player.
- Trainers are playable even when they would do nothing (Rare Candy with no target, Potion with no damage).
- A Fossil can't be discarded from play by choice.
- Effects that look at the deck and put the cards back in any order do nothing.
- Clemont's Backpack's "+20 to your opponent's Pokémon" isn't supported (our damage bonuses only count against the Active).

## Rules (DESIGN.md §10)

Every rule now has a source in DESIGN.md §10. On 2026-10-16 Remco checked the open ones in the app:
- The first player draws and may play a Supporter on turn 1.
- The turn limit is 30, then a tie. The hand limit is 10.
- 1 Tool per Pokémon, 3 Bench spots, 3 energy types, 1 retreat per turn.
- Evolving clears Special Conditions.
- Unused energy goes to the discard pile.
- "Once during your turn" counts per card (abilities and Stadium actions).

One row in §10 is still marked ○, Stadium actions, and R5 below covers it. R1–R4 are done.

## Next: the rules milestone

Planned 2026-10-16. R1–R4 are done (2026-10-16); R5 and R6 are still to come. One step at a time, each with tests, `check_states` at 0 violations (its own reading of the rules updated where needed) and a commit.

| Step | What | Where |
|---|---|---|
| R1 ✓ | **Win condition:** a player with no Pokémon in play loses, whatever the points; both without Pokémon is a tie. Today, reaching 3 points together is always a draw. | `game.cpp` (knock-out check), `test_rules`, `check_states` |
| R2 ✓ | **Damage order:** Weakness applies whenever the attack does damage, even if the attacker's effects took it to 0 or below; floor at 0 once, at the end. | `damage.cpp`, `test_effects` |
| R3 ✓ | **Abilities per card:** evolving resets "used its ability this turn", so the evolved card can use its own. | `evolve.cpp`, `test_effects` |
| R4 ✓ | **Energy Zone and discard-pile energy:** store the current and the next energy. At the end of the turn, unused energy goes to the discard pile, and a new one comes at the start of the next turn. Energy in the discard pile is counted per type; energy from retreating, discard effects and knocked-out Pokémon goes there too. `PlayerView` shows your own next energy and both discard piles. | `state.h`, `game.cpp`, `retreat.cpp`, `blocks_energy.cpp`, `view.h`, `check_states` |
| R5 | **Stadium actions:** a "use the Stadium" move, once per player per turn, per Stadium card. A new Stadium starts fresh, so playing one after using the old one allows a second use. Also the Stadium's "at the end of each player's turn" trigger. Needs compiler patterns for both sentence shapes. | `moves/` (new move), `state.h`, `game.cpp`, `load_scripts.cpp`, `compile_effects.py` |
| R6 | **Source tags:** each constant in `rules.h` gets a short tag pointing at §10 (App, FAQ, Card, Tips, Fan, Assumed). | `rules.h` |

R1–R3 are a few lines each. R4 changes the game state and the turn flow. R5 is a new mechanic: it unlocks Stadiums such as Mesagoza, Fragrant Forest, Area Zero, Arcade, Hiking Trail and Soothing Shore, as far as their effects have blocks. Rainbow Cave also needs R4.

## Later

1. **Seeds:** make games replayable from a seed, so a violation found by `check_states` can be replayed. This is also part of milestone 3.
2. **Card coverage for the new sets:** the `tcgp --coverage` output lists the most common missing texts.
3. **Milestone 3, the simulation runner:** threads and stats over many games. Win rates from the random player mean little (about 70% of its games are draws at the turn cap), but it's enough to test the runner.

Players: the random player, or at most a minimal one, is enough for now. No real bots yet.
