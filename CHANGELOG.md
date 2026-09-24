# 变更日志

## [0.3.0] — 2026-09-24

**多格式批处理工作台**：在 0.2.0（Linux + Windows 双平台）基础上完成 M4 的九项需求——
输入预览/分类/勾选、生效值元数据、多格式输出、逐文件进度、自适应并行、GUI 重构（无边框三栏）、
CI 工业化（快慢道四流水线）、依赖一把全升、AVX2 性能基线。工具链 = MSVC（VS 2022/2026 Build Tools）
+ vcpkg（`x64-windows-avx2` 自定义 triplet，继承 `x64-windows` 动态链接形态）+ **Qt 6.11.2**
（`msvc2022_64`）；ISA 基线 = **AVX2 / x86-64-v3**（调优目标 i7-14700K）。

### 新增

**预览、分类与勾选（需求 1）**

- **常驻输入预览面板**（中栏）：点选即出图（`decode_preview(path, 2048)`，内嵌预览优先、无内嵌
  则全解码后 LANCZOS3 降采样），独立低并发后台池（≤2） + 16 张 LRU；缩放「适应 | 1:1」、
  ←/→ 翻图、徽标「输入 · 未修改像素」；**只预览输入，不预览输出**（负面清单松绑范围）
- **手动分类**：`ClassRegistry` 命名分类（颜色点 + 热键 0–9）、计数、预览底部热键提示动态生成；
  持久化 `data_dir()/classes.json`（键 = 规范化绝对路径；Windows 大小写折叠）
- **自动分组视图**：QTreeView 节头（月份 / 相机型号 / 源格式 + 无分组），节头带计数
- **文件列表勾选**：`CheckState` role ⇒ 勾选 = 参与运行 = 批量规则作用范围；搜索过滤与勾选正交；
  分类行右键「全选该类/反选该类/清空勾选」

**生效值元数据（需求 2）**

- `metadata::preview_effective()`：**与写路径 `build_plan()` 同源的纯函数**（杜绝显示/写入两张皮）
- 元数据页新增**关键信息卡**（拍摄时间/相机/镜头/曝光/尺寸·位深·通道/色彩空间/文件/ GPS）与
  **时间「原 → 生效」对照行**（删除线原值 + 强调色生效值 + 来源字段标注；未修改也常显）；
  多选（≥2）→ 逐行小表（上限 8 行 + 「…还有 N 个」）
- GPS 卡显示**生效坐标**、地图跟随选中文件 `center_on` 生效值单钉（D4：不画新旧对比）；
  改动值统一 `.mod` 样式（强调边框 + 强调色字）

**多格式输出（需求 3）**

- **1 输入 → N 格式输出，解码仅一次**：`OutputTarget` 多目标化、`RunConfig.outputs[]`、
  色彩变换与元数据 plan 全局共享一次；BMP 丢元数据与灰度/alpha 告警语义沿用 0.2
- **输出路径模板**（`$format`/`$dir`/`$file`/`$name`/`$ext` 可混排；未知符号/`..`/绝对路径
  = 校验错误并阻断开始运行；`$dir` 空段坍缩）；「按文件格式分文件夹」单行开关与模板 `$format` 段联动
- **逐输出独立冲突解析**：`reserved` 按每个 `out_path` 登记；一源多输出撞同一 desired 时第 2..N 个
  走 rename，不再互相覆盖
- flatten 编排：alpha-preserving 目标先编码，之后在工作缓冲**原地**拍平（峰值内存仍是 2×frame）

**逐文件进度（需求 4）**

- 三级进度模型（运行级 / 源文件级聚合 / 输出级）+ 阶段权重表（probe 3% · decode 27% ·
  orient+color 10% · encode 50% · 落盘 10%）
- **真实行级进度**：JPEG（jpegli 写扫描行）/ JXL（编码回调）/ PNG / TIFF（OIIO 写行）；
  **合成进度**：WebP / HEIF / AVIF（编码器无回调）→ 按尺寸估算时长的缓出推进 + **斜纹条标识**
  （`synthetic=true`，完成跳 1.0，不倒退）
- 运行页：源文件分组行 + 逐输出子行（进度条 + 百分比 + 阶段文字 + 产物事实/失败原因）、
  总览（吞吐/并行/交错/线程预算读数 + 取消唯一入口）、日志尾卡；运行期锁定（步骤 1/2 只读）
- `--dev` 新增进度侧车（`progress-trace.jsonl`）供金样断言事件流

**自适应并行（需求 5）**

- **交错启动**：全局限速器 `next_start = max(now, last_start + stagger_ms)`（0–2000ms 可配置，
  0=立即）；取消路径立即唤醒等待者；日志记每文件启动偏移
- **E3 自适应线程预算**：纯函数 `alloc_threads(T, remaining, inflight)` —— 队列深则多文件并行 +
  编码器单线程，队列浅则收缩并行、放大 `encode_threads`（≤16）；契约 = 任一时刻「活跃编码线程 +
  活跃 worker 主线程 ≤ T」；线程映射义务落到各库 API（jxl runner / heif·avif 插件 threads /
  webp thread_level；jpegli·OIIO 无内部线程，如实记录）

**GUI 重构（需求 6）**

- **无边框窗口 + 自绘 caption 三钮**（`platform/frameless`：`WM_NCHITTEST` HTCAPTION 拖拽 /
  八向边缘缩放 / 最大化钮 HTMAXBUTTON 触发 Win11 Snap；命中区 46×32、hover 面满顶栏 52px、
  关闭悬停 `#c42b1c`；Linux 走 `startSystemMove/Resize`）；Mica + 深色标题栏随系统主题
- 三栏骨架（左 258px / 中 1.0 / 右 1.119，QSplitters 可持久化）+ 三步内容区（元数据/输出/运行）、
  底栏摘要 + 开始运行；`ui/theme` tokens 单源，明暗双主题
- 输出页：8 格式复选磁贴 + 计数、逐格式页签（位深 + ParamForm）、一行一字段统一标签轴、
  全界面禁折行（省略号 + 悬浮全文）；设置对话框追加交错启动/线程预算/分文件夹默认

**CI 工业化（需求 7）**

- 四道流水线：**pr-fast**（fmt 门禁 + 双平台 build/unit + 金样子集）· **main-full**（lint + tidy 棘轮
  → 双平台全量 → appimage/winzip → 五门禁 + 烟测 → audit/cache-gc → gate）·
  **nightly**（asan/ubsan/tsan 三路并行 + 回归基线 diff（含 Windows 基线）+ 许可/SPDX/依赖审计）·
  **release**（tag v*：appimage ∥ winzip → **probe-appimage / probe-winzip 双探测** → publish）
- 硬约束「每 job 与整轮 wall-clock 双 ≤20min」写入每 job `timeout-minutes: 20`（唯一豁免 =
  `warm-cache.yml` 播种）；发布链路前置探测 = `gh release view` 查重 + 产物 `--version` 断言
- clang-format 硬门禁（全源 0 diff）+ clang-tidy 棘轮（基线入库，告警数 ≤ 基线且新增代码 0 告警）

**依赖全升（需求 8）**

| 库 | 0.2.0 | 0.3.0 |
|---|---|---|
| Qt (aqt) | 6.8.3 | **6.11.2** |
| OpenImageIO | 3.1.14.0 | **3.1.17.0** |
| libjxl | 0.11.2 | **0.12.0**（修 box 追加的上游 bug，正落我们的 JXL 元数据路径） |
| libheif | 1.23.1 | **1.23.5** |
| exiv2 | 0.28.8 | **0.28.9**（顺删已弃用且无替代的 `enableBMFF()` 调用） |
| x265 | 4.2 | **4.3** |
| SVT-AV1 | 4.1.0 | **4.2.0** |
| aom | 3.13.3 | **3.15.1** |
| libde265 | 1.1.1 | **1.1.3**（CVE 累积必升） |
| libwebp / lcms / spdlog / zstd / libpng / giflib / openexr / highway 等 | 现状 | 对齐 vcpkg master 可得最新（实测：libwebp 1.6.0 · lcms 2.19.1 · spdlog 1.17.0 · zstd 1.5.7 · libpng 1.6.58 · giflib 6.1.3 · openexr 3.4.13 · highway 1.4.0） |
| CMake | 3.28 | 兼容线 **3.28...4.4**（CI 装 4.4.x） |

- **停留（白纸黑字）**：OIIO 3.2（未 GA）· exiv2 main/0.29-dev · jpegli 保持钉 commit `031a0077` ·
  SVT-AV1 不跨 4.x · Qt 不回 6.8；vcpkg baseline tag 保持 `2026.07.29`（超前版本走 overlay port）

**性能与 SIMD（需求 9）**

- **AVX2 / x86-64-v3 基线**：自研 targets `/arch:AVX2 /GL /LTCG`（MSVC）或
  `-march=x86-64-v3 -mtune=raptorlake|haswell` + LTO（GCC/Clang）；依赖侧**不注入 ISA/LTO flags**
  （见"修复"）；OIIO overlay 显式 `-DUSE_SIMD=avx2`、libwebp `WEBP_ENABLE_SIMD=ON`
- **5 对自研热路径**（`src/core/simd/`，各带 `*_avx2` / `*_ref` 同签名 + 一致性单测）：
  flatten（alpha 合成）/ quantize8·16（float→int 舍入）/ transpose8（8×8 分块转置）/
  downscale（LANCZOS3 水平+垂直两遍）/ interleave（通道展开）
- 基准 `--dev bench` 三场景实测（详见 `docs/m4-report.md` §7）：多格式（jpeg+webp）总耗时
  **0.932×** 两次单格式之和（解码共享生效，判据 ≤1.35×）；flatten 微基准 4.08x@256² → 1.54x@4096²

### 破坏性变更

- **接口一次性解冻 + 再冻结（D20）**：`EncodeRequest`（`target` 取代 `params/outBitDepth/outPath`）、
  `RunConfig`（`format/backend/tech/params/out_bitdepth` → `outputs[]` + 模板/交错/线程预算）、
  `Scheduler` 事件（`FileEvent.progress`）、`PresetData`（schema v1 → v2）均为**结构重排**，
  不再保证聚合初始化兼容；相关冻结头已重新标注 `PP-FROZEN(0.3.0)`
- **预设格式 v1 → v2**：v2 = `outputs: [OutputFormatSpec]` 数组 + `output_template` /
  `split_by_format`；0.2 的单格式 JSON 由 `migrate_preset_v1()` **零丢失**迁移（R31：含
  `outputs[i].lossless` 显式键，避免 png/tiff/bmp 等"技术本身无损"的格式经
  load→save→load 静默回退 false）
- **线程契约 E2 → E3**：0.2 的「编码器内部线程全部关闭」解冻为「按自适应预算分配，总活跃线程 ≤ T」
- **`--dev` CLI**：`--format/--backend/--tech` 单输出参数保留，新增 `--outputs`（多目标）、
  `--template`、`--stagger`、`--thread-budget` 与 `bench` 子场景
- **AVX2 为硬基线**：不再要求 pre-AVX2 CPU 兼容（x86-64-v3 产物）

### 修复

- **色彩回拷黑图（静默数据损失）**：非 `keep` 色彩目标下，3/4 通道源经 `ColorManager` **原地**写回
  工作缓冲后又被"回拷"一次 —— 同址读写把刚变换好的像素清零，产出**全黑图且 `ok=true`、无告警**。
  修复判据 = "当前缓冲的本地像素地址是否仍是工作缓冲的存储"（同址不回拷，灰度升维换缓冲才回拷）；
  配套像素级回归断言（`test_pipeline_contract`）。证据与逐条根因见 `docs/m4-report.md` §5.1
- **UI 偶发访问违例（D-W3-1 悬垂指针）**：`page_output.cpp` 的 `tech_by_id()` 返回**入参容器内**的
  指针，而 `effective_backends()` 按值返回 → 临时对象在语句结束即亡，指针悬垂
  （ASAN: `heap-use-after-free @ page_output.cpp:1211`）。修复 = 先绑定具名局部再取指针
  （同类点 3 处一并收敛）。证据见 `docs/m4-report.md` §5.2
- **依赖侧全局 ISA flag 的真回归**：`/arch:AVX2` 经 triplet 注入会让 **libwebp 1.6.0 无损解码崩溃**
  （`0xC0000005`；最小复现 = 仅链 libwebp 的探针，40 个依赖 DLL 回填扫描唯一触发者 = `libwebp.dll`）
  —— 根因是该库的 AVX2 内核必须**逐文件**加 flag，全局 flag 破坏其运行时 CPU 派发模型。裁定：
  依赖侧零 ISA/LTO 注入，AVX2 走各库自身机制（等价物一个不丢）
- **`/GL` 与依赖端口构建的机制冲突**：aom 端口 `WINDOWS_EXPORT_ALL_SYMBOLS` 走 `cmake -E __create_def`
  读不了 IL 对象 → LTO 只作用于自研 targets
- **HEIF/AVIF 4 通道入参错位（T19 引入，全语料回归暴露）**：逐行取像素后一律按 `x*3+c` 取 RGB，
  而步长是 `nch` ⇒ RGBA 源（或灰度升维）逐像素错位；修复 = 展开条件 `nch < 3` → `nch != 3`
  （1/2/4 通道统一经 `pp::simd::interleave` 展成 3 通道，alpha 按原 stride 取）；修复后 heif 产物
  逐字节复现基线、解码回源逐位一致
- **打包产物缺陷（T18 发现）**：便携模式把 `classes.json` / `presets` / `logs` 写进 staging →
  zip 内会带上**本机**的分类归属；改为 E-1/E-2 烟测前后 staging 快照比对 + 还原 + 硬断言
- **测试/工具二进制崩溃与断言失真的历史坑（M3 延续）**：日志 CRLF、`capture_stderr` Windows 空存根、
  缺 UTF-8 清单导致 ACP 不匹配等，均在 M3 修复，0.3.0 沿用其回归防线

### 已知问题

- **CI 四道流水线尚未在 GitHub 上首跑**：YAML 落地、本地可验证项（fmt/tidy/YAML 解析/命令面核对/
  预算表/timeout 核对）全绿；wall-clock 双 ≤20min、Linux 全链、appimage 烟测、tidy 棘轮首跑判定
  以**首次 CI 运行**为准（详见 `docs/m4-report.md` §10）
- **预览 250ms 判据**：内嵌预览命中路径满足；**无内嵌预览的 48MP 输入**实测 535.1 ms
  （含一次全解码，`--dev bench` 场景 `preview48`）——为如实记录，不改判据也不改口径
- **Windows 回归基线随版本号漂移**：`tools/baseline/golden.windows.log` 含 `version=app` 行
  （归一化规则不覆盖版本串），版本提升后需按 `regression.py --update` 重生成（本版已重生成，
  见 `docs/m4-report.md` §8.5）
- **macOS 未构建/未发行**（承 0.2.0）；Linux 侧 0.3.0 的完整验证（ctest/金样/ui_smoke/appimage）
  由 CI 与 Linux 机执行——本机为 Windows
- **zip / AppImage 不承诺字节可复现**（条目 mtime / squashfs 超级块时间戳参与，承 M2/M3 结论）
- `AVIF 10-bit + alpha 不可用`（SVT-AV1 上游缺陷，编码前明确报错并建议 libaom）、
  非 ASCII 交互控制台代码页、第三方 sRGB→sRGB ≤1.8e-4 暗部偏差等 0.2.0 遗留项**语义未变**

### 发行

- Windows：`PhotoPipeline-0.3.0-win64.zip`（由发布轮打包产出；**T21 未在本机重跑打包**，
  release 上传由主对话执行——口径见 `docs/m4-report.md` §8.7/§10）
- Linux：`PhotoPipeline-0.3.0-x86_64.AppImage`（由 `release.yml` 的 `appimage` job 构建）
- **本轮不含 push / tag / Release**：`v0.3.0` tag 与 GitHub Release 待用户验收授权后由主对话执行

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
