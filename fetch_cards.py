"""Download every Pokémon TCG Pocket set from Limitless (pocket.limitlesstcg.com) into data/raw/limitless/.

Two pages per set: the text view (every card's full text) and the list view (rarities).
About 50 requests in all. Re-run when a new set comes out; export_cards.py reads the result.
"""
import re
import time
import urllib.request
from pathlib import Path

SITE = "https://pocket.limitlesstcg.com"
OUT = Path(__file__).parent / "data" / "raw" / "limitless"


def get(path, tries=3):
    req = urllib.request.Request(SITE + path, headers={"User-Agent": "Mozilla/5.0 (personal deck simulator)"})
    for attempt in range(tries):
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read().decode("utf-8")
        except Exception:
            if attempt == tries - 1:
                raise
            time.sleep(2 ** attempt)


def main():
    sets = list(dict.fromkeys(re.findall(r'href="/cards/([A-Za-z0-9-]+)"', get("/cards"))))
    assert "A1" in sets and "P-A" in sets, sets
    OUT.mkdir(parents=True, exist_ok=True)
    for s in sets:
        for view in ("text", "list"):
            (OUT / f"{s}.{view}.html").write_text(get(f"/cards/{s}?display={view}"))
            time.sleep(1)  # be polite to a community site
        print(s)
    print(f"{len(sets)} sets -> {OUT}")


if __name__ == "__main__":
    main()
