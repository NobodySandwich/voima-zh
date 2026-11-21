#!/usr/bin/env python3
"""Watch VOIMA trace logs and capture a screenshot for every new text/object.

Run this while cycling block types in the game:
  python tools/block_watch.py

Screenshots and an index are written under ../VOIMA_tools/blocks.
Existing log lines are ignored; only entries that appear after startup are captured.
"""
from __future__ import annotations

import datetime as _dt
import re
import sys
import time
from pathlib import Path

from PIL import ImageGrab

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
ROOT = Path(__file__).resolve().parent.parent
LOG_DIR = ROOT / "logs"
OUT_DIR = ROOT.parent / "VOIMA_tools" / "blocks"
OUT_DIR.mkdir(parents=True, exist_ok=True)
INDEX = LOG_DIR / "block_capture.tsv"


def read_lines(path: Path) -> list[str]:
    try:
        return path.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    except FileNotFoundError:
        return []


def slug(text: str) -> str:
    text = re.sub(r"[^0-9A-Za-z\u4e00-\u9fff]+", "_", text)
    return text.strip("_")[:60] or "entry"


def capture(label: str, key: str, source: str) -> None:
    stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
    path = OUT_DIR / f"{stamp}_{slug(label)}.png"
    ImageGrab.grab().save(path)
    with INDEX.open("a", encoding="utf-8") as f:
        f.write(f"{stamp}\t{key}\t{source}\t{path}\n")
    print(f"captured {path.name}  key={key}  source={source}", flush=True)


def main() -> None:
    # Ignore existing history.
    trace_seen = set(read_lines(LOG_DIR / "test_trace.tsv"))
    untranslated_seen = set(read_lines(LOG_DIR / "untranslated.log"))
    print("block watcher ready; show one block type at a time in game", flush=True)

    while True:
        time.sleep(0.4)
        for raw in read_lines(LOG_DIR / "test_trace.tsv"):
            if raw in trace_seen:
                continue
            trace_seen.add(raw)
            parts = raw.split("\t")
            if len(parts) < 2:
                continue
            key = parts[1]
            source = parts[2] if len(parts) > 2 else ""
            if not key or key.startswith("#"):
                continue
            # Skip repeated chrome/noise; keep object keys and tooltips.
            if not (key.startswith("TIP:") or key.startswith("W:") or key.startswith("CARD:") or key.startswith("X:")):
                continue
            capture("trace", key, source)

        for raw in read_lines(LOG_DIR / "untranslated.log"):
            if raw in untranslated_seen:
                continue
            untranslated_seen.add(raw)
            parts = raw.split("\t")
            if len(parts) < 3:
                continue
            key = parts[2]
            source = parts[3] if len(parts) > 3 else ""
            if not (key.startswith("TIP:") or key.startswith("W:") or key.startswith("CARD:") or key.startswith("X:")):
                continue
            capture("untranslated", key, source)


if __name__ == "__main__":
    main()
