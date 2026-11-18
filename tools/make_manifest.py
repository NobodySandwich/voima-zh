#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT.parent
files = ["VOIMA.exe", "GameAssembly.dll", "VOIMA_Data/il2cpp_data/Metadata/global-metadata.dat"]
out = {f: hashlib.sha256((GAME / f).read_bytes()).hexdigest() for f in files}
(ROOT / "tests" / "game_manifest.json").write_text(json.dumps(out, indent=1) + "\n")
print(json.dumps(out, indent=1))
