# 翻译遗漏补全流程

> 2026-11-17 修订：总体计划以 PLAN.md 为准。补翻译主渠道改为“静态对象目录为主，运行时对象图抽样为辅”；本文的逐场景运行时段落只用于关键界面验证，不再作为发现全游戏文本的主手段。

这不是“把英文句子换成中文”，而是：在正确的对象上，把该对象的文字换成正确的中文，并证明没有漏。采集单位是对象键，不是英文句子。

---

## 0. 原则

1. 先备份，再采集，再翻译。
2. 每个待翻译项必须能回答：哪个对象、哪个字段、哪段原文、在哪个场景出现。
3. 不根据截图里的英文直接猜键；没有对象键的条目宁可标为 ADD_CONTEXT，也不做全局裸文本映射。
4. 不把用户内容、按键名、专有名词误算成遗漏。
5. 每批改动必须通过静态测试、运行时目标测试、视觉检查三道门。
6. 一次只交付一个可回滚批次：commit + tag + bundle。

---

## 1. 数据模型

| 数据                  | 内容                       | 来源                                |
| ------------------- | ------------------------ | --------------------------------- |
| TranslationEntry    | 对象键、英文源模式、中文             | translations/translations.tsv     |
| CaptureRecord       | 场景、对象键、原文、译文、页号、Build 次数 | logs/test_trace.tsv + plugin.log  |
| Gap                 | 类型、对象键、原文、对象详情、首次场景、次数   | logs/untranslated.log             |
| Exemption           | 用户内容/按键名/专有名词/技术文本，附理由   | tests/exemptions.tsv              |
| CoverageRequirement | 场景、必须命中的对象键+原文、期望中文      | tests/coverage_manifest.json      |
| FontPageReport      | 页号、字符数、创建结果、异常           | logs/plugin.log + Font/glyphs.txt |

建议在插件 trace 中增加场景列，由命令文件的 scenario <name> 设置；没有场景时记 unknown。这样同一句英文在不同界面的出现可以分开统计。

---

## 2. 六个阶段

### 阶段 A：冻结基线

- git status 必须干净。
- 记录当前 commit、GameAssembly/metadata SHA256、VOIMA 版本。
- 创建 backup-before-<batch>-YYYYMMDD tag 和 bundle。
- 运行现有测试，记录基线：翻译条目数、字形数、页数、主菜单命中数、untranslated 唯一条目数。

产物：reports/baseline.json

### 阶段 B：建立文本面清单

静态目录为主，运行时对象图为关键界面校验。

B1. 静态对象目录（主渠道）

- 从 global-metadata / dump.cs / stringliteral.json 提取 UI 字符串。

- 扫描 Menu_*、Window/Button 子类、TextureText 构造/Build 调用点、CardType、tooltip、loading。

- 对设置页，直接扫描 Menu_Settings* 方法里的字符串加载，得到标题、tab、标签的真实英文。

- 输出 catalog/ui-objects.tsv，每行包含 owner_class、owner_method、window_header、button_name、button_class、field、source_text、static_key、confidence。

- 按对象分类：
  
    W:Window:<header>|header_text
    W:Window:<header>|n:<button>:<class>|text_name / text_value
    CARD:<path>
    KEYBIND:<action>
    TIP:<word> / TIPD:<text>
    LOAD:<stage>
    HUD:<action> / MUSIC:<song> / MENU:<...>

B2. 运行时对象图抽样（关键界面）

- 启动 VOIMA_ZH.exe --test。
- 只对关键界面做对象图 dump：主菜单、控制、设置四页、暂停、HUD/卡牌、tooltip。
- 每个场景：设置 scenario <name>，Hook Window.RenderWindows 一帧内 dump 所有 Window/Button/TextureText，而不是逐条 Build 记录。
- 合成鼠标受 DPI 影响时，优先用插件 call Menu_* 命令；主菜单直接 Menu_Settings 可能因 uni_ui 为空抛异常，此时用真实鼠标或进入世界后再调用。
- 其余界面由静态对象目录覆盖；正常游玩产生的 untranslated 作为 P2 遥测补漏。

产物：logs/object-dump-<scenario>.tsv + screenshots/<scenario>.png

### 阶段 C：生成差距报告

    python tools/gap_report.py --strict

输入：translations.tsv、logs/test_trace.tsv、logs/untranslated.log、logs/plugin.log、tests/exemptions.tsv。

输出：reports/gap-report.md、reports/gap-report.json。

每条差距必须归入下列一类，不能只写“未翻译”：

| 类型               | 含义                       | 处理                |
| ---------------- | ------------------------ | ----------------- |
| MISSING_ENTRY    | 对象键正确，表里没有               | 补 TSV             |
| KEY_MISMATCH     | 运行时键和表里的键不一致             | 修 resolver/键格式    |
| PATTERN_MISMATCH | 键存在但源模式不匹配（大小写/换行/数字/句点） | 修源模式              |
| UNOWNED          | 解析成 X:，没有对象上下文           | 增加对象探测，禁止裸文本兜底    |
| USER_CONTENT     | 玩家名/世界名/预购者名单            | 写 exemption，不翻译   |
| KEYCAP           | H/J/ENTER/F1 等按键名        | 写 exemption，不翻译   |
| PROPER_NOUN      | 人名/歌名/开发者名               | 白名单，保留英文或音译       |
| FONT_MISSING     | 有中文但 Font 没有 PNG/glyphs  | 重生成字形             |
| PAGE_FAILED      | 页字体构造异常/超层               | 查重复标点、页预算、GC root |
| LAYOUT           | 文字已翻译但溢出/重叠/截断           | 缩短/换行/调字号，不改键     |

指标：

    object_coverage = 已命中对象键数 / (已命中 + 非豁免 MISSING)
    scenario_coverage = 已满足 CoverageRequirement / 总需求
    font_coverage = 有 PNG 的译文字符 / 全部译文字符
    page_success = 成功页数 / 尝试页数

### 阶段 D：按对象翻译

- 只改 translations/translations.tsv，不要直接在源码里写中文。

- 同一个英文在不同对象下必须显式分条；如果确实同义，在注释里写“与 X 同义”，不要偷偷复用。

- 动态数字用 {0}/{1}；动态名字用 {*}。

- 长文本保留换行，按游戏按钮宽度手动断行。

- 专有名词先查 glossary；用户内容写 exemption。

- 每补完一批运行：
  
    python tools/import_translations.py
    python tools/build_font.py
    cmd /c tools\build_all.bat

### 阶段 E：验证

E1. 静态测试

    python -m unittest tests.test_translations tests.test_font tests.test_package tests.test_settings_coverage -v

必须覆盖：

- 三列结构、对象键前缀、W 键含 |
- 占位符不越界
- 同键同源不冲突
- 译文用字都有 PNG + glyphs
- 页预算不超过 100 字/页
- 设置页 tab/标签/颜色值/快捷键动作全覆盖
- 游戏原文件 SHA256 未变
- 无 BepInEx 依赖

E2. 运行时目标测试

    python -m unittest tests.test_integration -v

必须覆盖：

- start ready
- 主菜单对象键实际命中
- 所有页字体创建成功，无 ctor threw
- untranslated.log 无 LOAD/promo 噪音
- 进程存活、无 FATAL
- trace 中出现完整对象键，而不是裸英文

E3. 场景覆盖测试

- 对照 tests/coverage_manifest.json：
  - 每个场景必须有截图。
  - 每个 required_key + source 必须出现在该场景 trace。
  - 每个 required key 的中文必须与 manifest 一致。
- 没有出现在任何场景的翻译条目标记为 UNUSED，人工确认是否废弃。

E4. 视觉检查

- 1280×720、1920×1080、2560×1600；DPI 100% 与 150%。
- 检查截断、重叠、越界、tooltip 换行、中文与图标混排、数字/单位。
- 视觉问题不能靠测试通过掩盖；截图存入 screenshots/<scenario>/<resolution>.png。

### 阶段 F：提交与备份

- CHANGELOG.md 写清本批新增/修复/剩余。
- git add -A && git commit。
- Tag：v0.1.x。
- Bundle：G:\VOIMA_ZH_git\VOIMA_ZH_backup_YYYYMMDD_<batch>.bundle。
- reports/ 保留本批差距报告和覆盖率，作为下一批基线。
- 清理 logs/ 和临时 .done 文件。

---

## 3. 场景清单（最低要求）

| 场景     | 关键对象                   | 必查                           |
| ------ | ---------------------- | ---------------------------- |
| 主菜单    | 根 Window、promo         | 七个按钮、continue 动态存档名、promo 计数 |
| 控制     | controls、KeybindButton | 动作名、键名、tooltip               |
| 设置-音频  | tabs、audio settings    | tab、音量、混响、耳机                 |
| 设置-视频  | video settings         | 菜单颜色/缩放/分辨率/像素/帧率/全屏         |
| 设置-画面  | graphics settings      | 两页图形选项                       |
| 设置-玩法  | gameplay settings      | 右键建造/奔跑/视角摇晃/雾/提示            |
| 新世界    | your new world...      | 尺寸/风格/种子/难度                  |
| 暂停/退出  | pause、quit             | 返回/保存/退出                     |
| 模组     | mods                   | 导入/应用/编辑模式/空状态               |
| 多人     | multiplayer            | Steam 错误、房间列表                |
| 加载     | LOAD                   | 每个 loading 阶段                |
| HUD/卡牌 | HUD、CARD               | 动作、物品名、tooltip               |
| 制作人员   | credits                | 人名/角色名（白名单）                  |

---

## 4. 完成定义

一批翻译可以交付，当且仅当：

1. gap_report --strict 对目标场景返回 0 个非豁免差距。
2. 所有 CoverageRequirement 满足。
3. 单元测试与集成测试全绿。
4. 所有字体页构造成功，所有译文字符有 PNG。
5. 目标场景截图人工检查无截断/重叠。
6. untranslated.log 为空，或只剩已登记 exemption。
7. Git 干净，有 tag + bundle，CHANGELOG 更新。

---

## 5. 为什么不能只扫英文

同一个 settings 可能是：

- 主菜单按钮 → W:Window:|n:settings:Button|text_name
- 设置页 tab → W:Window:tabs|n:settings:Button|text_name
- 某个设置项说明 → TIP:settings

三者可能分别是“设置”“设置页”“设置项说明”。只按英文建表会把它们压成一句；对象键流程才能保留差异。

同样，loading 可能是加载阶段，也可能是按钮进行时文案；back 可能是返回，也可能是背部/后方。对象键是消歧的唯一稳定依据。
