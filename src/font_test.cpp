#include "il2cpp_api.h"
#include "MinHook.h"
#include <windows.h>
#include <string>
#include <vector>
#include <set>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

HANDLE g_log = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
std::string g_modDir;
std::string g_fontDir;
std::vector<uint16_t> g_extra;

using ToCharArrayFn = Il2CppArray* (*)(Il2CppString*);
using LoadCharFn = void (*)(void*, uint16_t, Il2CppString*, Il2CppString*, void*);
using BuildFn = void (*)(void*, Il2CppString*, float, float, float, float, float);
using StringNewFn = Il2CppString* (*)(const uint16_t*, int32_t);
using ArrayNewFn = Il2CppArray* (*)(Il2CppClass*, size_t);
using DomainGetFn = Il2CppDomain* (*)();
using GetAssembliesFn = const Il2CppAssembly** (*)(const Il2CppDomain*, size_t*);
using AssemblyImageFn = Il2CppImage* (*)(const Il2CppAssembly*);
using ClassFromNameFn = Il2CppClass* (*)(const Il2CppImage*, const char*, const char*);
using GCHandleNewFn = uint32_t (*)(Il2CppObject*, bool);

ToCharArrayFn g_origToCharArray = nullptr;
LoadCharFn g_origLoadChar = nullptr;
BuildFn g_origBuild = nullptr;
StringNewFn g_stringNew = nullptr;
ArrayNewFn g_arrayNew = nullptr;
DomainGetFn g_domainGet = nullptr;
GetAssembliesFn g_getAssemblies = nullptr;
AssemblyImageFn g_assemblyImage = nullptr;
ClassFromNameFn g_classFromName = nullptr;
GCHandleNewFn g_gcHandleNew = nullptr;
uintptr_t g_base = 0;
bool g_installed = false;
std::atomic<int> g_buildCalls{0};
std::atomic<int> g_loadCalls{0};
std::atomic<int> g_buildSBCalls{0};

void Log(const std::string& s) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    EnterCriticalSection(&g_lock);
    DWORD w = 0;
    WriteFile(g_log, s.data(), (DWORD)s.size(), &w, nullptr);
    WriteFile(g_log, "\r\n", 2, &w, nullptr);
    FlushFileBuffers(g_log);
    LeaveCriticalSection(&g_lock);
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

Il2CppString* NewString(const std::wstring& s) {
    if (!g_stringNew) return nullptr;
    return g_stringNew(reinterpret_cast<const uint16_t*>(s.data()), (int32_t)s.size());
}

void LoadExtraChars() {
    // Read Font/glyphs.txt as UTF-8; fall back to a small test set.
    std::wstring path = Utf8ToWide(g_fontDir + "\\glyphs.txt");
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    std::vector<char> data;
    if (h != INVALID_HANDLE_VALUE) {
        DWORD sz = GetFileSize(h, nullptr);
        data.resize(sz);
        DWORD got = 0;
        ReadFile(h, data.data(), sz, &got, nullptr);
        CloseHandle(h);
    }
    std::wstring wide;
    if (!data.empty()) {
        int n = MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), nullptr, 0);
        wide.resize(n);
        MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), &wide[0], n);
    } else {
        wide = L"你的世界我是测试中文设置继续游戏多人模组退出音频视频控制";
    }
    std::set<uint16_t> seen;
    for (wchar_t c : wide) {
        if (c == L'\r' || c == L'\n' || c == L'\t' || c == 0) continue;
        if (seen.insert((uint16_t)c).second) g_extra.push_back((uint16_t)c);
    }
    Log("extra chars=" + std::to_string(g_extra.size()));
}

Il2CppClass* FindCharClass() {
    if (!g_domainGet || !g_getAssemblies || !g_assemblyImage || !g_classFromName) return nullptr;
    Il2CppDomain* d = g_domainGet();
    if (!d) return nullptr;
    size_t count = 0;
    const Il2CppAssembly** asms = g_getAssemblies(d, &count);
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* img = g_assemblyImage(asms[i]);
        if (!img) continue;
        Il2CppClass* cls = g_classFromName(img, "System", "Char");
        if (cls) return cls;
    }
    return nullptr;
}

Il2CppArray* HookToCharArray(Il2CppString* self) {
    Log("HookToCharArray begin");
    Il2CppArray* base = g_origToCharArray ? g_origToCharArray(self) : nullptr;
    Log("orig ToCharArray done base=" + std::to_string(reinterpret_cast<uintptr_t>(base)));
    if (!base || g_extra.empty() || !g_arrayNew) return base;
    Il2CppClass* charClass = FindCharClass();
    if (!charClass) { Log("no char class"); return base; }
    int oldLen = *reinterpret_cast<int*>(reinterpret_cast<char*>(base) + 0x18);
    Log("charClass=" + std::to_string(reinterpret_cast<uintptr_t>(charClass)) + " oldLen=" + std::to_string(oldLen));
    std::set<uint16_t> existing;
    const uint16_t* oldData = reinterpret_cast<const uint16_t*>(reinterpret_cast<char*>(base) + 0x20);
    for (int i = 0; i < oldLen; ++i) existing.insert(oldData[i]);
    std::vector<uint16_t> add;
    for (uint16_t c : g_extra) if (existing.insert(c).second) add.push_back(c);
    if (add.empty()) return base;
    int total = oldLen + (int)add.size();
    Il2CppArray* arr = g_arrayNew(charClass, (size_t)total);
    if (!arr) { Log("array_new failed"); return base; }
    uint16_t* dst = reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(arr) + 0x20);
    memcpy(dst, oldData, (size_t)oldLen * 2);
    memcpy(dst + oldLen, add.data(), add.size() * 2);
    if (g_gcHandleNew) g_gcHandleNew(reinterpret_cast<Il2CppObject*>(arr), false);
    Log("ToCharArray expanded " + std::to_string(oldLen) + " -> " + std::to_string(total) +
        " arr=" + std::to_string(reinterpret_cast<uintptr_t>(arr)) +
        " arrLen=" + std::to_string(*reinterpret_cast<int*>(reinterpret_cast<char*>(arr) + 0x18)));
    return arr;
}

bool FileExistsW(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

void HookLoadChar(void* self, uint16_t c, Il2CppString* fontPath, Il2CppString* fileName, void* textures) {
    int n = ++g_loadCalls;
    if (n <= 140) Log("LoadChar enter " + std::to_string(n) + " U+" + std::to_string((unsigned)c) + " file='" + ReadIl2CppString(fileName) + "'");
    std::string file = ReadIl2CppString(fileName);
    std::wstring wfile = Utf8ToWide(file);
    std::wstring candidate = Utf8ToWide(g_fontDir) + L"\\" + wfile;
    if (!file.empty() && FileExistsW(candidate)) {
        static int logged = 0;
        if (logged < 40) {
            Log("LoadChar redirect U+" + std::to_string((unsigned)c) + " file='" + file + "'");
            ++logged;
        }
        fontPath = NewString(Utf8ToWide(g_fontDir));
    }
    g_origLoadChar(self, c, fontPath, fileName, textures);
}

void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fs) {
    static int count = 0;
    ++count;
    ++g_buildCalls;
    std::string s = text ? ReadIl2CppString(text) : std::string();
    if (!s.empty() && count <= 80) Log("Build enter " + std::to_string(count) + " text='" + s + "'");
    if (text) {
        bool replaced = false;
        if (s == "your worlds") { text = NewString(L"你的世界"); replaced = true; }
        else if (s == "settings") { text = NewString(L"设置"); replaced = true; }
        else if (s == "multiplayer") { text = NewString(L"多人游戏"); replaced = true; }
        else if (s == "mods") { text = NewString(L"模组"); replaced = true; }
        else if (s == "controls") { text = NewString(L"控制"); replaced = true; }
        else if (s == "quit game") { text = NewString(L"退出游戏"); replaced = true; }
        else if (s == "continue playing...\nsad") { text = NewString(L"继续游戏...\nsad"); replaced = true; }
        else if (s == "voice") { text = NewString(L"语音"); replaced = true; }
        else if (s == "audio") { text = NewString(L"音频"); replaced = true; }
        else if (s == "video") { text = NewString(L"视频"); replaced = true; }
        else if (s == "graphics") { text = NewString(L"画面"); replaced = true; }
        if (replaced && count <= 80) Log("Build replaced " + s);
    }
    g_origBuild(self, text, x, y, z, effect, fs);
}

DWORD WINAPI Heartbeat(LPVOID) {
    for (int i = 0; i < 30; ++i) {
        Sleep(2000);
        Log("heartbeat build=" + std::to_string(g_buildCalls.load()) + " load=" + std::to_string(g_loadCalls.load()) +
            " sb=" + std::to_string(g_buildSBCalls.load()));
    }
    return 0;
}

DWORD WINAPI EarlyThread(LPVOID) {
    for (int i = 0; i < 2000; ++i) {
        HMODULE ga = GetModuleHandleW(L"GameAssembly.dll");
        if (ga) {
            g_base = reinterpret_cast<uintptr_t>(ga);
            break;
        }
        Sleep(5);
    }
    if (!g_base) { Log("no GameAssembly"); return 0; }
    Log("GameAssembly base=" + std::to_string(g_base));
    g_api.Load();

    g_stringNew = reinterpret_cast<StringNewFn>(GetProcAddress((HMODULE)g_base, "il2cpp_string_new_utf16"));
    g_arrayNew = reinterpret_cast<ArrayNewFn>(GetProcAddress((HMODULE)g_base, "il2cpp_array_new"));
    g_domainGet = reinterpret_cast<DomainGetFn>(GetProcAddress((HMODULE)g_base, "il2cpp_domain_get"));
    g_getAssemblies = reinterpret_cast<GetAssembliesFn>(GetProcAddress((HMODULE)g_base, "il2cpp_domain_get_assemblies"));
    g_assemblyImage = reinterpret_cast<AssemblyImageFn>(GetProcAddress((HMODULE)g_base, "il2cpp_assembly_get_image"));
    g_classFromName = reinterpret_cast<ClassFromNameFn>(GetProcAddress((HMODULE)g_base, "il2cpp_class_from_name"));
    g_gcHandleNew = reinterpret_cast<GCHandleNewFn>(GetProcAddress((HMODULE)g_base, "il2cpp_gchandle_new"));

    MH_Initialize();
    const uintptr_t toCharArrayCall = g_base + 0x42A4A0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(toCharArrayCall);
    if (p[0] == 0xE8) {
        int32_t rel = *reinterpret_cast<const int32_t*>(p + 1);
        uintptr_t target = toCharArrayCall + 5 + rel;
        g_origToCharArray = reinterpret_cast<ToCharArrayFn>(target);
        bool patched = false;
        for (int attempt = 0; attempt < 2 && !patched; ++attempt) {
            void* relay = VirtualAlloc(attempt == 0 ? nullptr : reinterpret_cast<void*>((toCharArrayCall & ~0xFFFFULL) - 0x1000000),
                                       4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (!relay) continue;
            uintptr_t r = reinterpret_cast<uintptr_t>(relay);
            int64_t delta = static_cast<int64_t>(r) - static_cast<int64_t>(toCharArrayCall + 5);
            if (delta < INT32_MIN || delta > INT32_MAX) { VirtualFree(relay, 0, MEM_RELEASE); continue; }
            unsigned char stub[14] = {0xFF, 0x25, 0, 0, 0, 0};
            uintptr_t fn = reinterpret_cast<uintptr_t>(&HookToCharArray);
            memcpy(stub + 6, &fn, 8);
            memcpy(relay, stub, sizeof(stub));
            DWORD old = 0;
            VirtualProtect(reinterpret_cast<void*>(toCharArrayCall), 5, PAGE_EXECUTE_READWRITE, &old);
            unsigned char patch[5];
            patch[0] = 0xE8;
            int32_t r32 = static_cast<int32_t>(delta);
            memcpy(patch + 1, &r32, 4);
            memcpy(reinterpret_cast<void*>(toCharArrayCall), patch, 5);
            VirtualProtect(reinterpret_cast<void*>(toCharArrayCall), 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(toCharArrayCall), 5);
            Log("patched ToCharArray site=" + std::to_string(toCharArrayCall) + " target=" + std::to_string(target) +
                " relay=" + std::to_string(r) + " delta=" + std::to_string(delta));
            patched = true;
        }
        if (!patched) Log("ToCharArray patch failed");
    } else {
        Log("ToCharArray site pattern mismatch");
    }
    void* loadPtr = reinterpret_cast<void*>(g_base + 0x429C80);
    MH_STATUS st = MH_CreateHook(loadPtr, reinterpret_cast<void*>(&HookLoadChar), reinterpret_cast<void**>(&g_origLoadChar));
    Log("hook LoadChar st=" + std::to_string(st));
    if (st == MH_OK) MH_EnableHook(loadPtr);
    void* buildPtr = reinterpret_cast<void*>(g_base + 0x42BBD0);
    st = MH_CreateHook(buildPtr, reinterpret_cast<void*>(&HookBuild), reinterpret_cast<void**>(&g_origBuild));
    Log("hook Build st=" + std::to_string(st));
    if (st == MH_OK) MH_EnableHook(buildPtr);
    g_installed = true;
    Log("installed");
    HANDLE hb = CreateThread(nullptr, 0, Heartbeat, nullptr, 0, nullptr);
    if (hb) CloseHandle(hb);
    return 0;
}

DWORD WINAPI DllThread(LPVOID) {
    wchar_t path[MAX_PATH]{};
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&DllThread), &self);
    GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    std::wstring dir = pos == std::wstring::npos ? L"." : p.substr(0, pos);
    g_modDir = WideToUtf8(dir);
    g_fontDir = g_modDir + "\\Font";
    std::wstring logPath = dir + L"\\font_test.log";
    g_log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
    InitializeCriticalSection(&g_lock);
    Log("start dir=" + g_modDir);
    LoadExtraChars();
    EarlyThread(nullptr);
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE h = CreateThread(nullptr, 0, DllThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
