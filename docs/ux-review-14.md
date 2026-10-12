# PhotoPipeline · 用户界面 14 项问题 —— 定位分析与改进方案

日期：2026-10-11 ｜ 版本口径：`CMakeLists.txt:19` = 0.3.2（M6 GPS 地图升级版）
性质：**只读定位 + 改进方案**（本文件不含任何代码改动；改动须经确认后另起提交）
基线：`ctest --test-dir build/release-dev` **33/33 全绿**（48.4s，Windows 实跑，含 `ui_smoke` 冻结行 `UI-SMOKE OK shots=12 pages=3`）
规范真源：`docs/qt-gui-design-spec.md`（v6）｜令牌单源 `src/ui/theme.h` ｜表单栅格单源 `src/ui/formkit_layout.h`

---

## 0. 结论摘要

> 四个待定口径已于 **2026-10-11 拍板**，逐条记录见 [§16 决定记录](#16-决定记录2026-10-11)；第 14 项的优先级/重要性排序见 [§14 优先级与重要性说明](#优先级与重要性说明排序依据)。

| # | 问题 | 定位结论（一句话） | 严重度 | 改动量 |
|---|---|---|---|---|
| 1 | 无右键菜单 | 文件列表 `FileTreeView` **完全没有** contextMenu；分类行有、其他卡全无 | 高 | 中 |
| 2 | 无全选/取消全选 | `set_all_checked()` **生产零调用**，只被冒烟探针调过；无 Ctrl+A、无按钮 | 高 | 小 |
| 3 | 输入预览分辨率太低 | 预览与文件缩略图**共用同一个内嵌预览门槛 64px**（`thumbs.h:75`），160px 的 EXIF 缩略图被当预览用 | 高 | 小 |
| 4 | GPS 卡不像地图 App | 搜索结果框**常驻固定 112px**、分类 chips 行常驻、图层是表单行、地址行与状态行重复、地图锁死 136px | 高 | 大 |
| 5 | 功能启用勾选消失 | 时间卡勾选框在 `M4-T12` 被删；GPS 的 `gps_enable` 仍在但**埋进默认折叠面**，用户看不见 | 高 | 中 |
| 6 | 卡片行距不统一、样式各自为政 | **exif_editor 全文件不守 28px 行距**；卡头 9/7 内边距在 4 个 TU 各存一份；卡体上内边距两制（0 vs 10）；4 份页级 QSS + 3 条属性钩子轨 | 高 | 大 |
| 7 | 关闭/三钮点不动、冒出复古按钮 | 证据 `docs/m7-evidence/temp_bug.png`：窗口顶部还有一排**原生带框 — □ ✕**，自绘那排不生效。`WM_NCCALCSIZE` 全仓 0 处 + 缩放带优先于三钮吃掉顶部 6px | 高 | 中 |
| 8 | 高级参数展开只给一点地方 | `paramform.cpp:532` `setMaximumHeight(28*8=224px)` 硬限，展开后全在 224px 里内滚 | 中 | 中 |
| 9 | 多格式卡"多少位"、宽度不一 | 页签文案 `"格式名 · N 位"` 随内容变宽；页签无等宽；磁贴也带位深文本 | 中 | 小 |
| 10 | HEIF/AVIF 无 **编码档位** | 档位 = `Main/Main10/Main12`（HEVC）、`Main/High/Professional`（AV1），**决定解码兼容性的核心选项**。libheif 两个插件**都不暴露 `profile` 参数**，档位被插件按位深/色度**自动选**（`encoder_x265.cc:793-802`、`encoder_svt.cc:946-951`），UI 因此完全没有这一行；`speed`/`tune` 还被误折进高级 | **高** | 中 |
| 11 | 滚动条太粗、卡片被压缩 | `theme.h:955` 滚动条 `width:10px`；**弹窗 QSS 完全没有滚动条规则**（两套皮）；QScrollArea + Preferred 尺寸策略导致压卡片 | 中 | 小 |
| 12 | 亮色设置页签标题色错 | `settings_dialog.h:129` 把 tokens **写死 Dark**，`set_tokens` 全仓零调用，主题切换漏重放 tabs | 中 | 小 |
| 13 | 窗口没有圆角 | `DWMWA_WINDOW_CORNER_PREFERENCE` 全仓 **0 处**；`#pp-root` 只有背景无 radius | 中 | 小 |
| 14 | 色彩转换是否业界最高质量 | 用 **lcms2 2.19.1**（非自研、非 OCIO），float32 + RC+BPC，主干达标；差距在色域映射缺位、HEIF nclx 错配、灰度/RGB ICC 直接整文件失败 | 高 | 中 |

---

## 1. 右键菜单：每个卡片不同右键菜单

### 现象
右键各卡片没有任何菜单；用户预期"每个不同卡片右键菜单不同"。

### 定位
| 位置 | 现状 | 证据 |
|---|---|---|
| 文件列表 | **无**右键菜单 | `src/ui/filelistmodel.h:347-354` `FileTreeView` 只覆写 `mouseReleaseEvent`；全仓 `contextMenuPolicy/customContextMenuRequested/QMenu` 对 `d.view` **零命中** |
| 分类行 | **有** | `classify_panel.cpp:277`（policy）+ `:308-320`（exec）；菜单项 `build_row_menu` `:551-642` = 全选该类 / 反选该类 / 清空勾选 / 重命名… / 颜色… / 热键… / 删除 |
| 参数表单标签 | **有** | `paramform.cpp:713-723`「重置为默认」 |
| GPS 经纬度框 | **有** | `page_meta.cpp:3168-3177`「复制坐标 / 度分显示（DMS）」 |
| 例外列表 | **有** | `page_meta.cpp:3300` + `:3343-3355`「清除例外」 |
| ＋钮 / 筛选钮 | 有（但是**点击**弹出，非右键） | `mainwindow.cpp:1859-1867` / `:1891-1898` |
| 三页各卡片 | **无** | — |

**还有一处注释与实现相反**：`mainwindow.cpp:1838-1839` 写着「清空列表」仍在"列表右键/＋菜单"里 —— 实际**列表右键菜单根本不存在**，该项只在 ＋ 菜单里。

### 根因
从未给 `FileTreeView` 与各卡片接 `contextMenuPolicy(Qt::CustomContextMenu)`；右键菜单只在少数控件上零散做过，没有形成"一卡一菜单"的公共机制。

### 改进方案
1. 新增公共件 `src/ui/contextmenu.{h,cpp}`（**进公共层，不进各页**）：`context_menu(parent, items)` 单一构造点，菜单样式走 `theme`（与 0.3.1 已令牌化的 QMenu 同源，不新写样式）。
2. 每类卡片一个 `actions` 构造函数，**菜单内容不同**：

| 目标 | 右键菜单项 |
|---|---|
| 文件列表 | 勾选全部 / 取消全部勾选 / 反选勾选 ｜ 编辑 EXIF… / 移除选中 / 清空列表 ｜ 按分类打标 → / 复制路径 |
| 关键信息卡 | 复制全部信息 / 刷新（重读源文件） |
| 时间偏移卡 | 启用 / 重置为默认 / 从文件读取时间 / 清除规则 |
| GPS 卡 | 启用 / 从文件读取 GPS / 地图选点 / 取地图中心 / 复制坐标 / 清除 GPS |
| 隐私与文件时间卡 | 启用 / 隐私剥除开·关 / 时间同步开·关 / 编辑关键字… |
| 输出格式卡 | 全选格式 / 清除选择 / 恢复默认格式 |
| 全局卡 | 重置为默认 / 浏览输出目录… |
| 格式参数卡 | 重置为默认 / 复制参数 / 粘贴参数 / 展开全部高级参数 |
| 预设与预估卡 | 另存为预设… / 覆盖保存 / 删除预设 |
| 运行总览卡 | 查看完整日志… / 打开输出目录 / 取消剩余 / 再次运行 |
| 任务进度卡 | 复制失败原因 / 跳到该文件 / 重试该文件 |
| 日志尾卡 | 复制日志 / 打开日志文件 / 清空显示 |

3. 运行中（`lock_for_run`）菜单项按语义置灰（沿用分类行 `:310` 的锁定不弹口径）。

### 举一反三
同类"该有而没有"的入口：文件列表的 `Ctrl+A`（见第 2 项）、卡片的 `F2` 重命名等都不在；右键菜单统一建成后，快捷键与 tooltip 同源补全。

### 影响面 / 回归
新增对象名会进 `pp-*` 冻结清单；`--ui-smoke` 加一条 `context-menu` 探针（每类卡片弹一次并断言菜单项数与首项文案）。风险低。

---

## 2. 文件没有全选和取消全选

### 定位
- **API 早就有了，只是没人用**：`src/ui/filelistmodel.h:152` `void set_all_checked(bool on);`，实现 `filelistmodel.cpp:556-570`。
- **生产 UI 零调用**：grep 全 `src/ui` 只有冒烟探针 `mainwindow.cpp:6534`、`:7180` 在调。
- 无 `Ctrl+A`：全仓 `SelectAll|Ctrl\+A|QShortcut` 只命中 `mainwindow.cpp:641`（ShellNav）、`formkit.cpp:534`、`settings_dialog.cpp:619`、`preview_panel.h:35`，**与文件列表无关**。
- 工具行（`mainwindow.cpp:1840-1923`，`pp-tools`）只有：搜索框 `pp-search`、＋ `pp-add`、− `pp-remove-sel`、筛选 `pp-filter`、分组下拉 `pp-group-mode` —— **没有任何勾选相关按钮**。
- 现有批量勾选入口只在分类行右键（`check_class/invert_class/clear_checks`，`classify_panel.cpp:442-458`）。

### 根因
勾选体系只做到"模型层 API + 分类面板转发"，文件列表自身的批量勾选入口从未接入 UI。

### 改进方案
1. **文件列表右键菜单**加一组（见第 1 项）：`勾选全部` / `取消全部勾选` / `反选勾选`（作用于**当前可见（过滤后）**的行，与"运行集是全局的"这一现状给出提示）。
2. **工具行加一个图标钮** `pp-check-menu`（图标 = icons 库新增 `check-all`），点击弹出同一组三项 —— 高频操作不必依赖右键。
3. **快捷键**：`Ctrl+A` = 全选**行**（Qt 标准），`Ctrl+Shift+A` = 勾选全部，`Ctrl+D` = 取消全部勾选；全部带 tooltip。
4. 复用既有 `FileListModel::set_all_checked / checked_rows`，**不加第二套状态**。

### 举一反三
- `set_all_checked` 目前只被探针调用 —— 这是"API 与 UI 脱节"家族；同类的还有 `FileListModel::checks_changed`、`PreviewPanel::zoom_changed`、`preview_loaded` 三个孤儿信号（`deep-review` R-134）。本次一并接线或删除。
- 过滤态下的语义要显式（R-067）：过滤出 5 行点"勾选全部"，是勾 5 行还是 24 行？方案：**勾选可见行**，并在状态栏给一次非阻塞提示。

### 影响面 / 回归
`--ui-smoke` 加 `check-all` 探针：勾选全部 → `checked_count()==rows`，取消 → 0，反选 → `rows - n`。风险低。

---

## 3. 输入预览分辨率太低

### 定位（根因确凿）
| 事实 | 位置 |
|---|---|
| 预览上限 2048px | `src/core/thumbs.h:79` `kPreviewCacheMaxPx = 2048` |
| 文件缩略图解码 96px | `src/ui/mainwindow.cpp:1551` `new Thumbnailer(96, w)`；`src/ui/thumbnails.cpp:58` `target_edge = 96` |
| **两者共用同一个内嵌预览门槛 64px** | `src/core/thumbs.h:75` `kPreviewEmbeddedMinLongEdge = 64`，注释自述「缩略图与预览共用一个门槛，防止两条路径漂移」；实现侧 `thumbs.cpp:66` `kMinPreviewLongEdge = kPreviewEmbeddedMinLongEdge` |
| 预览优先吃内嵌预览 | `thumbs.cpp:465` `if (try_embedded_preview_image(src, max_px, out.image)) { out.from_embedded = true; return out; }` |
| 内嵌预览只按 64px 放行 | `thumbs.cpp:331-352` `try_embedded_preview_image` → `find_preview()` 选最大候选 → `scale_to_rgba8` |

**结论**：数码相机 JPEG 的 EXIF/MPF 内嵌预览通常只有 **160×120 / 320×240**，远高于 64px 门槛 → 预览面板拿到的就是这张**缩略图级**图像，再被拉到 487px 宽的预览区显示 = 明显发糊。用户说"不可以和文件卡片内的预览使用同一个分辨率"，正是这个共用门槛。

### 根因
把"缩略图的内嵌预览门槛"错误地当成"预览的内嵌预览门槛"。缩略图最终只画 46×46，用 160px 内嵌图完全够；预览要支持 1:1 / 800% 缩放，内嵌图必须够大才有资格用。

### 改进方案
1. **拆开两条通道的门槛**（`src/core/thumbs.h`）：
   - `kThumbEmbeddedMinLongEdge = 64`（缩略图通道，行为零变化）；
   - `kPreviewEmbeddedMinLongEdge = 1600`（**新值**）：内嵌预览长边 < 1600 一律放弃，走整图 float32 解码 + LANCZOS3 降到 2048。
   - 两值都在 `thumbs.h` 公共处，仍满足"单源"；`thumbs.cpp:66` 的转发改成分通道。
2. **预览上限 2048 → 4096**（`kPreviewCacheMaxPx`）：配合 1:1/800% 缩放；LRU 容量由 16 降到 **8** 以守内存（4096² RGBA8 ≈ 64MB/张 × 8 = 512MB 峰值上限，实际按图像比例）。**内存口径做成 `theme::Metrics`/`thumbs.h` 常量并加 NOTE**。
3. `PreviewImage::from_embedded` 语义保留（徽标"内嵌预览"），但只有真用了足够大的内嵌图才置真。

### 举一反三
同一"共用常量导致两边都不对"的家族：
- `kPreviewEmbeddedMinLongEdge`（本次）；
- `theme::Metrics::row_h=32` 与 `formkit_layout.h kRowPitch=28` 双行高并存（`deep-review` R-112）；
- `providers.cpp` 与 `geocoder` 两套 `search_url`（limit=6 vs 20，R-060）。
本次顺带把 `theme.h:228` 的死常量 `row_h` 删除（见第 6 项）。

### 影响面 / 回归
`test_thumbs` 加断言：构造一张"内嵌预览 320px + 整图 4000px"的样本，断言 `decode_preview_detailed(...).from_embedded == false` 且输出长边 == 2048/4096。`--ui-smoke` 预览几何不变。风险低（预览变慢一点：由内嵌快速路径换整图解码）。

---

## 4. GPS 卡：地图 App 化

### 定位（`src/ui/page_meta.cpp` `build_gps_card` = 2512–3193，7 个子区）

| 用户诉求 | 现状 | 证据 |
|---|---|---|
| 去掉固定搜索结果框，改为搜索后弹出、不选中消失 | `QListWidget` `pp-meta-gps-search-results` **常驻固定 112px**（`kResultVisibleRows(3) × kResultRowH(36) + 4`），初始即填占位行「搜索后在此选择」，**从不隐藏** | `:2748`、`:2752`（常量 `:186-187`）、占位 `:1705-1733`、初填 `:3191` |
| 去除类行 | 6 个分类 chip 常驻一整行（餐饮/酒店/加油站/景点/地铁/商场） | `:2700-2731`，`pp-meta-gps-category-0..5`，行 `pp-meta-gps-category-row` |
| 图层卡片放到地图上（像地图 App） | 「图层」是 body 第 7 个**表单行**（矢量/卫星/混合 Seg），不在地图上 | `:2635-2651`，`pp-meta-gps-layer` / `-layer-row` |
| 地图下方地址与已选点重复，仅保留已选点 | **确实重复**：`pp-meta-gps-address`（`:2806-2818`）与 `gps_status` 的「已选点 · %1」（`:3130`）写同一段地址 | `:3123-3132` |
| 腾出的空间都给地图 | 地图 `setFixedHeight(kMapHeight)` **136px**，无 stretch | `:2765`，常量 `:167`（引 mockups-v2 `.map{height:136px}`） |

**当前 body 行序（14 行）**：纬度/经度 → 坐标显示(WGS-84/DMS/复制) → 海拔/方位 → 操作 → **图层** → 地图源 → 地点搜索 → 联想下拉(116) → **分类 chips** → **搜索结果(112)** → **地图(136)** → **地址** → [状态行] → [折叠行]。

**冻结契约约束**：卡内所有 `pp-*` objectName（`page_meta.cpp:75-92` 清单）+ `mainwindow.cpp` 的 `smoke_probe_meta`/`meta-loop` 断言（`ops-above-map`、`meta-geom checked=17`、`-row` 通配行距探针、`pin-fill-wgs84` 等）。**改造必须保留 objectName**，探针断言按新几何重写。

### 根因
M6 把地图能力（搜索/联想/分类/图层/结果列表）做进了"表单卡"的行节奏里，每加一个能力就加一行，没有按"地图 App"的交互模型（**地图是主角、控件是浮层**）重构版面。

### 改进方案（地图 App 化，目标版面）
```
┌──────────────────────────────┐
│ GPS  [WGS-84]            [启用]│ ← 卡头加启用开关（见第 5 项）
├──────────────────────────────┤
│ 纬度 ▸ …      经度 ▸ …        │ ← 保留（生效值只读）
│ 海拔 ▸ …      方位 ▸ …        │
│ 操作 [从文件读取][地图选点][清除]│
│ 地点搜索 [____________] [搜索] │
│ ┌──────────────────────────┐ │
│ │ ☰ 图层(浮层)   ⤢ 展开      │ │ ← 图层 Seg 浮在地图左上角
│ │                          │ │
│ │        地 图（放大）       │ │ ← 高度 136 → 260，stretch 吃满
│ │                          │ │
│ │  [比例尺]   [光标 WGS-84]  │ │
│ └──────────────────────────┘ │
│ 已选点 · 39.9042, 116.4074    │ ← 只留一行"已选点"（含地址在 tooltip）
│ ▸ 批量 GPS 规则               │
└──────────────────────────────┘
```

1. **搜索结果框改弹出式**：`pp-meta-gps-search-results` 由常驻行改为 **QListWidget 挂在地图上的浮层**（`parent = map`，绝对定位在搜索框下方）；`setVisible(false)` 默认；`search_results_ready` 且非空 → 显示；选中/清空/换源/失焦 → 隐藏。objectName **保留**，`pp-meta-gps-results-host` 保留为浮层容器（仍不带 `-row` 后缀，不进行距探针）。联想下拉 `pp-meta-gps-suggest` 同样改成浮层并复用同一条显隐规则。
2. **删除分类 chips 行**（`pp-meta-gps-category-row` + 6 个 chip）：6 个类目入口并入搜索框右侧的「分类 ▾」下拉按钮（保留一键搜索能力，不再占一整行）。objectName 保留给下拉菜单的 action（探针改断言菜单项数 == 6）。
3. **图层控件浮到地图上**：`pp-meta-gps-layer` 从 body 行移入地图浮层左上角（半透明底 + 圆角，用公共令牌），`pp-meta-gps-layer-row` 删除。图层记忆/`set_tile_style` 接线不变（`apply_map_style` `:3311`）。
4. **删除地址行**：`pp-meta-gps-address` 改为 `gps_status` 的 tooltip 与右键「复制地址」来源，**视觉只留 `gps_status` 的「已选点 · …」一行**。`pp-meta-gps-address-row` 删除，`pp_meta_gps_address` 动态属性保留（探针 `:10933-10935` 仍可读）。
5. **地图最大化**：`kMapHeight` 136 → **260**（**已定档**，见 §16-2）；地图改 `stretch=1` 吃掉腾出的空间；浮层不遮挡取点区。

### 举一反三
- 「浮层优于表单行」这条原则同样适用于：预览卡的缩放条（已是浮层）、运行页的日志尾；本次只动 GPS 卡。
- `R-047`（生效值刷新把地图拽回去）顺带修：仅生效坐标变化时 `center_on`。

### 影响面 / 回归
- `smoke_probe_meta` 的几何断言要按新版面重写（`ops-above-map` 保留，`-row` 探针减 3 行，`content 高` 期望值改）。**冻结行 `UI-SMOKE OK shots=12 pages=3` 不变**。
- `meta-loop`（选点值 = WGS-84）断言全保留。
- 风险中高：`build_gps_card` 是 682 行巨型函数（`deep-review` R-061），改造同时按子区拆函数。

---

## 5. 元数据页"功能勾选启用"消失

### 现象
用户问：每个功能的启用勾选怎么没了？我不想 GPS 怎么办？

### 定位（含历史）
| 时点 | 事实 | 证据 |
|---|---|---|
| M1b-U7 | **有**「启用 QCheckBox」：时间偏移卡 + GPS 卡各一个，未勾选则卡内控件禁用 | `docs/m1b-tasks.md:645-652`；`4722fdd`；`67be3f2 M1b-U7-FIX: disable card controls when unchecked` |
| **M4-T12** | **移除时间卡勾选框** | 提交说明逐字：「取消「启用」复选框——全零 Δ / from==to 时区 = noop（`TimeShift::is_noop()` 为写路径与显示路径同一判据），语义零损失」；diff 删 `QCheckBox *time_enable` |
| M4-T12 | GPS 规则输入收进**折叠行** | 「批量规则输入收进折叠行（`rules()` 的"规则存在性"必须显式可控）」 |
| 现在 | GPS 的 `gps_enable` **还在**，但埋在「批量 GPS 规则」折叠面内，默认折叠 | `page_meta.cpp:2854-2858`（`check_row(..., "启用", "gps_enable")`）、折叠默认收起 `:2833-2834` / `:2909`；装配 `:991-1013`（`else if (st->gps_enable->isChecked()) rules.gps = gps;`） |
| 数据层 | 无独立 enable 位：`BatchRules{ std::optional<TimeShift>; std::optional<GpsData>; bool gps_clear; bool strip_privacy; bool sync_mtime; }` | `src/core/metadata.h:56-73` |
| 设计稿 | `docs/qt-gui-design-spec.md` §7.1 卡表**无任何"启用"字样** | `grep 启用 docs/qt-gui-design-spec.md` = 0 |

### 「你是什么考虑？」—— 当时的理由与我的评价
- **当时的理由**（M4-T12 提交说明）：时间规则的"不生效"已有客观判据（`TimeShift::is_noop()`，全零 Δ / from==to），勾选框是冗余位，删掉可以少一行、少一种"用户勾了但值是空"的非法状态。
- **这个考虑的问题**：它把"**功能开关**"和"**参数是否为空**"两件事合并了。用户的心智模型是"我要不要这个功能"，不是"我的参数碰巧是不是空"。合并后：
  1. 想"明确不做时间偏移"的用户，无法表达"关掉"，只能把值留空 —— 界面上看不出"这是我不做的决定"还是"我还没填"；
  2. GPS 更糟：`gps_enable` 被塞进默认折叠面，**等于把开关藏起来了**，用户以为功能消失了；
  3. 一旦以后加了"默认非空"的规则（比如默认保留原 GPS），空值判据立刻失效，而开关已经被删了。
- **结论**：应当**恢复每卡一个可见的「启用」开关**，并与参数是否为空**解耦**。

### 改进方案
1. **卡头右置一个「启用」开关**（`formkit::SwitchControl`，30×16，§6.1"立即生效的布尔"）：时间偏移卡 / GPS 卡 / 隐私与文件时间卡各一个。objectName：`pp-meta-time-enable` / `pp-meta-gps-enable` / `pp-meta-privacy-enable`。
2. **语义**：
   - 关闭 = 该卡**整卡不参与规则**（`build_rules` 直接跳过），卡内控件置灰（沿用 `67be3f2` 的禁用行为）；
   - 打开 = 按当前参数装配规则；参数非法时保留黄字警告（`gps_warning` 现有机制）。
   - GPS 的 `gps_enable`（折叠面里那个）**保留为同一状态的第二入口**（双向同步），避免破坏冻结 objectName 与既有接线（选点/联想自动勾上 `:1773`、`:3000`）。
3. **数据层加显式 enable 位**（加性，不删 optional）：`BatchRules::time_enabled / gps_enabled / privacy_enabled`，`effective_rules` 与写路径同一判据（沿用"显示=写入"单源纯函数纪律）。老预设缺键 → 默认"按 optional 是否有值"回落，**零迁移成本**。
4. **默认值**：三个开关默认**关**（与现状"不填即不写"一致，不改变任何现有用户的输出结果）。

### 举一反三
- 输出页同样有"功能"：转码模式（转码 / 仅元数据）已是分段控件，语义完整；
- 设置页「分文件夹默认结构」改后当次会话不生效（`deep-review` R-138）—— 同属"开关语义不透明"，本次一并标注生效时机。

### 影响面 / 回归
`effective_rules` 有单测（`test_effective`）逐字段对拍，加 enable 位后补三条用例。`--ui-smoke` 加 `meta-enable` 探针：关 GPS → 运行产物无 GPS。风险中。

---

## 6. 样式收口：行距统一 + 全部样式进公共文件

> 用户原话：「严禁维护独立的样式，所有样式必须在公共中维护，能公共就公共，证明必须单独开一个新的才允许在公共样式文件中开一个新的样式，检查所有卡片。」

### 定位（逐卡片审计结果）

**A. 行距不统一 —— 用户投诉的直接来源**

| 文件/控件 | 行高 | 取值来源 | 判定 |
|---|---|---|---|
| `formkit.cpp:48` `make_row` | **28** | `formkit_layout.h:25 kRowPitch` | ✅ 单源（表单行/kv 行/checkbox 行全走它） |
| `page_run.cpp:1485` 表行 | 28 | `fk::kRowPitch` | ✅ 单源 |
| `formkit.cpp:1075` CheckBox 行 | 28 | 同上 | ✅ 单源 |
| `classify_panel.cpp:77,276` `kRowHeight` | **28** | **文件内自拷字面量** | ⚠️ 同值重复 |
| **`exif_editor.cpp`（全文件）** | **无任何 setFixedHeight** | 行高由 24px 控件 + QGroupBox padding 自算 | ❌ **完全不守 28 节奏** |
| `page_meta.cpp:187` `kResultRowH` | **36** | 文件内字面量 | ❌ 独立 |
| `page_run.cpp:120` `kTableHeadH` | **24** | 文件内字面量（注释自称"表头不入 R1"） | ⚠️ 例外未入规范 |

**B. 卡片内边距两制 / 四份复制**

| 部位 | page_meta | page_output | page_run | mainwindow | 判定 |
|---|---|---|---|---|---|
| 卡头内边距 | `(12, 9, 12, 7)` `:2190` | `(12, 9, 12, 7)` `:580` | `(12, 9, 12, 7)` 全字面量 `:1420` | `(12, 9, 12, 7)` 全字面量 `:379` | ❌ **9/7 在 4 个 TU 各一份** |
| 卡头 spacing | `group_gap=8` | `group_gap=8` | **8 字面量** `:1421` | **8 字面量** `:380` | ⚠️ 同值重复 |
| 卡体上内边距 | **0** `:2221-2223`（"由卡头下沿承担"） | **10** `:614-616` | **10** `:1442-1444` | — | ❌ **两制** |
| 卡体下内边距 | 12 | 12 | 12 | — | ✅ |

**C. 同值重复的字面量清单（应改引用公共常量）**

| file:line | 值 | 真源常量 |
|---|---|---|
| `classify_panel.cpp:77` kRowHeight | 28 | `formkit::kRowPitch` |
| `classify_panel.cpp:79` kColGap | 12 | `formkit::kColGap` |
| `classify_panel.cpp:80` kCellGap | 6 | `formkit::kInlineGap` |
| `classify_panel.cpp:81` kPadX | 12 | `theme::Metrics::card_pad_x` |
| `classify_panel.cpp:82-83` kPadTop/kPadBottom | **8 / 4** | 应为 `kCardPadTop=10 / kCardPadBottom=12`（**偏离值**，非同值） |
| `page_run.cpp:112` kRowPadX | 12 | `theme::Metrics::card_pad_x`（注释自认"= 卡内边距 12"） |
| `page_run.cpp:111` kCellGap | 6 | `formkit::kInlineGap` |
| `page_run.cpp:1723,1766` | 6 | `formkit::kInlineGap` |
| `page_meta.cpp:2534/2824/2852/3275/3292` | 4 / 4 / 10 / 8 / 8 | 8 = `Metrics::group_gap`；4/10 无常量 |
| `exif_editor.cpp:1377,1474` | 10 | 无常量 |
| `page_meta.cpp:2692 "+6"`、`2752 "+4"` | 6 / 4 | 无常量 |
| `mainwindow.cpp:1843-1844` | 8 / 6 | `Metrics::group_gap` / `formkit::kInlineGap` |
| `mainwindow.cpp:1812-1813, 1964` | 6 / 4 / 6 | 无常量 |
| `page_run.cpp:1796` | 10,8,10,8 | 8 = `group_gap` |

**D. 独立样式（QSS 散落各页）**

| 层 | 位置 | 内容 |
|---|---|---|
| 公共 | `theme.h:830-1029` `style_sheet()` / `:747-827` `formkit_qss()` / `:689-707` `pill_qss()` / `:727-737` `mod_qss()` | ✅ 应为唯一出处 |
| **页级独立** | `page_meta.cpp:1931-2164` `apply_style()` ≈ **234 行拼 QSS**（且 `:1939-1941` 把 theme 的 formkit_qss/mod_qss/pill_qss **整段重拼**） | ❌ |
| **页级独立** | `page_output.cpp:343-425` `page_qss()` ≈ 82 行 | ❌ |
| **页级独立** | `page_run.cpp:1347-1405` `page_qss()` ≈ 58 行 | ❌ |
| **面板独立** | `preview_panel.cpp:962-1035` `apply_style()` ≈ 75 行 | ❌ |
| **弹窗独立** | `settings_dialog.cpp:411-541` `dialog_qss()` ≈ 130 行（4 弹窗共用，但**不在 theme.h**） | ⚠️ |
| 逐控件 | `page_run.cpp:511` `color:%1`；`mainwindow.cpp:1821` `color:#dd8800`（**写死色**）；`paramform.cpp:1044-1056` | ❌ |

**E. 三条属性钩子轨并存**（"样式必须公共"的直接反例）
| 钩子 | 定义 | 消费 |
|---|---|---|
| `ppFk`（`theme.h:656-666`） | formkit | `theme.h:747-827`（**目标形态**） |
| `ppMetaField`（`page_meta.cpp:158`） | page_meta 37 处 | `page_meta.cpp:1946-2056` |
| `ppField`（`page_output.cpp:341` / `page_run.cpp:1345` / `settings_dialog.h:60`） | 三处 | 三份 QSS |

### 根因
M5 建立了令牌单源（`theme.h`）与表单栅格单源（`formkit_layout.h`），但**只收了"值"，没收"规则"**：各页仍各自拼 QSS、各自写内边距字面量、各自定行高。结果就是用户看到的"每个卡片行距不一样"。

### 改进方案（按"能公共就公共"落地）
1. **公共几何补全**（`formkit_layout.h` + `theme.h`，只增不删）：
   - 新增 `kCardHeadPadTop = 9`、`kCardHeadPadBottom = 7`、`kCardHeadGap = 8`（现在 4 份复制的 9/7/8 收成一处）；
   - 新增 `kStatusPadY = 4`、`kFoldPadBottom = 6`、`kGroupPadBottom = 10`、`kListRowH = 36`（GPS 结果行）、`kTableHeadH = 24`（表头，**规范里显式登记为唯一例外**）；
   - 删除死常量 `theme::Metrics::row_h = 32`（`theme.h:228` 自认 M4 遗留、零调用）。
2. **卡片骨架单源**：把 4 份 `make_card/make_header/make_body` 合并为 `formkit::make_card(parent, title, pill_name, hint_name) -> {frame, head, body}`，四处调用点全换。`make_button` 三份（`page_meta.cpp:872`、`page_output.cpp:620-633`、`settings_dialog.cpp:805`）保留 dialogkit 版并上提 `formkit::make_button`。
3. **行距统一**：
   - `exif_editor.cpp` 全部行容器补 `setFixedHeight(formkit::kRowPitch)`（这是用户投诉的最大来源）；
   - `classify_panel` 的 `kRowHeight/kColGap/kCellGap/kPadX` 改引用公共常量，`kPadTop/kPadBottom` 由 8/4 改成 10/12（对齐 R1）；
   - GPS 结果行 36 / 表头 24 在规范 §5 R1 里**显式登记为例外**（列表行/表头不属"信息行"），避免以后又被当成漂移。
4. **QSS 收口**：
   - `page_meta::apply_style` / `page_output::page_qss` / `page_run::page_qss` / `preview_panel::apply_style` 里**跨页复用的规则**全部上提到 `theme.h` 的 `style_sheet()`（卡壳、卡头、pill、hint、分隔线、字段控件、按钮、chip、下拉、滚动条…）；
   - 真正只属于某页的规则（如运行表的 `ppRun=thead`、GPS 的 HUD 浮层）**允许新开**，但必须：① 在 `theme.h` 的公共样式文件里开（用户要求"在公共样式文件中开一个新的样式"），② 命名为 `theme::xxx_qss(tokens)`，③ 带注释说明"为何不能公共"。
   - 三条钩子轨收敛到 `ppFk`（`kFk*`）；`ppMetaField`/`ppField` 保留定义但 QSS 侧并入 `formkit_qss`，各页 `setProperty` 改写到 `kFkProperty`。
5. **写死色清零**（举一反三，见第 12 项）：`mainwindow.cpp:1821` `#dd8800` → `theme::css_color(tokens.warn)`；地图 HUD / 预览浮层的固定色（`page_meta.cpp:2024-2037,2095-2105`、`preview_panel.cpp:121-138`）**统一进 theme.h 的 overlay 令牌组**（这些是"覆盖在照片上的浮层"，不随主题变，属合法的固定色，但必须单源 + 注释说明）。

### 影响面 / 回归
- `--ui-smoke` 的 `meta-geom` 行距探针、`settings_geom_probe`、`format-card-align` 全部要按新常量复核；
- 两主题成对渲染（G5）必须再走一次；
- 风险中高（触及 4 个大 TU 的样式面），但**全部是机械替换 + 常量上提**，不含逻辑改动。

---

## 7. 关闭/三钮点不动，冒出复古 X 按钮

### 现象（证据：`docs/m7-evidence/temp_bug.png`）
窗口右上角**有两排窗口控制**：
1. **上排**：三个**带方框**的 `— □ ✕`（复古/系统原生样式），贴在窗口最上沿；
2. **下排**：应用自绘的 `☆ ☀ — □ ✕`（细矢量字形，规范 §3.3 的 46×32 caption）。

用户：点应用自绘那排的关闭**关不掉**，要"弹出"的那排复古 `✕` 才能关；最小化/最大化同理。

### 定位
| 事实 | 位置 |
|---|---|
| 无边框只置了 flag，**没有非客户区收口** | `platform/frameless.cpp:311` `window->setWindowFlag(Qt::FramelessWindowHint, true)` |
| **`WM_NCCALCSIZE` 全仓 0 处** | `grep WM_NCCALCSIZE\|NCCALCSIZE\|WS_CAPTION\|GWL_STYLE\|SetWindowLong src/` → 只命中 `frameless.cpp:311` 一处（即只 set flag） |
| **无 DWM 圆角/边框请求** | `grep DWMWA_WINDOW_CORNER_PREFERENCE` → 0 处；`platform/mica.cpp:31,34-36` 只设 `IMMERSIVE_DARK_MODE` + `SYSTEMBACKDROP_TYPE` |
| 缩放带**优先于**三钮命中 | `frameless.cpp:142-171`：`top/bottom/left/right` 判定在 `close_button/max_button/min_button` **之前**返回 → 三钮顶部 6px 被 `HitZone::Top`（缩放）吃掉（= `deep-review` R-312） |
| 三钮接线有条件 | `mainwindow.cpp:2113-2121`：`if (d.frameless != nullptr) { connect(cap_min/max/close, clicked → frameless->trigger(...)); }` |
| 命中区构成 | `frameless.cpp:208-237` `build_box`：`caption` = 顶栏 rect，三钮 rect 由 `rect_of(child, physical, caption_button=true)` 算出；`hit_test` 三钮先于拖拽区 |
| 命令出口 | `frameless.cpp:359-389` `trigger()`：Windows = `SendMessageW(WM_SYSCOMMAND, SC_*)` |

### 根因（两层）
1. **视觉层**：窗口并非真正"无边框"。只调 `setWindowFlag(Qt::FramelessWindowHint)` 而**不处理 `WM_NCCALCSIZE`**，Windows 仍会保留/绘制非客户区的原生窗口控制（就是那排"复古 — □ ✕"）。规范 §3.1「无边框窗口」要求的是"没有系统边框/系统标题栏"，实现只做了一半。
2. **交互层**：自绘三钮的点击要靠 `WM_NCHITTEST` 返回 `HTMINBUTTON/HTMAXBUTTON/HTCLOSE`；而 `hit_test` 里**边缘缩放带优先于三钮**，把三钮顶部 6px 划给了缩放；再加上命中矩形取自 `build_box`（三钮 rect 与顶栏 rect 的坐标口径由 `rect_of` + `shift` 两次换算），任何一次几何不同步都会让点击落进 `HitZone::Caption`（拖拽）而不是按钮区 → "点了没反应"。

### 改进方案
1. **真·无边框收口**（`platform/frameless.{h,cpp}`）：
   - 处理 `WM_NCCALCSIZE`：把客户区扩到整个窗口（返回 0 并把 `rgrc` 设为整窗），彻底去掉原生标题栏/原生三钮 —— 复古那排从此不存在；
   - 保留 `WS_THICKFRAME`（否则 Win11 圆角与阴影消失），靠 `WM_NCCALCSIZE` + `WM_NCHITTEST` 自己管边框；
   - 三钮命中带**优先于**边缘缩放带：`hit_test` 里把三钮判定提到 `top/left/right/...` 之前（一并修 R-312）；
   - 顶栏 6px 缩放带只在**三钮之外**生效。
2. **单一控制轨**：界面只保留自绘 `— □ ✕`（规范 §3.3 46×32、WinUI3 样式、悬停灰、关闭悬停 `#c42b1c`）；给三钮加 `--ui-smoke` 探针 `caption-action`，**真发一次** `trigger()`（intercept 模式）断言 `action_count` 递增 + 命中区分类为 `CloseButton/MaxButton/MinButton`。
3. **三钮视觉校准**：`docs/m7-evidence/temp_bug.png` 里自绘字形偏细、与 WinUI3 的 10×10 字形盒不一致（`kCaptionIconPx = 10`，`mainwindow.cpp:1068`）。按规范 §3.3 重画 icons 的 `caption-min/max/restore/close`，线宽 1.4–1.6。

### 举一反三（同一类"点了不生效 / 要点第二次"）
| 项 | 位置 | 处置 |
|---|---|---|
| 弹窗 caption ✕ | `settings_dialog.cpp:587-593` / `presets_dialog.cpp:295` / `exif_editor.cpp:1652` / `map_dialog.cpp:81` 全部 `Frameless::attach` | 同样补 `WM_NCCALCSIZE` 收口（共用同一实现） |
| 三钮顶部 6px 死区 | `frameless.cpp:142-171` | 同修 |
| 双击标题栏最大化 / Aero 摸边 | 能力表 §9.1 承诺 | `WM_NCCALCSIZE` 改动后需回归验证 |
| 菜单/下拉弹出后不收起 | `classify_panel.cpp:308-320` 已用 `WA_DeleteOnClose` | 逐个核对 |
| `R-047` 生效值刷新把地图拽回 | `page_meta.cpp:1436-1438` | 顺带修 |

### 影响面 / 回归
触及平台层（`frameless.cpp`），是 14 项里**风险最高**的一项；需要 Windows 实跑验证拖拽/双击最大化/Snap/八向缩放/三钮。`--ui-smoke` 无 Windows 窗口系统，需补一条**人工/真机验收清单**。

---

## 8. 高级参数展开区

### 定位
| 事实 | 位置 |
|---|---|
| 折叠头 | `paramform.cpp:481-491` `pp-advanced-toggle`（checkable QToolButton）+ 行 `pp-advanced-row`（`:492-498`），起始折叠 |
| 折叠内容 | `pp-advanced-area`（`:501-509`）= 1px 顶缘 `pp-adv-rule` + 搜索行 `pp-advanced-search`（`:511-526`）+ **QScrollArea** `pp-advanced-scroll`（`:527-531`） |
| **"只留一点点地方"的元凶** | `paramform.cpp:532` `adv_scroll->setMaximumHeight(formkit::kRowPitch * 8); // 限高内滚（2-42）` = **224px** |
| 分组 | `paramform.cpp:130-144` **key 词根启发式**（`progressive`→渐进 / `noise`→噪声 / `speed|budget`→解码预算 / 其余→编码工具），schema 无分组字段 |
| 数值行宽随档变 | `paramform.cpp:596-598` 核心 160 / 折叠 **88** |

### 根因
为了不让格式参数卡被 JXL 的 34 个参数撑爆，给高级区设了 224px 硬上限并内滚 —— 结果是"展开了却只有一小条"，比不展开还难受；而且外层整页有滚动区（`page_output.cpp:537-541`），**两层滚动互相抢**。

### 改进方案
1. **去掉 224px 硬限**，高级区**完全展开**（高度 = 内容自然高），滚动只留外层整页一层 —— 消除双层滚动。
2. **重新设计这部分**（视觉 + 交互）：
   - 折叠头做成整行可点（点击任意处展开/收起），右侧显示"**12 项**"计数 + chevron；
   - 展开后顶部保留**搜索框**（过滤即时生效，已有 `:511-526`），并加"全部收起"链；
   - 子分组改成**二级折叠**（渐进 / 噪声 / 解码预算 / 编码工具），默认只展开第一组 —— 这样"完全展开"也不会一次性铺 34 行；
   - 分组由 **schema 显式字段**驱动，废除 key 词根启发式（`params.h:14-26` `ParamDef` 加 `group` 字段；`format_tables.cpp` 82 条补 group）。
3. 折叠区数值行宽**与核心区一致（160）**，废除"折叠=88"的第二档（§5 R2 只有 88/160/flex 三档，88 是"组内小数值"用途，不该按折叠与否换档）。

### 举一反三
"限制高度 + 内滚"这个手法全仓还有：`page_meta.cpp:3301` `exc_list->setMaximumHeight(120)`、搜索结果 112、联想 116（第 4 项改成浮层后消失）。统一原则：**滚动只在一层**（整页），卡内不再嵌滚动。

---

## 9. 格式参数多格式卡："多少位" + 各格式宽度一致

### 定位
| 事实 | 位置 |
|---|---|
| 页签文案带位深 | `page_output.cpp:1279-1285` `format_tab_label()` = `"格式名 · N 位"`，`refresh_tab_labels()` `:1327-1330` 位深一变就重写 |
| 页签宽度**不固定、不等宽** | 全文件无 `setExpanding`/固定宽度；宽度 = 文字宽 + QSS padding（`page_output.cpp:402-406`）→ "JPEG · 8 位" 与 "JPEG XL · 16 位" 宽不同 |
| 位深行每格式都有 | `page_output.cpp:1193-1210` `build_tab()` 无条件创建 `pp-bitdepth-row` / `pp-bitdepth-combo` / `pp-bitdepth-note-<id>` |
| 单一位深时是**纯装饰** | `set_bitdepth_options()` `:1403-1409` 显示 `"%1 仅支持 %2 位输出"`（JPEG 只有 8、WebP 只有 8、BMP 只有 24） |
| 输出格式磁贴也带位深文本 | `page_output` 输出格式 8 磁贴（`kTileH=30`，`formkit_layout.h`），实拍里磁贴宽度不一 |
| 冒烟探针钉住现状 | `mainwindow.cpp:9248`（`six_rows` 含 `pp-bitdepth-row`，断言"六行一义"）、`:9602`（`bitdepth-lock`）、`:9464`（`tab-label` 以"位"结尾） |

### 根因
页签文案把**参数值**（位深）拼进了**标签**，标签随内容变宽 → 页签宽度不齐；磁贴同理。而"位深"作为行又在**单一位深的格式**上是无意义的下拉。

### 改进方案（口径已定，见 §16-1）
> **口径**：只在**选项卡标题**里去掉"多少位"，**位深的控制功能原样保留**（位深行、下拉、注释、取值、预设往返、`evaluate_avif_alpha_preselect` 联动全部不变）。

1. **页签文案去掉 "· N 位"**，只留格式名；**页签等宽**（`tabBar()->setExpanding(true)` + 统一固定宽度，按最长格式名定档）。位深变化只改行控件，**不动页签文案**（`refresh_tab_labels()` 仍调用但只重写格式名，可保留以便将来换文案）。
2. **位深行保持现状**：`pp-bitdepth-row` / `pp-bitdepth-combo` / `pp-bitdepth-note-<id>` 全部保留，包括单一位深的格式（JPEG/WebP/BMP）——**功能不删**。
3. **磁贴去掉位深文本**，只留「☑ 格式名」，磁贴固定宽（`kTileH`/`kTilePadX`/`kTileGap` 已是固定值，补 `setFixedWidth`）。
4. 探针同步：`tab-label`（`mainwindow.cpp:9464`）改断言**不含**"位"且以格式名结尾；`bitdepth-lock`（`:9602`）**逐字保留**（它钉的就是位深控制功能不变）；`six_rows`（`:9248`）逐字保留。

### 举一反三
"参数值混进标签"同类：`settings_dialog` 的并行"自动=物理核数"文案与实现不符（`deep-review` R-065）；本次一并核对所有把可变值拼进固定标签的地方。

---

## 10. HEIF/AVIF 缺「编码档位 (profile)」核心选项；各格式选项合理性

> **口径澄清（2026-10-11）**：本项的 profile = **编码档位**（HEVC 的 `Main` / `Main10` / `Main12` / `Main Still Picture`，AV1 的 `Main` / `High` / `Professional`），决定**解码兼容性**，是核心选项；**不是**色彩 ICC profile。

### 定位（重点 —— 已核实到 libheif 插件源码）

| 事实 | 位置 |
|---|---|
| **两个插件都不暴露 `profile` 参数** | libheif 1.23.5 `plugins/encoder_x265.cc:167-183` 注册面 = `preset / tune / tu-intra-depth / complexity / chroma`；`plugins/encoder_svt.cc:116-133` = `min-q / max-q / qp / threads / speed / tune / chroma`。**两表均无 `profile`** |
| **HEVC 档位由插件按位深自动选** | `encoder_x265.cc:793-802`：`bit_depth==8 -> api->param_apply_profile(param, "main")`（静止图 -> `"mainstillpicture"`）、`10 -> "main10-intra"`、`12 -> "main12-intra"` |
| **AV1 档位由插件按色度/位深自动选** | `encoder_svt.cc:946-951`：`EB_YUV422 or bitdepth_y > 10 -> PROFESSIONAL_PROFILE`、`EB_YUV444 -> HIGH_PROFILE`、其余 `MAIN_PROFILE`；静止图模式 `:941-943` `svt_config.avif = true` |
| **本项目对档位零处理** | `src/codecs/enc_heif.cpp` 全文 grep `profile` 只命中 nclx/ICC 色彩配置（`:640-656`）；无任何 `main/main10` 字样 |
| 核心/高级白名单 | `enc_heif.cpp:204-206` `advanced = !(key=="quality" || key=="lossless" || key=="chroma" || key=="preset")` —— **SVT 的 `speed`（最重要的速度档）与 `tune` 全被折进高级**；且 **SVT 根本没有 `preset` 参数**（只有 `speed`），白名单里的 `preset` 对 AVIF **永不命中** |
| 位深档 | HEIF/AVIF = `{8,10,12}`（`format_tables.cpp:784,794`），已由 `pp-bitdepth-row` 呈现 |
| 参数来源 | 静态表零 ParamDef（`format_tables.cpp:782-783,790-793` `techs={}` + `runtime_introspected=true`）；实际参数 = libheif 运行时内省（`enc_heif.cpp:915-947` `introspect_backends` -> `:146-210` `param_def_from`） |

**结论**：用户问「HEIF/AVIF 怎么没有 profile」是**完全正确的** —— 档位是这类格式最关键的兼容性参数，而它在 UI 上**根本不存在**，只能靠位深间接决定；用户不知道"选 10 位 = Main10，老解码器放不了"。

### 各格式"关键参数是否都直接呈现"—— 逐格式结论
| 格式 | 核心直排 | 折叠 | 缺口 / 失效 |
|---|---|---|---|
| JPEG | quality_mode / distance / quality / chroma / progressive | optimize_coding / arith_code / restart_in_rows / dct_method / smoothing_factor / xyb_mode / adaptive_quantization / std_quant_tables / psnr_target / cicp_transfer_function | **`dct_method` 是装饰性控件**（jpegli 固定浮点 DCT，R-246）；**`arith_code` 勾了 100% 失败**（R-244）；**`xyb_mode` 承诺"需嵌 XYB ICC"但全仓无该路径**（R-245）-> 三者应置灰 + 禁用原因 |
| JPEG XL | effort / distance | 其余 30+ | 无损时 VarDCT 全片消失（正确）；`use_dct4/8/16` 在 `param-catalog.md:73` 承诺但**实现没有** |
| PNG | compressionLevel | 无 | **`compressionLevel` 写错键名**（`enc_oiio.cpp:244-249` 写裸 `compressionLevel`，OIIO 只认 `png:compressionLevel`）-> **调 0-9 完全无效**（R-010） |
| TIFF | compression | deflate_level / predictor / tiff_tile_width / tiff_tile_height | **`tiff:predictor` 无人消费**（R-011）-> 选项无效；catalog 承诺的 `zstd`/`jpeg`/`quality` 实现没有 |
| WebP | quality / sharp_yuv | method / preset / sns_strength / … 22 项 | 汇总口径与 catalog 漂移 |
| BMP | — | — | 零参数，正确 |
| **HEIF** | quality / lossless / chroma / preset | **`tune` / `tu-intra-depth` / `complexity`** | ❌ **无编码档位**；`tune`（画质取向）不该折进高级 |
| **AVIF** | quality / lossless / chroma / preset | **`speed` / `tune` / `threads` / `qp` / `min-q` / `max-q`** | ❌ **无编码档位**；**`speed` 是 SVT 最重要的速度档却被折进高级**（catalog `:110` 承诺是核心）；白名单里的 `preset` 对 SVT 永不命中；catalog 承诺的 `tier` 未兑现 |

### 根因
1. **档位被当成"插件内部细节"漏掉了**：libheif 不给 `profile` 参数，插件自己按位深/色度推导，于是 UI 从内省接口拿不到它，就等于不存在。
2. **核心/高级白名单是硬编码 key 名**（`enc_heif.cpp:205`），与插件真实参数面**不对表**（SVT 无 `preset`，`speed` 因此落空）。
3. 其它格式的假接线（PNG 键名、TIFF predictor、JPEG 三个装饰/必败控件）属同一族"参数有 UI 但不生效/不该出现"。

### 改进方案

#### 10.1 编码档位作为核心选项（本项正题）
1. **新增核心行「编码档位 (Profile)」**，objectName `pp-profile-row` / `pp-profile-combo`，置于位深行**之上**（档位是因、位深是果）：
   - **HEIF 选项**：`自动` / `Main（8 位 · 兼容性最好）` / `Main10（10 位）` / `Main12（12 位）` / `Main Still Picture（8 位 · 静止图）`
   - **AVIF 选项**：`自动` / `Main（8/10 位 · 4:2:0/4:0:0）` / `High（8/10 位 · 4:4:4）` / `Professional（12 位 / 4:2:2）`
2. **联动语义（一控件一义，不产生第二套真相）**：
   - 选定档位 -> **位深下拉限幅**（`Main` 只留 8 位；`Main10` 只留 10 位…）+ 色度限幅（`Main` 不给 4:4:4；AV1 `High` 才给 4:4:4）；
   - 反向：直接改位深/色度 -> 档位行自动重算显示为对应档；
   - `自动` = 保持现状（插件自选），档位行显示**推导结果**。
3. **落盘校验**：libheif 不接受 profile 参数，实际控制面仍是**位深 + 色度 + 静止图模式**（映射 `encoder_x265.cc:793-802` 与 `encoder_svt.cc:946-951` 的推导规则）。编码前按同一套规则反查"当前设置会产出哪个档位"，与用户选定不一致 -> **拒绝并报中文错误**（不静默出错档位）。
4. **兼容性提示**：档位非 `Main` 时在该行给 warn 色注释（"Main10 需较新解码器"），符合规范 §8「禁用不静默灰」。

#### 10.2 核心/高级重排（"关键参数都直接呈现"）
| 格式 | 提到核心 | 置灰/删除 | 说明 |
|---|---|---|---|
| AVIF | **`speed`**、`tune` | — | `speed` 是 SVT 速度主档（catalog 本就承诺核心）；`tune` 是画质取向 |
| HEIF | **`tune`** | — | x265 的画质取向（vq/psnr/ssim…） |
| JPEG | — | **`arith_code`、`dct_method`、`xyb_mode` 置灰 + 禁用原因 tooltip** | 勾了必失败 / 纯装饰 / 承诺未兑现，不该让用户踩 |
| PNG | — | 修 `compressionLevel` -> `png:compressionLevel` | 假接线，正确性缺陷 |
| TIFF | — | `predictor` 接真实控制面或标注"OIIO 自动选择、不可控" | 假接线 |
| HEIF/AVIF | 保持 `quality` / `lossless` / `chroma` / 位深 / **档位** | — | 核心面稳定为「档位 + 位深 + 色度 + 质量 + 无损 + 速度」六项 |

#### 10.3 白名单改为对表
`enc_heif.cpp:205` 的硬编码 key 白名单改为**按插件真实参数面声明**（`ParamDef` 增加 `core` 标志，由插件注册时给出），消除"SVT 无 preset 但白名单等 preset"这类静默错位。

#### 10.4 `docs/param-catalog.md` 与实现对齐
12 处漂移逐条修（`deep-review` §C9 表），并把「编码档位」写进 catalog 的 HEIF/AVIF 章节。

### 举一反三
"插件/库自动决定、用户看不见"的关键决策都要显式呈现 —— 同族还有：
- **静止图模式**：`encoder_svt.cc:941-943` `svt_config.avif = true` 与 x265 的 `mainstillpicture` 都是自动的，用户无从知晓/控制；
- **HEVC `Main` vs `Main Still Picture`**：同为 8 位下两种档位，由插件按"是否静止图"自己决定；
- **色彩侧同族（见 §14）**：HEIF/AVIF 的 nclx 写死 sRGB 与 ICC 矛盾（`enc_heif.cpp:639-650`）、JPEG `psnr_target` 静默覆盖 quality_mode（R-247）、预设 avif 后端往返丢失（R-066，`svt-av1->libaom` 被静默改写）。

---

## 11. 滚动条太粗 / 出现滚动条后卡片被压缩

### 定位
| 事实 | 位置 |
|---|---|
| 滚动条宽 **10px** | `theme.h:949-971`：竖 `width: 10px`（`:955`）、横 `height: 10px`；滑块 `border-radius: 5px`、`min-height/width: 24px`（`:957,962-963`） |
| **弹窗 QSS 完全没有滚动条规则** | `settings_dialog.cpp:411-541` `dialog_qss()` grep `QScrollBar` = 0 → 设置/EXIF/预设/地图四弹窗内滚动条走**平台默认皮**（两套并存） |
| 三页滚动区 | `page_meta.cpp:3368-3370`、`page_output.cpp:537-540`、`page_run.cpp:1886-1890`：均 `setWidgetResizable(true)`、`NoFrame`、**纵向 ScrollBarPolicy 未设**（默认 AsNeeded） |
| 卡片被压的机制 | 三页内容列都是 `QVBoxLayout(cards…, addStretch(1))`，卡片 `QFrame` 纵向 sizePolicy = **Preferred**；`widgetResizable(true)` 下视口不足 → 先吃尾部 stretch（可到 0）→ 再把 Preferred 件压向 `minimumSizeHint`。行已 `setFixedHeight(28)` 的卡有硬下限，**被压的是无固定下限的部分**（hint 行、QGroupBox 区、`exc_list->setMaximumHeight(120)`、磁贴网格） |
| 卡片内还嵌滚动 | `paramform.cpp:527-532`（高级区，见第 8 项） |

### 根因
1. 滚动条按"10px 实心轨 + 24px 滑块"画，比 SPA/现代桌面的**细浮层滚动条**粗一倍，且常驻占宽 —— 滚动条一出现就把内容区挤窄 10px，卡片跟着变形；
2. 卡片没有**最小高度保护**，`widgetResizable(true)` + Preferred 让 QScrollArea 可以把它压扁。

### 改进方案
1. **滚动条改 SPA 式细浮层**（统一进 `theme.h`，同时补进 `dialog_qss`）：
   - 轨道**透明**（不占视觉），滑块 `width: 6px`、`border-radius: 3`、半透明 `ctl-bd`；悬停加宽到 8px、加深；
   - `setOverlayScrollBar` 语义：用 `QProxyStyle`/QSS 做到"不占布局宽度"（Qt 的 `QStyle::SH_ScrollView_FrameAroundContents` + 把滑块画在视口内）；
   - **两主题一套规则**、**弹窗与主窗一套规则**（消除两套皮）。
2. **卡片不被压缩**：
   - 三页内容列的卡片全部 `setSizePolicy(Preferred, Minimum)` + `setMinimumHeight(sizeHint)`，滚动条只在**卡片整体超出**时出现；
   - 废除卡内嵌滚动（第 8 项），滚动只剩整页一层。
3. `addStretch(1)` 保留（避免卡片被拉伸到填满）。

### 举一反三
"两套皮"还有：`dialog_qss` 缺滚动条规则之外，`exif_editor` 的 QGroupBox 间距、弹窗底栏按钮 padding（`deep-review` R-186 已记 button padding 12 vs 11）—— 一并归到 `theme.h`。

---

## 12. 亮色设置页签标题色错误（+ 举一反三）

### 定位（根因确凿）
| 事实 | 位置 |
|---|---|
| 页签是自绘 `UnderlineTabs`（非 QTabWidget） | `settings_dialog.h:95-130`，段 = `QAbstractButton` 子类 `settings_dialog.cpp:597-653` |
| 标题色自绘取 tokens | `settings_dialog.cpp:646` `p.setPen(on \|\| underMouse() ? tokens_.text : tokens_.text2)`；下划线 `:640-643` `tokens_.accent`；基线 `:731-735` `tokens_.card_bd` |
| **tokens 写死 Dark** | `settings_dialog.h:129` `UnderlineTabs::tokens_ = theme::tokens(theme::ThemeMode::Dark);` |
| 暗档被复制给每段 | `settings_dialog.cpp:668` `tab->set_tokens(tokens_);` |
| **`set_tokens` 全仓零调用** | `UnderlineTabs::set_tokens`（`:722-727`）grep 无调用点 |
| 主题切换漏重放 tabs | `settings_dialog.cpp:1583-1616` 重放了 dialog_qss / mode / rotate_box / sat_labels_box / swatch / ring / app_version / title_bar，**唯独没有 tabs** |

**后果**：亮色下选中标题画 `#ececf0`（暗 text）、未选中画 `#a9aab4`（暗 text2），落在亮色卡底 `#ffffff` 上 → 选中项近乎不可见；基线 hairline 取暗 `card_bd = rgba(255,255,255,.10)` 在亮底上消失。正确值应为 `#1b1c20` / `#5d5f68` / `rgba(0,0,0,.09)`。

### 改进方案
1. `UnderlineTabs::tokens_` 初值改 `theme::tokens(theme::preferred_theme_mode())`；
2. `settings_dialog` 构造与主题切换槽都调 `tabs->set_tokens(tokens)`；
3. 加 `--ui-smoke` 探针 `settings-tabs-light`：亮色档下抓 `Tab` 绘制像素，断言选中标题色 == 亮 `text`、未选中 == 亮 `text2`。

### 举一反三（"令牌重放缺口"家族）
| 项 | 位置 |
|---|---|
| formkit Combo/Spin/DSpin **箭头**吃 `tokens_` 但主题切换漏重放 | `formkit.cpp:334-339`；漏在 `paramform.cpp:1034-1057`、`page_output.cpp:1912-1931`、`settings_dialog.cpp:1583-1616`（`deep-review` R-064） |
| **PageRun 整页硬编码 Dark** 且 `refresh_theme` 不重放 | `page_run.cpp:1883` `tokens = theme::tokens(ThemeMode::Dark)`；`mainwindow.cpp:2885-2894` `refresh_theme` 无 `page_run`（R-009） |
| `build_ui` 早期读未填充的 `d.tokens` | `mainwindow.cpp`（R-251） |
| 写死色字面量 | `mainwindow.cpp:1821` `#dd8800`；地图 HUD `page_meta.cpp:2024-2037,2095-2105`；预览浮层 `preview_panel.cpp:121-138` |

**统一处置**：`theme.h` 增加 `replay_tokens(QWidget *root, const Tokens &)` 单点重放（扫 `kFk*`/`kPill*`/`kMod*` 属性 + 会自绘的控件），所有主题切换只调它一次；写死色全换令牌（覆盖层固定色单独成组并注释"不随主题变"）。

---

## 13. 窗口没有圆角

### 定位
| 事实 | 位置 |
|---|---|
| **`DWMWA_WINDOW_CORNER_PREFERENCE` 全仓 0 处** | `grep CORNER/CornerPreference src/` = 0 |
| 主窗 QSS 无 radius | `theme.h:836-837` `QWidget#pp-root { background: <渐变>; }` |
| 无 `WA_TranslucentBackground` | grep 0 处 |
| DWM 只设了两项 | `platform/mica.cpp:31`（`DWMWA_USE_IMMERSIVE_DARK_MODE`）、`:34-36`（`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW`） |
| 弹窗有圆角 | `settings_dialog.cpp:417-421` `dialog_radius=6`（QSS） |

### 根因
Win11 的窗口圆角对**无边框窗口**需要显式申请 `DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND`；本项目从没申请过（`mica.cpp` 只做了深色标题栏与 Mica 背板）。又因为没有 `WA_TranslucentBackground`，Qt 侧也无法用 QSS 画圆角。

### 改进方案
1. `platform/mica.cpp`（或新建 `platform/frameless.cpp` 的同一收口处）加 `DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, DWMWCP_ROUND)`；不支持的 Win10 版本静默忽略（属性不存在时 `DwmSetWindowAttribute` 返回 `E_INVALIDARG`）。
2. 与第 7 项的 `WM_NCCALCSIZE` 收口一起做 —— 圆角 + 真无边框 + 单一三钮是一次性的窗口壳层改造。
3. 弹窗同走同一函数（`settings/exif/presets/map` 已是 frameless）。

### 举一反三
窗口壳层还缺的：窗口阴影（`WS_THICKFRAME` 保留后由 DWM 给）、最大化时圆角自动切直角（DWM 处理）、多显示器 DPI 下圆角半径（DWM 处理）。三项都在同一次改造里覆盖。

---

## 14. 色彩空间转换：自研还是用库，是否业界最高质量

### 结论先行
**不是自研，用的是 lcms2 2.19.1**（不是 OCIO）。主干做法（float32 批量变换 + Relative Colorimetric + 黑点补偿 + 精确 sRGB 分段 TRC + Bradford 白场适配）**达到业界主流水位**；与"业界最高质量"的真实差距在**色域映射缺位**、**元数据与像素色彩不一致**、**异常路径直接整文件失败**、**缺 ΔE 级测试**。

### 定位
| 项 | 事实 | 位置 |
|---|---|---|
| 库 | lcms2（`vcpkg.json:10`，实装 2.19.1），**无 OCIO、无 OIIO colorconvert、无自研矩阵** | `colormanager.cpp:12`；`CMakeLists.txt:156,198` |
| 精度 | **float32 全程**（解码 `oiio_reader.cpp:168` TypeDesc::FLOAT → 变换 `TYPE_RGB_FLT`），只在编码器入口量化一次 | `colormanager.cpp:46,425-427`；`enc_oiio.cpp:299-330` |
| 变换方式 | **批量** `cmsDoTransform(count = npix)`，非逐像素；α 从不进 lcms2 | `colormanager.cpp:706-707,728` |
| 意图/BPC | **写死** `INTENT_RELATIVE_COLORIMETRIC + cmsFLAGS_BLACKPOINTCOMPENSATION`，无 UI | `colormanager.cpp:39-40` |
| 色域映射 | **无**（无 `cmsFLAGS_GAMUTCHECK`、无软压缩）→ 广色域→sRGB 硬裁 | 同上 |
| sRGB TRC | 精确分段（type 4 parametric：2.4 / 1/1.055 / 0.055/1.055 / 1/12.92 / 0.04045），**不是 gamma 2.2 近似** | `colormanager.cpp:108-111` |
| 目标 profile | sRGB = `cmsCreate_sRGBProfile()`；P3 / AdobeRGB = `cmsCreateRGBProfile` 内存生成（矩阵+TRC、ICC v4、含 chad），色度学参数正确 | `colormanager.cpp:113-137`；`assets/icc/README.md:31-46`（目录里**没有 .icc 文件**） |
| 白场适配 | lcms2 chad（Bradford）自动 | `test_color.cpp:627-643` |
| transform 缓存 | LRU 16，key = `源ICC SHA1\|target\|channels\|bitdepth`；SHA1 字节恒等走恒等捷径 | `colormanager.cpp:638-643,389-396` |

### 与业界最高质量基线逐条对照
| # | 业界做法 | 本项目 | 差距 | 严重度 |
|---|---|---|---|---|
| 1 | 浮点管线、单点量化 | ✅ float32 全程 | — | 达标 |
| 2 | 白场适配（Bradford/chad） | ✅ lcms2 自动 | — | 达标 |
| 3 | 精确 sRGB TRC | ✅ 分段曲线 | — | 达标 |
| 4 | 意图/BPC 可选（默认 RC+BPC） | ❌ 写死，无 UI | 无 Perceptual/Absolute/Saturation | 中 |
| 5 | 广色域→小色域软压缩/感知映射 | ❌ 硬裁 | P3/AdobeRGB→sRGB 高饱和断崖 | **高** |
| 6 | 元数据与像素色彩空间严格一致 | ❌ HEIF/AVIF nclx 写死 sRGB 与 ICC 矛盾（`enc_heif.cpp:639-656`）；EXIF ColorSpace tag **完全不读**；JPEG CMYK 静默走 OIIO 朴素转换后当 sRGB | 三处语义脱钩 | **高**（HEIF）/中 |
| 7 | profile 通道不匹配给降级/警告 | ❌ lcms2 拒 transform → **整文件 fail**（`colormanager.cpp:425-432` → `pipeline.cpp:854-855`） | "灰度 JPEG 带 sRGB RGB ICC"这种常见输入直接不可用 | **高** |
| 8 | 线性光重采样 | ⚠️ LANCZOS3 滤波器正确、与 OIIO 逐式对拍（`test_simd.cpp:306-321`），但**在 gamma 编码空间做**（`thumbs.cpp:302-317`、`resample.cpp` 无 EOTF） | 暗部变暗/边缘光晕（仅缩略图/预览） | 中 |
| 9 | 8bit 输出抖动 | ❌ `simd.h:75`"标准舍入、无抖动"，默认 8bit | 深渐变 banding | 中 |
| 10 | ΔE 级回归 + 跨库对拍 | ❌ `test_color.cpp` 无 ΔE、无跨库对拍；P3/AdobeRGB 只有粗断言（白>0.9、spread≤0.01） | 正确性缺数值护栏 | 中 |
| 11 | HDR（PQ/HLG） | ❌ 冻结枚举显式不支持；日志说"按 sRGB 处理"但实际保留 OIIO 合成 ICC（`colormanager.cpp:565-567` vs `pipeline.cpp:667-668`） | 能力缺口 + 日志与行为不符 | 低 |
| 12 | 舍入口径统一 | ⚠️ enc_heif 内 `lround(double)`（chroma）与 `quantize16` 截断（Y/alpha）并存 | 1 LSB 级不一致 | 中 |

### 优先级与重要性说明（排序依据）

排序看三个量：**用户可见影响**（输出是错的 / 文件拿不到 / 只是不够好）× **触发概率**（哪些用户、哪些输入会撞上）× **修复成本**。核心判断是一句话：

> **"输出是错的"必须先修；"输出不够好"可以排期；但护栏（ΔE 测试）要跟正确性同批做，否则修了也无法验证。**

#### A. 正确性 —— 输出是错的 / 文件拿不到（**最高优先级，建议 0.3.2-rc**）

| 项 | 重要性 | 用户可见影响 | 触发概率 | 修复成本 | 判定 |
|---|---|---|---|---|---|
| **A1 HEIF/AVIF nclx 与 ICC 互相矛盾** | **极高** | 产物**色彩是错的**：P3/AdobeRGB 目标下同一 `colr` 组里 nclx 说 sRGB、ICC 说 P3，不同解码器显示结果不一样，甚至整图偏色。**且无任何告警** | 只要输出 HEIF/AVIF **且**色彩目标非"保持原样"就触发 —— 这是广色域工作流的主路径 | **小**（`enc_heif.cpp:639-656` 数行） | **必修** |
| **A2 灰度源 + RGB ICC（或反向）→ 整文件失败** | **高** | 常见输入**直接不可用**："灰度 JPEG 带 sRGB RGB ICC"是相机/扫描件常态，现在整批报 `color transform failed` | 灰度图 + 带 ICC 的组合；批处理里撞上一个就整文件废 | **小**（`colormanager.cpp:425-432` 加降级 + Warning） | **必修** |
| **A3 PNG `compressionLevel` 键名错误**（第 10 项） | **高** | UI 上"zlib 压缩级别"调 0–9 **完全无效**，静默 | 每个 PNG 输出 | **极小**（一个属性名） | **必修** |
| **A4 TIFF `tiff:predictor` 无人消费**（第 10 项） | 中 | `none`/`float` 选项不起作用 | 每个 TIFF 输出 | **极小**（标注"不可控"或接真控制面） | **必修**（至少标注） |
| **A5 CICP 日志说"按 sRGB 处理"但实际保留合成 ICC** | 低 | 只误导排查，不影响产物 | 少（JXL CICP 源） | **极小** | 顺手 |

**为什么 A 级最高**：A1/A2 不是"不好看"，是**产物内容错误或根本产不出来**。批量转码器的契约是"输出忠实于设定"，这两条直接违反契约，且都静默。A1 与 A2 的修复成本都只有几行，收益/成本比是全部 14 项里最高的。

#### B. 护栏 —— 不做就无法验证 A 级修复（**与 A 同批**）

| 项 | 重要性 | 为什么必须与 A 同批 | 成本 |
|---|---|---|---|
| **B1 `test_color` 补 ΔE2000 断言**（sRGB↔P3↔AdobeRGB 往返 ≤ 1.0） | **高** | A1 改的是 nclx/ICC 一致性，改完必须有**数值标尺**证明色彩没跑偏；现在 `test_color.cpp` 只有粗断言（白>0.9、spread≤0.01），改坏了也测不出来 | 小 |
| **B2 跨库对拍**（与 OIIO `ImageBufAlgo::colorconvert` 同样本对拍） | 中高 | 防"两边错得一样"——自研/库侧改动要有独立参照 | 中 |
| **B3 ICC 嵌入回读测试补 heif/avif/png/tiff/bmp** | **高** | A1 的修复**恰恰落在 heif/avif**，而现在回读测试只覆盖 jpeg/jxl/webp 且只断言"非空"。不补这条，A1 修没修好无法证明 | 小 |

**判定**：B 级不是可选项。**A 与 B 必须同一批提交**，否则等于改了色彩却没有任何护栏。

#### C. 质量提升 —— 输出不够好，但不错（**0.3.3 首批**）

| 项 | 重要性 | 用户可见影响 | 触发概率 | 成本 | 判定 |
|---|---|---|---|---|---|
| **C1 广色域→sRGB 色域映射（软裁/感知）** | **高** | P3/AdobeRGB 高饱和色转 sRGB 时**色相断崖、渐变断层**。这是摄影师批量转码最常做的一次转换 | **高**（广色域源已是常态） | 中 | **0.3.3 首个** |
| **C2 8bit 输出抖动** | 中 | JPEG/WebP/BMP 的平滑渐变出现 banding | 中（天空、棚拍背景） | 小 | 0.3.3 |
| **C3 渲染意图可选（Perceptual / RC / Absolute）** | 中 | 现在**固定 RC+BPC**——这正是 Lightroom/darktable 的默认，所以不是缺陷，是**选项缺失**。想要 Perceptual 的用户无法表达 | 中 | 小 | 0.3.3 |
| **C4 线性光重采样** | 中低 | 只影响**缩略图/预览显示**（暗部偏暗、边缘光晕），**不影响编码产物**（主管线不缩放） | 仅显示面 | 中 | 0.3.3 末 |

**为什么 C 级排在 A 之后**：C 级的每一项都"输出合法、色彩自洽，只是不如最好的工具好"。尤其 C3 —— 固定 RC+BPC **本身就是业界默认**，缺的是可选项而非正确性。C1 是 C 级里唯一"常见场景下肉眼可见"的，所以排 C 级第一。

#### D. 解码侧语义补齐（**0.3.3，举一反三**）

| 项 | 重要性 | 说明 | 成本 |
|---|---|---|---|
| **D1 JPEG CMYK/YCCK 静默走 OIIO 朴素转换后当 sRGB** | 中 | 色彩天然不对，且**与 TIFF CMYK 的"整文件拒收"口径不一致**（同一个 CMYK，TIFF 拒、JPEG 悄悄转） | 小（统一口径） |
| **D2 `Exif.Photo.ColorSpace`（0xA001）完全不读** | 中 | 只用 EXIF 标 AdobeRGB 的老 JPEG 被当 sRGB 处理 | 小 |
| **D3 HDR（PQ/HLG/BT.2100）不支持** | 低 | 冻结枚举显式不支持；在解码器、管线、编码器都没有 HDR 面的今天，补它属于**新能力**而非修缺陷 | 大 |

#### 优先级一句话总结

```
0.3.2-rc   A1 nclx错配 + A2 灰度ICC降级 + A3/A4 假接线 + B1/B3 测试护栏
0.3.3 首批 C1 色域映射（软裁） + C2 抖动 + D1/D2 解码侧语义
0.3.3 末   C3 渲染意图可选 + C4 线性光重采样
后续       D3 HDR（属新能力，需单独立项）
```

**成本合计**：A 级 + B 级约 1–2 天（含测试）；C1 是唯一需要做色彩设计决策的（软裁曲线怎么压），其余都是小改。

---

## 15. 改造顺序与风险（已定档）

| 批次 | 项 | 理由 | 风险 | 状态 |
|---|---|---|---|---|
| **S（平台壳层，单独批次）** | **7 三钮/关闭 + 13 窗口圆角** | 两者都要动 `platform/frameless`：`WM_NCCALCSIZE` 收口 + 三钮命中优先 + `DWMWA_WINDOW_CORNER_PREFERENCE`。**独立提交、独立验收、可独立回退**（已定，见 §16-4） | **高**（需 Windows 真机人工验收） | 待开工 |
| **A（正确性 + 护栏）** | 10 的 nclx 错配 + PNG `compressionLevel` 键名 + TIFF `predictor` 标注、14 的 **A1–A4 + B1/B3**、12 的页签令牌 + 举一反三令牌重放 | 都是"错的"，不是"不好看"；**测试护栏与修复同批** | 低 | 待开工 |
| **B（规范收口）** | 6 样式单源、11 滚动条、3 预览门槛、2 全选 | 一次把"公共样式"立起来，后面改版面才有依据 | 中 | 待开工 |
| **C（版面重做 + 核心选项）** | **10.1 编码档位核心行（HEIF/AVIF，用户标为核心选项，优先做）**、10.2 核心/高级重排、4 GPS 卡（地图 **260px**）、5 启用开关、8 高级参数、9 格式卡（**只去页签位深文案**）、1 右键菜单 | 依赖 B 的公共骨架 | 中高 | 待开工 |
| **D（能力补齐）** | 14 的 **C1 色域映射 / C2 抖动 / C3 意图 / C4 线性光**、D1/D2 解码侧语义 | 新增能力，按 §14 优先级排序 | 中 | 0.3.3 |

**回归口径（每批必须）**：
1. `python tools/env.py run -- cmake --build build/release-dev`；
2. `ctest --test-dir build/release-dev --output-on-failure` —— **33/33 全绿**，冻结行 `UI-SMOKE OK shots=12 pages=3` 逐字不变；
3. 亮/暗两主题成对渲染（`PP_UI_THEME=light` / `dark`），走查截图入 `docs/`；
4. 引擎零改动批次需 `tests/golden/smoke.py` 20 对金样 + 全语料 regression zero diff；
5. **批次 S 必须 Windows 真机人工验收**：拖拽 / 双击最大化 / 八向缩放 / Win11 Snap / 三钮 / 圆角 / 弹窗四件套同口径。

---

## 16. 决定记录（2026-10-11）

| # | 问题点 | 决定 | 落到方案的位置 |
|---|---|---|---|
| **1** | 第 9 项「不需要加多少位」口径 | **只是在选项卡标题中不显示，位深控制功能保持不变** —— 页签文案去掉 `· N 位`、页签等宽、磁贴去位深文本；`pp-bitdepth-row`/`pp-bitdepth-combo`/`pp-bitdepth-note-*` 及其联动（预设往返、`evaluate_avif_alpha_preselect`）**一律不动**。探针 `bitdepth-lock` / `six_rows` 逐字保留 | §9 改进方案 |
| **2** | 第 4 项地图高度 | **先按 260px 做一版**（现状 136，`kMapHeight`） | §4 改进方案 5 |
| **3** | 第 14 项范围 | **给出说明 + 优先级/重要性说明**，按用户可见影响 × 触发概率 × 修复成本排序：A 正确性（nclx / 灰度 ICC / 假接线）+ B 护栏（ΔE / 回读测试）**必做且同批**；C 质量（色域映射 C1 最优先）/ D 解码侧排 0.3.3 | §14「优先级与重要性说明」 |
| **4** | 第 7 项风险 | **可以单独批次** —— 与第 13 项（圆角）合并为**批次 S（平台壳层）**，独立提交、独立验收、可独立回退 | §15 批次 S |
| **5** | 第 10 项 "profile" 的含义 | **profile = 编码档位**（HEVC `Main/Main10/Main12/Main Still Picture`、AV1 `Main/High/Professional`），**不是色彩 ICC profile**；这是决定解码兼容性的**核心选项**，必须直接呈现。定位与方案见 §10 | §10.1 / §10.2 |

**口径补充（第 9 项）**：本次**不删任何位深能力**。若将来发现单一位深格式（JPEG/WebP/BMP）的位深下拉纯属噪音，可在 §9 加一条"单档折叠为只读文本"，但那是**功能取舍**，需另行决定，不在本轮范围。

---

*本报告只做定位与方案，不含任何代码改动。基线 33/33 全绿未被触碰。*
