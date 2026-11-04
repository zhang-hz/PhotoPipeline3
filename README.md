# PhotoPipeline

批量像素级转码器 + 元数据手术台（batch pixel-level transcoder with metadata surgery），GPL-3.0-or-later。

M0 交付仓库骨架、冻结接口、链接探针与金标语料。**M1a 交付全部编码引擎**：core 基础设施（logger / fsops / 像素预算 / 参数引擎 / 预设）、解码层、色彩层、元数据层、8 个编码器、调度器与 `--dev` 命令行 harness。**M1b 交付完整桌面 UI**：三页主窗口（元数据规则 / 输出配置 / 运行监控）、异步缩略图文件列表、内嵌地图选点、参数表单引擎、单文件 EXIF/XMP 编辑器、设置与预设对话框，以及 `--ui-smoke` 无头走查。**M2 完成 Linux 发行收口**：`--version` 版本单源、AppImage 打包（含离线打包与许可随附）、金样断言级 16 对、回归基线、ASan/UBSan/TSan 清扫，以及持续集成——`build-test.yml` 的**四个 job**（`linux` / `ui-smoke` / `appimage` / `cache-gc`）与缓存播种 workflow `warm-cache.yml`。**M3 完成 Windows 移植与双平台发行**（0.2.0）：UTF-8 全局语义、Mica 深色标题栏、`make_winzip.py` 五项门禁、tag 触发发行自动化。

**M10（0.4.0.alpha1 预发布）= 性能与运行页 + 参数面 + 格式预设与自适应**：① **性能**——评分预算拆分（大图 + 评分不再被预算打穿，`need_bytes` 8.832 GB → **1.152 GB**）、评分色彩归一快速内核（**8.49 s → 0.42 s，20×**）、SSIMULACRA 2 流式化（**7.6 B/px**，约旧实现 5%）、双重 probe 消除（22.8 MP HEIC 首个 `Probing → Decoding` **2,902.9 → 1,447.8 ms**）、**独立评分池 + 异步评分**、**同文件多格式并行**（1 文件 4 格式 **1.31×**，四格式 SHA256 并行==串行）、**逐输出事件**。② **参数面**——HEIF **grid/tile** 配置行、**still 档位 16–21**（hvcC 回读断言；16 实测不可用 ⇒ 置灰）、**tune 7 项**、RC 数值行上移核心区、**JXL 治理 43→31 键**。③ **格式预设与自适应（新）**——格式卡可变快照存/导入导出；**自适应策略默认关闭**，出厂 5 条。详见 [docs/m10-report.md](docs/m10-report.md)、[docs/m10-adjudications.md](docs/m10-adjudications.md)。

**M9（0.3.6.alpha2 预发布）= JXL 有损修复 + HEIF/iPhone 兼容配置矩阵**：① **JXL 有损 VarDCT 修复**——有损 VarDCT 不再走 libjxl 非 XYB 缺陷路径（`uses_original_profile` 恒 false、XYB 编码；显式 true 明确报错），修复「distance 越小评分越差、体积暴涨」；实测 T1 16-bit keep `d2.0 2.04MB/80.81 → d0.2 12.66MB/92.38`（体积与分数双单调）。② **HEIF 真实档位/Level/grid 配置矩阵**——档位下拉扩为 12 个真实 HEVC 信号（Main Still idc3 / Main idc1 / Main10 idc2 / RExt 4:2:0·4:2:2·4:4:4 × 8/10/12 idc4）+ 自动；HEVC Level 合法性按 x265 4.3「8 像素补齐后 MaxLumaPs」预校验并给中文可操作错误；新增有损 HEIF grid（Apple 1024×576 基准 / 自动适配 Level；默认仅在显式 Level 装不下整图时自动分块），大图 + L3.1 从「失败」变为合法可解码 grid；编码后 hvcC 回读校验档位/Level/位深/色度。③ **穷举矩阵验证器** `tools/heif_matrix_check.py`：1013 例 = **pass 697 / fail 0 / expected-fail 316 / unexpected 0**，697 个合法组合全部结构断言 + 解码通过。详见 [docs/m9-report.md](docs/m9-report.md)、[docs/m9-adjudications.md](docs/m9-adjudications.md)。

**M8（0.3.6.alpha1 首个发行件）= 参数面全暴露 + SSIMULACRA 2 逐输出评分**：① HEIF/AVIF/JXL 参数面扩展——精选目录 **116 行**（x265 40 / libaom 57 / SVT 3 / JXL 16；三态启用、只读/禁用、RC 模式显式下拉、联动规则与运行前预检；清单外透传键 = 预检错误）；② **SSIMULACRA 2 逐输出评分**——GUI 默认开、8 种输出全覆盖，运行页任务表新增「评分」列（逐输出行显示两位小数，
文件组行显示 `—`；M8 初版为输出行下隐藏子行，本轮改为列），颜色阈值见下文「M8 参数面与评分」；算法为 libjxl 0.12 官方源码 + 兼容层（BSD-3），参考=编码器实际收到的量化输入（真无损=100）。详见 [docs/m8-report.md](docs/m8-report.md)、[docs/m8-encoder-params-and-ssimulacra2-design.md](docs/m8-encoder-params-and-ssimulacra2-design.md)。


**M4 = 0.3.0**：从"能用的转码器"到**多格式批处理工作台**——九项需求全量落地：① 常驻**输入预览** + 手动**分类**（热键打标/持久化）+ 文件列表**勾选**与 EXIF 自动分组视图；
② 元数据卡**生效值显示**（时间/GPS「原 → 生效」+ 关键信息卡，与写路径同源纯函数）；
③ **多格式输出**（1 输入 → N 格式，解码仅一次；输出路径模板 `$format/$dir/$file/$name/$ext`；
逐输出冲突解析）；
④ **逐文件进度**（JPEG/JXL/PNG/TIFF 真实行级 + WebP/HEIF/AVIF 合成进度斜纹标识；逐输出行）；
⑤ **自适应并行**（交错启动可配置 + `alloc_threads` 线程预算；读数为**估算口径**——成立的是
`active ≤ W ≤ T` 与 `ΣE ≤ T`，SynthPump/x265 库内线程不入账，见 CHANGELOG [0.3.3]「已知」）；
⑥ **GUI 重构**（无边框窗口 + 自绘 caption 三钮、三栏骨架：文件/分类 + 预览 + 三页内容区、
明暗双主题 tokens）；
⑦ **CI 工业化**（pr-fast / main-full / nightly / release 四道，job 与整轮双 ≤20min 硬约束、
clang-format 硬门禁 + clang-tidy 棘轮 + sanitizer 三路）；
⑧ **依赖一把全升**（Qt 6.11.2 / OIIO 3.1.17 / libjxl 0.12 / exiv2 0.28.9 / x265 4.3 / SVT-AV1 4.2 …）；
⑨ **AVX2 性能基线**（x86-64-v3 + 5 对自研热路径 SIMD：flatten / quantize / transpose / downscale / interleave）。
金样断言级 **20 对**、ctest 条目以 `ctest -N` 实测为准（2026-10-27 实测 **35 条**（release）/
**36 条**（release-dev，含 UI 冒烟））、UI 冒烟冻结行 `UI-SMOKE OK shots=12 pages=3`。

**M5（GUI 刷新 v6，折入 0.3.0，版本号不变）**：视觉体系与排版全量重做——单源规范
[docs/qt-gui-design-spec.md](docs/qt-gui-design-spec.md)（v6）+ 视觉终稿 [docs/mockups-v2/](docs/mockups-v2/)
（4 页 × 亮暗 8 张）。要点：令牌单源 `src/ui/theme.h`；表单体系 `src/ui/formkit.*`（行 pitch 28 /
标签轨 80 / 字段宽只有 88·160·flex）；矢量图标集 `src/ui/icons.*`（**全界面无字形图标**）；
caption 单轨 46×32；壳层顶栏 44 + 状态栏 30；**栏宽固定轴 330 / 587**；文件列表行 58px +
分类面板 2 列网格；预览画布 `#16181d` 舞台；元数据/输出/运行三页与设置弹窗 v6 化；
**明暗主题可手动切换并跨进程持久化**（三档 `ui_theme_mode`，见下表）。
**引擎零改动**（`src/core`/`src/codecs`/`src/decode` 未触碰，只做视觉与排版）。验收证据、§12 十七条
逐条探针、偏差与未决项见 [docs/m5-report.md](docs/m5-report.md)，开发期裁定见
[docs/m5-adjudications.md](docs/m5-adjudications.md)。

**0.4.3（当前版本，正式版）= M15：0.4.2 用户 6 项反馈修复批**：① **HEIC 无损模式** —— HEIF 速率控制
下拉由「质量/CRF/QP」扩为「质量/CRF/QP/**无损**」四档（与「无损编码」勾选框同源，唯一真值 = 选择项
`lossless`）；**无损下色度采样/编码档位/Level 仍可配置**（删除强制 4:4:4），非 4:4:4 时如实警告
「YCbCr 码流无损 ≠ 像素级无损」。② **多选「多个值」框界面异常** —— 时间卡时区下拉 mixed 态此前只换虚线
边框、文本仍显示首值，现真正显示「多个值」（`meta::MixedComboBox`，paint 级覆盖、零 item 模型改动）；
mixed 提交统一到全部选中文件。③ **单击「多个值」框闪退** —— 本机未复现（真实事件链 25+ 交互点 × 3 轮，
无 WER 事件），同批复核修 3 处防御性收口 + 15 条 mixed 交互机械探针。④ **JXL 编码效率严重退化（600s+）**
—— 根因 = 多输出规划器按「预算/P」名义均分线程，JXL/AVIF 被从 16 压到 8/4/2 再乘自适应 30 轮放大；
修复 = **逐输出线程公平分配**（heavy 编码器分走余下预算、flat 保底 1；`RunScope` 末尾加性
`encode_threads_per_output`）；单轮 A/B 混合 3 输出 JXL encode_ms 中位 **12320 → 5096 ms（≈2.4×）**，
产物 SHA256 逐对相同。⑤ **部分 Windows 启动卡死（转圈无响应）** —— 预览色彩管理的系统 profile 获取
从 GUI 线程构造期移出（构造期立即 sRGB、后台线程 + QTimer 轮询应用，5s 软超时/30s 硬上限）；
模拟 `PP_DISPLAY_ICC_DELAY_MS=5000` 实测 `shown=1399ms`（<2s）窗口可响应。⑥ **导入含 XMP 的 HEIC
闪退** —— 根因 = Exiv2 0.28.8 `XmpParser::initialize()` 非线程安全懒初始化，GUI 多池并发首读含 XMP
文件时竞态进入 Adobe XMP SDK 初始化 → 空指针崩溃（WER：exiv2.dll+0x7c9cf）；修复 = 新增
`core/metadata_runtime.h`（call_once 初始化 + 进程级递归锁串行化 Exiv2 I/O/解析入口，锁内只取预览字节、
OIIO 解码移出锁外）+ 并发 XMP 回归探针。版本单源 = `CMakeLists.txt`，`--version` =
**`PhotoPipeline 0.4.3`**；裁定 = [docs/m15-adjudications.md](docs/m15-adjudications.md)（A1–A14 / U1–U7），
读数与证据 = [docs/m15-report.md](docs/m15-report.md)。

**发行状态：0.4.3 正式版已发行**（tag **annotated `v0.4.3`**（tag object `64dcdf1a…`）→ commit
**`0dbf6e5`**；GitHub Release [PhotoPipeline 0.4.3](https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.3)，
release id `434190819`，prerelease=**False**，publishedAt **2026-11-04T16:48:39Z**；`release.yml` run
**41864935620 = success**（attempt 2，7/7 job；attempt 1 因 E-3 WIN-LAUNCH 在 runner 上
`timeout-no-visible-window` 失败，`rerun-failed-jobs` 重跑通过）；**4 资产**：`win64.zip` 34,319,466 B /
`91194f8f…4c5385`、`x86_64.AppImage` 52,783,608 B / `de2a9ad1…ffdd43`、`deps.txt` 2,439 B /
`e8c80b3d…407854d`、`SHA256SUMS.txt` 293 B / `1a0cc74b…f94582`；下载件 win64 解包 exe 实跑
`--version` = `PhotoPipeline 0.4.3`（zip 条目 118）；`main-full` run **41864930206 = success（10/10 job）**）。

**0.4.2（历史正式版）= M14 用户 8 条需求 + 真实债清零 + 目录清理**：① **元数据页逻辑重梳** ——
右侧修改严格只作用于**行选中**图片（单选=仅该张、多选=全部选中）；**无选中 = 空态 + 控件禁用 + 零写入**
（删除 0.4.1「无选中回退首个文件」错误路径）；多选右侧信息逐字段聚合：同值显示值、异值 **「多个值」** +
逐文件 tooltip（关键信息卡用每张照片实际元数据 probe，>8 张不再假 mixed）；过滤/分组隐藏选中项不再误写；
② **「批量 GPS 规则」概念退役**（GPS 坐标/海拔/方位/时间戳 = 选中图片编辑面，多选即批量）；③ **大预设重设计** ——
`outputs[i].source_preset` 加性来源注记、格式行来源显式化（不再假装高亮 default）、保存落点修复（写绑定
绝对路径）、dirty 指示、中文预设名、导入/导出、rules 覆盖确认；④ **元数据详细编辑器深查**（OK 返回与旧
override 合并零丢失；删除旧批量规则三态与「〔批〕」概念）；⑤ **预览窗格真实色彩管理** —— 显示 profile 获取
（Windows `GetICMProfile`）+ lcms2 变换到显示色域，宽色域源不再发灰（P3 编码红 (234,51,35) → 显示
(255,0,0)，中性灰不变）；⑥ **调度并行自适应** —— 新 `core/thread_plan` 按编码器特性 + CPU 实时利用率 EMA
动态决定 E（每文件编码线程）与 P（同文件多输出并行），深队列保守、浅队列放大、饱和收敛、迟滞防抖；
⑦ **真实债清零** —— hwy dispatch ODR 链接期 UB 根治（`pp_ssim2_hwy` 私有化 + bridge 转发，SSIMULACRA2
**逐位零漂移**）、ProgressMux/FileEvent 数据竞争根治（单线程 drain 队列）、地图 EventFilter UAF 修复、
MSVC ASan 纳入本机门禁；⑧ **目录清理** —— 清空 50.6GB `build/` 等生成垃圾，产品/脚本零预置构建树依赖。
版本单源 = `CMakeLists.txt`，`--version` = **`PhotoPipeline 0.4.2`**；契约 / 裁定 / 报告见
[docs/m14-tasks.md](docs/m14-tasks.md) / [docs/m14-adjudications.md](docs/m14-adjudications.md) /
[docs/0.4.2-report.md](docs/0.4.2-report.md)。

**发行状态：0.4.2 正式版已发行**（tag **annotated `v0.4.2`** → commit **`658dcc6`**；GitHub Release [PhotoPipeline 0.4.2](https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.2)，release id `431816449`，prerelease=**False**，publishedAt **2026-11-02T18:17:11Z**；`release.yml` run **41496852580 = success（7/7 job）**；**4 资产**：`win64.zip` 34,312,667 B / `909a0f94…d239f285`、`x86_64.AppImage` 52,799,992 B / `09585e07…b1e41ece`、`deps.txt` 2,439 B / `e8c80b3d…407854d`、`SHA256SUMS.txt` 293 B / `1dc2cabb…4281dde`；下载件 win64 解包 exe 实跑 `--version` = `PhotoPipeline 0.4.2`）。

**0.4.1（历史正式版）= M13 用户 4 条需求**：① **元数据页选择项级编辑** —— 右侧每项修改默认只作用于
**当前选中文件**（每文件独立 `MetadataOverride`）；多选 = 批量生效（字段级合并互不串扰）；多选值不一致时各面
明确显示 **「多个值」**（Δ spin / 时间与 GPS 输入 / 关键信息卡 / GPS 生效坐标 / 开关三态）；允许部分文件不开启
某类修改（三态开关 + 逐文件 override）；预设载入 = 应用到选中文件；「例外」语义 = 带元数据修改的文件。
② **隐私与文件时间卡新增「去除 XMP CreatorTool」**（删除 `xmp:CreatorTool`，清除 Photoshop 等生成工具签名）。
③ **修复 `Xmp.photoshop.DateCreated` 未随 UTC 偏移 / 时区语义刷新**（与 xmp:CreateDate/ModifyDate 同一条偏移
纯函数，显示 / 写入同源）。④ **GPS 选点自动海拔** —— 地图点选 / 搜索结果 / 联想 / 取地图中心后经 Open-Meteo DEM
自动取海拔回填（默认开、可关、400ms 去抖、失败静默降级、标注近似值）。版本单源 = `CMakeLists.txt:19-28`，
`--version` = **`PhotoPipeline 0.4.1`**；契约 / 裁定 / 报告见 [docs/m13-tasks.md](docs/m13-tasks.md) /
[docs/m13-adjudications.md](docs/m13-adjudications.md) / [docs/0.4.1-report.md](docs/0.4.1-report.md)。
**发行状态：0.4.1 正式版已发行**（tag **annotated `v0.4.1`** → commit **`d43b9db`**；GitHub Release
**`PhotoPipeline 0.4.1`** published **2026-11-01T17:32:19Z**（prerelease=**False** / Latest），URL
<https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.1>；**4 资产**：`win64.zip` 34,272,466 B /
`5efbe8fa…20b452`、`x86_64.AppImage` 52,697,592 B / `8100712f…0aaefe`、`deps.txt` 2,439 B / `e8c80b3d…07854d`、
`SHA256SUMS.txt` 293 B / `980fff8b…348182`；`release.yml` run **41330906366 = success**，7 job 全 success）。
**如实**：0.4.0 登记的 asan/tsan 两项真实
产品债（hwy ODR 链接期风险、ProgressMux/FileEvent 数据竞争）顺延 0.4.2；不宣称 nightly 通过、不造绿。

**0.4.0（历史正式版）= M10 + M11 迭代修复 + M12 用户 5 条反馈**：① **M10（性能/参数面/预设与自适应）**——
评分预算拆分（48 MP PNG + `--score` 由 `EXIT=1` 变 `EXIT=0`，`need_bytes` 8.832 GB → **1.152 GB**）、
评分色彩归一快速内核（**8.49 s → 0.42 s，20×**）、SSIMULACRA 2 流式化（23 MP 峰值额外内存 **7.6 B/px**）、
双重 probe 消除（22.8 MP HEIC 首个 `Probing → Decoding` **2,902.9 → 1,447.8 ms**）、**独立评分池 + 异步评分**、
**同文件多格式并行**（1 文件 4 格式 22.8 MP **1.31×**，四格式产物 SHA256 并行==串行）、**逐输出事件**（`OutputDone`）；
HEIF **grid/tile 配置行** + **still 档位 16–21**、**tune 7 项/RC 面**、**JXL 治理 43→31 键**；格式预设与自适应策略
（新文件类型 + 执行器）。② **M11 迭代修复**——HEIF tile 尺寸改由**目标 Level 的 MaxLumaPs + still 单图应用格式
Level 上界（L5.1）**推导（删除 1024×576 固定基准）、自适应 DSL v1.1（`down`/`up` 显式方向 + `breakpoint` 步长断点、
错误码 42→45）、自适应迭代日志（`adaptive plan/stage/round/summary`）、运行状态机修复（S-1…S-5）、
格式预设行 UI 收口。③ **M12 用户 5 条反馈**——**出厂预设外部化为仓库 JSON 数据文件**（`presets/formats/**`，11 条，
人类可直接改/复制；随发行包分发、首启**字节复制** seed 到数据目录）；**每格式 `default`**（HEIF/JXL 历史内建删除）；
**删除「未选择」态**（格式卡自动载入 default；default 不可删但可「保存」覆盖）；HEIF「Apple自适应」**CRF=20**；
**JPEG `default` 开启自适应视觉距离策略**（3.0 起 1–3 段步长 1、1 以下步长 0.1 精扫到 0.1，取达 90 分的最小体积），
JPEG distance UI 域同步放宽为 **0.1–3.0**（R-294）。**性能目标部分未达（如实）**：P4-1 metric 2,178.6 / encode
2,032.1 ms = **1.072×**、10-bit HEIC probe 1,635–1,670 ms、HEIC 深 probe 首事件 1.44–1.67 s（见 CHANGELOG「已知」）。
**发行状态：0.4.0 正式版已发行**（`--version` = `PhotoPipeline 0.4.0`；tag **annotated `v0.4.0` → commit `1b6fdbc`**；
GitHub Release **`PhotoPipeline 0.4.0`** published **2026-11-01T10:24:21Z**（prerelease=**False** / Latest），
URL <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.0>；**4 资产**：`win64.zip` 34,205,404 B /
`6fe7d1ca…c48ccc`、`x86_64.AppImage` 52,648,440 B / `40e3c082…5ce2e`、`deps.txt` 2,439 B、`SHA256SUMS.txt` 293 B；
`release.yml` run **41296584097 = success**）。逐条变更见 [CHANGELOG.md](CHANGELOG.md) `[0.4.0]`，发行报告/门禁与
CI 最终读数见 [docs/0.4.0-report.md](docs/0.4.0-report.md)。

**0.3.6.alpha2（历史预发布）= M9：JXL 有损修复 + HEIF/iPhone 兼容配置矩阵**：① **JXL 有损
VarDCT 修复**——有损 VarDCT 不再走 libjxl 非 XYB 缺陷路径（`uses_original_profile` 恒 false、XYB 编码；
显式 true 明确报错），修复「distance 越小评分越差、体积暴涨」；T1 16-bit keep 实测 `d2.0 2.04MB/80.81
→ d0.2 12.66MB/92.38`（体积与分数双单调）。② **HEIF 真实档位/Level/grid 配置矩阵**——档位下拉扩为
12 个真实 HEVC 信号（Main Still idc3 / Main idc1 / Main10 idc2 / RExt 4:2:0·4:2:2·4:4:4 × 8/10/12
idc4）+ 自动；HEVC Level 合法性按 x265 4.3「8 像素补齐后 MaxLumaPs」预校验并给中文可操作错误；新增
有损 HEIF grid（Apple 1024×576 基准 / 自动适配 Level；默认仅在显式 Level 装不下整图时自动分块），
大图 + L3.1 从「失败」变为合法可解码 grid；编码后 hvcC 回读校验档位/Level/位深/色度。③ **穷举矩阵
验证器** `tools/heif_matrix_check.py`：1013 例 = **pass 697 / fail 0 / expected-fail 316 /
unexpected 0**。**已发行**（tag `v0.3.6.alpha2`，GitHub Release 4 资产 + 源码归档，2026-10-31；
预发布 alpha，仅建议尝鲜/验证用）；逐条变更见 [CHANGELOG.md](CHANGELOG.md)
`[0.3.6.alpha2]`，验收证据见 [docs/0.3.6.alpha2-report.md](docs/0.3.6.alpha2-report.md)。

**0.3.6.alpha1（历史预发布）= M8 首个发行件**：① **参数面全暴露**——HEIF/AVIF/JXL 精选
目录 **116 行**（x265 40 / libaom 57 / SVT-AV1 3 只读 / JXL 16），三态启用/只读/禁用、RC 模式显式
下拉、数据驱动联动与运行前预检（错误清单 + 聚焦首错；清单外透传键 = 预检错误）；② **SSIMULACRA 2
逐输出评分**——GUI 默认开（设置 → 运行 →「质量评分」可关），8 种输出全评，运行页任务表
「评分」列逐输出行显示两位小数（文件组行显示 `—`），颜色阈值：**≥95 极好（ok/绿）/ ≥90 好（accent/蓝）/ ≥80 ok（warn/琥珀）/ <80 不 ok（err/红）**；评分算法为
libjxl 0.12 官方源码 + 兼容层（BSD-3 + PATENTS，随包许可），参考 = 编码器实际收到的量化输入
（真无损 = 100；HEIF/AVIF 码流无损如实 <100）。③ 4 项实测缺陷修复（写入提权优化 / 高级参数
勾选框与评分列 / 参数连线审计 / 评分真实性核验 + HEIF 444/420）。**已发行**（tag `v0.3.6.alpha1`）；
逐条变更见 [CHANGELOG.md](CHANGELOG.md) `[0.3.6.alpha1]`，验收证据见
[docs/0.3.6.alpha1-report.md](docs/0.3.6.alpha1-report.md)。

**0.3.5（上一正式版）= 0.3.5 系列收口修复批**：0.3.5.alpha3 之后的实测缺陷与体验修复。
三块交付：① **同目录输出数据安全收口**——输出路径命中源文件不再整文件失败，按普通名字冲突
改名（`名称 (1).扩展名`），源文件永不写（Overwrite 也降级 Rename）；② **元数据现代化**——
HEIF/AVIF 元数据回读兜底（修 grid 主图二次转码丢 EXIF/XMP）、HEIF/AVIF 真实位深显示、可选
「补全现代 EXIF」（只补可推导字段，不编造）；③ **GUI 实测缺陷清理**——AVIF 标签、运行页溢出/
状态、输出页页签错位/空白带/空态、折叠按钮主题、顶层菜单深色可读、输出目录权限 UAC 一键授权
等，并新增 `--ui-traverse` 深走查门禁（115 截图 / 159 断言）。**发行状态：0.3.5 已发布**（2026-10-28；GitHub Release 4 资产），验收证据见
[docs/0.3.5-report.md](docs/0.3.5-report.md)；逐条变更见 [CHANGELOG.md](CHANGELOG.md) `[0.3.5]`。

**0.3.5.alpha3（历史预发布）= 全格式编码器配置审计修复批**：8 个格式逐参数核对暴露/
生效/默认值/联动，修掉 JPEG 色度采样不生效、JXL 无损 `resampling`、WebP 假 `preset`、PNG
ICC 自相矛盾、HEIF/AVIF 假默认值（`qp=0`）、BMP 假位深选择等；Alpha 明确不支持（统一拍平 +
警示），TIFF 多页只取首图但保留合法缩略图 IFD1，仅元数据补 IPTC/COM 隐私剥除与 CMYK 支持。
全量报告见 [docs/encoder-config-audit-2026-10-25.md](docs/encoder-config-audit-2026-10-25.md)，
修复证据见 [docs/encoder-audit-fix-status-2026-10-25.md](docs/encoder-audit-fix-status-2026-10-25.md)。
**预发布（alpha）**，仅建议尝鲜/验证用；逐条变更见 [CHANGELOG.md](CHANGELOG.md)
`[0.3.5.alpha3]`。

**0.3.5.alpha2（历史预发布）= 七项实测缺陷修复**：HEIF 无损/10bit/色度与
preset/tune 参数真正生效 + 落盘位深回读校验；HEIF/AVIF 容器 ICC 在 probe/预览中可见；
Adobe RGB 预览/缩略图按源 ICC 显示（含「无 ICC、仅 EXIF ColorSpace=2」的合成回退）；
无边框窗口右上角三钮改客户区命中，真机鼠标实测可点；高德搜索源不再被瓦片源绑死
（有 Key 即走高德，并规范化 Key/追加错误码提示）；「仅元数据 + 改 GPS」新增
`run_metadata_only` 图像数据逐字节相同测试（`super/T1.tif` 137,053,848 B 全等）。
**预发布（alpha）**，仅建议尝鲜/验证用；逐条变更见 [CHANGELOG.md](CHANGELOG.md)
`[0.3.5.alpha2]`。

**0.3.5.alpha1（历史预发布）= M7R25 GUI 渲染面根治 + 识图门禁**：用户两张红框截图
7 处可见缺陷全部根治，并把「识图」机械化成常驻门禁 `layout-audit`
（六类几何 + 像素断言，每一处红框都有阳性对照）。根因/门禁深查见
[docs/m7r25-report.md](docs/m7r25-report.md)。

**0.3.4 = M7-R2.4 热修轮**：0.3.3 Windows 发行件「双击打不开」P0 热修——无边框
收口（M7-WP5）曾在窗口创建期递归建窗（`nativeEventFilter` 内 `winId()` 重入）直至栈崩，
现句柄只读化（`internalWinId()`）根治，触达面仅 `src/platform/frameless.cpp`；同时新增
**winzip 门禁 E-3 真机启动探针**（`tools/win_launch_probe.py`，真实 qwindows 平台断言可见
主窗 + 窗口数不超阈 + 进程存活），补上 offscreen 判据看不见真机建窗缺陷的结构性盲区。
**0.3.3 的 Windows zip 为坏件**（启动即挂），请改用 0.3.4。逐条变更见
[CHANGELOG.md](CHANGELOG.md) `[0.3.4]`，根因/证据/门禁归因见 [docs/0.3.4-report.md](docs/0.3.4-report.md)。

**0.3.3 = M7 工程收口轮 + R2 尾单轮 + R2.1 收口轮 + R2.2 收口轮 + R2.3 收口轮**：发布前深度
评审（331 条）+ UI 14 项评审的批量整改、结构重构与能力补强。用户可见变化：**数据安全根治**
（输出路径命中源文件即拒绝、写盘原子化、元数据写失败=失败、密钥 DPAPI 封存）、**HEIF/AVIF
编码档位核心行**（Main/Main10/Main12/… 直接呈现 + 位深联动）、时间/GPS/隐私三卡**启用开关**、
**12 卡右键菜单 + 全选入口**（文件列表/元数据四卡/输出四卡/运行三卡逐卡右键菜单；工具行
「勾选」钮 + Ctrl+A / Ctrl+Shift+A / Ctrl+D，作用于可见行）、**预览分辨率提升**（内嵌预览
门槛 1600px、缓存 4096px）、**GPS 卡地图 App 化**（浮层结果、分类下拉、地图 260px 且吃满
富余高度）、**窗口真无边框 + Win11 圆角**（原生标题栏消失、三钮可点）、滚动条细浮层、格式
页签去位深文案并等宽、**RTL 布局支持**（从右到左布局下自绘几何/内距/非对称样式随方向镜像；
LTR 逐字节不变，地图瓦片/图钉等地理内容不随语言翻转）、失败原因下钻与中文分类、参数区
子分组折叠与「已禁用：原因」提示、版本徽标/翻图打标/过滤态双击等交互缺陷修复；R2.2 轮再收
**EXIF 编辑器时间「清除」语义换锚**（连带 OffsetTime*/XMP MetadataDate 共 9 键，与批量规则
命中面同集）与分类面板重建零换件（闪烁/误连根除）；R2.3 轮再收**关现役后现役格式前移首位**
（格式页签落点与契约一致）与**禁用态原因 tooltip 补齐**（预设三按钮 / 参数技术条目）。
逐条变更见 [CHANGELOG.md](CHANGELOG.md) `[0.3.3]`，331+14 覆盖台账（R2.3 终态四档）与验收
证据见 [docs/m7-report.md](docs/m7-report.md)，裁定汇总见 [docs/m7-adjudications.md](docs/m7-adjudications.md)，
R2 轮方案见 [docs/m7r2-plan.md](docs/m7r2-plan.md)。**色彩能力四选项（色域映射 / 8bit 抖动 /
渲染意图 / 线性光重采样）已入引擎并在界面提供开关**——**输出页全局卡**四行（默认全关 =
行为不变；0.3.x 默认路径另有两处已归因字节变化，见 CHANGELOG「变更」节）；8bit 抖动仅作用
于 JPEG/WebP/JXL 及 OIIO 系四家的 8bit 量化段（HEIF/AVIF 的 8bit 路径不接）。**发行状态：0.3.3 已发布**（2026-10-13；其 Windows zip 有启动 P0，已由 0.3.4 热修，
请使用 0.3.4 或更新版本；当前最新正式版 = 0.4.3 正式版（本树））。

**0.3.2 = GPS 地图升级版（M6）**：把「内嵌地图选点」从能用升级到**地图 App 水平**——
R1 搜索搜得出（改用可达的**高德 Web 服务**事实表：`place/text` + 周边偏置 **`around` 20 条**按距离升序 +
6 类目 chips + 联想 / 逆地理 / 可点结果列表）、R2 矢量 / 卫星 / **混合图层**（Hybrid = 影像 + 标注双层
同帧绘制）、R3 搜索 → 点钉 / 点行 → **回填**的**地图内选点闭环**、R4 **坐标基准透明**（GPS 卡
「**WGS-84（标准 GPS）**」标签、十进制 ↔ DMS、复制到剪贴板；高德火星偏移（GCJ-02）已在地理编码
边界内部抵消，**选点值 = 未加密 WGS-84，可直接写 EXIF**）、R5 体验优化（图层记忆 + 设置页「地图」
页签、缩放「－ / ＋」、比例尺、光标 WGS-84 坐标 chip、**大地图对话框**（「展开」= 同一只 MapWidget
重挂载））。**引擎零改动**（`src/codecs` / `src/decode` 零 diff；`src/core` 仅 `settings.{h,cpp}`
**加性**追加 `map_style` / `map_sat_labels` 两键，0 删、老 INI 缺键回落默认），由金样 **20/20** 与
全语料 regression **zero diff** 双重背书；离线 `--ui-smoke` 新增 `meta-loop`（**选点值 = WGS-84 断言**）/
`map-hud` / `meta-map-expand`（14/14），冻结末行与截图 12 张不变。**高德 Key 用户自填、不入库**
（设置页密文录入；`.gitignore` 增补 + 全历史 blob 泄漏扫描 0 命中）。验收证据与偏差登记见
[docs/m6-report.md](docs/m6-report.md)，开发期裁定见 [docs/m6-adjudications.md](docs/m6-adjudications.md)，
逐条变更见 [CHANGELOG.md](CHANGELOG.md) `[0.3.2]`。

**0.3.1 = GUI 缺陷根治版**：对 0.3.0 做可视化排查（真实平台双主题实拍 + 像素级取证），
根治四类用户可见缺陷 —— 亮色下拉整只不绘制 / 格式卡错位 / 弹窗 ✕ 无效 / 文件列表多一枚勾选框，
并按「设计要求自绘、实现却让平台画」把**平台代画存量 10 项**一次收口（下拉与数值框矢量箭头、
树展开符、菜单勾选、滚动条、下拉弹层、菜单 / 报错框 / 提示框 / 分组盒令牌化）。同时新增
**QComboBox 唯一构造点** `formkit::combo_box()`（6 个建造点归零）与 **GUI-R2 五条渲染面回归探针**
（判定口径由控件几何改为**渲染像素**）。引擎零改动。排查报告与证据：
[docs/gui-review-0.3.0-r2.md](docs/gui-review-0.3.0-r2.md)。

## 构建（四步）

```bash
# 1. 新机器一键引导：aqtinstall 装 Qt、vcpkg 钉死 tag（M3-W3 起不再生成 tools/env.sh）
python tools/bootstrap.py

# 2. 环境注入（工具链全部在仓库内：.toolchain/、.cache/、vcpkg/）
#    tools/env.sh / env.sh.example 已随 W3 删除；改为每条命令经 tools/env.py 包装器执行
#    （它注入 vcvars 环境、Qt bin 与仓库内 venv 的 cmake/ninja）
python tools/env.py run -- cmake --preset release

# 3. 构建
python tools/env.py run -- cmake --build --preset release

# 4. 生成金标语料 + 跑测试
python tools/env.py run -- python tools/gen_corpus.py
python tools/env.py run -- ctest --preset release
```

其它预设：`cmake --preset dev`（ASan+UBSan，Debug）、`cmake --preset tsan`（TSan，仅 configure）、
`cmake --preset asan`（**0.3.0 追加**，Windows/MSVC AddressSanitizer，`PP_ASAN=ON`，见下）。

### 构建依赖表（0.3.0）

| 依赖 | 版本 / 形态 | 说明 |
|---|---|---|
| CMake | **3.28 ... 4.4**（兼容线；CI 装 4.4.x） | `cmake_minimum_required(VERSION 3.28...4.4)`（`CMakeLists.txt:2`） |
| vcpkg | tag **2026.07.29**（baseline `9e593bb18ea69cc5095e012465dcd675a822ed0d`） | 超前版本一律走仓库内 `vcpkg-overlay/`（OIIO / libjxl / libheif / exiv2 / x265 / SVT-AV1 / aom / libde265 / libjpeg-turbo） |
| Qt | **6.11.2**（Windows `msvc2022_64` / Linux `gcc_64`） | 单源 = `tools/versions.env` 的 `QT_VERSION`；`tools/bootstrap.py` 经 aqtinstall 装到 `.toolchain/Qt/` |
| 编译器 | MSVC（VS 2022/2026 Build Tools）或 GCC ≥11 / Clang ≥12 | ISA 基线 **x86-64-v3（AVX2）**，见下 |
| **CPU 要求** | **必须支持 AVX2 / x86-64-v3** | 0.3.0 起全产物按 AVX2 基线构建（`cmake/avx2.cmake`），**不提供 pre-AVX2 回退**；更老的 CPU 请用 0.2.0 产物 |
| 图像 / 元数据库 | OIIO 3.1.17 · libjxl 0.12.0 · libheif 1.23.5 · exiv2 0.28.9 · x265 4.3 · SVT-AV1 4.2.0 · aom 3.15.1 · libde265 1.1.3 · libwebp 1.6.0 · lcms 2.19.1 · spdlog 1.17.0 … | 逐库版本与本版升级史见 [docs/m4-report.md](docs/m4-report.md) §1/§8；**停留项**（OIIO 3.2 / exiv2 0.29-dev / jpegli 钉 commit）见同节 |
| Python | 3.9+（仅工具链与测试脚本用） | `tools/env.py`（环境注入）· `bootstrap.py` · `gen_corpus.py` · `regression.py` · `collect_licenses.py` · `make_winzip.py` · `tests/golden/smoke.py` · `tests/ui_smoke.py` |
| vcpkg triplet | `x64-windows-avx2` / `x64-linux-avx2`（`triplets/`） | 只承载**命名身份**（继承原生 triplet 的动态/静态形态），**不注入 ISA/LTO flags** —— 原因（libwebp 逐文件派发 / aom IL 对象）见 [docs/m4-report.md](docs/m4-report.md) 勘误 f |

### 构建与运行开关（0.3.0 追加）

| 开关 | 作用 | 取值 / 默认 |
|---|---|---|
| `PP_ASAN`（CMake option） | Windows/MSVC 下开 AddressSanitizer（`/fsanitize=address`，仅调试用；D-W3-1 缺陷取证即经此） | `ON` / `OFF`（**默认 OFF**；`release`、`release-dev` 的 flags 与语义完全不变，只有 `cmake --preset asan` 置 ON） |
| `PP_UI_THEME`（环境变量） | **强制 GUI 主题**（走查/截图用），**优先级最高** | `dark` \| `light`；未设置 = 落到下一档（见下） |
| `ui_theme_mode`（设置项 / `AppSettings` 键） | **主题三档持久化档**：设置 → 通用 → 主题（M5 起）。解析顺序 = **强制档（`PP_UI_THEME`）> 持久化档（`ui_theme_mode`）> 跟随系统**；单源 = `src/ui/theme.h` 的 `preferred_theme_mode()`，`--ui-smoke` 截图基准同源 | `system`（默认，= 跟随系统）\| `light` \| `dark`；写回 `settings.ini`，跨进程保留 |
| `PP_LOG_LEVEL`（环境变量） | 启动期一次性覆盖日志级别（0.1.0 起，语义不变） | `trace`/`debug`/`info`/`warn`/`error` |
| `PP_BUILD_DEV`（CMake option） | 构建 `--dev` harness / `--ui-smoke` / 金样与基准入口 | `ON` / `OFF`（默认 OFF，发布构建不含该代码路径） |

**引擎验证构建（含 `--dev`）**：`--dev` 分支由 `PP_BUILD_DEV` 宏保护，发布构建不含该代码路径（design §8.6）。跑 harness / 矩阵 / 规模验证时另建构建目录并打开开关；并行任务各用自己的 `build/<task-id>`，依赖已在 `vcpkg_installed` 就位，关掉 manifest 自动安装以免并发写：

```bash
python tools/env.py run -- cmake --preset release -B build/m1-dev -DVCPKG_MANIFEST_INSTALL=OFF -DPP_BUILD_DEV=ON
python tools/env.py run -- cmake --build build/m1-dev
```

## M1a 工具与用法

### `photopipeline --dev`（§3.15 冻结 CLI；仅 `PP_BUILD_DEV=ON` 的构建可用）

```text
photopipeline --dev <input...> --out <dir> [options]
  --out DIR              输出根目录（必填）
  --format ID            jpeg|jxl|png|tiff|webp|bmp|heif|avif（默认 jxl）
  --backend ID           后端（avif: svt-av1|libaom；默认空=首选）
  --tech ID              技术（jxl: vardct|modular；webp: lossy|lossless；默认空=首选）
  --outputs LIST         多输出（0.3.0）：逗号分隔 format[:backend[:tech]]，可重复追加；
                         与 --format/--backend/--tech 互斥（二选一）
  --template TMPL        输出路径模板（0.3.0）：$format/$dir/$file | $dir/$file | $dir/$format/$file
                         （默认按输出数派生；符号集 $format/$dir/$file/$name/$ext）
  --lossless             无损开关
  --bitdepth N           输出位深；默认 jpeg 8 / jxl 16 / png 16 / tiff 16 /
                         webp 8 / bmp 24 / heif 10 / avif 10（探测不支持时回退 8 并记 warning）
  --color TARGET         keep|srgb|p3|adobergb（默认 keep）
  --conflict POLICY      skip|overwrite|rename（dev 默认 overwrite，保证幂等重跑）
  --metadata-only        仅元数据模式（零重编码；jpeg/png/tiff/webp）
  --preset FILE          载入预设 JSON（命令行选项优先）
  --param KEY=VALUE      覆盖单个参数（可多次）
  --meta KEY=VALUE       写入/覆盖元数据标签（可多次；Exiv2 全名，如 Exif.Image.Artist）
  --workers N            并发 worker 数（0 = 自动：线程预算或逻辑核数；dev 默认 1 便于日志对照）
  --base DIR             镜像路径基准目录（可多次；默认各输入文件所在目录）
  --log-level LVL        trace|debug|info|warn|error
注意（R-145）：--dev 默认值有意与 GUI 不同 —— --conflict 默认 overwrite（GUI 默认 rename，
  显式传 --conflict rename 即同 GUI 语义）；单输出模板派生为 $dir/$file（GUI 默认
  $format/$dir/$file，显式传 --template '$format/$dir/$file' 即同 GUI 语义）。
退出码 = 失败文件数（0=全部成功）；stdout 末尾打印汇总表（成功/失败/跳过/取消、总耗时、吞吐 MB/s）
sidecar（--dev 专有）：<out>/<每个产物>.pp.json（逐输出断言面）、
                       <out>/progress-trace.jsonl（进度事件流；schema 见 tests/golden/SCHEMA.md）

photopipeline --dev bench --out DIR [--bench-cases LIST] [--bench-scale N]     # 0.3.0 性能基准（§11.3）
  # 场景：48MP TIFF→JXL / 24MP JPEG→WebP×16 批 / 单张 HEIF / 多格式(jpeg+webp) 解码共享 /
  #       flatten·quantize·transpose·downscale·interleave 微基准（avx2 vs ref）/ 48MP 无内嵌档预览
  # 输出：stdout 逐场景行 + `bench: verdict …` + <out>/bench-report.json（机器可读留证）
```

示例：

```bash
build/m1-dev/photopipeline --dev tests/golden/base/*.png --out .cache/out --format jxl --workers 1
build/m1-dev/photopipeline --dev tests/golden/meta/exif_full.jpg --out .cache/out-meta \
    --format jpeg --metadata-only --meta Exif.Image.Artist=M1
```

### 金样断言与冒烟

| 工具 | 用途 |
|---|---|
| `pp_verify <expected.json> <actual_output> [--selftest]` | 按 `tests/golden/SCHEMA.md` 断言 `pixel.mode`（exact / psnr+threshold_db）、`metadata[]`、`warnings_contain[]`。输出 `VERIFY <case> OK\|FAIL <detail>`，退出码 = FAIL 数；`--selftest` 用内存样本自检（不需要语料，已进 ctest） |
| `tests/golden/smoke.py [BUILD_DIR]` | **20 对断言级**金样冒烟（M1 冒烟级 9 对 → M2 断言级 16 对 → M4 追加 4 对），逐例跑 `photopipeline --dev` 并用 `pp_verify` 断言像素 + 元数据值 + warnings 三面（M4 的 `progress-trace` 对另断言进度事件流）。脚本内的默认 `BUILD_DIR` 指向 `build/release`，**请显式传入自己的构建目录**（或用 `PP_BIN` / `PP_VERIFY` / `OUT_ROOT` 覆盖）；输出 `SMOKE total=20 pass=20 fail=0`，退出码 = 失败例数；`--cases a,b,c`（或 `PP_CASES`）取子集，断言口径与全量同源 |

### 测试命令

```bash
# 单测 + M0 工具 + 金样自检 + linkprobe（条目数以 ctest -N 实测为准；2026-10-12 实测 34 条）
ctest --test-dir build/release --output-on-failure

# 金标语料（27 fixture，幂等；PP_MKFIXTURES= 指向本次构建的 pp_mkfixtures）
PP_MKFIXTURES=build/release/pp_mkfixtures python tools/gen_corpus.py

# 20 对断言级金样（需 PP_BUILD_DEV=ON 的构建，脚本走 --dev）
python tests/golden/smoke.py build/release-dev
```

## 开发工具（M2）

| 工具 | 用途 |
|---|---|
| `tools/regression.py` | 全语料 `--dev` 回归基线：日志规范化后与 `tools/baseline/golden.log` diff（双跑零 diff） |
| `tools/lsan.supp` | LeakSanitizer 抑制文件（当前**无生效规则**，注释即论证） |
| `tools/tsan.supp` | ThreadSanitizer 抑制文件（每条规则附 happens-before 论证 + 阳性对照） |
| `tests/golden/smoke.py` | 金样冒烟：**20 对断言级**用例（像素 + 元数据值 + warnings；`progress-trace` 对另含进度事件流断言） |
| `photopipeline --ui-smoke` | 无头 UI 走查：三页遍历 + 参数谓词/地图边界断言 + 真实转码，产出 12 张截图 |
| `tools/make_appimage.sh` | 打包 AppImage（离线、可重复重跑；内置四项烟测） |
| `tools/ci-system-deps.txt` | CI/构建机系统依赖**单一事实来源**（64 个 apt 包，行尾注释格式；`linux` / `ui-smoke` / `appimage` 三 job 与 `warm-cache.yml` 共用同一清单） |
| `tools/ci-install-deps.sh` | 按清单安装依赖；发行版改名容错（本发行版不存在的包只打 `::warning::`，不整体失败），真正的安全网是 soname 级自检 |
| `tools/ci-check-deps.sh [BUILD_DIR]` | **覆盖自检**：从构建产物派生全部 `DT_NEEDED` soname，断言每个都由清单包提供；未覆盖项必须 0（退出码 0 = 全覆盖 / 1 = 有未覆盖 / 2 = 用法环境错误） |
| `tools/ci-cache-gc.sh` | Actions 缓存清理：`vcpkg-*` / `ccache-*` 保留最新 8 个且 7 天内，`qt-*` 永不触碰；幂等，任何失败都不把 job 弄红 |

### CI 依赖与缓存

```bash
bash tools/ci-install-deps.sh                    # 装 tools/ci-system-deps.txt 全部包（幂等；PP_APT_DRY_RUN=1 只打印）
bash tools/ci-check-deps.sh build/release        # soname 级覆盖自检（未覆盖项必须 0；见上表）
PP_GC_DRY_RUN=1 bash tools/ci-cache-gc.sh        # 预览缓存清理（CI 由 cache-gc job 真删）
gh workflow run warm-cache.yml                   # 缓存播种：改 vcpkg.json / overlay / Qt 版本后预热
                                                 # （workflow_dispatch；每周日 03:17 UTC 另有定时兜底）
```

### `tools/regression.py` — 全语料回归基线

```bash
python tools/regression.py [BUILD_DIR] [--update]
#   BUILD_DIR  默认 build/release-dev（须为 -DPP_BUILD_DEV=ON 的构建）
#   --update   用当前构建重新生成 tools/baseline/golden.log（而非对比）
```

- 运行内容：27 输入 × 8 格式 + `--metadata-only` × base 16 × 4 格式 = **12 次顺序调用 / 280 文件槽**；
  `tests/golden/real/`（用户样本）与 `tests/golden/smoke/`（归 smoke.py）不入基线。
- **三个前提（不得放宽）**：① `workers=1` 保证逐文件顺序、预算记账与编码调用序列确定；
  ② 输出目录固定为 `.cache/regression/out-<run>` 且先清空，日志中的输出路径每次一致；
  ③ 脚本整体幂等（新输出目录、`--conflict overwrite`、不跨运行携带状态）。
- 规范化 N1–N9：去时间戳/tid/内存地址/绝对路径，`*_ms` 与吞吐数值化，预算容量值（随机器可用内存漂移）
  占位化，`elapsed ≥ 1s` 才出现的 budget 行整行剔除；级别/阶段/文件名/参数快照/警告/失败原因与全部
  汇总计数**逐字节保留**（这正是基线要捕捉的信号）。
- 退出码：`0` 零 diff / `1` 有 diff（打印前 40 行）/ `2` 环境缺失（用法、二进制、基线或语料）。

### `tools/lsan.supp` / `tools/tsan.supp` — sanitizer 抑制

两个文件都是**证据文件**，不改变构建；接线完全由命令行负责。冻结调用行：

```bash
# ASan + UBSan + LSan（抑制文件当前无生效规则；控制实验证明通道有效）
LSAN_OPTIONS="suppressions=$PWD/tools/lsan.supp:print_suppressions=0" \
ASAN_OPTIONS="detect_leaks=1:halt_on_error=0" \
UBSAN_OPTIONS="print_stacktrace=1" \
build/m2-t1/photopipeline --dev tests/golden/base/* --out /tmp/out --format jxl --workers 2

# TSan（并发路径；同样编码进 `tsan` test preset）
TSAN_OPTIONS="halt_on_error=0:second_deadlock_stack=1:ignore_interceptors_accesses=1:suppressions=$PWD/tools/tsan.supp:print_suppressions=1" \
ctest --test-dir build/m2-t2 --output-on-failure
```

- `lsan.supp`：全语料 ASan/UBSan 矩阵零泄漏报告 → **无生效规则**；注释说明为何 lcms2/OIIO/Exiv2/libheif
  的进程级单次分配（可达的静态生命周期）不需要规则，并给出复现干净结果的脚本路径。
- `tsan.supp`：每条规则上方用注释给出 happens-before 论证；根因统一为**未插桩的第三方二进制**
  （vcpkg debug 静态库 + 预编译 Qt/GLib DSO）不产生 TSan 影子边。含阳性对照（纯 Qt 复现器、
  直接竞态复现器）证明抑制不是"关闭竞态检测"。

### `tests/golden/smoke.py` — 金样冒烟（20 对断言级）

```bash
python tests/golden/smoke.py <build_dir>      # 须为 -DPP_BUILD_DEV=ON 的构建
```

- 20 对用例：jpeg-lossy / jxl-lossless / png16-lossless / tiff16-lzw（走 `--preset` 分支）/
  webp-lossless / heif-lossy / avif-lossy / bmp-exact / meta-artist / gray-webp / alpha-jpeg（展平警告）/
  depth-jpeg（降档警告）/ multipage-png（多页截断警告）/ unicode-png（中文+emoji 路径）/ exif-roundtrip /
  metaonly-jpeg（仅元数据）· **M4 追加 4 对**：multiformat-split（`$format/$dir/$file` 双产物）/
  multiformat-mirror（`$dir/$format/$file`）/ multiformat-conflict（两同主名源 → 逐输出 rename）/
  progress-trace（多格式 + 进度事件流：单调不倒退、synthetic 口径、终态达 1.0）。
- 每例跑 `photopipeline --dev` 后用 `pp_verify` 对 `tests/golden/smoke/<case>.json` 断言三面：
  像素（exact / PSNR+阈值）、元数据值（规范化文本）、warnings 列表。
- 输出末行 `SMOKE total=20 pass=20 fail=0`；**退出码 = 失败例数**。可用 `PP_BIN` / `PP_VERIFY` /
  `OUT_ROOT` 覆盖默认路径（默认输出到 `.cache/out-smoke/`）。

### `--ui-smoke` — 无头 UI 走查

```bash
QT_QPA_PLATFORM=offscreen build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review
# ctest 入口：python tests/ui_smoke.py <build_dir>（ctest -R ui_smoke）
```

- 12 张截图（M4 冻结集）：`01-meta` / `02-output` / `02b-output-avif` / `03-run` / `03b-run-done` /
  **`03c-run-idle`** / **`03d-run-rows`** / **`03e-run-cancel`** / `04-settings` / `05-exif-editor` /
  `06-presets` / **`07-run-light`**（新增 4 张覆盖运行页空闲态/逐输出行探针态/取消态与浅色主题）；
  覆盖三页遍历、参数谓词断言（jxl 无损→modular+distance 0、jpeg quality 显隐、tiff 压缩联动）、
  地图 GCJ↔WGS 边界断言（点击偏差 <0.001°）、16 文件真实转码。
- 成功 stdout 末行 `UI-SMOKE OK shots=12 pages=3`（**0.3.0 冻结行**，替换 0.2 的 `shots=8`）；UI 类不进 ctest 单测，此即 UI 的回归通道。

### `tools/make_appimage.sh` — 打包

```bash
tools/make_appimage.sh [OUT_DIR]     # 默认 dist/
# env: PP_BUILD_DIR（默认 build/release，与 make_winzip.py 同口径）、QT_DIR（默认从产物 ldd 反查 Qt）
# 产物: <OUT_DIR>/PhotoPipeline-<版本>-x86_64.AppImage + PhotoPipeline.AppDir/（先删后建，可重跑）
```

- 输入为 release 预设产物；版本号从产物 `--version` 读取（不在脚本硬编码）；第三方许可由
  `tools/collect_licenses.py` 汇总为 `usr/share/licenses/<port>/copyright`（目录名 = port 名）
  与本项目 `usr/share/licenses/PhotoPipeline/LICENSE`。
- 打包器 `tools/bin/appimagetool-x86_64.AppImage` 与 type2 runtime `tools/bin/type2-runtime-x86_64`
  均入库并旁置 `.sha512`：打包先校验两份 SHA512，再以 `APPIMAGE_EXTRACT_AND_RUN=1`（不依赖 FUSE）与
  `--runtime-file` 指向入库 runtime 运行 —— **全程不联网**；入库 runtime 缺失或校验失败即 exit 2 硬失败，
  不静默回退到在线下载（appimagetool 默认从 GitHub `continuous` 渠道拉取，版本会漂移）。
- 组装：`usr/lib` = `ldd` 闭包中的非系统 `.so`（Qt6*、libjpeg 等），Qt 插件
  （platforms/imageformats/iconengines/styles/**tls**）、OIIO 插件目录（静态 OIIO 时留空），
  `usr/share` 拷图标与 desktop，`AppRun` 导出 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` / `OIIO_LIBRARY_PATH`。
- 脚本内烟测四项：① 产物 `--version` 输出 `PhotoPipeline <版本>`；② AppDir 二进制 `ldd` 无 `not found`；
  ③ `AppRun` 存在且 desktop 的 `Exec=`/`Icon=` 与冻结文本逐行一致；④ 从产物解包确认
  `usr/share/licenses/` 内 ≥30 个 port 的 `copyright` + 本项目 `LICENSE`。
- 不承诺字节可复现（squashfs 超级块时间戳/mtime 参与），同结构不同 sha 按现状接受。
- **设计取舍**（R-121 登记）：CMakeLists 全文**无 `install()`**，打包器（make_appimage.sh /
  make_winzip.py）直读构建树组装产物——两器默认构建目录已统一 `build/release`。引入 install()
  属构建面重构，未在本轮范围。

## M1b 桌面 UI

### 构建与运行

```bash
python tools/env.py run -- cmake --preset release
python tools/env.py run -- cmake --build --preset release
./build/release/photopipeline
```

功能速览（冻结文案与行为详见 [docs/m1b-tasks.md](docs/m1b-tasks.md) §2–§3）：

- **文件面板**：拖放或按钮添加（目录递归收集）、异步缩略图、按文件名搜索、十二态状态徽标、不支持文件计数
- **① 元数据页**：时间偏移（Δ 六字段 / 时区语义，首文件前后预览）、GPS（经纬度 + DMS 实时 + 内嵌地图选点 + 从选中文件读取 + 清除）、标签修改（Exif/Xmp 分流）、隐私剥除、mtime 同步、单文件例外列表
- **② 输出页**：转码 / 仅元数据、8 格式 ×（后端 × 技术）参数表单（无损锁定、谓词显隐、位深运行时探测；**M8 起 116 行参数目录全暴露 + 联动预检**）、色彩目标、同名冲突策略、预设按钮
- **③ 运行页**：进度条 / 吞吐 / 逐文件状态着色、取消（幂等）、结束摘要 + 打开输出目录 / 查看日志、**M8 起逐输出 SSIMULACRA 2 评分列**
- **双击文件** → 单文件编辑器：EXIF 树（IFD0/Exif/GPS/只读 MakerNote）、XMP、时间/GPS/隐私三态覆盖、忽略批量规则（源文件永远只读）
- **设置**（worker / 内存预算 / 展平底色 / 旋转 / 地图提供方 / 瓦片缓存 / 日志级别 / **主题三档** / **质量评分开关**）与**预设**（JSON，`预设管理` 对话框）持久化于平台配置目录

### GUI 刷新 v6（M5）

视觉体系与排版按 v6 单源规范全量重做（引擎零改动）。设计稿即终稿：**视觉终稿 = [docs/mockups-v2/](docs/mockups-v2/)
的 8 张 PNG**（`meta` / `output` / `run` / `settings` 四页 × `dark` / `light`）；规范正文与验收清单
（§12 十七条）见 [docs/qt-gui-design-spec.md](docs/qt-gui-design-spec.md)，实测截图证据包与逐条探针
见 [docs/m5-report.md](docs/m5-report.md) §1.2/§3。

| 区域 | v6 口径 |
|---|---|
| 令牌 / 表单 | `src/ui/theme.h` 令牌单源（明暗 = 换令牌重建 QSS，暗色卡片无投影）；`src/ui/formkit.*` 行构造器：行 pitch **28** / 标签轨 **80** 右对齐 / 字段宽只有 **88 · 160 · flex** / 单位 16px 槽 |
| 图标 / 标题栏 | `src/ui/icons.*` 矢量图标（24×24 网格、1.5px 描边）——**全界面无 Unicode 字形图标**；caption 三钮 **46×32 单轨**（可见面 = 命中区，关闭悬停 `#c42b1c`，最大化钮保留 Win11 Snap） |
| 壳层 / 栏宽 | 顶栏 44px 三段式 + 一体式分段导航 92×30（**无 ✓ 完成态装饰**）+ 状态栏 30px 四档（含「剩余磁盘」读数）；**栏宽固定轴 左 330 / 右 587**，切页列不错位 |
| 文件列表 / 分类 | 行高 **58px**（勾选 13px + 46px 缩略图 + 2–3 行信息不截断 + 状态胶囊）；工具行常驻（搜索/添加/移除/筛选/分组）；分类面板 **2 列网格**（色点 + 名 + 计数胶囊）；筛选「仅显示已勾选 / 仅显示有例外 + 清除」只影响可见性 |
| 预览画布 | `#16181d` 舞台（明暗同值）、矢量空态 + 引导语、缩放条「− / 100% / ＋ / 适应 / 1:1」、翻图钮 28px、小图最近邻、letterbox 照片衬垫 |
| 三页 / 弹窗 | 元数据页四卡组（kv 双列 / 时间偏移 3×spin88 + 生效对照 / GPS 双列轨 + 地图 / 隐私）；输出页四卡组（磁贴 2×4 零形变 / 一行一义六行 / 预设与预估）；运行页三卡组（总览 + 指标单源 kv + 分组/逐输出行 + 日志尾等宽 4 行）；设置弹窗 660px 下划线页签 **通用｜输出｜运行｜地图｜关于** |

**主题切换（三档，M5 起）**：设置 → **通用 → 主题** = `跟随系统｜浅色｜深色`，选后即时生效并写入
`settings.ini`（键 `ui_theme_mode`，默认 `system`）⇒ 跨进程保留。解析顺序 = **强制档 > 持久化档 >
跟随系统**：命令行/截图走查可用 `PP_UI_THEME=dark|light` 强行覆盖（优先级最高，**不写回**设置）。
亮暗两主题**布局等价**（`--ui-smoke` 逐一比对 988 个控件的几何与可见性，仅令牌不同）。

地图支持 OSM（WGS-84）与高德（GCJ-02，坐标自动边界转换）双提供方；高德需在设置中填 Web 服务 Key。

### M8 参数面与评分（0.3.6.alpha1）

**全参数暴露（参数目录 116 行）**

- **HEIF/x265 40 行**（36 可设置 + 4 只读）、**AVIF/libaom 57**（51+6）、**AVIF/SVT-AV1 3 只读**、
  **JXL 16**；事实层单源 = `tools/param_catalog_specs/*.json` + `tools/gen_param_catalog.py` 生成
  `src/codecs/param_catalog/`，**UI 表单 / 预设 / `--param` / 校验 / 编码器读取同一目录**。
- **三态语义**：新增参数默认 Unset；N/D 行「启用 / 跟随默认」；布尔参数三态下拉；只读行只显示不
  可写。**RC 模式显式下拉**（HEIF 质量/CRF/QP；libaom 质量/CQ Level；SVT 质量/QP）。
- **高级参数勾选框（本轮 R2）**：「可选启用」勾选框位于**行尾右侧**，固定状态槽宽（空注记也占位）+
  勾选框固定 13px + 状态注记 `ElidedLabel`（超长省略号 + tooltip 全文）；标签 / 数值列与普通行严格
  对齐；三态下拉语义零改动（探针 `param-enable-geom checked=8 bad=0`）。
- **联动与预检**（`src/core/param_rules.*` 数据驱动）：无损 / 位深 / 色度 / 依赖 / 互斥规则在运行页
  与输出页实时判定；运行前预检给出错误清单、拒绝开始并聚焦首错。**透传面白名单**：`x265:<opt>` /
  `aom:<opt>` 目录外键 = 预检错误（`svt:` 无透传通道，出现即错）。
- JXL 新增键：noise / group_order_center_x/y / upsampling_mode / uses_original_profile / box_compress /
  expert_options + effort 11 / buffering / output_mode / disable_perceptual_heuristics / HDR 四值 /
  intrinsic_size / frame_name；codestream_level 顺序修正。

**SSIMULACRA 2 逐输出评分**

- 算法 = libjxl 0.12 官方源码 + 兼容层（BSD-3 + PATENTS，`third_party/ssimulacra2/`），实现于
  `src/core/metric/`；**参考 = 编码器实际收到的量化输入**（真无损 = 100；HEIF/AVIF 码流无损如实 <100）。
- **8 种输出全覆盖**；运行页任务表**「评分」列**逐输出行显示（两位小数；文件组行显示 `—`；
  `Off`/仅元数据不显示；不可用显示 `—（原因）` + tooltip）。评分是**附加信息**：评分失败不影响
  产物成功与否（只落 `MetricState`）。
- **颜色阈值**：**≥95 极好（ok/绿）· ≥90 好（accent/蓝）· ≥80 ok（warn/琥珀）· <80 不 ok（err/红）**
  —— 四档全部取现有主题令牌（明暗双主题一致），tooltip 同步给出阈值表与「参考 = 编码器输入 /
  失真 = 解码产物」口径；「评分」列位于耗时与状态之间（44px，行/表头共用常量）。
- **开关**：GUI 默认开，可在 设置 → 运行 →「质量评分」关闭（`settings.ini` 键 `quality_score`；
  `--dev` 默认关，需显式 `--score`）。关闭 = **不执行评分计算**，不产生评分列/预算行差异。
- 代价（如实）：评分内部物化 ≈142 B/px，单次评分额外预算 160 B/px（12MP ≈1.8GB/输出）；
  24MP 预估 ≈28–33s/图（8 输出）——大图批量评分会显著拉长墙钟，可关闭。
- 证据面：`--dev --score` sidecar 加性 `metric` 字段 + `tests/golden/SCHEMA.md` 的 `assert.score`
  区间断言（金样 4 个既有 case + `pp_verify` selftest 覆盖）。

### M9 HEIF/iPhone 兼容配置矩阵与 JXL 修复（0.3.6.alpha2）

**HEIF 真实档位（profile）**

- 编码档位下拉 = **12 个真实 HEVC 信号 + 自动**：Main Still Picture（idc 3 · 8b 4:2:0）、
  Main（idc 1 · 8b 4:2:0）、Main10（idc 2 · 10b 4:2:0，Apple HEIC 常用档）、
  RExt Main 4:2:0/4:2:2/4:4:4 × 8/10/12（idc 4）；旧预设编号 0–6 不重排。
- 编码器按选定信号注入内部 x265 参数；编码后用 **hvcC 回读校验** `profile_idc / level_idc /
  chroma / bit-depth`，名不副实即失败。
- **Level**：x265 4.3 全档（1.0–7.2）按「8 像素补齐后 MaxLumaPs + 宽高上限」预校验；非法 Level 给
  中文可操作错误（样本数/上限/最小可行 Level/建议提高 Level 或启用 grid），不再出现误导性
  「encoder does not support N-bit input」。
- **有损 grid**：保留键 `__heif_grid`（0 关 / 1 总是 Apple 式分块（1024×576 基准，按 Level 收缩）/
  2 自动，缺省）；自动模式仅当显式 Level 装不下整图时分块；每 tile 独立合法 Level、逐 tile
  nclx+ICC、alpha+grid 明确拒绝；**默认（无显式 Level）编码产物与旧版逐字节相同**。
- 验证：`tools/heif_matrix_check.py` 穷举矩阵 **1013 例 = pass 697 / fail 0 / expected-fail 316 /
  unexpected 0**（697 个合法组合全部 hvcC 结构断言 + OIIO 解码通过）；证据 `build/matrix/final.md`。

**JXL 有损修复**

- 有损 VarDCT 恒 **XYB**（`uses_original_profile=false`），修复 libjxl #4552 非 XYB 路径的
  「distance 越小评分越差、体积暴涨」；显式 `uses_original_profile=true` 明确报错；无损/Modular
  语义不变、金样不受影响。
- 实测 T1 16-bit keep/AdobeRGB：d2.0 2.04MB/80.81 → d1.0 4.01MB/86.44 → d0.5 7.24MB/90.21 →
  d0.3 10.06MB/91.67 → d0.2 12.66MB/92.38（体积与分数双单调）；P3 XYB 回读 P3 参考 idiff
  PSNR 41.03 dB（仅压缩误差）。

### M10 性能/运行页 + 参数面 + 格式预设与自适应（0.4.0）

**性能与运行页**

- **评分预算拆分**：文件 `need` 恢复 `2×frame`，评分走独立配额（容量 = min(文件预算/2, 4 GiB)）⇒
  48 MP PNG + `--score` 由 `EXIT=1` 变 `EXIT=0`；`need_bytes` 8.832 GB → **1.152 GB**；评分批并发
  由 1 升到 14（24 MP 推算）。
- **评分加速**：色彩归一 matrix-shaper 快速内核 **8.49 s → 0.42 s（20×）**（`|Δscore| ≤ 0.01`；
  分数 ≥90 且 <100 时用精确 lcms 重算兜底）；SSIMULACRA 2 流式/分块，23 MP 峰值额外内存
  **165.1 MB ≈ 7.6 B/px**（旧实现 148.7 B/px 的 5.1%），单次评分 1.572 s。
- **probe 去重**：probe 结果一次产出并复用 ⇒ 22.8 MP HEIC 首个 `Probing → Decoding`
  **2,902.9 → 1,447.8 ms（−50.1%）**，`decode_ms` 不变。
- **独立评分池 / 多格式并行 / 逐输出事件**：`ScorePool`（K = min(4, max(1, hw/4))、独立配额、心跳、
  per-run 取消复位）；同文件多格式并行（1 文件 4 格式 22.8 MP **12,719 ms vs 串行 16,690 ms = 1.31×**，
  四格式产物 SHA256 并行==串行）；`OutputDone` / `FileEvent::output` 逐输出事件（成功/跳过/失败/取消）。
- **运行页**：列单源 `RunTableColumns`，删「速度」列（信息并入大小列 tooltip）、名称列中间省略
  （`ElideMiddle`）+ 全路径 tooltip、评分列 44px。
- **仍未达（如实）**：P4-1 `metric_ms ≤ encode_ms` 未达（2,178.6 vs 2,032.1 ms = 1.072×）；
  10-bit HEIC 输入 probe 1,635–1,670 ms（8-bit 达标）；HEIC 深 probe 首事件 gap 1.44–1.67 s。

**参数面（HEIF / JXL）**

- **HEIF grid/tile 配置行**：`heif_grid`（0 关 / 1 自动 / 2 总是，默认自动）+ `heif_tile_w/h`
  （0=自动、64–8192、8 向下对齐）；`x265:level-idc` 上移核心区；**默认（无显式 Level）产物零变化**；
  保留键 `__heif_grid` 仍可读（兼容别名）。
- **still 档位 16–21**（编号不重排）：编码后从 hvcC 内嵌 SPS 解析 constraint + DPB 断言 still 语义；
  **16（Main10Still）实测不可用（DPB 4/2）⇒ 置灰并给原因**；修正 3 个误导标签（7/10/13）。
- **tune / RC**：HEIF tune 7 项（4 原生 + `animation` 仿真 + 2 置灰）、AVIF-libaom 扩 `ssimulacra2` /
  `vmaf_saliency_map`、AVIF-SVT 5 值中文标签；RC 数值行上移核心区，动态域 8-bit `0..51` /
  10-bit `−12..51` / 12-bit `−24..51`；QP + 显式 Level 给中文错误、CRF + Level 告警（规则 H5）。
- **JXL 治理**：43 → **31 键**（删 12 个结构性 no-op）、`epf` 默认 **−1（跟随库）**、
  `gaborish` / `dots` / `patches` 改**三态**（未设置 = 不下发）、`codestream_level` 只读、
  `channel_colors_*` Int→Float、`effort=11` 自动放开 expert options。**行为变化（有意）**：
  JXL 有损产物 23/23 变化（回归已归因，未重播种）。
- 参数目录 **116 → 117 行**（HEIF +3 grid/tile、JXL −2）。

**格式预设与自适应（0.4.0 最终形态；出厂预设 = 仓库 JSON）**

- **格式预设**：每个格式页签顶部「格式预设」行（下拉 + 保存 / 另存 / 删除 / 导入 / 导出），
  存 `<data_dir>/presets/formats/<format>/<name>.json`；保存→载入→保存字节稳定。**0.4.0 起出厂预设 =
  仓库 JSON 数据文件 `presets/formats/**`（11 条，随发行包分发、首启字节复制 seed）**，人类可直接改/复制；
  **每格式一条 `default`**（HEIF 另有「Apple自适应」、JXL 另有「无损归档」「自适应压缩」）；下拉**无「未选择」**，
  格式卡自动载入 `default`；`default` 不可删但可「保存」覆盖（= 直接改默认值），可「另存」复制。
  存储/字段/seed/迁移规则见 [docs/format-presets.md](docs/format-presets.md)。
- **自适应策略**：目标质量 + 最多轮数 + 阶段（`linear` / `bisect` / `exp` / `greedy`），DSL v1.1 显式方向
  与步长断点；每轮编码到同目录候选（**不写元数据**）→ 评分 → 选定提交 → 走既有元数据/时间路径；终止 = 达标 /
  plateau / 轮数耗尽 / 阶段耗尽；GUI 只有**只读摘要 + 启用开关**（无文本编辑器）。**GUI 默认按 default
  策略启用**（JPEG/HEIF/AVIF/WebP/JXL 的 `default` 自带策略；PNG/TIFF/BMP 无策略）；出厂策略全文见
  [docs/adaptive-policy.md](docs/adaptive-policy.md) §10。
- **引擎/CLI 默认仍关闭**：`--dev`/金样/回归不带预设 ⇒ 无策略、产物与 0.3.x 逐字节相同（C25）；
  GUI 里也可随时关掉「启用自适应」。

### M11 / M12 迭代修复批（0.4.0 正式版）

- **HEIF tile 尺寸按 Level 与 still 规格推导（C17'）**：删除 Apple 1024×576 固定基准，改由「目标 Level 的
  MaxLumaPs/单边上限 + still 单图应用格式 Level 上界（3GPP TS 26.140/26.141 = L5.1）」推导；
  T1(3902×5854)+L3.1 → **768×1280**（=983,040 整容量）6×5、L 自动+总是 grid → 2176×4096（=M51）2×2；
  新增 `planning_level_idc`/`still_level_capped`/warning 与逐 tile hvcC Level 回读；默认单图产物零变化。
- **自适应 DSL v1.1 + 迭代日志**：`down`/`up` 显式方向、`breakpoint <v> step <s>` 步长断点、动作级
  `max_rounds`；错误码 42→45；pipeline 四类 info 行 `adaptive plan/stage/round/summary`（含中文决策原因与
  逐轮参数/分数序列）。
- **运行状态机修复（S-1…S-5）**：迟到过程事件不覆写终态、取消 no-op 不再显示「已取消」、全局预设载入清
  「格式预设来源」、「未选择/删除预设」相关残留会话态（M12 后改为 default 自愈）——详见
  [docs/state-machine-analysis.md](docs/state-machine-analysis.md)。
- **工厂预设外部化 + `default` 语义（M12）**：11 条 JSON（8 格式 `default` + HEIF「Apple自适应」+
  JXL「无损归档」「自适应压缩」）随包分发、首启字节复制 seed、旧 `_builtin-*.json` 迁移删除；
  `default` 不可删但可保存覆盖；无「未选择」态（详见 [docs/format-presets.md](docs/format-presets.md) §7、
  [docs/m12-adjudications.md](docs/m12-adjudications.md)）。
- **JPEG 出厂 `default` 开启自适应距离策略**：`quality_mode=distance`、`distance=3.0`，策略
  `search distance from current down to 0.1 step 0.1 breakpoint 1 step 1 max_rounds 20`（1–3 段步长 1、
  1 以下步长 0.1）；JPEG distance UI 域 0.3–3.0 → **0.1–3.0**（R-294 修订）。
- **HEIF「Apple自适应」CRF=20**：`x265:crf=20` + Main10 档位（`reserved.heif_profile=2`）+ L3.1；
  `--ui-smoke` 探针 `heif-apple-crf=20` 钉住「文件 → 控件」读数。
- **CI 流程优化**：脚本判据/缓存/预算修复，目标 main-full + nightly + release 全绿（逐 job 读数见
  [docs/0.4.0-report.md](docs/0.4.0-report.md) §2.5）。

### M15 用户反馈 6 项修复批 + 发行收口（0.4.3 正式版）

- **HEIC 无损第四模式（反馈 1/2）**：HEIF 速率控制下拉 = 质量 / CRF / QP / **无损**（与「无损编码」
  勾选框同源，唯一真值 = 选择项 `lossless`）；无损下 **chroma / profile / Level 仍可配置**并真正下发
  （删除强制 4:4:4；非 4:4:4 如实警告「YCbCr 码流无损 ≠ 像素级无损」；Level 因 x265 CQP+decoder-level
  硬约束「可配但不下发 + 如实告警」，产物 PTL Level 固定 8.5）；「无损」随格式预设 params 往返，
  工厂 `heif/default.json` 零改动。
- **多选「多个值」界面异常（反馈 2）**：时间卡「时区语义」QComboBox mixed 态真正显示「多个值」
  （`meta::MixedComboBox`，paint 级覆盖、零 item 模型/索引改动）；mixed 提交统一到全部选中文件。
- **单击「多个值」框闪退（反馈 3）**：本机未复现（真实事件链 25+ 交互点 × 3 轮、无 WER）；同批复核修
  3 处防御性收口（GPS 警告空指针 / 时间多选小表越界夹取 / 信息卡单文件 probes 空表）+ 15 条 mixed
  交互机械探针（meta-selection 19→34）。
- **JXL 编码效率严重退化（反馈 4）**：根因 = 多输出规划器按「每文件预算/P」名义均分线程，JXL/AVIF 被从
  16 压到 8/4/2，再乘 JXL default 自适应（最多 30 轮重编码）放大成数量级退化。修复 = **逐输出线程公平
  分配**（`allocate_output_threads()`：heavy 编码器分走余下预算、flat 编码器保底 1；`RunScope` 末尾加性
  `encode_threads_per_output`，默认空 = 旧口径逐字不变）；单轮 A/B 混合 3 输出 JXL encode_ms 中位
  **12320 → 5096 ms（≈2.4×）**，产物 SHA256 修复前后逐对相同。
- **部分 Windows 启动卡死（反馈 5）**：0.4.1→0.4.2 唯一新增启动期阻塞调用（预览色彩管理的
  `GetDC`/`GetICMProfileW` + ICC 读取 + lcms2）移出 GUI 线程：构造期立即注入 sRGB 回退，系统 profile
  后台线程获取 + QTimer 轮询应用（generation 丢弃过期、5s 软超时/30s 硬上限、析构不 join、offscreen
  不启线程）；模拟 `PP_DISPLAY_ICC_DELAY_MS=5000` 实测 `shown=1399ms`（<2s）且窗口可响应；mica DWM
  背景按 Win11 22H2 门控（Win11 视觉不变）+ 启动日志记录 OS build/Qt/平台。
- **导入含 XMP 的 HEIC 闪退（反馈 6）**：根因 = Exiv2 0.28.8 `XmpParser::initialize()` 非线程安全懒
  初始化；GUI 导入时缩略图池/预览池/元数据探针池并发首读含 XMP 文件 → 竞态进入 Adobe XMP SDK 初始化
  → 内部空指针崩溃（WER：exiv2.dll+0x7c9cf，反汇编 = XML_Node/std::map 内部 `mov r14,[rcx]`）。
  修复 = 新增 `core/metadata_runtime.h`（`call_once` 初始化 + 进程级递归锁**串行化全部 Exiv2 I/O/解析
  入口**；锁内只取内嵌预览字节、OIIO 解码/缩放移出锁外）+ 8 线程并发 XMP 回归探针；真实 GUI 导入同一
  「下载」目录（27 文件含 HEIC/TIFF/JXL）复跑无崩溃。
- 门禁：集成树 ctest **52/52**、release 树 **51/51**、金样 **20/20**、回归 **zero diff**、`--ui-smoke`
  冻结行逐字、format/tidy 0 违规；`make_winzip` 五门禁 + E-1/E-2/E-3 烟测全绿。

### M14 元数据作用域/大预设/预览色彩/调度自适应/真实债清零（0.4.2 正式版）

- **元数据页作用域与聚合（需求 1/2）**：编辑作用域 = 行选中集合（勾选 = 参与运行，页面显式提示
  「修改仅作用于选中的 N 个文件」）；无选中 = 空态，控件禁用/隐藏，任何路径**零写入**；文件列表空→非空
  自动选中首行。多选逐字段聚合（一致=值；不一致=「多个值」+ 逐文件 tooltip）；>8 张不再因 probe 截断
  假 mixed；过滤/分组隐藏选中项时不再对隐藏文件生效。**「批量 GPS 规则」折叠面删除**（GPS 坐标输入即
  选中图片编辑面；生效坐标合并为一行只读值）。
- **大预设重设计（需求 3）**：大预设 `outputs[i]` 加性 `source_preset`（来源格式预设名；空不写 ⇒ 旧文件
  零字节变化）；载入后格式行显示真实来源（占位「来自大预设：<名>」+ 注记「基于格式预设 X / 自定义参数」，
  保存/删除禁用、另存可用、重选恢复绑定）；保存写绑定绝对路径；外部删除解绑；dirty「（已修改）」；
  中文预设名 + 导入/导出 + 坏文件原因；无选中载入含元数据修改预设只应用输出配置并注记。
- **元数据详细编辑器（需求 4）**：OK 返回与旧 `MetadataOverride` 合并零丢失（六 enable 与不表达字段不丢）；
  删除「忽略全部批量规则 / 继承批量规则 / 〔批〕」旧概念；三态改「不修改 / 设置为 / 清除」；无选中/文件
  不在列表显式反馈；行映射沿 R-152 收口链。
- **预览色彩管理（需求 5）**：新增 `src/core/display_profile.*`（Windows 系统 ICC / 其它回退 sRGB）+
  lcms2 相对比色+BPC 变换到显示 profile；缩略图同根因修复；离屏/CI 强制 sRGB 保冻结截图集；真实桌面
  A/B 实测：P3 编码红 (234,51,35) 修复前 (234,51,35) → 修复后 **(255,0,0)**，中灰 128 不变。
- **调度并行自适应（需求 6）**：`src/core/thread_plan.*` 规划器 = 编码器能力表（jxl/avif 可扩展、webp 上限 2、
  heif/x265 与 jpegli/OIIO 计 1）× CPU 利用率 EMA（Windows GetSystemTimes / Linux /proc/stat）× 队列深度
  → E（每文件编码线程）与 P（同文件多输出并行）；冻结 `alloc_threads` W 公式不变；深队列 E=P=1、浅队列按
  线程预算放大、CPU 饱和收敛、迟滞防抖；`RunScope.parallel_outputs` 末尾加性；规划轨迹走 debug 日志
  （info 级回归基线零变化）。
- **真实债清零 + CI（需求 7）**：hwy ODR 根治（私有化 + bridge；oracle 10 条逐位零漂移、我方 TU 的
  `hwy::` 定义符号 178→0）；ProgressMux/FileEvent 竞争根治（EventFn 单线程 drain，不持锁回调、事件不丢不改序）；
  地图 EventFilter UAF 修复；ASan 本机门禁 scoping；CI main-full 关键路径压缩（判据零放宽）。
- **目录清理（需求 8）**：清空 `build/`（50.6GB）+ `dist/`/`.cache/`/`.pp_test_tmp/`/`debug/*/`；构建树现场
  生成；工具脚本对已清空的一次性证据路径缺失容忍。

### M13 元数据选择项级编辑 / 去 CreatorTool / DateCreated / 选点海拔（0.4.1 正式版）

- **元数据页改为「选择项作用域编辑器」**：右侧每项修改默认只作用于**当前选中文件**（每文件独立
  `MetadataOverride`，文件列表徽标/「例外」入口 = 带元数据修改的文件）；**多选 = 批量生效**（字段级合并，
  不串扰各文件其它分量）。多选值不一致时明确显示 **「多个值」**（Δ spin 文本、时间/GPS 规则输入、关键信息卡、
  GPS 生效坐标、隐私开关；三卡「启用」开关为**三态**，部分文件开启 = partial）。生效值仍全出自
  `preview_effective()`（显示 = 写入同源）。新纯内核 `src/ui/meta_selection.{h,cpp}`（聚合 / 字段级合并 /
  预设聚合，可无 GUI 单测）。
- **隐私与文件时间卡新增「去除 CreatorTool」**：删除输出 XMP 的 `xmp:CreatorTool`（清除 Photoshop 等生成工具
  签名）；引擎 `build_plan` 静默删除该键、不动其它 XMP/EXIF 字段。
- **修复 `Xmp.photoshop.DateCreated` 未随偏移刷新**：纳入 XMP 时间键表，与 `xmp:CreateDate`/`xmp:ModifyDate`
  走同一条 `shifted_xmp_time` 纯函数（Δ 偏移与时区语义均改写；ISO-8601 带时区/小数秒支持；不可解析则跳过写入）。
- **GPS 选点自动海拔**：地图点选/搜索结果/联想/取地图中心后经 Open-Meteo DEM 自动取海拔回填 GPS 海拔输入
  （默认开、可关、400ms 去抖、单在途、5s 超时、失败静默降级、标注「近似值」；离线模式不发请求）。
- **预设语义变更**：元数据预设载入 = 替换式应用到选中文件的逐文件修改；保存预设 = 当前选择项编辑状态聚合。

### 无头 UI 冒烟（`--ui-smoke`，需 `PP_BUILD_DEV=ON`）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure    # 条目以 ctest -N 实测为准（2026-11-03 实测 52 条 = 51 引擎/工具 + ui_smoke；0.4.1 基线 50）
# 或手动跑（offscreen，产出 12 张走查截图）：
QT_QPA_PLATFORM=offscreen ./build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review
```

脚本化走查覆盖：三页遍历截图（01-meta / 02-output / 02b-output-avif / 03-run / 03b-run-done / 03c-run-idle / 03d-run-rows / 03e-run-cancel / 04-settings / 05-exif-editor / 06-presets / 07-run-light）、参数谓词断言（jxl 无损→modular+distance 0、jpeg quality 显隐、tiff 压缩联动）、地图 GCJ↔WGS 边界断言（点击偏差 <0.001°）、16 文件真实转码运行；成功 stdout 末行 `UI-SMOKE OK shots=12 pages=3`（0.3.0 冻结行）。ctest 入口 `python tests/ui_smoke.py <build_dir>`（浅色档 `PP_UI_THEME=light`）。

## 发行版（AppImage）

发行产物为单一 AppImage（Linux x86_64）；自包含 Qt6、8 个编码器、OIIO、色彩与元数据运行库，免安装。

预编译 AppImage 见 [Releases](https://github.com/zhang-hz/PhotoPipeline3/releases)（下载最新 tag 的 `PhotoPipeline-<版本>-x86_64.AppImage`；sha256 以 release 页与下方说明为准）

### 下载与运行

```bash
# 1) 取得产物：Release 附件（首选），或本地打包产物 dist/
#    PhotoPipeline-<版本>-x86_64.AppImage（约 50 MiB）
chmod +x PhotoPipeline-<版本>-x86_64.AppImage

# 2) 运行
./PhotoPipeline-<版本>-x86_64.AppImage            # 图形界面（文件管理器中亦可双击，需允许"执行"）
./PhotoPipeline-<版本>-x86_64.AppImage --version  # → PhotoPipeline <版本>
```

- **取件优先级（v0.1.0 起）**：**首选** [Release 附件](https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0)
  `PhotoPipeline-0.1.0-x86_64.AppImage`（**50,940,408 B**，sha256 `31f75c48052be34a…`，
  完整校验值以 Release 页为准）；**次选** GitHub Actions 的 `PhotoPipeline-AppImage` artifact
  （**需登录 GitHub**，且属更早轮次的历史产物，sha256 与 Release 附件不同）；本地打包产物见
  「开发工具（M2）」。
- AppImage 类型 2：正常挂载运行需要 FUSE（`libfuse2`）。目标机无 FUSE 时改用
  `./PhotoPipeline-<版本>-x86_64.AppImage --appimage-extract-and-run`（等价环境变量
  `APPIMAGE_EXTRACT_AND_RUN=1`）。
- 无图形环境（服务器/CI）可 `QT_QPA_PLATFORM=offscreen` 运行无头冒烟（见"开发工具（M2）"节）。

### 便携模式（设计 §8.5）

- **可执行文件所在目录可写 → 便携模式**：`settings.ini`、`presets/*.json`、`logs/` 落在可执行文件旁。
- **目录只读 → 回退数据目录**：`$XDG_DATA_HOME/PhotoPipeline`（默认 `~/.local/share/PhotoPipeline`），
  预设与日志为其子目录；两处创建都失败时程序以空路径交调用方处理。
- AppImage 的挂载点只读，故**直接运行 AppImage 时自动回退 XDG**（数据在用户目录，不随 AppImage 移动）。
- 需要"真便携"（U 盘/移动介质）：`./PhotoPipeline-<版本>-x86_64.AppImage --appimage-extract` 解包到可写
  目录后运行 `squashfs-root/AppRun`，数据即写在该解包目录内。

### 系统要求

- **平台**：Linux x86_64（本版单一发行平台；Windows / macOS 未发行）
- **CPU 必须支持 AVX2（x86-64-v3）**：0.3.0 起全部产物按 AVX2 基线编译（`cmake/avx2.cmake` +
  `x64-linux-avx2` triplet），**不提供 pre-AVX2 回退**；不支持 AVX2 的机器请使用 0.2.0 产物。
- **glibc**：**官方发行产物由 CI 构建**（`appimage` job，ubuntu-24.04 runner），基线 **≥ 2.39**
  （noble 自带 glibc，`objdump -T` 最大符号版本）；**本地自建产物取决于本机 glibc** —— 例如在
  Ubuntu 26.04 上打包，实测要求 `GLIBC_2.43`，该产物只适用于 glibc ≥ 2.43 的目标机。两句都成立：
  取发行附件请认 **Release 附件**（= CI 构建产物，见章首下载入口）；本地打包件按本机 glibc 自用
- **系统库**（不在 AppImage 内，需目标机提供）：
  - `libssl3`：Qt 6.8 的 TLS 后端插件（`libqopensslbackend.so`）运行期 dlopen `libssl.so.3` /
    `libcrypto.so.3`；缺失时 `QNetworkAccessManager` 报 `No functional TLS backend was found`，
    在线地图瓦片与经纬度反查失效（离线功能不受影响）
  - xcb/X11 相关包：`libxcb1`、`libxcb-cursor0`、`libxcb-icccm4`、`libxcb-image0`、`libxcb-keysyms1`、
    `libxcb-randr0`、`libxcb-render-util0`、`libxcb-shape0`、`libxcb-sync1`、`libxcb-xfixes0`、
    `libxcb-xinput0`、`libxcb-xkb1`、`libxkbcommon-x11-0`、`libX11-6`、`libX11-xcb1`、`libSM6`、`libICE6`
  - **X11 / xcb / xkbcommon 核心库的版本下限提示**：随包的 `libxcb-*.so*` / `libxkbcommon-x11` /
    `libX11-xcb` 链接的是**目标机**`libxcb.so.1` / `libxkbcommon.so.0` / `libX11.so.6`（单副本
    不变式库，刻意不随包）。这些扩展库引用的是**无符号版本节点**的 `xcb_*` 符号，系统 `libxcb.so.1`
    过旧时失败发生在 **dlopen 期**（`undefined symbol: xcb_…`），**`ldd` 与打包期闭包检查都查不出符号
    级缺失** —— 目标机 libxcb 不低于打包机版本即可：CI 发行产物打包机 = ubuntu-24.04（`libxcb1` 1.15），
    开发机 26.04（1.17），两者均实测可用
  - OpenGL/EGL：`libGL1`、`libEGL1` —— Qt **xcb 平台插件的 `DT_NEEDED`**（非可选）；缺失时 AppRun
    的启动前自检会指名报出 `libGL.so.1` / `libEGL.so.1` 与对应发行版包名并以退出码 3 结束
  - 字体与基础库：`libfontconfig1`、`libfreetype6`、`libdbus-1-3`、`libglib2.0-0`、`libxkbcommon0`
  - Ubuntu 24.04 一行：
    `sudo apt install libssl3 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxkbcommon-x11-0 libgl1 libegl1 libfontconfig1 libdbus-1-3`
  - **Wayland 会话**：AppImage 内只随包 `offscreen` / `xcb` 两个平台插件（**无原生 wayland 插件**），
    因此在 Wayland 桌面下经 **XWayland** 回退运行 —— Qt 会先打印一行 wayland 插件缺失警告、随后
    正常回退到 xcb，**非缺陷**；如需静默可显式 `QT_QPA_PLATFORM=xcb`（无 XWayland 的纯 Wayland
    环境可改用 `QT_QPA_PLATFORM=offscreen` 无头运行）。
  - **完整 soname 清单**：打包时产出 `dist/PhotoPipeline-<版本>-deps.txt`（**0.3.0 CI 实测**：闭包检查
    覆盖全部 **36 个 ELF**、逐项列出目标机系统要求 **45 条** soname（本机自建产物的条数会因打包
    基础镜像的依赖闭包不同而不同，非笔误；CI 产物清单见 job 摘要与 artifact 内 deps.txt）。
    本机未复跑该脚本（依赖 Linux ELF 工具链，Windows 主机不可用）—— 口径见 [docs/m5-report.md](docs/m5-report.md) §1.4(c)）。
- **磁盘**：AppImage 约 50 MiB；另需输出目录与地图瓦片缓存（数据目录内）空间

### 源码与许可

- **许可**：GPL-3.0-or-later（全文见 [LICENSE](LICENSE)）；本程序不提供任何担保。
- **源码**：<https://github.com/zhang-hz/PhotoPipeline3>（发行版对应源码 = 该仓库同 tag 的提交；
  可执行文件的源码 offer 以仓库 URL 为准）。
- **第三方组件**（与"关于"页清单同源）：Exiv2 / x265（GPLv2+）、libheif（LGPLv3）、Qt（LGPLv3）、
  OpenImageIO（Apache-2.0）、libjxl / libwebp / SVT-AV1 / libaom（BSD）、lcms2（MIT）、
  libpng / libtiff / libjpeg（jpegli 分支）等；各库版权与许可全文随 AppImage 分发
  （`tools/collect_licenses.py` 汇总 → `usr/share/licenses/<port>/copyright`；**0.3.0 实测两平台同为
  35 个 port**（AppImage 与 win64.zip 的产物内复核均报 `copyright = 35`，门禁阈值 ≥30），另附子项目
  `usr/share/licenses/PhotoPipeline/LICENSE`；各见对应发行章节；
  本项目许可 → `usr/share/licenses/PhotoPipeline/LICENSE`）。

## 发行版（Windows）

**M3 起提供 Windows x64 免安装产物**（0.2.0 为首个 Windows 版本，当前版本 0.4.3）；与 Linux 的 AppImage 同一引擎
与界面，差异只在打包形态（zip vs AppImage）与平台运行时（MSVC 运行时 / Qt msvc2022_64）。

### 下载与运行

```powershell
# 1) 取得产物：GitHub Releases 页面（见下方"源码与许可"链接）
#    Windows: PhotoPipeline-<版本>-win64.zip
#    Linux 对照件同页: PhotoPipeline-<版本>-x86_64.AppImage

# 2) 解压到任意可写目录（zip 顶层为单一目录 PhotoPipeline/），双击或命令行运行
.\PhotoPipeline\photopipeline.exe
```

### 系统要求

- **Windows 10 1903+ / Windows 11**，x64。1903 是**下限**：可执行文件嵌入的应用清单声明
  `activeCodePage=UTF-8`（全局 UTF-8 语义，见 D2），更早的 Windows 不支持该清单字段。
- **CPU 必须支持 AVX2（x86-64-v3）**：0.3.0 起全部产物按 AVX2 基线编译（`cmake/avx2.cmake` +
  `x64-windows-avx2` triplet），**不提供 pre-AVX2 回退**；不支持 AVX2 的机器请使用 0.2.0 产物。
- 无需自备 Qt / vcpkg / VC++ 再发行包：包内已含 Qt 6.11.2 运行库（Core/Gui/Widgets/Network/Svg）
  与其插件树（`platforms/`、`imageformats/`、`iconengines/`、`styles/`、`tls/`、
  `networkinformation/`、`generic/`）、全部第三方图像库 DLL，以及 **app-local VC 运行时**
  （`msvcp140*.dll` / `vcruntime140*.dll` / `concrt140.dll` / `vccorlib140.dll`，取自 MSVC 的
  `Microsoft.VC*.CRT` 部署单元）—— 目标机不需要预装任何 VC redist。
- `platforms/qoffscreen.dll` 随包（供无头 `--ui-smoke` 使用）；`tls/` 内含 Qt 的
  Schannel / CertOnly 后端（Windows 无需 OpenSSL 动态库）。

### 解压即用 / 便携模式

- 解压到任意**可写**目录后直接运行 `photopipeline.exe`（无安装步骤、不写注册表）。
- **数据 = 便携**：日志与预设首次使用时创建在 **exe 同目录**（`logs/`、`presets/`）。
- exe 目录**不可写**时（例如解压到 `C:\Program Files`）自动回退到
  `%APPDATA%\PhotoPipeline\`（同样建 `logs/`、`presets/`）。两条路径都不可写时本次运行不落盘
  （日志走 stderr，不静默失败）。
- 便携用法示例：解压到 U 盘或用户目录（如 `D:\Tools\PhotoPipeline\`）直接运行，配置随目录走。

### 命令行

```powershell
.\photopipeline.exe --version      # → PhotoPipeline 0.4.3（单行 + 退出码 0；非 dev 门控，发布构建亦可用）
.\photopipeline.exe                # 图形界面
.\photopipeline.exe --ui-smoke     # 无头 UI 走查（offscreen，产出 12 张截图；仅 PP_BUILD_DEV=ON 的构建）
.\photopipeline.exe --dev ...      # 开发 harness：全语料矩阵 / 回归基线入口（同上，仅 dev 构建）
```

- **输出与行尾**：`stdout`/`stderr` 走**平台文本模式**（Windows 为 CRLF）；**日志文件两平台统一 LF**。
  脚本匹配冻结行时一律 `rstrip('\r\n')`。
- **控制台可见性**：GUI 子系统进程在**捕获型句柄**（管道、文件重定向——`ctest`、`subprocess`、
  `> out.txt` 等）下原样保留句柄，输出可被捕获；其余情形（双击、直接在终端调用）自动挂接父控制台，
  故 `--version` 等输出在终端可见。

### 从源码构建（Windows）

```powershell
python tools\bootstrap.py                                     # 一键引导：Qt(win64_msvc2022_64) + vcpkg(pinned) + 仓库内工具链
python tools\env.py run -- cmake --preset release             # 配置（env.py 注入 vcvars/Qt/cmake/ninja）
python tools\env.py run -- cmake --build --preset release     # 构建（产物 build\release\photopipeline.exe）
python tools\env.py run -- ctest --preset release             # 单测（条目以 ctest -N 实测为准，2026-10-12 为 34 条）
python tools\env.py run -- python tests\golden\smoke.py build\release-dev                       # 20 对金样（需 release-dev 树）
python tools\env.py run -- python tools\make_winzip.py dist --smoke-exe build\release-dev\photopipeline.exe   # 打包 zip
```

`tools/make_winzip.py` 内置与 AppImage 同口径的**五项烟测**（闭包 / Qt 来源 / 结构 / 许可 / 启动，
含 offscreen `--ui-smoke`），任一失败即以非零码退出；产物 = `dist\PhotoPipeline-<版本>-win64.zip`。

### 许可

- 本项目：**GPL-3.0-or-later**（全文见 [LICENSE](LICENSE)）。
- 包内 `licenses/` 随附全部第三方许可文本：`licenses/<port>/copyright`（**0.3.0 实测 35 个**
  port）+ `licenses/PhotoPipeline/LICENSE`；打包时会校验数量（≥30）并在产物内复核。

## M0 工具

| 工具 | 用途 |
|---|---|
| `pp_linkprobe` | 链接探针：逐库运行时校验（lcms2 / exiv2(+BMFF) / OIIO 插件 / jpegli / libjxl / libheif HEVC+AV1 编码器 / libwebp）。输出 `PROBE <name> OK\|FAIL <detail>` 与尾部两行 `PLUGINS:`、`HEIF_ENCODERS:`；退出码 = FAIL 数 |
| `pp_mkfixtures` | fixture 生成与校验：`--make <dir>` 生成 exif_full.jpg / webp×2 / heif_exif.heic / avif_exif.avif / jxl_exif.jxl / cmyk.tif，`--verify <dir>` 逐项回读校验（`MADE`/`FIXTURE` 行，退出码 = FAIL 数，目录缺失 → SKIP 77） |
| `pp_spikes` | Spike 验证：`e` = lcms2 数值金值（sRGB→sRGB 恒等 ≤1e-5；白点→Lab(D50) L∈[99.5,100.5]）；`f --golden-root <dir>` = Exiv2 无损重写保真（SOS `FF DA` 之后字节完全一致 + OIIO 像素 hash 相等） |
| `tools/gen_corpus.py` | 生成 `tests/golden/{base,edge,meta}` 语料并写出 `tests/golden/CHECKSUMS`（幂等；可 `OIIOTOOL=` / `PP_MKFIXTURES=` 覆盖工具路径） |

## 文档

- [docs/design.md](docs/design.md) — 架构与设计
- [docs/param-catalog.md](docs/param-catalog.md) — 参数目录（编码器参数语义来源）
- [docs/brainstorm-consensus.md](docs/brainstorm-consensus.md) — 共识记录
- [docs/m0-tasks.md](docs/m0-tasks.md) — M0 任务书（执行依据 / 冻结契约）
- [docs/m1-tasks.md](docs/m1-tasks.md) — M1 任务书（界面冻结清单 / 批次划分 / 出口准则；M1a 引擎）
- [docs/m1b-tasks.md](docs/m1b-tasks.md) — M1b 任务书（UI 冻结头 / 全局规格 / 落地口径与裁定记录；M1b 界面）
- [docs/m1b-report.md](docs/m1b-report.md) — M1b 收口报告（任务/裁定/事故/验收证据）
- [docs/m2-tasks.md](docs/m2-tasks.md) — M2 任务书（Linux debug 收口 + 发行；30 项 TODO 处置裁定）
- [docs/m2-report.md](docs/m2-report.md) — M2 收口报告（任务/裁定/事故/验收证据）
- [docs/m3-tasks.md](docs/m3-tasks.md) — M3 任务书（Windows 移植 + 双平台发行）
- [docs/m3-report.md](docs/m3-report.md) — M3 收口报告（Windows 移植、打包/CI/发行自动化、跨平台语义裁决）
- [docs/v0.3.0-consensus.md](docs/v0.3.0-consensus.md) — **M4 需求共识**（5 轮 × 21 项决策：预览分类 / 输出元数据 / 运行并行 / GUI 与 CI / 依赖与纪律）
- [docs/v0.3.0-design.md](docs/v0.3.0-design.md) — **M4 设计文档**（接口解冻裁定表、多格式管线、进度/调度、GUI 规格、依赖升级表、CI 拓扑、测试与金样扩展）
- [docs/m4-tasks.md](docs/m4-tasks.md) — **M4 任务书**（波次 W0–W5、总出口准则、subagent 开发协议与纪律）
- [docs/m4-report.md](docs/m4-report.md) — **M4 收口报告**（九需求验收对照、总出口 8 条、金样 20 对、两大缺陷修复史、设计勘误清单落地对照、偏差与未决项）
- [docs/m5-tasks.md](docs/m5-tasks.md) — **M5 任务书**（GUI 刷新 v6：架构契约 C1–C8、波次 W-G1–W-R、subagent 协议与纪律、收口清单）
- [docs/qt-gui-design-spec.md](docs/qt-gui-design-spec.md) — **GUI v6 设计规范（单源）**（令牌/字阶/间距圆角、表单体系 R1–R4、页面规格、§12 验收十七条）
- [docs/mockups-v2/](docs/mockups-v2/) — **GUI v6 视觉终稿**（4 页 × 亮暗 8 张 PNG + HTML/`tokens.css`）
- [docs/m5-adjudications.md](docs/m5-adjudications.md) — **M5 开发期裁定记录**（A1–A40：规格冲突口径、跨波次缺陷追认、core 窄幅解冻）
- [docs/m5-report.md](docs/m5-report.md) — **M5 收口报告**（§12 十七条逐条探针、波次执行记录、视觉比对结论、CI/打包实测指纹、偏差与未决项）
- [docs/m6-map-upgrade.md](docs/m6-map-upgrade.md) — M6 地图升级契约（C1–C10：瓦片图层/地理编码/选点闭环/坐标口径）
- [docs/m6-report.md](docs/m6-report.md) / [docs/m6-adjudications.md](docs/m6-adjudications.md) — M6 收口报告与裁定记录
- [docs/m7r2-plan.md](docs/m7r2-plan.md) — **M7-R2 轮方案**（遗留 finding 四类审视表 / 14 项技术设计定稿 / 波次·包·文件面计划 / 纪律；「任务书」引述出处收口）
- [docs/m7-report.md](docs/m7-report.md) — **M7 收口报告**（331+14 覆盖台账四档终态、八项终门禁取证、R2/R2.1/R2.2/R2.3 轮交付、偏差与未决项）
- [docs/m7-adjudications.md](docs/m7-adjudications.md) — M7 开发期裁定记录（A/B/C/D/E 各节 + R2 轮八/九节 + R2.1 轮十一/十二节 + R2.2 轮十四节 + R2.3 轮十六节：冻结面加性、行为变化、误识别关闭、受约束登记）
- [docs/m7-evidence/](docs/m7-evidence/) — M7 证据留存目录（各包门禁读数摘录 + 发布链 runbook，换机可复核）

## 许可

GPL-3.0-or-later，全文见 [LICENSE](LICENSE)。AppImage 内 `libiconv` 未随附许可文本：该 port 在 x86_64-linux 不产出库文件，`iconv*` 由系统 glibc 提供，本发行未分发其代码。
