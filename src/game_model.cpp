#include "game_model.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace {

static bool IsSingleKeyToken(const std::string& s) {
    if (s.size() == 1 && (std::isdigit((unsigned char)s[0]) ||
                          (s[0] >= 'a' && s[0] <= 'z'))) return true;
    if (s.size() >= 2 && s[0] == 'f' && std::isdigit((unsigned char)s[1])) {
        for (size_t i = 1; i < s.size(); ++i)
            if (!std::isdigit((unsigned char)s[i])) return false;
        return std::stoi(s.substr(1)) >= 1 && std::stoi(s.substr(1)) <= 24;
    }
    return false;
}

static bool SplitKeyAction(const std::string& raw, std::string& key, std::string& action) {
    static const char* names[] = {
        "side mouse button a", "side mouse button b", "middle mouse button",
        "right mouse button", "left mouse button", "caps lock",
        "backspace", "up arrow", "down arrow", "left arrow", "right arrow",
        "control", "escape", "space", "shift", "enter", "ctrl", "esc", "tab"
    };
    key.clear();
    action.clear();
    auto cleanAction = [](std::string a) {
        a = Trim(a);
        if (a.rfind("to ", 0) == 0) a = Trim(a.substr(3));
        else if (a.rfind("for ", 0) == 0) a = Trim(a.substr(4));
        return a;
    };
    std::string s = LowerAscii(Trim(raw));
    if (s.empty()) return false;
    if (s.size() >= 2 && s.front() == '[') {
        size_t close = s.find(']');
        if (close != std::string::npos) s = Trim(s.substr(1, close - 1)) + s.substr(close + 1);
    }
    for (const char* n : names) {
        std::string k = n;
        if (s == k) { key = k; return true; }
        if (s.size() > k.size() && s.compare(0, k.size(), k) == 0 && s[k.size()] == ' ') {
            key = k;
            action = cleanAction(s.substr(k.size() + 1));
            return true;
        }
    }
    size_t sp = s.find(' ');
    std::string first = sp == std::string::npos ? s : s.substr(0, sp);
    if (IsSingleKeyToken(first)) {
        key = first;
        if (sp != std::string::npos) action = cleanAction(s.substr(sp + 1));
        return true;
    }
    action = cleanAction(s);
    return true;
}

static bool IsNamedKey(const std::string& lower) {
    std::string probe = lower;
    if (probe.size() >= 2 && probe.front() == '[' && probe.back() == ']')
        probe = probe.substr(1, probe.size() - 2);
    static const char* keys[] = {
        "left mouse button", "right mouse button", "middle mouse button",
        "side mouse button a", "side mouse button b",
        "up arrow", "down arrow", "left arrow", "right arrow",
        "space", "shift", "tab", "backspace", "enter", "escape", "esc", "caps lock"
    };
    for (const char* k : keys) if (probe == k) return true;
    return false;
}

std::string NormalizeSpace(const std::string& s) {
    return CollapseSpaces(Trim(s));
}

std::string NormalizeKeyName(const std::string& raw) {
    std::string s = raw;
    size_t nl = s.find('\n');
    if (nl != std::string::npos) s = s.substr(0, nl);
    s = NormalizeSpace(s);
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (std::isdigit((unsigned char)s[i])) {
            out += '#';
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
        } else {
            out += s[i++];
        }
    }
    return out;
}

std::string NormalizeHeader(const std::string& s) {
    return NormalizeSpace(s);
}

std::string NormalizeCardPath(const std::string& raw) {
    std::string s = raw;
    std::replace(s.begin(), s.end(), '\\', '/');
    size_t p = s.find("/content/");
    if (p != std::string::npos) s = s.substr(p + 9);
    if (s.size() > 5 && s.substr(s.size() - 5) == ".json") s = s.substr(0, s.size() - 5);
    return s;
}

const char* kWorldTypes[] = {"default", "floating islands", "sunken ocean", "flat survival", "flat sandbox"};
const char* kHudActions[] = {"attack", "build", "drink", "drop", "eat", "hold", "jump", "mine", "none", "place", "punch", "shoot", "swing", "throw", "use"};

bool IsWorldType(const std::string& s, int* index) {
    std::string lower = LowerAscii(s);
    for (char& c : lower) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    std::string collapsed;
    collapsed.reserve(lower.size());
    bool space = false;
    for (char c : lower) {
        if (c == ' ') {
            if (!space) collapsed.push_back(' ');
            space = true;
        } else {
            collapsed.push_back(c);
            space = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    size_t start = collapsed.find_first_not_of(' ');
    if (start == std::string::npos) return false;
    std::string clean = collapsed.substr(start);
    for (int i = 0; i < 5; ++i) {
        if (clean == kWorldTypes[i]) { if (index) *index = i; return true; }
    }
    return false;
}

bool IsHudAction(const std::string& s) {
    std::string lower = LowerAscii(s);
    for (const char* a : kHudActions) if (lower == a) return true;
    return false;
}

std::string FirstLine(const std::string& s) {
    size_t p = s.find('\n');
    return p == std::string::npos ? s : s.substr(0, p);
}

std::string KeybindText(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) t += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    return LowerAscii(CollapseSpaces(Trim(t)));
}

bool LooksLikeKeycap(const std::string& s) {
    if (s.empty() || s.size() > 12) return false;
    bool hasLetter = false;
    for (unsigned char c : s) {
        if (c >= 'a' && c <= 'z') return false;
        if (c == ' ') return false;
        if (c >= 'A' && c <= 'Z') hasLetter = true;
    }
    return hasLetter || std::isdigit((unsigned char)s[0]);
}

} // namespace

bool GameModel::Init() {
    window_ = FindClass("Window");
    button_ = FindClass("Button");
    textureText_ = FindClass("TextureText");
    textureFont_ = FindClass("TextureFont");
    typeButton_ = FindClass("TypeButton");
    cardType_ = FindClass("CardType");
    keybindButton_ = FindClass("KeybindButton");
    lord_ = FindClass("Lord");
    tip_ = FindClass("Tip");
    if (!tip_) tip_ = FindClass("Window.Tip");
    if (!tip_) tip_ = FindClass("Window/Tip");
    if (!window_ || !button_ || !textureText_ || !textureFont_ || !lord_) return false;

    winHeader_ = FieldOffset(window_, "header");
    winHeaderText_ = FieldOffset(window_, "header_text");
    winButtons_ = FieldOffset(window_, "buttons");
    winDragText_ = FieldOffset(window_, "drag_text");
    winWindows_ = FindField(window_, "windows");
    winPopups_ = FindField(window_, "popups");
    winLeaving_ = FindField(window_, "leaving");
    winTip_ = FindField(window_, "tip");
    winTooltipText_ = FindField(window_, "tooltip_text");

    btnName_ = FieldOffset(button_, "name");
    btnTextName_ = FieldOffset(button_, "text_name");
    btnTextValue_ = FieldOffset(button_, "text_value");
    btnTooltip_ = FieldOffset(button_, "tooltip");
    btnWindow_ = FieldOffset(button_, "w");
    btnValueString_ = FieldOffset(button_, "value_string");
    btnTranslate_ = FieldOffset(button_, "translate");
    btnConfirmable_ = FieldOffset(button_, "confirmable");

    ttTranslate_ = FieldOffset(textureText_, "translate");
    ttText_ = FieldOffset(textureText_, "text");
    ttFont_ = FieldOffset(textureText_, "font");
    ttFontSize_ = FieldOffset(textureText_, "font_size");
    ttLanguageId_ = FieldOffset(textureText_, "language_id");
    ttUniverseFont_ = FieldOffset(textureText_, "universe_font");
    ttKeybindChecks_ = FieldOffset(textureText_, "keybind_checks");

    if (typeButton_) tbType_ = FieldOffset(typeButton_, "type");
    if (cardType_) {
        ctPath_ = FieldOffset(cardType_, "path");
        ctName_ = FieldOffset(cardType_, "name");
    }
    if (tip_) {
        tipWord_ = FieldOffset(tip_, "word");
        tipText_ = FieldOffset(tip_, "text");
    }
    if (tipWord_ == SIZE_MAX || tipText_ == SIZE_MAX) {
        // Window.Tip is a nested class; Il2CppDumper reports word at 0x10 and
        // text at 0x18 on this build. The offsets are stable for 2.2x2.
        tipWord_ = 0x10;
        tipText_ = 0x18;
    }
    lordDefaultFont_ = FindField(lord_, "default_font");
    ready_ = true;
    return true;
}

float GameModel::ReadFontSize(void* self) const { return ReadObjectFloat(self, ttFontSize_); }
int GameModel::ReadLanguageId(void* self) const { return ReadObjectInt(self, ttLanguageId_); }
bool GameModel::ReadTranslate(void* self) const { return ReadObjectInt(self, ttTranslate_) != 0; }
void* GameModel::ReadFont(void* self) const { void* p = nullptr; ReadObjectPtr(self, ttFont_, &p); return p; }

std::vector<void*> GameModel::Windows() const {
    std::vector<void*> out;
    auto add = [&](FieldInfo* field) {
        ForEachManagedList(StaticPtr(field), [&](void* w, int) { if (w) out.push_back(w); });
    };
    add(winWindows_);
    add(winPopups_);
    add(winLeaving_);
    return out;
}

std::vector<void*> GameModel::Buttons(void* window) const {
    std::vector<void*> out;
    if (!window || winButtons_ == SIZE_MAX) return out;
    void* list = nullptr;
    ReadObjectPtr(window, winButtons_, &list);
    ForEachManagedList(list, [&](void* b, int) { if (b) out.push_back(b); });
    return out;
}

std::string GameModel::WindowHeader(void* window) const {
    if (!window || winHeader_ == SIZE_MAX) return {};
    void* s = nullptr;
    ReadObjectPtr(window, winHeader_, &s);
    return ReadManagedString(reinterpret_cast<Il2CppString*>(s));
}

void* GameModel::WindowHeaderText(void* window) const {
    if (!window || winHeaderText_ == SIZE_MAX) return nullptr;
    void* p = nullptr;
    ReadObjectPtr(window, winHeaderText_, &p);
    return p;
}

std::string GameModel::ButtonName(void* button) const {
    if (!button || btnName_ == SIZE_MAX) return {};
    void* s = nullptr;
    ReadObjectPtr(button, btnName_, &s);
    return ReadManagedString(reinterpret_cast<Il2CppString*>(s));
}

std::string GameModel::ButtonClass(void* button) const { return ClassName(button); }

void* GameModel::ButtonTextName(void* button) const {
    if (!button || btnTextName_ == SIZE_MAX) return nullptr;
    void* p = nullptr;
    ReadObjectPtr(button, btnTextName_, &p);
    return p;
}

void* GameModel::ButtonTextValue(void* button) const {
    if (!button || btnTextValue_ == SIZE_MAX) return nullptr;
    void* p = nullptr;
    ReadObjectPtr(button, btnTextValue_, &p);
    return p;
}

std::string GameModel::TextureText(void* text) const {
    if (!text || ttText_ == SIZE_MAX) return {};
    void* s = nullptr;
    ReadObjectPtr(text, ttText_, &s);
    return ReadManagedString(reinterpret_cast<Il2CppString*>(s));
}

void* GameModel::WindowTip(void* window) const {
    if (!window || winTip_ == nullptr) return nullptr;
    void* p = nullptr;
    ReadObjectPtr(window, FieldOffset(window_, "tip"), &p);
    return p;
}

std::string GameModel::TipWord(void* tip) const {
    if (!tip || tipWord_ == SIZE_MAX) return {};
    void* s = nullptr;
    ReadObjectPtr(tip, tipWord_, &s);
    return ReadManagedString(reinterpret_cast<Il2CppString*>(s));
}

std::string GameModel::TipText(void* tip) const {
    if (!tip || tipText_ == SIZE_MAX) return {};
    void* s = nullptr;
    ReadObjectPtr(tip, tipText_, &s);
    return ReadManagedString(reinterpret_cast<Il2CppString*>(s));
}

void* GameModel::TooltipText() const { return StaticPtr(winTooltipText_); }

ObjectKeys GameModel::Describe(void* self, const std::string& input) const {
    ObjectKeys out;
    if (!self) return out;
    const std::string cls = ClassName(self);
    auto add = [&](const std::string& k) {
        if (k.empty()) return;
        for (const auto& e : out.keys) if (e == k) return;
        out.keys.push_back(k);
    };
    auto setPrimary = [&](const std::string& k) {
        out.primary = k;
        add(k);
    };

    const std::string firstLine = LowerAscii(Trim(FirstLine(input)));
    if (firstLine.find("recipes crafted") != std::string::npos)
        add("X:TextureText|recipes crafted");
    if (firstLine.find("recipes still locked") != std::string::npos)
        add("X:TextureText|recipes still locked");
    if (firstLine.find(" to trade") != std::string::npos)
        add("X:TextureText|to trade");
    // Death text is handled only through the Card death-context hook; never
    // add an anonymous X:TextureText|death key here.

    auto processButton = [&](void* w, void* b, int index, int dupIndex, const std::string& listName) {
        if (!b) return;
        void* tn = nullptr;
        void* tv = nullptr;
        ReadObjectPtr(b, btnTextName_, &tn);
        ReadObjectPtr(b, btnTextValue_, &tv);
        const char* which = nullptr;
        if (tn == self) which = "text_name";
        else if (tv == self) which = "text_value";
        if (!which) return;

        std::string header;
        void* h = nullptr;
        if (ReadObjectPtr(w, winHeader_, &h)) header = NormalizeHeader(ReadManagedString(reinterpret_cast<Il2CppString*>(h)));
        std::string bname = ReadManagedString(reinterpret_cast<Il2CppString*>(
            [&]() { void* p = nullptr; ReadObjectPtr(b, btnName_, &p); return p; }()));
        std::string keyName = NormalizeKeyName(bname);
        if (keyName.empty()) keyName = "#" + std::to_string(index);
        else if (dupIndex > 0) keyName += "#" + std::to_string(dupIndex);
        std::string bclass = ClassName(b);

        std::string namedKey = LowerAscii(Trim(input));
        if (IsNamedKey(namedKey) &&
            bclass.find("CategoryButton") == std::string::npos &&
            bclass.find("BoolButton") == std::string::npos) {
            add("KEYCAP:" + KeybindText(input));
            out.translatable = true;
            out.userContent = false;
        }

        // RequestButton displays a physical key name (e.g. "control"), not a
        // behavior label. Keep it separate from KEYBIND:control.
        if (bclass.find("RequestButton") != std::string::npos) {
            add("KEYCAP:" + KeybindText(input));
            std::string requestKey = LowerAscii(Trim(input));
            if (requestKey == "control" || requestKey == "escape" || requestKey == "esc") {
                out.translatable = true;
                out.userContent = false;
            } else {
                out.translatable = false;
                out.userContent = true;
            }
        }

        std::string full = "W:Window:" + header + "|n:" + keyName + ":" + bclass + "|" + which;
        setPrimary(full);
        // World-detail windows are titled with the user's world name.  Emit a
        // stable structural key as an alternate so custom world names do not
        // lose translations.
        {
            // Identify the window by object identity, not by its English text.
            // Menu.w_world_details is the same Window instance for played and
            // never-opened worlds.
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            void* details = nullptr;
            if (menu) ReadObjectPtr(menu, FieldOffset(FindClass("Menu"), "w_world_details"), &details);
            if (details && details == w)
                add("W:Window:world details|n:" + keyName + ":" + bclass + "|" + which);
        }
        out.owned = true;
        out.confirm = btnConfirmable_ != SIZE_MAX && ReadObjectInt(b, btnConfirmable_) != 0 &&
                       LowerAscii(Trim(input)).rfind("are you sure", 0) == 0;
        out.translatable = btnTranslate_ == SIZE_MAX || ReadObjectInt(b, btnTranslate_) != 0;
        if (!out.translatable) out.userContent = true;
        // World-size buttons are identified by membership in Menu.b_world_size,
        // not by the visible letter.
        bool worldSizeButton = false;
        {
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            void* arr = nullptr;
            if (menu) ReadObjectPtr(menu, FieldOffset(FindClass("Menu"), "b_world_size"), &arr);
            int n = arr ? ReadObjectInt(arr, 0x18) : 0;
            void** items = arr ? reinterpret_cast<void**>(reinterpret_cast<char*>(arr) + 0x20) : nullptr;
            for (int i = 0; i < n && items; ++i) {
                if (items[i] == b) { worldSizeButton = true; break; }
            }
        }
        // They are marked non-translatable by the game but are static UI.
        if (worldSizeButton) {
            out.translatable = true;
            out.userContent = false;
        }
        // The promo window is identified by its Menu pointer, not its header
        // text.  Its counters are static UI even though the value buttons are
        // marked non-translatable by the game.
        {
            void* menu = StaticPtr(FindField(FindClass("Lord"), "menu"));
            void* promo = nullptr;
            if (menu) ReadObjectPtr(menu, FieldOffset(FindClass("Menu"), "w_promo"), &promo);
            if (promo && promo == w) {
                add("W:Window:promo|n:" + keyName + ":" + bclass + "|" + which);
                if (keyName == "# people have already pre-ordered!" ||
                    keyName == "#€ raised" || keyName == "#% this months rent") {
                    out.translatable = true;
                    out.userContent = false;
                }
            }
        }
        out.detail = "list=" + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w)) +
                     " button=" + std::to_string(reinterpret_cast<uintptr_t>(b)) + " btnclass=" + bclass +
                     " keyname=" + keyName;

        // TypeButton carries a CardType: use its content path as an object-level key.
        if (typeButton_ && bclass.find("TypeButton") != std::string::npos && tbType_ != SIZE_MAX) {
            void* type = nullptr;
            if (ReadObjectPtr(b, tbType_, &type) && type) {
                std::string path = NormalizeCardPath(ReadManagedString(reinterpret_cast<Il2CppString*>(
                    [&]() { void* p = nullptr; ReadObjectPtr(type, ctPath_, &p); return p; }())));
                if (path.empty()) {
                    path = NormalizeCardPath(ReadManagedString(reinterpret_cast<Il2CppString*>(
                        [&]() { void* p = nullptr; ReadObjectPtr(type, ctName_, &p); return p; }())));
                }
                if (!path.empty()) add("CARD:" + path);
            }
        }

        // Keybind context.
        bool keybind = !worldSizeButton &&
                       (bclass.find("Keybind") != std::string::npos ||
                        ReadObjectInt(self, ttKeybindChecks_) != 0);
        if (keybind) {
            std::string keycapName = LowerAscii(Trim(input));
            bool namedKey = IsNamedKey(keycapName);
            if (LooksLikeKeycap(input) || namedKey) {
                add("KEYCAP:" + KeybindText(input));
                // Named physical keys (mouse/arrows) and the two remapped keys.
                if (namedKey || keycapName == "control") {
                    out.translatable = true;
                    out.userContent = false;
                } else {
                    out.translatable = false;
                    out.userContent = true;
                }
            } else {
                add("KEYBIND:" + KeybindText(input));
                add("KEYBIND:" + LowerAscii(FirstLine(input)));
                // Keybind action labels are explicitly translated through KEYBIND:.
                out.translatable = true;
                out.userContent = false;
            }
        }

        // Music player context.
        std::string hlower = LowerAscii(header);
        if (hlower.find("music") != std::string::npos || hlower.find("song") != std::string::npos)
            add("MUSIC:" + LowerAscii(FirstLine(input)));

        // World type selector.
        int wt = 0;
        if (IsWorldType(input, &wt)) add("MENU:world_types:" + std::to_string(wt));

        if (out.translatable) out.userContent = false;
    };

    auto processWindow = [&](void* w, const std::string& listName) {
        if (!w) return false;
        void* ht = nullptr;
        ReadObjectPtr(w, winHeaderText_, &ht);
        if (ht == self) {
            void* h = nullptr;
            ReadObjectPtr(w, winHeader_, &h);
            std::string header = NormalizeHeader(ReadManagedString(reinterpret_cast<Il2CppString*>(h)));
            setPrimary("W:Window:" + header + "|header_text");
            out.owned = true;
            out.detail = "list=" + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w));
            return true;
        }
        void* drag = nullptr;
        ReadObjectPtr(w, winDragText_, &drag);
        if (drag == self) {
            void* h = nullptr;
            ReadObjectPtr(w, winHeader_, &h);
            std::string header = NormalizeHeader(ReadManagedString(reinterpret_cast<Il2CppString*>(h)));
            setPrimary("W:Window:" + header + "|drag_text");
            std::string lower = LowerAscii(Trim(input));
            add("KEYBIND:" + lower);
            add("HUD:" + lower);
            std::string flat = lower;
            for (char& c : flat) if (c == '\n' || c == '\r') c = ' ';
            add("KEYBIND:" + flat);
            add("HUD:" + flat);
            out.owned = true;
            out.translatable = true;
            out.userContent = false;
            out.detail = "list=" + listName + " window=" + std::to_string(reinterpret_cast<uintptr_t>(w)) + " drag_text";
            return true;
        }
        void* buttons = nullptr;
        ReadObjectPtr(w, winButtons_, &buttons);
        if (!buttons) return false;

        // Count names to disambiguate duplicates within one window.
        std::map<std::string, int> counts;
        ForEachManagedList(buttons, [&](void* b, int) {
            void* p = nullptr;
            ReadObjectPtr(b, btnName_, &p);
            std::string n = NormalizeKeyName(p ? ReadManagedString(reinterpret_cast<Il2CppString*>(p)) : std::string());
            if (!n.empty()) counts[n]++;
        });
        std::map<std::string, int> seen;
        bool hit = false;
        ForEachManagedList(buttons, [&](void* b, int idx) {
            if (hit) return;
            void* p = nullptr;
            ReadObjectPtr(b, btnName_, &p);
            std::string n = NormalizeKeyName(p ? ReadManagedString(reinterpret_cast<Il2CppString*>(p)) : std::string());
            int dup = 0;
            if (!n.empty() && counts[n] > 1) dup = seen[n]++;
            processButton(w, b, idx, dup, listName);
            if (out.owned) hit = true;
        });
        return hit;
    };

    for (int pass = 0; pass < 3 && !out.owned; ++pass) {
        FieldInfo* f = pass == 0 ? winWindows_ : pass == 1 ? winPopups_ : winLeaving_;
        const char* name = pass == 0 ? "windows" : pass == 1 ? "popups" : "leaving";
        ForEachManagedList(StaticPtr(f), [&](void* w, int) { if (!out.owned) processWindow(w, name); });
    }

    // Tooltip object: Window.tip is static; Button.tooltip may be an instance
    // pointer. Scan buttons for the latter, then use the static current tip.
    if (!out.owned) {
        auto matchTip = [&](void* tip) -> bool {
            if (!tip || tipWord_ == SIZE_MAX) return false;
            void* wp = nullptr;
            void* tp = nullptr;
            ReadObjectPtr(tip, tipWord_, &wp);
            if (tipText_ != SIZE_MAX) ReadObjectPtr(tip, tipText_, &tp);
            std::string word = ReadManagedString(reinterpret_cast<Il2CppString*>(wp));
            std::string text = ReadManagedString(reinterpret_cast<Il2CppString*>(tp));
            auto flatten = [](std::string v) {
                for (char& c : v) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
                return Trim(v);
            };
            std::string fword = flatten(word);
            std::string ftext = flatten(text);
            std::string finput = flatten(input);
            if (fword.empty() && ftext.empty()) return false;
            std::string spaced = fword;
            if (!ftext.empty() && ftext != fword) spaced += " " + ftext;
            if (!spaced.empty()) add("TIP:" + LowerAscii(spaced));
            bool joined = finput == spaced;
            if (finput != fword && finput != ftext && !joined) return false;
            auto addCanonical = [&](const std::string& v) {
                std::string key, action;
                if (!SplitKeyAction(v, key, action)) return;
                if (!key.empty()) {
                    add("KEYCAP:" + key);
                    add("KEYCAP:[" + key + "]");
                }
                if (!action.empty()) add("TIP:" + action);
            };
            addCanonical(spaced);
            addCanonical(finput);
            std::string tipKey = joined ? spaced : (fword.empty() ? finput : fword);
            std::string lower = LowerAscii(tipKey);
            setPrimary("TIP:" + lower);
            add("TIP:" + LowerAscii(fword.empty() ? finput : fword));
            auto addTipAction = [&](const std::string& raw) {
                std::string v = LowerAscii(Trim(raw));
                std::string action;
                if (v.rfind("to ", 0) == 0) action = Trim(v.substr(3));
                else if (v.rfind("for ", 0) == 0) action = Trim(v.substr(4));
                else {
                    size_t sep = v.find(" to ");
                    size_t off = 4;
                    if (sep == std::string::npos) {
                        sep = v.find(" for ");
                        off = 5;
                    }
                    if (sep != std::string::npos) action = Trim(v.substr(sep + off));
                    else {
                        size_t op = v.find(" opens ");
                        if (op != std::string::npos) action = Trim(v.substr(op + 1));
                        else {
                            size_t sp = v.find(' ');
                            if (sp != std::string::npos) action = Trim(v.substr(sp + 1));
                        }
                    }
                }
                if (!action.empty()) add("TIP:" + action);
            };
            addTipAction(fword);
            addTipAction(ftext);
            addTipAction(spaced);
            addTipAction(finput);
            add("TIPD:" + lower);
            out.owned = true;
            out.detail = "tip word=" + EscapeForLog(word) + " text=" + EscapeForLog(text);
            return true;
        };
        matchTip(StaticPtr(winTip_));
    }

    // Music prompt rendered by Soul.RenderMusicText: "now playing <song>".
    // It is not inside a Window, so it needs an explicit music context.
    if (!out.owned) {
        std::string lower = LowerAscii(input);
        if (lower.rfind("now playing ", 0) == 0) {
            std::string rest = Trim(input.substr(12));
            size_t by = LowerAscii(rest).find(" by ");
            std::string song = by == std::string::npos ? rest : Trim(rest.substr(0, by));
            std::string songLower = LowerAscii(song);
            setPrimary("MUSIC:now playing:" + songLower);
            add("MUSIC:" + songLower);
            add("MUSIC:now playing");
            out.owned = true;
            out.detail = "music prompt";
        }
    }

    // Loading screen text is "voima demo 2.2x2" followed by a newline and "loading X".
    if (!out.owned) {
        std::string lower = LowerAscii(input);
        size_t p = lower.rfind("loading ");
        if (p != std::string::npos && input.find('\n') != std::string::npos) {
            std::string stage = Trim(input.substr(p));
            while (!stage.empty() && (stage.back() == '.' || stage.back() == ' ')) stage.pop_back();
            setPrimary("LOAD:" + LowerAscii(stage));
            out.owned = true;
            out.loading = true;
            out.detail = "loading";
        }
    }

    if (!out.owned) {
        std::string cls = ClassName(self);
        setPrimary("X:" + cls + "|" + FirstLine(Trim(input)));
        if (ReadObjectInt(self, ttKeybindChecks_) != 0) {
            add("KEYBIND:" + KeybindText(input));
            add("KEYBIND:" + LowerAscii(FirstLine(input)));
        }
        std::string lower = LowerAscii(FirstLine(input));
        if (IsHudAction(lower) && !ReadObjectInt(self, ttTranslate_))
            add("HUD:" + lower);
        if (lower.rfind("edit ", 0) == 0) add("HUD:edit");
        // Drag ghosts and other ownerless behavior labels use bare action text.
        add("KEYBIND:" + lower);
        add("HUD:" + lower);
        std::string flat = lower;
        for (char& c : flat) if (c == '\n' || c == '\r') c = ' ';
        add("KEYBIND:" + flat);
        add("HUD:" + flat);
        if (IsNamedKey(lower)) {
            add("KEYCAP:" + lower);
            out.translatable = true;
        }
        int wt = 0;
        if (IsWorldType(lower, &wt)) add("MENU:world_types:" + std::to_string(wt));
    }

    if (out.keys.empty()) out.primary = "X:" + cls + "|" + FirstLine(Trim(input));
    out.userContent = !out.owned && !ReadObjectInt(self, ttTranslate_);
    if (input.size() > 500 || std::count(input.begin(), input.end(), '\n') > 12) out.userContent = true;
    return out;
}
