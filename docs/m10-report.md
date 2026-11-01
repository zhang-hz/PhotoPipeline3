# M10 / 0.4.0 收口报告（定稿）

> **状态**：**已定稿（待发行）**。本文由 **FINAL-REPORT**（team task-27）按**最终集成树** `build/m10/final` 的实测数字定稿；
> 发行动作（版本号 / CHANGELOG / README / tag / git 提交）仍**待用户确认**，逐项见 [§6 发行前检查清单](#6-发行前检查清单)。
> **体例**：AGENTS.md §6.2 五段式 —— ①交付内容 ②验收结论 ③偏差与追认 ④未覆盖/未决 ⑤复现命令与证据路径（+ §6 检查清单）。
> **契约真源**：[`docs/v0.4.0-design.md`](v0.4.0-design.md)（C1–C25）；任务卡 [`docs/m10-workflow.md`](m10-workflow.md)；
> 冻结面与偏差 [`docs/m10-adjudications.md`](m10-adjudications.md)；执行主体/事故台账 [`docs/m10-execution-log.md`](m10-execution-log.md)。
> **判读口径（重要）**：§2 的读数**一律来自最终集成树**（FINAL-GATE / FINAL-TIDY 在同一棵树 `build/m10/final` 上实测）；
> §1 的任务级读数来自**各任务自己的构建树**，只用于说明交付内容，逐条标注来源。
> **证据留存**：报告原件在 `build/m10/<task>/report.md`（gitignored）。M10 期间两次磁盘告急，删除了 13 个**可重建**的
> 任务构建树，报告先被抢救到 `build/m10/_evidence/<task>__report.md`（16 份）⇒ 这些任务以 `_evidence` 副本为准；
> 仍在树内的原件：`engine-scorefix` / `w3-core` / `w3-ui` / `w3-exec` / `w3-preset` / `w3-docs` / `final-gate` / `tidy-fix`。
> **本文的数字纪律**：每个数字都能在 §5 列出的日志/报告里逐字找到；没跑过的写"未跑"，没测过的写"未测"。

---

## 0. 状态总览

### 0.1 W1 · 性能与运行页（需求 1/4/5/6/9）

| 任务 | 交付（一句话） | 状态 | 报告 / 证据 |
|---|---|---|---|
| W1-T1 | 评分预算拆分：文件 `need` 恢复 2×frame、评分独立配额、`stagger_ms` 默认 0 | ✅ 完成 | `_evidence/w1-engine-a__report.md` |
| W1-T2 | 评分内 lcms 按带 + color 日志限流（并如实推翻设计归因，见 §3.2-1） | ✅ 完成 | 同上 §3 |
| W1-T2b | exiv2「无手术」快路径（无变更 ⇒ 跳过整文件重写） | ✅ 完成 | 同上 §4 |
| W1-T2c | 评分色彩归一快速内核（matrix-shaper + ≥90 精确重算兜底） | ✅ 完成 | `_evidence/w1-fastcolor__report.md` |
| W1-T3 | 双重 probe 消除（`FileEntry::probe` 加性 + 复用） | ✅ 完成 | `_evidence/w1-engine-a__report.md` §4 |
| W1-T4 | SSIMULACRA2 流式化（峰值内存 + 数值 oracle） | ✅ 完成 | `_evidence/w1-metric__report.md` |
| W1-T5 | 独立评分池 + 异步评分（`ScorePool`） | ✅ 完成 | `_evidence/w1-engine-b__report.md` |
| W1-T6 | 同文件多格式并行编码（飞行预算 token） | ✅ 完成 | 同上 |
| W1-T7 | 引擎逐输出事件（`OutputDone` / `FileEvent::output` / 心跳） | ✅ 完成 | 同上 |
| W1-T8 / T9a | 运行页布局（`RunTableColumns`、删速度列、ElideMiddle）+ 状态/刷新 | ✅ 完成 | `_evidence/w1-ui__report.md`、`_evidence/w1-ui__report-status.md` |
| W1-T9b | 运行页消费 `OutputDone` + 评分池接线（A10 加性） | ✅ 完成 | `_evidence/ui-t9b__report.md` |
| W1-T10 | spec §7.3 / mockup run.html / 探针换锚（布局部分） | ✅ 完成 | `_evidence/w1-ui__report.md` |
| W1-T11 | 机械探针工具 `perf_smoke.py` + `pp_progress_audit.py` | ✅ 完成 | `_evidence/w1-tools__report.md` |
| W1-T12 | W1 集成门禁、W1 报告增量（回归 zero diff / TSan / 全量门禁） | **部分**：门禁由 FINAL-GATE 统一重跑；**TSan 未跑**；报告增量并入本文 | `build/m10/final-gate/report.md`、本文 §2/§4 |
| W1-SCOREFIX | 评分池 per-run 取消/复位、提交原因三分类、所有权闭环、可测 drain | ✅ 完成 | `build/m10/engine-scorefix/report.md` |

### 0.2 W2 · 参数面（需求 2/3/7/8/12）

| 任务 | 交付（一句话） | 状态 | 报告 / 证据 |
|---|---|---|---|
| W2-T1 | HEIF grid/tile 配置面（C16/C17）+ Level 上移核心区 | ✅ 完成 | `_evidence/w2-heif-grid__report.md` |
| W2-T2 | HEVC profile 16–21（still 变体）+ SPS PTL 断言 + 下拉 C19 | ✅ 完成 | `_evidence/w2-profile__report.md` |
| W2-T3 | tune 全量（HEIF 7 项 / AVIF-SVT 5 值 / libaom 通道） | ✅ 完成 | `_evidence/w2-tune-rc__report.md` |
| W2-T4 | RC 数值行上移核心区 + 动态域 + QP+Level 预检 | ✅ 完成 | 同上 |
| W2-T5 | JXL 治理（43→31 键、三态、`epf` 默认、回归归因） | ✅ 完成（重播种待 D-9） | `_evidence/w2-jxl__report.md` |
| W2-T6 | catalog/矩阵/单测/文档收口 | **部分**：计数/文档面由 AUDIT-1 + W3-T6 + 本文收口；**矩阵全量未跑**（仅四子集 233 例） | `_evidence/audit__report.md`、本文 §2 |
| W2-T7 | W2 集成、矩阵全量跑批、重播种证据、0.4.0.alpha2（可选） | **部分**：集成门禁由 FINAL-GATE 重跑；全量矩阵/重播种未做（D-9）；alpha2 未发 | `build/m10/final-gate/report.md` |
| AUDIT-1 | W2 脚本层审计 + 组合树复核（`gen_param_catalog.py` 两处修复） | ✅ 完成 | `_evidence/audit__report.md` |
| UI-ADAPT | JXL 治理后的 UI 面适配（旧探针换锚到 `dots`、`epf=-1` 几何断言、effort 1–11） | ✅ 完成 | `_evidence/ui-adapt__report.md` |

### 0.3 W3 · 格式预设 + 自适应 + 文档（需求 10/11）

| 任务 | 交付（一句话） | 状态 | 报告 / 证据 |
|---|---|---|---|
| W3-T1 | 格式预设 core + io（`format_preset.*`、`format_preset_io.*`、schema） | ✅ 完成 | `build/m10/w3-core/report.md` §1 |
| W3-T3 | 自适应 core（DSL/AST/校验/42 错误码/5 条默认库/schema） | ✅ 完成 | 同上 §2 |
| W3-T2 | PageOutput 格式预设行 + 自适应只读摘要/开关 + MainWindow 接线 | ✅ 完成 | `build/m10/w3-ui/report.md` |
| W3-T4 | 自适应执行器 + pipeline 集成（候选文件/轮循环/进度/侧车/`--adaptive-policy`） | ✅ 完成 | `build/m10/w3-exec/report.md` |
| W3-T5 | 预设↔自适应集成 + 内置预设库（**运行时幂等 seed**，见 D-8） | ✅ 完成（`share/presets` 出厂文件按 D-8 取消） | `build/m10/w3-preset/report.md` |
| W3-T6 | 文档：`adaptive-policy.md`（新）、`format-presets.md`（新）、本文件骨架、`param-catalog.md` 校核 | ✅ 完成（文档面） | `build/m10/w3-docs/report.md` |
| W3-T7 | W3 集成、全门禁、0.4.0 收口、发行准备 | ✅ 完成（门禁/报告）；**发行待用户确认** | `build/m10/final-gate/report.md`、`build/m10/tidy-fix/report.md`、本文 |

### 0.4 收口任务（本次三连）

| 任务 | 交付 | 状态 | 报告 / 证据 |
|---|---|---|---|
| FINAL-GATE（task-25） | A14 补丁 + 干净树构建 + 格式 + 全量门禁 + 性能/事件 + HEIF 矩阵 + 回归归因 + `pipeline.cpp` 复核 + 只读核对 | ✅ 完成（tidy 项转 FINAL-TIDY） | `build/m10/final-gate/report.md` |
| FINAL-TIDY（task-26） | `adaptive.cpp` 解析失败修复 + 132 条告警收敛（48 代码 + 84 NOLINT）+ 重建 + 五道门禁复跑 | ✅ 完成 | `build/m10/tidy-fix/report.md` |
| FINAL-REPORT（task-27） | 按最终数字定稿本文 + `m10-workflow.md` 状态/门禁记录 + `param-catalog.md` 3 处补登记 + 两份用户手册漂移修正 + 发行前检查清单 | ✅ 完成 | `build/m10/final-report/report.md` |

---

## 1. 交付内容（按波次）

### 1.1 W1 · 性能与并行（C1–C15）

- **预算/调度（T1、A17）**：`stage_budget` 移除评分 `+160 B/px`，文件 `need` 恢复 `2×frame`；评分走独立
  `ScoreQuota`（容量 = min(文件预算/2, 4 GiB)）；`AppSettings::stagger_ms` 默认 150 → 0。
  实测（任务树）：48 MP PNG + `--score` 由 `EXIT=1`（`need 8,832,000,000 B > capacity 8,589,934,592 B`）变为
  `EXIT=0`（产物 282,405 B、`total_ms=1294.6`）；`need_bytes` 8.832 GB → **1.152 GB（=2×frame）**；
  24 MP 评分批并发推算 1 → 14。最终树复测见 §2.2（P4-4：`EXIT 0`、`out_bytes=282,405`、score=100）。
- **评分色彩归一（T2/T2c）**：lcms 由逐行改**按带**并做日志限流（数值等价 `max_err=0`，1485 checks）；
  实测按带化**没有可测提速**（band/row ≈ 0.97）⇒ 设计"逐行 lcms 白扔 7.45 s"的归因**不成立**（见 §3.2-1）；
  真正的提速来自 T2c 的 matrix-shaper 快速内核：色彩归一 **8.49 s → 0.42 s（20×）**，SSIMULACRA2 端到端
  `|Δscore| ≤ 0.01`，并加"快速内核分数 ≥90 且 <100 ⇒ 精确 lcms 重算"兜底。
- **元数据（T2b，C9）**：无 EXIF/XMP 变更 / 无隐私剥除 / 无 mtime 同步 ⇒ 跳过整文件拷贝重写（命中日志
  `metadata: no surgery, skipped rewrite`）；有手术时 R-002 原子写逐字不变（D-2 已把可省范围收窄到"源无元数据"子集）。
- **probe（T3，C8）**：probe 结果一次产出、`FileEntry::probe` 复用；22.8 MP HEIC 输入首个
  `Probing → Decoding` **2,902.9 ms → 1,447.8 ms（−50.1%）**，`decode_ms` 不变。
- **指标（T4，C2）**：SSIMULACRA2 流式/分块；23 MP 单次评分 **1.572 s**（68.8 ms/MP）、峰值额外内存
  **165.1 MB ≈ 7.6 B/px**（旧实现 148.7 B/px 的 5.1%）；与 HEAD 旧实现 oracle 逐组 `|Δ| = 0.000000000`。
- **评分池/事件/多格式并行（T5/T6/T7，C3/C13/C14/C15/C5）**：进程级 `ScorePool`（K = min(4, max(1, hw/4))、
  独立配额、看门狗心跳、`wait_idle`/`cancel_pending`）；每输出 `Encode → MetaWrite → OutputDone`
  （成功/跳过/失败/取消四路，11/11 早于文件终态、0 违规）；心跳使进度静默 `max_gap 181.6–425.0 ms`；
  1 文件 4 格式 22.8 MP 并行 **12.57 s**（串行 18.56 s）且四格式产物 SHA256 逐字节一致（任务树读数；
  最终树复测 12,719 / 16,690 ms（1.31×），见 §2.2「多格式并行」）。
- **运行页（T8/T9a/T9b/T10）**：`RunTableColumns` 单源列宽、删速度列、名称列 `ElideMiddle` + 全路径 tooltip、
  压缩比迁入大小列 tooltip；组行聚合态与逐输出 `OutputDone` 消费（评分列第二条事件同帧显示、终态不清空）；
  A10 `RunPlan::quality_score` 加性；spec §7.3 与 `mockups-v2/run.html` 同步；`ui_smoke.py` 前缀只增。
- **探针工具（T11）**：`tools/perf_smoke.py`（11 探针 + `--selftest` 16 例 + `--exec`）、
  `tools/pp_progress_audit.py`（9 项检查 + `--selftest` 10 例）；dev-only，不进 ctest。
- **SCOREFIX**：评分池取消改 **per-run 代次**（`begin_run/reset_for_run/current_run/cancel_run/wait_run_idle`）；
  `submit_ex()` 三分类（Accepted / PoolStopping / RunCancelled / QuotaExceeded / InvalidTask）并写准确中文 note；
  事件出口改 `shared_ptr<EventSink>`、取消谓词改 `shared_ptr<ScoreRun>`（消除 `Impl*` 悬垂）；
  GUI 收尾语义下沉为可单测的 `ScorePool::drain(timeout, grace, pump, slice)`。
  该任务曾用文本替换损坏 `src/core/pipeline.cpp` 并重放恢复 —— 恢复合理性由 FINAL-GATE 专项复核（§2 表格 #15）。

### 1.2 W2 · 参数面（C16–C22）

- **HEIF grid/tile（T1，C16/C17）**：新目录行 `heif_grid`（0 关闭 / 1 自动 / 2 总是，默认自动）、
  `heif_tile_w/h`（0=自动、64–8192、8 向下对齐）；`x265:level-idc` 上移核心区；`__heif_grid` 保留为兼容读别名。
  自动语义：Level=自动 + 常规尺寸 ⇒ 单图（默认产物零变化，3/3 SHA 逐字节相同）；显式 Level 装不下整图 ⇒
  按 Apple 基准 1024×576 自动分块；rows×cols==1 ⇒ 降级单图；MIAF 奇偶只 warn。单测 **223 checks / 0 failed**；
  矩阵 `final-gridtile` 11 例 `pass=8 / expected-fail=3 / unexpected=0`；目录行 HEIF 40→43（全局 117）。
- **HEVC profile（T2，C18/C19）**：枚举追加 16–21（编号不重排，`kHeifProfileCount=22`）；新增
  `heif_parse_sps_ptl`（从 hvcC 内嵌 SPS 去 `00 00 03` 后解析 48 位 constraint + DPB）；编码后对 16–21 断言
  still 语义；**16（Main10Still）实测不可用**（SPS DPB 4/2 ≠ 0/0）⇒ UI 置灰 + 原因；修正 3 个误导标签（7/10/13）；
  档位下拉 19 项、按 value 定位遍历；C19 弹出层自适应宽度 + item tooltip 全文。
- **tune / RC（T3/T4，C20/C21）**：HEIF tune **7 项** = 4 原生（psnr/ssim/grain/fastdecode）+ `animation`
  （仿真：插件 ssim + 注入 `x265:psy-rd=0.4`/`aq-strength=0.4`/`deblock=1:1`，用户显式覆盖优先）+ 2 置灰
  （`zerolatency`/`vmaf`）；AVIF-libaom 扩 `ssimulacra2`/`vmaf_saliency_map`（内部 `aom:tune` 通道，不新增目录行）+
  3 个置灰；AVIF-SVT 5 值中文标签 + `iq/ms-ssim` BRC 提示；RC 数值行（quality/crf/qp、`aom:cq-level`、SVT `qp`）
  重建到核心区紧随模式行、动态域 8bit `0..51` / 10bit `−12..51` / 12bit `−24..51`、QP+显式 Level 中文错误、
  CRF+Level 告警。单测 `test_heif_tune` **142 checks**；矩阵 `final-tune` 22 例 `0 unexpected`；`m8-params checked=104 bad=0`。
- **JXL 治理（T5，C22/A18）**：43 → **31 键**（删 12 个结构性 no-op）；`epf` 默认 2 → **−1**（跟随库）；
  `gaborish`/`dots`/`patches` 改**三态**（未设置 = 不下发）；`codestream_level` 只读；`brotli_effort` 修作用域；
  `color_transform` 收窄；`channel_colors_*` Int→Float；`effort=11` 自动 `AllowExpertOptions`。
  回归：JXL 有损产物 23/23 变化（**有意**、已归因，A15/A18），非 JXL 产物 0 diff；金样 20/20（JXL 用例全无损）。
- **脚本层审计（AUDIT-1）**：`gen_param_catalog.py` 修 `BACKENDS` jxl 16→14 与 `--no-anchors` 安全默认；
  三检查 exit 0（`rows=117 x265_anchors=239 aom_anchors=192`）；`anchors.inc` 7715 B 与 HEAD 逐字节相同；
  组合树 build exit 0（439 步）、定向 9/9、全量除 `ui_smoke` 42/42、金样 20/20。

### 1.3 W3 · 格式预设 + 自适应（C23/C24/C25）

- **格式预设 core+io（T1）**：`src/core/format_preset.{h,cpp}`（纯 C++）+ `src/ui/format_preset_io.{h,cpp}`
  （Qt JSON/原子写/列举/导入导出）+ `docs/schemas/format-preset.v1.schema.json`；`core/presets.h` **零改动**（A8）。
  实测规则：`kind` 必填、`version>1` 显式报错、未知根字段忽略+日志、`params` 的 `__` 键不落盘由 `reserved` 重建、
  空 `adaptive` 不写字段、保存→载入→保存**字节稳定**（`a/` 与 `b/` 两目录 jpeg 预设同 SHA256、630 B）、坏文件仍列出并标记、删除走绝对路径守卫。
- **自适应 core（T3）**：`src/core/adaptive.{h,cpp}`（纯 C++）+ `docs/schemas/adaptive-policy.v1.schema.json`；
  行式 DSL 解析/渲染（幂等）/校验，**42 条错误码**，search 白名单 **6 键**，set 允许面 = 目录键 − **40 键禁止表**
  + 类别键 + 伪参数 `profile`；`output best` 校验期显式拒绝、`tech` 默认禁止；**5 条出厂默认策略**
  （HEIF/AVIF/JXL/WebP/JPEG）。单测 `test_adaptive_policy` 逐条钉住（含 §9.2 三例、默认库逐字段、禁止表全表）。
- **PageOutput UI（T2，A9/A11）**：每个格式页签顶部「格式预设」行（下拉 + 保存/另存…/删除/导入…/导出…，选中即载入）
  + 只读自适应面（摘要「目标 X / 最多 N 轮 / K 个阶段」+「启用自适应」开关，**无文本编辑器**）；
  `ui/page_output.h` 仅末尾加性（7 信号 + 8 方法 + 一个 `public:` 续段）；objectName 只增（`pp-format-preset-*` /
  `pp-adaptive-*`）；`--ui-smoke` 新增 16 组断言 `bad=0`、冻结末行逐字不变；离屏 `ui_smoke.py` 前缀只增。
- **自适应执行器（T4，A3/A13/A14）**：`src/core/adaptive_exec.{h,cpp}`（可注入 encode/score/cancel/hooks 的纯执行器；
  linear/bisect/exp/greedy、达标即停、max_rounds/plateau/阶段耗尽、每轮参数校验、候选三件套、`output last`）；
  pipeline 集成：`OutputFormatSpec::adaptive_policy`（末尾加性）→ `PlannedTarget` → `run_target` 空串分派；
  每轮编码到同目录候选（**不写元数据**）→ **同步**评分 → 选定 `commit_temp_file` → 既有 metawrite/mtime；
  每轮 `ProgressInfo.adaptive_round/adaptive_max` + `scoring=true` 心跳；`adaptive round` / `adaptive summary(policy_sha 前 16)`
  日志；侧车加性 `adaptive` 对象（`tests/golden/SCHEMA.md` +74 行、旧断言零改动）；
  `--dev --adaptive-policy FILE` 加性 + `--preset` 也接受格式预设。单测 `test_adaptive_exec` **22 例全绿**。
- **预设↔自适应集成 + 内置库（T5，A20）**：全局预设 v2 `outputs[i].adaptive = {enabled, policy}` 读写
  （空 adaptive **不写**字段 ⇒ 旧文件逐字节不变；结构错误显式报错）；GUI `spec_of` 采集 / `config_base()` 按
  `adaptive_enabled` **门控下发** / `apply_state_to_form` 恢复；内置预设 = **代码内置 5 条**（HEIF/AVIF/JXL/WebP/JPEG）
  + **运行时幂等 seed**（`<data_dir>/presets/formats/<fmt>/_builtin-<fmt>.json`，只补不存在那条；`_builtin-` 只读：
  保存/删除拒绝、另存可用）。`--ui-smoke` 新增 6 组（`builtin` / `builtin-idem` / `builtin-readonly` /
  `builtin-saveas` / `global-adaptive` / `adaptive-gate`），format-preset 组 `checked=23 bad=0`。
- **文档（T6 + FINAL-REPORT）**：[`adaptive-policy.md`](adaptive-policy.md)（写法/示例/语义/允许禁止键/42 错误码/
  默认库/JSON 承载/成本模型）、[`format-presets.md`](format-presets.md)（存储/字段/保存加载导入导出/损坏与版本/
  与全局预设关系/内置预设 seed/与自适应关系）、本文件定稿、`docs/param-catalog.md` 三处登记缺口补齐（§3.5）。
- **dev E2E（最终树，全部可复算）**：
  - **格式预设四格式**（`build/m10/w3-preset/report.md` §7.1，内置预设携带 adaptive）：jpeg 侧车
    `reason=met/rounds=1/value=92/score=100`（产物 551 B `EA62C702…`）、webp `value=87`（226 B `0BE41D8F…`）、
    heif `value=87`（770 B `FF58B5D6…`，可再解码）、avif `value=87/score=99.447`（661 B `DB689ADA…`，可再解码）；
    同参数 `adaptive.enabled=false` 的产物 = 非自适应路径（`FF331868…`）。
  - **全局预设 adaptive + A14 补丁**（`build/m10/final-gate/report.md` §A）：补丁 `src/dev_cli.cpp` 加性 3 行后，
    `--dev --preset <全局预设含 adaptive>` 侧车出现 `adaptive`（policy_version=1/target=90/max_rounds=3/output=last/
    rounds=1/value 82/score 100/reason met/policy_sha `ee6e3f7e…`，产物 `bd6a3f54…`）；**旧预设（无 adaptive）路径
    与补丁前二进制产物逐字节相同**（`d541aafa…`）。
  - 详见 §2 表格 #14 与 §3.4（A14 属"缺口的补齐"，不是新增能力）。

---

## 2. 验收结论（逐命令逐结果 · 最终集成树 `build/m10/final`）

> 所有命令经 `python tools/env.py run --` 包装（下文省略前缀）。二进制：`build/m10/final/photopipeline.exe`
> = 5,088,768 B / SHA256 `E25F4F2923531D02A8054D5A40E3338CA417814A3AEA96FBC6D7E477B49F4731`（tidy 修复后重建；tidy 前 `A2675B61…`）。

| # | 门禁 | 命令 | 最终树结果 | 证据 |
|---|---|---|---|---|
| 1 | 干净树 configure | `cmake --preset release-dev -B build/m10/final -DVCPKG_MANIFEST_INSTALL=OFF` | **EXIT=0** | `build/m10/final-build.log` |
| 2 | 干净树 build | `cmake --build build/m10/final -j 8` | **EXIT=0（481 步）**，尾部 `[480/481] Linking CXX executable photopipeline.exe` | `build/m10/final-build.log` |
| 3 | tidy 修复后重建 | `cmake --build build/m10/final -j 8` | **EXIT=0（157 步，增量）** | `build/m10/tidy-fix/build.log` |
| 4 | 全量单测 | `ctest --test-dir build/m10/final --output-on-failure` | **`100% tests passed out of 48`**（75.75 s；tidy 修复后复跑 76.45 s） | `build/m10/final-ctest.log`、`tidy-fix/ctest.log` |
| 5 | 金样 | `python tests/golden/smoke.py build/m10/final` | **`SMOKE total=20 pass=20 fail=0`**（tidy 修复后复跑同结果） | `build/m10/final-golden.log`、`tidy-fix/golden.log` |
| 6 | UI 冻结末行 | `$env:QT_QPA_PLATFORM='offscreen'; photopipeline.exe --ui-smoke --inputs tests/golden/base --shots .cache/ui-review-final` | EXIT=0，末行**逐字** `UI-SMOKE OK shots=12 pages=3` | `build/m10/final-ui-smoke.log` |
| 7 | UI 脚本口径 | `python tests/ui_smoke.py build/m10/final` | **`UI-SMOKE pass`**（exit 0；format-preset `checked=23 bad=0`） | `build/m10/final-ui-smoke-py.log`、`tidy-fix/ui-smoke.log` |
| 8 | 格式 | `tools\bin\clang-format-win64.exe --dry-run -Werror --style=file`（233 文件） | **FILES=233 RC=0 VIOLATIONS=0**（钉版 23.1.1；**未 `-i`**）。FINAL-REPORT 独立复跑：`RC=0 / FILES=233 / OUTPUT_LINES=0 / clang-format version 23.1.1` | `build/m10/final-fmt-files.txt`、`tidy-fix/format.log`、`build/m10/final-report/fmt-recheck.log` |
| 9 | clang-tidy 棘轮 | `python tools/ci-tidy.py build/m10/final --jobs 12` | **`TIDY total=262 baseline=262 files=76 violations=0 seed=0`，EXIT=0**；`140/140 TU`（无解析失败）；逐文件 76 文件全 ≤ 基线、基线外文件 0；未 `--update`、基线未改 | `build/m10/tidy-fix/ci-tidy.utf8.log`、`warnings.tsv`、`collect-after.log` |
| 10 | 回归 | `python tools/regression.py build/m10/final` | **`DIFF FOUND`，EXIT=1**（2187 行 unified diff；normalized 2606 vs baseline 2534）。**产物层**：只有 JXL 有损 **23/23** 变化、非 JXL **0 diff**；日志面 diff = 加性日志 + runner 形状（`thread_budget` 4→16）+ vcpkg 路径 ⇒ **本地未重播种**（D-9） | `build/m10/final-gate/regression-1.log`、`regression-attrib.log`、`jxl-deltas.log`；`.cache/regression/{current,attribution}.log` |
| 11 | HEIF 矩阵 | `python tools/heif_matrix_check.py --phase final --set final-gridtile --set final-still --set final-tune --set final-profile-compat --binary build/m10/final/photopipeline.exe …` | **233 例（240 planned，跨集 case_id 去重 7）pass=59 / fail=0 / expected-fail=174 / unexpected=0**，`done run=233 in 598.5s`，EXIT=0。子集：gridtile 11（8/0/3/0）、still 9（8/0/1/0）、tune 22（20/0/2/0）、profile-compat 191（23/0/168/0） | `build/m10/final-gate/heif/{matrix.log,final-subset.jsonl,final-subset.md}` |
| 12 | 默认产物 SHA（无评分） | 见 §2.1 表 | jpeg/webp/jxl **3/3 与基线逐字节相同**；HEIF 默认 **6/6 相同** | `build/m10/final-gate/perf/`、`build/m10/final-gate/heif/`、§2.1 |
| 13 | 性能/事件（D-4 口径） | `perf_smoke.py --exec …`、`pp_progress_audit.py --trace …` | **P4-1 wall ✅ / metric ❌**（1.072×，D-5）；**P4-4 ✅**；**P1-1 ✅**（peak_active_files=7）；**P4-2** 非 HEIC ✅、HEIC ❌ 1 处（1.44–1.67 s，D-7）；多格式并行 **1.31×** 且四格式 SHA 并行==串行 | §2.2、`build/m10/final-gate/perf/analysis.log` |
| 14 | dev E2E | `--dev tests/golden/base/rgb8.png --workers 1` + `--preset` / `--adaptive-policy` | 格式预设四格式（jpeg/webp/heif/avif）侧车 `adaptive` 齐备、round1 达标；全局预设含 adaptive（A14 补丁）侧车 `adaptive`、旧预设路径逐字节不变；`--adaptive-policy` 不可达标路径 `reason=max_rounds`（W3-T4 任务树：3 轮、输出最后一轮、score 97.479） | §1.3、`build/m10/final-gate/a14/`、`build/m10/w3-preset/e2e/`、`build/m10/w3-exec/report.md` §9 |
| 15 | `pipeline.cpp` 恢复复核 | `python build/m10/final-gate/check_pipeline.py` | **恢复合理性成立**：156,770 B / 3,070 行 / UTF-8 合法 / `U+FFFD`=0 / CRLF=0 / 0 乱码 token / 顶层函数重复定义 0 / 无冲突标记 / 文件尾正常；恢复件→当前 **15 个 diff 块全部可归因**（SCOREFIX 3 处 + W3-T4 自适应与 metawrite 抽函数） | `build/m10/final-gate/check-pipeline.log` |
| 16 | 密钥扫描 | `docs/m6-report.md` §6 命令 | tracked/untracked **0 命中**；`git check-ignore` 命中 `.zcode/secrets/` | `build/m10/final-gate/report.md` §I-3 |
| 17 | 版本 | `photopipeline.exe --version` | **`PhotoPipeline 0.4.0.alpha1`**（版本单源已升级；release 树与 zip E-1 实测同值） | 本机实测（经 `env.py run`） |

### 2.1 默认产物 SHA（零变化门禁）

| 格式 | 最终树产物 | 基线 | 一致 |
|---|---|---|---|
| jpeg（默认） | `bceff673e70d580a6e5f90e4…` | `bceff673…`（W3-T5 e2e `base-default-jpeg`） | ✅ |
| webp（默认） | `7b867fae3c0562f44483ddec…` | `7b867fae…` | ✅ |
| jxl（默认） | `8a01c36cde40a4cccbd2d75b…` | `8a01c36c…` | ✅ |
| heif `rgb8.png` | 717 B `314b9074…d668ac9` | 同（w2-heif-grid §3.1） | ✅ |
| heif `rgb16.tif` | 761 B `b42c6c17…ee01db22` | 同 | ✅ |
| heif `super/T1.tif`（22.8MP） | 1,941,390 B `9596c665…b8f906e` | 同 | ✅ |
| heif `small.tif` + `--bitdepth 8 --param chroma=420 --param quality=80` | 311,708 B `894f43f9…918ccd13` | 同（w2-profile §4） | ✅ |
| heif 同上 `--bitdepth 10` | 304,948 B `49230e64…008c5c73c` | 同 | ✅ |
| heif `small.tif`（纯默认） | 66,189 B `54ec1763…2af8c96` | 同 | ✅ |

### 2.2 性能/事件探针（设计 §12.1/§12.2；D-3/D-4 修订口径）

| 探针 | 实测（最终树） | 判据 | 判定 |
|---|---|---|---|
| **P4-1** 单文件 22.8MP TIFF→JXL + `--score` | encode 2032.1 ms / **metric 2178.6 ms**（**1.072×**）/ wall 4387 ms（`exec_wall` 4452） | `metric_ms ≤ encode_ms` 且 `wall ≤ encode+3000=5032` | metric **FAIL（超 7.2%，D-5）**；wall **PASS** |
| **P4-4** 48MP PNG + `--score` | `EXIT 0 (failed files)`；`ok=true`、out_bytes 282,405、metric state=ok、score=100、metric_ms 4801 | exit=0 且产物存在 | **PASS**（HEAD 时此场景 EXIT=1 = W1-T1 修复点） |
| **P1-1** 有效并发 | 8×22.8MP：`peak_active_files=7`、`pixel budget acquired` 8 次、峰值 3.57 GiB、wall 9663 ms、CPU 39,125 ms（≈4.01 核）、Σmetric/wall=0.298 | 从 `pixel budget acquired` 算并发 ≥4 | **PASS** |
| **P9-1** CPU | 同上 4.01 核 | ≥6 核 | **FAIL（诊断项）** |
| **P4-2** 相邻事件 ≤500ms | `single-jxl-score` / `batch8-jpeg-score` / `multi4-parallel` → `AUDIT-SUMMARY pass=1 fail=0`；**HEIC 轨迹 FAIL 1 处**：probe 段单 gap 1440.3–1667.0 ms（其余 ≤406 ms） | 相邻事件 ≤500 ms（首事件前 1 s） | 非 HEIC **PASS**；HEIC **FAIL（D-7）** |
| **P4-6** HEIC 输入 probe→decode | 8-bit `q95-420/T1.heic`：1470.2 / 1443.0 ms **PASS**；10-bit `super/T5.heic`：1669.6 / 1635.0 / 1646.5 ms **FAIL** | ≤1600 ms（D-3 修订） | 8-bit PASS；10-bit FAIL（D-6） |
| **多格式并行** | 1 文件 4 格式 22.8MP：并行 12,719 ms vs 串行 16,690 ms = **1.31×**；四格式 SHA 并行==串行（jpeg `8bc76472…`/webp `d7150e8c…`/jxl `3eb4780d…`/heif `9596c665…`） | wall ≤15000 ms + SHA 逐字节相同 | **PASS** |

### 2.3 回归产物层归因（A15/A18）

- `full-jxl` 段：产物字节 diff **94 行 / 23 个有损产物全部变化**（−518…+7 B）；`jxl8.jxl` 1501→983（−518）、
  `jxl_exif.jxl` 3684→3242（−442）、`avif_exif.avif` −1、19 个小图 +4…+7（`photo.jpg` +4、`webp_lossy.webp` +6、`exif_full.jpg` +7）。
- `full-jpeg/png/tiff/webp/bmp/heif/avif` 与 `meta-*` 段：产物字节 **0 diff**。
- **A15 已本地闭环（task-28/A15-CLOSE）**：`tools/regression.py` 增加 **N11**（宿主核数形状 → `<threads>`/`<lp>`，丢弃核数条件行）、**N12**（vcpkg buildtrees 源目录 → `<src>`）、**N6 修正**（`*_ms`/`throughput` 吃科学计数）；`--self-check` +12 条归一断言。
- `--update` 护栏：version-line=12 / behavior=196 / changed-lines=943；重播种前逐段白名单核对 **107 removed / 150 added、unmatched=0**（JXL 字节 + 参数面 + 加性日志 + heif profile 文本）。
- 重播种后 `tools/baseline/golden.windows.log`：2534→2567 行，318,270 B / SHA256 `280D2E38…224C` → **336,399 B / SHA256 `4FC72B33…6A49`**；新基线 normalize 幂等、机器形状残留 0、12 段、app version 行 = 0.4.0.alpha1。
- 复跑 **zero diff ×2（EXIT=0）**；一次慢 run 的 `5.2e+04` 科学计数假 diff 已归因并由 N6 修正。
- **未覆盖（如实）**：CI `windows-2025` 侧 zero diff 未实测（Windows 亲和性不改变 `ProcessorCount`）；**Linux 基线 `tools/baseline/golden.log` 仍旧（0.3.6.alpha2 / 4vCPU）**，需 nightly `workflow_dispatch` + `update_baseline=true` 在 ubuntu 重生成后入库，N11/N12 同样生效。

---

### 2.6 FINAL-VERIFY（0.4.0.alpha1 候选，2026-11-01）

- 二进制固定：`build/m10/final/photopipeline.exe` = 0.4.0.alpha1 / SHA256 `F3A0448AA2C3EACBE40EB7A4EE27788ACBA986BA9EDBB4E24345C5C8FDFD0FE9`（09:57:32；前后未变；HEIF 全量矩阵同 SHA）。
- 全部 PASS：ctest **48/48**（76.33s）、金样 **20/20**、`--ui-smoke` 末行逐字（format-card 81/0、m8-params 104/0）、`ui_smoke.py` pass、`--version` = `PhotoPipeline 0.4.0.alpha1`、`ci-versions-check: OK (10 keys)`、tidy **262=262/violations=0**（161s）、regression **zero diff（2567=2567 行）**、format **233 文件 RC=0**（ci-format.sh 集，钉版 23.1.1）。
- 证据 `build/m10/final-verify/`；`third_party/ssimulacra2/**` 不在 ci-format 判据集（宽口径误报已更正）。
### 2.7 CI 发行轮与 Release（tag `v0.4.0.alpha1`，2026-11-01）

- **release.yml run 41271187323（`530896c`）：success（7/7 job）** — appimage / winzip-build-dev / winzip-build / winzip-pack / probe-appimage / probe-winzip / publish；winzip-build 子集化（`--target photopipeline`）后 4m15s（首发冷构建全量超 20min 硬顶的修复，见 `530896c`）。
- **GitHub Release**：<https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.0.alpha1>，published **2026-11-01T04:07:55Z**，prerelease=False（Latest），tag `v0.4.0.alpha1`（annotated）→ `530896c`。
- **4 资产**：`win64.zip` 34,173,446 B / sha256 `0876df59…9217a`；`x86_64.AppImage` 52,595,192 B / sha256 `26b786ea…cc983`；`deps.txt` 2,439 B；`SHA256SUMS.txt` 314 B（逐项指纹见 `docs/0.4.0.alpha1-report.md` §2.5）。
- **首发失败与修复（如实）**：run 41267509907（tag @ `fa8a7be`）→ linux `test_quality_metric` 跨平台浮点判据（D-10，commit `ce5de10`）+ winzip-build 冷构建 20min 硬顶（commit `530896c` release 子集构建）；修后重打 tag 并发布。
- **`main-full` 仍未绿（CI 预算债，非产品失败）**：run 41267508093 / 41269784449 / 41271182789 的 Windows job 均在 `configure+build+ctest release（全量）` 到 20min 被 cancel、tidy job 在 clang-tidy 步骤到 20min 被 cancel（linux job 已 success）；M10 新增 20+ 源文件后 Windows 4 vCPU 冷构建全量 475 目标 >20min。待办：windows job 拆 build→artifact→ctest、tidy 分片、修复 vcpkg/ccache 缓存命中（P2 CI 债）。
## 3. 偏差与追认

### 3.1 实现期偏差登记 D-1…D-9（真源 = 设计 §16.1；此处只补最终数字与状态）

| # | 原假设 → 实测 | 最终数字 / 状态 | 处置 |
|---|---|---|---|
| **D-1** | 评分慢主因是"逐行 lcms" → **证伪** | 单次整帧 lcms≈3.34 s、默认路径两次≈7 s；按带化 row/band≈0.97 无提速；真提速 = T2c 快速内核 8.49→0.42 s（20×） | 新增 W1-T2c；设计 §2.2/§4.2.2(3) 已修订 |
| **D-2** | "无手术"可跳过 exiv2 写回 → **证伪**（跳过=丢元数据） | C9 收窄为"源无元数据且无手术"安全子集；大幅节省需编码器内嵌/免拷贝原子写 | 登记 P2；W1-T2b 已实现入口判定 |
| **D-3** | HEIC 输入 pre-decode ≤1.0 s | 双 probe 消除 2902.9→1447.8 ms（−50.1%）；剩余单次 probe≈1.45 s 为 OIIO/libheif 固有 | P4-6 目标修订 ≤1600 ms；≤1.0 s（跨 run probe 缓存）登记 P2 |
| **D-4** | 并行化后 P4-1/P4-3 可直接达成 → **证伪** | encode 降至 ~2.08 s 使比值收紧；P4-1 改 `metric≤encode` + `wall≤encode+3000`；P4-3 改"评分池并发≥2 且 wall 改善"（最终树只取到**文件级**并发 `peak_active_files=7`；评分池并发度未单独取样） | 口径已修订（设计 §12.1）；恢复原口径需指标核心按带并行（P2） |
| **D-5** | 修订后 P4-1 可达 | **未达**：encode 2032.1 / metric 2178.6 = **1.072×**（超 7.2%）；wall 4387 ms 达标 | 保持目标、如实记录未达；优化=指标核心按带并行（P2） |
| **D-6** | P4-6 ≤1600 ms | 8-bit `q95-420/T1` = 1443/1470 ms ✅；**10-bit `super/T5.heic` = 1635–1670 ms ❌**（输入相关） | P4-6 细化为「8-bit ≤1600 ms；10-bit ≤1700 ms（实测口径）」 |
| **D-7** | P4-2 首事件前 ≤1 s | HEIC 深 probe 首事件 gap 1.44–1.67 s（scheduler probe 无心跳；后续阶段均 ≤500 ms） | P4-2 例外：HEIC 深 probe 允许首事件 ≤2 s；严格 ≤500 ms 需 scheduler probe 心跳（P2） |
| **D-8** | 内置预设随包装 `share/presets/**` 出厂文件 + 安装规则 | 改为**运行时幂等 seed**：`default_adaptive_policies()` → `<data_dir>/presets/formats/<fmt>/_builtin-<fmt>.json`（首个运行创建、只读、可另存） | 无 `share/presets` 目录/安装规则；`format-presets.md` §7 已按实际行为定稿 |
| **D-9** | A15 回归基线可本地重播种 | 最终门禁曾因 runner 形状/路径噪音本地不播 | **已由 task-28/A15-CLOSE 本地收口**：N11/N12 归一 + baseline 重播种（336,399B / SHA 4FC72B33…6A49）+ zero diff ×2；剩余 CI `windows-2025`/Linux 侧复核（nightly workflow_dispatch） |

### 3.2 实现口径偏差 7 条（如实；均已登记，不静默改口径）

1. **T2 的按带 lcms 没有可测提速**：设计把 `--color srgb` 隔离出的 12.18→4.73 s 归因于"逐行 lcms"，实测 band/row≈0.97
   （固定开销 <2%）⇒ 归因不成立；真正省下 ~7 s 的是 T2c 快速内核（D-1）。
2. **P4-1/P4-3 口径修订（D-4）**：T5 异步化后 `metric_ms/encode_ms` 比值语义失真，改判 `metric_ms ≤ encode_ms`
   且 `wall ≤ encode_ms+3000`（最终树该口径下 metric 臂仍未达，D-5）。
3. **`filelistmodel_util.h` 一次越界改动**：W1-T7 为跨文件编译加性 1 行映射 + 断言 14→15（`src/ui/**` 属 UI 属主），
   **仍待 UI 属主书面复核**后追认（engine-b §7-③ / A1 证据列）。
4. **`format_preset_io.cpp` 未进 `pp_core`**：CMake GLOB 使其只进 `photopipeline` UI 目标；单测按先例
   `#include "ui/format_preset_io.cpp"`。是否迁移由 Lead 决策（CMake 属授权面，本报告不改）。
5. **设计 §8.2 的 `heif_grid:"auto"` 与目录 int 枚举冲突**：实现与 schema 取目录类型（int 0/1/2）；
   写字符串或非法值在逐键校验被拒（不静默当 0），设计示例已按 Lead 决策修正为 int。
6. **JXL 删行导致既有 UI 探针失效**：`pp-param-enable-noise` 锚点迁到保留键 `dots`（语义逐条不变）；
   `magic-auto` 几何断言根因 = `epf` 默认 2→−1（"−1 即自动"），改三态取样而非放宽（ui-adapt）。
7. **quality 行标签变更影响截图文本**：C21 改「质量（0–100；CRF=(100−q)/2)」⇒ 02-output 截图内容有意变化；
   截图集/冻结行不变，`tests/ui_smoke.py` 明写不做像素比对（tune-rc §偏差 1）。

> 另有三项**事故/文档类**（不是口径变更，单独记录）：⑧ SCOREFIX 的 `pipeline.cpp` 文本替换损坏 → 已重放恢复，
> 由 FINAL-GATE §H 专项复核"恢复合理性成立"（§2 表格 #15）；⑨ 磁盘两次告急（per-task 构建树 21 GB / 0 字节）→ 删除
> 可重建构建树、报告副本先存 `_evidence/`；⑩ AUDIT-1 发现 3 处 docs 计数漂移（目录总行 117=43+57+3+14、m8 文本/只读 2/14）
> → Lead 已修、`param-catalog.md` CRLF 归一（A16）。

### 3.3 FINAL-TIDY 的 132 条收敛（84 条 NOLINT 分类）

- 修复前 `394` 条（1 个 TU `src/core/adaptive.cpp` 解析失败 ⇒ 计数不完整）；修复后 **262 = 基线 262**、76 文件、`violations=0`。
- **132 条 = 代码改 48 条 + NOLINT 抑制 84 条**（62 行指令：56 行 `NOLINTNEXTLINE`/行尾 `NOLINT` + 3 对 `NOLINTBEGIN/END`）：

| 检查 | 条数 | 处置 | 理由 |
|---|---|---|---|
| `bugprone-easily-swappable-parameters` | **41** | NOLINT | 启发式误报：数值内核/测试辅助的 x/y、width/height、name/text/format 由名字与调用点固定 |
| `portability-simd-intrinsics` | **26** | NOLINTBEGIN/END | AVX2 是本仓硬要求（x86-64-v3，AGENTS §1.4）；`simd/kernels.cpp` 同类 37 条在基线内 |
| `bugprone-exception-escape` | **15**（另 1 条代码改） | NOLINT | MSVC STL `_Throw_bad_array_new_length`（`bad_alloc` 派生）未被 `ignoreBadAlloc` 过滤 + 闭包/结构体隐式 move ctor 被 matcher 当作 should-not-throw |
| `bugprone-suspicious-include` | **2** | NOLINT | 白盒单测包含实现 `.cpp`（与既有 `test_icons`/`test_meta_format` 同模式） |

- 代码改 48 条全部语义等价（`static_cast`/`qsizetype`、`reserve`、`+=`/`insert`、去 `const` 启用隐式 move、enum `: std::uint8_t`、
  `std::swap` 取代跨迭代 move、`(void)0;` 显式空语句）；两处需注明：① `src/core/adaptive.cpp:569` 全角 `'）'` 窄字面量
  （clang 前端 parse error、MSVC 可编过的**可移植性缺陷**）按注释意图改为 UTF-8 字节序列 `ends_with` —— 本任务唯一
  **非纯静态**改动；② `src/core/score_queue.cpp::~ScorePool` 包 try/catch，正常路径零变化、仅异常路径由 `std::terminate`
  改为尽力清理。
- 基线未改：`tools/baseline/clang-tidy.tsv` 8,889 B / SHA256 `82364EC4…2C8D`、mtime 2026-10-13（`git status` 空）；**未 `--update`**。
- 既有 5 文件仍有 20 条基线内老告警未动（`page_run` 6 / `page_output` 5 / `test_color` 5 / `pipeline` 3 / `output_page_build` 1）。

### 3.4 A1–A20 台账状态（最终核对）

| 裁定 | 状态（最终） | 证据 |
|---|---|---|
| A1 `FileState::OutputDone` | 已落地 | trace 11/11 早于文件终态、0 违规；`filelistmodel_util.h` 加性 1 行**待 UI 属主复核**（§3.2-3） |
| A2 `FileEvent::output` | 已落地 | 3 条真实 trace 事件顺序 0 违规；侧车/trace 含 output 对象 |
| A3 `adaptive_policy` / `max_parallel_targets` | 已落地 | W3-T4（pipeline.h 末尾字段 → PlannedTarget → 空串分派）；多格式并行 SHA 相同 |
| A4 `FileEntry::probe` | 已落地 | 2902.9→1447.8 ms |
| A5 `ProgressInfo` 加性字段 | 已落地 | 心跳/评分/自适应字段进 trace；`pp_progress_audit` 非 HEIC 全 PASS |
| A6 `settings.h` 评分池配置 | 已落地（降级为不改） | 评分池参数走内部常量，**未为池新增字段**（`settings.h` 的唯一改动是 A17 的 `stagger_ms` 默认值） |
| **A7** `[sched] thread alloc`/`e3` 数值语义 | **未落地**（M10 不实施 E3/W_eff 改造） | `_evidence/w1-engine-b__report.md`；实际并发由像素预算+评分池决定 |
| A8 `core/presets.h` 零改动 | 已落地（零改动） | `presets.h` 未进 `git status` 改动面；台账已更新为「已落地（零改动）」 |
| A9 `ui/page_output.h` 加性 | 已落地 | `build/m10/w3-ui/report.md`：7 信号 + 8 方法、仅末尾加性 |
| A10 `RunPlan::quality_score` | 已落地 | `_evidence/ui-t9b__report.md` |
| A11 UI objectName 收缩 + 只增 | 已落地 | 速度列族删除；`pp-format-preset-*`/`pp-adaptive-*` 落地；冻结末行逐字不变 |
| **A12** `--ui-smoke` 换锚 + 只增 | **部分落地** | 布局/状态/JXL/HEIF/RC/tune/格式预设/内置/全局自适应探针已落地；T9b 待补清单（`score-immediate`/`facts-immediate`/`run-status-map` 扩展等）未补 ⇒ 仍需 Lead 复核追认 |
| A13 `SCHEMA.md` 加性 | 已落地 | +74 行、旧断言零改动；侧车 `adaptive` 结构实测齐备 |
| A14 `--dev` CLI 加性 | **已落地（本次补齐）** | `--adaptive-policy`（W3-T4）+ `--preset` 格式预设（W3-T4）+ **全局预设 `outputs[].adaptive`**（FINAL-GATE 加性 3 行 + E2E） |
| **A15** 回归重播种 | **已归因、未重播种** | 产物仅 JXL 有损 23/23；本地不播（D-9）⇒ CI `windows-2025` 或 N11 归一后重播 |
| **A16** 文档真源同步 | **部分**（文档面已收口） | W1 spec/mockup、W2 计数、W3 手册、本文、param-catalog（含 3 缺口）已收口；**README/CHANGELOG/版本号待发行确认后同步** |
| A17 `stagger_ms` 默认 0 | 已落地 | `settings.h` 默认 0；回归日志无 stagger 差异 |
| A18 JXL 43→31 键 | 已落地 | `epf=-1`、三态、12 个 no-op 删除；回归逐条一致（§2.3） |
| **A19** `OutputResult::adaptive` | **预登记（可延后）** | 未改 `OutputResult`；自适应摘要走 dev-only 进程级发布表 + `ProgressInfo.adaptive_round/adaptive_max` |
| A20 `OutputFormatSpec::adaptive_enabled` | 已落地 | 末尾加性；引擎判据仍是 `adaptive_policy` 非空；GUI `config_base()` 门控；全局预设 `outputs[].adaptive.enabled` 持久化 |

### 3.5 `docs/param-catalog.md` 三处登记缺口（本次已补，最小修正）

1. **grid 结构上限**：补「HEIF grid `rows`/`cols` 是 u8+1 ⇒ 单边 ≤256 且 `rows×cols ≤65535`；超限中文 `error_zh`」
   （实现 `src/codecs/heif_capability.cpp:322-323,605-612` 的 `kMaxGridSide=256` / `kMaxGridTiles=65535`）。
2. **H5 告警**：补「Level=自动 + 手动 tile 允许但 warn（规则 H5）：与 Apple 参考 1024×576 不同，iPhone 接受度未验证」
   （实现 `src/core/param_rules.cpp` 的 `H5` 规则，文案逐字对齐）。
3. **WebP 两行表格格式缺陷**（`qmin/qmax`、`near_lossless` 行尾多一个说明单元格）：把说明折回「级别」单元格，
   6 列对齐；**事实不动**。

> 同类**既有**缺陷（不在本次 3 处范围）：`param-catalog.md` §6 PNG 的 `compressionLevel` 行仍带第 7 个说明单元格
> （R-010 期风格）—— 本报告未动，已 pin 在 `build/m10/final-report/tables.log`（`KNOWN(pinned, out of scope)`）。
> 另：`m10-workflow.md` 两处 `|Δ|` 未转义导致表格列错位，已改为 `\|Δ\|`（**纯格式，事实不动**）。

---

## 4. 未覆盖 / 未决（如实）

1. **A15 本地已闭环、CI 侧待复核**（D-9 → task-28/A15-CLOSE）：N11/N12 归一 + 本地重播种后 **zero diff ×2**（基线 336,399 B / SHA `4FC72B33…6A49`，app version 行 = 0.4.0.alpha1）。仍未实测：CI `windows-2025`（Windows 亲和性无法模拟 4 vCPU）与 **Linux `golden.log`**（需 nightly `workflow_dispatch` + `update_baseline=true` 在 ubuntu 重生成后入库；N11/N12 同样生效）。
2. **TSan 未测（本机不可跑；CI nightly tsan job 自 #28 起 build 红）**：`wsl -l -v` 未安装、无 clang/gcc、MSVC 仅 ASan、`cmake --preset tsan` 为 disabled preset ⇒ 本机无 TSan 运行面，**不宣称竞态已验**。CI 事实：nightly tsan job 存在（ubuntu-24.04/20min/契约 ctest+金样+0 报告扫描），last success #27（2026-10-11）；#28–#50 共 23 轮无 success（22 failure + 1 cancelled，抽样 7 轮均 build 红、ctest/金样 skipped）。修复路径见 `build/m10/tsan/report.md` §2.2（P0 取日志修 build→复跑三步；P1 固定 targets 加 `pp_test_score_queue`）；替代压力证据：4 格式并行 40/40、评分池 20/20+硬杀恢复 5/5+cancel→run2 20/20、8×22.8MP×3 轮全绿（max_gap 164–276ms）。
3. **HEIF 全量矩阵已跑（0.4.0.alpha1 候选 `F3A0448A…`）**：全量 1133 planned、跨集去重 31 ⇒ **实跑 1102 = pass 732 / fail 0 / expected-fail 370 / unexpected 0**（2566.0s，EXIT=0），默认 HEIF SHA **6/6 同基线**；与 M9 1013 例对比 verdict/expected_legal/hvcC 事实差异 0、685/697 pass 产物逐字节同，新增 89 例全为 M10 新覆盖。登记：195 ef reason kind `level→grid`（文案/标签变化）、12 pass 字节数变（hvcC 同，码流级归因未做）；AVIF/iPhone 不在矩阵范围。
4. **内置预设不走 `share/presets`**（D-8）：无出厂文件、无安装规则；改运行时幂等 seed。任务卡 §W3-T5 的"打包"措辞
   与实现不一致，按 D-8 为准（**行为已由单测/`--ui-smoke` 钉住**）。
5. **P4-1 metric 臂未达**（D-5，1.072×）：恢复原口径需指标核心按带并行（P2）。
6. **HEIC 10-bit probe 超 1600 ms**（D-6，1635–1670 ms）与**深 probe 首事件 gap**（D-7，1.44–1.67 s）：`super/T5.heic`
   单样本口径差异 + scheduler probe 段无心跳；均非本次引入。
7. **P9-1 CPU ≥6 核未达**（实测 ≈4.01 核，诊断项）；P4-3 已按 D-4 改为"评分池并发≥2 且 wall 改善"，
   最终树只取到**文件级**并发 7（P1-1），**评分池并发度未单独取样** ⇒ 该项只按诊断口径记录。
8. **A12 部分落地 / A7 未落地 / A16 文档面已收口（README/CHANGELOG/版本待发行）/ A19 预登记**：见 §3.4；
   A8/A14/A16 已由 Lead 在台账追认（A8「已落地（零改动）」、A16 收口说明），剩下的 A12/A7/A19 仍需发行前统一复核。
9. **A14 之外的 dev 文案瑕疵**：全局预设 adaptive 与 `--adaptive-policy` 同时给出时报错文案仍写
   "conflicts with the format preset's adaptive policy"（行为正确=exit 2，措辞未区分全局预设；dev-only，建议顺手改）。
10. **版本 / CHANGELOG / README 未动、git 未提交**：当前工作树 **66 个 modified + 33 个 untracked**（详见 §6.3），
    版本单源仍 `0.3.6` + `.alpha2`；按任务书禁改，**待用户确认**后再做发行提交与 tag。
11. **`format_preset_io.cpp` 未进 `pp_core`**（§3.2-4）、**jsonschema 包缺失**（两份 schema 只做 `json.loads` 语法解析，
    未做 2020-12 meta-schema 校验）、**iPhone 真机接受度 / RExt 硬解 / R-2 规范命名**（外部规范待复核）——均未变。
12. **GUI 收尾 `pending>0` 端到端**：仍只有语义单测 + dev 侧证（夹具太小无法触发真实 pending>0）。

---

13. **`main-full` windows/tidy 20min 硬顶（CI 预算债）**：三次 run 证据见 §2.7；根因为 Windows 4 vCPU 冷构建全量 475 目标 >20min（M10 新增 20+ 源文件），vcpkg 缓存 `VCPKG_HIT=false`。release 独立 workflow 已成功并发布；本报告不宣称 `main-full` 绿。待办：windows 拆分 build/test 两 job、tidy 分片、修缓存命中。
## 5. 复现命令与证据路径

### 5.1 命令（最终集成树；全部经 `python tools/env.py run --`）

```powershell
# 干净树配置 + 构建
python tools/env.py run -- cmake --preset release-dev -B build/m10/final -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/m10/final -j 8

# 五道既有门禁
python tools/env.py run -- ctest --test-dir build/m10/final --output-on-failure
python tools/env.py run -- python tests/golden/smoke.py build/m10/final
$env:QT_QPA_PLATFORM='offscreen'; python tools/env.py run -- build/m10/final/photopipeline.exe --ui-smoke --inputs tests/golden/base --shots .cache/ui-review-final
python tools/env.py run -- python tests/ui_smoke.py build/m10/final
python tools/env.py run -- python tools/ci-tidy.py build/m10/final --jobs 12
tools\bin\clang-format-win64.exe --dry-run -Werror --style=file (Get-Content build/m10/final-fmt-files.txt)

# 回归 + HEIF 矩阵
python tools/env.py run -- python tools/regression.py build/m10/final
python tools/env.py run -- python tools/heif_matrix_check.py --phase final --set final-gridtile --set final-still `
    --set final-tune --set final-profile-compat --binary build/m10/final/photopipeline.exe `
    --jsonl build/m10/final-gate/heif/final-subset.jsonl --tmp-dir build/m10/final-gate/heif/tmp

# 性能 / 事件 / 恢复复核
python tools/env.py run -- python tools/perf_smoke.py --exec <run> --cwd . -- build/m10/final/photopipeline.exe --dev <inputs> --out <run> --format jxl --score --workers 1 --log-level info
python tools/env.py run -- python tools/pp_progress_audit.py --trace <run>/progress-trace.jsonl
python tools/env.py run -- python build/m10/final-gate/check_pipeline.py

# dev E2E（格式预设四格式 / 全局预设 adaptive / --adaptive-policy）
python tools/env.py run -- build/m10/final/photopipeline.exe --dev tests/golden/base/rgb8.png `
    --preset build/m10/final-gate/a14/global-adaptive.json --out build/m10/final-gate/a14/post-adaptive --workers 1
python tools/env.py run -- build/m10/final/photopipeline.exe --dev tests/golden/base/rgb8.png `
    --preset build/m10/w3-preset/presets/formats/heif/_builtin-heif.json --out <run> --workers 1
python tools/env.py run -- build/m10/final/photopipeline.exe --dev tests/golden/base/rgb8.png `
    --adaptive-policy tests/golden/smoke-adaptive/policy-notmet.dsl --out <run> --workers 1

# 版本
python tools/env.py run -- build/m10/final/photopipeline.exe --version
```

### 5.2 证据路径

| 类别 | 路径 |
|---|---|
| 最终门禁报告（A–K 全结果） | `build/m10/final-gate/report.md`（+ `a14/`、`perf/`、`heif/`、`regression-*.log`、`check-pipeline.log`、`tidy*.log`） |
| 静态分析修复报告 | `build/m10/tidy-fix/report.md`（+ `ci-tidy.utf8.log`、`warnings.tsv`、`collect-after.log`、`ctest.log`、`golden.log`、`format.log`） |
| 本次定稿的一致性证据 | `build/m10/final-report/report.md`（+ `fmt-recheck.log`、`check.log`、`tables.log`、`check_numbers.py`、`check_tables.py`） |
| 门禁日志（最终树） | `build/m10/final-build.log`、`final-ctest.log`、`final-golden.log`、`final-ui-smoke.log`、`final-ui-smoke-py.log`、`final-fmt-files.txt` |
| 任务报告（在树原件） | `build/m10/{engine-scorefix,w3-core,w3-ui,w3-exec,w3-preset,w3-docs,final-gate,tidy-fix}/report.md` |
| 任务报告（归档副本） | `build/m10/_evidence/<task>__report.md`（16 份：w1-engine-a / w1-engine-b（+`__report-task5-w1a1`）/ w1-fastcolor / w1-metric / w1-ui（+`__report-status`）/ w1-tools / ui-t9b / w2-heif-grid / w2-profile / w2-tune-rc / w2-jxl / audit / ui-adapt / w3-core） |
| 回归产物/归因 | `.cache/regression/{current.log,attribution.log}`；`build/m10/final-gate/{regression-1.log,regression-attrib.log,jxl-deltas.log}` |
| 设计/裁定/日志/手册 | [`v0.4.0-design.md`](v0.4.0-design.md)、[`m10-adjudications.md`](m10-adjudications.md)、[`m10-execution-log.md`](m10-execution-log.md)、[`m10-workflow.md`](m10-workflow.md)、[`adaptive-policy.md`](adaptive-policy.md)、[`format-presets.md`](format-presets.md)、[`param-catalog.md`](param-catalog.md) |

---

## 6. 发行前检查清单

> 本节逐项列**动作项**：§6.1–§6.6 均需用户/Lead 决策或执行；本报告只提供口径与建议，**未执行任何发行动作**。

### 6.1 版本单源升级（建议，待用户确认）

- 现状：`CMakeLists.txt:19` `project(PhotoPipeline VERSION 0.3.6)` + `:26` `set(PP_VERSION_LABEL ".alpha2")`，
  `vcpkg.json` 数字段 `0.3.6`；`--version` = `PhotoPipeline 0.3.6.alpha2`（**本次未动**）。
- 建议（二选一，须用户书面确认）：
  - **A（预发布递进）**：`0.3.6.alpha2 → 0.4.0.alpha1`：数字段 `0.3.6→0.4.0`、标签 `.alpha2→.alpha1`、`vcpkg.json` 同步 `0.4.0`；
    适合"W1/W2/W3 已收口但回归重播种/TSan/全量矩阵仍欠"的当前状态（预发布允许登记未决）。
  - **B（直接正式版）**：`0.4.0`（标签置空串）：要求先关闭 §4 的 1（A15 重播种）、2（TSan）、3（全量矩阵）与 A12/A16 追认。
- 单源纪律：改版本只改 `CMakeLists.txt:19-26` + `vcpkg.json` 数字段；CHANGELOG/README/AGENTS 一律**引用**，不得自定义；
  R-122 断言要求 `vcpkg.json` 数字段 == `project(VERSION)`。

### 6.2 CHANGELOG / README（未动，待写）

| 文件 | 需要的增量 | 依据 |
|---|---|---|
| `CHANGELOG.md` | 新增 `0.4.0`（或 `0.4.0.alpha1`）发行节：发行/新增/修复/变更四段；逐条对应 §1.1–§1.3 与 §3.1 的 D-1…D-9 | AGENTS §5.4、§6.2 |
| `README.md` | 用户视角增量：评分默认开 + 评分池、运行页列变更（删速度列/中间省略/全路径 tooltip）、HEIF grid/tile/Level/still 档/tune、JXL 治理（43→31/三态/`epf` 默认）、**格式预设 + 自适应（默认关闭、无 GUI 编辑器、内置 5 条 sample）**、系统要求不变 | AGENTS §5.4 |
| `docs/0.4.0-report.md`（发行报告） | 由本报告摘要 + 双平台 CI 读数组成（本报告不含 CI 结果） | 先例 `docs/0.3.6.alpha2-report.md` |
| `AGENTS.md` §2 | 版本口径与里程碑表更新（M10 = 0.4.0） | 同上 |

### 6.3 git 提交拆分建议（当前全部未提交）

现状（`git status --porcelain`）：**66 modified + 33 untracked = 99 条路径**；`tools/baseline/**` 干净；分支 `main`，
HEAD = `1208894`（0.3.6.alpha2 收口）。建议**按可回滚单元拆分**（每提交自带门禁证据）：

| # | 建议提交 | 范围（示例） | 验收 |
|---|---|---|---|
| 1 | `M10-W1: 评分/预算/事件/多格式并行/运行页` | `src/core/{pipeline,score_queue,metric,fastcolor,pixelbudget,progress,scheduler,metadata,colormanager}*`、`third_party/ssimulacra2/compat/*`、`src/ui/{page_run,mainwindow_run,filelistmodel_util.h,formkit}*`、`tests/unit/test_{score_queue,quality_metric,progress,pixelbudget,scheduler_contract,pipeline_contract,metadata,color,settings,params}*`、`tools/{perf_smoke,pp_progress_audit}.py` | 构建 + 48 单测 + 金样 20/20 |
| 2 | `M10-W2: HEIF grid/profile/tune/RC + JXL 治理` | `src/codecs/{heif_*,enc_heif,enc_common,enc_jxl,enc_tune_facts,format_tables,param_catalog/*}`、`src/core/param_rules.cpp`、`src/ui/{page_output*,paramform,output_page_build}*`、`tools/{param_catalog_specs/*,gen_param_catalog.py,heif_matrix_check.py,pp_linkprobe.cpp}`、`tests/unit/test_{heif_*,jxl_*,param_*,tune}*` | 矩阵 4 子集 0 unexpected + 默认 HEIF SHA 不变 |
| 3 | `M10-W3: 格式预设 + 自适应（core/exec/UI/预设/seed）` | `src/core/{format_preset,adaptive,adaptive_exec}*`、`src/ui/{format_preset_io,preset_io,mainwindow_presets,smoke_probes}*`、`docs/schemas/*`、`tests/unit/test_{format_presets,adaptive_*,presets}*`、`tests/golden/{SCHEMA.md,smoke-adaptive/**}`、`tests/ui_smoke.py` | ctest 48/48 + ui-smoke 冻结行 + E2E 侧车 |
| 4 | `M10-DOCS: 设计/裁定/工作流/收口报告/手册/参数清单` | `docs/{v0.4.0-design,m10-*,adaptive-policy,format-presets,param-catalog,m8-param-control-map,qt-gui-design-spec,mockups-v2/run.html}` | 文档口径与本报告一致（`build/m10/final-report/check_numbers.py`） |
| 5 | `M10-BASELINE: N11/N12 归一 + 回归基线重播种（JXL 有损 + runner 形状）` | `tools/regression.py`、`tools/baseline/golden.windows.log`（336,399B / SHA 4FC72B33…6A49） | 本地 zero diff ×2；CI windows-2025/Linux 侧待 nightly workflow_dispatch 复核 |
| 6 | `M10-REL: 版本 0.4.0(.alpha1) + CHANGELOG/README/发行报告` | `CMakeLists.txt`、`vcpkg.json`、`CHANGELOG.md`、`README.md`、`docs/0.4.0-report.md`、`AGENTS.md` | `--version` 单行 + 打包门禁 |

- 纪律：**报告类提交不入 tag**；tag 指向发行树；资产挂载后不可移动（AGENTS §9）。
- 注意：第 5 项若在本地播（不推荐，D-9）会把 16 核形状播进基线 ⇒ 必须由 CI 侧完成。
- 共享文件冲突：`page_output*`/`output_page_build.cpp`/`smoke_probes.cpp`/`preset_io.cpp` 被 W2 与 W3 先后修改，
  拆提交时按**最终功能归属**或按 hunk（`git add -p`）拆，不要整文件硬塞给其中一波。

### 6.4 CI 重播种任务（windows-2025）

1. 在 `windows-2025`（4 vCPU）runner 上跑 `python tools/env.py run -- python tools/regression.py --update`（先复核 diff 归因：
   只允许 JXL 有损 23/23 + 加性日志 + 版本行；runner 形状差异在 4 vCPU 上应消失）。
2. 若仍要保留本地可复现：先加 **N11 归一**（`thread_budget`/`alloc_files`/`encode_threads`/`peak_sum_encode_threads`、
   vcpkg buildtrees 路径），再在任一环境重播 —— 建议 N11 与重播**同一提交**，避免二次漂移。
3. nightly `regression-windows` 继续用同一基线比对；重播后必须复跑一次 nightly 确认绿。

### 6.5 P2 后续（不在本版交付，按影响排序）

| # | 项 | 现状 | 建议 |
|---|---|---|---|
| 1 | **指标核心按带并行** | P4-1 metric 2178.6 ms > encode 2032.1 ms（D-5） | 恢复 `metric≤0.6×encode` 原口径的唯一路径 |
| 2 | **scheduler probe 心跳** | HEIC 深 probe 首事件 gap 1.44–1.67 s（D-7） | 让 probe 段发心跳，回到 P4-2 严格 ≤500 ms |
| 3 | **`output best`** | 校验期显式拒绝（`output_best_unsupported`） | 需候选文件集保留/磁盘成本评估（设计 §16 第 4 条） |
| 4 | **`tech` 切换作为策略目标** | 默认禁止（`tech_not_supported`） | 需"切换后重跑整套校验"设计（设计 §16 第 5 条） |
| 5 | **TSan** | **未跑** | 至少对多格式并行 + 评分池跑一次 `tsan` 预设 |
| 6 | **HEIF 全量矩阵** | 本次 4 子集 233 例 | 发行前补 `final-auto`/`final-signals` 等到 1013 例口径 |
| 7 | **84 条 NOLINT 工具链复核** | clang-tidy 21.1.0 无 `-Wunused-nolint` | 升级 clang-tidy 时复核（若 `ignoreBadAlloc`/lambda move ctor 判定变化，NOLINT 会变噪音） |
| 8 | 其它 | 跨 run probe 缓存（D-3 ≤1.0 s）、编码器内嵌元数据 / 免拷贝原子写（D-2）、既有 5 文件 20 条老告警收敛、`format_preset_io` 入 `pp_core`、N11 归一 | 按需 |

### 6.6 tag / 资产纪律

- tag 只在**发行树**上打（annotated）；报告类提交不入 tag；资产上传后**不可再移动/替换**。
- 版本单源改动的提交必须在正文里贴 `--version` 输出与 `vcpkg.json` 数字段一致性证据（R-122）。
- 发行前最后一跑：干净树 configure/build + ctest 48/48 + 金样 20/20 + `--ui-smoke` 冻结行 + `ui_smoke.py` + format 233/0 +
  tidy 262/violations=0 + HEIF 默认 SHA；回归本地已 zero diff（N11/N12 + 重播种），CI 双平台待复核。
- **本次未做**：版本号、CHANGELOG、README、发行报告、任何 git 写操作（`add/commit/checkout/reset/stash` 全部未执行）。
