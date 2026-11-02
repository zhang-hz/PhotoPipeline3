# M14 = 0.4.2 任务与契约（元数据作用域 / 大预设 / 预览色彩管理 / 调度自适应 / 真实债清零 / 发布）

> 状态：**本轮执行中**（2026-11-03 起草，Lead 定稿）。本文件 = 本轮唯一契约面（C0…C8）。
> 权威链：AGENTS.md（纪律总纲）> 本文件（本轮契约）> docs/design.md（长期语义）。冲突时以
> AGENTS.md 的冻结面/证据纪律为准，并回改本文。
> 上一轮契约：docs/m13-tasks.md（0.4.1）；裁定：docs/m13-adjudications.md。本轮裁定表 =
> docs/m14-adjudications.md（执行中滚动登记，Lead 收口）。

## 1. 用户需求原文（2026-11-02，8 条）

1. **元数据页面逻辑仍然错误**：右侧启用的修改**仅应用到选中图片**。选中可以是单个也可以多个：
   单选 → 仅应用到单个；多选 → 应用到多个。同时右侧信息：单选显示这张照片的信息；多选显示
   多张照片的信息——同一字段多张一致显示一致值、不一致显示「多个值」。这就是最正常最普遍的逻辑。
   **深度梳理元数据页面的逻辑。**
2. 元数据页面其他地方边边角角的逻辑也要梳理，比如「批量 GPS 规则」，这个还有什么必要？
3. **大预设的逻辑也重新设计。回顾大预设最初的设计目标。**
4. 元数据详细编辑页面深度检查逻辑。
5. 预览窗格显示图片的色彩进行色彩管理：需要在屏幕上显示出照片真正的色彩；现在色彩很灰，
   没有正确处理 ICC 和色域。
6. 任务并行的逻辑进一步优化：当前固定每图片 2 种并行？不论图片有几个？经常太多或者经常填不满。
   结合编码器特性、CPU 实时状态优化。例如 jxl 有时可以吃满、有时只用单线程。发挥软件调度能力，
   但也不能太激进。
7. 修复所谓的设计债，不存在这种东西。**CI 必须全绿，必须高效。**
8. 先精简项目目录，`build` 这种垃圾堆清理一下，先清空 `build`。如果当前有依赖 `build`，修复。
   不应该依赖这种临时文件。**完成后发布 0.4.2。**

## 2. 全局纪律（C0）

- **冻结面只走「加性 + 追认」**：`core/types.h`、`core/params.h`、`codecs/encoder.h`、
  `core/pipeline.h`、`core/scheduler.h`、`core/fsops.h`、`core/metadata.h`、`core/presets.h`、
  `core/settings.h`、`core/thumbs.h`、`core/colormanager.h`、`mapwidget/coord.h`、
  `mapwidget/providers.h`、`mapwidget/mapwidget.h`、`ui/page_meta.h`、`ui/formkit.h`、
  `tests/golden/SCHEMA.md`、UI 冻结 `pp-*` objectName、`--ui-smoke` 冻结截图集与末行——
  只允许**末尾追加**（结构体字段/声明/新文件承载），既有签名与语义零改动；逐条登记
  `docs/m14-adjudications.md`。
- **显示 = 写入同源**：任何「生效值」显示仍必须出自 `pp::preview_effective()` 一类的单一合成源；
  唯一可写状态 = `FileEntry.exception`（`std::optional<pp::MetadataOverride>`）；UI 不得维护第二份
  可写状态。
- **断言只增不减**：既有单测/金样/UI 探针断言不得为让新代码变绿而放宽。语义确需变更的探针按
  「机械换锚」处理（判据含义不变，只换锚点/字段名），逐条在 `docs/m14-adjudications.md` +
  `docs/0.4.2-report.md`「偏差与追认」登记归因。
- **金样/夹具零改动；回归 zero diff**。任何引擎行为变化必须有明确理由 + 双重背书口径说明。
  `SMOKE total=20 pass=20 fail=0`、`UI-SMOKE OK shots=12 pages=3`、`--version` 单行格式逐字不变。
- **CI 判据零放宽**：只允许修 bug、补空跑防线、并行化/缓存化（不改变判据含义）。「设计债」不再
  作为不修理由：本轮登记的既有真实债（A7：hwy ODR / ProgressMux 竞争）必须修复或给出可验证的
  根治方案；不允许新增「顺延」条目。
- **证据文化**：每条验收命令 + 读数进报告；未覆盖/未决如实列 `docs/0.4.2-report.md` §4。
- **目录纪律**：`build/` 是临时构建产物（gitignored），**任何产品代码/脚本/测试/打包流程不得依赖
  仓库内预置的 `build/` 内容**；构建树由命令现场生成。本轮开工已清空 50.6GB `build/`，发行前再收口
  到最小必要集合。
- **协作纪律**：teammate 不得 `git commit/push/tag`；Lead 统一收口。临时脚本/证据放
  `build/<task-id>/`、`.cache/`，不入库。

## 3. C1 元数据页：选择作用域 + 逐字段聚合（需求 1）

### 3.1 作用域（唯一口径）

- **编辑作用域 = 文件列表「行选中」集合**（ExtendedSelection / SelectRows；Ctrl/Shift 多选）。
  不包含复选框勾选集合（勾选 = 参与运行，语义独立，不得静默变成编辑作用域）。
- **单选 → 仅该文件**；**多选 → 全部选中文件**；**无选中 → 页面空态，所有编辑控件禁用，
  任何控件路径不得写任何文件**。删除 0.4.1 的「无选中 → 回退首个文件」编辑路径
  （`sync_selection()` 的 fallback 项只可用于只读呈现的旧口径整体退役；若保留展示必须与写入隔离，
  推荐直接空态）。
- 为体验补一条正常逻辑：文件列表由空变非空时自动选中第一行（若低风险实现；否则如实登记）。
- 文件列表过滤/分组视图下：行号必须经完整
  `proxy->mapToSource(groups->mapToSource(idx))` 链映射到源行；分组头不得被当作某张照片（不得
  触发对首个文件的写入）；选中集合去重、保持列表序。

### 3.2 显示聚合（逐字段三态）

- 三态 = absent（所有选中文件都未设置）/ uniform（同值）/ mixed（至少两值不同）。
- **右侧信息（只读）**：关键信息卡逐字段聚合**选中照片的实际元数据（probe）**——拍摄时间/相机/
  镜头/曝光/尺寸/色彩/文件/GPS/白平衡/评级；uniform → 值，mixed → 「多个值」+ tooltip 逐文件值
  （≤8 条 + 「…还有 N 个」）。生效时间/GPS 生效坐标等有「原→生效」语义的字段同理。
- **右侧输入面（可编辑）**：时间卡（模式/Δ 六分量/时区）、GPS 卡（lat/lon/alt/dir/ts）、隐私卡
  （四开关 + 标签/关键字）均按选择项聚合装载——uniform 显示值；mixed 显示「多个值」（线输入框空
  文本 + placeholder + `ppMixed` 属性；spin = `DeltaSpin::set_mixed(true)`；开关 = `partial` 三态视觉），
  单值工具提示保留逐文件值。
- 卡头「启用」开关三态：全开=on / 全关=off / 部分=partial；开 → 对全部选中置 enable（不凭空造值）；
  关 → 清除选中文件该卡全部字段 + enable=false。
- 不得出现「显示一个文件的值、写入另一个文件」的两张皮；显示的状态必须可由选择集 + override 复算。

### 3.3 提交语义（字段级、不串扰）

- 每个控件变化 = 字段级合并（只改被编辑字段），逐选中文件各算各的 base，不把某文件的值复制给
  另一文件除非用户显式编辑该字段。
- 空 override 归一为 `nullopt`；每次提交 = 模型一次增量写入（`selection_edits_committed` →
  `FileListModel::set_exception`）+ 聚合刷新；不得全量重装重置正在输入的控件/光标。
- 标签/关键字：只展示选择集共有的编辑行；mixed 行显示「多个值」且未被编辑前不参与整表提交；
  删除行按 baseline 差分移除。

### 3.4 页面边角逻辑收口（需求 1/2）

- 退役全部「批量规则」用户可见概念：批次规则文案、规则使能胶囊、右键「复位/清除规则」、
  无选中回退、`set_batch_files` 的双重职责、与批量规则相关的过期注释/命中标记。
- 「例外」语义 = 带元数据修改的文件；列表项应能定位对应文件（选中它），清除 = 清该文件全部修改。
- 预设保存/载入语义保持：`PageMeta::rules()` = 当前选择项编辑状态聚合（uniform 入规则）；
  `apply_rules(r)` = 替换式转成选中文件 override。无选中时载入预设**不得**静默写入首个文件
  （改为提示/不动作，如实登记）。
- 启动恢复 `last_preset` 的同类路径一并按上述口径修正。

## 4. C2 GPS 卡 / 隐私卡 / 元数据详细编辑页（需求 2/4）

- **删除「批量 GPS 规则」折叠概念**：坐标/海拔/方位/时间戳 = 当前选中图片的编辑面（多选=批量
  编辑）。结构上取消 rule_toggle/rule_host 包装（可保留「更多字段」折叠行），删除「批量 GPS
  规则 · 已启用」等文案；输入面按 C1 聚合装载（mixed/无选中禁用）。
- GPS 选点闭环（地图点选/搜索/联想/中心取点）写入选中的每个文件（保留各自其它分量）；
  「从文件读取」= 重新聚合装载（不产生编辑）；清除 GPS 三态。
- 隐私与文件时间卡：四开关三态 + 逐字段提交；标签/关键字多选语义按 C1.3；「例外」列表按 C1.4。
- **元数据详细编辑页（ExifEditor）深查**：删除「忽略全部批量规则」与「继承/覆盖/清除」旧三态
  概念（改为无批量规则语义）；`〔批〕`命中标记退役或改为「已改」；OK 返回与旧 override 合并、
  保留六个 enable 字段与其它字段不丢失；只作用当前文件；过滤/分组下行映射正确（R-152 同类）；
  取消=不改；源文件只读；保存失败显式报错。

## 5. C3 大预设重设计（需求 3）

- 先重建「最初设计目标」：大预设 = **一键复用整套批量输出配置**（多格式卡 + 全局项），格式预设 =
  单格式参数集；两者组合关系显式化，不得隐式叠加/来源撒谎/静默覆盖文件。
- 新模型（待 Lead 批准提案后定稿）：大预设 outputs 记录「来源格式预设名（可空）+ 完整落值快照
  （兜底可移植）」+ 全局项；载入恢复页签/卡值并显示真实来源；保存 = 当前页整卡状态采集；
  旧 v1/v2 文件兼容读、不丢字段；save→load→save 字节稳定。
- 删除 detached 态/幽灵「未选择」等矛盾状态；预设管理对话框与卡片 UX 同步收口；错误显式可读。

## 6. C4 预览显示色彩管理（需求 5）

- 预览解码面携带源 ICC/CICP（`core/thumbs.h` 加性）；新增显示 profile 获取（Windows
  GetICMProfile(W)；Linux/其它回退 sRGB 并如实登记）；lcms2 把源像素变换到**显示 profile**
  （相对比色 + 黑点补偿，与引擎口径一致）后呈现，QImage 色彩空间标注正确、不得二次变换。
- 无嵌入 ICC / 未支持 CICP → sRGB 假定（既有警告口径）。
- 仅显示面变化：引擎/金样/回归零影响；预览交互（缩放/1:1/翻图/LRU/linear_resample）不回归；
  性能可接受（单张 4096px 变换 <20ms 量级）。
- 验收：合成 P3/AdobeRGB 图经变换后像素可复算（证明不再按 sRGB 直出）；真实 GUI 截图对比。

## 7. C5 调度并行自适应（需求 6）

- 保留冻结 `alloc_threads` 契约（断言只增不减）；新增自适应规划器（建议 `core/thread_plan.*`，
  纯函数可单测）：结合 编码器并行特性（jxl/x265/svt-av1/aom/jpegli/webp…）、剩余文件数、
  同文件多输出并行、**CPU 实时利用率**（Windows GetSystemTimes / Linux /proc/stat）动态决定
  并发文件数 W 与每文件编码线程数 E。
- 规则：文件多 → W 大 E=1 保吞吐；文件少且编码器并行友好且 CPU 未饱和 → 放大 E（jxl 可吃满）；
  CPU 饱和 → 收敛；迟滞/最小调整间隔防抖；总活跃线程数受 T 约束；**不激进**。
- 产物字节不得变化（金样 20/20 + 回归 zero diff；E=1 vs E=N 的 SHA 实测抽样如有漂移则限制并登记）。
- 读数/日志带规划轨迹（W/E/CPU/决策）可复核。

## 8. C6 真实债清零 + CI 全绿高效（需求 7）

- ① hwy dispatch ODR 链接期 UB（asan SEGV，`jpegli::VectorSize()`）：根治且 **SSIMULACRA2 数值
  不得漂移**（M12 回退红线）；MSVC ASan 本地多轮无 SEGV 为证。
- ② ProgressMux/FileEvent 数据竞争（TSan 9 条）：事件投递单线程 drain/队列化，消除跨线程共享
  可变状态；压力测试 + 设计论证；Linux TSan 由 nightly 事后背书（本地如实登记）。
- ③ CI：main-full 全绿且整轮 wall ≤30min（判据零放宽的缓存/并行/按需构建）；nightly 各 job 真绿
  （禁止空跑/假绿）；release 链路不动判据。
- ④ 不再以「设计债」为不修理由；AGENTS §11 的债表在收口时按「已修/已证伪/如实未决」改写。

## 9. C7 目录清理与构建零临时依赖（需求 8）

- 开工已清空 `build/`（50.6GB）、`dist/`、`.cache/`、`.pp_test_tmp/`、`debug/*/`；保留用户数据
  （`super/`）与基础设施（`vcpkg/`、`vcpkg_installed/`、`.toolchain/`）。
- 扫描并修复「依赖仓库内预置 build/ 内容」的脚本/流程；文档中历史证据路径仅作历史引用（可如实
  标注已按用户要求清理）。
- 发行前把 `build/` 收敛到最小必要集合（release + release-dev 之类的构建树），不得再积累任务垃圾。

## 10. C8 版本与发布（需求 8）

- 版本单源：`CMakeLists.txt` `project(PhotoPipeline VERSION 0.4.2)` + `PP_VERSION_LABEL ""`；
  `vcpkg.json` 数字段 = 0.4.2（R-122 一致性断言）；`--version` = `PhotoPipeline 0.4.2`。
- `CHANGELOG.md` 新增 `## [0.4.2]`（发行/新增/修复/变更）；README/AGENTS 版本与口径同步；
  新增 `docs/0.4.2-report.md`（交付/验收读数/偏差追认/未决/复现命令）与
  `docs/m14-adjudications.md`。
- 门禁：format 0 diff；ctest 全绿；金样 20/20；ui-smoke 冻结末行 + 探针全过；regression zero diff；
  tidy 棘轮不增；`--version` 断言。
- 发布：提交 → push main → annotated tag `v0.4.2` → push tag → release.yml（appimage/winzip/
  probe-*/publish）→ 验证 4 资产 + Release 指纹；tag 指向发行树，报告类提交不入 tag。
  用户已明确「发布 0.4.2」= 授权推送 tag 与建 Release（AGENTS §10.9 确认项）。若本机无
  GitHub 凭据，则完成本地 tag + 全部门禁与打包产物，并在报告中如实登记「推送待用户侧凭据」。

## 11. 任务分解与写作用域

| 任务 | 内容 | 所有者 | 写作用域（互斥） |
|---|---|---|---|
| task-1 | C1 选择作用域/聚合/混合值/时间卡/信息卡/MainWindow 接线 + 单测/探针 | meta-core | 见共享任务板 task-1 |
| task-2 | C2 GPS 卡/隐私卡/ExifEditor + 批量 GPS 规则移除 | meta-cards | 见 task-2 |
| task-3 | C3 大预设重设计 | preset | 见 task-3 |
| task-4 | C4 预览 ICC 色彩管理 | color | 见 task-4 |
| task-5 | C5 调度并行自适应 | sched | 见 task-5 |
| task-6 | C6 真实债 + CI | ci | 见 task-6 |
| task-7 | 基线/真实 GUI 取证/独立复核/打包 dry-run | verifier | `build/verify/**` |
| Lead | 本契约、裁定表、CMakeLists/版本、smoke_probes 总控、集成门禁、发行报告 | lead | `CMakeLists.txt`、`vcpkg.json`、`smoke_probes.cpp` 总控、`tests/ui_smoke.py`、`docs/**`、`CHANGELOG.md`、`README.md`、`AGENTS.md`、`.github/**` 审批 |

构建并行纪律：各组自己的构建树 `build/<task-id>`，`-DVCPKG_MANIFEST_INSTALL=OFF` 复用根
`vcpkg_installed`；一切命令经 `python tools/env.py run --`。禁止 `git commit/tag/push`（Lead 收口）；
禁止改金样夹具与其它组文件；CMakeLists 新文件注册由 glob 自动收编（`src/**`/`tests/unit/test_*.cpp`），
特殊 target 配置由 Lead 统一接线。

## 12. 验收判据（0.4.2 发行前逐条取证）

1. 构建：release-dev（含 dev 工具）与 release 均成功；`--version` = `PhotoPipeline 0.4.2`。
2. `ctest` 全绿（0.4.1 基线 50 条 + 新增）；`SMOKE total=20 pass=20 fail=0`。
3. `UI-SMOKE OK shots=12 pages=3` 逐字不变 + 新探针全过（bad=0）。
4. `regression.py` zero diff（未 `--update`）；金样/夹具零改动。
5. `ci-format.sh` 0 diff；`ci-tidy.py` 棘轮不增；`ci-versions-check` 通过。
6. 逐条需求证据：
   - 需求 1：单选/多选/无选中三态真实 GUI 截图 + 机械探针（作用域/混合值/禁用态）；
   - 需求 2：GPS 卡无「批量规则」概念 + 输入面聚合正确（截图+探针）；
   - 需求 3：大预设 save→load→save 字节稳定 + 旧预设兼容 + 来源显示真实（单测+截图）；
   - 需求 4：ExifEditor 合并保留字段/只作用当前文件/旧概念清零（单测/探针+截图）；
   - 需求 5：合成宽色域图变换像素可复算 + 真实截图前后对比；
   - 需求 6：规划器单测矩阵 + 本机前后实测读数 + 金样/回归零 diff；
   - 需求 7：ASan 多轮无 SEGV + 评分数值零漂移 + TSan 方案与 Linux 事后背书 + CI 变更清单；
   - 需求 8：清空 build 后全流程可重建 + 发行产物/标签/报告。
7. 文档齐全：本文件 / m14-adjudications.md / 0.4.2-report.md / CHANGELOG / README / AGENTS。

## 13. 未决与风险（初始登记，执行中滚动更新）

- **R1 硬件/平台限制**：TSan 仅 Linux CI 可跑（本机 Windows 无 TSan）；Linux 回归基线/AppImage
  无法本机复跑；相关结论以「设计论证 + 本地 ASan/UBSan + CI 事后背书」口径如实登记，不宣称。
- **R2 大预设语义变更**：可能触及旧预设兼容与格式预设行会话态，需逐条裁定并写进 CHANGELOG「变更」。
- **R3 元数据作用域变更**：无选中空态/自动选中首行是用户可见行为变化，探针需机械换锚并登记。
- **R4 预览色彩管理**：Linux 显示 profile 获取受限，回退 sRGB 的观感差异需如实说明；Windows
  多显示器/显示器 profile 变更的覆盖范围如实登记。
- **R5 调度自适应**：不同编码器并行度对产物字节的影响需实测；无法保证所有编码器字节稳定时按
  编码器限制 E 并登记。
- **R6 真实 GUI 验收**：仍以 offscreen `--ui-smoke` 探针 + 本机真实截图双通道；无真机/多显示器人眼
  验收的项如实登记。
- **R7 GitHub 凭据**：本机 gh 未登录；push/tag/Release 是否可执行取决于用户侧凭据，不能执行时
  如实登记「待推送」，不得假称已发布。
