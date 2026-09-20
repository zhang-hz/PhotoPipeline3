# README 发行章节草案（T12 — 供主对话终审后并入根 `README.md`）

> 用途：根 README 的"发行版（0.1.0）"章节。末尾"附"节是 GPL 自查清单，**内部核对用，不建议随
> README 落盘**。文案/日期/下载地址由主对话终审确定。

## 发行版（0.1.0）

### 下载与运行

发行产物为单一 AppImage（Linux x86_64）；自包含 Qt6、8 个编码器、OIIO、色彩与元数据运行库，免安装。

```bash
# 1) 取得产物：发行页附件，或本地打包产物 dist/
#    PhotoPipeline-0.1.0-x86_64.AppImage（约 50 MiB）
chmod +x PhotoPipeline-0.1.0-x86_64.AppImage

# 2) 运行
./PhotoPipeline-0.1.0-x86_64.AppImage            # 图形界面（文件管理器中亦可双击，需允许"执行"）
./PhotoPipeline-0.1.0-x86_64.AppImage --version  # → PhotoPipeline 0.1.0
```

- AppImage 类型 2：正常挂载运行需要 FUSE（`libfuse2`）。目标机无 FUSE 时改用
  `./PhotoPipeline-0.1.0-x86_64.AppImage --appimage-extract-and-run`（等价环境变量
  `APPIMAGE_EXTRACT_AND_RUN=1`）。
- 无图形环境（服务器/CI）可 `QT_QPA_PLATFORM=offscreen` 运行无头冒烟（见"开发工具"节）。

### 便携模式（设计 §8.5）

- **可执行文件所在目录可写 → 便携模式**：`settings.ini`、`presets/*.json`、`logs/` 落在可执行文件旁。
- **目录只读 → 回退数据目录**：`$XDG_DATA_HOME/PhotoPipeline`（默认 `~/.local/share/PhotoPipeline`），
  预设与日志为其子目录；两处创建都失败时程序以空路径交调用方处理。
- AppImage 的挂载点只读，故**直接运行 AppImage 时自动回退 XDG**（数据在用户目录，不随 AppImage 移动）。
- 需要"真便携"（U 盘/移动介质）：`./PhotoPipeline-0.1.0-x86_64.AppImage --appimage-extract` 解包到可写
  目录后运行 `squashfs-root/AppRun`，数据即写在该解包目录内。

### 系统要求

- **平台**：Linux x86_64（本版单一发行平台；Windows / macOS 未发行）
- **glibc**：≥ 打包机 glibc。本地打包机 = Ubuntu 26.04 / glibc **2.43**；CI 打包 job = ubuntu-24.04 /
  glibc **2.39** → **发行附件建议采用 CI 产物**，兼容面更宽（决策点，见文末）
- **系统库**（不在 AppImage 内，需目标机提供）：
  - `libssl3`：Qt 6.8 的 TLS 后端插件（`libqopensslbackend.so`）运行期 dlopen `libssl.so.3` /
    `libcrypto.so.3`；缺失时 `QNetworkAccessManager` 报 `No functional TLS backend was found`，
    在线地图瓦片与经纬度反查失效（离线功能不受影响）
  - xcb/X11 相关包：`libxcb1`、`libxcb-cursor0`、`libxcb-icccm4`、`libxcb-image0`、`libxcb-keysyms1`、
    `libxcb-randr0`、`libxcb-render-util0`、`libxcb-shape0`、`libxcb-sync1`、`libxcb-xfixes0`、
    `libxcb-xinput0`、`libxcb-xkb1`、`libxkbcommon-x11-0`、`libX11-6`、`libX11-xcb1`、`libSM6`、`libICE6`
  - OpenGL/EGL：`libGL1`、`libEGL1`（Qt xcb 平台插件）
  - 字体与基础库：`libfontconfig1`、`libfreetype6`、`libdbus-1-3`、`libglib2.0-0`、`libxkbcommon0`
  - Ubuntu 24.04 一行：
    `sudo apt install libssl3 libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxkbcommon-x11-0 libgl1 libegl1 libfontconfig1 libdbus-1-3`
- **磁盘**：AppImage 约 50 MiB；另需输出目录与地图瓦片缓存（数据目录内）空间

### 从源码构建到打包（简短路径）

```bash
bash tools/bootstrap.sh        # 一键引导：aqtinstall 装 Qt 6.8、vcpkg 钉死 tag、生成 tools/env.sh
source tools/env.sh
cmake --preset release -B build/m2-release -DVCPKG_MANIFEST_INSTALL=OFF
cmake --build build/m2-release -j
python3 tools/make_icon.py     # 幂等生成 256×256 图标（冻结参数，仓库已入库 PNG）
tools/make_appimage.sh dist    # → dist/PhotoPipeline-0.1.0-x86_64.AppImage（自带 SHA512 校验与烟测）
```

测试、回归基线、sanitizer 抑制文件、无头 UI 冒烟等见 README 的"开发工具"一节。

### 源码与许可

- **许可**：GPL-3.0-or-later（全文见 [LICENSE](../../LICENSE)）；本程序不提供任何担保。
- **源码**：<https://github.com/zhang-hz/PhotoPipeline3>（发行版对应源码 = 该仓库同 tag 的提交；
  可执行文件的源码 offer 以仓库 URL 为准）。
- **第三方组件**（与"关于"页清单同源）：Exiv2 / x265（GPLv2+）、libheif（LGPLv3）、Qt（LGPLv3）、
  OpenImageIO（Apache-2.0）、libjxl / libwebp / SVT-AV1 / libaom（BSD）、lcms2（MIT）、
  libpng / libtiff / libjpeg（jpegli 分支）等；各库版权与许可全文随依赖树提供
  （`vcpkg_installed/x64-linux/share/<port>/copyright`）。

---

## 附：GPL 自查清单（T12 草案 — 内部核对，不随 README 落盘）

| # | 检查项 | 事实 | 结论 |
|---|---|---|---|
| 1 | LICENSE 在库 | 根 `LICENSE` = GPL-3.0 全文（"Version 3, 29 June 2007"） | ✅ |
| 2 | 源文件许可标识 | `src/**` 66 个 .cpp/.h 中 **52** 个带 `SPDX-License-Identifier: GPL-3.0-or-later`；其余 **14** 个恰为 PP-FROZEN 冻结接口头（`codecs/encoders.h`、`core/pipeline.h`、`platform/paths.h`、`ui/preset_io.h` 等），冻结期内不可改 | ⚠ 决策点：是否在 M3 冻结解除时补齐 SPDX（当前由根 LICENSE 覆盖） |
| 3 | "关于"页许可清单 | `src/ui/settings_dialog.cpp` 关于页 `license_text` 列出 GPL-3.0-or-later 与主要第三方库许可类别，版本号引用 `PP_VERSION_STRING`（单源） | ✅ |
| 4 | 第三方许可文本 | `vcpkg_installed/x64-linux/share/*/copyright` 共 **40** 个 port 目录；仓库内可查，AppImage 内未随附许可文本 | ⚠ 决策点：发行附件是否追加 `THIRD-PARTY-LICENSES` 汇总文件或 README 链接 |
| 5 | 源码 offer | 仓库 public，URL 见上；`--version` 与关于页版本一致（0.1.0） | ✅ |
| 6 | 许可兼容性 | GPLv2+（Exiv2/x265）、LGPLv3（libheif/Qt）、Apache-2.0（OIIO）、BSD（jxl/webp/SVT-AV1/aom）、MIT（lcms2）与 GPL-3.0-or-later 组合分发成立 | ✅ |
| 7 | LGPL 链接形态 | Qt 为动态链接（AppImage 内 `usr/lib/libQt6*.so.6`，可替换 → 满足再链接要求）；**libheif / lcms2 / exiv2 / OIIO 为静态链接**（`vcpkg_installed/x64-linux/lib/*.a`，`ldd` 无对应 DSO） | ⚠ 决策点：LGPLv3 组件（libheif）静态链接分发，是否需随附目标文件 / 可重链接形式，请终审裁定 |

**决策点摘要（供主对话）**：① 发行附件取本地（Ubuntu 26.04 / glibc 2.43）还是 CI（ubuntu-24.04 /
glibc 2.39）产物；② 是否随附第三方许可汇总；③ libheif 静态链接的 LGPL 处置；④ 冻结头 SPDX 补齐时点。
