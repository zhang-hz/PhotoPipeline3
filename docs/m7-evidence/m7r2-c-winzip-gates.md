# m7r2-c-winzip-gates — make_winzip 五门禁本地全跑读数（含 E-2）

- **包**：M7-R2-C（CI 与发布链修复）｜**日期**：2026-10-12｜**机器**：开发机 Windows（win32 10.0.26100 x64）
- **原始日志**：`build/m7r2-c/winzip-gates.log`（首跑）、`build/m7r2-c/winzip-gates2.log`（rc 捕获复跑）——gitignored，本文件为摘录。

## 命令行（逐字）

```bash
# 0) 构建树确认（增量；227 步，触发源见下方「构建面来源注记」）
python tools/env.py run -- cmake --build build/release

# 1) 五门禁 + E-2（release.yml winzip job 同款命令形态；OUT_DIR 默认 dist）
python tools/env.py run -- python tools/make_winzip.py --smoke-exe build/release-dev/photopipeline.exe
```

前置树：`build/release`（打包面）与 `build/release-dev/photopipeline.exe`（E-2 用，含 `--ui-smoke`
的 PP_BUILD_DEV 树；只作只读输入，make_winzip 把它拷入 staging 再跑，不改 build/release-dev）。

**构建面来源注记（如实）**：本轮增量构建 227 步的触发源是**并行任务包 M7-R2A 的在飞未暂存改动**
（`CMakeLists.txt` R-302 FP 收缩开关 + `src/codecs/**`/`src/core/**` 等，`git diff` 可见），
不是版本行——跑门禁时工作树含该批改动，`build/release` 为其重建产物；E-2 的
`build/release-dev/photopipeline.exe` 为当日 10:31 构建（早于该批改动）。五门禁判读的是
**打包面**（闭包/来源/结构/许可/启动），与源码在飞状态无关，读数照实记录于上表。

## 读数（两轮一致；rc=0）

| # | 门禁/断言 | 读数 |
|---|---|---|
| A | 闭包门禁 | `68 个 PE / 105 个导入名 → staging 51 · apiset 17 · system 37`；MUST_BUNDLE 就位：`VC 运行时就位（app-local）: msvcp140/vcruntime140 系列 10 个` |
| A′ | 非传递闭包点 | `1 个已随包（0 个为本次显式补拷）`（jxl_threads.dll） |
| B | 来源门禁 | `5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致（…\Qt\6.11.2\msvc2022_64\bin）` |
| C | 结构断言 | `photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备`；Qt 6.11 DLL 面：链接面 4 模块（Qt6Core/Gui/Widgets/Network）全部随包，staging 内 Qt6*.dll 5 个（Qt6Svg 为插件依赖，由门禁 A 覆盖） |
| D | 许可断言 | 写盘前 `licenses/ = 35 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓`；产物内复核 35 个（≥30 ✓）+ NOTICE ✓ |
| E-1 | 启动烟测 | `PhotoPipeline 0.3.3 (exit 0)`；产物内解压即用 `--version = PhotoPipeline 0.3.3 (exit 0)` |
| E-2 | offscreen 走查 | `UI-SMOKE OK shots=12 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)`（**冻结行逐字**） |
| — | 烟测状态隔离 | `已从 staging 清除走查运行态 3 项（portable data_dir=exe 目录）: classes.json, logs, presets`（快照比对回原状） |
| F | 指纹 | `PhotoPipeline-0.3.3-win64.zip (34731846 bytes, sha256=8a9e398cadc5119f52111fa498e9c48029733e2d4aef98895a73e618ed099327, 条目 105)` |

退出码：`MAKE_WINZIP_RC=0`（winzip-gates2.log 轮捕获）。CRT 部署源：`VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT`；
剔除冗余：vc_redist.x64.exe / dxcompiler.dll / dxil.dll（与脚本头口径一致）。

## 与 WP13 轮的差异归因

WP13（`build/m7-wp13-winzip.log`）同命令**未带** `--smoke-exe`，E-2 未跑（其 handoff 已登记）；
其 zip = 34,727,624 B / sha256 `4a3ec25d…` / 105 条目。本轮 zip 字节与 sha 不同
（34,731,846 B / `8a9e398c…`）——增量重建后 exe/DLL 时间戳与内容面变化所致；zip **不字节可复现**
是发行口径已知事实（release.yml 文件头 §12.7 明文：以「tag → Release 附件」为锚，不以字节可复现为判据）。
条目数 105、许可 35、门禁各计数与 WP13 逐字一致。

## 结论

五门禁（A/B/C/D/E-1）+ E-2 全过，rc=0；冻结行 `UI-SMOKE OK shots=12 pages=3` 逐字命中。
本读数即 §6.4 门禁口径下 make_winzip 的本地取证，与 release.yml winzip job 的判据同源。
