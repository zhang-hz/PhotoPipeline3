# m7r23-g8-gates.md — R2.3-G8「尾单清理」证据摘录

> 归属：M7-R2.3-G8（2026-10-13）。原始日志 = `build/m7r23-g8/`（gitignored：`00-configure.log`…
> `11-tidy-ratchet2.log`）；本文件只留**命令行 + 读数摘录 + 结论**（三段式）。四提交：
> `4c9d7db`（R-266 语义归位）/ `af7687f`（R-318/R-268/R-260 余量 + dialogkit）/
> `9b37f75`（ProbeContext 全面迁移收尾）/ `0b9a4bb`（R-253 build_ui/wire 分段）。

## ① 门禁读数（全部本机实跑，专属树 `build/m7r23-g8`，Windows 侧）

| 判据 | 命令 | 结果 |
|---|---|---|
| 配置 | `python tools/env.py run -- cmake --preset release-dev -B build/m7r23-g8 -DVCPKG_MANIFEST_INSTALL=OFF`（+ `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` 复配） | exit 0 |
| 构建 | `python tools/env.py run -- cmake --build build/m7r23-g8` | exit 0（迁移后/分段后/格式化后三轮 BUILD_OK） |
| 单测全量 | `python tools/env.py run -- ctest --test-dir build/m7r23-g8 --output-on-failure` | **100% tests passed out of 36**（48.79s，`04-ctest-full.log` 尾行逐字） |
| ui_smoke | `python tools/env.py run -- python tests/ui_smoke.py build/m7r23-g8` | **`UI-SMOKE OK shots=12 pages=3`**（冻结行逐字）+ `UI-SMOKE pass`；分项：**format-roundtrip checked=16 bad=0 x=webp y=jxl off-active=jpeg front=jpeg**（R-266 归位 + 新断言过；`05-uis.log:291` 逐字）、context-menu cards=12 checked=60 bad=0、check-all 13/0 rows=16、color-options 6/0、zero-byte-redo 7/0 outputs=26 a=598、failure-list 7/0 lines=1 |
| 金样 | `python tools/env.py run -- python tests/golden/smoke.py build/m7r23-g8` | **`SMOKE total=20 pass=20 fail=0`**（冻结行逐字，`06-golden.log`） |
| 格式 | `bash tools/ci-format.sh` | `ci-format: check 196 files … violations=0` → `OK —— 全源 0 diff`（钉版 clang-format 23.1.1） |
| tidy 棘轮 | `python tools/env.py run -- python tools/ci-tidy.py build/m7r23-g8` | **`TIDY total=262 baseline=262 files=76 violations=0 seed=0`**（153s；本包零新增告警，`11-tidy-ratchet2.log` 逐字） |
| 字面量扫描 | 迁移后 meta 族字面量扫描脚本（pp-meta-/pp_meta_/gps_/tag_/exception_/privacy_strip/mtime_sync 前缀） | **0 站点**（pp-card/pp-card-hint 4 处非 meta 族不含；`meta_card_privacy.cpp` 写侧 4 处字面量不在本包文件面，登记） |

**回归（tools/regression.py）未跑**（如实）：本包全部改动在 UI 装配/探针/注释面，`--dev` 语料
回归不经这些路径（G2 同口径先例）；波次收口如要求全量回归归收口轮统一跑。

## ② 逐条交付→读数对应（断言只增声明）

| 交付 | 提交 | 机械锚 |
|---|---|---|
| R-266 关现役语义归位（M7R23G8-1）：`toggle_format` remove_tab 后重申 front（`src/ui/page_output.cpp:157-161`），修 R-189 语义漂移；模型侧零改动 | `4c9d7db` | format-roundtrip 断言只增 1 条「R-266 关现役后现役前移首位（active == front）」，checked 15→**16**，读数归位 **off-active=jpeg front=jpeg** |
| R-318 两处未覆盖面：presets_dialog 三按钮禁用态原因 tooltip + paramform 技术条目条目级 tooltip（恢复可选清空） | `af7687f` | 双态文案与 page_output sync_preset_buttons 同款；tooltip 不在 `--ui-smoke` 渲染面（无机械探针，代码级自查 + 全绿背书） |
| R-268 界外 3 处注释改实（page_output.h:30-32 / classify_panel.h:62-64 / preview_panel.h:58-61，M5-A5 起失实的旧理由） | `af7687f` | 注释级零行为；page_output.h 为 PP-FROZEN(file)，只动注释 |
| R-260 余量措辞收敛：page_meta.cpp 头注释（清单人读本体）与 meta_probe_context.h:7-8（机读单源）互指对齐 | `af7687f`+`9b37f75` | 注释级 |
| dialogkit findChild 收口（M7R23G8-2）：`dialogkit::TitleBar` 结构末尾加性追加 `icon` 成员，restyle_title_bar 退役 `pp-dlg-title-icon` findChild 反向伸入 | `af7687f` | objectName 不变（探针/走查契约不变）；PP-FROZEN 加性 + 追认 |
| ProbeContext 全面迁移收尾（M7R23G8-3）：objname 词表补 **13** 键 + 2 辅助常量（kInfoRow0 派生 / kPpMetaPrefix 前缀；`git diff 6894200..HEAD -- src/ui/meta_probe_context.h` 实测新增常量 15 条 = 13+2，词表 111→126；值串逐字与既有 objectName 一致）+ smoke_probes 迁移 100 站点（QStringLiteral 85 + raw char* 15）+ hook 动态属性 8 处走类型化访问器 | `9b37f75` | 迁移后 meta 族字面量 **0 站点**（①表末行） |
| R-253 装配函数分段（行为零变化）：build_ui（477 行）→ 五段 + 编排；wire（349 行/~50 连接）→ 七段 + 编排；MainWindow 冻结头签名零改动 | `0b9a4bb` | 全量 ctest 36/36 + ui_smoke 冻结行 + 金样 20/20 三背书（分段后复跑） |

## ③ 结论与判据

- **断言只增不减**：R-266 修复带机械化探针钉住（format-roundtrip +1 断言，M7R23G8-4）；
  金样/夹具/冻结行零改动；tidy 棘轮 262/76 持平。
- **行为面**：R-266 = UI 组合层向契约归位的**行为修正**（M7R23G8-1）；其余各条注释级 /
  结构级零行为变化（M7R23G8-5 锚定）。
- **词表键数 13 ≠ 任务书「11」**：G1 前缀口径漏计 privacy_strip/mtime_sync 两族 4 键，实测
  补齐 13（如实报数，M7R23G8-3）。
