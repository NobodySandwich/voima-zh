#!/usr/bin/env python3
"""Filtered static report for multiline item tooltips.

Only item categories that can actually produce the stat tooltip are scanned:
  damage        -> CARD:tools/...
  heals/heals others -> CARD:healing/...
Health/entity debug literals are deliberately excluded.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LITERALS = Path(r"G:\VOIMA_tools\dump\stringliteral.json")
TSV = ROOT / "translations.tsv"
OUT = ROOT / "catalog" / "multiline_tooltip_report.tsv"


def load_tsv():
    keys = set()
    key_sources = {}
    card_paths = {}
    for line in TSV.read_text(encoding="utf-8-sig").splitlines():
        p = line.split("\t")
        if len(p) < 3:
            continue
        key, source = p[0], p[1]
        keys.add(key)
        key_sources.setdefault(key, set()).add(source)
        if key.startswith("CARD:"):
            path = key[5:]
            item = path.split("/")[-1].strip()
            if item:
                card_paths.setdefault(item, set()).add(path)
    return keys, key_sources, card_paths


def templates():
    data = json.loads(LITERALS.read_text(encoding="utf-8"))
    found = set()
    for e in data:
        v = e.get("value") if isinstance(e, dict) else e
        if not isinstance(v, str) or "\n" not in v:
            continue
        label = v.split("\n")[-1].strip()
        if label.endswith("=") and label.startswith(("damage", "heals")):
            found.add(label[:-1].strip())
    return sorted(found)


def category_ok(item, label, card_paths):
    paths = " ".join(card_paths.get(item, set())).lower()
    if label == "damage":
        return paths.startswith("tools/") or "/tools/" in paths
    if label.startswith("heals"):
        return paths.startswith("healing/") or "/healing/" in paths
    return False


def has_base_tip(item, keys):
    return ("TIP:" + item) in keys or any(k.startswith("TIP:" + item + " ") for k in keys)


def main():
    keys, sources, cards = load_tsv()
    ts = templates()
    rows = []
    for item, label in sorted((i, t) for i in cards for t in ts):
        if not category_ok(item, label, cards):
            continue
        key = "TIP:" + item + " " + label + " = {0}"
        source = item + "\n" + label + " = {0}"
        if not has_base_tip(item, keys):
            status = "NO_BASE_TIP"
        elif key not in keys:
            status = "MISSING_KEY"
        else:
            srcs = sources.get(key, set())
            if any("{0}" in s or "{*}" in s for s in srcs):
                status = "PATTERN_COVERED"
            elif any(s == source for s in srcs):
                status = "EXACT_ONLY"
            else:
                status = "SOURCE_MISMATCH"
        rows.append((status, label, item, key, source))

    OUT.parent.mkdir(exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="") as f:
        f.write("status\tstat\titem\tkey\tsource\n")
        for r in sorted(rows):
            f.write("\t".join(x.replace("\t", " ").replace("\n", "\\n") for x in r) + "\n")
    counts = {}
    for r in rows:
        counts[r[0]] = counts.get(r[0], 0) + 1
    print("templates", ts)
    print("rows", len(rows))
    for k, v in sorted(counts.items()):
        print(k, v)
    print(OUT)


if __name__ == "__main__":
    main()
