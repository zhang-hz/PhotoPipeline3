# PhotoPipeline · AGENTS.md(智能体 / 新协作者工作手册)

> **这份文档是什么**:在本仓库工作的 AI 智能体与新协作者的总导航 + 纪律总纲。目标是让你不必读完 docs/ 下 1.1 万行文档,就能回答四个问题:**这个项目要什么、东西放在哪、哪些红线不能碰、干完活要留下什么证据**。
> **权威链**:本文只是导航与纪律;细节真源永远是它指向的那份文档或代码。两者冲突时**以真源为准**,并顺手回来修订本文。
> **版本口径**:0.3.5.alpha2(版本号单源 = `CMakeLists.txt:19-21`:数字段 `project(PhotoPipeline VERSION 0.3.5)` + 预发布标签 `PP_VERSION_LABEL`;CMake `project(VERSION)` 只收数字段,标签拼在字符串侧。任何其它地方出现的版本号都是引用,不是定义)。

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

## 2. 当前状态(2026-10-25)

| 项 | 状态 |
|---|---|
| 版本 | **0.3.5.alpha2**(0.3.5 预发布)已按 REL 流程发行;0.3.3 Windows zip 为坏件(启动 P0,见 `docs/0.3.4-report.md`);CHANGELOG `[0.3.5.alpha2]` 为终稿 |
| 里程碑 | M0 环境/六 Spike/冻结面 → M1a 引擎 + M1b UI → M2 Linux 收口 → M3 Windows(=0.2.0) → M4/M5 = 0.3.0(九项需求 + GUI v6 刷新) → 0.3.1 GUI 缺陷根治 → M6 = 0.3.2 → M7 = 0.3.3(工程收口,Windows 启动 P0)→ 0.3.4 热修 → 0.3.5.alpha1/alpha2 预发布 |
| 质量面 | 金样断言级 **20 对** 全绿;全语料回归 **zero diff**;ctest 36/36 全绿;`--ui-smoke` 冻结行未变 + 识图门禁 `layout-audit`(六类几何/像素断言,`checked=1027 bad=0`);winzip 门禁 6 项(E-3 真机启动探针 `WIN-LAUNCH OK`) |
| **开放问题** | ① `docs/deep-review-0.3.2.md`:发布前深度评审,**2 个 P0(数据破坏)+ 20 P1** 待修;② `docs/ux-review-14.md`:14 项 UI 问题定位与改进方案(只读评审,**改动须经用户确认后另起提交**);③ `docs/m7r25-report.md` §5:识图门禁未覆盖项;④ libde265 对部分 HEVC RExt 码流(如 `super/T2.heic`)仍不可解码;⑤ 高德线上搜索仅账号实测可最终确认(本机 TLS 环境不可用);⑥ 详见 §11 已知债 |

---

## 3. 仓库地图

```
CMakeLists.txt          构建主文件(版本单源在 :19;pp_core 静态库 + photopipeline + 4 个工具)
CMakePresets.json       release / release-dev / dev(ASan+UBSan) / tsan / asan(MSVC)
CHANGELOG.md            变更日志(发行段落有固定体例:发行/新增/修复/变更)
README.md               面向用户的全量说明(构建、工具、发行、系统要求)——口径与本文一致
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
  → decode(float32) → orient(如启用) → color(全局目标,一次)
  → 按 targets 顺序逐个:
      [首个破坏 alpha 的 target 前原地 flatten]
      encode(target, progress) → metawrite(target 写路径) → result[target]
  → mtime(一次,按生效 DateTimeOriginal) → release → 源文件终态事件
```

**解码一次、编码 N 次**是多格式输出的性能核心;色彩变换全局共享。

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
- 源文件**永远只读**(单文件编辑器亦然);输出不得覆盖源文件是用户数据安全线——当前已知 P0 缺口见 §11,修复属高优。
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

CI 四道:`pr-fast`(快速) / `main-full` / `nightly` / `release`(tag 触发)+ `warm-cache.yml`(依赖预热);**job 与整轮双 ≤20min 硬约束**。

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

**发行前高优(来自 `docs/deep-review-0.3.2.md`,2026-10-11):**

- **P0 ×2(数据破坏,老问题非 M6 引入)**:① 输出路径可精确命中源文件(渲染路径后不与 `fs::equivalent` 比较 + 覆盖策略直写)→ 原图被重编码替换;② Exiv2 就地截断重写、写失败被吞成 warning 且 `ok=true` → 坏文件当成功。修复都是几行守卫(`plan_targets`/`run_metadata_only` 拒绝 `out==src`;写回走临时文件 + rename,失败必须 `ok=false`)。
- **P1 代表项**:联想请求不清算(陈旧建议可点进写坐标闭环)、PageRun 硬编码 Dark 令牌(浅色主题整页深色)、classes.json 损坏被默认值覆写、回归防线机制缺口(重播种无护栏 / Windows 基线从不 diff / UBSan 假绿 / 金样不锁语料 / 棘轮可静默下线)。
- **R3 裁决**(工程/GUI 综合):代码不是屎山(分层、注释纪律、错误处理属上游水平);真正的系统性问题是**「契约活在注释里」**与**上帝单元/复制粘贴维持的同构**。另有 4 条发行前值得修:版本徽标硬编码 0.3.0、过滤态双击编辑错文件、翻图/打标对象错位、分组重建吞多选。

**结构性债:**

- `mainwindow.cpp` 上帝对象(1.1 万行,其中约 7600 行是自检探针且编进发布二进制)。
- UI 建造器复制粘贴(ElidedLabel×6、卡头×4、make_button×3)→ 收口进 formkit/theme。
- 回归基线 runner-shaped 耦合(`thread_budget=4`,4 vCPU runner;GitHub 改 runner 规格会触发 nightly diff,已登记)。
- 平台代画存量已按 GUI-R2 收口 10 项,但新增控件仍要自查"设计要求自绘、实现却让平台画"。

**UX 开放 14 项**(`docs/ux-review-14.md`,含定位、根因、改进方案):右键菜单、全选/取消全选、预览分辨率(内嵌预览门槛 64px 误用)、GPS 卡地图 App 化、功能启用勾选消失、样式收口进公共文件、无边框三钮点不动(`WM_NCCALCSIZE` 缺失)、高级参数展开区限高、格式卡等宽、HEIF/AVIF 无 profile 且 nclx 写死 sRGB 与 ICC 矛盾、滚动条两套皮、亮色设置页签色错、窗口圆角缺失、色彩转换质量边界。

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
