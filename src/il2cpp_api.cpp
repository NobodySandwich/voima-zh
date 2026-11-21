#include "il2cpp_api.h"
#include <vector>

Il2CppApi g_api;

static void* GetProc(HMODULE mod, const char* name) {
    void* p = reinterpret_cast<void*>(GetProcAddress(mod, name));
    return p;
}

bool Il2CppApi::Load() {
    if (gameAssembly && domain_get) return true;
    gameAssembly = GetModuleHandleW(L"GameAssembly.dll");
    if (!gameAssembly) return false;

#define LOAD(name) name = reinterpret_cast<decltype(name)>(GetProc(module, "il2cpp_" #name)); if (!name) { missingExport = "il2cpp_" #name; OutputDebugStringA("MISSING EXPORT: il2cpp_" #name); return false; }
    HMODULE module = gameAssembly;
    LOAD(domain_get);
    LOAD(domain_get_assemblies);
    LOAD(assembly_get_image);
    LOAD(image_get_name);
    LOAD(image_get_class_count);
    LOAD(image_get_class);
    LOAD(class_from_name);
    LOAD(class_get_name);
    LOAD(class_get_namespace);
    LOAD(class_get_methods);
    LOAD(class_get_parent);
    LOAD(method_get_name);
    LOAD(method_get_param_count);
    LOAD(method_get_param);
    LOAD(method_get_return_type);
    LOAD(type_get_name);
    LOAD(class_from_type);
    LOAD(object_get_class);
    LOAD(class_get_field_from_name);
    LOAD(field_get_type);
    LOAD(field_get_offset);
    LOAD(field_get_name);
    LOAD(field_static_get_value);
    LOAD(string_length);
    LOAD(string_chars);
    LOAD(string_new_utf16);
    LOAD(array_length);
    LOAD(array_new);
    LOAD(object_new);
    LOAD(object_get_virtual_method);
    LOAD(runtime_invoke);
    LOAD(type_get_object);
    LOAD(class_get_type);
    LOAD(gchandle_new);
    LOAD(gchandle_free);
    LOAD(thread_attach);
    // il2cpp_free exists but is optional for our usage
    free = reinterpret_cast<decltype(free)>(GetProc(module, "il2cpp_free"));
#undef LOAD
    return true;
}

std::string Utf16ToUtf8(const uint16_t* chars, size_t length) {
    if (!chars || length == 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars),
                                     static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars),
                        static_cast<int>(length), &out[0], needed, nullptr, nullptr);
    return out;
}

std::string ReadIl2CppString(Il2CppString* s) {
    if (!s || !g_api.string_length || !g_api.string_chars) return {};
    size_t len = g_api.string_length(s);
    if (len == 0) return {};
    return Utf16ToUtf8(g_api.string_chars(s), len);
}

void* ReadPtr(void* obj, size_t offset) {
    if (!obj) return nullptr;
    return *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + offset);
}

int ReadInt(void* obj, size_t offset) {
    if (!obj) return 0;
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(obj) + offset);
}

bool IsValidObject(void* obj) {
    return obj != nullptr && (reinterpret_cast<uintptr_t>(obj) & 7) == 0;
}
