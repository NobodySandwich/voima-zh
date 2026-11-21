#!/usr/bin/env python3
"""Summarize and sanity-check one runtime object dump."""
from __future__ import annotations

import argparse
import csv
import re
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CJK = re.compile(r"[\u3400-\u9fff]")


def unescape(s: str) -> str:
    return s.replace("\\\\n", "\\n").replace("\\\\t", "\\t").replace("\\\\\\\\", "\\\\")


def load_tsv(path: Path):
    keys = set()
    sources = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) >= 2:
            keys.add((cols[0], unescape(cols[1])))
            sources.add(unescape(cols[1]))
    return keys, sources


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--tsv", default=str(ROOT / "translations" / "translations.tsv"))
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    keys, sources = load_tsv(Path(args.tsv))
    rows = []
    with open(args.dump, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f, delimiter="\t"):
            rows.append(row)

    windows = Counter(r.get("window", "") for r in rows)
    kinds = Counter(r.get("kind", "") for r in rows)
    data_rows = [r for r in rows if r.get("kind") != "WINDOW"]

    def is_user_row(r):
        source = r.get("source", "")
        key = r.get("primary_key", "")
        if r.get("window") == "names" or len(source) > 500:
            return True
        if r.get("window") == "promo" and key not in keys and r.get("translated") != "1":
            return True
        return False

    translated = sum(1 for r in data_rows if r.get("translated") == "1")
    user_rows = [r for r in data_rows if is_user_row(r)]
    missing = [r for r in data_rows if r.get("translated") != "1" and not is_user_row(r)]
    effective_rows = len(data_rows) - len(user_rows)
    effective = translated / effective_rows if effective_rows else 0.0
    unowned = [r for r in data_rows if r.get("primary_key", "").startswith("X:")]
    exact = sum(1 for r in data_rows if (r.get("primary_key", ""), unescape(r.get("source", ""))) in keys)
    cjk_source = [r for r in data_rows if CJK.search(r.get("source", ""))
                  and r.get("window") != "names" and len(r.get("source", "")) <= 500
                  and r.get("translated") != "1"]
    large = [r for r in data_rows if len(r.get("source", "")) > 500]
    non_exact_translated = [r for r in data_rows if r.get("translated") == "1"
                            and (r.get("primary_key", ""), unescape(r.get("source", ""))) not in keys]

    flags = []
    for r in data_rows:
        key = r.get("primary_key", "")
        source = r.get("source", "")
        if is_user_row(r):
            flags.append(("USER_CONTENT", key, source[:80]))
        elif r.get("translated") != "1":
            flags.append(("UNTRANSLATED", key, source[:80]))
        if key.startswith("X:"):
            flags.append(("UNOWNED", key, source[:80]))
        if CJK.search(source) and not is_user_row(r):
            flags.append(("SOURCE_CHINESE", key, source[:80]))

    lines = [
        f"# Object dump report: {args.dump}",
        "",
        f"- rows: {len(rows)} (data rows: {len(data_rows)})",
        f"- kinds: {dict(kinds)}",
        f"- windows: {dict(windows)}",
        f"- translated: {translated}/{len(data_rows)} (effective non-user: {effective:.1%})",
        f"- user-content rows: {len(user_rows)}",
        f"- exact key+source in TSV: {exact}/{len(data_rows)}",
        f"- translated via pattern/normalization: {len(non_exact_translated)}",
        f"- unowned X: keys: {len(unowned)}",
        f"- source contains Chinese: {len(cjk_source)}",
        f"- source > 500 chars: {len(large)}",
        "",
        "## Untranslated rows",
        "",
    ]
    if not missing:
        lines.append("none")
    else:
        for r in missing:
            lines.append(f"- {r.get('kind')} | {r.get('window')} | {r.get('button')} | "
                         f"{r.get('primary_key')} | source={r.get('source','')[:120]!r} | detail={r.get('detail','')[:120]}")
    lines += ["", "## Translated via pattern/normalization", ""]
    if not non_exact_translated:
        lines.append("none")
    else:
        for r in non_exact_translated:
            lines.append(f"- {r.get('primary_key')} | source={r.get('source','')!r} | result={r.get('result','')!r}")
    lines += ["", "## Flags", ""]
    if not flags:
        lines.append("none")
    else:
        for kind, key, source in flags:
            lines.append(f"- {kind}: {key} | {source!r}")
    text = "\n".join(lines) + "\n"
    if args.out:
        out = Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text, encoding="utf-8")
    print(f"rows={len(rows)} translated={translated} missing={len(missing)} flags={len(flags)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
