# M12 裁定记录（0.4.0 正式版发行批）

> **体例**：`# | 裁定 | 理由 | 实施归属`。每条都是**发行批开发期裁定**；发行动作（版本号/commit/tag/Release）由 Lead 执行。
> **基线**：HEAD `26d3554`（M10-DOCS）+ 工作树 M11 未提交批（`docs/m11-report.md` / `docs/m11-adjudications.md`）；
> 本批 = M11 批 + 用户 5 条新反馈（工厂预设外部化 / 历史内建改名 default+删除 / HEIF Apple CRF=20 / 无「未选择」态 /
> JPEG default 自适应距离策略）+ CI 优化 → **发布 0.4.0 正式版（tag annotated `v0.4.0` → `1b6fdbc`）**。
> 任务真源 = `build/m12-brief.md`（gitignored，Lead 锁定）；逐条变更见 [CHANGELOG.md](../CHANGELOG.md) `[0.4.0]`；
> 读数/发行指纹见 [docs/0.4.0-report.md](0.4.0-report.md)（**已按 0.4.0 发布回填**）；CI 诊断真源
> `build/m12-ci/report.md`（835 行/22 节）。
> 本批**未改动任何 PP-FROZEN 冻结接口**；`core/format_preset.h` 不在 PP-FROZEN 清单但属核心契约，改动按「加性/可检索」登记（A1/A4）。
> 裁定共 **A1–A13**（A10 预算校准 / A11 基线重播种 / A12 sanitizer scoping / A13 hwy 尝试回退 + 两项产品债）；
> 文末另附 **附 1**（需求 3 根因链）与 **附 2**（UI 探针机械换锚 + 新增）。

| # | 裁定 | 理由 | 实施归属 |
|---|---|---|---|
| **A1** | **出厂格式预设外部化**：出厂预设从 C++ 编译内置（`builtin_format_presets()`）改为**仓库 JSON 数据文件 `presets/formats/**`（11 个，单一真源）**，随发行包分发（zip 顶层 `PhotoPipeline/presets/formats/**`、AppImage `usr/bin/presets/formats/**`）、首启**按字节复制** seed 到 `<data_dir>/presets/formats/`；新增工厂目录 API（`factory_format_presets_root/dir`）与 seed API（`FormatPresetSeedReport{written,skipped,removed_legacy,failed}`）。**D-8 口径作废** | 用户需求 1 原话：「内建预设是 cpp 编码的吗？为什么这么做？需要人类可直接修改，直接复制」。历史成因（A1 需写清）：0.4.0.alpha1 按 D-8 = 发行包不含 `share/presets/**`，内置库由 C++ 编译进程序、首启幂等 seed 成 `_builtin-<stem>.json` 只读模板；HEIF 默认快照由 libheif 运行时内省生成（避免写死漂移值）。问题 = 人类不可直接改/复制、出厂更新不随包走、C++ 数据与 JSON 双源；JSON 数据文件即真源后可直接 diff/编辑/复制 | task-1（factory-presets）：`src/core/format_preset.{h,cpp}`、`src/ui/format_preset_io.{h,cpp}`、`CMakeLists.txt`（POST_BUILD 复制 presets + `PP_FACTORY_PRESETS_DIR`）、`tools/make_winzip.py`、`tools/make_appimage.sh`、`tests/unit/test_format_presets.cpp`；文档 task-4 |
| **A2** | **旧 `_builtin-*.json` 迁移删除（破坏性，登记）**：首启删除数据目录各格式子目录下所有 `_builtin-*.json`（`removed_legacy` 计数）；`_builtin-` 前缀 / `is_builtin_*` / 只读语义整体删除；删除不弹确认、失败只记日志 | 用户需求 1/2：旧文件是只读模板（`builtin_format_presets()` 的 seed 产物），内容已被 11 条新工厂预设取代；保留会出现双源与「内置示例 ·」「Default/ default」混名。可接受性：文件是历史只读模板（保存被拒），用户自定义内容不可能写在其中；另存的普通名字预设不受影响；历史内容可由 0.4.0.alpha1/alpha2-dev 源码与发布档案回溯。只影响数据目录，发行包/仓库 `presets/` 不含 `_builtin-*` | task-1：seed 迁移清理 + 单测（`removed_legacy>=2`、文件消失、便携模式只清理不复制）；task-4 文档（format-presets §7.4、CHANGELOG 破坏性变更） |
| **A3** | **每格式 `default` + 历史内建改名/删除**：8 格式各一条出厂 `default`（**小写**，文件 stem 与 JSON `name` 均为 `default`）；HEIF 只留 `Apple自适应` + `default`、JXL 只留 `无损归档`/`自适应压缩`/`default`（**历史内建不保留**）；AVIF/WebP/JPEG 历史内建内容即新 `default`；PNG/TIFF/BMP 新增 `default`。合计 **11 条** | 用户需求 2 原话：「把每个的历史内建都该叫 default，jxl heif 直接删除历史内建」。统一语义 = 每个格式有且只有一份默认；去掉「内置示例 · X」与 `Default`（大写）/`default` 混用 | Lead 生成 `presets/formats/**` 内容（11 文件，逐字节核对）；task-1 工厂库单测（8 格式/11 文件/每格式恰有 default/HEIF 恰 2/JXL 恰 3/无 `_builtin-`/无「内置示例 ·」）；task-4 文档 |
| **A4** | **`default` 不可删除但可「保存」覆盖**：删除按钮仅 `stem=="default"` 时置灰 + tooltip「出厂默认预设不可删除：可直接「保存」修改它，或用「另存」复制一份」；I/O 层 `format_preset_delete_file` 防御性拒绝（中文原因）；`保存` 恒可用（= 直接修改 `default.json`）；`另存` 复制（default 时建议名「default 副本」）；**导入不得顶替 `default`**（改名后再导入，要改 default 走「保存」）。名单源 = `kFormatPresetDefaultName = "default"` + `is_default_format_preset_name()`（UI/IO 共用） | 用户需求 4：默认预设必须存在（无「未选择」），但用户要能改默认值。旧「`_builtin-` 只读 ⇒ 保存拒绝」语义与需求相反，整体删除；保护从「只读」改为「只禁删除」；导入顶替会绕过「保存」语义、等于静默覆盖出厂文件 | task-1：`core/format_preset.h` 常量/判定、`format_preset_io.cpp` 删除守卫 + 单测；task-2：`mainwindow_presets.cpp` 按钮禁用/保存可用/导入守卫、`smoke_probes.cpp` `default-protected`/`save-default`；task-4 文档 |
| **A5** | **删除「未选择」态 + `default` 自动载入**：格式预设下拉删除「未选择」项与占位符；格式卡创建/首启自动选中并**静默载入** `default`（失败只 `log_warn` + 注释位中文错误，不弹 modal）；列表空 = 错误态；预设文件消失 → 自愈选回 `default`；**全局预设载入后 detached 态**（combo 仍显示 default、保存禁用、注记说明「值来自全局预设」、重选预设恢复）；删除 S-4/S-5「未选中清自适应会话态」分支 | 用户需求 4 原话：「每个格式默认使用default，不需要显示未选择，也不应该有这个状态。同时未选择时，不显示自适应，逻辑奇怪」。状态机口径见 `docs/state-machine-analysis.md`（S-4/S-5 的「未选择」前提消失） | task-2（ui-defaults）：`page_output.*`、`output_page_build.cpp`、`mainwindow_presets.cpp`、`smoke_probes.cpp`、`tests/ui_smoke.py`；探针 `default-selected`/`no-unselected`/`default-protected`/`save-default`/`png-no-adaptive`；task-4 文档 |
| **A6** | **JPEG `default` 开启自适应 + distance UI 域 0.3→0.1**：`presets/formats/jpeg/default.json` = `quality_mode=distance`、`distance=3.0`、自适应开启，策略 `search distance from current down to 0.1 step 0.1 breakpoint 1 step 1 max_rounds 20`（target 90 / max_rounds 20 / output last / on_metric_unavailable stop）；`src/core/format_tables.cpp` jpegli `distance` `.lo 0.3 → 0.1`、tooltip 同步 [0.1,3.0]（**R-294 修订**） | 用户需求 5 原话：「JPEG的default开启自适应，减少视觉距离优化。1-3时 步长1 0.1-1时步长0.1」。旧 0.3 UI 下限会让策略要求的 0.1–0.3 段不可达 ⇒ 域必须同步放宽（否则策略在后半段被钳死）；库允许仍是 0.0–25.0，UI 收窄口径不变 | Lead 生成 `presets/formats/jpeg/default.json`；task-2：`format_tables.cpp` + `test_adaptive_exec.cpp`（11 候选逐点）/`test_adaptive_policy.cpp`（parse/render/validate）、`test_params`/`test_param_catalog` 域断言；task-4 文档（`param-catalog.md` §1、`adaptive-policy.md` §6.1/§10、`format-presets.md` §7.1） |
| **A7** | **HEIF「Apple自适应」CRF 默认 = 20（不是 28）**：工厂文件 `params["x265:crf"]=20`；配套 `profile=2`（Main10）、`x265:level-idc=31`（L3.1）、`chroma=420`/`preset=slow`/`tune=ssim`/`x265:aq-mode=3`；`--ui-smoke` 新增探针 `heif-apple-crf` 断言「选中 Apple自适应 后 CRF 控件读数 = 20」。**根因不是探针问题而是产品显示缺陷**（JSON 整数字面量 → int64 → `ParamForm::set_values` 只收同类型 ⇒ 20 被静默跳过、控件停内省默认 28；修复 = `apply_state_to_form` 落卡前归一 `int64↔double`，见**附 1**） | 用户需求 3 原话：「heif Apple自适应 crf默认应该是20，不是28」。口径以工厂文件字节为准（单一真源），并用 GUI 探针钉住"文件 → 控件"链路 | Lead（presets 内容，已就绪）；task-2 探针 + `page_output.cpp` 类型归一修复；task-1 单测（`x265:crf==20.0`、`heif_profile==2`）；task-4 文档（本节 A7 + 附 1） |
| **A8** | **CI 优化（M12；最终结果如实）**：诊断真源 `build/m12-ci/report.md`（835 行/22 节，基线 HEAD `26d3554`）。① **脚本判据 bug（已修）**：nightly asan 的 R-042 env 检查被复制错位、`PP_SAN_TARGETS` 的 `sed 's#\cpp$##'` 转义 bug 致目标名保留 `.cpp`（`ninja: error: unknown target 'pp_test_formkit.cpp'`）、regression-windows 缺 Qt bin `PATH`（`0xC0000135`）、sanitizer ctest 防空跑断言；② **ubsan 金样 19/20 产品 UB（已修）**：根因 = vendored `ssimulacra2` compat 与静态 libjxl 的 `namespace jxl` ODR 冲突（**M12-D25**，`jxl=pp_ssim2_jxl` 宏私有化，vendored 零字节改动）⇒ 最终轮 **ubsan ✅ 9/9 + 金样 20/20 + 0 sanitizer 报告**；③ **sanitizer 性能判据 scoping（A12）**：`PP_SANITIZER_BUILD=1`，只对 sanitizer 构建跳过 fastcolor 吞吐阈值（release/release-dev 判据逐字保留，本机 108.8 MPx/s）；④ **预算校准（A10）**：windows 预算 24/硬顶 30、tidy 预算 22/硬顶 25、整轮 wall-limit 30（release 12/15/3 不变；实测 windows 27.1min / tidy 21.5min / 整轮 32min）；⑤ **发布道**：`release.yml` 新增 presets 产物级断言（probe-winzip/probe-appimage：8 default+11 JSON+无 `_builtin-`；与打包门禁 G 同源；run 41296584097 全绿）。**最终：main-full run 41294175349 全绿 10/10（`main-full gate OK`）；nightly run 41294609588 = FAILURE = ubsan/双平台回归/audit 绿，asan/tsan 红（两项真实产品债，见 A13，0.4.1 修）——不宣称 nightly 通过** | 用户要求「修复后发布 0.4.0，并优化 CI 流程确保全绿」。归因必须诚实：脚本 bug/预算/基线可修且已修；ubsan 是产品 ODR 已修；asan/tsan 残留是**真实产品债**（hwy ODR + ProgressMux 数据竞争），不靠抑制/放宽/删测试造绿 | task-3（ci-green）：`.github/workflows/**`、`tools/ci-*.sh`/`tools/ci-*.py`、`tools/regression.py`、`build/m12-ci/**`；产品侧 M12-D25 由 Lead/集成批；文档 task-4（本文件/CHANGELOG/AGENTS/0.4.0-report） |
| **A9** | **版本 0.4.0 正式发行（标签清空）**：`CMakeLists.txt:26` `PP_VERSION_LABEL ""` ⇒ `--version` = `PhotoPipeline 0.4.0`；数字段 `project(PhotoPipeline VERSION 0.4.0)` 不变；`vcpkg.json` = `0.4.0`（R-122）；CHANGELOG 新增 `## [0.4.0]` 正式发行节（保留 `[0.4.0.alpha1]` 历史节；**不再标 prerelease/未发行**）；tag `v0.4.0` + GitHub Release（prerelease=False）由 Lead 执行。**实测**：tag **annotated `v0.4.0` → `1b6fdbc`**；Release published 2026-11-01T10:24:21Z；4 资产（见报告 §2.5） | 用户要求「直接发布 0.4.0（正式版，去掉 alpha 标签）」。**取代 M11-A11**（「本轮不升版本号/CHANGELOG 以未发行记录」）；M11 批内容整体并入 0.4.0 节的「新增/修复/变更」 | Lead：版本块/commit/tag/Release/CI monitor；task-4：`CHANGELOG.md`、`README.md`、`AGENTS.md`、`docs/0.4.0-report.md`、`docs/m11-*` 加性勘误指针 |
| **A10** | **CI 预算校准（M12；判据零放宽，只动 wall-clock；只改 main-full 相关行）**：main-full **windows 预算 15→24min / 硬顶 20→30min**、**tidy 单列预算 22min / 硬顶 25min**、**整轮 wall-limit 20→30min**；`release` 的 appimage/winzip/publish **12/15/3 不变**、其余流水线/job 仍 20min；真优化 = ① vcpkg binary cache 全命中（configure **908s→38s**，warm-cache 播种）② **M12-D25 产品 ODR 修复**（`namespace jxl` 私有化）。**实测**：main-full run 41294175349 全绿 10/10——windows **1624s（27.1min < 30 ✓）**、tidy **1291s（21.5min < 25 ✓）**、整轮 wall **32min > 30（仅提示，per-job timeout 全过）**；windows 实测高于软预算 24 ⇒ **建议 0.4.1 预算表 windows 24→27**。文档落点：`docs/v0.3.0-design.md` §12.3 表 + §12.6 口径、`AGENTS.md` §6.4 | 用户要求「优化 CI 流程，确保全绿」并授权；20min 是 0.3.x D16 口径（`docs/v0.3.0-design.md` §12.6），4 vCPU 冷构建硬成本已超 20min，属**预算口径与硬件不匹配**；校准只动 wall-clock，判据/断言/冻结面零放宽；release 道预算不动 | task-3（ci-green）：`main-full.yml` timeout/wall + 缓存、`nightly.yml` 判据修复；产品侧 M12-D25 由 Lead/集成批；文档 task-4（`v0.3.0-design` §12.3/§12.6、`AGENTS` §6.4/§11、CHANGELOG、0.4.0-report） |
| **A11** | **回归基线版本行重播种（M12）**：**Windows** `tools/baseline/golden.windows.log` 本地 `regression.py --update` 重播种，归因 = **version-line changes=12 / behavior changes=0 / changed-lines=24**（仅 `0.4.0.alpha1 → 0.4.0` 版本行换锚，行为零变化）；**Linux** `tools/baseline/golden.log` 由 nightly `workflow_dispatch` + `update_baseline=true` 生成 artifact 并**复核入库**（sha256 **`b86fead07f9d24da10066b0f5a640b6c6c5dacd590fa3aa686a795e43ddced04`** / 2567 行）。**实测**：nightly run 41294609588 双平台 regression **0 diff**；本地 `normalized 2567 lines vs baseline 2567 lines` | 版本正式化（A9）后 `version {lib=app version=…}` 行必然漂移 12 行；**行为零变化已逐行归因**（非放宽、非重播种掩盖真实差异），符合 AGENTS §5.3「重播种前先归因」；Linux 侧需 runner 环境生成 | Lead（基线文件入库 + artifact 复核）；task-3 提供重播种通道/归因（`build/m12-ci/report.md` §14/§17）；task-4 文档 |
| **A12** | **sanitizer 性能判据 scoping（M12；scoping 不是放宽）**：`test_quality_metric` 的 fastcolor 吞吐 **>10 MPx/s** 是**优化构建**的性能退化守卫；sanitizer 构建（Debug+插桩，实测 **2–4.7 MPx/s**）**恒假红**、不承载正确性语义 ⇒ CMake 检测 `-fsanitize=` 定义 **`PP_SANITIZER_BUILD=1`**，测试侧仅该构建打印跳过原因并跳过该阈值；**release/release-dev 判据逐字保留**（本机 **108.8 MPx/s** 通过）。三路正确性断言 / 0 sanitizer 报告扫描**不变** | nightly sanitizer 三路红点含「优化构建性能守卫」在插桩构建下的必然误报，会掩盖真正的正确性红点（如 D25/D26）；把阈值限定在承载其语义的构建类型，同时保留 release/release-dev 原判据，符合「证据面只增不减/不放宽断言」 | task-3：`tests/unit/test_quality_metric.cpp` + CMake sanitizer 检测/`PP_SANITIZER_BUILD` 定义；判据数值不变；task-4 文档（本文件 + CHANGELOG/报告 CI 段） |
| **A13** | **hwy 私有化尝试→回退 + nightly 两项已定案产品债（M12；`1b6fdbc` 最终口径）**：① **M12-D26 尝试**：`hwy=pp_ssim2_hwy` 在 Linux 链接期缺 `pp_ssim2_hwy::GetChosenTarget()/SupportedTargets()` 定义；补 `HWY_HEADER_ONLY/HWY_COMPILE_ONLY_STATIC`（自包含）后链接/SEGV 解决，但 gauss_blur 固定 AVX2 ⇒ **SSIMULACRA2 数值漂移 0.05–0.27**（`test_quality_metric` oracle `|Δ|≤0.01` 全红 + main-full release fastcolor parity 红）⇒ **回退**（`1b6fdbc`），只保留 D25 与 A12；**红线：`HWY_COMPILE_ONLY_STATIC` 不得用于 vendored metric**。② **两项产品债（0.4.1 修，如实登记）**：**hwy dispatch ODR 链接期 UB**（asan 3/9 SEGV `jpegli::VectorSize()` pc=0、tsan 同栈；**跨轮非确定**——前轮同测试 PASS；**release 二进制同样含该碰撞 = 潜在风险**）与 **ProgressMux/FileEvent 数据竞争**（tsan 1/9 + 9 条：`vector<FileEvent>::push_back` ×5、`FileEvent` move ×4、`shared_ptr<OutputResult>` ×2；写方 `ProgressMux::Impl::emit_seq`（progress.cpp:338，编码 worker）↔ 读方 main 线程测试回调（test_pipeline_contract.cpp:217））。③ **修复方向（0.4.1）**：hwy 私有化 + **bridge shim**（在 `pp_ssim2_hwy` 命名空间转发 `GetChosenTarget/SupportedTargets/Warn/Abort` 到真实 libhwy，保留动态派发=数值不变）或 ISA 基线对齐；事件投递收敛**单线程 drain 队列** / 回调加锁 + 明确 EventFn 跨线程契约 | **数值语义不得为 CI 修复而漂移（用户红线）**；「确保全绿」不能靠抑制报告/放宽判据/删测试（本批未做）；两项是 M12-C 把 tsan 真正跑起来后暴露的真实技术债，如实登记为 0.4.1 计划项 | task-3（ci-green）：诊断/尝试/回退 patch（`build/m12-ci/odr-hwy-*.patch`、`tests-patch-*.patch`）、`nightly.yml` UB 诊断/0 报告扫描 always()；产品修复 = 0.4.1（`src/core/progress.*`/`pipeline` 事件队列；vendored hwy 配置/bridge）；task-4 文档（本文件、CHANGELOG 已知、AGENTS §11、0.4.0-report §2.5/§4） |

---

## 附 1：需求 3 根因链 —— HEIF「Apple自适应」CRF 显示 28（B 提供）

> 这是**产品显示缺陷修复**，不是探针问题；`--ui-smoke` 探针 `heif-apple-crf` 只是把「文件里的 20」到「控件读数」的
> 整条链路钉住。锚点 = A7（HEIF Apple CRF=20）。

1. 工厂预设 `presets/formats/heif/apple-adaptive.json` 的 `params["x265:crf"] = 20` 是 **JSON 整数字面量**（无小数点）。
2. `format_preset_io` 读 JSON 时按字面量推断为 **int64**（`ParamSet` 的三态值由此为 int64）。
3. `ParamForm::set_values` 只接受与 `ParamDef` **同类型**的值；x265 运行时内省出的 `x265:crf` 是 **Float（double）**
   ⇒ int64 的 `20` 被**静默跳过**（不报错、不写入），控件停在运行时内省默认值 **28**。
4. **修复** = `src/ui/page_output.cpp` 的 `apply_state_to_form` 在**落卡前按生效后端的参数表做类型归一**
   （`int64 ↔ double`），**覆盖格式预设载入与全局预设载入两条写卡路径**，使「文件里的 20」真正落到 Float 控件。
5. **验收** = `--ui-smoke` 的 `heif-apple-crf` 探针（载入工厂 HEIF Apple自适应后 `pp-param-x265:crf` 控件读数 == **20**，
   `heif-apple-crf=1`）+ 工厂库单测（`x265:crf==20.0`、`reserved.heif_profile==2`）。
6. **口径**：JSON 保持人类自然的整数字面量（工厂数据无需改写）；类型归一放在**落卡边界**统一处理，而不是逐预设改数据。

## 附 2：`--ui-smoke` format-preset 组机械换锚与新增（B 提供；登记用）

**口径**：旧字段名**全部保留、语义换锚**；新增字段**追加**（「行字段只增」）。`format-preset:` 行 `checked`
**27 → 39**（既有 27 + 新增 12），`tests/ui_smoke.py` 的下限断言 **10 → 35**（**只升不降**）；冻结末行
`UI-SMOKE OK shots=12 pages=3` 与 12 张截图集**逐字不变**（截图不做像素比对）。真源 = `tests/ui_smoke.py`
（`FORMAT_PRESET_GROUP_FIELDS`）+ `src/ui/smoke_probes.cpp`。

**表 1：10 个换锚字段（旧名保留，语义更新）**

| # | 字段（行键） | 旧语义（M10/M11） | 新语义（M12） |
|---|---|---|---|
| 1 | `empty` | 未选择占位态：count==1（仅「未选择」项） | 未喂列表 = **空错误态**：count==0（无「未选择」占位项） |
| 2 | `prod` | 生产页行在位 | 生产页行在位 **+ 首启自动选中并载入 default** |
| 3 | `builtin` | C++ 内置库 seed 就位（`_builtin-*`） | **工厂 seed 就位**（11 文件 / 8 格式 default）且列表含 default |
| 4 | `builtin-idem` | seed 幂等（written=0/skipped=10） | 工厂 seed 幂等（written=0/**skipped=11**） |
| 5 | `builtin-readonly` | `_builtin-` 只读：I/O 拒绝删除（保存也拒绝） | **default 不可删**：I/O 拒绝删除（文件零改动；**保存覆盖仍可用**） |
| 6 | `builtin-saveas` | 另存为普通名字副本成功 | 另存为普通名字副本成功（并断言名字 ≠ default；default 时建议名「default 副本」） |
| 7 | `clears-source` | 全局预设后清「格式预设来源」 | 全局预设后进入 **detached 接管态**：path 空 / save 禁用 + 原因 tooltip / 注记含「全局预设」/ 点 save 无覆盖意图 |
| 8 | `unselect-adaptive` | 选「未选择」/外部删除后清自适应会话态 | 来源文件消失 ⇒ **自愈回 default 并载入**（策略随 default 更新，不残留旧策略） |
| 9 | `preset-tip` | 预设名 tooltip | 显式选 default → combo tooltip = `default`、注记 = 「来自格式预设：default」 |
| 10 | `roundtrip` | 临时目录 保存→列举→显式选中 | 临时目录 保存→列举→**自动载入**→显式选中；卡值一致 |

**表 2：12 个新增字段（只增）**

| # | 字段 | 钉住什么（需求） |
|---|---|---|
| 1 | `default-selected` | 工厂 `jpeg/default` 自动选中并载入（RC=视觉距离 / `distance=3.0`）——需求 4/5 |
| 2 | `no-unselected` | 下拉只含真实预设文件（无「未选择」占位项）——需求 4 |
| 3 | `default-protected` | default 不可删除：UI 删除置灰 + tooltip 且 I/O 拒绝（文件零改动）——需求 4 |
| 4 | `save-default` | default 可「保存」直接覆盖（临时根：写入成功 + 值读回）——需求 4 |
| 5 | `jpeg-adaptive` | JPEG default 载入后自适应行可见 / 开关开 / 摘要「目标 90 分」/ 策略含 `breakpoint 1 step 1`——需求 5 |
| 6 | `heif-apple-crf` | 工厂 `heif/apple-adaptive` 载入后 `pp-param-x265:crf` 控件读数 == **20**——需求 3（附 1 的验收面） |
| 7 | `legacy-migrated` | 旧 `_builtin-*.json` 迁移后文件真的消失（与第 11 项计数互补）——需求 1/2 |
| 8 | `png-no-adaptive` | PNG default 无策略 ⇒ 自适应整行隐藏（空策略隐藏不变）——需求 2/4 |
| 9 | `factory-seed` | seed 首轮：`written=11` / 8 格式 `default.json` / `removed_legacy=2`——需求 1/2 |
| 10 | `factory-idem` | seed 次轮：`written=0 / skipped=11` 且文件字节不变——需求 1 |
| 11 | `factory-migrate` | 迁移清理计数 `removed_legacy`（与第 7 项互补）——需求 1/2 |
| 12 | `global-detached` | 接管态可恢复：显式选 default 载入后 save 恢复、注记回来源——需求 4 |

> **实测**：0.4.0 最终树 `--ui-smoke` → `UI-SMOKE format-preset: checked=39 bad=0`（39 字段全 1，含
> `heif-apple-crf=1` / `jpeg-adaptive=1` / `global-detached=1` / `default-selected=1`）；冻结末行与截图集逐字不变。

---

## 附：与 M11 裁定的关系（加性勘误）

M11 批（`docs/m11-adjudications.md`）的结论**未被改写**，只登记被本批取代的条目：

| M11 条目 | 本批取代/修订 | 说明 |
|---|---|---|
| **A4**（内置预设库加性扩展：`builtin_file_stem` / `builtin_unavailable_reason` / 新增 5 条 `_builtin-*`） | **被 A1/A2/A3 取代** | C++ 内置库整体删除；5 条新预设内容保留为工厂 JSON（Apple自适应/无损归档/自适应压缩 + 两条 default） |
| **A11**（本轮不升版本号；CHANGELOG 以「未发行」记录；不重播种基线） | **被 A9 取代** | 本批发布 0.4.0 正式版；M11 内容并入 `[0.4.0]`；回归/基线口径按 Lead 最终门禁（默认产物路径不变，预期 zero diff） |
| **A6**（状态机 S-4/S-5：「未选择」清自适应会话态） | **前提消失，分支随 A5 删除** | 「未选择」状态不存在；S-1/S-2/S-3 结论保留 |
| **A5**（格式预设行 UI：去省略号/160px/不截断） | **保留，追加 A4/A5** | 行几何与文案不变；新增 default 自动载入/保护/detached 态 |

> 历史记录：M11 报告正文里的 `_builtin-*`、只读模板、内置库、seed 文件名等表述，已由
> [docs/m11-report.md](m11-report.md) §6 与 [docs/m11-adjudications.md](m11-adjudications.md) 末尾的
> **M12 加性勘误指针**标注「历史口径，已被 0.4.0 正式版取代」，不改历史结论。
