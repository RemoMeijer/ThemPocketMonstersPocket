"""Turn data/cards.json into data/cards.tsv, the flat file the C++ engine loads.

Drops Star, Crown and Shiny cards (alt-art reprints) and dedupes cards that play
the same. Keeps all effect text: compile_effects.py turns it into scripts, and the
engine decides which cards it supports.

Columns: id name category stage hp type weakness retreat suffix evolves_from attacks abilities effect
  stage     Basic/Stage1/Stage2 for Pokémon, Item/Supporter/Tool/Stadium for Trainers
  attacks   "name~cost~damage~effect" joined by "|", cost as type letters (e.g. "WC")
  abilities "name~effect" joined by "|"
  effect    a Trainer's text
Fossils ("Play this card as if it were a 40-HP Basic {C} Pokémon") are written as that Pokémon.
"""
import json
import re
from pathlib import Path

DATA = Path(__file__).parent / "data"
LETTER = {"Grass": "G", "Fire": "R", "Water": "W", "Lightning": "L", "Psychic": "P",
          "Fighting": "F", "Darkness": "D", "Metal": "M", "Dragon": "N", "Colorless": "C"}
ALT_ART = ("Star", "Crown", "Shiny")
FOSSIL = re.compile(r"Play this card as if it were a (\d+)-HP Basic \{(\w)\} Pokémon\.")


def clean(text):
    text = " ".join(str(text or "").split())
    assert not any(ch in text for ch in "|~\t"), text
    return text


def gameplay_key(c):
    fields = ("name", "category", "stage", "trainerType", "hp", "types", "weaknesses",
              "retreat", "attacks", "abilities", "effect", "evolveFrom")
    return json.dumps([c.get(f) for f in fields], sort_keys=True)


def row(c):
    attacks = "|".join(
        f"{clean(a['name'])}~{''.join(LETTER[t] for t in a.get('cost', []))}~{clean(a.get('damage'))}~{clean(a.get('effect'))}"
        for a in c.get("attacks", []))
    abilities = "|".join(f"{clean(a['name'])}~{clean(a.get('effect'))}" for a in c.get("abilities", []))
    weakness = c.get("weaknesses") or [{}]
    category, stage = c["category"], c.get("stage") or c.get("trainerType", "")
    hp, type_ = str(c.get("hp", "")), LETTER.get((c.get("types") or [""])[0], "")
    fossil = FOSSIL.search(c.get("effect") or "")
    if fossil:
        category, stage, hp, type_ = "Pokemon", "Basic", fossil.group(1), fossil.group(2)
    return [c["id"], clean(c["name"]), category, stage, hp, type_,
            LETTER.get(weakness[0].get("type"), ""), str(c.get("retreat", "")), c.get("suffix") or "",
            clean(c.get("evolveFrom")), attacks, abilities, clean(c.get("effect"))]


def main():
    cards = json.loads((DATA / "cards.json").read_text())
    kept = [c for c in cards if not any(s in (c.get("rarity") or "") for s in ALT_ART)]
    unique = {}
    for c in kept:
        unique.setdefault(gameplay_key(c), c)  # first one wins: the original print
    header = "id name category stage hp type weakness retreat suffix evolves_from attacks abilities effect".split()
    lines = ["\t".join(header)] + ["\t".join(row(c)) for c in unique.values()]
    (DATA / "cards.tsv").write_text("\n".join(lines) + "\n")
    print(f"{len(cards)} cards, {len(kept)} without alt arts, {len(unique)} unique -> data/cards.tsv")


if __name__ == "__main__":
    main()
