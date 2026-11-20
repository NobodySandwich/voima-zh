# Changelog

## 0.1.2 — 2026-11-20

翻译覆盖

- 翻译表扩展到 2119 行，补齐主菜单、设置、控制、世界菜单、作弊菜单、HUD、卡牌、动物对话、加载文字和 credits。
- 新增对象键：populate tools、change card size、world details、Karhula 删除确认、printer、import universe、credits 等。
- 作弊 HUD 补齐 paint、reset、glue、unglue、to select；修正 set grid / remove grid 与 are you sure 的显示。
- credits 按原版内容补全，第三方开源条目、作者名和致谢保留原有格式。

字体与加载

- 字体页改为按需构造，并加入主线程分帧队列，每 80ms 最多构造一页，避免一次性长时间阻塞。
- 字体页队列提前到加载条阶段入队，进入世界时不再临时构造页面，减少卡顿和加载转圈。
- 保留加载文本原始的省略号点数，不再用固定文本覆盖游戏的 . / .. / ... 动画。
- 修复动物对话缓存命中时字体页未重新应用导致的空气泡。
- 移除了会破坏按钮状态并导致物品栏崩溃的 Button 字段写入实验。

稳定性与工程

- 启动器改为 GUI 子系统，启动时不再弹出控制台。
- 保持独立注入，不引入 BepInEx 或其他模组框架。
- GitHub 源码树整理：只保留构建、测试、文档、许可证和翻译所需内容。
- 移除构建产物、日志、生成字体、发布二进制、临时字体注入文件和实验字形。

## 0.1.0 — 2026-11-17

首个可运行版本。

- 独立启动器注入 VOIMA_ZH.dll，不依赖 BepInEx。
- 运行时 IL2CPP 反射解析 TextureText / TextureFont / Window / Button / Lord。
- 对象感知对象键：窗口 + 按钮 + 控件类 + 字段，另有 CARD / KEYBIND / TIP / LOAD / HUD / MUSIC / MENU 上下文键。
- 687 条翻译条目，覆盖主菜单、控制、设置、卡牌、动物语音、加载等。
- 霞鹜文楷 Screen 字形，200×200，717 个字形。
- 字体分页：绕开 Texture2DArray 256 层限制，按需在 Lord.Update 创建页字体。
- 修复 GC 导致的 String.get_Chars 崩溃（翻译字符串、字体路径全部加 GC root）。
- 日志：plugin.log / untranslated.log / test_trace.tsv。
- 测试：单元测试 13 项 + 实际启动游戏集成测试 6 项。
