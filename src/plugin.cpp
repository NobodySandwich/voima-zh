#include "il2cpp_api.h"
#include "util.h"
#include "logger.h"
#include "game_model.h"
#include "translations.h"
#include "font_patch.h"
#include "font_pages.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using BuildFn = void (*)(void*, Il2CppString*, float, float, float, float, float);
using BuildSBFn = void (*)(void*, void*, float, float, float, float, float);
using LordUpdateFn = void (*)(void*);
using StringNewFn = Il2CppString* (*)(const uint16_t*, int32_t);

std::atomic<bool> g_ready{false};
std::atomic<bool> g_runtimeReady{false};
std::atomic<int> g_buildCalls{0};
std::atomic<int> g_translatedCalls{0};
std::atomic<int> g_untranslatedUnique{0};
std::atomic<bool> g_buildHooked{false};
std::atomic<bool> g_buildSBHooked{false};
std::atomic<bool> g_dumpRequested{false};
std::string g_modDir;
std::string g_fontDir;
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

void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize);
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
                    for (const char* field : {"hurt_messages", "attack_messages", "flee_messages"}) {
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

void HookLordUpdate(void* self) {
    if (g_dumpRequested.exchange(false)) DumpObjectGraph(g_scenario);
    if (g_origLordUpdate) reinterpret_cast<LordUpdateFn>(g_origLordUpdate)(self);
    if (g_ready.load()) {
        if (!g_earlyReplayed) ReplayEarlyBuilds();
        g_fontPages.ProcessPending();
        static int frames = 0;
        if (Logger::Instance().TestMode() && (frames == 120 || frames == 600)) DumpButtonBounds();
        if (++frames % 15 == 0) ProcessCommandFile();
        if (Logger::Instance().TestMode() && frames == 10) {
            for (size_t i = 0; i < g_db.pageCount(); ++i) g_fontPages.RequestPage((int)i);
        }
    }
}

void HookPickFont(void* self, void* font) {
    if (g_fontPages.IsForced(self)) return;
    if (g_origPickFont) reinterpret_cast<void (*)(void*, void*)>(g_origPickFont)(self, font);
}

void* FindBind(const std::string& name);
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

void HookBuildRotated(void* self, Il2CppString* text, float x, float y, float z, Quat rotation, float dream) {
    if (self && text && g_ready.load()) {
        std::string s = ReadManagedString(text);
        std::string lower = LowerAscii(s);
        std::string key = "X:TextureText|" + LowerAscii(Trim(s));
        // BuildRotated is also used for world prompts such as "[e] to trade".
        // Their key is dynamic, so use the same canonical key as Describe().
        if (lower.find(" to trade") != std::string::npos)
            key = "X:TextureText|to trade";
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
             lower == "you died."))
            key = "X:TextureText|death";
        std::string translated, matchedKey, matchedSource;
        int page = -1;
        bool found = g_db.Lookup({key}, s, translated, matchedKey, matchedSource, &page);
        if (!found && key.rfind("X:TextureText|", 0) == 0)
            found = g_db.Lookup({"X:TextureText|animal"}, s, translated, matchedKey, matchedSource, &page);
        if (found && !translated.empty()) {
            translated = LocalizeSpeechKeyPrefix(translated);
            std::string spOut, spKey, spSource;
            int speechPage = -1;
            if (g_db.Lookup({"X:TextureText|speech_font_page"}, "speech_font_page", spOut, spKey, spSource, &speechPage) && speechPage >= 0)
                page = speechPage;
            bool ready = true;
            if (page >= 0) {
                if (g_fontPages.EnsurePage(page) >= 0)
                    g_fontPages.Force(self, g_fontPages.FontForPage(page), g_model.fontOffset());
                else ready = false;
            }
            if (ready) {
                Il2CppString* ns = NewManagedString(translated);
                if (ns) {
                    if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                    RecordSpeech(s, translated);
                    Logger::Instance().Log("speech", "translated=" + EscapeForLog(translated));
                    if (g_origBuildRotated) g_origBuildRotated(self, ns, x, y, z, rotation, dream);
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
    if (g_origBuildRotated) g_origBuildRotated(self, text, x, y, z, rotation, dream);
}

void HookBuildSB(void* self, void* builder, float x, float y, float z, float effect, float fontSize) {
    ++g_buildCalls;
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
        if (ReadManagedString(reinterpret_cast<Il2CppString*>(np)) == name) found = b;
    });
    return found;
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
    std::string name = label;
    auto it = actionBinds.find(a);
    if (it != actionBinds.end()) name = it->second;
    return FindBind(name);
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

std::string TranslateHudAction(const std::string& action) {
    static const std::map<std::string, std::string> actions = {
        {"attack", "攻击"}, {"build", "建造"}, {"set grid", "设置网格"},
        {"copy size", "复制尺寸"}, {"toggle grid", "切换网格"}, {"more", "更多"},
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
    auto it = actions.find(LowerAscii(Trim(action)));
    return it == actions.end() ? std::string() : it->second;
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
        }
        out += line;
        if (end == std::string::npos) break;
        out.push_back('\n');
        start = end + 1;
    }
    return out;
}

void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize) {
    ++g_buildCalls;
    static thread_local bool inHook = false;
    if (inHook || !text || !self) {
        if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    if (!g_ready.load()) {
        BufferEarlyBuild(self, text, x, y, z, effect, fontSize);
        if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    inHook = true;
    const std::string input = ReadManagedString(text);
    if (input.empty()) {
        inHook = false;
        if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }
    const std::string lowerInput = LowerAscii(input);
    if (LooksLikeHudHint(input)) {
        std::string local = TranslateHudKeyHint(input);
        if (local != input) {
            Il2CppString* ns = NewManagedString(local);
            if (ns) {
                if (g_api.gchandle_new) g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(ns), false);
                std::string dummy, mk, ms;
                int page = -1;
                g_db.Lookup({"X:TextureText|hud_font_page"}, "hud_font_page", dummy, mk, ms, &page);
                if (page >= 0 && g_fontPages.EnsurePage(page) >= 0)
                    g_fontPages.Force(self, g_fontPages.FontForPage(page), g_model.fontOffset());
                Logger::Instance().Log("hudhint", "translated=" + EscapeForLog(local) + " page=" + std::to_string(page));
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
        if (g_origBuild) g_origBuild(self, text, x, y, z, effect, fontSize);
        return;
    }

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
    bool pagePending = false;
    bool useTranslation = ok && translated != input && !translated.empty();
    if (useTranslation) {
        if (page >= 0) {
            if (g_fontPages.EnsurePage(page) >= 0) {
                g_fontPages.Force(self, g_fontPages.FontForPage(page), g_model.fontOffset());
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
            if (g_origBuild) g_origBuild(self, cache.outputString, x, y, z, effect, fontSize);
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
    g_runtimeReady = true;
    g_ready = true;
    // Spread any missing font pages over the menu frames so the first control
    // page open does not synchronously build a page on the UI thread.
    for (int p = 0; p < g_fontPages.PageCount(); ++p) g_fontPages.RequestPage(p);
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
