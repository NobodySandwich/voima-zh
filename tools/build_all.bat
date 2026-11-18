@echo off
setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0.."
call tools\build_plugin.bat || exit /b 1
if not exist build mkdir build
windres -I assets assets\voima_zh.rc -O coff -o build\voima_zh_icon.o || exit /b 1
g++ -O2 -std=c++20 -static -static-libgcc -static-libstdc++ -municode -DUNICODE -D_UNICODE ^
  -o VOIMA_ZH.exe src\launcher.cpp build\voima_zh_icon.o -luser32 || exit /b 1
if not exist "Font\glyphs.txt" (
  echo Missing Font/glyphs.txt - run tools\build_font.py first
  exit /b 1
)
copy /Y translations\translations.tsv translations.tsv >nul
echo BUILD ALL OK
