#include "translations.h"
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    TranslationDb db;
    if (!db.Load(argv[1])) { std::printf("LOAD_FAIL %s\n", db.lastError().c_str()); return 2; }
    struct Case { const char* key; const char* input; const char* want; };
    const std::vector<Case> cases = {
        {"HUD:push", "push", "推动"},
        {"HUD:edit every", "edit every", "批量编辑"},
        {"HUD:wall thickness", "wall thickness", "墙体厚度"},
        {"GEN:cards", "24 cards", "24 卡片"},
        {"GEN:cards", "1.2k cards", "1.2k 卡片"},
        {"GEN:slabs", "200 slabs", "200 形块"},
        {"GEN:slabs", "74k slabs", "74k 形块"},
        {"PREWARM:hud", "prewarm hud", "缩短 延长 断开"},
        {"GEN:generating the world", "generating the world...", "正在生成世界..."},
        {"GEN:rendering the world", "rendering the world...", "正在渲染世界..."},
    };
    int failed = 0;
    for (const auto& c : cases) {
        std::string out, mk, ms;
        bool ok = db.Lookup({c.key}, c.input, out, mk, ms);
        bool pass = ok && out == c.want;
        std::printf("%s %s -> %s (want %s)\n", pass ? "PASS" : "FAIL", c.key, out.c_str(), c.want);
        if (!pass) ++failed;
    }
    return failed == 0 ? 0 : 1;
}
