# M10 裁定登记（pre-registration · 随实现逐条追加证据）

> 依据：`docs/v0.4.0-design.md` §10（A1–A18）；AGENTS.md §5.1「加性 + 追认」。
> 纪律：本文件是**滚动台账**：每条裁定在落地提交时补"落地任务/提交/证据"三列；不得删行、不得改已落地裁定的性质。
> 状态口径：`预登记`（设计已定、未落地）→ `已落地`（代码+测试+提交）→ `已追认`（用户/主对话书面确认，发行前统一复核）。

| # | 冻结面 | 改动 | 性质 | 归属任务 | 状态 | 落地证据（提交/命令/路径） |
|---|---|---|---|---|---|---|
| A1 | `core/pipeline.h`（设计误写 types.h；实现期实测修正） | `FileState` 末尾追加 `OutputDone` | 加性枚举值 | W1-T7 | 已落地 | `build/m10/w1-engine-b/report.md`：trace 首条 OutputDone 11/11 早于文件终态、0 违规；跨文件编译依赖 `src/ui/filelistmodel_util.h` 加性 1 行映射+断言 14→15（待 UI 属主复核） |
| A2 | `core/pipeline.h` | `FileEvent` 末尾追加 `std::shared_ptr<const OutputResult> output` | 加性字段 | W1-T7 | 已落地 | `build/m10/w1-engine-b/report.md`：3 条真实 trace 事件顺序 0 违规、trace 加性字段含 output 对象 |
| A3 | `core/pipeline.h` | `OutputFormatSpec` 末尾追加 `adaptive_policy`；`RunConfig` 末尾追加 `max_parallel_targets` | 加性字段 | W1-T6 / W3-T4 | 已落地 | `max_parallel_targets`（W1-T6，dev-only A/B）；`adaptive_policy`（W3-T4，`build/m10/w3-exec/report.md`：pipeline.h 末尾字段→PlannedTarget→run_target 空串分派，非自适应路径逐字节不变） |
| A4 | `core/pipeline.h` | `FileEntry` 末尾追加 `ProbeOutcome probe`（完整 probe 缓存） | 加性字段 | W1-T3 | 已落地 | task-5：HEIC 首 Probing→Decoding 2902.9→1447.8ms；`build/m10/w1-engine-a/report.md` |
| A5 | `core/progress.h` | `ProgressInfo` 末尾追加 `heartbeat/scoring/adaptive_round/adaptive_max` | 加性字段 | W1-T7 | 已落地 | `build/m10/w1-engine-b/report.md`：心跳/评分/自适应字段进 trace；`pp_progress_audit.py` 4/4 PASS、max_gap 181.6–425.0ms |
| A6 | `core/settings.h` | 末尾追加评分池配置字段（若本版采用内部常量则本条降级为"不改"） | 加性字段 | W1-T5 | 已落地（降级） | 评分池 K=4/独立配额 4GiB 走内部常量，`settings.h` 零改动（符合 A6 降级条款） |
| A7 | `core/scheduler.h` | 无签名改动；`[sched] thread alloc`/`e3 report` 数值语义随 `W_eff` 变化 | 数值语义变化 | W1 引擎 | 未落地（M10 不实施 E3 改造） | M10 未做 E3/W_eff 重构；实际并发由像素预算+评分池决定（见 w1-engine-b/report.md）。若后续实施 E3 需按原契约登记重播种 |
| A8 | `core/presets.h` | 零改动（格式预设用新文件） | — | W3 | 已落地（零改动） | `core/presets.h` 零 diff；格式预设/自适应由新文件（format_preset/adaptive/preset_io）承载 |
| A9 | `ui/page_output.h` | 末尾加性方法/信号（格式预设） | 加性 | W3-T2 | 已落地 | `build/m10/w3-ui/report.md`：7 信号 + `public:` 续段 + 8 方法，仅末尾加性、未新增 include、既有签名零改动 |
| A10 | `ui/page_run.h` | `RunPlan` 末尾加性 `quality_score`（可选） | 加性 | W1-T9 | 已落地 | `build/m10/ui-t9b/report.md`：`RunPlan::quality_score=false` + 末尾加性 `scoring_enabled()`；既有签名/语义零改动 |
| A11 | UI objectName | 删除 `pp-run-speed-*`/`pp-run-gspeed-*`；新增 `pp-format-preset-*`/`pp-adaptive-*`/布局探针钩子 | 收缩 + 只增 | W1-T8/W1-T10 | 已落地 | 速度列族删除（task-7）；`pp-format-preset-*`/自适应只读面 W3-T2 落地（build/m10/w3-ui/report.md）；布局钩子 fit/name_w/no-speed/ratio-tip 等已落地 |
| A12 | `--ui-smoke` | 换锚 4 处既有断言；新增探针只增；12 张截图重生成、冻结末行逐字不变 | 换锚 + 只增 | W1-T10 | 部分落地 | 布局/状态/jxl 删行/HEIF 档位/RC/tune/格式预设探针已落地（task-7/9/17/21 + UI-ADAPT）；T9b 待补清单（score-immediate/facts-immediate/run-status-map 扩展等）由 W3-T7 集成补；冻结末行各次实测逐字保持 |
| A13 | `tests/golden/SCHEMA.md` | 加性追加 `adaptive` 侧车节 + 新事件字段说明 | 加性 | W3-T4 | 已落地 | `build/m10/w3-exec/report.md`：顶层 `adaptive{policy_version,target,max_rounds,output,rounds[],selected_round,final_score,final_metric,reason,policy_sha[,error]}`；SCHEMA.md +74 行、旧断言零改动 |
| A14 | `--dev` CLI | 加性 `--adaptive-policy FILE`；`--preset` 复制 `adaptive_policy` | 加性 | W3-T4 | 已落地 | `build/m10/w3-exec/report.md`（`--adaptive-policy` 与格式预设路径）；`build/m10/w3-preset/report.md`（格式预设 adaptive E2E jpeg/webp/heif/avif）；`build/m10/final-gate/report.md`（全局预设分支加性 3 行补丁 E2E：侧车 adaptive reason=met/score=100；旧预设与补丁前二进制逐字节同 SHA） |
| A15 | 回归基线 | JXL 有损产物变化 → 归因后 `regression.py --update` 重播种 | 基线重播种 | W2-T5 | **已落地（本地）** | `build/m10/a15-close/report.md`：N11/N12/N6 归一 + 旧基线逐字节核对（280D2E38…224C）+ `--update` 护栏（107 removed/150 added，unmatched=0）+ zero diff ×2；golden.windows.log 2534→2567 行 / SHA 4FC72B33…6A49 / app version 0.4.0.alpha1。剩余：CI windows-2025 与 Linux golden.log 待 nightly workflow_dispatch 复核 |
| A16 | 文档真源 | `param-catalog.md`、`qt-gui-design-spec.md` §7.3、`mockups-v2/run.html`、`m8-param-control-map.md` | 文档 | W1/W2 | 部分落地 | 已收口：W1 UI spec/mockup、W2 计数漂移与 param-catalog（含 3 处登记缺口）、W3 新增 adaptive-policy/format-presets/m10-report/m10-workflow；**README/CHANGELOG/版本号待发行确认后同步** |
| A17 | `core/settings.h` | 默认 `stagger_ms` 150→0（行为变化，无产物影响） | 默认值变更 | W1-T1 | 已落地 | task-5：`settings.h` 默认 0；GUI 默认生效、`--dev` 轨迹零变化（RunConfig 冻结默认未动）；`build/m10/w1-engine-a/report.md` |
| **A18** | JXL 参数面（`format_tables`/`jxl_catalog`/`enc_jxl`） | 43→31 键：删 12 个结构性 no-op（keep_invisible/noise/expert_options/uses_original_profile/group_order+center_x/y/progressive_ac/qprogressive_ac/progressive_dc/responsive/buffering）；`epf` 默认 2→-1；`gaborish/dots/patches` 改三态；`codestream_level` 只读；`color_transform` 收窄 auto/YCoCg；channel_colors 类型 Float | 产品参数面行为变化（非冻结结构；有产物漂移） | W2-T5 | 已落地 | `build/m10/w2-jxl/report.md`；JXL 有损产物变化归因见 A15 |
| **A19** | `core/pipeline.h`（`OutputResult`） | 备用：自适应摘要如需 GUI 运行页展示，加性追加 `OutputResult::adaptive` 字段；当前实现走 dev-only 进程级发布表（publish/published/clear_adaptive_summary，先例 `ScorePool::published`），未改 OutputResult | 加性字段（可延后） | W3-T4 / W3-T7 | 预登记（可延后） | 决策：运行页自适应轮次已由 `ProgressInfo.adaptive_round/adaptive_max` 承载，暂不需要 OutputResult 字段；若发行前需"自适应摘要列"再落地 |
| **A20** | `core/pipeline.h`（`OutputFormatSpec`） | 末尾加性追加 `bool adaptive_enabled = false`（与 `adaptive_policy` 配套，全局预设/运行配置承载启用状态；默认 false=旧行为） | 加性字段 | W3-T5 | 已落地 | `build/m10/w3-preset/report.md`：唯一加性追加于结构体末尾；引擎判据仍为 `adaptive_policy` 非空（W3-T4 零改动）；GUI 在 `config_base()` 按本字段门控下发 |

## 复核触发点

1. 每条落地前：实现任务在提交信息引用本行编号；落地后补"已落地"与证据路径。
2. W1 收口：A1–A7、A10–A12、A17 复核；W2 收口：A15/A16 复核；W3 收口：A3/A8/A9/A13/A14 复核。
3. 任何"收缩/换锚/重播种"类裁定（A11/A12/A15）在发行前由用户/主对话书面复核一次。
