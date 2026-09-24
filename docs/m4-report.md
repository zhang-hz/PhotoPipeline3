# PhotoPipeline M4 收口报告（0.3.0 · 多格式批处理工作台）

- **里程碑**：M4 = **0.3.0**（任务书 `docs/m4-tasks.md`；依据 `docs/v0.3.0-consensus.md`（5 轮 × 21 项决策）
  + `docs/v0.3.0-design.md`（接口/实现定稿））。
- **基线**：M3 发行 0.2.0（`docs/m3-report.md`）。
- **版本**：本里程碑把版本单源由 `0.2.0` 提升为 **`0.3.0`** —— 唯一功能定义点 = `CMakeLists.txt:3`
  `project(PhotoPipeline VERSION 0.3.0 LANGUAGES C CXX)`；`vcpkg.json:3` 同步；`cmake/version.h.in`
  生成 `PP_VERSION_STRING`，`--version` 与"关于"页同源。本机实测（T21 轮）：
  `PhotoPipeline 0.3.0`（`rc=0`）。
- **提交锚点**：W0–W5 的落地提交见 §3；**本报告不声明最终 HEAD**，当前状态以 `git log` 为准。
- **发行纪律（本任务硬边界）**：本轮**不含** `git push` / `git tag` / `gh release`。
  tag `v0.3.0` → GitHub Release 待**用户验收授权**后由主对话执行。
- **口径**：本报告沿用 M3 报告口径 —— **不圆场**、逐字证据、未决项如实。凡本机未实测者
  一律标「未跑」或「待 CI 首跑」，不写推测值。

---

## 1. 总出口 8 条逐条状态（`docs/m4-tasks.md:10-19`）

| # | 出口项（任务书原文摘要） | 状态 | 证据 / 未决 |
|---|---|---|---|
| 1 | **九项需求逐条验收**（共识 §1 表 + 设计 §1）全绿 | ⚠️ **8/9 实测绿** | 逐条见 §2；需求 **7（CI）**属"待 CI 首跑"（本机无法实测 wall-clock/runner）；需求 9 的 `flatten ≥2×` 判据**部分未达**（≥2048² 实测 1.69×/1.54×，见 §7，如实记录） |
| 2 | 金样 **20/20**（16 旧对不回退 + 4 新对）；ctest 全绿；UI 冒烟冻结行 `UI-SMOKE OK shots=12 pages=3` | ✅ **实测绿** | `SMOKE total=20 pass=20 fail=0`；`ctest --preset release` **27/27**（提交后复跑 10.89 s，`build/w5-logs/t21-post-ctest.log:59`）；`ctest --preset release-dev -R ui_smoke` **1/1 Passed 16.38 s**（`t21-post-uismoke.log`）（`UI-SMOKE OK shots=12 pages=3` 逐字由 `tests/ui_smoke.py:48` 断言） |
| 3 | **CI 四道**（pr-fast / main-full / nightly / release）全绿且双 ≤20min | ⚠️ **待 CI 首跑** | 四道 YAML 全部落地（`.github/workflows/`）+ 每 job `timeout-minutes: 20` + 预算表注释；本地可验证项（fmt 门禁 / YAML 解析 / 命令面核对 / 预算表核对）全绿（W4-T16 记账）。**wall-clock 与 runner 行为本地不可测** ⇒ 以首次 CI 运行为准（§10） |
| 4 | **依赖升级表**（设计 §10）落地：全升项到位、停留项零漂移；`pp_linkprobe` 扩展断言全绿 | ✅ **实测绿** | 逐库版本实测见 §8.4（与设计 §10 目标逐条一致）；`pp_linkprobe` 本机单跑 **rc=0 / 11 条 PROBE 全 OK**（含 `libheif-params-hevc`/`-av1` 内省面断言）；ctest 条目 #23 `linkprobe` 亦 Passed |
| 5 | **AVX2**：全产物 ISA 基线；SIMD 5 对一致性单测全绿；基准无回退 + 多格式解码共享达 1.35× | ⚠️ **口径修订后达成** | 自研 targets `/arch:AVX2 /GL /LTCG` 落地（`cmake/avx2.cmake`）；**依赖侧 ISA/LTO flags 经裁定撤除**（勘误 f，两次实测引爆：libwebp 无损解码崩溃 / aom IL 对象与 `__create_def` 冲突）——AVX2 改走各库自身机制（OIIO `USE_SIMD=avx2`、libwebp `WEBP_ENABLE_SIMD`、NASM/Highway 运行时派发）。5 对单测绿（`test_simd`）；多格式 ratio **0.932 ≤ 1.35** ✅；`flatten` 微基准 4.08×@256² / 2.03×@1024² / 1.69×@2048² / **1.54×@4096²**（≥2× 仅在 ≤1024² 成立，如实） |
| 6 | **接口再冻结**完成：§3 裁定表全部落地并重新标注 PP-FROZEN | ✅ **实测绿** | 冻结头 `PP-FROZEN(0.3.0)` 随落地逐文件标注；表外缺口与命名映射逐条登记（勘误 a–r，§6） |
| 7 | **双平台产物**（AppImage + win64.zip）五门禁 + 烟测通过；tag → Release 自动化通过含前置探测 | ⚠️ **部分/待授权** | Windows 侧：T18 本机五项门禁全过（0.2.0 时点；指纹 34,484,306 B / 104 条目）；**0.3.0 版本的打包轮未在本机重跑**（属发布轮动作，见 §10）；Linux AppImage 侧待 CI 首跑；**tag/Release 动作按任务硬边界不执行** |
| 8 | **文档收口**：任务书执行记录 + `docs/m4-report.md` + CHANGELOG 0.3.0 + README 增补 | ✅ **实测绿** | 本文 + `CHANGELOG.md` 的 `[0.3.0]` 节 + `README.md` 增补（0.3.0 特性 / 构建依赖表 / `PP_ASAN`·`PP_UI_THEME` 开关说明 / AVX2 CPU 要求 / 陈旧事实订正）+ 设计勘误逐条落地（§6） |

---

## 2. 九需求验收对照（共识 §1 表 + 设计 §1 验收目标）

| # | 需求 | 设计 §1 验收目标 | 落地 | 证据 | 状态 |
|---|---|---|---|---|---|
| 1 | 预览 / 分类 / 勾选 | 点选 **250ms 内**出预览图；热键打标 0 交互成本；勾选圈定运行集合 | `core/thumbs.decode_preview(path,2048)`（内嵌预览优先 + 全解码降采样，独立池 ≤2 + 16 张 LRU）；`core/classify` 注册表 + `classes.json` 持久化；`CheckState` role + 分组 QTreeView | `test_thumbs` / `test_classify` Passed（ctest #21 / #1）；`--ui-smoke` 12 张截图含分类与列表段 | ⚠️ **250ms 部分达成**：内嵌预览命中路径满足；**无内嵌预览的 48MP 输入实测 535.1 ms**（`--dev bench` 场景 `preview48`，含一次全解码）——如实记录，未改判据；真实 48MP 点选的主观手感仍属 W5 手动走查项（§10） |
| 2 | 生效值元数据 | 时间/GPS/关键信息随选中文件即时显示"原→生效"，零修改也显示 | `metadata::preview_effective()`（与 `build_plan()` 同源纯函数）；元数据页关键信息卡 + 时间对照行 + GPS 生效值 + 地图跟随；多选小表 | `test_effective` Passed（ctest #4）；`test_metadata` Passed；`--ui-smoke` 的 meta-strip / meta-gps 探针断言（`mainwindow.cpp:5121-5147`） | ✅ |
| 3 | 多格式输出 | 1 输入 → N 格式，**解码仅一次**；路径模板定义结构；默认兼容 v0.2 | `RunConfig.outputs[]` + `OutputTarget`；`fsops::render_output_path()`（5 符号 + 校验）；逐输出冲突解析；flatten 编排（2×frame 不变） | 金样 `multiformat-split` / `multiformat-mirror` / `multiformat-conflict` 三对 PASS；`test_fsops`（模板全组合）Passed；bench `multifmt` ratio **0.932**（解码共享实证） | ✅ |
| 4 | 逐文件进度 | 行级真实进度（4 格式）+ 合成进度（3 格式，斜纹）；逐输出行粒度 | `core/progress`（权重表 / `ProgressSynth` / `ProgressMux` 节流）；编码器 progress 映射；运行页源文件分组行 + 逐输出子行 + 总览 | `test_progress` Passed（ctest #16）；金样 `progress-trace` 对 PASS（单调不倒退 / synthetic 口径 / 终态达 1.0）；`--ui-smoke` 的 `03d-run-rows` 探针态截图（真实/斜纹/失败三类同行） | ✅ |
| 5 | 并行 | 交错启动可配置；总活跃线程 ≤ T；大文件自动获得编码器内部多线程 | 交错限速器（`next_start` 全局闸，0–2000ms）；`alloc_threads(T, remaining, inflight)` 纯函数 + 并发闸；`encode_threads` 映射到各库 API | `test_scheduler_contract` Passed（**4.25 s**，`build/w5-logs/t21-ctest.log:38`；含 alloc_threads 穷举与限速器时序）；`test_settings` 覆盖新设置往返 | ✅ |
| 6 | GUI | 原型稿全量落地；明暗双主题；运行期锁定保留 | 无边框窗口 + 自绘 caption 三钮（`platform/frameless`）；三栏骨架 + 三页内容区；`ui/theme` tokens 双主题；运行期锁定（G5） | `--ui-smoke` 12 张截图 + `UI-SMOKE OK shots=12 pages=3`；T20 双主题走查留档 `.cache/walkthrough/{dark,light}/` 各 15 张（主窗亮度 self-证 dark 67~82 / light 199~202） | ✅ |
| 7 | CI | 快慢道三流水线；job 与整轮双 ≤20min；四类质量门禁 | `pr-fast.yml` / `main-full.yml` / `nightly.yml` / `release.yml`（+ `warm-cache.yml` 播种）；fmt 硬门禁 + tidy 棘轮 + sanitizer 三路 + 许可/SPDX 审计 | 本地：YAML 全解析（T16）、fmt 门禁（T4/W0 起）、命令面与预算表核对；**实跑待 CI 首跑** | ⚠️ 待首跑 |
| 8 | 依赖 | §10 表内全升项落地 + 金样全绿；停留项白纸黑字 | overlay bump 九项 + 滚动对齐上游；停留项（OIIO 3.2 / exiv2 0.29-dev / jpegli 钉 commit / SVT 不跨 4.x / Qt 不回 6.8） | §8.4 逐库实测版本；`pp_linkprobe` 11/11；金样 20/20 | ✅ |
| 9 | 性能 | 全部目标产物 AVX2 基线；5 条自研热路径 SIMD 化 | `cmake/avx2.cmake` + 自定义 triplet；`src/core/simd/` 5 对 `*_avx2/*_ref` | §7（bench 实测 + `test_simd` 一致性断言） | ⚠️ 口径修订（勘误 f）+ flatten ≥2× 部分未达（如实） |

**「不做」清单复核**（共识 §5 全 18 项，逐项点名）：RAW/DNG 输入 · GIF 输出 · 输出结果预览（编码前后
对比）· 编码前效果试算 · 图像编辑（缩放/裁剪/水印/锐化/降噪）· HDR/色调映射 · 断点续跑/队列持久化 ·
IPTC-IIM · Google 底图 · CMYK 输入 · CLI · 自动选格式的『魔法』 · 图库/评级/审阅工作流 ·
GPX 轨迹匹配 · 分类绑定规则/输出设置 · OIIO 3.2 · exiv2 0.29-dev · AVX-512 专项优化 —— 均**未做**。
（输入预览与轻量分类 = 共识 §4 明文松绑范围，未外扩；全表原文见 `docs/v0.3.0-consensus.md:176`。）

---

## 3. 波次执行记录 W0–W5

| 波次 | 任务 | 落地提交 | 出口证据（当轮记录，摘要） |
|---|---|---|---|
| 预备 | 共识/设计/任务书定稿 + GUI 原型入库 | `f73d860` | `docs/v0.3.0-consensus.md`、`docs/v0.3.0-design.md`、`docs/m4-tasks.md`、`docs/mockups/*.html/png` |
| **W0** | W0-GC 升级前基线归档；T1 解冻裁定表落地；T2 依赖一把全升；T3 AVX2 地基；T4 pr-fast 骨架 + 金样子集 | `1f8c372`、`0b71a6a`、`ef0eb0d`、`c4824d3`、`58a2059` | 裁定表 100% 对应到文件；九库升级落地 + 金样 16 对无回退；AVX2 flags 可查证（依赖侧两次引爆 → 勘误 f）；pr-fast 全绿 |
| **W1** | T5 多格式管线；T6 进度系统；T7 E3 调度；T8 classify + 生效值；**T5-fix 黑图/同路径覆盖** | `d94646e`、`a4dde80`、`6767ca2`、`fb2285b`、`e28142c` | `--dev` 1→N 输出全语料；3 新金样对绿；`test_progress`/`test_scheduler`/`test_classify`/`test_effective` 绿；§5.1 缺陷修复 |
| **W2** | T9 主题与无边框骨架；T10 预览面板；T11 列表勾选/分类；W2-fix 视觉保真与预览收尾 | `2dabde4`、`706c976`、`ebdfc0b`、`dc410b2` | 双主题截图与原型逐区对齐（±4px）；`UI-SMOKE` 扩展段绿；`classes.json` 往返 |
| **W3** | T12 元数据页；T13 输出页（预设 v2 + lossless schema）；T13b D-W3-1 修复；T14 运行页（冻结行升级 shots=12）；T15 设置追加项 | `7f1d554`、`b2ee2a6`、`9adf30a`、`54116da`、`4706d2f` | 生效值显示与写入一致性抽查；`test_presets` v2 迁移绿；`test_fsops` 模板绿；冻结行 `shots=12 pages=3` 逐字；§5.2 缺陷修复 |
| **W4** | T16 CI 工业化四道 + tidy 棘轮 + 预算表；T17 Windows 回归基线口径；T18 打包闭包 + 五门禁 | `fdce63d`、`a5d0467`、`66bf7a6` | 四道 YAML 落地（实跑待首跑）；Windows 基线 diff 流程走通 + 升级归因；winzip 门禁 A–E 全过 + 许可 36 ≥30 + SPDX 129/129 |
| **W5** | T19 5 对 SIMD + `--dev bench`；T20 金样/UI 全量收口 + TODO 清零 + 走查留档；**T21 发行准备（本文）** | `cf72a3a`、`bdece32`、（T21 本轮） | T19 验收实测（§7）；T20：`ctest release 27/27`、`release-dev 28/28`、金样 20/20、回归 zero diff、TODO 标签清零（2 处误报说明 + 1 处历史引用改写）；T21：版本单源 + 文档收口 + 勘误落地（§6/§8） |

**执行顺序偏差（据 `git log`）**：W1 的 T5 修复独立成提交 `e28142c`（`M4-T5-fix`，未 amend）；
W2 的 T9/T10 视觉与预览性能收尾独立成 `dc410b2`（`M4-W2-fix`）。两轮均为"闸门发现 → 同波次修复"，
未 rebase/amend。

---

## 4. 金样 20 对清单（`tests/golden/smoke.py:147-176`）

16 对（M1 冒烟 9 → M2 断言级 16）+ 4 对（M4 新增）。**断言零改动、零放水**（金样只增不减）。

| # | 用例 | 输入 → 产物 | 断言面 |
|---|---|---|---|
| 1 | `jpeg-lossy` | `base/photo.jpg` → `.jpg` | 像素 + 元数据 + warnings |
| 2 | `jxl-lossless` | `base/rgb8.png` → `.jxl`（modular） | 同上 |
| 3 | `png16-lossless` | `base/rgb16.png` → `.png` | 同上 |
| 4 | `tiff16-lzw` | `base/rgb16.tif` → `.tif`（走 `--preset` 分支） | 同上 |
| 5 | `webp-lossless` | `base/rgba8.png` → `.webp` | 同上 |
| 6 | `heif-lossy` | `base/rgb8.png` → `.heic`（x265） | 同上 |
| 7 | `avif-lossy` | `base/rgb8.png` → `.avif`（SVT-AV1） | 同上 |
| 8 | `bmp-exact` | `base/rgb8.png` → `.bmp` | 同上 |
| 9 | `meta-artist` | `base/photo.jpg` → `.jpg` | 同上 |
| 10 | `gray-webp` | `base/gray8.png` → `.webp` | 同上 |
| 11 | `alpha-jpeg` | `base/rgba8.png` → `.jpg` | 同上（含展平警告） |
| 12 | `depth-jpeg` | `base/rgb16.png` → `.jpg` | 同上（含降档警告） |
| 13 | `multipage-png` | `base/multi.tif` → `.png` | 同上（含多页截断警告） |
| 14 | `unicode-png` | `edge/测试📸unicode.png` → `.png` | 同上（中文 + emoji 路径） |
| 15 | `exif-roundtrip` | `meta/exif_full.jpg` → `.jpg` | 同上 |
| 16 | `metaonly-jpeg` | `meta/exif_full.jpg` → `.jpg`（`--metadata-only`） | 同上 |
| 17 | **`multiformat-split`** | `base/rgba8.png` → `jpeg/rgba8.jpg` + `webp/rgba8.webp`（`$format/$dir/$file`） | 双产物像素 + 元数据 + 路径 |
| 18 | **`multiformat-mirror`** | `base/rgb8.png` → `base/jpeg/…` + `base/webp/…`（`$dir/$format/$file`） | 同上 |
| 19 | **`multiformat-conflict`** | `base/rgb8.png;base/rgb8.tif` → 4 产物（同名 → 逐输出 rename） | 逐输出命名 + 像素 + 元数据 |
| 20 | **`progress-trace`** | `base/rgb8.png` → `jxl/rgb8.jxl` + `webp/rgb8.webp` | 产物面 + **进度事件流**（`overall_frac` 单调不倒退、`synthetic` 口径 jxl=false/webp=true、终态 `done` → 1.0） |

**本机实测（T21 轮）**（命令：`python tools/env.py run -- python tests/golden/smoke.py build/release-dev`）：

```
jpeg-lossy       PASS     VERIFY jpeg-lossy OK
...
multiformat-conflict PASS     VERIFY multiformat-conflict OK
progress-trace   PASS     VERIFY progress-trace OK
SMOKE total=20 pass=20 fail=0 (out: C:\Data\Code\PhotoPipeline3\.cache\out-smoke)
```

---

## 5. 两大缺陷修复史

### 5.1 黑图（静默数据损失）——W1-T5 复核项 1；修复提交 `e28142c`（`M4-T5-fix`）

**现象**：非 `keep` 色彩目标（`srgb`/`p3`/`adobergb`）下，3/4 通道源经色彩变换后输出**全黑图**，
且 `ok=true`、**无任何告警** —— 属"静默数据损失"，金样 `exact` 面之外的组合（灰度→彩色目标为
`alpha-jpeg` 之外的 RGB 源）当时未被断言覆盖。

**根因（逐字；引文前半 = `src/core/pipeline.cpp:20-22` 的文件头注，后半 = `:840-845` 的实现段注释，
两段在文件内均逐字存在）**：

> ColorManager 的两条落地路径是
> (i) 灰度源（1/2ch）→ 彩色目标：换**独立新缓冲**（升维）→ 必须回拷；
> (ii) RGB/RGBA（3/4ch）→ **原地**写回我们包装的同一块存储 → **不得回拷**。
> …`cbuf = work.buf` 对 APPBUFFER 是**共享存储**而非深拷…ColorManager 的 3/4 通道分支用
> write_planes **原地**写进这块存储（`src/core/colormanager.cpp:721-733`），此时像素已就位 ——
> 若照旧回拷，`work_from_buf` 的第一步（assign/覆写 n 个样本）与"源"同址 → 把刚变换好的像素
> **清零后再从同一块内存读回** → **全黑图**且 ok=true、无告警（静默数据损失）。

**修复**：判据改为"当前缓冲的本地像素地址是否仍是工作缓冲的存储"——
`src/core/pipeline.cpp:860` `if (cbuf.localpixels() != work_px) { … work_from_buf(work, cbuf, cerr); }`；
即**同址不回拷**，仅灰度升维（换缓冲）路径回拷。

**回归防线（像素级断言，随修复入库）**：`tests/unit/test_pipeline_contract.cpp:522-524`（回归防线注释：
"修复前此处产出**全黑图**而 ok=true 无告警"，阈值 0.02 = 5/255 判负）、`:961`（共享 sRGB 升维后
png 全零判负）、`:975`（webp 行全零判负）、`:1153`（同址回拷机理复述）、`:1191` / `:1229`（非 keep
目标 / RGBA 源的非 keep 目标全零判负）。

**同提交一并修复（同类数据安全面）**：同文件同路径覆盖 —— 一源多输出撞同一 desired 时，
第 2..N 个 target 走 rename 得到独立名字，而不是沿用同一最终路径互相覆盖
（`src/core/pipeline.cpp:396-399`，复核项 2）；以及 orient 路径的内存峰值收敛。

### 5.2 D-W3-1 悬垂指针（UI 偶发访问违例）——W3-T13b；修复提交 `9adf30a`

**现象**：`--ui-smoke` 走查**偶发**访问违例（进程崩溃），一度被归因到参数表单的省略号形态
（`src/ui/paramform.cpp:318` 记录了这次被证伪的归因）。

**根因（ASAN 逐字，`build/asan-smoke-1.log`）**：

```
==17996==ERROR: AddressSanitizer: heap-use-after-free on address 0x12678b5b2c90 …
READ of size 8 at 0x12678b5b2c90 thread T0
    #0 … pp::ui::`anonymous namespace'::Impl::changed_param_count …page_output.cpp:1211
    #1 … Impl::format_tab_label …page_output.cpp:1193
    #2 … Impl::refresh_tab_labels …page_output.cpp:1226
    #3 … Impl::apply_state_to_form …page_output.cpp:1330
…
SUMMARY: AddressSanitizer: heap-use-after-free …page_output.cpp:1211 in …changed_param_count
```

**机理（`src/ui/page_output.cpp:1208-1210` 逐字）**：`tech_by_id()` 返回的是**入参容器内**的指针，
而 `effective_backends(*f)` **按值返回** → 必须先把临时绑定到具名局部再取指针，
否则 `t` 在语句结束即**悬垂**（ASAN: heap-use-after-free @ `page_output.cpp:1211`）。

**修复**：先绑定具名局部再取指针 —— `src/ui/page_output.cpp:1213`（`const std::vector<…> backends = effective_backends(*f);`）；
同类点一并收敛：`page_output.cpp:1348`（同一"按值返回 → 指针必须落在具名局部"纪律）；
`page_output.cpp:211-213` 在 `tech_by_id()` 声明处补**生存期契约**注释（防回归）。

**证据留档**：ASAN 报告（上引）+ T13b 轮 ASAN 复跑 5 次（`build/w3t13b-asan-1..5.log`）
与最终闸门轮（`build/w3t13b-final-*`）。

---

## 6. 设计勘误清单落地对照表（a–q 17 条 + T16 三条裁定 r 组）

口径：**裁定人** = 谁定的；**落地点** = 代码/产物里的真身（文件:行）；**文档修正位置** = 哪份文档
被改（`（T21）` = 本轮改的）。凡设计文档未列出、由代码落地并经追认的，一律登记在本文并注明
"设计无需改文"。

| 条 | 内容 | 裁定人 | 落地点 | 文档修正位置 | 状态 |
|---|---|---|---|---|---|
| **a** | `ColorTarget::Keep` **即** `ColorTarget::KeepOriginal`（**命名映射**，非行为差异） | W0 主对话（解冻表落地） | `src/core/pipeline.h:82`（`ColorTarget color = ColorTarget::KeepOriginal;`） | **设计 §3.2（T21 改文）**：代码块改为 `KeepOriginal` + 命名映射注 | ✅ |
| **b** | `FileState` 已含 `Progress`（末尾追加值） | W1-T6 落地，主对话授权（W0 口径 b） | `src/core/pipeline.h:140-144`（追加在**末尾** ⇒ 既有值数值/语义零变化） | **设计 §3.3（T21 补注）**：新增 `FileState` 追加值块（含 `Stage::Queued`/阶段名缺口的同源说明） | ✅ |
| **c** | `flatten_gray` = **`double 1.0`**（§3.2 的 `bool false` 是**勘误**） | W0 主对话（解冻表口径 c） | `src/core/pipeline.h:85`；语义源 = v0.2 白底（`src/core/settings.h:65` 同值） | **设计 §3.2（T21 改文）**：`bool flatten_gray = false` → `double flatten_gray = 1.0` + 勘误标注 | ✅ |
| **d** | **lossless 显式 schema 已落地**：`__lossless` 为**内部管道键**（保留、不落盘、不显示） | W1-T13 落地；W4 追认（口径 d） | `src/core/format_tables.cpp:47-65`（`lossless_param()`）、`src/core/params.h:73-74`（`kLosslessParamKey`/`kLosslessKey`）；同步点 `apply_locks()` | `docs/param-catalog.md` §2.2/§2.3/§5.1/§5.2（T13 期）；**设计 §3.6（T21）**：行内明写「勘误 d/m」两字（按字母 grep `勘误` 可命中 d 与 m） | ✅ |
| **e** | §4.4 inflight 键 = **冲突解析前的 desired 路径** | W1-T5 第三次裁定（复核项 6 追认） | `src/core/scheduler.cpp:99-107`（`desired_keys()`）、`:335`（登记点）；角色区分：`target.out_path` = 解析后最终写路径 | **设计 §4.4（T21 改文）**：原文「按 `(target.out_path)` 键」改为「按每输出的 desired 路径键」+ 口径澄清块 | ✅ |
| **f** | **triplet 不注入 ISA/LTO flags**（依赖侧零注入） | W0-T3 两次实测引爆后主对话裁定 | `triplets/x64-windows-avx2.cmake:5-41`、`triplets/x64-linux-avx2.cmake:5-28`（首注即裁定原文）；自研侧 = `cmake/avx2.cmake`；**该文件头注的过时措辞已随本轮修文**（原写 vcpkg 依赖侧同一组 flags 由 overlay triplet 注入、两处口径必须一致 → `cmake/avx2.cmake:6-13` 现为「依赖侧不注入」；见 §9.1 第 6b 行） | **设计 §11.1（T21 改文）**：依赖列改为"不注入"+ 勘误 f 块（libwebp `0xC0000005` 最小复现 / aom IL 对象与 `__create_def`） | ✅ |
| **g** | `FileResult.outputs[]` / `EventFn` / `RunScope` / `FileOutcome` / `OutputResult` 已追认 | W1 落地；W4 口径 g 追认 | `src/core/pipeline.h:102`（`OutputResult`）、`:115-124`（`FileResult.outputs`）、`:179`（`RunScope`）、`:203`（`FileOutcome`） | 设计无需改文（§3.2 代码块已含 `outputs[]` 语义） | ✅ |
| **h** | caption 命中 **46×32** + hover 满高 **52** | W2-T9 探针取证 + 主对话追认（口径 h） | `src/platform/frameless.h:43-48`（命中几何唯一真源）、`src/ui/mainwindow.cpp:424-454`；`--ui-smoke` 打印 `caption-geometry`（`mainwindow.cpp:2748`） | 设计 §9.1 明文「46×32」**无需改文**（本文登记双轨口径） | ✅ |
| **i** | 中/右栏比例 = **mockup 1.119**（设计文本 「1.15」以原型为准） | W2-T9 原型实测 + 追认（口径 i） | `src/ui/theme.h:82-83`（537:601≈1:1.119 探针取证）、`src/ui/mainwindow.cpp:1181` | 设计 §9.1 文本**保留**「1.15」（§9.4 已定"以 mockup 为准"），本文登记 | ✅ |
| **j** | `pp-start`（开始运行）文字色 **#0c1b24**（双主题同值） | W2-fix 落地（口径 j） | `src/ui/theme.h:187`、`:581` | 设计无需改文（§9.2 tokens 表未含 `.go` 文本色，属 mockup 细目） | ✅ |
| **k** | `set_mod` / `.pill` 组件**已存在** | W2 落地（口径 k） | `src/ui/theme.h:6-7`（`set_mod`/repolish 职责）、`:54`/`:124-129`（`.pill` 几何） | 设计无需改文 | ✅ |
| **l** | 运行期锁定视觉 / backdrop 重放已落地 | W3-T14 落地（口径 l） | `src/ui/page_run.cpp:40-42`（锁定 = 唯一取消入口可交互）、`src/ui/mainwindow.cpp:1999`（`apply_window_backdrop` 与 GUI 同源模式）；T20 走查留档 `03c/03d/03e` 探针页 | 设计无需改文 | ✅ |
| **m** | `outputs[i].lossless` JSON 键 = **R31 必要手段**（T21 登记设计） | W1-T13 定稿；T21 登记 | `src/ui/preset_io.cpp:488-500`（写：仅真值落盘 + 载体二选一）、`:501-533`（读：落回内部管道键/不发明键，落键点 `:527`） | **设计 §3.6（T21 补登记）**：`core/presets.h` 行追加 schema v2 的 `outputs[i].lossless` 键说明；**`docs/m1-tasks.md` §3.4（T21）** 注入点表补「预设 JSON 载入」行 | ✅ |
| **n** | `format_tables.cpp` 冻结标记「只允许替换占位符」**措辞过时** | W4 口径 n 认定（T21 修文） | **`src/core/format_tables.cpp:1-6`（T21 修文）**：首注补"0.3.0/M4-T13 追认的显式无损参数行"例外说明（结构其余逐字不变，78 条既有 `ParamDef` 的 key/type/def/lo/hi/choices 未动） | 本文登记（文件面外的注释修文，见 §9【机械性】） | ✅ |
| **o** | `enc_webp` 取值链**例外口径**已文档化（tech_id → 显式 schema 参数 → 内部键） | W1-T13 复核项 6（口径 o） | `src/codecs/enc_webp.cpp:74-76`、`:202-209`；`docs/param-catalog.md:20`（例外口径原文） | 设计无需改文（已在参数目录登记） | ✅ |
| **p** | T12 三条 mockup 偏离已追认（时间卡无启用复选框 / **GPS 批量规则折叠行** / 地图选点 = 视口中心） | W3-T12 回报 + 主对话追认（口径 p） | `src/ui/page_meta.cpp:33-45`（三条逐条记账与理由）、`:1591-1608`（折叠行控件面） | **mockup 原型（T21 改稿）**：`docs/mockups/meta-dark.html` / `meta-light.html` 的 GPS 卡补「批量 GPS 规则 ▾」折叠行（对齐实现控件面；**PNG 未重渲**，见 §9【机械性】） | ✅ |
| **q** | `PP_ASAN` 默认 **OFF**（release 语义不变） | W3-T13b 落地（口径 q） | `CMakeLists.txt:10-12`（option 定义 + 说明）、`CMakePresets.json:55-61`（仅 `asan` 预设置 ON） | **`README.md`（T21 增补）**：开关表登记 `PP_ASAN`（含默认值与"release/release-dev 语义不变"） | ✅ |
| **r1** | T16 裁定①：**DAG 扁平化**（打包 job 与平台 job 并行），授权 = 设计 §12.3 末行「整轮超 20min → 重排 DAG 关键路径」 | T16 升报 → 主对话追认（r 组） | `.github/workflows/main-full.yml:16-28`（「唯一口径说明（重要）」标签在 `:16`、25/31 min 数字在 `:18`：照字面串行 = 25/31 min，结构性越 20min 硬顶；改由末端 `gate` 承担"平台绿才允许打包"语义） | 设计 §12.1 图示**保留**（§12.3 已授权重排），本文登记 | ✅ |
| **r2** | T16 裁定②：**tidy 棘轮落点 = `windows-2025`**（首跑红则 `tools/ci-tidy.py --update` 重播种） | T16 升报 → 主对话追认 | `.github/workflows/main-full.yml:156-159`（`tidy` job：`runs-on: windows-2025`，`needs: lint`，预算 12 min）；`tools/ci-tidy.py` + `tools/versions.env` 的 `CLANG_TIDY_VERSION=21.1.0`（断言实测版本一致） | 本文登记；**首跑判定待 CI**（§10） | ✅ 待首跑 |
| **r3** | T16 裁定③：**release 双探测 job**（`probe-appimage` 2 + `probe-winzip` 3 + `publish` 2，winzip 15 ⇒ 恰 20 min 硬顶内） | T16 升报 → 主对话追认 | `.github/workflows/release.yml:458-530`（`probe-appimage` / `probe-winzip` / `publish`）；探测内容 = `gh release view` 查重 + 产物 `--version` 断言（设计 §12.7 三件套） | 设计 §12.1 发布道一行原文（"publish（前置探测…）"）**保留**，本文登记 job 拆分 | ✅ 待首跑 |

**勘误小结**：17 条中 **7 条**（a/b/c/d/e/f/m）本轮改到设计文档（其中 d 与 m 合并登记在 §3.6 的
`core/presets.h` 行）；**7 条**（g/h/i/j/k/l/o）设计无需改文，由本文与代码注释承担登记；
**3 条**在别处落地 —— n（源码冻结标记修文）、p（mockup 原型补稿）、q（README 开关表增补）。
T16 三条裁定 r1–r3 均已落地，其中 r2/r3 的首跑判定待 CI。

---

## 7. 性能与 SIMD（需求 9，T19 实测）

**ISA 基线**（`cmake/avx2.cmake`）：MSVC `/arch:AVX2 /GL` + 链接 `/LTCG`；GCC/Clang
`-march=x86-64-v3 -mtune=raptorlake`（不识别则回退 `-mtune=haswell`，ISA 基线不变）+ LTO。
**依赖侧零 ISA/LTO 注入**（勘误 f）。

**5 对热路径一致性单测**（`tests/unit/test_simd.cpp`，ctest #19 Passed）：`flatten` / `quantize8·16` /
`transpose8` / `downscale` / `interleave` —— 同签名 `*_avx2` / `*_ref`，逐位/舍入/容差断言；
另含 OIIO 交叉断言（自研 `downscale` vs `OIIO resize(lanczos3)` `max|Δ| = 1.073e-06`）。

**基准实测**（`photopipeline --dev bench`，scale=1；机器 = 本机 Windows 11 / i7-14700K；
留证 `.cache/bench/bench-report.json`（未入库）+ T19 提交 `cf72a3a` 正文）：

| 场景 | 实测 | 判据 |
|---|---|---|
| 48MP TIFF→JXL | 7009.8 ms（1.48 MB/s） | 记录项（无阈值） |
| 24MP JPEG→WebP×16 批 | 16414.5 ms（11.38 MB/s，0.97 files/s） | 记录项 |
| 单张 HEIF | 5655.5 ms（1.29 MB/s） | 记录项 |
| **多格式 jpeg+webp（解码共享）** | multi **6377.2 ms** vs jpeg 1221.6 + webp 5620.1 = 6841.7 ms ⇒ **ratio 0.932** | **≤1.35 ✅ PASS** |
| `flatten` 微基准（avx2 vs ref，逐位相等） | 256² **4.08×** / 1024² **2.03×** / 2048² **1.69×** / 4096² **1.54×** | **≥2×：仅在 ≤1024² 成立**；≥2048² 双方触顶单线程内存带宽（如实记录，见下） |
| `quantize8` / `quantize16` | 2.65× / 2.10× | 记录项 |
| `transpose`（2048²） | 4.21× | 记录项 |
| `interleave`（4M，4→3） | 1.06× | 记录项 |
| `downscale`（2048²→512²） | 3.26×（`max|Δ|=0`） | 记录项 |
| `preview48`（48MP 无内嵌档预览） | **535.1 ms**（2048×1365，含全解码） | 记录项（**250ms 判据未达**，见 §2 需求 1） |

**禁回退口径**：批吞吐与 v0.2 的对照**未在本机执行**（v0.2 产物已不在本机构建树；T19 记
【机械性】待发行侧/CI 验证）—— 如实登记，不写推测对照值。

**"flatten ≥2× 标量"判据的诚实结论**：微基准在 256²/1024² 达 2.03–4.08×，在 2048²/4096² 回落到
1.69×/1.54×。原因（T19 记录）：两实现均触顶单线程内存带宽，AVX2 的算术优势在图像级尺寸被
内存墙吃掉。**未改判据、未改口径**；设计 §11.3 的"≥2×"按微基准口径（小尺寸）成立，
图像级尺寸的真实收益以端到端 bench（多格式 ratio 0.932）为准。

---

## 8. 验证证据（本机，0.3.0；T21 轮实测）

> 全部命令经 `python tools/env.py run -- <命令>`（vcvars / Qt / 仓库内工具链注入）。
> 全量日志：`build/w5-logs/t21-*.log`。
>
> **日志复写提示（本轮实测）**：外层波次闸门以**同名文件**复写 `build/w5-logs/<task>-<step>.log`
> （T21 提交后闸门复跑 ⇒ `t21-configure/build/ctest/build-dev/uismoke/golden.log` 已被覆盖为闸门轮的
> 增量结果）。故本节**权威数值取自 `t21-post-*.log`（提交后我自跑的复验轮，此后未再被复写）**；
> 凡与现状不一致的首轮读数一律不再引用。
> **最终复验轮**（勘误回炉修文后，含 `cmake/avx2.cmake` 注释改动）另行留证 `t21-final-*.log`：
> `build` 绿、`ctest --preset release` **27/27**（13.17 s）、`ui_smoke` **1/1 Passed 16.83 s**、
> 金样 `SMOKE total=20 pass=20 fail=0`。

### 8.1 构建与测试（0.3.0 树）

| 项 | 结果（逐字摘录） |
|---|---|
| `cmake --preset release` | exit 0（re-configure 后版本切换生效） |
| `cmake --build --preset release` | exit **0**（post 轮为增量：`[28/31] Generating CXX dyndep file …pp_verify.dir/CXX.dd`，`t21-post-build.log` 末行） |
| `ctest --preset release` | **27/27**，`100% tests passed out of 27`（**10.89 s**，`t21-post-ctest.log:57-59`） |
| `cmake --build --preset release-dev` | exit 0 |
| `ctest --preset release-dev -R ui_smoke --output-on-failure` | **1/1 Passed 16.38 s**（`t21-post-uismoke.log`） |
| `python tests/ui_smoke.py build/release-dev`（直接跑，取冻结行逐字） | exit **0**，末两行 `UI-SMOKE OK shots=12 pages=3` / `UI-SMOKE pass`（§8.2） |
| `python tests/golden/smoke.py build/release-dev` | `SMOKE total=20 pass=20 fail=0` |
| `build/release/photopipeline.exe --version` | `PhotoPipeline 0.3.0`（rc 0） |
| `build/release/pp_linkprobe.exe` | **rc 0**，11 条 `PROBE … OK`（见 §8.6） |

ctest 27 条 = `test_classify` / `test_color` / `test_decode` / `test_effective` / `test_enc_oiio` /
`test_enc_smoke` / `test_fsops` / `test_gcj02` / `test_logger` / `test_metadata` / `test_params` /
`test_paths` / `test_pipeline_contract` / `test_pixelbudget` / `test_presets` / `test_progress` /
`test_scheduler_contract` / `test_settings` / `test_simd` / `test_smoke` / `test_thumbs` /
`test_timeshift` + `linkprobe` / `fixtures` / `spike_e` / `spike_f` / `verify_selftest`。

### 8.2 UI 冒烟冻结行（逐字）

冻结行由 `tests/ui_smoke.py:48` 断言（`FROZEN_LINE = 'UI-SMOKE OK shots=12 pages=3'`）。
本机直接运行并捕获（`python tools/env.py run -- python tests/ui_smoke.py build/release-dev`，
exit 0，日志 `build/w5-logs/t21-uismoke-direct.log`）——stdout 尾部逐字：

```
UI-SMOKE shot 04-settings.png 5835 bytes
UI-SMOKE shot 05-exif-editor.png 7368 bytes
UI-SMOKE shot 06-presets.png 2496 bytes
UI-SMOKE shot 07-run-light.png 13094 bytes
UI-SMOKE OK shots=12 pages=3
UI-SMOKE pass
```

12 张截图（`src/ui/mainwindow.cpp:130-136` 的 `kSmokeShots`，`kSmokeShotCount = 12`）：`01-meta` / `02-output` /
`02b-output-avif` / `03-run` / `03b-run-done` / **`03c-run-idle`** / **`03d-run-rows`** /
**`03e-run-cancel`** / `04-settings` / `05-exif-editor` / `06-presets` / **`07-run-light`**
（0.2 的 `shots=8` 已按 W3-T14 冻结升级）。

### 8.3 金样 20 对

见 §4（`SMOKE total=20 pass=20 fail=0`）。**16 对旧用例的断言与命令零改动**（`smoke.py:30` 明文：
"M4-T5 单源单产物的 16 对行为逐字不变"；`:36` "对其它 19 对的断言与命令**零改动**"）。

### 8.4 依赖版本（本机实测 = `vcpkg_installed/vcpkg/status` + linkprobe 运行时读数）

| 库 | 设计 §10 目标 | 本机实测 | 一致 |
|---|---|---|---|
| Qt | 6.11.2 | **6.11.2**（`tools/versions.env`；回归基线 `version {lib=qt version=6.11.2}`） | ✅ |
| OpenImageIO | 3.1.17.0 | **3.1.17.0** | ✅ |
| libjxl | 0.12.0 | **0.12.0**（运行时 `JxlEncoderVersion=0x00002ee0`） | ✅ |
| libheif | 1.23.5 | **1.23.5** | ✅ |
| exiv2 | 0.28.9 | **0.28.9**（linkprobe 运行时确认；`enableBMFF()` 调用已删） | ✅ |
| x265 | 4.3 | **4.3**（`x265 HEVC encoder (4.3-vcpkg)`） | ✅ |
| SVT-AV1 | 4.2.0 | **4.2.0** | ✅ |
| aom | 3.15.1 | **3.15.1**（`AOMedia Project AV1 Encoder v3.15.1`） | ✅ |
| libde265 | 1.1.3 | **1.1.3** | ✅ |
| 滚动对齐项 | ≤ vcpkg master 可得最新 | libwebp **1.6.0** · lcms **2.19.1** · spdlog **1.17.0** · zstd **1.5.7** · libpng **1.6.58** · giflib **6.1.3** · openexr **3.4.13** · highway **1.4.0** · brotli **1.2.0** · zlib **1.3.2** · libjpeg-turbo **3.2.0**（jpegli 钉 `031a0077`） | ✅ |
| CMake 兼容线 | 3.28...4.4 | `CMakeLists.txt:2` `cmake_minimum_required(VERSION 3.28...4.4)` | ✅ |
| 停留项 | OIIO 3.2 未升 / exiv2 0.29-dev 未跟 / jpegli 钉 commit / SVT 不跨 4.x / Qt 不回 6.8 | 均**零漂移**（上表可证） | ✅ |

### 8.5 回归基线（Windows）与版本号漂移的处置

- **升级归因（W4-T17 已做）**：Windows 基线 `tools/baseline/golden.windows.log` 由升级后的树重生成，
  逐行归因表见 T17 自检记录。
- **本轮（T21）新发现的唯一差异**：版本单源提升后，全语料回归的唯一 diff = **`version {lib=app}` 行**
  —— 本机实测（`build/w5-logs/t21-regression.log`）：

  ```
  regression: normalized 2406 lines vs baseline 2406 lines
  regression: DIFF FOUND (110 lines of unified diff, first 40 shown)
  -[info] [tid] [run] [main.cpp] version {lib=app version=0.2.0}
  +[info] [tid] [run] [main.cpp] version {lib=app version=0.3.0}
  regression: attrib: sections: old=12 new=12 paired=12 only-baseline=0 only-current=0
  regression: attrib: removed by kind:      （空）
  regression: attrib: added by kind:        （空）
  regression: attrib: transitions:
  regression: attrib:   version-bump lib=app                        12
  ```

  即：**12 个 run 段各 1 行版本串**，无任何其它行型增删（`removed/added by kind` 为空）。
- **处置（【机械性】，见 §9）**：`python tools/regression.py build/release-dev --update` 重生成
  Windows 基线（git 追踪文件 `tools/baseline/golden.windows.log`，**在 T21 声明文件面之外**，
  属"版本单源提升"的机械蕴含）。实测（`build/w5-logs/t21-regression-update.log`）：

  ```
  regression: --update: replacing baseline (2406 lines -> 2406 lines, 24 changed lines)
  regression: baseline written: tools/baseline/golden.windows.log (2406 lines)
  ```
  重生成后 git diff 该文件 = **12 删 / 12 增，且全部为 `version {lib=app}` 行**（无其它行改动，
  `git diff -U0 | grep '^[+-][^+-]' | sort | uniq -c` 逐字核过）；随后再跑对比复验
  **`regression: zero diff (baseline reproduced)`，exit 0**。
  另：脚本以 LF 写出，已就地归一为 **CRLF**（与同目录 `golden.log` 的工作树形态一致，
  `git ls-files --eol` 现为 `i/lf w/crlf`）—— 纯行尾归一，`git diff` 计数不变（仍 12/12）。
- **Linux 基线**：`tools/baseline/golden.log`（含 `version=0.1.0`）**未动** —— 按 M3 §8-9 的既定处置，
  它必须在 Linux 机上 `--update` 重生成（本机为 Windows，无法产出），仍属**未决项**（§10）。

### 8.6 依赖/内省面（`pp_linkprobe`，本机实测 rc=0）

```
PROBE lcms2 OK identity_err=0 closed=1
PROBE exiv2 OK version=0.28.9
PROBE exiv2-bmff OK ftyp(heic) -> ImageType=4 (bmff=4)
PROBE oiio-plugins OK required=9 all present (jxl=plugin "jpegxl"; heif covers avif)
PROBE jpegli OK create/set_distance/destroy ok via jpegli/encode.h
PROBE libjxl OK JxlEncoderVersion=0x00002ee0
PROBE libheif-hevc OK x265 HEVC encoder (4.3-vcpkg)
PROBE libheif-av1 OK AOMedia Project AV1 Encoder v3.15.1,SVT-AV1 encoder v4.2.0
PROBE libheif-params-hevc OK encoder=x265 HEVC encoder (4.3-vcpkg) params=7 types_valid=1 names_ok=1 required=all(quality|lossless|preset) list=quality|lossless|preset|tune|tu-intra-depth|complexity|chroma
PROBE libheif-params-av1 OK encoder=SVT-AV1 encoder v4.2.0 params=10 types_valid=1 names_ok=1 required=all(threads|quality) list=speed|threads|tile-rows|tile-cols|quality|lossless|qp|min-q|max-q|tune
PROBE libwebp OK WebPGetEncoderVersion=67072
PLUGINS: openexr,tiff,jpeg,bmp,cineon,dds,dpx,fits,gif,heif,hdr,ico,iff,jpegxl,null,png,pnm,psd,rla,sgi,softimage,targa,term,webp,zfile
HEIF_ENCODERS: x265 HEVC encoder (4.3-vcpkg),AOMedia Project AV1 Encoder v3.15.1,SVT-AV1 encoder v4.2.0
```

（风险登记册对应关系：`libheif-params-*` = **R26**（编码器参数面漂移 → linkprobe 扩展，设计 §14 R26）；
`oiio-plugins` = **R27**（OIIO 3.1.17 的 patch/插件面）。R27 的『8 个 patch 逐条重验』在 W0-T2 完成，
linkprobe 只覆盖其运行时面。）

### 8.7 打包与五门禁（**说明：本轮未重跑**）

Windows 侧五项门禁（闭包 A / 来源 B / 结构 C / 许可 D / 启动 E）已在 **W4-T18** 于本机全过
（当时指纹：34,484,306 B / 104 条目；`collect_licenses.py --check` = `total=36 min=30 spdx=129/129 ok=1`）。
**0.3.0 版本的打包轮未在 T21 重跑**（属发布轮动作；打包器读产物 `--version` 命名，与版本号解耦）。
⇒ 记为未跑（§10），不以 0.2.0 的指纹替代 0.3.0 的结论。

---

## 9. 偏差与例外（全波次归并）

### 9.1 规格与执行的偏差

| # | 偏差 | 依据 / 证据 | 类别与处置 |
|---|---|---|---|
| 1 | **依赖侧 ISA/LTO flags 撤除**（§11.1 原设计为 triplet 注入） | libwebp 1.6.0 无损解码 `0xC0000005`（最小复现：仅链 libwebp 的探针 `WebPGetInfo` 通过 → `WebPDecodeRGBA` 段错误；无全局 flag → `checksum=2192` OK；40 个依赖 DLL 回填扫描唯一触发者 `libwebp.dll`）；aom 端口 `/GL` IL 对象 vs `WINDOWS_EXPORT_ALL_SYMBOLS` 的 `__create_def` | **【语义性】**（设计口径修订）→ 主对话裁定，设计 §11.1 已改文（勘误 f），AVX2 等价物零丢失 |
| 2 | **`ColorTarget::Keep` 命名映射**；**`flatten_gray` bool→double**；**`FileState::Progress` 末尾追加** | `src/core/pipeline.h:10-21`、`:82`、`:85`、`:140-144` | **【语义性】**（§3 表缺口/勘误）→ 主对话追认（口径 a/b/c），设计已改文（勘误 a/b/c） |
| 3 | **§4.4 inflight 键口径**（`target.out_path` → desired 路径） | `src/core/scheduler.cpp:99-107` | **【语义性】**（用词混淆）→ T5 第三次裁定 + 复核项 6 追认，设计已改文（勘误 e） |
| 4 | **GUI 原型与实现的控件面差异**：GPS 卡「批量 GPS 规则」折叠行（实现 ⊃ mockup，唯一一处）；时间卡无「启用」复选框；「地图选点」= 视口中心 | `src/ui/page_meta.cpp:33-45` | **【语义性】**→ T12 回报 + 主对话追认（口径 p）；原型本轮补稿（HTML），**PNG 未重渲**（见第 8 行） |
| 5 | **§13 测试矩阵行归错**：「预设迁移往返」原写 `test_params` | 实际断言在 `tests/unit/test_presets.cpp:404-442` | **【机械性】**→ 设计 §13 已改文（勘误登记） |
| 6 | **`format_tables.cpp` 冻结标记措辞过时**（"只允许替换占位符"，而 T13 追认的 lossless ParamDef 行已插入） | `src/core/format_tables.cpp:1-6`（T21 修文）；`docs/m1-tasks.md:318` 同源约束 | **【机械性】**（注释修文，零语义）—— 文件面外改动，本报告记账 |
| 7 | **Windows 回归基线随版本号漂移 → 本轮 `--update` 重生成** | §8.5（attribution = 12 × `version-bump lib=app`，无其它行型；更新后 zero diff） | **【机械性】**（`tools/baseline/golden.windows.log` 在 T21 文件面外）—— 属"版本单源提升"的机械蕴含；落地 = `--update` 重生成（12 删/12 增全为版本行）+ 行尾归一 CRLF + 复跑 `zero diff` |
| 8 | **mockup PNG 未重渲**：HTML 源已补 GPS 折叠行，四张 PNG 仍是 0.3.0 开发期渲染 | `docs/mockups/meta-dark.html` / `meta-light.html`（T21 改） vs `*.png`（未动） | **【机械性】**（PNG 为渲染产物；重渲需浏览器/无头渲染器，本机未执行）→ 如实登记，如需对齐可重渲 |
| 9 | **AVX2 依赖侧验收口径放宽为"各库自身机制可查证"** | 同上第 1 行；`triplets/*.cmake` 首注"验收口径（修订后）" | **【语义性】**→ 已裁定；`webp-lossless` 金样为哨兵（本机 PASS） |
| 10 | **批吞吐"不低于 v0.2"未做对照** | 本机无 v0.2 构建树（M3 产物已清理） | **【机械性】**→ 如实登记为未跑（§7 / §10），不写推测对照值 |
| 11 | **README 陈旧事实订正**（16→20 对金样、8→12 张截图、ctest 23/24→27/28、Qt 6.8.3→6.11.2、`--version` 0.2.0→0.3.0） | T21 逐条以本机实测为准订正 | **【机械性】**（文档与实测对齐；改动均在 T21 文件面内的 `README.md`） |
| 6b | **`cmake/avx2.cmake` 头注过时措辞**（写 vcpkg 依赖侧同一组 flags 由 overlay triplet 注入、两处口径必须一致 —— 正是勘误 f 撤除的口径） | `cmake/avx2.cmake:6-13`（T21 修文） vs `triplets/x64-windows-avx2.cmake:5-7`（不注入任何 ISA/LTO flags） | **【机械性】**（注释修文，零语义）—— 文件面外改动，本报告记账；与第 6 行（format_tables 冻结标记）同类处置 |
| 12 | **CI 实跑类项目本地不可验证** | 设计 §12.6 双 ≤20min、runner 行为、tidy 首跑基线、appimage 烟测 | **【机械性】**→ 全部如实标"待 CI 首跑"（§10），不假装通过 |
| 13 | **工作树遗留两枚未跟踪文件**：`docs/mockups$n.png`（227,040 B，2026-09-23）与 `nul`（0 B） | `git status --short` → `?? docs/mockups$n.png` / `?? nul`；两者自会话开始即存在（**非本任务产出**） | **【机械性】**→ 清点登记；**未删除**（在 T21 文件面之外，且非本任务产物）——如需清理请主对话裁定 |

### 9.2 无法在库内证实的项（**不圆场**）

- **`T16 未见的 3 项 escalations`**：指的是 T16 升报、主对话在本轮任务书 r) 组**追认**的三条
  （DAG 扁平化 / tidy 棘轮落点 windows-2025 / release 双探测 job）。其原始出处 = **W4 波次执行报告**
  的「升级事项」栏（workflow 产物）。**本机无该报告的落盘副本**：本会话的 workflow 运行记录
  不可读（工具返回 `workflow_introspection_unavailable`）⇒ 本报告按任务书 r) 组的追认原文登记
  （§6 r1–r3），**未逐字引用 W4 报告原文**（不编造引用）。
- **v0.2 与 0.3.0 的批吞吐对照**：未跑（§9.1 第 10 行）。
- **0.3.0 打包指纹**：未跑（§8.7）。
- **Linux 侧 0.3.0 全链**（ctest / 金样 / ui_smoke / AppImage 打包与烟测）：本机为 Windows，未跑；
  以 Linux 机或 CI 首跑为准。
- **真实 48MP 点选 250ms 主观手感、1:1 拖拽平移、快速翻图不堆积**：属手测项
  （`src/ui/mainwindow.cpp:3213` 明文列为 W5 走查清单），未自动化。

---

## 10. 未决项与发布前待办

| # | 事项 | 现状 | 处置建议 |
|---|---|---|---|
| 1 | **CI 四道首跑实测** | 四道 YAML 已落地、本地可验证项全绿；`main-full` 预算表关键路径预算 = lint 1 + max(linux 12, windows 15, appimage 12, winzip 15, tidy 12, audit 3) + gate 1 = **17 min ≤ 20min**（文件头注释 `main-full.yml:37-38`）——但**未实测** | 首推 `main` 前先 `workflow_dispatch` 观察；实测后按 §12.3 处置条款调 job（禁放宽时限）；tidy 若首跑红 → `python tools/ci-tidy.py --update` 重播种（裁定 r2） |
| 2 | **Linux 全链**（ctest / 金样 20 / ui_smoke / AppImage 打包与四项烟测） | 本机 Windows 不可跑 | CI `linux` / `appimage` job 首跑；`tools/baseline/golden.log`（Linux 基线，含 `version=0.1.0`）需在 Linux 机 `regression.py --update` 重生成，并同步 `regression.py` 的 `HEADER` 两行（M3 §8-9 的既定处置，仍未做） |
| 3 | **预览 250ms（无内嵌档）** | 实测 **535.1 ms**（48MP，含全解码） | 如实保留；如需达标需另行裁定（例如更大内嵌档依赖 / 预览期专用降采样解码），本轮**未改判据** |
| 4 | **用户走查清单 7 项**（`docs/m4-tasks.md:116-122`） | 截图与探针证据齐（`--ui-smoke` 12 张 + `.cache/walkthrough/{dark,light}` 各 15 张），**人工判定未做** | 用户按清单逐项走查：① 分类热键/改名/删除/整类圈选 + 重启保留；② 预览翻图/缩放/徽标 + 运行期只读；③ 时间/GPS/地图生效值（改前同值、改后高亮、多选小表、隐私提示）；④ 输出页多选→分文件夹默认开→取消→单选恢复、模板编辑/校验/示例、仅元数据互斥置灰；⑤ 运行页行级/斜纹/失败原因/交错与线程预算读数/取消；⑥ 明暗双主题 + Mica + 深色标题栏；⑦ 预设 v1 迁移加载 + v2 保存再加载 |
| 5 | **T16 三条裁定的首跑判定**（r2 tidy 棘轮基线、r3 release 探测 job；r1 DAG 关键路径） | 同第 1 行 | 首跑后用 `tools/ci-budget.py` 的预算校准表核对 |
| 6 | **`warm-cache.yml` 播种**（含 aqt 3.3.0 在 Windows 无法定位 Qt 6.11.2 的兜底上传档） | T16 备注（`main-full.yml:50-52`） | 首次运行前先手动 `warm-cache`，否则四道会因缓存 miss 早失败（设计 §12.4 纪律） |
| 7 | **tag `v0.3.0` → GitHub Release** | **硬边界：本任务不做** | 待用户验收授权后由主对话执行：`git tag v0.3.0 && git push origin v0.3.0`（tag 未发布资产前可移动；发布后不可移动——M3 §7.1-11 纪律延续） |
| 8 | **0.3.0 打包轮**（win64.zip / AppImage 指纹 + 五门禁 + 许可清点） | 未跑（§8.7） | 发布轮执行（`make_winzip.py` / CI `winzip`·`appimage` job） |
| 9 | **批吞吐『不低于 v0.2』未实测**（总出口 5 / 设计 §11.3 的验收项之一） | 本机无 v0.2 构建树，未跑对照（§7 末段、§9.1 第 10 行） | 如需闭环：取 v0.2.0 产物（Release 附件）在本机跑同一批语料对照；或裁定以 0.3.0 绝对值 + 多格式 ratio 为准 |
| 10 | **mockup PNG 重渲** | HTML 已补 GPS 折叠行，PNG 未重渲（§9.1 第 8 行） | 需要时重渲四张 PNG |

---

## 附：复现命令与证据路径

```powershell
# 版本单源（唯一功能定义点）
#   CMakeLists.txt:3  project(PhotoPipeline VERSION 0.3.0 …)   /  vcpkg.json:3

# 构建与测试（本报告 §8.1）
python tools\env.py run -- cmake --preset release
python tools\env.py run -- cmake --build --preset release
python tools\env.py run -- ctest --preset release                       # 27/27
python tools\env.py run -- cmake --build --preset release-dev
python tools\env.py run -- ctest --preset release-dev -R ui_smoke --output-on-failure   # 1/1（shots=12）
python tools\env.py run -- python tests\golden\smoke.py build\release-dev               # 20/20
python tools\env.py run -- python -c "import subprocess;print(subprocess.run(['build/release/photopipeline.exe','--version'],capture_output=True,text=True).stdout)"
python tools\env.py run -- build\release\pp_linkprobe.exe               # 11/11 OK

# 回归基线与归因（§8.5）
python tools\env.py run -- python tools\regression.py build\release-dev            # 归因表（transitions）
python tools\env.py run -- python tools\regression.py build\release-dev --update   # 重生成 Windows 基线
```

证据留档（本机 `build/w5-logs/`，未入库）：
* **提交后复验轮（权威数值来源，未被复写）**：`t21-post-configure.log` / `t21-post-build.log` /
  `t21-post-ctest.log` / `t21-post-build-dev.log` / `t21-post-uismoke.log` / `t21-post-golden.log`；
* **ui_smoke 冻结行直取**：`t21-uismoke-direct.log`（§8.2 引文来源）；
* **最终复验轮（勘误回炉后，含 cmake/avx2.cmake 注释改动）**：`t21-final-configure.log` /
  `t21-final-build.log` / `t21-final-ctest.log` / `t21-final-build-dev.log` /
  `t21-final-uismoke.log` / `t21-final-golden.log`（§8 头注的数值来源）；
* **回归/基线**：`t21-regression.log`（归因）· `t21-regression-update.log` · `t21-regression-after.log`（zero diff）；
* **首轮闸门轮（已被外层闸门同名复写，仅存文件、数值不采信）**：`t21-configure.log` / `t21-build.log` /
  `t21-ctest.log` / `t21-build-dev.log` / `t21-uismoke.log` / `t21-golden.log`；
* **缺陷取证**：`build/asan-smoke-1.log`（D-W3-1）/ `build/w3t13b-asan-1..5.log`；
* **W0–W4 波次日志**：同目录 `build/w0-logs` … `build/w4-logs`；
* **性能与走查**：`bench` 留证 `.cache/bench/bench-report.json`；走查截图 `.cache/walkthrough/{dark,light}/`。
