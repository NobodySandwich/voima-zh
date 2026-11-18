#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

using Il2CppDomain = void;
using Il2CppAssembly = void;
using Il2CppImage = void;
using Il2CppClass = void;
using Il2CppObject = void;
using Il2CppString = void;
using Il2CppArray = void;
using Il2CppType = void;
using MethodInfo = void;
using FieldInfo = void;

struct Il2CppApi {
    HMODULE gameAssembly = nullptr;

    Il2CppDomain* (*domain_get)() = nullptr;
    const Il2CppAssembly** (*domain_get_assemblies)(const Il2CppDomain*, size_t*) = nullptr;
    Il2CppImage* (*assembly_get_image)(const Il2CppAssembly*) = nullptr;
    const char* (*image_get_name)(const Il2CppImage*) = nullptr;
    Il2CppClass* (*class_from_name)(const Il2CppImage*, const char*, const char*) = nullptr;
    const char* (*class_get_name)(Il2CppClass*) = nullptr;
    const char* (*class_get_namespace)(Il2CppClass*) = nullptr;
    MethodInfo* (*class_get_methods)(Il2CppClass*, void**) = nullptr;
    const char* (*method_get_name)(const MethodInfo*) = nullptr;
    uint32_t (*method_get_param_count)(const MethodInfo*) = nullptr;
    const Il2CppType* (*method_get_param)(const MethodInfo*, uint32_t) = nullptr;
    const char* (*type_get_name)(const Il2CppType*) = nullptr;
    Il2CppClass* (*class_from_type)(const Il2CppType*) = nullptr;
    Il2CppClass* (*object_get_class)(Il2CppObject*) = nullptr;
    FieldInfo* (*class_get_field_from_name)(Il2CppClass*, const char*) = nullptr;
    size_t (*field_get_offset)(FieldInfo*) = nullptr;
    const char* (*field_get_name)(FieldInfo*) = nullptr;
    void (*field_static_get_value)(FieldInfo*, void*) = nullptr;
    size_t (*string_length)(Il2CppString*) = nullptr;
    const uint16_t* (*string_chars)(Il2CppString*) = nullptr;
    Il2CppString* (*string_new_utf16)(const uint16_t*, int32_t) = nullptr;
    uint32_t (*array_length)(Il2CppArray*) = nullptr;
    Il2CppArray* (*array_new)(Il2CppClass*, size_t) = nullptr;
    Il2CppObject* (*object_new)(Il2CppClass*) = nullptr;
    MethodInfo* (*object_get_virtual_method)(Il2CppObject*, const MethodInfo*) = nullptr;
    Il2CppObject* (*runtime_invoke)(const MethodInfo*, void*, void**, Il2CppObject**) = nullptr;
    uint32_t (*gchandle_new)(Il2CppObject*, bool) = nullptr;
    void (*gchandle_free)(uint32_t) = nullptr;
    void (*thread_attach)(Il2CppDomain*) = nullptr;
    void (*free)(void*) = nullptr;
    const char* missingExport = nullptr;

    bool Load();
};

extern Il2CppApi g_api;

std::string Utf16ToUtf8(const uint16_t* chars, size_t length);
std::string ReadIl2CppString(Il2CppString* s);
void* ReadPtr(void* obj, size_t offset);
int ReadInt(void* obj, size_t offset);
bool IsValidObject(void* obj);
