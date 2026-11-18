#!/usr/bin/env python3
"""Convert the legacy VOIMA_ZH translations.txt into the object-aware TSV.

Input lines:  object_key <TAB> chinese <TAB> # english source
Output lines: object_key <TAB> source_pattern <TAB> chinese

The object key is preserved as the semantic identity.  Digit runs become
{0}, {1}, ... so changing counts (pre-orders, goals) do not invalidate the key.
"""
import re
import sys
from pathlib import Path

SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(r"G:\VOIMA\game\VOIMA_ZH\translations.txt")
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / "translations" / "translations.tsv"


def norm_key(key: str) -> str:
    if key.startswith("W:Window:"):
        rest = key[len("W:Window:"):]
        header, sep, tail = rest.partition("|")
        header = " ".join(header.strip().split())
        if not sep:
            return key
        if tail.startswith("n:"):
            name, _, rest2 = tail[2:].partition(":")
            # button name is the first line only; digits become # for stability
            name = name.split("\\n")[0].split("\n")[0]
            name = " ".join(name.strip().split())
            if not name:
                name = "#0"
            name = re.sub(r"\d+", "#", name)
            tail = "n:" + name + ":" + rest2
        return f"W:Window:{header}|{tail}"
    return key


def make_source_pattern(source: str) -> tuple[str, list[str]]:
    nums = []
    def repl(m):
        val = m.group(0)
        if val not in nums:
            nums.append(val)
        return "{%d}" % nums.index(val)
    pat = re.sub(r"\d+", repl, source)
    return pat, nums


def main() -> int:
    seen = set()
    rows = []
    for raw in SRC.read_text(encoding="utf-8-sig").splitlines():
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        parts = raw.split("\t")
        if len(parts) < 2:
            continue
        key = norm_key(parts[0].strip())
        zh = parts[1]
        comment = ""
        if "#" in raw:
            comment = raw.split("#", 1)[1].strip()
        if not key or key.startswith("R:"):
            continue
        source = comment or ""
        if not source:
            continue
        if "\t" in key or "\t" in source or "\t" in zh or "\\NUM\\" in key or "\\NUM\\" in source or "\\NUM\\" in zh:
            continue
        # Special dynamic cases observed at runtime.
        if key.startswith("W:Window:") and "|n:continue playing..." in key:
            source = "continue playing...\n{*}"
            if "\n" not in zh and "\\n" not in zh:
                zh = zh + "\n{*}"
        if key == "W:Window:promo|n:911 people have already pre-ordered!:TextButton|text_name":
            key = "W:Window:promo|n:# people have already pre-ordered!:TextButton|text_name"
        if key.startswith("W:Window:promo|n:") and "monthly goal" in key:
            key = "W:Window:promo|n:monthly goal #€:Button|text_name"
            source = "monthly goal {0}€"
            zh = "每月目标 {0}€"
        if key == "ANIMAL:characters/bear/bear:flee_messages:7":
            # "4" is leetspeak for "for", and "1!1!!" is excitement.  These
            # digits are not counts, so do not turn them into placeholders.
            pat = source
            zh = "快跑，为了你的 VOIMA！！！1！1！！"
        else:
            pat, nums = make_source_pattern(source)
            # Mirror the digit placeholders in the translation where the same literal appears.
            for i, n in enumerate(nums):
                zh = zh.replace(n, "{%d}" % i, 1)
        ident = (key, pat)
        if ident in seen:
            continue
        seen.add(ident)
        rows.append((key, pat, zh))

    # A handful of strings that were not in the legacy table but are visible on the
    # first screen (captured from the runtime object graph).
    extra = [
        ("W:Window:promo|n:help us create new updates!:TextButton|text_name", "help us create new updates!", "帮助我们更新！"),
        ("W:Window:promo|n:Be forever remembered..:TextButton|text_name", "Be forever remembered..", "永远被铭记于心..."),
        ("W:Window:promo|n:as one of the first:TextButton|text_name", "as one of the first", "作为第一批"),
        ("W:Window:promo|n:believers in VOIMA:TextButton|text_name", "believers in VOIMA", "信仰 VOIMA 的人"),
        ("W:Window:promo|n:by naming a bear:TextButton|text_name", "by naming a bear", "通过为一只熊命名"),
        ("W:Window:promo|n:with a pre-order!:TextButton|text_name", "with a pre-order!", "通过预购！"),
        ("W:Window:promo|n:Instead of adding microtransactions,:TextButton|text_name", "Instead of adding microtransactions,", "我们没有加入微交易，"),
        ("W:Window:promo|n:or rushing an unfinished paid release,:TextButton|text_name", "or rushing an unfinished paid release,", "也没有仓促推出未完成的付费版，"),
        ("W:Window:promo|n:we are funding voima with pre-orders :):TextButton|text_name", "we are funding voima with pre-orders :)", "我们通过预购来资助 VOIMA :)"),
        ("W:Window:promo|n:visit our website:Button|text_name", "visit our website\nand name a bear!", "访问我们的网站"),
        ("W:Window:promo|n:and...:TextButton|text_name", "and...", "而且..."),
        ("W:Window:promo|n:we also have a patreon:Button|text_name", "we also have a patreon", "我们也有 Patreon"),
        ("X:TextureText|voima is 100% community funded.", "voima is 100% community funded.", "VOIMA 100% 由社区资助"),
        ("W:Window:promo|n:# people have already pre-ordered!:TextButton|text_name", "{0} people have already pre-ordered!", "已有 {0} 人预购"),
        ("W:Window:promo|n:monthly goal #€:Button|text_name", "monthly goal {0}€", "每月目标 {0}€"),
        ("KEYBIND:up arrow", "up arrow", "上方向键"),
        ("KEYBIND:down arrow", "down arrow", "下方向键"),
        ("KEYBIND:left arrow", "left arrow", "左方向键"),
        ("KEYBIND:right arrow", "right arrow", "右方向键"),
        ("KEYBIND:move forth", "move forth", "向前移动"),
        ("KEYBIND:move left", "move left", "向左移动"),
        ("KEYBIND:move back", "move back", "向后移动"),
        ("KEYBIND:move right", "move right", "向右移动"),
        ("KEYBIND:left mouse button", "left mouse button", "鼠标左键"),
        ("KEYBIND:middle mouse button", "middle mouse button", "鼠标中键"),
        ("KEYBIND:right mouse button", "right mouse button", "鼠标右键"),
        ("KEYBIND:side mouse button a", "side mouse button a", "鼠标侧键 A"),
        ("KEYBIND:side mouse button a", "side mouse button A", "鼠标侧键 A"),
        ("KEYCAP:control", "control", "Ctrl"),
        ("KEYCAP:control", "CONTROL", "Ctrl"),
        ("KEYCAP:escape", "escape", "esc"),
        ("KEYCAP:escape", "ESCAPE", "esc"),
        ("KEYCAP:escape", "esc", "esc"),
        ("KEYCAP:escape", "ESC", "esc"),
        ("W:Window:tabs|n:audio settings:Button|text_name", "audio settings", "音频设置"),
        ("W:Window:tabs|n:video settings:Button|text_name", "video settings", "视频设置"),
        ("W:Window:tabs|n:graphics settings:Button|text_name", "graphics settings", "画面设置"),
        ("W:Window:tabs|n:gameplay settings:Button|text_name", "gameplay settings", "游戏设置"),
        ("W:Window:tabs|n:exit settings:Button|text_name", "exit settings", "退出设置"),
        ("TIP:audio settings", "audio settings", "音频设置"),
        ("TIP:video settings", "video settings", "视频设置"),
        ("TIP:graphics settings", "graphics settings", "画面设置"),
        ("TIP:gameplay settings", "gameplay settings", "游戏设置"),
        ("TIP:exit settings", "exit settings", "退出设置"),
        ("W:Window:video settings|n:menu color:Button|text_value", "CALM", "柔和"),
        ("W:Window:video settings|n:menu color:Button|text_value", "DITHER", "抖动"),
        ("W:Window:video settings|n:menu color:StringButton|text_value", "CALM", "柔和"),
        ("W:Window:video settings|n:menu color:StringButton|text_value", "DITHER", "抖动"),
        ("W:Window:graphics settings|n:toggle run:BoolButton|text_name", "toggle run", "切换奔跑"),
        # M2 batch 1: main menu / controls / multiplayer / set color / updates /
        # pause / backups / world details / missing types / universes / mods.
        ("W:Window:|n:change universe:Button|text_name", "change universe", "切换宇宙"),
        ("W:Window:|n:wishlist:Button|text_name", "wishlist", "加入愿望单"),
        ("W:Window:|n:discord blog:Button|text_name", "discord blog", "Discord 博客"),
        ("W:Window:controls|n:show keyboard and mouse:Button|text_name", "show keyboard and mouse", "显示键鼠"),
        ("W:Window:controls|n:show simple controls list:Button|text_name", "show simple controls list", "显示简洁控制"),
        ("W:Window:multiplayer|n:create server!:Button|text_name", "create server!", "创建服务器！"),
        ("W:Window:set color|n:brightness:Button|text_name", "brightness", "亮度"),
        ("W:Window:set color|n:color:Button|text_name", "color", "颜色"),
        ("W:Window:updates|n:Removed:TextButton|text_name", "Removed", "移除"),
        ("W:Window:updates|n:Added:BoolButton|text_name", "Added", "新增"),
        ("W:Window:updates|n:Modified:TextButton|text_name", "Modified", "修改"),
        ("W:Window:updates|n:Moved:TextButton|text_name", "Moved", "移动"),
        ("W:Window:updates|n:no updates needed:Button|text_name", "no updates needed", "无需更新"),
        ("W:Window:updates|n:update now:Button|text_name", "update now", "立即更新"),
        ("W:Window:updates|n:updates included!:TextButton|text_name", "updates included!", "已包含更新！"),
        ("W:Window:|n:enable cheats:Button|text_name", "enable cheats", "启用作弊"),
        ("W:Window:backups|n:auto save:BoolButton|text_name", "auto save", "自动保存"),
        ("W:Window:backups|n:delete backups:Button|text_name", "delete backups", "删除备份"),
        ("W:Window:backups|n:latest:Button|text_name", "latest", "最新"),
        ("W:Window:backups|n:create new backup:StringButton|text_name", "create new backup", "创建新备份"),
        ("W:Window:backups|n:original:Button|text_name", "original", "原始"),
        ("W:Window:backups|n:clicking one of these backups:TextButton|text_name", "clicking one of these backups\nwill replace the current save\nwith a copy of the backup", "点击任一备份\n将用副本替换当前存档"),
        ("W:Window:backups|n:done:Button|text_name", "done", "完成"),
        ("W:Window:world details|n:survival world:TextButton|text_name", "survival world", "生存世界"),
        ("W:Window:world details|n:clear stats:Button|text_name", "clear stats", "清除统计"),
        ("W:Window:world details|n:play as:Button|text_name", "play as\nmultiplayer", "以多人身份游玩"),
        ("W:Window:world details|n:done editing:Button|text_name", "done editing", "完成编辑"),
        ("W:Window:world details|n:duplicate:Button|text_name", "duplicate", "复制"),
        ("W:Window:world details|n:rename:StringButton|text_name", "rename", "重命名"),
        ("W:Window:your new world...|n:New World:Button|text_name", "New World", "新世界"),
        ("W:Window:world buttons|n:Worlds:Button|text_name", "Worlds", "世界"),
        ("W:Window:world buttons|n:Select Worlds:Button|text_name", "Select Worlds", "选择世界"),
        ("W:Window:missing types|n:done and play world:Button|text_name", "done and play world", "完成并进入世界"),
        ("W:Window:missing types|n:Replace:TypeButton|text_name", "Replace", "替换"),
        ("W:Window:missing types|n:Would you like to replace:TextButton|text_name", "Would you like to replace\nthese missing slab types?", "是否替换这些缺失的板类型？"),
        ("W:Window:missing types|n:Would you like to replace:TextButton|text_name", "Would you like to replace\nthese missing card types?", "是否替换这些缺失的卡牌类型？"),
        ("W:Window:|n:thanks for playing voima!:TextButton|text_name", "thanks for playing voima!", "感谢游玩 VOIMA！"),
        ("W:Window:|n:credits:Button|text_name", "credits", "制作人员"),
        ("W:Window:universes|n:import universe:Button|text_name", "import universe", "导入宇宙"),
        ("W:Window:universes|n:Select Universe:Button|text_name", "Select Universe", "选择宇宙"),
        ("W:Window:mods|n:show health:BoolButton|text_name", "show health", "显示生命"),
        ("W:Window:mods|n:show touch area:BoolButton|text_name", "show touch area", "显示触碰范围"),
        ("W:Window:mods|n:show eye level:BoolButton|text_name", "show eye level", "显示视线高度"),
        ("W:Window:mods|n:create mod:Button|text_name", "create mod", "创建模组"),
        ("W:Window:mods|n:delete mod:Button|text_name", "delete mod", "删除模组"),
        ("W:Window:mods|n:save mod:Button|text_name", "save mod", "保存模组"),
        ("W:Window:mods|n:Select Mods:Button|text_name", "Select Mods", "选择模组"),
        # Early loading-screen text rendered by Lord, outside the Window graph.
        ("X:TextureText|voima is still buggy and not complete...    so for now   i made this free :)",
         "voima is still buggy and not complete...    so for now   i made this free :)",
         "voima 仍然一堆bug，并且还没做完...\n所以暂时免费提供给大家 :)"),
        ("X:TextureText|voima is still buggy, laggy and not complete btw...     thats why i made this free :)",
         "voima is still buggy, laggy and not complete btw...     thats why i made this free :)",
         "voima 仍然一堆bug，还很卡，并且还没做完...\n所以暂时免费提供给大家 :)"),
        ("TIP:open chat", "open chat", "打开聊天"),
        ("TIP:exit universe editor", "exit universe editor", "退出宇宙编辑器"),
        ("TIP:weapons", "weapons", "武器"),
        ("TIP:world settings", "world settings", "世界设置"),
        ("W:Window:creative settings|n:survival:Button|text_name", "survival", "生存"),
        ("W:Window:world settings|n:delete things:TypeButton|text_name", "delete things", "删除物品"),
        ("TIP:survival", "survival", "生存"),
        ("W:Window:world settings|header_text", "world settings", "世界设置"),
        ("W:Window:world settings|n:drops:BoolButton|text_name", "drops", "掉落物"),
        ("W:Window:world settings|n:freeze cards:BoolButton|text_name", "freeze cards", "冻结卡牌"),
        ("W:Window:world settings|n:freeze slabs:BoolButton|text_name", "freeze slabs", "冻结板"),
        ("W:Window:world settings|n:peaceful beings:BoolButton|text_name", "peaceful beings", "和平生物"),
        ("W:Window:world settings|n:card spawning:BoolButton|text_name", "card spawning", "卡牌生成"),
        ("W:Window:world settings|n:delete things:Button|text_name", "delete things", "删除物品"),
        ("TIP:drops", "drops", "掉落物"),
        ("TIP:freeze cards", "freeze cards", "冻结卡牌"),
        ("TIP:freeze slabs", "freeze slabs", "冻结板"),
        ("TIP:peaceful beings", "peaceful beings", "和平生物"),
        ("TIP:card spawning", "card spawning", "卡牌生成"),
        ("TIP:delete things", "delete things", "删除物品"),
        ("W:Window:fly settings|header_text", "fly settings", "飞行设置"),
        ("W:Window:fly settings|n:ghost movement:BoolButton|text_name", "ghost movement", "幽灵移动"),
        ("W:Window:fly settings|n:fly speed:SliderButton|text_name", "fly speed", "飞行速度"),
        ("W:Window:fly settings|n:player scale:SliderButton|text_name", "player scale", "玩家大小"),
        ("W:Window:fly settings|n:camera spin:SliderButton|text_name", "camera spin", "镜头旋转"),
        ("TIP:fly settings", "fly settings", "飞行设置"),
        ("TIP:glue gun", "glue gun", "胶枪"),
        ("TIP:healer heals others = 4", "healer\nheals others = 4", "治疗器\n治疗他人 = 4"),
        ("TIP:selector", "selector", "选择器"),
        ("TIP:card hand", "card hand", "卡牌手"),
        ("TIP:slab hand", "slab hand", "板材手"),
        ("W:Window:cheat menu|header_text", "cheat menu", "作弊菜单"),
        ("TIP:cheat menu", "cheat menu", "作弊菜单"),
        ("W:Window:crafting|header_text", "crafting", "合成"),
        ("X:TextureText|crafting", "crafting", "合成"),
        ("X:TextureText|misc", "misc", "杂项"),
        ("X:TextureText|weapons", "weapons", "武器"),
        ("X:TextureText|mining", "mining", "采矿"),
        ("X:TextureText|building", "building", "建造"),
        ("X:TextureText|tough", "tough", "坚韧"),
        ("X:TextureText|fluids", "fluids", "流体"),
        ("X:TextureText|healing", "healing", "治疗"),
        ("X:TextureText|tools", "tools", "工具"),
        ("X:TextureText|paintings", "paintings", "画作"),
        ("X:TextureText|slabs", "slabs", "板"),
        ("X:TextureText|hud_font_page", "hud_font_page", "副操作建造主操作设置网格复制尺寸切换网格更多攻击挖掘放置跳跃丢弃使用装备拳击射击挥动投掷手持饮用食用瘫倒奔跑重生打开背包合成返回鼠标侧键滚轮"),
        ("X:TextureText|recipes crafted", "{0} / {1} recipes crafted", "{0} / {1} 个配方已合成"),
        ("X:TextureText|recipes still locked", "{0} / {1} recipes still locked", "{0} / {1} 个配方未解锁"),
        ("X:TextureText|recipes still locked", "{0} recipes still locked", "{0} 个配方未解锁"),
        ("X:TextureText|recipes still locked", "{0} recipe still locked", "{0} 个配方未解锁"),
        ("X:TextureText|speech_font_page", "speech_font_page", "交易鼠标左右中键侧滚轮"),
        ("X:TextureText|to trade", "{*} to trade", "{*} 交易"),
        ("TIP:to trade", "{*} to trade", "{*} 交易"),
        ("X:TextureText|death", "you were killed by {*}.", "你被 {*} 杀死了。"),
        ("X:TextureText|death", "you exploded by {*}.", "你被 {*} 炸死了。"),
        ("X:TextureText|death", "you fell to your death.", "你摔死了。"),
        ("X:TextureText|death", "you fell to their death.", "你摔死了。"),
        ("X:TextureText|death", "you fell into the void.", "你掉入虚空。"),
        ("X:TextureText|death", "you drowned.", "你淹死了。"),
        ("X:TextureText|death", "you exploded.", "你爆炸了。"),
        ("X:TextureText|death", "you squished by a slab.", "你被板压扁了。"),
        ("X:TextureText|death", "you hit a wall too hard.", "你撞墙太狠了。"),
        ("X:TextureText|death", "you died from a falling {*}", "你被掉落的 {*} 砸死了。"),
        ("X:TextureText|death", "you died.", "你死了。"),
        ("X:TextureText|letz lvl", "letz lvl", "来单挑"),
        ("X:TextureText|srsly", "srsly", "说真的"),
        ("X:TextureText|you fell to your death.", "you fell to your death.", "你摔死了。"),
        ("X:TextureText|play again", "play again", "再玩一次"),
        ("TIP:play again", "play again", "再玩一次"),
        ("W:Window:death|n:play again:Button|text_name", "play again", "再玩一次"),
        ("W:Window:|n:play again:Button|text_name", "play again", "再玩一次"),
        ("TIP:recipes crafted", "{0} / {1} recipes crafted", "{0} / {1} 个配方已合成"),
        ("TIP:recipes still locked", "{0} / {1} recipes still locked", "{0} / {1} 个配方未解锁"),
        ("X:TextureText|{0} / {1} recipes crafted", "{0} / {1} recipes crafted", "{0} / {1} 个配方已合成"),
        ("TIP:{0} / {1} recipes crafted", "{0} / {1} recipes crafted", "{0} / {1} 个配方已合成"),
        ("TIP:{0} / {1} recipes still locked", "{0} / {1} recipes still locked", "{0} / {1} 个配方未解锁"),
        ("X:TextureText|{0} / {1} recipes still locked", "{0} / {1} recipes still locked", "{0} / {1} 个配方未解锁"),
        ("X:TextureText|1 recipe still locked", "1 recipe still locked", "1 个配方未解锁"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "normal cube", "普通立方"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "normal slab", "普通板"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "normal pillar", "普通柱"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "normal wall", "普通墙"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "half cube", "半立方"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "half slab", "半板"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "half pillar", "半柱"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "half wall", "半墙"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "quarter cube", "四分之一立方"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "quarter slab", "四分之一板"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "quarter pillar", "四分之一柱"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "quarter wall", "四分之一墙"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiny cube", "微型立方"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiny slab", "微型板"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiny pillar", "微型柱"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiny wall", "微型墙"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiniest cube", "最小立方"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiniest slab", "最小板"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiniest pillar", "最小柱"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "tiniest wall", "最小墙"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "circle", "圆形"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "triangle", "三角形"),
        ("W:Window:block tools|n:size:BlockButton|text_name", "hexagon", "六边形"),
        ("X:TextureText|tooth", "tooth", "牙齿"),
        ("X:TextureText|plank", "plank", "木板"),
        ("X:TextureText|jawn axe", "jawn axe", "嚼斧"),
        ("TIP:cheat cards", "cheat cards", "作弊卡牌"),
        ("TIP:time settings", "time settings", "时间设置"),
        ("TIP:immortal", "immortal", "无敌"),
        ("W:Window:time settings|header_text", "time settings", "时间设置"),
        ("W:Window:time settings|n:freeze sun:BoolButton|text_name", "freeze sun", "冻结太阳"),
        ("W:Window:time settings|n:time of day:SliderButton|text_name", "time of day", "时间"),
        ("W:Window:time settings|n:shadow angle:SliderButton|text_name", "shadow angle", "阴影角度"),
        ("W:Window:creative settings|n:immortal:Button|text_name", "immortal", "无敌"),
        ("TIP:mining", "mining", "采矿"),
        ("TIP:building", "building", "建造"),
        ("TIP:cheat slabs", "cheat slabs", "作弊板"),
        ("TIP:tough", "tough", "坚韧"),
        ("TIP:fluids", "fluids", "流体"),
        ("TIP:healing", "healing", "治疗"),
        ("TIP:tools", "tools", "工具"),
        ("TIP:misc", "misc", "杂项"),
        ("TIP:paintings", "paintings", "画作"),
        ("TIP:voim voim", "voim voim", "VOIM"),
        ("TIP:slabs", "slabs", "板"),
        ("TIP:control pad", "control pad", "控制板"),
        ("TIP:control pad", "#control pad", "控制板"),
        ("TIP:#control pad", "control pad", "控制板"),
        ("TIP:#control pad", "#control pad", "控制板"),
        ("TIP:honey potion heals 50%", "honey potion\nheals 50%", "蜂蜜药水\n治疗 50%"),
        ("TIP:burger heals 100%", "burger\nheals 100%", "汉堡\n治疗 100%"),
        ("KEYBIND:exit universe editor", "exit universe editor", "退出宇宙编辑器"),
        ("HUD:exit universe editor", "exit universe editor", "退出宇宙编辑器"),
        ("HUD:edit", "edit {*}", "编辑 {*}"),
        ("KEYBIND:craft", "craft", "合成"),
        ("HUD:craft", "craft", "合成"),
        ("TIP:craft", "craft", "合成"),
        ("KEYBIND:crafting", "crafting", "合成"),
        ("HUD:crafting", "crafting", "合成"),
        ("TIP:crafting", "crafting", "合成"),
        ("TIP:up arrow", "up arrow", "上方向键"),
        ("TIP:down arrow", "down arrow", "下方向键"),
        ("TIP:left arrow", "left arrow", "左方向键"),
        ("TIP:right arrow", "right arrow", "右方向键"),
        ("TIP:left mouse button", "left mouse button", "鼠标左键"),
        ("TIP:right mouse button", "right mouse button", "鼠标右键"),
        ("TIP:middle mouse button", "middle mouse button", "鼠标中键"),
        ("TIP:side mouse button a", "side mouse button a", "鼠标侧键 A"),
        ("TIP:side mouse button b", "side mouse button b", "鼠标侧键 B"),
        ("TIP:opens the chat window", "{*} opens the chat window", "【{*}】打开聊天窗口"),
        ("TIP:opens the chat window", "opens the chat window", "打开聊天窗口"),
        ("KEYCAP:left mouse button", "left mouse button", "鼠标左键"),
        ("KEYCAP:right mouse button", "right mouse button", "鼠标右键"),
        ("KEYCAP:middle mouse button", "middle mouse button", "鼠标中键"),
        ("KEYCAP:side mouse button a", "side mouse button a", "鼠标侧键 A"),
        ("KEYCAP:side mouse button b", "side mouse button b", "鼠标侧键 B"),
        ("KEYCAP:up arrow", "up arrow", "上方向键"),
        ("KEYCAP:down arrow", "down arrow", "下方向键"),
        ("KEYCAP:left arrow", "left arrow", "左方向键"),
        ("KEYCAP:right arrow", "right arrow", "右方向键"),
        ("TIP:[left mouse button]", "[left mouse button]", "鼠标左键"),
        ("TIP:[right mouse button]", "[right mouse button]", "鼠标右键"),
        ("TIP:[middle mouse button]", "[middle mouse button]", "鼠标中键"),
        ("TIP:[side mouse button a]", "[side mouse button a]", "鼠标侧键 A"),
        ("TIP:[side mouse button b]", "[side mouse button b]", "鼠标侧键 B"),
        ("TIP:[up arrow]", "[up arrow]", "上方向键"),
        ("TIP:[down arrow]", "[down arrow]", "下方向键"),
        ("TIP:[left arrow]", "[left arrow]", "左方向键"),
        ("TIP:[right arrow]", "[right arrow]", "右方向键"),
        ("KEYCAP:[left mouse button]", "[left mouse button]", "鼠标左键"),
        ("KEYCAP:[right mouse button]", "[right mouse button]", "鼠标右键"),
        ("KEYCAP:[middle mouse button]", "[middle mouse button]", "鼠标中键"),
        ("KEYCAP:[side mouse button a]", "[side mouse button a]", "鼠标侧键 A"),
        ("KEYCAP:[side mouse button b]", "[side mouse button b]", "鼠标侧键 B"),
        ("KEYCAP:[up arrow]", "[up arrow]", "上方向键"),
        ("KEYCAP:[down arrow]", "[down arrow]", "下方向键"),
        ("KEYCAP:[left arrow]", "[left arrow]", "左方向键"),
        ("KEYCAP:[right arrow]", "[right arrow]", "右方向键"),
        ("TIP:primary action", "primary action", "主操作"),
        ("TIP:secondary action", "secondary action", "副操作"),
        ("TIP:higher shadow quality", "higher shadow quality", "提升阴影"),
        ("MUSIC:now playing", "now playing {*}", "正在播放 {*}"),
        ("MUSIC:now playing", "now playing {*} by {*}", "正在播放 {*}"),
    ]
    for key, src, zh in extra:
        ident = (key, src)
        if ident in seen:
            continue
        seen.add(ident)
        rows.append((key, src, zh))

    # Remove malformed legacy duplicates that double-escaped placeholders.
    rows = [r for r in rows if "{{" not in r[1] and "{{" not in r[2]
            and "key keeps first line" not in r[1]
            and "pre-orders:):TextButton|text_name" not in r[0]]
    corrections = {
        ("W:Window:promo|n:by naming a bear:TextButton|text_name", "by naming a bear"): "通过为一只熊命名",
        ("W:Window:promo|n:with a pre-order!:TextButton|text_name", "with a pre-order!"): "通过预购！",
        ("W:Window:promo|n:believers in VOIMA:TextButton|text_name", "believers in VOIMA"): "信仰 VOIMA 的人",
        ("W:Window:promo|n:Be forever remembered..:TextButton|text_name", "Be forever remembered.."): "  永远被铭记于心...",
        ("W:Window:promo|n:as one of the first:TextButton|text_name", "as one of the first"): "  作为第一批",
        ("W:Window:promo|n:#% this months rent:Button|text_name", "{0}% this months rent"): "本月房租 {0}%",
        ("W:Window:promo|n:help us create new updates!:TextButton|text_name", "help us create new updates!"): "帮助我们更新！",
        ("W:Window:promo|n:# people have already pre-ordered!:TextButton|text_name", "{0} people have already pre-ordered!"): "已有 {0} 人预购",
        ("KEYBIND:control", "control"): "额外操作",
        ("ANIMAL:characters/bear/bear:attack_messages:4", "{0} voimuhhhh"): "{0} voimu呃呃呃呃",
        ("KEYBIND:side mouse button b", "side mouse button B"): "鼠标侧键 B",
        ("KEYBIND:side mouse button b", "side mouse button b"): "鼠标侧键 B",
        ("KEYBIND:side mouse button a", "side mouse button A"): "鼠标侧键 A",
        ("W:Window:promo|n:help us create new updates!:TextButton|name", "help us create new updates!"): "帮助我们更新！",
    }
    rows = [(k, s, corrections.get((k, s), z)) for k, s, z in rows]

    # TIP/TIPD keys encode the tooltip word. The legacy import used the
    # placeholder "keybind tooltip" as source, so no real tooltip ever matched.
    fixed_tips = []
    for k, s, z in rows:
        if k.startswith("TIP:") and s in ("keybind tooltip", "keybind tooltip (single string)"):
            s = k[4:]
        elif k.startswith("TIPD:") and s == "tooltip description":
            s = k[5:]
        fixed_tips.append((k, s, z))
    rows = fixed_tips

    # Tooltips must use the game's key names, not Chinese key names. Only
    # control is intentionally rendered as ctrl.
    tip_key_names = {
        "【空格键】": "【space】",
        "【退格键】": "【backspace】",
        "【鼠标中键】": "【middle mouse button】",
        "【鼠标左键】": "【left mouse button】",
        "【鼠标右键】": "【right mouse button】",
        "【escape】": "【esc】",
        "【侧键a】": "【side mouse button a】",
        "【middle mouse button】": "【鼠标中键】",
        "【left mouse button】": "【鼠标左键】",
        "【right mouse button】": "【鼠标右键】",
        "【side mouse button a】": "【鼠标侧键 A】",
        "【side mouse button b】": "【鼠标侧键 B】",
        "【下】": "【down arrow】",
        "【上】": "【up arrow】",
        "【左】": "【left arrow】",
        "【右】": "【right arrow】",
        "【down arrow】": "【下方向键】",
        "【up arrow】": "【上方向键】",
        "【left arrow】": "【左方向键】",
        "【right arrow】": "【右方向键】",
        "【侧键b】": "【鼠标侧键 B】",
    }
    fixed_tip_keys = []
    for k, src, zh in rows:
        if k.startswith("TIP:"):
            for old, new in tip_key_names.items():
                zh = zh.replace(old, new)
        fixed_tip_keys.append((k, src, zh))
    rows = fixed_tip_keys

    # Pattern fallbacks that preserve whatever key the player has bound now.
    # Exact TIP entries still win for control -> ctrl.
    tip_patterns = []
    seen_tip_patterns = set()
    for k, src, zh in rows:
        if not k.startswith("TIP:") or "【" not in zh:
            continue
        body = None
        conn = "to"
        parts = src.split(" to ", 1)
        if len(parts) == 2:
            body = parts[1]
        else:
            parts = src.split(" for ", 1)
            if len(parts) == 2:
                body = parts[1]
                conn = "for"
        if not body or not body.strip():
            continue
        action = body.strip().lower()
        pattern_src = "{*} " + conn + " " + body
        close = zh.find("】")
        pattern_zh = "【{*}】" + zh[close + 1:] if close >= 0 and zh.startswith("【") else zh
        ident = ("TIP:" + action, pattern_src)
        if ident in seen_tip_patterns:
            continue
        seen_tip_patterns.add(ident)
        tip_patterns.append((ident[0], pattern_src, pattern_zh))
    rows.extend(tip_patterns)

    # Action-level tooltip phrases. The game splits many tooltips into a key
    # word plus a description ("1" + "to equip the 1st slot"), so the action
    # must match independently of the truncated legacy keys.
    tip_actions = [
        ("open the menu", "打开菜单"),
        ("take a photo", "拍照"),
        ("look around smoothly", "平滑环视"),
        ("delete selections with the world editor", "在世界编辑器中删除所选"),
        ("equip the 1st slot", "装备第 1 格"),
        ("equip the 2nd slot", "装备第 2 格"),
        ("equip the 3rd slot", "装备第 3 格"),
        ("equip the 4th slot", "装备第 4 格"),
        ("equip the 5th slot", "装备第 5 格"),
        ("equip the 6th slot", "装备第 6 格"),
        ("equip the 7th slot", "装备第 7 格"),
        ("equip the 8th slot", "装备第 8 格"),
        ("equip the 9th slot", "装备第 9 格"),
        ("equip the 10th slot", "装备第 10 格"),
        ("inventory", "打开背包"),
        ("extra action like with blocks", "额外操作（例如与方块互动）"),
        ("crouch when flying", "飞行时蹲下"),
        ("increase the view distance", "提高视距"),
        ("lower the view distance", "降低视距"),
        ("increase the shadow quality", "提升阴影质量"),
        ("lower the shadow quality", "降低阴影质量"),
        ("copy block", "复制方块"),
        ("primary action", "主操作"),
        ("secondary action", "副操作"),
        ("jump", "跳跃"),
        ("slump", "瘫倒"),
        ("crouch", "蹲下"),
        ("opens the chat window", "打开聊天窗口"),
        ("fixed camera", "固定视角"),
    ]
    action_entries = []
    for action, zh in tip_actions:
        for conn in ("to", "for"):
            action_entries.append(("TIP:" + action, "{*} " + conn + " " + action, "【{*}】" + zh))
            action_entries.append(("TIP:" + action, conn + " " + action, zh))
    rows.extend(action_entries)

    # Crafting-tree stack labels are bare TextureText names. The owner object is
    # not always resolved as a Card, so some labels fall back to X:TextureText.
    # Add a lower-case fallback for every simple card display name, not just slabs.
    animal_aliases = []
    for key, src, zh in list(rows):
        if key.startswith("ANIMAL:"):
            animal_aliases.append(("X:TextureText|animal", src, zh))
    rows.extend(animal_aliases)

    card_fallbacks = []
    seen_card_display = set()
    for key, src, zh in list(rows):
        if not key.startswith("CARD:") and not key.startswith("ANIMAL:"):
            continue
        if not src or any(ch in src for ch in "{\n\r\t") or len(src) > 80:
            continue
        display = src.strip().lower()
        if not display or display in seen_card_display:
            continue
        seen_card_display.add(display)
        card_fallbacks.append(("X:TextureText|" + display, display, zh))
    rows.extend(card_fallbacks)

    deduped = []
    seen_action_rows = set()
    for k, src, zh in rows:
        ident = (k, src)
        if ident in seen_action_rows:
            continue
        seen_action_rows.add(ident)
        deduped.append((k, src, zh))
    rows = deduped

    # Mod-editor palette tooltips are bare display names (e.g. "adult pine").
    # Reuse CARD/ANIMAL/BIOME translations as TIP aliases, lowercased.
    seen_tip_names = {(k, src) for k, src, _ in rows if k.startswith("TIP:")}
    aliases = []
    for k, src, zh in rows:
        if not k.startswith(("CARD:", "ANIMAL:", "BIOME:")):
            continue
        name = " ".join(src.strip().split()).lower()
        if not name or "{" in name or "\n" in name or len(name) < 2:
            continue
        ident = ("TIP:" + name, name)
        if ident in seen_tip_names:
            continue
        seen_tip_names.add(ident)
        aliases.append((ident[0], ident[1], zh))
    rows.extend(aliases)

    # Soul.RenderMusicText builds "now playing <song>" outside the Window graph.
    # Derive one prompt entry per known song so the prefix and song both translate.
    music_prompts = []
    for key, src, zh in rows:
        if not key.startswith("MUSIC:") or key.startswith("MUSIC:now playing:"):
            continue
        if "{" in src:
            continue
        song = key[len("MUSIC:"):]
        music_prompts.append(("MUSIC:now playing:" + song, "now playing " + src, "正在播放 " + src + " (" + zh + ")"))
        music_prompts.append(("MUSIC:now playing:" + song, "now playing " + src + " by {*}", "正在播放 " + src + " (" + zh + ") by {*}"))
    rows.extend(music_prompts)

    # A few legacy lines used digits that are not counts (leetspeak, music
    # numbers, repeated versus numbers).  Keep their placeholders consistent.
    placeholder_fixes = {
        ("ANIMAL:characters/bear/bear:attack_messages:5", "letz {0}v{0}"): "{0}v{0} 单挑",
        ("ANIMAL:characters/bear/bear:attack_messages:7", "{0} nozcop"): "{0} 次盲狙",
        ("MUSIC:cherry tree pt 1", "cherry tree pt {0}"): "樱桃树 第 {0} 部分",
        ("MUSIC:cherry tree pt 2", "cherry tree pt {0}"): "樱桃树 第 {0} 部分",
        ("MUSIC:5", "{0}"): "第 {0} 首",
        ("MUSIC:now playing", "now playing {*} by {*}"): "正在播放 {*}（{*}）",
    }
    rows = [(key, src, placeholder_fixes.get((key, src), zh)) for key, src, zh in rows]
    alias_fixes = {
        ("X:TextureText|animal", "letz {0}v{0}"): "{0}v{0} 单挑",
        ("X:TextureText|animal", "{0} nozcop"): "{0} 次盲狙",
    }
    rows = [(key, src, alias_fixes.get((key, src), zh)) for key, src, zh in rows]

    # These two generated patterns are shadowed by the earlier
    # "{*} opens the chat window" pattern and can never be reached.
    dead_patterns = {
        ("TIP:opens the chat window", "{*} to opens the chat window"),
        ("TIP:opens the chat window", "{*} for opens the chat window"),
    }
    rows = [(key, src, zh) for key, src, zh in rows if (key, src) not in dead_patterns]

    # Put the artist-bearing music pattern before the broad one; otherwise
    # "now playing {*}" swallows "now playing {*} by {*}" first.
    music_now = [row for row in rows if row[0] == "MUSIC:now playing"]
    if len(music_now) > 1:
        first_music = next(i for i, row in enumerate(rows) if row[0] == "MUSIC:now playing")
        music_now.sort(key=lambda row: 0 if " by " in row[1] else 1)
        rows = [row for row in rows if row[0] != "MUSIC:now playing"]
        for offset, row in enumerate(music_now):
            rows.insert(first_music + offset, row)

    # VOIMA Cards are general world objects, not a card game.
    rows = [(key, src, zh.replace("卡牌", "卡片")) for key, src, zh in rows]

    def esc(s: str) -> str:
        s = s.replace("\\n", "\n").replace("\\t", "\t")
        return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r")

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# VOIMA_ZH object-aware translation table\n")
        f.write("# object_key\\tsource_pattern\\ttranslation\n")
        f.write("# {0}, {1}... capture digit runs; {*} captures arbitrary text.\n")
        for key, src, zh in rows:
            f.write(f"{esc(key)}\t{esc(src)}\t{esc(zh)}\n")
    print(f"wrote {len(rows)} entries to {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
