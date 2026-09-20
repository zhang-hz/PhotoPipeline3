# M2 收口报告（Linux debug 收口 + 发行状态）

- **状态**：SUCCESS —— 出口准则 §8 十条全部满足；R15（CI 真实首跑）闭合。
- **执行依据**：`docs/m2-tasks.md` v1.0（`ab5a88a`）+ §9.1–§9.7 落地记录（本次收口订正）。
- **周期**：任务书 `ab5a88a`（2026-09-20）→ 收口 `9adf5b8`（本报告 = T26）；基线 = M1b 收口 `b97eff2`。
- **批次提交**：`ab5a88a^..9adf5b8` 共 **43** 个提交（`git rev-list --count`；全部追加式，无 amend/rebase/reset）。
- **纪律**：subagent = deepseek-flash（max），主对话独占裁定与文档；禁二次下发；pathspec 提交。

## 1. 概述

M2 目标：在 M1a（引擎）/M1b（界面）行为冻结的基础上完成 **Linux debug 收口 + 发行状态**。交付定义（任务书 §0）= ① 本地与 CI 均能产出**可双击运行**的 AppImage；② `--version` 可查（版本单源）；③ CHANGELOG 0.1.0 + README 发行章节齐备；④ GPL/第三方许可自查通过（许可文本随产物分发）；⑤ 出口准则 §8 十条全绿。

范围外：Windows bring-up（归 M3）。发行平台 = Linux x86_64 单平台，产物 = AppImage 单一产物（§9.0 决策 1/2）。

**结论一句话**：M2 全部目标达成 —— sanitizer/回归/金样/UI 冒烟/打包/CI 六条验证链均有可复跑证据，`TODO(M2)` 归零，CI 四 job 全绿并产出 AppImage artifact，PhotoPipeline 进入 **0.1.0 发行状态**。

## 2. 交付总览表（按波次）

| 波次 | 任务 | 内容（一句话） | 主提交 | 关键验证证据（极简） |
|---|---|---|---|---|
| W1 | T1 | ASan/UBSan 全语料清扫 | `6999878` | 144 日志 sanitizer 关键字 **0 命中**；通道活性双向对照；live heap 25,350,653 B 四档相等 |
| W1 | T2 | TSan + 并发卫生（#25/#27/#30） | `6f1a1c5` `972f115` | TSan ctest 24/24 且 **0 报告**；`tools/tsan.supp` 22 条规则 + 阳性对照仍报 `pp::` 竞争 |
| W1 | T3 | 日志通道：`PP_LOG_LEVEL` + 16 MiB 上限 | `241e823` | logger 单测（env 覆盖/非法值/截断）+ ctest 24/24；`logger.h` 仅追加 |
| W2 | T4 | 警告通道贯通（元数据写路径显式出参） | `8bab12e` | 金样 `warnings_contain` 断言；既有 `plan.warnings` 语义不变 |
| W2 | T13 | whole-archive 自注册统一、删 anchor（#1/#5） | `9b484bc` | `pp_linkprobe` 9× `PROBE OK`、`PLUGINS:` 24 项、`HEIF_ENCODERS:` 3 项 |
| W2 | T7 | UI 观感 4 项 + XMP 搜索 + 时间预览异步化（#22/#28/#29） | `6c338a3` | `.cache/tmp` 临时自验 **61/61** 断言；ctest 24/24 |
| W2 | T7b | 色彩目标字面量去重（#24） | `32060bf` | 复用 `pp::to_string/parse_color_target`；round-trip 自验 |
| W3a | T5 | 交叉参数约束 + 未知参数报告（#2/#16） | `88cd36a` | `cross_validate` 全分支单测；ParamForm 红字 + `on_start` 阻断；`--dev` 失败路径 |
| W3a | T6 | CICP 最小色彩映射（#7，§2.8） | `14b111a` | 四组合映射单测（原色/白点容差 ≤ 2/255）；PQ/HLG 维持现状 |
| W3a | T11a | 版本单源 + `--version` + 图标/desktop（§2.1/2.2/2.5/2.6） | `5ddc741` | `--version` → `PhotoPipeline 0.1.0`；图标脚本幂等；冻结文本逐字节 |
| W3a | T11b | AppImage 打包脚本 + Qt TLS 后端 | `91b14f0` `a908ed0` | 打包烟测；TLS 实网探针 `http_status=200 bytes=6929 png_magic=yes` |
| W3a | T11c | 随附第三方许可文本 | `466ade3` | 产物内 `usr/share/licenses/` = 40 port `copyright` + 本项目 `LICENSE` |
| W3a | T11d | type2 runtime 入库 + 离线打包 | `bdfee54` | 打包期零网络（runtime 固定 `--runtime-file`，SHA512 校验） |
| W3a | T14 | locale 安全路径层（#23） | `8ec8a68` | 新增 `test_paths`/`test_presets` 断言；非 UTF-8 路径往返一致 |
| W3b | T8 | 金样断言级 16 对 + 元数据文本化规范化（#26） | `8caf8ea` `97a440a` | `SMOKE total=16 pass=16 fail=0`；`pp_verify --selftest` 27 probes 0 unexpected |
| W3b | T9 | 全语料回归基线 + 规范化器 | `35f3c59` | `golden.log` **1636 行**随提交入库；双跑零 diff；注入敏感性成立 |
| W3c | T10/T10a | CI 从零到绿（首跑 R15） | `eeebd3e` `c802f52` `5d828e8` `8e1815b` `cb5795b` `d837194` `8cc8039` `fb86cef` | 七根因全部环境类（§4②）；首轮 linux ctest 23/23 |
| W3c | T12 | TODO(M2) 归零 + 发行文档草案 | `08c9a0c` `91bbeef` | `grep -rn "TODO(M2)" src/ tests/ tools/` = 0；草案入 `docs/m2-drafts/` |
| W3c | T15 | vcpkg overlay 安装 `jerror.h`（干净环境 libtiff） | `f5354dd` | 干净 tree 构建通过（CI linux job） |
| W3c | T16 | ui_smoke fixture 确定性 | `c61bdd8` | 8 张截图断言顺序无关 |
| W3c | T16b | 断言顺序无关化 + 集合排序 | `088e591` | iso/repo 两树对照：6 张稳定图 sha256 相同（`/tmp/pp-t16b-evidence.txt`） |
| W3c | T17 | 发行文档落盘（CHANGELOG 0.1.0 + README 章节） | `7d83ede` | `CHANGELOG.md` + README「发行版（AppImage）」章节 |
| W3c | T18 | AppImage 插件依赖闭包修复（双击无响应） | `0219105` | 闭包门禁 A/B 全过；GUI 实启 PASS（窗口 2560×1600 + 截图 231,759 B） |
| W3c | T19 | CI 重构（依赖清单化 + 三类缓存 + cache-gc） | `4b27c92` `27c9a29` `48a0190` `a63acff` | 四 job 全绿；整轮 20–35 min → **2–3.5 min**；依赖覆盖自检 61/61 |
| W3c | T21a/T21b/T21b2 | UI 审查截图重出与视觉复核（无代码提交） | — | `.cache/t21-report.md`（8 张 × 2 平台，像素/调色板/裁片证据）；确定性事实见 §9.5 |
| W3c | T21c | 审查基线隔离 + 预设名可用性 | `c5342d2` | `.cache/ui-smoke-ci` 与 `.cache/ui-review` 互不覆盖；`另存为` 由灰转可用 |
| W3c | T22 | 运行页状态行裁切修复 | `9adf5b8` | 1440×900/1024×680 两种分辨率探针 `fits=1`；ctest 24/24、金样 16/16 |
| 收口 | T24 | AppRun 有界失败弹窗 + `PP_NO_GUI_POPUP` | **收口时尚未落盘**（在跑） | 见 `docs/m2-tasks.md` §2.9 落地口径④（执行中，不记为已实装） |
| 收口 | T25 | 文档收口（`docs/m2-tasks.md` §9 订正 + README/CHANGELOG 终稿） | 本次（未提交时撰写） | 主对话工作区，非本报告文件域 |
| 收口 | T26 | **本报告**（`docs/m2-report.md`） | 本提交 | 全部数字附来源；待核清单见文末 |

编排变更（实际执行，`docs/m2-tasks.md` §6）：`pipeline.cpp` 三方争用串行化 → T13 留 W2、**T5 后移 W3a、T6 后移 W3b**；T4 在 W2 早段独占该文件。**T20/T23 编号未使用**（原书与执行中均无）。

## 3. 规格裁定与冻结文本订正

### 3.1 冻结头 v1.1（M1b 遗留）

M1b 已就 `ParamForm` 析构、`thumbnails.h` AUTOMOC 引用 + `unsupported_paths_` 去重成员、`page_run` 终态计数三处冻结头 v1.1 修订（`docs/m1b-report.md` §2），M2 全部沿用。M2 内唯一冻结头改动 = `logger.h` **仅追加** `LogLevel level_from_env_or(LogLevel)`（T3）：`git show 241e823 -- src/core/logger.h` = **7 insertions(+), 0 deletions**，既有签名零变动。

### 3.2 §2.7 规则②订正（条件与文案）

- **原文（错）**：`progressive==true && optimize_coding==true` → “不能同时启用”。
- **订正后**：`format=jpeg && progressive==true && optimize_coding==false` → **“启用渐进式时必须启用哈夫曼表优化”**。
- **jpegli 依据**：jpegli 上游 `tools/cjpegli.cc:146`（检出 `.cache/jpegli/jpegli-031a0077…/`）要求 `progressive_level>0 && !optimize_coding` 必须改配 `--fixed_code` 且 `-p 0`；`src/codecs/enc_jpegli.cpp:304` 为 `cinfo.optimize_coding = (progressive || optimize_coding)`；`format_tables.cpp:88-100` 的 jpeg 默认值（`progressive=true`、`optimize_coding=true`）与 `lock_true_when_progressive` 谓词共同印证 **(true,true) 才是规范态、(true,false) 非法**。
- **落地**：`src/core/params.cpp:313-324`；消息逐字 = 上句；jpeg 两默认值天然不误报。

### 3.3 §2.8 触发条件订正 + PQ/HLG 维持现状

- **触发条件订正**：原措辞“**无 ICC** 的 JXL 源”不可观测 —— OIIO 3.1.14 对 JXL 恒提供 `ICCProfile`（libjxl 由 codestream 合成后发布）：`tests/golden/base/jxl8.jxl` 本体 222 字节，`ICCProfile` 属性报 536 字节。故条件改为 **“`CICP` 属性在场的 JXL 源”**；落地实装 `src/core/pipeline.cpp:282-304`（注释逐字记录同一条事实）。
- **枚举不变**：(1,13)→sRGB；(12,13)/(12,1)→Display P3；(9,8)→BT.2020 linear；(9,13)→BT.2020 sRGB-TRC。
- **PQ(16)/HLG(18) 与未列组合维持现状**（仅记日志 `CICP transfer <n> 未支持，按 sRGB 处理`，像素沿用 libjxl/OIIO 合成 ICC）：实测若强制落 sRGB，PQ 中灰 0.5020 → 0.6024（Max/RMS error 0.1004 ≈ **25.6/255**），属可见劣化，故不动。
- 其它证据：`.cache/tmp/m2-t6/evlogs/all.log:111/130` 为两条冻结告警原文；ev2/ev3 下同一 64×64 photo 源的“无 CICP PNG”与“(9,16) JXL”转码到 sRGB **逐像素一致（max|Δ|=0）**；ev4 gray 组存在恒定 26/255 差异（灰度路径差，与 CICP 无因果，处置见文末待核）。

### 3.4 用户四项裁决（`docs/m2-tasks.md` §9.6）

| # | 裁决 | 仓库内证据 |
|---|---|---|
| 1 | **发行产物取 CI 构建**（ubuntu-24.04 runner，glibc 基线 ≥ 2.39）；本机 26.04 自建产物（glibc ≥ 2.43）只作开发验证 | README「系统要求」写明 glibc 基线与“发布附件应取 glibc 基线更低的打包产物”；CI `appimage` job + `PhotoPipeline-AppImage` artifact |
| 2 | **必须随附第三方许可**：40 port `copyright` + 本项目 `LICENSE` | `tools/collect_licenses.sh`；产物内 `usr/share/licenses/` 41 目录 = 40 port + `PhotoPipeline/LICENSE`；打包烟测 ④ 校验 |
| 3 | **LGPL 静态链接合规基础** = “完整源码可得 + 文档化可复现构建”（源码 offer = 仓库 URL + 对应 tag） | README「源码与许可」；`docs/m2-drafts/README-release-section.md` LGPL 链接形态表（Qt 动态可替换；libheif/lcms2/exiv2/OIIO 静态） |
| 4 | **14 个 `PP-FROZEN` 头 SPDX 补标归 M3**（本批不动冻结头） | GPL 自查表第 2 行（`src/**` 66 文件中 52 带 SPDX，其余 14 = 冻结头） |

### 3.5 30 项 TODO(M2) 处置结果

| 处置 | 数量 | 明细 |
|---|---|---|
| 修复 | **17** | #1 #2 #5 #6 #7 #8 #14 #16 #22 #23 #24 #25 #26 #27 #28 #29 #30 |
| 重分类 `TODO(M3)` | **2** | #15 `pixelbudget.cpp:24` 内存探针、#21 `paths.cpp:14` Windows 分支 |
| 弃权改 `NOTE(...)` | **11** | #3 #4 #9 #10 #11 #12 #13 #17 #18 #19 #20（perf/limit/design/fact/upstream 各附一句理由） |

结果：非弃权 **19 = 修复 17 + M3 2**（`docs/m2-tasks.md` §3 落地结果）；`grep -rn "TODO(M2)" src/ tests/ tools/` = **0**（无输出、退出码 1，实测）；现存 `TODO(M3)` 2 处（实测 `src/core/pixelbudget.cpp:24`、`src/platform/paths.cpp:14`）。表内行号为裁定当时锚点，复核按内容匹配。

## 4. 事故与工程事实

### 4.1 宿主 `systemd-run ENOENT` 全局中断（§9.3）

- **现象**：`spawn systemd-run ENOENT` → 全局 shell 中断，并行波次多路任务命令同时失败。
- **根因**：部署的 `runnerCommand` 指向自定义沙箱 runner，该 runner 不可解析（不在 PATH）；且该 runner 选择在提供方生命周期内被缓存 ⇒ 改回配置也必须重启 DSH 才生效。
- **处置**：四路在跑任务**冻结保全**（不提交半成品、不放弃工作树），恢复后 resume 续跑；恢复后全部工作树与证据完整。
- **损失**：**零**（无提交丢失、无证据丢失、无需重跑已完成的验证）。
- **教训**：长任务必须定期产出可恢复证据（落盘报告/日志 + 明确断点）——M2 的证据目录（`.cache/m2-t*`、`tools/baseline/`、`docs/m2-drafts/`）即该纪律的产物。

### 4.2 CI 七根因表（§9.1，全部环境/打包类）

| # | 根因 | 性质 | 修复 |
|---|---|---|---|
| 1 | job 级 `env` 写 `~`（该上下文无 runner context，`~` 不展开）→ 缓存/工具路径失效 | CI 配置 | `5d828e8` |
| 2 | `nasm` 缺失 → svt-av1 / aom / libwebp `BUILD_FAILED`（run `35513359184`） | 干净环境缺构建工具 | `cb5795b` |
| 3 | `jerror.h` 缺失 → 干净环境 libtiff 构建失败 | vcpkg overlay | `f5354dd` |
| 4 | release `libjpeg.pc` 缺失（`libtiff-4.pc` `Requires: libjpeg`；本机被系统 libjpeg-turbo8-dev 掩盖） | vcpkg overlay | `d837194` |
| 5 | debug `libjpeg.pc` 缺失（vcpkg DEBUG 的 pkgconfig 检查只搜 `debug/lib/pkgconfig`） | vcpkg overlay | `8cc8039` |
| 6 | OpenGL dev 包缺失 → Qt6Gui WrapOpenGL / FindOpenGL 失败（run `35517286929`） | 干净环境缺 apt 包 | `fb86cef` |
| 7 | `libEGL.so.1` 缺失 → `undefined reference to eglGetCurrentDisplay/eglMakeCurrent…`（19 符号，run `35519174344`） | 干净环境缺 apt 包 | `4b27c92`（依赖清单一次补齐） |

**结论**：七根因全部是“本机有、干净 runner 无”的环境/打包类（vcpkg overlay 产物 + pkgconfig 安装位置 + apt 包），**零 gcc-13 代码问题** —— 首轮 `linux` job 的 ctest **23/23 即全绿**（run `35520031504`），全批未因编译器差异改任何行为语义（任务书 §1.4 允许修复面内）；历史教训逐条固化在 `tools/ci-system-deps.txt` 头注释（含 run id）。

### 4.3 本机掩盖类缺陷的方法论

1. **dpkg 属主反查**：`ldconfig -p` → soname 路径 → `dpkg -S` 反查包名（`tools/ci-check-deps.sh`），把“本机恰好有”变成“清单显式覆盖”。CI 覆盖 61/61、未覆盖 0；本机 55/55。
2. **反查前 `realpath -m` 规范化**：Ubuntu merged-usr 下 `ldconfig` 打印 `/lib/...` 而 dpkg 记 `/usr/lib/...` ⇒ 修复前 61/61 全假阳性（run `35520031504`），本机 26.04 因路径恰为 `/usr/lib` 而**未能复现**——典型的“本机掩盖”。
3. **干净环境仿真**：`git archive` 导出 iso 树复建（T13/T16b/T18 均用），干净 tree 构建暴露 `jerror.h`、`libjpeg.pc` 两类只在本机被系统包掩盖的缺陷。
4. **负向对照**：删 `libegl1`/`libxcb-cursor0`/`libxkbcommon-x11-0` → 自检精确报出 3 项；`tools/lsan.supp`/`tools/tsan.supp` 各自用注入/纯复现器证明“抑制文件不掩盖真问题”。

### 4.4 前序工程事实（M2 相关）

- **`-fPIC` copy-relocation 陷阱**（M1b）：手写 g++ 自验不加 `-fPIC` → Qt `staticMetaObject` 本地定义 → `findChild`/`qobject_cast` 静默 null；M2 的 T7/T21c/T22 探针均沿用既有 ninja 编译行（含 `-fPIC`）。
- **AUTOMOC 陈旧**：新 glob 文件 + `Q_OBJECT` 在共享 build 目录首次构建必在 `.ddi` 扫描边报错 → 重试前删 `<target>_autogen`（M2 各任务独立 `build/m2-t<n>` 目录天然规避）。
- **ENOSPC**：T1 首次构建链接期 `ld: final link failed: 设备上没有空间`（`build/m1-*`/`build/m1b-*` 32 棵已关账树占 ~45 GB，可用仅 981 MB）；清理 gitignored 陈旧树后 **47 GB 可用**（证据 `.cache/m2-t1/SUMMARY.md`）。
- **UI 类不进 ctest 单测**：`pp_test_*` 只链 `pp_core`（M1b 事实不变）；UI 验证通道 = `ui_smoke` + 临时自验程序（`build/m2-t<n>` 独立构建目录同时规避 AUTOMOC 陈旧）。

## 5. 验证证据

### ① ctest

- release 树 **23/23**：CI `linux` job `ctest release（23 条）` → `100% tests passed, 0 tests failed out of 23`（run `35520615242`，2.6 s）。
- release-dev 树 **24/24**：本地 `ctest --test-dir build/m2-t22 --output-on-failure` → `100% tests passed, 0 tests failed out of 24`（7.72 s，`.cache/m2-t22-ctest-final.log`；T21c 7.62 s）。24 = 18 单测 + `ui_smoke` + `linkprobe` + `fixtures` + `spike_e` + `spike_f` + `verify_selftest`。

### ② 金样 16 对 + `pp_verify --selftest`

- `bash tests/golden/smoke.sh build/m2-t22` → **`SMOKE total=16 pass=16 fail=0`**（`.cache/m2-t22-smoke-final.log`）；CI `linux` job 同获该行（run `35520615242`，1.5 s）。
- 16 对：jpeg-lossy / jxl-lossless / png16-lossless / tiff16-lzw / webp-lossless / heif-lossy / avif-lossy / bmp-exact / meta-artist / gray-webp / alpha-jpeg / depth-jpeg / multipage-png / unicode-png / exif-roundtrip / metaonly-jpeg（`tests/golden/smoke/*.json` 16 个用例文件 + 1 个 preset 辅助文件）。
- `./build/m2-t22/pp_verify --selftest` → `VERIFY selftest OK (27 probes, 0 unexpected)`，退出码 0；ctest `verify_selftest` 0.02 s 绿。
- 引擎注册面：`./build/m2-t22/pp_linkprobe` → 9× `PROBE … OK`、`PLUGINS:` 24 项、`HEIF_ENCODERS:` 3 项，退出码 0。

### ③ 回归基线

- 基线 `tools/baseline/golden.log`：**1636 行 / 178,544 B / sha256 092e3dee…**（随 `35f3c59` 入库）。
- 双跑零 diff：`regression: normalized 1636 lines vs baseline 1636 lines` + `regression: zero diff (baseline reproduced)`（`/tmp/t9-final.log`，run1 归一化输出与基线 `cmp` **IDENTICAL**）；基线口径 = `--dev`、workers=1、12 次调用（8 格式 × 全语料 + 4 格式 × 仅元数据）、280 文件槽位、9 条规范化规则（N1–N9）。
- 敏感性：注入（移除 1 个语料输入，27→26）→ `normalized 1620 lines vs baseline 1636 lines` + `DIFF FOUND (384 lines of unified diff)` ⇒ 断言确实敏感（口径偏差见文末待核）。

### ④ sanitizer

- **ASan/UBSan**：3 轮必跑矩阵 + 29 次补充选项轴 = **144 个日志**，`grep -E "ERROR: (Address|Leak|Memory)Sanitizer|runtime error:"` **0 命中**；`src/core/**`、`src/codecs/**` 无修复。
- **通道活性双向对照**：`LD_PRELOAD` 注入泄漏 → `ERROR: LeakSanitizer: detected memory leaks … 8192 byte(s) leaked`；注入 UBSan → `runtime error: signed integer overflow`+栈回溯。`tools/lsan.supp` 39 行纯注释 / **0 条生效规则**，且同一注入泄漏在该文件下**仍被报告**。
- **增长性泄漏 = 0**：同一进程重复 1/4/8/16 份语料，live heap（`__sanitizer_get_current_allocated_bytes()`）**25,350,653 B 四档逐字节相等**；RSS 187.6 → 201.2 → 207.8 → 215.0 MB（次线性，ASan 运行时占主导）。`tools/lsan.supp` 与 `tools/tsan.supp` 均不入编译/链接，只经环境变量消费。
- **TSan**：`build/m2-t2`（tsan preset）ctest **24/24** 且 **0 报告**（`ctest-final.log` / `ctest-final2.log`，17.62/17.84 s）；`tools/tsan.supp` **158 行 / 22 条规则**，逐条附 happens-before 论证，**无一条匹配 `pp::`**（规则族 = OpenImageIO spin/ustring/thread_pool、`tsl::robin_map`、`std::packaged_task`/`std::function`、Qt `QCallableObject`）；**阳性对照** `race_control.log` 在加载抑制文件时仍报出 `pp::race_touch()` 的 data race（`SUMMARY: ThreadSanitizer: data race … in pp::race_touch()`）。

### ⑤ `--ui-smoke`

- 8 张截图、3 页遍历、末行冻结断言 `UI-SMOKE OK shots=8 pages=3`（本地 offscreen + 真实平台 + CI 三处一致）；冻结行含 `UI-SMOKE run status: 已完成：成功 16 · 失败 0 · 跳过 0`、`05-exif-editor selection: Software = OpenImageIO 3.1.14.0 : 05BA6CFD…`、8 行 `UI-SMOKE shot`。
- **顺序无关性（iso/repo 对照，T16b）**：iso 树（`git archive HEAD`）与工作树两种语料目录顺序下，01/02/02b/04/05/06 **6 张 sha256 相同**；03-run/03b-run-done 因计时抖动不同（约定不参与断言）。
- **同平台确定性 6/6**：offscreen 三跑（正式 ×2 + 探针）与真实平台三跑各 **6/6 sha256 逐字节相同**；跨平台必然不同（offscreen=Fusion `#308cc6` / 真实=Yaru `#e95420`，RGB vs RGBA）——**禁止跨平台字节断言**。强调色与主题差异见 `.cache/t21-report.md` §3。
- **基线隔离**（T21c）：CI/ctest 固定写 `.cache/ui-smoke-ci`（offscreen），人工审查集固定写 `.cache/ui-review`（显式 `--shots`）；实测两轮 ctest 前后审查集 8/8 sha256+大小+mtime 完全不变，且两目录 8/8 互不相同。

### ⑥ AppImage

- **本地产物**：`dist/PhotoPipeline-0.1.0-x86_64.AppImage`，**52,464,120 B**，sha256 `faa73d0b54c684ff56b00d48922c0ca918c17c87ef2ca59d96435f018efb091c`（`dist/` 按 §2.9 忽略，不入库）。
- **闭包门禁 A/B**（`tools/appimage-check-closure.sh`，36 个 ELF：主二进制 + `usr/lib` + 全部插件）：① `[FAIL-A] not found` = 无；② `[FAIL-B] libQt6* 解析到 AppDir 之外` = 无；结论 **PASS**；目标机系统要求 **40 个 soname** 写入 `dist/PhotoPipeline-0.1.0-deps.txt`。
- **随包库 26 个 `.so`**：7 × `libQt6*`（Core/DBus/Gui/Network/Svg/Widgets/XcbQpa）+ 3 × `libicu*` + `libjpeg.so.62` + 13 × `libxcb-*` + `libxkbcommon-x11.so.0` + `libX11-xcb.so.1`；插件 `qt-plugins/` = platforms(offscreen,xcb) + imageformats/svg/gif/ico/jpeg + iconengines + tls(openssl,certonly)。
- **“绝不放行”清单**（`NEVER_BUNDLE`，命中即硬失败）：GL/EGL/GLX 驱动栈（libGL/libEGL/libGLX/libOpenGL/libGLdispatch/libdrm/libgbm）、单副本不变式库（libX11/libxcb/libxkbcommon/libXau/libXdmcp/libICE/libSM/libglib-2.0/libdbus-1）、glibc/libstdc++/libgcc 家族；`libQt6*` 必须来自 Qt 工具链（逐字节 `cmp` 溯源断言）。
- **TLS 实网探针**（`tools/tls_probe.cpp`，用 AppDir 内 Qt + `tls/` 插件）：`probe tls=supported` / `probe http_status=200 bytes=6929 png_magic=yes` / `TLS-PROBE OK`（`/tmp/tls-debug.log`）——修复前同一 AppImage 启动即 `qt.network.ssl: No functional TLS backend was found`（`/tmp/t11-appimage-gui.log`）。
- **许可 + 离线打包**：产物内 `usr/share/licenses/` = **40 个 port `copyright`** + `PhotoPipeline/LICENSE`（`collect_licenses: 第三方条目 40 个`；`libiconv` 为真实 port 但 vcpkg 未提供许可文本 → 打包只告警，已在 §8 列名处置）；appimagetool + type2 runtime 均入库（`tools/bin/*` + `.sha512`），`--runtime-file` 固定，校验失败 `exit 2` **不静默回退网络**，`APPIMAGE_EXTRACT_AND_RUN=1` 免 FUSE。
- **GUI 真机窗口 + 截图**（`tools/appimage-gui-smoke.sh`，T18）：1 s 检出窗口，`WM_NAME = "PhotoPipeline"`、`WM_CLASS = photopipeline`、`Map State: IsViewable`、几何 **2560×1600**；截图 `.cache/tmp/t18-appimage-gui.png` **231,759 B / sha256 2536718ff409ea91…**；结论 `PASS（进程存活 + 窗口存在）`。便携模式语义不变：挂载点只读 → 自动回退 `$XDG_DATA_HOME/PhotoPipeline`。

### ⑦ CI

- **R15 闭合**：首跑 run `35512612876`（1m6s，红）→ 连续 7 轮红灯（§4.2）→ **首次四 job 全绿 run `35520386390`**（`linux` 1m52s / `appimage` 2m4s / `ui-smoke` 2m21s / `cache-gc` 7s）；当前最新全绿 **run `35520615242`**（head `a63acff`：`linux` 2m21s / `appimage` 2m13s / `ui-smoke` 1m51s / `cache-gc` 6s）。**首次在 CI 取得冻结行** = run `35520031504` 的 `ui-smoke` job（`UI-SMOKE OK shots=8 pages=3` + ctest 1/1）。
- **ccache**：`Cacheable calls 326/326 (100%)`、`Hits 241/326 (73.93%)`（direct 228/241 = 94.61%）、`Misses 85/326 (26.07%)`、缓存占用 0.24%（上限 2 GB）。
- **vcpkg**：冷装 ≈ **34 min** → 热缓存 `Restored 41 package(s) … in 5.4 s`，configure 步 **11.2–14.3 s**（linux 14.3 s / appimage 11.2 s，run `35520615242`）；release build 111.2 s → 17.7 s。
- **依赖覆盖自检 + 拓扑**：清单 64 包（T19 首版 57 + 首轮闭包差异 7）；soname 72（随包/loader 11、需系统 61）；**已覆盖 61/61、未覆盖 0**；负向测试删 3 包精确报 3 项。三构建 job（`linux` 含 ctest release + 金样 16 / `ui-smoke` / `appimage`）+ `cache-gc`（`needs: [linux, ui-smoke, appimage]`）+ concurrency(cancel-in-progress) + 每 job timeout 60 min + 每 job `$GITHUB_STEP_SUMMARY`；`warm-cache.yml`（`workflow_dispatch` + 每周日 03:17 UTC）按同一 key 方案播种（run `35520773894`，1m47s）。
- **AppImage artifact**：`PhotoPipeline-AppImage`（zip 50,328,849 B，artifact ID 10608730168）；产物指纹 `PhotoPipeline-0.1.0-x86_64.AppImage` **50,944,504 B**、sha256 `21ba18e0480e0a2b5f920518ee51dbd8f41a03719a2aea5fa440d743b8cc4c3b`、目标机系统要求 46 条 soname；产物启动烟测（offscreen 6 s 存活）PASS。
- **未覆盖**：`9adf5b8`（T22）撰写时**尚未推送**（`git log origin/main..HEAD` 显示 1 个提交）⇒ 上表全绿结论对应 `a63acff`；T22/T25/T26 的推送与再跑一轮见文末待办。

## 6. R2 迭代记录（发现 → 裁定 → 修复 → 复测）

### 6.1 R1 预审（M1b，作为 M2 入口）

- **发现 → 裁定**：8 张截图视觉审查 → 高 3 / 中 22 / 低 20；事实核查排除 4 项误报（设置页控件符合冻结规格、缩略图棋盘格 = 语料本体、03-run 时点伪影、格式短名两表面各自冻结）→ 七域修正（lossless 参数行抑制、avif 预选重触发+文案、G5 单一取消、EXIF 页签回冻结文本、卡片禁用观感、对话框尺寸/空态、摘要全角冒号）+ §9.1 冻结文本修订。
- **修复**：`dbbfa67` 及后续 U-FIX 批次；**复测**：8 项中 4 项完全生效、4 项部分生效（均 [低] 级观感），零回归 → M2 承接遗留 5 项（#22/#28a-d/#29）与已知问题清单。

### 6.2 T18：AppImage 双击无响应

- **发现**：AppImage 双击无反应、终端启动才见 `Could not load the Qt platform plugin "xcb"`（exit 134）；根因 = 打包只对主二进制做依赖闭包，插件 `libQt6XcbQpa`/`libQt6Svg` 解析到**系统 Qt 6.10** 而随包 Qt 是 6.8.3 ⇒ `version 'Qt_6.10' not found`（B 类缺陷，旧烟测测不出）。
- **裁定 → 修复**：闭包输入扩为「主二进制 + 全部插件源 ELF」，解析路径加 Qt 工具链 lib 目录，插件 X11 支持库纳入随包；打包期对每个 ELF 做 A/B 硬门禁 → `0219105`（新增 `tools/appimage-check-closure.sh` +117、`tools/appimage-gui-smoke.sh` +122；`tools/make_appimage.sh` +195/-10；合计 3 files +434/-10）。
- **复测**：闭包门禁 A/B 无命中；`appimage-gui-smoke.sh` 实启 PASS（2560×1600 窗口 + 截图）；CI `appimage` job 加“产物启动烟测”后持续绿。

### 6.3 T21 系列：截图复核与基线隔离

- **发现**（用户视觉复核）：① `搜索参数…` 只在高级组展开时渲染 ⇒ 8 张基线缺该状态、列对齐无视觉证据；② 预设对话框 `suggested_name` 为 CJK，被 `sanitize_name` 清空 ⇒ `另存为` 灰化（用户看到名字却点不动）；③ 跑 `ctest -R ui_smoke` 会覆盖人工审查集 `.cache/ui-review`。
- **裁定 → 修复**：补高级组展开证据图（不入冻结 8 张清单）、预设名改 ASCII 且**不动** U6 清洗规则、审查集与 CI 输出目录隔离 → `c5342d2`（`tests/ui_smoke.sh` 默认目录 → `.cache/ui-smoke-ci`；`mainwindow.cpp` `示例预设` → `sample`；2 files, +14/-3）。
- **复测**：三方证据 —— 脚本默认值；两轮 ctest 前后 `.cache/ui-review` 8/8 sha256+大小+mtime 不变；两目录 8/8 互不相同；高级组展开实测 13/13 字段 `x=763, d=0`；预设 `btn_saveas.enabled` 0→1。

### 6.4 T22：运行页状态行截断

- **发现 → 裁定**：`runStatus` 标签在窄窗口/长文案下裁切（`fits=0`）→ 顶部状态行布局激活同步（不引入新语义），分辨率与状态两轴探针化。
- **修复 → 复测**：`9adf5b8`（`src/ui/page_run.cpp` 顶部行布局）→ 1024×680 / 1440×900 两分辨率 × 多状态探针 `fits=1 slack ≥ 0`（`.cache/m2-t22-probe-platform.log` 等）；ctest 24/24（7.72 s）、金样 16/16、`ui_smoke` 冻结行不变。

## 7. 出口准则核对表（§8 十条）

| # | 准则 | 证据 | 结果 |
|---|---|---|---|
| 1 | 金样断言级 ≥ 11 对全绿（本地） | `SMOKE total=16 pass=16 fail=0`（本地 + CI linux job） | ✅ |
| 2 | ASan/UBSan 全语料矩阵干净；TSan 并发路径干净 | 144 日志 0 命中 + 通道活性双向对照 + live heap 四档相等；TSan 0 报告 + 22 条论证规则 + 阳性对照 | ✅ |
| 3 | offscreen `ui_smoke` 绿（本地 + CI job） | 本地 ctest 24/24；CI `ui-smoke` job 全绿并取到 `UI-SMOKE OK shots=8 pages=3` | ✅ |
| 4 | AppImage 产出 + 烟测三断言绿（本地 + CI job + artifact） | 本地四项内置烟测 + 闭包门禁 PASS（52,464,120 B）；CI `appimage` job 绿 + `PhotoPipeline-AppImage` artifact（50,944,504 B） | ✅ |
| 5 | `grep -rn "TODO(M2)" src/ tests/ tools/` 归零 | 实测无输出（退出码 1）；30 项全处置（17 修复 + 2 M3 + 11 弃权） | ✅ |
| 6 | 回归基线入库，双跑零 diff | `tools/baseline/golden.log` 1636 行入库；连跑零 diff；注入敏感性成立 | ✅ |
| 7 | CI 全绿（R15 闭合） | 首绿 run `35520386390`，最新全绿 run `35520615242` 四 job 全 success | ✅ |
| 8 | 版本单源生效；CHANGELOG + README 发行章节落盘；GPL 自查通过 | `--version` = 关于页 = `PP_VERSION_STRING` = 0.1.0；`CHANGELOG.md` + README 发行章节；LICENSE 在库 + 关于页清单 + 产物内 40 port 许可 + 源码 offer = 仓库 URL | ✅ |
| 9 | ctest 无回归（23 release / 24 release-dev + 本批新增） | release 23/23（CI）、release-dev 24/24（本地 T21c/T22） | ✅ |
| 10 | UI 观感 5 项修复经截图差分复核生效 | T7 五项实装（61/61 断言）+ T21 截图逐张复核（02-output 描边、x=763 对齐、设置页底部留白 11/17 px、空态入列表框、XMP 搜索框） | ✅ |

## 8. 已知问题与 M3 候选（不阻塞本批发行）

1. **Windows bring-up**（含 2 项 `TODO(M3)`）：`pixelbudget.cpp:24` 内存探针、`paths.cpp:14` 数据目录分支 —— 影响：Windows 不可用；建议 M3 首任务。
2. **14 个 `PP-FROZEN` 头缺 SPDX 标识**（`src/codecs/*.h`、`src/core/*.h`、`src/decode/oiio_reader.h`、`src/platform/paths.h`、`src/ui/preset_io.h`）—— 影响：许可元数据不完整（当前由根 `LICENSE` 覆盖）；建议 M3 冻结解除时补齐。
3. **内省参数中文标签与后端短名**：当前沿用后端英文名 —— 影响：界面中英混排；建议 M3 统一术语表后一次性改。
4. **其余 QString 边界**（`QDir` → `std::filesystem`）：model/dialog 层仍保留 `filesystem::path` —— 影响：跨平台路径语义一致性；建议 M3 与 Windows 分支一并处理。
5. **`tools/gen_corpus.sh` 跨路径字节幂等**：OIIO 写入的 `Software` 属性随版本/路径变化 —— 影响：不同机器生成的语料 CHECKSUMS 不完全一致；建议 M3 固定写入值或纳入规范化。
6. **vcpkg 二进制缓存“三份 → 单写者”**：三个 job + warm workflow 均可能写 —— 影响：冗余条目与 409 竞争；建议保留单写者（同 Qt 缓存做法）。
7. **`appimage` job 复用构建产物**：当前与 `linux` job 重复编译 release —— 影响：约 14 s/轮的重复成本；建议用 artifact 传递构建树。
8. **`appimage-gui-smoke.sh` 依赖 X 工具**：当前 GUI 烟测 = offscreen + 产物启动存活，真窗口烟测需 xvfb/xprop —— 影响：CI 未覆盖“窗口真出现”；建议 M3 加 xvfb 真 X 烟测 job。
9. **GitHub Actions 升 v5**（checkout/cache/upload-artifact）：当前 v4 被 runner 强制运行于 Node 24 并告警 —— 影响：未来可能失效；建议 M3 一次性升级。
10. **AppImage 非字节可复现**：squashfs 超级块时间戳/mtime 非确定 —— 影响：同源不可复算 sha256（本机 vs CI 产物 sha256 必然不同）；建议仅承诺结构一致 + 依赖清单一致。
11. **xcb 系统依赖与 `libxcb` 最低版本**：目标机需清单内的 xcb/X11/GL 包与 `libssl3` —— 影响：缺包时启动失败；已由 AppRun 缺库自检 + 包名提示兜底（T18）；建议 README 系统要求继续随产物 `deps.txt` 同步。
12. **Wayland 会话经 XWayland 属预期**：AppRun 不设 `QT_QPA_PLATFORM_PLATFORM_PATH`，真实平台走 `wayland;xcb` 回退 —— 影响：非缺陷；建议在 README「便携模式」补一句说明。
13. **T24（AppRun 有界失败弹窗 + `PP_NO_GUI_POPUP`）收口时未落盘** —— 影响：无终端场景缺友好提示（stderr 日志兜底已在）；建议收口后单独提交并复跑 `appimage` job。

## 9. M1+ 阶段总结（M0 → M1a → M1b → M2）

| 批次 | 范围 | 提交数 | 关键产出 | 收口提交 |
|---|---|---|---|---|
| M0 | 骨架/冻结接口/语料/构建引导 | 19 | 设计文档、任务书、vcpkg 清单 + jpegli overlay、27 fixture 语料 | `6792826` 等（首提交 `75fca98`） |
| M1a | 引擎（core/decode/codecs/pipeline/scheduler/dev harness） | 40 | 8 编码器、色彩管理、Exif 无损重写、`--dev`、金样 9 对、15 冻结头 | `e4fe158` |
| M1b | 界面（三页主窗口/参数表单/地图/编辑器/设置预设/UI 冒烟） | 40 | `--ui-smoke` 走查 8/8、16 冻结头、UI 自验 1000 断言 | `b97eff2` |
| **M2** | **Linux debug 收口 + 发行状态** | **43** | **sanitizer 全清、回归基线、金样 16 对、AppImage + 许可、CI 四 job 全绿** | **`9adf5b8`** |

- **仓库与规模**：<https://github.com/zhang-hz/PhotoPipeline3>（public，账号 `zhang-hz`；首次推送 `ab5a88a`）；累计提交 **143**（`git rev-list --count HEAD`，撰写时 HEAD = `9adf5b8`）。
- **测试规模**：ctest **24**（release-dev）/ 23（release）；金样 **16** 对；单测可执行 **18** 个 + `ui_smoke`/`linkprobe`/`fixtures`/`spike_e`/`spike_f`/`verify_selftest`。
- **冻结契约**：**31 个 `PP-FROZEN` 头**（M1a 15 + M1b 16）+ 1 个结构标记文件（`src/core/format_tables.cpp`），与任务书逐字节一致。
- **发行产物**：
  - 出厂口径 = **CI 产物**（ubuntu-24.04 / glibc ≥ 2.39）：`PhotoPipeline-0.1.0-x86_64.AppImage` **50,944,504 B**，sha256 `21ba18e0480e0a2b5f920518ee51dbd8f41a03719a2aea5fa440d743b8cc4c3b`（artifact `PhotoPipeline-AppImage`，46 条系统 soname）。
  - 本机开发产物（Ubuntu 26.04 / glibc ≥ 2.43，**不作附件**）：**52,464,120 B**，sha256 `faa73d0b54c684ff56b00d48922c0ca918c17c87ef2ca59d96435f018efb091c`（40 条系统 soname）。
- **许可**：GPL-3.0-or-later；第三方 40 port 许可文本 + 本项目 LICENSE 随产物分发；源码 offer = 仓库 URL + 对应 tag。

## 10. 附录

### 10.1 关键文件清单

| 类别 | 文件 |
|---|---|
| 任务书 | `docs/m2-tasks.md`（§1 纪律 / §2 冻结文本 / §3 TODO 处置 / §6 波次 / §8 出口准则 / §9.1–§9.7 落地记录） |
| 报告 | `docs/m2-report.md`（本文件）、`docs/m1-report.md`、`docs/m1b-report.md` |
| 文档草案 | `docs/m2-drafts/{CHANGELOG-0.1.0.md, README-release-section.md, README-dev-tools.md}`；终稿 `CHANGELOG.md`、`README.md`（发行版/系统要求/源码与许可/开发工具章节） |
| 打包/资产 | `tools/make_appimage.sh`、`tools/appimage-check-closure.sh`、`tools/appimage-gui-smoke.sh`、`tools/collect_licenses.sh`、`tools/make_icon.py`、`tools/tls_probe.cpp`、`share/applications/photopipeline.desktop`、`share/icons/hicolor/256x256/apps/photopipeline.png`、`tools/bin/appimagetool-x86_64.AppImage{,.sha512}`、`tools/bin/type2-runtime-x86_64{,.sha512}`、`cmake/version.h.in` |
| CI | `.github/workflows/build-test.yml`、`.github/workflows/warm-cache.yml`、`tools/ci-system-deps.txt`、`tools/ci-install-deps.sh`、`tools/ci-check-deps.sh`、`tools/ci-cache-gc.sh` |
| 验证 | `tools/regression.sh`、`tools/baseline/golden.log`、`tests/golden/smoke/*.json`、`tests/ui_smoke.sh`、`tools/lsan.supp`、`tools/tsan.supp`、`tools/pp_verify.cpp` |
| 证据（不入库） | `.cache/m2-t1/SUMMARY.md`、`.cache/t21-report.md`、`.cache/t21c-report.md`、`.cache/m2-t9-*.log`、`.cache/m2-t2-tsan/**`、`.cache/m2-t22-*.log`、`.cache/ui-review/`、`.cache/ui-smoke-ci/`、`.cache/tmp/t18-appimage-gui.png` |

### 10.2 复现本报告全部验证

```bash
source tools/env.sh
# 0) 两棵树（release 23 条 / release-dev 24 条）
cmake --preset release      -B build/release      -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/release -j
cmake --preset release-dev  -B build/release-dev  -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/release-dev -j
ctest --test-dir build/release --output-on-failure            # 23/23
ctest --test-dir build/release-dev --output-on-failure        # 24/24（含 ui_smoke）
# 1) 金样 16 对 + 引擎注册面 + 校验器自检 + 版本单源
bash tests/golden/smoke.sh build/release-dev                  # SMOKE total=16 pass=16 fail=0
./build/release-dev/pp_linkprobe                              # 9× PROBE OK / PLUGINS / HEIF_ENCODERS
./build/release-dev/pp_verify --selftest                      # 27 probes, 0 unexpected
./build/release-dev/photopipeline --version                   # PhotoPipeline 0.1.0
# 2) 回归基线（双跑零 diff；--update 才重生成）
bash tools/regression.sh build/release-dev && bash tools/regression.sh build/release-dev
# 3) sanitizer（ASan/UBSan + TSan）
cmake --preset dev  -B build/dev  -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/dev -j
ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=suppressions=$PWD/tools/lsan.supp \
  ./build/dev/photopipeline --dev --inputs tests/golden/base --format jpeg --workers 2
cmake --preset tsan -B build/tsan -DVCPKG_MANIFEST_INSTALL=OFF && cmake --build build/tsan -j
TSAN_OPTIONS="halt_on_error=0:suppressions=$PWD/tools/tsan.supp:print_suppressions=1" \
  ctest --test-dir build/tsan --output-on-failure             # 24/24, 0 报告
# 4) UI 冒烟（offscreen 8 张 + 冻结行；真实平台审查集用显式 --shots .cache/ui-review）
QT_QPA_PLATFORM=offscreen ./build/release-dev/photopipeline --ui-smoke \
  --inputs tests/golden/base --shots .cache/ui-smoke-ci       # UI-SMOKE OK shots=8 pages=3
ctest --test-dir build/release-dev -R ui_smoke --output-on-failure
# 5) AppImage（离线打包 + 四项内置烟测 + 闭包门禁 + 依赖覆盖自检）
PP_BUILD_DIR=build/release bash tools/make_appimage.sh dist
bash tools/appimage-check-closure.sh dist/PhotoPipeline.AppDir
tools/appimage-gui-smoke.sh dist/PhotoPipeline-0.1.0-x86_64.AppImage --shot .cache/tmp/appimage-gui.png
bash tools/ci-check-deps.sh build/release
# 6) CI（本地只做 YAML 语法校验；推送后观察 run）
gh run list --workflow=build-test.yml --limit 5
```

---

## 待主对话裁决 / 补录事项

1. **TODO 口径**：任务下发口径曾写“修复 **19**”，而 §3 表与 §9 记录一致为 **修复 17 + 重分类 M3 2 = 非弃权 19**；本报告采信 **17/2/11**（可 grep 复核）。若主对话另有口径请裁定。
2. **T9 敏感性注入**：任务书 §8 与 §9.2 写“注入一行 → diff 一行”，而现存证据（`/tmp/t9-inject.out`）的注入是**移除 1 个语料输入**（27→26）→ `1620 vs 1636 行`、`DIFF FOUND (384 lines)`。本报告按实测写“敏感性成立”。若需严格对齐“一行”口径，请补一次日志单行注入实验或修订 §8/§9.2 表述。
3. **§2.8 PQ/HLG 附注口径**：任务提示中的“**25.6/255**”与任务书 §2.8 订正注一致（Max/RMS error 0.1004），但仓库内**无该数字的原始日志**；本报告同时给出可复算证据（`.cache/tmp/m2-t6/ev2|ev3` 逐像素 max|Δ|=0）。另 gray 组实测恒定 **26/255** 差异（疑似灰度升维路径），归因未在源码/日志中固化 → 若需写进 §2.8 请裁定归因或补测。
4. **T24 未落盘**：AppRun 弹窗超时 + `PP_NO_GUI_POPUP`（`/tmp/pp-t24` 在跑）在本报告撰写时未提交；本报告**不记为已实装**。落盘后需补跑 `appimage` job 并在 §2.9/本报告 §8-13 更新。
5. **T20/T23 编号未使用**（§9.1/§9.6 记录一致），本报告按“未使用”列示；若主对话另有分配请补录。
6. **CI 覆盖边界**：撰写时 `9adf5b8`（T22）**未推送**，CI 全绿结论对应 `a63acff`；T25 文档提交 + T22 + 本报告（T26）推送后建议再跑一轮并回填 run id。
7. **产物 sha256 双份**：本机 52,464,120 B / `faa73d0b…`（glibc ≥ 2.43，仅开发验证）与 CI 50,944,504 B / `21ba18e0…`（glibc ≥ 2.39，出厂附件）**必然不同**（AppImage 非字节可复现）——若发行说明要写单一 sha256，请确认取 CI 一份。
8. **`libiconv` 许可缺失**：真实 port 但 vcpkg 未提供许可文本（打包只告警）。本报告按“列名处置”记录；是否需要在 README 发行章节显式声明请裁定。
