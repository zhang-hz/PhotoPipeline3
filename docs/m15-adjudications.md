# M15 = 0.4.3 正式版裁定与追认记录（2026-11-04，Lead 收口）

> 背景：0.4.2 正式版发行后用户反馈 6 项（HEIC 无损模式 / 元数据页多选「多个值」框界面异常 /
> 单击「多个值」框闪退 / JXL 编码效率严重退化（600s+）/ 部分 Windows 版本打开后卡死无响应 /
> 导入含 XMP 的 HEIC 闪退），本轮修复批**升版本为 0.4.3 正式版**发布
> （`--version` = `PhotoPipeline 0.4.3`；tag `v0.4.3`；Release 由 `release.yml` 构建发布）。
> 体例同 `docs/m14-adjudications.md`：# | 裁定 | 理由 | 实施归属 | 状态。
> 权威链：AGENTS.md > 本文件（本轮裁定）> 长期设计文档。判据意义变更一律登记；断言不得放宽。

## 1. 冻结面加性（「加性 + 追认」纪律，AGENTS §5.1）

| # | 冻结面 | 裁定 | 理由 | 实施归属 | 状态 |
|---|---|---|---|---|---|
| A1 | `core/pipeline.h`（`RunScope`） | 结构体**末尾**加性追加 `std::vector<int> encode_threads_per_output;`（默认空 = 全部 target 用既有 `encode_threads` = 0.4.2 旧口径**逐字不变**；既有字段名/顺序/语义零改动） | 修复 JXL 退化：多输出并行时按编码器能力逐 output 下发内部线程数 E（0.4.2 名义均分把 JXL 从 16 压到 8/4/2）；冻结面只走末尾追加（先例 M10-A20 / M14-A1） | jxl-perf（task-3） | 已落地（test_thread_plan 26400 matrix / 6560 contract 全绿；单输出产物 SHA 前后相同） |
| A2 | `core/thread_plan.h`（新模块，非冻结面） | `ThreadPlan` 末尾追加 `encode_threads_per_output`；新增纯函数 `allocate_output_threads()`（每路保底 1、heavy 编码器分走余下预算；全 flat 退化旧值）。**不变式零放宽**：任意 P 路并发子集 Σ有效线程 ≤ 每文件预算、W×P×E≤T 旧断言全保留（只增） | 同 A1；单输出 / P==1 / 深队列路径向量为空 ⇒ 与 0.4.2 逐字一致 | jxl-perf（task-3） | 已落地 |

## 2. 用户可见语义变更（用户需求直接导致）

| # | 变更 | 理由（用户原文 / 需求） | 实施归属 | 状态 |
|---|---|---|---|---|
| A3 | **HEIC 速率控制下拉 = 质量 / CRF / QP / 无损 四档**（第 4 档与既有「无损编码」勾选框**同源**，唯一真值 = `selection.lossless`；无损档下 quality/crf/qp 不进 `values()`，切回有损档自动退出）。**无损下色度采样 / 编码档位 / Level 仍可配置**：删除 G3 的 chroma Lock=444、enc_heif 无条件强制 444、page_output 强制改写/仅 444 可选；无损 grid 路径按所选 chroma 推导色度面。无损 + 非 444 时**如实警告**「YCbCr 码流无损 ≠ 像素级无损」（规则 warn + 编码器中文 log + 档位注记 tooltip），不静默假装像素无损 | 用户需求 #1：「heic 提供无损选项（编码无损，作为质量/CRF/QP 之外的第四种模式），同时 profile、色度采样、level 等还是可以配置」 | heic-lossless（task-1） | 已落地（单测 6/6；444 hvcC chroma=3 可解码；420 成功+chroma=1+中文 warning；无损下显式 Main10 → idc=2 证明档位下发） |
| A4 | **HEIC 无损下 Level 的诚实降级**：`x265:level-idc` 仍可配置但无损下**不下发**（规则 warn + 编码器中文 warning；产物 PTL Level 由 x265 固定写 8.5=level_idc 255）。不把「可配置项」变成「必失败项」（x265 `enforceLevel` 对「恒定 QP + decoder level」硬失败，`level.cpp:443-445`） | 用户需求 #1 的 level 可配置面与后端硬约束冲突时取「如实告警、不静默失败」；登记为本轮唯一需求偏差 | heic-lossless（task-1） | 已落地（test_enc_oiio ⑤ 断言：无损+level31 → 成功且 hvcC level=255 + 两条 warning） |
| A5 | **多选 mixed「多个值」显示修复**：时间卡「时区语义」QComboBox 在 mixed 态**真正显示「多个值」**（新增 `meta::MixedComboBox`：仅 paint 覆盖 `initStyleOption.currentText`，**零 item 模型/索引改动**；常态逐像素委托基类）；mixed 提交统一到全部选中文件 | 用户反馈 #2：「多选后右侧显示多个值，"多个值"框界面异常」——0.4.2 只给 combo 加 ppMixed 虚线边框、文本仍显示首值 | meta-ui（task-2） | 已落地（UI 探针 `mixed-combo-display`/`mixed-combo-activate` bad=0；meta-selection 19→34 断言全绿） |
| A6 | **启动期显示色彩管理改为「先 sRGB、后台异步换档」**：`PreviewPanel` 构造期不再同步调用 `system_display_profile()`（GetDC/GetICMProfileW + ICC 读取 + lcms2 解析全部移出 GUI 线程），立即注入 sRGB 回退；后台 `std::thread` + shared_ptr probe、GUI QTimer 轮询、generation 丢弃过期、5s 软超时 + 30s 硬上限、析构不 join；屏幕变更同走异步。启动后 ~0.6s 内预览按 sRGB 渲染、profile 到达后重取当前图（可见一次颜色校正，属预期语义变化）。offscreen / CI 恒 sRGB 且不启后台线程（冻结截图集逐字不变） | 用户反馈 #5：「0.4.2 在部分 Windows 版本上，软件打开后鼠标转圈，无响应」——0.4.1→0.4.2 唯一新增启动期阻塞系统调用即此路径 | win-startup（task-4） | 已落地（`PP_DISPLAY_ICC_DELAY_MS=5000` 模拟：shown=1399ms < 2s、窗口 WM_NULL 响应 0ms、后台 5011ms 后换档日志；`delay=8000` 走 5s 软超时且窗口可响应；test_color 1542 checks/0 failed） |

## 3. 探针 / 门禁机械换锚（判据含义不变，断言只增不减）

| # | 探针/门禁 | 换锚内容 | 理由 | 状态 |
|---|---|---|---|---|
| A7 | `--ui-smoke` folds 段（HEIF 无损 chroma） | 旧断言「无损强制 chroma=444 / 仅 444 可选」→ 新语义「无损下 chroma 三项全可选、当前值不被改写；非 444 时 `pp-profile-note-heif` tooltip 含「色度在编码前被下采样」；444 时警告消退」。旧断言是**用户需求变更直接导致**，非放宽 | A3 语义变更的机械化落点 | 已落地（`folds: checked=11 bad=0`） |
| A8 | `--ui-smoke` m8-params 段（HEIF RC 下拉） | 旧「count==3 / 无损⇒RC 行整组隐藏」→ 新「count==4 且第 4 项=无损 / 勾无损⇒RC 行仍可见且 currentIndex==3、quality/crf/qp 不下发 / 下拉切回质量⇒勾选框同步取消且 quality 恢复下发」 | A3 语义变更的机械化落点 | 已落地（`m8-params: checked=107 bad=0`） |
| A9 | `--ui-smoke` meta-selection 段 | 断言只增：19 → 34（新增 mixed 交互回归 15 条：mixed combo 真显示「多个值」/ mixed 提交覆盖全部选中 / spin 箭头/focusOut/pending 窗口/连续点击等；`PP_MIXED_REPRO=1` dev 取证快路径不改变默认走查） | 用户反馈 #2/#3 的机械化回归面；冻结截图集与末行未动 | 已落地（`meta-selection: checked=34 bad=0`） |
| A10 | 日志面（启动期 preset timing） | `refresh_preset_card` 分项计时**仅 `PP_STARTUP_TIMING` 下**输出（非诊断模式零 info/warn 行；>50ms warn 也仅诊断模式） | 不新增日志面 ⇒ 回归基线 zero diff 不受影响 | 已落地（regression normalized 2567 = baseline 2567，zero diff） |
| A11 | `platform/mica.cpp`（Windows 版本兼容） | `DWMWA_SYSTEMBACKDROP_TYPE` 增加 `RtlGetVersion ≥ Win11 22H2(build 22621)` 门控（取不到版本保守不调；失败静默）。Win11 22H2+ 调用路径/视觉逐字不变；旧系统少一次无效 DWM 调用。启动日志如实记录 `os_build / qt / platform`（无新系统调用：复用 Qt 缓存值） | 用户反馈 #5「提高 GUI 版本兼容性」；不改变任何既有视觉/行为判据 | 已落地（真实平台 ui-smoke 通过；offscreen 不涉及） |

## 4. 诊断开关（默认关闭；生产/GUI/CI 从不设置）

| 开关 | 语义 | 用途 |
|---|---|---|
| `PP_THREAD_PLAN_LEGACY=1` | 逐输出公平分配退化为 0.4.2 名义均分（同二进制 A/B） | JXL 退化根因取证（默认关闭） |
| `PP_DISPLAY_ICC_DELAY_MS=<ms>` | 显示 profile 获取模拟慢系统（上限 60s） | 启动卡死防御性验证（默认不设置 = 行为不变） |
| `PP_MIXED_REPRO=1` | `--ui-smoke` 只跑元数据 mixed 交互复现段 | mixed 交互取证快路径（默认不设置） |

## 5. 版本与发行口径

| # | 事项 | 裁定 | 状态 |
|---|---|---|---|
| A12 | 版本号与发布 | **升版本为 0.4.3 正式版发布**（原「0.4.2 不升版本 hotfix」口径作废，用户 2026-11-04 指示「发布 0.4.3」）：`project(PhotoPipeline VERSION 0.4.3)`、`vcpkg.json` 0.4.3、`--version` = `PhotoPipeline 0.4.3`；annotated tag `v0.4.3` → 发行树 commit；`release.yml`（tag 触发）四资产发布。回归基线 version 行**机械换锚**（`regression.py --update`，归因 = 仅版本行，语义零变化） | 已落地（tag/Release 读数待发布后回填） |
| A13 | dist 产物 | `dist/PhotoPipeline-0.4.3-win64.zip`（release 预设 clean-first 构建 + `tools/make_winzip.py` 五门禁：闭包/来源/结构/许可/烟测 E-1/E-2/E-3 + 工厂预设 + zip 内复核） | 已落地（指纹见 `docs/m15-report.md` §2） |

## 5b. 补充裁定：Exiv2 并发崩溃修复（2026-11-04 21:34 用户实测闪退，反馈 #6）

| # | 事项 | 裁定 | 理由（证据链） | 状态 |
|---|---|---|---|---|
| A14 | 第三方 Exiv2 运行时保护 | 新增 `src/core/metadata_runtime.h`（**新文件，冻结面零改动**）：`ensure_exif_metadata_runtime()`（`std::call_once` 完成 `Exiv2::XmpParser::initialize()`，静态析构 terminate 成对）+ `exif_metadata_mutex()`（进程级递归锁）。在**所有 Exiv2 I/O / XMP·Exif 解析入口**持锁：`metadata.cpp` 的 `read_metadata`/`read_heif_metadata_fallback`/`exiv2_apply`/`key_exif_landed`/`make_payloads`；`thumbs.cpp` 的 `try_embedded_preview`/`try_embedded_preview_image`（锁内只取预览字节，OIIO 解码/缩放移出锁外，像素热路径不被串行化）。**不改变任何读写语义/产物字节**（纯串行化 + 初始化顺序） | 用户实测：导入含 XMP 的 HEIC 后 WER `photopipeline.exe` 崩溃于 **exiv2.dll +0x7c9cf**（c0000005）；反汇编该偏移 = Adobe XMP `XML_Node`/`std::map<int,std::string>` 内部函数首指令 `mov r14,[rcx]`（null deref）。Exiv2 v0.28.8 源码（vcpkg buildtrees）`src/xmp.cpp:512` 的 `XmpParser::initialize()` 是**裸 static bool 无锁懒初始化**，`decode()` 在未初始化时自动调用它；GUI 导入时缩略图池(2)/预览池(2)/元数据探针池并发首次读取含 XMP 文件 ⇒ 多个线程同时进入 initialize → Adobe XMP 内部状态未完成初始化 → 崩溃。本机无法直接复现（竞态），故以「崩溃模块+偏移反汇编 + 依赖源码行为 + 新增并发回归探针」作为根因证据链 | 已落地（`test_metadata` 首置 8 线程×8 次并发 XMP 读取探针 reads_ok=64；全量 ctest 52/52 通过） |

## 6. 如实未决（不宣称已解决）

| # | 事项 | 现状 |
|---|---|---|
| U1 | 用户反馈的「单击多个值框闪退」 | **本机未复现**：真实 Windows 平台真实事件链覆盖 25+ 交互点 × 3 轮（含 spin 上下箭头/focusOut/时区弹层 activated/partial 开关/pending 窗口/连续点击）进程全程存活、无 WER 事件。已修确认的界面异常 + 3 处防御性收口（空指针/越界守卫）+ 15 条机械探针；未复现条件如实登记，建议用户侧 WER LocalDumps / nightly ASan 事后取证 |
| U2 | 「部分 Windows 版本」启动卡死的具体版本/驱动/网络 profile 路径 | 本机 Win11 26100 `GetICMProfileW` ~2ms 无法复现；根因归属基于「0.4.1→0.4.2 唯一新增启动阻塞调用」排除法 + 异步化后即使系统调用慢/挂也不阻塞 GUI（5s/8s 模拟实测）。未做真机取证 |
| U3 | x265 无损 Level 不下发 | A4 登记为需求偏差（后端硬约束）；产物 Level 恒 8.5 已如实告警 |
| U4 | AVIF 无损 chroma Lock=444 | 本轮只按用户需求改 HEIF；AVIF 维持 0.4.2 语义（未动 G3 lock），AVIF 矩阵未复跑 |
| U5 | JXL A/B 的 wall 读数 | 修复期 4 agent 并发构建导致 wall 噪声大；**JXL 单轮 encode_ms 交错中位 2.4x** 为可引用读数（混合 3 输出 12320ms → 5096ms），静机 wall 复测建议见报告 §4 |
| U6 | `theme-equiv` offscreen 单次卡宽（651 vs 661） | meta-ui 轮次出现一次（真实平台不复现），集成树全量 ctest/ui-smoke 未复现；按既有 A30 去抖口径登记为环境时序抖动观察项，未改判据 |
| U7 | Exiv2 并发崩溃（A14）本机无法直接复现 | 崩溃发生在用户机器（race）；本机用 8 线程并发读 XMP fixture 的新探针 + 原 GUI 导入流程均未复现（竞态窗口小）。**根因归属 = 崩溃偏移反汇编（Adobe XMP XML_Node 内部空指针）+ Exiv2 0.28.8 源码非线程安全 initialize 的机械事实**，修复 = 串行化 + call_once（消除竞争条件本身，非 try/catch 掩盖）。未做用户机器 WER dump 复核（dump 未落盘）；若仍复现建议用户侧开 WER LocalDumps 取全栈 |
