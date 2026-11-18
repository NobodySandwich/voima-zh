# Verification matrix

| Surface | Evidence | Status |
|---|---|---|
| Main menu | integration trace + screenshot | 中文 |
| Controls page | real hover + drag + hover_sweep.py | 中文 |
| Settings 4 tabs | real clicks + tab tooltip | 中文 |
| Music prompt | real in-game trace, lullaby/convivium | 中文 |
| Pause menu | real ESC screenshot | 中文 |
| Backpack items | 88-point hover sweep | 中文 |
| World settings | real screenshot + coverage manifest | 中文 |
| Creative time settings | real screenshot | 中文 |
| Creative fly settings | real screenshot | 中文 |
| Crafting tree | real open/pan + 64-point sweep + coverage | 中文 |
| Mod editor palette | real hover + TIP aliases | 中文 |
| HUD action strip | Render text observed, mutation unsafe | English known gap |

## Latest strict gap report

```text
translated keys (trace): 31
unique gaps: 0
non-exempt gaps: 0
object coverage: 100.00%
```

## Automated coverage

- 27 unit tests
- 11 integration tests, 1 conditional skip for missing help button
- test_binary_is_newer_than_sources prevents stale-DLL false passes
- tests/coverage_manifest.json locks world settings, pause, craft tree, backpack items, creative panels

## HUD gap

See docs/KNOWN_ISSUES.md. The attack label is part of a combined prebuilt
TextureText; all managed text hooks and safe mutation paths have been ruled out.
