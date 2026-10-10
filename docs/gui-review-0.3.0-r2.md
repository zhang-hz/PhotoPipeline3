# PhotoPipeline 0.3.0 GUI 可视化排查报告（第二轮 · 问题定位稿）

- 排查对象：0.3.0（`build/release-dev/photopipeline.exe`，与打包版同源；`dist/PhotoPipeline/photopipeline.exe`
  为 release 构建、不含 dev harness，故实机走查用 dev 产物）
- 排查方式：**真实平台渲染**（不带 `QT_QPA_PLATFORM=offscreen`）跑 `photopipeline --ui-smoke --inputs
  tests/golden/base --shots …`，暗/亮双主题各一轮，15 张实拍；关键区做像素级取证（非背景像素计数、
  行剖面、带边界取色）；根因逐条回溯到 `src/ui/**` 源码行。
- 截图防缓存：每轮截图写独立时间戳目录，读图前复制为唯一文件名；裁剪件一律带 nonce 后缀。
  证据件已固化到 [gui-r2-evidence/](gui-r2-evidence/)。
- 状态图例：🔴 高（硬伤/功能失效）· 🟡 中（明显不精致）· 🟢 低（打磨）
- **本轮性质：只定位、不改代码**。每条给出「实拍事实 → 根因（file:line）→ 修法方向」。

---

## 0. 结论速览

| # | 问题 | 级别 | 根因一句话 | 定位 |
|---|---|---|---|---|
| 1 | 亮色下拉大面积样式错误/隐形 | 🔴 | `QComboBox` 无统一构造器，5 条样式路径并存；ParamForm 的下拉**没挂样式钩子属性** | `paramform.cpp:302` 等 |
| 2 | 格式参数里格式卡片错位 | 🟡 | 「＋ 添加格式…」是 QTabWidget 的 corner widget，与页签带**不同高不同 y** | `page_output.cpp` `setCornerWidget` |
| 3 | 设置 / EXIF 弹窗 ✕ 点击无效 | 🔴 | caption 关闭钮只创建、**从未 `connect` 到 close/reject** | `settings_dialog.cpp:543` |
| 4 | 加载图片后点运行概率性错误 | 🟡 | 走查 5/5 全绿 ⇒ 不在脚本化路径；需真实交互复现（回归探针已列） | 见 §4 |
| 5 | 同功能控件样式不统一 | 🔴 | formkit「表单唯一入口」**缺 combo 构造器**，各页自造自样式 | `formkit.h` 控件清单 |

---

## 1. 亮色下拉：大面积错误，部分正常（🔴）

### 1.1 实拍事实

**（a）ParamForm 造的下拉在亮色下整只不绘制。**

像素取证（输出页「格式参数」卡字段轨，2880×1800 实拍按显示坐标取域）：

| 行 | 暗色 非背景像素 | 亮色 非背景像素 | 结论 |
|---|---|---|---|
| 位深（`page_output` 造） | 7072 / 19266 | 6099 / 19266 | 双主题都画 |
| **技术（`paramform` 造）** | **5839 / 19604** | **0 / 19604** | **亮色完全空白** |
| 无损编码（formkit 自绘） | 1329 / 16900 | 1329 / 16900 | 一致 |
| 编码努力（formkit spin） | 7111 / 19604 | 5930 / 19604 | 一致 |
| 视觉距离（formkit spin） | 7222 / 19266 | 6041 / 19266 | 一致 |

行剖面扫描（同一字段轨逐行）证实：暗色在 y=747–780 有「技术」下拉的完整绘制，亮色该段**逐行 nonbg=0**。
放大对比见 [e04-combo-4way-dark-light.png](gui-r2-evidence/e04-combo-4way-dark-light.png)。

**（b）AVIF 页签同现**：`后端`、`chroma`（枚举参数）两行同样只剩标签、字段轨空 —— [e07-paramform-light-missing-combos.png](gui-r2-evidence/e07-paramform-light-missing-combos.png)。
即 **ParamForm 建造的每一个 QComboBox 在亮色下都不可见**，而同卡内 `page_output` 造的「位深」正常。

**（c）可见的下拉是 Win32 经典「分格箭头按钮」**，与设计 v6 §R4「下拉箭头 8px 居右（.dd .car）」不符：
箭头被画进一个带竖分隔线、独立描边的方格里（见 e04 上「16 位」格、以及下条对比）。

**（d）同屏三种箭头形态**：暗色「位深」= 分格按钮 + 粗箭头；暗色「技术」= 圆角盒 + **裸箭头无分格**；
亮色「位深」= 分格按钮（描边更重）；亮色「技术」= 空。元数据页「地图源 / 搜索结果」又是**细箭头**
（[e03-meta-light-full.png](gui-r2-evidence/e03-meta-light-full.png)）。

### 1.2 根因

`QComboBox` 在本项目里**没有统一构造器**。`src/ui/formkit.h` 的自绘控件清单（契约 C2「表单唯一入口」）
包含 `CheckBox / SwitchControl / SegmentedControl / CheckableCard / ChipButton / PillBadge /
ProgressStrip / IconButton` 与 `spin_box / dspin_box`，**唯独没有 `combo_box`**。于是每个页面自己
`new QComboBox` + 自己挂样式钩子，钩子名还不一样：

| 位置 | 建造 | 样式钩子 | 谁给样式 |
|---|---|---|---|
| `page_output.cpp:1152` | `new QComboBox(st.page)` | `ppField="combo"` | `page_qss()` `QComboBox[ppField="combo"]`（page_output.cpp:356） |
| `page_meta.cpp:1868,2074,2107` | `new QComboBox(body)` | **`ppMetaField="combo"`** | `page_meta.cpp:1510` 自己的 QSS |
| `settings_dialog.cpp:348,1177,1324,1362` | `make_auto_combo()/new QComboBox` | `ppField="combo"` | `dialogkit::dialog_qss()`（settings_dialog.cpp:429） |
| `exif_editor.cpp:1043…1514` | `new QComboBox()` + `dress_combo()` | `ppField="combo"` | 同上 dialogkit |
| **`paramform.cpp:284,302,677`** | `new QComboBox` | **无任何属性** | **无规则匹配 → 走 QSS 样式表的默认路径** |
| `mainwindow.cpp:2853`（左栏「按月份」） | `group_mode` | — | 逐控件 `setStyleSheet(input)` |

`paramform.cpp` 全文件只有一处 `setProperty`（`paramform.cpp:551`，给的是注释标签的 `ppFk`）——
`tech_combo`（:302）、`backend_combo`（:284）、枚举参数 combo（:677）**全都没挂样式钩子**。

其次，全局无 `QComboBox::drop-down` / `::down-arrow` 规则：`theme.h` 的 `formkit_qss()` 注释明写
「箭头本体沿用样式绘制，待 M5-T3 的矢量图标集接入后再换 `::up-arrow/::down-arrow` 的 image」——
于是**平台样式画了经典的箭头按钮分格**，这就是 1(c) 的形态。`Metrics::combo_arrow_px = 8`
（`theme.h:195`）这个规格值实际上**没有任何消费点**。

### 1.3 修法方向

1. `formkit` 增 `combo_box(parent, spec, width)` 构造器（与 `spin_box` 同口径），统一挂 `ppFk="combo"`
   钩子 + 高 24 + 箭头 8px 居右；所有 `new QComboBox` 收敛到这一处。
2. `theme::formkit_qss()` 补 `QComboBox[ppFk="combo"]` 规则与 `::drop-down / ::down-arrow`（8px 矢量），
   退役 page_output / page_meta / dialogkit 三份重复规则与 `ppMetaField` 属性名。
3. 左栏 `group_mode` 改走同一条路径（现为逐控件 `setStyleSheet`，`mainwindow.cpp:2853`）。

---

## 2. 格式参数：格式卡片错位（🟡）

### 2.1 实拍事实

亮/暗双主题一致。页签条带与「＋ 添加格式…」是**两块分离的圆角块，垂直错位 14px，中间有缝**：
[e05-fmt-tabstrip-misalign.png](gui-r2-evidence/e05-fmt-tabstrip-misalign.png)。

像素量化（亮色，显示坐标）：

| 元素 | y 区间 | 高 | 备注 |
|---|---|---|---|
| 页签带（含 2px accent 下划线） | **664 – 704** | 40 | 下划线在 y=700–704 |
| 「＋ 添加格式…」块 | **678 – 718** | 40 | **比页签带低 14px** |

水平方向：x≈1840 处页签带圆角收尾（y=666–668 转色），x≈1860 起是添加格式块 —— **中间有可见缝隙**。

### 2.2 根因

`page_output.cpp` 的 `build_param_card()`：

```cpp
tabs = new QTabWidget(param_card);
add_format_button = new QToolButton(tabs);          // 父 = tabs
add_format_button->setFixedHeight(theme::Metrics::btn_h);   // 26
tabs->setCornerWidget(add_format_button, Qt::TopRightCorner);
```

corner widget 由 QTabBar 用自己的几何排布，与页签带底色带**不是同一个绘制单元**：按钮固定高 26，
页签带高 40（`QTabBar::tab` 的 `padding: 5px 12px 6px` + 2px 下划线），corner widget 在其中的垂直
居中口径与页签带背景不一致 → 14px 错位 + 缝隙。

### 2.3 修法方向

把「＋ 添加格式…」从 corner widget 改为与页签条同一条水平布局（同一容器、同高、同 y），
或给 corner widget 显式几何钉齐（高度 40 + 与页签带同顶边）；探针断言两者 y 区间一致。

---

## 3. 设置 / EXIF / 预设 弹窗：✕ 点击无效（🔴）

### 3.1 事实（代码级已闭环）

`dialogkit::make_title_bar()`（`settings_dialog.cpp:525`）创建 caption 关闭钮：

```cpp
auto *close = new CaptionCloseButton(t.text, t.close_hover_fg, bar);
close->setObjectName(QStringLiteral("pp-dlg-close"));
close->setFixedSize(theme::Metrics::caption_btn_w, theme::Metrics::caption_btn_h);
```

但**没有任何一处把它接到关闭动作**。逐文件核对 `connect(` 全清单：

- `settings_dialog.cpp` 共 9 处 `connect`（:642 :717 :1294 :1470 :1472 :1473 :1476 :1518 :1566）
  —— 只有 `cancel_btn → QDialog::reject`、`ok_btn → QDialog::accept`、`defaults_btn`。**无 `pp-dlg-close`。**
- `exif_editor.cpp` 只有 `QDialogButtonBox::accepted/rejected`（:1656/:1657）。**无 `pp-dlg-close`。**
- `presets_dialog.cpp:310` 的 `close_btn → reject` 是**页脚「关闭」按钮**（:275 `make_button`），
  不是标题栏 ✕。**caption ✕ 同样无连接。**

对照：主窗 caption 关闭是显式接线的 —— `mainwindow.cpp:2055` `connect(d.cap_close, &QPushButton::clicked, …)`。
三处弹窗漏接，实锤。

`Frameless::attach(this, title_bar.widget, {title_bar.close})`（settings:1577 / exif:1664 / presets:295）
只把 ✕ 放进 `drag_excludes`（点它**不会**误触发拖拽，`frameless.cpp` 的 `hit_test` 命中
`HitZone::Client`），也不负责接线 —— `set_caption_buttons()` 只有主窗调用（`mainwindow.cpp:2024`）。

### 3.2 影响

设置、元数据编辑器、预设管理三个弹窗的标题栏 ✕ **点了没反应**，只能靠「取消 / 确定 / 页脚关闭」退出。
用户第一反应是"软件卡死"。

### 3.3 修法方向

`dialogkit::make_title_bar()` 出参携带 `close`，三个弹窗各自补
`connect(title_bar.close, &QPushButton::clicked, <dialog>, &QDialog::reject);`
（或在 `make_title_bar` 内直接接 `QWidget::close()`）。回归探针：点 ✕ 后断言 `result()==Rejected`
且对话框不可见。

---

## 4. 加载图片后点运行概率性错误（🟡 · 待实机复现）

### 4.1 已排除

`photopipeline --ui-smoke`（**含真实实跑** `smoke_run()`）连跑 **5 次全绿**（exit 0，无 `ui-smoke:` 失败行）。
⇒ 故障不在脚本化走查覆盖的路径上，属**真实交互时序**问题。

### 4.2 守卫点清单（`MainWindow::on_start()`，mainwindow.cpp:2281）

点击「开始运行」时会弹模态框的全部出口：

1. `无法开始 / 正在运行`（:2284）
2. `无法开始 / 没有文件`（:2288）
3. `无法开始 / 未勾选任何文件`（:2293）
4. `无法开始 / <ready_to_start()>`（:2298）—— 未设置输出根目录 / 非绝对路径 / 未选格式 / 模板非法
5. `无法开始 / <cross_validate 消息>`（:2329）
6. `仅元数据预检 / …仍要继续吗？`（:2351，仅元数据模式）

「概率性」最可能来自 **1**（重入）或 **4/5**（状态在两次读取之间变化）：

- 重入面：`lock_for_run()` **没有直接禁用 `d.start`**，禁用由 `refresh_status()` 的
  `d.start->setEnabled(can_start)` 落地（mainwindow.cpp:2604），且 `can_start` 依赖
  `ready_to_start()` 的重算。若 `refresh_status()` 在某些时序下未被调用到，按钮在运行期仍可点 →
  弹「正在运行」。
- 状态面：加载图片后缩略图/probe 异步进行，`ready_to_start()` 在 `refresh_status()`（定时/事件驱动）
  与 `on_start()`（点击时刻）各算一次，两次之间控件状态可能已变。

### 4.3 待补回归探针（本轮建议列入测试）

`run-guard`：加载语料 → 设输出根 → 连点「开始运行」N 次 + 运行中再点 + 结束后立即重跑，
断言**全程不出现任何模态 QMessageBox**（可经 `qApp` 的 `QEvent::WindowBlocked` / 定时器扫描
`QApplication::activeModalWidget()` 取证），并断言运行期 `pp-start` 处于 disabled。

---

## 5. 同功能控件样式不统一：没有公用代码（🔴）

### 5.1 事实

设计说明书 v6 的契约 C2 写明「页面的表单行一律经 formkit 构造 —— R1–R4 在**构造层**强制」。
但 `src/ui/formkit.h` 的控件清单**没有 `combo_box`**：

- 有：`frow / double_row / kv_row / check_row / spin_box / dspin_box / spin_row`
- 有（自绘）：`CheckBox / SwitchControl / SegmentedControl / CheckableCard / ChipButton /
  PillBadge / ProgressStrip / IconButton`
- **无：QComboBox 构造器**

于是同一功能控件出现 6 个实现点、3 种属性名、4 种视觉形态（§1 表）。同类问题也波及：

- spin：`formkit::spin_box`（挂 `ppFk="spin"`，走 `formkit_qss`）vs `exif_editor.cpp:236 dress_spin()`
  （自己 `setProperty(theme::kFkProperty, kFkSpin)`）vs `settings_dialog.cpp:362 make_spin()`。
- 按钮：`page_output` 的 `QPushButton[ppField="button|primary"]`、`page_meta` 的
  `QPushButton[ppMetaField="btn|danger|link]`、`dialogkit` 的 `QPushButton[ppField="button|primary"]`
  —— 三套选择器、三套规则文本，逐字重复。

### 5.2 修法方向（软件工程化收口）

1. `formkit` 补 `combo_box`（+ 可选 `combo_row`），成为 QComboBox **唯一**构造点；
   以 `static_assert` / 探针钉住"全界面下拉同高同钩子"。
2. 属性名收敛到单一名（建议 `ppField`），退役 `ppMetaField`。
3. 按钮规则抽到 `theme::formkit_qss()`，页面不再自带 `QPushButton[...]` 规则。
4. 加一条结构位断言（同 `filelist-geom` 的 `focus-frame-off` 口径）：
   `formkit-combo-hook` —— 枚举全窗口 QComboBox，逐个断言挂了唯一钩子且几何一致。

---

## 6. 附带观察（本轮顺带，未单独编号）

- 🟡 元数据页「日期偏移/时间偏移」6 个 spin 的箭头仍走平台分格（`::up-button/::down-button` 只定了
  几何 17×12，箭头图形沿用样式绘制），观感与下拉同病（§1.2 末段）。
- 🟢 格式参数卡「无损编码」勾选框左缘 x≈1316.7，与同卡字段轨（位深/技术/编码努力）左缘存在小偏差，
  随 §2 的栅格收紧一并校准。

---

## 7. 证据索引（`docs/gui-r2-evidence/`）

| 文件 | 内容 |
|---|---|
| `e01-output-light-full.png` | 输出页 · 亮色整页（下拉/格式卡） |
| `e02-output-dark-full.png` | 输出页 · 暗色整页（同状态对照） |
| `e03-meta-light-full.png` | 元数据页 · 亮色整页（第三种下拉形态） |
| `e04-combo-4way-dark-light.png` | 下拉 4 格对比：暗位深/暗技术/亮位深/亮技术（亮技术空白） |
| `e05-fmt-tabstrip-misalign.png` | 页签带 vs「＋添加格式…」错位（双主题对照） |
| `e06-fmtcard-light-missing-tech.png` | 亮色格式参数卡（技术行空白） |
| `e07-paramform-light-missing-combos.png` | 亮色 AVIF 页签（后端/chroma 行空白） |
| `e08-settings-dialog-light.png` | 设置弹窗（✕ 目标） |
| `e09-exif-editor-light.png` | 元数据编辑器弹窗（✕ 目标） |

### 取证口径（防识图误差）

- 所有"缺失"结论以**像素计数**为准，不以肉眼为准：非背景像素 = 与该域众数色 RGB 距离和 > 24 的像素数。
- 「亮色技术行 0/19604」与「暗色 5839/19604」是同一坐标域、同一脚本两次跑出，可复算。
- 行/列坐标一律注明是**显示坐标**（原图 2880×1800，显示比例 ×1.44），避免与物理像素混用。

---

## 8. 已落地的回归测试（`smoke_probe_gui_r2()` + `tests/ui_smoke.py`）

四条**只增**自检行，判据硬判（`smoke_fail`），前缀已注册进 `GUI_R2_LINE_PREFIXES`（缺行即 FAIL）。
既有前缀清单零改动、冻结末行不变（截图仍 12 张）；实测既有 12 组探针行各 1 条、全齐。

| 自检行 | 钉住的问题 | 0.3.0 实测 | 判定 |
|---|---|---|---|
| `combo-consistency` | 问题 1 + 5 | `checked=29 no-hook=9 h-bad=2 invisible=0 heights=[24 30 18 22]` | 🔴 红 |
| `format-card-align` | 问题 2 | `tab=0+0,462x25 add=462+10,123x26 dy=10 dh=1` | 🔴 红 |
| `dialog-close` | 问题 3 | `checked=3 bad=3` `settings/close-noop exif-editor/close-noop presets/close-noop` | 🔴 红 |
| `run-guard` | 问题 4 | `checked=2 bad=0 modals=0 rounds=2` | 🟢 绿（脚本化路径未复现） |

**读法：前三条红即三条缺陷的活证据**，修好对应缺陷后自动转绿；第四条是为问题 4 预置的守卫
（真实交互若再出"概率性错误"，它会把模态框内容打进 `modals=` 字段并判红）。

> 本表是**修复前**基线。四条缺陷已根治，修复后的实测与改动清单见 [§9](#9-修复记录本轮已落地)
> —— 现在四条全绿、`ctest` 29/29。

### 逐条要点

**combo-consistency** —— 29 个下拉里 9 个没有样式钩子：

```
win:pp-tech-combo, pp-param-quality_mode, pp-param-chroma, pp-param-dct_method,
pp-param-preset          ← ParamForm 造的全部下拉（问题 1 病灶）
win:pp-group-mode        ← 左栏「按月份」（mainwindow 逐控件 setStyleSheet）
exif:add_exif_group, exif:add_exif_type  ← EXIF 编辑器底部两枚
```

`heights=[24 30 18 22]` —— 同一功能控件**四种高度**，是问题 5「样式不统一」的直接物证
（`formkit.h` 钉的规格是 `combo_h = 24`）。

> ⚠️ `invisible=0` 的口径说明：亮色"整只不绘制"是**真实平台**渲染现象；`ui_smoke.py` 恒以
> `QT_QPA_PLATFORM=offscreen`（Fusion）跑，该档下不复现。但**结构性病灶就是 `no-hook`** ——
> 9 个无钩子控件与实拍隐形清单完全重合，故本探针在任一平台上都能钉住根因。
> 真实平台的亮色复核请用 `PP_UI_THEME=light photopipeline --ui-smoke --shots <dir>`（不带 offscreen）。

**format-card-align** —— `dy=10`：「＋ 添加格式…」比页签带低 10px（真实平台实拍色带口径为 14px，
两者同因：corner widget 与页签带不是同一绘制单元）。判据 `|dy|≤2 且 |dh|≤2`。

**dialog-close** —— 三个弹窗逐个 `show → click ✕ → 断言不可见`，**全部 `close-noop`**：
点关闭钮后对话框仍在。与 §3 的代码结论（三处均无 `connect`）互证。

**run-guard** —— 两轮「点击开始运行 → 断言进入运行态 + 运行期按钮禁用 + 无模态 → 取消复位」，
实测全绿；模态看门狗（30ms 轮询 `QApplication::activeModalWidget()`，一出现即记录并关闭）
全程 0 命中。**问题 4 在脚本化路径上未复现**（与 §4.1 的 5/5 全绿一致），需真实交互时序。

---

## 9. 修复记录（本轮已落地）

问题 1/2/3/5 已根治，四条探针**全绿**；问题 4 预置防御。`ctest --preset release-dev` **29/29 通过**，
`tools/ci-format.sh` 全源 0 diff。

### 9.1 问题 1 + 5 —— 下拉单源化（`formkit::combo_box`）

**新增唯一构造点**：`formkit::ComboSpec` + `formkit::combo_box()` / `combo_row()`（`src/ui/formkit.*`），
配套自绘 `formkit::Combo`（基类画盒/字，右端用 `icons::paint(ChevronDown)` 补画 8px 矢量箭头 —— C3 单源）。

- **样式钩子唯一** = `ppFk="combo"`（`theme::kFkCombo`）；规则只有一份，在 `theme::formkit_qss()`，
  主窗与三个弹窗共用（`dialogkit::dialog_qss` 本来就包含 `formkit_qss`）。
- **`::drop-down` 只留箭头位几何、`::down-arrow` 置空** —— 平台不再画经典分格按钮，
  `Metrics::combo_arrow_px = 8` 终于有了消费点。
- **盒高单源** = `combo_box()` 的 `setFixedHeight(combo_h)`；QSS 不再夹 min/max-height
  （Qt 的 QSS 高度是另一套盒模型口径，会把总高压成 22 → 同一控件两种高）。

**六个建造点全量迁移**（`new QComboBox` 归零）：

| 原建造点 | 处置 |
|---|---|
| `paramform.cpp` 技术/后端/枚举参数 | → `formkit::combo_box` |
| `page_output.cpp` 位深/同名冲突/色彩目标/当前预设 | → `formkit::combo_box` |
| `page_meta.cpp` 时区×2/地图源/搜索结果 | → `formkit::combo_box` |
| `settings_dialog.cpp` `make_auto_combo` + 结构/底图/日志级别 | → `formkit::combo_box` |
| `exif_editor.cpp` `dress_combo` + 分组/类型 | `dress_combo` 改挂 `ppFk="combo"` + 高 24 |
| `mainwindow.cpp` 左栏「按月份」 | → `formkit::combo_box`；退役逐控件 `setStyleSheet` |

**退役三套重复规则**：`page_qss` 的 `QComboBox[ppField="combo"]`、`page_meta` 的
`QComboBox[ppMetaField="combo"]`、`dialogkit` 的 `QComboBox[ppField="combo"]` + 其 `:disabled` 变体。

**实测**：`combo-consistency: checked=29 no-hook=0 h-bad=0 invisible=0 heights=[24]` ——
29 个下拉（主窗 + 设置/元数据编辑器/预设三弹窗）全部同钩子、同高 24、明暗两档实拍都有内容。
`grep "new QComboBox" src/` **归零**（只剩两条契约注释）—— 下拉确实只有一处出生地了。
真机亮色 `技术` 行非背景像素 **0/19604 → 6331/19604**，见
[e10-fixed-fmtcard-light.png](gui-r2-evidence/e10-fixed-fmtcard-light.png)（两枚下拉箭头已统一为
8px 矢量、无分格）。

### 9.2 问题 2 —— 页签带与「＋ 添加格式…」对齐

`page_output.cpp` 的 `AddFormatAligner`（事件过滤器）：布局/尺寸变化后把按钮**垂直居中**到页签带上
（按钮高 26 是 A7 规格，不随页签带拉伸；真机页签带高随字族为 29）。判据相应改为
**中心线对齐 + 含于带内**（`|dmid|≤2 && inside`），不要求同高。

**实测**：offscreen `dmid=1 inside=1`、真机 `dmid=0 inside=1` —— 原 14px 错位与中间缝隙消除。

### 9.3 问题 3 —— 弹窗 caption ✕ 接线

新增共享接线 `dialogkit::bind_close_button(TitleBar&, QDialog*)`（`settings_dialog.*`），
三个弹窗各调用一次（幂等，`ppCloseBound` 标记防重复 connect）。原先 `make_title_bar` 只建按钮、
三个弹窗都没接线。

**实测**：`dialog-close: checked=3 bad=0` —— 设置/元数据编辑器/预设 三个弹窗点 ✕ 全部真关窗。

### 9.4 问题 4 —— 防御性收紧

`lock_for_run()` 开头**立即** `d.start->setEnabled(!lock)`，不再只依赖末尾 `refresh_status()`
的 `setEnabled(can_start)`（原先中间存在可点窗口期，重入会弹「正在运行」模态）。
`run-guard` 探针继续值守：两轮重入 + 模态看门狗，实测 0 命中。

### 9.5 改动清单

| 文件 | 改动 |
|---|---|
| `src/ui/theme.h` | `kFkCombo` 常量 + `formkit_qss` 的下拉规则（含 `::drop-down/::down-arrow`） |
| `src/ui/formkit.h/.cpp` | `ComboSpec` / `Combo`（自绘箭头）/ `combo_box` / `combo_row` |
| `src/ui/paramform.cpp` | 3 处下拉迁移到 `combo_box` |
| `src/ui/page_output.cpp` | 4 处迁移 + 退役本页下拉规则 + `AddFormatAligner` |
| `src/ui/page_meta.cpp` | 4 处迁移 + 退役 `ppMetaField` 下拉规则 |
| `src/ui/settings_dialog.*` | 4 处迁移 + 退役 dialogkit 下拉规则 + `bind_close_button` |
| `src/ui/exif_editor.cpp` | 9 处下拉收敛到本地 `make_combo`（→ `formkit::combo_box`）；`dress_combo` 随之退役 |
| `src/ui/presets_dialog.cpp` | 调用 `bind_close_button` |
| `src/ui/mainwindow.cpp` | 左栏下拉迁移 + `lock_for_run` 防御 + 探针换锚 `ppFk="combo"` |
| `tests/ui_smoke.py` | `GUI_R2_LINE_PREFIXES` 四行注册（既有清单零改动） |

冻结末行不变（截图仍 12 张）；既有 12 组探针行全绿。

---

## 10. 第二轮：判定口径修正 + 举一反三（用户复审后）

用户复审指出两处仍不合格：**数值输入框还是复古 spinner**、**「＋ 添加格式…」仍未与页签同带**。
根因不是"又出了两个新 bug"，而是**我的验收口径错了** —— 我用**控件几何**（`dmid=0`、`heights=[24]`）
宣布合格，可这两个缺陷的本质是"几何对了、**画出来**还是平台的复古件"，几何探针天然测不到。

### 10.1 两处残留缺陷的修法

**① 数值框复古 spinner** —— 与下拉完全同病：`formkit_qss` 只给 `::up-button/::down-button` 定了几何，
注释自己写着「箭头本体沿用样式绘制，待 M5-T3 矢量接入后再换」。上一轮我只换了下拉、漏了 spin。

- `theme::formkit_qss`：`::*-button` 去掉底/边、`::*-arrow` 置空 → 平台不再画独立描边按钮格；
- 新增 `formkit::Spin` / `DSpin`（`paint_spin_arrows`）自绘：列内**竖发丝线 + 横发丝线 + 矢量 ▲▼**
  （ChevronDown 旋转 180° 得上箭头，形只有一份）—— 对齐设计稿 `.spin .ar`（17px 列、`border-left`）。

**② 「＋ 添加格式…」两条灰带** —— 上一轮修的是**按钮**几何，但视觉带不是按钮画的。
设计稿 `output.html` 的页签行是 `[tab][tab][＋ 添加格式…]` 同一 flex 行 + 底部 1px 发丝线 +
按钮 `btn.lnk`（`border:0;background:transparent`）—— **整行没有任何色带**。实测两条 29px 灰带是
`QTabBar` / corner 区没设透明、吃了 palette 的 `--ctl` 底。补 `QTabWidget/QTabBar { background: transparent }`
后色带消失（pixel 扫描 x=1800/1870/1950 处 `gray-bands=[]`）。

### 10.2 举一反三：同一缺陷类的完整审计

判据是「**设计稿要求自绘/令牌化，但实现让平台画**」。全量清点结果：

| 控件 | QSS 规则数 | 状态 | 处置 |
|---|---|---|---|
| `QComboBox` 本体箭头 | 有 | 曾漏 | ✅ 已自绘（第一轮） |
| `QSpinBox`/`QDoubleSpinBox` 箭头 | 有 | **曾漏** | ✅ 已自绘（本轮） |
| `QComboBox` **弹出列表** | **0** | 曾漏 | ✅ 本轮补 `QAbstractItemView` 规则 |
| `QScrollBar` | **0** | 曾漏（审查 1-50 点过名） | ✅ 本轮补令牌化细轨样式 |
| 页签行色带 | — | **曾漏** | ✅ 本轮透明化 |
| `QTreeView::branch`（树展开符） | **0** | 平台画 | ⬜ 未修 —— 需代理样式画矢量箭头 |
| `QMenu`（右键菜单，18 处） | **0** | 平台样式 | ⬜ 未修 |
| `QMessageBox`（38 处报错框） | **0** | 平台样式 | ⬜ 未修 |
| `QToolTip` | **0** | 只吃 palette | ⬜ 未修 |
| `QGroupBox`（10 处） | **0** | 平台默认 | ⬜ 未修 |
| `QFileDialog` | — | 原生对话框 | ⬜ 设计上允许（平台惯例） |

前五行已修；后六行是**同一缺陷类的存量**，留作下一轮（建议与 `QMessageBox` 一并做，因为
问题 4 的报错框就在其中）。

### 10.3 判定口径修正：`render-tokens` 探针

新增第五条自检行 `UI-SMOKE render-tokens:` —— **只以渲染像素判**，专抓"几何对、画错"：

* 箭头列内不得有**贯穿的对比线**（竖分隔 / 横分隔）—— 平台「独立描边按钮格」的签名；
* 箭头列内必须有图形（矢量箭头）；
* 页签行不得有 `--ctl` 平坦色块（占比阈值 3%）。

对比度取**相对域众数色**的偏差（>40），不写死色值 → 明暗两档同判据。
实测 offscreen + 真机亮色双绿：`checked=43 bad=0`。

> **写这条探针时我自己又踩了两次同一个坑**，一并记下：
> ① 第一版拿整个 `QTabWidget` 的 grab 判色带 —— 页签**内容区**的下拉/spin 本来就是 `--ctl` 底，
> 误报 `ctl-fill=8%`；改成只扫页签条那一行后归零。
> ② 反复用肉眼估裁剪坐标（把 vision 2000 宽当物理像素、把文本旁笔画当箭头），两次判错；
> 改用 PNG 物理像素程序测量后一次定案（组合盒 1896..2215 = 160 逻辑宽、箭头在 2190..2193 = 居右）。
> **小图/裁剪图的肉眼判读不可靠，程序测量才是判据** —— 这是本轮最该带走的一条。

### 10.4 验收（第二轮）

| 项 | 结果 |
|---|---|
| GUI-R2 五条探针（offscreen） | 5/5 绿 |
| GUI-R2 五条探针（真机 · 亮色） | 5/5 绿，`UI-SMOKE OK shots=12 pages=3` |
| `ctest --preset release-dev` | **29/29** |
| `tools/ci-format.sh` | 全源 0 diff |

---

## 11. 第三轮：平台代画存量全部收口

用户要求「全部修复」—— §10.2 审计表里剩下的 5 项 + 走查中新发现的 1 项，共 6 处全部处理。

### 11.1 矢量指示器（QSS 只能塞图片、画不了矢量形 → 样式层自绘）

| 平台件 | 处置 | 落点 |
|---|---|---|
| `QTreeView::branch`（树展开符） | ✅ 矢量 ChevronRight/ChevronDown | `formkit::paint_branch_indicator` |
| `QMenu::indicator`（菜单勾选） | ✅ 矢量 Check | `formkit::paint_menu_check` |
| `PE_IndicatorArrowDown`（菜单钮下拉小三角） | ✅ 矢量 ChevronDown | 同代理样式（**走查中新发现**：左栏 ＋/筛选 钮下方那枚） |

三者统一收在**应用级 `QProxyStyle`**（即既有的 `FileViewFocusFrameStyle`，安装点不变），
形只有一份（`icons` 库，C3 禁字形/禁第二套图标）。树展开符**不画连接线**（设计稿的树没有）。

### 11.2 令牌 QSS（此前零规则 → 平台默认皮）

| 控件 | 处置 |
|---|---|
| `QMenu`（18 处：分类右键/筛选/参数/异常列表） | ✅ `--card` 底 + `--card-bd` 边 + r3 + 选中 `--acc-dim` + 分隔线令牌；勾选留几何位给矢量 |
| `QMessageBox`（38 处：`on_start` 六闸门、预设读写失败…） | ✅ `--card` 底 + 标签 `--txt` + 按钮令牌化（R4 高 26 口径） |
| `QToolTip` | ✅ `--card-hi` 底 + `--txt` 字 + 边/圆角/内距（palette 的 `ToolTipText` 已在令牌表内） |
| `QGroupBox`（EXIF 编辑器 时间/GPS/更多字段） | ✅ 扁平化：透明底 + `--card-bd` 发丝框 + 标题 `--txt2`，消除双层框线 |
| `QTreeView::branch` / `QMenu::indicator` 几何位 | ✅ 16px 盒，图形交给矢量自绘 |

### 11.3 `render-tokens` 探针扩到全部新修项

新增两类判据（仍是**渲染像素**口径）：

* **树展开符**：矢量 chevron 的笔画是斜线（横/竖连续段都短）；平台的方框/加号展开符有 ≥8px 的
  轴对齐贯通描边。据此区分，**不写死形状**。
* **令牌 QSS 在场性**（结构位）：`QMenu / QMessageBox / QToolTip / QGroupBox` 四条规则不得被回退掉。

实测 offscreen + 真机亮色双绿：`render-tokens: checked=45 bad=0`。

### 11.4 验收（第三轮）

| 项 | 结果 |
|---|---|
| GUI-R2 五条探针（offscreen / 真机亮色） | 5/5 绿、5/5 绿 |
| `ctest --preset release-dev` | **29/29** |
| `tools/ci-format.sh` | 全源 0 diff |
| §10.2 审计表 10 项 | **全部已修**（`QFileDialog` 除外 —— 原生对话框是平台惯例，设计上允许） |

> **取证方式说明**：本轮小裁剪图（工具行 1152×192 等）在本环境出现过「读到的内容与磁盘文件不符」，
> 故结论一律以探针的程序测量为准，肉眼只做辅助。这也再次印证 §10.3 的教训 ——
> **小图肉眼判读不可靠，程序测量才是判据**。

---

## 12. 第四轮：工具钮箭头 + 「两排选择框」

### 12.1 问题 1 —— ＋/−/筛选 要不要下拉箭头？**不要**

设计稿 `output.html` 的 `.ftools` 是：`搜索框 / ＋ / − / 筛选 /「按月份 ▾」` ——
前三枚是**纯图标钮**（`icon-btn bordered`，语义由 `title`/tooltip 承担），
**只有分组下拉带箭头**。当前 ＋/筛选 因挂了菜单（`setMenu` + `InstantPopup`），
平台就画了下拉三角 —— 是实现自己长出来的。

处置：`QToolButton::menu-indicator { image: none; width:0; height:0 }`（theme::style_sheet）。
菜单功能保留（＋ = 添加文件…/添加文件夹…，筛选 = 三个可见性开关），语义由 tooltip 承担。
减号钮本就无菜单，不涉及。

实测：工具钮区深色像素只在 y=198–216（图标本体），下方 y=220–276 全 0 → 箭头已去除。

### 12.2 问题 2 —— 选中后出现两排选择框

**根因**：`FileListModel::flags()` 对**所有列**返回 `Qt::ItemIsUserCheckable`，而内容列的绘制走
`FileDelegate::paint` —— 它 `initStyleOption` 后**没清 `HasCheckIndicator`**（勾选列那条分支清了，
内容列漏了）。于是样式在内容列左缘又画了一枚原生勾选框，大半被缩略图挡住、只露出一条浅蓝边；
选中高亮后更显眼 → 看着像"两排选择框"。

**处置**（两处都改，一治本一防御）：

1. `flags()`：`ItemIsUserCheckable` **只给勾选列**（勾选语义本就只属勾选列）；
2. `FileDelegate::paint` 的背景绘制块补 `bg_opt.features &= ~HasCheckIndicator`（任何列都不许
   样式画勾选指示，勾选框一律自绘）。

实测（行中线 y=360 色段，PNG 物理像素）：

| | 修复前 | 修复后 |
|---|---|---|
| 勾选框（自绘，含勾形白缝） | `ACCENT 38..56 / OTHER 57..59 / ACCENT 60..63` | 同 |
| **第二枚框** | `CTL 64..85 (w=22)` + `OTHER 86..91`（描边） | — |
| 行底 | — | `CTL 64..91 (w=28)` **连续** |

> 附带澄清一处误读：`ACCENT 38..56 + OTHER 57..59 + ACCENT 60..63` 不是三个元素，
> 逐像素 ASCII 显示 y=363–372 的白缝是**勾形笔画** —— 整体就是一枚勾选框。

### 12.3 探针

`render-tokens` 增两条：

* **勾选语义只属勾选列**：逐行断言 `flags(content列)` 不含 `Qt::ItemIsUserCheckable`
  —— 直接钉根因（回归时立刻红）；
* `QToolButton::menu-indicator` 规则在场（结构位）。

实测 `render-tokens: checked=46 bad=0`（offscreen + 真机亮色双绿）。

### 12.4 验收

| 项 | 结果 |
|---|---|
| GUI-R2 五条探针（offscreen / 真机亮色） | 5/5 绿、5/5 绿 |
| `ctest --preset release-dev` | **29/29** |
| `tools/ci-format.sh` | 全源 0 diff |
