#pragma once
#include "il2cpp_api.h"
#include <string>
#include <vector>

struct ObjectKeys {
    std::string primary;
    std::vector<std::string> keys;
    std::string detail;
    bool owned = false;
    bool userContent = false;
    bool loading = false;
    bool confirm = false;
    bool translatable = true;
};

class GameModel {
public:
    bool Init();
    bool Ready() const { return ready_; }

    ObjectKeys Describe(void* self, const std::string& input) const;

    std::vector<void*> Windows() const;
    std::vector<void*> Buttons(void* window) const;
    std::string WindowHeader(void* window) const;
    void* WindowHeaderText(void* window) const;
    std::string ButtonName(void* button) const;
    std::string ButtonClass(void* button) const;
    void* ButtonTextName(void* button) const;
    void* ButtonTextValue(void* button) const;
    std::string TextureText(void* text) const;
    void* WindowTip(void* window) const;
    std::string TipWord(void* tip) const;
    std::string TipText(void* tip) const;
    void* TooltipText() const;

    Il2CppClass* textureTextClass() const { return textureText_; }
    Il2CppClass* textureFontClass() const { return textureFont_; }
    Il2CppClass* lordClass() const { return lord_; }
    Il2CppClass* windowClass() const { return window_; }
    size_t fontOffset() const { return ttFont_; }

    float ReadFontSize(void* self) const;
    int ReadLanguageId(void* self) const;
    bool ReadTranslate(void* self) const;
    void* ReadFont(void* self) const;

private:
    bool ready_ = false;

    Il2CppClass* window_ = nullptr;
    Il2CppClass* button_ = nullptr;
    Il2CppClass* textureText_ = nullptr;
    Il2CppClass* textureFont_ = nullptr;
    Il2CppClass* typeButton_ = nullptr;
    Il2CppClass* cardType_ = nullptr;
    Il2CppClass* keybindButton_ = nullptr;
    Il2CppClass* lord_ = nullptr;
    Il2CppClass* tip_ = nullptr;

    size_t winHeader_ = SIZE_MAX, winHeaderText_ = SIZE_MAX, winButtons_ = SIZE_MAX, winDragText_ = SIZE_MAX;
    size_t btnName_ = SIZE_MAX, btnTextName_ = SIZE_MAX, btnTextValue_ = SIZE_MAX, btnTooltip_ = SIZE_MAX, btnWindow_ = SIZE_MAX;
    size_t btnValueString_ = SIZE_MAX;
    size_t btnTranslate_ = SIZE_MAX;
    size_t btnConfirmable_ = SIZE_MAX;
    size_t ttTranslate_ = SIZE_MAX, ttText_ = SIZE_MAX, ttFont_ = SIZE_MAX, ttFontSize_ = SIZE_MAX;
    size_t ttLanguageId_ = SIZE_MAX, ttUniverseFont_ = SIZE_MAX, ttKeybindChecks_ = SIZE_MAX;
    size_t tbType_ = SIZE_MAX;
    size_t ctPath_ = SIZE_MAX, ctName_ = SIZE_MAX;
    size_t tipWord_ = SIZE_MAX, tipText_ = SIZE_MAX;

    FieldInfo* winWindows_ = nullptr;
    FieldInfo* winPopups_ = nullptr;
    FieldInfo* winLeaving_ = nullptr;
    FieldInfo* winTip_ = nullptr;
    FieldInfo* winTooltipText_ = nullptr;
    FieldInfo* lordDefaultFont_ = nullptr;
};
