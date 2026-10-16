"""Turn card text from data/cards.tsv into effect scripts in data/effects.tsv.

Each piece of text (an attack's effect, an ability, a Trainer's or Fossil's text) is split into
sentences, and each sentence is matched against the patterns below. A piece where every sentence
matches becomes one script; anything else is left out, and the engine then treats that card as
unsupported. The script language is described in DESIGN.md §4 and engine/src/effects/load_scripts.cpp.

  python3 compile_effects.py          compile, and list the sentences that matched nothing
  python3 compile_effects.py --test   only run the self-test
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
        raise Unsupported("the attacking Pokemon")
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
    (r"both of them are heads", lambda m, c: "all-heads"),
    (r"both of them are tails", lambda m, c: "all-tails"),
    (r"at least (\d+) of them are heads", lambda m, c: f"(>= heads {m.group(1)})"),
    (REF + r" has damage on it", lambda m, c: f"(has-damage {target(m)})"),
    (REF + r" is " + STATUS, lambda m, c: f"(has-status {target(m)} {m.group(len(m.groups())).lower()})"),
    (REF + r" is affected by a Special Condition", lambda m, c: f"(has-status {target(m)} any)"),
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
    (r"you have (\w[\w' ]*?) or (\w[\w' ]*?) in play",
     lambda m, c: f'(exists my all (name "{m.group(1)}" "{m.group(2)}"))'),
    (r"your opponent has (\d+) or more cards in their hand", lambda m, c: f"(>= (hand-size opp) {m.group(1)})"),
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


def timed(duration):
    return {"During your opponent's next turn": "opp-next-turn", "during your opponent's next turn": "opp-next-turn",
            "During your next turn": "my-next-turn", "during your next turn": "my-next-turn",
            "During this turn": "this-turn"}[duration]


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
    (REF + r" is now " + r"((?:Asleep|Burned|Confused|Paralyzed|Poisoned)(?:(?:, | and )(?:Asleep|Burned|Confused|Paralyzed|Poisoned))*)",
     lambda m, c: ("post", f"(status {target(m)} {status_list(m.group(len(m.groups())))})")),
    (r"(?:you may )?make your opponent's Active Pokemon " + STATUS, lambda m, c: ("post", f"(status opp-active {m.group(1).lower()})")),
    (r"heal (\d+) damage from " + REF + r", and it recovers from all Special Conditions",
     lambda m, c: ("post", f"(pick {target(m)} (seq (heal picked {m.group(1)}) (cure picked)))")),
    (r"heal (\d+) damage from " + REF + r", and it recovers from being ((?:Asleep|Burned|Confused|Paralyzed|Poisoned)(?:,? (?:and )?(?:Asleep|Burned|Confused|Paralyzed|Poisoned))*)",
     lambda m, c: ("post", f"(pick {target(m)} (seq (heal picked {m.group(1)}) (cure picked {status_list(m.group(len(m.groups())).replace(', and ', ', '))})))")),
    (REF + r" recovers from all Special Conditions", lambda m, c: ("post", f"(cure {target(m)})")),
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
    (OPP_NEXT + r", this Pokemon takes -(\d+) damage from attacks",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} self (damage-taken -{m.group(2)}))")),
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
    (r"During this turn, the Retreat Cost of your Active Pokemon is (\d+) less",
     lambda m, c: ("post", f"(effect this-turn me (retreat-cost -{m.group(1)} :scope my-active))")),
    (r"During this turn, attacks used by your (Pokemon|" + NAMES + r") do \+(\d+) damage to your opponent's Active Pokemon( ex)?",
     lambda m, c: ("post", f"(effect this-turn me (damage-dealt {m.group(2)} :scope my-all{names_opt(m.group(1))}{' :vs-ex' if m.group(3) else ''}))")),
    (r"During this turn, attacks used by your (" + NAMES + r") cost (\d+) less \{C\} Energy",
     lambda m, c: ("post", f"(effect this-turn me (attack-cost -{m.group(2)} :scope my-all{names_opt(m.group(1))}))")),
    (OPP_NEXT + r", all of your (?:(?P<ty>" + E.replace("(", "(?:") + r") )?(?P<who>Pokemon|" + NAMES + r") take -(?P<n>\d+) damage from attacks(?: from your opponent's Pokemon)?",
     lambda m, c: ("post", f"(effect {timed(m.group('d'))} me (damage-taken -{m.group('n')} :scope my-all"
                           f"{type_opt(m.group('ty')[1]) if m.group('ty') else ''}{names_opt(m.group('who'))}))")),
    # Switching
    (r"switch this Pokemon with (?:1|one) of your Benched Pokemon", lambda m, c: ("post", "(switch-in (choose my bench))")),
    (r"switch out your opponent's Active Pokemon to the Bench", lambda m, c: ("post", "(switch-in (owner-chooses opp bench))")),
    (r"\(Your opponent chooses the new Active Pokemon\.\)", lambda m, c: ("post", "")),
    (r"switch out your Active Pokemon to the Bench", lambda m, c: ("post", "(switch-in (choose my bench))")),
    (r"switch out your opponent's Active Basic Pokemon to the Bench",
     lambda m, c: ("post", "(if (is-basic opp-active) (switch-in (owner-chooses opp bench)))")),
    (r"switch your Active Pokemon that has damage on it with (?:1|one) of your Benched Pokemon",
     lambda m, c: ("post", "(if (has-damage my-active) (switch-in (choose my bench)))")),
    (r"switch in (?:1|one) of your opponent's Benched (Basic )?Pokemon( that has damage on it)? to the Active Spot",
     lambda m, c: ("post", f"(switch-in (choose opp bench{' basic' if m.group(1) else ''}{' damaged' if m.group(2) else ''}))")),
    # Cards
    (r"draw (a|\d+) cards?", lambda m, c: ("post", f"(draw {num(m.group(1))})")),
    (r"draw a card for each card in your opponent's hand", lambda m, c: ("post", "(draw (hand-size opp))")),
    (r"put (a|\d+) random (Basic )?(?:" + E + r" )?Pokemon from your deck (into your hand|onto your Bench)",
     lambda m, c: ("post", f"(search {num(m.group(1))} {'hand' if 'hand' in m.group(4) else 'bench'}"
                           f"{' basic' if m.group(2) else ' pokemon'}{' (type ' + t(m.group(3)) + ')' if m.group(3) else ''})")),
    (r"put (a|\d+) random (" + NAMES + r") from your deck (into your hand|onto your Bench)",
     lambda m, c: ("post", f'(search {num(m.group(1))} {"hand" if "hand" in m.group(3) else "bench"} (name {name_list(m.group(2))}))')),
    (r"put (a|\d+) random cards from among (" + NAMES + r") from your deck into your hand",
     lambda m, c: ("post", f'(search {num(m.group(1))} hand (name {name_list(m.group(2))}))')),
    (r"put a random card that evolves from ([\w' -]+?) from your deck into your hand",
     lambda m, c: ("post", f'(search 1 hand (evolves-from "{m.group(1)}"))')),
    (r"put a random Supporter card from your deck into your hand", lambda m, c: ("post", "(search 1 hand supporter)")),
    (r"discard the top (card|\d+ cards) of (your opponent's|your|each player's) deck",
     lambda m, c: ("post", f"(mill {dict(zip(['your opponent' + chr(39) + 's', 'your', 'each player' + chr(39) + 's'], ['opp', 'my', 'both']))[m.group(2)]} "
                           f"{1 if m.group(1) == 'card' else m.group(1).split()[0]})")),
    (r"discard a random card from your opponent's hand", lambda m, c: ("post", "(discard-hand-random opp 1)")),
    (r"your opponent reveals a random card from their hand and shuffles it into their deck",
     lambda m, c: ("post", "(hand-to-deck-random opp 1)")),
    (r"your opponent shuffles their hand into their deck and draws (a card|\d+ cards)",
     lambda m, c: ("post", f"(shuffle-hand-draw opp {1 if m.group(1) == 'a card' else m.group(1).split()[0]})")),
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
    m = re.fullmatch(r"If you do, (.+)", s)  # after an optional "you may …": the effect ran, so you did
    if m:
        return clause(m.group(1), ctx)
    m = re.fullmatch(r"If (.+?), (.+)", s)
    if m:
        try:
            cond = condition(m.group(1), ctx)
        except Unsupported:
            cond = None
        if cond:
            phase, stmt = clause(m.group(2), ctx)
            return phase, f"(if {cond} {stmt})" if stmt else ""
    m = re.fullmatch(r"For each heads, (.+)", s)
    if m:
        phase, stmt = clause(m.group(1), ctx)
        return phase, f"(repeat heads {stmt})"
    m = re.fullmatch(r"(N|\d+|1) of your opponent's Pokemon is chosen at random (\d+) times", s)
    if m:
        return "post", f"(set-random-hits {m.group(2)} opp)"
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
    if not any(re.search(r"\bpicked\b", x) for x in stmts) or any("(pick " in x for x in stmts):
        return stmts
    for i, x in enumerate(stmts):
        j = x.find("(choose ")
        if j >= 0:
            chooser = balanced(x, j)
            rest = [x[:j] + "picked" + x[j + len(chooser):]] + stmts[i + 1:]
            return stmts[:i] + [f"(pick {chooser} {seq(rest)})"]
    raise Unsupported("'that Pokemon' without a choice")


def seq(stmts):
    stmts = [s for s in stmts if s]
    if not stmts:
        return None
    return stmts[0] if len(stmts) == 1 else "(seq " + " ".join(stmts) + ")"


def statements(text, ctx):
    """All sentences of a text → list of (phase, statement), with random-hit pairs joined up."""
    out = []
    for s in sentences(text):
        if out and out[-1][0] == "random-hits":  # "… is chosen at random 4 times. For each time …, do 40 damage to it."
            times, side = out.pop()[1]
            d = re.fullmatch(r"For each time a Pokemon was chosen, do (\d+) damage to it|Do (\d+) damage to the chosen Pokemon", s.rstrip("."))
            if not d:
                raise Unsupported(f"random hits: {s}")
            pool = "(random opp all)" if side == "opp" else "(random both all not-self)"
            out.append(("post", f"(hits {times} {pool} {d.group(1) or d.group(2)})"))
            continue
        phase, stmt = sentence(s, ctx)
        m = re.fullmatch(r"\(set-random-hits (\d+) (opp|both)\)", stmt or "")
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
    if damage.endswith(("×", "x")) and not any("set-damage" in s for s in pre):
        raise Unsupported("× damage without a rule")
    return "(seq " + " ".join([s for s in pre if s] + ["(hit)"] + [s for s in post if s]) + ")"


ABILITY_FORMS = [
    (r"Once during your turn, when you play this Pokemon from your hand to evolve 1 of your Pokemon, you may (.+)",
     lambda m, c: ("on-evolve", "", m.group(1))),
    (r"Once during your turn, you may (.+)", lambda m, c: ("activated", "", m.group(1))),
    (r"If this Pokemon is in the Active Spot, once during your turn, you may (.+)", lambda m, c: ("activated", ":active", m.group(1))),
    (r"Once during your turn, if this Pokemon is in the Active Spot, you may (.+)", lambda m, c: ("activated", ":active", m.group(1))),
    (r"At the end of your turn, if this Pokemon is in the Active Spot, (.+)", lambda m, c: ("at-end-of-turn", ":active", m.group(1))),
    (r"At the end of your turn, (.+)", lambda m, c: ("at-end-of-turn", "", m.group(1))),
    (r"Whenever you attach an? " + E + r" Energy from your Energy Zone to this Pokemon, (.+)",
     lambda m, c: ("on-attach", f":type {t(m.group(1))}", m.group(2))),
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
    (r"This Pokemon can't be affected by any Special Conditions", None),
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
    for rx, fn in ABILITY_FORMS:
        m = re.fullmatch(rx, text)
        if m:
            form, opts, rest = fn(m, ctx)
            stmts = [s for _, s in statements(rest[:1].upper() + rest[1:], ctx)]
            body = seq(with_pick(stmts))
            if not body:
                raise Unsupported(f"ability without effect: {text}")
            return f"({form}{' ' + opts if opts else ''} {body})"
    raise Unsupported(f"ability: {text}")


CONDITIONAL_PASSIVES = [
    (r"Attacks used by this Pokemon do \+(\d+) damage to your opponent's Active Pokemon", lambda m: f"(damage-dealt {m.group(1)})"),
    (r"Attacks used by this Pokemon cost (\d+) less \{C\} Energy", lambda m: f"(attack-cost -{m.group(1)})"),
]

TOOLS = [
    (r"The Pokemon this card is attached to gets \+(\d+) HP", lambda m: f"(hp {m.group(1)})"),
    (r"The " + E + r" Pokemon this card is attached to gets \+(\d+) HP", lambda m: f"(hp {m.group(2)} :type {t(m.group(1))})"),
    (r"If the Pokemon this card is attached to is in the Active Spot and is damaged by an attack from your opponent's Pokemon, "
     r"do (\d+) damage to the Attacking Pokemon", lambda m: f"(reflect {m.group(1)})"),
    (r"The Pokemon this card is attached to takes -(\d+) damage from attacks", lambda m: f"(damage-taken -{m.group(1)})"),
    (r"The " + E + r" Pokemon this card is attached to takes -(\d+) damage from attacks",
     lambda m: f"(damage-taken -{m.group(2)} :type {t(m.group(1))})"),
    (r"Attacks used by the Pokemon this card is attached to do \+(\d+) damage to your opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(1)})"),
    (r"The (Stage 2 )?Pokemon this card is attached to has no Retreat Cost",
     lambda m: "(no-retreat-cost" + (" :stage stage2)" if m.group(1) else ")")),
    (r"The Retreat Cost of the (?:" + E + r" )?Pokemon this card is attached to is (\d+) less",
     lambda m: f"(retreat-cost -{m.group(2)}{type_opt(m.group(1))})"),
]

STADIUMS = [
    (r"Each (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) gets \+(\d+) HP",
     lambda m: f"(hp {m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
    (r"The Retreat Cost of each (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) is (\d+) less",
     lambda m: f"(retreat-cost -{m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
    (r"Attacks used by (Basic |Stage 1 |Stage 2 )?(?:" + E + r" )?Pokemon in play \(both yours and your opponent's\) do \+(\d+) damage to the opponent's Active Pokemon",
     lambda m: f"(damage-dealt {m.group(3)}{stage_opt(m.group(1))}{type_opt(m.group(2))})"),
]


def stage_opt(s):
    return {"Basic ": " :stage basic", "Stage 1 ": " :stage stage1", "Stage 2 ": " :stage stage2"}.get(s or "", "")


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
    m = re.fullmatch(r"At the end of your turn, if the Pokemon this card is attached to is in the Active Spot, (.+)", text)
    if kind == "Tool" and m:
        body = seq([s for _, s in statements(m.group(1).replace("that Pokemon", "this Pokemon"), ctx)])
        return f"(at-end-of-turn :active {body})"
    if kind == "Tool":
        for rx, fn in TOOLS:
            m = re.fullmatch(rx, text.rstrip("."))
            if m:
                return f"(tool {fn(m)})"
        raise Unsupported(f"tool: {text}")
    if kind == "Stadium":
        for rx, fn in STADIUMS:
            m = re.fullmatch(rx, text.rstrip("."))
            if m:
                return f"(stadium {fn(m)})"
        raise Unsupported(f"stadium: {text}")
    if text == RARE_CANDY:
        return "(play (rare-candy))"
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
            out.append(("card", compile_card(row["stage"], row["effect"], ctx)))
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
    ("ability", "", "If you have Arceus or Arceus ex in play, this Pokémon takes −30 damage from attacks.",
     '(passive (damage-taken -30 :needs "Arceus" "Arceus ex"))'),
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
    lines, failures, complete = ["id\tpart\tscript"], Counter(), 0
    for row in rows:
        scripts, failed = compile_row(row)
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
