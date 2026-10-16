# TCG Pocket engine: how card effects fit in

Built: see PROGRESS.md for the current state.
Written 2026-10-15. The counts in §1 come from the TCGdex data of that day (1,708 cards); the card data now comes from Limitless (2,319 cards, 2026-10-16).

## 1. What has to fit in

After dropping alt arts and duplicates there are 1,708 cards. These are the parts that carry effect text:

| Part | Total | With effect text |
|---|---|---|
| Attacks | 1,701 | 818 |
| Abilities | 140 | 140 |
| Trainers | 120: 63 Supporters, 34 Items, 19 Tools, 4 Stadiums | 120 |

That gives 1,078 effect texts, split into 1,445 sentences. Blanking out the numbers and energy types leaves **586 different sentence shapes**. The 100 most common shapes cover 63% of all sentences. The rest is a long tail of one-offs.

**Effect families.** A text can belong to more than one family.

| Texts | Family | Mostly on |
|---|---|---|
| 349 | Conditions ("If …") | attacks, abilities |
| 237 | Attack damage changes: +N, ×N, "for each" | attacks |
| 230 | Coin flips | attacks |
| 145 | Lasts this turn or until the next turn | attacks, Supporters |
| 136 | Draw, search, deck or hand | Supporters, attacks, abilities |
| 134 | Damage to Benched or chosen Pokémon | attacks |
| 133 | Special Conditions: Poisoned, Burned, Asleep, Paralyzed, Confused | attacks |
| 90 | Discard or move energy | attacks |
| 70 | "Can't" attack, retreat, evolve or play | attacks, Items |
| 68 | Damage reduction or prevention | abilities, attacks |
| 61 | Heal | attacks, abilities, Items |
| 52 | Extra energy from the Energy Zone | attacks, abilities |
| 50 | Names specific cards ("your Ninetales, Rapidash, or Magmar") | Supporters |
| 49 | Once during your turn (abilities you activate) | abilities |
| 48 | Triggers: whenever, end of turn, is damaged, is knocked out | abilities, Tools |
| 37 | Damage to itself | attacks |
| 31 | Switch, or pull the opponent's Pokémon to Active | attacks, abilities |
| 29 | Changes to attack cost or retreat cost | abilities |
| 25 | HP changes, including Fossils | Items, Tools |
| 20 | Evolution tricks (Rare Candy, "evolved this turn") | abilities |

**Rare but structural.** Each of these needs a hook in the core, even when only 1–5 cards use it:
- **Copy an attack:** Mew ex, Ditto, Mimikyu.
- **Points rules:** Dusknoir ("your opponent can't get any points for it"), Mars, Buzzwole, Beastite.
- **Memory of what happened:**
  - last turn: Sweets Relay, Marshadow, Wobbuffet;
  - this turn: Jolteon ("evolved during this turn"), Silvally ("played a Supporter this turn").
- **Locks:**
  - no Supporters: Gengar ex, Psyduck;
  - no Abilities on Basics: Alolan Muk;
  - the Active can't evolve: Aerodactyl ex.
- **Energy Zone tricks:**
  - change the opponent's *next* energy: Porygon-Z, so the zone needs a "next" energy;
  - each energy counts double: Serperior;
  - change the type of an attached energy: Smeargle.
- **Discard pile as a resource:** Flame Patch, Pokémon Flute, Celestic Town Elder.
- **Randomness:**
  - random targets: Rayquaza ex, Zapdos, Magcargo;
  - random cards: Poké Ball, Houndoom.
- **Ignoring rules:**
  - "isn't affected by Weakness": Hitmonchan ex;
  - "isn't affected by any effects on your opponent's Active Pokémon": Morgrem, Ledian.
- **Fossils:** Items played as a 40-HP Basic Colorless Pokémon that can't retreat and can be discarded at any time.
- **Stadiums:** one in play, shared by both players. The placement rules are printed on Mesagoza: one per turn, it replaces the old one, and a Stadium with the same name can't be played.
- **Tools:** attached to a Pokémon. Guzma, Starly and Purrloin discard them.
- **Effects of other cards:** Penny uses another Supporter's effect, so an effect must be usable on its own, apart from its card.
- **Groups the data doesn't mark:** Ultra Beasts, and "Arceus or Arceus ex". TCGdex has no field for these.
- **ex:** TCGdex has `suffix: "EX"` on 414 cards. Use that instead of the name trick the loader uses now. Mega is still only visible from the name ("Mega … ex").

## 2. Principles

1. **The core knows the rulebook, never a card.** No `if (card is X)` anywhere in C++.
2. **Cards are data.** A card's text is translated once into a small *effect script*. The engine builds it from building blocks.
3. **One building block per kind of effect,** written once and given numbers. "Heal" is one block, whether it comes from Potion, Butterfree's ability or an attack.
4. **Every decision is a move from a list,** including choices in the middle of an effect.
5. **Everything the game must remember lives in the state** as plain data, so it can be copied, checked and replayed.
6. **Unsupported stays visible.** A card whose text isn't translated can't go in a deck, and a coverage report lists it.

## 3. Three ways an effect plugs in

| Shape | What it is | Comes from |
|---|---|---|
| **Action** | Happens once, when played or used | Attack effects, Items, Supporters, "Once during your turn, you may…" abilities, discarding a Fossil |
| **Modifier** | Always on while its source is there. The engine asks it fixed questions. | Passive abilities, Tools, the Stadium, timed effects ("during your opponent's next turn …") |
| **Trigger** | Reacts to an event | "Whenever you attach energy…", "if damaged by an attack…", "when knocked out…", "at the end of your turn…" |

The questions the engine asks modifiers are fixed, so the core knows every place where something can change:
- damage dealt and damage taken;
- max HP;
- retreat cost and attack cost;
- can attack, can retreat, can evolve, can play a Supporter;
- has abilities;
- points scored for a knock-out;
- energy provided.

The events triggers can react to are also fixed:
- turn start and turn end;
- Pokémon Checkup;
- energy attached;
- damaged by an attack;
- knocked out;
- a Pokémon put into play or evolved;
- a card played.

## 4. Effect scripts

Each effect text becomes a short script in a small language with nested brackets. It's stored in `data/effects.tsv`, one row per card part: card id, part (attack 1, ability, trainer) and script.

| Card text | Script |
|---|---|
| Flip 2 coins. This attack does 30 damage for each heads. | `(damage opp-active (* (coins 2) 30))` |
| Flip a coin. If heads, this attack does 30 more damage. | `(if (coin) (bonus 30))` |
| This Pokémon also does 20 damage to itself. | `(damage self 20)` |
| Heal 30 damage from this Pokémon. | `(heal self 30)` |
| Your opponent's Active Pokémon is now Poisoned. | `(status opp-active poisoned)` |
| This attack also does 20 damage to 1 of your opponent's Benched Pokémon. | `(damage (choose opp-bench) 20)` |
| During your opponent's next turn, this Pokémon takes −20 damage from attacks. | `(until opp-next-turn self (damage-taken -20))` |
| Take a {P} Energy from your Energy Zone and attach it to this Pokémon. | `(attach-energy zone psychic self)` |
| Put a random Basic Pokémon from your deck into your hand. (Poké Ball) | `(to-hand (random my-deck basic))` |
| The Pokémon this card is attached to gets +20 HP. (Giant Cape) | `(while-attached (hp +20))` |

**Vocabulary** (rough, it grows with the build order below):

| Kind | Words |
|---|---|
| Actions | `damage` `bonus` `heal` `status` `draw` `discard-energy` `attach-energy` `move-energy` `switch` `gust` `to-hand` `shuffle-hand` `reveal` `discard-tool` `discard-stadium` `copy-attack` `cancel` |
| Targets | `self` `my-active` `opp-active` `(choose …)` `(random …)` `(each …)`. Filters: bench, type, stage, ex, names, ultra-beast, damaged |
| Numbers | Plain numbers, `(coins n)`, `(coins-until-tails)`, `(count …)` for Benched Pokémon, attached energy, damage, points |
| Conditions | `(if cond then else)`, `(coin)`, `(has-tool …)`, `(evolved-this-turn)`, `(energy-at-least …)`, `(points …)` |
| Durations | `this-turn` `opp-next-turn` `my-next-turn` `while-active` `while-attached` `while-in-play` |
| Modifiers | `(damage-taken n)` `(damage-dealt n)` `(hp n)` `(retreat-cost n)` `(attack-cost n type)` `(cant attack/retreat/evolve/supporter)` `(no-weakness)` `(no-abilities …)` |
| Triggers | `(on energy-attached …)` `(on damaged-by-attack …)` `(on knocked-out …)` `(on end-of-turn …)` |

**Where scripts come from:**
1. **`compile_effects.py`:** a table of sentence patterns, each a regular expression with blanks for numbers, types and targets, mapped to a script template. It handles the common shapes automatically and grows the exporter we already have.
2. **`data/effects_manual.tsv`:** hand-written scripts for the long tail. An LLM could draft these and a person reviews them, which fits the LLM plans for later.
3. **A coverage report:** which cards are fully supported, which pattern covers how many cards, and which sentences match nothing.

**C++ side:** each script is parsed once when the cards load, into a tree of small block objects, one class per word. An unknown word makes the card unsupported and names the word. Nothing gets parsed during a game.

This avoids writing C++ per card. There are about 60 building blocks, and adding a card is a data change.

## 5. Damage pipeline (one place, fixed order)

1. **Base damage** from the attack, including its own +N, ×N, coins and "for each".
2. **The attacker's modifiers:** abilities (Lucario +20), Tools, the Stadium, effects that last this turn (Blaine), and effects on the attacker such as "the Defending Pokémon's attacks do −20 damage".
3. **Weakness** +20 (×2 under Bounded Field), unless the attack ignores Weakness. It applies whenever the attack does damage, even if step 2 brought the number to 0 or below.
4. **The defender's modifiers:** "takes −20", "prevent all damage", unless the attack ignores effects on the defender.
5. Floor at 0, once, here at the end. Then apply the damage.

This is the official order from the "Detailed battle FAQ" (§10). Example: 10 damage, −20 on the attacker, a weak defender: 10 − 20 + 20 = 10.

Damage to the Bench skips Weakness (Tips). The attacker's bonuses name the Active ("to your opponent's Active Pokémon"), so they don't apply on the Bench either. The defender's own modifiers do apply on the Bench (assumed, as in the paper game). Damage an attacker does to itself and damage from Checkup are put on directly.

**After any damage** (attack, Bench, self, Checkup, ability), one knock-out check runs:
- it scans every Pokémon in play, not only the Defending one;
- it awards points: ex 2, Mega ex 3, adjusted by modifiers such as Dusknoir;
- each side moves a new Pokémon to Active if needed;
- then it checks for a winner.

## 6. Decisions inside effects

An effect asks the game `choose(player, options)`. The game turns the options into normal moves (new kinds: choose target, choose card, choose attack, yes/no) and asks that player's Policy. The chooser can be the opponent, as in "Your opponent chooses the new Active Pokémon".

Promoting after a knock-out moves onto this same mechanism, which removes the special `Promote` phase. Random outcomes such as coins and random cards stay inside the engine and are never a player's choice.

The trade-off is that the game doesn't pause mid-effect. That keeps it simple and fast. A future lookahead player branches on its main moves and makes these smaller choices with its own policy.

## 7. What the state needs to remember

| Where | Added |
|---|---|
| Pokémon in play | Special Conditions (Asleep, Paralyzed and Confused replace each other; Poisoned and Burned stack on top), attached Tool, whether its top card used its ability this turn (see "Once per turn belongs to the card" below), damaged-by-attack turn, timed effects on it, counters ("this effect stacks") |
| Player | Energy Zone *current and next*; energy in the discard pile, as a count per type; Supporter played this turn; Stadium played this turn; a record of this turn and last turn (attacks used, knock-outs suffered, evolutions); player-wide timed effects (Supporter lock) |
| Game | The Stadium in play, who played it, and whether each player used its action this turn |

Timed effects are small records: what it is, who it's on, and when it ends. The game removes them at the right turn boundary. Effects on a Pokémon also end when it goes to the Bench (official FAQ) or evolves (assumed, see §10).

**Energy in the discard pile.** Energy that leaves play lands in its owner's discard pile, as a count per type. Remco checked this in the app for unused Energy Zone energy at the end of the turn. For energy spent on retreating, energy discarded by an effect and energy on a knocked-out Pokémon it's assumed. Cards such as Flame Patch, Professor Sada and some abilities attach energy from the discard pile, which only makes sense if it collects there. Energy isn't a card, so it doesn't go in the discard card list.

**Once per turn belongs to the card.** "Once during your turn" is tracked per card, not per spot on the board (Remco checked this in the app):
- A Pokémon uses its ability, evolves, and the evolved card can use its own ability in the same turn. Evolving resets the "used this turn" mark.
- A Stadium with "Once during each player's turn, that player may…" gives each player one use per turn. If a new Stadium replaces it, the new card can be used too, even if the old one was used this turn. Only one Stadium can be played per turn (printed on the cards), so this can't repeat.
- The same goes for any future "once during your turn" source: the mark sits on the card that has the text.

**Fossils** get Pokémon stats at load: 40 HP, Colorless, a "can't retreat" modifier and a "discard from play" action. After that the engine treats them as Basic Pokémon, with no special case in the core.

## 8. Engine core: rules as small classes

This is the cleanup from the last review, now shaped to carry effects.

- **One class per kind of move:** place Basic, evolve, attach energy, retreat, attack, play Item, play Supporter, attach Tool, play Stadium, use ability, use the Stadium's action, end turn.
  - Each class lists its legal moves and carries them out, so the rules for one move live in one place.
  - Each asks the modifiers. For example, retreat asks "retreat cost?" and "can retreat?", and attack asks "attack cost?" and "can attack?" (Asleep, Paralyzed, "can't attack next turn").
- **`Game` keeps only the turn flow:**
  ```
  start of turn  draw (also the first player on turn 1) → Energy Zone: current = next, roll a new
                 next (no energy for the first player's turn 1) → start-of-turn triggers
  main           moves until Attack or End turn
  attack         can attack? → pay cost → effect script and damage pipeline → knock-out check
  end of turn    unused Energy Zone energy → discard pile → end-of-turn triggers (abilities, Tools,
                 the Stadium's "at the end of each player's turn") → "this turn" effects end →
                 Pokémon Checkup (Poison, Burn, Sleep, Paralysis; the player whose turn ended first)
                 → knock-out check → effects that ran "until the end of the opponent's next turn" end
  ```
- **Named rule checks** instead of bare conditions, for example `ownFirstTurn(state, player)` instead of `turn <= 2`.
- **Fixed values (enums)** for category, stage and trainer kind, set when the cards load. ex is set by `export_cards.py` from the name ending in " ex"; Mega ex from "Mega … ex". Their points (2 and 3) are printed on the cards.
- **Player view:** policies get a `PlayerView`, not the whole state. It shows their own hand, both boards, discard piles, the opponent's hand *size* and any cards revealed by effects. A smarter player then can't cheat by design.

## 9. Folder layout

```
engine/src/
  core/      state.h  action.h  rules.h  fixed_vec.h  view.h
  cards/     card.*   deck.*   (enums, ex/Mega/Fossil flags)
  game/      game.*  (turn flow, knock-outs, Checkup)   damage.*  (pipeline)
  moves/     move_rule.h + one file per kind of move
  effects/   effect.h (Action, Modifier, Trigger)  script.* (parser)  blocks/ (one file per family)
  players/   policy.h  random_policy.h
export_cards.py, compile_effects.py → data/cards.tsv, data/effects.tsv  (+ data/effects_manual.tsv)
```

## 10. Rules and where they come from

Updated 2026-10-16. One row per rule: what the engine does and how we know.

**Sources**, strongest first:
- **App:** Remco checked it in the game, 2026-10-16.
- **FAQ:** the official Pokémon Support [Battle Rules FAQ](https://support.pokemon.com/hc/en-us/articles/38906248102292-Pok%C3%A9mon-TCG-Pocket-Battle-Rules-FAQ).
- **pokemon.com:** the official [deck-building guide](https://www.pokemon.com/us/strategy/learn-how-to-build-a-deck-in-pokemon-tcg-pocket).
- **Card:** printed on the cards (rule boxes, Stadium text).
- **Tips:** the in-app Tips panel, as quoted word for word in deckgym-core's [rule tests](https://github.com/bcollazo/deckgym-core/tree/main/tests/rules), transcribed 2026-09-08.
- **Battle FAQ:** the official in-app "Detailed battle FAQ", as quoted in the same deckgym tests. We couldn't open it ourselves.
- **Fan:** Serebii or fan guides only.
- **Assumed:** the paper-game rule; no Pocket source found.

**Engine column:** ✓ the engine does this. ✗ it doesn't yet; the fix is planned in PROGRESS.md. ○ not built yet.

| Rule | Source | Engine |
|---|---|---|
| **Deck and setup** | | |
| 20 cards, at most 2 with the same name, at least 1 Basic | pokemon.com | ✓ |
| At most 3 energy types per deck | App | ✓ |
| The Bench holds 3 Pokémon | App | ✓ |
| Opening hand of 5 always holds a Basic; no mulligans | Fan | ✓ The real dealing method is unknown: the engine reshuffles until the top 5 hold a Basic (assumed) |
| **Turns** | | |
| The first player draws on turn 1 | App | ✓ |
| The first player gets no energy on turn 1 | FAQ (free attacks are "usable on the first turn of the player that goes first") | ✓ |
| The Energy Zone makes 1 energy per turn, from the types picked for the deck | pokemon.com | ✓ Which type comes up is random and even across those types (assumed) |
| The next energy is known in advance | Card (Porygon-Z, Rainbow Cave) | ✗ only the current one is stored |
| Unused energy goes to the discard pile at the end of the turn; a new one is made at the start of the next | App | ✗ the engine keeps unused energy and makes no new one |
| Hand limit 10: no draws while the hand is full | App | ✓ |
| 1 Supporter per turn, also on the first player's turn 1 | App (turn 1); assumed (1 per turn) | ✓ |
| Items: any number per turn | Assumed | ✓ |
| 1 Tool per Pokémon | App | ✓ |
| 1 Stadium per turn; it replaces the one in play; a Stadium with the same name can't be played | Card (Mesagoza), App | ✓ |
| Retreat once per turn, paying the Retreat Cost | App | ✓ Which energy is paid is picked automatically (shortcut) |
| No evolving on your own first turn, or on a Pokémon's first turn in play | Tips | ✓ |
| "Once during your turn" abilities: once per card, so an evolved Pokémon can use its own ability the same turn | App | ✓ evolving resets the mark (R3) |
| Stadium actions ("once during each player's turn"): once per player per turn, per Stadium card; a new Stadium can be used again | App | ○ no Stadium actions yet |
| Attacking ends the turn; attacks with no cost can be used on the first player's turn 1 | FAQ | ✓ |
| Turn limit 30 (both players' turns together), then a tie | App | ✓ |
| **Damage** | | |
| Order: base → attacker's effects → Weakness → defender's effects → floor at 0 at the end | Battle FAQ | ✓ (R2) |
| Weakness +20, only on the Active Pokémon | Tips | ✓ |
| The defender's "takes −N" effects also apply on the Bench | Assumed | ✓ |
| **Special Conditions** | | |
| Only the Active Pokémon has them. Asleep, Paralyzed and Confused replace each other; Poisoned and Burned stack | Tips | ✓ |
| Checkup after every turn, for both players, the player whose turn ended first. Order: Poison, Burn, Asleep, Paralyzed. Knock-outs happen together at the end | Tips | ✓ |
| Poison 10; Burn 20, then a flip (heads recovers) | Tips | ✓ |
| Asleep: can't attack or retreat; a flip at every Checkup, heads wakes it | Tips | ✓ |
| Paralyzed: can't attack or retreat; recovers at the Checkup after its owner's next turn | Tips | ✓ A Pokémon paralyzing itself would recover a turn early, but no card does that |
| Confused: flip when attacking; tails means no attack and the turn ends. It can still retreat | Tips | ✓ |
| Going to the Bench clears Special Conditions and all effects on the Pokémon | Tips, FAQ | ✓ |
| Evolving clears Special Conditions | App | ✓ |
| Evolving also ends other effects on the Pokémon | Assumed | ✓ |
| **Winning** | | |
| 3 points wins. A knock-out scores 1, an ex 2, a Mega ex 3 | pokemon.com, Card | ✓ |
| A player with no Pokémon in play loses, whatever the points. If both have none, it's a tie | Tips, pokemon.com | ✓ (R1) |
| Both reach 3 points at once, both with Pokémon in play: a tie | Tips | ✓ |
| An empty deck doesn't lose; there's just no draw | Tips | ✓ |
| Only knock-outs score; a Pokémon discarded by an effect gives no points | FAQ | ✓ |
| Both Active Pokémon knocked out together: the player whose turn it isn't picks a new Active first | Assumed | ✓ |

## 11. Build order

Each step adds tests for its blocks, adds rules to the state checker (`check_states`), and reports the new card coverage.

| Step | What | Unlocks |
|---|---|---|
| 0 | **Core cleanup:** move rules as classes, enums, ex from `suffix`, named rules, `choose()`, player view. No behaviour change: 0 violations before and after. | foundation |
| 1 | Script parser, `compile_effects.py` and the coverage report. First blocks: bonus damage, coins, ×N, damage to itself, "does nothing". | ~240 attack texts |
| 2 | Special Conditions and Pokémon Checkup | 133 |
| 3 | Trainer basics: playing Items and Supporters (1 per turn); draw, heal, switch, random search (Professor's Research, Poké Ball, Potion, Sabrina) | almost every real deck |
| 4 | Choices, damage to the Bench, Energy Zone and discard-energy blocks | 134 + 90 + 52 |
| 5 | Timed effects, damage reduction, locks | 145 + 68 + 70 |
| 6 | Abilities (activated, passive, triggers), Tools, Stadiums | 140 + 19 + 4 |
| 7 | The long tail, card by card, as scripts | rest |
