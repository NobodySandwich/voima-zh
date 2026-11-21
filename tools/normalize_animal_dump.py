#!/usr/bin/env python3
"""Normalize the runtime AnimalType dump into stable ANIMAL keys.

Input:  catalog/animal_messages.runtime.tsv (raw path/message rows)
Output: catalog/animal_messages.tsv (key/source/path)
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
RAW = ROOT / "catalog" / "animal_messages.runtime.tsv"
OUT = ROOT / "catalog" / "animal_messages.tsv"
TABLE = ROOT / "translations" / "translations.tsv"


def normalize_path(path: str) -> str:
    p = path.replace("\\", "/").lower()
    if "/content/" in p:
        p = p.split("/content/", 1)[1]
    if p.endswith(".txt"):
        p = p[:-4]
    return re.sub(r"/+", "/", p).strip("/")


def main() -> int:
    rows = []
    for line in RAW.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) < 6:
            continue
        _, field, index, path, _name, message = cols[:6]
        norm = normalize_path(path)
        rows.append((f"ANIMAL:{norm}:{field}:{index}", message, norm))

    translations = {}
    for line in TABLE.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        cols = line.split("\t")
        if len(cols) >= 3:
            translations[cols[0]] = cols[2]

    with OUT.open("w", encoding="utf-8") as f:
        f.write("# key\tsource\tpath\n")
        for key, source, norm in rows:
            f.write(f"{key}\t{source}\t{norm}\n")
    missing = sorted({key for key, _, _ in rows if key not in translations})
    print(f"wrote {len(rows)} animal messages to {OUT}")
    if missing:
        print("missing translations:", len(missing))
        for key in missing[:20]:
            print(" ", key)
        return 1
    print("all runtime animal messages have translations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
