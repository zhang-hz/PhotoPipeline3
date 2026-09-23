# PhotoPipeline M3 收口报告（Windows 移植 + 双平台发行）

- **里程碑**：M3 = Windows 移植与双平台发行（任务书 `docs/m3-tasks.md`，冻结于 `e501a04`）。
- **基线**：M2 收口 `0e700f3`（v0.1.0 发行之后）；分支 `main`。
- **版本**：本里程碑把版本单源由 `0.1.0` 提升为 **`0.2.0`**（`CMakeLists.txt:2`
  `project(PhotoPipeline VERSION 0.2.0 …)`；`cmake/version.h.in` 生成 `PP_VERSION_STRING`，
  `--version` 与"关于"页同源）。
- **提交锚点**：W1–W4 的落地提交见 §2；W5（本文档 + CHANGELOG + README + SPDX）为收口轮，
  **报告不声明最终 HEAD**，当前状态以 `git log origin/main` 为准。

---

## 1. 概览与出口准则对照

M3 的目标（任务书 §1）：在 M1a/M1b/M2 冻结行为的基础上，**把 PhotoPipeline 带到 Windows**，
补齐打包、CI 与发行自动化，并保证 Linux 侧"零破坏"。

出口准则（任务书 §4-W4，`docs/m3-tasks.md:179` 原文）：

> **出口**：design §9.1 M3 出口准则——Win CI 全绿；干净 Win11 25H2 zip 解压即用。

W5 出口（`docs/m3-tasks.md:185` 原文）：

> **出口**：双平台 release tag 产物齐（S2 自动化）；`TODO(M3)` 归零；文档收口。

| # | 出口项 | 本机状态（本报告实测） | CI 状态 |
|---|---|---|---|
| 1 | Windows release/release-dev 构建通过 | ✅ `cmake --preset release` / `release-dev` 均 exit 0 | ✅ run #22 `windows` job success（8.2 min；冷装已由 #21 买断） |
| 2 | ctest（Windows） | ✅ release **23/23**、release-dev **24/24**（含 `ui_smoke`） | 同上 |
| 3 | 金样 16 对 | ✅ `SMOKE total=16 pass=16 fail=0` | 同上 |
| 4 | UI 冒烟 8 截图 / 3 页 | ✅ `UI-SMOKE OK shots=8 pages=3` + `UI-SMOKE pass` | 同上 |
| 5 | `--version` 冻结行 + 句柄可见性 | ✅ `PhotoPipeline 0.2.0`（管道/文件句柄；console 型未附加句柄亦可见，见 §4） | 同上 |
| 6 | Windows 打包（zip 解压即用） | ✅ `PhotoPipeline-0.2.0-win64.zip`（32,782,738 B / 108 条目；五项门禁 + 产物内复核） | 同上 |
| 7 | 许可随附 | ✅ `licenses/` 39 个 port `copyright` + 本项目 `LICENSE`（写盘前与 zip 内各校验一次） | 同上 |
| 8 | CI：windows/winzip job + tag 发行自动化 | 已落地（`build-test.yml` / `warm-cache.yml` / `release.yml`） | ✅ run #22 **六 job 全绿**（详见 §5.6）；`release.yml` 的 tag 自动化尚无 tag 触发记录 |
| 9 | `TODO(M3)` 归零 | ✅ `paths.cpp` / `pixelbudget.cpp` 两处随 T4 清零（commit `1c7de35`） | — |
| 10 | 14 个 PP-FROZEN 头 SPDX | ✅ 补齐，`PP-FROZEN` 文件 **63/63** 均带 SPDX（src 内 SPDX 文件 55 → 69） | — |

> **CI 段收敛**：本报告 CI 证据已由主对话回填 —— **最终轮 run #22（id `35848888594`，head `f7b0579`）六 job
> 全绿**；**首轮 run #21（head `342f1dc`）红**并已定位根因（同一机制的两个表现），由 T11b 消除。
> 逐 job 耗时、逐字冻结行与首轮失败分析见 **§5.6**。

---

## 2. 波次执行记录 W1–W5

| 波次 | 任务 | 落地提交 | 出口证据（实测） |
|---|---|---|---|
| 预备 | 任务书冻结 v1.0（W1–W5、裁定 D1–D6、纪律条款） | `e501a04` | `docs/m3-tasks.md` 入库 |
| **W1** | T1 overlay Windows 适配（jpegli 产物名平台化 + x265 dynamic multilib + manifest platform 限定 pkgconf）；T2 `vcpkg install`（x64-windows，41 port） | `0ce1389` | `vcpkg_installed/x64-windows` 39 port 就位；`pp_linkprobe` 9/9；位深能力集 heif/x265 = 8,10,12、avif/svt-av1 = 8,10、avif/libaom = 8,10,12 |
| **W2** | T3 构建接线（自包含 presets、MSVC 块、UTF-8 清单、依赖闭包、`find_package(Python3)`）；T4 `paths.cpp`/`pixelbudget.cpp`/`main.cpp` Windows 分支（清 TODO(M3) ×2）；T5 `mica.cpp` + 接线；T6 测试侧平台断言分支 | `89a739a`、`1c7de35` | configure/build 通过；`generate` 产出 `photopipeline.manifest`；Mica 生效 |
| **W3** | T7 `tools/bootstrap.py`（aqt arch 分派、vcpkg、vcvars 注入）；T8 六脚本 Python 单实现 + 调用点更新 + **删 bash 版** | `e1d2f38`、`5210ee2` | 两平台同入口；冻结行逐字（`SMOKE total=16 pass=16 fail=0`、`UI-SMOKE OK shots=8 pages=3`）与退出码不变 |
| **W4** | T9 Windows 本地全绿（首轮通电修复）；T10 `tools/make_winzip.py`；T11 CI 三文件 | `82aa316`、`9fe72a4`、`8c7e8f3`、`342f1dc` | ctest 23/24、金样 16/16、ui_smoke 8 截图、zip 五项门禁全过（本报告 §5） |
| **W5** | T12 文档收口（README Windows 段 / CHANGELOG 0.2.0 / 本报告）；T13 14 个冻结头 SPDX | 本收口轮 | 版本 0.2.0 全链复验通过（§5）；SPDX 63/63 |

**执行顺序偏差（据 `git log`）**：T6 与 T9 合并提交为 `9fe72a4`（`M3-T6/T9`），T4/T5 合并为
`1c7de35`（`M3-T4/T5`）——均为同文件/同主题的追加式合并，未 amend/rebase。

---

## 3. 裁定记录

### 3.1 用户裁定（D1–D6 / S1、S2；`docs/m3-tasks.md:103-145`）

| # | 议题 | 裁定 |
|---|---|---|
| D1 | Windows triplet | **`x64-windows`（动态）**——LGPL 干净（libheif 以 DLL 分发）、x265 dynamic 为上游支持路径、与 Linux 发行形态对称、Qt 本就 `/MD` |
| D2 | 全局 UTF-8 | **接受**：应用清单 `activeCodePage=UTF-8` + `/utf-8` ⇒ argv/getenv/fopen/`path::string()` 全 UTF-8；代价 = 最低 **Win10 1903+** |
| D3 | 共享脚本 | **Python 单实现**，删除 bash 版（六件：bootstrap / gen_corpus / smoke / ui_smoke / regression / collect_licenses），冻结行与退出码逐字节保持 |
| D4 | 打包 | **共享逻辑单实现 + 机制后端分派**：`collect_licenses.py` 两后端共用（新增 `--dest`），`make_appimage.sh` 冻结不动，新增 `tools/make_winzip.py` |
| D5 | GUI 子系统输出 | `/SUBSYSTEM:WINDOWS` + `AttachConsole(ATTACH_PARENT_PROCESS)`（双击无黑框；终端/CI 可捕获） |
| D6 | Mica / 深色标题栏 | `src/platform/mica.cpp`：`DWMWA_SYSTEMBACKDROP_TYPE` + `DWMWA_USE_IMMERSIVE_DARK_MODE` 随系统主题；非 Windows 编译为 no-op |
| S1/S2 | CI 与发行 | `build-test.yml` 增 `windows`/`winzip` job；`warm-cache.yml` 增 windows 播种；新增 tag 触发 `release.yml`（双产物 + `gh release`）；**销 m2 候选 #11（发行取件自动化）与 #12（tag 重复触发去重）** |

### 3.2 执行期裁定全表（逐条：问题 / 根因证据 / 裁定 / 落地）

| 裁定 | 问题 | 根因证据 | 裁定与落地 |
|---|---|---|---|
| **v1.1** | MSVC `/W4` 下 `min/max` 宏与 `NOMINMAX`；x265 库名含 `/lib/` 段 | `windows.h` 的 `min/max` 宏污染；x265 导出名平台差异 | `NOMINMAX` 进 MSVC 块；x265 `X265_DEPTH_LIB` 命名平台化（去 `/lib/` 段）→ `CMakeLists.txt` / overlay portfile |
| **v1.2** | x265 `EXTRA_LIB` 以 `;` 传入被 `execute_process` 拆表 | 分号在 CMake 列表语义下是分隔符 | 转义为 `\;` → `vcpkg-overlay/x265/portfile.cmake` |
| **v1.3** | jpegli 静态库在 MSVC 下 ABI/导出不匹配 | 上游把 jpegli 编入 `EXCLUDE_FROM_ALL` 且 `libjpeg.so` 用 `--exclude-libs` 隐藏符号 | 新增 overlay patch（`jpegli-win32-static-compat.patch`）+ portfile `PATCHES` + config v2 → `vcpkg-overlay/libjpeg-turbo/*` |
| **v1.3a** | `win/jconfig.h` 生成后不覆盖（同时间戳） | `file(COPY)` 对同时间戳文件不覆盖 | `file(REMOVE)` + `file(COPY)` 模板替换 → `vcpkg-overlay/libjpeg-turbo/portfile.cmake` |
| **v1.4** | GUI 子系统下 `--version`/`--dev` 输出不可见 | 双击无控制台；管道句柄需保留 | 控制台守卫**一版**：仅当 `GetStdHandle` 无效时 `AttachConsole` → `src/main.cpp` |
| **v1.5** | 测试侧 POSIX 环境/进程 API 缺失（`setenv`/`unsetenv`/`getpid`/`unistd.h`） | MSVC 不提供 POSIX 环境 API | 新增 `tests/unit/env_compat.h` 薄垫层（`_putenv_s`/`_getpid`）；空值环境变量语义经**探针实证**（`.cache/envprobe.cpp`：UCRT 下"存在但为空"不可表达，`ptr=null`） |
| **v1.6** | `clock_cast<file_time_type>` 方案被 MSVC 证伪 | 探针 cp1–cp4：`clock_cast` 对 dest=file_clock 落 `_None`（`chrono.h:3058`），`from_sys` 已被 LWG 3694 移除 | 裁定 1 作废；顺带裁定 `noread` fixture 为 **POSIX-only**（Windows `fs::perms::none` 不禁止读取） |
| **v1.7** | `file_clock` 双 STL 分歧（libstdc++ `from_sys` vs MSVC `from_utc`） | 两侧各持一对成员，无单表达式解 | **平台分派**：`#if defined(_WIN32)` → `file_clock::from_utc(utc_clock::from_sys(...))`，POSIX 保持 `from_sys`；`preset_io.cpp` 读取改 `_wfopen`（`path::c_str()` 为 `wchar_t*`） |
| **v1.8** | LNK2019：`WebPMux*` 未解析（4 符号 × 全部链接目标） | `pp_core` 经 `pp_core_registered` whole-archive 自注册 ⇒ `enc_webp.obj` 对所有消费者必达；Linux 静态 pc 闭包携带 mux、Windows 动态 pc 不带 | 新增 `pkg_check_modules(WEBPMUX … libwebpmux)` + `pp_core` PUBLIC 链 `PkgConfig::WEBPMUX` → `CMakeLists.txt` |
| **v1.9** | 3 个测试二进制 abort（`0xc0000409` / `P9=FAST_FAIL_FATAL_APP_EXIT`）+ 6 项 stderr/格式断言失败 | 事件日志：三个 abort 签名一致（`ucrtbase.dll` @ `0xa527e`）；根因 = 测试/工具二进制**无 UTF-8 清单** ⇒ 进程 ACP 跟随系统区域（GBK），非 ASCII 语料宽→窄转换失败抛异常 ⇒ `std::terminate`。断言侧：日志文件行尾为 CRLF（`getline` 保留 `\r` 使 `$` 失配）；`capture_stderr` 的 Windows 分支是空存根 | ① 日志 formatter 显式 `eol="\n"`；② `capture_stderr` 补 Windows CRT 实现（`_dup`/`_dup2`/`_close` + 二进制捕获）；③ 全部测试/工具 target 嵌 `photopipeline.manifest`；④ `test_presets` 非法 UTF-8 字节路径用例 **POSIX-only** → `src/core/logger.cpp`、`tests/unit/test_logger.cpp`、`CMakeLists.txt`、`tests/unit/test_presets.cpp` |
| **v2.0** | `--version` 在部分句柄形态下静默（v1.4 判据把 console 型**有效**句柄误判为"要保留"） | 句柄探针（`.cache/handleprobe.cpp`，GUI 子系统链接）：`[inherit] hOut=0 type=0`、`_fileno(stdout)=-2`、pre-attach `ferror=1`；`AttachConsole=1`、post-attach `ferror=0`；console 屏读探针（`.cache/consoleprobe.cpp`）在**target 场景**读到 `SCREEN[0]=PhotoPipeline 0.1.0` | 判据改为**捕获型**（`GetFileType ∈ {FILE_TYPE_PIPE, FILE_TYPE_DISK}`）：捕获型原样保留，其余挂接父控制台并**只**重开未捕获的流；`SetConsoleOutputCP` 仅在自己挂接成功时调用 → `src/main.cpp` |
| **v2.1** | Windows CI runner 的 MSVC 版本与本机（14.51）不同，v1.7 的硬 `#if` 有构建失败风险 | 两种 STL 形态并存，硬编码任一侧都会在另一侧失败 | 新增 `src/core/filetime.h`：`requires` **实例化期**择一（`from_sys` → `from_utc` → `static_assert`）；`metadata.cpp` / `test_metadata.cpp` 改用 `pp::file_time_from_sys` |
| **v2.2（T10-R1）** | Qt 6.8.3 的 `windeployqt --compiler-runtime` **不落 app-local CRT** | 实证：给出 `--compiler-runtime` 后 staging 内出现 `vc_redist.x64.exe`（18,779,992 B）与 `dxcompiler.dll`/`dxil.dll`，**无** `vcruntime140.dll`/`msvcp140.dll` ⇒ 与"解压即用"不符 | 去掉该选项；显式 app-local 部署 `Microsoft.VC*.CRT` **整目录**（10 个 DLL，禁止硬编码 VC 号：本机 VC145 / runner 或 VC143）；剔除 3 个冗余产物（合计 34,736,792 B）→ `tools/make_winzip.py` |
| **v2.2（T10-R2）** | 门禁 A 的硬编码系统白名单永远列不全（漏 `d3d9`/`d3d12`/`uiautomationcore`） | 三者均实测存在于 `%SystemRoot%\System32` | 改为**逐名三判据 + MUST_BUNDLE**（顺序敏感）：a) staging 命中 → OK[staging]；b) `msvcp140`/`vcruntime140`/`concrt140`/`vccorlib140` 前缀 → **FAIL[must-bundle]（必须在系统判据之前**，否则本机装过 VC redist 会掩盖 app-local 缺失）；c) `api-ms-win-*`/`ext-ms-win-*` → OK[apiset]；d) `%SystemRoot%\System32\<name>` 存在 → OK[system]；e) 否则 FAIL[missing] → `tools/make_winzip.py` |

---

## 4. 跨平台语义裁决

| 语义面 | 裁决 | 落地 | 证据 |
|---|---|---|---|
| **全局 UTF-8** | 应用清单 `activeCodePage=UTF-8` + `/utf-8` ⇒ 进程 ACP = UTF-8，与 Linux 的 UTF-8 字节语义对齐 | `cmake/photopipeline.manifest.in` + `CMakeLists.txt` MSVC 块；**所有**测试/工具 target 亦嵌清单 | 3 个 abort（0xc0000409）由此消除；`unicode-png` 金样用例通过 |
| **路径语义** | 路径 = **UTF-8 字节**（不引入宽字符 API 分叉）；仅"打开文件"这一处机制分派 | `src/ui/preset_io.cpp:114-118`（`_wfopen` / `fopen`） | 非 ASCII 路径往返（`test_presets` 的 UTF-8 非 ASCII 段）通过；`unicode-png` PASS |
| **日志行尾** | **日志文件两平台统一 LF**（spdlog formatter 显式 `eol="\n"`） | `src/core/logger.cpp`（`set_formatter(pattern_formatter(kLogPattern, local, "\n"))`） | `test_logger` 的 `format/*` 两项在 Windows 通过（此前因 `\r` 失配） |
| **控制台行尾** | `stdout`/`stderr` 走**平台文本模式**（Windows CRLF，POSIX LF）；脚本匹配一律 `rstrip('\r\n')` | 无代码改动（口径裁决）；`tests/ui_smoke.py:77`、`tests/golden/smoke.py` 匹配器 | `--version` 管道捕获字节 = `b'PhotoPipeline 0.2.0\r\n'` |
| **控制台句柄判据** | v1.4"有效即保留" → **v2.0"捕获型才保留"**：`GetFileType ∈ {PIPE, DISK}` 原样保留；否则挂接父控制台并只重开未捕获的流；CP 仅在自己挂接时改 | `src/main.cpp:800-842` | 句柄矩阵 + console 屏读探针（§5） |
| **POSIX-only 判据清单** | ① `noread` fixture（Windows `fs::perms::none` 仅映射只读属性，读取不受影响，前提不可构造）；② `test_presets` 的**非法 UTF-8 原始字节路径**（Windows 路径为 UTF-16，非法序列不可表示） | `tests/unit/test_presets.cpp`（`#if !defined(_WIN32)` 两处） | 两者仅在 POSIX 生效；合法 UTF-8 非 ASCII 段两平台均保留 |
| **测试侧 POSIX 垫层** | `setenv`/`unsetenv`/`getpid` 经 `tests/unit/env_compat.h` 单实现（`_putenv_s`/`_getpid`）；空值语义按平台分派断言 | `tests/unit/env_compat.h` | 探针 `A(win32-setempty): ptr=null` / `B(crt-setempty): ptr=null` |

---

## 5. 验证证据（本机，0.2.0）

### 5.1 构建与测试

| 项 | 结果 |
|---|---|
| `cmake --build --preset release` | exit **0**（含自动 re-configure；199 边） |
| `ctest --preset release` | **23/23**，`100% tests passed, 0 tests failed out of 23`（2.53 s） |
| `cmake --build --preset release-dev` | exit **0** |
| `ctest --preset release-dev` | **24/24**（8.76 s；`ui_smoke` **6.23 s**） |
| 金样 16 对 | `SMOKE total=16 pass=16 fail=0` |
| `TODO(M3)` | 归零（随 T4） |
| EOL 复原后复验 | 14 个头的行尾复原为 CRLF 后，release 重建 exit 0 + `ctest --preset release` 再取 **23/23**（2.56 s）；`--version` 复取 `PhotoPipeline 0.2.0` |

### 5.2 UI 冒烟冻结行（逐字）

```
UI-SMOKE run status: 已完成：成功 16 · 失败 0 · 跳过 0
UI-SMOKE OK shots=8 pages=3
UI-SMOKE pass
```

### 5.3 `--version` 与句柄矩阵

| 子进程 stdout 形态 | 结果 |
|---|---|
| 管道（`subprocess` capture / ctest） | `PhotoPipeline 0.2.0`，rc 0 |
| 真实文件句柄（重定向，DISK） | `PhotoPipeline 0.2.0`，rc 0 |
| console 型句柄且进程未附加（v2.0 目标场景） | console 屏读探针读回 `SCREEN[0]=PhotoPipeline 0.1.0`（**该探针在 0.1.0 时取证**；机制与版本号无关），rc 0 |
| 本 harness 的"直调"（stdout = NULL） | 写入 console（post-attach `ferror=0`）；harness 只转播自建通道，故日志中不可见（**验证通道属性，非产品缺陷**） |

### 5.4 Windows 打包产物指纹与五项门禁（逐字）

```
make_winzip: 版本=0.2.0 build=…\build\release
make_winzip: staging: photopipeline.exe + 40 个 DLL（applocal 部署产物）
make_winzip: windeployqt: exit 0（39 行输出已折叠）
make_winzip: CRT app-local 部署: 10 个 ← …\VC\Redist\MSVC\14.51.36231\x64\Microsoft.VC145.CRT
make_winzip: 剔除冗余产物: vc_redist.x64.exe（18779992 bytes）
make_winzip: 剔除冗余产物: dxcompiler.dll（14317000 bytes）
make_winzip: 剔除冗余产物: dxil.dll（1509800 bytes）
make_winzip: 补拷 platforms/qoffscreen.dll ← …\plugins\platforms\qoffscreen.dll
make_winzip: 闭包门禁 A: 68 个 PE / 102 个导入名 → staging 51 · apiset 16 · system 35
make_winzip: VC 运行时就位（app-local）: msvcp140/vcruntime140 系列 10 个
make_winzip: 来源门禁 B: 5 个 Qt6*.dll 与 Qt 工具链 sha256 逐一一致（…\Qt\6.8.3\msvc2022_64\bin）
make_winzip: 结构断言 C: photopipeline.exe / platforms/qwindows.dll / platforms/qoffscreen.dll / imageformats/=4 dll / tls/=2 dll 齐备
make_winzip: 许可断言 D（写盘前）: licenses/ = 39 个 copyright（≥30 ✓）+ PhotoPipeline/LICENSE ✓
make_winzip: 烟测 E-1: PhotoPipeline 0.2.0 (exit 0)
make_winzip: 烟测 E-2: UI-SMOKE OK shots=8 pages=3 (exit 0, QT_QPA_PLATFORM=offscreen)
make_winzip: 产物内复核: 解压即用 --version = PhotoPipeline 0.2.0 (exit 0)；licenses/*/copyright = 39（≥30 ✓）
make_winzip: 产物: C:\Data\Code\PhotoPipeline3\dist\PhotoPipeline-0.2.0-win64.zip
             (32782738 bytes, sha256=709904c4405ec621904f5bff5be06b6f676ea27e05b36ddaf8baabd162f01266, 条目 108)
```

**门禁 A 反向自证**（破坏性，均以"输入端制造缺失"实现，判据走产品代码路径）：

| 场景 | 输出（逐字） |
|---|---|
| 缺 `libwebpmux.dll` | `闭包门禁 A 失败: 1 个被导入的 DLL 未通过（staging 50 · apiset 16 · system 35）:` / `  libwebpmux.dll [missing] ← OpenImageIO.dll, photopipeline.exe` |
| 缺 `msvcp140.dll`（**System32 里存在**，仍不得放行） | `  msvcp140.dll [must-bundle] ← Iex-3_4.dll, …（共 37 个导入者）` |
| 恢复后 | 全链通过（见上） |

### 5.5 数据目录落点（便携 vs 回退）

- `%APPDATA%\PhotoPipeline` 在本机全部测试/GUI 运行后**不存在**；exe 旁生成 `logs/`、`presets/`
  （`test_presets`/`test_logger` 的临时目录在 `.cache/` 与 `.pp_test_tmp/`）。
- 代码依据：`src/platform/paths.cpp:131-141`（`data = exe_writable ? exe_dir : platform_data_dir()`；
  `%APPDATA%\PhotoPipeline` 为 Windows 回退，`ensure_layout` 建 `presets`/`logs`）。
- **只读 exe 目录 → 回退分支：已实测通过（W5b，有界且已回滚）**。做法：仓库外临时目录
  （`%TEMP%\pp-ro-test\PhotoPipeline`）拷贝 `dist\PhotoPipeline` 全量 **108** 文件 →
  `icacls <dir> /deny "<user>:(W)"`（rc 0）→ 写探测**按预期被拒**
  （`UnauthorizedAccessException`）→ `QT_QPA_PLATFORM=offscreen` 启动 GUI 8 s（存活后 kill）→
  `--version` 以文件重定向捕获。结果（逐字）：
  ```
  APPDATA\PhotoPipeline exists: True
    C:\Users\gmw\AppData\Roaming\PhotoPipeline\logs (1)
    C:\Users\gmw\AppData\Roaming\PhotoPipeline\presets (1)
    C:\Users\gmw\AppData\Roaming\PhotoPipeline\logs\run-20260923-011332.log (0)
  exe-side logs: False ; presets: False
  version rc=0  out: PhotoPipeline 0.2.0
  ```
  即 exe 目录不可写时数据落 `%APPDATA%\PhotoPipeline`（回退分支成立），exe 旁**不**建 `logs`/`presets`。
  回滚：`icacls /remove:d` → `post-rollback write: OK`（可写恢复）→ 临时目录删除 →
  测试创建的 `%APPDATA%\PhotoPipeline` 清理（测试前不存在，已恢复原状）。**该分支因此从 §8 移出**。

### 5.6 CI

**最终轮（绿）**：run #22 / id `35848888594` / head `f7b0579` / 2026-09-23T10:27:24Z → 10:36:59Z（9.5 min），六 job 全绿：

| job | 结论 | 耗时 | 关键证据（逐字） |
|---|---|---|---|
| linux | success | 2.3 min | `100% tests passed, 0 tests failed out of 23`；`SMOKE total=16 pass=16 fail=0` |
| ui-smoke | success | 2.9 min | `UI-SMOKE OK shots=8 pages=3`（`ctest -R ui_smoke`） |
| appimage | success | 3.4 min | 四项内置烟测 + 产物启动烟测；artifact `PhotoPipeline-AppImage` |
| windows | success | 8.2 min | `100% tests passed out of 23`；`SMOKE total=16 pass=16 fail=0 (out: D:\a\PhotoPipeline3\PhotoPipeline3\.cache\out-smoke)` |
| winzip | success | 9.4 min | 门禁 A/B/C/D 与 E-1/E-2 全过；`UI-SMOKE OK shots=8 pages=3`；指纹 `ZIP_SIZE=31769308` / `ZIP_ENTRIES=108` / `ZIP_SHA256=e89eb6d4430acc1a9982389d2f97b47d24362012a81051316e27b6dbe870ad37`；artifact `PhotoPipeline-win64` |
| cache-gc | success | 0.1 min | 缓存清理（保留最新 8 / 7 天） |

**首轮（红）与根因**：run #21 / head `342f1dc` —— `linux`/`ui-smoke`/`appimage` success（10.8 / 10.6 / 8.9 min），`winzip` **failure**，`windows` **cancelled**（`timeout-minutes: 120` 顶格）。

- `windows`：`configure release` 49.9 min（Windows 首次冷装 41 个 port）→ `build release` 4.0 min → `ctest release` **挂起 66 min**（日志 `Start 13: test_presets` 之后无任何输出；前 12 个测试均 <1 s 通过）。
- `winzip`：`configure` 47.9 min → `build` 4.0 min → release-dev 子集 3.0 min → 打包步 **6 秒失败**：`make_winzip: 无法从 'D:\a\...\build\release\photopipeline.exe --version' 读取版本号（输出: ''）`。
- **同一根因**：Windows 上只有 `photopipeline.exe` 走 applocal 部署，**测试二进制与构建树 exe 取不到 Qt DLL**（此前依赖调用者 PATH）。`pp_test_presets.exe` 是 ctest 序列中**第一个依赖 Qt 的测试**，加载器缺 DLL 的错误对话框在无人值守的 runner 会话中得不到确认 ⇒ 挂死；`make_winzip` 探测构建树 exe 时同样缺 DLL ⇒ 空输出。故两者不是两个缺陷，而是同一机制的两个表现。
- **重新设计（T11b，`ee0a899`）**：① 每个测试 `TIMEOUT 120`（三个 testPreset 的 `execution.timeout` + 逐条 `TIMEOUT` 属性，23/23 全覆盖）；② 三个 Windows job 显式经 `GITHUB_PATH` 注入 `$env:QT_DIR\bin`（根因由此消除）；③ `make_winzip` 版本探测移到 staging 之后（Qt/vcpkg/CRT 就位，零 PATH 依赖），并给全部 6 个子进程注入 `qt_bin`，失败消息带 rc/stdout/stderr；④ Windows job 上限 120 → 60 min；⑤ `test_presets` 增逐用例 stderr 进度（同类挂起的定位手段，①为兜底）。
- **成本对账**：冷装成本已由 #21 的 `vcpkg (save)`（与 Qt save）买断 ⇒ #22 的 Windows `configure` 回落至分钟级；`windows` 8.2 min / `winzip` 9.4 min 即稳态耗时（对照 Linux 2–3 min）。

> 说明：`windows` job 的 UI 覆盖由 `winzip` job 的 E-2（冻结行 `UI-SMOKE OK shots=8 pages=3`）承担，故 windows job 只跑 `ctest release`（23）与金样 16（**不**全量构建 release-dev：Windows 上约多 8–12 min 而无额外判据）。

---

## 6. 铁律六审计：单实现 vs 机制分派点

| 面 | 判定 | 位置（文件:行） |
|---|---|---|
| 平台路径与数据目录 | **机制分派**（平台目录语义不同，不可统一） | `src/platform/paths.cpp:19`、`:50`、`:101`（`%APPDATA%` vs `$XDG_DATA_HOME`/`$HOME`） |
| Mica / 深色标题栏 | **机制分派**（非 Windows 为 no-op，内聚单文件） | `src/platform/mica.cpp:6`、`:23` |
| 内存探测（像素预算） | **机制分派**（Win32 API vs POSIX） | `src/core/pixelbudget.cpp:15`、`:28` |
| `system_clock → file_time_type` | **单实现 + 编译期探测**（`requires` 择一，无平台宏） | `src/core/filetime.h:23-25`；调用点 `src/core/metadata.cpp`、`tests/unit/test_metadata.cpp` |
| 日志时间戳 | **机制分派**（`localtime_s` vs `localtime_r`） | `src/core/logger.cpp:147-151` |
| 日志行尾 | **单实现**（显式 `eol="\n"`，两平台同一 formatter） | `src/core/logger.cpp`（`set_formatter`） |
| 控制台输出守卫 | **机制分派**（`#ifdef _WIN32` 内聚；判据本身平台无关） | `src/main.cpp:40`（`windows.h`）、`:801`（`std_handle_is_captured`）、`:817`（守卫块） |
| 文件打开方式 | **机制分派**（`_wfopen` vs `fopen`），语义目标单一（UTF-8 路径） | `src/ui/preset_io.cpp:114-118` |
| 测试侧 POSIX 环境 API | **单实现垫层**（`env_compat.h` 内 `#if defined(_WIN32)`） | `tests/unit/env_compat.h` |
| 打包后端 | **机制分派**（两后端，共享逻辑 `collect_licenses.py`） | `tools/make_appimage.sh`（冻结）+ `tools/make_winzip.py`（新）；`collect_licenses.py` 共用 `--dest` |
| 构建系统 | **机制分派**（MSVC 块 / UTF-8 清单 / 平台预设条件） | `CMakeLists.txt`（MSVC 块、`target_sources(... manifest)`）、`CMakePresets.json`（self-contained toolchain、平台 default） |
| overlay ports | **加性分派**（`if(VCPKG_TARGET_IS_WINDOWS)`，Linux 路径字节不变） | `vcpkg-overlay/libjpeg-turbo/*`、`vcpkg-overlay/x265/portfile.cmake`、`vcpkg.json` |
| CI | **机制分派**（linux/ui-smoke/appimage vs windows/winzip；缓存键前缀分离 `vcpkg-` / `vcpkgwin-`） | `.github/workflows/build-test.yml`、`warm-cache.yml`、`release.yml` |

**结论**：全仓平台差异集中在 12 个**机制分派点**（均为小范围内聚的 `#if`/平台预设/independent backend），
业务逻辑与冻结契约保持**单实现**；v2.1 起连"STL 差异"也改为**编译期探测**而非平台宏。

---

## 7. 偏差与例外

### 7.1 规格与执行的偏差（本报告可证）

| # | 偏差 | 原文/依据 | 处置 |
|---|---|---|---|
| 1 | **步骤 7 目标纠偏**：任务书步骤写 `smoke.py build/release`，实际必须 `build/release-dev` | `tests/golden/smoke.py:80-85` 原文 `a plain release build has no --dev`；`.github/workflows/build-test.yml:180` 用 `build/release-dev` | 以 `release-dev` 为正式目标；`build/release` 下 16 例 `--dev exit 2` 的原文已留档 |
| 2 | **自证机制**：T10 的"移走 staging 内 X 再重跑"不可字面执行 | `make_winzip.py` 每次运行 `rmtree` 重建 staging（幂等设计） | 改为在**输入端**（windeployqt 包装器删 staging 内 DLL / `PP_VCREDIST_DIR` 指向缺件 CRT）制造同一条件；判据仍走产品代码路径；**不加**"不重建 staging"开关（用户裁定接受） |
| 3 | `windows` job 不含 `ctest --preset release-dev` | 规格步骤表只列 `ctest release` + 金样；release-dev 仅建子集 ⟹ 全量 ctest 会因缺测试二进制失败 | 头注释已按用户裁定改为准确表述；Windows 侧 ui_smoke 覆盖由 `winzip` job 的 E-2 承担 |
| 4 | 纯注释更新 3 处（超出逐字清单） | 拓扑表 / cache-gc 计数 / warm-cache 头部 | 用户已**追认**（"注释失真才是问题"） |
| 5 | **SPDX 插入脚本误改行尾（自查已修）** | T13 的首版脚本以文本模式读写 14 个头，把 CRLF 统一成了 LF（`git status` 随即对 14 个文件报 `LF will be replaced by CRLF`，而改动前无此告警）——属"只补标注"之外的意外副作用 | 立即用 `.cache/restore_eol.py` 逐文件复原 CRLF（字节数增量 = 行数 × 1），复核：`git status` 告警消失、`git diff --stat` 仍为每文件 **+1 行**；随后**重建 release + 重跑 ctest 23/23**（§5.1 末行）。四要素（14 文件 / git 告警发现 / restore_eol.py 复原 / 重建复验 23/23）齐备 |
| 6 | **W2 偏差 3 处（全部批准）** | ① `src/platform/paths.h` 的 include/声明行合并；② `src/main.cpp` 增加 `#include "platform/mica.h"`（Mica 接线经 `main` 而非 `mainwindow`）；③ `CMakeLists.txt` 的 NOMINMAX 注释位置调整 | 均属机械性偏差，W2 收口时经主对话**批准**；不改语义 |
| 7 | **W3 偏差 8 处** | ⑥ 保留 `CC`/`CXX` 未移植进 `env.py`（判定 **keep-as-is**）；⑦ `env.py` 的环境变量无条件赋值（判定 **keep-as-is**）；⑧ `.pyc` 清理（判定 **acknowledged**，无害） | ①–⑤ 当时裁定为"设计不变的机械性偏差，**批准**"，**留存记录未逐条列名**（本报告照此表述，不编造条目内容）；⑥⑦⑧ 如上 |
| 8 | **D3"六件"与实删 7 个 bash 文件的调和** | D3 列的六件 = bootstrap / gen_corpus / smoke / ui_smoke / regression / collect_licenses | 第 7 个删除项是 `tools/env.sh.example`（**模板示例**，其职责被 `env.py` 的生成/注入逻辑取代，不计入 D3 的"六件脚本"）；两者不矛盾 |
| 9 | **仓库根意外文件 `8s`（自查已删）** | W5b 首版只读回退测试脚本因 PowerShell 变量插值 + 控制台编码问题被误解析，`Start-Sleep -Seconds 8` 一类片段把脚本正文写进了仓库根的文件 `8s`（3018 B，`git status` 报 `?? 8s`） | 用 ASCII-only 重写脚本后复核并 `Remove-Item 8s`；`git status` 已无该条目。教训：跨平台/中文脚本一律 ASCII 输出 + `${env:VAR}` 花括号形式 |
| 10 | **推送时机取消在飞冷构建（流程，非代码）** | 第二次推送（`342f1dc`）触发 `build-test.yml` 的 `concurrency: cancel-in-progress: true`，把 run #20（`5210ee2`）**取消**——那正是一轮 Windows 冷构建，其缓存播种随之丢失，直接导致 run #21 仍需冷装（49.9 min） | **处置**：① 缓存转热后取消代价可忽略（稳态 8–9 min），保留 `cancel-in-progress`；② 纪律：在昂贵冷构建在飞期间不得推送，先等其结束（本轮已按此执行） |

### 7.2 无法在库内证实的项（**不圆场**）

- **W2 偏差 3 处 / W3 偏差 8 处**：已按用户提供的原文补入 §7.1 第 6/7 行（W3 的 ①–⑤ 留存记录
  未逐条列名，照实表述为"设计不变的机械性偏差，批准"）。D3"六件"与实删 7 个 bash 文件的调和见
  §7.1 第 8 行（第 7 项 = `tools/env.sh.example` 模板示例）。
- **`docs/m3-tasks.md` "W3 六脚本" vs 实际删除数**：任务书 D3 列六件（bootstrap / gen_corpus /
  smoke / ui_smoke / regression / collect_licenses），`git status` 显示删除的 bash 文件为
  `tools/bootstrap.sh`、`tools/collect_licenses.sh`、`tools/env.sh.example`、`tools/gen_corpus.sh`、
  `tests/golden/smoke.sh`、`tests/ui_smoke.sh`、`tools/regression.sh`（7 个）——调和见 §7.1 第 8 行
  （第 7 项是模板示例，不计入 D3 的六件）。
- **README 失真：已在 W5b 修复**（用户解除"只加不改"限制后）。**共 17 处编辑、覆盖约 20 个引用点**：
  - 陈旧引用（指向 W3 已删除的 bash 文件）→ Python 单实现入口：
    `python tools/bootstrap.py`、`python tools/gen_corpus.py`、`python tools/regression.py`、
    `python tests/golden/smoke.py`、`python tests/ui_smoke.py`、`tools/collect_licenses.py`；
    其中 4 处 `source tools/env.sh` 按实际改写为 `python tools/env.py run -- …`（`env.sh` **不再生成**、
    `env.sh.example` 已删，仅保留 2 处**说明性**提及）；`tools/ci-*.sh` 与 `tools/make_appimage.sh`
    **仍在库**，引用不动。
  - **逐行分类表（`0.1.0` 语境）**：

    | 行（W5b 前） | 原文本摘要 | 类别 | 处置 |
    |---|---|---|---|
    | 184 | `# 产物: <OUT_DIR>/PhotoPipeline-0.1.0-x86_64.AppImage` | 指导性 | 改 `<版本>` |
    | 197 | 烟测 ① `--version` 输出 `PhotoPipeline 0.1.0` | 指导性 | 改 `<版本>` |
    | 241 | Release 指针（`…/tag/v0.1.0`，无本行度量） | 指导性 | 改为 Releases 首页 + `<版本>` |
    | 247–252 | 下载/`chmod`/运行/`--version` 示例 | 指导性 | 改 `<版本>` |
    | 255–256 | 取件优先级 + **50,940,408 B** / sha256 `31f75c48…` | 历史/实测 | **原文保留** |
    | 261 | FUSE 回退示例命令 | 指导性 | 改 `<版本>` |
    | 271 | `--appimage-extract` 便携示例 | 指导性 | 改 `<版本>` |
    | 280 | `（v0.1.0 = CI 构建…）`（额外发现） | 指导性 | 改为"= CI 构建产物" |
    | 303–305 | `deps.txt` + **36 ELF / 40 条 / 46 条** soname | 历史/实测 | **原文保留** |
    | 188 / 316 | `tools/collect_licenses.sh`（额外发现） | 陈旧引用 | 改 `.py`；`40 个 port` 补平台分述（AppImage 40 / Windows zip 39） |
    | 10–20 / 31 / 74 / 93–123 / 154–157 / 174 / 208–210 / 227–231 / 238 / 402 | `bootstrap.sh`/`env.sh`/`gen_corpus.sh`/`smoke.sh`/`ui_smoke.sh`/`regression.sh`（额外发现） | 陈旧引用 | 全部改为 `.py` / `env.py` 入口 |
- **其它仍含 `0.1.0` 字面量处**（均非版本单源，故未改）：`tools/baseline/golden.log`（Linux 回归基线，
  含 `version=0.1.0`）、`tools/appimage-gui-smoke.sh:18`（默认产物名）、`tools/tls_probe.cpp:33`
  （User-Agent 字符串）、`CHANGELOG.md` 的 0.1.0 历史条目、`docs/m0|m1|m2-*`（历史文档）。
- **版本单源确认**：唯一功能定义点 = `CMakeLists.txt:2`；`src/core/logger.cpp:59`
  `constexpr const char* kAppVersion = PP_VERSION_STRING;`（生成头），无第二处硬编码。

---

## 8. 遗留与 M4 候选

| # | 事项 | 现状与影响 | 建议 |
|---|---|---|---|
| 1 | **Windows 回归基线 `golden.windows.log`** | 未建立；`tools/regression.py` 未在 Windows 接入（其基线 `tools/baseline/golden.log` 为 Linux 侧且含 `version=0.1.0`） | M4：生成 Windows 基线 + 加 `pp_verify`/规范化口径，或明确"Windows 仅断言级金样" |
| 2 | **zip 非字节可复现** | 条目内嵌文件 mtime ⇒ 同源两轮 sha256 不同（0.2.0 三轮实测：`a647b74d…`/`afa7eb44…`/`709904c4…`，同为 32,782,738 B / 108 条目） | 已接受（同 M2-T27 AppImage 结论）；如需要，可在 zip 写入时固定 `date_time`（需裁定） |
| 3 | **非 ASCII 交互控制台代码页** | 未强制 `SetConsoleOutputCP(UTF-8)`（v2.0 刻意只在**自己** AttachConsole 时改）；中文控制台下直显可能乱码 | M4 候选：提供 `--utf8-console` 或仅在检测到自身新建控制台时设置 |
| 4 | ~~**便携回退路径未实测**~~ → **W5b 已实测通过（移出候选）** | exe 目录只读 → `%APPDATA%\PhotoPipeline` 回退分支 | 证据见 §5.5：`icacls /deny` 构造只读 → GUI 8 s + `--version` rc 0 → `%APPDATA%\PhotoPipeline\{logs,presets}` 新建、exe 旁不建；已回滚（可写恢复 + 临时目录与测试产生的 APPDATA 目录清理）。可选 M4 增量：把该流程固化为一条 CI/本地自动化 |
| 5 | ~~**README 失真行**~~ → **W5b/W5c/W5d 已修** | README 的 bash 脚本引用、指导性 `0.1.0` 语境均已订正；脚本内指示串亦已清扫 | 剩余**有意保留**项见第 6/9/10 行（历史说明、冻结基线、`.gitignore`） |
| 6 | **`collect_licenses.py` 历史 `.sh` 提及** | `:13`（与 make_appimage.sh 头注释一致）、`:30`（"本文件替代 tools/collect_licenses.sh"）—— 均为历史说明，非失效调用 | 保留（W5c 分类 (b)）；如要求"零 `.sh` 残留"再清理 |
| 7 | **发布取件仍由 release.yml + 人工确认** | tag 推送 → 双产物 artifact → `publish` job；tag 已存在同名 release 时失败（设计如此） | 观察首轮 tag 运行 |
| 8 | **M2 遗留转入 M4**：HDR/PQ 输出色彩、gray 26/255 舍入事实、内省参数中文标签 | 见 `docs/m2-report.md` §9/§10 | M4 候选池 |
| 9 | **回归基线头部两行仍引用 `tools/regression.sh`（冻结，M3 保留）** | `tools/regression.py:185/:189` 属 `HEADER`，写入 `current.log` 后与 `tools/baseline/golden.log` 整体 diff；该基线 `:9/:13` 逐字含这两行（`regression.py:73` 自述即为此设计） | **M4 处置（用户裁定原文）**：修需在 Linux 上 `python tools/regression.py <build> --update` 重生成基线后，同步改 `tools/regression.py` 的 `HEADER` 两行；**当前保留以保证与冻结基线逐字可比**（改字符串会把 `zero diff` 变成 `DIFF FOUND` = 行为变化） |
| 10 | **`.gitignore:6` 的 `tools/env.sh` 条目** | `env.sh` 已不再生成（W3），该忽略条目成历史遗留；但它**不是**"指示运行已删除脚本"，且保留可防止"本地按旧文档重建 env.sh 后误提交" | 建议保留（W5d 分类：非 (a)/(b)/(c)，属陈旧但无害）；如要求彻底清理需你确认（删除会让将来本地重建的 `env.sh` 进入 `git status`） |

---

## 附：本报告的证据文件与复现命令

```powershell
# 版本与构建
python tools\env.py run -- cmake --preset release
python tools\env.py run -- cmake --build --preset release
python tools\env.py run -- ctest --preset release                       # 23/23
python tools\env.py run -- build\release\photopipeline.exe --version    # PhotoPipeline 0.2.0

# 金样与 UI 冒烟（需 release-dev）
python tools\env.py run -- cmake --preset release-dev
python tools\env.py run -- cmake --build --preset release-dev
python tools\env.py run -- ctest --preset release-dev                    # 24/24（含 ui_smoke）
python tools\env.py run -- python tests\golden\smoke.py build\release-dev

# Windows 打包（五项门禁 + 产物内复核）
python tools\env.py run -- python tools\make_winzip.py dist --smoke-exe build\release-dev\photopipeline.exe
```

探针与审计脚本（均在 `.cache/`，可删）：`envprobe.cpp`（环境变量空值语义）、`cp1-4.cpp`（file_clock
四形态）、`handleprobe.cpp`（句柄判定日志）、`consoleprobe.cpp`（console 屏读）、`runver.py` /
`runprobe.py`（矩阵驱动）、`spdx_audit.py`（SPDX 覆盖审计）、`verify_workflows.py`（workflow 解析与
缓存键清单）、`wdq-wrapper.cmd` / `vcredist-scratch/`（门禁反向自证）。
