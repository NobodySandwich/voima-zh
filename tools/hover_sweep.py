"""Hover a grid of client coordinates and report new translation activity.

Usage: python tools/hover_sweep.py --x0 380 --y0 100 --x1 900 --y1 600 --step 70
The game must already be in the target screen and use VOIMA_ZH --test.
"""
import argparse
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / "logs"


def read(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--x0", type=int, default=380)
    ap.add_argument("--y0", type=int, default=100)
    ap.add_argument("--x1", type=int, default=900)
    ap.add_argument("--y1", type=int, default=600)
    ap.add_argument("--step", type=int, default=70)
    ap.add_argument("--delay", type=float, default=0.7)
    args = ap.parse_args()

    seen_trace = set(read(LOGS / "test_trace.tsv"))
    seen_gaps = set(read(LOGS / "untranslated.log"))
    new = []
    points = [(x, y) for y in range(args.y0, args.y1 + 1, args.step)
                      for x in range(args.x0, args.x1 + 1, args.step)]
    for i, (x, y) in enumerate(points):
        (LOGS / "command.txt").write_text(f"hover {x} {y}\n", encoding="ascii")
        time.sleep(args.delay)
        for line in read(LOGS / "test_trace.tsv"):
            if line not in seen_trace and ("TIP:" in line or "CARD:" in line or "X:TextureText|" in line):
                seen_trace.add(line)
                new.append("TRACE " + line)
        for line in read(LOGS / "untranslated.log"):
            if line not in seen_gaps:
                seen_gaps.add(line)
                new.append("GAP " + line)
        if (i + 1) % 10 == 0:
            print(f"swept {i + 1}/{len(points)} new={len(new)}", flush=True)
    print("--- new entries ---")
    for line in new:
        print(line)
    print(f"total points={len(points)} new={len(new)}")


if __name__ == "__main__":
    main()
