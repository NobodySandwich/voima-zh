@echo off
setlocal
set PATH=C:\msys64\mingw64\bin;%PATH%
cd /d "%~dp0.."
if not exist build mkdir build
if not exist build\obj mkdir build\obj
set CXX=g++
set CC=gcc
set CFLAGS=-O2 -std=c++20 -D_CRT_SECURE_NO_WARNINGS -DUNICODE -D_UNICODE -Ithird_party\minhook\include -Ithird_party\minhook\src -Isrc
%CC% -O2 -c third_party\minhook\src\hook.c -o build\obj\hook.o -Ithird_party\minhook\include -Ithird_party\minhook\src || exit /b 1
%CC% -O2 -c third_party\minhook\src\buffer.c -o build\obj\buffer.o -Ithird_party\minhook\include -Ithird_party\minhook\src || exit /b 1
%CC% -O2 -c third_party\minhook\src\trampoline.c -o build\obj\trampoline.o -Ithird_party\minhook\include -Ithird_party\minhook\src || exit /b 1
%CC% -O2 -c third_party\minhook\src\hde\hde64.c -o build\obj\hde64.o -Ithird_party\minhook\include -Ithird_party\minhook\src || exit /b 1
%CXX% %CFLAGS% -c src\probe.cpp -o build\obj\probe.o || exit /b 1
%CXX% %CFLAGS% -c src\il2cpp_api.cpp -o build\obj\il2cpp_api.o || exit /b 1
%CXX% -shared -static -static-libgcc -static-libstdc++ -o build\probe.dll build\obj\probe.o build\obj\il2cpp_api.o build\obj\hook.o build\obj\buffer.o build\obj\trampoline.o build\obj\hde64.o -luser32 || exit /b 1
%CXX% -O2 -std=c++20 -static -static-libgcc -static-libstdc++ -municode -o build\injector.exe src\injector.cpp -luser32 || exit /b 1
echo BUILD OK
