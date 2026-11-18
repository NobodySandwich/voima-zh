# Known issues

## HUD action strip — resolved in 0.3.35

The bottom-left HUD key hints now translate.

The HUD is one combined TextureText built from text such as:

```text
secondary action build
primary action set grid
copy size
control toggle grid
tab more
```

HUD key hints are action-only translations. The bound key segment is preserved
verbatim, so changing a binding to z (or any other key) displays the actual key,
and primary/secondary mouse action placeholders remain icons.

Two things are required:

1. Translate only the action phrase after the game's control byte, padding it to preserve the original UTF-16 length.
2. Force a font page that contains every HUD action character before calling the original Build. The page is reserved by the synthetic entry:

```text
X:TextureText|hud_font_page
```

Verified in-game:

```text
副操作 建造
主操作 设置网格
复制 复制尺寸
ctrl 切换网格
tab 更多
```

## Crafting progress

Resolved in 0.3.34 by normalizing the dynamic key to:

```text
X:TextureText|recipes crafted
X:TextureText|recipes still locked
```
