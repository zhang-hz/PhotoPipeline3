# M8 开发 Workflow（三阶段串行 · 阶段内并行 · 每阶段可验收）

> 状态：**执行计划 v2（2026-10-28，随设计 v4 / 控件形态与开发纪律定稿）**
> 关联：设计 `docs/m8-encoder-params-and-ssimulacra2-design.md`（v4）｜参数评审清单 `docs/m8-param-exposure-review.md`｜控件形态 `docs/m8-param-control-map.md`｜**开发纪律 `docs/m8-discipline.md`**
> 用户裁定（2026-10-28）：① 参数按评审清单定稿、清单外完全封死；② SVT-AV1 不打补丁（10 原生键）；③ SSIMULACRA 2 用方案 A（官方源码 + 兼容层）；④ 评分 GUI 默认开、8 种输出全评、参考=量化后编码器输入；⑤ 线程池不作为格式参数（软件默认调度）；⑥ **三阶段串行：参数 → 引擎+UI → 评分**，阶段内可并行；⑦ 每阶段一个可验收增量；⑧ **RC 模式用显式模式下拉**；⑨ 新增布尔行用三态下拉；⑩ **开发纪律：任务卡自包含/约束清晰、子任务不得再次派发子任务**；⑪ 计数双口径（报告 ≈142 组行 / 测试以 170 键展开为唯一真源）。
> 本文件 = 任务卡/依赖/写作用域/门禁的执行口径；实现期变更走 `docs/m8-adjudications.md`。

---

## 0. 总原则（为什么这样切）

1. **先事实、后接线、再评分**：参数目录/规则是“事实层”，引擎/UI 是“消费层”，评分是独立能力。事实层不先冻结，UI 与规则会反复改；评分不排最后，会和 `OutputResult`/结果页/SCHEMA 的冻结面抢改。
2. **阶段内并行、阶段间串行**：每个阶段只有 2–3 个并行卡（写作用域互斥），Lead 负责接口冻结/集成/门禁；**不跨阶段抢跑**（B 不碰评分文件、C 不碰参数目录）。
3. **每阶段可验收增量**：构建 + 单测 + 该阶段门禁全绿 + 提交 + 报告增量；阶段结束打一个“可回滚检查点”。
4. **零回归是硬门禁**：Stage A/B 不改变既有产物（新参数默认不设、既有默认值不动）；Stage C 评分默认在 GUI 开、`--dev` 默认关，回归基线不重播种。
5. **证据先行**：每卡在 `build/m8/<task-id>/`（gitignored）留命令/日志/截图/数据；收口进 `docs/m8-report.md`。
6. **冻结面只走加性 + 追认**：开工首日登记 `docs/m8-adjudications.md`（清单见设计 §7）。
7. **任务卡自包含、约束清晰**：每张卡按 `docs/m8-discipline.md` §3 的必填字段写；写作用域/验收命令/期望输出/禁止项/停止条件缺一不可；不清晰的卡子代理有权退回。
8. **只由 Lead 派发，子任务不得再派发**：teammate/subagent 不得调用任何再派发机制（含 `spawn_teammate`/`subagent`/`workflow`/脚本启动 agent）；卡太大 ⇒ 回报 Lead 拆分。

---

## 1. 阶段与工期（关键路径）

| 阶段 | 内容 | 并行卡 | 人日（串行等价） | 日历日（2–3 agent） | 可验收增量 |
|---|---|---|---:|---:|---|
| **K** | 启动：裁定登记/任务板/基线取证/纪律与任务卡模板/分支与构建树 | Lead | 0.5 | 0.5 | `m8-adjudications.md` + 基线门禁日志 + 纪律确认 |
| **A** | 参数事实层（目录/规则数据/生成脚本/对账测试） | 3 卡 + Lead | 3–4 | 2–3 | 目录 + 规则数据 + 单测；零行为变化、回归 zero diff |
| **B** | 引擎 + UI（三态/规则引擎/接线/预检/表单） | 2 卡 + Lead | 5–6 | 3–4 | GUI 全参数可调 + 非法组合预检拦截；全门禁绿 |
| **C** | SSIMULACRA 2（模块/集成/结果面/证据） | 3 卡 + Lead | 5–6 | 3–4 | 每输出显示分数、sidecar/断言、性能数据；全门禁绿 |
| **D** | 收口（报告/文档/双平台/全门禁复跑） | Lead | 1–2 | 1–2 | `m8-report.md` + 全绿证据 + 收口提交 |
| **合计** | | | **≈15–18 人日** | **≈9–11 个工作日** | 4 个可验收提交 |

> 缓冲：预留 +2 天（SSIMULACRA 2 数值门禁/兼容层是最大不确定项；x265 规则表漂移为次级）。若 Stage C 数值不达标，按设计 §5.1 切方案 B（需 Lead 裁定，预计 +2–3 天）。

---

## 2. 任务卡（ID / 内容 / 写作用域 / 依赖 / 验收）

### K — 启动（Lead，0.5 天）

| ID | 内容 | 写作用域 | 验收 |
|---|---|---|---|
| K1 | 登记 `docs/m8-adjudications.md`（pipeline.h/settings.h/params.h 加性、SCHEMA、`--dev --score`、third_party 源、UI objectName；设计 §7 清单逐条） | `docs/m8-adjudications.md` | 每条有先例/依据 |
| K2 | 建 `docs/m8-report.md` 骨架（交付/验收/偏差/未覆盖/复现命令） | `docs/m8-report.md` | 骨架可追加 |
| K3 | 基线取证：`ctest`、金样 20/20、`regression.py`、`--ui-smoke`、format（**Stage A 前状态**留档） | `build/m8/k3-baseline/` | 全绿日志 |
| K4 | 任务板 + 分支/构建树约定；PyPI oracle 可用性检查（`ssimulacra2==0.3.0` 装 repo venv） | 任务板/环境 | 任务卡可 claim |
| K5 | 发布并确认 `docs/m8-discipline.md`（任务卡必填字段/清晰性自检/子任务禁止再派发/违规处理）；所有参与者开工前确认已读 | `docs/m8-discipline.md` | 纪律冻结；参与者确认 |

**出口**：K1–K5 完成；用户说“开工”后进入 A。

### Stage A — 参数事实层（3 卡并行 + Lead 集成，2–3 天）

> 目标：把“要暴露什么”变成**机器可读目录 + 规则数据 + 生成脚本 + 对账测试**，不接 UI/引擎（零用户可见变化）。
> 先由 Lead 冻结 `.inc` 行格式与 `catalog.h` API（半天），A1–A3 并行填数据，A4 脚本/测试并行。

| ID | 内容 | 写作用域（互斥） | 依赖 | 验收 |
|---|---|---|---|---|
| A0（Lead） | 冻结 `catalog.h`（`CatalogRow`/`ParamCatalog`/状态枚举/查询 API）+ `.inc` 行格式（含**控件形态 M/N/D/S/T/R 与联动角色 锚点/依赖/独立**，见 `docs/m8-param-control-map.md`）+ 目录↔评审清单对账表模板；登记 C1 契约 | `src/codecs/param_catalog/catalog.h`、`catalog.cpp`（骨架） | K | 头文件冻结；格式样例 |
| A1 | JXL 目录（14 项：noise/center_x/y/upsampling_mode/uses_original_profile/box_compress/expert_options/effort11/buffering/output_mode/disable_perceptual_heuristics/HDR 4 项/intrinsic_size/frame_name）+ 规则数据 R1–R22（可落地部分） | `src/codecs/param_catalog/jxl_catalog.inc`、`docs/m8-param-exposure-review.md` 对应行核对 | A0 | 每行有 key/类型/域/默认/中文标签/分组/状态/理由/**控件形态/联动角色**；与评审稿逐条对账 |
| A2 | x265 目录（≈33 新增 + 4 只读行）+ 24 条冲突镜像 + L1 名字/值等价表（x265_param_parse 可接受名/域锚点） | `src/codecs/param_catalog/x265_catalog.inc` | A0 | 同上；CLI-ONLY/死参/禁设不建可设行 |
| A3 | libaom 目录（≈40）+ SVT 只读/限制登记（10 键、无 `svt:`） | `src/codecs/param_catalog/aom_catalog.inc`、`svt_catalog.inc` | A0 | 同上 |
| A4（Lead） | 生成脚本 `tools/gen_param_catalog.py`（从 pinned 源码抽取锚点 + 生成 .inc，CI 不联网）；目录不变量/对账/范围单测 + **控件形态/角色完整性与计数对账** | `tools/gen_param_catalog.py`、`tests/unit/test_param_catalog.cpp` | A0–A3（数据） | 脚本可重复生成；单测全绿；评审清单↔目录行对账 0 差异；`docs/m8-param-control-map.md` §9 计数一致 |

**Stage A 门禁（全绿才进 B）**

```
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- ctest --test-dir build/release-dev        # 含 test_param_catalog / test_param_control_map
python tools/env.py run -- python tests/golden/smoke.py build/release-dev   # SMOKE 20/20
python tools/env.py run -- python tools/regression.py                        # zero diff（本阶段零行为变化）
bash tools/ci-format.sh                                                      # 0 violation
```

**提交 A**（Lead）：`M8-A: 参数目录/规则数据/清单对账（零行为变化）`；报告增量：目录行数、清单对账结果、生成脚本用法。

### Stage B — 引擎 + UI（引擎卡 B1 / UI 卡 B2 并行 + Lead 集成，3–4 天）

> 前置：Lead 先冻结 `params.h` 加性契约（`ParamType::String`、`ParamDef::requires_enable`、`ParamContext`、`cross_validate` 重载）与 `paramform.h` 加性接口（半天），B1/B2 再并行。

| ID | 内容 | 写作用域（互斥） | 依赖 | 验收 |
|---|---|---|---|---|
| B0（Lead） | 冻结加性契约 + UI 接口；接入 Stage A 目录到 `introspect_backends`/静态表路径的集成点（骨架） | `src/core/params.h`、`src/codecs/param_catalog/catalog.cpp`（合并逻辑）、`src/ui/paramform.h` | A | 接口冻结；编译通过 |
| B1 | 引擎：目录合并/三态（未启用不下发）/后端作用域（清单外封死）/规则引擎 `param_rules.*` + `ParamContext`/`enc_heif`、`enc_jxl` 接线（新键、顺序修正、错误中文化）/运行前预检接入 `validate_run_config`/`dev_cli --param` 类型修复/`pp_linkprobe` 扩展/SVT `svt:` 假接线清理 | `src/core/params.cpp`、`src/core/param_rules.*`、`src/codecs/enc_heif.cpp`、`src/codecs/enc_jxl.cpp`、`src/dev_cli.cpp`、`tools/pp_linkprobe.cpp` | B0 | 单测：三态/规则/作用域/预设往返/参数类型；`--dev --param x265:crf=20` 生效；非法组合运行前报错 |
| B2 | UI：参数行「启用/跟随默认」三态、**新增布尔行三态下拉、RC 模式控件（质量/CRF/QP 等）**、只读/禁用原因、分组词表扩展、搜索/过滤（已启用/可设置）、预检错误清单+定位、预设往返、空/禁用态视觉、settings 的 `quality_score` 行（框架，评分逻辑留 C） | `src/ui/paramform.cpp`、`src/ui/page_output.cpp`、`src/ui/page_output_impl.h`、`src/ui/settings_dialog.cpp`、`src/ui/smoke_probes.cpp`（加性探针） | B0 | 手动验收清单通过；`ui-smoke` 冻结末行不变、新增探针通过；无裁剪/重叠 |
| B3（Lead） | 集成/门禁/证据：跑全门禁、截图、更新报告；处理跨卡冲突 | 集成 | B1+B2 | 全绿 + 证据包 |

**Stage B 门禁**

```
ctest --preset release                 # 全绿（新增 test_param_rules / test_params 扩展）
python tests/golden/smoke.py build/release-dev          # SMOKE total=20 pass=20 fail=0
python tools/regression.py                              # zero diff（新参数默认不设；既有默认不动）
QT_QPA_PLATFORM=offscreen … --ui-smoke / python tests/ui_smoke.py <build>   # UI-SMOKE OK shots=12 pages=3；layout-audit bad=0
bash tools/ci-format.sh / ci-tidy.py                    # 0 diff / 棘轮不增
```

**提交 B**：`M8-B: 参数全暴露（三态/规则引擎/预检）+ UI`；报告增量：每后端三态计数、预检规则覆盖表、GUI 截图/手测记录、回归 zero diff 证据。

### Stage C — SSIMULACRA 2（3 卡并行 + Lead 集成，3–4 天）

> 前置：Lead 冻结指标 API（`third_party/ssimulacra2/` 目录结构、`compute()` 入参/出参、`metric_state` 契约）与 sidecar/断言 schema 草案。

| ID | 内容 | 写作用域（互斥） | 依赖 | 验收 |
|---|---|---|---|---|
| C0（Lead） | 取 libjxl 0.12 官方源码（`tools/ssimulacra2.*` + `gauss_blur.cc`）入 `third_party/`，保留 BSD-3 头 + 算法版本常量；安装 PyPI oracle 到 venv；benchmark 脚本骨架 | `third_party/ssimulacra2/`、`build/m8/c0/`、`.venv` | B | 源码哈希/许可记录；oracle 可跑 |
| C1 | 指标核心：兼容层 + 算法（streaming/scalar；5-tap 模糊/XYB/SSIM/边缘图/108 权重/remap 逐字对照官方源码）；单测（恒等=100、灰度/16bit/8×8/奇数尺寸、取消、确定性）；AVX2 决策与实现（按实测） | `src/core/metric/*`（新）、`tests/unit/test_quality_metric.cpp` | C0 | 单测全绿；与 PyPI oracle |Δ|≤0.1（≥5 对）；性能/内存数据 |
| C2 | 管线/结果面：`RunConfig::quality_score` + `OutputResult` 字段（尾加性）、量化参考（与编码器同量化核）、`probe_file`+`decode_float` 回读、ColorManager 目标→sRGB 归一、取消、`metric_ms`、`--dev --score` + sidecar `metric`、运行页评分子行 + tooltip、settings 开关 | `src/core/pipeline.h`、`src/core/pipeline.cpp`、`src/dev_cli.cpp`、`src/ui/page_run.cpp`、`src/core/settings.*`、`src/ui/mainwindow_run.cpp` | C0 | 每个输出（8 格式）能显示分数；无损=100；metadata-only 无分数；回归 zero diff（默认不评分） |
| C3 | 证据面：`SCHEMA.md` 加性段、`pp_verify assert.score` + `--selftest`、金样 2–4 个既有 case 加区间断言、`smoke.py` 带 `--score`、`ui_smoke` 评分子行探针、性能/内存报告 | `tests/golden/SCHEMA.md`、`tools/pp_verify.cpp`、`tests/golden/smoke/*.json`、`tests/golden/smoke.py`、`tests/ui_smoke.py`、`src/ui/smoke_probes.cpp` | C1+C2（接口） | 全门禁绿；报告含 ms/MP、wall time、峰值 RSS、oracle 对照命令 |
| C4（Lead） | 集成/门禁/证据包 | 集成 | C1–C3 | 全绿 |

**Stage C 门禁**

```
ctest …                                   # 含 test_quality_metric
python tests/golden/smoke.py build/release-dev   # SMOKE 20/20（内部 --score）
python tools/regression.py                # zero diff（--dev 默认不评分；日志/预算不变）
python tests/ui_smoke.py <build>          # UI-SMOKE OK shots=12 pages=3 + 评分子行探针
oracle 对照脚本                            # |Δ|≤0.1；恒等=100；无损≥99.9
性能脚本                                  # 12/24MP ms/MP、批处理 wall time、峰值 RSS
```

**提交 C**：`M8-C: SSIMULACRA 2 逐输出评分（默认开/8 格式）+ 结果页`；报告增量：数值证据、性能数据、评分 UI 截图、SCHEMA/断言说明。

### Stage D — 收口（Lead，1–2 天）

| ID | 内容 | 验收 |
|---|---|---|
| D1 | `docs/m8-report.md` 完稿（交付/验收逐条命令+结果/偏差追认/未覆盖/复现路径） | 可复核 |
| D2 | `CHANGELOG.md`、`README.md`、`docs/param-catalog.md`、`docs/design.md` 口径更新；关于页/许可（BSD-3 + PATENTS） | 文档一致 |
| D3 | 双平台构建 + 全门禁复跑（ctest/金样/回归/ui-smoke/format/tidy/密钥扫描） | 全绿 |
| D4 | 收口提交（不入发行 tag）+ 交接摘要 | 提交说明可审计 |

---

## 3. 角色与并行规则（Lead + 子代理）

| 角色 | 职责 | 说明 |
|---|---|---|
| **Lead（主对话）** | 冻结契约、任务分配、集成、门禁、提交、报告、用户沟通 | 所有“冻结面/跨卡文件”由 Lead 写；不把集成文件分给子卡；**唯一派发权**（teammate/subagent 不得再派发） |
| **数据卡（A1–A3）** | 只写自己的 `.inc` + 对应评审稿核对；不改 API/UI/引擎 | 行格式由 A0 冻结；冲突只有“目录 API vs 数据行”，靠 A0 先冻结 |
| **引擎卡（B1）** | 只写 params/rules/encoders/dev_cli/linkprobe；不改 UI 文件 | 与 UI 卡通过 `params.h`/`paramform.h` 冻结接口对接 |
| **UI 卡（B2）** | 只写 paramform/page_output/settings_dialog/smoke_probes（加性）；不改引擎 | 可在 B1 未完成时用 Stage A 目录数据推进 |
| **指标卡（C1）** | 只写 `third_party/` + `src/core/metric/` + 其单测 | 与 C2 通过冻结 `compute()` API 对接 |
| **集成卡（C2）** | 只写 pipeline/settings/dev_cli/page_run/mainwindow_run | 不碰指标算法 |
| **证据卡（C3）** | 只写 SCHEMA/pp_verify/golden JSON/smoke.py/ui_smoke.py/探针 | 可先用 C0 冻结 schema 写断言，C2 完成后跑 |
| **用户** | 裁定/验收；清单增删 | 每阶段 gate 通过后可抽查 |

**并行纪律**
- 一卡一目录/一组文件，**写作用域互斥**；共享文件（`params.h`、`pipeline.h`、`SCHEMA.md`、`smoke_probes.cpp`）全部由 Lead 在 B0/C0 冻结后统一写/合并。
- 每卡用自己的构建树 `build/m8/<task-id>/`（`-DVCPKG_MANIFEST_INSTALL=OFF`）与临时目录；**最终门禁**统一在 `build/release-dev` 由 Lead 跑。
- 子代理不执行 git/不碰发布/不改 CI；Lead 汇总后按阶段提交（每阶段 1 个可回滚提交）。
- **子代理不得再次派发子任务**：不得调用 `spawn_teammate`/`subagent`/`workflow`，不得通过 shell/脚本启动 agent；卡太大/需要并行 ⇒ 停止并回报 Lead 拆分（细则 `docs/m8-discipline.md` §8）。
- 每卡交付 = 代码 + 单测 + 证据日志 + 一段“未覆盖/风险”说明；缺证据不合并。
- 任务卡按 `docs/m8-discipline.md` §3 必填字段写；子代理发现不清晰/自相矛盾 ⇒ 退回，不得猜测开工。

---

## 4. 门禁与证据（每阶段都要跑的“最小集”）

| 门禁 | 命令（经 `tools/env.py`） | Stage A | B | C | D |
|---|---|---|---|---|---|
| 构建 | `cmake --build build/release-dev` | ✅ | ✅ | ✅ | ✅ |
| 单测 | `ctest --test-dir build/release-dev` | ✅ | ✅ | ✅ | ✅ |
| 金样 | `python tests/golden/smoke.py build/release-dev` | 20/20 | 20/20 | 20/20（含 score 断言） | ✅ |
| 回归 | `python tools/regression.py` | zero diff | zero diff | zero diff | ✅ |
| UI | `python tests/ui_smoke.py build/release-dev` | —（无 UI 改） | ✅ 冻结行+新探针 | ✅ 评分探针 | ✅ |
| 格式 | `bash tools/ci-format.sh` | ✅ | ✅ | ✅ | ✅ |
| 静态 | `python tools/ci-tidy.py build/release-dev` | ✅ | ✅ | ✅ | ✅ |
| 数值 | oracle 对照（Stage C） | — | — | ✅ | ✅ |
| 性能 | ms/MP + wall time + RSS（Stage C） | — | — | ✅ | ✅ |

**证据路径**：`build/m8/<task-id>/{cmd.log,out/,notes.md}`（gitignored）；报告引用路径 + 关键输出行。

---

## 5. 快速推进的机制（为什么不会“越做越慢”）

1. **接口先冻结、数据先落地**：A0 冻结 `.inc`/`catalog.h`，B0 冻结 `params.h`/`paramform.h`，C0 冻结指标 API/schema；并行卡不互相等代码。
2. **每阶段一个“用户可见增量”**：A 不改变行为（只落事实）；B 完成后参数功能即可试用（可提前验收）；C 完成后评分可见；D 收口。用户可在 B 结束就试用参数，不必等评分。
3. **门禁前移**：每卡完成即跑自己的单测/小语料；集成门禁只做“合并验证”，不把问题攒到收口。
4. **回归零 diff 作为“免重播种”护城河**：所有新增参数默认不设 + 评分默认不在 `--dev`，因此 A/B/C 都不需要动基线；若出现 diff，优先修实现而不是重播种（需 Lead 裁定）。
5. **风险隔离**：SSIMULACRA 2（最大风险）在 C 阶段独立卡，A/B 的价值不被它阻塞；数值不达标切方案 B 时，A/B 已交付。
6. **限时卡**：每卡预估 + 0.5 天缓冲；超时未过卡由 Lead 拆小/换实现，不在一个卡上无限打磨。
7. **任务卡纪律前置**：派卡前过 `docs/m8-discipline.md` §3.3 十项清晰性自检；子代理有权退回不清晰卡；递归派发直接撤销——用流程清晰度换返工率。

---

## 6. 开工清单（用户说“开工”后 Lead 的第一天）

1. 确认 `docs/m8-discipline.md`（含任务卡模板/清晰性自检/禁止递归派发）为开工前置条件；所有参与者已读。
2. 建 `docs/m8-adjudications.md`、`docs/m8-report.md` 骨架（K1/K2）。
3. 录基线门禁（K3）。
4. 建共享任务板（M8-A1…D4，含写作用域/依赖/验收；每卡通过 §3.3 清晰性自检）。
5. 检查/安装 dev-only oracle（PyPI `ssimulacra2==0.3.0` 到 repo venv；若离线不可用 → 记录并改用 cloudinary 参考 CLI 构建/固定测试向量，作为 fallback 证据路径）。
6. 派发 Stage A 三张数据卡 + A4（Lead 自留）；每天同步一次“卡状态/门禁/风险”。
7. Stage A 门禁通过后提交 A，再进 Stage B；**不跨阶段抢跑**。

---

## 7. 风险与升级路径

| 风险 | 触发信号 | 升级动作 |
|---|---|---|
| SSIMULACRA 2 数值不达标（|Δ|>0.1） | Stage C1 oracle 对照 | Lead 裁定切方案 B（overlay 子集），或延长 C1 1–2 天；报告如实记录 |
| 兼容层性能不达标（24MP > 预期） | C1 基准 | 先 AVX2 内核（C1 内），仍不行则把“快速模式/首输出评分”作为 P2 另行确认（默认不做） |
| x265 规则镜像漂移/误拦 | Stage B 预检测试/回归 | 规则表加“证据+来源”列；误拦优先修规则，不放宽门禁；x265 升级时重跑 A2 抽取 |
| 目录行与评审清单不一致 | Stage A 对账 | 以评审清单为准；差异必须书面说明（报告“偏差与追认”） |
| UI 探针/冻结行回归 | Stage B ui-smoke | 加性探针优先；确需改冻结行 → Lead 向用户确认（AGENTS §5.3） |
| 回归 diff | 任一阶段 | 先归因；不得 `--update` 重播种，除非 Lead 裁定并写报告 |

---

*本 workflow 随阶段执行更新；每阶段结束在 `docs/m8-report.md` 追加“阶段验收”小节。*
