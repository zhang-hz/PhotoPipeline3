#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — M1b-U10 offscreen UI smoke（ctest 名 ui_smoke，由 CMake 在 PP_BUILD_DEV 下注册）

用法: tests/ui_smoke.py [BUILD_DIR]      # 默认 build/release-dev
退出码: 0 = 冻结断言全过（末行 `UI-SMOKE OK shots=12 pages=3`，M4-W3-T14 起冻结 12 张）；
        2 = 用法/二进制缺失；其余 = photopipeline --ui-smoke 的退出码透传。

取证口径（M7-WP3 / R-044，deep-review-0.3.2；如实表述，勿再传"截图比对"）：
  * **截图仅存档，不做像素比对** —— 12 张 PNG 落 .cache/ui-smoke-ci（CI 存档）/ .cache/ui-review
    （人工审查），本脚本与 `--ui-smoke` 都**不**比对截图字节或像素差异；回归判据是下一行的
    C++ 硬判 + 冻结末行。
  * **硬判在 C++ 探针侧**（mainwindow.cpp 的 smoke_probe_* 段）：逐条 meta_expect/expect 断言
    （几何/文案/对象名/像素取证），失败经 smoke_fail 通道 ⇒ `--ui-smoke` 退出码 1；本脚本只负责
    ① 校验冻结末行 `UI-SMOKE OK shots=12 pages=3`、② 校验各 tag 自检行齐备（防断言被静默跳过）。
  * 字体敏感的像素断言（ClearType 彩边 / dpr 缩放）一律给**容差**或换锚到非字形区域（先例：
    offline-banner 的"内边距环带 + 占比"双判据、icons-engine 的同坐标系回读）——字体/渲染差异
    不得判红。像素探针本体在 C++ 侧（非本文件面），改动须经 UI 面任务。

截图目录（M2-T21c 隔离裁定）：
  * CI/ctest（本脚本默认）固定写 .cache/ui-smoke-ci —— 本脚本总是以 QT_QPA_PLATFORM=
    offscreen 运行，产物是 Fusion/offscreen 口径，**不是**真实平台渲染。
  * 人工审查集固定用 .cache/ui-review（真实平台渲染，不带 QT_QPA_PLATFORM=offscreen，
    由 `photopipeline --ui-smoke --shots .cache/ui-review` 显式产出）。
  二者必须分开：否则任何人跑一次 ctest 就会用 offscreen 图覆盖人工审查基线
  （M2-T21b 复核期间实测发生过：8/8 字节与 sha256 全变、强调色由 Yaru 橙变 Fusion 蓝）。

M3-W3（裁定 D3：Python 单实现，本文件替代 tests/ui_smoke.sh）:
  * BIN 按 name / name + '.exe' 双探测（Windows 产物带 .exe）
  * `mktemp -t` → tempfile；tee 用 Python 边读边写（终端透传 + 临时日志）
  * 冻结行为不变：QT_QPA_PLATFORM=offscreen、`^UI-SMOKE OK shots=12 pages=3$` 断言、
    `UI-SMOKE pass` 成功行与两条缺失提示逐字

M4-W2-T11 扩展段（§6.2 + §3.6 filelistmodel.h 行）:
  * 追加"T11 自检行齐备"断言：勾选/搜索正交/分组节头/分类打标/classes.json 往返/圈选/只读
    这些自动化部分由 `photopipeline --ui-smoke` 在进程内断言（smoke_fail → 退出码 1）并逐行
    以 `UI-SMOKE <tag>:` 打印；本脚本额外要求这些**行存在**（防止断言被静默跳过/裁剪）。
    清单 = 前缀匹配（行内容由 C++ 侧给出，脚本只验 tag 存在）。

M4-W3-T14（运行页）:
  * 冻结末行**升级** `shots=8` → **`shots=12`**（逐字；新增 4 张运行页图：03c-run-idle /
    03d-run-rows / 03e-run-cancel / 07-run-light，见 mainwindow.cpp 的 kSmokeShots）。
  * 追加运行页自检行齐备断言（逐输出行数/结束态定稿/真实vs斜纹像素取证/锁定态/总览读数/
    取消态/空闲态/浅色主题/日志尾/底栏运行读数）。

M4-W3-T15（设置对话框追加项）:
  * 追加 `UI-SMOKE settings-dialog:` 行齐备断言（§9.3 三项：交错启动 ms / 线程预算 /
    分文件夹默认结构 → AppSettings 读回；截图仍为 12 张、冻结末行不变）。

M5-T4（W-G2 壳层）:
  * 追加 `UI-SMOKE shell-geom:` 行齐备断言（C7 几何探针：顶栏 44 / 状态栏 30 / 导航段 92×30 /
    caption 命中 46×32 / 栏宽 330·587 轴 / CTA 22 / 按钮 26 / 品牌图标 20 / 窗口内边距 10；
    值表驱动，行内 `checked=N bad=M` 由 C++ 侧给出）与 `UI-SMOKE status-tones:`（§8 状态四档
    QSS 规则 + 点/词同档 + 空态不得为红）。截图仍为 12 张、冻结末行不变。

M5-T5（W-G2 壳层：文件行/工具行/空态/分类面板）:
  * 追加 `UI-SMOKE filelist-geom:` 行齐备断言（C7 几何探针：行高 58 / 缩略图 46 / 勾选框 13 /
    工具行按钮 28×26 / 信息行字阶 10 / 工具行常驻 + 无选择时移除钮隐藏 / 信息行不截断（无省略号）/
    空态投放区 / 选中行左缘强调条像素取证；值表驱动，行内 `checked=N bad=M` 由 C++ 侧给出）。
    截图仍为 12 张、冻结末行不变。

M5-T6（W-G2 壳层：预览画布 缩放/拖动/空态/角标）:
  * 追加 `UI-SMOKE preview-geom:` 行齐备断言（C7 几何探针：画布底 #16181d（明暗两档同值实拍）/
    缩放条各钮 28×28（图标钮、非 checkbox）/ 翻图钮 28×28 / 角标空态隐藏 + 半透明底与描边 /
    左下提示逐字 / 空态矢量图标 + 引导语 + chip 同档禁用（修 1-16/1-17/1-19/1-21/1-22）/
    照片衬垫与画布底可区分（修 3-21）/ 小图放大档最近邻（修 3-20）；值表驱动，行内
    `checked=N bad=M` 由 C++ 侧给出）。截图仍为 12 张、冻结末行不变。

M5-T8（W-G3 页：输出页 §7.2 四卡组）:
  * 追加 `UI-SMOKE output-geom:` 行齐备断言（C7 几何探针：8 格式磁贴 2×4 + 对勾位零形变（选中不改宽、
    不改 sizeHint）/ 未选中 ≠ 禁用（修 2-05）/ 六行一义 pitch 28（位深·技术·无损·编码努力·视觉距离·
    高级参数；行内只有一个标签）/ 标签轨 80 + 组内外同轴（修 2-32）/ spin 只取 88·160 两档（修 2-33）/
    范围入 10px 注释（修 2-18）/ 魔法值「自动」（修 2-37）/ 高级参数折叠行默认折叠 + 子分组 + 搜索行
    （修 2-19/2-31/2-39/2-40/2-41）/ 互斥置灰（仅元数据：磁贴区 + 格式参数卡禁用，模式分段仍可用）/
    模板示例行单行且不再混用 <根>（修 2-14/2-15）/ 卡头计数 = 中性胶囊（修 2-07）/ 按钮 26（A7）/
    页签 = 格式名 · 位深（修 2-21）/ 预设 placeholder + 禁用原因（修 2-24/2-25）/ 根目录 placeholder
    （修 2-10）/ 色彩目标 tooltip（修 2-16）/ 模板 tooltip（修 2-13）；值表驱动，行内 `checked=N bad=M`
    由 C++ 侧给出）。既有 T13/T15 相关前缀全保留。截图仍为 12 张、冻结末行不变。

M5-T7（W-G3 页：元数据页 §7.1 四卡组）:
  * 追加 `UI-SMOKE meta-geom:` 行齐备断言（C7 几何探针：卡内行 pitch 28 / 标签轨 80 /
    日期时间 spin 88×24 / 生效行 acc 值（像素取证：修改值 = 强调色、原值 ≠ 强调色）/
    空态收敛（未选择文件 → 一行说明，kv 行收起）/ 双列第二标签起点（A2：160 档 =
    second_label_x(160)、kv 行第二标签同 x 且 = 首值轨右缘 + 24）/ 卡头右置一律中性胶囊
    （1-24/1-25）/ GPS 状态 = 独立徽标「已有 GPS」（修 1-33）/ 地图操作行在地图上方（修 1-37）/
    批量规则折叠行默认折叠 / 按钮 26（A7）/ 地图离线说明中性灰 + 括号进 tooltip（修 1-38/1-39）；
    值表驱动，行内 `checked=N bad=M` 由 C++ 侧给出）。既有 meta-* 探针行（meta-baseline /
    meta-effective / meta-timezone / meta-gps / meta-strip / meta-gps-clear / meta-multi /
    meta-editor / meta-info / meta-geometry / meta-follow-*）全保留。截图仍为 12 张、冻结末行不变。

M5-T10（W-G3：设置弹窗 §7.4 重做）:
  * 追加 `UI-SMOKE settings-geom:` 行齐备断言（C7 几何探针：660 宽 / 标题栏 40 / 页签条 34（5 页签，
    序逐字 = A21）/ 底栏 48 / 行 pitch 28 / 字段 160 + 单位槽 16 + 范围注释 10px / 确定实心居右 +
    恢复默认居左 / 焦点环 accent 1px 跟随 + outline:none / 主题三档段几何 + 段组贴字段轨 /
    无语言假控件 / 修 4-08 自动档 = 下拉项 / 修 4-15 alpha 端值映射；值表驱动，行内
    `checked=N bad=M` 由 C++ 侧给出）。既有 `UI-SMOKE settings-dialog:` 行（交错启动 ms / 线程预算 /
    分文件夹默认三项读回）全保留（线程预算的控件类型按修 4-08 换锚为下拉，断言本体不变；
    M5-T10 机械换锚登记在任务报告）。截图仍为 12 张、冻结末行不变。

M5-T11（W-G4 首项：缺陷根治与壳层收尾；裁定 A27/A28/A13/A16/A17/A19/A10）:
  * 追加 `UI-SMOKE settings-theme:` 行齐备断言（A28 主题档持久化：三档写回
    system/light/dark 逐档一致 + 点「亮色」段的即时生效链 + AppSettings 经真实存储层落盘往返
    + 启动读取点 apply_startup_theme()（持久化档 → 落地档）+ 主窗构造路径端到端；
    PP_UI_THEME 强制档在场时启动链按设计让位，该行 checked 相应少 3 项 = 6）。
  * A27（分段控件段高 0 根治）不新增行名：断言落在既有 `meta-geom` / `output-geom` /
    `settings-geom` 行内（段高 = kSegH − 2 且可见可选），checked 计数随之增加；调用点
    临时补丁已撤销，段高由 formkit::Segment 自持。
  * A16（空态文案去重）同样落在既有 `filelist-geom` 行内：文件列表空态文案 = 「拖放照片到此处」
    且 ≠ 预览空态逐字「将照片拖到此处，或点击 ＋ 添加」。

M5-T12（W-G4：峰值内存接线 + 分类网格 + 无障碍 + 地图收口；裁定 A29/A15/A18/A30）:
  * 追加 `UI-SMOKE classify-grid:` 行齐备断言（A15：分类面板 = 2 列 kv-grid —— 相邻单元同 y、
    两列等宽、列距 = form_col_gap、行 pitch 28、「＋ 新建分类…」占网格下一个空位；值表驱动，
    行内 `bad=N` 由 C++ 侧给出）。
  * A29（RunSummary::peak_bytes 加性解冻）不新增行名：断言落在既有 `run-geom` 行内
    （`memory-peak`：峰值内存 kv = 真实读数、量级换档非「—」），另在实跑结束态新增
    `UI-SMOKE run-memory:` 行（真实 Scheduler → PixelBudget 的读数非「—」）。
  * A18（ShellNav 无障碍）不新增行名：断言落在既有 `shell-geom` 段内的 `UI-SMOKE nav-a11y:`
    行（可访问接口 = PageTabList/PageTab（段）/选中态、accessibleName、Tab 可达、自绘焦点环
    accent 1px（聚焦前后实拍差异像素取证）、段间 Ctrl+←/→（本应用里可达的一路）视觉序导航
    + 预览翻图热键不被抢、骨架 QSS outline:none）。
  * A30（地图 banner 中性化）落在既有 `meta-geom` 行的 `offline-banner` 内：调色板不得残留
    警示黄 #fff1b8/#6b4a00 + banner 实拍像素不得出现警示黄（阈值 40）+ 文案去括号（进 tooltip）。
  * 截图仍为 12 张、冻结末行不变。

M5-T9（W-G3 页：运行页 §7.3 三卡组）:
  * 追加 `UI-SMOKE run-geom:` 行齐备断言（C7 几何/语义值表：总览 kv 行 pitch 28 + 标签轨 80 /
    表行高 28（表头 24）/ 进度条高 6（总览合成条 + 分组条 + 逐输出条）/ 日志尾等宽字族（§7.3）+
    4 行真实尾部 + 「查看完整日志…」链 + 有日志才有日志名（修 3-14）/ 状态胶囊单形态（完成=绿胶囊
    「已完成」修 3-12、0 字节=琥珀「0 字节」且条 warn 禁绿 修 3-10、失败=红胶囊 + 「└ 原因：…」行）/
    指标单源（无「已用时」第二出处、摘要句无指标 修 3-03/3-04）/ 实现性文案出界面（任务 hint
    「实心=已写盘 · 斜纹=合成中」+ 术语入 tooltip 修 3-05）/ 操作行三级动作（链/危险钮/次钮）+
    按钮高 26（修 3-08 + A7）/ 计数单源（逐文件不再重复「N 个输出」修 3-13）；值表驱动，行内
    `checked=N bad=M` 由 C++ 侧给出）。既有 T14 运行页探针前缀（03-run / run-rows / run-bottom /
    run-rows-final / lock-visual / run-log-tail / run-idle / run-probe-rows / run-bars /
    run-overview / run-cancel / run-light）全保留（M5-T9 机械换锚逐条登记在任务报告）。
    截图仍为 12 张、冻结末行不变。

M5-T13（W-G4 验收：v6 §12 十七条逐条核验）:
  * 追加 `UI-SMOKE theme-equiv+nowrap:` 行齐备断言（M5-T13 只增探针 `smoke_probe_verification()`：
    §12-13 亮暗布局等价 —— 全部 pp-* 控件 (objectName, 窗口坐标几何, 可见性) 在
    Dark↔Light 两次落地后逐项一致（行内 `equiv-pp-widgets=N` 为比对数，证明取证非空）；
    §12-14 卡头 hint —— 全部 *-hint 标签禁折行、单行高、文本放得下或以省略号结尾
    （行内 `hints=M`）；§12-8 数值右对齐+tabular —— spinbox 与运行页数值列（大小·速度·耗时）
    右对齐且 tnum、kv 值标签（ppFk=value）tnum（mockup .kv-v 不设 text-align；行内 `nums=K`）。
    三条 §12 条目此前无独立探针；既有探针/冻结行零改动。
  * 真实平台诊断行（不新增前缀清单；失败定位用）：`icons-engine-space`（引擎路径 vs 直接
    绘制的像素空间/dpr）、`filelist-selprobe`/`filelist-selgrid`（选中条取样邻域）、
    `offline-banner-px`（banner 最近警示黄像素位置/颜色）。

M5-T14（W-G4 收尾：T13 FAIL 清单逐条修复 + 全量回归复验）:
  * **前缀清单零改动**（行名/冻结末行都不变）；三条 T13 FAIL 的修法与断言补强：
    1) §12-5 图标：`icons-engine` 探针口径修正 —— 引擎路径从 `QIcon::pixmap(box)` 的**物理**
       像素回读（dpr≠1 平台 = 24×24）改为 `QIcon::paint()` 渲到 12×12/dpr=1 的**同一坐标系**
       目标（按钮经 QStyle 实际走的路径）；断言本体（逐边 ≤1px + 居中）不动。诊断行
       `icons-engine-space` 追加 `engine-raw=`（旧回读原样，供 dpr 归因）。
    2) §12-12/§11 文件行选中面：真实平台焦点框（Fluent 样式给当前行画的「1px 白 + 1px 黑」
       指示框）盖住左缘 2px accent 条的**实现面缺陷**根治 —— QSS `outline: none` 对该绘制
       路径天然无效（`QTreeView::drawRow` 的焦点框调用不带 widget，QTBUG-4281），实收口 =
       壳层 `FileViewFocusFrameStyle`（按 objectName 识别文件列表视图的代理样式）。断言补强
       （只增）：2px 条带**双点**像素取证（x=0.5 与 1.5 逻辑像素都须为 accent）+ 骨架 QSS
       `focus-frame-off` 结构位；`filelist-geom` 行末尾新增 `|%s focus-frame-off=%d` 字段，
       `filelist-selprobe` 追加 `bar2=` 与 `focus-frame-suppress[opt/wid/dev/miss]` 诊断。
    3) §12-9 地图 banner：`offline-banner` 像素判据换锚（原「全幅最近警示黄距离 > 40」为点
       代理，真实平台亮色下被文字 ClearType 彩边 #f7e7c9（距 21.3）误报）→ 双判据
       「内边距环带（避开文字行）最近距离 > 40」+「全幅近警示黄像素计数占比 < 5%」
       （实测亮色 1.19%、暗色 0.00%）；`offline-banner-px` 诊断行追加
       `ring-dist/near/比率/dpr/text-box`，`meta-geom` 失败详情补齐环带/占比取证。
  * 截图仍为 12 张、冻结末行不变。

M5-T13（W-R 视觉收尾修正：裁定 A39 ①–⑥ + A40 核对）:
  * **前缀清单零改动**（不新增行名、冻结末行不变）——六项各自落在既有行内，逐条登记换锚：
    ① 线程预算措辞「自适应（N 核）」→「自动（N 核）」（run-overview 换锚 + 页内「自适应」残留
       扫描；settings-geom 追加同词核对：`thread_budget_word/note`（自动 / 0 = 自动）+ 旧措辞否定）。
    ② 「取消剩余」红描边钮 → **红文字链**（run-geom 的 `action-tiers` 换锚到 `ppField=link-danger`
       + 页 QSS 规则取 err 令牌 + **旧 danger 档必须退役**（否定断言）+ 三条文字链不挂图标）。
    ③ 排队 pill 收「排队」（mockup run.html 逐字；「交错启动」语义移进 pill tooltip）——
       run-probe-rows 换锚（f2 行/组行文案 + `f2tip` 带步距 150 ms 与「交错启动」）。
    ④ 缩放条「适应 / 1:1」恢复**文字形态**（§8 组件清单逐字）——preview-controls 换锚到钮文案
       + tooltip（不再锚图标名），preview-geom 的 `zoom-group` 换锚为「高 28 + 宽随文本（放得下）
       + icon() 空」，−/＋ 仍钉 28×28 图标钮（icon() 非空）。
    ⑤ 状态栏选中数不被警告吞掉 —— 新增 `UI-SMOKE status-selection:` 行（两态在位取证后还原：
       根目录未设 =「已选 N / M · 未设置输出根目录」琥珀；根目录就位 =「已选 N / M · 就绪」）。
    ⑥ 隐私卡「圆形伪影」定性 = **实现缺陷**（`build_switches_card` 的卡体未进卡布局 → 卡体停在
       默认 100×30、三行压成 6px pitch 盖住卡题）→ 已根治；`meta-geom` 追加 `privacy-card-body`
       （卡高 ≥ 卡头 + 3 行 × 28 / 行 pitch 28 / 首行不压卡头）。
  * `status-selection` 为**新增的自检/诊断行**（不在前缀清单内，与 run-bars-hist / icons-engine-space
    同类）：判据本身在探针里逐条 meta_expect/expect 硬判，失败经 smoke_fail 通道报错。

M6-T9（R3 选点闭环；docs/m6-map-upgrade.md §4.4/§7-T9）:
  * 追加 `UI-SMOKE meta-loop:` 行齐备断言（`smoke_probe_meta` 的 (j) 段，**全程离线**）：搜索成功
    路径的两步输出由探针等价喂入（图钉面 `set_result_pins` + 列表面 `search_results_ready`）→
    点图钉（视口中心命中）→ 主钉 + 规则框回填 = **WGS-84**（amap/GCJ-02 基准面下断言回填值逐位
    等于结果点 WGS-84 且 ≠ GCJ-02 偏移值；选点值 = WGS-84 的机械断言）→ 列表当前行跟随 +
    取点文案（已选结果：<名>，与点钉同句）→ 地图微调（point_selected 回填）→ 「地图选点」取
    拖拽后的视口中心 → 逆地理离线分类文案（不谎报地址）→ 换地图源清列表 + 清结果钉（点击旧钉
    位必须落回取点路径 = 不留"点不动"的旧钉）。行内 `checked=N bad=M` 由 C++ 侧给出；端点
    （真实搜索/逆地理地址）由 M6-T15 验收的联网证据承担。
  * 既有前缀清单与冻结末行零改动（截图仍 12 张）。

M6-T12（W-M5 体验件；§5.3/§7-T12）:
  * 不新增行名：断言落在既有 `UI-SMOKE meta-geom:` 行内（`smoke_probe_meta` 的 (i-9) 段）——
    「－/＋」缩放钮（24×24 矢量图标钮 + tooltip/accessibleName + `ppIcon` 键）· 缩放**真的改档**
    （同中心下 grab 逐像素变化、±1 往返复原）· 光标坐标读数（`cursor_moved` → 覆盖层 chip 文本
    = 信号值 6 位小数 + 坐标行 tooltip 同读数）· 比例尺（mapwidget 自绘：左下白底 α190 段的
    像素取证 —— 底部左侧 96×26 内近白像素计数）。另加一条 `UI-SMOKE map-hud:` 诊断行
    （钮几何 / 读数文本 / 比例尺近白计数，只报数）。既有前缀清单零改动、冻结末行不变。

M6-T13（W-M5 R5 大地图；§7-T13）:
  * 追加 `M6T13_LINE_PREFIXES` 一行齐备断言（`smoke_probe_meta` 的 (k) 段，**全程离线**）：
    覆盖层「展开」钮（24×24 矢量图标钮 + tooltip/accessibleName + `ppIcon`=zoom-fit + 右上角位）
    → 点按进模态「地图」对话框（标题「地图」/ 底栏关闭 / caption ✕ 齐备 + `isModal()`）→
    对话框内是**同一只** MapWidget（指针同源 = 重挂载，图层/图钉/视口随控件走、零同步；地图区
    ≥640×480 + 实拍非空：左上 96×26 近白像素 ≥ 20 = 覆盖层白底 chip/钮随控件进对话框）→
    底栏「关闭」（真实用户路径）→ 挂回卡片行（固定高 136、几何/落点
    逐位复原、`pp-meta-map` objectName 与可视性不变）→ 二次展开（可重复 + 尺寸记忆 = 上次尺寸；
    视图随对话框尺寸）。探针在 exec() 的嵌套循环内取证并关窗（预置 0ms 定时器 + 兜底 reject）；
    行内 `checked=N bad=M` 由 C++ 侧给出。既有前缀清单与冻结末行零改动（截图仍 12 张）。

M6-T16（全量回归收口；T15 偏差裁定）:
  * **前缀清单零改动**（不新增行名、冻结末行不变）——`meta-map-expand` 的
    `dialog-state-kept` 判据修正：grab() 的实拍图是**设备像素**（= 逻辑 × dpr），
    T13 原判据拿设备尺寸比逻辑尺寸（`shot.size() == inside->size()`），dpr≠1 平台
    （200% 缩放，T15 真机复跑实测 dpr=2.0）恒假 ⇒ 误报 `painted=0`。现按图的
    devicePixelRatio 换算两侧（dpr=1 时与旧判据逐位等价），ROI 的 96×26 保持
    逻辑像素口径；失败详情追加 `shot/dpr/near` 诊断字段。**探针修正、非产品缺陷**。

GUI-R2（docs/gui-review-0.3.0-r2.md 可视化排查回归）:
  * 追加 `GUI_R2_LINE_PREFIXES` 四行齐备断言（`smoke_probe_gui_r2()`，只增）：
    `combo-consistency`（全界面 QComboBox 样式钩子 / 高 24 / **实拍非空像素**——钉问题 1「亮色下拉
    整只不绘制」与问题 5「无公用构造器」）· `format-card-align`（格式参数页签带 ↔「＋ 添加格式…」
    corner widget 几何同带——钉问题 2「格式卡片错位」）· `dialog-close`（设置/元数据编辑器/预设
    弹窗 caption ✕ 点击后必须真关窗——钉问题 3）· `run-guard`（「开始运行」可点性与闸门同源 +
    点击不弹模态 + 运行期禁用 + 两轮重入——钉问题 4「点运行概率性错误」）。
  * 判据硬判（smoke_fail）；行内 `checked=N bad=M` 与取证值由 C++ 侧给出。既有前缀清单零改动、
    冻结末行不变（截图仍 12 张）。
  * **本节为缺陷回归**：0.3.0 现状会红 —— 每条红即报告里对应问题的活证据。

M7-R2.2-G1（G1 探针总集与 Linux ui_smoke 定点修复）:
  * 追加 `G1_LINE_PREFIXES` 十一行齐备断言（只增；冻结末行/既有清单零改动）：
    wave-3/4 常驻探针行收编进回归通道（rtl-mirror / check-all / context-menu / run-reason /
    folds / intake / classify-filter / edit-target / settings-tabs-light —— 此前只在 C++
    smoke_fail 硬判、python 侧无「行齐备」防线 ⇒ 断言被静默跳过不可见；即「context-menu /
    check-all 补强」与 R-152/153 回归通道的齐备性补强），并随本批新增
    `format-roundtrip`（R-189/192 format_selection_model 往返）与 `rtl-deep`
    （R-218 深水区：QLabel 数值列右对齐 RTL 随向定论）两行。

M7-R2.4-G2（布局审计 · R-识图1 —— 识图步骤的机械化落点）:
  * 起因 = debug/debug1.png + debug/debug2.png（2026-10-13，红框 7 处：图标钮白方 / 卡头大片
    空白 / 翻图钮无图标 / "JPEG XL"→"JPEG…" / 下拉裁半字 / 开关列错位 / 行重叠 + 杂散空盒）。
    本文件头此前明写「**截图仅存档，不做像素比对**」—— 也就是说全链路没有任何『识图』步骤，
    渲染面缺陷只能靠人眼。本批把识图固化成常驻探针 `smoke_probe_layout_audit()`
    （smoke_probes.cpp）：六类几何 + 像素断言（text-clip / overlap / blank-btn+ink-btn /
    overflow / ghost / card-blank），任一命中 smoke_fail ⇒ 退出码 1。
  * 追加 `LAYOUT_AUDIT_LINE_PREFIXES` 一行齐备断言（只增；冻结末行 `UI-SMOKE OK shots=12
    pages=3` 与既有清单零改动）。12 张截图仍按原口径人工留档（不改"截图不比对"的既定取证
    口径），但从此渲染面有机器看守。

M8-C13（评分证据面 · SSIMULACRA 2）→ M8-W2（需求3 改列 + 需求2 勾选框换位）:
  * `RUN_SCORE_LINE_PREFIX` 一行齐备断言保留（只增；冻结末行与既有清单零改动）：
    `UI-SMOKE run-score:` 由 `smoke_probes.cpp` 的 `smoke_probe_run_geometry()` 末尾 (m) 段给出
    —— 需求3 起「评分」= 任务表**一列**（C12 隐藏子行退役）：Off/仅元数据留空；Ok 显示两位
    小数 + 四档语义色（≥95/≥90/≥80/<80）；Unavailable/Failed 显示 `—` + tooltip 原因；
    列对齐（组行/输出行/表头同 x 同宽）由 C++ 探针断言。
  * 本脚本除行齐备外，另做**文本/取值断言**（防 C++ 断言被静默跳过；只增不放宽）：
    `bad=0`、`off_blank=1`、`ok_row=1`、`col_align=1`、`header=1`、`tiers=4`、
    `group_dash=1`、`ok_text="<两位小数>"`（正则）、`unavail_text="—"`。
  * 需求2：追加 `PARAM_ENABLE_GEOM_LINE_PREFIX` 行齐备 + 取值断言（`smoke_probes.cpp` 的
    `probe_m8_params()` ⑨ 段）：`bad=0`、`rows>=8`、`tail/label/editor/order == rows`
    （勾选框在行尾 + 右端 x 严格对齐 + 标签轨/数值列归位 + 行内顺序）、
    `status-clip-bad=0`、`long-elide=1`（超长状态文案兜底 = 省略号 + tooltip 全文）。
  * 截图仍 12 张；`--dev` 评分与金样 assert.score 见 tests/golden/smoke.py / SCHEMA.md。

M10-W1 / W1-A3（运行页列布局 C10/C11；A11 收缩 + A12 换锚/只增）:
  * `run-geom` 行（既有前缀，行名零改动）新增列布局字段与取值断言：`fit=1`（pp-run-content
    宽 == pp-run-scroll 视口宽，587/520 两档；横向滚动关闭）· `name-w`/`name-w520`（名称列
    实际宽，587 ≥150、520 ≥80）· `no-speed=1`（`pp-run-speed-*`/`pp-run-gspeed-*` 钩子与
    「速度」列题均不存在 —— A11 objectName **收缩**的否定断言）· `ratio-tip=1`（压缩比从已删
    速度列迁入大小列 tooltip：含「压缩比」与「×」）· `cols-ok=1`（表头各列 x/宽 == 组行/输出行
    对应列）· `long-elide=1`（68 字符中文/ASCII 混合名：ElideMiddle 显示含「…」且扩展名可见、
    ppFullText = 全名、tooltip 含完整路径、名称列宽不变）。断言只增/机械换锚，不放宽；冻结末行
    `UI-SMOKE OK shots=12 pages=3` 与 12 张截图集逐字不变（截图按新列布局重新生成、不比对像素）。

M10-W1 / W1-A5（运行页状态/刷新 T9a；C12/C15，仅现有事件面）:
  * 追加 `run-status-map` 行（`smoke_probe_run_geometry()` 的 (o) 段）：组行/格式行状态映射 ——
    共享 Color 完成 + 首输出 Encode 0.4 → 组行「编码中 0/2」/格式行「编码 40%」（行数入 tooltip）；
    Encode 1.0 无 MetaWrite → 「评分中」（Accent + 斜纹）；MetaWrite 0 → 「写入」/「写入中 0/2」；
    MetaWrite 1.0 → 「已完成」（评分列留空到文件终态）；仅 MetaWrite 1.0（无 Encode 段）→「跳过」。
    本脚本除行齐备外做取值断言：`ok=1`、`g-enc="编码中 0/2"`、`o-score="评分中"`、
    `score-tone=<Accent>`、`striped=1`、`o-write="写入"`、`o-done="已完成"`、`done-score=""`、
    `o-skip="跳过"`。
  * 追加 `flush-state` 行：状态翻转后**≤2 个事件循环**内文本变化（双通道 flush 的立即通道），
    且 dirty-row 只渲染脏文件（2 文件页面只翻 1 行 → `rows=1`）；取值断言 `state=1`、`rows=1`。
  * `run-probe-rows` 换锚（A12）：编码行主文案 = `^编码 \\d+%$`（行数移入 tooltip：真实行级
    R/H 行；合成保留斜纹 + 「合成进度（编码器无回调）」tooltip）；**不新增** score-immediate /
    facts-immediate（依赖 W1-T7 的 OutputDone/评分事件，属 W1-T9b）。截图仍 12 张、冻结末行不变。

M10-UI-ADAPT（W2-T5 JXL 治理后的 UI 面适配；断言只改语义锚点，不放宽）:
  * 前缀清单**只增**：新增 `UI-SMOKE m8-params:` 行齐备断言（`probe_m8_params()` 的出口行）——
    本批改写的三态布尔探针 / 只读行 / RC / 规则 / 预设断言全落在该行内，此前不在清单 ⇒
    探针被静默跳过时脚本不可见。既有前缀与行名零改动。
  * 语义适配（C++ 侧，逐条清单见 build/m10/ui-adapt/report.md）：
    ① 三态布尔探针锚点 `pp-param-enable-noise`（`noise` 行随 12 个结构性 no-op 键删除）→
       保留键 `dots`（gaborish/dots/patches 同构），断言条数/语义逐条不变；加性覆盖三条新行在位。
    ② `output-geom` 的 `magic-auto`：`epf` 默认值 2 → -1（= 范围下端 = 魔法值「自动」）⇒ 旧
       前提「默认 ≠ 自动」作废，改为三态取样（默认=自动 / 显式 2 = "2" / 回 -1 = 自动）；
       `changed-pill` 改值基准取显式 2，并加性断言改回默认后计数归零（`changed-pill-reset`）。
    ③ `range-note`：effort 域 1–10 → 1–11（expert_options 门控随该行退场；编码器在 effort=11
       时自动 JxlEncoderAllowExpertOptions），并加性断言 spin 上界 = 11（`effort-max`）。
  * 冻结末行 `UI-SMOKE OK shots=12 pages=3` 与 12 张截图集（不增不减、不比对像素）逐字不变。

M10-W2-T3/T4（tune 全量 + RC 数值行上移；本脚本**不新增前缀、不放宽断言**）:
  * 两批新断言全部落在既有 `UI-SMOKE m8-params:` 行内（`probe_m8_params()` / 输出页逐档探针），
    前缀清单零改动；C++ 侧 expect 失败仍经 `--ui-smoke` 退出码 1 判红。
  * 语义适配（断言只增/换锚，不放宽）：
    ① 档位遍历改**按 value 定位**（`pp-profile-combo.findData(档位编号)`）：W2-T2 追加 16–21 后
       旧「末项 = MainStill」约定作废；逐档断言「可用性 = 单源 disabled 原因」+ 置灰项 item
       tooltip 含「不可用」，并显式钉住 16（Main10Still）置灰；C19 弹出层最小宽 ≥ 控件宽 +
       每项 tooltip 全文。
    ② RC 数值行：`pp-param-row-x265:crf` 父链含 `pp-core-area` 且在 `pp-param-rcmode-row-heif`
       之后；切 CRF/QP 后可见/下发集；位深 8/10/12 的 CRF/QP 域与行内注记（−12..51 等）。
    ③ tune：7 项 = 事实表、4 原生 + animation（tooltip 含「近似仿真」）+ zerolatency/vmaf 置灰
       （tooltip 含「不可用」）；行 tooltip 说明插件强制 aq-mode/psy-rd 与仿真机制。
    ④ QP + 显式 Level → 中文预检错误且 `ready_to_start` 拒绝；Level 改自动后错误实时消失。
  * 截图集仍 12 张（额外 `-11-profile-<value>` / `-12-chroma-*` 走既有存档路径，不计入冻结集）。

M10-W3-T2（格式预设行 + 只读自适应面；C23 §8.5 / C24 §9；断言**只增**）:
  * 前缀清单只增：新增 `UI-SMOKE format-preset:` 行齐备断言（`smoke_probe_format_preset()` 出口行）。
  * 本脚本除行齐备外做**取值断言**（防 C++ 断言被静默跳过/裁剪；任一组红 ⇒ 该组字段 0）：
    `bad=0` · `row=1`（行存在 + 选中即载入的六件套齐备）· `first=1`（行在「位深」行之上 = 页签
    顶部）· `empty=1`（列表空态 = 只有「未选择」）· `prod=1`（生产页同行在位：行由页面自建）·
    `idle=1`（未选中时「删除」禁用 + 原因 tooltip）
    · `collect=1`（卡快照可采集）· `roundtrip=1`（临时目录 保存→列举→载入；载入后卡值/「已改 N」）
    · `note=1`（注释位「来自格式预设：X」）· `normalize=1`（稀疏预设 → 「已补齐 N 个默认项」）
    · `invalid-save=1`（保存前校验：非法名字 → 中文错误且不写盘）· `bytes=1`（save→load→save
    逐字节相同）· `broken=1`（损坏 JSON：列举标「（损坏）」+ 中文错误 + 卡值零改动）· `kind=1`（kind
    不符显式拒绝）· `adaptive=1`（真实含策略预设 → 摘要含目标/轮数/阶段）· `switch=1`（开关按预设
    初值 + 关闭 = 本次不启用）· `nopolicy=1`（无策略 → 整行不显示）· `policy-err=1`（坏策略 →
    行内中文错误 + 开关禁用）。
  * 冻结末行 `UI-SMOKE OK shots=12 pages=3` 与 12 张截图集逐字不变（本批不新增截图）。
"""

import os
import re
import subprocess
import sys
import tempfile

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')

FROZEN_LINE = 'UI-SMOKE OK shots=12 pages=3'

# M4-W3-T14 运行页自检行（前缀匹配；缺任一 → FAIL）
T14_LINE_PREFIXES = (
    'UI-SMOKE 03-run:',        # 真实运行读数：progress / outputs / 底栏「运行中… N%」
    'UI-SMOKE run-rows:',      # 真实运行：逐输出行数 = 文件数 × 格式数
    'UI-SMOKE run-bottom:',    # 底栏运行态：状态计数 + ETA 约 mm:ss + .go「运行中… N%」
    'UI-SMOKE run-rows-final:',  # 结束态：全行已结算 + 产物事实（字节/压缩比）+ 失败原因
    'UI-SMOKE lock-visual:',   # 运行期锁定（G5）：步骤1-2/设置/预设/文件面板/预览/页1-2/取消钮
    'UI-SMOKE run-log-tail:',  # 日志尾卡（单一持有者 = MainWindow 读 run-*.log）
    'UI-SMOKE run-idle:',      # 运行页空闲态（就绪 + 引导 + 空任务表）
    'UI-SMOKE run-probe-rows:',  # 探针页逐输出行：真实行级/合成/失败原因/排队/产物事实
    'UI-SMOKE run-bars:',      # 真实 vs 斜纹像素取证（accent 实心 vs 双色带）
    'UI-SMOKE run-overview:',  # 总览只读栅格（交错/线程预算/并行/当前分配/输出计数）
    'UI-SMOKE run-cancel:',    # 取消态（唯一取消入口 → 定稿「已取消」）
    'UI-SMOKE run-light:',     # 浅色主题下的斜纹渲染
)

# M4-W3-T15 自检行（前缀匹配；缺任一 → FAIL）：设置对话框三项追加控件 → AppSettings 读回
T15_LINE_PREFIXES = (
    'UI-SMOKE settings-dialog:',  # 交错启动 / 线程预算 / 分文件夹默认结构（§9.3）
)

# M5-T4 自检行（前缀匹配；缺任一 → FAIL）：壳层几何值表（§3/§6.2/R4）+ 状态四档（§8）
T4_LINE_PREFIXES = (
    'UI-SMOKE shell-geom:',     # 顶栏 44 / 状态栏 30 / 导航段 92×30 / caption 46×32 /
                                # 栏宽 330·587 / CTA 22 / btn 26 / 品牌图标 20 / 内边距 10
    'UI-SMOKE nav-text:',       # §6.2 段序（① 元数据→② 输出→③ 运行）+ K1 无完成态装饰/字形
    'UI-SMOKE status-tones:',   # §8 四档 QSS 规则 + 点/词同档 + 空态不得为红
)

# M5-T5 自检行（前缀匹配；缺任一 → FAIL）：文件行/工具行几何（§7.5 + C7）+ 空态 + 选中面
T5_LINE_PREFIXES = (
    'UI-SMOKE filelist-geom:',  # 行高 58 / 缩略图 46 / 勾选框 13 / 工具行按钮 28×26 /
                                # 信息行字阶 10 / 工具行常驻 + 移除钮隐藏 / 不截断 / 空态 / 选中条
)

# M5-T6 自检行（前缀匹配；缺任一 → FAIL）：预览画布几何/状态（§8 + C7）
T6_LINE_PREFIXES = (
    'UI-SMOKE preview-geom:',   # 画布底 #16181d（明暗同值）/ 缩放条钮 28 / 翻图钮 28 /
                                # 角标空态隐藏 + 半透明底与描边 / 提示逐字 / 空态图标+引导语 /
                                # chip 同档禁用 / 衬垫与画布底区分（3-21）/ 最近邻（3-20）
)

# M5-T7 自检行（前缀匹配；缺任一 → FAIL）：元数据页几何/空态/状态徽标（§7.1/R1–R4 + 修 1-23/1-33/1-37/1-38/1-39）
T7_LINE_PREFIXES = (
    'UI-SMOKE meta-geom:',      # 卡内行 pitch 28 / 标签轨 80 / 日期时间 spin 88 / 生效行 acc 值
                                # （像素取证）/ 空态收敛 / 双列第二标签起点（A2）/ 卡头中性胶囊 /
                                # 操作行在地图上方 / 折叠默认 / 按钮 26 / 离线说明中性灰
    # M13（0.4.1）/ C2/C3/C5：选择项级编辑 / 多选混合值 / 卡头三态 / CreatorTool / 自动海拔
    # （独立 PageMeta 探针，离线；只增行）
    'UI-SMOKE meta-selection:',
    # 0.4.2（需求 1/2）：无行选中 = 空态 + 可编辑控件禁用 + 零写入（真实行为契约变更；
    # 本行由 sec_h 输出：hint=未选择文件 empty-rows=1 controls-off=1 items=0 commits=0）
    'UI-SMOKE meta-no-selection:',
)

# M5-T8 自检行（前缀匹配；缺任一 → FAIL）：输出页四卡组几何（§7.2 逐行 + R1–R4 + 修 2-05…2-42）
T8_LINE_PREFIXES = (
    'UI-SMOKE output-geom:',    # 磁贴 2×4 零形变 / 六行一义 pitch 28 / spin 88·160 / 折叠行 +
                                # 子分组 / 互斥置灰 / 模板示例行 / 中性计数胶囊 / 按钮 26 / 页签
                                # 格式名·位深 / 预设 placeholder
)

# M5-T9 自检行（前缀匹配；缺任一 → FAIL）：运行页三卡组几何/语义（§7.3/R1–R4/K4 + 修 3-03…3-16）
# M10-W1 / W1-A3 起：run-geom 行内另带列布局新字段（fit/name-w/name-w520/no-speed/ratio-tip/
# cols-ok/long-elide；取值断言见 RUN_GEOM_* 常量段），行名与既有前缀零改动。
T9_LINE_PREFIXES = (
    'UI-SMOKE run-geom:',       # kv 行 pitch 28 / 标签轨 80 / 表行高 28 / 进度条 6 / 日志尾等宽 4 行 +
                                # 查看完整日志… / 状态胶囊单形态（完成绿·0 字节琥珀·失败红 + 原因行）/
                                # 指标单源 / 实现性文案出界面 / 三级动作 + 按钮 26 / 计数单源
)

# M5-T10 自检行（前缀匹配；缺任一 → FAIL）：设置弹窗 §7.4 重做几何/语义（值表驱动，行内
# `checked=N bad=M` 由 C++ 侧给出；既有 settings-dialog 行 = 交错启动/线程预算/分文件夹默认三项读回）
# + EXIF 编辑器树行标记（A8 零字形）
T10_LINE_PREFIXES = (
    'UI-SMOKE exif-rows:',      # 行文本无 〔 〕 ● 与「批」字形；批量命中 ≥ 1 行走 item data
                                # （delegate 画中性 pill「批」）；注入改值 → changed role +
                                # 强调色字 + 700（无 ●）
    'UI-SMOKE settings-geom:',  # 660 宽 / 标题栏 40 / 页签条 34（5 页签，序逐字 = A21 通用·输出·
                                # 运行·地图·关于）/ 底栏 48 / 行 pitch 28 / 字段 160 × 4 +
                                # 单位槽 16 + 范围注释 / 确定实心（QSS 规则 + accent 色）且居右 +
                                # 恢复默认居左 / 焦点环（accent 1px，跟随焦点控件；outline:none）/
                                # 主题三档段几何 + 段组贴字段轨 / 无语言假控件 / 修 4-08 自动档 =
                                # 下拉项（0）/ 修 4-15 alpha 端值映射（100→255 白、0→0 黑）
)

# M5-T11 自检行（前缀匹配；缺任一 → FAIL）：A28 主题档持久化（三档写回 + 点段即时生效 +
# 存储往返 + 启动读取点 + 主窗构造路径端到端）；PP_UI_THEME 强制档在场时启动链让位 →
# 行内 checked 少 3 项（6 而非 9），断言本体不变。
# A27/A16 的断言不新增行名（落在 meta-geom/output-geom/settings-geom/filelist-geom 行内）。
T11_M5_LINE_PREFIXES = (
    'UI-SMOKE settings-theme:',  # system/light/dark 三档写回 + AppSettings 往返 + 单源解析链
)

# M4-W2-T11 自检行（前缀匹配；缺任一 → FAIL）
# M5-T12（A15）追加 `classify-grid:`：分类面板 = 2 列 kv-grid（相邻单元同 y / 两列等宽 /
# 列距 12 / 行 pitch 28 / 「＋ 新建分类…」占下一个空位）。
T11_LINE_PREFIXES = (
    'UI-SMOKE classify-grid:',
    'UI-SMOKE filelist-roles:',
    'UI-SMOKE filelist-check:',
    'UI-SMOKE filelist-search:',
    'UI-SMOKE filelist-group-format:',
    'UI-SMOKE filelist-group-camera:',
    'UI-SMOKE filelist-group-none:',
    'UI-SMOKE filelist-group-month:',
    'UI-SMOKE filelist-span:',
    'UI-SMOKE filelist-collapse:',
    'UI-SMOKE classify-panel:',
    'UI-SMOKE classify-tag:',
    'UI-SMOKE classify-menu:',
    'UI-SMOKE classify-bulk:',
    'UI-SMOKE classify-crud:',
    'UI-SMOKE classify-roundtrip:',
    'UI-SMOKE classify-lock:',
)

# M5-T13（W-G4 验收，探针只增）：§12-13 亮暗布局等价 + §12-14 hint 禁折行 —— 两条 §12 条目
# 此前无独立探针，本行由 `smoke_probe_verification()` 给出（`checked=2 bad=0 equiv-pp-widgets=N
# hints=M`；N/M 为比对到的控件/hint 数，证明取证非空）。
T13_LINE_PREFIXES = (
    'UI-SMOKE theme-equiv+nowrap:',  # pp-* 控件 (objectName, 窗口几何, 可见性) Dark↔Light 逐项一致
                                     # （§9 布局零差异）+ *-hint 禁折行/单行高/省略号（§12-14）
                                     # + 数值面右对齐/tnum（§12-8 spin/数值列右对齐 + kv tnum；
                                     # 行内 checked=3）
)

# GUI-R2（docs/gui-review-0.3.0-r2.md 回归探针；前缀匹配，缺任一 → FAIL）：
#   combo-consistency —— 全界面 QComboBox 样式钩子/高 24/实拍非空（问题 1+5）
#   format-card-align —— 格式参数页签带 ↔「＋ 添加格式…」几何同带（问题 2）
#   dialog-close      —— 设置/元数据编辑器/预设 弹窗 caption ✕ 真关窗（问题 3）
#   run-guard         —— 运行闸门一致性 + 点击不弹模态 + 运行期禁用（问题 4）
GUI_R2_LINE_PREFIXES = (
    'UI-SMOKE combo-consistency:',
    'UI-SMOKE format-card-align:',
    'UI-SMOKE dialog-close:',
    'UI-SMOKE run-guard:',
    'UI-SMOKE render-tokens:',
)

# M6-T9（R3 选点闭环）自检行（前缀匹配，缺任一 → FAIL）：搜索→多钉+可点列表→点钉→主钉+
# 规则框回填（**选点值 = WGS-84 断言**：amap/GCJ-02 基准面下回填值 = 结果点 WGS-84，显式
# 排除 GCJ-02 偏移值）→地图微调→「地图选点」取中心→逆地理（离线分类文案）→换源清钉
# （不留"点不动"的旧钉）。探针全程离线（结果面由 set_result_pins + search_results_ready
# 等价喂入），行内 `checked=N bad=M` 由 C++ 侧给出。
M6T9_LINE_PREFIXES = (
    'UI-SMOKE meta-loop:',
)

# M6-T13（R5 大地图）自检行（前缀匹配，缺任一 → FAIL）：覆盖层「展开」钮（24×24 矢量图标钮 +
# tooltip/accessibleName + `ppIcon` 键 + 右上角位）→ 点按进模态「地图」对话框（标题/底栏关闭/
# caption ✕ 齐备），对话框内是**同一只** MapWidget（重挂载：指针同源 = 图层/图钉/视口随控件走、
# 零同步；地图区 ≥640×480、实拍非空）→ 底栏「关闭」（真实用户路径）→ 挂回卡片行（固定高 136、
# 几何/落点逐位复原、`pp-meta-map` objectName 不变）→ 二次展开（可重复 + 尺寸记忆 = 上次尺寸；
# 大视图随对话框尺寸）。探针在 exec() 的嵌套循环内取证并关窗（预置 0ms 定时器），全程离线；
# 行内 `checked=N bad=M` 由 C++ 侧给出。
M6T13_LINE_PREFIXES = (
    'UI-SMOKE meta-map-expand:',
)

# M7-R2.2-G1（G1 探针总集）自检行（前缀匹配，缺任一 → FAIL）：wave-3/4 常驻探针行齐备性
# 收编 + 本批新增两行（format-roundtrip / rtl-deep）；见文件头 M7-R2.2-G1 节。
G1_LINE_PREFIXES = (
    'UI-SMOKE rtl-mirror:',
    'UI-SMOKE check-all:',
    'UI-SMOKE context-menu:',
    'UI-SMOKE run-reason:',
    'UI-SMOKE folds:',
    'UI-SMOKE intake:',
    'UI-SMOKE classify-filter:',
    'UI-SMOKE edit-target:',
    'UI-SMOKE settings-tabs-light:',
    'UI-SMOKE format-roundtrip:',
    'UI-SMOKE rtl-deep:',
    'UI-SMOKE color-options:',
    'UI-SMOKE zero-byte-redo:',
    'UI-SMOKE failure-list:',
    # 2026-10（0.3.5）：格式参数卡交互矩阵（页签序/空态/模板联动/跨后端不串参数；
    # 普通 --ui-smoke 也跑，行由 smoke_probes.cpp 的 probe_format_card_interactions 给出）
    'UI-SMOKE format-card:',
    # M8-W2（需求2）：目录行「可选启用」勾选框行尾几何（probe_m8_params() ⑨ 段；只增）
    'UI-SMOKE param-enable-geom:',
    # M10-UI-ADAPT：`probe_m8_params()` 出口行（三态布尔 / 只读行 / RC / 规则预检 / 预设往返的
    # 硬判都在该行内）。此前不在清单 ⇒ 探针被静默跳过时脚本不可见；本批语义适配的探针也在此行。
    'UI-SMOKE m8-params:',
    # M10-W3-T2（C23 §8.5 / C24 §9；只增）：格式预设行 + 只读自适应面探针出口行
    # （`smoke_probe_format_preset()`；行内取值断言见 FORMAT_PRESET_* 常量段）。
    'UI-SMOKE format-preset:',
)

# M7-R2.4-G2（布局审计 · R-识图1）自检行：**识图步骤的机械化落点**。起因 = debug/debug1.png +
# debug/debug2.png（2026-10-13 红框 7 处）——本文件头明写「截图仅存档，不做像素比对」，于是
# 渲染面的裁切/重叠/空按钮在全部门禁下无人看守。探针本体在 smoke_probes.cpp 的
# smoke_probe_layout_audit()（几何 + 像素断言六类），行内 `checked=N bad=M` 由 C++ 侧给出；
# 本脚本只要求该行存在（防断言被静默跳过）。冻结末行与既有清单零改动。
LAYOUT_AUDIT_LINE_PREFIXES = (
    'UI-SMOKE layout-audit:',
)

# M8-C13 → M8-W2（需求3 评分列·只增）：`smoke_probe_run_geometry()` 末尾 (m) 段。探针本体在
# C++ 侧用独立 PageRun 实例取证（四档 96.20/92.31/85.00/72.50 + Unavailable + Off），本脚本除
# 「行齐备」外还做**文本/取值断言**（防断言被静默跳过/裁剪）。旧子行口径换成列口径，强度不降：
#   bad=0 · off_blank=1（Off 留空）· ok_row=1 · col_align=1（组行/输出行同 x 同宽）
#   header=1（表头「评分」同 x 同宽）· tiers=4（四档语义色）· group_dash=1（组行「—」不平均）
#   ok_text="<两位小数>"（正则）· unavail_text="—"（Unavailable 简洁形态，原因进 tooltip）
RUN_SCORE_LINE_PREFIX = 'UI-SMOKE run-score:'
RUN_SCORE_BAD_RE = re.compile(r'\bbad=0\b')
RUN_SCORE_TEXT_RE = re.compile(r'ok_text="-?\d+\.\d{2}"')
RUN_SCORE_DASH_RE = re.compile(r'unavail_text="—"')

# M8-W2（需求2 勾选框换位·只增）：`probe_m8_params()` ⑨ 段几何断言 —— 「可选启用」勾选框在
# 行尾右侧、右端 x 严格对齐、标签轨/数值列归位、状态注记固定槽宽不硬裁（超长走省略号+tooltip）。
PARAM_ENABLE_GEOM_LINE_PREFIX = 'UI-SMOKE param-enable-geom:'
PARAM_ENABLE_GEOM_BAD_RE = re.compile(r'\bbad=0\b')
PARAM_ENABLE_GEOM_ROWS_RE = re.compile(r'\brows=(\d+)\b')
PARAM_ENABLE_GEOM_TAIL_RE = re.compile(r'\btail=(\d+)\b')
PARAM_ENABLE_GEOM_LABEL_RE = re.compile(r'\blabel=(\d+)\b')
PARAM_ENABLE_GEOM_EDITOR_RE = re.compile(r'\beditor=(\d+)\b')
PARAM_ENABLE_GEOM_ORDER_RE = re.compile(r'\border=(\d+)\b')
PARAM_ENABLE_GEOM_CLIP_RE = re.compile(r'\bstatus-clip-bad=0\b')
PARAM_ENABLE_GEOM_LONG_RE = re.compile(r'\blong-elide=1\b')

# M10-W1 / W1-A3（C10/C11；A12 只增）：运行页**列布局**探针 —— `smoke_probe_run_geometry()`
# 的 (n) 段，行 = 既有 `UI-SMOKE run-geom:`（前缀已在 T9_LINE_PREFIXES 内）。本脚本除「行齐备」
# 外，另对新字段做**取值断言**（防 C++ 断言被静默跳过/裁剪）：
#   fit=1（内容宽 == 视口宽，587/520 两档）· name-w>=150（587 栏宽）· name-w520>=80（520 档）
#   no-speed=1（pp-run-speed-*/pp-run-gspeed-* 钩子与「速度」列题均不存在 = A11 收缩否定断言）
#   ratio-tip=1（压缩比迁入大小列 tooltip，含「压缩比」与「×」= C11）
#   cols-ok=1（表头各列 x/宽 == 组行/输出行对应列）· long-elide=1（68 字符名 ElideMiddle +
#   ppFullText=全名 + tooltip 含全路径 + 名称列宽不变 = C10/§5.2）
RUN_GEOM_LINE_PREFIX = 'UI-SMOKE run-geom:'
RUN_GEOM_BAD_RE = re.compile(r'\bbad=0\b')
RUN_GEOM_FIT_RE = re.compile(r'\bfit=1\b')
RUN_GEOM_NAME_W_RE = re.compile(r'\bname-w=(\d+)\b')
RUN_GEOM_NAME_W520_RE = re.compile(r'\bname-w520=(\d+)\b')
RUN_GEOM_NO_SPEED_RE = re.compile(r'\bno-speed=1\b')
RUN_GEOM_RATIO_TIP_RE = re.compile(r'\bratio-tip=1\b')
RUN_GEOM_COLS_RE = re.compile(r'\bcols-ok=1\b')
RUN_GEOM_LONG_ELIDE_RE = re.compile(r'\blong-elide=1\b')
# 状态机修复回归（只增）：迟到心跳不覆写终态 / 取消零效果的结束态 = 已完成
RUN_GEOM_LATE_TERMINAL_RE = re.compile(r'\blate-terminal=1\b')
RUN_GEOM_CANCEL_NOOP_RE = re.compile(r'\bcancel-noop=1\b')

# M10-W1 / W1-A5（T9a；C12/C15，A12 只增）：运行页**状态/刷新**探针 —— `smoke_probe_run_geometry()`
# 的 (o) 段，两行均**新增**（行齐备 + 取值断言；既有前缀/冻结末行零改动）：
#   `UI-SMOKE run-status-map:`  组行/格式行状态映射（仅现有事件面）——
#       ok=1 · g-enc="编码中 0/2"（Color 完成 + 首输出 Encode 0.4）· o-enc="编码 40%"（行数入 tooltip）
#       g-score="评分中 0/2" / o-score="评分中"（Encode 1.0 无 MetaWrite）· score-tone=3（Accent）
#       striped=1（评分中斜纹）· g-write="写入中 0/2" / o-write="写入" · g-done="编码中 1/2" /
#       o-done="已完成"（MetaWrite 1.0 中间态；done-score 空 = 评分列留空到文件终态）·
#       o-skip="跳过" / skip-tone=2（Warn；首事件即 MetaWrite 1.0、无 Encode 段）。
#   `UI-SMOKE flush-state:`      双通道 flush + dirty-row：状态翻转后 ≤2 个事件循环内文本变化；
#       2 文件页面只翻 1 行 → rows=1（只渲染脏文件，不是全量）。
RUN_STATUS_MAP_LINE_PREFIX = 'UI-SMOKE run-status-map:'
RUN_STATUS_MAP_OK_RE = re.compile(r'\bok=1\b')
RUN_STATUS_MAP_G_ENC_RE = re.compile(r'\bg-enc="编码中 0/2"')
RUN_STATUS_MAP_O_SCORE_RE = re.compile(r'\bo-score="评分中"')
RUN_STATUS_MAP_STRIPED_RE = re.compile(r'\bstriped=1\b')
RUN_STATUS_MAP_O_WRITE_RE = re.compile(r'\bo-write="写入"')
RUN_STATUS_MAP_O_DONE_RE = re.compile(r'\bo-done="已完成"')
RUN_STATUS_MAP_DONE_SCORE_RE = re.compile(r'\bdone-score=""')
RUN_STATUS_MAP_O_SKIP_RE = re.compile(r'\bo-skip="跳过"')
FLUSH_STATE_LINE_PREFIX = 'UI-SMOKE flush-state:'
FLUSH_STATE_OK_RE = re.compile(r'\bstate=1\b')
FLUSH_STATE_ROWS_RE = re.compile(r'\brows=(\d+)\b')

# 状态机修复批（需求 4 回归；只增）：四条独立自检行 —— 前缀清单只增、冻结末行不变。
#   run-terminal-late：文件终态后到达的评分池心跳/迟到阶段事件不得把组行/行胶囊打回进行中
#                      （ok=1 + g-late="已完成" + o-late="已完成"）。
#   run-cancel-noop  ：点了「取消剩余」但引擎汇总 cancelled=0（全部文件已成功）→ 结束态必须是
#                      「已完成」而不是「已取消」（ok=1 + pill="已完成"）。
#   run-rerun-clean  ：再运行不残留上一轮（评分/耗时/大小/状态）。
#   run-restart      ：开始→取消→再开始（已取消 → 进行中 + 取消入口可用 + 进度归零）。
STATE_FIX_LINE_PREFIXES = (
    'UI-SMOKE run-terminal-late:',
    'UI-SMOKE run-cancel-noop:',
    'UI-SMOKE run-rerun-clean:',
    'UI-SMOKE run-restart:',
)
RUN_TERMINAL_LATE_OK_RE = re.compile(r'\bok=1\b')
RUN_TERMINAL_LATE_G_RE = re.compile(r'\bg-late="已完成"')
RUN_TERMINAL_LATE_O_RE = re.compile(r'\bo-late="已完成"')
RUN_CANCEL_NOOP_OK_RE = re.compile(r'\bok=1\b')
RUN_CANCEL_NOOP_PILL_RE = re.compile(r'\bpill="已完成"')
RUN_CANCEL_NOOP_VIS_RE = re.compile(r'\bcancel-visible=0\b')
RUN_RERUN_CLEAN_OK_RE = re.compile(r'\bok=1\b')
RUN_RERUN_CLEAN_FIRST_RE = re.compile(r'\bfirst-score="92\.31"')
RUN_RERUN_CLEAN_TIME_RE = re.compile(r'\brerun-time="—"')
RUN_RERUN_CLEAN_PILL_RE = re.compile(r'\brerun-pill="排队"')
RUN_RESTART_OK_RE = re.compile(r'\bok=1\b')
RUN_RESTART_PILL_RE = re.compile(r'\brestarted-pill="进行中"')
RUN_RESTART_CANCEL_RE = re.compile(r'\bcancel-visible=1\b')
RUN_RESTART_PERCENT_RE = re.compile(r'\bpercent="0%"')

# M10-W3-T2（C23 §8.5 / C24 §9，只增）：格式预设行 + 只读自适应面 —— `smoke_probe_format_preset()`
# 的出口行。行齐备 + **取值断言**：C++ 侧每组断言的结论直接进字段（组红 ⇒ 字段 0），因此这里
# 逐字段钉死，任何一组被静默跳过/裁剪都会被抓到（不放宽既有断言；冻结末行与截图集不变）。
# M12（需求 2/3/4/5）**机械换锚**（Lead 裁定；登记 docs/m12-adjudications.md）：
#   旧字段名全部保留、语义换锚 + 新增字段追加（「行字段只增」）：
#     empty             → 未喂列表 = 空错误态（count==0，无「未选择」占位项）
#     prod              → 生产页行在位 + 首启自动选中并载入 default
#     builtin           → 工厂 seed 就位（11 文件/8 default）且列表含 default
#     builtin-idem      → 工厂 seed 二次幂等（written=0/skipped=11）
#     builtin-readonly  → I/O 拒绝删除 default（文件零改动；保存覆盖仍可用）
#     builtin-saveas    → 另存为普通名字副本成功（非 default）
#     clears-source     → 全局预设后进入「接管（detached）」态：path 空 / save 禁用 / 点保存无意图
#     unselect-adaptive → 预设来源消失 ⇒ 自愈回 default 并载入（不残留旧策略）
#   新增：default-selected/no-unselected/default-protected/save-default/jpeg-adaptive/
#         heif-apple-crf/legacy-migrated/png-no-adaptive/factory-seed/factory-idem/
#         factory-migrate/global-detached
FORMAT_PRESET_LINE_PREFIX = 'UI-SMOKE format-preset:'
FORMAT_PRESET_BAD_RE = re.compile(r'\bbad=0\b')
FORMAT_PRESET_CHECKED_RE = re.compile(r'\bchecked=(\d+)\b')
# 组字段 → 语义（逐条对应 C++ 侧 expect 组名；M12 换锚说明见上）
FORMAT_PRESET_GROUP_FIELDS = (
    'row',        # row-exists：行 + 六件套（下拉/保存/另存/删除/导入/导出）齐备且可见
    'first',      # row-first：行在「位深」行之上（页签顶部）
    'empty',      # empty（换锚）：未喂列表 = 空错误态（count==0，无「未选择」占位项）
    'prod',       # prod-row（换锚）：生产页行在位 + 首启已自动选中并载入 default
    'idle',       # idle-buttons：空态「删除」禁用 + 原因 tooltip（不静默灰）
    'collect',    # collect：卡快照可采集（format/backend/quality）
    'roundtrip',  # roundtrip（换锚）：临时目录 保存→列举→**自动载入**→显式选中；卡值一致
    'note',       # note-loaded：注释位「来自格式预设：X」
    'normalize',  # normalize-note：稀疏预设载入 → 注释位「已载入（已补齐 N 个默认项）」
    'invalid-save',  # save-invalid：保存前校验（非法名字 → 中文错误且不写盘）
    'bytes',      # bytes：save→load→save 逐字节相同
    'broken',     # broken：损坏 JSON → 列举「（损坏）」+ 中文错误 + 卡值零改动
    'kind',       # kind：kind 不符显式拒绝
    'adaptive',   # adaptive：真实含策略预设 → 摘要（目标/轮数/阶段）
    'switch',     # switch：开关按预设初值 + 关闭 = 本次不启用
    'nopolicy',   # nopolicy：无策略 → 整行不显示
    'policy-err', # policy-err：坏策略 → 行内中文错误 + 开关禁用
    # M12：工厂（出厂）数据文件 + default 语义（旧 builtin 字段 = 换锚映射，名称保留）
    'builtin',          # 工厂 seed 就位（11 文件/8 default）且 jpeg 列表含 default
    'builtin-idem',     # 工厂 seed 二次幂等（报告计数 wired=0/skipped=11）
    'builtin-readonly', # I/O 拒绝删除 default（中文原因 + 文件零改动；保存覆盖可用）
    'builtin-saveas',   # 另存为普通名字副本成功且名字非 default
    'global-adaptive',  # 全局预设含 adaptive 的 save→load→再保存字节稳定 + 载入后只读面同步
    'adaptive-gate',    # adaptive-gate：开 → 下发策略；关 → 不下发但采集面保留策略文本
    # 需求 5（0.4.0.alpha2 用户修订）：格式预设行几何 —
    # 下拉宽 == 同页参数行字段宽（kFieldStd 160 ±1）/ 导入·导出无省略号 / 按钮不截断 / 行内无重叠
    'preset-geom',
    # preset-tip（M12 换锚）：显式选 default → combo tooltip = "default"、注记 = 来自格式预设：default
    'preset-tip',
    # clears-source（0.4.2 机械换锚）：全局预设后进入「来源 = 大预设」态 —
    # 来源清空（非绑定文件）/ combo **无选中项**（不再假装高亮 default）+ 占位「来自大预设：<名>」/
    # save 禁用 + 原因 tooltip 指路另存 / 注记含「值来自大预设」/ 点 save 无覆盖意图
    'clears-source',
    # unselect-adaptive（M12 换锚）：预设来源文件消失 ⇒ 自愈回 default 并载入
    # （策略会话态随 default 更新，不残留被删来源的旧策略）
    'unselect-adaptive',
    # ---- M12 新增字段（只增；需求 2/3/4/5 的机械验收面）----
    'default-selected',   # 工厂 jpeg/default 自动选中并载入（RC=距离 / distance=3.0）
    'no-unselected',      # 下拉只含真实预设文件（无「未选择」占位项）
    'default-protected',  # default 不可删除：UI 删除置灰+tooltip 且 I/O 拒绝（文件零改动）
    'save-default',       # default 可「保存」直接覆盖（临时根：写入成功 + 值读回）
    'jpeg-adaptive',      # JPEG default 载入后自适应行可见/开关开/摘要「目标 90 分」/策略断点
                          # `breakpoint 1 step 1`（需求 5）
    'heif-apple-crf',     # 工厂 heif/apple-adaptive 载入后 `pp-param-x265:crf` 控件读数 == 20（需求 3）
    'legacy-migrated',    # 旧 `_builtin-*.json` 迁移后文件真的消失（与 factory-migrate 计数互补）
    'png-no-adaptive',    # PNG default 无策略 → 自适应整行隐藏（空策略隐藏不变）
    'factory-seed',       # seed 首轮：written=11 / 8 格式 default.json / removed_legacy=2
    'factory-idem',       # seed 次轮：written=0/skipped=11 且文件字节不变
    'factory-migrate',    # 迁移清理计数 removed_legacy（与 legacy-migrated 互补）
    'global-detached',    # 接管态可恢复：显式选 default 载入后 save 恢复、注记回来源
    # ---- 0.4.2 / M14-A13 新增字段（只增；需求 3 大预设重设计的机械验收面）----
    'global-source',          # 来源=大预设：outputs[i].source_preset 落盘/回读 + combo 无选中项 +
                              # 占位「来自大预设：<名>」+ 注记「基于格式预设 <stem>」+ 保存禁用
    'global-source-custom',   # 无来源注记 ⇒ 注记「（自定义参数）」（不编造来源）
    'global-source-rebind',   # 显式重选格式预设 ⇒ 解除大预设来源、恢复真绑定（save 恢复可用）
    'preset-binding',         # 保存落点 = 绑定绝对路径（不再用显示名拼新文件；无幽灵文件）
    'preset-dirty',           # 页面配置 ≠ 绑定预设基线 ⇒ 条目「（已修改）」；保存后归零
)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

build_dir = sys.argv[1] if len(sys.argv) > 1 else 'build/release-dev'
if not os.path.isabs(build_dir):
    build_dir = os.getcwd() + '/' + build_dir
bin_path = build_dir + '/photopipeline'


def resolve_exe(path):
    """name 与 name + '.exe' 双探测（Windows 上实际产物带 .exe）；找不到 → None。"""
    for candidate in (path, path + '.exe'):
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return candidate
    return None


def main():
    binary = resolve_exe(bin_path)
    if binary is None:
        print('ui_smoke: binary not found or not executable: {}'.format(bin_path),
              file=sys.stderr)
        print('ui_smoke: build it first, e.g. cmake --preset release-dev '
              '-DVCPKG_MANIFEST_INSTALL=OFF && cmake --build --preset release-dev -j',
              file=sys.stderr)
        return 2

    shots = os.path.join(ROOT, '.cache', 'ui-smoke-ci')
    os.makedirs(shots, exist_ok=True)

    handle, log_path = tempfile.mkstemp(prefix='ui-smoke-', suffix='.log')
    os.close(handle)
    try:
        child_env = dict(os.environ, QT_QPA_PLATFORM='offscreen')
        proc = subprocess.Popen(
            [binary, '--ui-smoke', '--inputs', ROOT + '/tests/golden/base', '--shots', shots],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT, env=child_env,
            text=True, encoding='utf-8', errors='replace')
        found = False
        t4_seen = set()
        t5_seen = set()
        t6_seen = set()
        t7_seen = set()
        t8_seen = set()
        t9_seen = set()
        t10_seen = set()
        t11_seen = set()
        m5t11_seen = set()
        t13_seen = set()
        t14_seen = set()
        t15_seen = set()
        gui_r2_seen = set()
        m6t9_seen = set()
        m6t13_seen = set()
        g1_seen = set()
        layout_audit_seen = set()
        state_fix_seen = set()
        run_score_line = None
        param_enable_geom_line = None
        run_geom_line = None
        run_status_map_line = None
        flush_state_line = None
        format_preset_line = None
        run_terminal_late_line = None
        run_cancel_noop_line = None
        run_rerun_clean_line = None
        run_restart_line = None
        with open(log_path, 'w', encoding='utf-8', newline='') as log_fp:
            for line in proc.stdout:
                sys.stdout.write(line)
                sys.stdout.flush()
                log_fp.write(line)
                if line.rstrip('\r\n') == FROZEN_LINE:
                    found = True
                for prefix in T4_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t4_seen.add(prefix)
                for prefix in T5_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t5_seen.add(prefix)
                for prefix in T6_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t6_seen.add(prefix)
                for prefix in T7_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t7_seen.add(prefix)
                for prefix in T8_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t8_seen.add(prefix)
                for prefix in T9_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t9_seen.add(prefix)
                for prefix in T10_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t10_seen.add(prefix)
                for prefix in T11_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t11_seen.add(prefix)
                for prefix in T11_M5_LINE_PREFIXES:
                    if line.startswith(prefix):
                        m5t11_seen.add(prefix)
                for prefix in T13_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t13_seen.add(prefix)
                for prefix in T14_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t14_seen.add(prefix)
                for prefix in T15_LINE_PREFIXES:
                    if line.startswith(prefix):
                        t15_seen.add(prefix)
                for prefix in GUI_R2_LINE_PREFIXES:
                    if line.startswith(prefix):
                        gui_r2_seen.add(prefix)
                for prefix in M6T9_LINE_PREFIXES:
                    if line.startswith(prefix):
                        m6t9_seen.add(prefix)
                for prefix in M6T13_LINE_PREFIXES:
                    if line.startswith(prefix):
                        m6t13_seen.add(prefix)
                for prefix in G1_LINE_PREFIXES:
                    if line.startswith(prefix):
                        g1_seen.add(prefix)
                for prefix in LAYOUT_AUDIT_LINE_PREFIXES:
                    if line.startswith(prefix):
                        layout_audit_seen.add(prefix)
                for prefix in STATE_FIX_LINE_PREFIXES:
                    if line.startswith(prefix):
                        state_fix_seen.add(prefix)
                if line.startswith(RUN_SCORE_LINE_PREFIX):
                    run_score_line = line.rstrip('\r\n')
                if line.startswith(PARAM_ENABLE_GEOM_LINE_PREFIX):
                    param_enable_geom_line = line.rstrip('\r\n')
                if line.startswith(RUN_GEOM_LINE_PREFIX):
                    run_geom_line = line.rstrip('\r\n')
                if line.startswith(RUN_STATUS_MAP_LINE_PREFIX):
                    run_status_map_line = line.rstrip('\r\n')
                if line.startswith(FLUSH_STATE_LINE_PREFIX):
                    flush_state_line = line.rstrip('\r\n')
                if line.startswith(FORMAT_PRESET_LINE_PREFIX):
                    format_preset_line = line.rstrip('\r\n')
                if line.startswith(STATE_FIX_LINE_PREFIXES[0]):
                    run_terminal_late_line = line.rstrip('\r\n')
                if line.startswith(STATE_FIX_LINE_PREFIXES[1]):
                    run_cancel_noop_line = line.rstrip('\r\n')
                if line.startswith(STATE_FIX_LINE_PREFIXES[2]):
                    run_rerun_clean_line = line.rstrip('\r\n')
                if line.startswith(STATE_FIX_LINE_PREFIXES[3]):
                    run_restart_line = line.rstrip('\r\n')
        status = proc.wait()

        if status != 0:
            print('ui_smoke: FAIL (photopipeline --ui-smoke exit {})'.format(status),
                  file=sys.stderr)
            return status
        if not found:
            print("ui_smoke: FAIL (missing frozen success line '{}')".format(FROZEN_LINE),
                  file=sys.stderr)
            return 1
        missing = [p for p in T4_LINE_PREFIXES if p not in t4_seen]
        if missing:
            print('ui_smoke: FAIL (missing T4 shell self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T5_LINE_PREFIXES if p not in t5_seen]
        if missing:
            print('ui_smoke: FAIL (missing T5 filelist self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T6_LINE_PREFIXES if p not in t6_seen]
        if missing:
            print('ui_smoke: FAIL (missing T6 preview canvas self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T7_LINE_PREFIXES if p not in t7_seen]
        if missing:
            print('ui_smoke: FAIL (missing T7 meta-page self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T8_LINE_PREFIXES if p not in t8_seen]
        if missing:
            print('ui_smoke: FAIL (missing T8 output-page self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T9_LINE_PREFIXES if p not in t9_seen]
        if missing:
            print('ui_smoke: FAIL (missing T9 run-page self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T10_LINE_PREFIXES if p not in t10_seen]
        if missing:
            print('ui_smoke: FAIL (missing T10 settings-dialog self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T11_LINE_PREFIXES if p not in t11_seen]
        if missing:
            print('ui_smoke: FAIL (missing T11 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T11_M5_LINE_PREFIXES if p not in m5t11_seen]
        if missing:
            print('ui_smoke: FAIL (missing M5-T11 theme-persistence self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T13_LINE_PREFIXES if p not in t13_seen]
        if missing:
            print('ui_smoke: FAIL (missing M5-T13 acceptance self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in T14_LINE_PREFIXES if p not in t14_seen]
        if missing:
            print('ui_smoke: FAIL (missing T14 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in T15_LINE_PREFIXES if p not in t15_seen]
        if missing:
            print('ui_smoke: FAIL (missing T15 self-check lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in GUI_R2_LINE_PREFIXES if p not in gui_r2_seen]
        if missing:
            print('ui_smoke: FAIL (missing GUI-R2 regression lines: {})'.format(', '.join(missing)),
                  file=sys.stderr)
            return 1
        missing = [p for p in M6T9_LINE_PREFIXES if p not in m6t9_seen]
        if missing:
            print('ui_smoke: FAIL (missing M6-T9 meta-loop self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in M6T13_LINE_PREFIXES if p not in m6t13_seen]
        if missing:
            print('ui_smoke: FAIL (missing M6-T13 map-expand self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in G1_LINE_PREFIXES if p not in g1_seen]
        if missing:
            print('ui_smoke: FAIL (missing G1 probe self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        missing = [p for p in LAYOUT_AUDIT_LINE_PREFIXES if p not in layout_audit_seen]
        if missing:
            print('ui_smoke: FAIL (missing layout-audit visual-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        # 状态机修复批（需求 4；只增）：两条新增自检行齐备 + 取值断言（防断言被静默跳过）。
        missing = [p for p in STATE_FIX_LINE_PREFIXES if p not in state_fix_seen]
        if missing:
            print('ui_smoke: FAIL (missing state-machine fix self-check lines: {})'.format(
                ', '.join(missing)), file=sys.stderr)
            return 1
        if (RUN_TERMINAL_LATE_OK_RE.search(run_terminal_late_line) is None
                or RUN_TERMINAL_LATE_G_RE.search(run_terminal_late_line) is None
                or RUN_TERMINAL_LATE_O_RE.search(run_terminal_late_line) is None):
            print('ui_smoke: FAIL (S-1 迟到事件覆写终态回归（期望 ok=1 g-late="已完成" '
                  'o-late="已完成"）: {})'.format(run_terminal_late_line), file=sys.stderr)
            return 1
        if (RUN_CANCEL_NOOP_OK_RE.search(run_cancel_noop_line) is None
                or RUN_CANCEL_NOOP_PILL_RE.search(run_cancel_noop_line) is None
                or RUN_CANCEL_NOOP_VIS_RE.search(run_cancel_noop_line) is None):
            print('ui_smoke: FAIL (S-2 取消零效果结束态回归（期望 ok=1 pill="已完成" '
                  'cancel-visible=0）: {})'.format(run_cancel_noop_line), file=sys.stderr)
            return 1
        if (RUN_RERUN_CLEAN_OK_RE.search(run_rerun_clean_line) is None
                or RUN_RERUN_CLEAN_FIRST_RE.search(run_rerun_clean_line) is None
                or RUN_RERUN_CLEAN_TIME_RE.search(run_rerun_clean_line) is None
                or RUN_RERUN_CLEAN_PILL_RE.search(run_rerun_clean_line) is None):
            print('ui_smoke: FAIL (重跑残留回归（期望 ok=1 first-score="92.31" '
                  'rerun-time="—" rerun-pill="排队"）: {})'.format(run_rerun_clean_line),
                  file=sys.stderr)
            return 1
        if (RUN_RESTART_OK_RE.search(run_restart_line) is None
                or RUN_RESTART_PILL_RE.search(run_restart_line) is None
                or RUN_RESTART_CANCEL_RE.search(run_restart_line) is None
                or RUN_RESTART_PERCENT_RE.search(run_restart_line) is None):
            print('ui_smoke: FAIL (开始→取消→再开始回归（期望 ok=1 restarted-pill="进行中" '
                  'cancel-visible=1 percent="0%"）: {})'.format(run_restart_line), file=sys.stderr)
            return 1
        # M8-W2（需求3，只增/不放宽）：评分列 —— 行齐备 + 文本/取值断言（C12′）。
        if run_score_line is None:
            print('ui_smoke: FAIL (missing M8-W2 run-score self-check line: {})'.format(
                RUN_SCORE_LINE_PREFIX), file=sys.stderr)
            return 1
        if (RUN_SCORE_BAD_RE.search(run_score_line) is None
                or 'off_blank=1' not in run_score_line
                or 'ok_row=1' not in run_score_line
                or 'col_align=1' not in run_score_line
                or 'header=1' not in run_score_line
                or 'tiers=4' not in run_score_line
                or 'group_dash=1' not in run_score_line):
            print('ui_smoke: FAIL (M8-W2 run-score self-check failed: {})'.format(
                run_score_line), file=sys.stderr)
            return 1
        if RUN_SCORE_TEXT_RE.search(run_score_line) is None:
            print('ui_smoke: FAIL (M8-W2 run-score 分数文本格式不符（期望 '
                  '"<两位小数>"）: {})'.format(run_score_line),
                  file=sys.stderr)
            return 1
        if RUN_SCORE_DASH_RE.search(run_score_line) is None:
            print('ui_smoke: FAIL (M8-W2 run-score Unavailable 形态不符（期望 "—"）: {})'.format(
                run_score_line), file=sys.stderr)
            return 1
        # M8-W2（需求2，只增）：行尾勾选框几何 —— 行齐备 + 取值断言（防 C++ 断言被静默跳过）。
        if param_enable_geom_line is None:
            print('ui_smoke: FAIL (missing M8-W2 param-enable-geom self-check line: {})'.format(
                PARAM_ENABLE_GEOM_LINE_PREFIX), file=sys.stderr)
            return 1
        pe_rows = PARAM_ENABLE_GEOM_ROWS_RE.search(param_enable_geom_line)
        pe_tail = PARAM_ENABLE_GEOM_TAIL_RE.search(param_enable_geom_line)
        pe_label = PARAM_ENABLE_GEOM_LABEL_RE.search(param_enable_geom_line)
        pe_editor = PARAM_ENABLE_GEOM_EDITOR_RE.search(param_enable_geom_line)
        pe_order = PARAM_ENABLE_GEOM_ORDER_RE.search(param_enable_geom_line)
        if (PARAM_ENABLE_GEOM_BAD_RE.search(param_enable_geom_line) is None
                or pe_rows is None or int(pe_rows.group(1)) < 8
                or pe_tail is None or pe_tail.group(1) != pe_rows.group(1)
                or pe_label is None or pe_label.group(1) != pe_rows.group(1)
                or pe_editor is None or pe_editor.group(1) != pe_rows.group(1)
                or pe_order is None or pe_order.group(1) != pe_rows.group(1)
                or PARAM_ENABLE_GEOM_CLIP_RE.search(param_enable_geom_line) is None
                or PARAM_ENABLE_GEOM_LONG_RE.search(param_enable_geom_line) is None):
            print('ui_smoke: FAIL (M8-W2 param-enable-geom self-check failed: {})'.format(
                param_enable_geom_line), file=sys.stderr)
            return 1
        # M10-W1 / W1-A3（C10/C11，只增）：运行页列布局探针 —— 行齐备 + 取值断言
        # （fit / name-w≥150 / name-w520≥80 / no-speed / ratio-tip / cols-ok / long-elide）。
        if run_geom_line is None:
            print('ui_smoke: FAIL (missing M10-W1 run-geom self-check line: {})'.format(
                RUN_GEOM_LINE_PREFIX), file=sys.stderr)
            return 1
        rg_name_w = RUN_GEOM_NAME_W_RE.search(run_geom_line)
        rg_name_w520 = RUN_GEOM_NAME_W520_RE.search(run_geom_line)
        if (RUN_GEOM_BAD_RE.search(run_geom_line) is None
                or RUN_GEOM_FIT_RE.search(run_geom_line) is None
                or rg_name_w is None or int(rg_name_w.group(1)) < 150
                or rg_name_w520 is None or int(rg_name_w520.group(1)) < 80
                or RUN_GEOM_NO_SPEED_RE.search(run_geom_line) is None
                or RUN_GEOM_RATIO_TIP_RE.search(run_geom_line) is None
                or RUN_GEOM_COLS_RE.search(run_geom_line) is None
                or RUN_GEOM_LONG_ELIDE_RE.search(run_geom_line) is None
                or RUN_GEOM_LATE_TERMINAL_RE.search(run_geom_line) is None
                or RUN_GEOM_CANCEL_NOOP_RE.search(run_geom_line) is None):
            print('ui_smoke: FAIL (M10-W1 run-geom 列布局自检失败（期望 bad=0 fit=1 '
                  'name-w>=150 name-w520>=80 no-speed=1 ratio-tip=1 cols-ok=1 '
                  'long-elide=1 late-terminal=1 cancel-noop=1）: {})'.format(run_geom_line),
                  file=sys.stderr)
            return 1
        # M10-W1 / W1-A5（T9a；C12/C15，只增）：运行页状态映射 + 双通道 flush 取值断言
        if run_status_map_line is None:
            print('ui_smoke: FAIL (missing M10-W1 run-status-map self-check line: {})'.format(
                RUN_STATUS_MAP_LINE_PREFIX), file=sys.stderr)
            return 1
        if (RUN_STATUS_MAP_OK_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_G_ENC_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_O_SCORE_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_STRIPED_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_O_WRITE_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_O_DONE_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_DONE_SCORE_RE.search(run_status_map_line) is None
                or RUN_STATUS_MAP_O_SKIP_RE.search(run_status_map_line) is None):
            print('ui_smoke: FAIL (M10-W1 run-status-map 状态映射自检失败（期望 ok=1 '
                  'g-enc="编码中 0/2" o-score="评分中" striped=1 o-write="写入" '
                  'o-done="已完成" done-score="" o-skip="跳过"）: {})'.format(
                      run_status_map_line), file=sys.stderr)
            return 1
        if flush_state_line is None:
            print('ui_smoke: FAIL (missing M10-W1 flush-state self-check line: {})'.format(
                FLUSH_STATE_LINE_PREFIX), file=sys.stderr)
            return 1
        fs_rows = FLUSH_STATE_ROWS_RE.search(flush_state_line)
        if (FLUSH_STATE_OK_RE.search(flush_state_line) is None
                or fs_rows is None or int(fs_rows.group(1)) != 1):
            print('ui_smoke: FAIL (M10-W1 flush-state 双通道/dirty-row 自检失败（期望 '
                  'state=1 rows=1）: {})'.format(flush_state_line), file=sys.stderr)
            return 1
        # M10-W3-T2（C23 §8.5 / C24 §9，只增）：格式预设行 + 只读自适应面 —— 行齐备 + 取值断言。
        if format_preset_line is None:
            print('ui_smoke: FAIL (missing M10-W3-T2 format-preset self-check line: {})'.format(
                FORMAT_PRESET_LINE_PREFIX), file=sys.stderr)
            return 1
        fp_checked = FORMAT_PRESET_CHECKED_RE.search(format_preset_line)
        fp_missing = [field for field in FORMAT_PRESET_GROUP_FIELDS
                      if re.search(r'\b' + re.escape(field) + r'=1\b', format_preset_line) is None]
        # M12：断言组从 27 → 37（工厂数据文件/default 语义/需求 3/4/5 机械验收）；下限 **只升不降**
        # （不放宽既有断言：旧下限 10 已由本批提到 35）。0.4.2 / A13：+8 组（来源显式化/绑定/
        # 已修改）→ 43；下限只升不降。
        if (FORMAT_PRESET_BAD_RE.search(format_preset_line) is None
                or fp_checked is None or int(fp_checked.group(1)) < 43
                or fp_missing):
            print('ui_smoke: FAIL (M10-W3-T2 format-preset 自检失败（期望 bad=0 checked>=43 '
                  '且各字段 =1；缺/红: {}）: {})'.format(
                      ', '.join(fp_missing) if fp_missing else '-', format_preset_line),
                  file=sys.stderr)
            return 1
        print('UI-SMOKE pass')
        return 0
    finally:
        try:
            os.unlink(log_path)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
