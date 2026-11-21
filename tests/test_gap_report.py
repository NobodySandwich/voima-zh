import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "gap_report.py"


class GapReportTest(unittest.TestCase):
    def test_classification(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "translations.tsv").write_text(
                "W:Window:a|n:known:Button|text_name\tknown\t已知\n",
                encoding="utf-8")
            (tmp / "untranslated.log").write_text(
                "U\t1\tW:Window:a|n:missing:Button|text_name\tmissing\tlist=windows\n"
                "U\t1\tW:Window:names|n:names:TextButton|text_name\tA very long list of names\tname list\n"
                "U\t1\tW:Window:controls|n:H:KeybindButton|text_name\tH\tkeycap\n"
                "U\t1\tX:TextureText|loose text\tloose text\tunowned\n",
                encoding="utf-8")
            (tmp / "test_trace.tsv").write_text(
                "T\tW:Window:a|n:known:Button|text_name\tknown\t已知\n",
                encoding="utf-8")
            (tmp / "plugin.log").write_text("", encoding="utf-8")
            (tmp / "exemptions.tsv").write_text("# none\n", encoding="utf-8")
            out = tmp / "out"
            subprocess.run([sys.executable, str(TOOL),
                            "--tsv", str(tmp / "translations.tsv"),
                            "--trace", str(tmp / "test_trace.tsv"),
                            "--untranslated", str(tmp / "untranslated.log"),
                            "--plugin-log", str(tmp / "plugin.log"),
                            "--exemptions", str(tmp / "exemptions.tsv"),
                            "--out", str(out), "--strict"],
                           check=False)
            report = (out / "gap-report.md").read_text(encoding="utf-8")
            self.assertIn("MISSING_ENTRY", report)
            self.assertIn("USER_CONTENT", report)
            self.assertIn("KEYCAP", report)
            self.assertIn("UNOWNED", report)


if __name__ == "__main__":
    unittest.main()
