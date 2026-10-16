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
  - `test_rules` and `test_effects` (19 effect tests) pass.
  - `check_states` ran 8.3M moves over all 7 decks with 0 violations, including 934k Trainers, 111k Tools, 383k abilities and 165k attacks with effects (on the Limitless data).
  - Clean under the memory and undefined-behaviour checkers (last run before the data switch; the engine code hasn't changed since).
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

## Assumed rules (DESIGN.md §10, still to confirm in the app)

deckgym-core transcribed the in-app Tips panel into tests, and that secondhand source agrees with Confused, Poison, Burn, Sleep, Paralysis and the Checkup order, the hand limit, the empty deck, the turn cap, and both players winning at once being a draw. Still open: whether evolving clears conditions, how the opening hand is dealt, and 1 Tool per Pokémon.

- Confused: tails, the attack fails.
- Poison 10; Burn 20 then a flip; Sleep flip; Paralysis ends after its owner's next turn.
- Mega ex is worth 3 points; both players winning at once is a draw.
- The first player may play a Supporter on turn 1.
- Bench damage has no Weakness. Effects on a Pokémon end when it leaves the Active Spot or evolves.
- Plus the milestone 1–2 guesses: turn cap 30, first player draws on turn 1, empty deck = no draw, hand limit 10, no evolving on your own first turn.

## Next options

1. **Rules:** check DESIGN.md §10 against the in-app Tips panel and the sources above, and mark each rule in `rules.h` with where it comes from.
2. **Seeds:** make games replayable from a seed, so a violation found by `check_states` can be replayed. This is also part of milestone 3.
3. **Card coverage for the new sets:** the `tcgp --coverage` output lists the most common missing texts.
4. **Milestone 3, the simulation runner:** threads and stats over many games. Win rates from the random player mean little (about 70% of its games are draws at the turn cap), but it's enough to test the runner.

Players: the random player, or at most a minimal one, is enough for now. No real bots yet.
