#!/usr/bin/env python3
"""Append translations for real card tooltip rows from card_tooltip_actual_report.tsv."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TSV = ROOT / "translations.tsv"
REPORT = ROOT / "catalog" / "card_tooltip_actual_report.tsv"

lines = TSV.read_text(encoding="utf-8-sig").splitlines()
cards = {}
for line in lines:
    p = line.split("\t")
    if len(p) >= 3 and p[0].startswith("CARD:"):
        item = p[0].split("/")[-1].strip().lower()
        cards[item] = p[2]
rows = []
for line in REPORT.read_text(encoding="utf-8").splitlines()[1:]:
    p = line.split("\t")
    if len(p) < 5 or p[0] != "MISSING_KEY": continue
    status, stat, item, key, source = p
    source = source.replace("\\n", "\n")
    name = cards.get(item)
    if not name:
        print("NO_CARD_NAME", item)
        continue
    if stat == "damage":
        tail = source.split("\n", 1)[1]
        tr = name + "\n" + tail.replace("damage", "伤害", 1)
    elif stat == "heals":
        tail = source.split("\n", 1)[1]
        tr = name + "\n" + tail.replace("heals", "治疗", 1)
    elif stat == "heals others":
        tail = source.split("\n", 1)[1]
        tr = name + "\n" + tail.replace("heals others", "治疗他人", 1)
    else:
        continue
    row = key + "\t" + source.replace("\n", "\\n") + "\t" + tr.replace("\n", "\\n")
    if row not in lines:
        rows.append(row)
    row_space = key + "\t" + source.replace("\n", " ") + "\t" + tr.replace("\n", " ")
    if row_space not in lines:
        rows.append(row_space)

if rows:
    if not TSV.read_text(encoding="utf-8-sig").endswith("\n"): lines.append("")
    lines.extend(rows)
    TSV.write_text("\n".join(lines) + "\n", encoding="utf-8")
print("added", len(rows))
for r in rows: print(r)
