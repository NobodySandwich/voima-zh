#include "translations.h"
#include "util.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace {

std::string NormalizeMatch(const std::string& s) {
    std::string t = Trim(s);
    // Keep newlines meaningful for patterns, but collapse runs of plain spaces.
    std::string out;
    out.reserve(t.size());
    bool space = false;
    for (char c : t) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!space) { out += ' '; space = true; }
        } else {
            out += c;
            space = false;
        }
    }
    return out;
}

std::string NormalizeLiteral(const std::string& s) {
    std::string out;
    bool space = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (!space) { out += ' '; space = true; }
        } else {
            out += c;
            space = false;
        }
    }
    return out;
}

std::string GetLeading(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && (unsigned char)s[i] <= ' ') ++i;
    return s.substr(0, i);
}

std::string GetTrailing(const std::string& s) {
    size_t i = s.size();
    while (i > 0 && (unsigned char)s[i - 1] <= ' ') --i;
    return s.substr(i);
}

std::string ApplyWhitespace(const std::string& original, std::string translated) {
    std::string lead = GetLeading(original);
    std::string trail = GetTrailing(original);
    if (!lead.empty() && !translated.empty() && translated[0] != ' ' && translated[0] != '\n')
        translated = lead + translated;
    if (!trail.empty() && !translated.empty() && translated.back() != ' ' && translated.back() != '\n')
        translated += trail;
    return translated;
}

} // namespace

std::vector<int> TranslationDb::PagesForPrefix(const std::string& prefix) const {
    std::vector<int> out;
    for (const auto& e : entries_) {
        if (e.page < 0 || e.key.compare(0, prefix.size(), prefix) != 0) continue;
        if (std::find(out.begin(), out.end(), e.page) == out.end()) out.push_back(e.page);
    }
    return out;
}

bool TranslationDb::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        lastError_ = "cannot open " + path;
        return false;
    }
    std::string line;
    std::set<std::string> seen;
    int lineNo = 0;
    while (std::getline(f, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        size_t start = 0;
        while (true) {
            size_t tab = line.find('\t', start);
            if (tab == std::string::npos) { cols.push_back(line.substr(start)); break; }
            cols.push_back(line.substr(start, tab - start));
            start = tab + 1;
        }
        if (cols.size() < 3) continue;
        std::string key = Trim(cols[0]);
        std::string source = UnescapeText(cols[1]);
        std::string translation = UnescapeText(cols[2]);
        if (key.empty() || source.empty() || translation.empty()) continue;
        std::string ident = key + "\x1f" + source;
        if (seen.count(ident)) continue;
        seen.insert(ident);
        TranslationEntry e;
        e.key = key;
        e.source = source;
        e.translation = translation;
        // Placeholder parser
        for (size_t i = 0; i < source.size();) {
            if (source[i] == '{') {
                size_t close = source.find('}', i + 1);
                if (close != std::string::npos) {
                    std::string token = source.substr(i + 1, close - i - 1);
                    if (token == "*") {
                        e.pattern = true;
                        e.literals.push_back(source.substr(0, i)); // will be rebuilt below
                        e.placeholders.push_back(-1);
                        i = close + 1;
                        continue;
                    }
                    bool digits = !token.empty() && std::all_of(token.begin(), token.end(), ::isdigit);
                    if (digits) {
                        e.pattern = true;
                        e.literals.push_back(source.substr(0, i));
                        e.placeholders.push_back(std::stoi(token));
                        i = close + 1;
                        continue;
                    }
                }
            }
            ++i;
        }
        if (e.pattern) {
            // Rebuild literals cleanly.
            e.literals.clear();
            e.placeholders.clear();
            std::string lit;
            for (size_t i = 0; i < source.size();) {
                if (source[i] == '{') {
                    size_t close = source.find('}', i + 1);
                    if (close != std::string::npos) {
                        std::string token = source.substr(i + 1, close - i - 1);
                        if (token == "*" || (!token.empty() && std::all_of(token.begin(), token.end(), ::isdigit))) {
                            e.literals.push_back(lit);
                            lit.clear();
                            e.placeholders.push_back(token == "*" ? -1 : std::stoi(token));
                            i = close + 1;
                            continue;
                        }
                    }
                }
                lit += source[i++];
            }
            e.literals.push_back(lit);
        }
        size_t idx = entries_.size();
        entries_.push_back(std::move(e));
        index_[key].push_back(idx);
    }
    return true;
}

namespace {

std::vector<uint16_t> NonAsciiChars(const std::string& utf8) {
    std::vector<uint16_t> out;
    std::wstring wide = Utf8ToWide(utf8);
    for (wchar_t wc : wide) {
        if (wc < 128) continue;
        uint16_t c = (uint16_t)wc;
        bool seen = false;
        for (uint16_t x : out) if (x == c) { seen = true; break; }
        if (!seen) out.push_back(c);
    }
    return out;
}

int KeyPriority(const std::string& key) {
    if (key.rfind("W:", 0) == 0) return 0;
    if (key.rfind("LOAD:", 0) == 0 || key.rfind("KEYBIND:", 0) == 0 ||
        key.rfind("HUD:", 0) == 0 || key.rfind("TIP:", 0) == 0 || key.rfind("TIPD:", 0) == 0 ||
        key.rfind("MENU:", 0) == 0 || key.rfind("MUSIC:", 0) == 0) return 1;
    if (key.rfind("CARD:", 0) == 0) return 2;
    return 3;
}

} // namespace

void TranslationDb::AssignPages(size_t pageCapacity) {
    pages_.clear();
    for (auto& e : entries_) e.page = -1;
    std::vector<size_t> order(entries_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        int pa = KeyPriority(entries_[a].key);
        int pb = KeyPriority(entries_[b].key);
        return pa < pb;
    });
    for (size_t idx : order) {
        TranslationEntry& e = entries_[idx];
        std::vector<uint16_t> chars = NonAsciiChars(e.translation);
        if (e.key.rfind("TIP:", 0) == 0) {
            // Runtime LocalizeTooltipKey injects Chinese key names into TIP
            // translations, so those glyphs must live on the TIP page too.
            static const wchar_t extra[] = L"鼠标左右中侧键上下方向";
            for (const wchar_t* p = extra; *p; ++p) {
                uint16_t c = (uint16_t)*p;
                if (std::find(chars.begin(), chars.end(), c) == chars.end()) chars.push_back(c);
            }
        }
        if (chars.empty()) continue;
        int found = -1;
        for (size_t p = 0; p < pages_.size(); ++p) {
            bool all = true;
            for (uint16_t c : chars) {
                if (std::find(pages_[p].chars.begin(), pages_[p].chars.end(), c) == pages_[p].chars.end()) {
                    all = false;
                    break;
                }
            }
            if (all) { found = (int)p; break; }
        }
        if (found < 0) {
            for (size_t p = 0; p < pages_.size(); ++p) {
                if (pages_[p].chars.size() + chars.size() <= pageCapacity) {
                    // Prefer a page that shares characters to reduce total pages.
                    size_t overlap = 0;
                    for (uint16_t c : chars)
                        if (std::find(pages_[p].chars.begin(), pages_[p].chars.end(), c) != pages_[p].chars.end()) ++overlap;
                    if (overlap == 0 && pages_[p].chars.size() + chars.size() > pageCapacity) continue;
                    found = (int)p;
                    break;
                }
            }
        }
        if (found < 0) {
            pages_.push_back({});
            found = (int)pages_.size() - 1;
        }
        FontPage& page = pages_[found];
        for (uint16_t c : chars)
            if (std::find(page.chars.begin(), page.chars.end(), c) == page.chars.end()) page.chars.push_back(c);
        page.entries.push_back(idx);
        e.page = found;
    }
}

bool TranslationDb::MatchEntry(const TranslationEntry& e, const std::string& normalized,
                               std::string& translated) {
    if (!e.pattern) {
        if (NormalizeMatch(e.source) != normalized) return false;
        translated = e.translation;
        return true;
    }
    // Build a regex from the literal pieces, with capture groups for placeholders.
    std::string rx = "^";
    for (size_t i = 0; i < e.literals.size(); ++i) {
        std::string escaped;
        for (char c : NormalizeLiteral(e.literals[i])) {
            if (std::string(".^$|()[]*+?{}\\").find(c) != std::string::npos) escaped += '\\';
            escaped += c;
        }
        rx += escaped;
        if (i < e.placeholders.size()) {
            rx += e.placeholders[i] == -1 ? "(.*?)" : "([0-9]+)";
        }
    }
    rx += "$";
    try {
        std::regex re(rx);
        std::smatch m;
        if (!std::regex_match(normalized, m, re)) return false;
        std::string out;
        for (size_t i = 0; i < e.translation.size();) {
            if (e.translation[i] == '{') {
                size_t close = e.translation.find('}', i + 1);
                if (close != std::string::npos) {
                    std::string token = e.translation.substr(i + 1, close - i - 1);
                    if (token == "*") {
                        size_t group = 1;
                        for (size_t k = 0; k < e.placeholders.size(); ++k)
                            if (e.placeholders[k] == -1) { group = k + 1; break; }
                        if (group < m.size()) out += m[group].str();
                        i = close + 1;
                        continue;
                    }
                    if (!token.empty() && std::all_of(token.begin(), token.end(), ::isdigit)) {
                        size_t n = (size_t)std::stoi(token);
                        size_t group = 0;
                        for (size_t k = 0; k < e.placeholders.size(); ++k)
                            if (e.placeholders[k] == (int)n) { group = k + 1; break; }
                        if (group == 0) {
                            for (size_t k = 0; k < e.placeholders.size(); ++k)
                                if ((size_t)e.placeholders[k] == n) { group = k + 1; break; }
                        }
                        if (group < m.size()) out += m[group].str();
                        i = close + 1;
                        continue;
                    }
                }
            }
            out += e.translation[i++];
        }
        translated = out;
        return true;
    } catch (const std::regex_error&) {
        return false;
    }
}

bool TranslationDb::Lookup(const std::vector<std::string>& keys, const std::string& input,
                           std::string& output, std::string& matchedKey, std::string& matchedSource,
                           int* page) const {
    std::string normalized = NormalizeMatch(input);
    for (const std::string& key : keys) {
        auto it = index_.find(key);
        if (it == index_.end()) continue;
        // Exact source first, then patterns.
        for (size_t pass = 0; pass < 2; ++pass) {
            for (size_t idx : it->second) {
                const TranslationEntry& e = entries_[idx];
                if ((pass == 0) == e.pattern) continue;
                std::string translated;
                if (!MatchEntry(e, normalized, translated)) continue;
                output = ApplyWhitespace(input, translated);
                matchedKey = key;
                matchedSource = e.source;
                if (page) *page = e.page;
                return true;
            }
        }
    }
    return false;
}
