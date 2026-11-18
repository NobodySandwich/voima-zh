#include "font_patch.h"
#include "il2cpp_api.h"
#include "util.h"
#include "MinHook.h"
#include <windows.h>
#include <cstring>
#include <set>

namespace {

using ToCharArrayFn = Il2CppArray* (*)(Il2CppString*);
using LoadCharFn = void (*)(void*, uint16_t, Il2CppString*, Il2CppString*, void*);
using FontCtorFn = void (*)(void*, Il2CppString*, int);
using ArrayNewFn = Il2CppArray* (*)(Il2CppClass*, size_t);
using GCHandleNewFn = uint32_t (*)(Il2CppObject*, bool);

ToCharArrayFn g_origToCharArray = nullptr;
LoadCharFn g_origLoadChar = nullptr;
FontCtorFn g_origFontCtor = nullptr;
ArrayNewFn g_arrayNew = nullptr;
GCHandleNewFn g_gcHandleNew = nullptr;

std::wstring g_fontDirWide;
HANDLE g_diag = INVALID_HANDLE_VALUE;
bool g_pageMode = false;
std::vector<uint16_t> g_pageChars;
std::string g_originalFontPath;
Il2CppString* g_pagePathString = nullptr;
uint32_t g_pagePathHandle = 0;
int g_originalFontOffset = 0;
int g_pageExpanded = 0;
int g_redirects = 0;
bool g_mismatch = false;

void Diag(const std::string& s) {
    if (g_diag == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(g_diag, s.data(), (DWORD)s.size(), &w, nullptr);
    WriteFile(g_diag, "\r\n", 2, &w, nullptr);
    FlushFileBuffers(g_diag);
}

Il2CppClass* FindCharClass() {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &count);
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* image = g_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        Il2CppClass* cls = g_api.class_from_name(image, "System", "Char");
        if (cls) return cls;
    }
    return nullptr;
}

Il2CppArray* BuildCharArray(const std::vector<uint16_t>& chars) {
    if (chars.empty() || !g_arrayNew) return nullptr;
    Il2CppClass* charClass = FindCharClass();
    if (!charClass) { Diag("page: no Char class"); return nullptr; }
    Il2CppArray* arr = g_arrayNew(charClass, chars.size());
    if (!arr) { Diag("page: array_new failed"); return nullptr; }
    uint16_t* dst = reinterpret_cast<uint16_t*>(reinterpret_cast<char*>(arr) + 0x20);
    memcpy(dst, chars.data(), chars.size() * 2);
    if (g_gcHandleNew) g_gcHandleNew(reinterpret_cast<Il2CppObject*>(arr), false);
    return arr;
}

Il2CppArray* HookToCharArray(Il2CppString* self) {
    if (!g_pageMode) {
        return g_origToCharArray ? g_origToCharArray(self) : nullptr;
    }
    Il2CppArray* arr = BuildCharArray(g_pageChars);
    ++g_pageExpanded;
    Diag("page charset chars=" + std::to_string(g_pageChars.size()) + " arr=" + std::to_string((uintptr_t)arr));
    return arr;
}

void HookLoadChar(void* self, uint16_t c, Il2CppString* fontPath, Il2CppString* fileName, void* textures) {
    static int pageLoads = 0;
    if (g_pageMode && pageLoads < 20) {
        Diag("page LoadChar c=U+" + std::to_string((unsigned)c) + " file='" + EscapeForLog(ReadManagedString(fileName)) + "'");
        ++pageLoads;
    }
    if (g_pageMode && g_origLoadChar) {
        std::string file = ReadManagedString(fileName);
        if (!file.empty() && g_api.string_new_utf16) {
            std::wstring wfile = Utf8ToWide(file);
            std::wstring cand = g_fontDirWide + L"\\" + wfile;
            if (FileExistsW(cand)) {
                ++g_redirects;
                if (!g_pagePathString && g_api.string_new_utf16) {
                    g_pagePathString = g_api.string_new_utf16(reinterpret_cast<const uint16_t*>(g_fontDirWide.data()),
                                                               (int32_t)g_fontDirWide.size());
                    if (g_pagePathString && g_api.gchandle_new)
                        g_pagePathHandle = g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(g_pagePathString), false);
                }
                if (g_pagePathString) fontPath = g_pagePathString;
            }
        }
    }
    if (g_origLoadChar) g_origLoadChar(self, c, fontPath, fileName, textures);
    if (g_pageMode && pageLoads <= 20) {
        Diag("page LoadChar done c=U+" + std::to_string((unsigned)c));
    }
}

void HookFontCtor(void* self, Il2CppString* fontPath, int offset) {
    if (!g_pageMode && g_originalFontPath.empty() && fontPath) {
        g_originalFontPath = ReadManagedString(fontPath);
        g_originalFontOffset = offset;
        Diag("captured font path='" + EscapeForLog(g_originalFontPath) + "' offset=" + std::to_string(offset));
    }
    if (g_origFontCtor) g_origFontCtor(self, fontPath, offset);
}

} // namespace

bool FontPatch::InstallEarly(uintptr_t base, const std::string& fontDir) {
    if (installed_) return true;
    g_fontDirWide = Utf8ToWide(fontDir);
    g_diag = CreateFileW((g_fontDirWide + L"\\patch_diag.log").c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr);
    g_arrayNew = reinterpret_cast<ArrayNewFn>(GetProcAddress(reinterpret_cast<HMODULE>(base), "il2cpp_array_new"));
    g_gcHandleNew = reinterpret_cast<GCHandleNewFn>(GetProcAddress(reinterpret_cast<HMODULE>(base), "il2cpp_gchandle_new"));
    MH_STATUS st = MH_Initialize();
    if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) return false;

    // TextureFont..ctor obtains its hard-coded alphabet from String.ToCharArray.
    const uintptr_t site = base + 0x42A4A0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(site);
    if (p[0] != 0xE8) { g_mismatch = true; return false; }
    int32_t rel = *reinterpret_cast<const int32_t*>(p + 1);
    uintptr_t target = site + 5 + rel;
    g_origToCharArray = reinterpret_cast<ToCharArrayFn>(target);

    bool patched = false;
    for (int attempt = 0; attempt < 6 && !patched; ++attempt) {
        void* relay = VirtualAlloc(attempt == 0 ? nullptr : reinterpret_cast<void*>((site & ~0xFFFFULL) - (uintptr_t)attempt * 0x200000),
                                   4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!relay) continue;
        uintptr_t r = reinterpret_cast<uintptr_t>(relay);
        int64_t delta = (int64_t)r - (int64_t)(site + 5);
        if (delta < INT32_MIN || delta > INT32_MAX) { VirtualFree(relay, 0, MEM_RELEASE); continue; }
        unsigned char stub[14] = {0xFF, 0x25, 0, 0, 0, 0};
        uintptr_t fn = reinterpret_cast<uintptr_t>(&HookToCharArray);
        memcpy(stub + 6, &fn, 8);
        memcpy(relay, stub, sizeof(stub));
        DWORD old = 0;
        VirtualProtect(reinterpret_cast<void*>(site), 5, PAGE_EXECUTE_READWRITE, &old);
        unsigned char patch[5] = {0xE8};
        int32_t r32 = (int32_t)delta;
        memcpy(patch + 1, &r32, 4);
        memcpy(reinterpret_cast<void*>(site), patch, 5);
        VirtualProtect(reinterpret_cast<void*>(site), 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 5);
        patched = true;
        Diag("patched ToCharArray relay=" + std::to_string(r));
    }
    if (!patched) { g_mismatch = true; return false; }

    void* load = reinterpret_cast<void*>(base + 0x429C80);
    st = MH_CreateHook(load, reinterpret_cast<void*>(&HookLoadChar), reinterpret_cast<void**>(&g_origLoadChar));
    if (st != MH_OK || MH_EnableHook(load) != MH_OK) { g_mismatch = true; return false; }

    void* ctor = reinterpret_cast<void*>(base + 0x42A090);
    const unsigned char* cp = reinterpret_cast<const unsigned char*>(ctor);
    // 48 89 5C 24 08 48 89 6C 24 10 ...
    if (cp[0] == 0x48 && cp[1] == 0x89 && cp[2] == 0x5C && cp[3] == 0x24) {
        st = MH_CreateHook(ctor, reinterpret_cast<void*>(&HookFontCtor), reinterpret_cast<void**>(&g_origFontCtor));
        if (st == MH_OK && MH_EnableHook(ctor) == MH_OK) {
            Diag("hooked TextureFont..ctor");
        } else {
            Diag("failed to hook TextureFont..ctor");
        }
    } else {
        Diag("ctor prologue mismatch");
    }
    installed_ = true;
    return true;
}

void FontPatch::BeginPage(const std::vector<uint16_t>& chars) {
    g_pageChars = chars;
    g_pageMode = true;
}

void FontPatch::EndPage() {
    g_pageMode = false;
    g_pageChars.clear();
}

bool FontPatch::PageMode() const { return g_pageMode; }
std::string FontPatch::OriginalFontPath() const { return g_originalFontPath; }
int FontPatch::OriginalFontOffset() const { return g_originalFontOffset; }
void FontPatch::SetFallbackFontPath(const std::string& path) {
    if (g_originalFontPath.empty()) g_originalFontPath = path;
}

bool FontPatch::Expanded() const { return g_pageExpanded > 0; }
int FontPatch::ExpandedCount() const { return g_pageExpanded; }
int FontPatch::RedirectCount() const { return g_redirects; }
bool FontPatch::SawMismatch() const { return g_mismatch; }
