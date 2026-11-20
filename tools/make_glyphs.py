#!/usr/bin/env python3
"""Render LXGW WenKai glyph PNGs used by VOIMA_ZH.

The game stores one PNG per character in its bitmap font directory.  We keep the
same convention: <character>.png, 200x200 RGBA, white ink on transparent.
"""
import argparse
import os
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

CELL = 200
FONT_SIZE = 172
ANCHOR = (100, 90)  # tuned against the original hand-drawn Latin cap height


def render(font_path: Path, chars: str, out_dir: Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    font = ImageFont.truetype(str(font_path), FONT_SIZE)
    written = 0
    invalid = set('<>:"/\\|?*')
    for ch in chars:
        if ch in "\r\n\t\0" or ord(ch) < 128:
            continue
        if ch in invalid or ord(ch) < 32:
            continue
        im = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.text(ANCHOR, ch, font=font, fill=(255, 255, 255, 255), anchor="mm",
               stroke_width=2, stroke_fill=(255, 255, 255, 255))
        if im.getbbox() is None:
            continue
        im.save(out_dir / f"{ch}.png", optimize=True)
        written += 1
    print(f"wrote {written} glyphs to {out_dir}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--chars", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    chars = Path(args.chars).read_text(encoding="utf-8")
    render(Path(args.font), chars, Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
