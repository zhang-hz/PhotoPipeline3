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
"""

import os
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
)

# M5-T8 自检行（前缀匹配；缺任一 → FAIL）：输出页四卡组几何（§7.2 逐行 + R1–R4 + 修 2-05…2-42）
T8_LINE_PREFIXES = (
    'UI-SMOKE output-geom:',    # 磁贴 2×4 零形变 / 六行一义 pitch 28 / spin 88·160 / 折叠行 +
                                # 子分组 / 互斥置灰 / 模板示例行 / 中性计数胶囊 / 按钮 26 / 页签
                                # 格式名·位深 / 预设 placeholder
)

# M5-T9 自检行（前缀匹配；缺任一 → FAIL）：运行页三卡组几何/语义（§7.3/R1–R4/K4 + 修 3-03…3-16）
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
        print('UI-SMOKE pass')
        return 0
    finally:
        try:
            os.unlink(log_path)
        except OSError:
            pass


if __name__ == '__main__':
    sys.exit(main())
