# M10 执行日志（团队 / 子代理 / 中断与审计）

> 目的：记录 M10 实施期的执行主体、任务映射、**误派与中断事件**及其审计结论，保证后续接手者不被历史遗留污染。
> 关联：`docs/m10-workflow.md`（任务卡）、`docs/m10-adjudications.md`（冻结面/偏差）、`docs/v0.4.0-design.md`（设计与决策）。

## 1. 执行主体与任务映射

| 执行主体 | 类型 | 已完成/进行中任务 | 证据 |
|---|---|---|---|
| w1-engine-a | 本会话专用开发成员（团队席位） | task-5（T1/T2/T2b/T3）已完成；task-10（T2c 评分快速归一）进行中 | `build/m10/w1-engine-a/report.md`、`build/m10/w1-fastcolor/`（进行中） |
| w1-metric | 专用开发成员 | task-6（SSIMULACRA2 流式化）已完成 | `build/m10/w1-metric/report.md` |
| w1-run-ui | 专用开发成员 | task-7（运行页列宽/删速度列）已完成；task-9（T9a 状态/刷新）已完成 | `build/m10/w1-ui/report.md`、`report-status.md` |
| w1-tools | 专用开发成员 | task-8（perf_smoke/pp_progress_audit）已完成 | `build/m10/w1-tools/report.md` |
| 独立 subagent `6258adcb` | 子代理（非席位） | task-12 = W2-T1（HEIF grid/tile + Level 核心区）已完成 | `build/m10/w2-heif-grid/report.md` |
| 独立 subagent `60dc2a9c` | 子代理（非席位） | task-13 = W2-T5（JXL 参数治理）已完成 | `build/m10/w2-jxl/report.md` |

## 2. 误派与中断事件（2026-10-31）

- **事件**：因团队成员上限 8 已达（设计期 4 名侦察成员仍占席位、无移除工具），Lead 尝试新建开发成员失败后，**错误地把 task-11（引擎事件/评分池/多格式并行）与 task-12（HEIF grid/tile）改派给设计期侦察成员** recon-pipeline / recon-heif（消息中写“复用你的团队席位”）。用户明确指出这是错误做法。
- **处置**：立即 `interrupt_agent` 中断两个侦察成员；task-11/task-12 从它们名下收回为 pending；后续开发一律改用**独立 subagent**（用户 2026-10-31 指示：“按照 subagent 开发，而不是团队席位”）。task-12 已由 subagent 重做完成；task-11 待派发。
- **中断前审计**：中断时 `git status` 中无 `score_queue.*`、无 `src/codecs/heif_*`/`enc_heif.cpp` 改动 → 两个侦察成员**未产出源码改动**；仅遗留 gitignored 构建目录 `build/m10/w1-engine-b/`、`build/m10/w2-heif-grid/`（后者已被后续 subagent 复用为新构建树）。
- **纪律修正**：团队席位只用于会话内专用开发成员；设计期侦察成员不得转做开发；spawn 受限时必须先向用户报告，不得自行改派。

## 3. 两个已完成 subagent 任务的脚本污染审计（Lead 初查，2026-11-01）

> 用户提示：“由于复用低权限环境导致的错误脚本开发，不要被影响”。以下为对 W2-T1 / W2-T5 越界脚本改动的初查结论。

| 项 | 结论 | 证据/处置 |
|---|---|---|
| `src/codecs/param_catalog/anchors.inc` | **完好**：7715 B，与 HEAD 无 diff（W2-T5 自述曾误用 `--no-anchors` 清空后还原，已核实当前未被污染） | `git diff --stat -- src/codecs/param_catalog/anchors.inc` 空；字节数 = 7715 |
| `tools/gen_param_catalog.py` | 有 33 行改动（W2-T1 加 HEIF_UI_KEYS 白名单、计数 40→43；W2-T5 允许 JXL readonly 行）——**结构合理、带注释**；但 **`--strict-counts` 失败**：`jxl.json: expected 16 rows, got 14`（W2-T5 改行数后未同步 `BACKENDS` 计数） | 待修：`BACKENDS` jxl 16→14；复跑 `--strict-counts` 应为 0 |
| `--no-anchors` 风险 | 该选项会写入空 `anchors.inc`（W2-T5 误用实证）；**默认行为不安全** | 待修：无 anchors 时不得覆盖 `anchors.inc`（或需显式 `--allow-empty-anchors`）；并在工具帮助/错误中提示 |
| `tools/param_catalog_specs/{x265,jxl}.json` | 有改动（x265 +3 行/HEIF UI 键；jxl 收窄 14 行）——属 W2 设计范围 | `--check`/`--validate-only` 均 OK：`rows=117 x265_anchors=239 aom_anchors=192` |
| `tools/pp_linkprobe.cpp` | 仅计数期望更新（heif 40→43、jxl 16→14、只读 0→1）——属必要联动 | diff 逐行核对为计数/注释 |
| 其它越界文件 | `tools/heif_matrix_check.py`（+344/−? 属 W2-T1 矩阵扩展）、`docs/param-catalog.md`、`docs/m8-param-control-map.md` —— 均与本波参数面相关，非无关污染 | 逐项归因由 W2 报告覆盖 |
| 共享文件并行合并 | `param_rules.cpp`、`test_param_catalog.cpp`、`docs/param-catalog.md` 被 W2-T1/W2-T5 同时修改，subagent 自述“首轮因对方半成品失败、重生成后复跑全绿” | 组合树仍需一次独立构建+定向测试复核（见 §4） |

## 5. 审计回执（AUDIT-1 / task-14，2026-11-01 完成）

- **脚本修复**（仅 `tools/gen_param_catalog.py`）：`BACKENDS` jxl 16→14（`--strict-counts` 恢复）；`--no-anchors` 安全默认（无锚点时保留既有 `anchors.inc`，需显式 `--allow-empty-anchors` 才写空；help 更新）。
- **三检查全绿**：`--check`/`--validate-only`/`--strict-counts` exit 0（`rows=117 x265_anchors=239 aom_anchors=192`）；`--no-anchors` 四变体 exit 0。
- **anchors 校验**：7715 B，与 HEAD blob 逐字节相同、`git diff` 空；239/192 名无重复；`--no-anchors` 实测前后 sha256 不变（备份 `anchors.inc.bak` 于 `build/m10/audit/`）。
- **越界改动审计**：无回退项、无编码损坏/截断/行数不符；gen/specs/pp_linkprobe/*.inc/docs 均属本波范围或必要联动。发现 3 处 docs 计数漂移（已由 Lead 修正：目录总行应为 **117**=43+57+3+14；m8 文本/只读应为 2/14）；`docs/param-catalog.md` CRLF 已归一为 LF。
- **组合树**：`build/m10/audit` configure/build exit 0（439 步）；定向 ctest 9/9、全量除 UI 42/42、金样 `SMOKE total=20 pass=20 fail=0`；linkprobe 目录计数 OK（heif 43(39+4)、jxl 14(13+1)）。
- **UI smoke 已知红 2 条**：`pp-param-enable-noise`（W2-T5 删行后的旧探针）+ `magic-auto` 几何（归属未定）→ 已派 UI 适配任务（task-16）。
- **待办**：A15 重播种（干净树后）；docs 计数已修；jxl.json/buffering 已在 A18 追认；全量矩阵仅子集；`heif_matrix_check` 的 plan_grid C++ 手工复刻存在漂移风险（后续用单测/矩阵交叉校验）。

## 6. 后续执行主体（改用 subagent）

| 任务 | 执行者 | 状态 |
|---|---|---|
| task-10 W1-T2c | 已由 w1-engine-a 完成（兼容旧席位任务收尾；后续不再复用席位） | 已完成（色彩归一 8493→420ms；e2e 分数差≤0.01） |
| task-11 W1 引擎 T7/T5/T6 | 独立 subagent | 已派发 |
| task-15 W2-T2 still 档 + SPS | 独立 subagent | 已派发 |
| task-16 UI 探针适配 | 独立 subagent | 已派发 |
| W2-T3 / W2-T4 / T9b / W1-T12 | 待前序完成后依次派发 subagent | 未开始 |
## 8. W2-T2 回执（task-15，2026-11-01 完成）

- 交付：heif_profile.h 枚举 16–21（不重排；count 22；依旧像门控/still_available/短标签+tooltip；16 走 Main10 注入、17–21 原生）；heif_capability 新增 heif_parse_sps_ptl（hvcC 内嵌 SPS NAL、去 00 00 03、48 位 constraint + DPB、中文断言）；enc_heif.cpp 仅 profile 段+编码后 hvcC 校验段；UI 档位表 19 项（16 置灰+原因、3 个误导标签修正、Apple 风险 hint）；paramform.cpp 通用 combo 弹出层自适应（C19）；单测/矩阵扩展；docs/param-catalog HEIF 节重写。构建树 uild/m10/w2-profile 构建 exit 0。
- SPS 实测：17–21 各一例 hvcC idc4、SPS intra=1/one_picture=1、DPB 2/0；**20 与既有 14 逐位相同**（同一真实信号，设计成立）；**16（Main10Still）不可用**：idc2 的 PTL 44 位为保留位（intra/one_picture 不适用）、SPS DPB 实测 4/2（要求 0/0）→ 断言失败、中文错误点名 DPB、无半成品、UI 置灰（与 x265 level.cpp:100 “无 10bit still profile” 一致，不伪造）。
- 默认 Auto HEIF SHA 零变化（3 组：304,948 / 311,708 / 66,189 B，与 M9 发行构建逐字节相同）。
- 验收：定向 ctest 3/3、相关面 11/11、**全量 ctest 43/43（含 ui_smoke 49.98s 冻结行）**；矩阵 inal-still pass=8/fail=0/expected-fail=1(16)/unexpected=0；inal-profile-compat 198 例 0 unexpected；本任务 9 文件 format 0 diff（全源存量红在 W1 在飞文件）。
- 未决：R-2 规范命名仍待外部复核（双写“待复核”）；16 不可用是实测结论；预设带 16 时 disabled item 的 Qt setCurrentIndex 行为未实测（建议探针加断言/显式回落 Auto）；**smoke_probes 档位遍历仍按下标（13→19 项、末项已变）且未覆盖 16 disabled/tooltip** → 已并入 task-17 探针适配。
## 7. UI-ADAPT 回执（task-16，2026-11-01 完成）

- --ui-smoke exit 0、12 张冻结截图重生成、末行逐字 UI-SMOKE OK shots=12 pages=3；	ests/ui_smoke.py pass；定向 ctest 2/2 + effort 附加 3/3；format 0 diff（本任务 2 个 C++ 文件）。
- 删键旧探针：仅 probe_m8_params ② 段引用 
oise；改写为保留键 dots（同族三态），原 6 条断言语义逐条不变 + 加性 3 条；ui_smoke.py 前缀只增。
- magic-auto 根因 = W2-T5 把 epf 默认 2→-1（-1 即"自动"魔法值），旧前提"默认≠自动"过期；修法为三态取样（不放宽），output-geom 32/0、m8-params 60/0。
- effort=11：jxl 目录无 effort 行（hi=11 在 ormat_tables.cpp:139）；缺陷是 paramform.cpp 旧门控按已删 expert_options 压到 10，已删门控、回到 ParamDef 单源（1–11）；编码器自动 AllowExpertOptions 已核对（enc_jxl.cpp:403-404）。
- 待办：①全仓 format 存量红 6 个 W1 文件（pipeline.cpp/progress.*/dev_cli.cpp/2 个单测）需 owner --write；②ormat_tables.cpp:137-138 注释过期待 core owner 更新；③发行门禁前源码冻结后全量重建重跑；④.cache/ui-review 人工集未重生成、smoke_probes.cpp:525 诊断串 %d 既有问题（非本任务）。


## 9. W1-B1 回执（task-11，2026-11-01 完成）

- 构建 uild/m10/w1-engine-b EXIT=0；ctest **43/43**（全量两次；ui_smoke 隔离重跑 PASS 49.32s）；金样 **20/20**；本任务 format 0 diff。
- **T7 事件**：每输出 Encode1→MetaWrite1→output_done(无 metric)→文件终态→output_done(+metric)；真实 trace 11/11 首条 OutputDone 早于终态、0 违规；评分事件 11/11 晚于首条且含 metric；心跳覆盖后 pp_progress_audit.py 4/4 PASS，max_gap 181.6–425.0ms。
- **T5 评分池**：core/score_queue.{h,cpp}（K=4、独立配额 4GiB、阻塞排队+单任务超池拒绝、看门狗、wait_idle/cancel_pending）；dev_cli 等池收敛后写 sidecar；48MP+score **EXIT=0 且真算出 100 分**；2 格式+评分 wall 6.65s；8×22.8MP 批 Σmetric=28079ms/wall=21383ms。
- **T6 多格式并行**：1 文件 4 格式 22.8MP 并行 12.57s vs 串行 18.56s（判据≤15s），四格式 SHA256 逐字节相同；飞行预算表进 enc_common，装不下退回串行；PP_MAX_PARALLEL_TARGETS dev-only A/B。
- **移交**：① UI 必须以 OutputDone 事件刷评分列（Scheduler::results() metric 恒 Off）；GUI 取消接 cancel_pending()、结束接 wait_idle() → **task-18（W1-T9b）已派发**；② 唯一越界改动 src/ui/filelistmodel_util.h 加性 1 行+断言 14→15（待 UI 属主复核）；③ P4-1/P4-3 口径修订为 D-4（见设计 §16.1）；④ 未跑 regression zero-diff/TSan（由 W1-T12 集成门禁补）。
## 10. W2-T3 + W2-T4 回执（task-17，2026-11-01 完成）

- 新 src/codecs/enc_tune_facts.{h,cpp}：后端→值→类别（原生/仿真/置灰）→引擎处理→中文原因单源，未并入 engine 在飞的 enc_common。
- **HEIF tune 7 项**（4 原生 + animation 仿真 + zerolatency/vmaf 置灰）：animation 为插件 ssim + 用户透传前注入 psy-rd=0.4/aq-strength=0.4/deblock=1:1（用户显式覆盖优先）；置灰项 UI disabled+tooltip、预检 G8、编码器同文案兜底。原生 4 值字节与侦察基线完全一致；animation 902615B ≠ ssim 824196B。
- **AVIF**：libaom 扩 auto/psnr/ssim/iq/**ssimulacra2**/**vmaf_saliency_map**（后两值内部 om:tune 通道、不新增目录行）+ vmaf/butteraugli/cdef-dist 置灰；SVT 5 值标签 + iq/ms-ssim→CRF 提示；修 canonical_backend_id 大小写敏感 bug。
- **RC**：quality/crf/qp（及 aom:cq-level/qp）重构建到核心区紧随模式行；动态域 0..51 / −12..51 / −24..51 + 行内注记；quality 标签改「质量（0–100；CRF=(100−q)/2）」；QP+显式 Level 中文错误（编码器兜底）、CRF+Level 告警、越界错误；默认态产物 SHA 与 W2-T2 树逐字节同（HEIF/libaom/SVT 三例）。
- 探针：档位遍历按 value（19 项、16 置灰+tooltip）、C19 minWidth/item tooltip、tune 7 项、RC 父链 core-area/行序/三档位深域/QP+Level；ui_smoke UI-SMOKE OK shots=12 pages=3 + python 门禁 pass；m8-params checked=104 bad=0。
- 验收：构建成功；ctest 定向 4/4（新 test_heif_tune 142 checks 含内省↔事实表一致性）；HEIF 矩阵 inal-tune 22 例 0 unexpected；tune_probe 全 pass；本任务 C++ format 0 diff。
- 偏差：quality 标签变化会改 02-output 截图文本（截图集/冻结行不变）；早期误向共享 build/matrix/final.jsonl 追加 1 条 tnpsnr（gitignored，W2 全矩阵重跑覆盖即可）；AVIF tune 未入 heif_matrix（由 tune_probe/单测/UI 覆盖）；全量 ctest 待集成树重跑；一次 ui_smoke 门禁 stdout/stderr 合流竞态重跑即 pass。
## 11. W1-T9b 回执（task-18，2026-11-01 完成）

- 只改 3 文件：src/ui/page_run.cpp（+169/−29）、src/ui/page_run.h（+14，A10 加性）、src/ui/mainwindow_run.cpp（+99/−8）；未碰 core/codecs/smoke_probes/tests。
- 消费：OutputDone 即结算行（Done/Failed/Skipped/Cancelled + 大小/耗时/压缩比 tooltip/原因/warnings），不等文件终态；首条（metric Off）+评分启用→「已完成」+评分列「评分中」；第二条含 metric→同帧 2 位小数+四档色阶；终态幂等校准且**绝不用 terminal metric Off 覆盖已显示分数**；end_run 残留「—」+提示；评分关闭留空；OutputDone 不喂文件级列表/计数。
- 池接线：事件回调深拷贝 OutputResult 后 queued 交 GUI；RunPlan.quality_score 下发；取消时 pending>0 才 cancel_pending()；on_scheduler_done 在 sched->wait() 后、sched 销毁前分片 wait_idle(50ms)+processEvents（60s 上限，超时取消+30s 宽限），真实 ui-smoke 日志证明 quiesced before end_run。
- A10 已落地（RunPlan::quality_score=false + scoring_enabled() 加性）。
- 验收：build EXIT=0；离屏 --ui-smoke EXIT=0、末行逐字 UI-SMOKE OK shots=12 pages=3；	ests/ui_smoke.py pass；ctest 4/4；3 文件 format 0 diff；真实 --dev trace 顺序 396/413 OutputDone(off)→415 terminal→440/443 metric(85.78/86.50)；离屏回放探针 35/35 PASS（含"评分先到/终态后到"、终态 Off 不清空、评分关闭留空、残留—、失败原因）。
- **移交风险（已派 task-20）**：① ScorePool::cancel_pending() 一次性：取消后同进程后续运行评分全 Unavailable，且引擎 note 误写"内存不足"→ 需 per-run 取消/复位；② 评分任务捕获 Scheduler::Impl*，关窗时评分中的所有权缺口；③ GUI 收尾 pending>0 分支未实跑（仅代码审查+dev side 证据）；④ 待补探针清单（score-immediate/facts-immediate/run-status-map 扩展/flush-state 扩展/terminal-off-no-overwrite/score-wait-end/score-off-blank/run-rows-final）在报告 §8。
## 12. W3-CORE 回执（task-19，W3-T1+W3-T3 完成）

- 交付（全为新增文件、未改既有文件/CMake/vcpkg）：src/core/format_preset.*、src/ui/format_preset_io.*、src/core/adaptive.*、docs/schemas/{format-preset,adaptive-policy}.v1.schema.json、	ests/unit/test_format_presets.cpp、	est_adaptive_policy.cpp。
- 验收：正常构建 exit 0；ctest -R "test_format_presets|test_adaptive_policy" **2/2 passed**；8 个新 C++ 文件 clang-format 0 diff；schema json.loads OK。
- 口径：格式预设路径 <data_dir>/presets/formats/<format>/<name>.json，kind/version/未知根字段/__ 键重建/空 adaptive 不写/保存前 validate/save→load→save 字节稳定；自适应 DSL parse/render/validate（行列定位、幂等）、search 白名单 6 键、set=目录键−40 键禁止表、profile 仅 HEIF/AVIF 门控、output best 显式拒绝（P1）、默认库 5 条、42 错误码。
- **Lead 决策**：① CMake 不做 ormat_preset_io.cpp 迁移——它只服务 GUI（app 目标 GLOB 已含），--dev/ctest 不需要 Qt io；若未来需要再单独授权。② 设计 §8.2 heif_grid 示例已修正为 int（0关/1自动/2总是）。③ §9.4 允许面按"目录键−禁止表"落地，已补设计说明；search 白名单 6 键。④ W3-CORE 未跑 tidy/金样/回归/ui-smoke（纯新增、默认关闭）→ 由最终集成门禁统一跑。
- 磁盘事故：构建期 C: 曾满（由 15 个 per-task 构建树 21GB 造成）；已删除 13 个已完成任务的可重建构建树并先保存 16 份 report 到 uild/m10/_evidence/；C: 恢复 ~24GB。D: 为只读 ISO，无需处理。
## 13. W1-SCOREFIX 回执（task-20，2026-11-01 完成）

- 修复：① per-run 代次 ScoreRun（begin_run/reset_for_run/current_run/cancel_run/wait_run_idle；cancel_pending 只取消当前代次，队列按代次丢弃；Scheduler::start + GUI on_start + dev_cli 三处接线）；② submit_ex() 三分类 SubmitStatus，note 文案区分"运行取消，评分未完成"/"评分池已关闭，评分未完成"/配额不足才写"内存不足"；MetricState 语义不变；③ 所有权：scheduler 事件出口改 shared_ptr<EventSink>（评分任务不再持 Impl*）、取消谓词改 shared_ptr<ScoreRun>；~Scheduler retire+cancel_run+有界 wait_run_idle(30s)+teardown 日志；GUI aboutToQuit 钩子；④ GUI drain 下沉重构 ScorePool::drain(timeout,grace,pump,slice)（可单测，保留原日志行）。
- 验收：构建 EXIT=0；定向 ctest 4/4；金样 20/20；run1 取消→run2 评分实测 un1 state=2 note=运行取消，评分未完成 product_ok=1 → run2 state=1 score=100.00；drain ok/forced/busy 三态单测；GUI teardown 日志 quiesced=true；dev 真实评分 85.78 converged wait_ms=2739.9；format 0 diff。
- 未决：GUI 收尾 pending>0 端到端未触发（夹具小，语义单测+dev tail 佐证）；关窗钩子未带活 sched 实测（析构兜底已实测）；配额不足 note 未真实触发（需 >4GiB 单任务）；regression/TSan 未跑。
- **事故与审计线索**：① SCOREFIX 自述曾用 PowerShell 5.1 文本替换损坏 src/core/pipeline.cpp，按 W1-A1/W1-B1 会话记录重放恢复（自称损坏文件 154,565B、恢复后逐字节一致）；当前 src/core/pipeline.cpp 139,221B → **最终集成门禁必须追加：pipeline.cpp 编译/全量 ctest/金样/回归/事件顺序/多格式并行 SHA/评分链路 端到端复核，并在报告记录"恢复文件合理性"结论**。② 磁盘再次告急（构建期 0 字节），删除了 uild/m8、uild/analysis* 5 个陈旧构建树的 *.obj/*.pdb/*.ilk（约 26GB；源码与证据日志未动）。当前 C: ~22GB 可用。
- 遗留 ui_smoke：最终 46/47，唯一红 = W3-T2 在飞的 invalid-save=1 新期望其 smoke_probes 尚未输出（02:45 修几何后二进制已绿）；由 W3-T2 补齐后整体重跑。
## 14. W3-T6 回执（task-23，2026-11-01 完成）

- 新文档：docs/adaptive-policy.md（563 行用户手册：DSL EBNF/6 选项/search+set/4 方法/三例/执行语义 11 条/允许禁止键/42 错误码三类表/默认库 5 条/预设 JSON 承载/无 GUI 编辑器说明/成本模型/日志侧车）；docs/format-presets.md（248 行：路径/字段表/JSON 示例/校验链 7 步/损坏版本行为/与全局预设关系/内置预设定稿处留白）；docs/m10-report.md（307 行五段式骨架，已完成任务预填读数，未完成项标待填）。
- 机械一致性（uild/m10/w3-docs/check_docs.py，60 断言 DOCS-CHECK OK）：EBNF/三例==设计；默认库 5 条==adaptive.cpp；42 错误码文档==代码==W3-CORE 报告；禁止表 40 键、白名单 6 键集合相等；JSON 片段可解析。
- param-catalog 校核：修 1 处内部矛盾（HEIF 档位数 20→19，Main12 别名不计下拉）；登记 3 处建议不改（grid 缺单边≤256/乘积≤65535 文档、缺 H5 warn 文案、WebP 节既有表格缺陷）；其余五区逐项一致。
- 待 Lead：① A9/A11/A12/A13/A14 台账状态在 W3-T7 统一追认（A10 已落）；② 三份新文档 untracked，提交由用户决定；③ 未跑 --dev 端到端（T2/T4 在飞）。
## 15. W3-T2 回执（task-21，2026-11-01 完成）

- 交付：每格式页签顶部「格式预设」行（下拉+保存/另存/删除/导入/导出；坏文件显「（损坏）」；载入写后端/技术/位深/核心+高级参数/保留键 heif_profile；「已改 N」重算；normalize 注释位「已补齐 N 个默认项」且 __ 内部键不计）；自适应只读面（目标/轮数/阶段摘要 + 启用开关，初值=预设，无编辑控件，坏策略行内中文错误+禁用）；MainWindow 为单一 I/O 持有者（load→applies_to→normalize→validate；保存前校验不写盘；同名二次确认；删除绝对路径守卫）；page_output.h 仅末尾加性（A9）；FormatState 会话态；smoke_probes 新增 smoke_probe_format_preset() 17 组断言 + UI-SMOKE format-preset: 行，ui_smoke.py 前缀只增/17 字段断言。
- 验收（build/m10/w3-ui）：构建 exit 0；离屏 ui-smoke ormat-preset checked=17 bad=0、末行逐字 UI-SMOKE OK shots=12 pages=3；	ests/ui_smoke.py pass；预设 save→load→save 字节稳定（jpeg 预设 630B SHA 相同）；**全量 ctest 48/48 passed（76s）**；clang-format 全树 203 文件 0 diff。
- 未决：接线点在 Impl::refresh_preset_card() 首调（mainwindow.cpp 不在写作用域，迁回只需挪一行）；「另存」用 QInputDialog（全局预设走 PresetsDialog）；坏文件无 per-item tooltip；模态对话框未机械化探针（handler+I/O 单测覆盖，属人工走查）；heif_profile UI 往返走既有 apply_state_to_form；一次 run-guard flake（%d 既有消息 bug，重跑 3 次+全量 48/48 绿）；未跑金样/回归/tidy（零引擎改动，归集成）。
- A9/A11/A12 台账已更新（A9 落地；A11 落地；A12 部分落地，T9b 待补探针留 W3-T7）。
## 16. W3-T4 回执（task-22，2026-11-01 完成）

- 新 src/core/adaptive_exec.{h,cpp}（219+1176 行）：可注入 encode/score/cancel/hooks 的执行器；linear≤16/阶段、bisect≤18/阶段、exp（×1.5/上限4×）、greedy（无改善减半至初始/16）；达标即停/max_rounds(≤32)/plateau/阶段耗尽；每轮 validate_params+cross_validate+rules::preflight（只读）；非法=硬失败无产物；候选临时文件 commit/discard/取消全清；output best 显式拒绝；policy_sha=SHA256(规范化文本)。
- pipeline 集成（A3 落地）：OutputFormatSpec 末尾 daptive_policy→PlannedTarget→run_target 空串分派；自适应每轮编码到候选（不写元数据）→**同步** un_metric_job 评分→选定 commit→既有 metawrite/mtime→含 metric 的 OutputDone（不再提交异步评分，避免重复计分）；每轮 ProgressInfo(adaptive_round/adaptive_max)+scoring 心跳；daptive round/daptive summary(policy_sha 前16) 日志；非自适应路径逐字节不变。
- A13：侧车顶层 adaptive 对象；SCHEMA.md 加性 +74 行、旧断言零改动。A14：--adaptive-policy FILE；--preset 按根 kind 判别接受格式预设（输出 #0），全局预设路径逐字不变。夹具 	ests/golden/smoke-adaptive/（不进 20 对）。
- 验收：单测 22 例 0 failures；定向 ctest 3/3；**全量 ctest 48/48**；金样 **20/20**（两次）；E2E A（--preset 含策略）reason=met/score=100/二次运行 SHA 相同；E2E B（目标 100 不可达）reason=max_rounds/3 轮/输出最后一轮 97.479；默认 webp/jxl/jpeg SHA 与改动前二进制逐字节相同；format 0 diff。
- **偏差待追认（7 条，实现口径）**：① rom current=当前生效参数集（前序 stage 已落地者优先）；② 每轮进度刻度=(r-1+f)/max_rounds；③ 空区间策略补编 1 轮（stage=init）；④ set stage=1 轮/多键合一轮；⑤ 参数非法=硬失败无产物、编码失败回退前序成功轮；⑥ heif/avif 运行时键域=静态目录→libheif 内省(缓存)→兜底域表；⑦ --adaptive-policy 读取剔 UTF-8 BOM（DSL parser 不剔）。
- 未决：heif/avif 自适应未真实端到端；output best(P1)/tech(P2) 未做；自适应每轮成本未入 perf_smoke；建议新增 A19（侧车走 dev-only 发布表，未改 OutputResult；GUI 运行页暂由 ProgressInfo 承载轮次）。
## 17. W3-T5 回执（task-24，2026-11-01 完成）

- A20 已落：core/pipeline.h 的 OutputFormatSpec 末尾 ool adaptive_enabled=false（唯一加性；引擎判据仍 daptive_policy 非空）。
- 全局预设 v2：输出级 daptive{enabled,policy} 加性；空 adaptive 不写字段（旧文件逐字节不变）、读旧文件默认 disabled/空、结构错误显式报错、策略不进 params。
- GUI：spec_of 采集两字段→全局预设写盘；config_base() 按门控下发（关=既有单次编码路径）；pply_state_to_form 恢复会话态；_builtin- 选中时删除置灰、保存拒绝并提示另存。
- 内置预设：core 加性 uiltin_format_presets() 5 条（HEIF/AVIF/JXL/WebP/JPEG，名称「内置示例 · label」，策略逐字 §9.7）；ui/format_preset_io 幂等 seed <data_dir>/presets/formats/<fmt>/_builtin-<fmt>.json + 只读守卫；MainWindow 构造期一次性 seed。
- 验收：构建 exit 0；全量 ctest 48/48；金样 20/20；--ui-smoke format-preset checked=23 bad=0（新增 builtin/builtin-idem/builtin-readonly/builtin-saveas/global-adaptive/adaptive-gate 全=1）末行逐字；ui_smoke.py 字段 17→23 只增；格式 212 文件 0 violation；全局预设自适应往返字节稳定（SHA ECA1FD99…,1504B；关状态 enabled:false 且策略文本不丢）；内置 seed 首次 written=5、二次 0/skipped=5 字节不变、删/覆盖拒绝、另存可用；默认产物 SHA 不变（jpeg/webp/jxl 三例）；dev E2E 格式预设携带 adaptive 真跑通 jpeg/webp/heif/avif。
- **遗留决策**：dev_cli 全局预设路径不消费 adaptive（:1366-1369 只拷 params；:1463/1465 仅格式预设/--adaptive-policy）→ 最终集成补丁 3 行 + E2E（A14 完成）；其余未决：heif/avif 自适应 round1 即达标（后续 stage 未执行到，单测覆盖）；avif 格式级参数并集 vs 后端范围不一致（既有机制）；clang-tidy 未跑（改到最终门禁）。
## 18. FINAL-GATE 回执（task-25，2026-11-01 完成；报告 build/m10/final-gate/report.md）

- **A14 补丁**：src/dev_cli.cpp 全局预设分支加性 3 行；E2E 全局预设含 adaptive → 侧车 adaptive（rounds=1/score=100/reason=met）；旧预设与补丁前二进制产物逐字节同 SHA。
- **构建/格式**：uild/m10/final configure/build EXIT=0（481 步，二进制 SHA256 a2675b61…）；233 文件（212 tracked+21 新）钉版 clang-format 0 violation（未 -i）。
- **门禁**：ctest 48/48（75.75s）；金样 20/20；UI-SMOKE OK shots=12 pages=3；ui_smoke.py pass；**clang-tidy 不合格**（1 TU parse error + total 389>262/+127、violations=25）→ 已派 FINAL-TIDY 修复。
- **性能/事件（部分未达，已登记 D-5…D-7）**：48MP+score EXIT=0/score=100；并行 4 格式 12.7s vs 串行 16.7s、SHA 相同；默认 SHA 不变；22.8MP encode 2032ms/metric 2178.6ms=**1.072×**（D-5 未达）、HEIC 10-bit probe 1635–1670ms（D-6）、HEIC 深 probe 首事件 gap 1.44–1.67s（D-7）。
- **HEIF 矩阵**：4 子集 233 例 pass=59/fail=0/expected-fail=174/**unexpected=0**；默认 HEIF SHA 6/6 同基线。
- **回归/A15**：产物仅 JXL 有损 23/23 变化（A18 归因成立）、非 JXL 0 diff；日志含 runner 形状（thread_budget 4→16）+ 加性日志 + vcpkg 路径噪音 → **本地不 --update**（D-9）；建议 CI windows-2025 侧重播或先加 N11 归一。
- **pipeline.cpp 复核**：UTF-8 干净/0 乱码/无重复定义/文件尾正常；恢复件→当前 15 个 diff 块全部可归因（SCOREFIX + W3-T4）；构建/ctest/金样/回归/事件/并行 SHA 强背书 → 恢复合理性成立。

## 19. FINAL-TIDY 回执（task-26，2026-11-01 完成；报告 build/m10/tidy-fix/report.md）

- 修复解析失败：src/core/adaptive.cpp:566 全角括号窄字面量→UTF-8 字节序列+ends_with；140/140 TU 全解析。
- clang-tidy 棘轮：TIDY total=262 baseline=262 files=76 violations=0 seed=0 EXIT=0（180s）；逐文件 76 文件全 ≤基线、基线外文件 0；**未改 tools/baseline、未 --update**。
- 修复量 132 条 = 代码改 48 + NOLINT 抑制 84（41 参数可交换 / 26 SIMD intrinsic / 15 MSVC STL bad_array_new_length+闭包 move ctor 误报 / 2 白盒包含）；新文件 20 个全 0；既有 5 文件回到基线（test_color 10→5、page_run 9→6、page_output 8→5、pipeline 6→3、output_page_build 6→1）。
- 门禁复跑：重建 EXIT=0（157 步）、ctest 48/48（76.45s）、金样 20/20、format 233 文件 0 violation、ui-smoke 末行逐字 + ui_smoke.py pass；基线文件 SHA256 82364EC4…2C8D 未变。
- 风险：84 条 NOLINT 工具链升级后复核；adaptive.cpp 全角修复为唯一非纯静态改动（按注释意图恢复剥离）；~ScorePool 析构 try/catch 仅异常路径由 terminate 改尽力清理；既有 5 文件 20 条基线内老告警未动；regression/矩阵/性能未在本任务重跑（GATE 已测）。
## 20. FINAL-REPORT 回执（task-27，2026-11-01 完成；报告 build/m10/final-report/report.md）

- docs/m10-report.md 307→492 行，0 处"待填"：§0 状态总览 / §1 交付（W1/W2/W3+dev E2E）/ §2 验收（17 行逐命令表+默认 SHA 9 行+性能 7 行+回归归因）/ §3 偏差（D-1…D-9 + 7 条口径 + 84 NOLINT 分类 + A1–A20 状态 + param-catalog 3 缺口）/ §4 未覆盖 12 条 / §5 复现+证据 / §6 发行前检查清单（6.1–6.6）。
- 权威数字复核一致：干净树 build EXIT=0（481 步；tidy 后重建 157 步）；二进制 5,088,768B SHA256 E25F4F29…4731；ctest 48/48（75.75s / tidy 后 76.45s）；金样 20/20；UI-SMOKE OK shots=12 pages=3 + pass（format-preset checked=23 bad=0）；format 233 文件 RC=0；clang-tidy 262=262 files=76 violations=0 seed=0（基线 SHA 82364EC4…2C8D 未改）；HEIF 矩阵 4 子集 233 例 unexpected=0（598.5s）；默认 SHA jpeg/webp/jxl 3/3 + HEIF 6/6 不变；P4-1 metric 1.072× 未达（D-5）、HEIC 8bit 达标/10bit 1635–1670ms（D-6）、深 probe 首事件 gap（D-7）。
- 文档修改：m10-workflow.md 179→302 行（三波收口、§10 任务卡状态 34 张、§11 最终门禁记录 15 行、§12 预算对账）；param-catalog.md 补 grid 上限/H5 warn/修 WebP 两行；daptive-policy.md 补全局预设/运行时 seed(D-8)/A20/两路径互斥；ormat-presets.md 补 UI 落地/dev 路径/D-8。
- 一致性核对：check_numbers.py 66 条 **CONSISTENCY PASS**；check_tables.py 0 anomaly（1 条既有 PNG 行 pin 住）。**FINAL-REPORT 发现已由 Lead 修正**：final-gate §C 证据路径、design §16.1 标题 D-1…D-9、A8 状态、本日志本回执；PNG compressionLevel 行 7 列为既有同类缺陷、留登记。
- 未决（发行前需用户确认）：版本仍 0.3.6.alpha2 / CHANGELOG/README/tag/git 全未动；工作树 66 M + 33 ??（HEAD 1208894）；TSan 未跑；HEIF 全量 1013 例未跑；A15 待 CI windows-2025 或 N11 归一后重播；P2 八项见 m10-report §6.5。
## 21. A15-CLOSE 回执（task-28，2026-11-01 完成；证据 build/m10/a15-close/report.md）

- 	ools/regression.py（+123/−5）：**N11**（thread_budget/alloc_files/encode_threads/sum/peak → <threads>；SVT Level of Parallelism → <lp>；丢弃核数条件行）+ **N12**（vcpkg buildtrees 源目录 → <src>）+ **N6 修正**（*_ms/	hroughput 吃科学计数 5.2e+04）；--self-check +12 条归一断言。
- baseline 重播种：	ools/baseline/golden.windows.log 2534→2567 行，318,270B/SHA 280D2E38…224C → **336,399B/SHA 4FC72B33…6A49**（旧值逐字节核对 = m10-report 记录值）；--update 护栏 version-line=12 / behavior=196 / changed-lines=943；重播前逐段白名单 107 removed/150 added、unmatched=0。
- 硬化：新基线 normalize 幂等、机器形状残留 0、<threads> 272/248/531/12、<lp> 23、<src> 8、12 段、app version 行=0.4.0.alpha1；**zero diff ×2（EXIT=0）**；一次慢 run 科学计数假 diff 已归因并由 N6 修正。
- 未覆盖：CI windows-2025 侧 zero diff（Windows 亲和性不改变 ProcessorCount，无法模拟 4 vCPU）；Linux golden.log 仍旧，需 nightly workflow_dispatch + update_baseline=true 重生成后入库（N11/N12 同样生效）。
- 磁盘/纪律：未 git 写、未改 docs/src；build/m10/final 重建至 0.4.0.alpha1（SHA F3A0448A…）。
## 22. TSAN-CLOSE 回执（task-31，2026-11-01 完成；证据 build/m10/tsan/report.md）

- **本机不可跑 TSan（取证）**：wsl -l -v exit=1「未安装 WSL」；无 clang/gcc（where 0 命中）；MSVC 19.51 仅 /fsanitize=address；cmake --preset tsan → "Cannot use disabled configure preset"（Linux 条件）；依赖仅 x64-windows + Qt msvc2022_64，无 Linux 三元组。未做 WSL 冷构建（按卡约束，避免 GB 级浪费）。
- **CI 事实**：nightly 有 tsan job（ubuntu-24.04/20min/	san 预设/TSAN_OPTIONS/契约 ctest+金样+0 报告扫描）；last success #27（2026-10-11 f661a1f，tsan 194s）；#28–#50 共 23 轮无 success（22 failure + 1 cancelled），抽样 #28/#32/#36/#40/#45/#49/#50 均失败在 configure+build tsan（#48 vcpkg 缓存暖度），ctest/金样全 skipped ⇒ **TSan 实际未执行**；匿名取不到 job 日志（API 403/web 404），根因未猜。取证命令：gh auth login; gh run view 41186572022 --log --job=124494594081。
- **CI 方案**：P0 取日志修 build 断点→复跑确认 ctest+金样+0 报告三步；P1 把 pp_test_score_queue 加入 tsan 固定 targets（现 8 晚轮转仅 1/8）；P2 PR 级 tsan 不建议（撞 20min 预算）。
- **本地压力替代证据**（build/m10/final，0.4.0.alpha1，SHA F3A0448A…）：①4 格式并行 4MP×10 轮 40/40、22.8MP×2 轮 8/8 归一化 SHA=串行、12/12 trace PASS（gap 39–422ms）；②评分池 20/20 真实 --dev --score 循环 OK、5/5 硬杀→同目录重跑恢复、pp_test_score_queue cancel→run2 20/20；③8×22.8MP×3 轮 3/3 exit 0、8/8 产物+8/8 metric ok、trace PASS、max_gap 164–276ms、active_files 峰值 7。缺口：--dev 冻结面无取消入口，真实 CLI「取消→再 run」未执行（仅单测层）；**不宣称竞态已验、TSan 未测**。
- 纪律：未改产品/CI/文档，未 git 写，未派子任务；证据目录 1.24GB（gitignored）。
## 23. HEIF-FULL 回执（task-29，2026-11-01 完成；证据 build/m10/heif-full/report.md）

- 口径：最终轮全程单一二进制 uild/m10/final/photopipeline.exe = 0.4.0.alpha1 / SHA F3A0448A…（09:57:32 由 0.3.6.alpha2 重链接；首轮混代 + 6 例 exit=127“文件被占用”已作废留证 aborted-run1-mixed-binary.jsonl）。
- 全量：heif_matrix_check.py --phase final --binary … --jsonl … --md … --tmp-dir … --timeout 900；计划 1133、跨集 case_id 去重 31 ⇒ 实跑 **1102**；**pass=732 / fail=0 / expected-fail=370 / unexpected=0**；耗时 **2566.0s（≈42.8min）**，EXIT=0。
- 默认 HEIF SHA **6/6 逐字节同基线**（717/761/1,941,390/66,189/311,708/304,948 B 全 SHA 见报告 §4）；磁盘守卫最低 19.87GB，tmp 0 残留。
- 与 M9 全量 1013 例对比：共同 1013 verdict/expected_legal/expected-fail 行为不符/hvcC 事实差异均 0；685/697 pass 产物逐字节同；新增 89 例全为 M10 新覆盖（still 16–21 54、tune 22、grid·tile 11、still 2）⇒ 732=697+35、370=316+54 对账一致。登记差异：①共同 ef 中 195 例 reason kind level→grid（M10 heif_grid 语义的验证器文案/标签变化；exit=1/无产物与 M9 全同）；②12 例 pass 产物字节数变（profile1×7/profile2×4/grid always×1，hvcC 全同，未做码流级归因）。
- 未决：12 例字节变逐例码流级归因未做；首例冷启动 81.9s（环境）；若发行构建再替换二进制则本报告结论仅对 F3A0448A… 有效。