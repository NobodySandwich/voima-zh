#!/usr/bin/env python3
"""Build a player-facing VOIMA_ZH release without source/build files.

The release uses only the root translations.tsv, so players do not need
build_all.bat. The HTML editor writes that file directly in Chrome/Edge.
"""
from pathlib import Path
import os
import shutil
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT_ROOT = Path(os.environ.get(
    "VOIMA_ZH_RELEASE_ROOT",
    ROOT.parent.parent / "VOIMA 2.2x2 Windows - Release",
))
OUT = OUT_ROOT / "VOIMA_ZH"
DIST = Path(os.environ.get(
    "VOIMA_ZH_DIST_DIR",
    ROOT.parent.parent / "VOIMA_ZH_git" / "dist",
))
ZIP = DIST / "VOIMA_ZH_release.zip"

README = """VOIMA 简体中文插件（发行版）
================================

安装/启动
---------
把整个 VOIMA_ZH 文件夹放到 VOIMA.exe 同级目录，双击 VOIMA_ZH.exe。

修改翻译
--------
双击“点我修改汉化.html”，打开根目录的 translations.tsv，编辑后保存。
Chrome / Edge 可直接写回文件，不需要 build_all.bat，也不需要任何开发工具。

授权
----
- 项目原创代码、编辑器代码、文档和中文译文：GNU GPL v3.0，见 LICENSE。
- 霞鹜文楷 Screen：SIL OFL 1.1，见 assets/OFL.txt。
- MinHook：BSD 2-Clause，见 licenses/MINHOOK.txt。
- VOIMA 游戏及其衍生字模/界面素材不属于 GPL；本发行版不包含游戏本体。
  游戏素材的商业使用仍需 VOIMA 权利方许可。

GPL 源代码
----------
https://github.com/NobodySandwich/voima-zh

注意
----
- 不要删除 translations.tsv 或 Font 文件夹。
- 本发行版不包含源代码、测试和构建脚本。
"""

def copy_required(src, dst):
    src = ROOT / src
    dst = OUT / dst
    if not src.exists():
        raise FileNotFoundError(src)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

def main() -> int:
    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    for name in ("VOIMA_ZH.exe", "VOIMA_ZH.dll", "translations.tsv", "点我修改汉化.html"):
        copy_required(name, name)

    for name in ("LICENSE",):
        copy_required(name, name)
    (OUT / "licenses").mkdir()
    shutil.copy2(ROOT / "licenses" / "MINHOOK.txt", OUT / "licenses" / "MINHOOK.txt")

    # Font pages are data, not source; diagnostic logs must not ship.
    shutil.copytree(ROOT / "Font", OUT / "Font", ignore=shutil.ignore_patterns("*.log"))

    assets = OUT / "assets"
    assets.mkdir()
    for name in ("LXGWWenKaiScreen.ttf", "VOIMA_Game.ttf", "OFL.txt"):
        shutil.copy2(ROOT / "assets" / name, assets / name)

    (OUT / "README.txt").write_text(README, encoding="utf-8")

    forbidden = [p for p in ("src", "tools", "tests", "translations", "third_party", "build") if (OUT / p).exists()]
    if forbidden:
        raise RuntimeError("release contains development paths: " + ", ".join(forbidden))

    DIST.mkdir(parents=True, exist_ok=True)
    if ZIP.exists():
        ZIP.unlink()
    with zipfile.ZipFile(ZIP, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for p in sorted(OUT.rglob("*")):
            if p.is_file():
                zf.write(p, p.relative_to(OUT_ROOT))

    files = sum(1 for p in OUT.rglob("*") if p.is_file())
    print(f"release: {OUT}")
    print(f"files: {files}")
    print(f"zip: {ZIP} ({ZIP.stat().st_size} bytes)")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
