#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdio>
#include "runtime_font_patch.h"

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

static bool HasArg(int argc, wchar_t** argv, const wchar_t* text) {
    for (int i = 1; i < argc; ++i) if (std::wstring(argv[i]) == text) return true;
    return false;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    wchar_t self[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
    std::wstring selfPath = self;
    size_t slash = selfPath.find_last_of(L"\\/");
    std::wstring modDir = slash == std::wstring::npos ? L"." : selfPath.substr(0, slash);
    std::wstring gameRoot = modDir;
    size_t parent = gameRoot.find_last_of(L"\\/");
    if (parent != std::wstring::npos) gameRoot = gameRoot.substr(0, parent);

    std::wstring exePath = gameRoot + L"\\VOIMA.exe";
    std::wstring dllPath = modDir + L"\\VOIMA_ZH.dll";
    if (!FileExists(exePath) || !FileExists(dllPath)) {
        MessageBoxW(nullptr, L"找不到 VOIMA.exe 或 VOIMA_ZH.dll。\n请把整个 VOIMA_ZH 文件夹放在游戏根目录。",
                    L"VOIMA 汉化", MB_ICONERROR);
        return 2;
    }
    if (HasArg(argc, argv, L"--help") || HasArg(argc, argv, L"-h")) {
        MessageBoxW(nullptr, L"VOIMA 汉化启动器\n\n双击直接启动；\n--test 以窗口模式启动并输出测试日志。",
                    L"VOIMA 汉化", MB_ICONINFORMATION);
        return 0;
    }

    HWND existing = FindWindowW(nullptr, L"VOIMA");
    if (existing) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return 0;
    }

    bool testMode = HasArg(argc, argv, L"--test");
    if (testMode) SetEnvironmentVariableW(L"VOIMA_ZH_TEST", L"1");
#ifdef VOIMA_ZH_DEBUG_BUILD
    SetEnvironmentVariableW(L"VOIMA_ZH_DEBUG", L"1");
#endif

    std::wstring cmd = Quote(exePath);
    bool hasFullscreen = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--test") continue;
        if (a == L"-screen-fullscreen") hasFullscreen = true;
        cmd += L" ";
        cmd += Quote(a);
    }
    if (testMode && !hasFullscreen) {
        cmd += L" -screen-fullscreen 0 -screen-width 1280 -screen-height 720";
    }

    bool fontInjected = rfp::Prepare(modDir);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    if (!CreateProcessW(exePath.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, gameRoot.c_str(), &si, &pi)) {
        DWORD err = GetLastError();
        wchar_t msg[256];
        swprintf(msg, 256, L"启动 VOIMA 失败（错误 %lu）。", err);
        MessageBoxW(nullptr, msg, L"VOIMA 汉化", MB_ICONERROR);
        return 5;
    }

    SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(pi.hProcess, remote, dllPath.c_str(), bytes, nullptr)) {
        TerminateProcess(pi.hProcess, 6);
        return 6;
    }
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary = GetProcAddress(kernel32, "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(pi.hProcess, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary), remote, 0, nullptr);
    if (!thread) {
        TerminateProcess(pi.hProcess, 7);
        return 7;
    }
    WaitForSingleObject(thread, 30000);
    DWORD loadResult = 0;
    GetExitCodeThread(thread, &loadResult);
    CloseHandle(thread);
    VirtualFreeEx(pi.hProcess, remote, 0, MEM_RELEASE);
    if (!loadResult) {
        TerminateProcess(pi.hProcess, 8);
        return 8;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (fontInjected) rfp::Restore(modDir);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}
