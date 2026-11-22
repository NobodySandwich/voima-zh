#pragma once
#include <cstdint>
#include <string>
#include <vector>

class FontPatch {
public:
    bool InstallEarly(uintptr_t gameAssemblyBase, const std::string& fontDir);
    bool InstallDynamic(uintptr_t gameAssemblyBase, void* ctor, void* loadChar, void* toCharArray, const std::string& fontDir);
    bool Installed() const { return installed_; }

    void BeginPage(const std::vector<uint16_t>& chars);
    void EndPage();
    bool PageMode() const;

    std::string OriginalFontPath() const;
    int OriginalFontOffset() const;
    void SetOriginalFontOffset(int offset);
    void SetFallbackFontPath(const std::string& path);

    bool Expanded() const;
    int ExpandedCount() const;
    int RedirectCount() const;
    bool SawMismatch() const;

private:
    bool installed_ = false;
};
