# M10 / 0.4.0 详细开发计划（workflow）

> **状态**：**三波已收口**（W1/W2/W3 + FINAL-GATE/FINAL-TIDY/FINAL-REPORT 三连），**0.4.0 发行待用户确认**。
> 收口报告 = [`docs/m10-report.md`](m10-report.md)（数字真源）；本文 §10–§12 为**任务卡最终状态 / 最终门禁记录 / 预算实际对账**，
> 前三节保留开工时的计划口径（不改写历史）。
> **纪律**：AGENTS.md §6（鼓点纪律 + 单测随码走 + 证据文化 + 门禁）；每个任务 = 实现 + 单测 + 探针 + 提交；冻结面改动只走加性+追认（设计 §10 A1–A16）。
> **执行方式**：波次串行、波内任务并行；每个任务卡有独立写作用域，重叠面由 Lead 串行化或分文件；Lead 负责契约冻结、集成、门禁与最终 diff 复核。

---

## 1. 波次总览与依赖

```
W0 设计确认（当前）──► W1 性能与运行页（1/4/5/6/9）──► W2 参数面（2/3/7/8/12）──► W3 预设+自适应（10/11）──► 0.4.0 发行
                          │                              │                              │
                          └─ 0.4.0.alpha1（预发布可选）    └─ 0.4.0.alpha2（预发布可选）    └─ 0.4.0 正式
```

| 波次 | 范围 | 预算（单人日） | 出口判据 |
|---|---|---|---|
| W0 | 设计确认、裁定预登记、分支/证据目录、验收探针骨架评审 | 1–2 | 用户书面确认 §15 决策 + §10 追认清单；`docs/m10-adjudications.md` 建文件 |
| W1 | 需求 1/4/5/6/9：评分链路重构、预算/默认值、双重 probe、多格式并行、引擎逐输出事件、运行页重构、机械探针 | **24–30** | §12.1/§12.2 全部探针通过；回归 zero diff（W1 无有意产物漂移）；`UI-SMOKE OK shots=12 pages=3` |
| W2 | 需求 2/3/7/8/12：HEIF grid/tile/Level、still 档+SPS、tune 全量、RC 数值行上移、JXL 治理+默认值变更 | **16–18** | §12.3/§12.4 探针；HEIF 默认产物 SHA 零变化；JXL 重播种归因完成；矩阵 0 unexpected |
| W3 | 需求 10/11：格式预设、自适应 DSL/执行器、内置策略库、GUI 只读面、文档/报告/发行 | **15–17** | §12.5 探针；dev 端到端；回归 zero diff；0.4.0 收口报告 |
| **合计** | | **≈55–65 人日** | 3–4 个并行子代理 + Lead 集成 ≈ **8–10 个日历周**（含评审/门禁/CI 排队） |

**裁剪档（如需压缩周期，P2 可后置 ≈5–7 人日）**：指标内并行（band 并行）、窄窗隐藏耗时/评分列、自适应 `output best`、`tech/backend` 切换策略、500+ 文件懒建行/虚拟滚动。

---

## 2. 波次内并行编排

### W1（性能与运行页）

| 任务 | 写作用域（workspace 相对） | 依赖 | 人日 |
|---|---|---|---|
| **W1-T1** P0 热修：评分预算拆分（文件 need 恢复 2×frame；评分独立配额；超大图评分 `Unavailable` 不失败）+ `stagger_ms` 默认 0 + `pixel budget` 日志归因 | `src/core/pipeline.cpp`（预算段）、`src/core/pixelbudget.*`、`src/core/settings.*`、`tests/unit/test_pixelbudget*`、`tests/unit/test_settings*` | — | 1.5 |
| **W1-T2** 评分按带 lcms + 日志限流（`rgb_rows_to_srgb_linear`，color 日志降频/限流） | `src/core/pipeline.cpp`（metric rows 段）、`src/core/colormanager.cpp`（日志）、`tests/unit/test_quality_metric*` | T1（同文件串行） | 1 |
| **W1-T2b** exiv2 元数据"无手术快路径"（C9）：无 EXIF/XMP 变更/无隐私剥除/无 mtime 同步时跳过整文件拷贝重写；有手术时行为逐字节不变 | `src/core/metadata.cpp`、`tests/unit/test_metadata*` | 可与 T1 并行（不同文件） | 0.5–1 |
| **W1-T2c** 评分色彩归一快速内核（D-1 修正后新增）：默认 keep 路径两次整帧 lcms≈7s/23MP → 3D CLUT/SIMD 归一，SSIMULACRA2 分数对拍 \|Δ\|≤0.01；metric_ms ≤0.6×encode | 新 `src/core/fastcolor.*`、`src/core/pipeline.cpp`（metric rows 区域）、`tests/unit/test_quality_metric*` | T2（同文件串行） | 2–3 |
| **W1-T3** 双重 probe 消除（`FileEntry::probe` 加性；scheduler 回填；pipeline 复用；HEIC 探针基准） | `src/core/pipeline.h`（A4）、`src/core/scheduler.cpp`、`src/core/pipeline.cpp`（probe 段）、`tests/unit/test_*probe*` | T2（同文件串行） | 0.75 |
| **W1-T4** 指标流式化（C2/C10）：row-band 实现、峰值内存、oracle 对拍（PyPI ssimulacra2 + 现实现双跑） | `third_party/ssimulacra2/**`（兼容层）、`src/core/metric/**`、`tests/unit/test_quality_metric*`、`tools/oracle/`（dev-only） | 独立（可与 T1–T3 并行） | 4–5 |
| **W1-T5** 独立评分池 + 异步评分（C3/C14）：`ScorePool`、任务队列、结果事件、dev_cli 收敛等待、取消 | `src/core/score_queue.*`（新）、`src/core/pipeline.cpp`（评分调用点）、`src/dev_cli.cpp`、`src/core/settings.*`（可选） | T4（流式后内存口径）× T1 | 2–2.5 |
| **W1-T6** 同文件多格式并行编码（C5）：任务组、飞行预算 token、进度并发安全、取消/失败隔离、单测（TSan） | `src/core/pipeline.cpp`（encode_loop 段）、`src/core/enc_common.*`（飞行预算表）、`src/core/pixelbudget.*`、`tests/unit/test_pipeline_multitarget*` | T1/T3 | 3.5–4 |
| **W1-T7** 引擎逐输出事件（C13/C14/C15）：`FileState::OutputDone`、`FileEvent::output`（shared_ptr）、`ProgressInfo` 心跳/评分/自适应轮次、事件发射点、scheduler 转发、dev_cli trace | `src/core/types.h`（A1）、`src/core/pipeline.h`（A2/A5）、`src/core/progress.*`、`src/core/pipeline.cpp`（事件段）、`src/core/scheduler.cpp`、`src/dev_cli.cpp`、`tests/unit/test_progress*` | T5/T6 | 2–2.5 |
| **W1-T8** 运行页布局（C10/C11）：`RunTableColumns` 单源、删速度列、squeeze/ElideMiddle、全路径 tooltip、表头/行统一 | `src/ui/page_run.cpp`、`src/ui/formkit.*`（加性 elide 参数）、`src/ui/page_run.h`（A10 可选） | 独立（UI 可先行） | 1.5–2 |
| **W1-T9** 运行页状态/刷新（C12/C15）：组行聚合态、百分比、评分中/完成/跳过、双通道 flush、dirty-row、逐输出事件消费 | `src/ui/page_run.cpp`、`src/ui/mainwindow_run.cpp`（按值拷贝 OutputResult） | T7、T8（同文件串行） | 2–2.5 |
| **W1-T10** 规格/文字/探针换锚（A11/A12）：`qt-gui-design-spec.md` §7.3、mockups run.html、smoke_probes 4 处换锚、新增探针、`--ui-smoke` 重跑 | `docs/qt-gui-design-spec.md`、`docs/mockups-v2/run.html`、`src/ui/smoke_probes.cpp`、`tests/ui_smoke.py`、`docs/m10-adjudications.md` | T8/T9 | 1.5 |
| **W1-T11** 机械探针：`tools/perf_smoke.py`、`tools/pp_progress_audit.py`、fixtures（48MP/HEIC/8×22.8MP 集） | `tools/perf_smoke.py`（新）、`tools/pp_progress_audit.py`（新）、`build/m10/probe-fixtures/`（gitignored） | T1–T7 | 1–1.5 |
| **W1-T12** W1 集成、门禁、证据、W1 报告增量、0.4.0.alpha1 预发布（可选） | `docs/m10-report.md`（新）、`CHANGELOG.md`（alpha1 节） | 全部 | 1.5–2 |

**W1 关键路径**：T1→T2→T3→T6→T7→T9→T12（≈10.75 天）+ T4/T5 并行（≈6.5 天）+ T8 并行 → 2–3 个并行代理下 **约 4 周**。

### W2（参数面）

| 任务 | 写作用域 | 依赖 | 人日 |
|---|---|---|---|
| **W2-T1** HEIF grid/tile 配置面（C16/C17）：新目录行 + 自动语义 + tile 推导/校验 + Level 上移核心 + UI note + 矩阵扩展 | `src/codecs/heif_capability.*`、`src/codecs/enc_heif.cpp`、`src/codecs/param_catalog/x265_catalog.inc`、`tools/param_catalog_specs/x265.json`、`tools/gen_param_catalog.py`、`src/core/param_rules.cpp`、`tests/unit/test_heif_capability.cpp`、`tools/heif_matrix_check.py` | W1 完成（事件/UI 稳定） | 4–4.5 |
| **W2-T2** profile 补齐（C18/C19）：枚举 16–21、SPS PTL 解析、编码后断言、UI 选项/限幅/hint、下拉溢出修法（与 T1 同文件串行） | `src/codecs/heif_profile.h`、`src/codecs/enc_heif.cpp`、`src/codecs/heif_capability.*`、`src/ui/page_output_impl.h`、`src/ui/page_output.cpp`、`src/ui/paramform.cpp`（下拉）、`tests/unit/test_heif_profile.cpp`、`tests/unit/test_heif_capability.cpp` | T1 | 3–3.5 |
| **W2-T3** tune 全量（C20）：HEIF animation 仿真/置灰、AVIF-libaom 通道、SVT 标签、事实表+一致性探针 | `src/codecs/enc_heif.cpp`（tune 段）、`src/core/enc_common.*`（事实表）、`src/ui/paramform.cpp`（置灰 choice）、`tests/unit/test_heif_tune.cpp`（新）、`docs/param-catalog.md` | T2（同文件串行） | 2.5–3 |
| **W2-T4** RC 数值行上移（C21）：核心区 host、动态域、QP+Level 错误、CRF+Level 告警、AVIF 提示 | `src/ui/paramform.cpp`、`src/core/param_rules.cpp`、`src/ui/smoke_probes.cpp`（⑤段扩展）、`tests/unit/test_param_rules*` | 可与 T1 并行（不同文件） | 1 |
| **W2-T5** JXL 治理（C22）：三态/默认值、删除/只读/危险分组、作用域修复、文档同步、回归归因+重播种（A15） | `src/core/format_tables.cpp`、`src/codecs/enc_jxl.cpp`、`src/codecs/param_catalog/jxl_catalog.inc`、`docs/param-catalog.md`、`docs/m8-param-control-map.md`、`tools/baseline/*`（重播种） | 可与 T1–T3 并行（不同文件；回归重播种最后做） | 2.5 |
| **W2-T6** catalog/矩阵/单测/文档收口 | `src/codecs/param_catalog/*`、`tests/unit/test_param_catalog.cpp`、`docs/param-catalog.md`、`docs/m10-adjudications.md` | T1–T5 | 1.5 |
| **W2-T7** W2 集成、门禁、矩阵跑批、重播种证据、0.4.0.alpha2（可选） | `docs/m10-report.md`、`CHANGELOG.md`、`build/matrix/`（gitignored） | 全部 | 1.5–2 |

**W2 关键路径**：T1→T2→T3→T6→T7（≈12 天）；T4/T5 并行 → 2–3 代理下 **约 2.5 周**。

### W3（格式预设 + 自适应）

| 任务 | 写作用域 | 依赖 | 人日 |
|---|---|---|---|
| **W3-T1** 格式预设 core+io（C23）：`format_preset.{h,cpp}`、`format_preset_io.{h,cpp}`、JSON schema、校验链、单测 | `src/core/format_preset.*`（新）、`src/ui/format_preset_io.*`（新）、`docs/schemas/format-preset.v1.schema.json`（新）、`tests/unit/test_format_presets.cpp`（新） | W2 完成（参数面定稿） | 2 |
| **W3-T2** PageOutput UI：每格式页签行、保存/另存/删除/导入/导出、损坏提示、只读自适应摘要+开关；MainWindow 接线 | `src/ui/page_output.*`（A9 加性）、`src/ui/output_page_build.cpp`、`src/ui/mainwindow_presets.cpp`、`src/ui/page_output_impl.h`、`src/ui/smoke_probes.cpp` | T1 | 2.5 |
| **W3-T3** 自适应核心（C24）：DSL 解析/渲染/校验、AST、允许/禁止注册表、默认策略库、单测 | `src/core/adaptive.*`（新）、`docs/schemas/adaptive-policy.v1.schema.json`（新）、`tests/unit/test_adaptive_policy.cpp`（新） | 可与 T1 并行（新文件） | 3 |
| **W3-T4** 自适应执行器 + pipeline 集成：候选临时文件、轮循环、同步评分、进度/取消、日志/侧车、假编码器单测、dev 端到端 | `src/core/adaptive_exec.*`（新）、`src/core/pipeline.cpp`（encode_loop）、`src/core/pipeline.h`（A3）、`src/dev_cli.cpp`（A14）、`tests/unit/test_adaptive_exec.cpp`（新）、`tests/golden/SCHEMA.md`（A13）、`tests/golden/smoke-adaptive/`（新，不进 20 对） | T3 + W1-T7（事件） | 3.5–4 |
| **W3-T5** 预设↔自适应集成 + 内置策略库打包：`adaptive.enabled/policy` 往返、只读摘要、`share/presets/formats/**` 出厂文件、安装拷贝 | `src/ui/format_preset_io.*`、`src/ui/page_output.cpp`、`share/presets/**`（新）、`cmake`/安装脚本（仅数据文件登记） | T1/T2/T4 | 1–1.5 |
| **W3-T6** 文档/裁定/DSL 手册：`docs/param-catalog.md` 收口、`docs/adaptive-policy.md`（新，人类可读写法+示例+JSON schema 说明）、`docs/m10-adjudications.md`、`docs/m10-report.md` | `docs/**` | T1–T5 | 1.5 |
| **W3-T7** W3 集成、全门禁、0.4.0 收口、报告、发行准备 | `docs/m10-report.md`、`CHANGELOG.md`、`README.md`（用户说明增量）、`docs/m10-adjudications.md` | 全部 | 2 |

**W3 关键路径**：T1→T2（UI）与 T3→T4 并行 → T5 → T6/T7（≈8 天 + 集成）；2–3 代理下 **约 2.5–3 周**。

---

## 3. 每任务 Definition of Done（统一口径）

1. 代码实现 + 单测（新能力必须有机械化探针；UI 类落 `--ui-smoke` 探针，不走 ctest UI 测试）。
2. 该模块门禁：`ctest`（受影响单测）、`bash tools/ci-format.sh` 0 diff、`python tools/env.py run -- python tools/ci-tidy.py build/release-dev` 棘轮不增。
3. 行为类任务：金样 20/20、`--dev` 相关证据、`--ui-smoke` 冻结末行。
4. 产物类任务：回归 zero diff（或按 A15 归因+重播种）、默认产物 SHA 对比（HEIF 零变化）。
5. 证据：命令 + 数字 + 证据路径写进 `docs/m10-report.md` 对应章节；临时探针放 `build/m10/<task-id>/`（gitignored）。
6. 提交：`M10-Txx: <一句话>`，正文含根因/验证命令/结果/复现路径；冻结面改动在提交说明与 `docs/m10-adjudications.md` 双登记。

---

## 4. 冻结面追认时序（不可跳过）

| 时机 | 动作 |
|---|---|
| W0 | `docs/m10-adjudications.md` 建文件，预登记 A1–A16；用户确认 |
| W1-T1/T3/T6/T7 | A1（FileState）、A2（FileEvent::output）、A3（adaptive_policy/max_parallel_targets）、A4（FileEntry::probe）、A5（ProgressInfo 字段）、A7（E3 数值语义）逐项落地并登记 |
| W1-T8/T10 | A9（page_output 加性）、A11（objectName 收缩）、A12（探针换锚/截图重生成） |
| W2-T5 | A15（回归重播种）——**先归因后重播**；单独提交 |
| W3-T1/T4 | A13（SCHEMA 加性）、A14（--dev CLI 加性） |
| 收口 | A16（文档真源同步）；报告「偏差与追认」章节逐条对照 |

---

## 5. 门禁矩阵（每波出口）

| 门禁 | 命令（皆经 `python tools/env.py run --`） | W1 | W2 | W3 |
|---|---|---|---|---|
| 构建 release + release-dev | `cmake --build --preset release` / `build build/release-dev` | ✅ | ✅ | ✅ |
| 单测 | `ctest --preset release`（或 `--test-dir build/release-dev`） | ✅ | ✅ | ✅ |
| 金样 | `python tests/golden/smoke.py build/release-dev`（`SMOKE total=20 pass=20 fail=0`） | ✅ | ✅ | ✅ |
| 回归 | `python tools/regression.py`（W1/W2 除 JXL 归因批外 zero diff；W3 zero diff） | ✅ | ✅（A15 归因） | ✅ |
| UI | `python tests/ui_smoke.py build/release-dev`（`UI-SMOKE OK shots=12 pages=3`） | ✅（换锚/新探针） | ✅ | ✅ |
| 格式 | `bash tools/ci-format.sh` | ✅ | ✅ | ✅ |
| 静态 | `python tools/ci-tidy.py build/release-dev` | ✅ | ✅ | ✅ |
| 性能探针 | `python tools/perf_smoke.py`（dev-only；W1 起） | ✅ SLO | ✅ | ✅ |
| 进度审计 | `python tools/pp_progress_audit.py`（dev-only） | ✅ | ✅ | ✅ |
| HEIF 矩阵 | `python tools/heif_matrix_check.py --phase m10`（W2 起） | — | ✅ 0 unexpected | ✅ |
| 密钥扫描 | `docs/m6-report.md` §6 命令 | ✅ | ✅ | ✅ |

---

## 6. 版本与发行计划

| 版本 | 内容 | 时机 | 发行纪律 |
|---|---|---|---|
| 0.4.0.alpha1（可选） | W1 全部 | W1 出口 + CI 绿 | tag 指向发行树；报告类提交不入 tag；版本单源 `CMakeLists.txt`+`PP_VERSION_LABEL` |
| 0.4.0.alpha2（可选） | W2 全部（JXL 重播种已完成） | W2 出口 + CI 绿 | 同上 |
| 0.4.0 正式 | W3 全部 + 双平台门禁 + 发行报告 | W3 收口 | 沿用 release.yml 流程；CHANGELOG/README/报告同步 |

> 版本号口径：`project(PhotoPipeline VERSION 0.4.0)` + `PP_VERSION_LABEL ".alphaN"`；`vcpkg.json` 数字段同步；任何其它出现都是引用。
>
> **实际（2026-11-01）**：**`0.4.0.alpha1` 进入发行**（版本单源已升 `0.4.0` + `.alpha1`；本地 release 构建 + winzip 打包完成，tag/CI Release 待执行）；`0.4.0.alpha2` 未发；收口门禁/报告已完成，
> 发行动作（版本升级 / CHANGELOG / README / tag / git 提交）**待用户确认** —— 见 `docs/m10-report.md` §6 与本文 §11–§12。

---

## 7. 风险与回退（与设计 §14 对应）

| 风险 | 触发点 | 回退动作 |
|---|---|---|
| 指标流式化 oracle 不达标（\|Δ\|>0.01） | W1-T4 | 回退"物化 + 独立评分池 + 超大图 Unavailable"过渡；P0 仍修复；登记偏差 |
| 多格式并行内存超限/OOM | W1-T6 | 默认并行度降为 1（等价串行）或按飞行预算回退；保留 B1 代码但默认关 |
| E3 公式导致回归日志 diff 不可控 | W1-T3/T6 | 保留旧公式（W_eff 仅用于 B1 预算判断，不改 E）；登记 |
| 异步评分影响 `--dev` 证据稳定性 | W1-T5/T7 | dev_cli 等池收敛 + 超时错误；必要时 dev 模式强制同步评分（GUI 异步） |
| UI 探针换锚引发评审争议 | W1-T10 | 逐条在裁定表登记"语义变化"；不含任何"放宽断言"；Lead 复核 diff |
| 规范命名无法复核（R-2） | W2-T2 | 采用"信号+待复核官方名"双写；不会阻塞编码能力；报告如实登记 |
| JXL 重播种把真实回归掩盖 | W2-T5 | 重播种前逐项归因（默认值/三态/作用域），归因写进提交正文与报告；只对 JXL 有损产物重播 |
| 周期超预期 | 任意 | 裁剪档（§1）后置 P2 项；波次可提前发布 alpha 增量 |

---

## 8. 开工前需用户确认的清单（阻塞项）

1. **设计文档确认**：`docs/v0.4.0-design.md` 全文（尤其 §0.3 SLO、§10 追认表、§13 预算）。
2. **冻结面加性追认**：A1–A16（尤其 A2 `FileEvent::output` shared_ptr、A11 objectName 收缩、A12 探针换锚）。
3. **JXL 回归重播种**（A15）：确认"有意产物漂移"范围与归因要求。
4. **并行 B1 默认并行度**：2（默认）/1（保守）/可配置（需新增设置项，本设计默认不暴露）。
5. **性能 SLO 数值**（§0.3）：是否按该口径验收；若需更激进/更保守请给出目标。
6. **0.4.0.alpha1/alpha2 预发布**：是否按波次发布（否则只在 0.4.0 收口一次发行）。
7. **规范命名复核人**（R-2）：是否有可访问 ITU H.265 文档的复核渠道；没有则按"待复核"双写发布并在报告登记未决。

---

## 9. 证据与复现（本计划引用的既有事实源）

| 内容 | 路径（本机，gitignored） |
|---|---|
| 并行/输出链路/进度侦察 | `build/design-recon/pipeline/report.md`（+ `results/summary-table.md`、`out/**/logs`、`progress-trace.jsonl`） |
| HEIF/AVIF 参数面侦察 | `build/design-recon/heif/report.md`（+ `experiments*.json`、`introspect.json`、`reparse_sps.py`） |
| JXL 参数/预设/自适应侦察 | `build/design-recon/jxl/report.md`（+ `bench/` 54 例） |
| 运行页侦察 | `build/design-recon/ui/report.md`（+ `shots/`、`shots-long/`、`ui-timeline.txt`、`trace-analysis.txt`） |
| 设计 | `docs/v0.4.0-design.md`（本计划对应） |
| 裁定登记（开工后） | `docs/m10-adjudications.md` |
| 收口报告（开工后） | `docs/m10-report.md` |

---

*本计划与 `docs/v0.4.0-design.md` 同时交付；用户确认后按 W0 启动，任何范围/契约变更先改设计文档再改任务卡。*

---

## 10. 任务卡最终状态（M10 收口，2026-11-01）

> 口径：**完成** = 有代码/文档 + 机械化探针 + 报告；**部分** = 主体已交付但有明确未做项（逐条写明）；**未做** = 无交付。
> 证据路径列**报告原件优先**，构建树已清理者用 `build/m10/_evidence/<task>__report.md`。

### 10.1 W1（性能与运行页）

| 任务卡 | 状态 | 未做/差异 | 证据 |
|---|---|---|---|
| W1-T1 预算拆分 + `stagger_ms=0` | 完成 | — | `_evidence/w1-engine-a__report.md` |
| W1-T2 按带 lcms + 日志限流 | 完成 | 按带**无可测提速**（D-1），真提速在 T2c | 同上 |
| W1-T2b 无手术快路径 | 完成 | 仅"源无元数据"安全子集（D-2） | 同上 |
| W1-T2c 评分色彩归一快速内核 | 完成 | — | `_evidence/w1-fastcolor__report.md` |
| W1-T3 双重 probe 消除 | 完成 | — | `_evidence/w1-engine-a__report.md` |
| W1-T4 指标流式化 | 完成 | — | `_evidence/w1-metric__report.md` |
| W1-T5 评分池 + 异步评分 | 完成 | — | `_evidence/w1-engine-b__report.md` |
| W1-T6 多格式并行 | 完成 | — | 同上 |
| W1-T7 逐输出事件 | 完成 | — | 同上 |
| W1-T8 运行页布局 | 完成 | — | `_evidence/w1-ui__report.md` |
| W1-T9 状态/刷新（T9a + T9b） | 完成 | — | `_evidence/{w1-ui__report-status,ui-t9b__report}.md` |
| W1-T10 spec/mockup/探针换锚 | 完成 | 布局面；HEIF/JXL 参数行探针面由 UI-ADAPT 承接 | `_evidence/w1-ui__report.md` |
| W1-T11 机械探针工具 | 完成 | — | `_evidence/w1-tools__report.md` |
| W1-T12 W1 集成/门禁/报告 | **部分** | 门禁由 FINAL-GATE 统一重跑；**TSan 未跑**；报告增量并入 `m10-report` | `build/m10/final-gate/report.md` |
| W1-SCOREFIX（追加卡） | 完成 | `pipeline.cpp` 损坏恢复由 FINAL-GATE §H 复核 | `build/m10/engine-scorefix/report.md` |

### 10.2 W2（参数面）

| 任务卡 | 状态 | 未做/差异 | 证据 |
|---|---|---|---|
| W2-T1 HEIF grid/tile + Level 核心区 | 完成 | grid 单边/乘积上限当时未进目录（FINAL-REPORT 已补：`m10-report` §3.5） | `_evidence/w2-heif-grid__report.md` |
| W2-T2 profile 16–21 + SPS PTL + C19 | 完成 | 16 实测不可用（如实置灰）；R-2 规范命名待外部复核 | `_evidence/w2-profile__report.md` |
| W2-T3 tune 全量 | 完成 | — | `_evidence/w2-tune-rc__report.md` |
| W2-T4 RC 数值行 + 动态域 + QP/Level | 完成 | — | 同上 |
| W2-T5 JXL 治理 43→31 | 完成 | A15 已本地重播种（task-28/A15-CLOSE：N11/N12 + zero diff×2；CI 侧待复核） | `_evidence/w2-jxl__report.md`、`build/m10/a15-close/report.md` |
| W2-T6 catalog/矩阵/单测/文档收口 | **完成** | 计数/文档由 AUDIT-1 + W3-T6 + FINAL-REPORT 收口；HEIF 全量矩阵已跑（1102 例 0 unexpected） | `_evidence/audit__report.md`、`build/m10/heif-full/report.md` |
| W2-T7 W2 集成/矩阵全量/重播种/alpha2 | **完成** | 集成门禁 FINAL-GATE 重跑；HEIF 全量矩阵复跑（0.4.0.alpha1 二进制，见 `build/m10/heif-full/`）；A15 已本地重播种；alpha2 未发（alpha1 发行中） | `build/m10/final-gate/report.md`、`build/m10/heif-full/` |
| AUDIT-1（追加卡） | 完成 | — | `_evidence/audit__report.md` |
| UI-ADAPT（追加卡） | 完成 | — | `_evidence/ui-adapt__report.md` |

### 10.3 W3（格式预设 + 自适应 + 文档）

| 任务卡 | 状态 | 未做/差异 | 证据 |
|---|---|---|---|
| W3-T1 格式预设 core + io | 完成 | `format_preset_io.cpp` 未进 `pp_core`（登记，待 Lead 决策） | `build/m10/w3-core/report.md` |
| W3-T2 PageOutput 格式预设行 + 只读自适应面 | 完成 | 保存/删除的模态确认框仍属人工走查 | `build/m10/w3-ui/report.md` |
| W3-T3 自适应 core（DSL/42 错误码/5 默认库） | 完成 | — | `build/m10/w3-core/report.md` |
| W3-T4 执行器 + pipeline 集成 | 完成 | heif/avif 策略 round1 即达标（后续 stage 未执行到，单测覆盖） | `build/m10/w3-exec/report.md` |
| W3-T5 预设↔自适应集成 + 内置库 | 完成 | **`share/presets` 出厂文件/安装规则按 D-8 取消**，改运行时幂等 seed | `build/m10/w3-preset/report.md` |
| W3-T6 文档（自适应手册/格式预设手册/报告骨架/目录校核） | 完成 | 目录三缺口当时只登记（FINAL-REPORT 已补：`m10-report` §3.5） | `build/m10/w3-docs/report.md` |
| W3-T7 W3 集成/全门禁/收口/发行准备 | **完成** | 版本单源/CHANGELOG/README/AGENTS/发行报告已做；本地 release 构建 + winzip 打包完成（`dist/PhotoPipeline-0.4.0.alpha1-win64.zip`）；git 六提交 + tag/CI 发行待执行 | `build/m10/final-gate/report.md`、`build/m10/tidy-fix/report.md`、`docs/0.4.0.alpha1-report.md` |

### 10.4 收口三连（本阶段追加）

| 任务 | 状态 | 结果摘要 | 报告 |
|---|---|---|---|
| FINAL-GATE（task-25） | 完成 | A14 补丁 + 干净树 481 步 + 五道门禁（tidy 转下卡）+ HEIF 233 例 0 unexpected + 性能/事件 + 回归归因 + `pipeline.cpp` 复核 | `build/m10/final-gate/report.md` |
| FINAL-TIDY（task-26） | 完成 | 解析失败修复 + 132 条收敛（48 代码 + 84 NOLINT）+ tidy `262=262 violations=0` + 重建 157 步 + 四门禁复跑 | `build/m10/tidy-fix/report.md` |
| FINAL-REPORT（task-27） | 完成 | `m10-report` 定稿 + 本文状态/门禁/预算 + `param-catalog` 3 缺口 + 两份手册漂移修正 + 发行清单 | `build/m10/final-report/report.md` |

---

## 11. 最终门禁记录（FINAL-GATE / FINAL-TIDY / FINAL-REPORT）

> 全部在**同一棵最终集成树** `build/m10/final` 上实测；与 §5 的"计划门禁矩阵"对照读。
> 二进制：`build/m10/final/photopipeline.exe` = 5,088,768 B / SHA256 `E25F4F29…B49F4731`（tidy 修复后重建；tidy 前 `A2675B61…`）。

| 门禁 | 命令（经 `python tools/env.py run --`） | 实测结果 | 证据 |
|---|---|---|---|
| 干净树 configure | `cmake --preset release-dev -B build/m10/final -DVCPKG_MANIFEST_INSTALL=OFF` | EXIT=0 | `build/m10/final-build.log` |
| 干净树 build | `cmake --build build/m10/final -j 8` | EXIT=0（**481 步**） | 同上 |
| 重建（tidy 修复后） | `cmake --build build/m10/final -j 8` | EXIT=0（**157 步**增量） | `build/m10/tidy-fix/build.log` |
| 单测 | `ctest --test-dir build/m10/final --output-on-failure` | **48/48**（75.75 s；修复后复跑 76.45 s） | `final-ctest.log`、`tidy-fix/ctest.log` |
| 金样 | `python tests/golden/smoke.py build/m10/final` | **`SMOKE total=20 pass=20 fail=0`** | `final-golden.log`、`tidy-fix/golden.log` |
| UI 冻结末行 | `photopipeline.exe --ui-smoke --inputs tests/golden/base --shots …` | 末行逐字 **`UI-SMOKE OK shots=12 pages=3`**；format-preset `checked=23 bad=0` | `final-ui-smoke.log` |
| UI 脚本 | `python tests/ui_smoke.py build/m10/final` | **`UI-SMOKE pass`** | `final-ui-smoke-py.log` |
| 格式 | 钉版 clang-format 23.1.1 `--dry-run -Werror --style=file`（233 文件） | **FILES=233 RC=0 VIOLATIONS=0**（FINAL-REPORT 复跑同结果） | `final-fmt-files.txt`、`tidy-fix/format.log`、`final-report/fmt-recheck.log` |
| 静态分析 | `python tools/ci-tidy.py build/m10/final --jobs 12` | **`TIDY total=262 baseline=262 files=76 violations=0 seed=0`**，EXIT=0；140/140 TU；**未 `--update`** | `tidy-fix/ci-tidy.utf8.log`、`warnings.tsv` |
| 回归 | `python tools/regression.py build/m10/final` | **DIFF FOUND（EXIT=1）**：产物层仅 JXL 有损 **23/23** 变化、非 JXL **0 diff**；日志面 = 加性日志 + runner 形状 + vcpkg 路径 ⇒ **未重播种**（D-9） | `final-gate/regression-1.log`、`regression-attrib.log` |
| HEIF 矩阵（4 子集） | `python tools/heif_matrix_check.py --phase final --set …` | **233 例 pass=59 / fail=0 / expected-fail=174 / unexpected=0**（598.5 s） | `final-gate/heif/matrix.log`、`final-subset.jsonl` |\n| HEIF 矩阵（全量） | `python tools/env.py run -- python tools/heif_matrix_check.py --phase final --binary build/m10/final/photopipeline.exe --timeout 900` | **1102 例 = pass 732 / fail 0 / expected-fail 370 / unexpected 0**（2566.0 s；默认 HEIF SHA 6/6 同基线） | `build/m10/heif-full/report.md`、`final-full.jsonl` |\n| FINAL-VERIFY（0.4.0.alpha1） | ctest/金样/ui-smoke/ui_smoke.py/tidy/regression/format/版本检查 | 全部 PASS：48/48、20/20、冻结末行、pass、262=262、**zero diff 2567=2567**、233 文件 RC=0、`PhotoPipeline 0.4.0.alpha1`；二进制 SHA `F3A0448A…` 前后未变 | `build/m10/final-verify/verify.log` 与分步日志 |
| 默认产物 SHA | 4 格式默认配置 | jpeg/webp/jxl **3/3** + HEIF **6/6** 与基线逐字节相同 | `m10-report` §2.1 |
| 性能/事件 | `perf_smoke.py`、`pp_progress_audit.py` | P4-1 wall ✅ / metric ❌ **1.072×**（D-5）；P4-4 ✅；P1-1 ✅（7 在飞）；P4-2 非 HEIC ✅ / HEIC ❌ 1 处；多格式并行 **12,719 vs 16,690 ms（1.31×）** 且四格式 SHA 并行==串行 | `final-gate/perf/analysis.log` |
| 密钥 | `docs/m6-report.md` §6 命令 | tracked/untracked **0 命中** | `final-gate/report.md` §I-3 |

**与计划门禁矩阵（§5）的差异（如实）**：

1. **静态分析**：计划期望"每波 ✅"，实际 M10 各任务均未跑；FINAL-GATE 首次在最终树跑 ⇒ **不合格**（1 TU 解析失败 +
   `total=389 > 262`、`violations=25`）；FINAL-TIDY 修复后达标（262=262、violations=0）。**教训**：每卡 DoD 的 tidy 项长期欠账，
   最后由独立卡偿还。
2. **回归**：计划写"W1/W2 除 JXL 归因批外 zero diff；W3 zero diff"；实际 **产物层**成立（仅 JXL 有损 23/23），
   但**日志面**因 runner 形状（基线 4 vCPU vs 本机 16 核）与 M10 加性日志不可能 zero diff ⇒ 门禁口径改为
   "产物层 zero diff（JXL 除外）+ 日志面逐条归因"，重播种按 D-9 交 CI `windows-2025`。
3. **HEIF 矩阵**：计划"W2 出口 0 unexpected"；实际 M10 已跑全量 **1102 例 = pass 732 / fail 0 / expected-fail 370 / unexpected 0**（0.4.0.alpha1 候选；与 M9 1013 例 verdict/hvcC 事实差异 0）。

---

## 12. 预算实际对账

> 计划口径（§1）：单人 **≈55–65 人日**；3–4 并行子代理 + Lead 集成 ≈ **8–10 个日历周**。
> **诚实前提**：执行日志（`docs/m10-execution-log.md`）只记任务回执，**没有工时记录** ⇒ 下表"实际"按
> **任务卡张数 + 墙钟 + 波次**对账，**不折算人日**（不编造未记录的工时）。

| 波次 | 计划人日 | 实际任务卡 | 墙钟（据回执时间戳） | 对账 |
|---|---|---|---|---|
| W1 | 24–30 | 15 张（T1/T2/T2b/T2c/T3/T4/T5/T6/T7/T8/T9/T10/T11/T12 + SCOREFIX） | 2026-10-31 → 11-01（约 1 天） | 14 张完成、T12 部分（TSan 未跑，门禁并入 FINAL-GATE） |
| W2 | 16–18 | 9 张（T1–T7 + AUDIT-1 + UI-ADAPT） | 2026-10-31 → 11-01 | 全部完成：T5 A15 已重播种（task-28）；T7 全量矩阵复跑 + alpha1 发行准备完成 |
| W3 | 15–17 | 7 张（T1–T7） | 2026-11-01 | 全完成（T5 按 D-8 改 seed；T7 本地发行构建/打包完成，tag/CI 待执行） |
| 收口三连 | （计划外，追加） | 3 张（FINAL-GATE / FINAL-TIDY / FINAL-REPORT） | 2026-11-01（约 3 小时） | 全完成；新增 1 个门禁子代理 + 1 个修复子代理 + 1 个报告子代理 |
| **合计** | **≈55–65 人日** | **34 张 = 28 计划卡 + 6 追加卡**（SCOREFIX / AUDIT-1 / UI-ADAPT / FINAL-GATE / FINAL-TIDY / FINAL-REPORT） | **≈2 个日历天** | 裁剪档**未启用**；P2 项（`m10-report` §6.5 清单）未做；本地发行动作完成（tag/CI Release 待执行） |

**预算结论（如实）**：

1. **未超预算但也不可宣称"省了 X 人日"**：任务卡张数与计划基本一致（28 张计划卡全部开卡 + 6 张追加卡），差异主要在并行度
   （3–4 子代理 + 收口串行三连），墙钟 ≈2 天不能换算成人日。
2. **计划外的成本**：① SCOREFIX（task-20，评分池取消/所有权缺口）；② FINAL-TIDY（tidy 欠账集中偿还，132 条）；
   ③ 两次磁盘告急导致的构建树重建/报告抢救；④ `pipeline.cpp` 损坏恢复与专项复核。这些不在 55–65 人日估算内。
3. **被裁掉/推迟的**：全量 HEIF 矩阵、A15 重播种（D-9）、TSan、`output best`、`tech` 切换、指标核心按带并行
   —— 均进 P2/发行前遗留（见 `m10-report` §4/§6.5），**未以"看起来完成"收场**。
