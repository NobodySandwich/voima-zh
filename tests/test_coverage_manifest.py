import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def unescape(s):
    return s.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")


class CoverageManifestTest(unittest.TestCase):
    def test_manifest_requirements_exist(self):
        manifest = json.loads((ROOT / "tests" / "coverage_manifest.json").read_text(encoding="utf-8"))
        rows = {}
        for raw in (ROOT / "translations" / "translations.tsv").read_text(encoding="utf-8").splitlines():
            if not raw or raw.startswith("#"):
                continue
            cols = raw.split("\t")
            if len(cols) >= 3:
                rows[(cols[0], unescape(cols[1]))] = unescape(cols[2])
        for scenario, requirements in manifest.items():
            for req in requirements:
                ident = (req["key"], req["source"])
                self.assertIn(ident, rows, f"{scenario}: missing {ident}")
                if "translation" in req:
                    self.assertEqual(rows[ident], unescape(req["translation"]), f"{scenario}: {ident}")

    def test_manifest_keys_have_object_context(self):
        manifest = json.loads((ROOT / "tests" / "coverage_manifest.json").read_text(encoding="utf-8"))
        for req in manifest["main_menu"]:
            self.assertIn("|", req["key"])
            self.assertTrue(req["key"].startswith("W:Window:"))


if __name__ == "__main__":
    unittest.main()
