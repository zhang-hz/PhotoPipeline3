# 运行 / 输出 / 预设状态机分析与缺陷台账（M10 状态机批 · task-3）

> **状态**：分析 + 高置信缺陷修复批（S-1/S-2 已修；S-3/S-4 待 Lead 批量批准）。
> **用户需求**：需求 4「运行里面的状态 bug 仍然很多，详细分析整个状态机，随便用就是 bug」；需求 1「迭代优化过程写入日志」。
> **写作用域**：`src/core/pipeline.cpp`、`src/ui/page_run.cpp/.h`、`src/ui/mainwindow_run.cpp`、`src/ui/mainwindow_status.cpp`、`src/ui/mainwindow_presets.cpp`、`src/ui/page_output.cpp/.h`、`src/ui/page_output_impl.h`、`src/ui/smoke_probes.cpp`、`tests/ui_smoke.py`、`docs/state-machine-analysis.md`、`tests/unit/*`（状态相关）。
> **集成树**：`build/iter1`（Lead 干净树）。**构建/测试一律经串行锁脚本** `build/iter1/build.ps1`（Windows PowerShell 5.1，本机无 pwsh 7）。
> **证据纪律**：本文件每个缺陷给 file:line（可复核）；"已修"= 有代码 + 机械探针 + 集成树读数；"未修"= 如实写原因。

---

## 0. 结论摘要

1. **运行页确实存在一类系统性状态缺陷**：**迟到事件覆写终态**。评分池看门狗心跳（`FileState::Progress`，
   `heartbeat=true`）与取消后的残余过程事件可能晚于文件终态事件到达 GUI，旧实现继续走
   `apply_progress` + `file.state = ev.state`，把 `Done/Failed/Skipped/Cancelled` 打回 `Progress`
   ⇒ 组行胶囊从「已完成/失败/跳过」退回「写入/色彩」进行中、完成态唯一形态丢失。**已修（S-1）**。
2. **取消按钮的展示语义与事实脱钩**：用户点了「取消剩余」但引擎汇总 `cancelled==0`（全部文件已成功）时，
   结束态恒显示「已取消」，与同一屏的「成功 N / 可打开输出目录」矛盾。**已修（S-2）**。
3. **预设来源账本会串**：全局预设载入后，格式预设行的会话态（`format_preset_path` / 「来自格式预设：X」）
   不清，卡值已来自全局预设却仍宣称来自格式预设 X；此时按「保存」会用全局值覆盖 X 文件。**高置信，
   待批准（S-3）**。同类：选「未选择」只清路径、不清自适应开关（S-4，落点在 Lead 的
   `output_page_build.cpp`，需 Lead 裁决）。
4. **已知 ui-smoke 崩溃 = 脏构建树假象，不是代码回归**：本机 `build/release-dev` 二进制四次同偏移
   `0xC0000005 @ 0xada24`；而**同机同时刻** `build/m10/final` 旧二进制完整通过、Lead 在干净树
   `build/iter1` 全量构建后 `--ui-smoke` 通过（`UI-SMOKE OK shots=12 pages=3`）。三二进制同尺寸
   5,088,768 B 但 SHA256 全不同 ⇒ 增量构建树对象/配置不一致。见 §6。
5. **需求 1（自适应迭代日志）已落地**：`pipeline.cpp` 在 `run_target_adaptive` 消费执行器 C 段
   hooks（`on_plan` / `on_stage_start` / `AdaptiveRoundRecord.effective_params`、`candidate_path` /
   `AdaptiveRunResult.selected_params_text`、`reason_detail`），新增/增强四类 info 行：
   **adaptive plan / adaptive stage / adaptive round / adaptive summary**。字段表与样例行见 §8。
6. 状态机整体**分层是清晰的**（引擎事件是唯一事实源、UI 只做投影、`OutputState`/`FileState` 各有
   单迁移点）；真正的债务是**跨层的"终态不可逆"契约没有在消费端强制**（S-1 是典型），以及
   **会话态与持久态之间缺少"写者冲突"规则**（S-3/S-4）。本轮把前者用"终态门槛"钉死，后者登记待裁。

---

## 1. 方法与取证面

| 面 | 做法 | 记录 |
|---|---|---|
| 读码 | 引擎：`pipeline.h/.cpp`、`progress.cpp`、`scheduler.cpp`、`score_queue.cpp`、`adaptive_exec.h/.cpp`；UI：`page_run.cpp`、`mainwindow_run.cpp`、`mainwindow_presets.cpp`、`page_output.cpp`、`output_page_build.cpp`（只读）、`run_rows_model.h`、`run_status_text.h` | 本文 §2–§4 |
| 事件流 | 用 `--dev` + `progress-trace.jsonl`、真实 GUI 运行日志（`[pipeline] output done` / `quality metric` / `[sched] e3 report`）核对事件序 | §8 样例行 |
| UI 机械探针 | `--ui-smoke`（`smoke_probes.cpp`：run-geom / run-status-map / flush-state / format-preset / run-score 等），配套 `tests/ui_smoke.py` 前缀 + 取值断言 | §7 |
| 崩溃归因 | `--ui-smoke` 复现 + Windows 事件日志（Application Error 的 faulting offset）+ 三棵构建树二进制 SHA 对比 | §6 |

**状态机覆盖清单**（用户需求要求"整个状态机"）：

- 引擎：`FileState`、`OutputState`、`MetricState`、`FileEvent` 序列（阶段 / `Progress` / `OutputDone` / 终态）、
  `ProgressMux` 内部 `OutState`、`ScorePool` per-run 代次、`Scheduler` 批生命周期、自适应轮状态。
- UI：`RunState` / `FileRow` / `OutRow` / `OutKind`、`apply_progress` / `apply_output_done` / `apply_terminal`、
  dirty 双通道 flush、tally/overview、取消/开始/结束/重入/关窗、`lock_for_run` 运行期禁用面。
- 输出页/预设：`FormatState`（spec/form/extra/bitdepth/profile/自适应/格式预设会话态）、格式预设行状态、
  全局预设 vs 格式预设 vs `last_preset` 覆盖顺序、自适应只读面+开关、运行前预检/禁用面。

---

## 2. 引擎状态机

### 2.1 FileState（文件级，`core/pipeline.h:211`）

状态集：`Queued, Probing, Decoding, Orienting, Coloring, Flattening, Encoding, Writing, Done,
Skipped, Failed, Cancelled, Progress, DoneWithErrors, OutputDone`（后四个是加性追加值）。

**产生点与转移**（唯一事件源 = `pipeline.cpp` 的阶段函数 + `scheduler.cpp` 的终态出口）：

| 触发 | 事件 | 产生点 |
|---|---|---|
| 取件后探测 | `Probing` | `pipeline.cpp` stage_probe；scheduler probe 失败直接终态 `Failed` |
| 解码/旋转/色彩/拍平/编码/落盘 | `Decoding/Orienting/Coloring/Flattening/Encoding/Writing` | `RunFileContext::stage()`（pipeline.cpp:716） |
| 编码器行级进度/心跳 | `Progress` | `ProgressMux` 回调（progress.cpp:324 `emit_seq`；:599 `emit_heartbeat`） |
| 每个输出结算 | `OutputDone` | `RunFileContext::emit_output_done()`（pipeline.cpp:748） |
| 文件终态 | `Done/DoneWithErrors/Failed/Skipped/Cancelled` | `Scheduler::Impl::finish()`（scheduler.cpp:298，唯一出口；映射单源 `file_state_of` scheduler.cpp:725） |

**不变量**：

- I-F1 **终态唯一且最末**：每个文件恰好一条终态事件（`Scheduler::finish` 是全部退出路径的唯一出口；
  取消入口 `cancel_exit` scheduler.cpp:316 也走 `finish`）。
- I-F2 **终态不可逆**：终态之后不得再被文件级过程事件改写（旧实现违反 → S-1；现由 UI 消费端强制）。
- I-F3 `FileState::Progress` 是**过程态**，不得进入文件列表模型（`mainwindow_run.cpp:1413` 已排除）。
- I-F4 `OutputDone` 是**输出级**事实，不改变文件级终态；度量完的第二次 `OutputDone`（含 metric）可晚于终态。

### 2.2 OutputState / MetricState（输出级，`pipeline.h:150/169`）

- `OutputState{Pending,Ok,Skipped,Failed,Cancelled}`：**单迁移点** `set_output_state()`（pipeline.cpp:2798+），
  旧布尔 `ok/skipped` 是它的只读投影（R-172 双轨对齐）。迁移序列：`Pending→{Ok,Skipped,Failed,Cancelled}`；
  不变量：**终态后再迁移=bug**（本批未发现引擎侧违反，除 §5 S-8 的"进度事件语义"表述差异）。
- `MetricState{Off,Ok,Unavailable,Failed}`：**永不影响产物成败**（C1/P0 兜底）；异步评分完成后行会被
  评分池**再发布一次**（第二次 `OutputDone`），故 UI 必须按"后到覆盖先到、同值早退"消费。
- 证据面：`OutputResult.state` 在 `OutputDone` 里随行快照走；文件终态 `FileResult.outputs[i].state`
  是**幂等校准**来源。

### 2.3 FileEvent 序列与 ProgressMux

`FileEvent`（`pipeline.h:265`）：`index`（批内槽位，scheduler 回填）、`state`、`result`（仅文件终态非空）、
`progress`（`ProgressInfo`）、`output`（`FileEvent::output`；仅 `OutputDone` 非空，`shared_ptr` 值安全）。

**ProgressMux 内部状态机**（`core/progress.cpp:218`，R-287 收口为显式 `OutState`）：

```
Unbound --bind_output--> Bound --on_output_encode_done--> Encoded --on_output_metawrite_done--> MetaDone
   |                        |                                  |
   |                        +--on_output_failed--> Failed     +--on_output_skipped--> Skipped
   +--on_output_no_encode--> Bound（仅元数据：enc=1）
```

- 不变量：每输出 `enc_frac/meta_frac` **单调不减**（`max` 水位）；`overall_frac` 由单层高水位兜底（R-288）。
- 事件发射在**事件序列锁**（`emit_mu`）下、数据锁之外调用回调（R-170：回调可重入、事件按序）。
- 节流：20ms / 0.5% / 段完成强制点（`should_emit_locked` progress.cpp:300）。
- `Progress` 事件的三种来源：**阶段内数值**（`emit_seq`）、**心跳**（`heartbeat=true`，不推进 frac；
  `emit_heartbeat` progress.cpp:599）、**自适应轮/评分心跳**（`adaptive_round/adaptive_max/scoring`）。
- ⚠ 关键消费契约（S-1 根因）：心跳/评分事件是**过程态**、可在文件终态之后到达；消费者必须
  "终态后忽略过程事件，仅允许 `OutputDone` 补评分列"。

### 2.4 ScorePool per-run 代次（`core/score_queue.*`）

状态：`{generation, cancelled}`（`ScoreRun`）+ 队列 `queue_` + 在飞 `running_` + 发布表 `published_`。

| 事件 | 迁移/语义 | 落点 |
|---|---|---|
| 运行开始 | `begin_run()/reset_for_run()`：取消上一代、丢弃上一代队列、清发布表、新代次 | score_queue.cpp:127 |
| 提交 | `submit_ex`：`Accepted / PoolStopping / RunCancelled / QuotaExceeded / InvalidTask`；单任务 > 池容量即拒（P0 兜底） | :197 |
| 取消 | `cancel_pending()/cancel_run()`：只作废**当前/指定代次**；在飞任务经代次令牌检查点退出 | :227/:240 |
| 完成/丢弃 | `publish_row`：写发布表（按产物路径 + 代次过滤；旧代次迟到行不污染新运行） | :265 |
| 收尾 | `drain(timeout,grace,pump,slice)`：分片 `wait_idle` + pump → 超时强制取消 + 宽限；如实返回读数 | :431 |

不变量：**文件线程永不在 `submit` 上等待**；评分失败只落 `metric_*`；`published()` 只对当前代次可见；
`pending` 含"已出队、等配额"的任务（旧空窗会提前放行收尾）。**能力边界**：GUI 收尾的
`pending>0` 端到端仍无小夹具可复现（m10-report §4-12 同款未决，本批未扩张）。

### 2.5 Scheduler 批生命周期（`core/scheduler.*`）

```
构造 → start()（解析 workers/stagger/thread_budget + begin_run 评分代次 + 起 worker 池）
  → worker: 取件(atomic ticket) → [取消?] → 交错等待 → probe(一次) → 同键串行化 → 并发闸(Alloc)
           → run_one_file/run_metadata_only → 放闸/放键 → 登记 reserved → finish(终态)
  → wait()：join + budget/e3 report（每 run 一次）
  → 析构：cancel + wait + sink->retire() + 评分代次 cancel + 有界 wait_run_idle(30s)
```

- 取消语义：`cancel()` 幂等；已在编码的文件跑完；等待点（交错/闸/键/预算 acquire）立即出
  `Cancelled` 终态、不进管线（`cancel_exit` 单点）。
- 预算：文件 `need = 2×frame`；评分走独立 `ScoreQuota`（不再挤占文件预算）；飞行 token 失败 → 退回串行。
- 交错：`StaggerLimiter`（scheduler.h:153），`stagger_ms<=0` 关闭；等待中取消立即唤醒。
- 不变量：`active ≤ W ≤ T`、`ΣE ≤ T`（E≥2 ⇒ W*E≤T）；`reserved` 只登记**成功**输出（R-005，防幽灵跳过）。

### 2.6 自适应轮状态（`core/adaptive_exec.*`，只读分析）

执行器是**纯函数式状态机**（无隐藏状态）：`rounds_`（全局轮数）+ 当前 stage + `selected_`（last 语义）。
终止原因词表 11 个（`kAdaptiveReason*`）；三层上界：全局 `max_rounds` ≤32 / 每 stage ≤18 / `set` 1 轮；
`plateau` 额外提前终止。每轮：应用参数 → 校验（validate/preflight）→ 编码到候选临时文件 → **同步**评分
→ 决策。失败语义：`metric_unavailable stop`（默认，输出最后成功轮）/ `fail`；取消 = 丢弃全部候选、不落盘。
**本批新增 C 段 hooks**（`adaptive_exec.h`）：`on_plan`（一次，含规范化 policy_sha）+ `on_stage_start`
（每 stage 一次，含 `round_before`）；`AdaptiveRoundRecord` 末尾补 `effective_params/candidate_path`；
`AdaptiveRunResult` 末尾补 `selected_params_text/reason_detail`。**pipeline 只消费这些数据，不重复打印。**

---

## 3. UI 运行页状态机（`src/ui/page_run.cpp`）

### 3.1 行模型

| 层 | 类型 | 状态集 | 事实来源 |
|---|---|---|---|
| 文件组行 | `FileRow`（page_run.cpp:552） | `pp::FileState` + `started/terminal` + 共享段进度 | 文件级事件 |
| 输出行 | `OutRow`（page_run.cpp:509） | `OutKind{Queued,Active,Done,Failed,Skipped,Cancelled}` + 中间态位 `seen_encode/score_pending/meta_done_seen/skip_seen/score_wait` | `Progress`（输出级）+ `OutputDone` + 文件终态 |
| 运行级 | `RunState`（page_run.cpp:582） | `running/finished/cancel_requested` + `started_files/terminal_files` + `dirty_*` | `begin_run/on_event/end_run/reset` |

`OutKind` 终态判定单源 = `run_rows_model.h` 的 `settled_kind()`；完成度折算单源 =
`runpage::out_completion/run_frac`（可单测）。

### 3.2 事件 → 单元格映射（§5.3 C12 落地版）

| 输入事件 | 组行 | 输出行 | 消费点 |
|---|---|---|---|
| `Progress out=-1` 共享段 | 探测/解码→色彩/旋转（仅共享段真正进行中时） | 首个未启动行承载同文案 | `apply_progress` :1070 |
| `Progress out=j stage=Encode 0<f<1` | 「编码中 k/n」 | 「编码 NN%」（行数进 tooltip；合成→斜纹） | 同上 |
| `Progress Encode=1.0`（强制点） | 「评分中 k/n」 | 「评分中」（Accent+斜纹） | 同上 |
| `Progress MetaWrite=0/1.0` | 「写入中 k/n」/「编码中 k/n」 | 「写入」/「已完成」中间态 | 同上 |
| 首事件即 `MetaWrite 1.0` | 「编码中 k/n」 | 「跳过」中间态 | 同上 |
| `OutputDone`（首次，metric Off） | — | 结算该行（Done/Failed/Skipped/Cancelled + 大小/耗时/原因）；评分列「评分中」 | `apply_output_done` :1160 |
| `OutputDone`（第二次，含 metric） | — | 评分列同帧填分/「—」+原因 | 同上 |
| 文件终态 | 已完成/部分成功/失败/跳过/取消 | **幂等校准**：只补事实，不覆盖已结算状态 | `apply_terminal` :1224 |
| **终态后的过程事件** | **必须忽略（S-1）** | **必须忽略** | `PageRun::on_event` :2630–2638 |

### 3.3 dirty 双通道 + 总览

- 状态翻转（阶段切换/评分开始/输出完成/评分到达/终态）→ `mark_state` → `QTimer::singleShot(0)` 立即刷一帧；
  数值/百分比 → `mark_progress` + 60ms 定时器合并；`row_dirty` 逐文件标脏 → 只渲染脏行（O(脏行)）。
- 总览 kv 单源：总耗时（运行中本页时钟/结束取 `RunSummary`）、吞吐、平均、并行（已取件−已结算）、
  交错、线程预算、汇总（**按输出**计）、峰值内存/峰值线程（结束态才有真读数、否则「—」）。

### 3.4 生命周期（开始 / 取消 / 结束 / 重入 / 关闭）

| 阶段 | 不变量 | 落点 |
|---|---|---|
| 开始 | 外层闸门 `d.running/模型非空/有勾选/ready_to_start/cross_validate/GPS 预检/输出根探针`；`d.running=true` 在 `lock_for_run(true)`；`begin_run` 建行并清脏位 | mainwindow_run.cpp:629 / page_run.cpp:2576 |
| 取消 | 唯一入口 = 运行页「取消剩余」；`scheduler.cancel()` + （评分开时）`ScorePool.cancel_pending()`；按钮点击后**立即**禁用以防重复 | mainwindow_run.cpp:1110 |
| 结束 | `wait()` → 评分池 `drain` → `sendPostedEvents(MetaCall)` 排空在途事件 → `end_run` 定稿 → `sched.reset()` → 解锁 | mainwindow_run.cpp:1127 |
| 重入 | `on_start` 入口 `if (d.running)` 拒绝 + `start` 按钮在 `lock_for_run(true)` 时先禁用（修 4 的防御面） | mainwindow_run.cpp:1208 |
| 关窗 | `aboutToQuit`（仅评分运行注册）：`sched->cancel() + pool.cancel_pending() + wait_idle(30s)`；`~Scheduler` 再兜底 cancel/wait/retire/有界等 | mainwindow_run.cpp:678 / scheduler.cpp:526 |

**已知窗口**（不修，登记）：`on_scheduler_done` 里的 `wait()`/`drain()` 会阻塞 GUI 线程（drain 用分片
pump 缓解）；评分池 `drain` 60s + 30s 宽限是设计内行为，不属状态 bug。

---

## 4. 输出页 / 预设状态机（`src/ui/page_output.cpp` + `output_page_build.cpp`）

### 4.1 每格式卡状态 `FormatState`（`page_output_impl.h:389`）

| 字段 | 语义 | 写者 |
|---|---|---|
| `spec` | 会话快照（取消勾选/重建页签后恢复卡值） | `snapshot_state` |
| `form`/`bitdepth`/`profile` | 控件真源（卡值 = 控件，不是 `spec`） | 用户交互 |
| `extra` | 表单不认的键（预设带入的未知/跨技术键；往返不丢） | `apply_state_to_form` |
| `format_preset_path` | 当前选中的格式预设（会话态，不落 settings） | 格式预设行 |
| `adaptive_policy` / `adaptive_enabled` / `adaptive_parse_ok` | 自适应只读面 + 执行开关 | 预设载入 / 开关 |
| `rule_errors` | 运行前预检错误（`ready_to_start` 拒绝依据） | `update_rule_errors` |

**不变量**：
- I-O1 卡值真源 = `form->values()`+`bitdepth`+`profile`；`spec`/`extra` 只是快照与透传（"预设只是写入器"）。
- I-O2 `config_base()` 是**执行面门控**：`adaptive_enabled=false`（或策略解析失败/为空）⇒ 不下发
  `adaptive_policy`（引擎走单次编码旧路径，逐字节不变）。采集面（`spec_of/collect_preset`）仍保留策略原文。
- I-O3 `ready_to_start()` = 输出根绝对路径 + 有格式 + 模板合法 + 逐格式规则预检；运行期由
  `lock_for_run(true)` 禁用整页（运行中只读）。

### 4.2 格式预设行状态机

控件：`[下拉（160 = kFieldStd，首项「未选择」）][保存][另存…][删除][导入][导出]` + 注释位 + 自适应行。

| 事件 | 迁移 | 落点 |
|---|---|---|
| 页签建立/激活 | 发 `format_preset_list_requested(fmt)` → MainWindow 列举 `<data>/presets/formats/<fmt>/*.json` → `set_format_preset_list`（保留会话选中；列表里找不到 → 回「未选择」并清会话态） | page_output.cpp:1883 + mainwindow_presets.cpp:415 |
| 用户下拉选中 | `activated` → `format_preset_load_requested(path)` → 载入校验链（kind/version/逐键/跨格式/normalize/validate）→ 成功：`apply_format_preset`（落卡）+ `set_current_format_preset` + 注释位；失败：**显式中文报错 + 卡值零改动 + 下拉回原项** | output_page_build.cpp:866 / mainwindow_presets.cpp:503 |
| 保存（有选中） | 采集卡快照 → normalize/validate → 写盘（校验不过**不写**）；内置 `_builtin-` 只读 → 拒绝并提示用「另存…」 | mainwindow_presets.cpp:380/468 |
| 另存… | 命名（内置时默认名加「副本」）→ 同名二次确认 → 写盘 → 刷新列表 | :563 |
| 删除 | 绝对路径守卫 + 内置只读守卫 + 二次确认 → 删文件 → 若当前选中则清会话态+注释位 → 刷新列表 | :608 |
| 导入… | 读文件校验 → 目标路径同名二次确认 → 复制进 `<data>/formats/<imported fmt>/` → 刷新**该格式**列表 | :647 |
| 导出… | 采集卡快照 → 用户路径（同名二次确认）→ 写 JSON；不改变会话态 | :684 |

**不变量**：
- I-P1 失败不静默：载入/保存/导入/导出的失败一律显式中文错误，且**卡值/会话态零改动**（除「显式回原项」）。
- I-P2 内置预设只读（保存/删除拒绝，另存可用）；I/O 层同前缀守卫是第二道。
- I-P3 **卡值真源不被"来源标注"污染**：`format_preset_path` 只表示"本卡当前值的来源预设"（S-3 违反）。

### 4.3 全局预设 vs 格式预设 vs `last_preset` 覆盖顺序

- 全局预设（`PresetData` v2，整批）：`apply_preset` → 重建已选集合 → 逐格式 `apply_state_to_form`（后端/技术/
  位深/参数/档位/**自适应策略**）→ 全局字段（色彩/冲突/模板/色彩四选项/仅元数据）。
- 格式预设：只写**当前格式卡**（后端/技术/位深/参数/档位/自适应），**不动全局项**。
- `last_preset`：全局预设卡的会话高亮（`set_current_preset`），与格式预设行互不影响。
- 设计口径："与全局预设**无隐式叠加**"。⇒ 全局预设载入后，格式预设行的"来源标注"必须清掉（S-3）。

### 4.4 自适应只读面 + 开关

- 策略非空 → 摘要「目标 X / 最多 N 轮 / K 个阶段」+「启用自适应」开关（初值 = 预设 `adaptive.enabled`）；
  策略解析/校验失败 → 行内中文错误 + 开关禁用（不静默套默认）；策略空 → 整行隐藏。
- 开关只改**本次运行**的 `adaptive_enabled`；预设文件里的 `enabled` 不改（关掉开关再保存不丢策略文本）。
- **需求 3 可读性（本批）**：摘要 tooltip 末尾追加策略原文（真实换行，悬停可读完整 DSL）；
  格式预设下拉弹层自适应宽 + combo tooltip = 当前预设全名（160px 内被 elide 时悬浮可读）。

---

## 5. 缺陷台账（S-n）

> 严重级：**P0** 数据破坏/崩溃；**P1** 用户可观察的状态错乱（高概率）；**P2** 特定序列可观察；
> **P3** 内部/呈现轻微。状态：**已修** / **待批准** / **登记未修（原因）**。

| 编号 | 级 | 症状（用户可观察） | 触发序列 | 根因（file:line，修复前） | 修法 / 状态 | 回归证据 |
|---|---|---|---|---|---|---|
| **S-1** | P1 | 已完成/失败/跳过的行，组行胶囊突然退回「写入/色彩」进行中；完成态唯一形态丢失；总览百分比不倒退但行状态倒退 | 评分开 + 多输出运行；文件终态事件先到，评分池看门狗心跳（`Progress/scoring=true`）或取消残余过程事件后到 | `PageRun::on_event` 对迟到事件无终态门槛：`OutputDone` 分支后直接 `apply_progress` 且非终态分支 `file.state = ev.state`（旧 page_run.cpp:2601-2643）；心跳由 `score_queue.cpp` 看门狗每 150ms 发（`pipeline.cpp` submit_score_task 的 heartbeat） | **已修**：`OutputDone` 之后 `if (file.terminal && !is_terminal_state(ev.state)) return;`（page_run.cpp:2630-2638）。终态不可逆在**消费端**强制；`OutputDone` 仍放行（补评分列是输出级事实） | `--ui-smoke` 新增 `UI-SMOKE run-terminal-late:`（smoke_probes.cpp:5300-5380；喂终态后迟到心跳+迟到阶段事件，断言组行/行胶囊仍「已完成」）+ `tests/ui_smoke.py` 取值断言 |
| **S-2** | P2 | 点了「取消剩余」但全部文件已成功，结束态显示「已取消」，与「成功 N / 运行结束·可打开输出目录」同屏矛盾 | 收尾阶段（最后文件已结算、`on_scheduler_done` 未跑）点取消；引擎汇总 `cancelled==0` | `PageRun::end_run` 旧逻辑：`if (sum.cancelled > 0) s->cancel_requested = true;` —— 只在有取消时置位、**从不清除**按钮点击置的 true（page_run.cpp:2660 旧） | **已修**：`s->cancel_requested = sum.cancelled > 0;`（page_run.cpp:2669-2672）——"已取消"= 实际取消效果，不是"点过按钮" | `--ui-smoke` 新增 `UI-SMOKE run-cancel-noop:`（smoke_probes.cpp:5381-5425；点取消 + 终态全 Done + `sum.cancelled=0` → 断言 pill「已完成」、取消钮隐藏）+ python 取值断言；既有 `run-cancel:` 探针（真取消 2 文件）仍绿 |
| **S-3** | P1 | 格式预设行仍显示「来自格式预设：X」/ 下拉选中 X，但卡值已是全局预设的值；此时点「保存」会用全局值**覆盖预设文件 X**（用户数据被非预期改写） | 载入格式预设 X（JPEG 页）→ 载入全局预设（同格式）→ 观察格式预设行/点保存 | `PageOutput::apply_preset`（全局预设）只逐格式 `apply_state_to_form`，不清 `FormatState::format_preset_path`/注释位（page_output.cpp:1345-1434；`apply_state_to_form` 只管 adaptive 不管 preset 来源）；格式预设行的"保存"= 覆盖当前 `format_preset_path`（mainwindow_presets.cpp:380） | **已修**（Lead 批准，数据安全相邻）：`apply_preset` 末尾对每个**已选格式**清会话来源 → `set_current_format_preset(fmt,"")` + 注释位改「卡值来自全局预设：<名>」（无名字则空）⇒ 下拉回「未选择」、「保存」走「另存…」路径（page_output.cpp:1452-1471） | `--ui-smoke` format-preset 组新增 `global-preset-clears-source`（smoke_probes.cpp:11768-11799；断言 path 空/combo=未选择/注释位不含「来自格式预设」/点保存只发 save-as 意图）+ python 取值断言 |
| **S-4** | P2 | 格式预设下拉选「未选择」后，自适应开关仍开着、引擎仍按旧策略迭代（用户以为已停用） | 载入带策略的格式预设 → 下拉改「未选择」 | 下拉 `activated` 的「未选择」分支只清 `format_preset_path` + 按钮态 + 注释位，**不清** `adaptive_*` 会话态（output_page_build.cpp:866-879） | **已修**（Lead 在 `output_page_build.cpp` 落地）：该分支调用 `set_format_preset_adaptive(id,"",false)` + `emit config_changed()`（策略空 ⇒ enabled=false + 整行隐藏 + `config_base` 不下发） | format-preset 组新增 `unselect-clears-adaptive`（smoke_probes.cpp:11495-11560；断言 policy 空/enabled=false/整行隐藏/`config_base()` 该输出策略空）+ python 取值断言 |
| **S-5** | P3 | 格式预设文件被外部删除/改名后，列表刷新回「未选择」并清来源，但自适应开关/策略仍留在会话态（策略来源已无文件） | 选中格式预设 → 外部删文件 → 切页签/刷新列表；或 UI 删除当前预设 | `set_format_preset_list`：`index==0` 时清 path/note，不清 `adaptive_*`（page_output.cpp:1899-1914）；`format_preset_delete_file` 成功分支清 path/note 不清 adaptive（mainwindow_presets.cpp:636-643） | **已修**（与 S-4 同口径）：`set_format_preset_list` 在「刷新前有来源、刷新后来源没了」时清 `adaptive_policy/adaptive_enabled` + 重放自适应行；`format_preset_delete_file` 删除当前来源时同样清（注释「预设来源消失 ⇒ 策略会话态清除」） | 由 S-4 的 `unselect-clears-adaptive` 探针覆盖主路径；外部删除路径按同函数分支覆盖（无独立像素探针，登记为覆盖边界） |
| **S-6** | P3 | 运行结束（end_run 已把未回分收成「—/运行结束前未收到评分结果」）后，极少数迟到评分 `OutputDone` 可能把评分列重新填分；计数/汇总不回写（不双计） | 评分池 drain 超时强杀后仍有迟到任务（正常路径 drain 已排空 → 不触发） | `apply_output_done` 不检查 `file.terminal`（有意为之：正常路径需要终态后补分）；`on_scheduler_done` 的 drain+sendPostedEvents 是防线（mainwindow_run.cpp:1142-1171） | **已登记（Lead 批准，P3 已知项）**：迟到的是真分数、不回写计数，属展示层边界；正常路径 drain 已排空，不修 | — |
| **S-7** | P3 | 输出 `OutputDone` 若带未知/越界 `output_index` 且格式 id 匹配不到 → 事件被静默丢弃（该行卡在"评分中"直到终态兜底） | 仅引擎 bug/版本错配才会出现（正常路径 progress.output_index 恒 = cfg_index） | `PageRun::on_event` 的 `apply_output_done` 对 `index<0` 直接 return（page_run.cpp:1160-1162）；兜底按格式 id 匹配（可能多输出同格式误配） | **已登记（Lead 批准，P3）**：防御性路径，正常不可达；保留（加断言需引擎侧契约变更） | — |
| **S-8** | P3 | 自适应输出被取消时，进度条停在取消前进度（不虚增），但事件类型用的是 `on_output_failed`（语义应为 cancelled） | 自适应轮循环中取消 | `run_target_adaptive` 取消分支调 `ctx.mux->on_output_failed`（pipeline.cpp 自适应失败段）；本轮 `OutputDone` 状态是 Cancelled（UI 以 OutputDone 为准，故用户可见状态正确） | **已登记（Lead 批准，P3）**：`ProgressMux` 无 `on_output_cancelled` 迁移点（`OutState` 无 Cancelled 值）；要修需给 R-287 状态机加枚值，收益低 | — |
| **S-9** | P3 | 自适应输出成功时，编码阶段进度条/行数只按选定轮校正，中间轮不逐轮结算 → 行百分比在轮间可能"阶梯"（监控口径非错误） | 自适应多轮运行 | `run_target_adaptive` 复用同一 ProgressFn（`(round-1+f)/max_rounds` 刻度），轮间 `enc_frac` 单调但非真实行级 | **已登记（Lead 批准，P3）**：设计口径（§9.6「overall 不虚增」），tooltip 已标合成/评分中 | — |
| **S-10** | P3 | `end_run` 的防御性收尾（缺终态事件时补定稿）只置 `file.terminal=true`，不递增 `s->terminal_files` → `PageRun::files_terminal()` 读数在异常收尾时少计（页面显示不受影响） | 调度器异常/事件丢失（正常不到） | `PageRun::end_run` 防御循环（page_run.cpp:2665-2684）；计数只在 `apply_terminal` 递增 | **已登记（Lead 批准，P3）**：防御路径，改计数需防双计（正常终态事件下不能重复），风险>收益 | — |
| **S-11** | P3 | 重复载入同一个已选格式预设时，注释位从「已载入（已补齐 N 个默认项）」变为「来自格式预设：X」（一次性提示被覆盖，信息量下降） | 同一预设载入两次 | `format_preset_load_into` 末尾无条件 `set_format_preset_note(...)`（mainwindow_presets.cpp:552-558） | **已登记（Lead 批准，P3）**：设计上"载入即最新提示"可接受；保留 | — |
| **S-12** | P3 | 仅元数据模式：产物格式跟随源（混合批次），运行页格式行的 format_id 来自 **plan.format_ids 的配置序**，混合批次时行名可能不精确 | 仅元数据 + 混合格式批次 | `mainwindow_run.cpp:993-1001` 的 plan 采集对 metadata_only 收集**去重集合**，但 PageRun 行固定按第一项 index 对齐 | **已登记（Lead 批准，P3）**：现有 UI 在仅元数据下的行语义已有既有口径，本批不扩张 | — |
| **S-13** | P1（UI 呈现，用户可观察） | 格式预设行在 **1440×900（ui-smoke/ctest 窗口 & 常见小屏窗口）** 下宿主字段宽不足：`host-w=447` 而需求 = combo 160 + 5 按钮 262 + 6×间距 40 = **462（差 15px）** → QBoxLayout 把 combo 按 145 分位后又 `setFixedWidth(160)` 钳回 ⇒ 真 15px 横向挤压（按钮/文字被压，用户原话「文字截断」同源） | 打开输出页（jpeg 等任一格式页签）于 1440 宽窗口 → 看「格式预设」行 5 按钮 + 下拉 | `output_page_build.cpp` 的 `build_format_preset_row`：0.4.0.alpha2 把下拉从 Flex 改为固定 160（用户要求与参数行一致）+ 按钮按 sizeHint，但行内可用字段轨只有 447（窗口越窄越糟）；Lead 的「无重叠」像素证据取自 **1800 宽截图**，1440 下复现挤压 | **已修**（Lead 改 `output_page_build.cpp`，仅该行）：行内间距 8→6（省 10px）+「另存…」→「另存」（省 ~10px）⇒ 442 ≤ 447；语义/objectName 不变。探针 `preset-geom` 按生产页测量，修后 `=1` | 布局修复后 exe（13:48:16）ui-smoke：`preset-geom=1`、`checked=27 bad=0`、冻结末行恢复（§9） |

---

## 6. ui-smoke 崩溃归因（`0xC0000005 @ 0xada24`）：脏构建树假象，非代码回归

**现象**：`build/release-dev/photopipeline.exe --ui-smoke --inputs tests/golden/base` 在
`UI-SMOKE preview-zoom:` 之后（尚未打印 `preview-hotkey`）稳定 access violation。stdout 末行恒为：

```
UI-SMOKE preview-zoom: 1:1=64x64 fit=465x465 (source 64x64)
EXIT=-1073741819
```

**证据链**：

| # | 实验 | 结果 |
|---|---|---|
| 1 | Windows 事件日志（Application Error） | 4 次 `photopipeline.exe` 崩溃（12:40:44 / 12:40:55 / 12:41:07 / 12:48:30），**异常码 0xc0000005、错误偏移全部 0xada24**（同一地址）；说明是同一条确定性崩溃路径，不是随机内存踩踏 |
| 2 | 同一时刻跑 `build/m10/final` 旧二进制（同命令/同输入/同数据目录口径） | **完整通过**：`UI-SMOKE OK shots=12 pages=3`，exit 0（含 preview 段） |
| 3 | 三棵构建树二进制 SHA256（同源码尺寸均 5,088,768 B） | `release-dev=4FDC15AF…`、`m10/final=F3A0448A…`（= FINAL-VERIFY 记录值）、`iter1=338B83C4…` —— **全不同**。m10/final 与 iter1 的差异可由 HEAD 之后的 `ce5de10`（测试判据）等提交解释；**release-dev 与 iter1 同 HEAD 却不同** ⇒ 该树的对象/配置与干净树不一致 |
| 4 | 干净树 `build/iter1`（Lead 全量重建后） | `--ui-smoke` 通过、`UI-SMOKE OK shots=12 pages=3`、exit 0（Lead 实测；本文 §9 复跑待填） |
| 5 | 进程行为 | 崩溃后 WER 对话框挂起进程（CPU 0），此前观测到的 "hang" 即此；事件日志证明是 AV 而非死锁 |

**结论**：release-dev 是**增量构建树不一致**（脏树）导致的假象；`build/iter1` 干净树无此崩溃。
**不修改任何产品代码来"修"这个崩溃**；报告如实登记，并以干净树读数作为门禁口径。
（提示：`build/release-dev` 不应作为门禁树；本文后续所有读数一律 `build/iter1`。）

---

## 7. 本轮修复与机械探针

### 7.1 代码修复

| 文件 | 改动 | 对应 |
|---|---|---|
| `src/ui/page_run.cpp:2630-2638` | `PageRun::on_event`：`OutputDone` 分支之后加终态门槛 —— 文件终态后到达的**过程事件**（`Progress`/阶段）一律丢弃；`OutputDone` 仍放行（补评分列） | S-1 |
| `src/ui/page_run.cpp:2669-2672` | `PageRun::end_run`：结束态「已取消」取**实际取消效果**（`sum.cancelled > 0`），不取按钮点击 | S-2 |
| `src/ui/page_output.cpp:1452-1472` | 全局预设载入（`apply_preset`）后清格式预设会话来源（path/高亮/注释位 → 「卡值来自全局预设：<名>」）；「保存」因此走另存路径 | S-3 |
| `src/ui/output_page_build.cpp`（Lead 落地）+ `src/ui/page_output.cpp` | 「未选择」分支清自适应会话态（`set_format_preset_adaptive(id,"",false)`）；`set_format_preset_list` 在「来源消失」时清 `adaptive_*` 并重放自适应行；`mainwindow_presets.cpp` 删除当前来源时同样清 | S-4 / S-5 |
| `src/ui/page_output.cpp:1830-1838 / 1946-1952 / 1973` | 需求 3/5：格式预设 combo 弹层自适应宽（复用 `combo_popup_autosize` 单源）+ combo tooltip = 当前预设全名；`sync_format_preset_buttons` 末尾兜底同步（「未选择」等不经过 `set_current_format_preset` 的路径也能清 tooltip） | 需求 3/5（Lead 口径） |
| `src/ui/page_output.cpp:1877-1884` | 自适应摘要 tooltip 末尾追加**策略原文**（真实换行；成功/错误两分支都追加） | 需求 3「DSL 可读」只读呈现 |
| `src/core/pipeline.cpp:1869-1945` | 需求 1 辅助：`adaptive_param_value_text` / `adaptive_params_brief`（白名单键，实际存在才出）/ `join_text` / `adaptive_decision_reason`（英文 decision → 中文） | 需求 1 |
| `src/core/pipeline.cpp:2126-2200` | 需求 1：`run_target_adaptive` 接 `on_plan` / `on_stage_start`，增强 `on_round`（candidate/params/target/decision_reason）与 `adaptive summary`（selected_params/reason_detail/scores） | 需求 1 |
| `src/core/pipeline.cpp` include | `codecs/heif_profile.h`（`kHeifProfileParamKey`：基准参数白名单的 `profile` 别名） | 需求 1 |
| `src/ui/smoke_probes.cpp`（sec_k） | 清理 4 个未引用局部别名（C4189；零行为变化） | 构建告警清理 |

### 7.2 机械探针（`--ui-smoke`；`tests/ui_smoke.py` 前缀清单**只增**）

| 行 | 断言 | 文件:行 |
|---|---|---|
| `UI-SMOKE run-terminal-late:`（新增行） | 文件终态 Done → 喂迟到心跳（`Progress/heartbeat/scoring`）+ 迟到阶段事件（`Writing`）→ 组行胶囊与输出行胶囊**仍为「已完成」**；`ok=1 g-late="已完成" o-late="已完成"` | smoke_probes.cpp:5300-5380 |
| `UI-SMOKE run-cancel-noop:`（新增行） | 全部输出 Done 后点「取消剩余」→ `end_run(sum.cancelled=0)` → 状态胶囊「已完成」、取消钮隐藏、摘要「运行结束 · 可打开输出目录查看结果」 | smoke_probes.cpp:5381-5427 |
| `UI-SMOKE run-rerun-clean:`（新增行） | 完成一批（评分列 92.31/耗时有值）→ 同页 `begin_run` 重跑 → 评分列空、耗时/大小「—」、行胶囊回「排队」 **（重跑不残留上一轮）** | smoke_probes.cpp:5428-5500 |
| `UI-SMOKE run-restart:`（新增行） | 开始→取消（2 文件 Cancelled 定稿「已取消」）→ 再开始 → 状态胶囊「进行中」、取消钮可见可用、进度归「0%」 | smoke_probes.cpp:5501-5560 |
| `UI-SMOKE format-preset:` 既有行新增字段 `preset-geom=1` / `preset-tip=1` | 几何：combo 宽 == 同页参数行 combo 宽且 == `kFieldStd(160)±1`；导入/导出文案逐字「导入」「导出」（无「…」）；5 按钮 `width() >= sizeHint().width()`；**同父兄弟** `combo.geometry().right() < save.geometry().left()`；末钮右缘 < 字段容器（host）宽。可读性：载入后 combo tooltip == 预设全名；选「未选择」→ tooltip 清空 + 会话路径清空 + 注释位清空。**测量对象 = 生产页 `page_output`**（真实窗口 6px 滚动条 + 生产布局；不测独立 PageOutput fixture —— 实测 fixture 宿主 441 < 需求 462 会产生 13px **假重叠**，见下方"探针 fixture 修正"） | smoke_probes.cpp:11120-11320（geom，含诊断面 `scroll-w/window-visible/每控件 host 内 geometry+layout idx+spacing`）/ :11330-11370（tip） |
| `UI-SMOKE format-preset:` 既有行新增字段 `clears-source=1` / `unselect-adaptive=1` | S-3：全局预设载入后 path 空 / combo=未选择 / 注释位不含「来自格式预设」/ 点「保存」只发 save-as 意图；S-4：选中有策略预设 → 选「未选择」→ policy 空 / enabled=false / 行隐藏 / `config_base()` 该输出策略空 | smoke_probes.cpp:11698-11735（S-3）/ :11495-11560（S-4） |
| `UI-SMOKE run-geom:` 行新增字段 `late-terminal=1` / `cancel-noop=1` | 上述状态修复的**取值镜像**（防 C++ 断言被静默裁剪） | smoke_probes.cpp:5560-5580 |

`tests/ui_smoke.py`：新增 `STATE_FIX_LINE_PREFIXES`（四条新增行齐备 + 取值断言：`g-late`/`o-late`、
`pill`、`first-score="92.31"`/`rerun-time="—"`/`rerun-pill="排队"`、`restarted-pill="进行中"`/
`cancel-visible=1`/`percent="0%"`）、`RUN_GEOM_LATE_TERMINAL_RE`、`RUN_GEOM_CANCEL_NOOP_RE`、
`FORMAT_PRESET_GROUP_FIELDS` 追加 `preset-geom`/`preset-tip`/`clears-source`/`unselect-adaptive`；
**既有前缀/冻结末行/截图集零改动**。

**探针 fixture 修正（preset-geom，0.4.0.alpha2；Lead 像素证据 + 本批诊断字段定位）**：

- 旧探针在**独立 PageOutput fixture**（587 宽 / 平台默认 14px 滚动条 / 无窗口 QSS）上量：
  `host-w=441`，而 160(combo) + 262(5 按钮) + 40(6×间距) = **462 → 差 21px**；QBoxLayout 负空间把
  combo 按 139 分位（save 起于 147），QWidget 又被 `setFixedWidth(160)` 钳回 ⇒ **13px 假重叠**
  （诊断还显示 combo y=1 / 按钮 y=0 = 两次布局错位）。
- 真实 app 页（窗口 QSS 6px 滚动条 + 生产布局 + 全窗口列宽）宿主足够 → Lead 渲染像素实测无重叠、
  无截断（`build/iter1/shots/preset-row-lead1.png`；combo 右缘 abs≈1104 / 保存左缘 abs≈1106）。
- 结论：**不是产品布局 bug、不改 `output_page_build.cpp`**；几何断言改在**生产页 `page_output`**
  上测（切输出页 + jpeg 页签、滚动区/内容布局 activate + pump 收敛、同父兄弟 geometry 比较、
  row 从 combo 祖先链取），断言语义不变。探针诊断字段保留（红时可直接定位布局/滚动条问题）。

---

## 8. 需求 1：自适应迭代优化过程写入日志（实现说明）

### 8.1 落点与 sink

- 产生点：`src/core/pipeline.cpp` 的 `run_target_adaptive`（每 (文件 × 输出) 一次轮循环）。
- 数据来源：执行器 C 段 hooks（`core/adaptive_exec.h`，由 task-2 落地）——pipeline **不重复解析/不重复打印**。
- 日志 sink：`core/logger` → 当前运行的 `run-<时间戳>.log`（data_dir/logs；便携模式下 = exe 旁 `logs/`）
  → GUI 运行页「日志尾」卡（`MainWindow::Impl::push_log_tail()` 每 500ms 读最新 `run-*.log` 尾 8KB、
  取末 4 行；`PageRun::set_log_tail`）。**级别 = info**（默认 `log_level=info`，GUI 默认可见；
  `--dev` 同样默认 info，`--log-level debug` 才额外有 debug 行）。
- 结论：四类行**同时进入日志文件与 GUI 运行日志面板**（同一文件、同一行；面板只是尾部窗口）。
  取证：`--dev` 跑自适应策略 → grep `run-*.log`；GUI 面板显示逻辑由既有 `run-log-tail` 探针保证。

### 8.2 字段表

**adaptive plan**（run_policy 入口、任何轮次之前恰好一条；非法策略也会先有）：

| 字段 | 含义 |
|---|---|
| `src/out/output_index/format/backend/tech/bitdepth/pixels` | 输出定位与像素事实 |
| `policy_version / target / max_rounds / output` | 策略面事实（`output` = last） |
| `stages` | stage id 列表（逗号连接，执行顺序） |
| `policy_sha` | **规范化策略文本** SHA-256 前 16 位（执行器 `on_plan` 给；跨运行可对比） |
| `base_params` | 基准关键参数 `k=v,k=v`（白名单：quality_mode/quality/x265:crf/x265:qp/aom:cq-level/qp/distance/effort/chroma/preset/tune/lossless + `profile`；**只有实际存在的键**） |

**adaptive stage**（每 stage 开始前恰好一条）：

| 字段 | 含义 |
|---|---|
| `stage` / `round_before` | stage id / 该 stage 开始前已执行轮数（下一轮 = +1） |
| `selected` / `best_score` | 目前最高分轮（`roundN`，无则 `none`）/ 对应分数；pipeline 侧跟踪，与 `output last` 语义同源 |

**adaptive round**（每轮一条；既有字段全保留）：

| 新增字段 | 含义 |
|---|---|
| `params` | 本轮**有效参数文本**（如 `x265:crf=18`；set stage = `chroma=444,profile=Main444_10Still`；基线 init 轮 = 相对 base 的差量，可为空） |
| `candidate` | 本轮候选临时文件路径（同目录 `atomic_temp_path`；失败轮可能为空） |
| `target` | 目标分（便于逐行对照） |
| `decision_reason` | **中文**决策原因：`达标：91.2 ≥ 目标 90` / `改善：89.4` / `未改善：88.1 低于目标 90` / `评分不可用：<note>` / `编码失败：<note>`；词表外取值原样透出（不编造） |

**adaptive summary**（结束一条）：

| 新增字段 | 含义 |
|---|---|
| `selected_params` | 选定轮的有效参数文本（= 选定 record.effective_params；无选定为空） |
| `reason_detail` | 终止原因中文一句话（成功/失败/取消都填；英文稳定码仍在 `reason`） |
| `scores` | 各轮分数序列（`88.1,89.4,90.2`；评分不可用的轮记 `-`，与 round 行 `state` 互证） |

### 8.3 样例行（**真实 `--dev` 运行日志逐字**；两场景见 §9 证据）

**场景 A（webp 格式预设携带自适应；目标 90 可达）** —— `adaptive plan / stage / round / summary` 各一条：

```
13:07:07.477 [info] [4172] [pipeline] [pipeline.cpp] adaptive plan {src=tests/golden/base/rgb8.png out="build/state-machine/adaptive-A\\rgb8.webp" output_index=0 format=webp backend=libwebp tech=lossy bitdepth=8 pixels=64x64 policy_version=1 target=90 max_rounds=3 output=last stages=quality-up policy_sha=0098d5686b5c2060 base_params=quality=80,lossless=false}
13:07:07.477 [info] [4172] [pipeline] [pipeline.cpp] adaptive stage {src=tests/golden/base/rgb8.png out="build/state-machine/adaptive-A\\rgb8.webp" output_index=0 stage=quality-up round_before=0 selected=none best_score=""}
13:07:07.493 [info] [4172] [pipeline] [pipeline.cpp] adaptive round {src=tests/golden/base/rgb8.png out="build/state-machine/adaptive-A\\rgb8.webp" output_index=0 round=1 stage=quality-up key=quality value=85 params=quality=85 candidate="build/state-machine/adaptive-A\\rgb8.webp.7040.64b0954b50526a690-r1.tmp" target=90 score=100 state=ok bytes=108 encode_ms=2.908 metric_ms=12.55 decision=met decision_reason="达标：100 ≥ 目标 90" note=""}
13:07:07.494 [info] [4172] [pipeline] [pipeline.cpp] adaptive summary {src=tests/golden/base/rgb8.png out="build/state-machine/adaptive-A\\rgb8.webp" output_index=0 rounds=1 target=90 selected=round1 selected_params=quality=85 policy_sha=0098d5686b5c2060 final_score=100 final_metric=ok scores=100 overhead_ms=15.78 reason=met reason_detail="达标：分数 100 ≥ 目标 90（第 1 轮）" state=ok error=""}
```

**场景 B（JXL 独立策略；目标 100 不可达 → 3 轮 max_rounds 终止，输出最后一轮）** ——
验证 `scores` 序列 / `selected` / `selected_params` / `reason_detail`：

```
13:06:21.685 [info] [14728] [pipeline] [pipeline.cpp] adaptive plan {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 format=jxl backend="" tech=vardct bitdepth=16 pixels=64x64 policy_version=1 target=100 max_rounds=3 output=last stages=distance-up policy_sha=cbf69db423ebcdf7 base_params=distance=1,effort=7,lossless=false}
13:06:21.685 [info] [14728] [pipeline] [pipeline.cpp] adaptive stage {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 stage=distance-up round_before=0 selected=none best_score=""}
13:06:21.740 [info] [14728] [pipeline] [pipeline.cpp] adaptive round {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 round=1 stage=distance-up key=distance value=0.9 params=distance=0.9 candidate="build/state-machine/adaptive-B\\photo.jxl.1308.7b2a17bb0aebb7000-r1.tmp" target=100 score=96.99 state=ok bytes=947 encode_ms=19.93 metric_ms=12.56 decision=improve decision_reason="改善：96.99" note=""}
13:06:21.746 [info] [14728] [pipeline] [pipeline.cpp] adaptive round {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 round=2 stage=distance-up key=distance value=0.8 params=distance=0.8 candidate="build/state-machine/adaptive-B\\photo.jxl.1308.7b2a17bb0aebb7002-r2.tmp" target=100 score=97.45 state=ok bytes=939 encode_ms=18.27 metric_ms=9.309 decision=improve decision_reason="改善：97.45" note=""}
13:06:21.775 [info] [14728] [pipeline] [pipeline.cpp] adaptive round {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 round=3 stage=distance-up key=distance value=0.7 params=distance=0.7 candidate="build/state-machine/adaptive-B\\photo.jxl.1308.7b2a17bb0aebb7004-r3.tmp" target=100 score=97.48 state=ok bytes=926 encode_ms=17.56 metric_ms=11.19 decision=improve decision_reason="改善：97.48" note=""}
13:06:21.776 [info] [14728] [pipeline] [pipeline.cpp] adaptive summary {src=tests/golden/base/photo.jpg out="build/state-machine/adaptive-B\\photo.jxl" output_index=0 rounds=3 target=100 selected=round3 selected_params=distance=0.7 policy_sha=cbf69db423ebcdf7 final_score=97.48 final_metric=ok scores=96.99,97.45,97.48 overhead_ms=90.18 reason=max_rounds reason_detail="轮数触顶：已达全局上限 3 轮，未达标，输出最后一轮成功编码（第 3 轮）" state=ok error=""}
```

> 日志 sink 实证：`<out>/logs/run-20261101-130621.log`（dev 树）/ GUI 运行写 `<data_dir>/logs/run-*.log`；
> GUI 运行页日志尾卡 = 同一文件尾 4 行（见 §8.1）。`--ui-smoke` 的 `run-log-tail:` 探针证明面板渲染同源。

---

## 9. 验证与证据（干净树 `build/iter1`）

> 所有构建/测试经 `powershell -NoProfile -ExecutionPolicy Bypass -File build/iter1/build.ps1 ...`
> 串行锁（本机无 pwsh 7，实际执行 Windows PowerShell 5.1）。

| # | 门禁 | 命令 | 读数 | 证据 |
|---|---|---|---|---|
| 1 | 增量构建 | `powershell -NoProfile -ExecutionPolicy Bypass -File build/iter1/build.ps1 -Targets photopipeline` | 见"构建读数" | `build/state-machine/build-*.log` |
| 2 | ui-smoke 冻结末行 | `photopipeline.exe --ui-smoke --inputs tests/golden/base --shots <dir>`（QT_QPA_PLATFORM=offscreen；`env.py run` 包装） | **exit 0；末行逐字 `UI-SMOKE OK shots=12 pages=3`**；末行前 `UI-SMOKE m8-params: checked=104 bad=0` | `build/state-machine/ui-smoke-final4.out.log`（布局修复后 13:48 exe）；最终 exe 复跑见 §9.1 |
| 3 | ui-smoke 脚本口径 | `python tools/env.py run -- python tests/ui_smoke.py build/iter1` | **exit 0；`UI-SMOKE pass`**（冻结行 + 全部既有前缀 + 本批新增四条状态行/format-preset 全字段齐备） | `build/state-machine/`（命令输出）；脚本内置临时日志 |
| 4 | format-preset 全字段 | 同上 ui-smoke | `checked=27 bad=0 row=1 first=1 empty=1 prod=1 idle=1 collect=1 roundtrip=1 note=1 normalize=1 invalid-save=1 bytes=1 broken=1 kind=1 adaptive=1 switch=1 nopolicy=1 policy-err=1 builtin=1 builtin-idem=1 builtin-readonly=1 builtin-saveas=1 global-adaptive=1 adaptive-gate=1 preset-geom=1 preset-tip=1 clears-source=1 unselect-adaptive=1` | `ui-smoke-final4.out.log` |
| 5 | 运行状态机探针 | 同上 ui-smoke | `run-terminal-late ok=1`（g/o 均「已完成」）；`run-cancel-noop ok=1`（pill「已完成」）；`run-rerun-clean ok=1`（first-score 92.31 / rerun 空·—·排队）；`run-restart ok=1`（已取消→进行中 / cancel 可见可用 / 0%）；`run-geom checked=26 bad=0` | 同文件 |
| 6 | 需求 1 真实自适应运行（**已完成**） | `photopipeline.exe --dev tests/golden/base/rgb8.png --out build/state-machine/adaptive-A --preset tests/golden/smoke-adaptive/smoke-adaptive.json --workers 1 --log-level info` | **EXIT=0**；plan/stage/round/summary 四类行齐备（场景 A 原文见 §8.3）；`reason=met / selected=round1 / selected_params=quality=85 / scores=100 / reason_detail="达标：分数 100 ≥ 目标 90（第 1 轮）"` | `build/state-machine/adaptive-A/logs/run-*.log` |
| 6b | 同上（不可达标 → max_rounds） | `photopipeline.exe --dev tests/golden/base/photo.jpg --out build/state-machine/adaptive-B --format jxl --tech vardct --adaptive-policy tests/golden/smoke-adaptive/policy-notmet.dsl --workers 1 --log-level info` | **EXIT=0**；3 轮、`scores=96.99,97.45,97.48`、`selected=round3`、`selected_params=distance=0.7`、`reason=max_rounds`、`reason_detail="轮数触顶：…输出最后一轮成功编码（第 3 轮）"` | `build/state-machine/adaptive-B/logs/run-20261101-130621.log` |
| 7 | 崩溃归因复核 | 见 §6 | 脏树 `release-dev`：4 次同偏移 0xC0000005 @0xada24；`m10/final` 旧二进制通过；干净树 `iter1` 通过 | `ui-smoke-release-dev.log` / Windows Application Error / `ui-smoke-m10final.out.log` |
| 8 | clang-format 门禁（pinned 23.1.1） | `tools/bin/clang-format-win64.exe --dry-run --Werror --style=file <3 文件>` | `--dry-run --Werror` exit 0（`src/core/pipeline.cpp`、`src/ui/page_output.cpp`、`src/ui/output_page_build.cpp`；`smoke_probes.cpp` 早前已 0 违规） | 本机命令读数 |

### 9.1 exe 读数（本批最终验证载体）

| 项 | 值 |
|---|---|
| exe | `build/iter1/photopipeline.exe` |
| 时间 / 大小 | **2026-11-01 13:48:16 / 5,199,872 B** |
| SHA256 | **`D580AE56BAFFC6B8D896D4732934998D85830B69902E5C92DB55449C594623D2`** |
| ui-smoke | **exit 0；`UI-SMOKE OK shots=12 pages=3`**（`build/state-machine/ui-smoke-final4.out.log`） |
| tests/ui_smoke.py | **exit 0；`UI-SMOKE pass`**（`python tools/env.py run -- python tests/ui_smoke.py build/iter1`） |
| 说明 | 该 exe = Lead 布局修复（S-13）后构建；此后本批仅对 3 个文件跑 pinned clang-format（**纯排版、零语义变化**），最终集成 exe 由 Lead 的全量构建产出（SHA 以 Lead 门禁报告为准）。本批**全部逻辑修复**均已在该 exe 上验证。 |

**构建读数（整洁树 `build/iter1`）**：

- 全量/增量构建（Lead 发起、含本批与其它任务改动）：`BUILD-EXIT=0`（`[85/86] Linking CXX executable photopipeline.exe`）。
- 本批单独增量（`-Targets photopipeline`）过程中踩到并如实登记的环境问题：①一次因 S-3 编译错误失败（已修，
  修正 = 改调 PageOutput 成员，非语义变化）；②多次 `LNK1104 无法打开 photopipeline.exe` —— 原因 = 其它 agent
  正在跑 ui-smoke/E2E 占用 exe（Windows 不允许链接覆盖运行中的 exe）；用「等待 exe 空闲 → 拿锁 → 构建，
  失败重试」循环解决（Lead 已协调广播暂停运行）。
- 测量环境事实：本机 shell = Windows PowerShell 5.1（无 pwsh 7）；构建/测试一律经 `build/iter1/build.ps1` 串行锁。
- 最终门禁（ctest 全量 / golden / regression / tidy / format / HEIF 矩阵）由 Lead 在最终集成窗口执行，本文只记录
  本批写作用域内的读数（§9 #1–#8）。

---

## 10. 未决项 / 边界（如实）

1. **S-3 / S-4 / S-5 已修**（Lead 批准；S-4 的 handler 落点由 Lead 在 `output_page_build.cpp` 落地，
   其余在本文写作用域内）。S-3 属数据安全相邻（避免"保存"覆盖旧预设文件），已配 `--ui-smoke` 探针。
2. **S-6/S-7/S-8/S-9/S-10/S-11/S-12** 为 P3 级"**已登记（Lead 批准）的已知项**"（原因逐条见表）；
   其中 S-8 修法会动 `ProgressMux::OutState`（R-287 状态机），收益 < 风险，登记为长期债。
3. **TSan 未测**（本机无 TSan 运行面；CI nightly tsan job 自 #28 起 build 红，m10-report §4-2 同款未决）——
   本批对状态机的修改是**单线程 UI 逻辑**（GUI 线程独占），不引入新并发面；仍不宣称竞态已验。
4. **GUI 日志面板对 adaptive 四类行的可见性**：判据是"同一 `run-*.log` + info 级别 + 面板读同一文件尾"，
   未构造"GUI 面板显示 adaptive 行"的像素级探针（面板内容依赖真实运行产生日志；`--ui-smoke`
   的日志尾探针用合成行）。如需像素级证据，可加 dev-only 探针在 MainWindow 真跑一次小批后抓面板文本
   （代价：ui-smoke 时长 +数秒），本轮按"文件→面板同源链路"证据登记（§8.1）。
5. **`build/release-dev` 仍保留**（不删；Lead 口径），但**禁止作为门禁树**；崩溃结论以 §6 为准。
6. **自适应执行器 hooks 由 task-2 落地**：本批 pipeline 代码按 task-2 最终契约实现并已实跑验证
   （`on_plan`/`on_stage_start`/`effective_params`/`candidate_path`/`selected_params_text`/`reason_detail`
   全部出现在 §8.3 真实日志中）；若后续执行器改字段/时点，日志需同步适配。
7. **format-preset 行几何（S-13）已收口**：本批先实现探针（生产页测量 + 完整诊断字段），据此定位到
   **1440 宽窗口下宿主 447 < 需求 462 的真实 15px 挤压**；Lead 在 `output_page_build.cpp` 以「行内间距 8→6 +
   「另存…」→「另存」」修复后，`preset-geom=1`、冻结末行恢复（§9）。独立 PageOutput fixture 的 441/462
   差异同时作为"探针 fixture 必须与生产页同源"的教训记录在 §7.2。
8. **最终集成 exe**：本批验证载体 = 13:48:16 exe（SHA `D580AE56…23D2`）；此后仅有 3 文件的 pinned
   clang-format（纯排版）变更，最终全量构建与全门禁由 Lead 复核（ctest/golden/regression/tidy/format/HEIF 矩阵）。
9. **ui_smoke.py 的取值断言**与新行前缀已同步；`tests/ui_smoke.py` 既有前缀清单/冻结末行零改动。
