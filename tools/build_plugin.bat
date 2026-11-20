@echo off
setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0.."
if not exist build\obj mkdir build\obj
set CXX=g++
set CFLAGS=-O2 -std=c++20 -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -Ithird_party\minhook\include -Ithird_party\minhook\src -Isrc
for %%F in (plugin game_model translations logger font_patch font_pages util il2cpp_api) do (
  echo compiling %%F
  %CXX% %CFLAGS% -c src\%%F.cpp -o build\obj\%%F.o || exit /b 1
)
g++ -shared -static -static-libgcc -static-libstdc++ -o VOIMA_ZH.dll ^
  build\obj\plugin.o build\obj\game_model.o build\obj\translations.o build\obj\logger.o ^
  build\obj\font_patch.o build\obj\font_pages.o build\obj\util.o build\obj\il2cpp_api.o ^
  build\obj\hook.o build\obj\buffer.o build\obj\trampoline.o build\obj\hde64.o -luser32 -lgdiplus -lgdi32 || exit /b 1
echo BUILD PLUGIN OK
