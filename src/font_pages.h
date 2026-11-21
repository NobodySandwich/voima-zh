#pragma once
#include "game_model.h"
#include "translations.h"
#include "font_patch.h"
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class FontPages {
public:
    bool Init(GameModel* model, TranslationDb* db, FontPatch* patch, const std::string& fallbackFontPath);
    bool IsPageReady(int page) const;
    int EnsurePage(int page);
    void RequestPage(int page);
    void ProcessPending();
    void Force(void* self, void* font, size_t fontOffset);
    void Clear(void* self, size_t fontOffset);
    void InvalidatePage(int page) { if (page >= 0 && page < (int)fonts_.size()) fonts_[page] = nullptr; failed_.erase(page); pending_.erase(page); }
    bool IsForced(void* self) const;
    void* ForcedFont(void* self) const;
    int CreatedCount() const { return created_; }
    int PageCount() const { return (int)fonts_.size(); }
    void* FontForPage(int page) const { return page >= 0 && page < (int)fonts_.size() ? fonts_[page] : nullptr; }

private:
    void* FindMethod(const char* method, int paramCount, const char* firstParamNeedle) const;
    MethodInfo* FindMethodInfo(const char* method, int paramCount, const char* firstParamNeedle) const;
    void* CreatePageFont(int page);
    Il2CppString* NewString(const std::string& utf8) const;

    GameModel* model_ = nullptr;
    TranslationDb* db_ = nullptr;
    FontPatch* patch_ = nullptr;
    std::string fontPath_;
    int fontOffset_ = 0;
    void* ctor_ = nullptr;
    MethodInfo* ctorInfo_ = nullptr;
    std::set<int> failed_;
    std::vector<void*> fonts_;
    std::vector<uint32_t> handles_;
    std::set<int> pending_;
    std::unordered_map<void*, void*> originalFont_;
    std::unordered_map<void*, void*> forcedFont_;
    int created_ = 0;
};
