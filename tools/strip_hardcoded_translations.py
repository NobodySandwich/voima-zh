from pathlib import Path
p=Path(r"E:\Github\VOIMA_ZH\src\plugin.cpp")
s=p.read_text(encoding="utf-8")
# Remove HUD fallback map.
start=s.index("std::string TranslateHudAction(const std::string& action) {")
m=s.index("    static const std::map<std::string, std::string> actions = {", start)
e=s.index("    std::string a = LowerAscii(Trim(action));", m)
s=s[:m]+s[e:]
# Remove fallback map lookup.
a=s.index("    auto it = actions.find(a);", start)
b=s.index("    // \"edit every <object>\" has a dynamic suffix.", a)
s=s[:a]+s[b:]
# Use TSV-only dynamic prefixes.
s=s.replace('            std::string prefix = "批量编辑";\n            std::string p;\n            if (g_db.Lookup({"HUD:edit every"}, "edit every", p, matchedKey, matchedSource) && !p.empty()) prefix = p;\n            return prefix + translated;', '            std::string prefix, p;\n            if (g_db.Lookup({"HUD:edit every"}, "edit every", p, matchedKey, matchedSource) && !p.empty()) prefix = p;\n            return prefix.empty() ? translated : prefix + translated;')
s=s.replace('        std::string prefix = "批量编辑";\n        std::string p;\n        if (g_db.Lookup({"HUD:edit every"}, "edit every", p, matchedKey, matchedSource) && !p.empty()) prefix = p;\n        return prefix;', '        std::string prefix, p;\n        if (g_db.Lookup({"HUD:edit every"}, "edit every", p, matchedKey, matchedSource) && !p.empty()) prefix = p;\n        return prefix;')
s=s.replace('            std::string prefix = "墙体厚度";\n            std::string p, mk, ms;\n            if (g_db.Lookup({"HUD:wall thickness"}, "wall thickness", p, mk, ms) && !p.empty()) prefix = p;\n            return prefix + " " + value;', '            std::string prefix, p, mk, ms;\n            if (g_db.Lookup({"HUD:wall thickness"}, "wall thickness", p, mk, ms) && !p.empty()) prefix = p;\n            return prefix.empty() ? value : prefix + " " + value;')
# Replace generation function with TSV-only implementation.
gs=s.index("bool TryTranslateGenerationText(const std::string& input, std::string& out) {")
ge=s.index("bool TryTranslateDeathContext", gs)
new='''bool TryTranslateGenerationText(const std::string& input, std::string& out) {
    if (!g_generationContext.load()) return false;
    std::string result;
    bool changed = false;
    size_t start = 0;
    while (start <= input.size()) {
        size_t end = input.find('\n', start);
        std::string line = input.substr(start, end == std::string::npos ? std::string::npos : end - start);
        std::string text = Trim(line);
        std::string lower = LowerAscii(text);
        bool generationLike = lower.rfind("generating the world", 0) == 0 || lower.rfind("rendering the world", 0) == 0 ||
                              lower.find(" cards") != std::string::npos || lower.find(" card") != std::string::npos ||
                              lower.find(" slabs") != std::string::npos || lower.find(" slab") != std::string::npos;
        std::string translated, matchedKey, matchedSource;
        if (generationLike) {
            for (const char* key : {"GEN:generating the world", "GEN:rendering the world", "GEN:cards", "GEN:slabs"}) {
                if (g_db.Lookup({key}, text, translated, matchedKey, matchedSource) && !translated.empty())
                    break;
            }
        }
        if (!translated.empty()) { result += translated; changed = true; }
        else result += line;
        if (end == std::string::npos) break;
        result.push_back('\n');
        start = end + 1;
    }
    out = result;
    return changed;
}

'''
s=s[:gs]+new+s[ge:]
p.write_text(s,encoding="utf-8")
print("stripped")
