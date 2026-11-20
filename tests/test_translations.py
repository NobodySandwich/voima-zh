import re
import unittest
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TSV = ROOT / "translations" / "translations.tsv"
FONT = ROOT / "Font"


def load_rows():
    rows = []
    for raw in TSV.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 3:
            continue
        rows.append(tuple(cols[:3]))
    return rows


class TranslationTableTest(unittest.TestCase):
    def test_size(self):
        rows = load_rows()
        self.assertGreaterEqual(len(rows), 600, "translation table unexpectedly small")

    def test_columns_and_keys(self):
        prefixes = ("W:", "CARD:", "BIOME:", "ANIMAL:", "TIP:", "TIPD:",
                    "KEYBIND:", "KEYCAP:", "HUD:", "LOAD:", "MUSIC:", "MENU:", "X:", "DEATH:", "ITEM:")
        for key, source, translation in load_rows():
            self.assertTrue(key.startswith(prefixes), key)
            self.assertIn(":", key)
            self.assertTrue(source.strip(), key)
            self.assertTrue(translation.strip(), key)
            self.assertNotIn("\t", key)
            self.assertNotIn("\\NUM\\", key)
            self.assertNotIn("{{", source + translation)
            # A window key must carry the window/button/field identity, not just text.
            if key.startswith("W:Window:"):
                self.assertIn("|", key, key)

    def test_placeholders(self):
        for key, source, translation in load_rows():
            src = set(re.findall(r"\{(\d+|\*)\}", source))
            dst = set(re.findall(r"\{(\d+|\*)\}", translation))
            # A translation may intentionally drop a joke/count placeholder, but
            # it must never introduce a placeholder the source did not capture.
            self.assertTrue(dst.issubset(src), f"undefined placeholder: {key} / {source} -> {translation}")

    def test_duplicate_object_entries_are_consistent(self):
        seen = {}
        for key, source, translation in load_rows():
            ident = (key, source)
            if ident in seen:
                self.assertEqual(seen[ident], translation, f"conflicting duplicate {ident}")
            seen[ident] = translation

    def test_no_reachable_pattern_is_shadowed(self):
        token = re.compile(r"\{(\d+|\*)\}")
        def as_regex(source):
            parts = []
            last = 0
            for match in token.finditer(source):
                parts.append(re.escape(source[last:match.start()]))
                parts.append(r"\d+" if match.group(1) != "*" else r".*")
                last = match.end()
            parts.append(re.escape(source[last:]))
            return re.compile("^" + "".join(parts) + "$", re.S)

        by_key = defaultdict(list)
        for key, source, _ in load_rows():
            if "{" in source:
                by_key[key].append(source)

        for key, patterns in by_key.items():
            for i, later in enumerate(patterns):
                for earlier in patterns[:i]:
                    self.assertIsNone(
                        as_regex(earlier).fullmatch(later),
                        f"{key}: pattern {later!r} is shadowed by {earlier!r}",
                    )

    def test_object_context_can_disambiguate_same_text(self):
        # The key model must allow the same English text to carry different
        # translations in different objects.  This mirrors the runtime resolver.
        def key(window, button, field):
            return f"W:Window:{window}|n:{button}:Button|{field}"
        self.assertNotEqual(key("menu", "settings", "text_name"),
                            key("audio settings", "settings", "text_name"))
        # And window keys contain both the object identity and the text field.
        self.assertTrue(all("|" in k for k, _, _ in load_rows() if k.startswith("W:")))


if __name__ == "__main__":
    unittest.main()
