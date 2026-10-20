# TCG Pocket engine: how card effects fit in

Written 2026-10-15 as the plan; §3–§9 updated 2026-10-19 to the engine as built (97% of 2,319 cards playable). PROGRESS.md has
the current state and next steps. §1 is the original survey, made on the TCGdex data of 2026-10-15 (1,708 cards); the card
data now comes from Limitless.

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
- **Groups the data doesn't mark:** Ultra Beasts, and "Arceus or Arceus ex". `cards.tsv` has no field for these.
- **ex:** set by `export_cards.py` from the name ending in " ex" (Limitless leaves the ex rule box off some cards); Mega from "Mega … ex".

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

The events triggers can react to are also fixed (the `CardDef` fields, one form each in `effects.tsv`):
- the start of your turn, after the draw (`at-start-of-turn`);
- the end of your turn (`at-end-of-turn`), the end of each turn before Checkup (`at-end-of-each-turn`), and during Checkup before Poison and Burn (`during-checkup`);
- Energy attached from the Energy Zone, to this Pokémon (`on-attach`) or by the opponent (`on-opp-attach`);
- this Pokémon damaged by an attack (`on-damaged`);
- this Pokémon knocked out (`on-knocked-out`), or knocking out an opponent's Pokémon (`on-knocks-out`);
- played from the hand to evolve (`on-evolve`) or onto the Bench (`on-bench`);
- a Stadium's end of each player's turn (`stadium-end-of-turn`).

## 4. Effect scripts

Each effect text becomes a short script in a small language with nested brackets. It's stored in `data/effects.tsv`, one row per card part: card id, part (attack 1, ability, trainer) and script.

| Card text | Script |
|---|---|
| Flip 2 coins. This attack does 30 damage for each heads. | `(seq (flip 2) (set-damage (* heads 30)) (hit))` |
| Flip a coin. If heads, this attack does 30 more damage. | `(seq (flip 1) (if heads (bonus 30)) (hit))` |
| This Pokémon also does 20 damage to itself. | `(seq (hit) (damage self 20))` |
| This attack also does 20 damage to 1 of your opponent's Benched Pokémon. | `(seq (hit) (damage (choose opp bench) 20))` |
| During your opponent's next turn, this Pokémon takes −20 damage from attacks. | `(seq (hit) (effect opp-next-turn self (damage-taken -20)))` |
| If any of your Pokémon were Knocked Out by damage from an attack during your opponent's last turn, this attack does 60 more damage. | `(seq (if (ko-last-turn) (bonus 60)) (hit))` |
| Put a random Basic Pokémon from your deck into your hand. (Poké Ball) | `(play (search 1 hand basic))` |
| The Pokémon this card is attached to gets +20 HP. (Giant Cape) | `(tool (hp 20))` |
| If this Pokémon has a Pokémon Tool attached, attacks used by this Pokémon cost 1 less {G} Energy. (Cherubi) | `(passive (attack-cost -1 :energy grass :if (has-tool self)))` |
| If this Pokémon is in the Active Spot and is Knocked Out by damage from an attack from your opponent's Pokémon, do 50 damage to the Attacking Pokémon. (Pyukumuku) | `(on-knocked-out :active :by-attack (damage attacker 50))` |

An attack is a statement where `(hit)` marks where its printed damage is dealt; `pre` parts change that damage first,
`post` parts come after. A card part's top-level form says how it plugs in (§3).

**Vocabulary** (2026-10-20, about 200 words; `load_scripts.cpp` and the `blocks_*.cpp` files define them, each with its
argument count):

| Kind | Words |
|---|---|
| Forms | `play` `tool` `stadium` `stadium-action` `stadium-end-of-turn` `passive` `activated` (`:active`, `:bench`, `:repeat`) and the triggers in §3 (`:active`, `:by-attack`, `:type`) |
| Flow | `seq` `if` (with an else) `then` ("if you do") `one-of` (the player picks) `repeat` `pick` `nothing` `cancel` `end-turn` `flip` `flip-until-tails` `first-heads` `ko-check` (knock-outs in the middle of an attack) `use-supporter` |
| Damage | `hit` (`:no-weakness`, `:ignore-effects`) `bonus` `set-damage` `damage` `hits` (random picks, added up first) `move-damage` `set-hp-left` `halve-hp` `knock-out` `copy-attack` `poison` (a set amount) `damage-at-end-of-opp-turn` `knock-out-at-end-of-opp-turn` (`:spot`) `devolve` `copy-attack-cards` (`copy-attack :energy`) |
| Conditions on Pokémon | `status` `status-random` `cure` `cure-random` `heal` |
| Energy | `attach-energy` (from the Zone) `attach-random-type` `attach-from-discard` (a type, `any`, `random`, `different`) `discard-energy` `discard-random-energy` `discard-energy-among` `move-energy` `move-random-energy` `cycle-energy` `change-energy-type` `change-next-energy` |
| Cards | `draw` `draw-until` `search` (hand or Bench) `dig` `from-discard` `hand-to-deck` `discard-from-hand` `discard-hand-to` `discard-hand-random-of` `mill` `hand-to-deck-random` `shuffle-hand-draw` `opp-hand-to-deck` `opp-hand-discard` `top-card-if` `swap-pokemon` `swap-for` `rare-candy` `evolve-from-deck` `shuffle-deck` `tools-to-deck` `opp-hand-to-bench` (`hand-to-deck-random any`) |
| Board | `switch-in` `to-hand` `to-deck` `to-hand-instead` `discard-pokemon` `discard-tools` `discard-this` `tools-to-hand` `discard-stadium` `no-points` `effect` (a modifier for `this-turn`, `opp-next-turn`, `my-next-turn` or `while-active`) |
| Targets | `self` `defending` `attacker` `picked` `my-active` `opp-active`, and pools `(choose …)` `(owner-chooses …)` `(random …)` `(each …)` over `my`/`opp`/`both` and `active`/`bench`/`all` |
| Pokémon filters | `damaged` `ex` `not-ex` `mega` `basic` `stage1` `stage2` `evolution` `tool` `has-ability` `not-self` `not-picked` `not-new` `(type …)` `(name …)` `(name-has …)` `(evolves-from …)` `(has-energy …)` `(other-ability …)` `(max-hp …)` `(trait …)` |
| Card filters | `pokemon` `basic` `stage1` `stage2` `trainer` `supporter` `item` `tool` `stadium` `mega` `(type …)` `(name …)` `(name-has …)` `(except …)` `(evolves-from …)` `(max-hp …)` `(has-attack …)` `(trait …)` `(min-retreat …)` |
| Numbers | integers, `heads` `tails` `(* …)`, `energy` `energy-types` `types-on` `damage-on` `hp-left` `retreat-cost` `status-count` `count` `hand-size` `discard-count` `deck-count` `points` `points-needed` `points-last-turn` `tools` `ko-count` `with-attack` `dealt` `top-count` `attack-uses` |
| Conditions | `>=` `<=` `=` `and` `or` `not`, `all-heads` `all-tails`, `has-damage` `has-tool` `has-status` `has-attack` `is-type` `is-ex` `is-basic` `is-evolution` `is-active` `half-hp` `at-zero` `energy-at-least` `extra-energy` `shared-energy` `exists` `moved-to-active` `evolved-this-turn` `evolved-from` `damaged-last-turn` `ko-last-turn` `used-last-turn` `healed` `played-supporter` `stadium-in-play` `my-turn` `first-turn` `first-attack` (`has-tool` with a name) |
| Modifiers | `damage-taken` `damage-dealt` `prevent-damage` `prevent-all` `prevent-effects` `prevent-flip` `prevent-first-hit` `prevent-ability-damage` `reflect` `hp` `retreat-cost` `no-retreat-cost` `cant-retreat` `cant-attack` `cant-use-attack` `attack-cost` `attack-flip` `cant-supporter` `cant-item` `cant-stadium` `cant-trainer` `cant-evolve` `cant-evolve-active` `cant-attach-to-active` `weakness-x2` `no-weakness` `no-conditions` `no-healing` `no-abilities` `poison-damage` `survive-at` `extra-point` `extra-tools` `extra-type` `double-energy` `evolve-early` `evolves-as` `previous-attacks` `extra-pick` `asleep-on-attach` `retreat-trap` `no-abilities` (timed too) `reflip-attack` `reflip-trainer` (`damage-dealt :any-target`) |
| Modifier options | `:scope` (self, my-active, my-all, opp-active, opp-all, all) `:active` `:bench` `:type` `:stage` `:name` `:evolves-from` `:trait` `:with-energy` `:status` `:needs` `:if COND` `:from` `:from-ex` `:from-basic` `:vs-ex` `:not-mega` `:min-retreat` `:flip` `:energy` `:per-point` `:as` |

**Where scripts come from:**
1. **`compile_effects.py`:** a table of sentence patterns, each a regular expression with blanks for numbers, types and targets, mapped to a script template. It handles the common shapes automatically and grows the exporter we already have.
2. **`data/effects_manual.tsv`:** hand-written scripts for the long tail. An LLM could draft these and a person reviews them, which fits the LLM plans for later. (Not built yet.)
3. **A coverage report:** which cards are fully supported, which pattern covers how many cards, and which sentences match nothing.

**How fragile is this?** (2026-10-16) Effects built from shared blocks are the normal way, for example MTG Arena and Forge (§12). What's unusual here is that sentence patterns write the scripts from card text. That can go wrong in three ways:
- **New wording matches no pattern.** The card is unsupported and shows in `--coverage`. That's safe.
- **A pattern matches but the meaning is slightly off.** The card plays wrong and nobody notices. This is the real risk. For example, TCGdex's Pachirisu text was missing "{L}", and the script copied the mistake.
- **A pattern change for one card quietly changes others.**

What guards against it today:
- `data/effects.tsv` is in git, so every regenerated script shows as a diff;
- the compiler has a self-test (86 examples);
- **golden tests** (2026-10-20): `data/golden.tsv` holds 487 real cards, each with its text and its reviewed scripts.
  They were picked so that every sentence pattern the compiler uses (384 of them, the inline ones included) appears on
  at least two of them, plus every hand-written card. `compile_effects.py --test` fails when a script or a card's text
  changes. After an intended change, review the diff and take it with `--accept-golden`;
- `data/effects_manual.tsv` for odd cards, so the patterns don't grow ever more specific;
- the coverage report;
- unit tests for the blocks.

`check_states` doesn't help here: it checks the rules, not whether a script matches its card. For each new set, also
review the `effects.tsv` diff.

**C++ side:** each script is parsed once when the cards load, into a tree of small block objects, one class per word. An unknown word makes the card unsupported and names the word. Nothing gets parsed during a game.

This avoids writing C++ per card. There are about 200 words, and adding a card is a data change, unless its text needs a new word.

## 5. Damage pipeline (one place, fixed order)

1. **Base damage** from the attack, including its own +N, ×N, coins and "for each".
2. **The attacker's modifiers:** abilities (Lucario +20), Tools, the Stadium, effects that last this turn (Blaine), and effects on the attacker such as "the Defending Pokémon's attacks do −20 damage".
3. **Weakness** +20 (×2 under Bounded Field), unless the attack ignores Weakness. It applies whenever the attack does damage, even if step 2 brought the number to 0 or below.
4. **The defender's modifiers:** "takes −20", "prevent all damage", unless the attack ignores effects on the defender.
5. Floor at 0, once, here at the end. Then apply the damage.

This is the official order from the "Detailed battle FAQ" (§10). Example: 10 damage, −20 on the attacker, a weak defender: 10 − 20 + 20 = 10.

Damage to the Bench skips Weakness (Tips). The attacker's bonuses name the Active ("to your opponent's Active Pokémon"), so they don't apply on the Bench either. The defender's own modifiers do apply on the Bench (assumed, as in the paper game). Damage an attacker does to itself and damage from Checkup are put on directly.

Two attack options skip parts of this order: `:no-weakness` skips step 3, and `:ignore-effects` skips step 4 entirely (all of
the defender's Abilities, Tools and effects left by attacks). A defender with "no Weakness" skips step 3.

**After any damage** (attack, Bench, self, Checkup, ability), one knock-out check runs as a loop:
- it scans every Pokémon in play, not only the Defending one; "would be Knocked Out" effects (Hala, Conkeldurr) may leave 10 HP;
- each knocked-out Pokémon's triggers run first, while its cards are still in play (Pyukumuku, Rescue Scarf, Dusknoir's "no points"), then the attacker's "knocks out" triggers;
- it awards points: ex 2, Mega ex 3, plus effects such as Iris;
- trigger damage can knock out more Pokémon, so it looks again until nothing is left at 0 HP;
- only then come the win rules, and each side moves a new Pokémon to Active if needed.

## 6. Decisions inside effects

An effect asks the game `choose(player, options)`. The game turns the options into normal moves (new kinds: choose target, choose card, choose attack, yes/no) and asks that player's Policy. The chooser can be the opponent, as in "Your opponent chooses the new Active Pokémon".

Promoting after a knock-out moves onto this same mechanism, which removes the special `Promote` phase. Random outcomes such as coins and random cards stay inside the engine and are never a player's choice.

The trade-off is that the game doesn't pause mid-effect. That keeps it simple and fast. A future lookahead player branches on its main moves and makes these smaller choices with its own policy. If a search player has to branch on choices inside effects, the pending choice moves into the state instead (§12).

## 7. What the state needs to remember

| Where | Added |
|---|---|
| Pokémon in play | Special Conditions (Asleep, Paralyzed and Confused replace each other; Poisoned and Burned stack on top); its Tools (one, two with Revavroom); the turn it came into play, last became Active, last used its "once during your turn" Ability (see "Once per turn belongs to the card" below) and last took attack damage as the Active; whether an attack has hit it since it came into play (Mimikyu ex); the stage it came into play as (Silcoon); whether it's being knocked out by an effect rather than damage |
| Player | Energy Zone *current and next*; energy in the discard pile, as a count per type; Supporter and Stadium played this turn, Stadium action used this turn; memory of earlier turns: the last turn one of their Pokémon was knocked out by an attack (and those types), their last attack and its turn, points scored that turn, knock-outs this game |
| Game | The Stadium in play and who played it; timed effects on a Pokémon or on a player |

Timed effects are small records: what it is, who it's on, and when it ends. The game removes them at the right turn boundary. Effects on a Pokémon also end when it goes to the Bench (official FAQ) or evolves (assumed, see §10).

**Energy in the discard pile.** Energy that leaves play lands in its owner's discard pile, as a count per type. This was checked in the app for all of these. Cards such as Flame Patch, Professor Sada and some abilities attach energy from the discard pile, which only makes sense if it collects there. Energy isn't a card, so it doesn't go in the discard card list.

**Once per turn belongs to the card.** "Once during your turn" is tracked per card, not per spot on the board (checked in the app):
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
                 next (no energy for the first player's turn 1) → "at the beginning of your turn" Abilities
  main           moves until Attack or End turn
  attack         can attack? → pay cost → effect script and damage pipeline → knock-out loop (§5)
  end of turn    unused Energy Zone energy → discard pile → "at the end of your turn" Abilities and Tools →
                 "at the end of each turn" Abilities and Tools (both players: berries, Darkrai, Caterpie) →
                 Pokémon Checkup: "during Checkup" Abilities, then Poison, Burn, Sleep, Paralysis (the player
                 whose turn ended first) → knock-out loop → the Stadium's "at the end of each player's turn" →
                 timed effects that end this turn → the next player's turn
  ```
  ```
- **Named rule checks** instead of bare conditions, for example `ownFirstTurn(state, player)` instead of `turn <= 2`.
- **Fixed values (enums)** for category, stage and trainer kind, set when the cards load. ex is set by `export_cards.py` from the name ending in " ex"; Mega ex from "Mega … ex". Their points (2 and 3) are printed on the cards.
- **Player view:** policies get a `PlayerView`, not the whole state. It shows their own hand, both boards, discard piles, the opponent's hand *size* and any cards revealed by effects. A smarter player then can't cheat by design.

## 9. Folder layout

```
engine/src/
  core/      state.h  action.h  rules.h  fixed_vec.h  view.h
  cards/     card.*  deck.*   (enums, ex/Mega/Fossil flags, traits)
  game/      game.*  (turn flow, knock-out loop, Checkup, triggers)   damage.*  (pipeline)
  moves/     move_rule.cpp + one file per kind of move
  effects/   effect.h  modifier.h  script.* (parser)  builder.cpp  load_scripts.cpp  blocks_*.cpp (one file per family)
  players/   policy.h
engine/tests/  test_rules, test_effects, check_states (state checker; check_all.sh runs it on all cores)
export_cards.py, compile_effects.py → data/cards.tsv, data/effects.tsv; data/traits.tsv by hand (Serebii)
```

## 10. Rules and where they come from

Updated 2026-10-19. One row per rule: what the engine does and how we know.

**Sources**, strongest first:
- **App:** checked in the game, 2026-10-16.
- **Ruling:** how this project reads the card text, settled before it was built; not checked in the app.
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
| The next energy is known in advance | Card (Porygon-Z, Rainbow Cave) | ✓ both players' next energy is rolled at the start of the game (R4) |
| Unused energy goes to the discard pile at the end of the turn; a new one is made at the start of the next | App | ✓ (R4) The exact moment, before the end-of-turn triggers, is assumed |
| Energy paid for a retreat, discarded by an effect, or on a knocked-out Pokémon goes to its owner's discard pile | App | ✓ (R4) |
| Hand limit 10: no draws while the hand is full | App | ✓ |
| 1 Supporter per turn, also on the first player's turn 1 | App (turn 1); assumed (1 per turn) | ✓ |
| Items: any number per turn | Assumed | ✓ |
| A Trainer can only be played when its effect has something to do: no Potion without damage, no Rare Candy without a Stage 2 to put down | App | ✓ Each effect block checks this |
| The same goes for "once during your turn" abilities: no Psy Shadow without a Psychic Active | Ruling | ✓ |
| When a Pokémon has more than one type of energy, the player picks which type is paid for a retreat, discarded or moved | Ruling | ✗ the engine picks; step 4 in PROGRESS.md |
| An effect that starts something can always be played, even if it can never pay off: Blaine without Ninetales, X Speed after retreating or with a Retreat Cost of 0 | App | ✓ timed effects always count as something to do |
| 1 Tool per Pokémon | App | ✓ |
| 1 Stadium per turn; it replaces the one in play; a Stadium with the same name can't be played | Card (Mesagoza), App | ✓ |
| Retreat once per turn, paying the Retreat Cost | App | ✓ Which energy is paid is picked automatically (shortcut) |
| No evolving on your own first turn, or on a Pokémon's first turn in play | Tips | ✓ |
| "Once during your turn" abilities: once per card, so an evolved Pokémon can use its own ability the same turn | App | ✓ evolving resets the mark (R3) |
| "If this Pokémon moved from your Bench to the Active Spot this turn" (Golisopod, Scizor, Basculin, Crobat): it counts for the card now on top. Evolved on the Bench then moved up, or benched this turn then moved up: yes. Moved up as Wimpod, then evolved: no | Ruling | ✓ evolving resets the mark, like R3 |
| Stadium actions ("once during each player's turn"): once per player per turn, per Stadium card; a new Stadium can be used again | App | ✓ the Use Stadium move (R5) |
| Stadium effects "at the end of each player's turn" (Hiking Trail, Soothing Shore) come last: after the end-of-turn abilities and after Pokémon Checkup | Ruling | ✓ |
| Rainbow Cave: discarding the Energy Zone's energy works like an extra turn for the zone: the shown next energy becomes the current one, and a new next one is rolled | Ruling | ✓ |
| Attacking ends the turn; attacks with no cost can be used on the first player's turn 1 | FAQ | ✓ |
| Turn limit 30 (both players' turns together), then a tie | App | ✓ |
| **Damage** | | |
| Order: base → attacker's effects → Weakness → defender's effects → floor at 0 at the end | Battle FAQ | ✓ (R2) |
| Weakness +20, only on the Active Pokémon | Tips | ✓ |
| Bounded Field: Weakness ×2 takes the place of +20 in the damage order (base → attacker's effects → ×2 → defender's effects) | Ruling | ✓ |
| Damage from an attack's text ("do 20 damage to each of your opponent's Pokémon", Lurantis) gets Weakness when it hits the Active Pokémon | Tips (only the Bench is excluded); deckgym-core's Lurantis test | ✓ |
| Random multi-hit attacks (Wugtrio ex: "chosen at random 3 times… do 50 damage to it"): the picks on one Pokémon add up first and are dealt as one, so Weakness and "+N damage" count once. All 3 on a Weak Active: 150 + 20 | Ruling | ✓ `(hits …)` |
| The defender's "takes −N" effects also apply on the Bench | Assumed | ✓ |
| **Special Conditions** | | |
| Only the Active Pokémon has them. Asleep, Paralyzed and Confused replace each other; Poisoned and Burned stack | Tips | ✓ |
| Checkup after every turn, for both players, the player whose turn ended first. Order: Poison, Burn, Asleep, Paralyzed. Knock-outs happen together at the end | Tips | ✓ |
| Lum Berry, Sitrus Berry ("at the end of each turn") act before Pokémon Checkup, so a Lum Berry cures Poison before it does damage | Assumed (best guess, to double-check) | ✓ |
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
| A card taken from the opponent (Team Rocket's Thieving Machine) becomes the taker's: once played it goes to the taker's discard pile | Assumed | ✓ open question in PROGRESS.md |
| A Pokémon put back into the hand (Koga, Ilima) takes its evolution cards and Tool with it; its energy goes to the discard pile | Ruling | ✓ |
| Putting your Active Pokémon back into the hand is allowed with an empty Bench: with no Pokémon left in play, that player loses (a way to concede) | Ruling | ✓ |
| A card put back into a full hand (10 cards) goes to the discard pile instead | Assumed | ✓ open question in PROGRESS.md |
| Both Active Pokémon knocked out by one attack: the defending player picks a new Active first, then the attacking player, then Pokémon Checkup | App | ✓ |
| **Knock-outs** | | |
| Knock-out triggers ("If this Pokémon is Knocked Out…", Rescue Scarf, Pyukumuku) run first; then every knock-out of that moment counts, and only then the win rules apply | Ruling | ✓ |
| Both at 3 or more points at once: a player with Pokémon left wins; Pokémon on both sides or on neither is a tie. More points (4 against 3) don't matter | Ruling | ✓ |
| Knock-out triggers of one moment: the defender's Pokémon first, each with its Ability and then its Tools; the attacker's "knocks out" triggers after each knock-out | Assumed | ✓ |
| Rescue Scarf: the Pokémon and its evolution cards go to the hand; the Scarf and the energy go to the discard pile; the opponent still gets the points | Ruling | ✓ |
| Lucky Mittens: one card per Pokémon knocked out | Ruling | ✓ |
| "Is Knocked Out" by an effect (Bewear, Galarian Cursola) isn't "Knocked Out by damage from an attack" (no Rescue Scarf, Hala, revenge bonus) | Assumed | ✓ |
| Electrical Cord with one Benched Pokémon moves only 1 Energy ("1 Energy each to 2") | Assumed | ✓ |
| **Abilities** | | |
| "During Pokémon Checkup" Abilities (Garganacl, Glaceon ex, Flygon ex) act first in Checkup, before Poison and Burn | Ruling | ✓ |
| "At the end of each turn" and "At the end of your opponent's turn" Abilities (Darkrai, Caterpie) act before Checkup | Ruling | ✓ |
| "At the beginning of your turn" (Meloetta) comes after the turn's draw and new Energy | Ruling | ✓ |
| Serperior's "each {G} Energy provides 2 {G} Energy": every count sees 2 per {G}: attack and retreat costs, "for each Energy attached", HP per Energy | Ruling | ✓ |
| An effect that discards Energy still discards real Energy: a doubled {G} goes as one | Assumed | ✓ |
| Urshifu's second type counts for "{X} Pokémon" effects only; Weakness always uses the printed type | Ruling | ✓ |
| Jolteon ex reacts to every Energy attached from the Energy Zone, by an effect too | Ruling | ✓ |
| Dusknoir may move damage that knocks itself out; the opponent scores | Ruling | ✓ |
| "You may" on a "when you put this Pokémon onto your Bench" Ability: the player chooses | Card | ✓ |
| Alolan Muk turns off Basic Pokémon's Abilities, not their Tools, and not a Fossil's rule (a Fossil has no Ability) | Assumed | ✓ |
| Under Claydol, a Potion can't be played: it has nothing to do | Assumed (as "Trainers need something to do") | ✓ |
| Misdreavus's "your opponent reveals their hand" does nothing in the engine: it's information only | Assumed | ✓ |
| Evolving from the deck by an effect (Caterpie, Porygon2, Magikarp) ignores the usual timing limits | Assumed | ✓ |
| A Pokémon whose HP drops below its damage because it lost Energy (Reuniclus retreating) is knocked out at once | Assumed (like a Stadium's +HP going away) | ✓ |
| **Long tail (batch 10)** | | |
| "This attack's damage isn't affected by any effects on your opponent's Active Pokémon" ignores all of the defender's effects: its Abilities, its Tools and effects left by attacks | Ruling | ✓ |
| Silcoon and Cascoon's "put … onto your Bench" puts Stage 1 Pokémon there with nothing under them; they evolve as usual | Ruling | ✓ |
| Ultra Beast, Ancient and Future: Serebii's lists, by card name, in `data/traits.tsv`. Serebii also lists the three Necrozma promos (Dawn Wings, Dusk Mane, Ultra Necrozma ex) as Ultra Beasts | Fan (Serebii) | ✓ open question in PROGRESS.md |
| "Its remaining HP is now N" never heals a Pokémon that has less left | Assumed | ✓ |
| "Halve its remaining HP, rounded down" rounds down to a whole 10 | Assumed | ✓ |
| A copied attack (Mew ex, Clefairy) needs no Energy, acts for the Pokémon that copies it, and can't copy again. The Dittos that say "if this Pokémon doesn't have the necessary Energy" need Energy that pays the copied attack's printed cost, types and amount included | Assumed; Ditto: App (2026-10-19) | ✓ |
| "Can't play any Trainer cards" covers Items, Supporters, Tools and Stadiums | Card (they're all Trainer cards) | ✓ |
| **Card names** | | |
| A named Pokémon means exactly that name: "Crabominable" doesn't cover Crabominable ex. A card that includes an ex names it | Ruling | ✓ |
| **Memory of the last turn** | | |
| "Your opponent's last turn" is the turn just before this one; "your last turn" is your turn before that | Card | ✓ |
| "Knocked Out by damage from an attack": Active or Benched Pokémon knocked out by the opponent's attack. Not Poison or Burn at Checkup, not Ability damage | Ruling | ✓ |
| A point scored at Checkup at the end of a player's turn counts as "during their last turn" (Hisuian Basculegion) | Ruling | ✓ |
| An attack that doesn't happen (Confused, or a coin-flip effect, tails) wasn't "used" (Sweets Relay) | Ruling | ✓ |
| **One-offs (batch 11)** | | |
| The Bench has 3 fixed spots. A Pokémon put on the Bench takes a free spot; a switch with the Active swaps the two; a knocked-out Active is replaced from the Bench, and that Bench spot stays empty | App (2026-10-19) | ✓ |
| Poison for a set amount (Toxicroak 20, Toxapex 40): the latest Poison sets the amount, so a plain Poison afterwards does 10 again | Ruling (2026-10-19) | ✓ |
| "At the end of your opponent's next turn, do N damage to the Defending Pokémon" (Mismagius) follows that Pokémon onto the Bench, does nothing if it left play, and still happens if the attacker left play. It's plain damage: no Weakness, no damage changes | Ruling (2026-10-19) | ✓ |
| "Choose a spot… at the end of your opponent's next turn" (Meowscarada ex, Armaldo) hits whoever is in that spot then; an empty spot means nothing happens. The same plain damage as Mismagius | Ruling (2026-10-19) | ✓ |
| Galarian Stunfisk's trap: only a retreat sets it off, not a switch by an effect; the 40 is attack damage, with Weakness | Ruling (2026-10-19) | ✓ |
| Gothitelle: any attach from the Energy Zone to that Pokémon, by any card, makes it Asleep | Ruling (2026-10-19) | ✓ |
| Devolving (Celebi): the top card goes to the owner's hand; damage stays, so it can be knocked out (points as usual); Special Conditions and effects end; Energy and Tools stay | Ruling (2026-10-19) | ✓ |
| Mega Kangaskhan ex's two hits are both attacks. If the first knocks out the Active, the opponent picks a new Active, which takes the second hit; if the first knock-out wins the game, there is no second hit | App (2026-10-19) | ✓ |
| Will: only coin flips for the effect of an attack, Ability or Trainer card count, not Checkup or Confusion | Ruling (2026-10-19) | ✓ |
| Team Rocket's Boss: as many Basic Pokémon as the opponent's Bench has room for; they don't set off "when you play this Pokémon" Abilities | Ruling (2026-10-19) | ✓ |
| Flareon B4-022 "Discard Fire{R} Energy" (Limitless and deckgym) means 1 {R} Energy | App (2026-10-19) | ✓ |
| A Pokémon holding more Tools than it may (Revavroom devolved by Celebi, or its Ability switched off by Alolan Muk or Budew) discards the last one attached, right away | App (2026-10-20) | ✓ |
| Mew B2b's "1 attack from among the Pokémon in your opponent's hand and deck": a list of every attack on those cards, one picked at random, so 2 copies make an attack twice as likely | App (2026-10-20) | ✓ |
| Team Rocket's Boss can be played when the opponent turns out to have no Basic Pokémon in hand: looking is part of the effect | App (2026-10-20) | ✓ |
| "Choose a spot" (Meowscarada ex, Armaldo): only a spot with a Pokémon in it can be chosen | App (2026-10-20) | ✓ |
| A copying attack (Mew ex, Ditto) is the attack that's used, not the copied one: a copied Sweets Relay doesn't count for Alcremie, and a copied "this Pokémon can't use X during your next turn" doesn't bind the copier | App (2026-10-20) | ✓ |
| Fossils are Item cards outside play (deck, hand, discard pile): Pokémon searches (Poké Ball, May) don't find them, Item checks (Pachirisu) count them, Team Rocket's Boss can't bench them. In play a Fossil is a Basic Pokémon | App (2026-10-20) | ✓ |
| **The last four (batch 12)** | | |
| Penny and Smeargle: a random Supporter (Penny: one that isn't Penny, from the opponent's deck; Smeargle: from their hand) is used as if this player had played it; nothing happens when there is none or its effect has nothing to do | App (2026-10-20) | ✓ |
| An effect used through Penny or Smeargle doesn't use another one (Smeargle finding a Penny does nothing) | Assumed | ✓ |
| Victini and Gholdengo, taken literally: right after the coins they cover, the player may flip them all again, once a turn and one Ability of that name a turn; declining doesn't use the Ability up. Victini covers the coins of an attack's own text when one of the player's {R} Pokémon attacks (not Confusion, not "your opponent flips a coin" effects); Gholdengo covers the coins of the player's Trainer card effects, a Supporter used through Penny included, through Smeargle not (that's an Ability) | Ruling (2026-10-20) | ✓ |

## 11. Build order

Each step adds tests for its blocks, adds rules to the state checker (`check_states`), and reports the new card coverage.

Status 2026-10-19: steps 0–7 are built: 2,269 of 2,319 cards (97%) are playable after coverage batches 0–10 (PROGRESS.md). The 50 left are one-offs.

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

## 12. Other engines, and what we take from them

Research from 2026-10-16. Details and links come from the sources named below. This section reports what they say; we didn't verify it all ourselves.

### Pokémon TCG Pocket simulators

- **[deckgym-core](https://github.com/bcollazo/deckgym-core)**: Rust, AGPL licence, actively maintained. It's the closest one to ours.
  - Card effects are Rust code per card, mostly in one 7,000-line file of attack handlers; there's no script language. It covers about 3,340 of 3,761 prints (89%; prints include reprints, so the numbers don't compare directly with ours).
  - Choices inside effects are stored in the state as a pending decision, and the game loop offers them as normal moves. The state can be copied and hashed, so search players can copy it.
  - Players: random, weighted random, attach-and-attack, evolution rusher, a value-function player, expectiminimax, MCTS. Its search players see hidden cards.
  - About 3,300 random games/s. An `optimize` command completes a partial deck by testing candidate cards against a folder of opponent decks.
  - Its rule tests quote the in-app Tips panel and the official "Detailed battle FAQ" word for word. We used them as a source in §10, and its `database.json` is our second card source (`check_cards.py`).
- **Others:** small hobby projects (a Python simulator with a few cards; another covering set A1 only, inactive).
- **Pokémon's own CPU player** (DeNA, CEDEC 2026 talk): it splits multi-step choices into atomic decisions and bundles them into one action. It's trained with reinforcement learning (a small Transformer, about 50M games), and uses no tree search because of the hidden information.

### Engines for other card games

| Engine | Card effects | Choices inside effects | Search |
|---|---|---|---|
| MTG Arena (Magic, official) | A C++ rules core; each card's abilities are small scripts in CLIPS, a rules language ([Wizards dev blog](https://magic.wizards.com/en/news/mtg-arena/on-whiteboards-naps-and-living-breakthrough)) | Not public | Not public |
| Forge (Magic) | Text scripts per card | Calls the player directly | Copies the game for its simulation AI |
| XMage (Magic) | A Java class per card, built from a shared effect library | Calls the player directly | Copies the game; minimax and MCTS players |
| SabberStone (Hearthstone) | Card definitions built from task objects | Pending choice in the state | Copies the game; hides the opponent's hand behind dummy cards |
| MetaStone / Spellsource (Hearthstone-like) | JSON data per card | Calls the player directly | Greedy and flat Monte Carlo players |
| RyuuPlay (Pokémon TCG) | A TypeScript class per card; every effect passes through every card in play | Prompts with a callback | A simple bot with tactics and state scoring |
| TCG ONE (Pokémon TCG) | A small Groovy language per card | Calls the player directly | Not public |

**What that means for us:**
- **Effects as data** puts us with Forge, MetaStone and TCG ONE. It's the right fit for a fixed card pool that grows every month: a new card is a data change.
- **Calling the player directly** is the simplest choice, and Forge and XMage do it too. It stops a search player from copying the game in the middle of an effect. Moving the choice into the state is the known fix, if we ever need it.
- **Hidden information:** search players should guess the hidden cards (determinization), or use information-set MCTS (Cowling et al. 2012), not look at the real state.

### Judging decks by simulation

- **Every player has a bias.** Research on Hearthstone deck search (Fontaine et al. 2019) found that any AI player favours certain kinds of decks. A random player rewards simple decks with cheap attackers and punishes evolution lines and Supporters that need good timing (our reading, not a sourced finding). So decks found with the random player are ideas to try, not proof.
- **Games needed** for a win rate at 95% confidence (worst case):

  | Within | Games |
  |---|---|
  | ±5% | 385 |
  | ±3% | 1,068 |
  | ±2% | 2,401 |
  | ±1% | 9,604 |

- **Comparing two decks** roughly doubles the variance. Give both the same seeds and the same opponents, and alternate who goes first.
- **Draws:** about 70% of our random-vs-random games end at the turn cap, so stats should report wins, draws and losses separately.
