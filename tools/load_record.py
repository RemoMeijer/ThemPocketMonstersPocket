#!/usr/bin/env python3
"""Loads a tcgp-record recording (record_format 2, numpy only), checks it against meta.json and prints a summary.

    python3 tools/load_record.py DIR

As a module:
    rec = load(DIR)                  # Recording: meta, arrays (read-only memmaps), cards (float32 card table)
    b = batch(rec, indices)          # padded float32/int64 arrays for those decisions, ready for a network
"""
import json
import os
import sys

import numpy as np

RECORD_FORMAT = 2


class Recording:
    def __init__(self, meta, arrays, cards):
        self.meta, self.arrays, self.cards = meta, arrays, cards
        self.offsets = arrays["action_offsets"]

    def __len__(self):
        return self.meta["decisions"]


def load(path):
    with open(os.path.join(path, "meta.json")) as f:
        meta = json.load(f)
    if meta.get("record_format") != RECORD_FORMAT:
        raise ValueError(f"record_format {meta.get('record_format')}, this loader reads {RECORD_FORMAT}")
    arrays = {}
    for name, a in list(meta["arrays"].items()) + [("cards", meta["cards"])]:
        file = os.path.join(path, a["file"])
        dtype, shape = np.dtype(a["dtype"]), tuple(a["shape"])
        want = int(np.prod(shape)) * dtype.itemsize
        if os.path.getsize(file) != want:
            raise ValueError(f"{a['file']}: {os.path.getsize(file)} bytes, meta.json says {want}")
        arrays[name] = np.memmap(file, dtype=dtype, mode="r", shape=shape) if want else np.zeros(shape, dtype)
    return Recording(meta, arrays, np.asarray(arrays.pop("cards")))


def batch(rec, indices):
    """The decisions `indices` as padded arrays: tokens [B, T, ...], actions [B, A, ...] (A = meta sizes.actions),
    masks as bool, floats as float32, ints as int64; padding is 0 (pointers -1)."""
    a, A = rec.arrays, rec.meta["sizes"]["actions"]
    idx = np.asarray(indices, dtype=np.int64)
    B = len(idx)
    out = {name: np.asarray(a[name][idx]).astype(np.int64) for name in ("token_type", "token_card", "token_tool")}
    out["token_float"] = np.asarray(a["token_float"][idx], dtype=np.float32)
    out["token_mask"] = out["token_type"] != 0
    for name in ("chosen", "player", "game", "legal_actions"):
        out[name] = np.asarray(a[name][idx]).astype(np.int64)
    out["result"] = np.asarray(a["result"][idx], dtype=np.float32)

    # Ragged → padded: action row src goes to (decision b, slot pos).
    start, counts = rec.offsets[idx], rec.offsets[idx + 1] - rec.offsets[idx]
    b = np.repeat(np.arange(B), counts)
    pos = np.arange(counts.sum()) - np.repeat(np.cumsum(counts) - counts, counts)
    src = np.repeat(start, counts) + pos
    for name, fill in (("action_kind", 0), ("action_card", 0), ("action_ptr", -1), ("action_ptr2", -1)):
        out[name] = np.full((B, A), fill, dtype=np.int64)
        out[name][b, pos] = a[name][src]
    out["action_float"] = np.zeros((B, A, rec.meta["sizes"]["action_floats"]), dtype=np.float32)
    out["action_float"][b, pos] = a["action_float"][src]
    out["action_mask"] = np.zeros((B, A), dtype=bool)
    out["action_mask"][b, pos] = True
    return out


def check(rec):
    """The problems found, as text; empty when the recording is sound."""
    a, sizes, cards = rec.arrays, rec.meta["sizes"], rec.cards
    n, problems = len(rec), []
    if cards.shape != (len(rec.meta["card_ids"]) + 1, sizes["card_fixed_features"] + len(rec.meta["vocabulary"])):
        problems.append(f"card table {cards.shape} doesn't match card_ids and vocabulary")
    counts = np.diff(rec.offsets)
    if rec.offsets[0] != 0 or rec.offsets[-1] != rec.meta["actions"] or (counts < 2).any():
        problems.append("action_offsets don't cover the actions, or a decision without a choice")
    if not (counts == np.minimum(a["legal_actions"], sizes["actions"])).all():
        problems.append("stored actions don't match legal_actions")
    if not ((a["chosen"] >= 0) & (a["chosen"] < counts)).all():
        problems.append("a chosen index outside the legal actions")
    if not (a["token_type"][:, 0] == 1).all():
        problems.append("a decision without its global token")
    if (a["action_kind"] < 1).any():
        problems.append("a stored action without a kind")
    decision = np.repeat(np.arange(n), counts)
    for ptr in ("action_ptr", "action_ptr2"):  # a pointer lands on a token that is there
        p = np.asarray(a[ptr], dtype=np.int64)
        on = p >= 0
        if (p >= sizes["tokens"]).any() or not (a["token_type"][decision[on], p[on]] != 0).all():
            problems.append(f"{ptr} points at an empty token")
    if (a["token_card"] >= cards.shape[0]).any() or (a["action_card"] >= cards.shape[0]).any():
        problems.append("a card index outside the card table")

    # batch() on a sample agrees with the files.
    sample = np.random.default_rng(0).choice(n, size=min(n, 256), replace=False)
    b = batch(rec, sample)
    if not ((b["action_mask"].sum(1) == counts[sample]).all() and b["action_mask"][np.arange(len(sample)), b["chosen"]].all()
            and (b["token_mask"] == (a["token_type"][sample] != 0)).all()
            and np.array_equal(b["action_float"][b["action_mask"]], a["action_float"][np.concatenate(
                [np.arange(rec.offsets[i], rec.offsets[i + 1]) for i in sample])].astype(np.float32))):
        problems.append("batch() doesn't match the files")
    return problems


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    rec = load(sys.argv[1])
    a, meta, n = rec.arrays, rec.meta, len(rec)
    counts = np.diff(rec.offsets)
    # One result per (game, player): wins, losses and draws seen from each side.
    _, first = np.unique(a["game"].astype(np.int64) * 2 + a["player"], return_index=True)
    results = a["result"][first]
    games = len(np.unique(a["game"]))
    size = sum(os.path.getsize(os.path.join(sys.argv[1], x["file"])) for x in meta["arrays"].values())
    print(f"encoding v{meta['encoding_version']}, format {meta['record_format']}: {n} decisions in {games} games "
          f"(meta: {meta['games']}), {n / max(games, 1):.1f} per game, {size / max(n, 1):.0f} bytes each; "
          f"legal actions avg {counts.mean():.2f}, max {meta['max_legal_actions']}")
    print(f"results per (game, player): {(results == 1).sum()} wins, {(results == -1).sum()} losses, "
          f"{(results == 0).sum()} draws; card table {rec.cards.shape[0]} x {rec.cards.shape[1]} "
          f"({len(meta['vocabulary'])} script words)")
    problems = check(rec)
    for p in problems:
        print("problem:", p)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
