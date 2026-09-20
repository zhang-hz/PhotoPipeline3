# PhotoPipeline

批量像素级转码器 + 元数据手术台（batch pixel-level transcoder with metadata surgery），GPL-3.0-or-later。

M0 交付仓库骨架、冻结接口、链接探针与金标语料。**M1a 交付全部编码引擎**：core 基础设施（logger / fsops / 像素预算 / 参数引擎 / 预设）、解码层、色彩层、元数据层、8 个编码器、调度器与 `--dev` 命令行 harness。**M1b 交付完整桌面 UI**：三页主窗口（元数据规则 / 输出配置 / 运行监控）、异步缩略图文件列表、内嵌地图选点、参数表单引擎、单文件 EXIF/XMP 编辑器、设置与预设对话框，以及 `--ui-smoke` 无头走查。

## 构建（四步）

```bash
# 1. 新机器一键引导：aqtinstall 装 Qt、vcpkg 钉死 tag、生成 tools/env.sh
bash tools/bootstrap.sh

# 2. 载入环境（工具链全部在仓库内：.toolchain/、.cache/、vcpkg/）
source tools/env.sh

# 3. 配置 + 构建
cmake --preset release && cmake --build --preset release

# 4. 生成金标语料 + 跑测试
bash tools/gen_corpus.sh && ctest --preset release
```

其它预设：`cmake --preset dev`（ASan+UBSan，Debug）、`cmake --preset tsan`（TSan，仅 configure）。

**引擎验证构建（含 `--dev`）**：`--dev` 分支由 `PP_BUILD_DEV` 宏保护，发布构建不含该代码路径（design §8.6）。跑 harness / 矩阵 / 规模验证时另建构建目录并打开开关；并行任务各用自己的 `build/<task-id>`，依赖已在 `vcpkg_installed` 就位，关掉 manifest 自动安装以免并发写：

```bash
source tools/env.sh
cmake --preset release -B build/m1-dev -DVCPKG_MANIFEST_INSTALL=OFF -DPP_BUILD_DEV=ON
cmake --build build/m1-dev
```

## M1a 工具与用法

### `photopipeline --dev`（§3.15 冻结 CLI；仅 `PP_BUILD_DEV=ON` 的构建可用）

```text
photopipeline --dev <input...> --out <dir> [options]
  --out DIR              输出根目录（必填）
  --format ID            jpeg|jxl|png|tiff|webp|bmp|heif|avif（默认 jxl）
  --backend ID           后端（avif: svt-av1|libaom；默认空=首选）
  --tech ID              技术（jxl: vardct|modular；webp: lossy|lossless；默认空=首选）
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
| `tests/golden/smoke.sh [BUILD_DIR]` | 8 对转码 + 1 对元数据的金样冒烟：逐例跑 `photopipeline --dev` 并用 `pp_verify` 断言。脚本内的默认 `BUILD_DIR` 指向历史构建目录，**请显式传入自己的构建目录**（或用 `PP_BIN` / `PP_VERIFY` / `OUT_ROOT` 覆盖）；输出 `SMOKE total=9 pass=9 fail=0`，退出码 = 失败例数 |

### 测试命令

```bash
# 单测 + M0 工具 + 金样自检（release 树共 23 条 ctest 条目）
ctest --test-dir build/release --output-on-failure

# 金标语料（27 fixture，幂等；PP_MKFIXTURES= 指向本次构建的 pp_mkfixtures）
PP_MKFIXTURES=build/release/pp_mkfixtures bash tools/gen_corpus.sh

# 8+1 对金样冒烟（需 PP_BUILD_DEV=ON 的构建，脚本走 --dev）
bash tests/golden/smoke.sh build/m1-dev
```

## M1b 桌面 UI

### 构建与运行

```bash
source tools/env.sh
cmake --preset release && cmake --build --preset release
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
source tools/env.sh
cmake --preset release-dev -B build/release-dev -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build/release-dev -j
ctest --test-dir build/release-dev --output-on-failure    # 24 条 = 23 引擎 + ui_smoke
# 或手动跑（offscreen，产出 8 张走查截图）：
QT_QPA_PLATFORM=offscreen ./build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review
```

脚本化走查覆盖：三页遍历截图（01-meta / 02-output / 02b-output-avif / 03-run / 03b-run-done / 04-settings / 05-exif-editor / 06-presets）、参数谓词断言（jxl 无损→modular+distance 0、jpeg quality 显隐、tiff 压缩联动）、地图 GCJ↔WGS 边界断言（点击偏差 <0.001°）、16 文件真实转码运行；成功 stdout 末行 `UI-SMOKE OK shots=8 pages=3`。ctest 入口 `tests/ui_smoke.sh <build_dir>`。

## M0 工具

| 工具 | 用途 |
|---|---|
| `pp_linkprobe` | 链接探针：逐库运行时校验（lcms2 / exiv2(+BMFF) / OIIO 插件 / jpegli / libjxl / libheif HEVC+AV1 编码器 / libwebp）。输出 `PROBE <name> OK\|FAIL <detail>` 与尾部两行 `PLUGINS:`、`HEIF_ENCODERS:`；退出码 = FAIL 数 |
| `pp_mkfixtures` | fixture 生成与校验：`--make <dir>` 生成 exif_full.jpg / webp×2 / heif_exif.heic / avif_exif.avif / jxl_exif.jxl / cmyk.tif，`--verify <dir>` 逐项回读校验（`MADE`/`FIXTURE` 行，退出码 = FAIL 数，目录缺失 → SKIP 77） |
| `pp_spikes` | Spike 验证：`e` = lcms2 数值金值（sRGB→sRGB 恒等 ≤1e-5；白点→Lab(D50) L∈[99.5,100.5]）；`f --golden-root <dir>` = Exiv2 无损重写保真（SOS `FF DA` 之后字节完全一致 + OIIO 像素 hash 相等） |
| `tools/gen_corpus.sh` | 生成 `tests/golden/{base,edge,meta}` 语料并写出 `tests/golden/CHECKSUMS`（幂等；可 `OIIOTOOL=` / `PP_MKFIXTURES=` 覆盖工具路径） |

## 文档

- [docs/design.md](docs/design.md) — 架构与设计
- [docs/param-catalog.md](docs/param-catalog.md) — 参数目录（编码器参数语义来源）
- [docs/brainstorm-consensus.md](docs/brainstorm-consensus.md) — 共识记录
- [docs/m0-tasks.md](docs/m0-tasks.md) — M0 任务书（执行依据 / 冻结契约）
- [docs/m1-tasks.md](docs/m1-tasks.md) — M1 任务书（界面冻结清单 / 批次划分 / 出口准则；M1a 引擎）
- [docs/m1b-tasks.md](docs/m1b-tasks.md) — M1b 任务书（UI 冻结头 / 全局规格 / 落地口径与裁定记录；M1b 界面）
- [docs/m1b-report.md](docs/m1b-report.md) — M1b 收口报告（任务/裁定/事故/验收证据）

## 许可

GPL-3.0-or-later，全文见 [LICENSE](LICENSE)。
