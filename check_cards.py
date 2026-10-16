"""Cross-check data/cards.tsv (from Limitless) against deckgym-core's card database, a second source.

Prints every card where the two disagree on numbers (HP, type, weakness, retreat, stage, evolves from,
attack costs and damage) or on effect text. Settle each one with a third source (serebii.net/tcgpocket)
and put real Limitless errors in FIXES in export_cards.py. Run after export_cards.py.
"""
import csv
import difflib
import json
import re
import urllib.request
from pathlib import Path

DATA = Path(__file__).parent / "data"
URL = "https://raw.githubusercontent.com/bcollazo/deckgym-core/main/database.json"
LETTER = {"Grass": "G", "Fire": "R", "Water": "W", "Lightning": "L", "Psychic": "P", "Fighting": "F",
          "Darkness": "D", "Metal": "M", "Dragon": "N", "Colorless": "C", None: ""}
# Text differences already checked (2026-10-16): deckgym glitches ("Discard Fire[R] Energy") or other
# wording ("a" vs "1", "back into") that doesn't change what the card does.
KNOWN = {"A1-045", "A1-132", "A1-181", "A1-196", "A1-210", "A2-129", "A3-051", "A3b-066", "B1-036", "B2-014",
         "B2-042", "B3-008", "P-A-005"}


def norm(t):
    t = (t or "").replace("’", "'").replace("−", "-").replace("–", "-").replace("Pokémon", "Pokemon")
    t = re.sub(r"\[(\w)\]", r"{\1}", t)
    t = re.sub(r"\.(?=[A-Z(])", ". ", t)  # deckgym drops the space between sentences
    return " ".join(t.split())


def main():
    raw = DATA / "raw" / "deckgym.json"
    raw.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(URL, timeout=60) as r:
        raw.write_bytes(r.read())
    other = {}
    for entry in json.loads(raw.read_text()):
        (kind, c), = entry.items()
        s, n = c["id"].split(" ")
        other[f"{s}-{int(n):03d}"] = (kind, c)
    problems = 0
    for row in csv.DictReader((DATA / "cards.tsv").open(), delimiter="\t", quoting=csv.QUOTE_NONE):
        kind, c = other.get(row["id"], (None, None))
        if not c:
            print(f"{row['id']} {row['name']}: not in deckgym")
            problems += 1
            continue
        if kind == "Trainer":
            if c["trainer_card_type"] == "Fossil":
                continue
            ours = {"name": row["name"], "stage": row["stage"], "text": [norm(row["effect"])]}
            theirs = {"name": c["name"], "stage": c["trainer_card_type"], "text": [norm(c["effect"])]}
        else:
            attacks = [a.split("~") for a in row["attacks"].split("|") if a]
            abilities = [a.split("~")[1] for a in row["abilities"].split("|") if a]
            ours = {"name": row["name"], "stage": row["stage"], "hp": row["hp"], "type": row["type"],
                    "weakness": row["weakness"], "retreat": row["retreat"], "evolves_from": row["evolves_from"],
                    "costs": [a[1] for a in attacks], "damage": [re.sub(r"\D", "", a[2]) or "0" for a in attacks],
                    "text": [norm(a[3]) for a in attacks] + [norm(a) for a in abilities]}
            theirs = {"name": c["name"], "stage": ["Basic", "Stage1", "Stage2"][c["stage"]], "hp": str(c["hp"]),
                      "type": LETTER[c["energy_type"]], "weakness": LETTER[c["weakness"]],
                      "retreat": str(len(c["retreat_cost"])), "evolves_from": c["evolves_from"] or "",
                      "costs": ["".join(LETTER[t] for t in a["energy_required"]) for a in c["attacks"]],
                      "damage": [str(a["fixed_damage"]) for a in c["attacks"]],
                      "text": [norm(a["effect"]) for a in c["attacks"]] + ([norm(c["ability"]["effect"])] if c["ability"] else [])}
        for field in ours:
            if ours[field] == theirs[field] or (field == "text" and row["id"] in KNOWN):
                continue
            problems += 1
            if field == "text":
                for a, b in zip(ours[field], theirs[field]):
                    sm = difflib.SequenceMatcher(None, a, b)
                    diffs = [(a[i:j], b[k:l]) for op, i, j, k, l in sm.get_opcodes() if op != "equal"]
                    if diffs:
                        print(f"{row['id']} {row['name']}: text, ours vs deckgym: {diffs[:4]}")
            else:
                print(f"{row['id']} {row['name']}: {field} {ours[field]!r}, deckgym {theirs[field]!r}")
    print(f"{problems} differences")


if __name__ == "__main__":
    main()
