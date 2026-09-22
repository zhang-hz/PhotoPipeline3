# M2 收口报告（Linux debug 收口 + 发行状态）

- **状态**：SUCCESS —— 出口准则 §8 十条全部满足；R15（CI 真实首跑）闭合；**T27 终轮 CI 四 job 全绿**（run `35521800777`，head `d1b5fb7`），本报告全部 CI 数字已按该轮实况回填。
- **执行依据**：`docs/m2-tasks.md` v1.0（`ab5a88a`）+ §9.1–§9.7 落地记录（T25 订正 + T27 终轮回填）。
- **周期**：任务书 `ab5a88a`（2026-09-20）→ 报告 `9adf5b8`（T26）→ **T27 终轮 `d1b5fb7`**（推送 + CI 取证 + 裁定落盘）；基线 = M1b 收口 `b97eff2`；**计数稳定锚点 = 发行提交 `4fb64c3`（tag `v0.1.0`）**。
- **批次提交（冻结口径，T30）**：**截至发行提交 `4fb64c3`（tag `v0.1.0`）：累计提交 155、M2 批次提交 55**（`git rev-list --count 4fb64c3` = 155；`git rev-list --count ab5a88a^..4fb64c3` = 55）。全部提交追加式，无 amend/rebase/reset。**不再按会随文档轮漂移的 HEAD 记数**：`4fb64c3` 之后仅 T29/T30 文档提交，不影响发行产物与 tag。
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
| 收口 | T24 | AppRun 有界失败弹窗 + `PP_NO_GUI_POPUP` | `aeba18c` | **已落盘**：修复前无界弹窗挂死 **25 s**（不自行退出）→ 修复后 `zenity --timeout=60` / `xmessage -timeout 60` **61.8 s 自行退出**；`PP_NO_GUI_POPUP=1` **1.31 s 零弹窗**（stderr 与日志照旧写）；`kdialog` 不入链（无超时参数）—— 与 `docs/m2-tasks.md` §2.9 落地口径④逐条一致（T27 解包产物内 AppRun 复核，见 §5⑥/§6.5） |
| 收口 | T25 | 文档收口（`docs/m2-tasks.md` §9 订正 + README/CHANGELOG 终稿） | `b58a29a` `160d206` | 主对话工作区，非本报告文件域 |
| 收口 | T26 | **本报告**（`docs/m2-report.md`） | `5a946b0` `b98a325` `387de62` | 全部数字附来源 |
| 收口 | T27 | 终轮推送 + CI 验证 + 八项裁定落盘（**本报告 CI 数字取自本轮**） | `fd06bb3` `b3774fa` `d1b5fb7` | run `35521800777` 四 job 全绿；AppRun 缺 `~/.cache` 静默退出缺陷修复（§6.5）；PQ/HLG 原始日志（§3.3）与单行敏感性实验（§5③）补齐 |
| 收口 | T28 | **GitHub Release 发布**（`v0.1.0`；无仓库代码变更） | — | Release <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0>（id 393729954，draft=false / prerelease=false）；唯一 asset `PhotoPipeline-0.1.0-x86_64.AppImage` **50,940,408 B** / sha256 `31f75c48…`，取自 run `35522357702`（head `4fb64c3`，headBranch `main`）；回下载 `cmp` IDENTICAL + 发布件实跑 `--version` = `PhotoPipeline 0.1.0`（§5⑦、§9） |
| 收口 | T29 | 发行记录与文档一致性收尾（**仅文档**：本报告 + `docs/m2-tasks.md` + `README.md` + `CHANGELOG.md`；提交信息 `M2-T29: release record consistency (GitHub Release v0.1.0)`） | — | 出厂口径改以**已发布 Release** 为准（§9）；旧 run 值一律紧邻标注"非发布附件；见 §9 发行口径"；引用口径 = `(run, head, sha256)` 三元组；§8-10 补同 commit 两轮 run 差异（`35522357702` vs `35546035069`） |

编排变更（实际执行，`docs/m2-tasks.md` §6）：`pipeline.cpp` 三方争用串行化 → T13 留 W2、**T5 后移 W3a、T6 后移 W3b**；T4 在 W2 早段独占该文件。**T20/T23 编号未使用**（原书与执行中均无）。**T27–T29 为收口尾段**：T27 = 终轮推送 + CI 取证 + 八项裁定落盘；T28 = 发行发布（GitHub Release `v0.1.0`，无仓库代码变更）；T29 = 发行记录与文档一致性收尾（仅文档，本任务）。

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
- **PQ(16)/HLG(18) 与未列组合维持现状**（仅记日志 `CICP transfer <n> 未支持，按 sRGB 处理`，像素沿用 libjxl/OIIO 合成 ICC）。
- **T27 复测（原始日志已补齐，取代"无原始日志"的说明）**：证据 = `.cache/m2-t27-pq-shift.log`（脚本 `.cache/m2-t27-pq-shift.sh`；测量器源码 `.cache/m2-t27/pq-shift.c`，用 **pipeline 链接的同一 lcms2 静态库**（版本 2190）+ 同一意图 `INTENT_RELATIVE_COLORIMETRIC`（`src/core/colormanager.cpp:39`）+ 16bit 数据型）。完整命令：
  `gcc -O2 -I vcpkg_installed/x64-linux/include -o .cache/tmp/m2-t27/pq-shift .cache/m2-t27/pq-shift.c vcpkg_installed/x64-linux/lib/liblcms2.a -lm`
  → `oiiotool <样本> --iccwrite <源合成 ICC>` → `.cache/tmp/m2-t27/pq-shift <ICC> 1`（中灰补丁 128/255）/ `… <ICC> 1 <in.ppm> <out.ppm>`（整幅）。实测：**PQ 0.5020 → 0.60240（Δ +0.10043 = 25.61/255）**、**HLG 0.5020 → 0.39713（Δ −26.73/255）** —— 与上文"0.6024 / 0.1004 ≈ 25.6/255"逐位吻合（故原数字来源确认，结论不变）。
- **两点口径澄清（T27 新增实测）**：① T6 的 `cicp-*.jxl` / `pq-*.jxl` 样本本体是**同一个 64×64 纯黑白图案**（min 0 / max 255 / avg 127.5），强制转换被裁剪（0→0、255→255）⇒ 中灰位移只能由补丁或全电平 ramp 测得（ramp 原始输出同日志）；② `--color keep` = **像素直通**（与解码逐像素 `idiff` = 0），而 `--color srgb` 的现状输出与上述转换**逐级一致**（256 级 ramp，max 1/255 取整差、mean 6.1e-05）⇒「(16,*)/(18,*) 维持现状」在像素层面的准确含义 = **不安装 CICP 派生 profile、沿用既有 ICC 路径**（M2 不新增像素决策分支）；"按 sRGB 解释（直通）"与"按合成 ICC 转换"两条读法在 256 级 ramp 上的差异上界 = PQ **49/255** / HLG **35/255**。HDR 正确映射（tone mapping）留 M3。
- 其它证据：`.cache/tmp/m2-t6/evlogs/all.log:111/130` 为两条冻结告警原文；ev2/ev3 下同一 64×64 photo 源的“无 CICP PNG”与“(9,16) JXL”转码到 sRGB **逐像素一致（max|Δ|=0）**（该样本为黑白图案，故位移不可见）。
- **gray 组 26/255 = 已知未归因（需 M3 单独测）**：`.cache/tmp/m2-t6/ev4` 的 `pq-gray.png` vs `plain-gray.png` 恒定 `0.50196 → 0.60392`（Δ = 0.10196 = **26/255**，T27 `idiff` 原始输出见同日志）。T27 实测已把该位移与 PQ→sRGB 转换对上号（lcms2 预测 0.60240 vs 实测 0.60392，残差 0.39/255 疑来自 gray→RGB 升维的 profile 构造），但**归因仍待 M3 专项测试**（候选清单 `docs/m2-tasks.md` §9.7 已加条目，本报告 §8-16）。

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

**已裁定（T27，主对话落盘）**：采信 **修复 17 + 重分类 M3 2 + 弃权 11 = 30**（与本表、`docs/m2-tasks.md` §3 落地结果、§9 记录一致）。任务下发口径曾写"修复 **19**"，系把"非弃权 19 = 修复 17 + M3 2"误记为"修复"⇒ **该口径作废**（可 grep 复核：17 项修复均在 §2 表标 `修复`，2 项标 `重分类 TODO(M3)`）。

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
- **敏感性实验 A（语料级注入，T9 原始证据）**：移除 1 个语料输入（27→26）→ `normalized 1620 lines vs baseline 1636 lines` + `DIFF FOUND (384 lines of unified diff)` ⇒ 注入受控变化后 diff 命中**预期形状**（少 16 行槽位 = 该输入 × 8 格式 + 仅元数据），**非预期行 = 0**。任务书 §8-6 原写"注入一行 → diff 一行"与实测不符，**T27 已按实测订正**（`docs/m2-tasks.md` §4 T9 / §8-6 / §9.2）。
- **敏感性实验 B（字面单行注入，T27 补齐）**：证据 = `.cache/m2-t27-single-line.log`。命令：`bash tools/regression.sh build/m2-t22`（复现基线：`normalized 1636 lines vs baseline 1636 lines` + `zero diff`，且两份文件 **sha256 完全相同** `092e3dee…`）→ 复制 `current.log` 后**只改一行**（第 90 行 `ok=23`→`ok=24`）→ `diff tools/baseline/golden.log <mutated>` = **`90c90`：恰好 1 行差异**（`^<` 1 行 / `^>` 1 行；diff 退出码 1）；对照组（未改动副本）diff 为空、退出码 0；第二处注入（第 32 行 `bytes=513`→`bytes=514`，不同行/不同类型）同样 **`32c32`：恰好 1 行** ⇒ 比较器对单行数值变化精确敏感（不是"整段失配"）。

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

- **本地产物（开发验证用，`不作附件`）**：`dist/PhotoPipeline-0.1.0-x86_64.AppImage`，**52,468,216 B**，sha256 `1b01f16511f230d0f24cc941f8e8a8066cc56e89c66cafd18140d54bb71afb96`（T27 重建，含 AppRun 修复；`dist/` 按 §2.9 忽略，不入库）。**出厂口径 = 已发布 Release 附件（`31f75c48…`，其本身即 CI 构建）**，见 §5⑦/§9。
- **闭包门禁 A/B**（`tools/appimage-check-closure.sh`，36 个 ELF：主二进制 + `usr/lib` + 全部插件）：① `[FAIL-A] not found` = 无；② `[FAIL-B] libQt6* 解析到 AppDir 之外` = 无；结论 **PASS**；目标机系统要求 **本机 40 条 soname / CI 46 条**（差异来源 = **打包基础镜像不同**：本机 Ubuntu 26.04 vs CI ubuntu-24.04 的依赖闭包差异；两值并列即为口径，非笔误），本机清单写入 `dist/PhotoPipeline-0.1.0-deps.txt`。
- **AppRun 日志目录鲁棒性（T27 修复，目标机三情形实测）**：证据 = `.cache/m2-t27-apprun-fix.log`。① `HOME=/tmp/pp-nohome`（无 `.cache`）→ `--version` 输出 `PhotoPipeline 0.1.0`、**exit 0**（修复前 rc=2 静默退出），并自动建出 `.cache/`；② `HOME=/proc`（不可写）→ 仍 **exit 0**（探测失败即定死 `/dev/null`，仅一行非致命 stderr）；③ 正常 `HOME` → 日志照常写入（`--version` 路径截断日志使 sha256 变化；无 DISPLAY 启动失败时写入 **1059 B** 真实 stderr）。解包产物内 AppRun 同时复核 T24 四项（`zenity --timeout=60` / `xmessage -timeout 60` / 无 `kdialog` 弹窗调用 / `PP_NO_GUI_POPUP=1` 直接返回）逐条在位。修复提交 `b3774fa`，详见 §6.5。
- **随包库 26 个 `.so`**：7 × `libQt6*`（Core/DBus/Gui/Network/Svg/Widgets/XcbQpa）+ 3 × `libicu*` + `libjpeg.so.62` + 13 × `libxcb-*` + `libxkbcommon-x11.so.0` + `libX11-xcb.so.1`；插件 `qt-plugins/` = platforms(offscreen,xcb) + imageformats/svg/gif/ico/jpeg + iconengines + tls(openssl,certonly)。
- **“绝不放行”清单**（`NEVER_BUNDLE`，命中即硬失败）：GL/EGL/GLX 驱动栈（libGL/libEGL/libGLX/libOpenGL/libGLdispatch/libdrm/libgbm）、单副本不变式库（libX11/libxcb/libxkbcommon/libXau/libXdmcp/libICE/libSM/libglib-2.0/libdbus-1）、glibc/libstdc++/libgcc 家族；`libQt6*` 必须来自 Qt 工具链（逐字节 `cmp` 溯源断言）。
- **TLS 实网探针**（`tools/tls_probe.cpp`，用 AppDir 内 Qt + `tls/` 插件）：`probe tls=supported` / `probe http_status=200 bytes=6929 png_magic=yes` / `TLS-PROBE OK`（`/tmp/tls-debug.log`）——修复前同一 AppImage 启动即 `qt.network.ssl: No functional TLS backend was found`（`/tmp/t11-appimage-gui.log`）。
- **许可 + 离线打包**：产物内 `usr/share/licenses/` = **40 个 port `copyright`** + `PhotoPipeline/LICENSE`（`collect_licenses: 第三方条目 40 个`；`libiconv` 为真实 port 但 vcpkg 未提供许可文本 → 打包只告警。**处置口径与 README「许可」章节同一句**：AppImage 内 `libiconv` 未随附许可文本：该 port 在 x86_64-linux 不产出库文件，`iconv*` 由系统 glibc 提供，本发行未分发其代码 —— 本报告 §8-15 引用同句）；appimagetool + type2 runtime 均入库（`tools/bin/*` + `.sha512`），`--runtime-file` 固定，校验失败 `exit 2` **不静默回退网络**，`APPIMAGE_EXTRACT_AND_RUN=1` 免 FUSE。
- **GUI 真机窗口 + 截图**（`tools/appimage-gui-smoke.sh`，T18）：1 s 检出窗口，`WM_NAME = "PhotoPipeline"`、`WM_CLASS = photopipeline`、`Map State: IsViewable`、几何 **2560×1600**；截图 `.cache/tmp/t18-appimage-gui.png` **231,759 B / sha256 2536718ff409ea91…**；结论 `PASS（进程存活 + 窗口存在）`。便携模式语义不变：挂载点只读 → 自动回退 `$XDG_DATA_HOME/PhotoPipeline`。

### ⑦ CI

- **R15 闭合**：首跑 run `35512612876`（1m6s，红）→ 连续 7 轮红灯（§4.2）→ **首次四 job 全绿 run `35520386390`**（`linux` 1m52s / `appimage` 2m4s / `ui-smoke` 2m21s / `cache-gc` 7s）。**首次在 CI 取得冻结行** = run `35520031504` 的 `ui-smoke` job（`UI-SMOKE OK shots=8 pages=3` + ctest 1/1）。**T27 终轮 = run `35521800777`（head `d1b5fb7`，2026-09-20T16:09:20Z→16:11:28Z）：四 job 全绿 —— `linux` 1m32s / `ui-smoke` 1m45s / `appimage` 1m37s / `cache-gc` 9s**；取证原文 = `ui-smoke` job 的冻结行 `UI-SMOKE OK shots=8 pages=3`（ctest `1/1`，5.85 s）+ `linux` job 的 `100% tests passed, 0 tests failed out of 23`（2.17 s）与 `SMOKE total=16 pass=16 fail=0`。
- **ccache**：热缓存轮 `Cacheable calls 856/856 (100.0%)`、`Hits 770/856 (89.95%)`、`Misses 86/856 (10.05%)`（run `35521800777`；首轮 326/326、Hits 241/326 = 73.93%、direct 228/241 = 94.61%），缓存占用 0.24%（上限 2 GB）。
- **vcpkg**：冷装 ≈ **34 min** → 热缓存 `Restored 41 package(s) … in 5.4 s`，configure 步 **3.2–14.3 s**（run `35521800777` 的 release-dev configure 3.2 s；run `35520615242` linux 14.3 s / appimage 11.2 s）；release build 111.2 s → 17.7 s。
- **依赖覆盖自检 + 拓扑**：清单 64 包（T19 首版 57 + 首轮闭包差异 7）；soname 72（随包/loader 11、需系统 61）；**已覆盖 61/61、未覆盖 0**；负向测试删 3 包精确报 3 项。三构建 job（`linux` 含 ctest release + 金样 16 / `ui-smoke` / `appimage`）+ `cache-gc`（`needs: [linux, ui-smoke, appimage]`）+ concurrency(cancel-in-progress) + 每 job timeout 60 min + 每 job `$GITHUB_STEP_SUMMARY`；`warm-cache.yml`（`workflow_dispatch` + 每周日 03:17 UTC）按同一 key 方案播种（run `35520773894`，1m47s）。
- **AppImage artifact（T27 终轮；非发布附件，见 §9 发行口径）**：`PhotoPipeline-AppImage`（zip **50,325,830 B**，artifact ID `10609041248`，run `35521800777`）；产物指纹 `PhotoPipeline-0.1.0-x86_64.AppImage` **50,940,408 B**、sha256 **`514ecf44658e428008b2db6fd970270075a5aee7510e9c1bd280fc0d1b26a244`**、目标机系统要求 **46 条 soname**（glibc 基线 ≥ 2.39）；产物启动烟测（`--version` → `PhotoPipeline 0.1.0` + offscreen 6 s 存活）**PASS**。（上一轮 a63acff 的旧值 50,944,504 B / `21ba18e0…` 已作废：AppImage 非字节可复现，且 T27 改了 AppRun。）
- **发行发布（T28）**：**GitHub Release 已发布** —— <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0>（tag `v0.1.0`，release id 393729954，`draft=false` / `prerelease=false`，name `PhotoPipeline 0.1.0`）；**唯一 asset** = `PhotoPipeline-0.1.0-x86_64.AppImage`，**50,940,408 B**，sha256 **`31f75c48052be34a2bf54508ea9df52fdc6331b0da1205c6359147566756e60b`**；取件来源 = run `35522357702`（head `4fb64c3`，headBranch `main`，四 job 全 success）的 `PhotoPipeline-AppImage` artifact（zip 50,325,837 B / digest `6442ef17…`）。**发布取证**：附件回下载后与上传件 `cmp` = **IDENTICAL**（sha256 复算一致）+ 发布件实跑 `--version` → `PhotoPipeline 0.1.0`。**引用口径**：同一 commit 的多次 run 产物指纹各异（§8-10）⇒ 任何产物引用一律带 **(run id, head sha, sha256) 三元组**。
- **推送与覆盖边界（T27 闭合；计数锚点见 §1/§9）**：T22 之后的全部提交（T24/T25/T26/T27）**已推送**，T27 轮 `origin/main` = `d1b5fb7`；上表 CI 结论**即该轮实况**（run `35521800777`，head = 推送后的树），取代此前"撰写时未推送、结论对应 `a63acff`"的过渡说明。**发行提交 `4fb64c3` 与后续文档提交均已推送**（`origin/main` 最终状态见 §5⑦ T30 终轮记录）。同一轮之前 `run 35521513855` 曾红在 `appimage` job（产物缺陷，非环境）→ 修复见 §6.5。
- **引用口径**：CI 产物指纹一律以 **(run id, head sha, sha256) 三元组**引用。**发行件 = run `35522357702` / head `4fb64c3` / `31f75c48…`**（§9 出厂口径）。历史轮次示例（**非发布附件；见 §9 发行口径**）：run `35521800777` / head `d1b5fb7` / `514ecf44…`（T27 终轮，**最后一次影响产物内容**的提交）与紧随其后的文档提交轮（run `35522192917` / head `680bb92`，四 job 同样全绿：`linux` 1m23s / `ui-smoke` 2m03s / `appimage` 1m36s / `cache-gc` 5s，冻结行与 `SMOKE total=16 pass=16 fail=0` 均再次取得）产出 sha256 `50003819…` —— 源码相同、仅文档差异即得不同指纹，故任何产物引用必须带 run/head/sha256（§8-10）。

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

### 6.5 T27：AppRun 在缺 `~/.cache` 的账户上静默退出（CI 抓到 → 主对话授权修复）

- **发现（CI 红，run `35521513855` 的 `appimage` job）**：产物启动烟测第 ① 步 `AppImage --version` **退出码 2**，日志仅一行
  `AppRun: 89: cannot create /home/runner/.cache/PhotoPipeline-appimage.log: Directory nonexistent`。
  本机复现：`HOME=/tmp/pp-nohome APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen ./dist/PhotoPipeline-0.1.0-x86_64.AppImage --version` → 同一条报错 + **rc=2**（修复前）。
- **根因**：`tools/make_appimage.sh` 生成的 AppRun 第 89 行 `: >"$LOG" 2>/dev/null || LOG=/dev/null` —— `:` 是 POSIX **特殊内建**，dash（`/bin/sh -> dash`）下重定向失败**直接终止 shell（退出码 2）**，`||` 兜底永不执行。该行潜伏自 T18（`0219105`），仅在"目标账户无 `~/.cache`"时触发（此前几轮 CI 恰好存在该目录，故一直未被发现）。
- **影响**：这正是 T18 要消灭的"双击无响应"场景 —— 无 `~/.cache` 的账户上双击产物 = **静默退出**（无窗口、无日志）。
- **裁定与修复**：主对话授权把文件域扩到 `tools/make_appimage.sh`（仅 AppRun 生成段）：日志目录改为「**先建目录、再探可写**」，写探测改用**非特殊内建** `printf`，任一失败即把 `LOG` 定死 `/dev/null`，且**绝不允许**写日志失败终止 AppRun（`b3774fa`）。同一轮**删除** CI 里临时预建 `$HOME/.cache` 的绕行（`fd06bb3` → `d1b5fb7`），保留"干净目标机"语义作为回归守卫。
- **复测（三类目标机情形，证据 `.cache/m2-t27-apprun-fix.log`）**：① `HOME=/tmp/pp-nohome`（无 `.cache`）→ `PhotoPipeline 0.1.0`、**exit 0**，并自动建出 `.cache/`；② `HOME=/proc`（不可写）→ **exit 0**（回退 `/dev/null`，仅一行非致命 stderr）；③ 正常 `HOME` → 日志写入 **1059 B** 真实 stderr（无 DISPLAY 启动失败路径），`--version` 路径截断日志使 sha256 变化。
- **CI 复验**：run `35521800777`（head `d1b5fb7`）`appimage` job **绿**（1m37s），产物 `50,940,408 B` / sha256 `514ecf44…`（历史轮次值，**非发布附件；见 §9 发行口径**）。

## 7. 出口准则核对表（§8 十条）

| # | 准则 | 证据 | 结果 |
|---|---|---|---|
| 1 | 金样断言级 ≥ 11 对全绿（本地） | `SMOKE total=16 pass=16 fail=0`（本地 + CI linux job） | ✅ |
| 2 | ASan/UBSan 全语料矩阵干净；TSan 并发路径干净 | 144 日志 0 命中 + 通道活性双向对照 + live heap 四档相等；TSan 0 报告 + 22 条论证规则 + 阳性对照 | ✅ |
| 3 | offscreen `ui_smoke` 绿（本地 + CI job） | 本地 ctest 24/24；CI `ui-smoke` job 全绿并取到 `UI-SMOKE OK shots=8 pages=3` | ✅ |
| 4 | AppImage 产出 + 烟测三断言绿（本地 + CI job + artifact） | 本地四项内置烟测 + 闭包门禁 PASS（**52,468,216 B**，T27 重建）；CI `appimage` job 绿（1m37s）+ `PhotoPipeline-AppImage` artifact（产物 **50,940,408 B** / sha256 `514ecf44…`，T27 轮次值，**非发布附件；见 §9 发行口径**）；发行件 = Release 附件 `31f75c48…`（§5⑦/§9） | ✅ |
| 5 | `grep -rn "TODO(M2)" src/ tests/ tools/` 归零 | 实测无输出（退出码 1）；30 项全处置（17 修复 + 2 M3 + 11 弃权） | ✅ |
| 6 | 回归基线入库，双跑零 diff | `tools/baseline/golden.log` 1636 行入库；连跑零 diff；注入敏感性成立 | ✅ |
| 7 | CI 全绿（R15 闭合） | 首绿 run `35520386390`；**终轮 run `35521800777`（head `d1b5fb7`）四 job 全 success**（`linux` 1m32s / `ui-smoke` 1m45s / `appimage` 1m37s / `cache-gc` 9s） | ✅ |
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
10. **AppImage 非字节可复现**：squashfs 超级块时间戳/mtime 非确定 —— 影响：同源不可复算 sha256（本机 vs CI 产物 sha256 必然不同；T27 实测同一源码的相邻两轮 CI 产物亦不同：`514ecf44…` vs `50003819…`；**T28 发行前后又实测同一 commit `4fb64c3` 的两轮 run 产物亦不同：run `35522357702`（headBranch `main`）vs run `35546035069`（headBranch `v0.1.0`），两轮四 job 全绿但 artifact zip digest/体积各异（`6442ef17…`/50,325,837 B vs `9ff3430e…`/50,325,836 B），故产物引用一律带三元组**；以上均为历史轮次值，**非发布附件；见 §9 发行口径**）；建议仅承诺结构一致 + 依赖清单一致，且**产物引用一律带 (run id, head sha, sha256) 三元组**（§5⑦）。
11. **xcb 系统依赖与 `libxcb` 最低版本**：目标机需清单内的 xcb/X11/GL 包与 `libssl3` —— 影响：缺包时启动失败；已由 AppRun 缺库自检 + 包名提示兜底（T18）；建议 README 系统要求继续随产物 `deps.txt` 同步。
12. **Wayland 会话经 XWayland 属预期**：AppRun 不设 `QT_QPA_PLATFORM_PLATFORM_PATH`，真实平台走 `wayland;xcb` 回退 —— 影响：非缺陷；建议在 README「便携模式」补一句说明。
13. ~~**T24（AppRun 有界失败弹窗 + `PP_NO_GUI_POPUP`）收口时未落盘**~~ —— **已修复/已落盘**：`aeba18c`（弹窗超时 60 s、`kdialog` 不入链、`PP_NO_GUI_POPUP=1` 零弹窗），T27 已解包产物内 AppRun 逐条复核与 `docs/m2-tasks.md` §2.9 落地口径④一致（§5⑥）。
14. **AppImage 在缺 `~/.cache` / HOME 不可写账户上的启动鲁棒性** —— 修复前双击 = 静默退出（rc=2）—— **T27 已修**（AppRun 自建日志目录 + 不可写回退 `/dev/null`，三情形实测 exit 0，`b3774fa`，§6.5）。残留：`HOME` 不可写时会多一行非致命 stderr 提示（无害）。
15. **`libiconv` port 无随附许可文本**（打包期只告警）—— 影响：许可清单少一条；**处置 = 不改动，理由与 README「许可」章节同一句**：AppImage 内 `libiconv` 未随附许可文本：该 port 在 x86_64-linux 不产出库文件，`iconv*` 由系统 glibc 提供，本发行未分发其代码。
16. **gray 路径 26/255 位移归因未定**：灰度 PQ 源升维后 `pq-gray` 与 `plain-gray` 输出恒定差 26/255（T27 已把该位移与 PQ→sRGB 转换对上号：lcms2 预测 0.60240 vs 实测 0.60392，残差 0.39/255 疑来自 gray→RGB 的 profile 构造）—— 影响：灰度 HDR 源的像素语义未定；建议 M3 专项测试（`docs/m2-tasks.md` §9.7 候选）。

## 9. M1+ 阶段总结（M0 → M1a → M1b → M2）

| 批次 | 范围 | 提交数 | 关键产出 | 收口提交 |
|---|---|---|---|---|
| M0 | 骨架/冻结接口/语料/构建引导 | 19 | 设计文档、任务书、vcpkg 清单 + jpegli overlay、27 fixture 语料 | `6792826` 等（首提交 `75fca98`） |
| M1a | 引擎（core/decode/codecs/pipeline/scheduler/dev harness） | 40 | 8 编码器、色彩管理、Exif 无损重写、`--dev`、金样 9 对、15 冻结头 | `e4fe158` |
| M1b | 界面（三页主窗口/参数表单/地图/编辑器/设置预设/UI 冒烟） | 40 | `--ui-smoke` 走查 8/8、16 冻结头、UI 自验 1000 断言 | `b97eff2` |
| **M2** | **Linux debug 收口 + 发行状态** | **55** | **sanitizer 全清、回归基线、金样 16 对、AppImage + 许可、CI 四 job 全绿、GitHub Release v0.1.0** | **`4fb64c3`**（**发行提交**，tag `v0.1.0`；T24–T27 追加轮，报告轮 `9adf5b8`；其后仅 T29/T30 文档提交） |

> **计数口径（T30 冻结）**：**M2 行 = 55**，以**发行提交 `4fb64c3`（tag `v0.1.0`）**为锚点实测（`git rev-list --count ab5a88a^..4fb64c3`）。M0/M1a/M1b 行沿用各批报告轮的记录值（其记数边界与本锚点不同）；按同一锚点实测的边界计数为 M0 **16** / M1a **44** / M1b **40** / M2 **55**，合计 **155**（= `git rev-list --count 4fb64c3`）。本批不追溯改写早期报告，差异在此标注。

- **仓库与规模**：<https://github.com/zhang-hz/PhotoPipeline3>（public，账号 `zhang-hz`；首次推送 `ab5a88a`）；**截至发行提交 `4fb64c3`（tag `v0.1.0`）：累计提交 155、M2 批次提交 55**（`git rev-list --count 4fb64c3` = 155；`git rev-list --count ab5a88a^..4fb64c3` = 55；不再以 HEAD 记数，`4fb64c3` 之后仅文档提交，不影响发行产物与 tag）。
- **测试规模**：ctest **24**（release-dev）/ 23（release）；金样 **16** 对；单测可执行 **18** 个 + `ui_smoke`/`linkprobe`/`fixtures`/`spike_e`/`spike_f`/`verify_selftest`。
- **冻结契约**：**31 个 `PP-FROZEN` 头**（M1a 15 + M1b 16）+ 1 个结构标记文件（`src/core/format_tables.cpp`），与任务书逐字节一致。
- **发行产物**（AppImage 非字节可复现，故任何后续提交都会产出不同 sha256 ⇒ 引用时必须连 `(run id, head sha, sha256)` 一起引用）：
  - **出厂口径 = 已发布 GitHub Release（T28）**：Release <https://github.com/zhang-hz/PhotoPipeline3/releases/tag/v0.1.0>（tag `v0.1.0` → commit `4fb64c3`；release id 393729954，`draft=false` / `prerelease=false`，name `PhotoPipeline 0.1.0`）；唯一 asset = `PhotoPipeline-0.1.0-x86_64.AppImage`，**50,940,408 B**（`50940408`），sha256 **`31f75c48052be34a2bf54508ea9df52fdc6331b0da1205c6359147566756e60b`**；来源 run **`35522357702`**（head **`4fb64c3`**，headBranch `main`，ubuntu-24.04 / glibc ≥ 2.39）的 `PhotoPipeline-AppImage` artifact（zip 50,325,837 B / digest `6442ef17…`，**46 条**系统 soname）；回下载 `cmp` IDENTICAL + 发布件实跑 `--version` = `PhotoPipeline 0.1.0`（§5⑦）。校验命令：`gh release view v0.1.0 --json url,assets,tagName`（体积/sha256 与本节逐字一致）。
  - 历史轮次 CI 产物（**非发布附件；见 §9 发行口径**）：run `35521800777` / head `d1b5fb7` / `514ecf44…`（T27 终轮，artifact zip 50,325,830 B / ID `10609041248`）与 run `35522192917` / head `680bb92` / `50003819…`；同 commit 两轮 run 亦不同：run `35522357702`（`main`）vs run `35546035069`（head `v0.1.0`，artifact zip 50,325,836 B / digest `9ff3430e…`）（§8-10）。
  - 本机开发产物（Ubuntu 26.04 / glibc ≥ 2.43，**仅开发验证、不作附件**）：**52,468,216 B**，sha256 `1b01f16511f230d0f24cc941f8e8a8066cc56e89c66cafd18140d54bb71afb96`（**40 条**系统 soname；条数差异来源 = 打包基础镜像闭包差异，本机 26.04 vs CI 24.04）。
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
| 证据（不入库） | `.cache/m2-t1/SUMMARY.md`、`.cache/t21-report.md`、`.cache/t21c-report.md`、`.cache/m2-t9-*.log`、`.cache/m2-t2-tsan/**`、`.cache/m2-t22-*.log`、`.cache/ui-review/`、`.cache/ui-smoke-ci/`、`.cache/tmp/t18-appimage-gui.png`、**T27**：`.cache/m2-t27-apprun-fix.log`、`.cache/m2-t27-single-line.log`、`.cache/m2-t27-pq-shift.log`、`.cache/m2-t27-ci-watch2.log`、`.cache/m2-t27-*.sh`、`.cache/tmp/m2-t27/pq-shift.c` |

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
# 7) T27 三项新证据（脚本 + 原文日志见 .cache/m2-t27-*.log；日志内 `+ ` 行即完整命令）
bash .cache/m2-t27-apprun-fix.sh     # AppRun 三情形：无 ~/.cache / HOME 不可写 / 正常 HOME（均须 exit 0）
bash .cache/m2-t27-single-line.sh    # 基线复现（sha256 相等）+ 单行注入 → diff 恰好 1 行（90c90、32c32）
bash .cache/m2-t27-pq-shift.sh       # PQ/HLG 强制 sRGB 位移：中灰补丁 + 全电平 ramp + gray 组（lcms2 2190）
```

---

## 八项裁定落盘记录（T27 终轮）

> 8 项裁决全部于本轮落盘；每项给出「落点 + 证据」。原清单已整体闭合，无遗留裁决项。

| # | 裁定 | 落点 | 证据 / 实测 |
|---|---|---|---|
| 1 | TODO 口径 = **修复 17 + 重分类 M3 2 + 弃权 11 = 30** | 本报告 §3.5 新增"已裁定"段（并留 §2 表/§3.5 表）；`docs/m2-tasks.md` §3 一致 | `grep -rn "TODO(M2)" src/ tests/ tools/` = 0（无输出、退出码 1）；"修复 19"口径作废 |
| 2 | T9 敏感性改为准确表述（"注入受控变化 → diff 命中预期形状、非预期行 = 0"）并**补字面单行实验** | `docs/m2-tasks.md` §4 T9 / §8-6 / §9.2；本报告 §5③ | 实验 A（语料级）：27→26 → `normalized 1620 vs 1636` 行 + `DIFF FOUND (384 lines)`；实验 B（单行）：`.cache/m2-t27-single-line.log` —— 第 90 行 `ok=23→24` ⇒ `90c90`；第 32 行 `bytes=513→514` ⇒ `32c32`（均恰好 1 行；基线两份 sha256 相同 `092e3dee…`） |
| 3 | §2.8 PQ/HLG **补原始日志**；gray 组 26/255 标为**已知未归因**并入 M3 候选 | 本报告 §3.3 + §8-16；`docs/m2-tasks.md` §2.8 订正注 + §9.7 | `.cache/m2-t27-pq-shift.log`（lcms2 **2190** = pipeline 同库 + `INTENT_RELATIVE_COLORIMETRIC`）：PQ 0.5020 → **0.60240**（Δ +0.10043 = **25.61/255**）、HLG 0.5020 → **0.39713**（Δ −26.73/255）；ramp 四源对照；gray 组 0.50196 → 0.60392 |
| 4 | T24 状态回填为**已落盘** | 本报告 §2 表 T24 行 + §5⑥ + §8-13；`docs/m2-tasks.md` §2.9④；`CHANGELOG.md` | `aeba18c`；要点：修复前挂死 **25 s** → `--timeout=60` 后 **61.8 s 自行退出**；`PP_NO_GUI_POPUP=1` **1.31 s 零弹窗**；解包产物内 AppRun 逐条复核（zenity 60 s / xmessage 60 s / 无 kdialog / 开关在位） |
| 5 | T20/T23 编号未使用 | `docs/m2-tasks.md` §6（新增同句）+ 本报告 §2 编排变更段（原有） | 两处均写明"**T20/T23 编号未使用**" |
| 6 | CI 覆盖边界回填（终轮 run/head/四 job 耗时/artifact sha256），删"撰写时未推送"过渡说明 | 本报告 §5⑦、§7-7、§9；`docs/m2-tasks.md` §8-7 | run **`35521800777`**（head **`d1b5fb7`**）：`linux` 1m32s / `ui-smoke` 1m45s / `appimage` 1m37s / `cache-gc` 9s；原文 `UI-SMOKE OK shots=8 pages=3` + `100% tests passed … out of 23` + `SMOKE total=16 pass=16 fail=0`；artifact 产物 **50,940,408 B** / sha256 **`514ecf44…`**（历史轮次值，**非发布附件；见 §9 发行口径**） |
| 7 | 发行 sha256 **单一化 = CI 产物**；本机产物只作开发验证（标注不作附件） | 本报告 §5⑥（本机）/§5⑦（CI + T28 发行）/§9；README「发行版（AppImage）」（Release 附件优先，sha256 前缀 + 指向 Release 页）；`CHANGELOG.md`（sha256 前 16 位 + 指向 Release 页） | **T28 细化口径：出厂 = 已发布 Release 附件 `31f75c48052be34a2bf54508ea9df52fdc6331b0da1205c6359147566756e60b`（源自 CI run `35522357702` / head `4fb64c3`）**；本机 = `1b01f165…`（"不作附件"）；T27 轮次 CI 值 `514ecf44…` 及 `680bb92` 轮 `50003819…` 均为历史轮次值，**非发布附件；见 §9 发行口径**；文档侧第三份 sha256 仍不存在 |
| 8 | `libiconv` 声明 | `README.md` 许可章节（原有，T27 逐字核对）；本报告 §5⑥ + §8-15 **同句引用** | "AppImage 内 `libiconv` 未随附许可文本：该 port 在 x86_64-linux 不产出库文件，`iconv*` 由系统 glibc 提供，本发行未分发其代码" |
| 9 | 附带修正：本机 40 / CI 46 条 soname 的差异来源 | 本报告 §5⑥ + §9（统一为"本机 40 / CI 46"两值并列） | 差异来源 = 打包基础镜像闭包不同（本机 Ubuntu 26.04 vs CI ubuntu-24.04），非笔误 |

**本轮额外发现并修复（八项之外，主对话逐项授权）**：AppRun 缺 `~/.cache` 时静默退出（§6.5；`b3774fa` 修复 + `d1b5fb7` 删除 CI 绕行），CI 复验绿。

**仍未闭合、留给 M3 的实质问题**（非本批出口）：HDR（PQ/HLG）正确映射（tone mapping）；gray 路径 26/255 归因；Windows bring-up；14 个冻结头 SPDX 补标 —— 详见 `docs/m2-tasks.md` §9.7。
