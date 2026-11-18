#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>

#pragma comment(lib, "user32.lib")

static std::wstring Quote(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'"') out += L"\\\"";
        else out += c;
    }
    out += L"\"";
    return out;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        wprintf(L"usage: injector.exe <game_dir> <dll_path> [game args...]\n");
        return 2;
    }
    std::wstring gameDir = argv[1];
    std::wstring dllPath = argv[2];
    std::wstring exePath = gameDir + L"\\VOIMA.exe";
    if (!FileExists(exePath)) {
        wprintf(L"VOIMA.exe not found: %s\n", exePath.c_str());
        return 3;
    }
    wchar_t fullDll[MAX_PATH * 2]{};
    if (!GetFullPathNameW(dllPath.c_str(), MAX_PATH * 2, fullDll, nullptr)) {
        wprintf(L"bad dll path\n");
        return 4;
    }
    if (!FileExists(fullDll)) {
        wprintf(L"dll not found: %s\n", fullDll);
        return 4;
    }

    std::wstring cmd = Quote(exePath);
    for (int i = 3; i < argc; ++i) {
        cmd += L" ";
        cmd += Quote(argv[i]);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    if (!CreateProcessW(exePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, gameDir.c_str(), &si, &pi)) {
        wprintf(L"CreateProcess failed: %lu\n", GetLastError());
        return 5;
    }

    SIZE_T bytes = (wcslen(fullDll) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        wprintf(L"VirtualAllocEx failed: %lu\n", GetLastError());
        TerminateProcess(pi.hProcess, 6);
        return 6;
    }
    if (!WriteProcessMemory(pi.hProcess, remote, fullDll, bytes, nullptr)) {
        wprintf(L"WriteProcessMemory failed: %lu\n", GetLastError());
        TerminateProcess(pi.hProcess, 7);
        return 7;
    }
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(pi.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remote, 0, nullptr);
    if (!thread) {
        wprintf(L"CreateRemoteThread failed: %lu\n", GetLastError());
        TerminateProcess(pi.hProcess, 8);
        return 8;
    }
    WaitForSingleObject(thread, 30000);
    DWORD loadResult = 0;
    GetExitCodeThread(thread, &loadResult);
    CloseHandle(thread);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    if (loadResult == 0) {
        wprintf(L"LoadLibraryW failed in target\n");
        TerminateProcess(pi.hProcess, 9);
        return 9;
    }
    ResumeThread(pi.hThread);
    wprintf(L"injected pid=%lu dll=%s\n", pi.dwProcessId, fullDll);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
