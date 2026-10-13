# m7r22-g4-gates —— R2.2 G4「文档终稿」复核读数与事实取证

> 包：M7-R2.2-G4（报告类提交，纯文档面）｜日期：2026-10-13｜原始日志：
> `build/m7r22-g4-{build1,uis,golden,regression}.log`（gitignored）。本文件两部分：
> ① G4 实跑的合流树复核（ui_smoke / 金样 / 回归 / 台账计数）；② 文档事实取证（grep/`wc -l`
> 级，逐条给命令与输出），供 §8 引用的 handoff 读数交叉核对。

## ① 命令行（逐字可复跑）

```bash
python tools/env.py run -- cmake --build build/m7r22-g2            # 合流 HEAD 4dee5c8 构建面确认
python tools/env.py run -- python tests/ui_smoke.py build/m7r22-g2
python tools/env.py run -- python tests/golden/smoke.py build/m7r22-g2
python tools/env.py run -- python tools/regression.py build/m7r22-g2
# 台账计数自检：python 正则逐行核 docs/m7-report.md 附录 A/B
```

## ② 读数/结果摘录（G4 实跑，2026-10-13）

- 重建：`ninja: no work to do.`（build/m7r22-g2 已是 HEAD `4dee5c8` 构建面；G1+G2+G3 合流）。
- ui_smoke（`build/m7r22-g4-uis.log`）：**`UI-SMOKE OK shots=12 pages=3`** + `UI-SMOKE pass`
  （冻结行逐字）；G1 探针行全绿：`settings-tabs-light: checked=4 bad=0 pp-tab-general
  mode=#1b1c20 est=#1b1c20 t=1.00 resid=0 pp-tab-output mode=#5d5f68 est=#5d5f68 t=1.00
  resid=0`（AA 容差机制取证行）/ `format-roundtrip: checked=15 bad=0 x=webp y=jxl
  off-active=webp front=jpeg` / `rtl-deep: checked=4 bad=0 cols=96` / `rtl-mirror: checked=18
  bad=0 stars=0 hud=0 align=0 geom=0` / `color-options: checked=6 bad=0` / `check-all:
  checked=13 bad=0 rows=16` / `context-menu: cards=12 checked=60 bad=0` / `zero-byte-redo:
  checked=7 bad=0 outputs=26 a=598` / `failure-list: checked=7 bad=0 lines=1`。
- 金样（`build/m7r22-g4-golden.log`）：**`SMOKE total=20 pass=20 fail=0`**（冻结行逐字）。
- 回归（`build/m7r22-g4-regression.log`）：**`regression: zero diff (baseline reproduced)`**
  （normalized 2448 lines vs baseline 2448，未 `--update`）。
- 台账计数自检（python 正则逐行核附录 A/B）：331 R-id 行 = 已修 312 + 登记 15 + 误识别关闭 3 +
  受约束口径闭环 1；UX-14 行 14 条全「已修」⇒ **345 行 = 已修 326 + 登记 15 + 误识别关闭 3 +
  受约束口径闭环 1**（与 F5 首核同数；R2.2 主标签零改判）。

## ③ 文档事实取证（grep/`wc -l` 级，G4 实跑）

| 命令 | 输出（摘录） | 证明的文档口径 |
|---|---|---|
| `git log --oneline origin/main..HEAD` | 5 行（`4c81bd0`…`4dee5c8`）；`git rev-parse origin/main` = `4addc51` | §8.4 残余①：格式收口提交未推送，main-full 未跑过合流树 |
| `wc -l src/mapwidget/mapwidget.cpp` | `760 src/mapwidget/mapwidget.cpp` | §8.2 B.5 收口读数（handoff 记 759，G4 复核 760） |
| `ls src/mapwidget/` | 含 tile_cache.h / map_interact.h / map_renderer.{h,cpp} / tile_fetcher.{h,cpp} / geocode_client.{h,cpp} / map_view.{h,cpp} | B.5 六步产物面 |
| `grep -n is_time_clear_key src/ui/time_tag_keys.h` | `:68 inline bool is_time_clear_key(...) { return is_time_key(k, xmp); }` + 契约注释两处 | M7R22G2-0 清除谓词 = touched 全集 |
| `grep -n done_with_errors tests/golden/SCHEMA.md` | 仅新增行 1 处（`:60`，行内注明 M7R22G3-1） | SCHEMA 加性补档 |
| `grep -n "mw_cabin\|run_row_paths" src/ui/mainwindow_impl.h` | `:872 namespace mw_cabin` / `:1008 run_row_paths` / `:1036-1040` Impl 五基类 | R-254 Impl 五舱分域、R-256 后半路径快照 |
| `grep -c qss_leading_side src/ui/theme.h`（trailing 同） | 8 | R-218 theme.h logical 侧词件在位 |
| `grep -n G1_LINE_PREFIXES tests/ui_smoke.py` | `:413 G1_LINE_PREFIXES = (` + `:536` 消费 | G1 齐备性收编 11 行 |
| `grep -n "tail-probe\|CTEST-FAIL-SUMMARY" .github/workflows/main-full.yml` | `:547/:581/:595/:616` tail-probe 锚、`:626` 首条注解 | M7R22G1-1 遥测三版三锚 |
| `ls tests/unit/test_meta_format.cpp` | 文件存在 | G2 test_meta_format 交付 |
| `gh run list` | 未执行成功：`gh` 未认证（`please run: gh auth login`） | §8.4 残余①：G4 无法查 CI，最近读数引 G1 handoff |

## ④ 结论与判据

- **合流树（G1+G2+G3）本机三判据全绿**：ui_smoke 冻结行 + G1 探针批、金样 20/20、回归
  zero diff——R2.2 三包在同一棵树上互不踩判据，冻结行逐字不动。
- **文档口径全部可溯源**：§8 引用的每条 handoff 读数在对应 `m7r22-g*-gates.md` 有摘录；
  G4 自跑读数以本文件为准。**未跑（如实）**：ci-format 全源、ci-tidy、ctest 全量、
  make_winzip、密钥扫描——归波次收口/G6 发行包（ci-format 全源 0 diff 出处 = G2 `4dee5c8`）。
- **发行状态**：待发布（条件见 CHANGELOG [0.3.3]「发行」节末条）；tag `v0.3.3` = `b1af371`
  未移动，`M7-ADJ-tag-move-2` 授权未消耗。
