#include "il2cpp_api.h"
#include "util.h"
#include "logger.h"
#include "game_model.h"
#include "translations.h"
#include "font_patch.h"
#include "font_pages.h"
#include "MinHook.h"
#include <windows.h>
#include <gdiplus.h>
#include <atomic>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <deque>

namespace {

using BuildFn = void (*)(void*, Il2CppString*, float, float, float, float, float);
using BuildSBFn = void (*)(void*, void*, float, float, float, float, float);
using RebuildFn = void (*)(void*);
using UiSetTextFn = void (*)(void*, Il2CppString*);
using ShakeFn = void (*)(void*, int, int, float);
using BouncedFn = void* (*)(void*, float);
using RenderFn = void (*)(void*, void*, bool);
struct Mat4 { float m[16]; };
using DrawMeshFn = void (*)(void*, int, Mat4, void*, int, void*, void*, int, bool, void*, int, void*);
using DrawInstancedFn = void (*)(void*, int, void*, void*, int, void*, int, bool, int, void*, int, void*);
using UniverseFontCurrentFn = bool (*)(void*);
struct Vec4 { float x, y, z, w; };
using SetFloatImplFn = void (*)(void*, int, float);
using SetTextureImplFn = void (*)(void*, int, void*);
using SetVectorImplFn = void (*)(void*, int, Vec4);
using SetArrayFn = void (*)(void*, int, void*);
using SetArrayCountFn = void (*)(void*, int, void*, int);
using LordUpdateFn = void (*)(void*);
using StringNewFn = Il2CppString* (*)(const uint16_t*, int32_t);

std::atomic<bool> g_ready{false};
std::vector<void*> g_pendingHud;
int g_bannerPage = -1;
void* g_chestTemplate = nullptr;
void ProcessPendingHud();
std::atomic<bool> g_generationContext{false};
std::vector<int> g_speechPages;
std::deque<int> g_pageQueue;
uint64_t g_lastPageBuild = 0;
int g_hudPage = -1;
std::string g_deathContext;
uint64_t g_deathContextAt = 0;
using Death0Fn = void (*)(void*);
using Death1Fn = void (*)(void*, void*);
Death0Fn g_origVoidDamage = nullptr;
Death0Fn g_origSuffocateDamage = nullptr;
Death1Fn g_origDeathDrop = nullptr;
std::atomic<bool> g_runtimeReady{false};
std::atomic<int> g_buildCalls{0};
std::atomic<int> g_translatedCalls{0};
std::atomic<int> g_untranslatedUnique{0};
std::atomic<bool> g_buildHooked{false};
std::atomic<bool> g_buildSBHooked{false};
std::atomic<bool> g_dumpRequested{false};
std::string g_modDir;
std::string g_fontDir;
std::atomic<bool> g_overlayActive{false};
std::atomic<bool> g_overlayThreadStarted{false};
std::atomic<uint64_t> g_overlayUntil{0};
std::mutex g_overlayMutex;
std::wstring g_overlayText;

DWORD WINAPI OverlayThread(LPVOID);
DWORD WINAPI BootBlackThread(LPVOID);
std::atomic<bool> g_bootOverlayDone{false};
std::atomic<bool> g_bootOverlayStarted{false};
std::atomic<uint64_t> g_lastLoadingBuild{0};
void StartBootBlackOverlay() {
    if (!g_bootOverlayStarted.exchange(true))
        CreateThread(nullptr, 0, BootBlackThread, nullptr, 0, nullptr);
}
DWORD WINAPI BootBlackThread(LPVOID) {
    const wchar_t* cls = L"VOIMA_ZH_BootBlack";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        cls, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 0;
    uint64_t started = GetTickCount64();
    bool visible = false;
    while (!g_bootOverlayDone.load()) {
        HWND game = FindWindowW(nullptr, L"VOIMA");
        if (!game) { Sleep(30); continue; }
        RECT cr{}; GetClientRect(game, &cr);
        POINT origin{0, 0}; ClientToScreen(game, &origin);
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        if (w <= 0 || h <= 0) { Sleep(30); continue; }
        SetWindowPos(hwnd, HWND_TOPMOST, origin.x, origin.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible = true;
        if (GetTickCount64() - started > 20000) break;
        Sleep(30);
    }
    if (visible) ShowWindow(hwnd, SW_HIDE);
    DestroyWindow(hwnd);
    return 0;
}

bool IsConfirmObject(const std::string& key) {
    // Explicit object list. Add future confirmation objects here; never match raw text.
    static const char* kObjects[] = {
        "W:Window:controls|n:reset controls:Button|text_name",
        "W:Window:|n:exit printer:Button|text_name",
    };
    for (const char* object : kObjects)
        if (key == object) return true;
    // World-detail delete uses a custom world name in the primary key.
    // Match the structural suffix instead of hardcoding world names.
    static const std::string kDeleteSuffix = "|n:delete:Button|text_name";
    if (key.size() >= kDeleteSuffix.size() &&
        key.compare(key.size() - kDeleteSuffix.size(), kDeleteSuffix.size(), kDeleteSuffix) == 0)
        return true;
    return false;
}

void HideConfirmOverlay() {
    g_overlayActive = false;
    g_overlayUntil = 0;
}

void ShowConfirmOverlay(const std::wstring& text) {
    {
        std::lock_guard<std::mutex> lock(g_overlayMutex);
        g_overlayText = text;
    }
    g_overlayUntil = GetTickCount64() + 120;
    g_overlayActive = true;
    if (!g_overlayThreadStarted.exchange(true))
        CreateThread(nullptr, 0, OverlayThread, nullptr, 0, nullptr);
}

DWORD WINAPI OverlayThread(LPVOID) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 0;
    const wchar_t* kClass = L"VOIMA_ZH_Overlay";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { Gdiplus::GdiplusShutdown(token); return 0; }
    std::map<wchar_t, Gdiplus::Image*> images;
    bool visible = false;
    while (true) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!g_overlayActive.load() || GetTickCount64() > g_overlayUntil.load()) {
            if (visible) { ShowWindow(hwnd, SW_HIDE); visible = false; }
            Sleep(30);
            continue;
        }
        HWND game = FindWindowW(nullptr, L"VOIMA");
        if (!game) { Sleep(30); continue; }
        RECT cr{}; GetClientRect(game, &cr);
        POINT origin{0, 0}; ClientToScreen(game, &origin);
        int w = cr.right - cr.left, h = cr.bottom - cr.top;
        if (w <= 0 || h <= 0) { Sleep(30); continue; }
        SetWindowPos(hwnd, HWND_TOPMOST, origin.x, origin.y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        HDC screen = GetDC(nullptr);
        static HBITMAP capDib = nullptr;
        static HDC capDC = nullptr;
        static void* capBits = nullptr;
        static int capW = 0, capH = 0;
        static uint64_t detectAt = 0;
        static int btnCenterX = 0, btnCenterY = 0, btnH = 0;
        if (!capDC || capW != w || capH != h) {
            if (capDib) DeleteObject(capDib);
            if (capDC) DeleteDC(capDC);
            capDC = CreateCompatibleDC(screen);
            BITMAPINFO cbmi{};
            cbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            cbmi.bmiHeader.biWidth = w;
            cbmi.bmiHeader.biHeight = -h;
            cbmi.bmiHeader.biPlanes = 1;
            cbmi.bmiHeader.biBitCount = 32;
            cbmi.bmiHeader.biCompression = BI_RGB;
            capDib = CreateDIBSection(screen, &cbmi, DIB_RGB_COLORS, &capBits, nullptr, 0);
            SelectObject(capDC, capDib);
            capW = w; capH = h;
            detectAt = 0;
        }
        if (GetTickCount64() - detectAt > 200) {
            detectAt = GetTickCount64();
            if (BitBlt(capDC, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY)) {
                std::vector<int> rows(h, 0);
                uint32_t* px = reinterpret_cast<uint32_t*>(capBits);
                for (int y = 0; y < h; y += 2) {
                    uint32_t* row = px + (size_t)y * w;
                    for (int x = 0; x < w; x += 2) {
                        uint32_t p = row[x];
                        int r = p & 0xff, g = (p >> 8) & 0xff, b = (p >> 16) & 0xff;
                        if (r > 150 && g < 90 && b < 90) rows[y]++;
                    }
                }
                int bestStart = -1, bestEnd = -1, runStart = -1;
                for (int y = 0; y < h; ++y) {
                    bool on = rows[y] > w / 50;
                    if (on && runStart < 0) runStart = y;
                    if (!on && runStart >= 0) {
                        if (bestStart < 0 || y - runStart > bestEnd - bestStart) { bestStart = runStart; bestEnd = y; }
                        runStart = -1;
                    }
                }
                if (runStart >= 0 && (bestStart < 0 || h - runStart > bestEnd - bestStart)) { bestStart = runStart; bestEnd = h; }
                if (bestStart >= 0 && bestEnd - bestStart >= 20) {
                    int xmin = w, xmax = 0;
                    for (int y = bestStart; y < bestEnd; ++y) {
                        uint32_t* row = px + (size_t)y * w;
                        for (int x = 0; x < w; ++x) {
                            uint32_t p = row[x];
                            int r = p & 0xff, g = (p >> 8) & 0xff, b = (p >> 16) & 0xff;
                            if (r > 150 && g < 90 && b < 90) { if (x < xmin) xmin = x; if (x > xmax) xmax = x; }
                        }
                    }
                    if (xmax - xmin > 80) {
                        btnCenterX = (xmin + xmax) / 2;
                        btnCenterY = (bestStart + bestEnd) / 2;
                        btnH = bestEnd - bestStart + 1;
                    }
                }
            }
        }
        if (!btnH) { btnCenterX = (int)(w * 0.630f); btnCenterY = (int)(h * 0.914f); btnH = (int)(h * 0.064f); }
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ old = SelectObject(mem, dib);
        {
            Gdiplus::Bitmap frame(w, h, PixelFormat32bppPARGB);
            Gdiplus::Graphics g(&frame);
            g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            g.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            std::wstring text;
            { std::lock_guard<std::mutex> lock(g_overlayMutex); text = g_overlayText; }
            int px = (int)(btnH * 0.58f);
            if (px < 18) px = 18;
            float advance = px * 0.92f;
            float total = advance * (float)text.size();
            float cx = btnCenterX + 1.5f;
            float cy = btnCenterY + 0.5f;
            float startX = cx - total * 0.5f;
            static uint64_t jitterAt = 0;
            static float jitter[32]{};
            uint64_t now = GetTickCount64();
            if (now - jitterAt >= 16) {
                jitterAt = now;
                for (int j = 0; j < 32; ++j)
                    jitter[j] = ((float)(std::rand() % 3001) / 1000.0f - 1.5f) * 2.0f;
            }
            for (size_t i = 0; i < text.size(); ++i) {
                auto it = images.find(text[i]);
                if (it == images.end()) {
                    std::wstring path = Utf8ToWide(g_fontDir + "\\") + text[i] + L".png";
                    Gdiplus::Image* img = Gdiplus::Image::FromFile(path.c_str());
                    it = images.emplace(text[i], img).first;
                }
                if (!it->second || it->second->GetLastStatus() != Gdiplus::Ok) continue;
                float sx = 0.0f;
                float sy = i < 32 ? jitter[i] : 0.0f;
                g.DrawImage(it->second, Gdiplus::RectF(startX + advance * i + sx, cy - px * 0.5f + sy, (float)px, (float)px));
            }
            g.Flush();
            Gdiplus::BitmapData bd{};
            Gdiplus::Rect lockRect(0, 0, w, h);
            if (frame.LockBits(&lockRect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
                for (int y = 0; y < h; ++y)
                    std::memcpy((char*)bits + (size_t)y * w * 4, (char*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)w * 4);
                frame.UnlockBits(&bd);
            }
        }
        POINT dst{origin.x, origin.y};
        SIZE size{w, h};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        DeleteObject(dib);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        if (!visible) { ShowWindow(hwnd, SW_SHOWNOACTIVATE); visible = true; }
        Sleep(16);
    }
}

std::string g_scenario = "main_menu";
GameModel g_model;
TranslationDb g_db;
FontPatch g_fontPatch;
FontPages g_fontPages;
void* g_origPickFont = nullptr;
void* g_origLordUpdate = nullptr;

bool g_pickFontHooked = false;
StringNewFn g_stringNew = nullptr;
BuildFn g_origBuild = nullptr;
BuildSBFn g_origBuildSB = nullptr;
RebuildFn g_origRebuild = nullptr;
UiSetTextFn g_origUiSetText = nullptr;
ShakeFn g_origShake = nullptr;
BouncedFn g_origBounced = nullptr;
RenderFn g_origRender = nullptr;
void* g_confirmSelf = nullptr;
int g_confirmPage = -1;
DrawMeshFn g_origDrawMesh = nullptr;
DrawInstancedFn g_origDrawInstanced = nullptr;
void* g_meshVertexCountFn = nullptr;
thread_local bool g_inConfirmRender = false;
std::atomic<bool> g_matrixLog{false};
std::atomic<bool> g_matrixMeasure{false};
std::ofstream g_matrixOut;
std::mutex g_matrixMutex;
int g_matrixFrame = 0;
UniverseFontCurrentFn g_origUniverseFontCurrent = nullptr;
SetFloatImplFn g_origSetFloatImpl = nullptr;
SetTextureImplFn g_origSetTextureImpl = nullptr;
SetVectorImplFn g_origSetVectorImpl = nullptr;
SetArrayFn g_origSetFloatArray = nullptr;
SetArrayFn g_origSetVectorArray = nullptr;
SetArrayCountFn g_origSetFloatArrayImpl = nullptr;
SetArrayCountFn g_origSetVectorArrayImpl = nullptr;
thread_local bool g_inConfirmBuild = false;
struct Quat { float x, y, z, w; };
using BuildRotatedFn = void (*)(void*, Il2CppString*, float, float, float, Quat, float);
BuildRotatedFn g_origBuildRotated = nullptr;
using LoadCardTypeFn = void* (*)(void*, Il2CppString*, Il2CppString*, int, bool);
LoadCardTypeFn g_origLoadCardType = nullptr;
std::vector<void*> g_loadedCards;
constexpr const char* kFontFailureText = "\xE2\x82\xACnull\xE2\x82\xAC";
bool g_buildEarlyInstalled = false;

struct BuildCache {
    std::string input;
    std::string output;
    Il2CppString* outputString = nullptr;
    uint32_t outputHandle = 0;
    uint64_t lastResolve = 0;
    ObjectKeys keys;
};
std::unordered_map<void*, BuildCache> g_cache;

struct EarlyBuild {
    std::string input;
    float x = 0, y = 0, z = 0, effect = 0, fontSize = 0;
    uint64_t last = 0;
};
std::unordered_map<void*, EarlyBuild> g_earlyBuilds;
bool g_earlyReplayed = false;
std::atomic<int> g_earlyBuffered{0};

void DumpHudBindsOnce();
void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize);
bool TryTranslateGenerationText(const std::string& input, std::string& out);
void ForceFontWithUniverse(void* self, void* font);
void HookPickFont(void* self, void* font);
void DumpObjectGraph(std::string scenario);
void DumpButtonBounds();

std::string LocalizeTooltipKey(const std::string& text) {
    size_t a = text.find("【");
    if (a == std::string::npos) return text;
    size_t b = text.find("】", a + 3);
    if (b == std::string::npos) return text;
    std::string inner = LowerAscii(Trim(text.substr(a + 3, b - a - 3)));
    static const std::map<std::string, std::string> names = {
        {"control", "ctrl"}, {"ctrl", "ctrl"},
        {"escape", "esc"}, {"esc", "esc"},
        {"left mouse button", "鼠标左键"}, {"right mouse button", "鼠标右键"},
        {"middle mouse button", "鼠标中键"},
        {"side mouse button a", "鼠标侧键 A"}, {"side mouse button b", "鼠标侧键 B"},
        {"up arrow", "上方向键"}, {"down arrow", "下方向键"},
        {"left arrow", "左方向键"}, {"right arrow", "右方向键"}
    };
    auto it = names.find(inner);
    if (it == names.end()) return text;
    return text.substr(0, a + 3) + it->second + text.substr(b);
}

std::wstring MakeWide(const std::string& s) { return Utf8ToWide(s); }

Il2CppString* NewManagedString(const std::string& utf8) {
    if (!g_stringNew) return nullptr;
    std::wstring wide = Utf8ToWide(utf8);
    return g_stringNew(reinterpret_cast<const uint16_t*>(wide.data()), (int32_t)wide.size());
}

bool LooksLikeTextureText(void* self) {
    if (!self) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(self, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    Il2CppClass* cls = g_api.object_get_class(self);
    if (!cls) return false;
    const char* name = g_api.class_get_name(cls);
    return name && std::strcmp(name, "TextureText") == 0;
}

void BufferEarlyBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize) {
    if (!self || !text || g_earlyBuilds.size() >= 512) return;
    std::string input = ReadManagedString(text);
    if (input.empty()) return;
    std::string lower = LowerAscii(input);
    bool relevant = lower.find("loading ") != std::string::npos ||
                    lower == "help us create new updates!" ||
                    lower.find("voima is still buggy") != std::string::npos;
    if (!relevant) return;
    EarlyBuild e;
    e.input = input;
    e.x = x; e.y = y; e.z = z; e.effect = effect; e.fontSize = fontSize;
    e.last = GetTickCount64();
    auto it = g_earlyBuilds.find(self);
    if (it == g_earlyBuilds.end()) {
        g_earlyBuilds.emplace(self, e);
        if (g_earlyBuffered.fetch_add(1) < 12)
            Logger::Instance().Log("early", "buffered '" + EscapeForLog(input).substr(0, 100) + "'");
    } else {
        it->second = e;
    }
}

void ReplayEarlyBuilds() {
    if (g_earlyReplayed) return;
    g_earlyReplayed = true;
    if (g_earlyBuilds.empty()) {
        Logger::Instance().Log("early", "no pre-ready text buffered");
        return;
    }
    int replayed = 0, dead = 0;
    for (auto& kv : g_earlyBuilds) {
        void* self = kv.first;
        EarlyBuild& e = kv.second;
        if (!LooksLikeTextureText(self)) {
            ++dead;
            continue;
        }
        Il2CppString* s = NewManagedString(e.input);
        if (!s) {
            ++dead;
            continue;
        }
        HookBuild(self, s, e.x, e.y, e.z, e.effect, e.fontSize);
        ++replayed;
    }
    Logger::Instance().Log("early", "replayed=" + std::to_string(replayed) + " dead=" + std::to_string(dead) +
                                      " buffered=" + std::to_string(g_earlyBuilds.size()));
    g_earlyBuilds.clear();
}

bool LoadGlyphChars(const std::string& path, std::vector<uint16_t>& chars) {
    std::string data;
    if (!ReadFileUtf8(Utf8ToWide(path), data)) return false;
    std::wstring wide = Utf8ToWide(data);
    std::set<uint16_t> seen;
    for (wchar_t wc : wide) {
        if (wc == L'\r' || wc == L'\n' || wc == L'\t' || wc == 0) continue;
        uint16_t c = (uint16_t)wc;
        if (seen.insert(c).second) chars.push_back(c);
    }
    return true;
}

Il2CppClass* FindUnityClass(const char* name) {
    Il2CppDomain* d = g_api.domain_get();
    if (!d) return nullptr;
    size_t count = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(d, &count);
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* img = assemblies[i] ? g_api.assembly_get_image(assemblies[i]) : nullptr;
        if (!img) continue;
        Il2CppClass* cls = g_api.class_from_name(img, "UnityEngine", name);
        if (cls) return cls;
        cls = g_api.class_from_name(img, "", name);
        if (cls) return cls;
    }
    return nullptr;
}

void* FindUnityMethodPtr(const char* className, const char* methodName, int paramCount) {
    Il2CppClass* cls = FindUnityClass(className);
    if (!cls) return nullptr;
    void* it = nullptr;
    while (MethodInfo* m = g_api.class_get_methods(cls, &it)) {
        const char* n = g_api.method_get_name(m);
        if (!n || std::strcmp(n, methodName) != 0 || (int)g_api.method_get_param_count(m) != paramCount) continue;
        if (paramCount > 0) {
            const Il2CppType* t = g_api.method_get_param(m, 0);
            const char* tn = t ? g_api.type_get_name(t) : nullptr;
            if (!tn || std::strstr(tn, "Single") == nullptr) continue;
        }
        return *reinterpret_cast<void**>(m);
    }
    return nullptr;
}

MethodInfo* FindMethodInfo(const char* className, const char* methodName, int paramCount, const char* firstParamNeedle) {
    Il2CppClass* cls = FindClass(className);
    if (!cls) return nullptr;
    void* iter = nullptr;
    while (MethodInfo* m = g_api.class_get_methods(cls, &iter)) {
        const char* name = g_api.method_get_name(m);
        if (!name || std::strcmp(name, methodName) != 0) continue;
        if ((int)g_api.method_get_param_count(m) != paramCount) continue;
        if (firstParamNeedle) {
            const Il2CppType* t = g_api.method_get_param(m, 0);
            const char* tn = t ? g_api.type_get_name(t) : nullptr;
            if (!tn || !std::strstr(tn, firstParamNeedle)) continue;
        }
        return m;
    }
    return nullptr;
}

void* FindMethodPtr(const char* className, const char* methodName, int paramCount, const char* firstParamNeedle) {
    MethodInfo* m = FindMethodInfo(className, methodName, paramCount, firstParamNeedle);
    return m ? *reinterpret_cast<void**>(m) : nullptr;
}

void InstallBuildHookAt(void* ptr, bool early) {
    if (!ptr || g_buildHooked.load()) return;
    void* target = ptr;
    if (early) {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(target);
        // TextureText.Build(string): push rbp; push rbx; push rdi; push r13; lea rbp, [rsp-...]
        bool prologue = (p[0] == 0x55 && p[1] == 0x53 && p[2] == 0x57 && p[3] == 0x41 && p[4] == 0x55) ||
                        (p[0] == 0x40 && p[1] == 0x55 && p[2] == 0x53 && p[3] == 0x57 && p[4] == 0x41 && p[5] == 0x55);
        if (!prologue) {
            Logger::Instance().Log("hook", "early Build prologue mismatch");
            return;
        }
    }
    MH_STATUS st = MH_CreateHook(target, reinterpret_cast<void*>(&HookBuild), reinterpret_cast<void**>(&g_origBuild));
    if (st != MH_OK) {
        Logger::Instance().Log("hook", "MH_CreateHook Build failed st=" + std::to_string((int)st));
        return;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Logger::Instance().Log("hook", "MH_EnableHook Build failed");
        return;
    }
    g_buildHooked = true;
    g_buildEarlyInstalled = early;
    Logger::Instance().Log("hook", std::string("TextureText.Build hooked ") + (early ? "early" : "late"));
}

void FocusGame(HWND hwnd) {
    if (!hwnd) return;
    DWORD tid = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD cur = GetCurrentThreadId();
    AttachThreadInput(cur, tid, TRUE);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    AttachThreadInput(cur, tid, FALSE);
    BringWindowToTop(hwnd);
}

void DumpObjectGraph(std::string scenario) {
    if (scenario.empty()) scenario = "manual";
    for (char& c : scenario) {
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-' && c != '_')
            c = '_';
    }
    std::string path = g_modDir + "\\logs\\object-dump-" + scenario + ".tsv";
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        Logger::Instance().Log("dump", "cannot open " + path);
        return;
    }
    out << "kind\tscenario\twindow\tbutton\tclass\tfield\tsource\tprimary_key\tkeys\ttranslated\tresult\tdetail\n";
    int rows = 0;
    std::vector<void*> windows = g_model.Windows();
    Logger::Instance().Log("dump", "windows=" + std::to_string(windows.size()));
    auto esc = [](const std::string& s) { return EscapeForLog(s); };
    auto emit = [&](const char* kind, const std::string& window, const std::string& button,
                    const std::string& cls, const char* field, void* textObj) {
        if (!textObj || rows >= 20000) return;
        std::string source;
        auto cached = g_cache.find(textObj);
        if (cached != g_cache.end()) source = cached->second.input;
        if (source.empty()) source = g_model.TextureText(textObj);
        if (source.empty()) return;
        ObjectKeys keys = g_model.Describe(textObj, source);
        std::string translated, matchedKey, matchedSource;
        int page = -1;
        bool ok = g_db.Lookup(keys.keys, source, translated, matchedKey, matchedSource, &page);
        bool used = ok && !translated.empty() && translated != source;
        std::string joined;
        for (const auto& k : keys.keys) {
            if (!joined.empty()) joined += ";";
            joined += k;
        }
        std::string dumpSource = source;
        if (dumpSource.size() > 500) dumpSource = dumpSource.substr(0, 500) + "...<truncated>";
        std::string detail = keys.detail;
        if (source.size() > 500) detail += " source_truncated=1";
        out << kind << "\t" << scenario << "\t" << esc(window) << "\t" << esc(button) << "\t"
            << esc(cls) << "\t" << field << "\t" << esc(dumpSource) << "\t" << esc(keys.primary)
            << "\t" << esc(joined) << "\t" << (used ? "1" : "0") << "\t" << esc(translated)
            << "\t" << esc(detail) << "\n";
        ++rows;
    };
    for (void* w : windows) {
        std::string header = g_model.WindowHeader(w);
        Logger::Instance().Log("dump", "window header='" + EscapeForLog(header) + "' buttons=" + std::to_string(g_model.Buttons(w).size()));
        out << "WINDOW\t" << scenario << "\t" << esc(header) << "\t\tWindow\theader\t"
            << esc(header) << "\tW:Window:" << esc(header) << "\t\t0\t\twindow="
            << std::to_string(reinterpret_cast<uintptr_t>(w)) << "\n";
        ++rows;
        emit("HEADER", header, "", "Window", "header_text", g_model.WindowHeaderText(w));
        for (void* b : g_model.Buttons(w)) {
            std::string name = g_model.ButtonName(b);
            std::string cls = g_model.ButtonClass(b);
            // The settings tab strip is icon-only; its button.text_name points at
            // a shared tooltip TextureText, not a visible label.
            if (header == "tabs") continue;
            emit("BUTTON", header, name, cls, "text_name", g_model.ButtonTextName(b));
            emit("BUTTON", header, name, cls, "text_value", g_model.ButtonTextValue(b));
        }
        void* tip = g_model.WindowTip(w);
        if (tip) {
            std::string word = g_model.TipWord(tip);
            std::string text = g_model.TipText(tip);
            if (!word.empty() || !text.empty()) {
                out << "TIP\t" << scenario << "\t" << esc(header) << "\t\tTip\tword\t"
                    << esc(word) << "\tTIP:" << esc(LowerAscii(word)) << "\t\t0\t\t" << esc(text) << "\n";
                ++rows;
            }
        }
    }
    void* tooltip = g_model.TooltipText();
    if (tooltip) emit("TOOLTIP", "", "", "TextureText", "text", tooltip);
    out.close();
    Logger::Instance().Log("dump", "wrote " + path + " rows=" + std::to_string(rows));
}

void DumpCardTooltipFields(void* card, Il2CppClass* ct) {
    Logger::Instance().Debug("carddump", "enter card=" + std::to_string((uintptr_t)card) + " ct=" + std::to_string((uintptr_t)ct));
    if (!card) return;
    if (!ct) { Logger::Instance().Debug("carddump", "ct=null"); return; }
    static size_t pathOff = SIZE_MAX, nameOff = SIZE_MAX, damageOff = SIZE_MAX, healOff = SIZE_MAX, pushOff = SIZE_MAX;
    static bool header = false;
    static bool logged = false;
    if (pathOff == SIZE_MAX) {
        pathOff = FieldOffset(ct, "path");
        nameOff = FieldOffset(ct, "name");
        damageOff = FieldOffset(ct, "damage");
        healOff = FieldOffset(ct, "heal_amount");
        pushOff = FieldOffset(ct, "push_damage");
    }
    int damage = damageOff == SIZE_MAX ? 0 : ReadObjectInt(card, damageOff);
    int heal = healOff == SIZE_MAX ? 0 : ReadObjectInt(card, healOff);
    int push = pushOff == SIZE_MAX ? 0 : ReadObjectInt(card, pushOff);
    void* pp = nullptr; void* np = nullptr;
    std::string path, nm;
    if (pathOff != SIZE_MAX && ReadObjectPtr(card, pathOff, &pp)) path = ReadManagedString(reinterpret_cast<Il2CppString*>(pp));
    if (nameOff != SIZE_MAX && ReadObjectPtr(card, nameOff, &np)) nm = ReadManagedString(reinterpret_cast<Il2CppString*>(np));
    std::string dir = g_modDir + "\catalog";
    std::string outPath = dir + "\card_tooltip_fields.tsv";
    CreateDirectoryA(dir.c_str(), nullptr);
    std::ofstream out(outPath, std::ios::app);
    if (!logged) {
        logged = true;
        Logger::Instance().Debug("carddump", "dir=" + EscapeForLog(dir) + " path=" + EscapeForLog(outPath) + " open=" + std::to_string(out ? 1 : 0));
    }
    if (!out) return;
    if (!header) { out << "path\tname\tdamage\theal_amount\tpush_damage\n"; header = true; }
    out << EscapeForLog(path) << "\t" << EscapeForLog(nm) << "\t" << damage << "\t" << heal << "\t" << push << "\n";
}

void DumpAllCardTypes() {
    static uint64_t last = 0;
    uint64_t now = GetTickCount64();
    if (now - last < 2000) return;
    last = now;
    Il2CppClass* ct = FindClass("CardType");
    FieldInfo* uniField = FindField(FindClass("Card"), "uni");
    if (!uniField) uniField = FindField(FindClass("Lord"), "uni");
    void* uni = uniField ? StaticPtr(uniField) : nullptr;
    void* cards = nullptr;
    int n = 0;
    if (uni && ReadObjectPtr(uni, 0x10, &cards) && cards) n = ReadObjectInt(cards, 0x18);
    Logger::Instance().Debug("unidump", "uni=" + std::to_string((uintptr_t)uni) + " cards=" + std::to_string((uintptr_t)cards) + " len=" + std::to_string(n) + " ct=" + std::to_string((uintptr_t)ct));
    if (!ct || !cards || n <= 0) return;
    void** items = reinterpret_cast<void**>(reinterpret_cast<char*>(cards) + 0x20);
    Logger::Instance().Debug("unidump", "first=" + std::to_string((uintptr_t)(n > 0 ? items[0] : nullptr)) + " second=" + std::to_string((uintptr_t)(n > 1 ? items[1] : nullptr)));
    static std::set<void*> seen;
    for (int i = 0; i < n; ++i) {
        void* card = items[i];
        if (card && seen.insert(card).second) DumpCardTooltipFields(card, ct);
    }
}

void* HookLoadCardType(void* self, Il2CppString* file, Il2CppString* name, int cardId, bool save) {
    void* card = g_origLoadCardType ? g_origLoadCardType(self, file, name, cardId, save) : nullptr;
    if (card) g_loadedCards.push_back(card);
    return card;
}

void DumpLoadedCards() {
    if (g_loadedCards.empty()) return;
    Il2CppClass* ct = FindClass("CardType");
    Il2CppClass* at = FindClass("AnimalType");
    if (!ct || !at) return;
    size_t pathOff = FieldOffset(ct, "path");
    size_t msgOffsets[3] = {
        FieldOffset(at, "hurt_messages"), FieldOffset(at, "attack_messages"), FieldOffset(at, "flee_messages")
    };
    static const char* fields[3] = {"hurt_messages", "attack_messages", "flee_messages"};
    CreateDirectoryA((g_modDir + "\\catalog").c_str(), nullptr);
    std::ofstream out(g_modDir + "\\catalog\\animal_messages.runtime.tsv", std::ios::trunc);
    out << "# category\tfield\tindex\tpath\tname\tmessage\n";
    int total = 0, animals = 0;
    for (void* card : g_loadedCards) {
        if (!card) continue;
        Il2CppClass* cls = g_api.object_get_class ? g_api.object_get_class(reinterpret_cast<Il2CppObject*>(card)) : nullptr;
        std::string cname = cls && g_api.class_get_name ? g_api.class_get_name(cls) : "";
        if (cname.find("AnimalType") == std::string::npos) continue;
        ++animals;
        void* pp = nullptr;
        std::string path;
        if (pathOff != SIZE_MAX && ReadObjectPtr(card, pathOff, &pp))
            path = ReadManagedString(reinterpret_cast<Il2CppString*>(pp));
        for (int f = 0; f < 3; ++f) {
            void* msgs = nullptr;
            if (msgOffsets[f] == SIZE_MAX || !ReadObjectPtr(card, msgOffsets[f], &msgs) || !msgs) continue;
            int n = ReadObjectInt(msgs, 0x18);
            for (int j = 0; j < n; ++j) {
                void* sp = nullptr;
                if (!ReadObjectPtr(msgs, 0x20 + (size_t)j * 8, &sp)) continue;
                std::string msg = ReadManagedString(reinterpret_cast<Il2CppString*>(sp));
                out << "animal\t" << fields[f] << '\t' << j << '\t'
                    << EscapeForLog(path) << "\t\t" << EscapeForLog(msg) << '\n';
                ++total;
            }
        }
    }
    Logger::Instance().Log("command", "dumped loaded cards animals=" + std::to_string(animals) +
                                    " messages=" + std::to_string(total) + " cards=" + std::to_string(g_loadedCards.size()));
}

extern std::atomic<bool> g_camStress;
extern void* g_camStressTransform;

void ProcessCommandFile() {
    if (g_modDir.empty()) return;
    std::wstring path = Utf8ToWide(g_modDir + "\\logs\\command.txt");
    std::string data;
    if (!ReadFileUtf8(path, data)) return;
    if (data.empty()) return;
    std::wstring done = Utf8ToWide(g_modDir + "\\logs\\command.done");
    DeleteFileW(done.c_str());
    MoveFileW(path.c_str(), done.c_str());
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        std::string line = Trim(data.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        pos = nl == std::string::npos ? data.size() : nl + 1;
        if (line.rfind("scenario ", 0) == 0) {
            g_scenario = Trim(line.substr(9));
            Logger::Instance().Log("command", "scenario=" + g_scenario);
            continue;
        }
        if (line.rfind("down ", 0) == 0 || line.rfind("move ", 0) == 0) {
            int x = 0, y = 0;
            int off = line[0] == 'd' ? 5 : 5;
            if (sscanf_s(line.c_str() + off, "%d %d", &x, &y) == 2) {
                HWND hwnd = FindWindowW(nullptr, L"VOIMA");
                if (hwnd) {
                    POINT p{x, y};
                    ClientToScreen(hwnd, &p);
                    FocusGame(hwnd);
                    SetCursorPos(p.x, p.y);
                    if (line[0] == 'd') mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    Logger::Instance().Log("command", std::string(line[0] == 'd' ? "down" : "move") +
                                                        " client=" + std::to_string(x) + "," + std::to_string(y));
                }
            }
            continue;
        }
        if (line == "up") {
            HWND hwnd = FindWindowW(nullptr, L"VOIMA");
            if (hwnd) {
                FocusGame(hwnd);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                Logger::Instance().Log("command", "up");
            }
            continue;
        }
        if (line.rfind("hover ", 0) == 0) {
            int x = 0, y = 0;
            if (sscanf_s(line.c_str() + 6, "%d %d", &x, &y) == 2) {
                HWND hwnd = FindWindowW(nullptr, L"VOIMA");
                if (hwnd) {
                    POINT p{x, y};
                    ClientToScreen(hwnd, &p);
                    FocusGame(hwnd);
                    SetCursorPos(p.x, p.y);
                    Logger::Instance().Log("command", "hover client=" + std::to_string(x) + "," + std::to_string(y));
                }
            }
            continue;
        }
        if (line.rfind("key ", 0) == 0) {
            unsigned int vk = 0;
            if (sscanf_s(line.c_str() + 4, "%x", &vk) == 1) {
                HWND hwnd = FindWindowW(nullptr, L"VOIMA");
                if (hwnd) {
                    FocusGame(hwnd);
                    keybd_event((BYTE)vk, 0, 0, 0);
                    Sleep(50);
                    keybd_event((BYTE)vk, 0, KEYEVENTF_KEYUP, 0);
                    Logger::Instance().Log("command", "key vk=" + std::to_string(vk));
                }
            }
            continue;
        }
        if (line.rfind("sclick ", 0) == 0) {
            int x = 0, y = 0;
            if (sscanf_s(line.c_str() + 7, "%d %d", &x, &y) == 2) {
                HWND hwnd = FindWindowW(nullptr, L"VOIMA");
                if (hwnd) FocusGame(hwnd);
                SetCursorPos(x, y);
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(40);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                Logger::Instance().Log("command", "sclick screen=" + std::to_string(x) + "," + std::to_string(y));
            }
            continue;
        }
        if (line.rfind("click ", 0) == 0) {
            int x = 0, y = 0;
            if (sscanf_s(line.c_str() + 6, "%d %d", &x, &y) == 2) {
                HWND hwnd = FindWindowW(nullptr, L"VOIMA");
                if (hwnd) {
                    POINT p{x, y};
                    ClientToScreen(hwnd, &p);
                    FocusGame(hwnd);
                    SetCursorPos(p.x, p.y);
                    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                    Sleep(40);
                    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                    Logger::Instance().Log("command", "click client=" + std::to_string(x) + "," + std::to_string(y) +
                                                        " screen=" + std::to_string(p.x) + "," + std::to_string(p.y));
                }
            }
            continue;
        }
        if (line == "music") {
            MethodInfo* getter = FindMethodInfo("Lord", "get_soul", 0, nullptr);
            MethodInfo* render = FindMethodInfo("Soul", "RenderMusicText", 0, nullptr);
            void* soul = nullptr;
            Il2CppObject* exc = nullptr;
            if (getter && g_api.runtime_invoke) soul = g_api.runtime_invoke(getter, nullptr, nullptr, &exc);
            if (render && soul && g_api.runtime_invoke) {
                g_api.runtime_invoke(render, soul, nullptr, &exc);
                Logger::Instance().Log("command", std::string("called Soul.RenderMusicText") + (exc ? " (exception)" : ""));
            }
            continue;
        }
        if (line == "live") {
            MethodInfo* m = FindMethodInfo("Menu", "Live", 0, nullptr);
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            if (m && menu && g_api.runtime_invoke) {
                Il2CppObject* exc = nullptr;
                g_api.runtime_invoke(m, menu, nullptr, &exc);
                Logger::Instance().Log("command", std::string("called Menu.Live") + (exc ? " (exception)" : ""));
            }
            continue;
        }
        if (line == "dumpui" || line.rfind("dumpui ", 0) == 0) {
            if (line.size() > 7) g_scenario = Trim(line.substr(7));
            g_dumpRequested = true;
            Logger::Instance().Log("command", "dump requested scenario=" + g_scenario);
            continue;
        }
        if (line.rfind("setmenu ", 0) == 0) {
            std::string spec = Trim(line.substr(8));
            size_t sp = spec.find(' ');
            if (sp != std::string::npos) {
                std::string field = spec.substr(0, sp);
                int value = std::atoi(spec.substr(sp + 1).c_str());
                void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
                FieldInfo* f = FindField(FindClass("Menu"), field.c_str());
                if (menu && f) {
                    *reinterpret_cast<int*>(reinterpret_cast<char*>(menu) + FieldOffset(FindClass("Menu"), field.c_str())) = value;
                    Logger::Instance().Log("command", "set Menu." + field + "=" + std::to_string(value));
                } else {
                    Logger::Instance().Log("command", "cannot set Menu." + field);
                }
            }
            continue;
        }
        if (line.rfind("call ", 0) == 0) {
            std::string method = Trim(line.substr(5));
            MethodInfo* m = FindMethodInfo("Menu", method.c_str(), 0, nullptr);
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            if (m && menu && g_api.runtime_invoke) {
                Il2CppObject* exc = nullptr;
                g_api.runtime_invoke(m, menu, nullptr, &exc);
                Logger::Instance().Log("command", "called Menu." + method + (exc ? " (exception)" : ""));
            } else {
                Logger::Instance().Log("command", "cannot call Menu." + method);
            }
            continue;
        }
        if (line == "matrixlog" || line == "matrixlog off") {
            std::lock_guard<std::mutex> lock(g_matrixMutex);
            if (line == "matrixlog off") {
                g_matrixLog = false;
                g_matrixMeasure = false;
                if (g_matrixOut.is_open()) g_matrixOut.close();
                Logger::Instance().Log("matrixlog", "stopped");
            } else {
                g_matrixOut.open(g_modDir + "\\logs\\matrices.tsv", std::ios::trunc);
                g_matrixFrame = 0;
                g_matrixMeasure = true;
                g_matrixLog = true;
                Logger::Instance().Log("matrixlog", "started");
            }
            continue;
        }
        if (line == "continue2") {
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            void* details = nullptr;
            if (menu) ReadObjectPtr(menu, FieldOffset(FindClass("Menu"), "uni_details"), &details);
            MethodInfo* m = FindMethodInfo("Menu", "ContinueWorld", 1, nullptr);
            Logger::Instance().Log("command", "continue2 menu=" + std::to_string((uintptr_t)menu) + " details=" + std::to_string((uintptr_t)details) + " method=" + std::to_string((uintptr_t)m));
            if (m && menu && details && g_api.runtime_invoke) {
                void* args[1] = {details};
                Il2CppObject* exc = nullptr;
                g_api.runtime_invoke(m, menu, args, &exc);
                Logger::Instance().Log("command", std::string("called ContinueWorld(uni_details)") + (exc ? " (exception)" : ""));
            }
            continue;
        }
        if (line == "continue2") {
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            void* details = nullptr;
            if (menu) ReadObjectPtr(menu, FieldOffset(FindClass("Menu"), "uni_details"), &details);
            MethodInfo* m = FindMethodInfo("Menu", "ContinueWorld", 1, nullptr);
            const Il2CppType* pt = m ? g_api.method_get_param(m, 0) : nullptr;
            Logger::Instance().Log("command", "continue2 menu=" + std::to_string((uintptr_t)menu) + " details=" + std::to_string((uintptr_t)details) + " param=" + std::string(pt && g_api.type_get_name ? g_api.type_get_name(pt) : "?"));
            if (m && menu && details && g_api.runtime_invoke) {
                void* args[1] = {details};
                Il2CppObject* exc = nullptr;
                g_api.runtime_invoke(m, menu, args, &exc);
                Logger::Instance().Log("command", std::string("called ContinueWorld(uni_details)") + (exc ? " (exception)" : ""));
            }
            continue;
        }
        if (line == "camstress") {
            bool on = !g_camStress.exchange(true);
            if (!on) g_camStress = false;
            g_camStressTransform = nullptr;
            Logger::Instance().Log("command", std::string("camstress=") + (on ? "on" : "off"));
            continue;
        }
        if (line == "dumpkeys") {
            void* souls = StaticPtr(FindField(FindClass("Lord"), "souls"));
            void* soul = nullptr;
            if (souls) ReadObjectPtr(souls, 0x20, &soul);
            void* list = nullptr;
            if (soul) ReadObjectPtr(soul, FieldOffset(FindClass("Soul"), "keys"), &list);
            Logger::Instance().Log("command", "dumpkeys soul=" + std::to_string(reinterpret_cast<uintptr_t>(soul)) +
                                            " list=" + std::to_string(reinterpret_cast<uintptr_t>(list)));
            int n = 0;
            ForEachManagedList(list, [&](void* b, int) {
                if (!b || n >= 40) return;
                void* np = nullptr;
                void* dp = nullptr;
                ReadObjectPtr(b, 0x10, &np);
                ReadObjectPtr(b, 0x18, &dp);
                std::string name = ReadManagedString(reinterpret_cast<Il2CppString*>(np));
                std::string desc = ReadManagedString(reinterpret_cast<Il2CppString*>(dp));
                int key = ReadObjectInt(b, 0x20);
                int c = ReadObjectInt(b, 0x52);
                Logger::Instance().Log("keys", "#" + std::to_string(n) + " name='" + EscapeForLog(name) +
                                               "' desc='" + EscapeForLog(desc) + "' key=" + std::to_string(key) +
                                               " c=" + std::to_string(c));
                ++n;
            });
            continue;
        }
        if (line == "dumpphrases") {
            void* uni = StaticPtr(FindField(FindClass("Lord"), "uni"));
            Logger::Instance().Log("command", "dumpphrases uni=" + std::to_string(reinterpret_cast<uintptr_t>(uni)));
            Il2CppClass* ct = FindClass("CardType");
            Il2CppClass* animalType = FindClass("AnimalType");
            size_t pathOff = ct ? FieldOffset(ct, "path") : SIZE_MAX;
            std::ofstream out(g_modDir + "\\logs\\animal_messages.tsv", std::ios::trunc);
            auto dumpArray = [&](size_t arrOff, const char* category) {
                if (!uni || !ct) return;
                void* arr = nullptr;
                if (!ReadObjectPtr(uni, arrOff, &arr) || !arr) return;
                int count = ReadObjectInt(arr, 0x18);
                Logger::Instance().Log("command", std::string("dumpphrases ") + category + " arr=" +
                                                std::to_string(reinterpret_cast<uintptr_t>(arr)) + " count=" + std::to_string(count));
                for (int i = 0; i < count; ++i) {
                    void* card = nullptr;
                    if (!ReadObjectPtr(arr, 0x20 + (size_t)i * 8, &card) || !card) continue;
                    void* pp = nullptr;
                    std::string path;
                    if (pathOff != SIZE_MAX && ReadObjectPtr(card, pathOff, &pp))
                        path = ReadManagedString(reinterpret_cast<Il2CppString*>(pp));
                    for (const char* field : {"hello_messages", "hurt_messages", "attack_messages", "flee_messages", "love_messages", "beg_messages"}) {
                        void* msgs = nullptr;
                        if (!animalType || !ReadObjectPtr(card, FieldOffset(animalType, field), &msgs) || !msgs) continue;
                        int n = ReadObjectInt(msgs, 0x18);
                        for (int j = 0; j < n; ++j) {
                            void* sp = nullptr;
                            if (!ReadObjectPtr(msgs, 0x20 + (size_t)j * 8, &sp)) continue;
                            std::string msg = ReadManagedString(reinterpret_cast<Il2CppString*>(sp));
                            out << category << '\t' << field << '\t' << j << '\t'
                                << EscapeForLog(path) << '\t' << EscapeForLog(msg) << '\n';
                        }
                    }
                }
            };
            dumpArray(0x10, "cards");
            dumpArray(0x20, "items");
            dumpArray(0x28, "blocks");
            Logger::Instance().Log("command", "dumped animal messages");
            continue;
        }
        if (line == "dumpcards") {
            DumpLoadedCards();
            continue;
        }
        if (line.rfind("press ", 0) != 0) continue;
        std::string target = LowerAscii(Trim(line.substr(6)));
        Il2CppClass* win = FindClass("Window");
        Il2CppClass* btn = FindClass("Button");
        if (!win || !btn) continue;
        static MethodInfo* press = nullptr;
        if (!press) press = FindMethodInfo("Button", "Press", 0, nullptr);
        FieldInfo* wins = FindField(win, "windows");
        size_t buttonsOff = FieldOffset(win, "buttons");
        size_t nameOff = FieldOffset(btn, "name");
        bool pressed = false;
        auto tryWindow = [&](void* w) {
            void* list = nullptr;
            ReadObjectPtr(w, buttonsOff, &list);
            ForEachManagedList(list, [&](void* b, int) {
                if (pressed) return;
                void* np = nullptr;
                ReadObjectPtr(b, nameOff, &np);
                std::string name = LowerAscii(CollapseSpaces(Trim(ReadManagedString(reinterpret_cast<Il2CppString*>(np)))));
                size_t nl = name.find('\n');
                if (nl != std::string::npos) name = name.substr(0, nl);
                if (name != target) return;
                if (!press || !g_api.object_get_virtual_method) return;
                MethodInfo* vm = g_api.object_get_virtual_method(b, press);
                void* fp = vm ? *reinterpret_cast<void**>(vm) : nullptr;
                if (fp) {
                    reinterpret_cast<void (*)(void*)>(fp)(b);
                    pressed = true;
                    Logger::Instance().Log("command", "pressed '" + target + "'");
                }
            });
        };
        ForEachManagedList(StaticPtr(wins), [&](void* w, int) { if (!pressed) tryWindow(w); });
    }
}

void RecordFrameTiming() {
    static LARGE_INTEGER freq{};
    static LARGE_INTEGER last{};
    static uint64_t windowStart = 0;
    static std::vector<double> times;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (last.QuadPart) {
        double ms = (double)(now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart;
        if (ms > 0.0 && ms < 1000.0) times.push_back(ms);
    }
    last = now;
    uint64_t tick = GetTickCount64();
    if (!windowStart) { windowStart = tick; return; }
    if (tick - windowStart < 1000 || times.empty()) return;
    std::vector<double> sorted = times;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double v : times) sum += v;
    size_t n = times.size();
    double avg = sum / (double)n;
    double p95 = sorted[(size_t)((sorted.size() - 1) * 0.95)];
    Logger::Instance().Debug("fps", "frames=" + std::to_string(n) +
        " avg_fps=" + std::to_string(1000.0 / avg) +
        " avg_ms=" + std::to_string(avg) +
        " p95_ms=" + std::to_string(p95) +
        " min_ms=" + std::to_string(sorted.front()) +
        " max_ms=" + std::to_string(sorted.back()));
    times.clear();
    windowStart = tick;
}

std::atomic<bool> g_camStress{false};
void* g_camStressCamera = nullptr;
void* g_camStressTransform = nullptr;
void* g_camStressGetMain = nullptr;
void* g_camStressGetTransform = nullptr;
void* g_camStressRotate = nullptr;


void TranslateNetLoadingButton() {
    static int logged = 0;
    static FieldInfo* netField = nullptr;
    static size_t boff = SIZE_MAX, textNameOff = SIZE_MAX, textOff = SIZE_MAX;
    if (!netField) {
        netField = FindField(FindClass("Lord"), "net");
        Il2CppClass* netClass = FindClass("Net");
        Il2CppClass* tb = FindClass("TextButton");
        if (!netField || !netClass || !tb) return;
        boff = FieldOffset(netClass, "b_loading");
        textNameOff = FieldOffset(tb, "text_name");
        textOff = FieldOffset(FindClass("TextureText"), "text");
    }
    void* net = StaticPtr(netField);
    if (!net) return;
    void* b = nullptr;
    if (boff == SIZE_MAX || !ReadObjectPtr(net, boff, &b) || !b) return;
    void* tt = nullptr;
    ReadObjectPtr(b, textNameOff, &tt);
    std::string s = tt ? g_model.TextureText(tt) : std::string();
    if (logged == 0) {
        Logger::Instance().Log("gen", "net loading button text='" + EscapeForLog(s) + "' tt=" + std::to_string((uintptr_t)tt));
        ++logged;
    }
    std::string gen;
    if (TryTranslateGenerationText(s, gen)) {
        int page = g_db.EnsurePageForText(gen, 200);
        if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
            Il2CppString* ns = NewManagedString(gen);
            if (ns) {
                if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                if (textOff != SIZE_MAX) *reinterpret_cast<void**>(reinterpret_cast<char*>(tt) + textOff) = ns;
                ForceFontWithUniverse(tt, g_fontPages.FontForPage(page));
                if (g_origRebuild) g_origRebuild(tt);
                ForceFontWithUniverse(tt, g_fontPages.FontForPage(page));
                if (logged++ < 4) Logger::Instance().Log("gen", "net loading out=" + EscapeForLog(gen));
            }
        }
    }
}


void TranslateAllTextureTexts() {
    static uint64_t lastScan = 0;
    uint64_t now = GetTickCount64();
    if (now - lastScan < 500) return;
    void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
    if (!menu) return;
    size_t coff = FieldOffset(FindClass("Menu"), "creating_world");
    if (coff == SIZE_MAX || ReadObjectInt(menu, coff) == 0) return;
    lastScan = now;
    Il2CppClass* objClass = FindClassInNamespace("UnityEngine", "Object");
    Il2CppClass* textClass = FindClass("TextureText");
    if (!objClass || !textClass || !g_api.type_get_object || !g_api.runtime_invoke) return;
    MethodInfo* find = nullptr;
    void* it = nullptr;
    while (MethodInfo* m = g_api.class_get_methods(objClass, &it)) {
        const char* n = g_api.method_get_name(m);
        if (n && std::strcmp(n, "FindObjectsOfType") == 0 && g_api.method_get_param_count(m) == 1) { find = m; break; }
    }
    if (!find) return;
    const Il2CppType* ttType = g_api.class_get_type ? g_api.class_get_type(textClass) : nullptr;
    if (!ttType) return;
    void* args[1] = {g_api.type_get_object(ttType)};
    Il2CppObject* exc = nullptr;
    Il2CppObject* arr = g_api.runtime_invoke(find, nullptr, args, &exc);
    if (!arr || exc) return;
    int len = *reinterpret_cast<int*>(reinterpret_cast<char*>(arr) + 0x18);
    void** items = reinterpret_cast<void**>(reinterpret_cast<char*>(arr) + 0x20);
    for (int i = 0; i < len; ++i) {
        void* tt = items[i];
        if (!tt) continue;
        std::string s = g_model.TextureText(tt);
        std::string gen;
        bool isGen = TryTranslateGenerationText(s, gen);
        if (!isGen) {
            ObjectKeys keys = g_model.Describe(tt, s);
            std::string tr, mk, ms; int pg = -1;
            if (!g_db.Lookup(keys.keys, s, tr, mk, ms, &pg) || tr.empty() || tr == s) continue;
            gen = tr;
        }
        int page = g_db.EnsurePageForText(gen, 200);
        if (page < 0 || g_fontPages.EnsurePage(page) < 0) continue;
        Il2CppString* ns = NewManagedString(gen);
        if (!ns) continue;
        if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
        size_t off = FieldOffset(FindClass("TextureText"), "text");
        if (off != SIZE_MAX) *reinterpret_cast<void**>(reinterpret_cast<char*>(tt) + off) = ns;
        ForceFontWithUniverse(tt, g_fontPages.FontForPage(page));
        if (g_origRebuild) g_origRebuild(tt);
        ForceFontWithUniverse(tt, g_fontPages.FontForPage(page));
        Logger::Instance().Log("gen", "findobjects out=" + EscapeForLog(gen));
    }
}

void ProcessPageQueue() {
    if (g_pageQueue.empty()) return;
    uint64_t now = GetTickCount64();
    if (now - g_lastPageBuild < 80) return;
    int page = g_pageQueue.front();
    g_pageQueue.pop_front();
    g_lastPageBuild = now;
    if (page >= 0 && !g_fontPages.IsPageReady(page)) {
        Logger::Instance().Log("pagequeue", "building page=" + std::to_string(page) + " remaining=" + std::to_string(g_pageQueue.size()));
        g_fontPages.EnsurePage(page);
    }
    if (g_pageQueue.empty()) g_bootOverlayDone = true;
}

void PrewarmHudPages() {
    static bool done = false;
    if (done) return;
    done = true;
    const char* samples[] = {"space 切割", "推动 发射", "[space] 切割", "缩短 延长 切割", "[e] 打开", "[e] to open", "inventory 打开"};
    for (const char* s : samples) {
        int page = g_db.EnsurePageForText(s, 200);
        if (page >= 0 && !g_fontPages.IsPageReady(page)) g_pageQueue.push_back(page);
    }
}

void HookLordUpdate(void* self) {
    if (g_ready.load()) {
        static bool allPagesQueued = false;
        if (!allPagesQueued) {
            allPagesQueued = true;
            for (size_t i = 0; i < g_db.pageCount(); ++i)
                if (!g_fontPages.IsPageReady((int)i)) g_pageQueue.push_back((int)i);
        }
        static bool bannerBuilt = false;
        if (!bannerBuilt && g_bannerPage >= 0) {
            bannerBuilt = true;
            int bp = g_fontPages.EnsurePage(g_bannerPage);
            void* bf = g_fontPages.FontForPage(g_bannerPage);
            Logger::Instance().Log("banner", "ready prebuild page=" + std::to_string(g_bannerPage) + " result=" + std::to_string(bp) + " font=" + std::to_string(reinterpret_cast<uintptr_t>(bf)));
        }
        ProcessPageQueue();
        ProcessPendingHud();
        PrewarmHudPages();
        static FieldInfo* worldField = nullptr;
        static size_t gen = SIZE_MAX, load = SIZE_MAX;
        if (!worldField) {
            worldField = FindField(FindClass("Lord"), "world");
            Il2CppClass* wc = FindClass("World");
            if (wc) {
                gen = FieldOffset(wc, "generating_world");
                load = FieldOffset(wc, "loading_world");
            }
        }
        void* world = worldField ? StaticPtr(worldField) : nullptr;
        g_generationContext = world && ((gen != SIZE_MAX && ReadObjectInt(world, gen) != 0) ||
                                        (load != SIZE_MAX && ReadObjectInt(world, load) != 0));
        static bool dumpedBinds = false;
        if (!dumpedBinds) { dumpedBinds = true; DumpHudBindsOnce(); }
        if (g_generationContext) TranslateNetLoadingButton();
    }
    if (g_dumpRequested.exchange(false)) DumpObjectGraph(g_scenario);
    if (g_origLordUpdate) reinterpret_cast<LordUpdateFn>(g_origLordUpdate)(self);
    if (g_camStress.load()) {
        if (!g_camStressGetMain) {
            g_camStressGetMain = FindUnityMethodPtr("Camera", "get_main", 0);
            g_camStressGetTransform = FindUnityMethodPtr("Component", "get_transform", 0);
            g_camStressRotate = FindUnityMethodPtr("Transform", "Rotate", 3);
            Logger::Instance().Log("camstress", "getMain=" + std::to_string((uintptr_t)g_camStressGetMain) + " getTransform=" + std::to_string((uintptr_t)g_camStressGetTransform) + " rotate=" + std::to_string((uintptr_t)g_camStressRotate));
            void* tc = FindUnityClass("Transform");
            if (tc) {
                void* ti = nullptr;
                while (MethodInfo* m = g_api.class_get_methods(reinterpret_cast<Il2CppClass*>(tc), &ti)) {
                    const char* n = g_api.method_get_name(m);
                    if (!n || std::strstr(n, "Rotate") == nullptr) continue;
                    std::string ps;
                    for (uint32_t pi = 0; pi < g_api.method_get_param_count(m); ++pi) {
                        const Il2CppType* pt = g_api.method_get_param(m, pi);
                        ps += (pi ? "," : "") + std::string(pt && g_api.type_get_name ? g_api.type_get_name(pt) : "?");
                    }
                    Logger::Instance().Log("camstress", std::string("Transform.") + n + "(" + ps + ")");
                }
            }
        }
        if (g_camStressGetMain && g_camStressGetTransform && g_camStressRotate) {
            if (!g_camStressTransform) {
                g_camStressCamera = reinterpret_cast<void* (*)()>(g_camStressGetMain)();
                if (g_camStressCamera) g_camStressTransform = reinterpret_cast<void* (*)(void*)>(g_camStressGetTransform)(g_camStressCamera);
            }
            if (g_camStressTransform && !g_camStressRotate) {
                Il2CppClass* tcls = g_api.object_get_class(reinterpret_cast<Il2CppObject*>(g_camStressTransform));
                if (tcls) {
                    void* ti = nullptr;
                    while (MethodInfo* m = g_api.class_get_methods(tcls, &ti)) {
                        const char* n = g_api.method_get_name(m);
                        if (!n || std::strcmp(n, "Rotate") != 0) continue;
                        std::string ps;
                        for (uint32_t pi = 0; pi < g_api.method_get_param_count(m); ++pi) {
                            const Il2CppType* pt = g_api.method_get_param(m, pi);
                            ps += (pi ? "," : "") + std::string(pt && g_api.type_get_name ? g_api.type_get_name(pt) : "?");
                        }
                        Logger::Instance().Log("camstress", std::string("object Rotate(") + ps + ")");
                        if (g_api.method_get_param_count(m) == 3) { g_camStressRotate = *reinterpret_cast<void**>(m); break; }
                    }
                }
            }
            if (g_camStressTransform && g_camStressRotate)
                reinterpret_cast<void (*)(void*, float, float, float)>(g_camStressRotate)(g_camStressTransform, 0.0f, 2.0f, 0.0f);
        }
    }
    if (!g_bootOverlayDone.load() && g_runtimeReady.load()) {
        void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
        if (menu) {
            static bool loggedMenuProbe = false;
            size_t off = FieldOffset(FindClass("Menu"), "w_main_menu");
            void* wmain = nullptr;
            if (off != SIZE_MAX) ReadObjectPtr(menu, off, &wmain);
            if (!loggedMenuProbe) {
                loggedMenuProbe = true;
                Logger::Instance().Log("bootprobe", "menu=" + std::to_string((uintptr_t)menu) + " off=" + std::to_string(off) + " wmain=" + std::to_string((uintptr_t)wmain));
            }
            if (wmain) { /* keep black overlay until Chinese loading text */ }
        }
    }
    if (g_ready.load()) {
        // Early replay disabled for stability.
        g_fontPages.ProcessPending();
        static int frames = 0;
        if (Logger::Instance().TestMode() && (frames == 120 || frames == 600)) DumpButtonBounds();
        ++frames;
        if ((Logger::Instance().DebugMode() || Logger::Instance().TestMode()) && frames % 60 == 0) ProcessCommandFile();
        if (Logger::Instance().TestMode() && frames == 10) {
            for (size_t i = 0; i < g_db.pageCount(); ++i) g_fontPages.RequestPage((int)i);
        }
        static bool liveTranslated = false;
        if (!liveTranslated && g_ready.load() && g_bootOverlayDone == false && GetTickCount64() - g_lastLoadingBuild.load() > 1000) {
            TranslateAllTextureTexts();
            liveTranslated = true;
            g_bootOverlayDone = true;
            Logger::Instance().Log("boot", "one-time live UI translation barrier released overlay");
        }
        if (Logger::Instance().DebugMode()) RecordFrameTiming();
    }
}

void ForceFontWithUniverse(void* self, void* font) {
    if (!self || !font) return;
    g_fontPages.Force(self, font, g_model.fontOffset());
}

void ApplyPageFont(void* self, int page) {
    if (!self) return;
    if (page >= 0 && g_fontPages.IsPageReady(page))
        ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
    else
        g_fontPages.Clear(self, g_model.fontOffset());
}

void ForceConfirmFont(void* self, void* font) {
    if (!self || !font) return;
    ForceFontWithUniverse(self, font);
}

void HookPickFont(void* self, void* font) {
    if (g_fontPages.IsForced(self)) return;
    if (self) {
        std::string early = g_model.TextureText(self);
        if (LowerAscii(early).find("voima is still buggy") != std::string::npos)
            Logger::Instance().Debug("bannerprobe", "pickfont-early=" + std::to_string((uintptr_t)font) + " text=" + EscapeForLog(early));
    }
    if (g_ready.load() && self) {
        std::string text = g_model.TextureText(self);
        if (text.find("确定") != std::string::npos)
            Logger::Instance().Log("confirm", "pickfont before forced=0 font=" + std::to_string((uintptr_t)font));
        if (LowerAscii(text).find("voima is still buggy") != std::string::npos)
            Logger::Instance().Debug("bannerprobe", "pickfont=" + std::to_string((uintptr_t)font) + " text=" + EscapeForLog(text));
    }
    if (g_origPickFont) reinterpret_cast<void (*)(void*, void*)>(g_origPickFont)(self, font);
}

static std::set<void*> g_confirmHooksSeen;

void LogConfirmHook(const char* kind, void* self, void* font) {
    if (!font || !self || !g_confirmHooksSeen.insert(self).second) return;
    std::string text = g_model.TextureText(self);
    if (text.find("确定") != std::string::npos)
        Logger::Instance().Log("confirm", std::string(kind) + " forced font=" + std::to_string((uintptr_t)font));
}

void HookSetFloatImpl(void* self, int id, float value) {
    if (g_inConfirmBuild)
        Logger::Instance().Log("mpb", "float id=" + std::to_string(id) + " value=" + std::to_string(value));
    if (g_origSetFloatImpl) g_origSetFloatImpl(self, id, value);
}

void HookSetTextureImpl(void* self, int id, void* texture) {
    if (g_inConfirmBuild)
        Logger::Instance().Log("mpb", "texture id=" + std::to_string(id) + " tex=" + std::to_string((uintptr_t)texture));
    if (g_origSetTextureImpl) g_origSetTextureImpl(self, id, texture);
}

void HookSetVectorImpl(void* self, int id, Vec4 value) {
    if (g_inConfirmBuild)
        Logger::Instance().Log("mpb", "vector id=" + std::to_string(id) + " value=" + std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w));
    if (g_origSetVectorImpl) g_origSetVectorImpl(self, id, value);
}

void LogManagedList(const char* kind, int id, void* list, bool vectors) {
    if (!g_inConfirmBuild || !list) return;
    int n = ReadObjectInt(list, 0x18);
    void* arr = nullptr;
    ReadObjectPtr(list, 0x10, &arr);
    std::string vals;
    if (arr && n > 0) {
        char* data = reinterpret_cast<char*>(arr) + 0x20;
        int lim = n < 8 ? n : 8;
        for (int i = 0; i < lim; ++i) {
            float* f = reinterpret_cast<float*>(data + (vectors ? i * 16 : i * 4));
            vals += (i ? "|" : "") + std::to_string(f[0]) + "," + std::to_string(f[1]);
        }
    }
    Logger::Instance().Log("mpb", std::string(kind) + " id=" + std::to_string(id) + " count=" + std::to_string(n) + " vals=" + vals);
}

void HookSetFloatArray(void* self, int id, void* list) {
    LogManagedList("floatarray", id, list, false);
    if (g_origSetFloatArray) g_origSetFloatArray(self, id, list);
}

void HookSetVectorArray(void* self, int id, void* list) {
    LogManagedList("vectorarray", id, list, true);
    if (g_origSetVectorArray) g_origSetVectorArray(self, id, list);
}

void LogManagedArray(const char* kind, int id, void* arr, int count, bool vectors) {
    if (!g_inConfirmBuild) return;
    std::string vals;
    if (arr && count > 0) {
        char* data = reinterpret_cast<char*>(arr) + 0x20;
        int lim = count < 8 ? count : 8;
        for (int i = 0; i < lim; ++i) {
            float* f = reinterpret_cast<float*>(data + (vectors ? i * 16 : i * 4));
            vals += (i ? "|" : "") + std::to_string(f[0]) + "," + std::to_string(f[1]);
        }
    }
    Logger::Instance().Log("mpb", std::string(kind) + " id=" + std::to_string(id) + " count=" + std::to_string(count) + " vals=" + vals);
}

void HookSetFloatArrayImpl(void* self, int id, void* arr, int count) {
    LogManagedArray("floatarrayimpl", id, arr, count, false);
    if (g_origSetFloatArrayImpl) g_origSetFloatArrayImpl(self, id, arr, count);
}

void HookSetVectorArrayImpl(void* self, int id, void* arr, int count) {
    LogManagedArray("vectorarrayimpl", id, arr, count, true);
    if (g_origSetVectorArrayImpl) g_origSetVectorArrayImpl(self, id, arr, count);
}

std::atomic<bool> g_bannerOverlayStarted{false};
std::atomic<uint64_t> g_bannerOverlayLastSeen{0};

DWORD WINAPI BannerOverlayThread(LPVOID) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 0;
    const wchar_t* kClass = L"VOIMA_ZH_BannerOverlay";
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { Gdiplus::GdiplusShutdown(token); return 0; }
    const std::wstring text = L"voima 仍然一堆bug，并且还没做完...\n所以暂时免费提供给大家 :)";
    while (true) {
        MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        uint64_t now = GetTickCount64();
        if (now - g_bannerOverlayLastSeen.load() > 1500) { ShowWindow(hwnd, SW_HIDE); Sleep(30); continue; }
        HWND game = FindWindowW(nullptr, L"VOIMA");
        if (!game) { Sleep(30); continue; }
        RECT cr{}; GetClientRect(game, &cr); POINT origin{0,0}; ClientToScreen(game, &origin);
        int w = cr.right, h = cr.bottom;
        if (w <= 0 || h <= 0) { Sleep(30); continue; }
        int ow = 1200, oh = 150;
        int ox = origin.x + (w - ow) / 2, oy = origin.y + h - 190;
        Gdiplus::Bitmap bmp(ow, oh, PixelFormat32bppARGB);
        Gdiplus::Graphics g(&bmp);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        g.Clear(Gdiplus::Color(0,0,0,0));
        Gdiplus::Font font(L"Microsoft YaHei", 28, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::StringFormat sf; sf.SetAlignment(Gdiplus::StringAlignmentCenter); sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::SolidBrush brush(Gdiplus::Color(255,245,245,235));
        Gdiplus::RectF rect(0,0,(float)ow,(float)oh);
        g.DrawString(text.c_str(), -1, &font, rect, &sf, &brush);
        HBITMAP hbmp = nullptr; bmp.GetHBITMAP(Gdiplus::Color(0,0,0,0), &hbmp);
        HDC mem = CreateCompatibleDC(nullptr); HGDIOBJ old = SelectObject(mem, hbmp);
        POINT pt{ox,oy}; SIZE size{ow,oh}; POINT src{0,0};
        BLENDFUNCTION bf{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        SetWindowPos(hwnd, HWND_TOPMOST, ox, oy, ow, oh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        UpdateLayeredWindow(hwnd, nullptr, &pt, &size, mem, &src, 0, &bf, ULW_ALPHA);
        SelectObject(mem, old); DeleteObject(hbmp); DeleteDC(mem);
        Sleep(30);
    }
}

void HookRender(void* self, void* camera, bool flag) {
    g_inConfirmRender = (self == g_confirmSelf);
    if (self && g_ready.load()) {
        std::string s = g_model.TextureText(self);
        if (LowerAscii(s).find("voima is still buggy") != std::string::npos) {
            static std::set<void*> bannerSeen;
            if (bannerSeen.insert(self).second) {
                void* font = nullptr;
                ReadObjectPtr(self, FieldOffset(FindClass("TextureText"), "font"), &font);
                void* texIds = nullptr; ReadObjectPtr(self, FieldOffset(FindClass("TextureText"), "texture_ids"), &texIds);
                void* charIds = nullptr; ReadObjectPtr(self, FieldOffset(FindClass("TextureText"), "char_ids"), &charIds);
                int texCount = texIds ? ReadObjectInt(texIds, 0x18) : -1;
                int charCount = charIds ? ReadObjectInt(charIds, 0x18) : -1;
                Logger::Instance().Debug("bannerprobe", "self=" + std::to_string((uintptr_t)self) + " font=" + std::to_string((uintptr_t)font) + " tex=" + std::to_string(texCount) + " chars=" + std::to_string(charCount));
            }
            ObjectKeys keys = g_model.Describe(self, s);
            std::string tr, mk, ms; int page = -1;
            if (g_db.Lookup(keys.keys, s, tr, mk, ms, &page) && !tr.empty() && tr != s) {
                page = g_db.EnsurePageForText(tr, 200);
                static std::set<int> bannerPagesInvalidated;
                if (page >= 0 && bannerPagesInvalidated.insert(page).second) g_fontPages.InvalidatePage(page);
                if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                    void* oldText=nullptr; void* oldFont=nullptr; void* oldMat=nullptr; void* oldTex=nullptr; void* oldChars=nullptr;
                    ReadObjectPtr(self, 0xB8, &oldText); ReadObjectPtr(self, 0x30, &oldFont);
                    ReadObjectPtr(self, 0x38, &oldMat); ReadObjectPtr(self, 0x58, &oldTex); ReadObjectPtr(self, 0x68, &oldChars);
                    ApplyPageFont(self, page);
                    Il2CppString* ns = NewManagedString(tr);
                    MethodInfo* layout = FindMethodInfo("TextureText", "Layout", 2, nullptr);
                    if (ns && layout && g_api.runtime_invoke) {
                        if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                        float dream = 0.0f;
                        void* args[2] = {ns, &dream};
                        Il2CppObject* exc = nullptr;
                        g_api.runtime_invoke(layout, self, args, &exc);
                        void* curTextPtr=nullptr; ReadObjectPtr(self, 0xB8, &curTextPtr);
                        std::string cur = curTextPtr ? ReadManagedString(reinterpret_cast<Il2CppString*>(curTextPtr)) : "";
                        void* curTex=nullptr; void* curChars=nullptr; ReadObjectPtr(self, 0x58, &curTex); ReadObjectPtr(self, 0x68, &curChars);
                        int tc = curTex ? ReadObjectInt(curTex, 0x18) : 0; int cc = curChars ? ReadObjectInt(curChars, 0x18) : 0;
                        bool ok = !exc && cur.find("仍然") != std::string::npos && tc > 0 && cc > 0;
                        if (!ok) {
                            *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0xB8) = oldText;
                            *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x30) = oldFont;
                            *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x38) = oldMat;
                            *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x58) = oldTex;
                            *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x68) = oldChars;
                        } else {
                            ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                        }
                        Logger::Instance().Debug("bannerprobe", std::string("layout=") + (ok ? "commit" : "revert") + " tex=" + std::to_string(tc) + " chars=" + std::to_string(cc));
                    }
                }
            }
        }
        if (s.empty()) {
            static std::set<void*> emptySeen;
            if (emptySeen.insert(self).second) {
                ObjectKeys k = g_model.Describe(self, "");
                Logger::Instance().Debug("renderempty", "self=" + std::to_string((uintptr_t)self) + " key=" + EscapeForLog(k.primary) + " detail=" + EscapeForLog(k.detail));
            }
        } else if (s.find_first_of("") != std::string::npos) {
            static std::set<void*> zhSeen;
            if (zhSeen.insert(self).second) {
                ObjectKeys k = g_model.Describe(self, s);
                Logger::Instance().Debug("renderzh", "self=" + std::to_string((uintptr_t)self) + " key=" + EscapeForLog(k.primary) + " text=" + EscapeForLog(s));
            }
        } else if (s.find('?') != std::string::npos) {
            static std::set<void*> rawSeen;
            if (rawSeen.insert(self).second)
                Logger::Instance().Log("renderraw", "self=" + std::to_string((uintptr_t)self) + " text=" + EscapeForLog(s));
        }
        std::string gen;
        if (TryTranslateGenerationText(s, gen)) {
            Logger::Instance().Log("gen", "render out=" + EscapeForLog(gen));
            int page = g_db.EnsurePageForText(gen, 200);
            if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                Il2CppString* ns = NewManagedString(gen);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    size_t off = FieldOffset(FindClass("TextureText"), "text");
                    if (off != SIZE_MAX) *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + off) = ns;
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    if (g_origRebuild) g_origRebuild(self);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    if (g_origRender) g_origRender(self, camera, flag);
                    g_inConfirmRender = false;
                    return;
                }
            }
        }
    }
    if (self) {
        std::string rs = g_model.TextureText(self);
        bool nonAscii = false;
        for (unsigned char c : rs) if (c >= 0x80) { nonAscii = true; break; }
        if (!nonAscii) {
            static std::set<void*> asciiCleared;
            bool first = asciiCleared.insert(self).second;
            g_fontPages.Clear(self, g_model.fontOffset());
            if (first && g_origRebuild) g_origRebuild(self);
        }
    }
    if (g_origRender) g_origRender(self, camera, flag);
    g_inConfirmRender = false;
}

void HookDrawMesh(void* mesh, int submesh, Mat4 matrix, void* material, int layer, void* camera,
                  void* props, int castShadows, bool receiveShadows, void* transform,
                  int lightProbeUsage, void* probeVolume) {
    if (g_inConfirmRender) {
        static int calls = 0;
        if (++calls % 20 == 1)
            Logger::Instance().Log("drawmesh", "mesh=" + std::to_string((uintptr_t)mesh) + " sub=" + std::to_string(submesh) +
                " pos=" + std::to_string(matrix.m[12]) + "," + std::to_string(matrix.m[13]) + "," + std::to_string(matrix.m[14]));
    }
    if (g_origDrawMesh) g_origDrawMesh(mesh, submesh, matrix, material, layer, camera, props, castShadows, receiveShadows, transform, lightProbeUsage, probeVolume);
}

void HookDrawInstanced(void* mesh, int submesh, void* material, void* matrices, int count,
                        void* props, int castShadows, bool receiveShadows, int layer,
                        void* camera, int lightProbeUsage, void* probeVolume) {
    if (g_inConfirmRender && g_meshVertexCountFn && !g_matrixMeasure) {
        static bool logged = false;
        if (!logged) {
            logged = true;
            int vc = reinterpret_cast<int (*)(void*)>(g_meshVertexCountFn)(mesh);
            Logger::Instance().Log("mesh", "confirm mesh vertices=" + std::to_string(vc) + " drawCount=" + std::to_string(count));
        }
    }
    if (g_inConfirmRender && g_matrixLog && matrices) {
        std::lock_guard<std::mutex> lock(g_matrixMutex);
        if (g_matrixOut.is_open()) {
            char* base = reinterpret_cast<char*>(matrices) + 0x20;
            ++g_matrixFrame;
            for (int i = 0; i < count; ++i) {
                float* m = reinterpret_cast<float*>(base + (size_t)i * 64);
                g_matrixOut << g_matrixFrame << '\t' << i << '\t' << m[12] << '\t' << m[13] << '\t' << m[14] << '\n';
            }
        }
    }
    if (g_inConfirmRender && !g_matrixMeasure && matrices && count > 0 && count <= 32) {
        static uint64_t jitterAt = 0;
        static float jitterX[32]{};
        static float jitterY[32]{};
        uint64_t now = GetTickCount64();
        if (now - jitterAt >= 16) {
            jitterAt = now;
            for (int i = 0; i < 32; ++i) {
                jitterX[i] = ((float)(std::rand() % 3001) / 1000.0f - 1.5f);
                jitterY[i] = ((float)(std::rand() % 3001) / 1000.0f - 1.5f);
            }
        }
        char* base = reinterpret_cast<char*>(matrices) + 0x20;
        float originalX[32]{};
        float originalY[32]{};
        for (int i = 0; i < count; ++i) {
            float* m = reinterpret_cast<float*>(base + (size_t)i * 64);
            originalX[i] = m[12];
            originalY[i] = m[13];
            m[12] = originalX[i] + 4.0f + jitterX[i];
            m[13] = originalY[i] + jitterY[i];
        }
        if (g_origDrawInstanced) g_origDrawInstanced(mesh, submesh, material, matrices, count, props, castShadows, receiveShadows, layer, camera, lightProbeUsage, probeVolume);
        for (int i = 0; i < count; ++i) {
            float* m = reinterpret_cast<float*>(base + (size_t)i * 64);
            m[12] = originalX[i];
            m[13] = originalY[i];
        }
        return;
    }
    if (g_origDrawInstanced) g_origDrawInstanced(mesh, submesh, material, matrices, count, props, castShadows, receiveShadows, layer, camera, lightProbeUsage, probeVolume);
}

void HookShake(void* self, int a, int b, float c) {
    void* font = self ? g_fontPages.ForcedFont(self) : nullptr;
    if (font) ForceFontWithUniverse(self, font);
    LogConfirmHook("shake", self, font);
    if (g_origShake) g_origShake(self, a, b, c);
    if (font) ForceFontWithUniverse(self, font);
}

bool HookUniverseFontCurrent(void* self) {
    bool result = g_origUniverseFontCurrent ? g_origUniverseFontCurrent(self) : false;
    if (result && self && g_fontPages.IsForced(self)) {
        if (g_model.TextureText(self).find("确定") != std::string::npos)
            Logger::Instance().Log("confirm", "UniverseFontCurrent true -> force false");
        return false;
    }
    return result;
}

void* HookBounced(void* self, float value) {
    void* font = self ? g_fontPages.ForcedFont(self) : nullptr;
    if (font) ForceFontWithUniverse(self, font);
    LogConfirmHook("bounced", self, font);
    void* result = g_origBounced ? g_origBounced(self, value) : nullptr;
    if (font) ForceFontWithUniverse(self, font);
    return result;
}

void* FindBind(const std::string& name);
void* FindBindByActionPrefix(const std::string& action);
std::string KeyLabelFromBind(void* bind);

std::string SpeechKeyName(const std::string& raw) {
    std::string k = LowerAscii(Trim(raw));
    if (k == "left mouse button") return "鼠标左键";
    if (k == "right mouse button") return "鼠标右键";
    if (k == "middle mouse button") return "鼠标中键";
    if (k == "side mouse button a") return "鼠标侧键 A";
    if (k == "side mouse button b") return "鼠标侧键 B";
    if (k == "side mouse button c") return "鼠标侧键 C";
    if (k == "side mouse button d") return "鼠标侧键 D";
    if (k == "control") return "ctrl";
    if (k == "escape") return "esc";
    return std::string();
}

std::string SpeechKeyFromBind(void* bind) {
    if (!bind) return std::string();
    int key = ReadObjectInt(bind, 0x20);
    int c = ReadObjectInt(bind, 0x52);
    if (key == 323 || c == 17) return "鼠标左键";
    if (key == 324 || c == 18) return "鼠标右键";
    if (key == 325 || c == 19) return "鼠标中键";
    if (key == 326) return "鼠标侧键 A";
    if (key == 327) return "鼠标侧键 B";
    if (key == 328) return "鼠标侧键 C";
    if (key == 329) return "鼠标侧键 D";
    std::string label = KeyLabelFromBind(bind);
    if (label.size() >= 2 && label.front() == '[' && label.back() == ']')
        label = label.substr(1, label.size() - 2);
    return label;
}

void RecordSpeech(const std::string& input, const std::string& translated) {
    static std::set<std::string> seen;
    if (!seen.insert(input).second) return;
    std::ofstream out(g_modDir + "\\logs\\speech.tsv", std::ios::app);
    if (!out) return;
    out << EscapeForLog(input) << '\t' << EscapeForLog(translated) << '\n';
}

std::string LocalizeEmbeddedItemWords(std::string text) {
    static const std::set<std::string> kSkip = {
        "a","an","and","are","as","at","be","but","by","can","do","for","from","get","got","had","has","have","he","her","his","i","if","in","is","it","its","me","my","ned","no","not","of","on","or","our","pls","plz","she","so","sum","that","the","their","them","they","this","to","u","us","was","we","were","with","you","your"
    };
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (!std::isalpha(c)) { out.push_back(text[i++]); continue; }
        size_t end = i;
        while (end < text.size() && (std::isalpha((unsigned char)text[end]) || text[end] == ' ' || text[end] == '\'')) ++end;
        while (end > i && text[end - 1] == ' ') --end;
        std::string repl;
        size_t consumed = i;
        // Prefer the longest complete item name ("wooden shotgun"), then fall
        // back word by word. This prevents "wooden 霰弹枪" and wrong splits.
        std::string cand = text.substr(i, end - i);
        while (!cand.empty()) {
            std::string key = LowerAscii(cand);
            if (key.size() > 1 && !kSkip.count(key)) {
                std::string tr, mk, ms; int pg = -1;
                if (g_db.Lookup({"ITEM:" + key}, cand, tr, mk, ms, &pg) &&
                    !tr.empty() && tr != cand && tr.find('{') == std::string::npos) {
                    repl = tr; consumed = i + cand.size(); break;
                }
            }
            size_t sp = cand.find_last_of(' ');
            if (sp == std::string::npos) break;
            cand.resize(sp);
        }
        if (repl.empty()) {
            size_t j = i;
            while (j < text.size() && (std::isalpha((unsigned char)text[j]) || text[j] == '\'')) ++j;
            std::string word = LowerAscii(text.substr(i, j - i));
            if (word.size() > 1 && !kSkip.count(word)) {
                std::string tr, mk, ms; int pg = -1;
                if (g_db.Lookup({"ITEM:" + word}, word, tr, mk, ms, &pg) &&
                    !tr.empty() && tr != word && tr.find('{') == std::string::npos)
                    repl = tr;
            }
            consumed = j;
        }
        if (!repl.empty()) out += repl; else out.append(text, i, consumed - i);
        i = consumed;
    }
    // Chinese words must not keep the English inter-word space.
    std::string clean;
    for (size_t i = 0; i < out.size();) {
        if (out[i] == ' ' && !clean.empty()) {
            unsigned char prev = (unsigned char)clean.back();
            size_t j = i; while (j < out.size() && out[j] == ' ') ++j;
            if (j < out.size() && prev >= 0x80 && (unsigned char)out[j] >= 0x80) { i = j; continue; }
        }
        clean.push_back(out[i++]);
    }
    return clean;
}

std::string LocalizeSpeechKeyPrefix(std::string text) {
    std::string prefix;
    std::string suffix;
    bool bracketed = false;
    if (!text.empty() && text[0] == '[') {
        size_t close = text.find(']');
        if (close == std::string::npos) return text;
        prefix = text.substr(1, close - 1);
        suffix = text.substr(close + 1);
        bracketed = true;
    } else {
        size_t sp = text.find(' ');
        if (sp == std::string::npos) return text;
        prefix = text.substr(0, sp);
        suffix = text.substr(sp);
    }
    std::string localized = SpeechKeyName(prefix);
    if (localized.empty()) localized = SpeechKeyFromBind(FindBind(LowerAscii(Trim(prefix))));
    if (localized.empty()) return text;
    // The speech bubble does not add brackets itself; always render the key
    // inside 【】-style square brackets, including when the source placeholder
    // was unbracketed ("inventory 交易").
    return "[" + localized + "]" + suffix;
}

void MarkDeathContext(const char* role) {
    g_deathContext = role ? role : "";
    g_deathContextAt = GetTickCount64();
}

void HookVoidDamage(void* self) {
    MarkDeathContext("void");
    if (g_origVoidDamage) g_origVoidDamage(self);
}

void HookSuffocateDamage(void* self) {
    MarkDeathContext("suffocate");
    if (g_origSuffocateDamage) g_origSuffocateDamage(self);
}

void HookDeathDrop(void* self, void* hurter) {
    MarkDeathContext("card");
    if (g_origDeathDrop) g_origDeathDrop(self, hurter);
}

std::string MarkMissingGlyphs(const std::string& text) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) { out.push_back((char)c); ++i; continue; }
        size_t n = (c >= 0xF0) ? 4 : (c >= 0xE0 ? 3 : 2);
        if (i + n > text.size()) { out.push_back('?'); break; }
        std::string ch = text.substr(i, n);
        if (g_db.FindPageForText(ch, 200) < 0) {
            Logger::Instance().Log("missingglyph", "char=" + EscapeForLog(ch));
            out.push_back('?');
        } else {
            out += ch;
        }
        i += n;
    }
    return out;
}

std::string TranslateHudKeyHint(const std::string& input);

void HookBuildRotated(void* self, Il2CppString* text, float x, float y, float z, Quat rotation, float dream) {
    if (self && text && g_ready.load()) {
        std::string s = ReadManagedString(text);
        if (s.empty()) Logger::Instance().Log("emptybuild", "rotated source empty");
        std::string lower = LowerAscii(s);
        if (lower.find("inventory") != std::string::npos && lower.find("to open") != std::string::npos) {
            std::string local = Trim(TranslateHudKeyHint(s));
            int page = g_db.EnsurePageForText(local, 200);
            if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                Il2CppString* ns = NewManagedString(local);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    if (g_origBuildRotated) g_origBuildRotated(self, ns, x, y, z, rotation, dream);
                    return;
                }
            }
        }
        if (lower.find("continue playing") != std::string::npos) { /* keep overlay */ }
        std::string key = "X:TextureText|" + LowerAscii(Trim(s));
        // BuildRotated is also used for world prompts such as "[e] to trade".
        // Their key is dynamic, so use the same canonical key as Describe().
        if (lower.find(" to trade") != std::string::npos)
            key = "X:TextureText|to trade";
        if (lower.find("inventory") != std::string::npos && lower.find("to open") != std::string::npos)
            key = "X:TextureText|inventory to open";
        if (lower.rfind("you ", 0) == 0 &&
            (lower.find("were killed by ") != std::string::npos ||
             lower.find("fell to your death") != std::string::npos ||
             lower.find("fell to their death") != std::string::npos ||
             lower.find("fell into the void") != std::string::npos ||
             lower.find("drowned.") != std::string::npos ||
             lower.find("exploded") != std::string::npos ||
             lower.find("squished by a slab") != std::string::npos ||
             lower.find("hit a wall too hard") != std::string::npos ||
             lower.find("died from a falling") != std::string::npos ||
             lower == "you died.")) {
            if (!g_deathContext.empty() && GetTickCount64() - g_deathContextAt < 5000)
                key = "DEATH:" + g_deathContext;
        }
        std::string translated, matchedKey, matchedSource;
        int page = -1;
        struct SpeechCacheEntry { Il2CppString* ns; int page; };
        static std::map<std::string, SpeechCacheEntry> speechCache;
        auto cit = speechCache.find(s);
        if (cit != speechCache.end() && cit->second.ns) {
            ApplyPageFont(self, cit->second.page);
            if (g_origBuildRotated) g_origBuildRotated(self, cit->second.ns, x, y, z, rotation, dream);
            return;
        }
        // Death text is accepted only within a recent Card death context.
        const bool hasDedicatedKey = (key == "X:TextureText|to trade" || key == "X:TextureText|inventory to open" || key.rfind("DEATH:", 0) == 0); // object role + source pattern
        bool found = hasDedicatedKey && g_db.Lookup({key}, s, translated, matchedKey, matchedSource, &page);
        if (!found)
            found = g_db.LookupByPrefix("ANIMAL:characters/bear/", s, translated, matchedKey, matchedSource, &page);
        if (found && !translated.empty()) {
            const std::string baseTranslated = LocalizeSpeechKeyPrefix(translated);
            translated = baseTranslated;
            if (matchedKey.rfind("ANIMAL:characters/bear/bear", 0) == 0)
                translated = LocalizeEmbeddedItemWords(translated);
            if (translated.empty()) {
                if (self) g_fontPages.Clear(self, g_model.fontOffset());
                if (g_origBuildRotated) g_origBuildRotated(self, text, x, y, z, rotation, dream);
                return;
            }
            translated = MarkMissingGlyphs(translated);
            // Never create a new page during speech. If an item substitution
            // adds glyphs outside the shared speech pages, fall back to the
            // sentence without item localization (item name stays English).
            int dynamicPage = g_db.FindPageForText(translated, 200);
            if (dynamicPage < 0 && translated != baseTranslated) {
                translated = baseTranslated;
                dynamicPage = g_db.FindPageForText(translated, 200);
            }
            if (dynamicPage < 0) {
                ApplyPageFont(self, -1);
                if (g_origBuildRotated) g_origBuildRotated(self, text, x, y, z, rotation, dream);
                return;
            }
            page = dynamicPage;
            bool ready = true;
            if (page >= 0) {
                if (g_fontPages.IsPageReady(page) || g_fontPages.EnsurePage(page) >= 0)
                    ApplyPageFont(self, page);
                else ready = false;
            }
            if (ready) {
                Il2CppString* ns = NewManagedString(translated);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    RecordSpeech(s, translated);
                    Logger::Instance().Debug("speechfirst", "page=" + std::to_string(page) + " text=" + EscapeForLog(translated));
                    speechCache[s] = { ns, page };
                    if (g_origBuildRotated) g_origBuildRotated(self, ns, x, y, z, rotation, dream);
                    if (page >= 0 && g_fontPages.IsPageReady(page))
                        ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    return;
                }
            } else {
                Il2CppString* fail = NewManagedString(kFontFailureText);
                if (fail) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(fail), false);
                    RecordSpeech(s, kFontFailureText);
                    Logger::Instance().Log("speech", "font page failed -> null marker");
                    if (g_origBuildRotated) g_origBuildRotated(self, fail, x, y, z, rotation, dream);
                    return;
                }
            }
        }
    }
    if (self && text) RecordSpeech(ReadManagedString(text), "");
    if (self) g_fontPages.Clear(self, g_model.fontOffset());
                if (g_origBuildRotated) g_origBuildRotated(self, text, x, y, z, rotation, dream);
}

bool TryTranslateGenerationText(const std::string& input, std::string& out);
void HookUiTextSet(void* self, Il2CppString* value) {
    if (value && g_ready.load()) {
        std::string s = ReadManagedString(value);
        std::string gen;
        if (TryTranslateGenerationText(s, gen)) {
            Logger::Instance().Log("gen", "ui text out=" + EscapeForLog(gen));
            Il2CppString* ns = NewManagedString(gen);
            if (ns) {
                if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                if (g_origUiSetText) g_origUiSetText(self, ns);
                return;
            }
        }
    }
    if (g_origUiSetText) g_origUiSetText(self, value);
}

void HookRebuild(void* self) {
    if (self && g_ready.load()) {
        std::string s = g_model.TextureText(self);
        std::string gen;
        if (TryTranslateGenerationText(s, gen)) {
            Logger::Instance().Log("gen", "rebuild out=" + EscapeForLog(gen));
            int page = g_db.EnsurePageForText(gen, 200);
            if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                Il2CppString* ns = NewManagedString(gen);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    size_t off = FieldOffset(FindClass("TextureText"), "text");
                    if (off != SIZE_MAX) *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + off) = ns;
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    if (g_origRebuild) g_origRebuild(self);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    return;
                }
            }
        }
    }
    if (g_origRebuild) g_origRebuild(self);
}

void HookBuildSB(void* self, void* builder, float x, float y, float z, float effect, float fontSize) {
    ++g_buildCalls;
    if (g_ready.load() && builder && g_api.runtime_invoke && g_api.object_new) {
        MethodInfo* toStringInfo = FindMethodInfo("StringBuilder", "ToString", 0, nullptr);
        if (toStringInfo) {
            Il2CppObject* exc = nullptr;
            Il2CppString* s = reinterpret_cast<Il2CppString*>(g_api.runtime_invoke(toStringInfo, builder, nullptr, &exc));
            std::string gen;
            if (!exc && s && TryTranslateGenerationText(ReadManagedString(s), gen)) {
                Logger::Instance().Log("gen", "sb out=" + EscapeForLog(gen));
                int genPage = g_db.EnsurePageForText(gen, 200);
                if (genPage < 0 || g_fontPages.EnsurePage(genPage) < 0) {
                    if (g_origBuildSB) g_origBuildSB(self, builder, x, y, z, effect, fontSize);
                    return;
                }
                Il2CppClass* sbClass = FindClass("StringBuilder");
                MethodInfo* ctor = FindMethodInfo("StringBuilder", ".ctor", 1, "System.String");
                if (sbClass && ctor) {
                    Il2CppString* ns = NewManagedString(gen);
                    if (ns) {
                        if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                        void* args[1] = {ns};
                        Il2CppObject* newBuilder = g_api.object_new(sbClass);
                        exc = nullptr;
                        if (newBuilder) g_api.runtime_invoke(ctor, newBuilder, args, &exc);
                        if (newBuilder && !exc) {
                            ForceFontWithUniverse(self, g_fontPages.FontForPage(genPage));
                            if (g_origBuildSB) g_origBuildSB(self, newBuilder, x, y, z, effect, fontSize);
                            ForceFontWithUniverse(self, g_fontPages.FontForPage(genPage));
                            return;
                        }
                    }
                }
            }
        }
    }
    if (g_origBuildSB) g_origBuildSB(self, builder, x, y, z, effect, fontSize);
}

void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
}

size_t Utf8Count(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

void ReplacePadded(std::string& s, const std::string& from, const std::string& to) {
    size_t p = 0;
    const size_t fromCount = Utf8Count(from);
    const size_t toCount = Utf8Count(to);
    const std::string pad = toCount < fromCount ? std::string(fromCount - toCount, ' ') : std::string();
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to + pad);
        p += to.size() + pad.size();
    }
}

void* HudSoul() {
    void* souls = StaticPtr(FindField(FindClass("Lord"), "souls"));
    void* soul = nullptr;
    if (souls) ReadObjectPtr(souls, 0x20, &soul);
    return soul;
}

void* FindBind(const std::string& name) {
    void* soul = HudSoul();
    void* list = nullptr;
    if (!soul) return nullptr;
    ReadObjectPtr(soul, FieldOffset(FindClass("Soul"), "keys"), &list);
    void* found = nullptr;
    ForEachManagedList(list, [&](void* b, int) {
        if (!b || found) return;
        void* np = nullptr;
        if (!ReadObjectPtr(b, 0x10, &np) || !np) return;
        if (LowerAscii(Trim(ReadManagedString(reinterpret_cast<Il2CppString*>(np)))) == LowerAscii(Trim(name))) found = b;
    });
    return found;
}

void* FindBindByActionPrefix(const std::string& action) {
    void* soul = HudSoul();
    void* list = nullptr;
    if (!soul) return nullptr;
    ReadObjectPtr(soul, FieldOffset(FindClass("Soul"), "keys"), &list);
    void* best = nullptr;
    size_t bestLen = 0;
    ForEachManagedList(list, [&](void* b, int) {
        if (!b) return;
        void* np = nullptr;
        if (!ReadObjectPtr(b, 0x10, &np) || !np) return;
        std::string name = LowerAscii(Trim(ReadManagedString(reinterpret_cast<Il2CppString*>(np))));
        if (name.empty() || name.find(' ') == std::string::npos) return;
        if (action == name || action.rfind(name + " ", 0) == 0) {
            if (name.size() > bestLen) {
                best = b;
                bestLen = name.size();
            }
        }
    });
    return best;
}

std::string KeyLabelFromBind(void* bind) {
    if (!bind) return std::string();
    int key = ReadObjectInt(bind, 0x20);
    int c = ReadObjectInt(bind, 0x52);
    // Bind.key is authoritative. Bind.c keeps a default mouse marker on some
    // keyboard rebinds, so checking c first would wrongly draw a mouse icon
    // after binding primary action to u.
    if (key == 323) return "primary action";
    if (key == 324) return "secondary action";
    if (key == 325) return "copy";
    if (key == 326) return "[鼠标侧键 A]";
    if (key == 327) return "[鼠标侧键 B]";
    if (key == 328) return "[鼠标侧键 C]";
    if (key == 329) return "[鼠标侧键 D]";
    if (key == 32) return "[space]";
    if (key >= 32 && key < 127) { char ch[8] = {}; snprintf(ch, sizeof(ch), "[%c]", key); return ch; }
    if (c >= 32 && c < 127) { char ch[8] = {}; snprintf(ch, sizeof(ch), "[%c]", c); return ch; }
    if (c == 20) return "[滚轮]";
    char out[16] = {};
    if (key >= 97 && key <= 122) snprintf(out, sizeof(out), "[%c]", key);
    else if (key >= 65 && key <= 90) snprintf(out, sizeof(out), "[%c]", key + 32);
    else if (key >= 48 && key <= 57) snprintf(out, sizeof(out), "[%c]", key);
    else {
        switch (key) {
            case 9: return "[tab]"; case 8: return "[backspace]"; case 32: return "[space]";
            case 13: return "[enter]"; case 27: return "[esc]"; case 304: return "[shift]";
            case 306: return "[ctrl]"; case 308: return "[alt]"; case 273: return "[上方向键]";
            case 274: return "[下方向键]"; case 275: return "[右方向键]"; case 276: return "[左方向键]";
            case 301: return "[caps lock]";
            case 277: return "[insert]"; case 278: return "[home]"; case 279: return "[end]";
            case 280: return "[page up]"; case 281: return "[page down]";
            case 127: return "[delete]"; case 19: return "[pause]";
            case 300: return "[num lock]"; case 302: return "[scroll lock]";
            case 303: return "[shift]"; case 305: return "[ctrl]"; case 307: return "[alt]";
            default:
                if (key >= 282 && key <= 293) {
                    char f[8] = {};
                    snprintf(f, sizeof(f), "[F%d]", key - 281);
                    return f;
                }
                return std::string();
        }
    }
    return out;
}

void* HudBindForAction(const std::string& label, const std::string& action) {
    std::string a = LowerAscii(Trim(action));
    static const std::map<std::string, std::string> actionBinds = {
        // The HUD key label already names the bound action (primary action,
        // secondary action, copy, ...). Only "more" has a stale/default label.
        {"more", "edit menu"}
    };
    auto it = actionBinds.find(a);
    if (it != actionBinds.end()) {
        if (void* bind = FindBind(it->second)) return bind;
    }

    if (a == "cut") {
        auto logBind = [](const char* tag, void* b) {
            if (!b) { Logger::Instance().Debug("hudbind", std::string(tag) + "=null"); return; }
            void* np = nullptr; std::string name;
            if (ReadObjectPtr(b, 0x10, &np) && np) name = ReadManagedString(reinterpret_cast<Il2CppString*>(np));
            Logger::Instance().Debug("hudbind", std::string(tag) + " name=" + EscapeForLog(name) + " key=" + std::to_string(ReadObjectInt(b, 0x20)) + " c=" + std::to_string(ReadObjectInt(b, 0x52)));
        };
        void* jb = FindBind("jump");
        void* cb = FindBind("cut");
        logBind("jump", jb); logBind("cut", cb);
        if (jb) return jb;
    }
    // Prefer the bind named after the action. This keeps the HUD following
    // rebinds for every action instead of trusting a stale display label.
    if (void* bind = FindBind(a)) return bind;
    if (a == "cut") {
        for (const char* alias : {"chop", "slice", "saw"}) {
            if (void* bind = FindBind(alias)) return bind;
        }
    }

    // Dynamic actions: "edit every <object>" and "wall thickness <value>".
    if (a.rfind("edit every ", 0) == 0) {
        for (const char* candidate : {"edit every object", "edit every", "edit all"}) {
            if (void* bind = FindBind(candidate)) return bind;
        }
    }
    if (a.rfind("wall thickness ", 0) == 0) {
        if (void* bind = FindBind("wall thickness")) return bind;
    }

    // Also accept a bind whose name is a stable prefix of the action.
    if (void* bind = FindBindByActionPrefix(a)) return bind;

    // Last resort: the game-supplied display label.
    return FindBind(Trim(label));
}

std::string HudKeyLabel(const std::string& label, const std::string& action) {
    return KeyLabelFromBind(HudBindForAction(label, action));
}

void RecordHudAction(const std::string& action, const std::string& translated, const std::string& line) {
    static std::set<std::string> seen;
    std::string key = LowerAscii(Trim(action));
    if (key.empty() || !seen.insert(key).second) return;
    std::ofstream out(g_modDir + "\\logs\\hud_actions.tsv", std::ios::app);
    if (!out) return;
    out << EscapeForLog(action) << '\t' << EscapeForLog(translated) << '\t' << EscapeForLog(line) << '\n';
}


void DumpHudBindsOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    void* soul = HudSoul();
    void* list = nullptr;
    if (!soul) return;
    ReadObjectPtr(soul, FieldOffset(FindClass("Soul"), "keys"), &list);
    ForEachManagedList(list, [&](void* b, int) {
        if (!b) return;
        void* np = nullptr;
        if (!ReadObjectPtr(b, 0x10, &np) || !np) return;
        std::string name = ReadManagedString(reinterpret_cast<Il2CppString*>(np));
        int key = ReadObjectInt(b, 0x20);
        int c = ReadObjectInt(b, 0x52);
        std::string lower = LowerAscii(name);
        if (lower.find("cut") != std::string::npos || key == 32 || c == 32)
            Logger::Instance().Debug("hudbind", "name='" + EscapeForLog(name) + "' key=" + std::to_string(key) + " c=" + std::to_string(c));
    });
}

std::string TranslateHudAction(const std::string& action) {
    static const std::map<std::string, std::string> actions = {
        {"attack", "攻击"}, {"build", "建造"}, {"set grid", "设置网格"},
        {"copy size", "复制尺寸"}, {"toggle grid", "切换网格"}, {"more", "更多"},
        {"draw", "绘制"}, {"wall thickness off", "关闭墙体厚度"},
        {"snap", "吸附"}, {"world edit", "世界编辑"},
        {"start setting grid", "开始设置网格"}, {"complete drawing", "完成绘制"},
        {"edit world settings", "编辑世界设置"}, {"launch", "发射"}, {"to open", "打开"}, {"for open", "打开"},
        {"mine", "挖掘"}, {"place", "放置"}, {"jump", "跳跃"}, {"drop", "丢弃"},
        {"use", "使用"}, {"equip", "装备"}, {"punch", "拳击"}, {"shoot", "射击"},
        {"swing", "挥动"}, {"throw", "投掷"}, {"hold", "手持"}, {"drink", "饮用"},
        {"eat", "食用"}, {"slump", "瘫倒"}, {"run", "奔跑"}, {"respawn", "重生"},
        {"open inventory", "打开背包"}, {"craft", "合成"}, {"back", "返回"},
        {"cut", "切割"}, {"chop", "劈砍"}, {"dig", "挖掘"}, {"harvest", "收获"},
        {"pick", "拾取"}, {"grab", "抓取"}, {"plant", "种植"}, {"pour", "倒出"},
        {"fill", "填充"}, {"paint", "涂色"}, {"pull", "拉动"}, {"push", "推动"},
        {"open", "打开"}, {"close", "关闭"}, {"mount", "骑乘"}, {"dismount", "下马"},
        {"sprint", "冲刺"}, {"dash", "冲刺"}, {"roll", "翻滚"}, {"block", "格挡"},
        {"parry", "招架"}, {"charge", "蓄力"}, {"reload", "装填"}, {"aim", "瞄准"}
    };
    std::string a = LowerAscii(Trim(action));
    auto it = actions.find(a);
    if (it != actions.end()) return it->second;

    // Fixed actions can also be supplied from translations.tsv as HUD:<action>,
    // so new HUD entries do not require another DLL rebuild.
    {
        std::string translated, matchedKey, matchedSource;
        if (g_db.Lookup({"HUD:" + a}, action, translated, matchedKey, matchedSource))
            return translated;
    }

    // "edit every <object>" has a dynamic suffix.
    if (a.rfind("edit every ", 0) == 0) {
        std::string object = a.substr(11);
        std::string translated, matchedKey, matchedSource;
        if (g_db.Lookup({"TIP:" + object}, object, translated, matchedKey, matchedSource))
            return "批量编辑" + translated;
        return "批量编辑";
    }

    // "wall thickness <value>" has a numeric suffix.
    if (a.rfind("wall thickness ", 0) == 0) {
        std::string value = a.substr(15);
        if (!value.empty()) return "墙体厚度 " + value;
    }
    return std::string();
}

bool LooksLikeHudHint(const std::string& input) {
    size_t start = 0;
    int controlLines = 0;
    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        size_t sep = std::string::npos;
        for (size_t i = 0; i < line.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(line[i]);
            if (c < 0x20 && c != '\t') sep = i;
        }
        if (sep != std::string::npos) {
            ++controlLines;
            std::string action = Trim(line.substr(sep + 1));
            if (!TranslateHudAction(action).empty()) return true;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (controlLines == 0) {
        // A single unbound HUD line such as "space cut". Only accept known
        // physical key prefixes plus a known action, so arbitrary world text is
        // not treated as a HUD hint.
        static const char* prefixes[] = {"space ", "primary action ", "secondary action ",
            "left mouse button ", "right mouse button ", "middle mouse button ",
            "tab ", "ctrl ", "shift ", "alt ", "inventory "};
        std::string lower = LowerAscii(Trim(input));
        for (const char* p : prefixes) {
            size_t n = std::strlen(p);
            if (lower.rfind(p, 0) == 0 && !TranslateHudAction(Trim(input.substr(n))).empty())
                return true;
        }
    }
    return controlLines >= 2;
}

std::string TranslateHudKeyHint(const std::string& input) {
    std::string out;
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        // The game separates the bound key name from the action with a control byte.
        size_t sep = std::string::npos;
        for (size_t i = 0; i < line.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(line[i]);
            if (c < 0x20 && c != '\t') sep = i;
        }
        if (sep != std::string::npos) {
            size_t actionStart = sep + 1;
            while (actionStart < line.size() && line[actionStart] == ' ') ++actionStart;
            std::string action = line.substr(actionStart);

            size_t keyStart = 0;
            for (size_t i = 0; i < sep; ++i) {
                unsigned char c = static_cast<unsigned char>(line[i]);
                if (c < 0x20 && c != '\t') keyStart = i + 1;
            }
            std::string keyText = line.substr(keyStart, sep - keyStart);
            void* keyBind = HudBindForAction(Trim(keyText), action);
            std::string keyLabel = KeyLabelFromBind(keyBind);
            if (keyLabel.empty() && LowerAscii(Trim(action)) == "cut") DumpHudBindsOnce();
            if (!keyLabel.empty()) {
                line.replace(keyStart, keyText.size(), keyLabel);
                sep = std::string::npos;
                for (size_t i = 0; i < line.size(); ++i) {
                    unsigned char c = static_cast<unsigned char>(line[i]);
                    if (c < 0x20 && c != '\t') sep = i;
                }
                actionStart = sep == std::string::npos ? line.size() : sep + 1;
                while (actionStart < line.size() && line[actionStart] == ' ') ++actionStart;
            }

            std::string translated = TranslateHudAction(action);
            RecordHudAction(action, translated, line);
            if (!translated.empty()) {
                while (!action.empty() && (action.back() == ' ' || action.back() == '\r')) action.pop_back();
                size_t oldCount = Utf8Count(action);
                size_t newCount = Utf8Count(translated);
                std::string padded = translated;
                if (newCount < oldCount) padded.append(oldCount - newCount, ' ');
                line = line.substr(0, actionStart) + padded;
            }
        } else {
            // Some HUD lines have no BIND control byte: "space cut". Keep the
            // key text, translate only the trailing action.
            size_t sp = line.find_last_of(' ');
            if (sp != std::string::npos && sp + 1 < line.size()) {
                std::string key = line.substr(0, sp);
                std::string action = line.substr(sp + 1);
                std::string translated = TranslateHudAction(action);
                if (!translated.empty()) {
                    RecordHudAction(action, translated, line);
                    // The unbound line still has a real key/action pair; run the
                    // same Bind lookup as the normal HUD path instead of leaving
                    // the raw key text.
                    void* bind = HudBindForAction(Trim(key), action);
                    if (LowerAscii(Trim(action)) == "cut" && KeyLabelFromBind(bind).empty()) DumpHudBindsOnce();
                    std::string keyLabel = KeyLabelFromBind(bind);
                    if (!keyLabel.empty()) key = keyLabel;
                    line = key + " " + translated;
                }
            }
        }
        out += line;
        if (end == std::string::npos) break;
        out.push_back('\n');
        start = end + 1;
    }
    return out;
}


bool TryTranslateGenerationText(const std::string& input, std::string& out) {
    if (!g_generationContext.load()) return false;
    static const std::string kGenerating = "generating the world";
    static const std::string kRendering = "rendering the world";
    auto translateLine = [](const std::string& raw) -> std::string {
        std::string text = Trim(raw);
        std::string lower = LowerAscii(text);
        if (lower.rfind(kGenerating, 0) == 0)
            return "正在生成世界" + text.substr(kGenerating.size());
        if (lower.rfind(kRendering, 0) == 0)
            return "正在渲染世界" + text.substr(kRendering.size());
        auto unitMatch = [&](const char* plural, const char* singular, const char* zh) -> std::string {
            std::string suffix(plural);
            size_t cut = std::string::npos;
            if (lower.size() > suffix.size() && lower.rfind(suffix, lower.size() - suffix.size()) != std::string::npos) {
                cut = text.size() - suffix.size();
            } else {
                suffix.assign(singular);
                if (lower.size() > suffix.size() && lower.rfind(suffix, lower.size() - suffix.size()) != std::string::npos)
                    cut = text.size() - suffix.size();
            }
            if (cut == std::string::npos) return {};
            std::string n = Trim(text.substr(0, cut));
            bool numeric = !n.empty();
            for (unsigned char c : n)
                if (!((c >= '0' && c <= '9') || c == ',' || c == '.' || c == 'k' || c == 'K')) numeric = false;
            return numeric ? n + zh : std::string();
        };
        std::string r = unitMatch(" cards", " card", " 张卡片");
        if (!r.empty()) return r;
        return unitMatch(" slabs", " slab", " 个板块");
    };
    std::string result;
    bool changed = false;
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::string tr = translateLine(line);
        if (!tr.empty()) { result += tr; changed = true; }
        else result += line;
        if (end == std::string::npos) break;
        result.push_back('\n');
        start = end + 1;
    }
    out = result;
    return changed;
}

bool TryTranslateDeathContext(const std::string& input, std::string& out) {
    if (g_deathContext.empty() || GetTickCount64() - g_deathContextAt > 5000) return false;
    std::string translated, matchedKey, matchedSource;
    if (g_db.Lookup({"DEATH:" + g_deathContext}, input, translated, matchedKey, matchedSource, nullptr) && !translated.empty()) {
        out = translated;
        return true;
    }
    if (g_deathContext != "card" &&
        g_db.Lookup({"DEATH:card"}, input, translated, matchedKey, matchedSource, nullptr) && !translated.empty()) {
        out = translated;
        return true;
    }
    return false;
}

void ProcessPendingHud() {
    static uint64_t last = 0;
    uint64_t now = GetTickCount64();
    if (now - last < 500) return;
    last = now;
    std::vector<void*> keep;
    for (void* p : g_pendingHud) {
        if (!p || !LooksLikeTextureText(p)) continue;
        std::string s = g_model.TextureText(p);
        std::string local = TranslateHudKeyHint(s);
        if (local.empty() || local == s) continue;
        int page = g_db.FindPageForText(local, 200);
        if (page < 0 || (!g_fontPages.IsPageReady(page) && g_fontPages.EnsurePage(page) < 0)) {
            keep.push_back(p);
            continue;
        }
        Il2CppString* ns = NewManagedString(local);
        size_t off = FieldOffset(FindClass("TextureText"), "text");
        if (ns && off != SIZE_MAX) {
            *reinterpret_cast<void**>(reinterpret_cast<char*>(p) + off) = ns;
            ApplyPageFont(p, page);
            if (g_origRebuild) g_origRebuild(p);
            Logger::Instance().Log("hud", "deferred rebuild=" + EscapeForLog(local));
        }
    }
    g_pendingHud.swap(keep);
}

void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize) {
    static const bool disableTranslation = [] {
        const char* v = std::getenv("VOIMA_ZH_DISABLE_TRANSLATION");
        return v && *v && std::string(v) != "0";
    }();
    if (disableTranslation) {
        ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    ++g_buildCalls;
    static thread_local bool inHook = false;
    if (inHook || !text || !self) {
        ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    if (!g_ready.load()) {
        std::string earlyInput = ReadManagedString(text);
        if (LowerAscii(earlyInput).find("voima is still buggy") != std::string::npos) {
            ObjectKeys earlyKeys = g_model.Describe(self, earlyInput);
            std::string earlyTr, earlyMk, earlyMs; int earlyPage = -1;
            if (g_db.Lookup(earlyKeys.keys, earlyInput, earlyTr, earlyMk, earlyMs, &earlyPage) && !earlyTr.empty()) {
                earlyPage = g_db.EnsurePageForText(earlyTr, 200);
                if (earlyPage >= 0 && g_fontPages.EnsurePage(earlyPage) >= 0) {
                    Il2CppString* earlyNs = NewManagedString(earlyTr);
                    if (earlyNs) {
                        ApplyPageFont(self, earlyPage);
                        Logger::Instance().Debug("bannerprobe", "early-build page=" + std::to_string(earlyPage));
                        if (g_origBuild) g_origBuild(self, earlyNs, x, y, z, effect, fontSize);
                        return;
                    }
                }
            }
            BufferEarlyBuild(self, text, x, y, z, effect, fontSize);
            ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
            return;
        }
        BufferEarlyBuild(self, text, x, y, z, effect, fontSize);
        ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    inHook = true;
    const std::string input = ReadManagedString(text);
    if (LowerAscii(input).find("generating the world") != std::string::npos || LowerAscii(input).find("rendering the world") != std::string::npos)
        g_generationContext = true;
    if (LowerAscii(input).find("to open") != std::string::npos || input.find("打开") != std::string::npos)
        Logger::Instance().Debug("chestdebug", "build input=" + EscapeForLog(input));
    if (input.empty()) {
        inHook = false;
        ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    const std::string lowerInput = LowerAscii(input);
    if (lowerInput.find("loading") != std::string::npos || input.find("加载") != std::string::npos) {
        g_lastLoadingBuild = GetTickCount64();
        static bool loadingEnqueued = false;
        if (!loadingEnqueued) {
            loadingEnqueued = true;
            for (int speechPage : {3, 4, 5, 6, 9, 10, 12, 13}) {
                if (!g_fontPages.IsPageReady(speechPage)) g_pageQueue.push_back(speechPage);
            }
            Logger::Instance().Log("pagequeue", "enqueued at loading count=" + std::to_string(g_pageQueue.size()));
        }
    }
    if (lowerInput.find("continue playing") != std::string::npos) {
        static bool menuEnqueued = false;
        if (!menuEnqueued) {
            menuEnqueued = true;
            for (int speechPage : {3, 4, 5, 6, 9, 10, 12, 13}) {
                if (!g_fontPages.IsPageReady(speechPage)) g_pageQueue.push_back(speechPage);
            }
            Logger::Instance().Log("pagequeue", "enqueued at menu count=" + std::to_string(g_pageQueue.size()));
        }
        if (g_pageQueue.empty()) g_bootOverlayDone = true;
    }
    {
        std::string gen;
        if (TryTranslateGenerationText(input, gen)) {
            Logger::Instance().Log("gen", "build out=" + EscapeForLog(gen));
            int page = g_db.EnsurePageForText(gen, 200);
            if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                Il2CppString* ns = NewManagedString(gen);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    inHook = false;
                    if (g_origBuild) g_origBuild(self, ns, x, y, z, effect, fontSize);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    return;
                }
            }
        }
    }
    {
        std::string deathOut;
        if (TryTranslateDeathContext(input, deathOut)) {
            Logger::Instance().Log("death", "context out=" + EscapeForLog(deathOut));
            int page = g_db.EnsurePageForText(deathOut, 200);
            if (page >= 0 && g_fontPages.EnsurePage(page) >= 0) {
                Il2CppString* ns = NewManagedString(deathOut);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    inHook = false;
                    if (g_origBuild) g_origBuild(self, ns, x, y, z, effect, fontSize);
                    ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                    return;
                }
            }
        }
    }
    if (LooksLikeHudHint(input)) {
        std::string local = TranslateHudKeyHint(input);
        if (local != input) {
            int page = g_db.EnsurePageForText(local, 200);
            if (page >= 0 && !g_fontPages.IsPageReady(page)) page = g_fontPages.EnsurePage(page);
            if (page < 0) {
                g_pendingHud.push_back(self);
                inHook = false;
                ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
                return;
            }
            Il2CppString* ns = NewManagedString(local);
            if (ns) {
                if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                if (Logger::Instance().TestMode()) Logger::Instance().Log("hudhint", "translated=" + EscapeForLog(local) + " page=" + std::to_string(page));
                inHook = false;
                if (g_origBuild) g_origBuild(self, ns, x, y, z, effect, fontSize);
                return;
            }
        }
    }

    BuildCache& cache = g_cache[self];
    uint64_t now = GetTickCount64();
    bool needResolve = cache.input != input || cache.lastResolve == 0 || now - cache.lastResolve > 5000;
    if (needResolve) {
        cache.input = input;
        cache.keys = g_model.Describe(self, input);
        cache.lastResolve = now;
    }

    if (!cache.keys.translatable) {
        cache.output.clear();
        cache.outputString = nullptr;
        inHook = false;
        ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    if (cache.keys.confirm) g_confirmSelf = self;
    else if (g_confirmSelf == self) g_confirmSelf = nullptr;

    std::string translated;
    std::string matchedKey;
    std::string matchedSource;
    int page = -1;
    bool ok = false;
    std::string lookupInput = input;
    std::string outputPrefix;
    if (cache.keys.loading) {
        size_t nl = input.find_last_of('\n');
        if (nl != std::string::npos) {
            outputPrefix = input.substr(0, nl + 1);
            lookupInput = LowerAscii(Trim(input.substr(nl + 1)));
            while (!lookupInput.empty() && lookupInput.back() == '.') lookupInput.pop_back();
        }
    }
    if (!g_db.size()) ok = false;
    else ok = g_db.Lookup(cache.keys.keys, lookupInput, translated, matchedKey, matchedSource, &page);
    if (ok && matchedKey.rfind("TIP:", 0) == 0) translated = LocalizeTooltipKey(translated);
    if (cache.keys.loading && !translated.empty()) {
        size_t dots = 0;
        while (dots < input.size() && input[input.size() - 1 - dots] == '.') ++dots;
        while (!translated.empty() && translated.back() == '.') translated.pop_back();
        translated.append(dots, '.');
    }
    bool pagePending = false;
    bool useTranslation = ok && translated != input && !translated.empty();
    if (useTranslation) {
        for (unsigned char ch : translated) {
            if (ch >= 0x80) { break; }
        }
        if (cache.keys.confirm && g_confirmPage >= 0)
            page = g_confirmPage;
        if (page >= 0) {
            if (g_fontPages.EnsurePage(page) >= 0) {
                ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                if (cache.keys.confirm) {
                    ForceConfirmFont(self, g_fontPages.FontForPage(page));
                }
            } else {
                g_fontPages.RequestPage(page);
                g_fontPages.Clear(self, g_model.fontOffset());
                Il2CppString* fail = NewManagedString(kFontFailureText);
                if (fail) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(fail), false);
                    inHook = false;
                    if (g_origBuild) g_origBuild(self, fail, x, y, z, effect, fontSize);
                    return;
                }
                useTranslation = false;
                pagePending = true;
            }
        } else {
            g_fontPages.Clear(self, g_model.fontOffset());
        }
    }
    if (useTranslation) {
        if (!outputPrefix.empty()) translated = outputPrefix + translated;
        if (cache.output != translated || !cache.outputString) {
            cache.output = translated;
            cache.outputString = NewManagedString(translated);
            if (cache.outputString && g_api.gchandle_new)
                cache.outputHandle = g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(cache.outputString), false);
        }
        if (cache.outputString) {
            ++g_translatedCalls;
            Logger::Instance().Trace(matchedKey, input, translated);
            inHook = false;
            g_inConfirmBuild = cache.keys.confirm;
            ApplyPageFont(self, page);
            if (g_origBuild) g_origBuild(self, cache.outputString, x, y, z, effect, fontSize);
            g_inConfirmBuild = false;
            if (page >= 0 && g_fontPages.IsPageReady(page)) {
                ForceFontWithUniverse(self, g_fontPages.FontForPage(page));
                if (cache.keys.confirm) {
                    ForceConfirmFont(self, g_fontPages.FontForPage(page));
                }
            }
            return;
        }
    }

    cache.output.clear();
    cache.outputString = nullptr;
    g_fontPages.Clear(self, g_model.fontOffset());
    bool shouldLog = !pagePending && (cache.keys.owned || cache.keys.loading || Logger::Instance().TestMode());
    if (cache.keys.primary.find("W:Window:promo|") != std::string::npos) shouldLog = false;
    if (shouldLog && !cache.keys.userContent) {
        Logger::Instance().Untranslated(cache.keys.primary, input, cache.keys.detail);
        g_untranslatedUnique = (int)Logger::Instance().UniqueUntranslated();
    }
    inHook = false;
    ApplyPageFont(self, -1);
                if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
}

DWORD WINAPI Heartbeat(LPVOID) {
    for (;;) {
        Sleep(10000);
        Logger::Instance().Counters(g_buildCalls.load(), g_translatedCalls.load(), Logger::Instance().UniqueUntranslated(),
                                    g_fontPatch.ExpandedCount(), g_fontPatch.RedirectCount());
    }
    return 0;
}

void DumpButtonBounds() {
    Il2CppClass* win = FindClass("Window");
    Il2CppClass* btn = FindClass("Button");
    if (!win || !btn) return;
    FieldInfo* wins = FindField(win, "windows");
    size_t headerOff = FieldOffset(win, "header");
    size_t buttonsOff = FieldOffset(win, "buttons");
    size_t nameOff = FieldOffset(btn, "name");
    size_t winPos = FieldOffset(win, "pos");
    size_t winScale = FieldOffset(win, "scale");
    size_t winW = FieldOffset(win, "window_width");
    size_t winH = FieldOffset(win, "window_height");
    size_t wx0 = FieldOffset(win, "window_x0");
    size_t wy0 = FieldOffset(win, "window_y0");
    size_t x0 = FieldOffset(btn, "bounds_x0");
    size_t x1 = FieldOffset(btn, "bounds_x1");
    size_t y0 = FieldOffset(btn, "bounds_y0");
    size_t y1 = FieldOffset(btn, "bounds_y1");
    Logger::Instance().Log("bounds", "window header_off=" + std::to_string(headerOff) + " buttons_off=" + std::to_string(buttonsOff) +
                                        " btn x0=" + std::to_string(x0) + " y1=" + std::to_string(y1));
    ForEachManagedList(StaticPtr(wins), [&](void* w, int) {
        void* hp = nullptr;
        ReadObjectPtr(w, headerOff, &hp);
        std::string header = ReadManagedString(reinterpret_cast<Il2CppString*>(hp));
        Logger::Instance().Log("bounds", "WINDOW '" + EscapeForLog(header) + "' pos=" +
                                            std::to_string(ReadObjectFloat(w, winPos)) + "," + std::to_string(ReadObjectFloat(w, winPos + 4)) +
                                            " scale=" + std::to_string(ReadObjectFloat(w, winScale)) +
                                            " wh=" + std::to_string(ReadObjectFloat(w, winW)) + "x" + std::to_string(ReadObjectFloat(w, winH)) +
                                            " xy=" + std::to_string(ReadObjectFloat(w, wx0)) + "," + std::to_string(ReadObjectFloat(w, wy0)));
        void* list = nullptr;
        ReadObjectPtr(w, buttonsOff, &list);
        ForEachManagedList(list, [&](void* b, int idx) {
            void* np = nullptr;
            ReadObjectPtr(b, nameOff, &np);
            std::string name = ReadManagedString(reinterpret_cast<Il2CppString*>(np));
            float bx0 = ReadObjectFloat(b, x0), bx1 = ReadObjectFloat(b, x1);
            float by0 = ReadObjectFloat(b, y0), by1 = ReadObjectFloat(b, y1);
            Logger::Instance().Log("bounds", "win='" + EscapeForLog(header) + "' idx=" + std::to_string(idx) +
                                                " name='" + EscapeForLog(name) + "' x=" + std::to_string(bx0) + ".." + std::to_string(bx1) +
                                                " y=" + std::to_string(by0) + ".." + std::to_string(by1));
        });
    });
}

void LogFontState() {
    if (!g_model.lordClass()) return;
    void* font = StaticPtr(FindField(g_model.lordClass(), "default_font"));
    if (!font) {
        Logger::Instance().Log("font", "Lord.default_font is null");
        return;
    }
    void* dict = *reinterpret_cast<void**>(reinterpret_cast<char*>(font) + 0x10);
    int count = dict ? *reinterpret_cast<int*>(reinterpret_cast<char*>(dict) + 0x20) : -1;
    Logger::Instance().Log("font", "default_font=" + std::to_string((uintptr_t)font) + " chars=" + std::to_string(count) +
                                      " early_patch=" + (g_fontPatch.Expanded() ? "expanded" : "not-expanded"));
}

void Startup() {
    Logger::Instance().Init(g_modDir);
    StartBootBlackOverlay();

    HMODULE ga = nullptr;
    for (int i = 0; i < 2400; ++i) {
        ga = GetModuleHandleW(L"GameAssembly.dll");
        if (ga) break;
        Sleep(25);
    }
    if (!ga) { Logger::Instance().Log("fatal", "GameAssembly.dll did not load"); return; }
    uintptr_t base = reinterpret_cast<uintptr_t>(ga);
    Logger::Instance().Log("start", "GameAssembly base=" + std::to_string(base));
    if (!g_api.Load()) {
        Logger::Instance().Log("fatal", std::string("IL2CPP exports missing: ") + (g_api.missingExport ? g_api.missingExport : "?"));
        return;
    }
    g_stringNew = reinterpret_cast<StringNewFn>(GetProcAddress(ga, "il2cpp_string_new_utf16"));

    if (!g_fontPatch.InstallEarly(base, g_fontDir)) {
        Logger::Instance().Log("font", "early font patch failed (bad version?)");
    } else {
        Logger::Instance().Log("font", "early font patch installed");
    }

    // Install the text hook immediately; it passes through until g_ready.
    InstallBuildHookAt(reinterpret_cast<void*>(base + 0x42BBD0), true);

    void* sbPtr = reinterpret_cast<void*>(base + 0x42B910);
    const unsigned char* sb = reinterpret_cast<const unsigned char*>(sbPtr);
    bool sbPrologue = (sb[0] == 0x55 && sb[1] == 0x53 && sb[2] == 0x56) ||
                       (sb[0] == 0x40 && sb[1] == 0x55 && sb[2] == 0x53 && sb[3] == 0x56);
    if (sbPrologue) {
        MH_STATUS st = MH_CreateHook(sbPtr, reinterpret_cast<void*>(&HookBuildSB), reinterpret_cast<void**>(&g_origBuildSB));
        if (st == MH_OK && MH_EnableHook(sbPtr) == MH_OK) {
            g_buildSBHooked = true;
            Logger::Instance().Log("hook", "TextureText.Build(StringBuilder) hooked early");
        }
    }

    // Give the loader a short head start, then poll for the IL2CPP domain.
    Sleep(500);
    Il2CppDomain* domain = nullptr;
    for (int i = 0; i < 240; ++i) {
        domain = g_api.domain_get();
        if (domain) break;
        Sleep(250);
    }
    if (!domain) { Logger::Instance().Log("fatal", "IL2CPP domain never became available"); return; }
    g_api.thread_attach(domain);
    Logger::Instance().Log("start", "IL2CPP domain ready");

    bool modelReady = false;
    for (int i = 0; i < 80 && !modelReady; ++i) {
        modelReady = g_model.Init();
        if (!modelReady) Sleep(100);
    }
    if (!modelReady) { Logger::Instance().Log("fatal", "GameModel.Init failed"); return; }
    Logger::Instance().Log("start", "GameModel ready");

    if (!g_buildHooked.load()) {
        void* p = FindMethodPtr("TextureText", "Build", 6, "System.String");
        if (!p) p = FindMethodPtr("TextureText", "Build", 6, "String");
        if (p) InstallBuildHookAt(p, false);
    }
    if (!g_buildSBHooked.load()) {
        void* p = FindMethodPtr("TextureText", "Build", 6, "StringBuilder");
        if (p) {
            MH_STATUS st = MH_CreateHook(p, reinterpret_cast<void*>(&HookBuildSB), reinterpret_cast<void**>(&g_origBuildSB));
            if (st == MH_OK && MH_EnableHook(p) == MH_OK) {
                g_buildSBHooked = true;
                Logger::Instance().Log("hook", "TextureText.Build(StringBuilder) hooked late");
            }
        }
    }

    {
        void* rp = reinterpret_cast<char*>(base) + 0x42B240;
        MH_STATUS st = MH_CreateHook(rp, reinterpret_cast<void*>(&HookBuildRotated), reinterpret_cast<void**>(&g_origBuildRotated));
        if (st == MH_OK && MH_EnableHook(rp) == MH_OK)
            Logger::Instance().Log("hook", "TextureText.BuildRotated hooked");
        else
            Logger::Instance().Log("hook", "TextureText.BuildRotated hook failed st=" + std::to_string((int)st));
    }

    {
        void* lp = reinterpret_cast<char*>(base) + 0x5716A0;
        MH_STATUS st = MH_CreateHook(lp, reinterpret_cast<void*>(&HookLoadCardType), reinterpret_cast<void**>(&g_origLoadCardType));
        if (st == MH_OK && MH_EnableHook(lp) == MH_OK)
            Logger::Instance().Log("hook", "Universe.LoadCardType store hooked");
        else
            Logger::Instance().Log("hook", "Universe.LoadCardType store hook failed st=" + std::to_string((int)st));
    }

    std::string dbPath = g_modDir + "\\translations.tsv";
    if (!g_db.Load(dbPath)) {
        std::string alt = g_modDir + "\\translations\\translations.tsv";
        if (!g_db.Load(alt)) {
            Logger::Instance().Log("translation", "failed to load translations: " + g_db.lastError());
        }
    }
    g_db.AssignPages(200);
    g_speechPages = g_db.PrepareSpeechPages(200);
    g_hudPage = g_db.EnsurePageForText("缩短延长切割", 200);
    int confirmPage = g_db.EnsurePageForText("你确定吗？", 200);
    g_bannerPage = g_db.EnsurePageForText("voima 仍然一堆bug，并且还没做完...\n所以暂时免费提供给大家 :)", 200);
    Logger::Instance().Log("translation", "entries=" + std::to_string(g_db.size()) + " keys=" + std::to_string(g_db.keyCount()) +
                                             " pages=" + std::to_string(g_db.pageCount()));
    for (size_t pi = 0; pi < g_db.pageCount(); ++pi) {
        Logger::Instance().Log("translation", "page " + std::to_string(pi) + " chars=" +
                                                 std::to_string(g_db.pageChars(pi).size()) + " entries=" +
                                                 std::to_string(g_db.pageEntries(pi).size()));
    }
    std::string gameRoot = g_modDir;
    size_t slash = gameRoot.find_last_of("\\/");
    if (slash != std::string::npos) gameRoot = gameRoot.substr(0, slash);
    std::string fallbackFont = gameRoot + "\\VOIMA_Data\\StreamingAssets\\Font";
    if (!g_fontPages.Init(&g_model, &g_db, &g_fontPatch, fallbackFont)) {
        Logger::Instance().Log("fontpage", "page font init failed");
    }
    if (confirmPage >= 0) { g_confirmPage = confirmPage; g_fontPages.RequestPage(confirmPage); }
    {
        void* vd = FindMethodPtr("Card", "VoidDamage", 0, nullptr);
        if (vd) {
            MH_STATUS st = MH_CreateHook(vd, reinterpret_cast<void*>(&HookVoidDamage), reinterpret_cast<void**>(&g_origVoidDamage));
            if (st == MH_OK && MH_EnableHook(vd) == MH_OK) Logger::Instance().Log("hook", "Card.VoidDamage hooked");
        }
        void* sd = FindMethodPtr("Card", "SuffocateDamage", 0, nullptr);
        if (sd) {
            MH_STATUS st = MH_CreateHook(sd, reinterpret_cast<void*>(&HookSuffocateDamage), reinterpret_cast<void**>(&g_origSuffocateDamage));
            if (st == MH_OK && MH_EnableHook(sd) == MH_OK) Logger::Instance().Log("hook", "Card.SuffocateDamage hooked");
        }
        void* dd = FindMethodPtr("Card", "DeathDrop", 1, nullptr);
        if (dd) {
            MH_STATUS st = MH_CreateHook(dd, reinterpret_cast<void*>(&HookDeathDrop), reinterpret_cast<void**>(&g_origDeathDrop));
            if (st == MH_OK && MH_EnableHook(dd) == MH_OK) Logger::Instance().Log("hook", "Card.DeathDrop hooked");
        }
    }
    if (!g_origLordUpdate) {
        void* upd = FindMethodPtr("Lord", "Update", 0, nullptr);
        if (upd) {
            MH_STATUS st = MH_CreateHook(upd, reinterpret_cast<void*>(&HookLordUpdate), &g_origLordUpdate);
            if (st == MH_OK && MH_EnableHook(upd) == MH_OK) {
                Logger::Instance().Log("hook", "Lord.Update hooked for deferred font pages");
            } else {
                Logger::Instance().Log("hook", "failed to hook Lord.Update st=" + std::to_string((int)st));
            }
        }
    }
    {
        Il2CppClass* te = FindClassInNamespace("UnityEngine.UIElements", "TextElement");
        if (te) {
            void* it = nullptr;
            while (MethodInfo* m = g_api.class_get_methods(te, &it)) {
                const char* n = g_api.method_get_name(m);
                if (n && std::strcmp(n, "set_text") == 0 && g_api.method_get_param_count(m) == 1) {
                    MH_STATUS st = MH_CreateHook(*reinterpret_cast<void**>(m), reinterpret_cast<void*>(&HookUiTextSet), reinterpret_cast<void**>(&g_origUiSetText));
                    if (st == MH_OK && MH_EnableHook(*reinterpret_cast<void**>(m)) == MH_OK)
                        Logger::Instance().Log("hook", "TextElement.set_text hooked");
                    break;
                }
            }
        }
    }
    {
        void* rb = FindMethodPtr("TextureText", "Rebuild", 0, nullptr);
        if (rb) {
            MH_STATUS st = MH_CreateHook(rb, reinterpret_cast<void*>(&HookRebuild), reinterpret_cast<void**>(&g_origRebuild));
            if (st == MH_OK && MH_EnableHook(rb) == MH_OK) {
                Logger::Instance().Log("hook", "TextureText.Rebuild hooked");
            } else {
                Logger::Instance().Log("hook", "failed to hook TextureText.Rebuild st=" + std::to_string((int)st));
            }
        }
    }
    if (!g_pickFontHooked) {
        void* pick = FindMethodPtr("TextureText", "PickFont", 1, nullptr);
        if (pick) {
            MH_STATUS st = MH_CreateHook(pick, reinterpret_cast<void*>(&HookPickFont), &g_origPickFont);
            if (st == MH_OK && MH_EnableHook(pick) == MH_OK) {
                g_pickFontHooked = true;
                Logger::Instance().Log("hook", "TextureText.PickFont hooked");
            } else {
                Logger::Instance().Log("hook", "failed to hook PickFont st=" + std::to_string((int)st));
            }
        }
    }
    {
        void* shake = FindMethodPtr("TextureText", "Shake", 3, nullptr);
        if (shake) {
            MH_STATUS st = MH_CreateHook(shake, reinterpret_cast<void*>(&HookShake), reinterpret_cast<void**>(&g_origShake));
            if (st == MH_OK && MH_EnableHook(shake) == MH_OK)
                Logger::Instance().Log("hook", "TextureText.Shake hooked");
        }
        void* ufc = FindMethodPtr("TextureText", "UniverseFontCurrent", 0, nullptr);
        if (ufc) {
            MH_STATUS st = MH_CreateHook(ufc, reinterpret_cast<void*>(&HookUniverseFontCurrent), reinterpret_cast<void**>(&g_origUniverseFontCurrent));
            if (st == MH_OK && MH_EnableHook(ufc) == MH_OK)
                Logger::Instance().Log("hook", "TextureText.UniverseFontCurrent hooked");
        }
        void* bounce = FindMethodPtr("TextureText", "Bounced", 1, nullptr);
        if (bounce) {
            MH_STATUS st = MH_CreateHook(bounce, reinterpret_cast<void*>(&HookBounced), reinterpret_cast<void**>(&g_origBounced));
            if (st == MH_OK && MH_EnableHook(bounce) == MH_OK)
                Logger::Instance().Log("hook", "TextureText.Bounced hooked");
        }
        void* render = FindMethodPtr("TextureText", "Render", 2, nullptr);
        if (render) {
            MH_STATUS st = MH_CreateHook(render, reinterpret_cast<void*>(&HookRender), reinterpret_cast<void**>(&g_origRender));
            if (st == MH_OK && MH_EnableHook(render) == MH_OK)
                Logger::Instance().Log("hook", "TextureText.Render hooked");
        }
        void* meshClass = FindUnityClass("Mesh");
        if (meshClass) {
            void* mi = nullptr;
            while (MethodInfo* m = g_api.class_get_methods(reinterpret_cast<Il2CppClass*>(meshClass), &mi)) {
                const char* n = g_api.method_get_name(m);
                if (n && std::strcmp(n, "get_vertexCount") == 0 && g_api.method_get_param_count(m) == 0) {
                    g_meshVertexCountFn = *reinterpret_cast<void**>(m);
                    break;
                }
            }
        }
        void* graphics = FindUnityClass("Graphics");
        if (graphics) {
            void* gi = nullptr;
            while (MethodInfo* m = g_api.class_get_methods(reinterpret_cast<Il2CppClass*>(graphics), &gi)) {
                const char* n = g_api.method_get_name(m);
                if (n && std::strcmp(n, "Internal_DrawMesh") == 0 && g_api.method_get_param_count(m) == 12) {
                    void* fp = *reinterpret_cast<void**>(m);
                    if (fp && MH_CreateHook(fp, reinterpret_cast<void*>(&HookDrawMesh), reinterpret_cast<void**>(&g_origDrawMesh)) == MH_OK) {
                        MH_EnableHook(fp);
                        Logger::Instance().Log("hook", "Graphics.Internal_DrawMesh hooked");
                    }
                } else if (n && std::strcmp(n, "Internal_DrawMeshInstanced") == 0 && g_api.method_get_param_count(m) == 12) {
                    void* fp = *reinterpret_cast<void**>(m);
                    if (fp && MH_CreateHook(fp, reinterpret_cast<void*>(&HookDrawInstanced), reinterpret_cast<void**>(&g_origDrawInstanced)) == MH_OK) {
                        MH_EnableHook(fp);
                        Logger::Instance().Log("hook", "Graphics.Internal_DrawMeshInstanced hooked");
                    }
                }
            }
        }
    }
    {
        void* mpb = nullptr;
        if (mpb) {
            void* iter = nullptr;
            while (MethodInfo* m = g_api.class_get_methods(mpb, &iter)) {
                const char* n = g_api.method_get_name(m);
                if (!n || g_api.method_get_param_count(m) != 2) continue;
                void* fp = *reinterpret_cast<void**>(m);
                if (!fp) continue;
                if (std::strcmp(n, "SetFloatImpl") == 0) {
                    if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetFloatImpl), reinterpret_cast<void**>(&g_origSetFloatImpl)) == MH_OK)
                        MH_EnableHook(fp);
                } else if (std::strcmp(n, "SetTextureImpl") == 0) {
                    if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetTextureImpl), reinterpret_cast<void**>(&g_origSetTextureImpl)) == MH_OK)
                        MH_EnableHook(fp);
                } else if (std::strcmp(n, "SetVectorImpl") == 0) {
                    if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetVectorImpl), reinterpret_cast<void**>(&g_origSetVectorImpl)) == MH_OK)
                        MH_EnableHook(fp);
                } else if ((std::strcmp(n, "SetFloatArrayImpl") == 0 || std::strcmp(n, "SetVectorArrayImpl") == 0) && g_api.method_get_param_count(m) == 3) {
                    const Il2CppType* t0 = g_api.method_get_param(m, 0);
                    const char* tn0 = t0 ? g_api.type_get_name(t0) : nullptr;
                    if (tn0 && std::strstr(tn0, "Int32")) {
                        if (std::strcmp(n, "SetFloatArrayImpl") == 0) {
                            if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetFloatArrayImpl), reinterpret_cast<void**>(&g_origSetFloatArrayImpl)) == MH_OK)
                                MH_EnableHook(fp);
                        } else {
                            if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetVectorArrayImpl), reinterpret_cast<void**>(&g_origSetVectorArrayImpl)) == MH_OK)
                                MH_EnableHook(fp);
                        }
                    }
                } else if ((std::strcmp(n, "SetFloatArray") == 0 || std::strcmp(n, "SetVectorArray") == 0) && g_api.method_get_param_count(m) == 2) {
                    const Il2CppType* t0 = g_api.method_get_param(m, 0);
                    const char* tn0 = t0 ? g_api.type_get_name(t0) : nullptr;
                    if (tn0 && std::strstr(tn0, "Int32")) {
                        if (std::strcmp(n, "SetFloatArray") == 0) {
                            if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetFloatArray), reinterpret_cast<void**>(&g_origSetFloatArray)) == MH_OK)
                                MH_EnableHook(fp);
                        } else {
                            if (MH_CreateHook(fp, reinterpret_cast<void*>(&HookSetVectorArray), reinterpret_cast<void**>(&g_origSetVectorArray)) == MH_OK)
                                MH_EnableHook(fp);
                        }
                    }
                }
            }
            Logger::Instance().Log("hook", "MaterialPropertyBlock Impl hooks installed");
        }
    }
    g_runtimeReady = true;
    g_ready = true;
    // Build the small world-size page during the menu, not on the New World
    // click.  Only one page is needed; prewarming every page hurts load time.
    {
        static const char* kSizeChars[] = {"小", "中", "大", "特", "太", "啦", "！"};
        std::set<int> sizePages;
        for (const char* ch : kSizeChars) {
            int p = g_db.FindPageForText(ch, 200);
            if (p >= 0) sizePages.insert(p);
        }
        for (int p : sizePages) {
            if (!g_fontPages.IsPageReady(p)) g_fontPages.RequestPage(p);
        }
    }
    // Startup prewarm disabled for stability.
    // Spread any missing font pages over the menu frames so the first control
    // page open does not synchronously build a page on the UI thread.
    Logger::Instance().Log("start", "ready");
    HANDLE h = CreateThread(nullptr, 0, Heartbeat, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
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
    StartBootBlackOverlay();
    Startup();
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
