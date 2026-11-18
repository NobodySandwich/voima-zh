@echo off
setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0.."
if not exist build\obj mkdir build\obj
set CFLAGS=-O2 -std=c++20 -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -Ithird_party\minhook\include -Ithird_party\minhook\src -Isrc
g++ %CFLAGS% -c src\font_test.cpp -o build\obj\font_test.o || exit /b 1
g++ %CFLAGS% -c src\il2cpp_api.cpp -o build\obj\il2cpp_api.o || exit /b 1
g++ -shared -static -static-libgcc -static-libstdc++ -o build\font_test.dll build\obj\font_test.o build\obj\il2cpp_api.o build\obj\hook.o build\obj\buffer.o build\obj\trampoline.o build\obj\hde64.o -luser32 || exit /b 1
echo BUILD FONT TEST OK
