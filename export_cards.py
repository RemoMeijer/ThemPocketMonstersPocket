"""Turn the Limitless pages in data/raw/limitless/ into data/cards.tsv, the flat file the C++ engine loads.

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
import html
import re
from pathlib import Path

DATA = Path(__file__).parent / "data"
RAW = DATA / "raw" / "limitless"
LETTER = {"Grass": "G", "Fire": "R", "Water": "W", "Lightning": "L", "Psychic": "P",
          "Fighting": "F", "Darkness": "D", "Metal": "M", "Dragon": "N", "Colorless": "C"}
ALT_ART = ("☆", "Crown")  # Limitless lists Shiny cards as ☆ too
FOSSIL = re.compile(r"Play this card as if it were a (\d+)-HP Basic \{(\w)\} Pokémon\.")
# Limitless errors, checked against Serebii, TCGdex and deckgym-core.
FIXES = {
    ("P-A-022", "hp"): "50",  # Jigglypuff: Limitless says 60, the other three 50
    # Flareon: Limitless and deckgym say "Discard Fire{R} Energy"; it means 1 {R} Energy (App)
    ("B4-022", "attacks"): "Bursting Tail~RR~70~Discard a {R} Energy from this Pokémon. "
                           "Your opponent's Active Pokémon is now Burned.",
}
COLUMNS = "id name category stage hp type weakness retreat suffix evolves_from attacks abilities effect".split()
CARD = re.compile(r'<div class="card-text">(.*?)<div class="card-set-info">\s*(\S+) #(\d+)\s*</div>', re.S)


def text(fragment):
    """HTML to plain card text; energy symbols become {W}."""
    fragment = re.sub(r'<span class="copy-only">\[</span><span[^>]*>(\w)</span><span class="copy-only">\]</span>',
                      r"{\1}", fragment)
    fragment = re.sub(r"<br\s*/?>", " ", fragment)
    text = " ".join(html.unescape(re.sub(r"<[^>]+>", "", fragment)).replace("’", "'").split())  # one apostrophe, so names match text
    assert not any(ch in text for ch in "|~\t"), text
    return text


def one(pattern, block, default=None):
    m = re.search(pattern, block, re.S)
    return text(m.group(1)) if m else default


def row(set_id, number, b):
    title = one(r'<p class="card-text-title">(.*?)</p>', b)
    kind = one(r'<p class="card-text-type">(.*?)</p>', b)
    name = one(r'<span class="card-text-name">(.*?)</span>', b)
    category, stage = kind.split(" - ")[:2]
    attacks, abilities, effect = [], [], ""
    for a in re.findall(r'<div class="card-text-attack">(.*?)</div>', b, re.S):
        m = re.search(r'<span class="ptcg-symbol">([^<]*)</span>(.*?)</p>', a, re.S)
        cost = m.group(1).strip().replace("0", "")
        attack, damage = re.fullmatch(r"(.*?)\s*(\d+[-+x×]?)?", text(m.group(2))).groups()
        attacks.append(f"{attack}~{cost}~{damage or ''}~{one(r'card-text-attack-effect.*?>(.*?)</p>', a, '')}")
    for a in re.findall(r'<div class="card-text-ability">(.*?)</div>', b, re.S):
        ability = one(r'<p class="card-text-ability-info">(.*?)</p>', a).removeprefix("Ability:").strip()
        abilities.append(f"{ability}~{one(r'card-text-ability-effect.*?>(.*?)</p>', a, '')}")
    if category == "Trainer":
        sections = re.findall(r'<div class="card-text-section">(.*?)</div>', b, re.S)
        effect = text(sections[1])
    hp = (re.search(r" - (\d+) HP$", title) or [None, ""])[1]
    type_ = (re.search(r" - (\w+) - \d+ HP$", title) or [None, ""])[1]
    weakness = one(r"Weakness: (\w+)", b, "")
    retreat = one(r"Retreat: (\d+)", b, "")
    fossil = FOSSIL.search(effect)
    if fossil:
        category, stage, hp, type_ = "Pokémon", "Basic", fossil.group(1), fossil.group(2)
    suffix = "EX" if name.endswith(" ex") else ""  # Limitless leaves out the "ex rule" box on some ex cards
    assert suffix or "ex rule:" not in text(b), (set_id, number, name)
    r = [f"{set_id}-{int(number):03d}", name, category.replace("Pokémon", "Pokemon"), stage.replace(" ", ""), hp,
         LETTER.get(type_, type_), LETTER.get(weakness, ""), retreat, suffix,
         one(r"Evolves from\s+(.*?)</p>", b, ""), "|".join(attacks), "|".join(abilities), effect]
    for (card, column), value in FIXES.items():
        if card == r[0]:
            r[COLUMNS.index(column)] = value
    return r


def main():
    prints = []  # (alt art?, row)
    for page in sorted(RAW.glob("*.text.html")):
        set_id = page.name.removesuffix(".text.html")
        t = page.read_text(encoding="utf-8")
        rarity = dict(re.findall(r'<a href="/cards/[^/]+/(\d+)">([^<]*)</a></td>\s*</tr>',
                                 (RAW / f"{set_id}.list.html").read_text(encoding="utf-8")))
        cards = [row(s, n, b) for b, s, n in CARD.findall(t)]
        listed = int(re.search(r"(\d+) Cards", t).group(1))
        assert len(cards) == listed == len(rarity), (set_id, len(cards), listed, len(rarity))
        prints += [(any(a in rarity[r[0].rsplit("-", 1)[1].lstrip("0")] for a in ALT_ART), r) for r in cards]
    unique = {}
    for alt, r in sorted(prints, key=lambda p: p[0]):  # stable: originals first, in set order
        unique.setdefault(tuple(r[1:]), (alt, r))  # everything but the id must match to count as the same card
    alt_only = [r[0] for alt, r in unique.values() if alt]
    assert not alt_only, f"alt-art prints that play differently from every original: {alt_only}"
    lines = ["\t".join(COLUMNS)] + sorted("\t".join(r) for _, r in unique.values())
    (DATA / "cards.tsv").write_text("\n".join(lines) + "\n")
    print(f"{len(prints)} cards, {len(unique)} unique -> data/cards.tsv")


if __name__ == "__main__":
    main()
