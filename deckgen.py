"""Random legal decks, and the changes the deck search makes to them (AI_PLAN.md step 2a).

A deck is 1-3 Pokémon lines (a Basic and what evolves from it, such as 2-2-1), Energy types taken from the
Pokémon's attack costs, and Trainers that fit those Pokémon, up to 20 cards. Whether a Trainer fits is read from
its script in data/effects.tsv (NEEDS below); no card is named here. The first line is the main line, picked with
weight by its top stage's best damage per Energy.

  python3 deckgen.py N OUTDIR   write N random decks
  python3 deckgen.py --test     self-test: 2,000 random and 2,000 changed decks are legal, 50 load in tcgp-run

A deck here is {"cards": Counter(id -> copies), "energy": [type letters], "main": the main line's Basic name}.
"""
import csv
import random
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict
from itertools import accumulate
from pathlib import Path

ROOT = Path(__file__).parent
DATA = ROOT / "data"
TYPES = {"G": "Grass", "R": "Fire", "W": "Water", "L": "Lightning", "P": "Psychic", "F": "Fighting",
         "D": "Darkness", "M": "Metal"}  # the Energy types: never Colorless or Dragon
LETTER = {"grass": "G", "fire": "R", "water": "W", "lightning": "L", "psychic": "P", "fighting": "F",
          "darkness": "D", "metal": "M", "dragon": "N", "colorless": "C"}
DECK_SIZE, MAX_COPIES, MAX_POKEMON = 20, 2, 12
STAGE = {"Basic": 0, "Stage1": 1, "Stage2": 2}
SHAPES = {1: [(2,)], 2: [(2, 2), (2, 1)], 3: [(2, 2, 1), (2, 2, 2), (2, 1, 1)]}
# What a Trainer's script asks of the deck's Pokémon: every match must be met by at least one of them.
NEEDS = [
    ("name", r'[(:]name ((?:"[^"]*" ?)+)'),
    ("name-has", r'\(name-has ("[^"]*")'),
    ("evolves", r'[(:]evolves-from ("[^"]*")'),
    ("attack", r'(?:\(has-attack (?:[a-z-]+ )?|:attack )("[^"]*")'),
    ("type", r'(?:\(type |:type |\(is-type self )([a-z]+)'),
    ("trait", r'[(:]trait ([a-z-]+)'),
    ("mega", r'\(search \d+ hand (mega)\)'),
    ("stage", r'\b(rare-candy|stage1|stage2)\b'),
]
DRAW = ("(draw", "(search", "(dig", "shuffle-hand-draw")  # Trainers that find cards: 3x as likely

CARDS = {r["id"]: r for r in csv.DictReader((DATA / "cards.tsv").open(), delimiter="\t", quoting=csv.QUOTE_NONE)}
SCRIPT = defaultdict(str)
for r in csv.DictReader((DATA / "effects.tsv").open(), delimiter="\t", quoting=csv.QUOTE_NONE):
    SCRIPT[r["id"]] += r["script"] + "\n"
TRAITS = defaultdict(set)
for r in csv.DictReader((DATA / "traits.tsv").open(), delimiter="\t", quoting=csv.QUOTE_NONE):
    TRAITS[r["name"]].add(r["trait"])

PRINTINGS = defaultdict(list)  # name -> ids
for i, c in CARDS.items():
    PRINTINGS[c["name"]].append(i)
POKEMON = {i for i, c in CARDS.items() if c["category"] == "Pokemon"}
TRAINERS = sorted(set(CARDS) - POKEMON)
EVOLVES_FROM = {CARDS[i]["name"]: CARDS[i]["evolves_from"] for i in POKEMON}
EVOLUTIONS = defaultdict(set)  # name -> names that evolve from it
for name, before in EVOLVES_FROM.items():
    if before:
        EVOLUTIONS[before].add(name)


def basic_of(name):
    while EVOLVES_FROM.get(name):
        name = EVOLVES_FROM[name]
    return name


def attacks(i):
    return [a.split("~") for a in CARDS[i]["attacks"].split("|") if a]


def damage_per_energy(i):
    return max([int(re.match(r"\d*", dmg).group() or 0) / max(1, len(cost)) for _, cost, dmg, _ in attacks(i)], default=0)


def needs(script):
    out = []
    for kind, pattern in NEEDS:
        for m in re.finditer(pattern, script):
            values = set(re.findall(r'"([^"]*)"', m.group(1))) or {m.group(1)}
            if kind == "type":
                values = {LETTER[v] for v in values}
            if kind == "stage":
                values = {"Stage1" if v == "stage1" else "Stage2" for v in values}
            out.append((kind, values))
    return out


NEED = {i: needs(SCRIPT[i]) for i in TRAINERS}
WEIGHT = {i: 3 if any(w in SCRIPT[i] for w in DRAW) else 1 for i in TRAINERS}
CANDY = [i for i in TRAINERS if "(rare-candy" in SCRIPT[i]]

# Every line as a chain of names, Basic first; the main line is picked with weight 5 + its top stage's best
# damage per Energy, so plausible attackers come up more but anything can.
CHAINS = []
for b in sorted({c["name"] for c in CARDS.values() if c["category"] == "Pokemon" and c["stage"] == "Basic"}):
    CHAINS.append((b,))
    for s in sorted(EVOLUTIONS[b]):
        CHAINS.append((b, s))
        CHAINS += [(b, s, t) for t in sorted(EVOLUTIONS[s])]
CHAIN_WEIGHTS = [5 + max(damage_per_energy(i) for i in PRINTINGS[ch[-1]]) for ch in CHAINS]
CUM_WEIGHTS = list(accumulate(CHAIN_WEIGHTS))


def name(i):
    return CARDS[i]["name"]


def pokemon_count(cards):
    return sum(n for i, n in cards.items() if i in POKEMON)


def lines(deck):
    """The deck's Pokémon grouped by their Basic's name: {basic name: [ids]}, main line first."""
    out = defaultdict(list)
    for i in sorted((i for i in deck["cards"] if i in POKEMON), key=lambda i: STAGE[CARDS[i]["stage"]]):
        out[basic_of(name(i))].append(i)
    return dict(sorted(out.items(), key=lambda kv: kv[0] != deck["main"]))


def main_line(deck):
    return " > ".join(dict.fromkeys(name(i) for i in lines(deck).get(deck["main"], [])))


def line_power(ids):
    top = max(STAGE[CARDS[i]["stage"]] for i in ids)
    return max(damage_per_energy(i) for i in ids if STAGE[CARDS[i]["stage"]] == top)


def facts(cards):
    f = defaultdict(set)
    for i in cards:
        if i in POKEMON:
            c = CARDS[i]
            f["name"].add(c["name"]), f["type"].add(c["type"]), f["stage"].add(c["stage"])
            f["evolves"].add(c["evolves_from"]), f["trait"].update(TRAITS[c["name"]])
            f["attack"].update(a[0] for a in attacks(i))
            if c["name"].startswith("Mega "):  # as the engine decides (card.cpp)
                f["mega"].add("mega")
    return f


def relevant(i, f):
    return all(any(s in n for s in v for n in f["name"]) if kind == "name-has" else f[kind] & v for kind, v in NEED[i])


def problem(deck):
    """Why the deck is illegal, or None. The engine's validateDeck, plus: every Stage 1 and Stage 2 card has its
    pre-evolution in the deck (a Stage 2 may instead have Rare Candy and its Basic)."""
    cards, energy = deck["cards"], deck["energy"]
    if sum(cards.values()) != DECK_SIZE:
        return f"has {sum(cards.values())} cards"
    copies = Counter()
    for i, n in cards.items():
        copies[name(i)] += n
    over = [n for n, k in copies.items() if k > MAX_COPIES]
    if over:
        return f"more than {MAX_COPIES} copies of {over[0]}"
    if not any(i in POKEMON and CARDS[i]["stage"] == "Basic" for i in cards):
        return "has no Basic Pokémon"
    if not 1 <= len(energy) <= 3 or len(set(energy)) != len(energy) or not set(energy) <= set(TYPES):
        return f"bad energy types {energy}"
    candy = any(i in cards for i in CANDY)
    for i in cards:
        before, stage = CARDS[i]["evolves_from"], CARDS[i]["stage"]
        if stage == "Stage1" and before not in copies:
            return f"{name(i)} without {before}"
        if stage == "Stage2" and before not in copies and not (candy and EVOLVES_FROM.get(before) in copies):
            return f"{name(i)} without {before}"
    return None


def energy_types(cards, main, rng):
    """The non-Colorless types in the Pokémon's attack costs; None when that's more than 3."""
    types = {t for i in cards if i in POKEMON for a in attacks(i) for t in a[1] if t in TYPES}
    if len(types) > 3:
        return None
    if not types:
        top = max((i for i in cards if i in POKEMON and basic_of(name(i)) == main), key=lambda i: STAGE[CARDS[i]["stage"]])
        types = {CARDS[top]["type"]} if CARDS[top]["type"] in TYPES else {rng.choice(sorted(TYPES))}
    return [t for t in TYPES if t in types]


def add_line(cards, rng, weighted=False):
    """Add a random line that shares no name with the deck and keeps it at 12 Pokémon or fewer; its Basic's name,
    or None. A Stage 2 line may be 2-1-2 with 1-2 Rare Candy."""
    names = {name(i) for i in cards}
    for _ in range(50):
        chain = rng.choices(CHAINS, cum_weights=CUM_WEIGHTS)[0] if weighted else rng.choice(CHAINS)
        shapes = SHAPES[len(chain)] + ([(2, 1, 2)] if len(chain) == 3 and CANDY else [])
        shape = rng.choice(shapes)
        if names & set(chain) or pokemon_count(cards) + sum(shape) > MAX_POKEMON:
            continue
        for n, k in zip(chain, shape):
            cards[rng.choice(PRINTINGS[n])] += k
        if shape == (2, 1, 2):
            candy = next((i for i in CANDY if i in cards), rng.choice(CANDY))
            cards[candy] = min(MAX_COPIES, cards[candy] + rng.randint(1, 2))
        return chain[0]
    return None


def finish(cards, main, rng):
    """Repair a new or changed deck: Energy types from its Pokémon, Trainers that no longer fit taken out, then
    trimmed or filled with fitting Trainers to 20 cards. The legal deck, or None."""
    cards = +Counter(cards)
    pokemon = [i for i in cards if i in POKEMON]
    if not pokemon or pokemon_count(cards) > MAX_POKEMON:
        return None
    by_line = defaultdict(list)
    for i in pokemon:
        by_line[basic_of(name(i))].append(i)
    if main not in by_line:
        main = max(by_line, key=lambda b: line_power(by_line[b]))
    energy = energy_types(cards, main, rng)
    if energy is None:
        return None
    f = facts(cards)
    for i in [i for i in cards if i not in POKEMON and not relevant(i, f)]:
        del cards[i]
    while sum(cards.values()) > DECK_SIZE:
        cards[rng.choice([i for i in cards.elements() if i not in POKEMON])] -= 1
    cards = +cards
    options = [i for i in TRAINERS if relevant(i, f)]
    while sum(cards.values()) < DECK_SIZE:
        free = [i for i in options if cards[i] < MAX_COPIES]  # no Trainer reprints share a name, so id = name here
        if not free:
            return None
        cards[rng.choices(free, [WEIGHT[i] for i in free])[0]] += 1
    deck = {"cards": cards, "energy": energy, "main": main}
    return deck if problem(deck) is None else None


def random_deck(rng):
    for _ in range(1000):
        cards = Counter()
        main = add_line(cards, rng, weighted=True)
        for _ in range(rng.choices((0, 1, 2), (30, 50, 20))[0]):
            add_line(cards, rng)
        deck = finish(cards, main, rng)
        if deck:
            return deck
    raise RuntimeError("no legal random deck in 1000 tries")


# The changes: each takes a deck and returns a changed legal deck, or None when this try didn't work out.

def mutate_trainer(deck, rng):
    cards = Counter(deck["cards"])
    trainers = [i for i in cards.elements() if i not in POKEMON]
    if not trainers:
        return None
    cards[rng.choice(trainers)] -= 1  # finish() fills the gap with a fitting Trainer
    return finish(cards, deck["main"], rng)


def mutate_count(deck, rng):
    cards = Counter(deck["cards"])
    i = rng.choice(sorted(i for i in cards if i in POKEMON))
    cards[i] += rng.choice((-1, 1))  # a Trainer makes up the difference; problem() rejects a lost pre-evolution
    return finish(cards, deck["main"], rng)


def swap_line(deck, rng):
    others = [b for b in lines(deck) if b != deck["main"]]
    old = rng.choice(others) if others and rng.random() < 0.9 else deck["main"]
    cards = Counter({i: n for i, n in deck["cards"].items() if i not in POKEMON or basic_of(name(i)) != old})
    new = add_line(cards, rng, weighted=old == deck["main"])
    return finish(cards, new if old == deck["main"] else deck["main"], rng) if new else None


def swap_printing(deck, rng):
    cards = Counter(deck["cards"])
    choices = sorted(i for i in cards if len(PRINTINGS[name(i)]) > 1)
    if not choices:
        return None
    i = rng.choice(choices)
    cards[rng.choice([j for j in PRINTINGS[name(i)] if j != i])] += cards.pop(i)
    return finish(cards, deck["main"], rng)


OPERATORS = [mutate_trainer, mutate_count, swap_line, swap_printing]


def key(deck):
    return tuple(sorted(deck["cards"].items())), tuple(deck["energy"])


def mutate(deck, rng):
    """One random change; a fresh random deck when 50 tries change nothing."""
    for _ in range(50):
        new = rng.choice(OPERATORS)(deck, rng)
        if new and key(new) != key(deck):
            return new
    return random_deck(rng)


def crossover(a, b, rng):
    """Lines from both parents (the first one a parent's main line), then Trainers from both parents that still
    fit. A mutation of a when 50 tries fail."""
    for _ in range(50):
        la, lb = lines(a), lines(b)
        first = rng.choice([(a, a["main"]), (b, b["main"])])
        rest = [(a, x) for x in la] + [(b, x) for x in lb]
        rng.shuffle(rest)
        cards, want = Counter(), rng.choices((1, 2, 3), (30, 50, 20))[0]
        for parent, basic in [first] + rest:
            line = Counter({i: parent["cards"][i] for i in lines(parent)[basic]})
            names = {name(i) for i in cards}
            if len({basic_of(name(i)) for i in cards if i in POKEMON}) < want and not names & {name(i) for i in line} \
                    and pokemon_count(cards) + sum(line.values()) <= MAX_POKEMON:
                cards += line
        f = facts(cards)
        trainers = [i for d in (a, b) for i in d["cards"].elements() if i not in POKEMON]
        rng.shuffle(trainers)
        for i in trainers:
            if cards[i] < MAX_COPIES and sum(cards.values()) < DECK_SIZE and relevant(i, f):
                cards[i] += 1
        deck = finish(cards, first[1], rng)
        if deck:
            return deck
    return mutate(a, rng)


def read_deck(path):
    cards, energy = Counter(), []
    for line in Path(path).read_text().splitlines():
        line = line.split("#")[0].strip()
        if line.startswith("energy:"):
            names = {v.lower(): k for k, v in TYPES.items()}
            energy = [names.get(t.strip().lower(), t.strip()) for t in line[7:].split(",")]
        elif line:
            n, i = line.split()
            cards[i] += int(n)
    by_line = defaultdict(list)
    for i in cards:
        if i in POKEMON:
            by_line[basic_of(name(i))].append(i)
    main = max(by_line, key=lambda b: line_power(by_line[b])) if by_line else ""
    return {"cards": cards, "energy": energy, "main": main}


def write_deck(deck, path, title=""):
    out = [f"# {title}"] if title else []
    out += ["energy: " + ", ".join(TYPES[t] for t in deck["energy"]), ""]
    ids = [i for ids in lines(deck).values() for i in ids]
    ids += sorted((i for i in deck["cards"] if i not in POKEMON), key=name)
    out += [f"{deck['cards'][i]} {i:<9} # {name(i)}" for i in ids]
    Path(path).write_text("\n".join(out) + "\n")



def self_test():
    rng = random.Random(1)
    decks = [random_deck(rng) for _ in range(2000)]
    changed = []
    for _ in range(2000):
        a = rng.choice(decks)
        changed.append(crossover(a, rng.choice(decks), rng) if rng.random() < 0.3 else mutate(a, rng))
    bad = 0
    for d in decks + changed:
        why = problem(d) or (pokemon_count(d["cards"]) > MAX_POKEMON and "over 12 Pokémon") or (len(lines(d)) > 3 and "over 3 lines")
        if why:
            bad += 1
            print("illegal:", why, sorted(d["cards"].elements()), d["energy"])
    for p in ("pikachu", "charizard", "gardevoir", "water", "lightning", "fighting"):
        why = problem(read_deck(ROOT / "decks" / f"{p}.txt"))
        if why:
            bad += 1
            print(f"decks/{p}.txt judged illegal: {why}")
    with tempfile.TemporaryDirectory() as tmp:
        paths = []
        for k, d in enumerate(decks[:25] + changed[:25]):
            paths.append(f"{tmp}/deck_{k:02d}.txt")
            write_deck(d, paths[-1])
        if [read_deck(p)["cards"] for p in paths] != [d["cards"] for d in decks[:25] + changed[:25]]:
            bad += 1
            print("read_deck(write_deck(d)) differs from d")
        run = subprocess.run([str(ROOT / "engine/build/tcgp-run"), "--games", "2", "--quiet", *paths],
                             cwd=ROOT, capture_output=True, text=True)
        if run.returncode:
            bad += 1
            print("tcgp-run:", run.stderr.strip())
    n_lines = Counter(len(lines(d)) for d in decks)
    print(f"self-test: {len(decks)} random + {len(changed)} changed decks, {bad} problems; lines 1/2/3: "
          f"{n_lines[1]}/{n_lines[2]}/{n_lines[3]}; with Rare Candy: {sum(any(i in d['cards'] for i in CANDY) for d in decks)}; "
          f"tcgp-run {'failed' if run.returncode else 'loaded 50 of them'}")
    return bad


def main():
    if "--test" in sys.argv:
        sys.exit(1 if self_test() else 0)
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    n, out = int(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    rng = random.Random()
    for k in range(n):
        write_deck(random_deck(rng), out / f"deck_{k:04d}.txt")
    print(f"{n} decks in {out}")


if __name__ == "__main__":
    main()
