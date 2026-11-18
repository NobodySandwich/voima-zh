#include "font_pages.h"
#include "il2cpp_api.h"
#include "util.h"
#include "logger.h"
#include <algorithm>
#include <cstring>
#include <set>

Il2CppString* FontPages::NewString(const std::string& utf8) const {
    if (!g_api.string_new_utf16) return nullptr;
    std::wstring wide = Utf8ToWide(utf8);
    return g_api.string_new_utf16(reinterpret_cast<const uint16_t*>(wide.data()), (int32_t)wide.size());
}

MethodInfo* FontPages::FindMethodInfo(const char* method, int paramCount, const char* firstParamNeedle) const {
    if (!model_) return nullptr;
    Il2CppClass* cls = model_->textureFontClass();
    if (!cls) return nullptr;
    void* iter = nullptr;
    while (MethodInfo* m = g_api.class_get_methods(cls, &iter)) {
        const char* name = g_api.method_get_name(m);
        if (!name || std::strcmp(name, method) != 0) continue;
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

void* FontPages::FindMethod(const char* method, int paramCount, const char* firstParamNeedle) const {
    MethodInfo* m = FindMethodInfo(method, paramCount, firstParamNeedle);
    return m ? *reinterpret_cast<void**>(m) : nullptr;
}

bool FontPages::Init(GameModel* model, TranslationDb* db, FontPatch* patch, const std::string& fallbackFontPath) {
    model_ = model;
    db_ = db;
    patch_ = patch;
    fontPath_ = patch->OriginalFontPath();
    if (fontPath_.empty()) fontPath_ = fallbackFontPath;
    fontOffset_ = patch->OriginalFontOffset();
    ctorInfo_ = FindMethodInfo(".ctor", 2, "System.String");
    ctor_ = ctorInfo_ ? *reinterpret_cast<void**>(ctorInfo_) : nullptr;
    fonts_.assign(db->pageCount(), nullptr);
    handles_.assign(db->pageCount(), 0);
    Logger::Instance().Log("fontpage", "init pages=" + std::to_string(db->pageCount()) +
                                        " path=" + EscapeForLog(fontPath_) + " offset=" + std::to_string(fontOffset_) +
                                        " ctor=" + std::to_string((uintptr_t)ctor_));
    return ctor_ != nullptr && model_->textureFontClass() != nullptr;
}

void* FontPages::CreatePageFont(int page) {
    if (page < 0 || page >= (int)db_->pageCount()) return nullptr;
    if (!ctor_ || !g_api.object_new) return nullptr;
    std::vector<uint16_t> chars;
    auto add = [&](uint16_t c) {
        if (c == 0 || c == '\r' || c == '\n' || c == '\t') return;
        if (std::find(chars.begin(), chars.end(), c) == chars.end()) chars.push_back(c);
    };
    // Base set shared by every page.  TextureFont..ctor already loads every
    // ASCII punctuation explicitly; adding it to the char array would insert
    // the same dictionary key twice and abort the ctor.
    for (uint16_t c = 'a'; c <= 'z'; ++c) add(c);
    for (uint16_t c = '0'; c <= '9'; ++c) add(c);
    for (uint16_t c : db_->pageChars(page)) {
        // These are loaded explicitly by TextureFont..ctor; adding them to the
        // charset array would insert the same dictionary key twice.
        if (c == 0x20AC || c == 0x221E || c == 0x00B0) continue;
        add(c);
    }
    if (chars.size() > 240) {
        Logger::Instance().Log("fontpage", "page " + std::to_string(page) + " too many chars=" +
                                                std::to_string(chars.size()) + "; refusing to exceed texture array limit");
        return nullptr;
    }

    Logger::Instance().Log("fontpage", "creating page " + std::to_string(page) + " chars=" + std::to_string(chars.size()));
    patch_->BeginPage(chars);
    Il2CppString* path = NewString(fontPath_);
    Il2CppObject* font = g_api.object_new(model_->textureFontClass());
    uint32_t fontHandle = 0;
    uint32_t pathHandle = 0;
    if (font) fontHandle = g_api.gchandle_new ? g_api.gchandle_new(font, false) : 0;
    if (path) pathHandle = g_api.gchandle_new ? g_api.gchandle_new(reinterpret_cast<Il2CppObject*>(path), false) : 0;
    if (font && path) {
        Logger::Instance().Log("fontpage", "calling ctor for page " + std::to_string(page));
        void* args[2] = { path, &fontOffset_ };
        Il2CppObject* exc = nullptr;
        if (g_api.runtime_invoke && ctorInfo_) {
            g_api.runtime_invoke(ctorInfo_, font, args, &exc);
        } else {
            reinterpret_cast<void (*)(void*, Il2CppString*, int)>(ctor_)(font, path, fontOffset_);
        }
        if (exc) {
            Logger::Instance().Log("fontpage", "page " + std::to_string(page) + " ctor threw an exception; skipping page");
            failed_.insert(page);
            patch_->EndPage();
            return nullptr;
        }
        Logger::Instance().Log("fontpage", "ctor returned for page " + std::to_string(page));
    }

    // Keep the path string rooted.  Freeing a GC handle from this native frame
    // has proven unstable; one handle per page is negligible.
    (void)pathHandle;
    patch_->EndPage();
    if (!font) {
        Logger::Instance().Log("fontpage", "page " + std::to_string(page) + " allocation failed");
        return nullptr;
    }
    handles_[page] = fontHandle;
    fonts_[page] = font;
    ++created_;
    Logger::Instance().Log("fontpage", "page " + std::to_string(page) + " ready");
    return font;
}

bool FontPages::IsPageReady(int page) const {
    return page >= 0 && page < (int)fonts_.size() && fonts_[page] != nullptr;
}

int FontPages::EnsurePage(int page) {
    if (page < 0 || page >= (int)fonts_.size()) return -1;
    if (!fonts_[page]) CreatePageFont(page);
    return fonts_[page] ? page : -1;
}

void FontPages::RequestPage(int page) {
    if (page < 0 || page >= (int)fonts_.size() || fonts_[page]) return;
    if (failed_.count(page)) return;
    pending_.insert(page);
}

void FontPages::ProcessPending() {
    if (pending_.empty()) return;
    auto it = pending_.begin();
    int page = *it;
    pending_.erase(it);
    CreatePageFont(page);
}

void FontPages::Force(void* self, void* font, size_t fontOffset) {
    if (!self || !font || fontOffset == SIZE_MAX) return;
    auto it = originalFont_.find(self);
    if (it == originalFont_.end()) {
        originalFont_[self] = *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + fontOffset);
    }
    *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + fontOffset) = font;
}

void FontPages::Clear(void* self, size_t fontOffset) {
    if (!self || fontOffset == SIZE_MAX) return;
    auto it = originalFont_.find(self);
    if (it == originalFont_.end()) return;
    *reinterpret_cast<void**>(reinterpret_cast<char*>(self) + fontOffset) = it->second;
    originalFont_.erase(it);
}

bool FontPages::IsForced(void* self) const {
    return originalFont_.find(self) != originalFont_.end();
}
