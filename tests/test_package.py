import hashlib
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT.parent
MANIFEST = Path(__file__).resolve().parent / "game_manifest.json"


class PackageTest(unittest.TestCase):
    def test_required_files(self):
        for name in ("VOIMA_ZH.exe", "VOIMA_ZH.dll", "translations.tsv", "Font/glyphs.txt"):
            self.assertTrue((ROOT / name).exists(), name)
        self.assertGreater((ROOT / "VOIMA_ZH.dll").stat().st_size, 100000)
        self.assertGreater((ROOT / "VOIMA_ZH.exe").stat().st_size, 100000)

    def test_root_table_matches_source(self):
        self.assertEqual((ROOT / "translations.tsv").read_bytes(),
                         (ROOT / "translations" / "translations.tsv").read_bytes())

    def test_game_untouched(self):
        if not MANIFEST.exists():
            self.skipTest("no game manifest")
        expected = json.loads(MANIFEST.read_text())
        for rel, sha in expected.items():
            p = GAME / rel
            self.assertTrue(p.exists(), rel)
            got = hashlib.sha256(p.read_bytes()).hexdigest()
            self.assertEqual(got, sha, f"game file changed: {rel}")

    def test_binary_is_newer_than_sources(self):
        dll = ROOT / "VOIMA_ZH.dll"
        sources = list((ROOT / "src").glob("*.cpp")) + list((ROOT / "src").glob("*.h"))
        self.assertTrue(sources)
        newest = max(p.stat().st_mtime for p in sources)
        self.assertGreaterEqual(dll.stat().st_mtime + 1, newest,
                                "VOIMA_ZH.dll is older than src; build before testing")

    def test_no_bepinex_dependency(self):
        data = (ROOT / "VOIMA_ZH.dll").read_bytes().lower()
        self.assertNotIn(b"bepinex", data)


if __name__ == "__main__":
    unittest.main()
