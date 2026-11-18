import re
import unittest
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TSV = ROOT / "translations" / "translations.tsv"
TOKEN = re.compile(r"\{\d+\}|\{\*\}|\{[A-Za-z]+\}")


class PlaceholderConsistencyTest(unittest.TestCase):
    def test_source_and_translation_placeholders_match(self):
        problems = []
        for raw in TSV.read_text(encoding="utf-8").splitlines():
            if not raw or raw.startswith("#"):
                continue
            cols = raw.split("\t")
            if len(cols) < 3:
                continue
            key, source, translation = cols[0], cols[1], cols[2]
            expected = Counter(TOKEN.findall(source))
            actual = Counter(TOKEN.findall(translation))
            if expected != actual:
                problems.append((key, dict(expected - actual), dict(actual - expected)))
        self.assertEqual(problems, [], problems[:20])


if __name__ == "__main__":
    unittest.main()
