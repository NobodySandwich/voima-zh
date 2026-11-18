import unittest
from pathlib import Path
from PIL import Image, ImageFont

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / "Font"
TTF = ROOT / "assets" / "LXGWWenKaiScreen.ttf"
GAME_PROVIDED = {"€", "°", "∞"}


def translation_chars():
    chars = set()
    for raw in (ROOT / "translations" / "translations.tsv").read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 3:
            continue
        text = cols[2].replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")
        chars.update(c for c in text if ord(c) >= 128)
    return chars


class FontTest(unittest.TestCase):
    def test_wenkai_font(self):
        self.assertTrue(TTF.exists())
        name = ImageFont.truetype(str(TTF), 32).getname()
        self.assertIn("LXGW WenKai", " ".join(name))
        self.assertGreater((ROOT / "assets" / "OFL.txt").stat().st_size, 1000)

    def test_glyph_coverage(self):
        glyphs = (FONT / "glyphs.txt").read_text(encoding="utf-8")
        required = translation_chars() - GAME_PROVIDED
        missing = sorted(c for c in required if c not in glyphs)
        self.assertEqual(missing, [], f"missing glyph characters: {missing[:30]}")
        no_png = sorted(c for c in required if not (FONT / f"{c}.png").exists())
        self.assertEqual(no_png, [], f"missing PNGs: {no_png[:30]}")

    def test_glyph_metrics(self):
        sample = list(translation_chars() - GAME_PROVIDED)[:20]
        for c in sample:
            im = Image.open(FONT / f"{c}.png").convert("RGBA")
            self.assertEqual(im.size, (200, 200), c)
            self.assertIsNotNone(im.getbbox(), c)

    def test_page_budget(self):
        # One page also carries the base ASCII set (~36 glyphs), so the CJK
        # budget is 200; the game is budgeted for at most 256 pages.
        rows = []
        for raw in (ROOT / "translations" / "translations.tsv").read_text(encoding="utf-8").splitlines():
            if not raw or raw.startswith("#"):
                continue
            cols = raw.split("\t")
            if len(cols) >= 3:
                rows.append(cols[2])
        pages = []
        for text in rows:
            chars = []
            for c in text.replace("\\n", "\n"):
                if ord(c) >= 128 and c not in GAME_PROVIDED and c not in chars:
                    chars.append(c)
            if not chars:
                continue
            slot = None
            for i, page in enumerate(pages):
                if all(c in page for c in chars):
                    slot = i
                    break
            if slot is None:
                for i, page in enumerate(pages):
                    if len(page) + len(chars) <= 200:
                        slot = i
                        break
            if slot is None:
                pages.append([])
                slot = len(pages) - 1
            for c in chars:
                if c not in pages[slot]:
                    pages[slot].append(c)
        self.assertLessEqual(len(pages), 256)
        self.assertTrue(all(len(p) <= 200 for p in pages))


if __name__ == "__main__":
    unittest.main()
