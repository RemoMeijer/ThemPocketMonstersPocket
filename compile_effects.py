"""Turn card text from data/cards.tsv into effect scripts in data/effects.tsv.

Each piece of text (an attack's effect, an ability, a Trainer's or Fossil's text) is split into
sentences, and each sentence is matched against the patterns below. A piece where every sentence
matches becomes one script; anything else is left out, and the engine then treats that card as
unsupported. The script language is described in DESIGN.md §4 and engine/src/effects/load_scripts.cpp.

  python3 compile_effects.py          compile, and list the sentences that matched nothing
  python3 compile_effects.py --test   only run the self-test
data/effects_manual.tsv holds hand-written scripts for one-off cards; a card listed there skips the patterns.
"""
import csv
import re
import sys
from collections import Counter
from pathlib import Path

DATA = Path(__file__).parent / "data"
TYPES = {"G": "grass", "R": "fire", "W": "water", "L": "lightning", "P": "psychic", "F": "fighting",
         "D": "darkness", "M": "metal", "N": "dragon", "C": "colorless"}
E = r"\{(\w)\}"  # an energy symbol such as {W}


class Unsupported(Exception):
    pass


def normalize(text):
    text = text.replace("’", "'").replace("“", '"').replace("”", '"').replace("−", "-").replace("–", "-")
    text = text.replace("—", "-").replace("Pokémon", "Pokemon").replace("{ex}", "ex")
    text = re.sub(r"from\s*-\s*and effects of\s*-\s*attacks", "from-and effects of-attacks", text)
    text = re.sub(r"from and effects of attacks", "from-and effects of-attacks", text)
    words = {v.capitalize(): k for k, v in TYPES.items() if k not in "NC"}
    text = re.sub(r"\b(" + "|".join(words) + r") (Energy|Pokemon)\b", lambda m: "{" + words[m.group(1)] + "} " + m.group(2), text)
    return " ".join(text.split())


def sentences(text):
    return [s for s in re.split(r"(?<=[.!])\s+(?![^(]*\))", normalize(text)) if s]


def num(word):
    return {"a": 1, "an": 1, "one": 1, "two": 2, "three": 3}.get(word, word if word.isdigit() else None) or int(word)


def t(letter):
    return TYPES[letter]


# --- Which Pokémon a phrase means ------------------------------------------------------------------

NAME = r"[A-Z][\w'.:\u2642\u2640-]*(?: [A-Z][\w'.:\u2642\u2640-]*)*(?: ex)?"
NAMES = NAME + r"(?:(?:,? or |,? and |, )" + NAME + r")*"
REF = (r"(?P<ref>this Pokemon|itself|it|the Defending Pokemon|your opponent's Active Pokemon|your Active Pokemon"
       r"|the Attacking Pokemon|that Pokemon|the (?P<atype>\{\w\}) Pokemon in the Active Spot"
       r"|(?:(?P<qty>1|a|an|each|all) (?:of )?)?(?P<side>your opponent's|your) (?P<act>Active )?(?P<bench>Benched )?"
       r"(?P<filt>(?:Basic |Stage 2 |Evolution |" + E.replace("(", "(?:") + r" )*)Pokemon(?P<ex> ex)?"
       r"|(?:(?P<nqty>1|each) of )?your (?P<names>" + NAMES + r")(?P<nactive> in the Active Spot)?"
       r"(?P<ndamaged> that has damage on it)?)")


def name_list(text):
    names = re.split(r",? or |,? and |, ", text)
    if any("Pokemon" in n or n in ("Active", "Benched", "Basic") for n in names):
        raise Unsupported(f"not names: {text}")
    return " ".join(f'"{n}"' for n in names)


def target(m, prefer_damaged=False):
    """The target for a REF match. `prefer_damaged`: choosing whom to heal only offers damaged Pokémon."""
    ref = m.group("ref")
    fixed = {"this Pokemon": "self", "itself": "self", "the Defending Pokemon": "defending",
             "your opponent's Active Pokemon": "opp-active", "your Active Pokemon": "my-active",
             "that Pokemon": "picked", "it": "picked"}
    if ref in fixed:
        return fixed[ref]
    if ref == "the Attacking Pokemon":
        return "attacker"
    if m.group("atype"):
        return f"(choose my active (type {t(m.group('atype')[1])}))"
    if m.group("names"):
        mode = "each" if m.group("nqty") == "each" else "choose"
        zone = "active" if m.group("nactive") else "all"
        damaged = " damaged" if m.group("ndamaged") or prefer_damaged else ""
        return f"({mode} my {zone} (name {name_list(m.group('names'))}){damaged})"
    side = "opp" if m.group("side") == "your opponent's" else "my"
    zone = "active" if m.group("act") else "bench" if m.group("bench") else "all"
    filters = []
    f = m.group("filt") or ""
    if "Basic" in f: filters.append("basic")
    if "Stage 2" in f: filters.append("stage2")
    if "Evolution" in f: filters.append("evolution")
    types = re.findall(E, f)
    if types: filters.append("(type " + " ".join(t(x) for x in types) + ")")
    if m.group("ex"): filters.append("ex")
    if prefer_damaged: filters.append("damaged")
    qty = m.group("qty")
    # "1 of your Pokemon" is one choice; a plural with no number ("your Benched Pokemon", Lt. Surge) is all of them
    mode = "choose" if qty in ("1", "a", "an") or m.group("act") else "each"
    return f"({mode} {side} {zone}{''.join(' ' + x for x in filters)})"


def ref_only(phrase, **kw):
    m = re.fullmatch(REF, phrase)
    if not m:
        raise Unsupported(f"unknown Pokemon: {phrase}")
    return target(m, **kw)


# --- Conditions ("If …,") ---------------------------------------------------------------------------

STATUS = r"(Asleep|Burned|Confused|Paralyzed|Poisoned)"
CONDITIONS = [
    (r"heads", lambda m, c: "heads"),
    (r"tails", lambda m, c: "tails"),
    (r"(?:both|all) of them are heads", lambda m, c: "all-heads"),
    (r"both of them are tails", lambda m, c: "all-tails"),
    (r"at least (\d+) of them are heads", lambda m, c: f"(>= heads {m.group(1)})"),
    (REF + r" has damage on it", lambda m, c: f"(has-damage {target(m)})"),
    (REF + r" is " + STATUS, lambda m, c: f"(has-status {target(m)} {m.group(len(m.groups())).lower()})"),
    (REF + r" is affected by (?:a Special Condition|any Special Conditions)", lambda m, c: f"(has-status {target(m)} any)"),
    (REF + r" has half of its maximum HP or less remaining", lambda m, c: f"(half-hp {target(m)})"),
    (REF + r" has a Pokemon Tool attached", lambda m, c: f"(has-tool {target(m)})"),
    (REF + r" is an? " + E + r" Pokemon", lambda m, c: f"(is-type {target(m)} {t(m.group(len(m.groups())))})"),
    (REF + r" is a Pokemon ex", lambda m, c: f"(is-ex {target(m)})"),
    (REF + r" is a Basic Pokemon", lambda m, c: f"(is-basic {target(m)})"),
    (REF + r" is an Evolution Pokemon", lambda m, c: f"(is-evolution {target(m)})"),
    (REF + r" has at least (\d+) extra " + E + r" Energy attached",
     lambda m, c: f"(extra-energy {target(m)} {m.group(len(m.groups()) - 1)} {t(m.group(len(m.groups())))})"),
    (REF + r" has at least (\d+) extra Energy attached",
     lambda m, c: f"(extra-energy {target(m)} {m.group(len(m.groups()))} " + " ".join(TYPES.values()) + ")"),
    (REF + r" has any " + E + r" Energy attached", lambda m, c: f"(energy-at-least {target(m)} 1 {t(m.group(len(m.groups())))})"),
    (REF + r" has at least (\d+) " + E + r" Energy attached",
     lambda m, c: f"(energy-at-least {target(m)} {m.group(len(m.groups()) - 1)} {t(m.group(len(m.groups())))})"),
    (REF + r" has at least (\d+) Energy attached", lambda m, c: f"(energy-at-least {target(m)} {m.group(len(m.groups()))})"),
    (r"this Pokemon moved from your Bench to the Active Spot this turn", lambda m, c: "(moved-to-active self)"),
    (r"this Pokemon evolved during this turn", lambda m, c: "(evolved-this-turn self)"),
    (r"you played a Supporter card from your hand during this turn", lambda m, c: "(played-supporter)"),
    (r"you healed any damage in this way", lambda m, c: "(healed)"),
    # the specific forms first: the plain one would read "Arceus or Arceus ex" or "another Beldum" as one name
    (r"you have another (\w[\w' ]*?) in play", lambda m, c: f'(exists my all (name "{m.group(1)}") not-self)'),
    (r"you have (\w[\w' ]*?) or (\w[\w' ]*?) in play",
     lambda m, c: f'(exists my all (name "{m.group(1)}" "{m.group(2)}"))'),
    (r"you have (\w[\w' ]*?) in play", lambda m, c: f'(exists my all (name "{m.group(1)}"))'),
    (r"your Pokemon in play have (\d+) or more different types of Energy attached",
     lambda m, c: f"(>= (energy-types my all) {m.group(1)})"),
    (r"your Pokemon in the Active Spot has the (.+) attack", lambda m, c: f'(has-attack my-active "{m.group(1)}")'),
    (r"a Stadium is in play", lambda m, c: "(stadium-in-play)"),
    (r"([A-Z][\w' .:-]*?) is in your discard pile", lambda m, c: f'(>= (discard-count my (name "{m.group(1)}")) 1)'),
    (r"your opponent has (\d+) or more cards in their hand", lambda m, c: f"(>= (hand-size opp) {m.group(1)})"),
    (r"any of your (?:" + E + r" )?Pokemon were Knocked Out by damage from an attack during your opponent's last turn",
     lambda m, c: f"(ko-last-turn{' ' + t(m.group(1)) if m.group(1) else ''})"),
    (r"1 of your Pokemon used (.+) during your last turn", lambda m, c: f'(used-last-turn "{m.group(1)}")'),
    (REF + r" has any Energy attached", lambda m, c: f"(energy-at-least {target(m)} 1)"),
    (r"the amount of Energy attached to both Active Pokemon is (\d+) or more", lambda m, c: f"(>= (energy (each both active)) {m.group(1)})"),
    (REF + r" has no damage on it", lambda m, c: f"(not (has-damage {target(m)}))"),
    (r"your opponent's Active Pokemon has an Ability", lambda m, c: "(exists opp active has-ability)"),
    (r"your opponent's Active Pokemon has (more|less) remaining HP than this Pokemon",
     lambda m, c: f"(not ({'<=' if m.group(1) == 'more' else '>='} (hp-left opp-active) (hp-left self)))"),
    (r"this Pokemon has more Energy attached than your opponent's Active Pokemon", lambda m, c: "(not (<= (energy self) (energy opp-active)))"),
    (r"this Pokemon and your opponent's Active Pokemon have the same amount of Energy attached", lambda m, c: "(= (energy self) (energy opp-active))"),
    (r"this Pokemon and your opponent's Active Pokemon have (\d+) or more of the same type of Energy attached",
     lambda m, c: f"(shared-energy self opp-active {m.group(1)})"),
    (r"(you|your opponent) ha(?:ve|s) exactly (\d+), (\d+), or (\d+) cards in (?:your|their) hand",
     lambda m, c: (lambda s: f"(or (= (hand-size {s}) {m.group(2)}) (or (= (hand-size {s}) {m.group(3)}) (= (hand-size {s}) {m.group(4)})))")
     ("my" if m.group(1) == "you" else "opp")),
    (r"you have the same number of cards in your hand as your opponent", lambda m, c: "(= (hand-size my) (hand-size opp))"),
    (r"(\w[\w' ]*?) is on your Bench", lambda m, c: f'(exists my bench (name "{m.group(1)}"))'),
    (r"your opponent's Active Pokemon is an? " + E + r" or " + E + r" Pokemon", lambda m, c: f"(is-type opp-active {t(m.group(1))} {t(m.group(2))})"),
    (r"your opponent's Active Pokemon has \"([^\"]+)\" in its name", lambda m, c: f'(exists opp active (name-has "{m.group(1)}"))'),
    (r"your opponent has gotten exactly (\d+) points?", lambda m, c: f"(= (points opp) {m.group(1)})"),
    (r"you haven't gotten any points", lambda m, c: "(= (points my) 0)"),
    (r"your opponent hasn't gotten any points", lambda m, c: "(= (points opp) 0)"),
    (r"your opponent has gotten at least (\d+) points?", lambda m, c: f"(>= (points opp) {m.group(1)})"),
    (r"you have fewer Pokemon in play than your opponent", lambda m, c: "(not (>= (count my all) (count opp all)))"),
    (r"you have any Stage 2 Pokemon on your Bench", lambda m, c: "(exists my bench stage2)"),
    (r"you have (\d+) or more " + E + r" Energy in play", lambda m, c: f"(>= (energy (each my all) {t(m.group(2))}) {m.group(1)})"),
    (r"this Pokemon has (\d+) or more different types of Energy attached", lambda m, c: f"(>= (types-on self) {m.group(1)})"),
    (r"this Pokemon evolved from (\w+) during this turn", lambda m, c: f'(evolved-from self "{m.group(1)}")'),
    (r"this Pokemon didn't move from the Bench to the Active Spot this turn", lambda m, c: "(not (moved-to-active self))"),
    (r"any of your Benched Pokemon have damage on them", lambda m, c: "(exists my bench damaged)"),
    (r"your opponent has any " + E + r" Pokemon in play", lambda m, c: f"(exists opp all (type {t(m.group(1))}))"),
    (r"(\d+) of them (?:is|are) heads", lambda m, c: f"(= heads {m.group(1)})"),
    (r"all of them are tails", lambda m, c: "all-tails"),
    (r"at least (\d+) of them is heads", lambda m, c: f"(>= heads {m.group(1)})"),
    (r"your opponent's Pokemon is Knocked Out by damage from this attack", lambda m, c: "(at-zero defending)"),
    (r"your opponent's Active Pokemon is ([A-Z][\w' ]*)", lambda m, c: f'(exists opp active (name "{m.group(1)}"))'),
    (REF + r" has full HP", lambda m, c: f"(not (has-damage {target(m)}))"),
    (r"this Pokemon's remaining HP is (\d+) or less", lambda m, c: f"(<= (hp-left self) {m.group(1)})"),
    (r"you don't have (\w+), (\w+), and (\w+) on your Bench",
     lambda m, c: f'(not (and (exists my bench (name "{m.group(1)}")) (and (exists my bench (name "{m.group(2)}")) '
                  f'(exists my bench (name "{m.group(3)}")))))'),
    (r"this Pokemon was damaged by an attack during your opponent's last turn while it was in the Active Spot",
     lambda m, c: "(damaged-last-turn self)"),
]


def condition(text, ctx):
    for rx, fn in CONDITIONS:
        m = re.fullmatch(rx, text)
        if m:
            return fn(m, ctx)
    raise Unsupported(f"condition: {text}")


# --- Clauses: one thing an effect does ---------------------------------------------------------------
# Each pattern gives (phase, statement). Phase "pre" runs before the attack's damage ((hit)), "post" after.

def status_list(text):
    return " ".join(s.lower() for s in re.split(r", | and ", text))


def heal_amount(m):
    return "999" if m.group("all") else m.group("n")


def names_opt(who):
    return "" if who == "Pokemon" else f" :name {name_list(who)}"


def move_energy(m):
    """(move-energy FROM TO TYPES N): the first REF has its groups renamed with an f_ prefix."""
    class From:
        def group(self, g):
            return m.group("f_" + g) if isinstance(g, str) else m.group(g)
    types = " ".join(t(x) for x in re.findall(E, m.group(2))) or "any"
    count = "all" if m.group(1) == "all" else num(m.group(1))
    return f"(move-energy {target(From())} {target(m)} {types} {count})"


def attack_index(ctx, name):
    names = [a[0] for a in ctx["attacks"]]
    if name not in names:
        raise Unsupported(f"unknown attack {name}")
    return names.index(name)


def only_attack(ctx, name):
    """"This Pokemon's X attack does +N": damage-dealt has no per-attack filter, so only a card whose one attack is X."""
    if [a[0] for a in ctx["attacks"]] != [name]:
        raise Unsupported(f"bonus for one of several attacks: {name}")
    return ""


def timed(duration):
    return {"During your opponent's next turn": "opp-next-turn", "during your opponent's next turn": "opp-next-turn",
            "During your next turn": "my-next-turn", "during your next turn": "my-next-turn",
            "During this turn": "this-turn"}[duration]


TRAINER_KINDS = {"Supporter": "supporter", "Item": "item", "Pokemon Tool": "tool", "Stadium": "stadium"}

OPP_NEXT = r"(?P<d>[Dd]uring your opponent's next turn|[Dd]uring your next turn|During this turn)"

CLAUSES = [
    # Coins
    (r"flip a coin", lambda m, c: ("pre", "(flip 1)")),
    (r"flip (\d+) coins", lambda m, c: ("pre", f"(flip {m.group(1)})")),
    (r"flip a coin until you get tails", lambda m, c: ("pre", "(flip-until-tails)")),
    (r"flip a coin for each Energy attached to this Pokemon", lambda m, c: ("pre", "(flip (energy self))")),
    (r"flip a coin for each " + E + r" Energy attached to this Pokemon", lambda m, c: ("pre", f"(flip (energy self {t(m.group(1))}))")),
    (r"flip a coin for each Pokemon you have in play", lambda m, c: ("pre", "(flip (count my all))")),
    (r"flip a coin for each of your Benched Pokemon", lambda m, c: ("pre", "(flip (count my bench))")),
    (r"flip a coin for each of your opponent's Benched Pokemon", lambda m, c: ("pre", "(flip (count opp bench))")),
    # The attack's own damage
    (r"[Tt]his attack does (\d+) (more )?damage for each (Evolution Pokemon on your Bench|Benched Pokemon \(both yours and your opponent's\)|"
     r"card in your hand|Pokemon Tool attached to all of your Pokemon|of your opponent's Pokemon in play that has an Ability|"
     r"point you have gotten|point your opponent has gotten|time your Pokemon have been Knocked Out during this game|"
     r"Trainer card in your opponent's deck|type of Energy attached to this Pokemon|of your Benched (?:" + NAMES + r"))",
     lambda m, c: ("pre", f"({'bonus' if m.group(2) else 'set-damage'} (* {count_of(m.group(3))} {m.group(1)}))")),
    (r"[Tt]his attack does damage to your opponent's Active Pokemon equal to (this Pokemon's remaining HP|the damage this Pokemon has on it)",
     lambda m, c: ("pre", f"(set-damage {'(hp-left self)' if 'remaining' in m.group(1) else '(damage-on self)'})")),
    (r"[Tt]his attack's damage is reduced by the amount of damage this Pokemon has on it", lambda m, c: ("pre", "(bonus (* (damage-on self) -1))")),
    (r"this attack does -(\d+) damage", lambda m, c: ("pre", f"(bonus -{m.group(1)})")),
    (r"[Tt]his attack's damage isn't affected by Weakness or by any effects on your opponent's Active Pokemon",
     lambda m, c: ("hitopt", ":no-weakness :ignore-effects")),
    (r"[Tt]his attack's damage isn't affected by any effects on your opponent's Active Pokemon", lambda m, c: ("hitopt", ":ignore-effects")),
    (r"[Tt]his attack's damage isn't affected by Weakness", lambda m, c: ("hitopt", ":no-weakness")),
    (r"this attack does (\d+) more damage", lambda m, c: ("pre", f"(bonus {m.group(1)})")),
    (r"this attack does (\d+) more damage for each heads", lambda m, c: ("pre", f"(bonus (* heads {m.group(1)}))")),
    (r"this attack does (\d+) damage for each heads", lambda m, c: ("pre", f"(set-damage (* heads {m.group(1)}))")),
    (r"this attack does (\d+) (more )?damage for each Energy attached to " + REF,
     lambda m, c: ("pre", f"({'bonus' if m.group(2) else 'set-damage'} (* (energy {target(m)}) {m.group(1)}))")),
    (r"this attack does (\d+) (more )?damage for each " + E + r" Energy attached to " + REF,
     lambda m, c: ("pre", f"({'bonus' if m.group(2) else 'set-damage'} (* (energy {target(m)} {t(m.group(3))}) {m.group(1)}))")),
    (r"this attack does (\d+) (more )?damage for each of your (opponent's )?Benched (?:" + E + r" )?Pokemon",
     lambda m, c: ("pre", f"({'bonus' if m.group(2) else 'set-damage'} (* (count {'opp' if m.group(3) else 'my'} bench"
                          f"{' (type ' + t(m.group(4)) + ')' if m.group(4) else ''}) {m.group(1)}))")),
    (r"this attack does (\d+) (more )?damage for each Energy in your opponent's Active Pokemon's Retreat Cost",
     lambda m, c: ("pre", f"({'bonus' if m.group(2) else 'set-damage'} (* (retreat-cost opp-active) {m.group(1)}))")),
    (r"this attack does more damage equal to the damage this Pokemon has on it", lambda m, c: ("pre", "(bonus (damage-on self))")),
    (r"this attack does (\d+) damage for each card in your opponent's hand",
     lambda m, c: ("pre", f"(set-damage (* (hand-size opp) {m.group(1)}))")),
    (r"this attack does nothing", lambda m, c: ("pre", "(cancel)")),
    (r"that attack doesn't happen", lambda m, c: ("pre", "")),  # follows an attack-flip effect, which handles it
    # Damage elsewhere
    (r"this Pokemon also does (\d+) damage to itself", lambda m, c: ("post", f"(damage self {m.group(1)})")),
    (r"this attack (?:also )?does (\d+) damage to " + REF, lambda m, c: ("post", f"(damage {target(m)} {m.group(1)})")),
    (r"do (\d+) damage to " + REF, lambda m, c: ("post", f"(damage {target(m)} {m.group(1)})")),
    # Healing and Special Conditions
    (r"heal (?:(?P<n>\d+) damage|(?P<all>all damage)) from " + REF,
     lambda m, c: ("post", f"(heal {target(m, prefer_damaged=True)} {heal_amount(m)})")),
    (REF + r" is now (?:also )?" + r"((?:Asleep|Burned|Confused|Paralyzed|Poisoned)(?:(?:, | and )(?:Asleep|Burned|Confused|Paralyzed|Poisoned))*)",
     lambda m, c: ("post", f"(status {target(m)} {status_list(m.group(len(m.groups())))})")),
    (r"(?:you may )?make your opponent's Active Pokemon " + STATUS, lambda m, c: ("post", f"(status opp-active {m.group(1).lower()})")),
    (r"heal (\d+) damage from " + REF + r", and it recovers from all Special Conditions",
     lambda m, c: ("post", f"(pick {target(m)} (seq (heal picked {m.group(1)}) (cure picked)))")),
    (r"heal (\d+) damage from " + REF + r", and it recovers from being ((?:Asleep|Burned|Confused|Paralyzed|Poisoned)(?:,? (?:and )?(?:Asleep|Burned|Confused|Paralyzed|Poisoned))*)",
     lambda m, c: ("post", f"(pick {target(m)} (seq (heal picked {m.group(1)}) (cure picked {status_list(m.group(len(m.groups())).replace(', and ', ', '))})))")),
    (REF + r" recovers from all Special Conditions", lambda m, c: ("post", f"(cure {target(m)})")),
    (r"remove a random Special Condition from " + REF, lambda m, c: ("post", f"(cure-random {target(m)})")),
    (r"this attack does (\d+) more damage for each Special Condition affecting " + REF,
     lambda m, c: ("pre", f"(bonus (* (status-count {target(m)}) {m.group(1)}))")),
    (r"heal (\d+) damage from each of your Pokemon that has any " + E + r" Energy attached",
     lambda m, c: ("post", f"(heal (each my all (has-energy 1 {t(m.group(2))}) damaged) {m.group(1)})")),
    (r"heal (\d+) damage from 1 of your (?:" + E + r" )?Pokemon that has (\d+) or more " + E + r" Energy attached",
     lambda m, c: ("post", f"(heal (choose my all{' (type ' + t(m.group(2)) + ')' if m.group(2) else ''}"
                           f" (has-energy {m.group(3)} {t(m.group(4))}) damaged) {m.group(1)})")),
    (r"move (an?|all|\d+) ((?:" + E.replace("(", "(?:") + r"(?:, |, or | or )?)*)\s?Energy from " + REF.replace("?P<", "?P<f_") + r" to " + REF,
     lambda m, c: ("post", move_energy(m))),
    # Energy
    (r"discard (an?|\d+|all) (?:" + E + r" )?Energy from " + REF,
     lambda m, c: ("post", f"(discard-energy {target(m)} {t(m.group(2)) if m.group(2) else 'any'} "
                           f"{'all' if m.group(1) == 'all' else num(m.group(1))})")),
    (r"discard (an?|\d+) random Energy from " + REF, lambda m, c: ("post", f"(discard-random-energy {target(m)} {num(m.group(1))})")),
    (r"discard a random Energy from both Active Pokemon",
     lambda m, c: ("post", "(seq (discard-random-energy my-active 1) (discard-random-energy opp-active 1))")),
    (r"take (an?|\d+) " + E + r" Energy from your Energy Zone and attach it to " + REF,
     lambda m, c: ("post", f"(attach-energy {target(m)} {t(m.group(2))} {num(m.group(1))})")),
    (r"take an amount of " + E + r" Energy from your Energy Zone equal to the number of heads and attach it to "
     r"your Benched (?:" + E + r" )?Pokemon in any way you like",
     lambda m, c: ("post", f"(repeat heads (attach-energy (choose my bench{' (type ' + t(m.group(2)) + ')' if m.group(2) else ''}) {t(m.group(1))} 1))")),
    (r"take a " + E + r" Energy from your Energy Zone and attach it to it", lambda m, c: ("post", f"(attach-energy picked {t(m.group(1))} 1)")),
    # Timed effects
    (OPP_NEXT + r", this Pokemon takes ([-+]\d+) damage from attacks",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (damage-taken {m.group(2).lstrip('+')}))")),
    (OPP_NEXT + r", this Pokemon's (.+) attack does \+(\d+) damage",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (damage-dealt {m.group(3)}{only_attack(c, m.group(2))}))")),
    (OPP_NEXT + r", (?P<who>this|the Defending) Pokemon can't (?P<what>attack|retreat)",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} {'self' if m.group('who') == 'this' else 'defending'} (cant-{m.group('what')}))")),
    (r"the Defending Pokemon can't attack during your opponent's next turn", lambda m, c: ("post", "(effect opp-next-turn defending (cant-attack))")),
    (OPP_NEXT + r", this Pokemon can't use (.+)",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (cant-use-attack {attack_index(c, m.group(2))}))")),
    (OPP_NEXT + r", prevent all damage from-and effects of-attacks done to this Pokemon",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (prevent-all))")),
    (OPP_NEXT + r", prevent all damage done to this Pokemon by attacks",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (prevent-damage))")),
    (OPP_NEXT + r", prevent all damage done to this Pokemon by attacks if that damage is (\d+) or less",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (prevent-damage {m.group(2)}))")),
    (OPP_NEXT + r", attacks used by the Defending Pokemon do -(\d+) damage",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} defending (damage-dealt -{m.group(2)}))")),
    (OPP_NEXT + r", attacks used by the Defending Pokemon cost (\d+) \{C\} more",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} defending (attack-cost {m.group(2)}))")),
    (OPP_NEXT + r", if this Pokemon is damaged by an attack, do (\d+) damage to the Attacking Pokemon",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (reflect {m.group(2)}))")),
    (OPP_NEXT + r", if the Defending Pokemon tries to use an attack, your opponent flips a coin",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} defending (attack-flip))")),
    (OPP_NEXT + r", they can't play any Item cards from their hand", lambda m, c: ("post", f"(effect {timed(m.group('d'))} opp (cant-item))")),
    (r"your opponent can't use any Supporter cards from their hand during their next turn",
     lambda m, c: ("post", "(effect opp-next-turn opp (cant-supporter))")),
    (OPP_NEXT + r", they can't take any Energy from their Energy Zone to attach to their Active Pokemon",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} opp (cant-attach-to-active))")),
    (r"until the end of your opponent's next turn, your opponent's Active Pokemon's Retreat Cost is (\d+) more",
     lambda m, c: ("post", f"(effect opp-next-turn opp-active (retreat-cost {m.group(1)}))")),
    (r"During this turn, the Retreat Cost of your Active Pokemon is (\d+) less",
     lambda m, c: ("post", f"(effect this-turn me (retreat-cost -{m.group(1)} :scope my-active))")),
    (r"(?P<d>During this turn|During your next turn), attacks used by your (?:" + E + r" )?(Pokemon|" + NAMES + r") do \+(\d+) damage to your opponent's Active Pokemon( ex)?",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} me (damage-dealt {m.group(4)} :scope my-all{type_opt(m.group(2))}"
                           f"{names_opt(m.group(3))}{' :vs-ex' if m.group(5) else ''}))")),
    (r"During this turn, attacks used by your (" + NAMES + r") cost (\d+) less \{C\} Energy",
     lambda m, c: ("post", f"(effect this-turn me (attack-cost -{m.group(2)} :scope my-all{names_opt(m.group(1))}))")),
    (OPP_NEXT + r", all of your Ultra Beasts take -(\d+) damage from attacks from your opponent's Pokemon",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} me (damage-taken -{m.group(2)} :scope my-all :trait ultra-beast))")),
    (OPP_NEXT + r", all of your (?:(?P<ty>" + E.replace("(", "(?:") + r") )?(?P<who>Pokemon|" + NAMES + r") take -(?P<n>\d+) damage from attacks(?: from your opponent's Pokemon(?P<fex> ex)?)?",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} me (damage-taken -{m.group('n')} :scope my-all"
                           f"{type_opt(m.group('ty')[1]) if m.group('ty') else ''}{names_opt(m.group('who'))}"
                           f"{' :from-ex' if m.group('fex') else ''}))")),
    # Switching
    (r"switch this Pokemon with (?:1|one) of your Benched Pokemon", lambda m, c: ("post", "(switch-in (choose my bench))")),
    (r"switch out your opponent's Active Pokemon to the Bench", lambda m, c: ("post", "(switch-in (owner-chooses opp bench))")),
    (r"\(Your opponent chooses the new Active Pokemon\.\)", lambda m, c: ("post", "")),
    (r"switch out your Active Pokemon to the Bench", lambda m, c: ("post", "(switch-in (choose my bench))")),
    (r"switch out your opponent's Active Basic Pokemon to the Bench",
     lambda m, c: ("post", "(if (is-basic opp-active) (switch-in (owner-chooses opp bench)))")),
    (r"switch your Active Pokemon that has damage on it with (?:1|one) of your Benched Pokemon",
     lambda m, c: ("post", "(if (has-damage my-active) (switch-in (choose my bench)))")),
    (r"switch your Active (Basic|Stage 1|Stage 2) Pokemon with (?:1|one) of your Benched Pokemon",
     lambda m, c: ("post", f"(if (exists my active {card_kind(m.group(1) + ' ')}) (switch-in (choose my bench)))")),
    (r"switch in (?:1|one) of your opponent's Benched (Basic )?Pokemon( that has damage on it)? to the Active Spot",
     lambda m, c: ("post", f"(switch-in (choose opp bench{' basic' if m.group(1) else ''}{' damaged' if m.group(2) else ''}))")),
    (r"your turn ends", lambda m, c: ("post", "(end-turn)")),
    (r"discard this card", lambda m, c: ("post", "(discard-this)")),  # a Tool, from the Pokémon it's on
    (r"discard this Pokemon", lambda m, c: ("post", "(discard-pokemon self)")),
    (r"switch your Active Ultra Beast with 1 of your Benched Ultra Beasts",
     lambda m, c: ("post", "(if (exists my active (trait ultra-beast)) (switch-in (choose my bench (trait ultra-beast))))")),
    (r"Attach (\d+) random Energy from your discard pile to 1 of your Ultra Beasts",
     lambda m, c: ("post", f"(attach-from-discard (choose my all (trait ultra-beast)) random {m.group(1)})")),
    (r"Attach (\d+) different types of Energy from your discard pile to your Ancient Pokemon in any way you like",
     lambda m, c: ("post", f"(attach-from-discard (choose my all (trait ancient)) different {m.group(1)})")),
    (r"Shuffle 1 of your Future Pokemon in play into your deck", lambda m, c: ("post", "(to-deck (choose my all (trait future)))")),
    (r"[Tt]his attack does (\d+) damage to 1 of your opponent's Pokemon that have damage on them",
     lambda m, c: ("post", f"(damage (choose opp all damaged) {m.group(1)})")),
    (r"this attack also does (\d+) damage to 2 of your opponent's Benched Pokemon",
     lambda m, c: ("post", f"(pick (choose opp bench) (seq (damage picked {m.group(1)}) (damage (choose opp bench not-picked) {m.group(1)})))")),
    (r"[Tt]his attack also does (\d+) damage to 1 of your opponent's Benched Pokemon that has damage on it",
     lambda m, c: ("post", f"(damage (choose opp bench damaged) {m.group(1)})")),
    (r"[Tt]his attack also does (\d+) damage to each of your opponent's Benched Pokemon that has (damage on it|any Energy attached)",
     lambda m, c: ("post", f"(damage (each opp bench {'damaged' if 'damage' in m.group(2) else '(has-energy 1)'}) {m.group(1)})")),
    (r"[Tt]his attack also does (\d+) damage for each heads to each of your opponent's Benched Pokemon",
     lambda m, c: ("post", f"(damage (each opp bench) (* heads {m.group(1)}))")),
    (r"[Tt]his attack also does (\d+) damage to each of your opponent's Benched Pokemon",
     lambda m, c: ("post", f"(damage (each opp bench) {m.group(1)})")),
    (r"This Pokemon also does (\d+) damage to itself and (\d+) damage to all Benched Pokemon \(both yours and your opponent's\)",
     lambda m, c: ("post", f"(seq (damage self {m.group(1)}) (damage (each both bench) {m.group(2)}))")),
    (r"this Pokemon also does (\d+) damage to itself", lambda m, c: ("post", f"(damage self {m.group(1)})")),
    (r"[Tt]his attack does (\d+) damage to the new Active Pokemon", lambda m, c: ("post", f"(damage opp-active {m.group(1)})")),
    (r"Heal from this Pokemon the same amount of damage you did to your opponent's Active Pokemon", lambda m, c: ("post", "(heal self (dealt))")),
    (r"Halve your opponent's Active Pokemon's remaining HP, rounded down", lambda m, c: ("post", "(halve-hp opp-active)")),
    (r"your opponent's Active Pokemon's remaining HP is now (\d+)", lambda m, c: ("post", f"(set-hp-left opp-active {m.group(1)})")),
    (r"Knock Out your opponent's Active Pokemon", lambda m, c: ("post", "(knock-out opp-active)")),
    (r"discard your opponent's Active Pokemon", lambda m, c: ("post", "(discard-pokemon opp-active)")),
    (r"put your opponent's Active Pokemon into their hand", lambda m, c: ("post", "(to-hand opp-active)")),
    (r"your opponent shuffles their Active Pokemon back into their deck", lambda m, c: ("post", "(to-deck opp-active)")),
    (r"Shuffle this Pokemon into your deck", lambda m, c: ("post", "(to-deck self)")),
    (r"[Mm]ake your opponent's Active Pokemon Poisoned and Burned", lambda m, c: ("post", "(status opp-active poisoned burned)")),
    (r"Both Active Pokemon are now (Asleep|Confused|Poisoned|Burned|Paralyzed)", lambda m, c: ("post", f"(status (each both active) {m.group(1).lower()})")),
    (r"Your opponent's Active Pokemon is now Poisoned or Confused", lambda m, c: ("post", "(one-of (status opp-active poisoned) (status opp-active confused))")),
    (r"1 Special Condition from among ((?:\w+, )+(?:and )?\w+) is chosen at random, and your opponent's Active Pokemon is now affected by that Special Condition",
     lambda m, c: ("post", f"(status-random opp-active {status_list(m.group(1).replace(', and ', ', '))})")),
    (r"Any Special Conditions already affecting that Pokemon will not be chosen", lambda m, c: ("post", "")),
    (r"[Uu]se this Ability", lambda m, c: ("post", "")),
    (r"If you use this Ability, your turn ends", lambda m, c: ("post", "(end-turn)")),
    (OPP_NEXT + r", this Pokemon (?:takes -(\d+) damage from attacks and )?has no Weakness",
     lambda m, c: ("post", f"(seq (effect {timed(m.group('d'))} self (no-weakness))" +
                           (f" (effect {timed(m.group('d'))} self (damage-taken -{m.group(2)}))" if m.group(2) else "") + ")")),
    (OPP_NEXT + r", this Pokemon takes -(\d+) damage from attacks from your opponent's Pokemon ex",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (damage-taken -{m.group(2)} :from-ex))")),
    (OPP_NEXT + r", they can't play any Trainer cards from their hand", lambda m, c: ("post", f"(effect {timed(m.group('d'))} opp (cant-trainer))")),
    (OPP_NEXT + r", they can't play any Pokemon from their hand to evolve their Pokemon", lambda m, c: ("post", f"(effect {timed(m.group('d'))} opp (cant-evolve))")),
    (OPP_NEXT + r", that Pokemon can't retreat", lambda m, c: ("post", f"(effect {timed(m.group('d'))} defending (cant-retreat))")),
    (r"it can't attack during your opponent's next turn", lambda m, c: ("post", "(effect opp-next-turn defending (cant-attack))")),
    (OPP_NEXT + r", attacks used by the Defending Pokemon cost (\d+) \{C\} more, and its Retreat Cost is (\d+) \{C\} more",
     lambda m, c: ("post", f"(seq (effect {timed(m.group('d'))} defending (attack-cost {m.group(2)})) (effect {timed(m.group('d'))} defending (retreat-cost {m.group(3)})))")),
    (OPP_NEXT + r", the Defending Pokemon takes \+(\d+) damage from attacks", lambda m, c: ("post", f"(effect {timed(m.group('d'))} defending (damage-taken {m.group(2)}))")),
    (r"[Pp]revent all damage from-and effects of-attacks from your opponent's Pokemon done to this Pokemon until the end of your opponent's next turn",
     lambda m, c: ("post", "(effect opp-next-turn self (prevent-all))")),
    (r"Prevent all damage done to this Pokemon by attacks from Basic Pokemon during your opponent's next turn",
     lambda m, c: ("post", "(effect opp-next-turn self (prevent-damage :from-basic))")),
    (r"Until this Pokemon leaves the Active Spot, this Pokemon's (.+) attack does \+(\d+) damage",
     lambda m, c: ("post", f"(effect while-active self (damage-dealt {m.group(2)}{only_attack(c, m.group(1))}))")),
    (r"If the Defending Pokemon tries to use an attack, your opponent flips a coin until it leaves the Active Spot",
     lambda m, c: ("post", "(effect while-active defending (attack-flip))")),
    # Energy
    (r"Take a " + E + r" and a " + E + r" Energy from your Energy Zone and attach them to this Pokemon",
     lambda m, c: ("post", f"(seq (attach-energy self {t(m.group(1))} 1) (attach-energy self {t(m.group(2))} 1))")),
    (r"Take a " + E + r", " + E + r", and " + E + r" Energy from your Energy Zone and attach them to your Benched Basic Pokemon in any way you like",
     lambda m, c: ("post", "(seq " + " ".join(f"(attach-energy (choose my bench basic) {t(x)} 1)" for x in m.groups()) + ")")),
    (r"Take (\d+) " + E + r" Energy from your Energy Zone and attach it to your " + E + r" Pokemon in any way you like",
     lambda m, c: ("post", f"(repeat {m.group(1)} (attach-energy (choose my all (type {t(m.group(3))})) {t(m.group(2))} 1))")),
    (r"Take a " + E + r" Energy from your Energy Zone and attach it to (\w+) or (\w+)",
     lambda m, c: ("post", f'(attach-energy (choose my all (name "{m.group(2)}" "{m.group(3)}")) {t(m.group(1))} 1)')),
    (r"Take a random Energy from among .+ Energy from your Energy Zone and attach it to 1 of your Benched Pokemon",
     lambda m, c: ("post", "(attach-random-type (choose my bench))")),
    (r"produce a " + E + r" Energy from your Energy Zone and attach it to this Pokemon", lambda m, c: ("post", f"(attach-energy self {t(m.group(1))} 1)")),
    (r"Discard a " + E + r" and a " + E + r" Energy from this Pokemon",
     lambda m, c: ("post", f"(seq (discard-energy self {t(m.group(1))} 1) (discard-energy self {t(m.group(2))} 1))")),
    (r"Discard a " + E + r", " + E + r", and " + E + r" Energy from this Pokemon",
     lambda m, c: ("post", "(seq " + " ".join(f"(discard-energy self {t(x)} 1)" for x in m.groups()) + ")")),
    (r"Discard all Energy attached to this Pokemon", lambda m, c: ("post", "(discard-energy self any all)")),
    (r"Discard an Energy from this Pokemon, and this attack also does (\d+) damage to each of your opponent's Benched Pokemon",
     lambda m, c: ("post", f"(seq (discard-energy self any 1) (damage (each opp bench) {m.group(1)}))")),
    (r"Discard (\d+) " + E + r" Energy from this Pokemon, and this attack does (\d+) damage to each of your opponent's Pokemon",
     lambda m, c: ("post", f"(seq (discard-energy self {t(m.group(2))} {m.group(1)}) (damage (each opp all) {m.group(3)}))")),
    (r"Discard all " + E + r" and " + E + r" Energy from this Pokemon, and this attack does (\d+) damage for each Energy you discarded in this way",
     lambda m, c: ("pre", f"(seq (set-damage (* (energy self {t(m.group(1))} {t(m.group(2))}) {m.group(3)})) "
                          f"(discard-energy self {t(m.group(1))} all) (discard-energy self {t(m.group(2))} all))")),
    (r"Discard 2 random Energy from among the Energy attached to all (of your Pokemon|Pokemon \(both yours and your opponent's\))",
     lambda m, c: ("post", f"(discard-energy-among (each {'my' if 'your Pokemon' in m.group(1) else 'both'} all) 2)")),
    (r"Move a random Energy from your opponent's Active Pokemon to this Pokemon", lambda m, c: ("post", "(move-random-energy opp-active self 1)")),
    (r"Move (\d+) random Energy from this Pokemon to 1 of your Benched Pokemon", lambda m, c: ("post", f"(move-random-energy self (choose my bench) {m.group(1)})")),
    (r"Change the type of a random Energy attached to your opponent's Active Pokemon to 1 of the following at random: .+",
     lambda m, c: ("post", "(change-energy-type opp-active)")),
    (r"Change the type of the next Energy that will be generated for your opponent to 1 of the following at random: .+",
     lambda m, c: ("post", "(change-next-energy opp)")),
    (r"Choose 2 of your Benched Pokemon and attach a " + E + r" Energy from your Energy Zone to each",
     lambda m, c: ("post", f"(pick (choose my bench) (seq (attach-energy picked {t(m.group(1))} 1) (attach-energy (choose my bench not-picked) {t(m.group(1))} 1)))")),
    # hands, decks, copying
    (r"Discard (\d+) cards from your hand or else nothing",
     lambda m, c: ("pre", f"(if (>= (hand-size my) {m.group(1)}) (discard-from-hand {m.group(1)}) (cancel))")),
    (r"Draw cards until you have the same number of cards in your hand as your opponent", lambda m, c: ("post", "(draw-until (hand-size opp))")),
    (r"Draw a card for each (\w+) you have in play", lambda m, c: ("post", f'(repeat (count my all (name "{m.group(1)}")) (draw 1))')),
    (r"Flip a coin for each (\w+) and (\w+) you have in play", lambda m, c: ("pre", f'(flip (count my all (name "{m.group(1)}" "{m.group(2)}")))')),
    (r"[Ll]ook at a random card from your opponent's hand and shuffle it into their deck", lambda m, c: ("post", "(hand-to-deck-random opp 1)")),
    (r"Your opponent shuffles heads random cards from their hand into their deck", lambda m, c: ("post", "(hand-to-deck-random opp heads)")),
    (r"Look at a random card from your opponent's hand", lambda m, c: ("post", "(nothing)")),
    (r"Discard a random (Item|Pokemon Tool) card from your opponent's hand",
     lambda m, c: ("post", f"(discard-hand-random-of opp 1 {TRAINER_KINDS[m.group(1)]})")),
    (r"[Yy]our opponent (discards|shuffles into their deck) a(?: (Supporter))? card you choose from their hand",
     lambda m, c: ("post", f"(opp-hand-{'discard' if m.group(1) == 'discards' else 'to-deck'}{' supporter' if m.group(2) else ''})")),
    (r"[Hh]ave your opponent shuffle their hand into their deck and draw a card for each remaining point they need to win",
     lambda m, c: ("post", "(shuffle-hand-draw opp (points-needed opp))")),
    (r"[Ll]ook at the top (\d+) cards of your deck and put all Item cards you find there into your hand",
     lambda m, c: ("post", f"(dig {m.group(1)} item)")),
    (r"Shuffle the other cards back into your deck", lambda m, c: ("post", "")),
    (r"Put (\d+) random cards from among (\w+) and (\w+) from your deck onto your Bench",
     lambda m, c: ("post", f'(search {m.group(1)} bench (name "{m.group(2)}" "{m.group(3)}"))')),
    (r"Reveal all of your Pokemon in play and in your hand that have the (.+) attack, and this attack does (\d+) damage for each Pokemon you revealed in this way",
     lambda m, c: ("pre", f'(set-damage (* (with-attack my "{m.group(1)}") {m.group(2)}))')),
    (r"[Cc]hoose 1 of your opponent's Active Pokemon's attacks and use it as this attack", lambda m, c: ("post", "(copy-attack opp-active)")),
    (r"[Cc]hoose 1 of your opponent's Pokemon's attacks and use it as this attack", lambda m, c: ("post", "(copy-attack (each opp all))")),
    (r"[Cc]hoose 1 of your Benched Pokemon's attacks, except any Pokemon ex, and use it as this attack",
     lambda m, c: ("post", "(copy-attack (each my bench) :not-ex)")),
    (r"[Ss]witch this Pokemon with 1 of your Benched (?:" + E + r" )?Pokemon",
     lambda m, c: ("post", f"(switch-in (choose my bench{' (type ' + t(m.group(1)) + ')' if m.group(1) else ''}))")),
    (r"Switch your Active " + E + r" Pokemon with 1 of your Benched Pokemon",
     lambda m, c: ("post", f"(if (is-type my-active {t(m.group(1))}) (switch-in (choose my bench)))")),
    (r"You may switch this Pokemon with 1 of your Benched Pokemon", lambda m, c: ("post", "(one-of (switch-in (choose my bench)) (nothing))")),
    (r"have your opponent reveal their hand", lambda m, c: ("post", "(nothing)")),  # only information: nothing for the engine
    (r"switch it with your Active Pokemon", lambda m, c: ("post", "(switch-in self)")),
    (r"move all of your Energy in play to this Pokemon", lambda m, c: ("post", "(move-energy (each my all) self any all)")),
    (r"move (\d+) damage that your Active Pokemon has on it to this Pokemon", lambda m, c: ("post", f"(move-damage my-active self {m.group(1)})")),
    (r"choose 1 of your Pokemon that has damage on it, and move all of its damage to this Pokemon",
     lambda m, c: ("post", "(move-damage (choose my all damaged not-self) self all)")),
    (r"put a random card from your deck that evolves from this Pokemon onto this Pokemon to evolve it",
     lambda m, c: ("post", "(evolve-from-deck self)")),
    (r"attach an Energy from your discard pile to your Active " + E + r" Pokemon",
     lambda m, c: ("post", f"(if (is-type my-active {t(m.group(1))}) (attach-from-discard my-active any 1))")),
    (r"Discard a card from your hand or else nothing", lambda m, c: ("pre", "(if (>= (hand-size my) 1) (discard-from-hand 1) (cancel))")),
    (r"Discard an? " + E + r" Energy from this Pokemon, and this attack also does (\d+) damage to 1 of your opponent's Benched Pokemon",
     lambda m, c: ("post", f"(seq (discard-energy self {t(m.group(1))} 1) (damage (choose opp bench) {m.group(2)}))")),
    (REF + r" is Knocked Out", lambda m, c: ("post", f"(knock-out {target(m)})")),
    (r"your opponent can't get any points for it", lambda m, c: ("post", "(no-points)")),
    (r"put it into your hand instead of the discard pile", lambda m, c: ("post", "(to-hand-instead)")),
    (r"put this [\w' ]+ into your hand instead of the discard pile", lambda m, c: ("post", "(to-hand-instead)")),
    (r"Choose 1 of your (" + NAMES + r") that has damage on it, and move (\d+) of its damage to your opponent's Active Pokemon",
     lambda m, c: ("post", f"(move-damage (choose my all (name {name_list(m.group(1))}) damaged) opp-active {m.group(2)})")),
    (r"move 2 " + E + r" Energy from that Pokemon and attach 1 Energy each to 2 of your Benched Pokemon",
     lambda m, c: ("post", f"(pick (choose my bench) (seq (move-energy self picked {t(m.group(1))} 1) "
                           f"(move-energy self (choose my bench not-picked) {t(m.group(1))} 1)))")),
    (OPP_NEXT + r", if your (" + NAMES + r") would be Knocked Out by damage from an attack, it is not Knocked Out and its remaining HP becomes (\d+)",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} me (survive-at {m.group(3)}{names_opt(m.group(2))}))")),
    (r"During this turn, if your opponent's Active Pokemon is Knocked Out by damage from an attack used by your (" + NAMES + r"), "
     r"you get (\d+) more points?", lambda m, c: ("post", f"(effect this-turn me (extra-point {m.group(2)}{names_opt(m.group(1))}))")),
    # Back to the hand or the deck
    (r"put " + REF + r" into your hand", lambda m, c: ("post", f"(to-hand {target(m)})")),
    (r"put 1 of your (?:" + E + r" )?Pokemon that has damage on it into your hand",
     lambda m, c: ("post", f"(to-hand (choose my all{' (type ' + t(m.group(1)) + ')' if m.group(1) else ''} damaged))")),
    (r"put 1 of your (?:" + E + r" )?Pokemon in play, except any Pokemon ex, into your hand",
     lambda m, c: ("post", f"(to-hand (choose my all{' (type ' + t(m.group(1)) + ')' if m.group(1) else ''} not-ex))")),
    (r"return all Pokemon Tools attached to each Pokemon \(both yours and your opponent's\) to their owner's hand",
     lambda m, c: ("post", "(tools-to-hand (each both all tool))")),
    (r"(you may )?shuffle this Pokemon and all attached cards into your deck",
     lambda m, c: ("post", "(one-of (to-deck self) (nothing))" if m.group(1) else "(to-deck self)")),
    (r"draw cards until you have (\d+) cards in your hand", lambda m, c: ("post", f"(draw-until {m.group(1)})")),
    (r"shuffle an? (Basic )?Pokemon from your hand into your deck",
     lambda m, c: ("post", f"(hand-to-deck {'basic' if m.group(1) else 'pokemon'})")),
    (r"choose a card in your hand and switch it with a random (Supporter|Item|Pokemon Tool) card in your deck",
     lambda m, c: ("post", f"(swap-for {TRAINER_KINDS[m.group(1)]})")),
    (r"discard the Energy that has been generated in your Energy Zone", lambda m, c: ("post", "(cycle-energy)")),
    (r"the next Energy is produced", lambda m, c: ("post", "")),  # follows (cycle-energy), which does it
    (r"choose 1 of your opponent's Benched Pokemon and move a random Energy from it to your opponent's Active Pokemon",
     lambda m, c: ("post", "(move-random-energy (choose opp bench) opp-active 1)")),
    (r"choose (1 of your [^,]+)", lambda m, c: ("post", f"(pick {ref_only(m.group(1))})")),  # the rest acts on "that Pokemon"
    (r"heal (\d+) damage and remove a random Special Condition from " + REF,
     lambda m, c: ("post", f"(pick {target(m)} (seq (heal picked {m.group(1)}) (cure-random picked)))")),
    (r"your opponent discards cards from their hand until they have (\d+) cards in their hand",
     lambda m, c: ("post", f"(discard-hand-to opp {m.group(1)})")),
    (r"discard a random Energy from among the Energy attached to all of your opponent's Pokemon that have an Ability",
     lambda m, c: ("post", "(discard-energy-among (each opp all has-ability) 1)")),
    (r"discard a random Energy from among the Energy attached to all Pokemon \(both yours and your opponent's\)",
     lambda m, c: ("post", "(discard-energy-among (each both all) 1)")),
    (r"discard a Pokemon Tool card from a Pokemon \(yours or your opponent's\), or discard a Stadium card in play",
     lambda m, c: ("post", "(one-of (discard-tools (choose both all tool)) (discard-stadium))")),
    (r"discard a Stadium in play", lambda m, c: ("post", "(discard-stadium)")),
    # Cards
    (r"draw (a|\d+) cards?", lambda m, c: ("post", f"(draw {num(m.group(1))})")),
    (r"draw a card for each card in your opponent's hand", lambda m, c: ("post", "(draw (hand-size opp))")),
    (r"draw a card for each Mega Evolution Pokemon ex in play \(both yours and your opponent's\)",
     lambda m, c: ("post", "(draw (count both all mega))")),
    (r"put (a|\d+) random (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon(?: with (\d+) HP or less)? from your deck (into your hand|onto your Bench)",
     lambda m, c: ("post", f"(search {num(m.group(1))} {'hand' if 'hand' in m.group(5) else 'bench'} {card_kind(m.group(2))}"
                           f"{' (type ' + t(m.group(3)) + ')' if m.group(3) else ''}{' (max-hp ' + m.group(4) + ')' if m.group(4) else ''})")),
    (r"put a random Mega Evolution Pokemon ex from your deck into your hand", lambda m, c: ("post", "(search 1 hand mega)")),
    (r'put a random Pokemon that has "([^"]+)" in its name from your deck into your hand',
     lambda m, c: ("post", f'(search 1 hand pokemon (name-has "{m.group(1)}"))')),
    (r"put (a|\d+) random (" + NAMES + r") from your deck (into your hand|onto your Bench)",
     lambda m, c: ("post", f'(search {num(m.group(1))} {"hand" if "hand" in m.group(3) else "bench"} (name {name_list(m.group(2))}))')),
    (r"put (a|\d+) random cards from among (" + NAMES + r") from your deck into your hand",
     lambda m, c: ("post", f'(search {num(m.group(1))} hand (name {name_list(m.group(2))}))')),
    (r"put a random card that evolves from ([\w' -]+?) from your deck into your hand",
     lambda m, c: ("post", f'(search 1 hand (evolves-from "{m.group(1)}"))')),
    (r"put a random (Supporter|Item|Pokemon Tool|Stadium) card from your deck into your hand",
     lambda m, c: ("post", f"(search 1 hand {TRAINER_KINDS[m.group(1)]})")),
    # The discard pile
    (r"put (a|\d+) random (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon from your discard pile into your hand",
     lambda m, c: ("post", f"(from-discard random {num(m.group(1))} my hand {card_kind(m.group(2))}"
                           f"{' (type ' + t(m.group(3)) + ')' if m.group(3) else ''})")),
    (r"an? (?:" + E + r" )?Pokemon is chosen at random from your discard pile and put into your hand",
     lambda m, c: ("post", f"(from-discard random 1 my hand pokemon{' (type ' + t(m.group(1)) + ')' if m.group(1) else ''})")),
    (r"put (a|\d+) random (Supporter|Item|Pokemon Tool) cards? from your discard pile into your hand",
     lambda m, c: ("post", f"(from-discard random {num(m.group(1))} my hand {TRAINER_KINDS[m.group(2)]})")),
    (r"put an? (Supporter|Item|Pokemon Tool) card from your discard pile into your hand",
     lambda m, c: ("post", f"(from-discard choose 1 my hand {TRAINER_KINDS[m.group(1)]})")),
    (r"put a random (Supporter|Item|Pokemon Tool) card, except any (.+?), from your opponent's discard pile into your hand",
     lambda m, c: ("post", f'(from-discard random 1 opp hand {TRAINER_KINDS[m.group(1)]} (except "{m.group(2)}"))')),
    (r"put a Basic Pokemon from your opponent's discard pile onto their Bench",
     lambda m, c: ("post", "(from-discard choose 1 opp bench basic)")),
    (r"attach (an?|\d+) " + E + r" Energy from your discard pile to " + REF,
     lambda m, c: ("post", f"(attach-from-discard {target(m)} {t(m.group(2))} {num(m.group(1))})")),
    (r"this attack does (\d+) more damage for each point your opponent got during their last turn",
     lambda m, c: ("pre", f"(bonus (* (points-last-turn opp) {m.group(1)}))")),
    (r"this attack does (\d+) more damage for each (Supporter|Item|Pokemon Tool) card in your discard pile",
     lambda m, c: ("pre", f"(bonus (* (discard-count my {TRAINER_KINDS[m.group(2)]}) {m.group(1)}))")),
    (r"this attack does (\d+) more damage for each (?:" + E + r" )?Pokemon in your discard pile",
     lambda m, c: ("pre", f"(bonus (* (discard-count my pokemon{' (type ' + t(m.group(2)) + ')' if m.group(2) else ''}) {m.group(1)}))")),
    (r"discard the top (card|\d+ cards) of (your opponent's|your|each player's) deck",
     lambda m, c: ("post", f"(mill {dict(zip(['your opponent' + chr(39) + 's', 'your', 'each player' + chr(39) + 's'], ['opp', 'my', 'both']))[m.group(2)]} "
                           f"{1 if m.group(1) == 'card' else m.group(1).split()[0]})")),
    (r"discard a random card from your opponent's hand", lambda m, c: ("post", "(discard-hand-random opp 1)")),
    (r"your opponent reveals a random card from their hand and shuffles it into their deck",
     lambda m, c: ("post", "(hand-to-deck-random opp 1)")),
    (r"your opponent shuffles their hand into their deck and draws (a card|\d+ cards)",
     lambda m, c: ("post", f"(shuffle-hand-draw opp {1 if m.group(1) == 'a card' else m.group(1).split()[0]})")),
    (r"your opponent shuffles their hand into their deck and draws a card for each of their remaining points needed to win",
     lambda m, c: ("post", "(shuffle-hand-draw opp (points-needed opp))")),
    (r"each player shuffles the cards in their hand into their deck, then draws that many cards",
     lambda m, c: ("post", "(shuffle-hand-draw both same)")),
    (r"shuffle your hand into your deck", lambda m, c: ("post", "(shuffle-hand-draw my 0)")),
    (r"your opponent reveals their hand", lambda m, c: ("post", "(nothing)")),
    (r"look at the top (card|\d+ cards) of your deck", lambda m, c: ("post", "(nothing)")),
    (r"then, you may shuffle your deck", lambda m, c: ("post", "(nothing)")),
    (r"your opponent reveals all of the Supporter cards in their deck", lambda m, c: ("post", "(nothing)")),
    # Looking at the top of a deck and putting the cards back in any order changes nothing for a player
    # that can't see the deck. shortcut: revisit when a smarter player can use what it saw.
    (r"for each of your (?:" + E + r" )?Pokemon in play, look at that many cards from the top of (?:your|your opponent's) deck "
     r"and put them back in any order", lambda m, c: ("post", "(nothing)")),
    (r"(?:before doing damage, )?discard all Pokemon Tools from your opponent's Active Pokemon",
     lambda m, c: ("pre" if m.group(0).startswith("before") else "post", "(discard-tools opp-active)")),
    (r"discard all Pokemon Tool cards attached to each of your opponent's Pokemon", lambda m, c: ("post", "(discard-tools (each opp all))")),
]


def clause(text, ctx):
    """One clause, without its final period and with a lowercase first letter where it starts a sentence."""
    text = text.rstrip(".")
    low = text[:1].lower() + text[1:]
    for candidate in (text, low):
        for rx, fn in CLAUSES:
            m = re.fullmatch(rx, candidate)
            if m:
                return fn(m, ctx)
    raise Unsupported(f"clause: {text}")


def sentence(s, ctx):
    """A sentence → (phase, statement); a statement may be empty for text the previous sentence covered."""
    s = s.rstrip(".") if not s.startswith("(") else s
    m = re.fullmatch(r"If you do, (.+)", s)  # after an optional "you may A": B only follows when A had something to do
    if m:
        phase, stmt = clause(m.group(1), ctx)
        return "then", (phase, stmt)
    if s.startswith("If "):  # the condition may itself hold commas ("exactly 1, 3, or 5 cards"): try each ", "
        for k in [i for i in range(len(s)) if s.startswith(", ", i)]:
            try:
                cond = condition(s[3:k], ctx)
            except Unsupported:
                continue
            phase, stmt = clause(s[k + 2:], ctx)
            return phase, f"(if {cond} {stmt})" if stmt else ""
    m = re.fullmatch(r"For each heads, (.+)", s)
    if m:
        phase, stmt = clause(m.group(1), ctx)
        return phase, f"(repeat heads {stmt})"
    m = re.fullmatch(r"(?:N|\d+|1) of your opponent's (Benched )?Pokemon is chosen at random(?: (\d+) times| for each " + E + r" Energy attached to this Pokemon)?", s)
    if m:
        times = m.group(2) or (f"(energy self {t(m.group(3))})" if m.group(3) else "1")
        return "post", f"(set-random-hits {times} {'opp-bench' if m.group(1) else 'opp'})"
    m = re.fullmatch(r"1 other Pokemon \(either yours or your opponent's\) is chosen at random (\d+) times?", s)
    if m:
        return "post", f"(set-random-hits {m.group(1)} both)"
    return clause(s, ctx)


def balanced(text, start):
    depth = 0
    for i in range(start, len(text)):
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        if depth == 0:
            return text[start:i + 1]
    raise ValueError(text)


def with_pick(stmts):
    """When a later statement says "that Pokemon" (picked), the first choice is made once, with (pick …)."""
    stmts = [x for x in stmts if x]
    for i, x in enumerate(stmts):  # "Choose 1 of your …." on its own: everything after it acts on that Pokemon
        if x.startswith("(pick ") and balanced(x, 6) == x[6:-1]:
            if i + 1 == len(stmts):
                raise Unsupported("a choice with nothing after it")
            return stmts[:i] + [f"(pick {x[6:-1]} {seq(stmts[i + 1:])})"]
    if not any(re.search(r"\bpicked\b", x) for x in stmts) or any("(pick " in x for x in stmts):
        return stmts
    for i, x in enumerate(stmts):
        j = x.find("(choose ")
        if j >= 0:
            chooser = balanced(x, j)
            rest = [x[:j] + "picked" + x[j + len(chooser):]] + stmts[i + 1:]
            return stmts[:i] + [f"(pick {chooser} {seq(rest)})"]
    if any(re.search(r"\bself\b", x) for x in stmts) and not any(re.search(r"opp-active|defending", x) for x in stmts):
        return [re.sub(r"\bpicked\b", "self", x) for x in stmts]  # only this Pokémon was named: "it" is this Pokémon
    raise Unsupported("'that Pokemon' without a choice")


def seq(stmts):
    stmts = [s for s in stmts if s]
    if not stmts:
        return None
    return stmts[0] if len(stmts) == 1 else "(seq " + " ".join(stmts) + ")"


# Rules written over two or three sentences, read as one.
JOINED = [
    (r"Choose 1 of your Ultra Beasts\. Attach (\d+) random Energy from your discard pile to that Pokemon\.",
     r"Attach \1 random Energy from your discard pile to 1 of your Ultra Beasts."),
    (r"Choose either Poisoned or Confused\. Your opponent's Active Pokemon is now affected by that Special Condition\.",
     "Your opponent's Active Pokemon is now Poisoned or Confused."),
    (r"[Yy]our opponent reveals their hand\. Choose a (Supporter )?card you find there and discard it\.",
     lambda m: f"Your opponent discards a{' Supporter' if m.group(1) else ''} card you choose from their hand."),
    (r"[Yy]our opponent reveals their hand\. Choose a (Supporter )?card you find there and shuffle it into your opponent's deck\.",
     lambda m: f"Your opponent shuffles into their deck a{' Supporter' if m.group(1) else ''} card you choose from their hand."),
    (r"For each heads, a card is chosen at random from your opponent's hand\. Your opponent reveals that card and shuffles it into their deck\.",
     "Your opponent shuffles heads random cards from their hand into their deck."),
    (r"[Hh]ave your opponent shuffle their hand into their deck\. For each remaining point that your opponent needs to win, they draw a card\.",
     "have your opponent shuffle their hand into their deck and draw a card for each remaining point they need to win."),
    (r"If the Defending Pokemon tries to use an attack, your opponent flips a coin\. If tails, that attack doesn't happen\. This effect lasts "
     r"until the Defending Pokemon leaves the Active Spot, and it doesn't stack\.",
     "If the Defending Pokemon tries to use an attack, your opponent flips a coin until it leaves the Active Spot."),
    (r"Choose 2 of your Benched Pokemon\. For each of those Pokemon, take a (\{\w\}) Energy from your Energy Zone and attach it to that Pokemon\.",
     r"Choose 2 of your Benched Pokemon and attach a \1 Energy from your Energy Zone to each."),
]


def count_of(what):
    """The number behind "for each …" in "this attack does N (more) damage for each …"."""
    fixed = {"Evolution Pokemon on your Bench": "(count my bench evolution)",
             "Benched Pokemon (both yours and your opponent's)": "(count both bench)",
             "card in your hand": "(hand-size my)", "Pokemon Tool attached to all of your Pokemon": "(tools my)",
             "of your opponent's Pokemon in play that has an Ability": "(count opp all has-ability)",
             "point you have gotten": "(points my)", "point your opponent has gotten": "(points opp)",
             "time your Pokemon have been Knocked Out during this game": "(ko-count my)",
             "Trainer card in your opponent's deck": "(deck-count opp trainer)",
             "type of Energy attached to this Pokemon": "(types-on self)", "of your Benched Pokemon": "(count my bench)"}
    if what in fixed:
        return fixed[what]
    return f"(count my bench (name {name_list(what[len('of your Benched '):])}))"


def statements(text, ctx):
    """All sentences of a text → list of (phase, statement), with random-hit pairs joined up."""
    out = []
    text = normalize(text)  # the joined rules below are written in normalized words ("Pokemon")
    # One rule in two sentences: "Discard a card from your hand. If you can't, this attack does nothing."
    text = text.replace("Discard a card from your hand. If you can't, this attack does nothing.", "Discard 1 cards from your hand or else nothing.")
    text = re.sub(r"Discard (\d+) cards from your hand\. If you can't discard \d+ cards, this attack does nothing\.", r"Discard \1 cards from your hand or else nothing.", text)
    for old, new in JOINED:
        text = re.sub(old, new, text)
    for s in sentences(text):
        if out and out[-1][0] == "random-hits":  # "… is chosen at random 4 times. For each time …, do 40 damage to it."
            times, side = out.pop()[1]
            d = re.fullmatch(r"For each time a Pokemon was chosen, (?:also )?do (\d+) damage to it|Do (\d+) damage to (?:the chosen Pokemon|it)|"
                             r"This attack also does (\d+) damage to it", s.rstrip("."))
            if not d:
                raise Unsupported(f"random hits: {s}")
            pool = {"opp": "(random opp all)", "opp-bench": "(random opp bench)"}.get(side, "(random both all not-self)")
            out.append(("post", f"(hits {times} {pool} {d.group(1) or d.group(2) or d.group(3)})"))
            continue
        phase, stmt = sentence(s, ctx)
        if phase == "then":
            if not out or not out[-1][1]:
                raise Unsupported(f"'If you do' without something before it: {s}")
            before_phase, before = out.pop()
            out.append((before_phase, f"(then {before} {stmt[1]})" if stmt[1] else before))
            continue
        m = re.fullmatch(r"\(set-random-hits (\d+|\(energy self \w+\)) (opp|opp-bench|both)\)", stmt or "")
        if m:
            out.append(("random-hits", (m.group(1), m.group(2))))
            continue
        out.append((phase, stmt))
    if out and out[-1][0] == "random-hits":
        raise Unsupported("random hits without damage")
    return out


# --- Card parts ---------------------------------------------------------------------------------------

def compile_attack(damage, text, ctx):
    parts = statements(text, ctx)
    pre = [s for p, s in parts if p == "pre"]
    post = with_pick([s for p, s in parts if p == "post"])
    opts = " ".join(s for p, s in parts if p == "hitopt")  # "This attack's damage isn't affected by …"
    if damage.endswith(("×", "x")) and not any("set-damage" in s for s in pre):
        raise Unsupported("× damage without a rule")
    return "(seq " + " ".join([s for s in pre if s] + [f"(hit {opts})" if opts else "(hit)"] + [s for s in post if s]) + ")"


ABILITY_FORMS = [
    (r"If this Pokemon is in the Active Spot and is damaged by an attack from your opponent's Pokemon, (.+)",
     lambda m, c: ("on-damaged", "", m.group(1))),
    (r"Once during your turn, when you play this Pokemon from your hand to evolve 1 of your Pokemon, you may (.+)",
     lambda m, c: ("on-evolve", "", m.group(1))),
    (r"Once during your turn, you may (.+)", lambda m, c: ("activated", "", m.group(1))),
    (r"If this Pokemon is in the Active Spot, once during your turn, you may (.+)", lambda m, c: ("activated", ":active", m.group(1))),
    (r"Once during your turn, if this Pokemon is in the Active Spot, you may (.+)", lambda m, c: ("activated", ":active", m.group(1))),
    (r"At the end of your turn, if this Pokemon is in the Active Spot, (.+)",
     lambda m, c: ("at-end-of-turn", ":active", m.group(1).replace("damage from it", "damage from this Pokemon"))),
    (r"At the end of your turn, (.+)", lambda m, c: ("at-end-of-turn", "", m.group(1))),
    (r"Whenever you attach an? " + E + r" Energy from your Energy Zone to this Pokemon, (.+)",
     lambda m, c: ("on-attach", f":type {t(m.group(1))}", m.group(2))),
    (r"During Pokemon Checkup, if this Pokemon is in the Active Spot, (.+)", lambda m, c: ("during-checkup", ":active", m.group(1))),
    (r"During Pokemon Checkup, (.+)", lambda m, c: ("during-checkup", "", m.group(1))),
    (r"At the beginning of your turn, if this Pokemon is in the Active Spot, (.+)", lambda m, c: ("at-start-of-turn", ":active", m.group(1))),
    (r"At the end of each turn, (.+)", lambda m, c: ("at-end-of-each-turn", "", m.group(1).replace("to that Pokemon", "to your opponent's Active Pokemon"))),
    (r"Once during your turn, when you put this Pokemon from your hand onto your Bench, you may (.+)",
     lambda m, c: ("on-bench", "", m.group(1))),
    (r"Once during your turn, if this Pokemon is on your Bench, you may (.+)", lambda m, c: ("activated", ":bench", m.group(1))),
    (r"As often as you like during your turn, you may (.+)", lambda m, c: ("activated", ":repeat", m.group(1))),
    (r"Whenever you attach an Energy from your Energy Zone to this Pokemon, (.+)", lambda m, c: ("on-attach", "", m.group(1))),
    (r"If this Pokemon is in the Active Spot and is Knocked Out by damage from an attack from your opponent's Pokemon, (.+)",
     lambda m, c: ("on-knocked-out", ":active :by-attack", m.group(1))),
    (r"When this Pokemon is Knocked Out, (.+)", lambda m, c: ("on-knocked-out", "", m.group(1))),
    (r"If your opponent's Pokemon is Knocked Out by damage from this Pokemon's attacks, (.+)",
     lambda m, c: ("on-knocks-out", "", m.group(1))),
    (r"As long as this Pokemon is in the Active Spot, whenever you attach an Energy from your Energy Zone to it, (.+)",
     lambda m, c: ("on-attach", ":active", m.group(1).replace("it is now", "this Pokemon is now"))),
]

PASSIVES = [
    (r"This Pokemon takes -(\d+) damage from attacks", lambda m: f"(damage-taken -{m.group(1)})"),
    (r"This Pokemon takes -(\d+) damage from attacks from " + E + r"(?: or " + E + r")? Pokemon",
     lambda m: f"(damage-taken -{m.group(1)} :from {t(m.group(2))}{' ' + t(m.group(3)) if m.group(3) else ''})"),
    (r"If this Pokemon is in the Active Spot and is damaged by an attack from your opponent's Pokemon, do (\d+) damage to the Attacking Pokemon",
     lambda m: f"(reflect {m.group(1)} :active)"),
    (r"If any damage is done to this Pokemon by attacks, flip a coin\. If heads, prevent that damage", lambda m: "(prevent-flip)"),
    (r"As long as this Pokemon is in the Active Spot, attacks used by your opponent's Active Pokemon cost (\d+) \{C\} more",
     lambda m: f"(attack-cost {m.group(1)} :scope opp-active :active)"),
    (r"As long as this Pokemon is in the Active Spot, attacks used by your opponent's Active Pokemon do -(\d+) damage",
     lambda m: f"(damage-dealt -{m.group(1)} :scope opp-active :active)"),
    (r"As long as this Pokemon is in the Active Spot, your opponent can't use any Supporter cards from their hand",
     lambda m: "(cant-supporter :scope opp-all :active)"),
    (r"Attacks used by your " + E + r" Pokemon do \+(\d+) damage to your opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(2)} :scope my-all :type {t(m.group(1))})"),
    (r"Attacks used by your " + E + r" Pokemon and " + E + r" Pokemon do \+(\d+) damage to your opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(3)} :scope my-all :type {t(m.group(1))} {t(m.group(2))})"),
    (r"This Pokemon has no Retreat Cost", lambda m: "(no-retreat-cost)"),
    (r"Your Active Pokemon has no Retreat Cost", lambda m: "(no-retreat-cost :scope my-active)"),
    (r"Your Active " + E + r" Pokemon has no Retreat Cost", lambda m: f"(no-retreat-cost :scope my-active :type {t(m.group(1))})"),
    (r"This Pokemon can't be affected by any Special Conditions", lambda m: "(no-conditions)"),
    (r"Prevent all effects of attacks used by your opponent's Pokemon done to this Pokemon", lambda m: "(prevent-effects)"),
    (r"As long as this Pokemon is on your Bench, prevent all damage done to this Pokemon by attacks", lambda m: "(prevent-damage :bench)"),
    (r"If this Pokemon would be Knocked Out by damage from an attack, flip a coin\. If heads, this Pokemon is not Knocked Out, "
     r"and its remaining HP becomes (\d+)", lambda m: f"(survive-at {m.group(1)} :flip)"),
    (r"This Pokemon may have up to 2 Pokemon Tool cards attached to it", lambda m: "(extra-tools 1)"),
    (r"As long as this Pokemon is on your Bench, your Active (Basic |)(?:" + E + r" )?Pokemon's Retreat Cost is (\d+) less",
     lambda m: f"(retreat-cost -{m.group(3)} :scope my-active{stage_opt(m.group(1))}{type_opt(m.group(2))} :bench)"),
    (r"Your Active (" + NAMES + r") has no Retreat Cost", lambda m: f"(no-retreat-cost :scope my-active{names_opt(m.group(1))})"),
    (r"Your opponent's Active Pokemon's Retreat Cost is (\d+) more", lambda m: f"(retreat-cost {m.group(1)} :scope opp-active)"),
    (r"Each of your (?:" + E + r" )?Pokemon gets \+(\d+) HP", lambda m: f"(hp {m.group(2)} :scope my-all{type_opt(m.group(1))})"),
    (r"As long as this Pokemon is in the Active Spot, your opponent can't play any Stadium cards from their hand",
     lambda m: "(cant-stadium :scope opp-all :active)"),
    (r"As long as this Pokemon is on your Bench, attacks used by your Pokemon that evolve from (\w+) do \+(\d+) damage to your "
     r"opponent's Active Pokemon", lambda m: f'(damage-dealt {m.group(2)} :scope my-all :bench :evolves-from "{m.group(1)}")'),
    (r"Prevent all damage done to this Pokemon by attacks from your opponent's Pokemon ex", lambda m: "(prevent-damage :from-ex)"),
    (r"If any damage is done to this Pokemon by attacks, flip a coin\. If heads, this Pokemon takes -(\d+) damage from that attack",
     lambda m: f"(damage-taken -{m.group(1)} :flip)"),
    (r"This Pokemon can't be (Asleep|Paralyzed|Confused|Poisoned|Burned)", lambda m: f"(no-conditions :status {m.group(1).lower()})"),
    (r"Your opponent's Active Pokemon takes \+(\d+) damage from being Poisoned", lambda m: f"(poison-damage {m.group(1)} :scope opp-active)"),
    (r"Your opponent can't play any Pokemon from their hand to evolve their Active Pokemon", lambda m: "(cant-evolve-active :scope opp-all)"),
    (r"Pokemon \(both yours and your opponent's\) can't be healed", lambda m: "(no-healing :scope all)"),
    (r"This Pokemon gets \+(\d+) HP for each " + E + r" Energy attached to it", lambda m: f"(hp {m.group(1)} :energy {t(m.group(2))})"),
    (r"When this Pokemon is first damaged by an attack after coming into play, prevent that damage", lambda m: "(prevent-first-hit)"),
    (r"Basic Pokemon in play \(both yours and your opponent's\) have no Abilities", lambda m: "(no-abilities :scope all :stage basic)"),
    (r"As long as this Pokemon is in play, it is " + E + r" and " + E + r" type",
     lambda m: f"(extra-type :type {t(m.group(1))} {t(m.group(2))})"),
    (r"As long as this Pokemon is in the Active Spot, it can evolve during your first turn or the turn you play it",
     lambda m: "(evolve-early :active)"),
    (r"This Pokemon can evolve into any Pokemon that evolves from (\w+) if you play it from your hand onto this Pokemon\. "
     r"\(This Pokemon can't evolve during your first turn or the turn you play it\.\)", lambda m: f'(evolves-as :as "{m.group(1)}")'),
    (r"Each " + E + r" Energy attached to your " + E + r" Pokemon provides 2 " + E + r" Energy\. This effect doesn't stack",
     lambda m: f"(double-energy :energy {t(m.group(1))} :scope my-all :type {t(m.group(2))})"),
    (r"Each of your evolved Pokemon can use any attack from its previous Evolutions\. \(You still need the necessary Energy to use each attack\.\)",
     lambda m: "(previous-attacks :scope my-all :stage stage1 stage2)"),
    (r"Each of your Pokemon that has any (?:" + E + r" )?Energy attached recovers from all Special Conditions and can't be affected by any "
     r"Special Conditions", lambda m: f"(no-conditions :scope my-all :with-energy{' ' + t(m.group(1)) if m.group(1) else ''})"),
    (r"All of your Pokemon take -(\d+) damage from attacks from your opponent's Pokemon", lambda m: f"(damage-taken -{m.group(1)} :scope my-all)"),
    (r"Attacks used by your Pokemon do \+(\d+) damage to your opponent's Active Pokemon", lambda m: f"(damage-dealt {m.group(1)} :scope my-all)"),
    (r"This Pokemon's Retreat Cost is (\d+) less", lambda m: f"(retreat-cost -{m.group(1)})"),
    (r"This Pokemon's attacks do \+(\d+) damage to your opponent's Active Pokemon", lambda m: f"(damage-dealt {m.group(1)})"),
    (r"This Pokemon takes -(\d+) damage from attacks from your opponent's Pokemon", lambda m: f"(damage-taken -{m.group(1)})"),
    (r"This Pokemon can't attack", lambda m: "(cant-attack)"),
    (r"Attacks used by your Future Pokemon cost (\d+) less \{C\} Energy", lambda m: f"(attack-cost -{m.group(1)} :scope my-all :trait future)"),
]


def compile_ability(text, ctx):
    text = normalize(text)
    m = re.fullmatch(r"If you have (" + NAMES + r") in play, (.+)", text)
    if m:  # "If you have Arceus or Arceus ex in play, …": a passive that only counts while one of them is in play
        rest = m.group(2)[:1].upper() + m.group(2)[1:]
        for rx, fn in PASSIVES + CONDITIONAL_PASSIVES:
            pm = re.fullmatch(rx, rest.rstrip("."))
            if pm and fn:
                return f"(passive {fn(pm)[:-1]} :needs {name_list(m.group(1))}))"
        raise Unsupported(f"ability: {text}")
    for rx, fn in PASSIVES:
        m = re.fullmatch(rx, text.rstrip("."))
        if m:
            if not fn:
                raise Unsupported(f"ability: {text}")
            return f"(passive {fn(m)})"
    m = re.fullmatch(r"(?:If|During) (.+)", text)
    cond, rest = None, ""
    for k in [i for i in range(len(text)) if text.startswith(", ", i)] if m else []:
        head = text[text.index(" ") + 1:k]
        try:
            cond = "(first-turn)" if text.startswith("During your first turn,") else condition(head, ctx) if text.startswith("If") else None
        except Unsupported:
            continue
        rest = text[k + 2:]
        break
    if cond:  # "If this Pokémon has a Pokémon Tool attached, …": passives that only count while that holds
        parts = re.sub(r"^it ", "this Pokemon ", rest.rstrip(".")).split(", and this Pokemon ")
        mods = []
        for i, part in enumerate(parts if cond else []):
            part = ("This Pokemon " + part) if i else part[:1].upper() + part[1:]
            fn = next((fn(pm) for rx, fn in PASSIVES + CONDITIONAL_PASSIVES if fn and (pm := re.fullmatch(rx, part))), None)
            mods.append(fn and f"{fn[:-1]} :if {cond})")
        if mods and all(mods):
            return f"(passive {' '.join(mods)})"
    m = re.fullmatch(r"This Ability works if you have any (\w+) in play with an Ability other than (\w+)\. (.+)", text)
    if m:  # the Unown: each needs another of them with a different Ability
        cond = f'(exists my all (name "{m.group(1)}") (other-ability "{m.group(2)}"))'
        for rx, fn in PASSIVES:
            pm = re.fullmatch(rx, m.group(3).rstrip("."))
            if pm and fn:
                return f"(passive {fn(pm)[:-1]} :if {cond}))"
    m = re.fullmatch(r"As long as this Pokemon is in the Active Spot, whenever your opponent attaches an Energy from their Energy Zone to "
                     r"1 of their Pokemon, do (\d+) damage to that Pokemon\.", text)
    if m:  # Jolteon ex: picked = the Pokémon the Energy went to
        return f"(on-opp-attach :active (damage picked {m.group(1)}))"
    m = (re.fullmatch(r"At the end of your (first) turn, (.+)", text) or
         re.fullmatch(r"At the end of your (opponent's) turn, if this Pokemon is in the Active Spot, (.+)", text))
    if m:  # Zeraora; Caterpie (end of each turn, before Checkup: ruling)
        body = seq([s for _, s in statements(m.group(2)[:1].upper() + m.group(2)[1:], ctx)])
        if m.group(1) == "first":
            return f"(at-end-of-turn (if (first-turn) {body}))"
        return f"(at-end-of-each-turn (if (and (not (my-turn)) (is-active self)) {body}))"
    m = re.fullmatch(r"You must discard a card from your hand in order to use this Ability\. Once during your turn, you may (.+)", text)
    if m:  # Garchomp
        body = seq([s for _, s in statements(m.group(1)[:1].upper() + m.group(1)[1:], ctx)])
        return f"(activated (then (discard-from-hand 1) {body}))"
    m = re.fullmatch(r"Once during your turn, if (.+?), you may (.+)", text)
    if m and not m.group(1).startswith("this Pokemon is"):  # Sylveon, Crobat: the condition goes in the effect
        try:
            cond = condition(m.group(1), ctx)
        except Unsupported:
            cond = None
        body = seq(with_pick([s for _, s in statements(m.group(2)[:1].upper() + m.group(2)[1:], ctx)])) if cond else ""
        if body:
            return f"(activated (if {cond} {body}))"
    for rx, fn in ABILITY_FORMS:
        m = re.fullmatch(rx, text)
        if m:
            form, opts, rest = fn(m, ctx)
            cost = re.fullmatch(r"(.+?) in order to use this Ability\. (.+)", rest)
            if cost and form == "activated":  # Skeledirge: "you may discard 1 {R} Energy … in order to use this Ability."
                pay = seq([s for _, s in statements(cost.group(1)[:1].upper() + cost.group(1)[1:] + ".", ctx)])
                then = seq([s for _, s in statements(cost.group(2), ctx)])
                return f"(activated (then {pay} {then}))"
            stmts = [s for _, s in statements(rest[:1].upper() + rest[1:], ctx)]
            body = seq(with_pick(stmts))
            if not body:
                raise Unsupported(f"ability without effect: {text}")
            if form == "on-bench":  # "you may": the player picks, as with any choice
                body = f"(one-of {body} (nothing))"
            return f"({form}{' ' + opts if opts else ''} {body})"
    raise Unsupported(f"ability: {text}")


CONDITIONAL_PASSIVES = [
    (r"Attacks used by this Pokemon do \+(\d+) damage to your opponent's Active Pokemon", lambda m: f"(damage-dealt {m.group(1)})"),
    (r"Attacks used by this Pokemon cost (\d+) less \{C\} Energy", lambda m: f"(attack-cost -{m.group(1)})"),
    (r"Attacks used by this Pokemon cost (\d+) less " + E + r" Energy", lambda m: f"(attack-cost -{m.group(1)} :energy {t(m.group(2))})"),
]

TOOLS = [
    (r"Attacks used by the Ultra Beast this card is attached to do \+(\d+) damage to your opponent's Active Pokemon for each point you have gotten",
     lambda m: f"(damage-dealt {m.group(1)} :trait ultra-beast :per-point)"),
    (r"The Ancient Pokemon this card is attached to gets \+(\d+) HP", lambda m: f"(hp {m.group(1)} :trait ancient)"),
    (r"Attacks used by the Future Pokemon this card is attached to do \+(\d+) damage to your opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(1)} :trait future)"),
    (r"The Pokemon this card is attached to can use any attack from its previous Evolutions\. \(You still need the necessary Energy to "
     r"use each attack\.\)", lambda m: "(previous-attacks)"),
    (r"The (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon this card is attached to gets \+(\d+) HP",
     lambda m: f"(hp {m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
    (r"If the Pokemon this card is attached to is in the Active Spot and is damaged by an attack from your opponent's Pokemon, "
     r"do (\d+) damage to the Attacking Pokemon", lambda m: f"(reflect {m.group(1)})"),
    (r"The Pokemon this card is attached to takes -(\d+) damage from attacks(?: from your opponent's Pokemon)?", lambda m: f"(damage-taken -{m.group(1)})"),
    (r"The " + E + r" Pokemon this card is attached to takes -(\d+) damage from attacks(?: from your opponent's Pokemon)?",
     lambda m: f"(damage-taken -{m.group(2)} :type {t(m.group(1))})"),
    (r"Attacks used by the Pokemon this card is attached to do \+(\d+) damage to your opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(1)})"),
    (r"The (Stage 2 )?Pokemon this card is attached to has no Retreat Cost",
     lambda m: "(no-retreat-cost" + (" :stage stage2)" if m.group(1) else ")")),
    (r"The Retreat Cost of the (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon this card is attached to is (\d+) less",
     lambda m: f"(retreat-cost -{m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
]

STADIUMS = [
    (r"Each (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) gets \+(\d+) HP",
     lambda m: f"(hp {m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
    (r"The Retreat Cost of each (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) is (\d+) less",
     lambda m: f"(retreat-cost -{m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
    (r"Attacks used by (?:each )?(Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) do \+(\d+) damage to the opponent's Active Pokemon( ex)?",
     lambda m: f"(damage-dealt {m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))}{' :vs-ex' if m.group(4) else ''})"),
]


def as_you(text):
    """A Stadium's "that player draws … their hand" as "draw … your hand", so the usual patterns read it."""
    text = re.sub(r"^(draws|heals|puts)\b", lambda m: m.group(1)[:-1], text)
    for old, new in (("that player puts", "put"), ("that player draws", "draw"), ("that player heals", "heal"),
                     ("If they do, they draw", "If you do, draw"), ("If they do,", "If you do,"),
                     ("until they have", "until you have")):
        text = text.replace(old, new)
    return re.sub(r"\btheir\b", "your", text)


def stage_opt(s):
    return {"Basic ": " :stage basic", "Stage 1 ": " :stage stage1", "Stage 2 ": " :stage stage2"}.get(s or "", "")


def card_kind(stage):
    """"Basic " → basic … for search filters; no stage → any Pokémon."""
    return {"Basic ": "basic", "Stage 1 ": "stage1", "Stage 2 ": "stage2"}.get(stage or "", "pokemon")


def type_opt(letter):
    return f" :type {t(letter)}" if letter else ""


RARE_CANDY = ("Choose 1 of your Basic Pokemon in play. If you have a Stage 2 card in your hand that evolves from that Pokemon, "
              "put that card onto the Basic Pokemon to evolve it, skipping the Stage 1. You can't use this card during your "
              "first turn or on a Basic Pokemon that was put into play this turn.")
FOSSIL = (r"Play this card as if it were a \d+-HP Basic \{C\} Pokemon\. At any time during your turn, you may discard this card "
          r"from play\. This card can't retreat\.")


def compile_card(kind, text, ctx):
    text = normalize(text)
    if re.fullmatch(FOSSIL, text):
        # shortcut: a Fossil can't be discarded from play by choice; add that once a player would want to.
        return "(passive (cant-retreat))"
    m = re.fullmatch(r"At the end of your turn, if the (?:" + E + r" )?Pokemon this card is attached to is in the Active Spot, (.+)", text)
    if kind == "Tool" and m:
        body = seq([s for _, s in statements(m.group(2).replace("that Pokemon", "this Pokemon"), ctx)])
        if m.group(1):
            body = f"(if (is-type self {t(m.group(1))}) {body})"
        return f"(at-end-of-turn :active {body})"
    if kind == "Tool":
        for rx, fn in TOOLS:
            m = re.fullmatch(rx, text.rstrip("."))
            if m:
                return f"(tool {fn(m)})"
        m = re.fullmatch(r"If the (?:" + E + r" )?Pokemon this card is attached to is (?:your Active Pokemon|in the Active Spot) "
                         r"and is damaged by an attack from your opponent's Pokemon, (.+)", text)
        if m:  # Poison Barb, Dark Pendant
            body = seq([s for _, s in statements(m.group(2)[:1].upper() + m.group(2)[1:], ctx)])
            return f"(on-damaged {'(if (is-type self ' + t(m.group(1)) + ') ' + body + ')' if m.group(1) else body})"
        m = re.fullmatch(r"If the (?:" + E + r" )?Pokemon this card is attached to is (in the Active Spot and is )?Knocked Out by damage "
                         r"from an attack from your opponent's Pokemon, (.+)", text)
        if m:  # Rescue Scarf, Lucky Egg, Electrical Cord
            body = seq([s for _, s in statements(m.group(3)[:1].upper() + m.group(3)[1:], ctx)])
            body = f"(if (is-type self {t(m.group(1))}) {body})" if m.group(1) else body
            return f"(on-knocked-out{' :active' if m.group(2) else ''} :by-attack {body})"
        m = re.fullmatch(r"Whenever your opponent's Pokemon is Knocked Out by damage from an attack used by the Pokemon this card is attached to, (.+)", text)
        if m:  # Lucky Mittens
            return f"(on-knocks-out {seq([s for _, s in statements(m.group(1)[:1].upper() + m.group(1)[1:], ctx)])})"
        m = re.fullmatch(r"At the end of each turn, (.+)", text)
        if m:  # the berries: "the Pokémon this card is attached to … it … discard this card"
            rest = m.group(1).replace("the Pokemon this card is attached to", "this Pokemon").replace("from it.", "from this Pokemon.")
            rest = rest.replace("it recovers from all of them, and discard this card", "this Pokemon recovers from all Special Conditions. If you do, discard this card")
            body = seq([s for _, s in statements(rest[:1].upper() + rest[1:], ctx)])
            return f"(at-end-of-each-turn {body})"
        if text == ("The {M} Pokemon this card is attached to takes -10 damage from attacks from your opponent's Pokemon, recovers from "
                    "all Special Conditions, and can't be affected by any Special Conditions."):
            return "(tool (damage-taken -10 :type metal) (no-conditions :type metal))"
        if text == "Prevent all effects of attacks used by your opponent's Pokemon done to the Pokemon this card is attached to. (Existing effects are not removed.)":
            return "(tool (prevent-effects))"
        m = re.fullmatch(r"If the Pokemon this card is attached to has a Retreat Cost of (\d+) or more, it takes -(\d+) damage from attacks from your opponent's Pokemon\.", text)
        if m:
            return f"(tool (damage-taken -{m.group(2)} :min-retreat {m.group(1)}))"
        if text == ("As long as the Pokemon this card is attached to is on your Bench, prevent all damage done to that Pokemon by your "
                    "opponent's attacks and Abilities."):
            return "(tool (prevent-damage :bench) (prevent-ability-damage :bench))"
        m = re.fullmatch(r"If this card is attached to 1 of your Pokemon, discard it at the end of your opponent's turn\. (.+)", text)
        if m:  # Metal Core Barrier: its modifier, and a second form that discards it
            return [compile_card(kind, m.group(1), ctx), "(at-end-of-each-turn (if (not (my-turn)) (discard-this)))"]
        raise Unsupported(f"tool: {text}")
    if kind == "Stadium":
        m = re.fullmatch(r"(Once during each player's turn, that player may|At the end of each player's turn, that player) (.+)", text)
        if m:  # the player whose turn it is runs it: "that player … their …" reads as "you … your …"
            form = "stadium-action" if m.group(1).startswith("Once") else "stadium-end-of-turn"
            body = seq(with_pick([s for _, s in statements(as_you(m.group(2)), ctx)]))
            if not body:
                raise Unsupported(f"stadium: {text}")
            return f"({form} {body})"
        if text == ("When applying the opponent's Active Pokemon's Weakness to damage from attacks used by Pokemon in play "
                    "(both yours and your opponent's) that aren't Mega Evolution Pokemon ex, apply Weakness as ×2."):
            return "(stadium (weakness-x2 :not-mega))"
        for rx, fn in STADIUMS:
            m = re.fullmatch(rx, text.rstrip("."))
            if m:
                return f"(stadium {fn(m)})"
        raise Unsupported(f"stadium: {text}")
    m = re.fullmatch(r"You can use this card only if (.+?)\. (.+)", text)
    if m:
        body = seq(with_pick([s for _, s in statements(m.group(2), ctx)]))
        if not body:
            raise Unsupported(f"trainer: {text}")
        return f"(play (if {condition(m.group(1), ctx)} {body}))"
    if text == RARE_CANDY:
        return "(play (rare-candy))"
    one_offs = {  # Trainers with a shape of their own
        "Look at the top card of your deck. If that card is a {P} Pokemon, put it into your hand. If it is not a {P} Pokemon, put it on "
        "the bottom of your deck.": "(play (top-card-if (type psychic)))",
        "Choose 1: ① During this turn, attacks used by your Pokemon that evolve from Eevee do +10 damage to your opponent's Active Pokemon. "
        "② Heal 20 damage from each of your Pokemon that evolves from Eevee.":
            '(play (one-of (effect this-turn me (damage-dealt 10 :scope my-all :evolves-from "Eevee")) '
            '(heal (each my all (evolves-from "Eevee") damaged) 20)))',
        "Choose 1 of your {G} Pokemon in play. Put a random {G} Pokemon from your deck that evolves from that Pokemon onto that Pokemon to "
        "evolve it. You can't use this card during your first turn or on a Pokemon that was put into play this turn.":
            "(play (if (not (first-turn)) (evolve-from-deck (choose my all (type grass) not-new) (type grass))))",
        "Choose 1 of your {W} Pokemon in play with a maximum HP of 50 or less. Put a random {W} Pokemon from your deck that evolves from "
        "that Pokemon onto that Pokemon to evolve it.": "(play (evolve-from-deck (choose my all (type water) (max-hp 50)) (type water)))",
    }
    if text in one_offs:
        return one_offs[text]
    m = re.fullmatch(r"Look at the top (\d+) cards of your deck\. Put all Pokemon you find there that have the (.+) attack into your hand\. "
                     r"Shuffle the other cards back into your deck\.", text)
    if m:  # Puppy-Loving Girl
        return f'(play (dig {m.group(1)} (has-attack "{m.group(2)}")))'
    if text == "Choose a Pokemon in your hand and switch it with a random Pokemon in your deck.":
        return "(play (swap-pokemon))"
    m = re.fullmatch(r"Look at the top (\d+) cards of your deck\. Put all (Pokemon Tool cards|Stage 1 Pokemon|Stage 2 Pokemon|"
                     r"Basic Pokemon|Supporter cards|Item cards) you find there into your hand\. Shuffle the other cards back into your deck\.", text)
    if m:
        what = {"Pokemon Tool cards": "tool", "Stage 1 Pokemon": "stage1", "Stage 2 Pokemon": "stage2",
                "Basic Pokemon": "basic", "Supporter cards": "supporter", "Item cards": "item"}[m.group(2)]
        return f"(play (dig {m.group(1)} {what}))"
    m = re.fullmatch(r"Choose 1 of your (?:" + E + r" )?Pokemon, and flip a coin until you get tails\. "
                     r"For each heads, take a " + E + r" Energy from your Energy Zone and attach it to that Pokemon\.", text)
    if m:
        pool = f"(choose my all{' (type ' + t(m.group(1)) + ')' if m.group(1) else ''})"
        return f"(play (pick {pool} (seq (flip-until-tails) (repeat heads (attach-energy picked {t(m.group(2))} 1)))))"
    m = re.fullmatch(r"Take (a|\d+) " + E + r" Energy from your Energy Zone and attach it to (?:your )?([\w' ]+?)(?:, ([\w' ]+?))?,? or ([\w' ]+?)\.", text)
    if m:
        names = " ".join(f'"{n}"' for n in (m.group(3), m.group(4), m.group(5)) if n)
        return f"(play (attach-energy (choose my all (name {names})) {t(m.group(2))} {num(m.group(1))}))"
    body = seq(with_pick([s for _, s in statements(text, ctx)]))
    if not body:
        raise Unsupported(f"trainer: {text}")
    return f"(play {body})"


def compile_row(row):
    """All scripts for one card: a list of (part, script), and a list of reasons for parts that failed."""
    out, failed = [], []
    attacks = [a.split("~") for a in row["attacks"].split("|")] if row["attacks"] else []
    ctx = {"attacks": attacks}
    for i, (name, cost, damage, effect) in enumerate(attacks):
        if not effect and not damage.endswith(("×", "x", "+")):
            continue
        try:
            out.append((f"attack:{i}", compile_attack(damage, effect, ctx)))
        except Unsupported as e:
            failed.append(str(e))
    for i, ability in enumerate(row["abilities"].split("|") if row["abilities"] else []):
        name, effect = ability.split("~")
        try:
            out.append((f"ability:{i}", compile_ability(effect, ctx)))
        except Unsupported as e:
            failed.append(str(e))
    if row["effect"]:
        try:
            scripts = compile_card(row["stage"], row["effect"], ctx)
            out += [("card", s) for s in (scripts if isinstance(scripts, list) else [scripts])]
        except Unsupported as e:
            failed.append(str(e))
    return out, failed


# --- Self-test --------------------------------------------------------------------------------------

EXAMPLES = [
    ("attack", "40", "Flip a coin. If heads, this attack does 30 more damage.", "(seq (flip 1) (if heads (bonus 30)) (hit))"),
    ("attack", "30×", "Flip 2 coins. This attack does 30 damage for each heads.", "(seq (flip 2) (set-damage (* heads 30)) (hit))"),
    ("attack", "", "This attack does 20 damage to 1 of your opponent's Pokémon.", "(seq (hit) (damage (choose opp all) 20))"),
    ("attack", "60", "This Pokémon also does 20 damage to itself.", "(seq (hit) (damage self 20))"),
    ("attack", "30", "Your opponent’s Active Pokémon is now Poisoned and Asleep.", "(seq (hit) (status opp-active poisoned asleep))"),
    ("attack", "50", "Flip a coin. If tails, this attack does nothing.", "(seq (flip 1) (if tails (cancel)) (hit))"),
    ("attack", "30", "During your opponent's next turn, this Pokémon takes −20 damage from attacks.",
     "(seq (hit) (effect opp-next-turn self (damage-taken -20)))"),
    ("attack", "", "Take a {P} Energy from your Energy Zone and attach it to 1 of your Benched Pokémon.",
     "(seq (hit) (attach-energy (choose my bench) psychic 1))"),
    ("attack", "30×", "This attack does 30 damage for each of your Benched {L} Pokémon.",
     "(seq (set-damage (* (count my bench (type lightning)) 30)) (hit))"),
    ("attack", "", "1 of your opponent's Pokémon is chosen at random 4 times. For each time a Pokémon was chosen, do 40 damage to it.",
     "(seq (hit) (hits 4 (random opp all) 40))"),
    ("ability", "", "Once during your turn, you may heal 20 damage from each of your Pokémon.", "(activated (heal (each my all damaged) 20))"),
    ("ability", "", "This Pokémon takes −20 damage from attacks.", "(passive (damage-taken -20))"),
    ("Supporter", "", "Draw 2 cards.", "(play (draw 2))"),
    ("Item", "", "Put a random Basic Pokémon from your deck into your hand.", "(play (search 1 hand basic))"),
    ("Item", "", "Heal 20 damage from 1 of your Pokémon.", "(play (heal (choose my all damaged) 20))"),
    ("Supporter", "", "Switch out your opponent's Active Pokémon to the Bench. (Your opponent chooses the new Active Pokémon.)",
     "(play (switch-in (owner-chooses opp bench)))"),
    ("Item", "", "During this turn, the Retreat Cost of your Active Pokémon is 1 less.",
     "(play (effect this-turn me (retreat-cost -1 :scope my-active)))"),
    ("Tool", "", "The Pokémon this card is attached to gets +20 HP.", "(tool (hp 20))"),
    ("Stadium", "", "Each Basic Pokémon in play (both yours and your opponent's) gets +20 HP.", "(stadium (hp 20 :stage basic))"),
    ("Supporter", "", "Take 2 {F} Energy from your Energy Zone and attach it to your Golem or Onix.",
     '(play (attach-energy (choose my all (name "Golem" "Onix")) fighting 2))'),
    ("Supporter", "", "During this turn, attacks used by your Ninetales, Rapidash, or Magmar do +30 damage to your opponent's Active Pokémon.",
     '(play (effect this-turn me (damage-dealt 30 :scope my-all :name "Ninetales" "Rapidash" "Magmar")))'),
    ("Supporter", "", "Heal 30 damage from 1 of your Pokémon, and it recovers from all Special Conditions.",
     "(play (pick (choose my all) (seq (heal picked 30) (cure picked))))"),
    ("Supporter", "", "Move an Energy from 1 of your Benched Pokémon to your Active Pokémon.",
     "(play (move-energy (choose my bench) my-active any 1))"),
    ("Supporter", "", "Move all {L} Energy from your Benched Pokémon to your Raichu, Electrode, or Electabuzz in the Active Spot.",
     '(play (move-energy (each my bench) (choose my active (name "Raichu" "Electrode" "Electabuzz")) lightning all))'),
    ("Item", "", "Flip a coin. If heads, take a Lightning Energy from your Energy Zone and attach it to 1 of your Benched Lightning Pokémon.",
     "(play (seq (flip 1) (if heads (attach-energy (choose my bench (type lightning)) lightning 1))))"),
    ("Tool", "", "At the end of your turn, if the Pokémon this card is attached to is in the Active Spot, heal 10 damage from that Pokémon.",
     "(at-end-of-turn :active (heal self 10))"),
    ("ability", "", "Once during your turn, when you play this Pokémon from your hand to evolve 1 of your Pokémon, you may draw 2 cards.",
     "(on-evolve (draw 2))"),
    ("Supporter", "", "During this turn, attacks used by your {F} Pokémon do +30 damage to your opponent's Active Pokémon ex.",
     "(play (effect this-turn me (damage-dealt 30 :scope my-all :type fighting :vs-ex)))"),
    ("Supporter", "", "Put 2 random Basic Pokémon with 50 HP or less from your deck into your hand.",
     "(play (search 2 hand basic (max-hp 50)))"),
    ("Supporter", "", "Heal 90 damage from 1 of your {P} Pokémon that has 2 or more {P} Energy attached. If you healed any damage "
     "in this way, discard 2 {P} Energy from that Pokémon.",
     "(play (pick (choose my all (type psychic) (has-energy 2 psychic) damaged) (seq (heal picked 90) (if (healed) (discard-energy picked psychic 2)))))"),
    ("Item", "", "Until the end of your opponent's next turn, your opponent's Active Pokémon's Retreat Cost is 1 more.",
     "(play (effect opp-next-turn opp-active (retreat-cost 1)))"),
    ("Tool", "", "At the end of your turn, if the {D} Pokémon this card is attached to is in the Active Spot, do 10 damage to your opponent's Active Pokémon.",
     "(at-end-of-turn :active (if (is-type self darkness) (damage opp-active 10)))"),
    ("Tool", "", "The Retreat Cost of the Basic Pokémon this card is attached to is 1 less.", "(tool (retreat-cost -1 :stage basic))"),
    ("Stadium", "", "Attacks used by each {F} Pokémon in play (both yours and your opponent's) do +20 damage to the opponent's Active Pokémon ex.",
     "(stadium (damage-dealt 20 :type fighting :vs-ex))"),
    ("Supporter", "", "You can use this card only if you have Araquanid in play. Switch in 1 of your opponent's Benched Pokémon to the Active Spot.",
     '(play (if (exists my all (name "Araquanid")) (switch-in (choose opp bench))))'),
    ("Supporter", "", "Choose 1 of your Alolan Marowak or Turtonator. Take 2 {R} Energy from your Energy Zone and attach it to that Pokémon. Your turn ends.",
     '(play (pick (choose my all (name "Alolan Marowak" "Turtonator")) (seq (attach-energy picked fire 2) (end-turn))))'),
    ("Item", "", "Discard a Pokémon Tool card from a Pokémon (yours or your opponent's), or discard a Stadium card in play.",
     "(play (one-of (discard-tools (choose both all tool)) (discard-stadium)))"),
    ("Supporter", "", "Choose 1 of your Electivire or Luxray. Attach 2 {L} Energy from your discard pile to that Pokémon.",
     '(play (pick (choose my all (name "Electivire" "Luxray")) (attach-from-discard picked lightning 2)))'),
    ("Item", "", "Put a Basic Pokémon from your opponent's discard pile onto their Bench.", "(play (from-discard choose 1 opp bench basic))"),
    ("Item", "", "Put a random Item card, except any Team Rocket's Thieving Machine, from your opponent's discard pile into your hand.",
     '(play (from-discard random 1 opp hand item (except "Team Rocket\'s Thieving Machine")))'),
    ("ability", "", "Once during your turn, you may attach a {R} Energy from your discard pile to this Pokémon. If you do, do 20 damage to this Pokémon.",
     "(activated (then (attach-from-discard self fire 1) (damage self 20)))"),
    ("Stadium", "", "Once during each player's turn, that player may flip a coin. If heads, that player puts a random Pokémon from their deck into their hand.",
     "(stadium-action (seq (flip 1) (if heads (search 1 hand pokemon))))"),
    ("Stadium", "", "Once during each player's turn, that player may shuffle a Basic Pokémon from their hand into their deck. If they do, they draw a card.",
     "(stadium-action (then (hand-to-deck basic) (draw 1)))"),
    ("Stadium", "", "At the end of each player's turn, that player draws cards until they have 3 cards in their hand.",
     "(stadium-end-of-turn (draw-until 3))"),
    ("Tool", "", "If the Pokémon this card is attached to is your Active Pokémon and is damaged by an attack from your opponent's Pokémon, "
     "the Attacking Pokémon is now Poisoned.", "(on-damaged (status attacker poisoned))"),
    ("Tool", "", "At the end of each turn, if the Pokémon this card is attached to has half of its maximum HP or less remaining, heal 30 damage "
     "from it. If you do, discard this card.", "(at-end-of-each-turn (then (if (half-hp self) (heal self 30)) (discard-this)))"),
    ("Tool", "", "At the end of each turn, if the Pokémon this card is attached to is affected by any Special Conditions, it recovers from all "
     "of them, and discard this card.", "(at-end-of-each-turn (then (if (has-status self any) (cure self)) (discard-this)))"),
    ("Supporter", "", "Put your Muk or Weezing in the Active Spot into your hand.",
     '(play (to-hand (choose my active (name "Muk" "Weezing"))))'),
    ("attack", "", "You may shuffle this Pokémon and all attached cards into your deck.", "(seq (hit) (one-of (to-deck self) (nothing)))"),
    ("ability", "", "If you have Arceus or Arceus ex in play, this Pokémon takes −30 damage from attacks.",
     '(passive (damage-taken -30 :needs "Arceus" "Arceus ex"))'),
    ("attack", "130", "During your opponent's next turn, this Pokémon takes +30 damage from attacks.",
     "(seq (hit) (effect opp-next-turn self (damage-taken 30)))"),
    ("attack", "60", "During your next turn, this Pokémon's Sacred Sword attack does +60 damage.",
     "(seq (hit) (effect my-next-turn self (damage-dealt 60)))"),
    ("attack", "30", "During your next turn, attacks used by your {F} Pokémon do +30 damage to your opponent's Active Pokémon.",
     "(seq (hit) (effect my-next-turn me (damage-dealt 30 :scope my-all :type fighting)))"),
    ("attack", "40+", "If any of your {D} Pokémon were Knocked Out by damage from an attack during your opponent's last turn, "
     "this attack does 80 more damage.", "(seq (if (ko-last-turn darkness) (bonus 80)) (hit))"),
    ("attack", "10+", "If 1 of your Pokémon used Sweets Relay during your last turn, this attack does 20 more damage.",
     '(seq (if (used-last-turn "Sweets Relay") (bonus 20)) (hit))'),
    ("attack", "30+", "If this Pokémon was damaged by an attack during your opponent's last turn while it was in the Active Spot, "
     "this attack does 50 more damage.", "(seq (if (damaged-last-turn self) (bonus 50)) (hit))"),
    ("attack", "50+", "This attack does 50 more damage for each point your opponent got during their last turn.",
     "(seq (bonus (* (points-last-turn opp) 50)) (hit))"),
    ("ability", "", "If this Pokémon is in the Active Spot and is Knocked Out by damage from an attack from your opponent's Pokémon, "
     "do 50 damage to the Attacking Pokémon.", "(on-knocked-out :active :by-attack (damage attacker 50))"),
    ("ability", "", "When this Pokémon is Knocked Out, flip a coin. If heads, your opponent can't get any points for it.",
     "(on-knocked-out (seq (flip 1) (if heads (no-points))))"),
    ("ability", "", "If this Pokémon would be Knocked Out by damage from an attack, flip a coin. If heads, this Pokémon is not "
     "Knocked Out, and its remaining HP becomes 10.", "(passive (survive-at 10 :flip))"),
    ("ability", "", "If your opponent's Pokémon is Knocked Out by damage from this Pokémon's attacks, during your opponent's next turn, "
     "prevent all damage from—and effects of—attacks done to this Pokémon.", "(on-knocks-out (effect opp-next-turn self (prevent-all)))"),
    ("ability", "", "If this Pokémon has a Pokémon Tool attached, attacks used by this Pokémon cost 1 less {G} Energy.",
     "(passive (attack-cost -1 :energy grass :if (has-tool self)))"),
    ("ability", "", "This Pokémon may have up to 2 Pokémon Tool cards attached to it.", "(passive (extra-tools 1))"),
    ("Tool", "", "If the Pokémon this card is attached to is Knocked Out by damage from an attack from your opponent's Pokémon, "
     "put it into your hand instead of the discard pile.", "(on-knocked-out :by-attack (to-hand-instead))"),
    ("Tool", "", "Whenever your opponent's Pokémon is Knocked Out by damage from an attack used by the Pokémon this card is attached "
     "to, draw a card.", "(on-knocks-out (draw 1))"),
    ("Supporter", "", "During your opponent's next turn, if your Hariyama or Crabominable would be Knocked Out by damage from an "
     "attack, it is not Knocked Out and its remaining HP becomes 10.",
     '(play (effect opp-next-turn me (survive-at 10 :name "Hariyama" "Crabominable")))'),
    ("Supporter", "", "During this turn, if your opponent's Active Pokémon is Knocked Out by damage from an attack used by your "
     "Haxorus, you get 1 more point.", '(play (effect this-turn me (extra-point 1 :name "Haxorus")))'),
    ("ability", "", "During Pokémon Checkup, if this Pokémon is in the Active Spot, do 10 damage to your opponent's Active Pokémon.",
     "(during-checkup :active (damage opp-active 10))"),
    ("ability", "", "If you don't have Regirock, Regice, and Registeel on your Bench, this Pokémon can't attack.",
     '(passive (cant-attack :if (not (and (exists my bench (name "Regirock")) (and (exists my bench (name "Regice")) '
     '(exists my bench (name "Registeel")))))))'),
    ("ability", "", "If you have another Falinks in play, this Pokémon's attacks do +20 damage to your opponent's Active Pokémon, and "
     "this Pokémon takes −20 damage from attacks from your opponent's Pokémon.",
     '(passive (damage-dealt 20 :if (exists my all (name "Falinks") not-self)) (damage-taken -20 :if (exists my all (name "Falinks") not-self)))'),
    ("ability", "", "Once during your turn, if you have Arceus or Arceus ex in play, you may do 30 damage to your opponent's Active Pokémon.",
     '(activated (if (exists my all (name "Arceus" "Arceus ex")) (damage opp-active 30)))'),
    ("ability", "", "Once during your turn, when you put this Pokémon from your hand onto your Bench, you may switch it with your Active "
     "Pokémon. If you do, move all of your Energy in play to this Pokémon.",
     "(on-bench (one-of (then (switch-in self) (move-energy (each my all) self any all)) (nothing)))"),
    ("ability", "", "Each {G} Energy attached to your {G} Pokémon provides 2 {G} Energy. This effect doesn't stack.",
     "(passive (double-energy :energy grass :scope my-all :type grass))"),
    ("ability", "", "As long as this Pokémon is in the Active Spot, whenever your opponent attaches an Energy from their Energy Zone to "
     "1 of their Pokémon, do 20 damage to that Pokémon.", "(on-opp-attach :active (damage picked 20))"),
    ("ability", "", "At the end of your opponent's turn, if this Pokémon is in the Active Spot, put a random card from your deck that "
     "evolves from this Pokémon onto this Pokémon to evolve it.",
     "(at-end-of-each-turn (if (and (not (my-turn)) (is-active self)) (evolve-from-deck self)))"),
    ("attack", "70", "Discard a card from your hand. If you can't, this attack does nothing.",
     "(seq (if (>= (hand-size my) 1) (discard-from-hand 1) (cancel)) (hit))"),
    ("attack", "", "This Pokémon is now Asleep. Heal 30 damage from it.", "(seq (hit) (status self asleep) (heal self 30))"),
    ("attack", "", "Choose 1 of your opponent's Active Pokémon's attacks and use it as this attack.", "(seq (hit) (copy-attack opp-active))"),
    ("attack", "40", "This attack's damage isn't affected by Weakness or by any effects on your opponent's Active Pokémon.",
     "(seq (hit :no-weakness :ignore-effects))"),
    ("attack", "50+", "This attack does 50 more damage for each point your opponent has gotten.", "(seq (bonus (* (points opp) 50)) (hit))"),
    ("attack", "40+", "If your opponent's Active Pokémon has more remaining HP than this Pokémon, this attack does 50 more damage.",
     "(seq (if (not (<= (hp-left opp-active) (hp-left self))) (bonus 50)) (hit))"),
    ("attack", "90", "1 of your opponent's Benched Pokémon is chosen at random 3 times. For each time a Pokémon was chosen, also do 20 "
     "damage to it.", "(seq (hit) (hits 3 (random opp bench) 20))"),
    ("attack", "50", "Flip a coin. If heads, your opponent reveals their hand. Choose a Supporter card you find there and discard it.",
     "(seq (flip 1) (hit) (if heads (opp-hand-discard supporter)))"),
    ("attack", "", "Put 3 random cards from among Silcoon and Cascoon from your deck onto your Bench.",
     '(seq (hit) (search 3 bench (name "Silcoon" "Cascoon")))'),
    ("attack", "20x", "Reveal all of your Pokémon in play and in your hand that have the Puppy Pile attack, and this attack does 20 "
     "damage for each Pokémon you revealed in this way.", '(seq (set-damage (* (with-attack my "Puppy Pile") 20)) (hit))'),
    ("Item", "", "Look at the top card of your deck. If that card is a {P} Pokémon, put it into your hand. If it is not a {P} Pokémon, "
     "put it on the bottom of your deck.", "(play (top-card-if (type psychic)))"),
]


def self_test():
    ctx = {"attacks": [["Sacred Sword", "", "", ""]]}
    for kind, damage, text, want in EXAMPLES:
        if kind == "attack":
            got = compile_attack(damage, text, ctx)
        elif kind == "ability":
            got = compile_ability(text, ctx)
        else:
            got = compile_card(kind, text, ctx)
        assert got == want, f"\n  text: {text}\n  want: {want}\n  got:  {got}"
    print(f"self-test: {len(EXAMPLES)} examples ok")


def main():
    self_test()
    if "--test" in sys.argv:
        return
    rows = list(csv.DictReader((DATA / "cards.tsv").open(), delimiter="\t", quoting=csv.QUOTE_NONE))
    manual = {}  # hand-written scripts for one-off cards: they replace every compiled script of that card
    for line in (DATA / "effects_manual.tsv").read_text().splitlines()[1:]:
        if line and not line.startswith("#"):
            card, part, script = line.split("\t")
            manual.setdefault(card, []).append((part, script))
    assert not set(manual) - {r["id"] for r in rows}, f"unknown ids in effects_manual.tsv: {set(manual) - {r['id'] for r in rows}}"
    lines, failures, complete = ["id\tpart\tscript"], Counter(), 0
    for row in rows:
        scripts, failed = (manual[row["id"]], []) if row["id"] in manual else compile_row(row)
        lines += [f"{row['id']}\t{part}\t{script}" for part, script in scripts]
        complete += not failed
        failures.update(re.sub(r"\d+", "N", f) for f in failed)
    (DATA / "effects.tsv").write_text("\n".join(lines) + "\n")
    print(f"{len(rows)} cards, {complete} with every piece of text compiled, {len(lines) - 1} scripts -> data/effects.tsv")
    print(f"\n{sum(failures.values())} pieces of text not compiled; most common:")
    for text, n in failures.most_common(40):
        print(f"  {n:3} {text[:150]}")


if __name__ == "__main__":
    main()
