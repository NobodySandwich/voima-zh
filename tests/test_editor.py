import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EDITOR = ROOT / "点我修改汉化.html"


class TranslationEditorTest(unittest.TestCase):
    def test_editor_is_self_contained(self):
        self.assertTrue(EDITOR.exists(), "点我修改汉化.html missing")
        html = EDITOR.read_text(encoding="utf-8")
        self.assertIn("中文翻译编辑器", html)
        self.assertIn("parseTsv", html)
        self.assertIn("serialize", html)
        self.assertIn("placeholderProblem", html)
        self.assertIn("按相似度", html)
        self.assertIn("同步同名译文", html)
        self.assertIn("同步包含该译名", html)
        self.assertIn("修改中文译文", html)
        self.assertIn("翻译一致性修复", html)
        self.assertIn("同中文异英检查", html)
        self.assertIn("match-1", html)
        self.assertIn("VOIMA Game", html)
        self.assertIn("VOIMA WenKai", html)
        self.assertTrue((ROOT / "assets" / "VOIMA_Game.ttf").exists())
        self.assertTrue((ROOT / "assets" / "LXGWWenKaiScreen.ttf").exists())
        self.assertNotIn("<script src=", html)
        self.assertNotIn("<link rel=\"stylesheet\"", html)

    def test_editor_loads_translation_schema(self):
        html = EDITOR.read_text(encoding="utf-8")
        table = (ROOT / "translations" / "translations.tsv").read_text(encoding="utf-8")
        self.assertIn("translations/translations.tsv", html)
        self.assertIn("object_key", table)
        self.assertIn("{*}", table)


if __name__ == "__main__":
    unittest.main()
