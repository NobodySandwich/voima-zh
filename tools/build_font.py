#!/usr/bin/env python3
"""Generate the CJK glyph set from the translation table using LXGW WenKai Screen."""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from make_glyphs import render

ROOT = Path(__file__).resolve().parents[1]


def unescape(s: str) -> str:
    out = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "n":
                out.append("\n")
                i += 2
                continue
            if n == "t":
                out.append("\t")
                i += 2
                continue
            if n == "\\":
                out.append("\\")
                i += 2
                continue
        out.append(s[i])
        i += 1
    return "".join(out)


# Supplied by the original VOIMA bitmap font; do not create a WenKai copy.
GAME_PROVIDED = {"€", "°", "∞"}


def collect_chars(tsv: Path) -> list[str]:
    chars: set[str] = set()
    for raw in tsv.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 3:
            continue
        for ch in unescape(cols[2]):
            if ord(ch) >= 128 and ch not in GAME_PROVIDED:
                chars.add(ch)
    return sorted(chars)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=str(ROOT / "assets" / "LXGWWenKaiScreen.ttf"))
    ap.add_argument("--translations", default=str(ROOT / "translations" / "translations.tsv"))
    ap.add_argument("--out", default=str(ROOT / "Font"))
    ap.add_argument("--all-cjk", action="store_true")
    args = ap.parse_args()

    chars = collect_chars(Path(args.translations))
    common = ROOT / "assets" / "common_chinese.txt"
    if common.exists():
        chars = sorted(set(chars) | {ch for ch in common.read_text(encoding="utf-8") if ord(ch) >= 128 and ch not in GAME_PROVIDED})
    if args.all_cjk:
        chars = sorted(set(chars) | {chr(c) for c in range(0x4E00, 0xA000)})
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "glyphs.txt").write_text("".join(chars), encoding="utf-8")
    render(Path(args.font), "".join(chars), out)
    print(f"glyph chars: {len(chars)} -> {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
