#!/usr/bin/env python3
"""Object-aware translation gap report.

Reads the runtime trace/untranslated log, joins it with the translation table,
and classifies each gap so the next batch does not turn into "translate every
English string again".
"""
from __future__ import annotations

import argparse
import fnmatch
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

PROMO_STATIC_PREFIXES = (
    "W:Window:promo|n:help us",
    "W:Window:promo|n:# people",
    "W:Window:promo|n:#€",
    "W:Window:promo|n:#%",
    "W:Window:promo|n:Be forever",
    "W:Window:promo|n:as one of",
    "W:Window:promo|n:believers",
    "W:Window:promo|n:by naming",
    "W:Window:promo|n:with a pre-order",
    "W:Window:promo|n:Instead of",
    "W:Window:promo|n:or rushing",
    "W:Window:promo|n:we are funding",
    "W:Window:promo|n:visit our website",
    "W:Window:promo|n:and...",
    "W:Window:promo|n:we also have",
    "W:Window:promo|n:monthly goal",
)


def unescape(s: str) -> str:
    return s.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")


def norm(s: str) -> str:
    return " ".join(unescape(s).replace("\n", " ").replace("\r", " ").split()).lower()


def load_tsv(path: Path):
    entries = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 3:
            continue
        entries.setdefault(cols[0], []).append((unescape(cols[1]), cols[2]))
    return entries


def load_trace(path: Path):
    rows = []
    if not path.exists():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        cols = raw.split("\t")
        if len(cols) >= 4 and cols[0] == "T":
            rows.append({"key": cols[1], "source": unescape(cols[2]), "translation": unescape(cols[3])})
    return rows


def load_untranslated(path: Path):
    rows = {}
    if not path.exists():
        return rows
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        cols = raw.split("\t")
        if len(cols) >= 5 and cols[0] == "U":
            try:
                count = int(cols[1])
            except ValueError:
                count = 1
            key, source = cols[2], unescape(cols[3])
            rows[(key, source)] = {"key": key, "source": source, "detail": cols[4], "count": count}
    return rows


def load_exemptions(path: Path):
    out = []
    if not path.exists():
        return out
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) >= 2:
            out.append((cols[0].strip(), cols[1].strip()))
    return out


def matches_exemption(key: str, source: str, exemptions):
    for pattern, reason in exemptions:
        if ("|" in pattern and (fnmatch.fnmatchcase(key, pattern) or key.startswith(pattern))) or            (not pattern.startswith("W:") and pattern in key) or            (pattern.lower() in source.lower()):
            return reason
    return None


def classify(key: str, source: str, detail: str, entries, exemptions, page_failures):
    exemption = matches_exemption(key, source, exemptions)
    if exemption:
        return "EXEMPT", exemption
    if key in page_failures:
        return "PAGE_FAILED", page_failures[key]
    if key.startswith("W:Window:names") or key.startswith("X:") and len(source) > 500:
        return "USER_CONTENT", "name/list surface"
    if len(source) > 500 or source.count("\n") > 12:
        return "USER_CONTENT", "long multi-line user content"
    if key.startswith("W:Window:promo|") and not any(key.startswith(p) for p in PROMO_STATIC_PREFIXES):
        # Backer-name buttons are static in structure but user-generated in content.
        return "USER_CONTENT", "promo backer name"
    if key.endswith(":KeybindButton|text_name") and re.fullmatch(r"[A-Z0-9 +_.-]{1,12}", source.strip()):
        return "KEYCAP", "key name, not a sentence"
    if key.startswith("LOAD:"):
        want = key.lower().rstrip(" .")
        for candidate in entries:
            if candidate.lower().rstrip(" .") == want:
                return "PATTERN_MISMATCH", f"table key exists but source did not match: {candidate}"
        return "MISSING_ENTRY", "loading stage"
    if key.startswith("X:"):
        return "UNOWNED", "no object context; add a resolver instead of a bare-text fallback"
    if key in entries:
        return "PATTERN_MISMATCH", "key exists; source pattern/case/newline/digits mismatch"
    # A near key often means a field/class/index mismatch.
    tail = key.split("|n:", 1)[-1].lower()
    for candidate in entries:
        if tail and tail in candidate.lower():
            return "KEY_MISMATCH", f"near key: {candidate}"
    return "MISSING_ENTRY", "add object key + source + translation"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tsv", default=str(ROOT / "translations" / "translations.tsv"))
    ap.add_argument("--trace", default=str(ROOT / "logs" / "test_trace.tsv"))
    ap.add_argument("--untranslated", default=str(ROOT / "logs" / "untranslated.log"))
    ap.add_argument("--plugin-log", default=str(ROOT / "logs" / "plugin.log"))
    ap.add_argument("--exemptions", default=str(ROOT / "tests" / "exemptions.tsv"))
    ap.add_argument("--out", default=str(ROOT / "reports"))
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    entries = load_tsv(Path(args.tsv))
    trace = load_trace(Path(args.trace))
    untranslated = load_untranslated(Path(args.untranslated))
    exemptions = load_exemptions(Path(args.exemptions))
    page_failures = {}
    log_path = Path(args.plugin_log)
    if log_path.exists():
        for line in log_path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = re.search(r"page (\d+) ctor threw", line)
            if m:
                page_failures[f"PAGE:{m.group(1)}"] = line.strip()

    gaps = []
    for (key, source), row in untranslated.items():
        kind, hint = classify(key, source, row["detail"], entries, exemptions, page_failures)
        gaps.append({"type": kind, "key": key, "source": source, "detail": row["detail"],
                     "count": row["count"], "hint": hint})
    for page_key, detail in page_failures.items():
        gaps.append({"type": "PAGE_FAILED", "key": page_key, "source": "font page",
                     "detail": detail, "count": 1, "hint": "inspect duplicate punctuation / page budget / GC roots"})

    translated = len({r["key"] for r in trace})
    unique_gaps = len(gaps)
    non_exempt = [g for g in gaps if g["type"] != "EXEMPT"]
    coverage = 0.0
    if translated + len(non_exempt):
        coverage = translated / (translated + len(non_exempt))

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "gap-report.json").write_text(json.dumps({
        "translated_keys": translated,
        "unique_gaps": unique_gaps,
        "non_exempt_gaps": len(non_exempt),
        "object_coverage": round(coverage, 4),
        "gaps": gaps,
    }, ensure_ascii=False, indent=1), encoding="utf-8")

    lines = ["# Translation Gap Report", "",
             f"- translated keys (trace): {translated}",
             f"- unique gaps: {unique_gaps}",
             f"- non-exempt gaps: {len(non_exempt)}",
             f"- object coverage: {coverage:.2%}", ""]
    if not gaps:
        lines.append("No untranslated entries in the current logs.")
    else:
        lines += ["| type | count | object key | source | hint |", "|---|---:|---|---|---|"]
        for g in sorted(gaps, key=lambda x: (x["type"], x["key"])):
            lines.append(f"| {g['type']} | {g['count']} | {g['key']} | {g['source'].replace(chr(10), ' / ')} | {g['hint']} |")
    (out / "gap-report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("\n".join(lines[:8]))
    if args.strict and non_exempt:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
