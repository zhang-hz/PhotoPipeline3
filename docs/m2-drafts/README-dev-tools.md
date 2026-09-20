# README 开发工具段落草案（T12 — 供主对话终审后并入根 `README.md`）

> 用途：根 README 的"开发工具（M2 新增）"一节。命令与冻结环境变量行均取自脚本头注释
> （`tools/regression.sh`、`tools/lsan.supp`、`tools/tsan.supp`、`tests/golden/smoke.sh`）与
> `docs/m2-tasks.md` §2.9/§4。

## 开发工具（M2）

| 工具 | 用途 |
|---|---|
| `tools/regression.sh` | 全语料 `--dev` 回归基线：日志规范化后与 `tools/baseline/golden.log` diff（双跑零 diff） |
| `tools/lsan.supp` | LeakSanitizer 抑制文件（当前**无生效规则**，注释即论证） |
| `tools/tsan.supp` | ThreadSanitizer 抑制文件（每条规则附 happens-before 论证 + 阳性对照） |
| `tests/golden/smoke.sh` | 金样冒烟：**16 对断言级**用例（像素 + 元数据值 + warnings） |
| `photopipeline --ui-smoke` | 无头 UI 走查：三页遍历 + 参数谓词/地图边界断言 + 真实转码，产出 8 张截图 |
| `tools/make_appimage.sh` | 打包 AppImage（离线、可重复重跑；内置三项烟测） |

### `tools/regression.sh` — 全语料回归基线

```bash
bash tools/regression.sh [BUILD_DIR] [--update]
#   BUILD_DIR  默认 build/release-dev（须为 -DPP_BUILD_DEV=ON 的构建）
#   --update   用当前构建重新生成 tools/baseline/golden.log（而非对比）
```

- 运行内容：27 输入 × 8 格式 + `--metadata-only` × base 16 × 4 格式 = **12 次顺序调用 / 280 文件槽**；
  `tests/golden/real/`（用户样本）与 `tests/golden/smoke/`（归 smoke.sh）不入基线。
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

### `tests/golden/smoke.sh` — 金样冒烟（16 对断言级）

```bash
bash tests/golden/smoke.sh <build_dir>      # 须为 -DPP_BUILD_DEV=ON 的构建
```

- 16 对用例：jpeg-lossy / jxl-lossless / png16-lossless / tiff16-lzw（走 `--preset` 分支）/
  webp-lossless / heif-lossy / avif-lossy / bmp-exact / meta-artist / gray-webp / alpha-jpeg（展平警告）/
  depth-jpeg（降档警告）/ multipage-png（多页截断警告）/ unicode-png（中文+emoji 路径）/ exif-roundtrip /
  metaonly-jpeg（仅元数据）。
- 每例跑 `photopipeline --dev` 后用 `pp_verify` 对 `tests/golden/smoke/<case>.json` 断言三面：
  像素（exact / PSNR+阈值）、元数据值（规范化文本）、warnings 列表。
- 输出末行 `SMOKE total=16 pass=16 fail=0`；**退出码 = 失败例数**。可用 `PP_BIN` / `PP_VERIFY` /
  `OUT_ROOT` 覆盖默认路径（默认输出到 `.cache/out-smoke/`）。

### `--ui-smoke` — 无头 UI 走查

```bash
QT_QPA_PLATFORM=offscreen build/release-dev/photopipeline \
    --ui-smoke --inputs tests/golden/base --shots .cache/ui-review
# ctest 入口：tests/ui_smoke.sh <build_dir>（ctest -R ui_smoke）
```

- 8 张截图：`01-meta` / `02-output` / `02b-output-avif` / `03-run` / `03b-run-done` / `04-settings` /
  `05-exif-editor` / `06-presets`；覆盖三页遍历、参数谓词断言（jxl 无损→modular+distance 0、jpeg quality
  显隐、tiff 压缩联动）、地图 GCJ↔WGS 边界断言（点击偏差 <0.001°）、16 文件真实转码。
- 成功 stdout 末行 `UI-SMOKE OK shots=8 pages=3`；UI 类不进 ctest 单测，此即 UI 的回归通道。

### `tools/make_appimage.sh` — 打包

```bash
tools/make_appimage.sh [OUT_DIR]     # 默认 dist/
# env: PP_BUILD_DIR（默认 build/m2-release）、QT_DIR（默认从产物 ldd 反查 Qt）
# 产物: <OUT_DIR>/PhotoPipeline-0.1.0-x86_64.AppImage + PhotoPipeline.AppDir/（先删后建，可重跑）
```

- 输入为 release 预设产物；版本号从产物 `--version` 读取（不在脚本硬编码）；打包器
  `tools/bin/appimagetool-x86_64.AppImage` 入库并先校验旁置 `.sha512`；打包以
  `APPIMAGE_EXTRACT_AND_RUN=1` 运行（不依赖 FUSE）。
- 组装：`usr/lib` = `ldd` 闭包中的非系统 `.so`（Qt6*、libjpeg 等），Qt 插件
  （platforms/imageformats/iconengines/styles/**tls**）、OIIO 插件目录（静态 OIIO 时留空），
  `usr/share` 拷图标与 desktop，`AppRun` 导出 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` / `OIIO_LIBRARY_PATH`。
- 脚本内烟测：① 产物 `--version` 输出 `PhotoPipeline 0.1.0`；② AppDir 二进制 `ldd` 无 `not found`；
  ③ `AppRun` 存在且 desktop 的 `Exec=`/`Icon=` 与冻结文本逐行一致。
- 不承诺字节可复现（squashfs 超级块时间戳/mtime 参与），同结构不同 sha 按现状接受。
