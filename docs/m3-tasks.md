# M3 任务书 — Windows bring-up + 跨平台化（v1.0，冻结执行依据）

> 执行依据：主对话 2026-10-14 用户四项裁定（§3.7 裁定记录）+ 本文 §4 波次计划。
> 基线：M2 收口 `0e700f3`（v0.1.0 发行之后）；分支 main。
> 上游文档：`docs/design.md` §8.1/§8.3/§9.1（M3 定义与出口准则）、`docs/m2-tasks.md` §9.7（M3 候选清单）。
> 五条铁律沿用（禁降级 / float32 / 全参数 / 小而美 / 无额外处理）；本里程碑追加第六条：

**铁律六（单实现跨平台）**：共享逻辑只有一份代码、一份脚本；平台分派只允许出现在
机制必然不同处——C++ 的 `src/platform/`（同文件内 `#ifdef` 内聚，不分叉文件）、打包
后端（AppImage / windeployqt+zip）、构建系统的平台分派点。禁止"Linux 一套、Windows
一套"的平行实现。

## §0 目标与范围

**目标**：在 Windows 实机完成 M3 全部开发任务（design §9.1）——MSVC 工具链 + vcpkg
（同 baseline）+ aqt Qt 6.8 + Windows CI 矩阵 + windeployqt/zip 打包 + Mica/深色标题栏
实装 + Win11 实机冒烟，且 **Linux 侧零破坏**（判定权 = 推送后 CI 既有 linux/ui-smoke/
appimage/cache-gc 四 job 保持全绿）。

**范围外**：macOS；HDR（PQ/HLG）tone mapping 与 gray 26/255 归因（m2 候选清单在案，
属色彩专项非 bring-up）；内省参数中文标签（同上）。

## §1 现状盘点（Windows 实机实测，2026-10-14）

| 项 | 状态 |
|---|---|
| MSVC | VS Build Tools 2026（工具集 14.51）`cl.exe` 在位；不在 PATH（构建需 vcvars 注入） |
| CMake/Ninja | VS 自带 CMake 4.2.3-msvc3 + Ninja 1.13.2；不在 PATH |
| Qt 6.8.3 | 已装：`.toolchain/Qt/6.8.3/msvc2022_64`（aqt，与设计 §8.1 同源方案） |
| vcpkg | tag `2026.07.29`（= versions.env = vcpkg.json baseline）已 bootstrap；binary cache 空 |
| Python | 系统 3.14.5 + `.toolchain/venv`（aqtinstall）；`make_icon.py` 已是 Python |
| ccache | 无（CMakeLists 探测已条件化排除 MSVC，回退无缓存 ✓） |
| git | 工作树干净 @ `0e700f3`；origin = github.com/zhang-hz/PhotoPipeline3 |
| 机器 | 16 逻辑核 / 空闲盘 ~96 GB |
| Git Bash | 5.3.9 在位但 **`bash` 不在 PATH**（`Git\bin` 默认不进 PATH）；WSL 未装 |
| CI | `build-test.yml` 四 job + `warm-cache.yml` 全 Linux |

## §2 Gap 分析（分层，附证据锚点）

### A. 依赖层（最大风险，前置；BB3 兑现点）

1. **4 个 overlay port 全部未经 Windows 验证**，两处硬性 Linux 假设：
   - `vcpkg-overlay/libjpeg-turbo/portfile.cmake:119-129` 硬编码 `lib/libjpegli-static.a`；
     `libjpeg-turbo-config.cmake:15-47` 硬编码 `libjpeg.so`/`libjpegli.a`/`libhwy.a`——
     MSVC 产物名为 `jpegli-static.lib`/`jpeg62.lib(.dll)`/`hwy.lib`，不改必失败；
   - `vcpkg-overlay/x265/portfile.cmake:59-64`：multilib（8+10+12bit）仅 static 生效，
     dynamic 直接 OFF → 8-bit-only DLL；HEIF 默认位深 10，能力集必缩水。
     已核实上游 x265 4.2 `source/CMakeLists.txt`：`x265-shared` 支持
     `target_link_libraries(x265-shared ${EXTRA_LIB})`——动态 multilib 是上游路径；
   - libheif overlay patch 基本跨平台（portfile 已 `vcpkg_find_acquire_program(PKGCONFIG)`）；
   - openimageio overlay：`FindJXL` 静态闭包 patch（`oiio-findjxl-static-deps.cmake`）
     在动态链下需复核。
2. **本项目 `CMakeLists.txt:72-76` 的 `pkg_check_modules`**（lcms2/libjxl/libwebp/tiff4）
   依赖 pkg-config 可执行文件。已核实 vcpkg 工具链（2026.07.29，`vcpkg.cmake:585-600`）
   把 `vcpkg_installed/<triplet>/tools/*` 注入 `CMAKE_PROGRAM_PATH` → vcpkg.json 加
   `{"name":"pkgconf","platform":"windows"}` 即可命中；Linux 继续用系统 pkg-config。
3. triplet 选型 → §3 D1（已裁定 x64-windows 动态）。

### B. C++ 源码层（平台耦合面实测极小）

| # | 位置 | 缺口 |
|---|---|---|
| 1 | `src/platform/paths.cpp:14`（TODO(M3) #21） | `/proc/self/exe`+XDG → Windows 分支（`GetModuleFileNameW` + `%APPDATA%`，design §8.5） |
| 2 | `src/core/pixelbudget.cpp:24`（TODO(M3) #15） | `/proc/meminfo`/sysconf → `GlobalMemoryStatusEx` |
| 3 | `src/main.cpp:709` | `executable_directory()` 依赖 `/proc/self/exe` |
| 4 | `src/platform/mica.cpp` | design §1.1/§6.6 规划文件从未创建（Mica backdrop + DWM 深色标题栏，M1b 只做 Linux） |

已自愈无需动：`logger.cpp` tid 回退/`localtime_s` 分支、`metadata.cpp:754` weak symbol
MSVC 守卫、`main.cpp:843` Fluent style 探测（Windows 命中）。

**编码雷区（比上表更关键）**：代码全线按"路径 = UTF-8 字节"的 Linux 语义写
（`preset_io.cpp` M2-T14 路径层、`metadata.cpp` `file.string()`、dev harness `char** argv`、
Exiv2/OIIO/spdlog 窄接口）。Windows 默认 ACP=GBK → 中文/emoji 路径必炸（金样含
`测试📸unicode.png`，16 对断言用例之一）。解法 = §3 D2 全局 UTF-8，否则每个边界
wide↔narrow 分叉 = 违反铁律六。

**控制台**：单二进制服务 GUI 与 `--version`/`--dev`/`--ui-smoke` 双职能 → §3 D5。

### C. 脚本层（铁律六的主战场）

7 个 bash 脚本承载双平台共享逻辑：`bootstrap.sh` / `env.sh(.example)` / `gen_corpus.sh` /
`tests/golden/smoke.sh` / `tests/ui_smoke.sh` / `regression.sh` / `collect_licenses.sh`；
其中 `ui_smoke.sh` 被 `CMakeLists.txt:138` 硬编码进 ctest（`bash tests/ui_smoke.sh`）。
机制绑定 Linux 可保留 bash：`make_appimage.sh`（冻结）/ `appimage-*.sh` /
`ci-install-deps.sh` / `ci-check-deps.sh`（ELF soname 专用）/ `ci-cache-gc.sh`（ubuntu runner）。
Git Bash 备选方案已评估并否决（裁定记录 §3.7-D3：实测 `bash` 不在 PATH、WSL 解析歧义、
MSYS 路径转换/vcvars 特调集合、双运行时语义测试矩阵——分叉下移到语义级，更隐蔽）。

### D. 构建系统层

- `CMakePresets.json`：toolchainFile 取 `$env{VCPKG_ROOT}`、QT_DIR 取 env、dev/tsan 为
  GCC 专属 flags——需自包含 + 平台条件（presets `condition`）；
- `CMakeLists.txt`：警告块仅 GNU/Clang；无 `/utf-8`/`WIN32_EXECUTABLE`/manifest 嵌入/
  `dwmapi` 链接；ui_smoke 注册硬编码 bash。

### E. CI / 发行层

- 无 windows job、warm-cache 无 windows 播种；
- Windows 打包链路（windeployqt + zip + DLL 闭包自检 + 许可随附 + 启动烟测）不存在；
- v0.1.0 Release 人工取件（m2 候选 #11）；tag 双产物自动化 = 裁定 S2 范围；
- M2 遗留归 M3：14 个 `PP-FROZEN` 头 SPDX 补标（m2 裁定 #4）、TODO(M3) ×2（= B1/B2）。

## §3 方案决策（D1–D6；D1–D3+S1/S2 为用户裁定）

**D1（裁定）Windows triplet = `x64-windows`（动态）**。依据：① LGPL 干净——libheif 以
DLL 随 zip 分发，不依赖"源码 offer"裁定 #3 向 Windows 扩展；② x265 动态 multilib 为
上游支持路径（EXTRA_LIB→shared），免 COFF 合档手术；③ 与 Linux 发行形态对称
（AppImage 带 Qt .so / zip 带 Qt+图像库 DLL）；④ Qt（aqt）本就动态 /MD，无 CRT 混链。
R8 残余（Windows 打包段）就此闭合。

**D2（裁定）Windows 全局 UTF-8**：应用清单 `activeCodePage=UTF-8` + `/utf-8` 编译 →
argv/getenv/fopen/`path::string()` 全部 UTF-8，现有"路径=UTF-8 字节"代码零分叉。
代价：最低 Windows 10 1903+（写入系统要求；目标 Win11 25H2 无实际影响）。

**D3（裁定）共享脚本 → Python 单实现，删除 bash 版**：六件（bootstrap / gen_corpus /
smoke / ui_smoke / regression / collect_licenses）移植为 Python（stdlib-only），冻结输出行
（`SMOKE total=16 pass=16 fail=0`、`UI-SMOKE OK shots=8 pages=3`）与退出码逐字节保持；
CI 与本地同入口。Git Bash 备选已分析否决（§2.C）。

**D4 打包 = 共享逻辑单实现 + 机制后端分派**：`collect_licenses.py` 单实现（两后端共用，
`make_appimage.sh` 改调它，由 CI appimage job 验证零回归）；`make_appimage.sh` 冻结不动
（Linux 后端）；新增 `tools/make_winzip.py`（Windows 后端：windeployqt + DLL 闭包硬门禁 +
许可随附 + `--version`/offscreen 启动烟测 + 指纹输出）。烟测口径与 AppImage 四项对齐。

**D5 GUI 子系统 = `/SUBSYSTEM:WINDOWS` + `AttachConsole(ATTACH_PARENT_PROCESS)`**：
双击无黑框；终端/CI 跑 `--version`/`--dev`/`--ui-smoke` 时挂接父控制台，输出可捕获。

**D6 Mica / 深色标题栏 = `src/platform/mica.cpp`**（design §1.1/§6.6）：`DwmSetWindowAttribute
(DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_MAINWINDOW)` + `DWMWA_USE_IMMERSIVE_DARK_MODE`
随系统主题；非 Windows 编译为 no-op（同文件 `#ifdef _WIN32` 内聚）。深浅色其余部分由
Qt 6.8 Fluent 风格自带。

**S1/S2（裁定）CI + tag 发行自动化**：`build-test.yml` 增 `windows` job（release+release-dev
+ ctest + 金样 16 + ui_smoke）与 `winzip` job（打包+烟测+artifact）；`warm-cache.yml` 增
windows 播种；新增 tag 触发的 `release.yml`（AppImage + win64.zip 双产物 + gh release），
一并销 m2 候选 #11（发行自动化）与 #12（push 重复触发去重——tag 工作流拆分解决）。

### §3.7 裁定记录（2026-10-14，主对话 ask_user_question）

| # | 问题 | 裁定 |
|---|---|---|
| D1 | Windows triplet | **x64-windows 动态**（vs 备选 x64-windows-static-md） |
| D2 | 全局 UTF-8（manifest+/utf-8） | **接受**（最低 Win10 1903+ 写入系统要求） |
| D3 | 共享脚本方案 | 先问"Git Bash 可行性"→ 补实测分析（bash 不在 PATH/WSL 歧义/MSYS 特调/双语义矩阵）→ **Python 单实现** |
| S1/S2 | CI 与发行范围 | **CI + tag 发行自动化**（M3 出口准则完整达成） |

## §4 波次计划与出口准则

### W1 依赖 bring-up（最长路径，最先启动）
- T1 overlay Windows 适配：jpegli 产物名平台化（portfile + config.cmake）、x265 multilib
  扩展到 dynamic（EXTRA_LIB→shared，深度档库名平台化）、vcpkg.json 加 platform 限定
  pkgconf。**纪律：全部为 `if(VCPKG_TARGET_IS_WINDOWS)` 加性分支，Linux 路径字节不变**。
- T2 `vcpkg install`（manifest，x64-windows）后台长任务；port 失败逐个修（BB3 迭代）。
- **出口**：install 成功；`pp_linkprobe` 9/9；运行期复核 `heif/x265 = 8,10,12`、
  `avif/svt-av1 = 8,10`、`avif/libaom = 8,10,12`。

### W2 构建系统 + 源码
- T3 presets 自包含（toolchainFile→`${sourceDir}/vcpkg/...`、QT_DIR 平台默认、dev/tsan
  加 Linux condition）+ CMakeLists MSVC 块（/utf-8、WIN32_EXECUTABLE、manifest 嵌入、
  dwmapi、ui_smoke→Python、警告块、`find_package(Python3)`）；
- T4 `paths.cpp`/`pixelbudget.cpp`/`main.cpp` Windows 分支（清 TODO(M3) ×2）；
- T5 `mica.cpp` + mainwindow 接线；
- T6 `test_paths.cpp` 等测试 Windows 断言分支（%APPDATA% 回退、编码断言按 D2 语义）。
- **出口**：configure/build 通过；ctest 全绿（依赖 W1）。

### W3 脚本移植
- T7 `tools/bootstrap.py`（aqt arch 分派、vcpkg、env 同源生成：`env.sh` + `env.ps1`，
  vcvars 自动注入）；
- T8 六脚本移植 + CMakeLists/CI 调用点更新 + **删 bash 版**；
- **出口**：两平台同入口；冻结行逐字节；Linux CI 切换调用点后仍绿。

### W4 验证 + 打包 + CI
- T9 Windows 本地全绿：ctest 24 + 金样 16（含 unicode 用例）+ ui-smoke 8 截图 +
  `--version` + 便携/回退目录行为；
- T10 `tools/make_winzip.py` + 四项烟测（闭包/结构/许可/启动，口径对齐 AppImage）；
- T11 CI：`windows` + `winzip` job（缓存 key：qt-6.8.3-win64_msvc202264-os-arch、
  vcpkg 滚动 key 沿用含 github.job）、warm-cache windows、`release.yml`（tag 触发双产物）；
  **推送后 Linux 四 job 必须全绿**。
- **出口**：design §9.1 M3 出口准则——Win CI 全绿；干净 Win11 25H2 zip 解压即用。

### W5 收口
- T12 README（Windows 下载/系统要求 Win10 1903+/便携模式 `%APPDATA%` 回退/许可）+
  CHANGELOG 0.2.0 + `docs/m3-report.md`；
- T13 14 个冻结头 SPDX 补标（m2 裁定 #4）。
- **出口**：双平台 release tag 产物齐（S2 自动化）；`TODO(M3)` 归零；文档收口。

## §5 风险登记（新增；R1–R19 见 design §10）

| # | 风险 | 缓解 |
|---|---|---|
| R20 | MSVC 编 jpegli/OIIO 失败（BB3 兑现） | W1 前置迭代；jpegli 属 libjxl 家族（MSVC 官方 CI 覆盖）、OIIO 为 vcpkg 常规 Windows port |
| R21 | x265 动态 multilib 未实测 | 上游代码已核实 EXTRA_LIB→shared；失败回退 D1 备选 static-md（需重新裁定） |
| R22 | UTF-8 ACP 在 <Win10 1903 失效 | 系统要求写明 1903+；实测 Win11 25H2 |
| R23 | Exiv2/OIIO/spdlog 窄字符路径 | D2 下理论全通；unicode 金样用例实证 |
| R24 | 脚本移植引入 Linux 回归 | 冻结行逐字节断言 + CI linux/ui-smoke job 把关；overlay/preset 改动全为加性分支 |
| R25 | CI runner VS 版本 vs 本机 VS2026 | MSVC ABI 自 2015 稳定；Qt msvc2022_64 两边可用；runner 选型 T11 定（windows-2022/2025） |

## §6 验证矩阵（双平台）

| 验证项 | Windows（本机+CI） | Linux（CI，判定"零破坏"） |
|---|---|---|
| vcpkg install | x64-windows 全 40 port | x64-linux（既有缓存，restore-keys 兜底） |
| build release/release-dev | MSVC+Ninja | gcc+Ninja（不变） |
| ctest 24 条 | 全绿 | 全绿（现有基线） |
| 金样 16 对 | smoke.py 同入口 | smoke.py 同入口（冻结行逐字节） |
| ui-smoke | offscreen 8 截图 | offscreen 8 截图（现有） |
| linkprobe | 9/9 + 位深能力集 | 9/9（现有） |
| 回归基线 | 生成态（golden.windows.log 可选） | diff tools/baseline/golden.log（现有） |
| 打包 | winzip 四项烟测 | AppImage 四项烟测（现有） |
| 产物 | PhotoPipeline-win64.zip | AppImage（不变） |

## §7 纪律条款（本里程碑）

1. **裁定先行**：方案级决策必须经主对话用户裁定后落盘执行（本次教训：不得先斩后奏）。
2. **加性分支**：overlay portfile / CMakeLists / presets 的全部改动以平台条件分支内聚，
   Linux 路径字节不变；每波次收口时 `git diff` 复核。
3. **鼓点纪律沿用**：每个模块落地即在 Windows 跑对应验证（ctest 分组/金样子集），
   不允许大批量"首次通电"。
4. **冻结语义**：`--dev`/`--ui-smoke`/smoke 的冻结输出行与退出码在移植后逐字节一致；
   `PP-FROZEN` 头只补 SPDX（T13），不动契约。
5. **TODO 标签**：`TODO(M3)` 两项（paths/pixelbudget）随 T4 清零；新增糙点打 `TODO(M4)`。
6. **提交纪律**：pathspec 提交、追加式（不 amend/rebase）；每波次收口提交对应
   任务号前缀（M3-T1、M3-T2…）。
