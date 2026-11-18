import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TSV = ROOT / "translations" / "translations.tsv"


def norm(text):
    return text.replace(chr(92) + "n", " ").replace(chr(10), " ")


def rows():
    out = []
    for raw in TSV.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) >= 3:
            out.append(tuple(cols[:3]))
    return out


class SettingsCoverageTest(unittest.TestCase):
    def setUp(self):
        self.rows = rows()
        self.by_key = {(k, s): z for k, s, z in self.rows}

    def test_headers_and_tabs(self):
        headers = ["audio settings", "video settings", "graphics settings", "gameplay settings"]
        for h in headers:
            self.assertIn((f"W:Window:{h}|header_text", h), self.by_key)
            self.assertIn((f"W:Window:tabs|n:{h}:Button|text_name", h), self.by_key)
        self.assertIn(("W:Window:tabs|n:exit settings:Button|text_name", "exit settings"), self.by_key)

    def test_all_visible_setting_labels(self):
        labels = {
            "audio settings": ["sound volume", "music volume", "reverbs"],
            "video settings": ["menu color", "menu scale", "resolution", "game pixels", "menu pixels",
                              "color dithering", "fps limit", "vsync", "fullscreen"],
            "graphics settings": ["fov", "view distance", "shadow quality", "bloom", "extra shadows",
                                 "texture depth", "layered cards", "thick cards", "disable layers in blocks",
                                 "disable clouds", "disable reflections\nand waves", "smooth textures",
                                 "texture distance", "texture resolution", "fog amount", "fadeout amount",
                                 "shadow distance", "particles"],
            "gameplay settings": ["right click\nto build blocks", "toggle run", "view bobbing",
                                 "haze effect", "show tooltips\nfor controls"],
            "controls": ["look speed", "reverse x", "reverse y", "back to menu", "controls list", "reset controls"],
        }
        for window, keys in labels.items():
            for label in keys:
                hit = False
                for (k, s), z in self.by_key.items():
                    if k.startswith(f"W:Window:{window}|n:") and norm(s) == norm(label):
                        hit = True
                        break
                self.assertTrue(hit, f"{window}: {label}")

    def test_promo_counters(self):
        cases = {
            "W:Window:promo|n:# people have already pre-ordered!:TextButton|text_name": ("{0} people have already pre-ordered!", "已有 {0} 人预购"),
            "W:Window:promo|n:#€ raised:Button|text_name": ("{0}€ raised", "众筹进度 {0}€"),
            "W:Window:promo|n:#% this months rent:Button|text_name": ("{0}% this months rent", "本月房租 {0}%"),
        }
        for key, (src, zh) in cases.items():
            self.assertIn((key, src), self.by_key)
            self.assertEqual(self.by_key[(key, src)], zh)

    def test_headphone_option(self):
        key = "W:Window:audio settings|n:#d sound:BoolButton|text_name"
        matches = [(s, z) for (k, s), z in self.by_key.items() if k == key]
        self.assertTrue(matches)
        self.assertTrue(any("{0}" in s for s, _ in matches))

    def test_color_values(self):
        for value, zh in (("CALM", "柔和"), ("DITHER", "抖动")):
            self.assertIn((f"W:Window:video settings|n:menu color:Button|text_value", value), self.by_key)
            self.assertEqual(self.by_key[(f"W:Window:video settings|n:menu color:Button|text_value", value)], zh)

    def test_keybind_action_labels(self):
        labels = ["move forth", "move left", "move back", "move right", "left mouse button",
                  "middle mouse button", "right mouse button", "side mouse button a", "up arrow",
                  "down arrow", "left arrow", "right arrow"]
        for label in labels:
            self.assertIn((f"KEYBIND:{label}", label), self.by_key)


if __name__ == "__main__":
    unittest.main()
