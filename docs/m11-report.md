# M11 收口报告（0.4.0.alpha2-dev · 迭代修复批）

> **状态**：**开发中（未发行）**。本文按用户 7 条反馈逐条落地，记录交付、验收读数、偏差/追认、未决项与复现命令。
> **基线**：HEAD `26d3554`（M10-DOCS，0.4.0.alpha1 已发行）；本批**不升级版本号**（发行动作待用户确认）。
> **集成树**：`build/iter1`（干净 `release-dev` 配置 + `-DVCPKG_MANIFEST_INSTALL=OFF`），全量构建 2026-11-01 13:15:40。
> **任务真源**：`docs/heif-tile-sizing.md`（HEIF C17'）、`docs/state-machine-analysis.md`（状态机 + 自适应日志）、
> `docs/adaptive-policy.md` / `docs/format-presets.md`（DSL v1.1 / 预设）、`docs/m11-adjudications.md`（本批裁定）、
> `docs/v0.4.0-design.md` §6.1.6（C17' 修订）。

---

## 0. 用户需求 → 交付映射

| # | 用户原话（摘要） | 交付 | 证据 |
|---|---|---|---|
| 1 | 迭代优化过程写入日志 | `pipeline.cpp` 自适应段新增/增强四类 info 行：`adaptive plan` / `adaptive stage` / `adaptive round`（补 candidate、params、target、中文 `decision_reason`）/ `adaptive summary`（补 selected_params、scores 序列、中文 `reason_detail`）；样例见 §2.1 | `build/state-machine/adaptive-A|B/logs/`、`build/task2/evidence/heif-apple/logs/` |
| 2 | 1024×576 是 L几规定的？按 level 和 still 规格制定，不要固定值 | 删除 Apple 1024×576 固定基准，tile 尺寸改由「目标 Level 的 MaxLumaPs/单边上限 + still 单图应用格式 Level 上界（3GPP TS 26.140/26.141 L5.1）」推导：T1(3902×5854)+L3.1 → 768×1280（=983,040 整容量）6×5；L 自动+总是 grid → 2176×4096（=M51）2×2。新增 `planning_level_idc`/`still_level_capped`/warning，编码后逐 tile hvcC Level 回读一致 | `docs/heif-tile-sizing.md`、`pp_test_heif_capability` 366/0、`build/heif-spec/final-grid.jsonl` |
| 3 | DSL 没有可阅读性、全是 \n、语义混乱 | `adaptive.policy` 持久化改**JSON 字符串数组（一行一条，零 \n 转义）**，读入兼容旧字符串；`set` 语义勘误（同 stage 全部 set 同轮生效）；新增显式 `down`/`up` 方向与 `breakpoint` 步长断点语法；渲染/校验/错误码同步（42→45）；GUI 自适应摘要 tooltip 显示策略全文 | seed JSON（§2.2）、`docs/adaptive-policy.md` §4/§6/§11/§16 |
| 4 | 运行状态 bug 很多，详细分析整个状态机 | `docs/state-machine-analysis.md`：引擎/调度/UI/预设全状态机清单+转移表+不变量+缺陷台账 S-1…S-12；修复 S-1 迟到过程事件覆写终态、S-2 取消零效果仍显示已取消、S-3 全局预设不清格式预设来源（会误覆盖预设文件）、S-4/S-5 取消预设来源不清自适应会话态；每项补 `--ui-smoke` 探针 | `docs/state-machine-analysis.md`、`tests/ui_smoke.py` |
| 5 | 导入/导出为什么加…？下拉框宽度要和其它参数一致、不能文字截断 | 格式预设行：导入/导出按钮去省略号（「导入」「导出」逐字）；下拉宽度钉 `FieldWidth::Standard` 160px（与同页参数行一致，不再被 Flex 压到 ~115px）；按钮固定 sizeHint 不截断；combo 弹层自适应宽 + tooltip 全文 | `output_page_build.cpp`、`--ui-smoke format-preset` 探针 |
| 6 | 增加 5 条预设（HEIF Apple自适应/Default；JXL 无损归档/自适应压缩/Default） | `builtin_format_presets()` 新增 5 条（seed 名 `_builtin-heif-apple|heif-default|jxl-lossless|jxl-adaptive|jxl-default.json`），全部可读数组策略；HEIF Apple=CRF20/L3.1/10bit/slow/ssim/aq3/Main10 + CRF 降档→444+Main444_10Still 兜底；HEIF Default=运行时默认全量快照+quality +4→90；JXL 无损归档=16bit/Modular/lossless/effort7；JXL 自适应压缩=16bit/VarDCT/effort4/distance1 + 0.1 降档；JXL Default=默认全量+断点降档 | seed JSON（§2.2）、`pp_test_format_presets` 0 fail、`--dev --preset` 实跑 |
| 7 | 步长断点特殊处理 | `breakpoint <v> step <s>`（可重复）语义：开区间上界分段 + **跨断点先钳到断点值再继续** + 端点必试 + 显式方向只降不升；逐点断言 current=1→0.9…0.1（9 轮）/4→2,1,0.9…/12→10,8,6,4,2,1,0.9… | `pp_test_adaptive_exec`、`docs/adaptive-policy.md` §6.1 |

---

## 1. 交付内容（按需求展开）

### 1.1 需求 1 · 自适应迭代日志

`run_target_adaptive` 的 hooks 由执行器（`core/adaptive_exec.*`）提供数据面，pipeline 只做渲染：

- `adaptive plan`：src/out/output_index/format/backend/tech/bitdepth/pixels/policy_version/target/max_rounds/output/stages/policy_sha/base_params；
- `adaptive stage`：stage/round_before/selected/best_score（每 stage 一条）；
- `adaptive round`：在既有 round/stage/key/value/score/state/bytes/encode_ms/metric_ms/decision 上补 `params`（本轮有效参数）、`candidate`（候选临时文件路径）、`target`、`decision_reason`（中文：达标/改善/未改善/评分不可用/编码失败）；
- `adaptive summary`：在既有 rounds/target/selected/policy_sha/final_score/final_metric/overhead_ms/reason/state/error 上补 `selected_params`、`scores`（逐轮分数序列，不可用记 `-`）、`reason_detail`（中文终止原因）。

样例（HEIF Apple自适应，64×64，7 轮；原文见证据目录）：

```
adaptive plan {… target=90 max_rounds=10 stages=crf-down,chroma-444 policy_sha=65520a19b5167c96 base_params=x265:crf=20,chroma=420,preset=slow,tune=ssim,profile=2}
adaptive stage {… stage=crf-down round_before=0 selected=none}
adaptive round {… round=1 stage=crf-down key=x265:crf value=18 params=x265:crf=18 target=90 score=61.08 decision=improve decision_reason=改善：61.08}
adaptive round {… round=6 … value=8 … decision=no-improve decision_reason=…}
adaptive stage {… stage=chroma-444 round_before=6 selected=round1 best_score=61.08}
adaptive round {… round=7 stage=chroma-444 key=chroma,profile params=chroma=444,profile=Main444_10Still decision=no-improve}
adaptive summary {… rounds=7 selected=round7 selected_params=chroma=444,profile=Main444_10Still scores=61.08,… reason=stage_exhausted reason_detail=阶段耗尽：…}
```

> 64×64 夹具的 SSIMULACRA2 恒 61.08（小图度量退化，非缺陷）；真实图上分数随参数变化。

### 1.2 需求 2 · HEIF tile 尺寸（C17'）

公式（`docs/heif-tile-sizing.md` 为真源）：

- 规划 Level `Lp`：显式 Level 且 still 单图档位（MainStill / 16–21）且 Level>5.1 → `Lp=5.1` + warning；Level=自动 → `Lp=5.1`（3GPP TS 26.140 §4.5 / TS 26.141 §4.2 的 HEIF still 互操作上界，**非 H.265 强制**，ITU-T H.265 V9 一手核对已否定 still≤5.1 的 profile 级条款）；其余 `Lp=用户 Level`。
- `M=MaxLumaPs(Lp)`、`D=floor(sqrt(8M))`；在 8 对齐、`[64, min(图宽,D)]` 枚举 `tw`，`th=align8(min(图高,D,floor(M/tw)))`，取面积最大者（同面积取长宽比更接近图像者）。**不再使用 1024×576/16:9 等魔法常数**。
- 手动 tile / 1×1 降级 / rows×cols 上限 / MIAF 奇偶 warn 等既有语义全部保留；手动越界错误里的「最大可行 tile」改按新公式。

实测（矩阵）：T1+3.1 → 6×5、tile 768×1280、每 tile hvcC level_idc=93；T1+自动+总是 → 2×2、tile 2176×4096、每 tile level_idc=150（L5.0，落在 5.1 内）。

### 1.3 需求 3/6/7 · DSL 可读化、5 条预设、步长断点

- **持久化**：`adaptive.policy` 写成 JSON 字符串数组（每行一个元素）；读入兼容旧字符串（按 `\n`/`\r\n` 拆行）；`format-preset schema` 为 `oneOf [array<string>, string]`；save→load→save 字节稳定（seed 9/9 逐字节相同）。
- **语法**：`search KEY from (current|N) [down|up] to N step N [breakpoint V step S]... [method ...] [max_rounds N]`；旧语法兼容；`down` 只降不升（起点已在端点错侧 = 0 轮，不报错）；breakpoints 严格升序、开区间上界分段、跨断点先钳断点、端点必试；breakpoint 仅 linear；新增错误码 `breakpoint_syntax`/`breakpoint_order`/`breakpoint_value`（42→45）。
- **语义勘误**：`set` 阶段 = 一个 stage 内全部 set 落在同一轮参数、编一轮（旧文档「每个 set=1 轮」为错，已修正）。
- **5 条内置预设（seed 只读模板；显示名 = JSON name）**：

| 预设 | 格式 | 参数 | 策略（数组形态，逐行可读） |
|---|---|---|---|
| Apple自适应 | HEIF | x265/runtime/10bit；Main10(profile 2)；chroma 420、preset slow、tune ssim、x265:aq-mode 3、x265:crf 20、x265:level-idc 31 | CRF 20 起 down 到 8 step 2（max 10）→ 444 + profile Main444_10Still 同轮 → 结束输出最后一轮 |
| Default | HEIF | x265/runtime/8bit；运行时内省默认全量（chroma 420/lossless false/preset slow/quality 50/tu-intra-depth 2/tune ssim） | quality up 到 90 step 4（max 30）→ 结束输出最后一轮 |
| 无损归档 | JXL | libjxl/modular/16bit；lossless true、effort 7 | 无自适应 |
| 自适应压缩 | JXL | libjxl/vardct/16bit；distance 1.0、effort 4 | distance down 到 0.1 step 0.1（max 20）→ 结束输出最后一轮 |
| Default | JXL | libjxl/vardct/16bit；静态表默认全量（9 键） | distance down 到 0.1，breakpoint 1 step 2 / breakpoint 10 step 5（max 30）→ 结束输出最后一轮 |

### 1.4 需求 4 · 状态机分析与修复

见 `docs/state-machine-analysis.md`（状态清单/转移表/不变量/缺陷台账/崩溃归因/日志字段/验收读数）。已修：

- **S-1（P1）** 迟到过程事件（评分池心跳 / 取消残余）覆写文件终态 → `on_event` 增加「文件终态后非终态事件早退」门槛（`OutputDone` 仍放行补评分），探针 `run-terminal-late`。
- **S-2（P2）** 点取消但引擎汇总 cancelled=0 时仍显示「已取消」→ 结束态按 `sum.cancelled` 判定，探针 `run-cancel-noop`。
- **S-3（P1）** 全局预设载入后格式预设来源不清（点保存会用全局值覆盖旧格式预设文件）→ `apply_preset` 清会话来源并改注记，探针 `global-preset-clears-source`。
- **S-4/S-5（P2）** 选「未选择」/外部删除预设后自适应会话态残留（引擎仍按旧策略跑）→ 三处清 `adaptive_*` 会话态，探针 `unselect-clears-adaptive`。
- 已知 ui-smoke 崩溃 = `build/release-dev` 脏构建树假象（同机旧二进制与干净树均通过，三二进制 SHA 不同）——不修产品代码，门禁统一用干净树。

### 1.5 需求 5 · 格式预设行 UI

- 导入/导出按钮文案去省略号（「导入」「导出」；tooltip 说明打开文件对话框）；
- 下拉改 `FieldWidth::Standard`（160px，= 同页参数行字段宽；旧 Flex 被行尾 stretch 压到 ~115px 且把按钮挤出卡片）；
- 按钮固定 sizeHint 宽（不截断）+ 行尾 stretch 吸收富余；combo 弹层按最长项自适应宽 + tooltip 全文（长预设名看不全时悬浮可见）；
- `--ui-smoke` 新增 `preset-geom`/`preset-tip` 断言（同父兄弟 geometry、无重叠、按钮不截断、末钮在行内）。

---

## 2. 验收读数

> 最终集成树 `build/iter1`（干净 `release-dev` 配置）。最终门禁 exe 指纹见 §2.1；命令见 §5。
> **未跑的写「未跑」，如实。**

### 2.1 最终二进制指纹

| 项 | 值 |
|---|---|
| `build/iter1/photopipeline.exe` | **2026-11-01 14:10:48 / 5,199,872 B / SHA256 `13A029D204BCC9E15B132105B87FBFB5951D21B1CE7F1E4EE93231B960A4E467`**（tidy 修 7 条新告警后的最终重建） |
| `--version` | `PhotoPipeline 0.4.0.alpha1`（单行；版本单源未动） |

### 2.2 单测 / 金样 / 回归

| 门禁 | 命令 | 结果 |
|---|---|---|
| 单测 | `ctest --test-dir build/iter1 --output-on-failure` | **48/48 通过**（79.67 s；含 `ui_smoke` 57 s 级） |
| 金样 | `python tests/golden/smoke.py build/iter1` | **`SMOKE total=20 pass=20 fail=0`** |
| 回归 | `python tools/regression.py build/iter1` | **zero diff（`normalized 2567 lines vs baseline 2567 lines`）**（默认产物零变化：本批未改默认路径/默认参数） |
| 版本单源 | `python tools/ci-versions-check.py` | **OK (10 keys, expect=0)** |

### 2.3 UI 门禁（`--ui-smoke` / `tests/ui_smoke.py`）

| 项 | 结果 |
|---|---|
| 冻结末行 | **`UI-SMOKE OK shots=12 pages=3`**（exit 0） |
| 格式预设行 | `format-preset: checked=27 bad=0 row=1 first=1 empty=1 prod=1 idle=1 collect=1 roundtrip=1 note=1 normalize=1 invalid-save=1 bytes=1 broken=1 kind=1 adaptive=1 switch=1 nopolicy=1 policy-err=1 builtin=1 builtin-idem=1 builtin-readonly=1 builtin-saveas=1 global-adaptive=1 adaptive-gate=1 preset-geom=1 preset-tip=1 clears-source=1 unselect-adaptive=1` |
| 运行状态机探针 | `run-terminal-late: ok=1 g-terminal="已完成" g-late="已完成" o-terminal="已完成" o-late="已完成"`；`run-cancel-noop: ok=1 hits=1 pill="已完成" cancel-visible=0`；`run-rerun-clean: ok=1 first-score="92.31" … rerun-score="" rerun-time="—" rerun-size="—" rerun-pill="排队"`；`run-restart: ok=1 cancelled-pill="已取消" restarted-pill="进行中" …`；`run-geom: checked=26 bad=0` |
| `tests/ui_smoke.py` | **`UI-SMOKE pass`**（exit 0；唯一一次失败为已知偶发 `run-guard r%d/not-running`：与 `ci-tidy` 12 线程并发抢 CPU 时 120ms 重入窗口抖动，单独重跑即过——该探针在 0.4.0.alpha1 发行报告 §4 已登记过同一偶发；本轮不修改探针） |
| 其它组 | `layout-audit 1128/0`、`m8-params 104/0`、`format-card 81/0`、`param-enable-geom 8/0`、`output-geom 32/0` 全绿 |

### 2.4 格式 / 静态分析

| 门禁 | 结果 |
|---|---|
| clang-format（钉版 23.1.1，`bash tools/ci-format.sh`） | **`check 233 files → violations=0` / `ci-format: OK`** |
| clang-tidy 棘轮（`ci-tidy.py build/iter1 --jobs 12`） | 首轮 **269 vs baseline 262 → 7 条新告警**（`adaptive_exec.cpp` 6：easily-swappable×4 + inefficient-string-concat×1 + build_ladder 同类×2；`format_preset_io.cpp` 1：inefficient-string-concat）。**已逐条修掉**（3 处 NOLINTNEXTLINE 注明语义、`crossing_breakpoint` 改 `std::pair` 形参消掉同类相邻、两处 `+=` 替代临时串），复跑：**`TIDY total=262 baseline=262 files=76 violations=0`**（184 s，未 `--update`） |

### 2.5 HEIF C17'（需求 2）验收

| 项 | 结果 |
|---|---|
| `pp_test_heif_capability` | **366 checks / 0 failed**（旧 223；全部按 C17' 重写） |
| 矩阵 `--selftest` | **SELFTEST RESULT: OK** |
| 矩阵 `final-grid` + `final-gridtile`（**最终 exe 干净单轮**，31 例） | **pass=24 / fail=0 / expected-fail=7 / unexpected=0**（`final-grid` 20 = pass 16 + expected-fail 4；`final-gridtile` 11 = pass 8 + expected-fail 3；257.0 s。证据 `build/m11-gates/heif-final.jsonl/.md`；早前 6–8 例环境 fail 全部为编码期间 exe 被并行重链接/占用，本次干净窗口清零） |
| 真实产物结构（T1 3902×5854） | 显式 L3.1 → grid **6×5**、tile ispe **768×1280**、每 tile hvcC **level_idc=93 (L3.1)**、tile 面积恰 **983,040 = M31**；L 自动+总是 grid → **2×2**、tile **2176×4096**（=M51）、每 tile level_idc=150 (L5.0)；small+L3.0 → 960×576 2×1、level 90；small+L1.0 → 256×144 4×4、level 30 |
| 默认行为零变化 | `--dev` 默认 HEIF（Level 自动+grid 自动）→ 717 B、**无 grid box**、hvcC idc=3/level 30/420/8bit 单图 |

### 2.6 自适应 DSL / 预设（需求 3/6/7）验收

| 项 | 结果 |
|---|---|
| `pp_test_adaptive_policy` / `_exec` / `_format_presets` / `_presets` | 全部 **0 failures**（断点候选逐点、0 轮、动作级 max_rounds、hooks、数组持久化/旧字符串兼容/字节稳定、10 条内置） |
| seed 可读性 | 10 条 seed JSON 的 `adaptive.policy` 全为**字符串数组**、`\n` 转义出现 **0** 次；显示名取 JSON `name`；旧 5 条文件名/内容逐字不变；第二次 seed `written=0 skipped=10 failed=0` |
| save→load→save | **9/9 逐字节相同**（jpeg 一条被测试故意改坏，排除） |
| 真机 E2E（T1 3902×5854，**最终 exe 复跑**） | **HEIF Apple自适应**：grid 6×5 / tile 768×1280 / L3.1（每 tile level 93）；round1 `x265:crf=18` → score **90.66 ≥ 90 met**（27.0 s，8,947,641 B；`DepthDowngrade` warning 为 T1 16bit→10bit 预期）；**JXL Default**：distance 0.9/0.8/0.7/0.6/0.5 → scores **87.29 / 88.05 / 88.72 / 89.45 / 90.07**，第 5 轮 met（30.7 s，7,283,511 B）——断点默认段（≤1 用 0.1）与「只降不升」在真实图上成立。证据 `build/m11-gates/e2e-t1-heif|t1-jxl/logs/`、侧车 + 产物 |
| 小图 E2E | HEIF Apple（64×64）7 轮 CRF 20→8 + 444+Main444_10Still 兜底、`stage_exhausted`；JXL Default（64×64）第 1 轮 met。64×64 下 SSIMULACRA2 退化（61.08 / 96.99 恒定）——度量特性非缺陷，真实图见上行 |

### 2.7 状态机修复（需求 4）验收

见 §2.3 的探针读数；缺陷台账 S-1…S-13、根因、修法与「已登记未修」见 `docs/state-machine-analysis.md`。
`build/release-dev` 的 ui-smoke 崩溃 = 脏构建树假象（干净树 + 旧发行二进制均通过；三二进制同尺寸不同 SHA）——不修产品代码。

### 2.8 需求 1 日志验收

`--dev` 真实运行的四类行原文见 §1.1 与 `docs/state-machine-analysis.md` §8.3；`adaptive plan/stage/round/summary`
均落日志文件与 GUI 日志面板（同一 `run-*.log` 尾 4 行）。

### 2.9 待补/未跑（如实）

- 矩阵**全量 1102 例**未跑（只 selftest + 两个子集 31 例；`final-grid`/`final-gridtile` 已干净单轮 0 unexpected）。
- TSan 未跑（本机无运行面；CI nightly tsan 自 #28 起 build 红，属 CI 债务）。
- 版本号/tag/Release 未做（`dist` 本机包见 §2.10；发行级新版本需先升 label）。

### 2.10 发行构建与 dist 产物（本机，开发完成后必做项）

| 项 | 读数 |
|---|---|
| release configure/build | `build/release`（`CMAKE_BUILD_TYPE=Release` / `PP_BUILD_DEV=OFF`），202 目标 **EXIT=0**；`build/release/photopipeline.exe` 2026-11-01 15:14:17 / 4,065,280 B；`--version` = **`PhotoPipeline 0.4.0.alpha1`** |
| Windows 打包 | `python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/iter1/photopipeline.exe` → **exit 0** |
| 门禁 A（PE 闭包） | 68 PE / 105 导入名 → staging 51 · apiset 17 · system 37；VC 运行时 app-local 10 DLL（msvcp140/vcruntime140 系列） |
| 门禁 B（Qt 来源） | 5 个 Qt6*.dll 与 Qt 6.11.2 工具链 sha256 逐一一致 |
| 断言 C（结构/Qt 面） | `photopipeline.exe` / `platforms/qwindows.dll` / `platforms/qoffscreen.dll` / `imageformats=4` / `tls=2` 齐备；链接面 `Qt6Core/Gui/Widgets/Network` 全随包，无清单外 Qt6*.dll |
| 断言 D（许可） | `licenses/*/copyright = 35`（≥30）+ `licenses/PhotoPipeline/LICENSE`（写盘前 + zip 内各一次） |
| 烟测 E-1 | `PhotoPipeline 0.4.0.alpha1`（exit 0） |
| 烟测 E-2 | `UI-SMOKE OK shots=12 pages=3`（offscreen，exit 0；用 `--smoke-exe build/iter1/photopipeline.exe`） |
| 烟测 E-3 | `WIN-LAUNCH OK windows=5 visible=1`（真实 qwindows 平台，exit 0） |
| 状态隔离 | staging 清除走查运行态 **22 项**（classes.json / logs×3 / presets 17）后快照回原状 |
| **产物** | `dist/PhotoPipeline-0.4.0.alpha1-win64.zip` = **35,274,496 B / sha256 `9a436c2cbe3974a7f5b547509ac9e0893c4aa8ab61b224045ddb6fb096d15e63` / 107 条目**（解压即用 `PhotoPipeline/` 顶层目录） |
| 版本说明（如实） | 版本 label **未升级**（仍 0.4.0.alpha1）：本机 zip 与已发行 CI 资产（34,173,446 B / sha256 `0876df59…`）**不同**（源码已前进 + zip 不可复现）；如需发行级新版本，应先 `PP_VERSION_LABEL .alpha2` + 全套门禁 + tag/Release。AppImage 需 Linux/CI，本机未产 |

---

## 3. 偏差与追认

本批**未改动任何 PP-FROZEN 冻结接口**；全部改动属「加性 / 非冻结面 / 消费端行为修正」，逐条登记见
[`docs/m11-adjudications.md`](m11-adjudications.md)：

- **A1 持久化格式变更**：`adaptive.policy` 字符串 → JSON 数组（读兼容旧字符串，schema oneOf）。
- **A2 HEIF C17' 语义变更**：tile 尺寸由 Level+still 应用格式上界推导，矩阵期望值按新公式重算；
  默认（Level 自动+grid 自动）产物零变化。
- **A3/A4 加性结构**：自适应 hooks/Record/Result、`FormatPresetData` 末尾字段、内置 seed stem。
- **A5 UI 文案/几何修订**（去省略号/160px 下拉/不截断）；**A6 状态机消费端行为修正**（S-1…S-5）。
- **A7 文档勘误**（set 语义）；**A8 DSL 语法加性 + 错误码 42→45**；**A9 目录文案走生成器单源**；
  **A10 既有 P3 已知项登记不修**；**A11 不升版本/不重播种基线**；**A12 脏构建树崩溃假象不改码**。
- 实现期偏差（如实）：① `final-grid` 首跑的 6–8 例 fail 为并行重链接导致的 `exe in use` 环境原因，
  最终干净窗口单轮 31 例 0 unexpected；② 64×64 夹具 SSIMULACRA2 退化（常数），因此小图 E2E 只能验流程、
  画质趋势由 T1 真实图验证（90.66 / 87.29→90.07）；③ `tests/ui_smoke.py` 一次失败为已知 `run-guard`
  120ms 重入窗口在高 CPU 负载下的偶发（0.4.0.alpha1 报告 §4 已登记同款），单独重跑即过，未改探针；
  ④ tidy 首轮 7 条新告警已全部修掉（非重播种基线）。

---

## 4. 未覆盖 / 未决（如实）

- 本批**未改版本号**、未打包、未发 Release（版本/CHANGELOG 发行动作待用户确认）。
- iPhone 对「Apple自适应」产物的接受度 **UNVERIFIED**（沿用 R-12；新 tile 尺寸 768×1280 与 Apple 参考 1024×576 不同，`docs/heif-tile-sizing.md` 已登记）。
- H.265 still 官方命名遗留：`Main10Still(16)` 可用性结论与 17/18 档「Main 4:2:2 10/12 Still Picture」官方名（H.265 V9 实测无此二名）**未在本轮修正**，登记为后续小任务（`docs/heif-tile-sizing.md` §未决）。
- SSIMULACRA2 在 64×64 夹具上退化为常数（度量本身限制），自适应小图 E2E 只能验证流程不能验证画质趋势；真实图验证见 §2。
- 评分池/进度心跳在 HEIC 深 probe 首事件等 M10 既有未决项不受本批影响。
- 状态机 S-6…S-12 按 P3 登记未修（理由见分析文档）。

---

## 5. 复现命令与证据路径

```powershell
# 干净树配置 + 全量构建
python tools/env.py run -- cmake --preset release-dev -B build/iter1 -DVCPKG_MANIFEST_INSTALL=OFF
powershell -NoProfile -ExecutionPolicy Bypass -File build/iter1/build.ps1

# 门禁
python tools/env.py run -- ctest --test-dir build/iter1 --output-on-failure
python tools/env.py run -- python tests/golden/smoke.py build/iter1
$env:QT_QPA_PLATFORM='offscreen'; python tools/env.py run -- build/iter1/photopipeline.exe --ui-smoke --inputs tests/golden/base --shots .cache/ui-m11
python tools/env.py run -- python tests/ui_smoke.py build/iter1
python tools/env.py run -- python tools/regression.py build/iter1
python tools/env.py run -- python tools/ci-tidy.py build/iter1 --jobs 12
tools\bin\clang-format-win64.exe --dry-run -Werror --style=file <files>   # 见 ci-format.sh 文件集

# HEIF 矩阵（C17' 验收）
python tools/env.py run -- python tools/heif_matrix_check.py --phase final --set final-grid --set final-gridtile --force `
    --binary build/iter1/photopipeline.exe --jsonl build/heif-spec/final-grid.jsonl --tmp-dir build/heif-spec/tmp

# 预设 E2E（seed 后的只读模板）
python tools/env.py run -- build/iter1/photopipeline.exe --dev tests/golden/base/rgb8.png `
    --preset build/iter1/presets/formats/heif/_builtin-heif-apple.json --out build/lead-e2e/heif-apple --workers 1 --log-level info
```

证据目录（gitignored）：`build/m11-gates/`（最终门禁日志：ctest/golden/regression/tidy/format/ui-smoke/ui-smoke-py、
HEIF 矩阵 `heif-final.jsonl/.md`、T1 真实图 E2E `e2e-t1-heif|t1-jxl/`）、`build/task2/evidence/`（seed 数组/字节稳定/HEIF Apple E2E）、
`build/state-machine/`（状态机探针与自适应日志样例）、`build/heif-spec/`（矩阵过程 jsonl/md/日志）。

---

## 6. M12 加性勘误指针（0.4.0 正式版发行批；历史结论不改）

> 本报告是 **M11 时点**的历史记录，正文（含「未发行」「不升级版本号」「`_builtin-` 只读」「未选择」等表述）
> **保持原样不改**；以下口径已被 0.4.0 正式版（M12 批）取代，读正文时请以本节与
> [docs/m12-adjudications.md](m12-adjudications.md) 为准：

| 本报告位置/口径 | 现行为（0.4.0） | 取代条目 |
|---|---|---|
| 头部「状态：开发中（未发行）」、§2.9「版本号/tag/Release 未做」、§4「本批未改版本号」 | **0.4.0 正式版已发布**（`PP_VERSION_LABEL` 清空 ⇒ `--version` = `PhotoPipeline 0.4.0`；tag/Release 由 Lead 执行，指纹见 [docs/0.4.0-report.md](0.4.0-report.md)） | M12-A9 |
| C++ 内置库 `builtin_format_presets()` + `_builtin-*.json` 只读模板 + 幂等 seed（§1.3、§2.6、§5 复现命令） | 已删除：出厂预设 = 仓库 **JSON 数据文件 `presets/formats/**`（11 条，单一真源）**，随包分发、首启**字节复制** seed、旧 `_builtin-*` 迁移删除（§5 的 `_builtin-heif-apple.json` 路径不再存在，改用工厂/seed 后的 `presets/formats/heif/apple-adaptive.json`） | M12-A1/A2/A3 |
| 5 条新预置（Apple自适应/Default/无损归档/自适应压缩/Default）以 `_builtin-*` 形式 seed | **内容保留**为工厂 JSON（HEIF `apple-adaptive.json`+`default.json`、JXL `lossless-archive.json`/`adaptive-compress.json`/`default.json`）；`default` 一律小写下名 | M12-A3 |
| S-4/S-5「选『未选择』/外部删除后清自适应会话态」（§1.4、§2.3） | **前提消失**：0.4.0 起无「未选择」态（格式卡自动载入 default；删除后自愈回 default），对应分支已删除；S-1/S-2/S-3 结论保留 | M12-A5 |
| §2.10 本机 dist zip（`0.4.0.alpha1` 版本串、未打包发行） | 仅历史本机读数；**0.4.0 正式版发行件与指纹见 [docs/0.4.0-report.md](0.4.0-report.md) §2.5** | M12-A9 |
| 格式预设行 UI 文案/几何（A5 结论） | **保留不变**（去省略号/160px/不截断）；新增 default 自动载入/保护/detached 态 | M12-A4/A5 |

> M11 其余结论（HEIF C17'、DSL v1.1、自适应日志、S-1/S-2/S-3、A7 文档勘误、A8 错误码、A9 生成器单源、
> A10 P3 登记、A12 脏树归因）**不受 M12 影响**，仍有效。
