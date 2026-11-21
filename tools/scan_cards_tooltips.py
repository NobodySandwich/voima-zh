#!/usr/bin/env python3
"""Generate real item tooltip candidates from the shipped card text configs."""
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ZIP = Path(r"G:\VOIMA 2.2x2 Windows - Release\Content Updates.zip")
TSV = ROOT / "translations.tsv"
OUT = ROOT / "catalog" / "card_tooltip_actual_report.tsv"


def load_tsv():
    keys = set(); sources = {}
    for line in TSV.read_text(encoding="utf-8-sig").splitlines():
        p = line.split("\t")
        if len(p) < 3: continue
        keys.add(p[0]); sources.setdefault(p[0], set()).add(p[1].replace("\\n", "\n"))
    return keys, sources


def parse_card(text):
    fields = {}
    lines = text.splitlines()
    if not any(x.strip().upper() == "CARD" for x in lines[:3]):
        return None
    for line in lines:
        if "=" not in line: continue
        k, v = line.split("=", 1)
        fields[k.strip().lower()] = v.strip()
    def num(k):
        try: return float(fields.get(k, "0").rstrip("%"))
        except: return 0.0
    return num("damage"), num("heal on use amount"), num("health")


def main():
    keys, sources = load_tsv()
    rows = []
    z = zipfile.ZipFile(ZIP)
    for n in z.namelist():
        if not n.lower().endswith(".txt"): continue
        try: text = z.read(n).decode("utf-8", "ignore")
        except: continue
        rel = n.split("Karhula/Content/", 1)[-1].lower()
        if not rel.startswith(("tools/", "healing/", "weapons/", "blocks/", "items/")):
            continue
        parsed = parse_card(text)
        if not parsed: continue
        damage, heal, health = parsed
        item = n.rsplit("/", 1)[-1][:-4].strip().lower()
        candidates = []
        if damage > 0:
            candidates.append(("damage", int(damage), f"damage = {int(damage)}"))
        elif damage < 0:
            candidates.append(("heals others", int(-damage), f"heals others = {int(-damage)}"))
        if heal > 0 and health > 0:
            pct = int(round(heal / health * 100))
            candidates.append(("heals", pct, f"heals {pct}%"))
        for stat, value, tail in candidates:
            key = f"TIP:{item} {tail.split(' = ')[0]} = {value}" if "=" in tail else f"TIP:{item} {tail}"
            src = f"{item}\n{tail}"
            if key not in keys:
                status = "MISSING_KEY"
            elif src in sources.get(key, set()):
                status = "EXACT"
            elif any("{0}" in s or "{*}" in s for s in sources.get(key, set())):
                status = "PATTERN_COVERED"
            else:
                status = "SOURCE_MISMATCH"
            rows.append((status, stat, item, key, src))
    OUT.parent.mkdir(exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="") as f:
        f.write("status\tstat\titem\tkey\tsource\n")
        for r in sorted(rows): f.write("\t".join(x.replace("\t"," ").replace("\n","\\n") for x in r)+"\n")
    counts = {}
    for r in rows: counts[r[0]] = counts.get(r[0], 0) + 1
    print("rows", len(rows))
    for k,v in sorted(counts.items()): print(k,v)
    print(OUT)

if __name__ == "__main__":
    main()
