#pragma once
#include "il2cpp_api.h"
#include <string>
#include <vector>

std::string WideToUtf8(const std::wstring& s);
std::wstring Utf8ToWide(const std::string& s);
std::string Trim(const std::string& s);
std::string CollapseSpaces(const std::string& s);
std::string LowerAscii(const std::string& s);
std::string BaseName(const std::string& path);
std::string EscapeForLog(const std::string& s);
std::string UnescapeText(const std::string& s);
bool FileExistsW(const std::wstring& path);
bool ReadFileUtf8(const std::wstring& path, std::string& out);

std::string ClassName(void* obj);
std::string ReadManagedString(Il2CppString* s);
bool ReadObjectPtr(void* obj, size_t offset, void** out);
int ReadObjectInt(void* obj, size_t offset);
float ReadObjectFloat(void* obj, size_t offset);
size_t FieldOffset(Il2CppClass* cls, const char* name);
FieldInfo* FindField(Il2CppClass* cls, const char* name);
void* StaticPtr(FieldInfo* field);
Il2CppClass* FindClass(const char* name);
Il2CppClass* FindClassInNamespace(const char* ns, const char* name);

template <typename Fn>
void ForEachManagedList(void* list, Fn&& fn) {
    if (!list) return;
    void* items = *reinterpret_cast<void**>(reinterpret_cast<char*>(list) + 0x10);
    int size = *reinterpret_cast<int*>(reinterpret_cast<char*>(list) + 0x18);
    if (!items || size <= 0) return;
    int cap = *reinterpret_cast<int*>(reinterpret_cast<char*>(items) + 0x18);
    if (size > cap) size = cap;
    for (int i = 0; i < size; ++i) {
        void* item = *reinterpret_cast<void**>(reinterpret_cast<char*>(items) + 0x20 + i * 8);
        if (item) fn(item, i);
    }
}
