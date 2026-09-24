# PhotoPipeline

批量像素级转码器 + 元数据手术台（batch pixel-level transcoder with metadata surgery），GPL-3.0-or-later。

M0 交付仓库骨架、冻结接口、链接探针与金标语料。**M1a 交付全部编码引擎**：core 基础设施（logger / fsops / 像素预算 / 参数引擎 / 预设）、解码层、色彩层、元数据层、8 个编码器、调度器与 `--dev` 命令行 harness。**M1b 交付完整桌面 UI**：三页主窗口（元数据规则 / 输出配置 / 运行监控）、异步缩略图文件列表、内嵌地图选点、参数表单引擎、单文件 EXIF/XMP 编辑器、设置与预设对话框，以及 `--ui-smoke` 无头走查。**M2 完成 Linux 发行收口**：`--version` 版本单源、AppImage 打包（含离线打包与许可随附）、金样断言级 16 对、回归基线、ASan/UBSan/TSan 清扫，以及持续集成——`build-test.yml` 的**四个 job**（`linux` / `ui-smoke` / `appimage` / `cache-gc`）与缓存播种 workflow `warm-cache.yml`。**M3 完成 Windows 移植与双平台发行**（0.2.0）：UTF-8 全局语义、Mica 深色标题栏、`make_winzip.py` 五项门禁、tag 触发发行自动化。

**M4 = 0.3.0（当前版本）**：从"能用的转码器"到**多格式批处理工作台**——九项需求全量落地：
① 常驻**输入预览** + 手动**分类**（热键打标/持久化）+ 文件列表**勾选**与 EXIF 自动分组视图；
② 元数据卡**生效值显示**（时间/GPS「原 → 生效」+ 关键信息卡，与写路径同源纯函数）；
③ **多格式输出**（1 输入 → N 格式，解码仅一次；输出路径模板 `$format/$dir/$file/$name/$ext`；
逐输出冲突解析）；
④ **逐文件进度**（JPEG/JXL/PNG/TIFF 真实行级 + WebP/HEIF/AVIF 合成进度斜纹标识；逐输出行）；
⑤ **自适应并行**（交错启动可配置 + `alloc_threads` 线程预算，总活跃线程 ≤ T）；
⑥ **GUI 重构**（无边框窗口 + 自绘 caption 三钮、三栏骨架：文件/分类 + 预览 + 三页内容区、
明暗双主题 tokens）；
⑦ **CI 工业化**（pr-fast / main-full / nightly / release 四道，job 与整轮双 ≤20min 硬约束、
clang-format 硬门禁 + clang-tidy 棘轮 + sanitizer 三路）；
⑧ **依赖一把全升**（Qt 6.11.2 / OIIO 3.1.17 / libjxl 0.12 / exiv2 0.28.9 / x265 4.3 / SVT-AV1 4.2 …）；
⑨ **AVX2 性能基线**（x86-64-v3 + 5 对自研热路径 SIMD：flatten / quantize / transpose / downscale / interleave）。
金样断言级 **20 对**、ctest **27 条**、UI 冒烟冻结行 `UI-SMOKE OK shots=12 pages=3`。

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
| `PP_UI_THEME`（环境变量） | **强制 GUI 主题**（走查/截图用），优先于系统主题 | `dark` \| `light`；未设置 = 跟随系统（`src/ui/theme.h` 的单源解析；`--ui-smoke` 截图基准同源） |
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
  --workers N            并发 worker 数（默认物理核数；dev 默认 1 便于日志对照）
  --base DIR             镜像路径基准目录（可多次；默认各输入文件所在目录）
  --log-level LVL        trace|debug|info|warn|error
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
# 单测 + M0 工具 + 金样自检 + linkprobe（release 树共 27 条 ctest 条目）
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
# env: PP_BUILD_DIR（默认 build/m2-release）、QT_DIR（默认从产物 ldd 反查 Qt）
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
- **② 输出页**：转码 / 仅元数据、8 格式 ×（后端 × 技术）参数表单（无损锁定、谓词显隐、位深运行时探测）、色彩目标、同名冲突策略、预设按钮
- **③ 运行页**：进度条 / 吞吐 / 逐文件状态着色、取消（幂等）、结束摘要 + 打开输出目录 / 查看日志
- **双击文件** → 单文件编辑器：EXIF 树（IFD0/Exif/GPS/只读 MakerNote）、XMP、时间/GPS/隐私三态覆盖、忽略批量规则（源文件永远只读）
- **设置**（worker / 内存预算 / 展平底色 / 旋转 / 地图提供方 / 瓦片缓存 / 日志级别）与**预设**（JSON，`预设管理` 对话框）持久化于平台配置目录

地图支持 OSM（WGS-84）与高德（GCJ-02，坐标自动边界转换）双提供方；高德需在设置中填 Web 服务 Key。

### 无头 UI 冒烟（`--ui-smoke`，需 `PP_BUILD_DEV=ON`）

```bash
python tools/env.py run -- cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
python tools/env.py run -- cmake --build build/release-dev
python tools/env.py run -- ctest --test-dir build/release-dev --output-on-failure    # 28 条 = 27 引擎 + ui_smoke
# 或手动跑（offscreen，产出 8 张走查截图）：
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
  - **完整 soname 清单**：打包时产出 `dist/PhotoPipeline-0.1.0-deps.txt`（**本机实测**：闭包检查
    覆盖全部 **36 个 ELF**、逐项列出目标机系统要求 **本机 40 条 / CI 46 条** soname —— 条数差异来自
    打包基础镜像的依赖闭包不同（本机 Ubuntu 26.04 vs CI ubuntu-24.04），非笔误；CI 产物清单见 job 摘要）。
- **磁盘**：AppImage 约 50 MiB；另需输出目录与地图瓦片缓存（数据目录内）空间

### 源码与许可

- **许可**：GPL-3.0-or-later（全文见 [LICENSE](LICENSE)）；本程序不提供任何担保。
- **源码**：<https://github.com/zhang-hz/PhotoPipeline3>（发行版对应源码 = 该仓库同 tag 的提交；
  可执行文件的源码 offer 以仓库 URL 为准）。
- **第三方组件**（与"关于"页清单同源）：Exiv2 / x265（GPLv2+）、libheif（LGPLv3）、Qt（LGPLv3）、
  OpenImageIO（Apache-2.0）、libjxl / libwebp / SVT-AV1 / libaom（BSD）、lcms2（MIT）、
  libpng / libtiff / libjpeg（jpegli 分支）等；各库版权与许可全文随 AppImage 分发
  （`tools/collect_licenses.py` 汇总 → `usr/share/licenses/<port>/copyright`，AppImage 为 40 个 port、
  Windows zip 为 39 个，各见对应发行章节；
  本项目许可 → `usr/share/licenses/PhotoPipeline/LICENSE`）。

## 发行版（Windows）

**M3 起提供 Windows x64 免安装产物**（0.2.0 为首个 Windows 版本，当前版本 0.3.0）；与 Linux 的 AppImage 同一引擎
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
.\photopipeline.exe --version      # → PhotoPipeline 0.3.0（单行 + 退出码 0；非 dev 门控，发布构建亦可用）
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
python tools\env.py run -- ctest --preset release             # 27 条单测
python tools\env.py run -- python tests\golden\smoke.py build\release-dev                       # 20 对金样（需 release-dev 树）
python tools\env.py run -- python tools\make_winzip.py dist --smoke-exe build\release-dev\photopipeline.exe   # 打包 zip
```

`tools/make_winzip.py` 内置与 AppImage 同口径的**五项烟测**（闭包 / Qt 来源 / 结构 / 许可 / 启动，
含 offscreen `--ui-smoke`），任一失败即以非零码退出；产物 = `dist\PhotoPipeline-<版本>-win64.zip`。

### 许可

- 本项目：**GPL-3.0-or-later**（全文见 [LICENSE](LICENSE)）。
- 包内 `licenses/` 随附全部第三方许可文本：`licenses/<port>/copyright`（本版 Windows 包 39 个
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

## 许可

GPL-3.0-or-later，全文见 [LICENSE](LICENSE)。AppImage 内 `libiconv` 未随附许可文本：该 port 在 x86_64-linux 不产出库文件，`iconv*` 由系统 glibc 提供，本发行未分发其代码。
