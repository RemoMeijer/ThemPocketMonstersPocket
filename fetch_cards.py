"""Download every Pokémon TCG Pocket card from the TCGdex API into data/cards.json.

Re-run when a new set comes out.
"""
import json
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

API = "https://api.tcgdex.net/v2/en"
OUT = Path(__file__).parent / "data" / "cards.json"


def get(path, tries=3):
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(f"{API}/{path}", timeout=30) as r:
                return json.load(r)
        except Exception:
            if attempt == tries - 1:
                raise
            time.sleep(2 ** attempt)


def main():
    sets = get("series/tcgp")["sets"]
    ids = [c["id"] for s in sets for c in get(f"sets/{s['id']}")["cards"]]
    print(f"{len(sets)} sets, {len(ids)} cards")
    with ThreadPoolExecutor(8) as pool:
        cards = list(pool.map(lambda i: get(f"cards/{i}"), ids))
    assert len(cards) == len(ids) and all(c["id"] == i for c, i in zip(cards, ids))
    OUT.parent.mkdir(exist_ok=True)
    OUT.write_text(json.dumps(cards, ensure_ascii=False, indent=1))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
