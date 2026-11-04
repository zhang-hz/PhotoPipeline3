# PhotoPipeline · AGENTS.md(智能体 / 新协作者工作手册)

> **这份文档是什么**:在本仓库工作的 AI 智能体与新协作者的总导航 + 纪律总纲。目标是让你不必读完 docs/ 下 1.1 万行文档,就能回答四个问题:**这个项目要什么、东西放在哪、哪些红线不能碰、干完活要留下什么证据**。
> **权威链**:本文只是导航与纪律;细节真源永远是它指向的那份文档或代码。两者冲突时**以真源为准**,并顺手回来修订本文。
> **版本口径**:0.4.3 正式版(版本号单源 = `CMakeLists.txt:19-28`:数字段 `project(PhotoPipeline VERSION 0.4.3)` + `PP_VERSION_LABEL ""`(正式版为空串);CMake `project(VERSION)` 只收数字段,标签拼在字符串侧;`vcpkg.json` version 只写数字段 0.4.3——R-122 单源一致性断言要求与 `project(VERSION)` 同值。**`--version` = `PhotoPipeline 0.4.3`**。任何其它地方出现的版本号都是引用,不是定义)。

---

## 1. 项目目标

### 1.1 一句话定位(不可改写)

**批量像素级转码器 + 元数据手术台**(batch pixel-level transcoder with metadata surgery),GPL-3.0-or-later。

三个身份:

| 身份 | 含义 |
|---|---|
| **像素搬运工** | 解码 → 色彩管理 → 编码,**中间不加料** |
| **元数据手术台** | 任意 EXIF/XMP 编辑、时间偏移(含时区语义)、GPS 写入(地图选点) |
| **批量流水线** | 文件级并行 + 点位极丰富的文本日志 |

它**不是**:图像编辑器、图库管理器、看图软件、RAW 处理器。守住这条边界本身就砍掉了大部分复杂度——有人提需求时先对照负面清单(§1.3)。

### 1.2 不可协商的核心原则(需求共识,`docs/brainstorm-consensus.md` §2)

1. **禁止降级**:内部处理禁止任何形式的质量降级;能浮点不用整数——内部管线全程 **float32**。
2. **高质量 + 高性能**:只用各格式最强的编码器实现;全功能构建,**禁止精简依赖**。
3. **全参数暴露**:所用库的全部参数进入 GUI 可配置(核心参数置顶、高级参数折叠展开)。
4. **小而美**:砍掉一切与"转码 + 元数据"无关的功能;以最快速度完成开发。
5. **无额外图像处理**:除 EXIF 方向旋转(开关式,默认开)外不做任何像素变换。

### 1.3 负面清单(明确不做,不许扩义)

RAW/DNG 输入 · GIF 输出 · 图像编辑(缩放/裁剪/水印/锐化/降噪) · HDR/色调映射 · 大图预览与编码前后对比 · 断点续跑/队列持久化 · IPTC-IIM · Google 底图 · CMYK 输入 · CLI(除 `--dev` 开发 harness) · 自动选格式的"魔法" · 图库/评级/管理类功能 · GPX 轨迹自动匹配。

新功能提案先问三件事:① 在负面清单里吗?② 动不动引擎(§5.2)?③ 有没有机械化探针能验收(§4.3)?

### 1.4 交付形态

- 桌面 GUI(Qt Widgets,中文界面)+ 打包发行:**Linux AppImage**(x86_64)+ **Windows 免安装 zip**。
- 转码全流程**离线**可用;联网仅限地图瓦片/搜索/逆地理。
- 便携优先落盘:可执行目录可写 → 配置/预设/日志落在 exe 旁;只读 → 回退 XDG / `%APPDATA%`。
- 系统要求硬线:**CPU 必须支持 AVX2(x86-64-v3)**,不提供 pre-AVX2 回退;Windows 10 1903+(`activeCodePage=UTF-8` 清单)。

---

## 2. 当前状态(2026-11-04)

| 项 | 状态 |
|---|---|
| 版本 | **0.4.3 正式版**（M15，stable，**已发行**：tag **annotated `v0.4.3`**（tag object `64dcdf1a…`）→ commit **`0dbf6e5`**；GitHub Release **`PhotoPipeline 0.4.3`**（release id `434190819`，prerelease=**False**），publishedAt **2026-11-04T16:48:39Z**，URL <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.4.3>；`release.yml` run **41864935620 = success**（attempt 2，7/7 job；attempt 1 = winzip-pack 的 E-3 WIN-LAUNCH `timeout-no-visible-window`，`rerun-failed-jobs` 重跑通过）；**4 资产**：`win64.zip` 34,319,466 B / `91194f8f…4c5385`、`x86_64.AppImage` 52,783,608 B / `de2a9ad1…ffdd43`、`deps.txt` 2,439 B / `e8c80b3d…407854d`、`SHA256SUMS.txt` 293 B / `1a0cc74b…f94582`；下载件 win64 解包 exe 实跑 `--version` = `PhotoPipeline 0.4.3`（zip 条目 118）；`main-full` run **41864930206 = success（10/10 job）**；版本单源 = `project(PhotoPipeline VERSION 0.4.3)` + `PP_VERSION_LABEL ""`，`vcpkg.json` = 0.4.3（R-122）；裁定 = `docs/m15-adjudications.md`（A1–A14 / U1–U7）、报告 = `docs/m15-report.md`。上一正式版 **0.4.2**（tag annotated `v0.4.2` → commit `658dcc6`；Release published 2026-11-02T18:17:11Z，4 资产；`docs/0.4.2-report.md`）；更早 **0.4.1**（`v0.4.1` → `d43b9db`）、**0.4.0**（`v0.4.0` → `1b6fdbc`）与 **0.4.0.alpha1 / 0.3.5**（见历史报告）。 |
| 里程碑 | M0 环境/六 Spike/冻结面 → M1a 引擎 + M1b UI → M2 Linux 收口 → M3 Windows(=0.2.0) → M4/M5 = 0.3.0(九项需求 + GUI v6 刷新) → 0.3.1 GUI 缺陷根治 → M6 = 0.3.2 → M7 = 0.3.3(工程收口,Windows 启动 P0)→ 0.3.4 热修 → 0.3.5.alpha1/alpha2/alpha3 预发布 → 0.3.5 正式版(收口修复批) → M8 = 0.3.6.alpha1(参数面全暴露 116 行 + SSIMULACRA 2 逐输出评分) → M9 = 0.3.6.alpha2(JXL 有损 VarDCT 修复 + HEIF 真实档位/Level/grid 配置矩阵 + 穷举矩阵验证器) → M10 = 0.4.0.alpha1(评分池/预算拆分/多格式并行/逐输出事件 + HEIF grid·still 档·tune·RC + JXL 治理 43→31 键 + 格式预设与自适应策略) → M11 = 迭代修复批(HEIF tile 按 Level/still 推导 C17' + 自适应 DSL v1.1 断点 + 5 条预置 + 运行状态机修复) → M12 = 0.4.0 正式版(M11 批 + 用户 5 条反馈 + CI 优化) → M13 = 0.4.1 正式版(用户 4 条需求:选择项级元数据编辑 + 多选混合值 / 去 CreatorTool / photoshop:DateCreated 同步偏移 / 选点海拔) → **M14 = 0.4.2 正式版(用户 8 条需求:元数据页作用域深梳 + 批量 GPS 规则退役 / 大预设重设计 / 元数据详细编辑器深查 / 预览 ICC 色彩管理 / 调度并行自适应 / 真实债(hwy ODR·ProgressMux 竞争)清零 + CI 全绿高效 / build 等目录清理 + 发布) → **M15 = 0.4.3 正式版(0.4.2 用户 6 项反馈修复:HEIC 无损第四模式 / 多选「多个值」界面异常+闪退防御 / JXL 多输出线程公平分配 / 部分 Windows 启动卡死去阻塞+GUI 兼容加固 / 导入含 XMP 的 HEIC 闪退(Exiv2 并发崩溃根治) + 发布收口; docs/m15-report.md)** |
| 质量面 | 金样断言级 **20 对** 全绿(`SMOKE total=20 pass=20 fail=0`);ctest **52/52**(release-dev,含 ui_smoke)+ **51/51**(release 树);`--ui-smoke` 冻结行 `UI-SMOKE OK shots=12 pages=3` 逐字不变 + `meta-selection: checked=34 bad=0`(多选聚合/混合值交互/过滤防误写 34 项) + `m8-params: checked=107 bad=0` + `folds: checked=11 bad=0` + `layout-audit 1124/0`;格式 `check 247 files → violations=0`(钉版 clang-format 23.1.1);tidy `TIDY total=253 baseline=262 files=76 violations=0 seed=0`(未 `--update`);回归 **zero diff**(normalized 2567 = baseline 2567;0.4.3 版本行机械换锚 = 两平台基线各 12 处 `version=0.4.2→0.4.3` 字节级替换,attribution `version-bump lib=app` 12 段、语义零变化);`ci-versions-check` `OK (10 keys, expect=0)`;`--version` = `PhotoPipeline 0.4.3`(release/release-dev 实跑);本地发行构建 `dist/PhotoPipeline-0.4.3-win64.zip`(make_winzip 五门禁 + E-1/E-2/E-3 烟测全绿);**GitHub Release run 41864935620 attempt 2 success（7/7 job）**、**main-full run 41864930206 success（10/10 job；wall 35m16s，windows job 31m55s 超 24min 预算但未失败，如实记录）**；0.4.2 历史读数与 tag 后修复链见 `docs/0.4.2-report.md` §2。逐条见 `docs/m15-report.md` §2 |
| **开放问题** | ① `docs/deep-review-0.3.2.md`:深度评审台账(P0 ×2 已收口;P1/R3 为长期台账);② `docs/ux-review-14.md`:14 项 UI 定位与方案(M14 复核确认多数已收口,其余属观感/功能建议非债务,状态以该文档为准);③ `docs/m7r25-report.md` §5:识图门禁未覆盖项;④ libde265 对部分 HEVC RExt 码流(如 `super/T2.heic`)仍不可解码;⑤ 高德线上搜索仅账号实测可最终确认;⑥ **M9/M10/M11/M12 未决**:S-6…S-12(P3 登记)、H.265 still 官方命名、iPhone 真机接受度、HEIF/AVIF 矩阵未复跑、`output best`/`tech` 策略校验期拒绝;⑦ **M13 未决**:Linux 回归基线换锚已由 Linux nightly 事前背书外(见 §11 C);金样语料本机不可重生成(M13-A16);选点自动海拔依赖第三方 DEM;⑧ **M14 未决**:nightly asan/tsan 与 Linux regression 的 0.4.2 轮读数待 nightly 事后背书;`main-full` tag 后修复链已终判全绿(run `41525441521`,10/10 job;A27 lint 格式门禁 / A29 Linux ui_smoke SIGSEGV / A30 theme-equiv / A31 rtl-mirror / A32 预算校准);预览色彩管理 Linux 回退 sRGB/4096² 变换 41ms(未达 20ms 目标,2048² 9.4ms 达标);调度单文件长编码期无法中途改 E;ExifEditor 六字段合并/例外点击定位仅人工+GUI 复核;⑨ 详见 §11 已知债;⑩ **M15 未决**:混合「多个值」框闪退本机未复现(真实事件链 25+ 交互点×3 轮、无 WER;建议用户侧 WER LocalDumps / nightly ASan)、部分 Windows 启动卡死具体版本/驱动/网络 profile 未真机取证、HEIC 无损 Level 因 x265 硬约束不下发(如实告警,U3)、AVIF chroma Lock 维持旧语义、JXL A/B wall 受并发构建噪声(单轮 encode_ms 2.4x 为可引用读数)、**导入含 XMP 的 HEIC 闪退（反馈 #6）本机未直接复现（根因=Exiv2 0.28.8 XmpParser::initialize 非线程安全 + 崩溃偏移反汇编证据链；修复=Exiv2 串行化+call_once，A14/U7）**;详见 `docs/m15-adjudications.md` U1–U7 |

---

## 3. 仓库地图

```
CMakeLists.txt          构建主文件(版本单源在 :19;pp_core 静态库 + photopipeline + 4 个工具)
CMakePresets.json       release / release-dev / dev(ASan+UBSan) / tsan / asan(MSVC)
CHANGELOG.md            变更日志(发行段落有固定体例:发行/新增/修复/变更)
README.md               面向用户的全量说明(构建、工具、发行、系统要求)——口径与本文一致
presets/                出厂格式预设数据文件(11 个 JSON,单一真源;随发行包分发、首启字节复制 seed 到数据目录;
                        人类可直接编辑/复制;每格式 default + HEIF Apple自适应 + JXL 无损归档/自适应压缩)
src/
  core/                 管线核心:types/params/fsops/logger/pipeline/scheduler/metadata/
                        colormanager/presets/settings/classify/progress/pixelbudget/thumbs/…
                        + simd/(自研热路径 AVX2 内核:flatten/quantize/transpose/downscale/interleave)
  codecs/               8 个编码器直连实现:enc_jpegli/enc_jxl/enc_heif/enc_webp/enc_oiio(jpeg)
                        + encoder.h(IEncoder)/encoder_registry.h(自注册)/format_tables.cpp(参数事实表)
  decode/               OIIO 统一读入(oiio_reader)
  mapwidget/            自绘 slippy 地图:coord(GCJ↔WGS)/dms/layers(瓦片事实表)/geocoder(搜索事实表)
                        providers(薄适配层,冻结)/mapwidget(绘制+交互)
  platform/             frameless(无边框)/mica(Win11 材质)/paths(便携落盘)
  ui/                   主窗口与三页(mainwindow/page_meta/page_output/page_run)、formkit(表单体系)、
                        theme.h(令牌单源)、icons(矢量图标)、paramform、exif_editor、
                        classify_panel、preview_panel、thumbnails、settings_dialog、presets_dialog、map_dialog
tools/                  env.py(环境注入,**所有命令都要经它**)、bootstrap.py(一键引导)、
                        gen_corpus.py、regression.py、ci-*.sh/py(格式/静态分析/依赖/缓存)、
                        make_appimage.sh、make_winzip.py、pp_verify、pp_linkprobe、pp_mkfixtures、pp_spikes、
                        baseline/(回归基线 + tidy 棘轮基线)、bin/(钉版 clang-format/appimagetool + .sha512)
tests/
  unit/                 27 个 test_*.cpp(纯函数/契约单测;ctest 自动收编)
  golden/               语料 base/edge/meta(27 fixture)+ smoke/(20 对断言级 expected.json)
                        + SCHEMA.md(断言 schema,**M0 冻结**)+ smoke.py + ui_smoke.py
docs/                   设计、任务、报告、评审(见 §8 文档地图)
.github/workflows/      pr-fast / main-full / nightly / release / warm-cache
vcpkg-overlay/          超前版本 overlay port(jpegli 拦截 port、x265 multilib 等)
triplets/               x64-windows-avx2 / x64-linux-avx2(只承载命名身份,不注入 ISA flags)
```

被 `.gitignore` 排除、**不要提交也不要去补**:`build/`、`vcpkg*/`、`.toolchain/`、`.cache/`、`tests/golden/CHECKSUMS`、`tests/golden/real/`、`dist/`、一切密钥类文件(`amap_key*`、`.zcode/secrets/`、`.env*`)。

---

## 4. 架构速览

### 4.1 分层与依赖方向

`ui/`(Qt Widgets)→ `mapwidget/` → `core/`(引擎)→ `codecs/`、`decode/`、第三方库。`platform/` 只做壳层适配。**引擎与 GUI 强分离**(core 不 include Qt Widgets);`--dev` harness 是引擎的无 GUI 验证入口。静态库 `pp_core` 靠 whole-archive 自注册编码器(注册表为空的经典坑)。

### 4.2 单文件数据流(多格式输出,`docs/v0.3.0-design.md` §4.1)

```
probe(一次) → budget.acquire(2×frame)
  → decode(float32) → orient(如启用) → flatten(带 alpha 输入,一次) → color(全局目标,一次)
  → 按 targets 顺序逐个:
      encode(target, progress) → metawrite(target 写路径) → result[target]
  → mtime(一次,按生效 DateTimeOriginal) → release → 源文件终态事件
```

**解码一次、编码 N 次**是多格式输出的性能核心;alpha 拍平与色彩变换全局共享(alpha 明确不支持,输出恒无 alpha 通道)。

### 4.3 线程模型

- **文件级并行**:每个 worker 完整处理一个文件(内部串行);**编码器内部线程全关**。
- **像素预算背压**:同时处理的像素总量设上限(峰值内存 = 2× frame),按需压低并发;`acquire` 处**必须检查取消标志**(死锁防线)。
- **交错启动**(`stagger_ms`)+ **线程预算**(`alloc_threads`,总活跃线程 ≤ T,`docs/v0.3.0-design.md` §8)。
- 任务控制**仅取消**(无暂停/续跑);取消语义幂等。

### 4.4 关键抽象

| 抽象 | 位置 | 要点 |
|---|---|---|
| ParamSchema 参数引擎 | `core/params` + `codecs/format_tables.cpp` | 数据驱动:格式 → 后端 → 技术 → 参数(核心+高级折叠);可见性/锁定谓词驱动;加参数 = 加一行数据 |
| IEncoder | `codecs/encoder.h` | 8 编码器直连原厂 API(jpeg-li / libjxl / libheif+x265 / SVT-AV1·libaom / libwebp / OIIO·PNG·TIFF·BMP);HEIF/AVIF 参数运行时内省 |
| 元数据写路径矩阵 | `core/metadata` + `design.md` §5.2 | **Exiv2 对 HEIF/AVIF/JXL 只读**(查证事实)→ 混合写入路径;仅元数据模式 = 零重编码字节级保真(JPEG/PNG/TIFF/WebP) |
| ColorManager | `core/colormanager` | lcms2 浮点变换,相对比色 + 黑点补偿;目标 keep/sRGB/P3/AdobeRGB;输出恒嵌 ICC |
| Scheduler | `core/scheduler` | 文件级并行 + 预算 + 取消 + 交错;`progress` 三级进度模型(真实行级 / 合成斜纹) |
| 交互模型 | `core/metadata` + UI | 批量规则 + 单文件例外;生效值显示与写路径**同源纯函数**(时间/GPS「原 → 生效」) |

### 4.5 色彩 / 元数据 / 地图口径(三条易错线)

- **像素边界**:灰度仅"保持原样"时原生保留,否则走"源→RGB(A)"单一路径;alpha 拍平底色 + 警示;多页/动图取首页;float→整型标准舍入无抖动;任何降档(位深/有损化)界面上**显式警示**。
- **坐标基准**:公共坐标**一律 WGS-84**(EXIF 合规);高德 GCJ-02(火星)**只在 `mapwidget` 边界内部转换**,选点写回值 = 未加密 WGS-84,可直接写 EXIF。GPS 卡显式标注「WGS-84(标准 GPS)」。
- **密钥**:高德 Web 服务 Key 用户自填、设置页密文录入、**永不入库**(全历史 blob 泄漏扫描是验收项)。

---

## 5. 硬约束(红线)

违反任何一条 = 直接返工,没有例外讨论。

### 5.1 冻结接口(PP-FROZEN)

- 冻结文件在头部带 `// PP-FROZEN(file)` / `PP-FROZEN(interface)` / `PP-FROZEN(0.3.0)` 注释,并在注释里**写明契约出处**(如 `Contract: docs/m1-tasks.md §3.7`)。改冻结面之前**先读文件头**。
- 冻结面包括:`core/types.h`、`core/params.h`、`codecs/encoder.h`、`core/pipeline.h`、`core/scheduler.h`、`core/fsops.h`、`core/metadata.h`、`core/presets.h`、`core/settings.h`、`mapwidget/coord.h`、`mapwidget/providers.h`、`mapwidget/mapwidget.h`、`--dev` CLI 面(`design.md` §3.15)、`tests/golden/SCHEMA.md`、UI 冻结 `pp-*` objectName、`--ui-smoke` 冻结截图集与末行。
- **确需扩展走「加性 + 追认」**:新字段一律**追加在结构体末尾**(保位置式聚合初始化兼容);既有签名/行为零改动;新能力尽量放**新文件**承载;任务提交注明 + 登记 `docs/mN-adjudications.md`(先例:m6 的 A1–A3、m5 的 A28/A29)。大改(重排)只能经**一次性解冻裁定表**(`docs/v0.3.0-design.md` §3),落地后**立即再冻结**。

### 5.2 引擎零改动纪律

GUI / 地图 / 体验类里程碑(M5、M6 先例)以「`src/codecs`/`src/decode` 零 diff、`src/core` 仅加性」为纪律,并由**金样 20/20 + 全语料回归 zero diff 双重背书**。动引擎要有明确理由 + 双重背书口径说明,进报告的「偏差与追认」。

### 5.3 证据面只增不减

- **金样/夹具零改动**(测试数据冻结;`tests/golden/real/` 用户样本不入基线)。
- **断言只增不减**:探针期望值不得为"让测试变绿"而放宽;机械换锚(语义不变)须登记裁定。
- **冻结行逐字不动**:`SMOKE total=20 pass=20 fail=0`、`UI-SMOKE OK shots=12 pages=3`、`--version` 单行输出格式、`pp_verify` 的 `VERIFY <case> OK|FAIL` 行式。
- 回归基线重播种(`regression.py --update`)前必须先复核 diff 归因(版本行 vs 真实行为变化),归因写进提交正文。

### 5.4 版本与文案单源

- 版本号唯一定义点 = `CMakeLists.txt:19`;`--version` 从产物读取(打包脚本不硬编码);发行前同步 CHANGELOG / README 口径。
- GUI 令牌单源 `src/ui/theme.h`;表单栅格单源 `src/ui/formkit_layout.h`;Qt 版本单源 `tools/versions.env`;系统依赖清单单源 `tools/ci-system-deps.txt`。
- 设计权威链(M5-A11):**spec 规则层(`docs/qt-gui-design-spec.md` §4/§5)> 页面细节节(§7)> tokens.css/HTML 原型量化值**;视觉终稿 = `docs/mockups-v2/` 的 8 张 PNG。spec 与 review 冲突时 spec 优先,review 的修法建议可登记后覆盖(先例 A33)。

### 5.5 安全与数据

- **密钥/凭据永不入库**(`.gitignore` 已列 14 行;`.zcode/secrets/`、`amap_key*`、`.env*`);调试凭据放 `.zcode/secrets/` 或本机环境变量。
- 源文件**永远只读**(单文件编辑器亦然);输出不得覆盖源文件是用户数据安全线——R-001(输出路径守卫)+ R-002(写回临时副本+rename)双重收口,见 §11 与 `docs/m7-adjudications.md` A3-2。
- 打包全程**不联网**(appimagetool/runtime 入库 + `.sha512` 校验,缺了就硬失败,不静默在线下载)。

---

## 6. 工程纪律(怎么干活)

### 6.1 节奏:鼓点纪律 + 单测随码走

- **鼓点纪律**(大爆炸开发的唯一前提):每落一个模块,**跑一遍该模块单测 + `--dev` 全语料**;绝不允许大段代码"首次通电"。
- **单测随码走**:单测是当前任务的组成部分,不是收尾作业。新能力 = 实现 + 单测 + 探针一起交付(M6-C10「测试机械化」)。
- `TODO(里程碑号)` 标签纪律:写码时觉得糙的地方全打标,下一阶段开工即有命中清单;收口阶段清零。

### 6.2 三件套文档(milestone 三件套)

每个里程碑/大特性维护三种文档,体例如下:

| 文档 | 体例 | 例子 |
|---|---|---|
| **契约/设计** | 目标、架构契约表(C1…Cn,**主对话定稿,子代理不得擅自变更**)、逐任务规格 | `docs/m6-map-upgrade.md`、`docs/v0.3.0-design.md` |
| **裁定记录** `mN-adjudications.md` | 表格滚动追加:`# \| 裁定 \| 理由 \| 实施归属`;冻结面加性追认、spec 冲突裁定、缺陷追认都记这里 | `docs/m5-adjudications.md`(A1–A47)、`docs/m6-adjudications.md` |
| **收口报告** `mN-report.md` | ① 交付内容 ② 验收结论(命令 + 结果逐条取证)③ 偏差与追认 ④ **未覆盖/未决项(如实)** ⑤ 复现命令与证据路径 | `docs/m6-report.md` |

### 6.3 证据文化

- **如实记录**:失败、偏差、未覆盖、"登记为已知差异不修",都比粉饰受欢迎。"未发生的量不显示"(如无估算器就显示 `—` + tooltip,不编数)。
- **取证优于断言改口径**:渲染面问题看**渲染像素**(GUI-R2 探针先例),不看控件几何;机制问题给复现命令;抑制文件(`lsan.supp`/`tsan.supp`)= **证据文件**(每条规则注释写 happens-before 论证 + 阳性对照),不是"关闭检测"。
- **评审与改动分离**:评审文档(如 `ux-review-14.md`)是只读定位 + 改进方案;**代码改动须经确认后另起提交**,不在评审提交里夹带实现。
- 临时探针/取证脚本放 `build/<task-id>/`(gitignored)**不入库**;需要留证的进报告的命令行与证据路径。

### 6.4 门禁(提交前自查)

| 门禁 | 命令 | 口径 |
|---|---|---|
| 格式 | `bash tools/ci-format.sh` | clang-format 23.1.1 **钉版二进制**(`tools/bin/` + `.sha512`),全源 0 diff 硬门禁;修格式用 `--write` |
| 静态分析 | `python tools/env.py run -- python tools/ci-tidy.py build/release-dev` | **棘轮**:`tools/baseline/clang-tidy.tsv` 告警数只减不增,新代码 0 告警;`--update` 重播种**慎用**且须提交说明 |
| 单测 | `python tools/env.py run -- ctest --preset release` | 全绿 |
| 金样 | `python tools/env.py run -- python tests/golden/smoke.py build/release-dev` | `SMOKE total=20 pass=20 fail=0`;需 `-DPP_BUILD_DEV=ON` 构建树 |
| 回归 | `python tools/env.py run -- python tools/regression.py` | zero diff;三前提不得放宽(workers=1 / 输出目录固定先清空 / 整体幂等),规范化 N1–N9 之外的行**逐字节保留** |
| UI | `QT_QPA_PLATFORM=offscreen … --ui-smoke …` 或 `python tests/ui_smoke.py <build>` | 冻结末行 + 截图 12 张;**UI 类不进 ctest 单测,此即 UI 的回归通道** |
| 密钥 | 按 `docs/m6-report.md` §6 的扫描命令 | 0 命中 |

CI 四道:`pr-fast`(快速) / `main-full` / `nightly` / `release`(tag 触发)+ `warm-cache.yml`(依赖预热);**job 与整轮有硬上限**（0.3.x D16 = ≤20min；**M12 校准（用户授权、判据零放宽、仅 wall-clock）**：main-full windows 预算 24min/硬顶 30、tidy 预算 22min/硬顶 25、整轮 wall-limit 30min；其余流水线与 job 仍 20min；`release` appimage/winzip/publish 12/15/3 不变；依据 `build/m12-ci/report.md`）。

### 6.5 构建与环境(踩坑事实)

```bash
python tools/bootstrap.py                     # 新机器一键:Qt(aqt→.toolchain/)+ vcpkg(pinned tag)+ 仓库内 venv
python tools/env.py run -- cmake --preset release
python tools/env.py run -- cmake --build --preset release
python tools/env.py run -- python tools/gen_corpus.py   # 生成金标语料(幂等;PP_MKFIXTURES= 指向本次构建)
python tools/env.py run -- ctest --preset release
```

- **一切命令经 `python tools/env.py run --` 包装**(注入 vcvars / Qt bin / 仓库内 cmake+ninja;工具链全在仓库内)。
- 并行任务**各用自己的 `build/<task-id>`** 并带 `-DVCPKG_MANIFEST_INSTALL=OFF`(防并发写 vcpkg)。
- `--dev` / `--ui-smoke` / 金样入口只在 `-DPP_BUILD_DEV=ON` 的构建树里;发布构建不含该代码路径(`PP_BUILD_DEV` 宏保护)。
- **Windows GUI 子系统 stdio 坑**(M6-T17 实证):`photopipeline.exe` 是 WIN32_EXECUTABLE,句柄继承式启动时 `GetStdHandle` 全 NULL ⇒ 管道/重定向捕获 0 字节。`env.py run` 已显式转发 stdin/stdout/stderr;自己写子进程包装器时照抄这个做法。
- 依赖升级纪律:vcpkg tag 钉死(`2026.07.29`)+ overlay port;**开发期禁止中途升级窗口**(BB2);超前版本一律走 `vcpkg-overlay/`,升级 overlay 需重跑对应 Spike。
- 打包:`tools/make_appimage.sh`(四项烟测,含许可 ≥30 port)/ `tools/make_winzip.py`(五项门禁,含 offscreen `--ui-smoke`)。

---

## 7. 思想(为什么是这样)

1. **风险前置引爆**:M0 用六个 Spike 把全部外部不确定性(解码/编码/EXIF 载荷/色彩金值/无损重写)提前打爆,再进入编码期。**先查证、后设计**是本仓习惯——决策文档里的每个"已查证"标注都是一次事实核验(OIIO 许可、Exiv2 写支持矩阵、jpegli 位深、…),不是拍脑袋。
2. **大爆炸 + 鼓点**:一次性完成全部编码的前提是节奏纪律;宁可每步小跑全语料,不攒大爆发。
3. **单源思想**:版本、令牌、栅格、schema、参数事实表、URL 事实表——同一事实只定义一次,其余全部引用。URL/参数**逐字入测试**(geocoder/layers 的 URL 事实单测是范本)。
4. **契约文化**:契约先写成文档表(主对话定稿)→ 落成冻结头注释(`Contract: docs/…` 引用)→ 钉成测试探针。已知系统性债:**"契约活在注释里"**(注释承载契约但无机器校验)——改进方向是把契约翻译成断言,不是删注释。
5. **数据驱动 > 复制粘贴**:参数表加一行数据 = 新参数;表单由 formkit 行构造器生成;UI 复制粘贴同构(ElidedLabel×6、卡头×4)是被点名的债(§11)。
6. **边界内转换**:格式/坐标/编码的分歧点(如 GCJ↔WGS、float→int)只在一个边界内部消化,公共面永远是干净的单一语义(WGS-84 / float32)。
7. **小而美**:负面清单是护城河。功能诱惑用"砍掉的复杂度"来定价;桌面软件尺度 5–8k 行引擎是美德,不是寒酸。
8. **诚实显示**:界面上不显示没发生的量;报告里"未覆盖/未决"是正式章节;断言口径由渲染像素/字节保真说话。
9. **工具先行**:`pp_verify`(断言器)、`pp_linkprobe`(依赖哨兵)、`regression.py`(基线)、`ci-format.sh`(钉版格式)都是**判据与本地同源**——"本地绿 → CI 绿"必须是同一条判据。

---

## 8. 文档地图(什么的真源在哪)

| 要查什么 | 真源 |
|---|---|
| 需求定位 / 负面清单 / 决策依据 | `docs/brainstorm-consensus.md` |
| 架构、数据流、元数据语义、里程碑纪律、风险册 | `docs/design.md`(v1.2) |
| 参数语义 | `docs/param-catalog.md`(对照真实 API 头文件冻结) |
| 0.3.0 九项需求设计 + **接口解冻裁定表** + 进度/调度规格 | `docs/v0.3.0-design.md` |
| GUI 视觉体系(令牌/字阶/栅格/caption/壳层)| `docs/qt-gui-design-spec.md`(v6,单源规范) |
| GUI 视觉终稿 | `docs/mockups-v2/`(4 页 × 亮暗 8 张 PNG) |
| 金样断言 schema | `tests/golden/SCHEMA.md`(M0 冻结) |
| 里程碑任务分解 / 实测事实 | `docs/mN-tasks.md`(如 `m1-tasks.md` §17) |
| 开发期裁定 | `docs/mN-adjudications.md` |
| 验收证据 / 复现命令 | `docs/mN-report.md` |
| 地图升级契约 C1–C10 | `docs/m6-map-upgrade.md` |
| 发行逐条变更 | `CHANGELOG.md` |
| 工厂格式预设(出厂数据 / seed / default 语义) | `presets/formats/**`(11 个 JSON,单一真源)+ `docs/format-presets.md`(§2 目录/§7 出厂文件) |
| 自适应策略 DSL / 出厂策略全文 | `docs/adaptive-policy.md`(§6.1 断点/§10 工厂文件策略全文) |
| M12 裁定(工厂预设外部化 / `_builtin` 迁移 / default / 无「未选择」/ JPEG 策略 / CI 预算·基线·scoping / hwy 尝试回退 / 版本) | `docs/m12-adjudications.md`(A1–A13 + 附 1/附 2) |
| M13 裁定(选择项级元数据编辑 / 卡级 active 判据 / apply_rules emit / 回归基线重播种·机械换锚 / ui-smoke 探针换锚 / tidy 豁免 / 构建卫生事件 / 语料不重生成 / 发行记录) | `docs/m13-adjudications.md`(A1–A18) |
| M14 契约(元数据作用域 C1 / GPS/隐私/编辑器 C2 / 大预设 C3 / 预览色彩 C4 / 调度自适应 C5 / 真实债+CI C6 / 清理 C7 / 版本发布 C8) | `docs/m14-tasks.md` |
| M14 裁定(冻结面加性 A1–A8 / 用户可见语义 A9–A14·A24 / 探针机械换锚 A15–A18 / 目录纪律 A20–A21 / 债务核销 A22–A23 / 构建卫生 A25 / tidy 处置 A26 / 格式门禁覆盖面 A27 / tag 后 CI 修复链 A28–A32) | `docs/m14-adjudications.md` |
| M15 裁定(0.4.3 正式版：冻结面加性 A1–A2 / HEIC 无损语义 A3–A4 / MixedComboBox A5 / 启动异步化 A6 / 探针机械换锚 A7–A9 / 日志面零新增 A10 / Win 版本门控 A11 / 升版本 0.4.3 发布 A12–A13 / **Exiv2 并发崩溃运行时保护 A14（新文件 metadata_runtime.h）** / 未决 U1–U7) | `docs/m15-adjudications.md` |
| 0.4.3 正式版报告(用户 6 项反馈根因与逐条证据 / 门禁读数 / 未决 / tag/Release 与 dist 指纹 / 复现命令) | `docs/m15-report.md` |
| 0.4.2 发行报告(逐条读数 / 偏差与追认 / 未决 / 复现命令 / 真实 GUI 与 A/B 证据) | `docs/0.4.2-report.md` |
| 0.4.1 契约(选择项级编辑 C1–C9 / CreatorTool / photoshop:DateCreated / 选点海拔) | `docs/m13-tasks.md` |
| 0.4.1 发行报告(读数 / 偏差 / 未决 / 复现命令) | `docs/0.4.1-report.md` |
| 0.4.0 发行报告(已发布版；读数已回填；tag/Release/资产指纹) | `docs/0.4.0-report.md` |
| 用户视角全量说明 | `README.md` |
| 开放评审 | `docs/deep-review-0.3.2.md`(P0/P1 清单 + 发行裁决)、`docs/ux-review-14.md` |

---

## 9. 命名与编号惯例

- **里程碑/波次/任务**:`M6-T13` = 里程碑 6 · 任务 13;波次 `W-M1…W-M6`;裁定条目 `A1…`(入 adjudications);缺陷编号 `R-nnn`(评审)、`D-n`(实现期发现)、`BBn`(大爆炸风险)、`N1–N9`(回归规范化规则)。
- **提交信息**:`<SCOPE>: <一句话概括>` 为标题,正文自由展开但要**可复核**——根因、验证命令与结果、复现路径。SCOPE 惯例:`M6-T13:` / `REL-0.3.2-prep:` / `GUI-R2:` / `M5-A5:`。发行纪律:tag 指向发行树,**报告类提交不入 tag**;tag 资产挂载后**不可再移动**。
- **探针落点**(UI 回归):`mainwindow.cpp` 的 `smoke_probe_*` 段 + `tests/ui_smoke.py` 前缀清单 = 标准落点(M5-A26);新增截图同名单重渲,冻结集不动。
- **文档语言**:中文;代码注释中英混合但**契约注释必须可检索**(引用文档路径 + 章节号)。

---

## 10. 给智能体的操作守则

1. **动码先读契约**:改任何文件前读文件头的 `PP-FROZEN` / `Contract:` 注释;改 UI 先翻 `docs/qt-gui-design-spec.md` 的规则层;改地图先翻 `docs/m6-map-upgrade.md` 契约表;改参数先翻 `docs/param-catalog.md`。
2. **命令经 `tools/env.py run --` 包装**;并行任务用自己的 `build/<task-id>` + `-DVCPKG_MANIFEST_INSTALL=OFF`。
3. **交付 = 实现 + 测试 + 证据**:新能力必须带机械化探针(单测或 `--ui-smoke` 探针),提交正文带验证命令与结果;大改动补进 `mN-report.md` 的体例章节。
4. **冻结面确需改动**:只走「加性 + 追认」(结构体末尾追加 / 新文件承载 / 既有行为零改动),登记 `docs/mN-adjudications.md`,提交注明。
5. **不放宽任何断言**;金样与夹具零改动;回归重播种前先归因并写进提交正文。
6. **负面清单不扩义**:新需求先对照 §1.3;拿不准就问,别顺手加功能。
7. **密钥永不入库**;临时脚本/探针放 `build/` 不入库;截图/取证产物要么进 `docs/` 证据目录并登记,要么留在 gitignored 目录并在报告里给路径。
8. **门禁自查**(§6.4)后再收工:format 0 diff、相关单测、受影响的 smoke/regression/ui-smoke。
9. **破坏性动作先确认**:删除/覆盖用户文件、移动 tag、发布 Release、`--update` 重播种基线、改 CI 判据——这些先向用户确认。
10. **如实汇报**:测试没跑就说没跑;有 diff 就贴归因;做不到的写进"未决项",不留"看起来完成了"的假象。评审意见与代码改动分开提交。

---

## 11. 已知债与开放问题(如实)

> **0.4.2 / M14 口径（用户原话：「修复所谓的设计债，不存在这种东西。CI 必须全绿，必须高效」）**：
> 本节在 M14 收口时**逐条核销**——已修 / 已证伪（原记录已失效）/ 如实未决；不再以「设计债」作为不修
> 理由，未被核销的条目必须带复现条件与证据面。核销证据见 `docs/0.4.2-report.md` §2.7/§3/§4 与
> `docs/m14-adjudications.md` A7/A8/A22/A23。

**A. M14 已清零的真实债（原 §11 登记项，0.4.2 已修并取证）**

- **hwy dispatch ODR 链接期 UB**（原 asan 3/9 SEGV `jpegli::VectorSize()` pc=0；Linux/AVX-512 触发）：
  0.4.2 根治 = 本批 SSIMULACRA2 TU 加 `hwy=pp_ssim2_hwy;HWY_HEADER_ONLY=1` 私有化 + 新增
  `src/core/metric/hwy_bridge.cpp` 把 `GetChosenTarget/SupportedTargets` 转发真实 libhwy（同一份
  ChosenTarget 状态/CPU 探测 ⇒ 动态派发目标与修复前逐位相同）。**数值零漂移**（10 条 oracle 逐位一致）
  + 符号审计（我方 TU 的 `hwy::` 定义符号 178→0）。Linux asan/AVX-512 路径由 nightly 事后背书。
- **ProgressMux/FileEvent 数据竞争**（原 TSan 9 条）：0.4.2 根治 = `run_one_file`/`run_metadata_only`
  入口把 EventFn 包成**单线程 drain 队列**（不持锁回调、事件不丢/不改序/同 run 内严格串行）；Linux TSan
  由 nightly 事后背书（本机 Windows 无 TSan，如实登记）。
- **地图 EventFilter 生命周期 heap-use-after-free**（0.4.2 MSVC ASan 实测发现）：`~MapWidget()` 在 Impl
  析构前摘滤波器并置空 `impl`；ASan 复测无报告。
- **MSVC ASan 门禁缺口**：`PP_SANITIZER_BUILD` 检测纳入 `PP_ASAN`（与 M12-A12 同口径 scoping；release/
  release-dev 判据逐字保留），本机 ASan 可作为 nightly asan 等效验收面。

**B. 已证伪/已失效的历史「债」记录（M14 复核，逐条以代码/读数取证）**

- 「`mainwindow.cpp` 上帝对象 1.1 万行、约 7600 行探针编进发布二进制」→ **已失效**：`mainwindow.cpp`
  现 1,864 行；探针在独立 TU `src/ui/smoke_probes.cpp` 且 **`PP_BUILD_DEV` 门控**（发布构建不含探针代码）。
- 「UI 建造器复制粘贴（ElidedLabel×6、卡头×4、make_button×3）」→ **已失效**：建造器已收口
  `formkit`/`theme`/`dialogkit`（`formkit::make_button`、卡头构造器、`ElidedLabel` 单源；残余仅一行为
  转发薄包装，不构成复制粘贴）。
- 「回归基线 runner-shaped 耦合（thread_budget=4）」→ **已失效**：`regression.py` N11 已归一
  `thread_budget/alloc_files/encode_threads/sum_encode_threads/peak_sum_encode_threads` 并丢弃核数条件行
  （0.4.2 集成期实测本机 16 核 zero diff）。
- 「契约活在注释里、无机器校验」→ 已按 §7.4 方向把契约翻译成单测/探针（`test_*_contract`、UI 探针、
  `pp_verify`）；M14 新增 `meta-follow-live`/无选中空态/规划器/事件压力等机械化断言，继续按此方向推进。
- 「代码 TODO 债」→ 全仓 `TODO(` 标签 **0 条**（M14 grep 复核）。
- 「依赖仓库内预置 `build/` 临时文件」→ 用户要求清空 `build/` 后审计：主流程/CI/打包**零功能依赖**
  （构建树现场生成）；曾被脚本默认值引用的一次性证据路径已做**缺失容忍 + 跳过日志**
  （`heif_matrix_check.py` / `perf_smoke.py` / `pp_progress_audit.py`）；历史文档中的 `build/**` 证据引用
  统一登记为「已随用户授权清理失效，权威结论已入 docs/ 契约/裁定/报告」。
- 「platform 代画存量」→ GUI-R2 已收口 10 项；M14 探针（`layout-audit`/`icons-engine`/`caption-*`）持续值守。

**C. 如实未决（不宣称已解决；非「债」而是环境/硬件/外部限制）**

- **0.4.3 CI 读数**：`release.yml` run `41864935620` = **success**（attempt 2，7/7 job；attempt 1 的 winzip-pack 在 E-3 WIN-LAUNCH 上 `windows=3 visible=0 reason=timeout-no-visible-window` 失败，`rerun-failed-jobs` 重跑通过 ⇒ 判定 runner 会话窗口可见性抖动，非本版启动路径回归）；`main-full` run `41864930206` = **success**（10/10 job，wall 35m16s，windows job 31m55s 超 24min 预算但未失败，如实记录）；`nightly`（asan/tsan/Linux regression）的 0.4.3 轮读数仍待 nightly 事后背书，**不宣称 nightly 通过**。
- **CI 读数（0.4.2 轮，历史）**：`main-full` tag 后修复链终判 run `41525441521`（main @`b29d666`）= **success（10/10 job，wall 28m55s<40min）**；`nightly`（asan/tsan/Linux regression）的 0.4.2 轮读数仍待 nightly 事后背书，**不宣称 nightly 通过**（本地等价门禁 = 证据面，不是 nightly 读数）。
- **nightly asan/tsan 的 Linux 真绿**待推送后事后背书（本机 Windows：ASan 可跑、TSan 不可跑；
  AVX-512 路径数值一致性 = 机制论证 + CI oracle 读数背书）。
- **Linux 回归基线机械换锚**（本机无法运行 Linux 二进制；0.4.2 版本行字节级换锚 sha256 见报告）
  待 nightly Linux regression job 事后背书。
- **金样语料不可重生成**（`gen_corpus.py` 与 M0 定格字节漂移，沿用入库语料；不 `--relock`，M13-A16）。
- **HEIF/AVIF 穷举矩阵、iPhone 真机接受度**：本轮未复跑/未验（无对应真机/环境），沿用 0.4.0 登记。
- **libde265 对部分 HEVC RExt 码流**（如 `super/T2.heic`）仍不可解码（第三方解码器能力边界）。
- **高德线上搜索**仅账号实测可最终确认（本机 TLS 环境不可用）。
- **选点自动海拔**依赖第三方公开 DEM（Open-Meteo）可达性/配额；失败静默降级（不阻塞核心流程）。
- **预览色彩管理**：Linux 无系统显示 profile 时回退 sRGB（如实登记）；多显示器/显示器 profile 热变更
  的覆盖范围见 `docs/0.4.2-report.md` §4。
- **调度自适应**：单文件长编码期间无法中途改 E（编码器接口限制）；各编码器实际并行上限为实测事实表，
  未知编码器按保守计 1；规划轨迹走 debug 日志。
- **UX 评审项**（`docs/ux-review-14.md`）：M14 复核确认 0.3.x–0.4.x 已收口多数（右键菜单/全选/预览
  分辨率/GPS 卡 App 化/启用勾选/样式收口/无边框三钮/格式卡等宽/高级参数限高等有代码与探针证据）；
  其余属观感/功能建议（非债务），状态以该文档为准，M14 未扩范围。

**出厂预设数据维护点(M12 新增，0.4.2 仍适用)**:`presets/formats/**` 是**出厂数据单一真源**——改预设内容必须同步(① `tests/unit/test_format_presets.cpp` 的工厂库/seed/迁移断言;② 打包门禁 `tools/make_winzip.py`/`make_appimage.sh` 的「8 格式 default 齐备 + 总数 11」;③ `docs/format-presets.md` §7 与 `docs/adaptive-policy.md` §10 策略全文;④ `--ui-smoke` 相关探针)。旧 `_builtin-*` 只读模板/migration 逻辑不得回退(破坏性迁移已登记 `docs/m12-adjudications.md` A2)。

---

## 12. 附:常用命令速查

```bash
# 构建四步(任何命令都经 env.py)
python tools/bootstrap.py
python tools/env.py run -- cmake --preset release
python tools/env.py run -- cmake --build --preset release
python tools/env.py run -- ctest --preset release

# 引擎验证树(--dev / 金样 / ui-smoke 用)
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev

# 语料 + 金样 20 对 + 回归
PP_MKFIXTURES=build/release-dev/pp_mkfixtures python tools/gen_corpus.py
python tools/env.py run -- python tests/golden/smoke.py build/release-dev
python tools/env.py run -- python tools/regression.py          # --update 重播种(慎)

# UI 冒烟(offscreen,12 张截图 + 冻结末行)
QT_QPA_PLATFORM=offscreen build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review

# 门禁
bash tools/ci-format.sh                                        # --write 就地修
python tools/env.py run -- python tools/ci-tidy.py build/release-dev

# dev harness 示例(仅 PP_BUILD_DEV=ON 树)
build/release-dev/photopipeline --dev tests/golden/base/*.png --out .cache/out --format jxl --workers 1
# 退出码 = 失败文件数;sidecar <产物>.pp.json + progress-trace.jsonl(schema 见 tests/golden/SCHEMA.md)
```

---

*本文随项目演进维护;发现口径漂移请直接修订并在提交中说明。*
