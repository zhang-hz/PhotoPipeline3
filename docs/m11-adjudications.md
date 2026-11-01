# M11 裁定记录（0.4.0.alpha2-dev 迭代修复批）

> **体例**：`# | 裁定 | 理由 | 实施归属`。每条都是**开发期裁定**；发行动作（版本号/CHANGELOG/tag）待用户确认。
> 基线 HEAD `26d3554`；集成树 `build/iter1`；契约真源见各条指针。
> 本批**未改动任何 PP-FROZEN 冻结接口**；下列改动全部属「加性 / 非冻结面 / 消费端行为修正」，逐条登记。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| A1 | **格式预设 / 全局预设的 `adaptive.policy` 持久化由「单个字符串」改为「JSON 字符串数组（一行一条）」**；读出兼容旧字符串（`\n`/`\r\n` 拆行）；schema 改 `oneOf [array<string>, string]` | 需求 3：用户原话「DSL 没有可阅读性，全是 \n」。JSON 字符串天然要转义换行；改数组后预设文件逐行可读、可 diff、零 `\n`。兼容读保证旧文件不破；`save→load→save` 字节稳定由单测钉住。属**非冻结** IO/序列化面（`FormatPresetData.adaptive_policy` 内部仍是 `std::string`，未改内存契约） | task-2（adaptive-dsl）：`ui/format_preset_io.cpp`、`ui/preset_io.cpp`、schemas；测试 `test_format_presets` / `test_presets` |
| A2 | **HEIF grid tile 自动推导（C17）语义变更**：删除 1024×576 固定基准，改为「目标 Level 的 MaxLumaPs/单边上限 + still 单图应用格式 Level 上界（3GPP TS 26.140/26.141 = L5.1）」推导；新增 `planning_level_idc`/`still_level_capped`/warning；手动 tile 与默认（Level=自动+grid=自动→单图）行为不变 | 需求 2：用户原话「1024×576 是 L 几规定的尺寸？为什么还是固定值？我的要求是根据 level 和 still 规格制定」。旧口径把 Apple 参考当基准，不符合「按规格推导」。规范依据：ITU-T H.265 V9 一手核对否定「still profile 强制 ≤5.1」（A.4.2 只约束 video profiles），但 3GPP HEIF still 互操作档确有 L5.1 上界 → 用作**应用格式上界**并在文档标证据等级 | task-1（heif-spec）：`codecs/heif_capability.*`、`enc_heif.cpp`（+1 行 profile 接线 + warning 日志）、`param_rules.cpp`、`format_tables.cpp`、`tools/param_catalog_specs/x265.json` + 重生成 `.inc`、`tools/heif_matrix_check.py`、`pp_test_heif_capability`；文档 `docs/heif-tile-sizing.md`、design §6.1.6 |
| A3 | **自适应执行器 hooks / 记录结构加性扩展**：`AdaptiveRunHooks` 新增 `on_plan`/`on_stage_start`（默认空）；`AdaptiveRoundRecord` 末尾追加 `effective_params`/`candidate_path`；`AdaptiveRunResult` 末尾追加 `selected_params_text`/`reason_detail`；新增 `AdaptivePlanInfo`/`AdaptiveBreakpoint`/`AdaptiveDirection`；`AdaptiveAction`/`AdaptivePolicy` 末尾加性字段 | 需求 1（迭代过程写日志）+ 需求 7（断点）。`core/adaptive_exec.h` 与 `core/adaptive.h` **不在 PP-FROZEN 清单**；全部为末尾追加/新类型，旧调用点零改动 | task-2（adaptive-dsl）：`core/adaptive.*`、`core/adaptive_exec.*`；消费方 task-3（state-machine）：`core/pipeline.cpp` |
| A4 | **内置预设库结构加性扩展**：`FormatPresetData` 末尾追加 `builtin_file_stem`（每条内置独立 seed 文件名）与 `builtin_unavailable_reason`（内省不可用时显式原因，不写坏文件）；旧 5 条内置预设与其 `_builtin-<format>.json` 文件名逐字节不变；新增 5 条用 `_builtin-<stem>.json` | 需求 6：新增 5 条预设需要唯一 seed 文件名，同时显示名必须是干净名字（JSON `name` 字段）。旧文件不覆盖 ⇒ 老用户幂等 seed 行为不变 | task-2：`core/format_preset.{h,cpp}`、`ui/format_preset_io.cpp`（seed 命名） |
| A5 | **格式预设行 UI 文案/几何变更**：导入/导出按钮去省略号；下拉宽度 `FieldWidth::Flex → Standard`（160px，与同页参数行一致）；按钮固定 sizeHint、行尾 stretch 吸收富余；`--ui-smoke` 探针**只增**（preset-geom/preset-tip） | 需求 5：用户原话「导入和导出为什么要加...？下拉框宽度要和其他参数一致，同时不能出现文字截断」。设计 §8.5 的 flex 表述按此修订（`docs/format-presets.md` 同步） | A5 设计口径 = Lead；实现 Lead（`ui/output_page_build.cpp`），探针 task-3（`ui/smoke_probes.cpp`、`tests/ui_smoke.py`） |
| A6 | **状态机消费端行为修正（可观察行为变化）**：① 文件终态后到达的非终态过程事件不再改写行状态（`OutputDone` 仍可补评分）；② 结束态「已取消」按引擎 `sum.cancelled` 判定；③ 全局预设载入清「格式预设来源」；④ 预设来源消失（未选择/外部删除/删除）清自适应会话态 | 需求 4：用户原话「运行里面的状态 bug 仍然很多…随便用就是 bug」。四项均为用户可观察的错误状态，修复后 `--ui-smoke` 机械探针钉住（S-1…S-5） | task-3（state-machine）：`ui/page_run.cpp`、`ui/page_output.cpp`、`ui/mainwindow_presets.cpp`、`ui/page_run.h`（加性） |
| A7 | **文档勘误（实现未变）**：`set` 阶段语义 = 「同一 stage 内全部 set 落在同一轮参数、编一轮」；旧文档「每个 set = 1 轮」错误，全部修订 | 需求 3「语义混乱」：实测与旧文档矛盾（执行器一直是同轮合并），按实现单源修正文档 | task-2：`docs/adaptive-policy.md` §3/§6.2/§7/§15 |
| A8 | **DSL 语法加性扩展 + 错误码 42→45**：新增可选方向词 `down`/`up`、`breakpoint <v> step <s>` 重复子句；新增 `breakpoint_syntax`/`breakpoint_order`/`breakpoint_value`；旧语法逐字兼容；`parse(render(p))` 幂等 | 需求 7「上述步长断点注意进行特殊处理」+ 需求 3「语义混乱」。跨断点不跳过断点值、端点必试、只降不升是用户预设策略可执行的前提 | task-2：`core/adaptive.*`（parse/render/validate）、`docs/adaptive-policy.md` §4/§6.1/§16、schema |
| A9 | **参数目录文案修订走生成器单源**：`tools/param_catalog_specs/x265.json` 的 `heif_grid`/`heif_tile_w`/`heif_tile_h`/`x265:level-idc` 四行 reason/tooltip 按 C17' 改写，并重生成 `src/codecs/param_catalog/x265_catalog.inc`（diff 恰为这 4 行）；手改 `.inc` 不再作为路径 | 单源纪律：`.inc` 是生成产物；直接手改会被下次生成覆盖 | task-1：`tools/param_catalog_specs/x265.json`、`tools/gen_param_catalog.py` 运行、`.inc` |
| A10 | **既有 P3 状态问题登记不修**（S-6…S-12） | 影响面/口径属产品决策（迟到评分回填、未知 output_index 静默丢弃、取消分支 ProgressMux 无迁移点、合成进度、异常路径计数、重复载入注记、混合批 format_id 行）——逐条理由见 `docs/state-machine-analysis.md` §缺陷台账 | task-3：`docs/state-machine-analysis.md` |
| A11 | **本轮不升版本号**（保持 `0.4.0.alpha1` 单源），CHANGELOG 新节以「未发行」形式记录；`tests/golden` 回归基线不重播种（默认产物零变化） | 用户未要求发行；版本/CHANGELOG/README 发行动作按 AGENTS §6.4 待确认 | Lead：`CHANGELOG.md`、`docs/m11-report.md` |
| A12 | **ui-smoke「崩溃」不修产品代码**：确认 `build/release-dev` 增量构建树对象/配置不一致导致的假象（同机旧二进制 + 干净树 `build/iter1` 均通过；三二进制同尺寸不同 SHA） | 证据优先：干净树复现不成立；强行改产品代码无根因 | task-3：`docs/state-machine-analysis.md` §6 |

---

## M12 加性勘误指针（0.4.0 正式版发行批；历史结论不改）

> 本文件是 **M11 时点**的裁定记录，表格正文**保持原样不改**。0.4.0 正式版（M12 批）取代/修订的条目如下，
> 详见 [docs/m12-adjudications.md](m12-adjudications.md)：

| M11 条目 | 现行为（0.4.0） | 取代/修订 |
|---|---|---|
| **A4** 内置预设库加性扩展（`builtin_file_stem`/`builtin_unavailable_reason`/新增 5 条 `_builtin-*`） | C++ 内置库整体删除；5 条预置内容保留为**仓库工厂 JSON**（`presets/formats/**`，11 条，单一真源、随包分发、首启字节复制 seed、旧 `_builtin-*` 迁移删除） | M12-A1/A2/A3 |
| **A5** 格式预设行 UI（去省略号/160px/不截断） | **保留不变**；追加 `default` 自动载入/不可删可保存/detached 态 | M12-A4/A5 |
| **A6** 状态机 S-4/S-5（「未选择」/外部删除后清自适应会话态） | 「未选择」状态不存在 ⇒ 两分支随 M12 删除；删除后改为**自愈回 default**；S-1/S-2/S-3 结论保留 | M12-A5 |
| **A11** 本轮不升版本号（保持 0.4.0.alpha1、CHANGELOG 以「未发行」记录） | **0.4.0 正式版已发布**：`PP_VERSION_LABEL` 清空、CHANGELOG `## [0.4.0]` 正式节（保留 alpha1 历史节）、tag `v0.4.0` + Release 由 Lead 执行 | M12-A9 |

> 其余裁定（A1 DSL 数组持久化、A2 HEIF C17'、A3 执行器 hooks 加性、A7 set 语义勘误、A8 DSL 语法加性、
> A9 参数目录生成器单源、A10 P3 登记不修、A12 脏构建树归因）**不受 M12 影响**，仍有效。
