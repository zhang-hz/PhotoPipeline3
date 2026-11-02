# M14 = 0.4.2 裁定与追认记录（执行中滚动登记，Lead 收口）

> 体例同 `docs/m13-adjudications.md`：# | 裁定 | 理由 | 实施归属 | 状态。
> 权威链：AGENTS.md > docs/m14-tasks.md（本轮契约）> 本文件（裁定）> 长期设计文档。
> 本表登记：冻结面加性、用户可见语义变更、探针机械换锚（判据含义不变）、CI/夹具口径变更、
> 由用户需求直接导致的删除/简化。收口时逐条核销（状态：**已落地** / 执行中 / 如实未决）。

## 1. 冻结面加性（「加性 + 追认」纪律，AGENTS §5.1）

| # | 冻结面 | 裁定 | 理由 | 实施归属 | 状态 |
|---|---|---|---|---|---|
| A1 | `core/pipeline.h`（`RunScope`） | 结构体**末尾**加性追加 `int parallel_outputs = 0;`（0 = 旧行为逐字不变） | 需求 6：同文件多输出并行度需由自适应规划器下发；冻结面只走末尾追加（先例 M10-A20） | sched（task-5） | 执行中 |
| A2 | `ui/page_meta.h` | 末尾加性追加信号 `locate_file_requested(const QString &path)`（既有签名/信号零改动） | 需求 1/2：隐私卡「例外」列表点击应定位到对应文件（主窗选中+滚动）；新能力放加性面 | meta-core（task-1） | 执行中 |
| A3 | `core/thumbs.h` | `PreviewImage` 末尾加性字段 `icc` / `display_space`；`PreviewCache` 末尾加性方法 `set_display_profile()`（默认未设置 = 既有路径逐字不变）；新增 `core/display_profile.{h,cpp}`（Windows `GetICMProfileW` + lcms2 校验；其它/失败回退 sRGB 并记原因；线程安全缓存；离屏平台强制 sRGB 回退保冻结截图）。诊断开关 `PP_PREVIEW_CM=0/off`（默认不设，仅 A/B 取证）；显示变换大图走 4 行带 + 每线程独立句柄（lcms2 同句柄并发会退化，实测） | 需求 5：预览/缩略图需按源 ICC + 显示 profile 做真实色彩管理；默认路径零变化保证冻结断言与截图集不动 | color（task-4） | 已落地（test_color 1526 checks/0 failed；test_thumbs OK；真实桌面 A/B 见报告 §2.5） |
| A4 | `ui/thumbnails.cpp`（写作用域扩展） | 缩略图通道复用同一显示变换（仅宽色域 ICC 触发） | 需求 5 同根因（Qt Windows raster 不做显示色彩管理；R-309 挂 QColorSpace 像素零变化），46px 成本可忽略 | color（task-4） | 执行中 |
| A5 | `core/presets.h`（`PresetData`） | 末尾加性 `struct PresetOutputSource { std::string format_preset; };` + `PresetData::output_sources`（空 vector = 无来源；非空时与 outputs 同长、逐项可空）；`version` 仍 = 2；JSON 每 output 可选加性 `source_preset`（空不写 ⇒ 旧文件零字节变化、旧版可读） | 需求 3：大预设记录「来源格式预设名」（修「来源不可恢复/来源撒谎」）；加性+追认，先例 M10-A20 | preset（task-3） | 已落地（schema I/O + 单测绿） |
| A6 | `core/scheduler.h` | 既有 `alloc_threads` 声明/公式/单测**零改动**；自适应规划器新增独立 `core/thread_plan.{h,cpp}`（纯函数 + CPU 采样，单测穷举）；`RunSummary` 既有读数键语义不变（ΣE 计入 P×E 真实并发） | 需求 6：契约冻结公式保持不变（断言只增不减），新能力放新文件 | sched（task-5） | 执行中 |
| A7 | `core/progress.h` / `core/pipeline.cpp`（事件投递实现面） | 私有实现重构（`run_one_file`/`run_metadata_only` 入口把 EventFn 包成**单线程 drain 队列**：mutex+队列+drainer 令牌，不持锁调回调；事件不丢/不改序/同 run 内严格串行），公开契约签名/语义零改动 | 需求 7：ProgressMux/FileEvent 数据竞争（TSan 9 条：编码 worker/SynthPump/心跳/评分池多线程并发回调 vs 消费端无锁）根治 | ci（task-6） | 执行中 |
| A8 | CMake / `PP_SSIM2_PRIVATE_TUS` 宏配置（hwy 隔离） | 该批 TU COMPILE_DEFINITIONS 加 `hwy=pp_ssim2_hwy;HWY_HEADER_ONLY=1`（**不加** `HWY_COMPILE_ONLY_STATIC`，M12-D26 已证伪红线）；新增 `src/core/metric/hwy_bridge.cc` 在 pp 命名空间把 `GetChosenTarget/SupportedTargets` 转发真实 libhwy（同一份 ChosenTarget 状态/CPU 探测 → 动态派发目标与修复前逐位相同 ⇒ 评分零漂移）。根因 = jpegli/libjxl 预编译 TU 带 `HWY_DISABLED_TARGETS(…AVX3…)`（dispatch 表 AVX3 槽 nullptr）与未禁用 AVX3 的本批 COMDAT `hwy::ChosenTarget::GetIndex()` 相撞，AVX-512 runner 上 jpegli `VectorSize()` 取空槽 SEGV；MSVC 标 AVX3 BROKEN ⇒ 仅 Linux/AVX-512 可复现 | 需求 7：hwy dispatch ODR UB 根治；「CI 必须全绿」且评分数值不得漂移 | ci（task-6）→ Lead 落 CMake | 执行中（CMake patch 已落） |
| A8b | CMake（`PP_SANITIZER_BUILD` 检测条件） | sanitizer 检测加 `OR PP_ASAN`（MSVC `/fsanitize=address` 预设此前得不到该宏 → 本机 ASan 下 fastcolor 吞吐断言恒假红，无法做 nightly 等效验收）。**与 M12-A12 同口径 scoping，不是放宽**：release/release-dev 仍无该宏、判据逐字保留 | 需求 7：本机 ASan 门禁可作为 Linux ASan 的等效证据；不改变任何优化构建判据 | ci（task-6）→ Lead 落 CMake | 已落地 |
| A8c | `src/mapwidget/mapwidget.cpp`（写作用域扩展） | `~MapWidget()` 在 `Impl` 析构前 `removeEventFilter(d.filter)` + `d.filter->impl = nullptr`（EventFilter 为 q 的子对象，晚于 Impl 销毁；期间事件投递会读悬垂 `impl`） | 需求 7：MSVC ASan 实测 `heap-use-after-free src/mapwidget/mapwidget.cpp:317 EventFilter`（真实生命周期缺陷，非测试问题） | ci（task-6） | 执行中 |

## 2. 用户可见语义变更（用户需求直接导致）

| # | 变更 | 理由（用户原文/需求） | 实施归属 | 状态 |
|---|---|---|---|---|
| A9 | **元数据页无选中 = 空态 + 所有编辑控件禁用 + 绝不写任何文件**；删除 0.4.1「无选中回退首个文件」的编辑路径。文件列表由空变非空时自动选中首行（不在运行期；用户主动清空后不自动再选） | 需求 1：「右侧启用的修改，仅应用到选中图片」——回退首个文件违反该语义（0.4.1 探针未覆盖真实 MainWindow 选择接线） | meta-core（task-1） | 执行中 |
| A10 | **元数据页多选逐字段聚合**：关键信息用实际 probe 元数据、生效值/输入面/开关/标签同值显示值、异值「多个值」；>8 不再假 mixed（probe 覆盖全部选中项，只读小表仍可截断）；复选框勾选 ≠ 编辑作用域 | 需求 1 用户原文「多张一致显示一致值、不一致显示多个值」 | meta-core（task-1） | 执行中 |
| A11 | **「批量 GPS 规则」概念整体退役**：GPS 卡坐标/海拔/方位/时间戳 = 当前选中图片的编辑面；删 rule_toggle/rule_host/「批量 GPS 规则·已启用」文案/「继承批量规则」三态（ExifEditor 改「不修改/设置为/清除」）/「忽略全部批量规则」/「批」命中 pill | 需求 2 用户原文「批量GPS规则，这个还有什么必要？」——逐选中编辑已覆盖，旧概念误导且与唯一可写真源（FileEntry.exception）矛盾 | meta-cards（task-2） | 执行中 |
| A12 | **ExifEditor 语义与外观变更**：`exif_editor.h` 仅注释更新（冻结面接口零改动）；旧 objectName/状态词条（`kGpsRule/kGpsRuleToggle/kGpsEnable/kPpMetaGpsLat/Lon/Alt/Dir` 等）在产品代码零引用后退役（`meta_probe_context.h` 由 meta-core 清）；`exif_roles::batch` 常量保留但标注退役（恒 false）；旧「继承批量规则/覆盖/清除」三态 →「不修改/设置为/清除」；OK 返回与旧 `MetadataOverride` 合并（六 enable/ignore_batch/旧 edits 全保留，MainWindow 手抄退役） | 需求 4「元数据详细编辑页面深度检查逻辑」；0.4.1 由 MainWindow 手抄六字段的脆弱实现退役 | meta-cards（task-2） | 已落地（构建/ui-smoke 元数据面绿） |
| A13 | **大预设来源显式化**：载入后格式预设行不再假装高亮 default；combo 无选中 + 占位「来自大预设：<名>」+ 注释位写「基于格式预设 <stem> / 自定义参数」+ 保存禁用；显式重选格式预设恢复绑定；绑定落点 = 绑定绝对路径（修「保存写到新文件」bug）；dirty 指示；预设名允许中文；加导入/导出；rules 替换选中文件既有修改前确认（仅当有损失风险） | 需求 3「大预设的逻辑也重新设计。回顾大预设最初的设计目标」——最初目标 = 一键复用整套批输出配置 + save→load 零丢失（core/presets.h 头注释）+ 与格式预设无隐式叠加；现状存在来源撒谎/不可恢复/保存态危险多重矛盾 | preset（task-3） | 已落地（format-preset 44/44 绿 + 单测 18 CASE） |
| A14 | **rules 载入无选中时不再静默应用到首个文件**：改为显式提示只应用输出配置（启动恢复只 log） | 需求 1 同源（0.4.1 报告 §4.7 已登记边界）；与 A9 一致 | preset + meta-core | 执行中 |

| A24 | 版本 bump 0.4.2 + 回归基线机械换锚 | `CMakeLists.txt` `project(PhotoPipeline VERSION 0.4.2)` + `PP_VERSION_LABEL ""`；`vcpkg.json` = 0.4.2（`ci-versions-check` OK）；`tools/baseline/golden.windows.log` 经 `regression.py --update`（归因：仅 `version {lib=app version=…}` 行 12 行，行为 0 变化）；`tools/baseline/golden.log`（Linux）因本机无法运行 Linux 二进制，采用**字节级机械换锚**（同 M13-A13 先例：逐行确认仅 app version 段变化），由 nightly Linux regression 事后背书；sha256 变化记录进 0.4.2 报告 | 发行版本单源（AGENTS §5.4）；AGENTS §5.3 重播种前归因（仅版本行，无行为变化） | Lead | 已落地（win `fa00a041…`→`4a671b46…`；linux `15b7aee3…`→`e65f52c2…`；隔离复跑 zero diff） |
| A25 | 构建卫生（生成头依赖）| M14 集成期实测：版本 bump 后增量 ninja 未重编 `main.cpp`（`gen/core/version.h` 为新生成值但 `--version` 仍报旧值）⇒ 门禁读数一律取 **`--clean-first` 构建树**；发布/CI 冷构建不受影响。集成期最终树即 clean-first 构建（随后仅做 tidy 真修的小增量重建，`--version` 复验 0.4.2） | M13-A15 同类构建卫生事件；「CI 必须全绿」要求门禁读数不可被增量陈旧对象污染 | Lead（集成期） | 已落地（登记为注意项，未改构建系统） |
| A28 | main-full `linux` job 硬顶 20→25min（预算校准） | run `41499971530` 实测：0.4.2 新增 8 个源文件/测试（thread_plan/event_stress/display_profile/hwy_bridge 等）+ release/release-dev 双树全量构建与 ctest，使 linux job 越过 20min 硬顶被取消、`gate` 随之红（唯一红点，无产品失败面）。处置 = 硬顶 20→25（与 windows 30 / tidy 25 同表；软预算 12min 仍为目标）；**判据/步骤/断言零改动，只动 wall-clock** | 用户需求 7「CI 必须全绿、必须高效」；M12-A10 预算校准先例（判据零放宽、只动 wall-clock） | Lead（tag 后） | 已落地（YAML 解析 10 jobs OK；重跑读数以 Actions 为准） |
| A29 | 探针 lambda 悬垂捕获（`smoke_probes.cpp` sec_h）修复 | run `41505011656` linux `ui_smoke` **exit -11（SIGSEGV）**；根因 = sec_h 用 `QObject::connect(&page, ..., [&commits]...)` 捕获**栈上局部**且连接在 block 结束后仍存活（PageMeta 长命）→ 后续任何 `selection_edits_committed` 发射写已出作用域栈变量。修法 = 保存 `QMetaObject::Connection` 并在 block 结束前 `disconnect`（模式对齐同文件既有 `save_conn` 用法）。本机 MSVC ASan 复现 `stack-use-after-scope smoke_probes.cpp:13209`，修复后 ASan/dev ui-smoke 均通过 | 「线上红 = 必须修根因」；ASan 门禁价值实证（探针代码 UB 会让 Linux ui_smoke 段错误 → gate 红） | Lead（tag 后） | 已落地（ASan + release-dev ui-smoke 复验通过；CI 重跑读数以 Actions 为准） |
| A31 | `rtl-mirror` geom-mirror 行宽口径（Linux 字体） | run `41510237748` linux ui_smoke 2 红：`geom-mirror pp-meta-delta-years/hours ltr=(0,0 88x24 row=248) rtl=(197,0 ... row=248)`。根因 = Linux Noto CJK 字体下「日期偏移 3×spin88」行内容宽（≈285）> 行宽（248）⇒ RTL 把内容整体右对齐到内容坐标（197 = 285−88），旧探针只用 row_w 镜像（期望 160）⇒ 假红。修法 = 行宽取 `max(行控件宽, 行布局 minimumSize().width())`（内容坐标系），LTR/RTL 两侧内容宽仍须一致 + 镜像 ≤2px。**判据语义不变**（固定宽控件行内 LTR↔RTL 镜像；容差 ±2px 不动） | 「CI 必须全绿」；跨平台字体度量差异导致的探针锚点口径（非产品布局变更；真实行几何与 0.4.1 同源） | Lead（tag 后） | 已落地（dev + ASan ui-smoke `rtl-mirror checked=18 bad=0`、冻结末行绿；CI 重跑读数以 Actions 为准） |
| A30 | `theme-equiv` 探针三阶去抖（ASan/高负载） | ASan 树 ui-smoke 仍偶发 `theme-equiv-widgets`（隐藏页整页宽差 20px；两快照各自「稳定」但中间态跨多轮事件循环）。修法 = `settle_layout` 连续 3 轮全页切页 + 逐页/根布局 `activate()` + processEvents；快照取「连续 3 次一致」；上限 80×80ms。**判据/容差零改动**：仍要求 Dark→Light 全部 1154 个 pp-* 控件逐项等价 | 「CI 必须全绿」；ASan 慢速环境暴露的布局落地时序（非主题差异），只改取样稳健性不改判据 | Lead（tag 后） | 已落地（dev + ASan ui-smoke `theme-equiv+nowrap checked=3 bad=0 / equiv=1154`） |
| A27 | `tools/ci-format.sh`（本地/CI 同源格式门禁） | **纳入 untracked 新文件**（`git ls-files --others --exclude-standard` 与 tracked 并集、`sort -u`）：旧实现只扫 tracked ⇒ 本地在 `git add` 前跑 `--write` 会静默跳过新建 .cpp/.h（v0.4.2 tag 的 `main-full` lint 即因此红：新建 `src/core/thread_plan.cpp` 的一行 CJK 注释换行未被本地门禁覆盖）。修复后本地 = tracked+untracked，CI = tracked ⇒ 两边仍是同一条判据；tag 后主分支提交修复（不入 tag，发行产物不受影响） | 「CI 必须全绿」；「本地绿 → CI 绿 是同一条判据」（AGENTS §7.9）——本次暴露的是判据覆盖面缺口，不是放宽 | Lead（tag 后） | 已落地（格式写回 + 修复后格式门禁 0 diff；main-full 重跑读数见报告 §2.9/§4） |
| A26 | tidy 棘轮处置（14 violations / +19 告警） | **不 `--update` 重播种**（保持棘轮语义）：产品代码逐条真修 —— `thread_plan` 去掉可互换的未用 `backend_id` 形参、`display_profile` 修 `use-after-move`（先转换/判空再 move）+ `no-automatic-move`、`scheduler` 两处 `(x+0.5)` 强转改 `std::lround`、`meta_state`/`preset_io`/`mainwindow_presets` 的 `const` 阻移动与 `qsizetype→int` 收窄、`meta_card_info` 合并同体分支；测试侧**新代码告警**：`reserve`/宽化字面量/收窄显式化真修，`test_event_stress`/`test_thread_plan` 入口 `exception-escape` 与两处测试辅助 `easily-swappable-parameters` 采用**带理由的行级 NOLINT**（M13 先例：结构性豁免，产品代码不豁免） | 用户需求 7「CI 必须全绿」；AGENTS §6.4 棘轮「新代码 0 告警」；重播种会掩盖真实告警（含一条 use-after-move 真缺陷） | Lead（冻结期集成修复） | 已落地（终判读数见报告 §2.0） |

## 3. 探针机械换锚（判据含义不变，不做放宽）

| # | 探针/夹具 | 变更 | 归因 | 状态 |
|---|---|---|---|---|
| A15 | `--ui-smoke` 文件列表/meta 区段 | `sec_h`「无选中 → 首个文件 +(1/N)」换锚为「无选中 → 空态 + 控件禁用」；GPS 卡旧 rule_toggle/view_* 断言换锚到新控件（编辑输入直显 + `pp-meta-gps-effective` 只读行）；新增真实 MainWindow 选择接线探针（行选中/多选/清空） | A9/A10/A11 的行为契约变更；断言不放宽、只换锚 | meta-core / Lead | 执行中 |
| A16 | `smoke_probes.cpp` 时序去抖（Lead + meta-core） | run-guard 固定 `pump(120)` → `wait_for(running, 3000)`；filelist 节头/展开态固定 `pump` → 期望态谓词等待（超时后原断言照旧红）；诊断串 `r%d` → `r%1` 笔误修正；**theme-equiv 暗亮等价去抖**：两快照前 `settle_layout`（等 meta 页 probe 收敛 → 逐页 setCurrentIndex+pump+`layout()->activate()` → 回原页 → processEvents）——隐藏页 deferred layout/异步 probe 落地导致整页同向 +10px，去抖后 1154 控件逐项等价（判据/容差零改动）；**meta-follow-live** 契约换锚（旧「无选中→首文件(1/N)」→ 新「空态 count=0/hint=未选择文件」+ select_row(0) 活体接线）；**rtl-mirror geom-mirror** 局部 PageMeta 显式喂选中（恢复行可见，容差 ±2px 不动） | docs/m11-report 已登记的高负载偶发；「CI 必须全绿」要求消除已知 flake；theme-equiv 为隐藏页布局/异步时序非主题差异 | Lead / meta-core | 已落地（冻结树读出后回填） |
| A17 | `tests/ui_smoke.py` | 只增字段/前缀：meta-selection 新断言字段（19 项，含真实 MainWindow 端到端 5 项 + 过滤态防误写 m6）；新增 `meta-no-selection` 前缀行；format-preset 新字段 `global-source`/`global-source-custom`/`global-source-rebind`/`preset-binding`/`preset-dirty`（44 项，下限 35→43）；冻结末行/截图集不动 | A10/A13；防静默跳过 + 新能力机械化 | meta-core / preset / Lead | 已落地（冻结树读出后回填） |
| A18 | `tools/regression.py` | 规划轨迹走 debug 级日志（info 级基线不进新行/键）；既有 N11 归一化不动 | 需求 6：避免新增线程读数破坏 zero diff；判据零放宽 | sched | 执行中 |
| A19 | CI 门禁 | 只修 bug/防空跑/并行缓存（判据零放宽）；nightly asan/tsan 真绿 | 需求 7「CI 必须全绿，必须高效」 | ci（task-6） | 执行中 |

## 4. 目录/构建纪律（用户直接要求）

| # | 裁定 | 理由 | 状态 |
|---|---|---|---|
| A20 | 开工清空 `build/`（50.6GB）、`dist/`、`.cache/`、`.pp_test_tmp/`、`debug/*/`；保留用户数据 `super/` 与必要基础设施 `vcpkg/`、`vcpkg_installed/`、`.toolchain/`。发行前 `build/` 收敛到最小必要集合（release / release-dev） | 需求 8「先精简项目目录，build 这种垃圾堆清理一下，先清空 build」 | 已执行（清空） |
| A21 | 「依赖 build/ 临时文件」审计：产品代码/脚本/测试**零功能依赖**（仅历史证据路径引用与 CI 自身构建目录约定）；`tools/heif_matrix_check.py` 引用的 `build/analysis-heic/...`、`build/matrix/...` 证据路径改为缺失容忍/跳过并如实日志 | 需求 8「如果当前有依赖 build，修复。不应该依赖这种临时文件」 | 执行中（ci） |

## 5. 债务登记口径变更（需求 7）

| # | 裁定 | 理由 | 状态 |
|---|---|---|---|
| A22 | 不再以「设计债」为不修理由。AGENTS §11 的既有条目收口时逐条改写为 **已修 / 已证伪（当时记录已失效）/ 如实未决**；本轮必须清零两项真实债（hwy ODR、ProgressMux 竞争）| 用户原文「修复所谓的设计债，不存在这种东西。CI必须全绿，必须高效」 | 执行中 |
| A23 | 复核确认已失效的历史债务记录（待收口时以代码/读数取证）：① `smoke_probes.cpp` 已 PP_BUILD_DEV 门控（发布构建不含探针代码，原「7600 行编进发布二进制」失效）；② `mainwindow.cpp` 现 1864 行（探针已拆出，原「1.1 万行上帝对象」失效）；③ 回归基线已 N11 归一化线程读数（原 runner-shaped 耦合已处置）；④ 代码零 `TODO(` 标签 | 同上；「不存在这种东西」= 先核实记录真伪，不删没修的账 | Lead | 执行中 |

---

*本表随执行滚动追加；最终状态与证据链接由 Lead 在 0.4.2 发行报告中复核。*
