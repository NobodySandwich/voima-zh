import re
from pathlib import Path

src = Path(__file__).resolve().parents[1] / "src" / "plugin.cpp"
text = src.read_text(encoding="utf-8")

assert "void ApplyPageFont(void* self, int page)" in text, "missing ApplyPageFont helper"
block = re.search(r"if \(cit != speechCache\.end\(\).*?return;\s*}", text, re.S)
assert block, "missing speech cache hit block"
body = block.group(0)
assert "ApplyPageFont" in body, "speech cache hit does not apply page font"
assert body.index("ApplyPageFont") < body.index("g_origBuildRotated"), "font not applied before BuildRotated"
print("font invariant OK")
