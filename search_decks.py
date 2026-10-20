"""Evolutionary deck search with the heuristic player (AI_PLAN.md step 2b).

Each generation the population plays a pool of opponents by successive halving: all decks play 20 games against each
pool deck, the top half 40 more, the top quarter 80 more, ranked by their win rate over all those games (a draw counts
half). The top quarter are the elites; the rest of the next generation are mutations of elites (50%), crossovers of
two elites (30%) and fresh random decks (20%), from deckgen.py. The pool is the reference decks, 10 random decks and a
hall of fame (each generation's 2 best decks not in it yet), at most 24, the oldest non-reference ones dropped first.

  python3 search_decks.py [--generations G] [--population P] [--out DIR] [--seed-decks FILE ...]

The run folder (results/search-<time>/ by default) gets gen_NNN.csv (every deck evaluated in that generation),
all/ (every deck's file), decks/ (the hall of fame and the final top 10) and report.md.
"""
import argparse
import csv
import random
import shutil
import statistics
import subprocess
import tempfile
import time
from collections import Counter
from pathlib import Path

import numpy as np
from scipy.optimize import linprog

import deckgen

ROOT = Path(__file__).parent
RUN = ROOT / "engine/build/tcgp-run"
REFERENCES = [ROOT / "decks" / f"{n}.txt" for n in ("pikachu", "charizard", "gardevoir", "water", "lightning", "fighting")]
POOL_MAX, POOL_RANDOM, HALL_PER_GEN = 24, 10, 2
ROUNDS = (20, 40, 80)  # games against each pool deck in each round of successive halving
FINAL_GAMES = 500


def play(mains, pool, games, tmp):
    """tcgp-run with every main deck against every pool deck: [(deck A path, deck B path, score, games)]."""
    out = Path(tmp) / "run.csv"
    subprocess.run([str(RUN), "--games", str(games), "--quiet", "--csv", str(out), *mains, *(["--against", *pool] if pool else [])],
                   cwd=ROOT, check=True)
    return [(r["deckA"], r["deckB"], int(r["wins"]) + int(r["draws"]) / 2, int(r["games"])) for r in csv.DictReader(out.open())]


def rate(e):
    return e["score"] / e["games"]


def interval(score, games):  # normal approximation of the 95% interval
    p = score / games
    half = 1.96 * (p * (1 - p) / games) ** 0.5
    return p, max(0.0, p - half), min(1.0, p + half)


def evaluate(pop, pool, tmp):
    """Successive halving against the pool; returns pop ranked best first (later rounds first, then win rate)."""
    for e in pop:
        e["score"], e["games"], e["round"] = 0.0, 0, 0
    by_path = {e["path"]: e for e in pop}
    alive = pop
    for r, games in enumerate(ROUNDS):
        if r:
            alive = sorted(alive, key=rate, reverse=True)[:max(1, len(alive) // 2)]
        for a, _, score, n in play([e["path"] for e in alive], pool, games, tmp):
            by_path[a]["score"] += score
            by_path[a]["games"] += n
        for e in alive:
            e["round"] = r
    return sorted(pop, key=lambda e: (e["round"], rate(e)), reverse=True)


def nash(m):
    """The mix of decks that no single deck beats, for the zero-sum game of win rate - 0.5 (made antisymmetric)."""
    n = len(m)
    a = (m - m.T) / 2
    res = linprog(np.r_[np.zeros(n), -1], A_ub=np.c_[-a.T, np.ones(n)], b_ub=np.zeros(n),
                  A_eq=[np.r_[np.ones(n), 0]], b_eq=[1], bounds=[(0, None)] * n + [(None, None)])
    return res.x[:n], res.x[n]


def diverse(ranking, n, per_line=3):
    """The best n decks with at most per_line sharing a main line, so one idea can't fill the elites and the report
    (filled up from the rest when there aren't enough different lines)."""
    out, lines = [], Counter()
    for e in ranking:
        if len(out) < n and lines[deckgen.main_line(e["deck"])] < per_line:
            out.append(e)
            lines[deckgen.main_line(e["deck"])] += 1
    return out + [e for e in ranking if e not in out][:n - len(out)]


def report(run, ranking, pool, args, tmp):
    top = diverse(ranking, 10)
    for k, e in enumerate(top, 1):
        e["final"] = str(run / "decks" / f"top{k:02d}-{e['id']}.txt")
        shutil.copy(e["path"], e["final"])
    paths = [e["final"] for e in top]
    vs = {}
    for a, b, score, n in play(paths, pool, FINAL_GAMES, tmp):
        vs[a, b] = (score, n)
    refs = [str(p) for p in REFERENCES]

    def summed(a, opponents):
        s = [vs[a, b] for b in opponents]
        return sum(x for x, _ in s) / sum(n for _, n in s)

    m = np.full((len(top), len(top)), np.nan)
    np.fill_diagonal(m, 0.5)
    if len(top) > 1:
        for a, b, score, n in play(paths, [], FINAL_GAMES, tmp):
            m[paths.index(a), paths.index(b)] = score / n
    m = np.where(np.isnan(m), 1 - m.T, m)  # two decks: tcgp-run plays only one ordered pairing
    weights, value = nash(m)

    pct = lambda x: f"{100 * x:.1f}"
    out = [f"# Deck search {run.name}", "",
           f"{args.generations} generations of {args.population} decks, heuristic against heuristic. The final pool has "
           f"{len(pool)} decks ({len(refs)} reference). Win rates in %, draws count half; {FINAL_GAMES} games per "
           "pairing below, seats alternating.", "",
           "## Top 10", "",
           "| # | deck | main line | vs references | vs final pool | " + " | ".join(Path(r).stem for r in refs) + " |",
           "|---" * (5 + len(refs)) + "|"]
    for k, e in enumerate(top, 1):
        out.append(f"| {k} | {e['id']} | {deckgen.main_line(e['deck'])} | {pct(summed(e['final'], refs))} | "
                   f"{pct(summed(e['final'], pool))} | " + " | ".join(pct(summed(e["final"], [r])) for r in refs) + " |")
    for k, e in enumerate(top, 1):
        d = e["deck"]
        out += ["", f"### {k}. {e['id']}", "", "Energy: " + ", ".join(deckgen.TYPES[t] for t in d["energy"]), ""]
        ids = [i for ids in deckgen.lines(d).values() for i in ids]
        ids += sorted((i for i in d["cards"] if i not in deckgen.POKEMON), key=deckgen.name)
        out += [f"- {d['cards'][i]} {deckgen.name(i)} ({i})" for i in ids]
    out += ["", "## Top 10 against each other", "", "Row deck's win rate against the column deck.", "",
            "| | " + " | ".join(str(k) for k in range(1, len(top) + 1)) + " |", "|---" * (len(top) + 1) + "|"]
    for i in range(len(top)):
        out.append(f"| {i + 1} | " + " | ".join("-" if i == j else pct(m[i, j]) for j in range(len(top))) + " |")
    out += ["", "## Nash mix", "", f"The mix of the top 10 that none of them beats (value {value:+.3f}).", "",
            "| # | deck | weight |", "|---|---|---|"]
    out += [f"| {k + 1} | {top[k]['id']} | {w:.3f} |" for k, w in enumerate(weights) if w > 0.0005]
    (run / "report.md").write_text("\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--generations", type=int, default=30)
    ap.add_argument("--population", type=int, default=64)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--seed-decks", nargs="+", default=[], metavar="FILE")
    args = ap.parse_args()
    if args.population < 8:
        ap.error("--population needs 8 or more")
    run = args.out or ROOT / "results" / f"search-{time.strftime('%Y%m%d-%H%M%S')}"
    (run / "all").mkdir(parents=True, exist_ok=True)
    (run / "decks").mkdir(exist_ok=True)
    rng = random.Random()

    def entry(deck, id_):
        path = run / "all" / f"{id_}.txt"
        deckgen.write_deck(deck, path, id_)
        return {"id": id_, "deck": deck, "path": str(path), "key": deckgen.key(deck)}

    pop = []
    for k, f in enumerate(args.seed_decks[:args.population]):
        deck = deckgen.read_deck(f)
        if why := deckgen.problem(deck):
            ap.error(f"{f}: {why}")
        pop.append(entry(deck, f"seed-{k:02d}"))
    seen = {e["key"] for e in pop}
    while len(pop) < args.population:
        deck = deckgen.random_deck(rng)
        if deckgen.key(deck) not in seen:
            seen.add(deckgen.key(deck))
            pop.append(entry(deck, f"g000-{len(pop):02d}"))
    refs = [str(p) for p in REFERENCES]
    extra = [entry(deckgen.random_deck(rng), f"pool-{k:02d}") for k in range(POOL_RANDOM)]  # oldest first
    n_elites = args.population // 4

    with tempfile.TemporaryDirectory() as tmp:
        for gen in range(args.generations):
            start = time.time()
            ranking = evaluate(pop, refs + [e["path"] for e in extra], tmp)
            with (run / f"gen_{gen:03d}.csv").open("w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["id", "games", "winrate", "lo", "hi", "file"])
                for e in ranking:
                    w.writerow([e["id"], e["games"], *(f"{x:.4f}" for x in interval(e["score"], e["games"])), e["path"]])
            in_pool = {e["key"] for e in extra}
            for e in [e for e in ranking if e["key"] not in in_pool][:HALL_PER_GEN]:
                path = run / "decks" / f"hall-{e['id']}.txt"
                shutil.copy(e["path"], path)
                extra.append({**e, "path": str(path)})
            del extra[:max(0, len(refs) + len(extra) - POOL_MAX)]
            best = ranking[0]
            print(f"gen {gen:3d}: best {100 * rate(best):5.1f}%, median {100 * statistics.median(map(rate, ranking)):5.1f}%, "
                  f"{best['id']}: {deckgen.main_line(best['deck'])}; {time.time() - start:.1f} s", flush=True)
            if gen == args.generations - 1:
                break
            elites = diverse(ranking, n_elites)
            pop, seen = list(elites), {e["key"] for e in elites}
            while len(pop) < args.population:
                r = rng.random()
                if r < 0.5:
                    deck = deckgen.mutate(min(rng.sample(elites, min(3, len(elites))), key=elites.index)["deck"], rng)
                elif r < 0.8:
                    a, b = rng.sample(elites, 2)
                    deck = deckgen.crossover(a["deck"], b["deck"], rng)
                else:
                    deck = deckgen.random_deck(rng)
                if deckgen.key(deck) not in seen:
                    seen.add(deckgen.key(deck))
                    pop.append(entry(deck, f"g{gen + 1:03d}-{len(pop):02d}"))
        report(run, ranking, refs + [e["path"] for e in extra], args, tmp)
    print(f"report: {run / 'report.md'}")


if __name__ == "__main__":
    main()
