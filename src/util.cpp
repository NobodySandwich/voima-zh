#include "util.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') ++a;
    while (b > a && (unsigned char)s[b - 1] <= ' ') --b;
    return s.substr(a, b - a);
}

std::string CollapseSpaces(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!space) { out += ' '; space = true; }
        } else {
            out += c;
            space = false;
        }
    }
    return out;
}

std::string LowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

std::string BaseName(const std::string& path) {
    size_t p = path.find_last_of("\\/");
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string EscapeForLog(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\\': out += "\\\\"; break;
        default: out += c;
        }
    }
    return out;
}

std::string UnescapeText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') { out += '\n'; ++i; continue; }
            if (n == 'r') { out += '\r'; ++i; continue; }
            if (n == 't') { out += '\t'; ++i; continue; }
            if (n == '\\') { out += '\\'; ++i; continue; }
        }
        out += s[i];
    }
    return out;
}

bool FileExistsW(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadFileUtf8(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    out.resize(size);
    DWORD read = 0;
    BOOL ok = ReadFile(h, out.empty() ? nullptr : &out[0], size, &read, nullptr);
    CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
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

std::string ReadManagedString(Il2CppString* s) {
    if (!s || !g_api.string_length || !g_api.string_chars) return {};
    size_t len = g_api.string_length(s);
    if (len == 0) return {};
    return Utf16ToUtf8(g_api.string_chars(s), len);
}

bool ReadObjectPtr(void* obj, size_t offset, void** out) {
    if (!obj || offset == 0 || offset == SIZE_MAX) return false;
    *out = *reinterpret_cast<void**>(reinterpret_cast<char*>(obj) + offset);
    return true;
}

int ReadObjectInt(void* obj, size_t offset) {
    if (!obj || offset == 0 || offset == SIZE_MAX) return 0;
    return *reinterpret_cast<int*>(reinterpret_cast<char*>(obj) + offset);
}

float ReadObjectFloat(void* obj, size_t offset) {
    if (!obj || offset == 0 || offset == SIZE_MAX) return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<char*>(obj) + offset);
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

void* StaticPtr(FieldInfo* field) {
    if (!field) return nullptr;
    void* value = nullptr;
    g_api.field_static_get_value(field, &value);
    return value;
}

Il2CppClass* FindClassInNamespace(const char* ns, const char* name) {
    Il2CppDomain* domain = g_api.domain_get();
    if (!domain) return nullptr;
    size_t count = 0;
    const Il2CppAssembly** assemblies = g_api.domain_get_assemblies(domain, &count);
    for (size_t i = 0; i < count; ++i) {
        Il2CppImage* image = g_api.assembly_get_image(assemblies[i]);
        if (!image) continue;
        Il2CppClass* cls = g_api.class_from_name(image, ns, name);
        if (cls) return cls;
    }
    return nullptr;
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
        if (cls) return cls;
    }
    return nullptr;
}
