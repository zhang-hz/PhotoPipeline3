# M13 = 0.4.1 任务与契约（选择项级元数据编辑 / CreatorTool / photoshop:DateCreated / 选点海拔）

> 状态：**本轮执行中**（2026-11-02 起草，Lead 定稿）。本文件 = 本轮唯一契约面（C1…C9），
> 任务分解与写作用域见 §10；验收判据见 §11；未决与风险见 §12。
> 权威链：AGENTS.md（纪律总纲）> 本文件（本轮契约）> docs/design.md（长期语义）。
> 冲突时以 AGENTS.md 的冻结面/证据纪律为准，并回改本文。

## 1. 用户需求原文（2026-11-01）

1. 元数据页面，右侧每个修改默认只针对选中的图片生效。当前是所有文件强制同一个数据，改掉。同时允许部分文件不开启某些修改。总是元数据修改都是默认只针对选中文件。如果需要批量生效，就多选文件后再修改。多选文件中，可能出现字段内容不一致，此时明确显示多个值。逻辑你自己梳理。
2. 隐私与文件时间卡片，增加一个功能，去除 XMP 中 CreatorTool 内容。
3. photoshop 中 DataCreated 时间没有同步偏移刷新，修复（指 `Xmp.photoshop.DateCreated`）。
4. GPS 选点后，能否自动获取并填充海拔？
完成后发布 0.4.1。

需求 1 的逻辑定稿 = 本文件 C2；2/3/4 的语义 = C3/C4/C5。

## 2. 全局纪律（C0）

- **冻结面只走「加性 + 追认」**：`core/metadata.h`、`ui/page_meta.h`、`ui/formkit.h`、`mapwidget/providers.h`
  等 PP-FROZEN 文件只允许**末尾追加**（结构体字段/声明），既有签名与语义零改动；逐条登记
  `docs/m13-adjudications.md`（Lead 收口）。
- **引擎默认行为零变化**：BatchRules / MetadataOverride 缺省值、`--dev` 默认参数、金样 20 对、
  回归基线一律不得因本批新增字段而改变；新增能力一律 opt-in。由「金样 20/20 + 回归 zero diff」
  双重背书（`--dev` harness 不感知新 UI 语义）。
- **显示 = 写入同源**：任何"生效值"显示仍必须出自 `pp::preview_effective()`；选择项编辑状态
  的唯一真源 = `FileEntry.exception`（`std::optional<pp::MetadataOverride>`）。UI 不得维护第二份
  可写入状态（控件值只是选择项聚合的视图）。
- **断言只增不减**：既有单测/金样/UI 探针断言不得为让新代码变绿而放宽；语义确需变更的探针
  要逐条在 `docs/0.4.1-report.md`「偏差与追认」登记归因。
- **证据文化**：每条验收命令 + 读数进报告；未覆盖/未决如实列 §12 与报告。

## 3. C1 引擎加性契约（engine-meta 组）

### 3.1 MetadataOverride 追加（末尾，三态；nullopt = 未设置）

```cpp
std::optional<bool> time_enabled;      // 时间卡：该文件是否参与时间修改
std::optional<bool> gps_enabled;       // GPS 卡
std::optional<bool> privacy_enabled;   // 隐私与文件时间卡
std::optional<bool> sync_mtime;        // 输出 mtime 跟随拍摄时间
std::optional<bool> modernize_exif;    // 补全现代 EXIF
std::optional<bool> strip_creator_tool;// 移除 XMP xmp:CreatorTool
```

语义（`effective_rules(rules, ex)` 内落实，缺省 = 既有行为逐字不变）：

- `time_enabled == false` → `eff.time_shift.reset()`；`gps_enabled == false` → `eff.gps.reset(); eff.gps_clear = false`；
  `privacy_enabled == false` → `eff.strip_privacy = false; eff.modernize_exif = false;
  eff.sync_mtime = false; eff.strip_creator_tool = false`（tag 编辑不在卡开关门控内 —— UI 关闭卡时
  会主动清除该文件的 tag 编辑，引擎不猜测来源）。
- `sync_mtime` / `modernize_exif` / `strip_creator_tool` 的 optional 值覆盖 `eff` 同名 bool。
- 无 override / 字段 nullopt → 与 0.4.0 逐字一致（`test_effective` 老断言不放宽）。

### 3.2 BatchRules 追加（末尾）

```cpp
bool strip_creator_tool = false; // 预设/兼容面；默认 false = 行为零变化
```

`BatchRules::is_noop()` 需把该字段计入（默认 false 不影响既有 noop 判定）。

### 3.3 MetadataPlan 追加（末尾）+ pipeline 改用

```cpp
bool sync_mtime = false; // build_plan 由 effective 值填；pipeline 据此同步 mtime
```

`pipeline.cpp` 两处（`ctx.cfg.rules.sync_mtime` / `cfg.rules.sync_mtime`，约 :2578 与 :2884）改为
读 `ctx.meta_plan.sync_mtime` / `meta_plan.sync_mtime`：无例外时与规则同值 → 行为零变化。

### 3.4 CreatorTool 移除（需求 2 引擎面）

`build_plan()` 在 `eff.strip_creator_tool` 为真时，从 `plan.xmp` 删除 `Xmp.xmp.CreatorTool`
（`normalize_xmp_key` 口径，与既有删除工具同风格）；不存在则静默跳过、不产生 warning。
只删该键，不动 `Xmp.photoshop.*` 其它字段、不动 EXIF `Software`（用户需求只点名 XMP CreatorTool；
如后续要扩，另立需求）。

### 3.5 photoshop:DateCreated 偏移（需求 3）

- `kXmpTimeKeys` 2 → 3：加 `"Xmp.photoshop.DateCreated"`；`apply_time_shift()` 对三键同路径
  （`shifted_xmp_time`，Δ 与时区语义都走该纯函数）。
- `EffectivePreview` 末尾追加 `EffectiveField<std::optional<DateTimeVal>> xmp_date_created;`，
  `preview_effective()` 同源填充（原值/生效值/changed；不可解析 → effective=original、changed=false）。
- 解析要求：`photoshop:DateCreated` 常为 ISO-8601 带时区（`2024-03-01T10:00:00+08:00`），既有
  `parse_xmp_date`/`shifted_xmp_time` 需覆盖（不要求新增格式；若既有解析器不支持则在本轮修）。
- mtime 同步仍只看 EXIF DateTimeOriginal（既有口径不变）。

### 3.6 C1 单测（只增）

- `test_effective`：六字段三态覆盖矩阵；`xmp_date_created` 的偏移/不可解析；默认路径逐字不变。
- `test_metadata`：CreatorTool 移除（有/无两态、其它 XMP 键不动）；`plan.sync_mtime` 随 override；
  photoshop:DateCreated 随 Δ 落盘。
- `test_timeshift`：DateCreated 偏移（跨日、时区语义、不可解析跳过）。

## 4. C2 选择项作用域编辑器契约（ui-core 组 + ui-gps 组）

### 4.1 数据模型

- **唯一可写状态**：`FileListModel` 每行 `FileEntry.exception`（`optional<MetadataOverride>`）。
  `MetadataOverride{}`（全空）= 该文件无元数据修改 → 模型存 `nullopt`（归一化在 UI 侧完成）。
- **UI 不再编辑全局 BatchRules**：`MainWindow` 生成 `RunConfig` 时 `cfg.rules = pp::BatchRules{}`；
  `PageMeta::rules()` 保留但语义改为「当前选择项编辑状态的聚合快照」（预设保存用）；
  `PageMeta::apply_rules(r)` 语义改为「把预设 r 逐字段转成选中文件的 override」（预设载入用）。
- **生效值**：对每个选中文件 `preview_effective(probe, pp::BatchRules{}, item.exception)`。
- **选择变化**：`PageMeta::set_selection(items, ...)` 携带每项 `exception` 快照；页面按聚合装载输入控件。
- **提交路径**：控件变化 → `commit_field(st, EditField)` → 只更新选中文件 → 发出新信号
  `PageMeta::selection_edits_committed(QList<MetaSelectionEdit>)` → MainWindow 写
  `FileListModel::set_exception(row, edit)`（path→row 查找）；页面同时就地更新 `follow.items` 快照
  并只刷新生效值/聚合（**不经过外部 reload**，避免打字光标重置）。模型 `exception_changed` →
  `sync_exceptions()` + 列表徽标刷新（既有）+ 状态栏"N 个例外"（语义 = "N 个文件带元数据修改"）。

### 4.2 聚合语义（每字段三态）

- `absent`：所有选中文件都未设置该字段；
- `uniform`：所有选中文件的值相同；
- `mixed`：至少两值不同（**明确显示"多个值"**）。
- 卡级 active：文件该卡 `*_enabled == true` **或**该卡任一编辑字段存在 → 该文件 active；
  `*_enabled == false` 且无编辑 → off。选择集聚合：全 active=on / 全 off=off / 否则 partial。

### 4.3 输入控件装载规则（load_inputs_from_selection）

- **时间卡**：模式 seg、六个 Δ spin、两个时区下拉 ← 选择项 `override.time_shift` 聚合
  （absent → 默认 0/UTC+8；mixed → "多个值"）；卡头开关三态。
- **GPS 卡**：lat/lon/alt/dir/ts 输入框 ← 选择项**生效坐标聚合**（`override.gps` 优先，否则 probe
  原始 GPS 读回）——这是"多选字段内容不一致显示多个值"的主展示面；用户编辑时才产生 override。
  卡头开关 / 清除 GPS 三态。
- **隐私与文件时间卡**：隐私剥除 / 时间同步 / EXIF 补全 / 去除 CreatorTool 四个开关 ←
  override 三态（部分 → partial）；关键字胶囊多选不一致 → "多个值"；标签编辑行只展示选择集中
  **完全相同**的编辑行；mixed / 仅部分文件有的编辑以"多个值"行与右侧计数提示。
- **只读生效显示**（关键信息卡、GPS 生效坐标、时间 old/new 对照）：多选时逐字段聚合
  preview 的 effective 值：uniform → 值；mixed → "多个值" + tooltip（逐文件值 ≤8 条 + "…还有 N 个"）。
  时间卡既有"多选逐行小表"保留（它是显式多值呈现，不冲突）。
- **渲染约定**：mixed 的线输入框 = 空文本 + placeholder「多个值」+ 动态属性 `ppMixed=true`
  （QSS 取强调/警示令牌，禁硬编码色）；mixed 的 spin = `DeltaSpin::set_mixed(true)` 文本「多个值」；
  mixed 的开关 = `SwitchControl::set_partial(true)` 三态视觉；mixed 的只读标签 = 文本「多个值」。

### 4.4 提交语义（commit_field，字段级不串扰）

- **只覆盖被编辑字段**：例：两文件 Δ 分别为 +1d/+2d（days mixed），用户把 days 改成 +3 →
  两文件其它分量（时/分/秒/年月）保留各自原值，只 days 变 +3；未编辑的 mixed 字段保持原样。
- **卡开关**：置 true → 该卡 `*_enabled=true`（值不变，不凭空写入）；置 false →
  清除该卡全部字段 + `*_enabled=false`。
- **GpsLat/GpsLon**：两值都解析成功才提交 GpsData（lat/lon），并保留各文件已有 alt/dir/ts；
  只改一个坐标框（另一个 mixed/空）→ 不提交（黄字提示走既有 refresh_gps_warning 通道）。
- **GpsAlt/GpsDir/GpsTs**：独立 optional 覆盖（文件无 gps 时忽略/不创造 GPS 点）。
- **GpsClear**：置 true = 选中文件 `gps_clear=true`+`gps.reset()`；置 false = 清 gps_clear。
- **Tags**：整表提交（选择集共有的编辑行 + 用户新编辑），逐文件写 `exif_edits/xmp_edits`
  （键归属规则沿用 `build_rules` 的 `Xmp.` 前缀判定）。
- 空 override 归一化为 `nullopt`；每次提交 = 一次模型更新 + 一次聚合刷新。

### 4.5 各卡具体行为

- **时间卡**：Δ/时区控件逐字段提交；「影响 N 个文件」胶囊改为「已选 N 个文件」；右键菜单
  「复位/清除规则」= 清除选中文件该卡字段；多选小表（旧/新/文件名）保留。
- **GPS 卡**：选点闭环（地图点选/搜索结果/联想/地图选点中心）→ 坐标写入**选中文件**（覆盖
  gps.lat/lon，保留各自其它分量）；「从文件读取」= 重新聚合装载（不产生编辑）；
  「清除 GPS」三态；只读生效坐标 uniform 才落钉，mixed 不落钉 + "多个值"；状态胶囊
  已有/无/将清除/mixed（"多个值"）。
- **隐私与文件时间卡**：四个开关三态 + 逐字段提交；例外列表 = 带元数据修改的文件；
  「清除例外」= 清除该文件全部元数据修改（既有入口语义更新，行为收口不变）。
- **关键信息卡**：单选逐字不变；多选逐字段"多个值"聚合（相机/镜头/曝光/尺寸/色彩/文件/GPS/
  白平衡/评级），tooltip 给逐文件值；空态不变。
- **单文件元数据编辑器（ExifEditor 对话框）**：保留；打开时传 `BatchRules{}` + 既有 exception；
  OK 返回的 override 与旧 override 合并（六个新字段从旧值保留 —— 对话框不表达它们，不得丢失）。

### 4.6 预设与运行

- **保存预设**：`preset.rules = page_meta->rules()` = 选择项编辑状态聚合（uniform 字段入规则，
  mixed/absent 留空；`*_enabled` 按聚合三态：全 on→true、否则 false）；不含 per-file 差异。
- **载入预设**：`apply_rules(r)` 把 r 的字段应用到选中文件（时间/GPS/标签/隐私四组 +
  `strip_creator_tool`），并把 `*_enabled` 置 true；随后页面聚合刷新。
- **运行**：`cfg.rules` 空；每文件 override 由调度器经 `FileEntry.exception` 带给
  `build_plan(src, {}, exception)`；mtime 同步走 `plan.sync_mtime`（C1.3）。
- **运行前检查**：`mainwindow_run.cpp` 的 GPS 坐标精度预检改为扫描 `FileEntry.exception` 的
  逐文件 GPS（无 override 的坐标不提示；语义与 0.4.0 的"规则坐标"提示对齐）。

## 5. C3 需求 2：去除 XMP CreatorTool（engine-meta + ui-gps）

- 引擎面 = C1.4（`strip_creator_tool` 三态）。
- UI = 隐私与文件时间卡新增开关行：label「去除 CreatorTool」；objectName `creator_tool_strip`；
  自验钩子 `pp-meta-creator-tool`；说明「移除 XMP 的 CreatorTool（xmp:CreatorTool），用于清除
  Photoshop 等写入的生成工具签名」；三态（部分文件已开 → partial）；点击 → `commit_field(PrivacyCreatorTool)`。
- 预设面 = `BatchRules::strip_creator_tool`（C1.2）。

## 6. C4 需求 3：photoshop:DateCreated 同步偏移（engine-meta）

见 C1.5。UI 时间卡 tooltip 的"另两个时间字段"说明可追加 photoshop:DateCreated（可选）；
核心验收 = 输出文件 XMP 中 `Xmp.photoshop.DateCreated` 随 Δ/时区语义同步改写，且单测覆盖。

## 7. C5 需求 4：GPS 选点自动海拔（ui-gps）

- 新模块 `src/mapwidget/elevation.{h,cpp}`：异步、无 key；provider 事实表 = open-meteo（默认）
  + opentopodata（备选，失败不自动重试；provider 选择暂不暴露 UI，代码留事实表）；纯函数
  `elevation_url(provider_id, lat, lon)` / `parse_elevation(provider_id, QByteArray, double *out, QString *err)`；
  单在途 + 序号丢弃陈旧响应；超时 5s；UA 用产品名。
- GPS 卡新增「自动获取海拔」开关（objectName `auto_elevation`，钩子 `pp-meta-auto-elevation`，
  默认开、不持久化）；选点闭环收口 `apply_picked_point()` 后异步取海拔：
  - 仅当 alt 输入为空或上次由本功能自动回填时写回；手动改 alt 后不再覆盖；
  - 回填成功：alt tooltip「自动获取：N m（Open-Meteo）」；失败：tooltip 给原因（不弹窗、不改
    gps_status，避免覆盖选点反馈）；
  - 响应回来先校验当前坐标 = 请求坐标，不等则丢弃。
- 离线语义：失败静默降级（无海拔回填），不阻塞选点/写盘；不新增必填设置项。
- **provider 选型实测（verifier，2026-11-02 上海网络）**：open-meteo HTTP 200/943ms（首连
  1241ms，批量 100 点 297ms，限 600/min+10k/day，无 key，WGS-84 直用）→ **默认源**；
  opentopodata HTTP 200/529ms（1 call/s、1000 calls/day，限流严格）→ 仅备选、不做自动回退重试；
  高德 Web 服务无海拔接口且 GCJ-02 ≠ WGS-84 → 排除。两家 DEM 同点差 9m（5.0 vs 14.0）→
  UI 必须标注「近似值」。沿用代码事实表默认 id = open-meteo。

## 8. C6 测试与探针

- 单测（只增）：`test_effective`、`test_metadata`、`test_timeshift`、`test_meta_selection`（新，
  聚合/字段级合并纯函数）、`test_elevation`（新，URL/解析，无网络）。
- UI 自验探针（`--ui-smoke`，Lead 统一改 `smoke_probes.cpp` + `tests/ui_smoke.py`，只增）：
  - 选择项编辑：单选改时间 Δ 只写该文件 exception、其它文件不动；多选批量生效；卡关闭清除。
  - 混合值显示：两文件不同 Δ/不同 GPS → 控件出现「多个值」（ppMixed 属性/文本断言）。
  - 部分文件开启：三态开关 partial 可达。
  - CreatorTool 开关存在 + 勾选后 override.strip_creator_tool == true。
  - 自动海拔开关存在 + elevation 模块纯函数单测（不依赖网络）。
- 冻结集不动：12 张截图、末行 `UI-SMOKE OK shots=12 pages=3` 逐字不变；新增断言只进已有
  tag 行或新 tag 行（新 tag 行需加进 `tests/ui_smoke.py` 前缀清单）。
- 金样/夹具零改动；回归 zero diff。

## 9. C7 版本与发布（0.4.1）

- 版本单源：`CMakeLists.txt:19` `project(PhotoPipeline VERSION 0.4.1 ...)` + `PP_VERSION_LABEL ""`；
  `vcpkg.json` version = 0.4.1（R-122 一致性断言）；`--version` = `PhotoPipeline 0.4.1`。
- `CHANGELOG.md` 新增 `## [0.4.1]` 节（发行/新增/修复/变更体例，发行小节末含内部口径记录）；
  README / AGENTS 版本口径同步；新增 `docs/0.4.1-report.md`（交付/验收读数/偏差与追认/未决/
  复现命令）与 `docs/m13-adjudications.md`（冻结面加性追认 A1…）。
- 门禁：format 0 diff；ctest 全绿；金样 20/20；ui-smoke 冻结末行 + 探针全过；regression zero diff；
  tidy 棘轮不增；`--version` 断言。
- 发布：提交 → push main → annotated tag `v0.4.1` → push tag → release.yml（appimage / winzip /
  probe-* / publish）→ 验证 4 资产 + Release 指纹；tag 指向发行树、报告类提交不入 tag（AGENTS §9）。
  用户已明确"发布 0.4.1"= 授权推送 tag 与建 Release（AGENTS §10.9 的确认项）。

## 10. 任务分解与写作用域

| 任务 | 内容 | 所有者 | 写作用域（互斥） |
|---|---|---|---|
| task-1 | C1 引擎加性 + 单测 | engine-meta | `src/core/metadata.{h,cpp}`、`src/core/pipeline.cpp`、`tests/unit/test_{effective,metadata,timeshift}.cpp` |
| task-2 | C2 数据流/聚合/混合值/时间卡/关键信息卡/MainWindow/预设/编辑器 + 单测 | ui-core | `src/ui/meta_selection.{h,cpp}`（新）、`meta_state.{h,cpp}`、`meta_rules.{h,cpp}`、`page_meta.{h,cpp}`、`mainwindow*.{cpp,h}`、`formkit.{h,cpp}`、`meta_style.cpp`、`meta_card_info.cpp`、`meta_card_time.cpp`、`meta_format.{h,cpp}`、`tests/unit/test_meta_selection.cpp`（新） |
| task-3 | C3/C5 + C2 的 GPS/隐私卡 | ui-gps | `src/mapwidget/elevation.{h,cpp}`（新）、`meta_card_gps.cpp`、`meta_card_privacy.cpp`、`tests/unit/test_elevation.cpp`（新） |
| task-4 | 基线/门禁/发布链路/海拔 API 取证 | verifier | `build/m13-verify/**`（仅证据） |
| Lead | 契约/文档/CMakeLists 接线/探针/集成/门禁/版本/发布 | lead | 其余（`CMakeLists.txt`、`smoke_probes.cpp`、`tests/ui_smoke.py`、`docs/**`、`CHANGELOG.md`、`README.md`、`AGENTS.md`、`vcpkg.json`） |

构建并行纪律：各组自己的构建树 `build/m13-<task>`，`-DVCPKG_MANIFEST_INSTALL=OFF`（复用根
`vcpkg_installed`），初始 `-j 3`；一切命令经 `python tools/env.py run --`。禁止 `git commit/tag/push`
（Lead 收口）；禁止改金样夹具与其它组文件；CMakeLists 新文件注册由 Lead 统一接线。

## 11. 验收判据（0.4.1 发行前逐条取证）

1. 构建：release-dev（含 dev 工具）与 release 均成功；`--version` = `PhotoPipeline 0.4.1`。
2. `ctest` 全绿（0.4.0 基线 48 条 + 新增）；`SMOKE total=20 pass=20 fail=0`。
3. `UI-SMOKE OK shots=12 pages=3` 逐字不变 + 新探针全过（bad=0）。
4. `regression.py` zero diff（normalized 与基线同值；未 `--update`）。
5. `ci-format.sh` 0 diff；`ci-tidy.py` 棘轮不增。
6. 需求逐条手工/机械证据：
   - 需求 1：单选改时间只影响该文件（模型 exception 取证）；多选批量；mixed 显示「多个值」；
     部分文件开启可达（三态/列表徽标）。
   - 需求 2：输入含 CreatorTool 的文件经 `strip_creator_tool` 输出后 Xmp 无 CreatorTool。
   - 需求 3：输入含 `photoshop:DateCreated` 的文件经 Δ 输出后该键同步偏移。
   - 需求 4：海拔 pure 单测 + 选点回填路径代码取证（真实网络受环境限制，如实记录）。
7. CHANGELOG/README/AGENTS/报告/裁定表齐全；`ci-versions-check` 通过。

## 12. 未决与风险（初始登记，执行中滚动更新）

- **R1 需求范围收紧**：0.4.1 不修 0.4.0 遗留的 asan/tsan 两项真实债（ProgressMux 数据竞争、
  hwy ODR 链接期风险）——本轮用户需求不含，且改引擎并发面风险高；报告「未决项」如实登记
  （AGENTS §11 原定 0.4.1 修复项，顺延）。
- **R2 网络可达性**：选点海拔依赖第三方公开 DEM API（open-meteo/opentopodata），中国大陆网络
  可达性由 verifier 实测记录；失败降级为不填海拔（不阻塞核心流程）。
- **R3 显示层语义变更**：多选/混合值/例外语义变化会触及既有 UI 探针断言，需逐条归因登记。
- **R4 预设兼容**：旧预设（含 BatchRules.poly 字段）载入语义从"全局规则"变为"应用到选中文件"；
  旧会话若恢复规则，同样按选择项应用 —— 用户可见行为变化需在 CHANGELOG「变更」写明。
