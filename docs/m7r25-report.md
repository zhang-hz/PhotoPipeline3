# M7R25 收口报告：GUI 视觉缺陷根治 + 识图门禁（2026-10-13）

> 起因 = 用户 `debug/debug1.png` + `debug/debug2.png`（7 处红框）+ 追问四条：
> 「首先消除问题，然后深度审查测试、约束、门禁。GUI 开发没有做测试吗？最终没有做 E2E 测试吗？
> 没有识图步骤吗？太差了！！杜绝类似问题发生。」
> 本文 = ① 交付 ② 验收取证 ③ **测试/门禁体系深查**（第二问正面回答）④ 偏差与追认 ⑤ 未覆盖/未决 ⑥ 复现命令。

---

## 1. 交付内容

### 1.1 视觉缺陷逐条根因与修复（9 处，全部真机核对通过）

| # | 用户红框（现象） | 根因 | 修复 |
|---|---|---|---|
| D1 | debug1 ①：工具栏第 4 钮（勾选操作）白底，与同组三钮不同脸 | `theme_replay_panels` 的 `mini` QSS 选择器只列了 `pp-add / pp-remove-sel / pp-filter`，`pp-check-menu` 漏在外面 → 吃默认样式 | 选择器补 `QToolButton#pp-check-menu`（含 `:disabled`）；`check_menu_btn` 同套 `mini`。`mainwindow.cpp:1662` |
| D2 | debug1 ②：「关键信息」卡头大片空白 | `formkit::lock_card_size` 用 **一次性** `setMinimumHeight(sizeHint)` 钉下限：钉高时隐藏的 5 行 kv 也被计入 → 空态收起行后卡体仍按满高铺，整片留白 | 下限改**实时 sizeHint**（`setSizePolicy(Preferred, Minimum)`，无 ShrinkFlag ⇒ 父布局把 sizeHint 当下限）+ 清掉历史钉数。`formkit.cpp:1145` |
| D3 | debug1 ③：预览底条「上一张/下一张」两块白方 | `make_nav` 只 `setProperty(kIconNameProperty)`（探针钩子），**从不 `icons::apply`** → 矢量图标从未挂上 | `PreviewPanel::Impl::apply_style` 补 `icons::apply(prev/next, …)`（图标是矢量渲染色、不吃 QSS，随令牌重放）。`preview_panel.cpp:943` |
| D4 | debug2 ①：格式磁贴「JPEG XL」被省略成「JPEG…」 | `CheckableCard::paintEvent` 的 `name_rect` **无后缀时也减掉列距 kTileGap**（6px）→ 最宽主字必然省略 | 后缀只在有后缀时让位（与 `sizeHint` 同式）。`formkit.cpp:680` |
| D5 | debug2 ②：「渲染意图」下拉硬裁成「相对比色 + 黑点补M」 | 项文案 `相对比色 + 黑点补偿（默认）` 墨迹 150px > 160 标准轨的 132px 文本区 | 项文案改 `相对比色+黑点补偿`，「（默认）」移入 R3 的行尾 10px 注释槽、随选择显示/隐藏（M7R25-3）。`output_page_build.cpp:281` |
| D6 | debug2 ③：三行开关不同列（8bit抖动/线性光居中、分文件夹靠左） | `make_flex_field` 给**首个**控件 stretch 1 → 单开关行的 Fixed 开关被居中到字段中段 | 控件一律贴字段轨起点、富余宽归行尾 stretch（M7R25-6）。`output_page_build.cpp:92` |
| D7 | debug2 ③/④：格式参数行互相重叠 + 「技术/无损」挤在一起 | 同 D2 的锁高陈值反向侧：建卡时参数表单还空 → 只钉 84px；等参数行建好后整卡被压到 84，28px 行被布局压到 6px 节奏后又被 `setFixedHeight(28)` 弹回 ⇒ 重叠 | 同 D2 修复（实时下限）；实测重叠簇 20+ 条全部消失 |
| D8 | debug2 ④：「＋ 添加格式…」旁杂散空盒 | 同 D7 的行重叠副产物（折叠头/行错位叠出的视觉碎片） | 同 D2 修复后消失（layout-audit ⑤ ghost 判据常驻看守） |
| D9 | debug1 ① 旁：搜索框挤成「···」 | 306px 工具行要装 4 个 28px 图标钮 + 160 标准档下拉 + 搜索框，搜索只剩 36px | 分组下拉改短档 88（mockup 工具行口径）+ 最长项改「按机型」（M7R25-4）。`mainwindow.cpp:786` |

真机核对（`--ui-smoke --shots .cache/ui-review`，2880×1800）：工具栏四钮同脸 + 搜索框完整、翻图钮 ‹ › 矢量图标、
磁贴全名「JPEG XL」、意图下拉完整 +「（默认）」注释、三开关同列、格式参数卡行序清晰无重叠、空态卡体紧凑无留白。

### 1.2 识图门禁（新常驻探针 `smoke_probe_layout_audit`）

`tests/ui_smoke.py` 头此前明写「**截图仅存档，不做像素比对**」—— 全链路确实**没有识图步骤**，
渲染面缺陷只能靠人眼（这正是用户两张截图能漏过 0.3.3/0.3.4 全部门禁的原因）。

新增常驻探针（`src/ui/smoke_probes.cpp`，`PP_BUILD_DEV` 门控），**六类几何 + 像素断言**：

| 判据 | 抓什么 | 实测抓到过 |
|---|---|---|
| ① `text-clip` | 下拉文本区墨迹顶到切断线（硬裁）/ 磁贴按 paint 口径主字被省略 | D5（渲染意图硬裁）、D4（JPEG XL 省略） |
| ② `overlap` | 两个叶子控件全局矩形相交 ≥8×8（裁到滚动视口内再比） | D7 行重叠簇（20+ 条）、GPS 卡与底栏误报（滚动裁剪修正后消） |
| ③ `blank-btn` / `ink-btn` | 纯图标钮无文案无图标；实拍内区墨迹 < 12px（图标挂了但画不出来） | D3（翻图两块白方，内区墨迹=0） |
| ④ `overflow` | 叶子控件越出父矩形 ≥3px | D9（`pp-group-mode` 越出 `pp-tools`） |
| ⑤ `ghost` | 无名·无文案·无图标的裸 QWidget/QFrame 空盒 | D8 类杂散控件 |
| ⑥ `card-blank` / `head-tall` | 卡体/卡内容层高于可见子件和 + 24；卡头子件 > 44 | D2（空态卡体留白） |

口径（与 `combo-consistency` 同源）：几何类只看**当前页可见**件、三页逐页各扫（隐藏件几何是布局跳过后的陈值）；
文案/图标类不看可见性（病灶藏在非当前页）；明暗两档各一轮；逐条命中以 `UI-SMOKE layout-audit: …` 打印（失败可定位）。
`tests/ui_smoke.py` 追加 `LAYOUT_AUDIT_LINE_PREFIXES` 齐备断言（防断言被静默跳过）——**只增**：
冻结末行 `UI-SMOKE OK shots=12 pages=3` 与既有前缀清单逐字未动。

---

## 2. 验收结论（命令 + 结果逐条取证）

| 门禁 | 命令 | 结果 |
|---|---|---|
| 格式 | `bash tools/ci-format.sh` | **violations=0**（clang-format 23.1.1 钉版，全源 0 diff） |
| 静态分析 | `python tools/env.py run -- python tools/ci-tidy.py build/release-dev` | **TIDY total=262 baseline=262 violations=0**（棘轮通过；中途新增 1 条 `performance-unnecessary-value-param` 已按纪律修掉，未重播种） |
| 单测 | `python tools/env.py run -- ctest --preset release` | **35/35 全绿** |
| 金样 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | **`SMOKE total=20 pass=20 fail=0`**（冻结行逐字） |
| 回归 | `python tools/env.py run -- python tools/regression.py` | **`regression: zero diff (baseline reproduced)`**（2448 行逐字节） |
| UI 冒烟（offscreen） | `python tools/env.py run -- python tests/ui_smoke.py build/release-dev` | **`UI-SMOKE layout-audit: checked=1027 bad=0`** + **`UI-SMOKE OK shots=12 pages=3`** + `UI-SMOKE pass`（冻结末行逐字） |
| UI 冒烟（真机） | `… photopipeline --ui-smoke --inputs tests/golden/base --shots .cache/ui-review` | `layout-audit: checked=1028 bad=0` + `UI-SMOKE OK shots=12 pages=3`（退出码 0） |

金样/夹具零改动；断言只增不减（新增 1 条齐备前缀，未放宽任何既有断言）；
`tile-zero-deform`（勾选零形变含位置）在锁高修复后一度 7/8，未放宽断言，改为**根治布局漂移**（M7R25-2 滚动条常驻）后回绿。

---

## 3. 测试 / 约束 / 门禁体系深查（用户第二问的正面回答）

### 3.1 「GUI 开发没有做测试吗？」——有，但全是**几何/语义**测试，没有**渲染面**测试

事实清单（逐条可在源码/文档核对）：

* `tests/ui_smoke.py` 头注释白纸黑字：「**截图仅存档，不做像素比对** —— 12 张 PNG 落 .cache/…，
  本脚本与 `--ui-smoke` 都不比对截图字节或像素差异」。**这是缺口的制度根源**。
* UI 侧回归通道 = `--ui-smoke` 内的 C++ 硬判（几何/文案/对象名/少量像素取证）+ 12 张截图人工留档。
  单测（ctest 35 项）只覆盖 core/纯函数，**UI 类不进 ctest**（项目明文口径）。
* 既有像素探针只在**非字形区域**取锚（offline-banner 内边距环带、icons-engine 墨迹包围盒、
  status-tones 色档、select-bar 强调条）——因为字形渲染跨平台/跨 DPI 不稳定，被明文回避。

⇒ 结论：**GUI 有测试，但"长得对不对"这件事当时没有任何机械判据**。用户看到的 7 处红框
（白钮、空图标、文本截断、行错位、空盒）全部落在这个盲区里。

### 3.2 「最终没有做 E2E 测试吗？」——有，但 E2E 的覆盖面停在**行为**，不含**视觉**

* `--ui-smoke` 是端到端走查：建窗 → 三页切换 → 勾选/搜索/分组/折叠 → 地图选点 → EXIF 编辑 →
  真实运行一轮（含取消/失败清单/0 字节补齐）→ 12 张截图。**行为面 E2E 是有的**（这也是为什么
  0.3.3「打不开」这类缺陷要另加 winzip 真机启动探针才拦住）。
* 但 E2E 的断言是"控件存在/几何对/文案对/退出码对"，**不含"渲染结果对"**。

⇒ 结论：E2E 有，视觉 E2E 没有。本次把视觉 E2E 补上（layout-audit），但仍以**几何 + 局部像素**
为判据，未上"整图比对"。

### 3.3 「没有识图步骤吗？」——没有，现在有了

* 修前：0 处识图。修后：`smoke_probe_layout_audit` 六类判据常驻（§1.2），每次 `--ui-smoke`
  （本地 ctest / winzip 门禁 / CI）都跑，命中即退出码 1。
* 阳性对照（探针有效性证据，同会话内逐条实测）：D5 硬裁被 ① 抓到、D3 白钮被 ③ 抓到（内区墨迹=0）、
  D9 搜索框被 ① 抓到、D7 行重叠簇被 ② 抓到 20+ 条、D2 留白被 ⑥ 抓到、D4 省略被 ① 磁贴公式抓到 ——
  **每一处用户红框都被探针独立复现过一次**（修复前跑出 bad>0，修复后 bad=0）。

### 3.4 约束面审查：红线守住了吗

| 红线 | 结论 |
|---|---|
| 冻结面 | **零改动**：`PP-FROZEN` 文件、`--version`、`SMOKE`/`UI-SMOKE` 冻结行、`SCHEMA.md` 逐字未动 |
| 断言只增不减 | 只新增 1 条齐备前缀；未放宽任何断言（`tile-zero-deform` 变红时选择根治布局漂移，不是删检查） |
| 金样/夹具 | 零改动 |
| 引擎零改动 | `src/codecs`/`src/decode`/`src/core` **零 diff**（本批全在 `src/ui` + `tests` + `docs`） |
| 版本/文案单源 | 版本号未动；文案改动两处均有裁定登记（M7R25-3/4） |
| 密钥 | 本批不涉 |

### 3.5 已知系统性问题（本批未根治，如实登记）

1. **`tests/ui_smoke.py` 的口径注释仍写"截图仅存档，不做像素比对"** —— 这句现在只对了"整图"一半：
   局部像素（图标墨迹/文本裁切）已有判据，**整图基线比对仍未做**（见 §5 未决 ①）。
2. **offscreen 与真机的字形度量不一致**：offscreen 的 CJK 以 tofu 满宽字形渲染（比真机墨迹宽约 10%），
   且点值字号（§4.2 的 11.5px 档走 `setPointSizeF`）在"控件度量"与"实绘"间解析差约 1.5×
   （实测同一文案 156 / 102）。**任何基于文本宽度的断言在 offscreen 门禁侧都会假阳** ——
   本批的 ① 判据最终收敛为"只判下拉 + 墨迹是否顶到切断线"（不做字号换算），
   按钮/chip/分段钮自绘带描边，墨迹法分不清"描边"与"被裁的字"，故不判（宽度多随内容，结构上不裁）。
3. **图标钮空白这类缺陷没有"按钮文案存在性"以外的语义断言**（修前 `icon-buttons` 探针用
   硬编码 8 钮清单，不含翻图钮）—— 本次由 ③ 的"任意纯图标钮内区必须有墨"泛化覆盖。

---

## 4. 偏差与追认

| 项 | 说明 | 追认 |
|---|---|---|
| 文案改动 2 处 | 渲染意图项（M7R25-3）、分组项「按相机型号」→「按机型」（M7R25-4） | 均入 `m7-adjudications.md`；节头「相机型号」与 `pp::GroupKey` 语义不变 |
| 滚动条常驻 | 三页页级滚动条 `ScrollBarAlwaysOn`（M7R25-2） | 视觉小改（滚动条常驻），换来内容宽恒定 + `tile-zero-deform` 保绿 + 消除整卡回流 |
| `lock_card_size` 语义 | 从"钉一次性数值"改"实时 sizeHint 当下限"（M7R25-1） | 语义更严（下限随内容），无行为放宽 |
| `make_flex_field` 值传参改 const 引用 | 顺手修 clang-tidy 新增告警（不重播种） | 与本批同提交 |
| layout-audit 只判下拉的 text-clip | 见 §3.5-2 | 如实登记为**已知覆盖边界**，不是"没有问题" |

---

## 5. 未覆盖 / 未决项（如实）

1. **整图基线比对未做**：12 张截图仍只存档。上"感知哈希/逐像素比对"需要先解决跨平台字体渲染差异
   （同 §3.5-2），否则 CI 会红在字体上。**建议下一步**：非字形区域的分区基线（卡框/栅格/图标位）+
   字形区域只查"墨迹边界"（本批 ① 的思路推广）。
2. **text-clip 只覆盖下拉**：按钮/chip/分段钮/标签的文本裁切目前无机械判据（宽度多随内容，
   结构上不裁；但"内容自适应件被布局压窄"这一类仍可能漏）。**建议**：把这些件纳入
   "内容宽 = sizeHint 时判不裁、否则判裁"的分档判据（需先解决字号换算，见 §3.5-2）。
3. **弹窗（设置/EXIF 编辑器/预设）不在 layout-audit 扫描面内**（只扫主窗口三页）；弹窗几何归既有
   `dialog-close` / `settings-dialog` 探针。
4. **`mainwindow.cpp` 上帝对象**（1.1 万行）与 UI 建造器复制粘贴仍在（结构性债，非本批引入）。
5. `docs/deep-review-0.3.2.md` 的 2 个 P0（数据破坏）+ 20 P1 **仍未修**（本批只动 GUI 面；
   那两条 P0 是引擎写路径守卫，属另一批）。

---

## 6. 复现命令与证据路径

```bash
# 门禁全套
bash tools/ci-format.sh
python tools/env.py run -- python tools/ci-tidy.py build/release-dev
python tools/env.py run -- ctest --preset release
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
python tools/env.py run -- python tools/regression.py
python tools/env.py run -- python tests/ui_smoke.py build/release-dev        # 含 layout-audit

# 真机截图（识图核对；不进 CI）
python tools/env.py run -- build/release-dev/photopipeline --ui-smoke \
    --inputs tests/golden/base --shots .cache/ui-review
```

证据路径：`build/m7r24-gui/v1_toolbar.png`（D1/D9）、`v1_navbar.png`（D3）、`v1_info_empty2.png`（D2）、
`v2_fmt.png`（D4/D5）、`v2_switch.png`（D6）、`v2_param.png`（D7/D8）—— 修后真机实拍；
`debug/debug1.png` / `debug/debug2.png` = 用户报障原图（保留）。
取证脚本（`shot.py`/`pwshot.py` 等）留在 `build/m7r24-gui/`（gitignored，不入库）。

---

---

## 8. 发行终态（0.3.5.alpha1，2026-10-14）

**Release `v0.3.5.alpha1` 已发布**（target=main，created 2026-10-14T01:46:06Z）；CI `release`
运行（sha `61eade0`）七 job 全绿（appimage / probe-appimage / winzip-build / winzip-build-dev /
winzip-pack / probe-winzip / publish），`main-full`（main @`61eade0`）同步全绿。

### 8.1 发行资产核对（GitHub Release 四件，SHA256SUMS.txt 原文）

| 资产 | 体积 | sha256 |
|---|---|---|
| `PhotoPipeline-0.3.5.alpha1-x86_64.AppImage` | 52054520 | `23d8121c32454de845e73568de610235d7e9e569d290fc693969f491d3a36b7c` |
| `PhotoPipeline-0.3.5.alpha1-win64.zip` | 33733574 | `80d76c21a935eab93d62e7a063e7dcad757d065f4bd73a6b2d4cdb3818231305` |
| `PhotoPipeline-0.3.5.alpha1-deps.txt` | 2439 | `e8c80b3d61a4e0d41cde50eb3c04577688d7428cc189129b3331cf221407854d` |
| `PhotoPipeline-0.3.5.alpha1-SHA256SUMS.txt` | 314 | — |

资产名与 release 的 tag↔版本断言（`VER=${TAG#v}`）逐字一致 ✓。

### 8.2 发行链双归因（本批的两条 CI 红灯，如实记录）

| 红灯 | 根因 | 处置 |
|---|---|---|
| release run #16（sha `b55ef09`）appimage job 失败 | **R-122 版本一致性**：`vcpkg.json` version 漏改（0.3.4）⇒「依赖单源一致性断言」红 | `vcpkg.json` → 0.3.5；本地补跑 `ci-versions-check.py`（本机门禁清单此前漏了这一道） |
| winzip 烟测 E-1 拦 `PhotoPipeline 0.3.5.alpha1` | `--version` **冻结行正则**只认三段数字 | 三处同锚机械换锚（winzip E-1 / release.yml / main-full.yml）为允许可选标签段；语义不变，登记裁定 **M7R25-7** |

两处都在**资产发布之前**被拦住（publish 未跑 ⇒ 无资产挂载）；tag `v0.3.5.alpha1` 由 `b55ef09`
前移到修复后的 `61eade0`（发行纪律：「tag 未发布资产前可移动，发布后不可移动」），
并 force-push 覆盖（旧 run #16 保留为失败记录）。

### 8.3 本地打包复核（与 CI 同判据）

`tools/make_winzip.py` 六门禁全绿：A 闭包（68 PE / 105 导入名）｜ B 来源（Qt DLL sha256 逐一一致）｜
C 结构（qwindows/qoffscreen/imageformats/tls 齐备）｜ D 许可（35 copyright ≥30）｜
E-1 `PhotoPipeline 0.3.5.alpha1`（exit 0）｜ **E-3 `WIN-LAUNCH OK windows=6 visible=1`**（真实
qwindows 平台）。本地产物 `dist/PhotoPipeline-0.3.5.alpha1-win64.zip` 34802808 bytes，
sha256 `bf4418885bfd50dbedd7c800d10f2c6e3d8d328738efdb6c31a35d4885d7b2ae` —— 与 CI 产物不同
（不同构建机，预期），CI 侧以 `SHA256SUMS.txt` 为准。

### 8.4 已知口径差（如实）

* **GitHub Release 未勾"Pre-release"**：`gh release create` 不带 `--prerelease`，页面上是普通
  Release（版本号带 `.alpha1` 才表达预发布）。补法 = release.yml 的 ③ 发布步加一个 flag
  （属 CI 面改动，未在本批动）；或人工在 Release 页勾选。

## 7. 一句话总结

7 处红框全部根治并真机核对通过；缺的"识图步骤"已机械化成常驻门禁（六类几何/像素断言，
每一处红框都有阳性对照）；冻结面/金样/断言口径零放宽；剩下的是"整图基线"与"文本裁切全类覆盖"
两件需要先解决字体度量一致性才能做的事，已如实登记为未决。
