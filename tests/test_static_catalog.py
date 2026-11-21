import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import static_catalog as sc


class StaticCatalogTest(unittest.TestCase):
    def test_ui_class_filter(self):
        self.assertTrue(sc.is_ui_class("ColorWindow"))
        self.assertTrue(sc.is_ui_class("MenuUniverse"))
        self.assertFalse(sc.is_ui_class("NormalizationTableUtil"))
        self.assertFalse(sc.is_ui_class("Button.UxmlTraits"))
        self.assertFalse(sc.is_ui_class("Window"))

    def test_text_filters(self):
        self.assertTrue(sc.ui_like("show health"))
        self.assertFalse(sc.ui_like("F10"))
        self.assertFalse(sc.ui_like("CAPS\nLOCK"))
        self.assertFalse(sc.ui_like("@VOIMA-Game"))
        self.assertFalse(sc.ui_like("https://example.com"))

    def test_name_and_pattern(self):
        self.assertEqual(sc.normalize_name("1 recipe still locked"), "# recipe still locked")
        self.assertEqual(sc.normalize_name("hotbar 4"), "hotbar #")
        self.assertTrue(sc.pattern_regex("hotbar {0}").fullmatch("hotbar 4"))
        self.assertFalse(sc.pattern_regex("hotbar {0}").fullmatch("hotbar x"))

    def test_find_pattern_entry(self):
        by_source = {"hotbar {0}": [("KEYBIND:hotbar 4", "快捷栏 {0}")]}
        flat_entries = [("KEYBIND:hotbar 4", "hotbar {0}")]
        patterns = [(k, s, sc.pattern_regex(s)) for k, s in flat_entries]
        found = sc.find_entry("hotbar 4", "KEYBIND", "KEYBIND", by_source, flat_entries, patterns)
        self.assertIsNotNone(found)
        self.assertEqual(found[0], "KEYBIND:hotbar 4")
        self.assertTrue(found[2])


if __name__ == "__main__":
    unittest.main()
