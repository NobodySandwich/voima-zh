import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class AnimalMessagesTest(unittest.TestCase):
    def test_every_exported_animal_message_has_translation(self):
        catalog = ROOT / "catalog" / "animal_messages.tsv"
        table = ROOT / "translations" / "translations.tsv"
        self.assertTrue(catalog.exists(), "catalog/animal_messages.tsv missing")
        translations = {}
        for line in table.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) >= 3:
                translations[cols[0]] = (cols[1], cols[2])
        missing = []
        for line in catalog.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#"):
                continue
            cols = line.split("\t")
            if len(cols) < 2:
                continue
            key, src = cols[0], cols[1]
            if key not in translations:
                missing.append(key)
        self.assertEqual(missing, [], f"missing animal translations: {missing[:20]}")


if __name__ == "__main__":
    unittest.main()
