# TCG Pocket engine: how card effects fit in

This is a proposal. Milestones 1 and 2 are built; nothing in this document is implemented yet.
Written 2026-10-15 from `data/cards.json` (TCGdex, fetched 2026-10-15).

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
2. **The attacker's modifiers:** abilities (Lucario +20), Tools, the Stadium, effects that last this turn (Blaine).
3. **Weakness** +20, unless the attack ignores Weakness.
4. **The defender's modifiers:** "takes −20", "prevent all damage", unless the attack ignores effects on the defender.
5. Never below 0. Then apply the damage.

Damage to the Bench skips steps 3–4 (assumed, see §10). Damage an attacker does to itself and damage from Checkup are put on directly.

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
| Pokémon in play | Special Conditions (Asleep, Paralyzed and Confused replace each other; Poisoned and Burned stack on top), attached Tool, ability used this turn, damaged-by-attack turn, timed effects on it, counters ("this effect stacks") |
| Player | Energy Zone *current and next*, Supporter played this turn, Stadium played this turn, a record of this turn and last turn (attacks used, knock-outs suffered, evolutions), player-wide timed effects (Supporter lock) |
| Game | The Stadium in play and who played it |

Timed effects are small records: what it is, who it's on, and when it ends. The game removes them at the right turn boundary. Effects on a Pokémon also end when it leaves the Active Spot or evolves (assumed, see §10).

**Fossils** get Pokémon stats at load: 40 HP, Colorless, a "can't retreat" modifier and a "discard from play" action. After that the engine treats them as Basic Pokémon, with no special case in the core.

## 8. Engine core: rules as small classes

This is the cleanup from the last review, now shaped to carry effects.

- **One class per kind of move:** place Basic, evolve, attach energy, retreat, attack, play Item, play Supporter, attach Tool, play Stadium, use ability, end turn.
  - Each class lists its legal moves and carries them out, so the rules for one move live in one place.
  - Each asks the modifiers. For example, retreat asks "retreat cost?" and "can retreat?", and attack asks "attack cost?" and "can attack?" (Asleep, Paralyzed, "can't attack next turn").
- **`Game` keeps only the turn flow:**
  ```
  start of turn  draw → Energy Zone (current, next) → start-of-turn triggers
  main           moves until Attack or End turn
  attack         can attack? → pay cost → effect script and damage pipeline → knock-out check
  end of turn    end-of-turn triggers → "this turn" effects end → Pokémon Checkup
                 (Poison, Burn, Sleep, Paralysis) → knock-out check → effects that ran
                 "until the end of the opponent's next turn" end
  ```
- **Named rule checks** instead of bare conditions, for example `ownFirstTurn(state, player)` instead of `turn <= 2`.
- **Fixed values (enums)** for category, stage and trainer kind, set when the cards load. ex comes from TCGdex's `suffix`, not the name.
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

## 10. Rules to confirm in the app

These are standard Pokémon TCG rules I'm assuming for Pocket. I couldn't find them in a public source. They'll go in `rules.h` or the Checkup code, in one place.

| Rule | Assumed |
|---|---|
| Poisoned | 10 damage at every Checkup |
| Burned | 20 damage at Checkup, then flip: heads recovers |
| Asleep | Can't attack or retreat; flip at Checkup, heads wakes up |
| Paralyzed | Can't attack or retreat; wears off at the end of the owner's next turn |
| Confused | Flip when attacking: tails means the attack fails. (The regular card game does 30 to itself instead. Which is it in Pocket?) |
| Moving to the Bench or evolving | Removes all Special Conditions and effects on that Pokémon |
| Supporters | 1 per turn. Can the player going first play one on turn 1? |
| Tools | 1 per Pokémon |
| Mega ex | Worth 3 points |
| Both players reach 3 points at once | ? (draw or sudden death) |
| Damage to the Bench | No Weakness |

The open guesses from milestones 1–2 still apply too: the turn cap of 30, the first player drawing on turn 1, an empty deck, the hand limit of 10, and nobody evolving on their own first turn.

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
