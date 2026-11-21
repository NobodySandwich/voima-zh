#!/usr/bin/env python3
"""Capture a screenshot for each newly observed HUD action.

Run while playing:
  python tools/hud_watch.py

The plugin appends unique HUD actions to logs/hud_actions.tsv.
Screenshots and an index are written under ../VOIMA_tools/hud.
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
LOG = ROOT / "logs" / "hud_actions.tsv"
OUT = ROOT.parent / "VOIMA_tools" / "hud"
OUT.mkdir(parents=True, exist_ok=True)
INDEX = ROOT / "logs" / "hud_capture.tsv"


def read_lines() -> list[str]:
    try:
        return LOG.read_text(encoding="utf-8-sig", errors="replace").splitlines()
    except FileNotFoundError:
        return []


def slug(text: str) -> str:
    text = re.sub(r"[^0-9A-Za-z\u4e00-\u9fff]+", "_", text)
    return text.strip("_")[:60] or "hud"


def main() -> None:
    seen = set(read_lines())
    print("HUD watcher ready; trigger different HUD actions in game", flush=True)
    while True:
        time.sleep(0.5)
        for raw in read_lines():
            if raw in seen:
                continue
            seen.add(raw)
            cols = raw.split("\t")
            action = cols[0] if cols else ""
            translated = cols[1] if len(cols) > 1 else ""
            stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S_%f")[:-3]
            path = OUT / f"{stamp}_{slug(action)}.png"
            ImageGrab.grab().save(path)
            with INDEX.open("a", encoding="utf-8") as f:
                f.write(f"{stamp}\t{action}\t{translated}\t{path}\n")
            print(f"captured {path.name} action={action} translated={translated}", flush=True)


if __name__ == "__main__":
    main()
