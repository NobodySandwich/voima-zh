#include "il2cpp_api.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "user32.lib")

namespace {

CRITICAL_SECTION g_logLock;
bool g_logLockInit = false;
HANDLE g_log = INVALID_HANDLE_VALUE;
std::string g_modDir;
std::atomic<bool> g_ready{false};

struct Game {
    Il2CppImage* image = nullptr;
    Il2CppClass* window = nullptr;
    Il2CppClass* button = nullptr;
    Il2CppClass* textureText = nullptr;
    Il2CppClass* textureFont = nullptr;
    Il2CppClass* typeButton = nullptr;
    Il2CppClass* cardType = nullptr;
    Il2CppClass* lord = nullptr;
    Il2CppClass* tip = nullptr;

    size_t winHeader = 0, winHeaderText = 0, winButtons = 0, winDragText = 0;
    size_t btnName = 0, btnTextName = 0, btnTextValue = 0, btnTooltip = 0, btnWindow = 0;
    size_t ttTranslate = 0, ttText = 0, ttFont = 0, ttLanguageId = 0, ttUniverseFont = 0, ttFontSize = 0;
    size_t tbType = 0;
    size_t ctPath = 0, ctName = 0;
    size_t tipWord = 0, tipText = 0;
    FieldInfo* winListField = nullptr;
    FieldInfo* popupListField = nullptr;
    FieldInfo* leaveListField = nullptr;
    FieldInfo* tipField = nullptr;
    FieldInfo* tooltipTextField = nullptr;
} g_game;

struct Orig {
    void (*build)(void*, Il2CppString*, float, float, float, float, float) = nullptr;
    void (*buildSB)(void*, Il2CppString*, float, float, float, float, float) = nullptr;
    void (*loadChar)(void*, uint16_t, Il2CppString*, Il2CppString*, void*) = nullptr;
    void (*fontCtor)(void*, Il2CppString*, int) = nullptr;
} g_orig;

std::unordered_map<std::string, int> g_seen;
int g_buildCount = 0;
int g_graphCount = 0;

void Log(const std::string& line) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    if (g_logLockInit) EnterCriticalSection(&g_logLock);
    DWORD written = 0;
    WriteFile(g_log, line.data(), (DWORD)line.size(), &written, nullptr);
    WriteFile(g_log, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(g_log);
    if (g_logLockInit) LeaveCriticalSection(&g_logLock);
}

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\\': out += "\\\\"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string ClassName(void* obj) {
    if (!obj) return "<null>";
    Il2CppClass* cls = g_api.object_get_class(obj);
    if (!cls) return "<noclass>";
    const char* ns = g_api.class_get_namespace(cls);
    const char* name = g_api.class_get_name(cls);
    std::string s;
    if (ns && *ns) { s += ns; s += "."; }
    s += name ? name : "<noname>";
    return s;
}

bool ReadField(void* obj, size_t off, void** out) {
    if (!obj || off == 0 || off == SIZE_MAX) return false;
    *out = *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + off);
    return true;
}

void* StaticPtr(FieldInfo* f) {
    if (!f) return nullptr;
    void* value = nullptr;
    g_api.field_static_get_value(f, &value);
    return value;
}

std::string ReadStrField(void* obj, size_t off) {
    void* p = nullptr;
    if (!ReadField(obj, off, &p)) return {};
    return ReadIl2CppString(reinterpret_cast<Il2CppString*>(p));
}

template <typename F>
void ForEachList(void* list, F&& fn) {
    if (!list) return;
    void* items = *reinterpret_cast<void**>(reinterpret_cast<char*>(list) + 0x10);
    int size = *reinterpret_cast<int*>(reinterpret_cast<char*>(list) + 0x18);
    if (!items || size <= 0) return;
    int cap = *reinterpret_cast<int*>(reinterpret_cast<char*>(items) + 0x18);
    if (size > cap) size = cap;
    for (int i = 0; i < size; ++i) {
        void* item = *reinterpret_cast<void**>(reinterpret_cast<char*>(items) + 0x20 + i * 8);
        if (item) fn(item);
    }
}

size_t FieldOffset(Il2CppClass* cls, const char* name) {
    if (!cls) return SIZE_MAX;
    FieldInfo* f = g_api.class_get_field_from_name(cls, name);
    if (!f) return SIZE_MAX;
    return g_api.field_get_offset(f);
}

FieldInfo* FindField(Il2CppClass* cls, const char* name) {
    return cls ? g_api.class_get_field_from_name(cls, name) : nullptr;
}

Il2CppClass* FindClass(const char* name) {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &count);
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* image = g_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        Il2CppClass* cls = g_api.class_from_name(image, "", name);
        if (cls) {
            g_game.image = image;
            return cls;
        }
    }
    return nullptr;
}

void* FindMethod(const char* className, const char* methodName, int paramCount, const char* firstParamNeedle) {
    Il2CppClass* cls = FindClass(className);
    if (!cls) {
        Log(std::string("MISS class ") + className);
        return nullptr;
    }
    void* iter = nullptr;
    while (MethodInfo* m = g_api.class_get_methods(cls, &iter)) {
        const char* n = g_api.method_get_name(m);
        if (!n || strcmp(n, methodName) != 0) continue;
        if (static_cast<int>(g_api.method_get_param_count(m)) != paramCount) continue;
        if (firstParamNeedle) {
            const Il2CppType* t = g_api.method_get_param(m, 0);
            const char* tn = t ? g_api.type_get_name(t) : nullptr;
            if (!tn || !strstr(tn, firstParamNeedle)) continue;
        }
        void* ptr = *reinterpret_cast<void**>(m);
        Log(std::string("FOUND method ") + className + "." + methodName + " ptr=" + std::to_string(reinterpret_cast<uintptr_t>(ptr)));
        return ptr;
    }
    Log(std::string("MISS method ") + className + "." + methodName);
    return nullptr;
}



void DumpTextureTextFields(void* self, const char* tag) {
    if (!self) return;
    auto ptr_field = [&](size_t off) -> std::string {
        if (off == 0 || off == SIZE_MAX) return "?";
        void* p = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + off);
        return std::to_string(reinterpret_cast<uintptr_t>(p));
    };
    auto int_field = [&](size_t off) -> int {
        if (off == 0 || off == SIZE_MAX) return -999;
        return *reinterpret_cast<int*>(reinterpret_cast<char*>(self) + off);
    };
    Log(std::string("TT ") + tag + " ptr=" + std::to_string(reinterpret_cast<uintptr_t>(self)) +
        " class=" + ClassName(self) +
        " text='" + Escape(ReadStrField(self, g_game.ttText)) + "'" +
        " translate=" + std::to_string(int_field(g_game.ttTranslate)) +
        " lang=" + std::to_string(int_field(g_game.ttLanguageId)) +
        " ufont=" + std::to_string(int_field(g_game.ttUniverseFont)) +
        " font_size=" + std::to_string(int_field(g_game.ttFontSize)) +
        " font=" + ptr_field(g_game.ttFont));
}

struct Node {
    std::string key;
    std::string detail;
    bool found = false;
};

Node Describe(void* self) {
    Node n;
    if (!self) return n;

    auto checkWindow = [&](void* w, const char* listName) -> bool {
        if (!w) return false;
        std::string header = Escape(ReadStrField(w, g_game.winHeader));
        std::string headerText = std::to_string(reinterpret_cast<uintptr_t>(
            *reinterpret_cast<void**>(reinterpret_cast<char*>(w) + g_game.winHeaderText)));
        void* headerTextObj = nullptr;
        ReadField(w, g_game.winHeaderText, &headerTextObj);
        if (headerTextObj == self) {
            n.key = "W:Window:" + header + "|header_text";
            n.detail = std::string("list=") + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w));
            n.found = true;
            return true;
        }
        void* drag = nullptr;
        ReadField(w, g_game.winDragText, &drag);
        if (drag == self) {
            n.key = "W:Window:" + header + "|drag_text";
            n.detail = std::string("list=") + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w));
            n.found = true;
            return true;
        }
        void* buttons = nullptr;
        ReadField(w, g_game.winButtons, &buttons);
        bool hit = false;
        ForEachList(buttons, [&](void* b) {
            if (hit) return;
            std::string bname = Escape(ReadStrField(b, g_game.btnName));
            std::string bclass = ClassName(b);
            void* textName = nullptr;
            void* textValue = nullptr;
            ReadField(b, g_game.btnTextName, &textName);
            ReadField(b, g_game.btnTextValue, &textValue);
            const char* which = nullptr;
            if (textName == self) which = "text_name";
            else if (textValue == self) which = "text_value";
            if (!which) return;
            n.key = "W:Window:" + header + "|n:" + bname + ":" + bclass + "|" + which;
            n.detail = std::string("list=") + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w)) +
                       " button=" + std::to_string(reinterpret_cast<uintptr_t>(b)) + " btnclass=" + bclass;
            if (g_game.typeButton && ClassName(b) == "TypeButton") {
                void* type = nullptr;
                ReadField(b, g_game.tbType, &type);
                if (type) {
                    std::string path = Escape(ReadStrField(type, g_game.ctPath));
                    std::string name = Escape(ReadStrField(type, g_game.ctName));
                    n.detail += " card_path=" + path + " card_name=" + name;
                    n.key += "|CARD:" + path;
                }
            }
            n.found = true;
            hit = true;
        });
        return hit;
    };

    void* list = StaticPtr(g_game.winListField);
    if (list) ForEachList(list, [&](void* w) { if (!n.found) checkWindow(w, "windows"); });
    list = StaticPtr(g_game.popupListField);
    if (list) ForEachList(list, [&](void* w) { if (!n.found) checkWindow(w, "popups"); });
    list = StaticPtr(g_game.leaveListField);
    if (list) ForEachList(list, [&](void* w) { if (!n.found) checkWindow(w, "leaving"); });

    // tooltip static
    if (!n.found) {
        void* tooltipText = StaticPtr(g_game.tooltipTextField);
        if (tooltipText == self) {
            n.key = "W:Tooltip|text";
            n.found = true;
            n.detail = "tooltip_text";
        }
    }
    if (!n.found) {
        n.key = "X:" + ClassName(self);
        n.detail = "unowned ptr=" + std::to_string(reinterpret_cast<uintptr_t>(self));
    }
    return n;
}

void DumpGraph(const char* tag) {
    Log(std::string("=== GRAPH ") + tag + " ===");
    auto dumpList = [&](FieldInfo* field, const char* name) {
        void* list = StaticPtr(field);
        int count = 0;
        if (list) count = *reinterpret_cast<int*>(reinterpret_cast<char*>(list) + 0x18);
        Log(std::string("LIST ") + name + " count=" + std::to_string(count) + " ptr=" + std::to_string(reinterpret_cast<uintptr_t>(list)));
        if (!list) return;
        ForEachList(list, [&](void* w) {
            std::string header = Escape(ReadStrField(w, g_game.winHeader));
            std::string cls = ClassName(w);
            void* buttons = nullptr;
            ReadField(w, g_game.winButtons, &buttons);
            int bcount = buttons ? *reinterpret_cast<int*>(reinterpret_cast<char*>(buttons) + 0x18) : 0;
            Log(std::string("WIN ") + name + " ptr=" + std::to_string(reinterpret_cast<uintptr_t>(w)) +
                " class=" + cls + " header='" + header + "' buttons=" + std::to_string(bcount));
            ForEachList(buttons, [&](void* b) {
                std::string bname = Escape(ReadStrField(b, g_game.btnName));
                std::string bclass = ClassName(b);
                void* tn = nullptr; void* tv = nullptr; void* tw = nullptr;
                ReadField(b, g_game.btnTextName, &tn);
                ReadField(b, g_game.btnTextValue, &tv);
                ReadField(b, g_game.btnWindow, &tw);
                Log(std::string("  BTN ptr=") + std::to_string(reinterpret_cast<uintptr_t>(b)) +
                    " class=" + bclass + " name='" + bname + "' text_name=" +
                    std::to_string(reinterpret_cast<uintptr_t>(tn)) + " text_value=" +
                    std::to_string(reinterpret_cast<uintptr_t>(tv)) + " w=" +
                    std::to_string(reinterpret_cast<uintptr_t>(tw)));
            });
        });
    };
    dumpList(g_game.winListField, "windows");
    dumpList(g_game.popupListField, "popups");
    dumpList(g_game.leaveListField, "leaving");
    void* tip = StaticPtr(g_game.tipField);
    if (tip) {
        Log(std::string("TIP ptr=") + std::to_string(reinterpret_cast<uintptr_t>(tip)) +
            " word='" + Escape(ReadStrField(tip, g_game.tipWord)) + "' text='" +
            Escape(ReadStrField(tip, g_game.tipText)) + "'");
    }
    Log("=== END GRAPH ===");
}

void HookBuild(void* self, Il2CppString* text, float x, float y, float z, float effect, float fontSize) {
    if (text) {
        std::string s = ReadIl2CppString(text);
        if (!s.empty() && s.size() < 400) {
            Node n = Describe(self);
            std::string key = n.key + "\t" + s;
            int count = ++g_seen[key];
            if (count <= 2 || (count % 200 == 0)) {
                ++g_buildCount;
                DumpTextureTextFields(self, count <= 2 ? "hit" : "repeat");
                Log("BUILD n=" + std::to_string(count) + " key='" + Escape(n.key) + "' text='" + Escape(s) +
                    "' " + n.detail);
            }
        }
    }
    g_orig.build(self, text, x, y, z, effect, fontSize);
}

void HookBuildSB(void* self, Il2CppString* builder, float x, float y, float z, float effect, float fontSize) {
    Log("BUILDSB self=" + std::to_string(reinterpret_cast<uintptr_t>(self)) + " builder=" +
        std::to_string(reinterpret_cast<uintptr_t>(builder)) + " class=" + ClassName(self));
    g_orig.buildSB(self, builder, x, y, z, effect, fontSize);
}

void HookLoadChar(void* self, uint16_t c, Il2CppString* fontPath, Il2CppString* fileName, void* textures) {
    static int logged = 0;
    if (logged < 50 || c > 0x7f) {
        if (logged < 500) {
            Log("LOADCHAR c=U+" + std::to_string(static_cast<unsigned>(c)) + " path='" + Escape(ReadIl2CppString(fontPath)) +
                "' file='" + Escape(ReadIl2CppString(fileName)) + "' self=" + std::to_string(reinterpret_cast<uintptr_t>(self)));
            ++logged;
        }
    }
    g_orig.loadChar(self, c, fontPath, fileName, textures);
}

void HookFontCtor(void* self, Il2CppString* fontPath, int offset) {
    Log("FONTCTOR self=" + std::to_string(reinterpret_cast<uintptr_t>(self)) + " path='" +
        Escape(ReadIl2CppString(fontPath)) + "' offset=" + std::to_string(offset));
    g_orig.fontCtor(self, fontPath, offset);
    if (self) {
        int count = -1;
        void* dict = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + 0x10);
        if (dict) count = *reinterpret_cast<int*>(reinterpret_cast<char*>(dict) + 0x20);
        Log("FONTCTOR-DONE self=" + std::to_string(reinterpret_cast<uintptr_t>(self)) +
            " char_count=" + std::to_string(count));
    }
}

DWORD WINAPI Worker(LPVOID) {
    // Wait for GameAssembly to load, then give Unity/IL2CPP time to initialize.
    // Calling il2cpp_domain_get() before the runtime is up can block the loader thread.
    bool waitedForInit = false;
    for (int i = 0; i < 600; ++i) {
        if (!g_api.gameAssembly || !g_api.domain_get) {
            if (!g_api.Load()) {
                if (i % 20 == 0) Log("Load failed i=" + std::to_string(i) + " missing=" + (g_api.missingExport ? g_api.missingExport : "?"));
            }
        }
        if (g_api.gameAssembly && g_api.domain_get && !waitedForInit) {
            Log("GameAssembly ready, waiting 4s for IL2CPP init");
            Sleep(4000);
            waitedForInit = true;
        }
        if (waitedForInit) {
            Il2CppDomain* d = g_api.domain_get();
            if (d) { Log("domain detected"); break; }
        }
        if (i % 40 == 0) Log("waiting for runtime i=" + std::to_string(i));
        Sleep(50);
    }
    if (!g_api.gameAssembly) { Log("FATAL no GameAssembly"); return 0; }
    Log("API loaded");
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) { Log("FATAL no domain"); return 0; }
    Log("domain=" + std::to_string(reinterpret_cast<uintptr_t>(domain)));
    g_api.thread_attach(domain);
    Log("RUNTIME ready");

    g_game.window = FindClass("Window");
    Log("class Window=" + std::to_string(reinterpret_cast<uintptr_t>(g_game.window)));
    g_game.button = FindClass("Button");
    Log("class Button=" + std::to_string(reinterpret_cast<uintptr_t>(g_game.button)));
    g_game.textureText = FindClass("TextureText");
    Log("class TextureText=" + std::to_string(reinterpret_cast<uintptr_t>(g_game.textureText)));
    g_game.textureFont = FindClass("TextureFont");
    Log("class TextureFont=" + std::to_string(reinterpret_cast<uintptr_t>(g_game.textureFont)));
    g_game.typeButton = FindClass("TypeButton");
    g_game.cardType = FindClass("CardType");
    g_game.lord = FindClass("Lord");
    g_game.tip = FindClass("Tip");
    Log("classes done");

    if (g_game.window) {
        g_game.winHeader = FieldOffset(g_game.window, "header");
        g_game.winHeaderText = FieldOffset(g_game.window, "header_text");
        g_game.winButtons = FieldOffset(g_game.window, "buttons");
        g_game.winDragText = FieldOffset(g_game.window, "drag_text");
        g_game.winListField = FindField(g_game.window, "windows");
        g_game.popupListField = FindField(g_game.window, "popups");
        g_game.leaveListField = FindField(g_game.window, "leaving");
        g_game.tipField = FindField(g_game.window, "tip");
        g_game.tooltipTextField = FindField(g_game.window, "tooltip_text");
    }
    if (g_game.button) {
        g_game.btnName = FieldOffset(g_game.button, "name");
        g_game.btnTextName = FieldOffset(g_game.button, "text_name");
        g_game.btnTextValue = FieldOffset(g_game.button, "text_value");
        g_game.btnTooltip = FieldOffset(g_game.button, "tooltip");
        g_game.btnWindow = FieldOffset(g_game.button, "w");
    }
    if (g_game.textureText) {
        g_game.ttTranslate = FieldOffset(g_game.textureText, "translate");
        g_game.ttText = FieldOffset(g_game.textureText, "text");
        g_game.ttFont = FieldOffset(g_game.textureText, "font");
        g_game.ttLanguageId = FieldOffset(g_game.textureText, "language_id");
        g_game.ttUniverseFont = FieldOffset(g_game.textureText, "universe_font");
        g_game.ttFontSize = FieldOffset(g_game.textureText, "font_size");
    }
    if (g_game.typeButton) g_game.tbType = FieldOffset(g_game.typeButton, "type");
    if (g_game.cardType) {
        g_game.ctPath = FieldOffset(g_game.cardType, "path");
        g_game.ctName = FieldOffset(g_game.cardType, "name");
    }
    if (g_game.tip) {
        g_game.tipWord = FieldOffset(g_game.tip, "word");
        g_game.tipText = FieldOffset(g_game.tip, "text");
    }

    Log("OFFSETS win header=" + std::to_string(g_game.winHeader) + " header_text=" + std::to_string(g_game.winHeaderText) +
        " buttons=" + std::to_string(g_game.winButtons) +
        " btn name=" + std::to_string(g_game.btnName) + " text_name=" + std::to_string(g_game.btnTextName) +
        " tt text=" + std::to_string(g_game.ttText) + " translate=" + std::to_string(g_game.ttTranslate));

    MH_STATUS st = MH_Initialize();
    Log("MH_Initialize=" + std::to_string(static_cast<int>(st)));

    void* buildPtr = FindMethod("TextureText", "Build", 6, "System.String");
    void* buildSBPtr = FindMethod("TextureText", "Build", 6, "StringBuilder");
    void* loadPtr = FindMethod("TextureFont", "LoadCharImage", 4, nullptr);
    void* ctorPtr = FindMethod("TextureFont", ".ctor", 2, "System.String");
    if (buildPtr) {
        st = MH_CreateHook(buildPtr, reinterpret_cast<LPVOID>(&HookBuild), reinterpret_cast<LPVOID*>(&g_orig.build));
        Log("MH_CreateHook Build=" + std::to_string(static_cast<int>(st)));
        if (st == MH_OK) Log("MH_EnableHook Build=" + std::to_string(static_cast<int>(MH_EnableHook(buildPtr))));
    }
    if (buildSBPtr) {
        st = MH_CreateHook(buildSBPtr, reinterpret_cast<LPVOID>(&HookBuildSB), reinterpret_cast<LPVOID*>(&g_orig.buildSB));
        Log("MH_CreateHook BuildSB=" + std::to_string(static_cast<int>(st)));
        if (st == MH_OK) Log("MH_EnableHook BuildSB=" + std::to_string(static_cast<int>(MH_EnableHook(buildSBPtr))));
    }
    if (loadPtr) {
        st = MH_CreateHook(loadPtr, reinterpret_cast<LPVOID>(&HookLoadChar), reinterpret_cast<LPVOID*>(&g_orig.loadChar));
        Log("MH_CreateHook LoadChar=" + std::to_string(static_cast<int>(st)));
        if (st == MH_OK) Log("MH_EnableHook LoadChar=" + std::to_string(static_cast<int>(MH_EnableHook(loadPtr))));
    }
    if (ctorPtr) {
        st = MH_CreateHook(ctorPtr, reinterpret_cast<LPVOID>(&HookFontCtor), reinterpret_cast<LPVOID*>(&g_orig.fontCtor));
        Log("MH_CreateHook FontCtor=" + std::to_string(static_cast<int>(st)));
        if (st == MH_OK) Log("MH_EnableHook FontCtor=" + std::to_string(static_cast<int>(MH_EnableHook(ctorPtr))));
    }

    g_ready = true;
    Sleep(8000);
    DumpGraph("t+8s");
    Sleep(8000);
    DumpGraph("t+16s");
    for (;;) {
        Sleep(5000);
        DumpGraph("periodic");
        if (++g_graphCount > 6) break;
    }
    Log("PROBE done buildCount=" + std::to_string(g_buildCount));
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
    char narrow[MAX_PATH * 4]{};
    WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    g_modDir = narrow;

    std::string logPath = g_modDir + "\\probe.log";
    std::wstring wlog(logPath.begin(), logPath.end());
    g_log = CreateFileW(wlog.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    InitializeCriticalSection(&g_logLock);
    g_logLockInit = true;
    Log("PROBE start dir=" + g_modDir + " handle=" + std::to_string((uintptr_t)g_log));
    Worker(nullptr);
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
