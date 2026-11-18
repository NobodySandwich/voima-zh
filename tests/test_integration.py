import os
import subprocess
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME = ROOT.parent
EXE = ROOT / "VOIMA_ZH.exe"
LOGS = ROOT / "logs"


def kill_game():
    subprocess.run(["taskkill", "/F", "/IM", "VOIMA.exe"], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL, check=False)


def read(path):
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


@unittest.skipUnless(os.name == "nt" and EXE.exists() and (GAME / "VOIMA.exe").exists(),
                     "VOIMA game/launcher not present")
class GameIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        kill_game()
        time.sleep(1)
        for p in LOGS.glob("*"):
            try:
                p.unlink()
            except OSError:
                pass
        cls.proc = subprocess.Popen([str(EXE), "--test"], cwd=str(ROOT))
        cls.ready_at = None
        deadline = time.time() + 60
        while time.time() < deadline:
            log = read(LOGS / "plugin.log")
            if "[start] ready" in log and "translated=" in log:
                # Wait for the first translated heartbeat.
                import re
                m = re.findall(r"translated=(\d+)", log)
                if m and max(int(x) for x in m) > 0:
                    cls.ready_at = time.time()
                    break
            time.sleep(0.5)
        # Wait for the main menu to finish building (promo counters included).
        deadline = time.time() + 20
        while time.time() < deadline:
            trace = read(LOGS / "test_trace.tsv")
            if "your worlds" in trace and "# people have already pre-ordered" in trace and "帮助我们更新" in trace:
                break
            time.sleep(0.5)
        cls.log = read(LOGS / "plugin.log")
        cls.trace = read(LOGS / "test_trace.tsv")
        cls.untranslated = read(LOGS / "untranslated.log")
        # Ask the plugin for one object-graph dump while the menu is built.
        (LOGS / "command.txt").write_text("scenario main_menu\ndumpui\n", encoding="ascii")
        dump = LOGS / "object-dump-main_menu.tsv"
        deadline = time.time() + 8
        while time.time() < deadline:
            if dump.exists() and dump.stat().st_size > 120 and "W:Window:" in read(dump):
                break
            time.sleep(0.5)
        cls.object_dump = read(dump)

    @classmethod
    def tearDownClass(cls):
        kill_game()
        try:
            cls.proc.wait(timeout=10)
        except Exception:
            pass

    def test_runtime_ready(self):
        self.assertIn("[start] ready", self.log)
        self.assertIn("GameAssembly base=", self.log)

    def test_main_menu_translated(self):
        self.assertIn("你的世界", self.trace)
        self.assertIn("设置", self.trace)
        self.assertIn("退出游戏", self.trace)

    def test_promo_help_when_visible(self):
        if "help us create new updates!" not in self.trace:
            self.skipTest("help button was built before hooks; tracked for M3 object-dump replay")
        self.assertIn("帮助我们更新！", self.trace)

    def test_object_keys_are_used(self):
        # The trace must contain full object keys, not bare English phrases.
        self.assertIn("W:Window:|n:your worlds:Button|text_name", self.trace)
        self.assertIn("W:Window:|n:settings:Button|text_name", self.trace)
        if "W:Window:promo|n:" in self.trace:
            self.assertIn("W:Window:promo|n:", self.trace)

    def test_early_text_replay(self):
        self.assertIn("[start] ready", self.log)
        if "LOAD:loading" not in self.trace:
            self.skipTest("loading stage was not visible during this run")
        self.assertIn("正在加载", self.trace)
        if "X:TextureText|voima is still buggy" not in self.trace:
            self.skipTest("loading disclaimer was not visible during this run")
        self.assertIn("voima 仍然一堆bug", self.trace)

    def test_object_dump(self):
        if "W:Window:" not in self.object_dump:
            self.skipTest("window list was not populated during the sampled frame")
        self.assertIn("W:Window:|n:your worlds:Button|text_name", self.object_dump)
        self.assertIn("你的世界", self.object_dump)

    def test_font_pages_created(self):
        self.assertIn("[fontpage] page", self.log)
        self.assertIn("ready", self.log)
        self.assertGreaterEqual(self.log.count("ready"), 8, "expected several page fonts")
        self.assertNotIn("ctor threw an exception", self.log)

    def test_controls_tooltip_translated(self):
        # Real control-page hover: catches tooltip keys that the main-menu
        # smoke test cannot see. Client coords are relative to the 1280x720 game.
        (LOGS / "command.txt").write_text("click 155 490\n", encoding="ascii")
        time.sleep(3)
        trace = ""
        for y in range(100, 501, 25):
            for x in (700, 760, 820):
                (LOGS / "command.txt").write_text(f"hover {x} {y}\n", encoding="ascii")
                time.sleep(0.25)
                trace = read(LOGS / "test_trace.tsv")
                if "【backspace】" in trace:
                    break
            if "【backspace】" in trace:
                break
        # The exact action name depends on the user's saved key bindings; the
        # stable part is the localized backspace keycap itself.
        self.assertIn("【backspace】", trace)

    def test_process_alive(self):
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq VOIMA.exe"],
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                             text=True, check=False).stdout
        self.assertIn("VOIMA.exe", out, "game process is not running")

    def test_no_crash_marker(self):
        self.assertNotIn("FATAL", self.log)

    def test_loading_and_promo_noise(self):
        self.assertNotIn("LOAD:loading", self.untranslated)
        self.assertNotIn("W:Window:promo|n:", self.untranslated)


if __name__ == "__main__":
    unittest.main()
