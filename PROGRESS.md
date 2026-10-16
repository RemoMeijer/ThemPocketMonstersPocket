# Progress

Updated 2026-10-16. DESIGN.md's foundation is built: card effects work through scripts, with no card handled by name in C++.

## Where things stand

- **Card coverage:** `engine/build/tcgp --coverage` reports 1,449 of 1,708 cards (85%) fully playable.

  | Kind | Playable |
  |---|---|
  | Pokémon | 1,387 / 1,597 |
  | Supporters | 39 / 63 |
  | Items | 14 / 25 |
  | Tools | 6 / 19 |
  | Stadiums | 3 / 4 |

  The rest are mostly one-of-a-kind mechanics we chose to skip:
  - memory of last turn (Sweets Relay, "knocked out last turn");
  - knock-out triggers, copying attacks, cards back from the discard pile, returning a Pokémon to the hand;
  - Energy Zone tricks, "you can use this card only if…", Ultra Beasts, "choose 1:" cards.
- **Pipeline:**
  - `fetch_cards.py` → `data/cards.json`;
  - `export_cards.py` → `data/cards.tsv`;
  - `compile_effects.py` → `data/effects.tsv` (sentence patterns, self-test of 27 examples);
  - the engine loads both TSV files.
- **Engine:**
  - `core/`, `cards/`, `game/` (turn flow, Checkup, damage pipeline, modifiers);
  - `moves/` (one class per move kind);
  - `effects/` (script parser, builder, blocks for flow, targets, damage, energy, cards);
  - `players/`.
- **Checks:**
  - `test_rules` and `test_effects` (19 effect tests) pass.
  - `check_states` ran 5.8M moves over 6 decks with 0 violations, including 840k Trainers, 95k Tools, 164k abilities and 165k attacks with effects.
  - Clean under the memory and undefined-behaviour checkers.
- **Decks:** `decks/` holds water, lightning, fighting (plain), and pikachu, charizard, gardevoir, abilities (with Trainers and effects).

## Shortcuts (marked `shortcut:` in the code)

- Which energy gets discarded (retreat, "discard an Energy") is picked automatically, not chosen by the player.
- Trainers are playable even when they would do nothing (Rare Candy with no target, Potion with no damage).
- A Fossil can't be discarded from play by choice.
- Effects that look at the deck and put the cards back in any order do nothing.
- Clemont's Backpack's "+20 to your opponent's Pokémon" isn't supported (our damage bonuses only count against the Active).

## Assumed rules (DESIGN.md §10, still to confirm in the app)

- Confused: tails, the attack fails.
- Poison 10; Burn 20 then a flip; Sleep flip; Paralysis ends after its owner's next turn.
- Mega ex is worth 3 points; both players winning at once is a draw.
- The first player may play a Supporter on turn 1.
- Bench damage has no Weakness. Effects on a Pokémon end when it leaves the Active Spot or evolves.
- Plus the milestone 1–2 guesses: turn cap 30, first player draws on turn 1, empty deck = no draw, hand limit 10, no evolving on your own first turn.

## Next options

1. **Milestone 3, the simulation runner:** threads, seeds, stats over many games.
2. More card coverage from the long tail. The `tcgp --coverage` output lists the commonest missing texts.
3. A better player policy (milestone 4). The random player wastes Trainers and draws often at the turn cap.
