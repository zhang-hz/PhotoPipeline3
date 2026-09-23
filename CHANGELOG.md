# 变更日志

## [0.2.0] — 2026-09-23

首个 **Windows 版本（x64）**：在 0.1.0（Linux x86_64 / AppImage）基础上完成 M3 的 Windows 移植与
打包自动化，形成 **Linux + Windows 双平台**产物。工具链 = MSVC（VS 2022/2026 Build Tools）+ vcpkg
（`x64-windows` 三元组，**动态**链接）+ Qt 6.8.3（`msvc2022_64`）。

### 新增

**平台与移植**

- Windows 支持（x64，**Windows 10 1903+ / Windows 11**；1903 是 UTF-8 代码页应用清单的下限）：
  三页主窗口、内嵌地图、缩略图、设置/预设、`--dev` harness 与 `--ui-smoke` 脚本化走查全部可用
- **全局 UTF-8**：可执行文件嵌入应用清单（`activeCodePage=UTF-8`）+ 全量 `/utf-8` 编译 ⇒
  进程 ACP = UTF-8，与 Linux 的 UTF-8 字节语义对齐（此前 ACP 跟随系统区域，非 ASCII 语料
  `tests/golden/base/edge/测试📸unicode.png` 经宽→窄转换失败会 abort）
- **Mica 深色标题栏**与系统主题跟随（`src/platform/mica`；不可用时优雅降级为常规标题栏）

**打包与发行**

- **Windows 打包后端 `tools/make_winzip.py`**（stdlib-only，与 AppImage 后端四项烟测同口径）：
  顶层单一目录 `PhotoPipeline/` 的 `PhotoPipeline-<版本>-win64.zip`（解压即用）；app-local
  **VC 运行时**（`Microsoft.VC*.CRT` 整目录，不依赖目标机装 VC redist）；五项门禁 ——
  A 闭包（PE 导入表逐名三判据 + `MUST_BUNDLE` 前缀，见下）· B 来源（`Qt6*.dll` 与 Qt 工具链
  sha256 逐一一致）· C 结构（exe + `platforms/{qwindows,qoffscreen}.dll` + `imageformats/` + `tls/`）·
  D 许可（`licenses/*/copyright` ≥30，写盘前与 zip 内各一次）· E 启动（staging `--version` 冻结行；
  `--smoke-exe` 时追加 offscreen `--ui-smoke` 走查）；并剔除 `vc_redist.x64.exe` 与
  `dxcompiler.dll`/`dxil.dll`（D3D12 RHI 着色器编译器，本应用 QWidget/QPainter 路径不用）
- 第三方许可随附：`tools/collect_licenses.py` 汇总 vcpkg port 许可文本（新增 `--dest` 以适配
  Windows 布局 `licenses/`；缺省 `<APPDIR>/usr/share/licenses` 逐字未变）
- CI（`build-test.yml`）新增 **`windows`**（release ctest 23 + release-dev 子集金样 16）与
  **`winzip`**（打包 + `PhotoPipeline-win64` artifact）两个 job；Windows 缓存键前缀 `vcpkgwin-`、
  Qt 键 `qt-6.8.3-win64_msvc2022_64-*`，与 Linux 侧严格分离；`warm-cache.yml` 新增 `warm-windows` 播种
- 新增 **tag 触发发行自动化** `.github/workflows/release.yml`（`on: push: tags: ['v*']`）：
  Linux AppImage + Windows zip 并行构建并上传 artifact，`publish` job 做 **tag↔版本一致性断言**
  后 `gh release create`（tag 已存在同名 release 时失败，不静默覆盖）；日常 CI 不再被 tag 重复触发

**质量设施**

- 金样 driver / UI 冒烟 / 许可汇总 / 环境注入 / 语料生成 / 回归 / 打包等**共享脚本统一为 Python
  单实现**（删除 bash 版），冻结行与退出码逐字不变
- 测试与工具二进制同样嵌入 UTF-8 应用清单（与主程序一致），并补 SPDX 标注（14 个 PP-FROZEN 头文件）

### 修复

- **控制台输出可见性**：GUI 子系统进程的 std 句柄判据由"句柄是否有效"改为"**是否捕获型**
  （`GetFileType ∈ {PIPE, DISK}`）"——捕获型（ctest / `subprocess` / shell 重定向）原样保留，
  不捕获时挂接父控制台；控制台输出代码页只在**自己**挂接成功时改写（不污染父 shell 共享控制台）
- **日志文件行尾统一 LF**（spdlog formatter 显式 `eol="\n"`）；`stdout`/`stderr` 仍为平台文本模式
  （Windows CRLF），脚本匹配器一律 `rstrip('\r\n')`
- `capture_stderr` 的 Windows 实现（`_dup`/`_dup2`/`_close` + 二进制模式捕获文件），此前 Windows
  分支是空存根（恒返回空串，4 项 stderr 断言失去意义）
- `file_clock` 转换改为**编译期探测**（`src/core/filetime.h`：`requires` 在实例化期择
  `from_sys` / `from_utc`，避免绑定某个 STL 版本）；本机 MSVC 14.51 走 `from_utc`
- 预设读取改用 `_wfopen`（`fs::path::c_str()` 在 Windows 为 `wchar_t*`）；正确性不再依赖 ANSI 代码页
- WebP 的 ICCP mux 符号显式接线（`PkgConfig::WEBPMUX`）：`pp_core` 经 whole-archive 自注册，
  mux 符号对所有消费者必达，而 Windows 动态 pkg-config 不携带该传递依赖
- POSIX-only 判据平台化：`noread` fixture（Windows 下 `fs::perms::none` 不禁止读取）、
  `test_presets` 的**非法 UTF-8 原始字节路径**用例（Windows 路径为 UTF-16，不可表示）
- 测试侧 POSIX 垫层 `tests/unit/env_compat.h`（`setenv`/`unsetenv`/`getpid`）；空值环境变量语义
  经探针实证（UCRT 下"存在但为空"不可表达）
- 三个测试二进制启动即 abort（`0xc0000409`）的根因 = 缺 UTF-8 清单导致 ACP 不匹配（见"新增"）

### 已知问题

- **Windows 产物未字节可复现**：zip 条目内嵌文件 mtime ⇒ 同源两轮 sha256 必然不同
  （与 0.1.0 的 AppImage 同类，已接受）；仅承诺**结构一致 + 依赖清单一致**
- **无 Windows 回归基线**：`tools/baseline/golden.log` 是 Linux 侧基线（其内 `version=0.1.0`），
  Windows 侧尚无 `golden.windows.log`；`tools/regression.py` 未在 Windows 上接入 CI
- **非 ASCII 交互控制台**：中文控制台代码页下 `--version` 等输出仍按平台文本模式，未强制 UTF-8
  控制台代码页（管道/重定向场景为 UTF-8 字节）
- **AVIF 10-bit + alpha 不可用**（承 0.1.0，SVT-AV1 上游缺陷，编码前明确报错并建议 libaom）
- 部分文档/工具仍含 `0.1.0` 字面量：`tools/baseline/golden.log`（Linux 基线，历史值）、
  `tools/appimage-gui-smoke.sh` 默认文件名、`tools/tls_probe.cpp` 的 User-Agent 字符串、
  `README.md` 的 v0.1.0 AppImage 发行引用（历史发行件，如实保留）
- Linux 侧行为与 0.1.0 一致（本版对 Linux 的影响仅限共享脚本 Python 化与日志行尾/句柄判据的
  等价重构；ctest 23 + 金样 16 在 Windows 侧复跑通过，Linux CI 判定由发行轮执行）

### 发行

- Windows：`PhotoPipeline-0.2.0-win64.zip`（本机打包件；**release 上传由主对话执行**）
- Linux：`PhotoPipeline-0.2.0-x86_64.AppImage`（由 `release.yml` 的 `appimage` job 构建）

## [0.1.0] — 2026-09-20

首个发行版本：**Linux x86_64 单平台**，单一产物 AppImage。M0（骨架/冻结接口/语料）→ M1a（引擎）
→ M1b（界面）→ M2（debug 收口 + 发行状态）后达到发行状态。

### 新增

**引擎**

- 8 种编码格式：JPEG（jpegli）、JXL（libjxl）、PNG（libpng）、TIFF（libtiff）、WebP（libwebp）、
  BMP、HEIF（libheif / x265）、AVIF（SVT-AV1 / libaom）；位深可选值 = 静态能力集 ∩ 运行期探测，
  越界请求给出明确错误（不静默降档）
- 色彩管理（lcms2）：色彩目标 keep / srgb / p3 / adobergb；M2 新增无 ICC 的 JXL 源的 CICP 最小映射
  （sRGB / Display P3 / BT.2020，未支持组合维持 sRGB 并记日志）
- 元数据手术（Exiv2）：EXIF / XMP / GPS 读写与无损重写；"仅元数据"模式对 JPEG / PNG / TIFF / WebP
  保持字节级保真（零重编码）
- 调度器：像素预算记账、可取消（取消幂等）、多 worker、同名冲突策略 skip / overwrite / rename、
  逐文件状态与运行摘要（成功/失败/跳过/取消、耗时、吞吐）
- `photopipeline --dev` 命令行 harness（仅 dev 构建）：全语料矩阵、日志与回归基线入口

**界面**

- 三页主窗口（元数据规则 / 输出配置 / 运行监控）：拖放与目录递归收集、异步缩略图、文件名搜索、
  十二态状态徽标、不支持文件计数
- 参数表单引擎：8 格式 × 后端 × 技术的参数联动（谓词显隐、无损锁定、内省默认值）；M2 新增
  跨字段约束（webp lossy `qmin > qmax`、jpeg `progressive + optimize_coding`）红字反馈与启动阻断
- 内嵌地图选点：OSM（WGS-84）与高德（GCJ-02 自动边界转换）；断网自动降级，高德需在设置填 Key
- 单文件编辑器：EXIF 树（IFD0/Exif/GPS/只读 MakerNote）、XMP、时间/GPS/隐私三态覆盖，源文件永远只读
- 设置与预设（JSON）持久化；M2 新增 XMP 页搜索框、时间预览异步化、未选中格式按钮可点性边框、
  搜索框列对齐、设置页按当前页收缩、预设空态提示移入列表区

**发行与质量设施**

- `photopipeline --version` → `PhotoPipeline 0.1.0`；版本单源（`cmake/version.h.in` 由 CMake 生成，
  唯一改动点 = 顶层 `project(PhotoPipeline VERSION 0.1.0)`），关于页与 `--version` 同源
- `PP_LOG_LEVEL` 环境变量启动期一次性覆盖日志级别（非法值 stderr 提示并忽略）；单日志文件
  16 MiB 上限（截断后保留尾部 8 MiB，轮转规则不变）
- AppImage 打包（`tools/make_appimage.sh`，免安装、便携）+ 参数化生成的 9 色块应用图标
  + desktop 文件；打包器与 type2 runtime 二进制均入库并旁置 SHA512（T11d：`--runtime-file` 指向
  入库 runtime，**全程离线**；缺件或校验失败即 exit 2 硬失败，不回退到在线下载）
- 第三方许可随附（T11c）：`tools/collect_licenses.sh` 汇总 **40 个 port** 的许可文本 →
  `usr/share/licenses/<port>/copyright`，本项目 `LICENSE` → `usr/share/licenses/PhotoPipeline/LICENSE`；
  打包烟测从产物内解包逐项校验
- AppImage 依赖闭包修复（T18）：插件依赖解析到**系统 Qt 6.10** 导致 xcb 插件加载失败（双击无响应）
  → 闭包输入扩为「主二进制 + 全部插件」、随包 26 个 `.so`、`libQt6*` 溯源硬门禁（逐字节 `cmp`）、
  覆盖全部 **36 个 ELF** 的 A/B 两类闭包门禁，并新增真实显示 GUI 烟测（`tools/appimage-gui-smoke.sh`）
- 金样断言级 **16 对**（像素 + 元数据值 + warnings 三面断言）；全语料回归基线
  （`tools/regression.sh` + `tools/baseline/golden.log`，规范化后双跑零 diff）
- sanitizer 清扫：ASan/UBSan 全语料矩阵零发现；TSan 并发路径 0 报告（抑制文件附 happens-before 论证
  与阳性对照）
- GitHub Actions 工作流（`build-test.yml`）**四个 job**：`linux`（release 构建 + ctest 23 + 16 对金样）
  · `ui-smoke`（release-dev + offscreen 无头冒烟）· `appimage`（打包 + 四项内置烟测 + 产物启动烟测 +
  `PhotoPipeline-AppImage` artifact）· `cache-gc`（缓存清理）；三类缓存（Qt 工具链 / ccache /
  vcpkg 二进制缓存）由 `warm-cache.yml` 播种（`workflow_dispatch` + 每周 cron）

### 修复

- UI 观感 5 项（M1b 审查遗留）：未选中格式按钮可点性、搜索框左边界、设置页留白、预设空态位置、
  XMP 页缺搜索框
- 时间预览扫描由 GUI 同步改为后台异步（200 文件上限；批变化时取消旧任务，避免乱序回填与冻结）
- 短批次运行不再输出无信息量的 budget 日志（`elapsed < 1s` 守卫）
- 未知编码参数、元数据写失败现在进入用户可见警告通道（此前仅落日志，UI 不可见）
- 非 UTF-8 locale 下预设保存/载入、设置 INI、日志目录的路径往返不一致（统一走 locale 安全层）
- `pp_verify` 元数据文本化不稳定（有理数定形 `a/b`、ASCII trim、数组 `, ` 连接）
- 链接自注册统一为 whole-archive 形态并删除冗余 anchor 符号；UI 冒烟断言改为顺序无关
- 运行页顶部状态行因布局激活推迟被裁切（末位数值缺失）→ 同步 `layout()->activate()`（T22）
- AppRun 失败弹窗改为**有界**：`zenity` / `xmessage` 各自带 60 秒超时（自动消失，不再无界阻塞
  自动化调用者），并新增 `PP_NO_GUI_POPUP=1` 开关（置 1 时完全不弹窗，stderr 与日志照旧写）（T24）
- AppRun 在**缺少 `~/.cache`（或 HOME 不可写）的账户**上会静默退出（无窗口、无日志）——根因是
  `: >"$LOG"` 里的 `:` 属 POSIX 特殊内建，重定向失败会直接终止 shell（退出码 2），`|| LOG=/dev/null`
  兜底永不执行；现改为「先建日志目录、再用非特殊内建探测可写、失败即回退 `/dev/null`」，双击启动
  不再受该目录影响（T27）
- CI / 干净环境构建修复：nasm 缺失、jpegli pkgconfig、libjpeg `jerror.h`、vcpkg 二进制缓存键

### 已知问题

- **仅 Linux x86_64 发行**：Windows / macOS 未构建（内存探测与数据目录的 Windows 分支顺延 M3）
- **AVIF 10-bit + alpha 不可用**：SVT-AV1 后端的上游缺陷（会破坏堆），程序在编码前明确报错并建议
  改用 libaom 后端；HEIF/x265 后端则不提供线程数参数，无法强制单线程（保留后端默认线程池，已实测）
- 第三方 sRGB ICC 源转到 sRGB 目标存在 ≤1.8e-4 的暗部偏差（lcms2 优化器精度；处于数值契约内，
  已记录为事实）
- 输入目录收集不跟随符号链接；批次内输出名冲突判定为词法近似（Linux 文件系统大小写敏感，不构成
  实际问题）
- 超大输入生成缩略图时仍执行一次全解码（内存峰值处于缩略图预算内）
- 在线地图瓦片与经纬度反查依赖网络与高德 Web 服务 Key；离线时自动降级（本版地图精度已接受）
- AppImage 不承诺字节可复现（squashfs 超级块时间戳参与打包）；运行依赖目标机提供 `libssl3`
  （Qt TLS 后端 dlopen 系统 OpenSSL，缺失时在线地图失效）与 xcb 相关系统库（见 README 发行章节）
- GitHub Actions 为托管运行（ubuntu-24.04 runner），本地不可完全复现该环境；本版收口轮次的 CI
  结果（四 job 全绿）见 M2 报告 —— 终轮 run `35521800777`（head `d1b5fb7`）：`linux` 1m32s /
  `ui-smoke` 1m45s / `appimage` 1m37s / `cache-gc` 9s

### 发行

- Release: <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0> —— 附件
  `PhotoPipeline-0.1.0-x86_64.AppImage`（**50,940,408 B**，sha256 `31f75c48052be34a…`，前 16 位；
  完整校验值以 Release 页为准）
