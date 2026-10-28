# M8 设计：HEIF / AVIF / JXL 编码器全参数暴露 + SSIMULACRA 2 输出评分

> 状态：**设计 v4 + 开发纪律（待用户确认后开工）**
> 日期：2026-10-28｜目标版本：M8 / 0.4.0（版本号届时按 `CMakeLists.txt:19` 单源更新；本文先称 M8）
> 需求来源（用户，逐字）：①「heif avif jxl 把底层编码器的模式、参数全部暴露。x265 参数那么多，全部都没有呈现。同时做好联动，避免提交不合法参数组合给编码器。充分联网搜索」；②「增加 SSIMULACRA 2 打分，对每个输出图像进行评价，结果分数显示在输出结果页」。
> 本文是**设计与任务计划**，不含实现代码。确认后按 §8 三阶段 workflow 开发（详见 `docs/m8-workflow.md`）；届时另建 `docs/m8-adjudications.md`（冻结面追认）与 `docs/m8-report.md`（收口取证）。

> **修订 v2（2026-10-28，依据用户评审反馈）**
> 1. **参数范围收敛为“精选重要参数”**：不再追求 220/125 全量呈现，改为经评审的清单（HEIF/x265 ≈40、AVIF/libaom ≈50、SVT 10、JXL ≈42），逐项重要性/理由见配套评审稿 **`docs/m8-param-exposure-review.md`**；未列入者按类登记理由、GUI 不出现。
> 2. **SVT-AV1 不打 libheif 补丁**（用户裁定）：仅保留原生 10 键；121 个 `svt_av1_enc_parse_parameter` 名字登记为库面限制，`pp_linkprobe` 锁定“SVT 参数面=10 键”的事实。
> 3. **SSIMULACRA 2 接入重新表述**：“GitHub 有 C++ 源码 ≠ 有可链接库”（libjxl 内部头不安装且挂 DEVTOOLS、cloudinary vendored 全 libjxl 0.8、vcpkg 无 port）；方案改为 **A 官方源码 + 兼容层（推荐）** / B overlay 编译官方内部子集（fallback），详见 §5.1 与评审稿 §8。
> 4. 配套评审稿把“每个库要暴露什么、为什么重要”做成可逐行评审的表；用户确认后再开工。

> **修订 v3（2026-10-28 交互裁定落地）**
> 1. **参数清单定稿**：按 `docs/m8-param-exposure-review.md`（x265 ≈40 / libaom ≈50 / SVT 10 / JXL ≈42）；清单外参数**完全封死**（未收录的 `x265:`/`aom:` 键 = 预检错误）。
> 2. **SSIMULACRA 2 采用方案 A**（官方源码 + 兼容层）；B 仅作数值门禁失败 fallback。
> 3. **评分默认**：GUI 默认开、**8 种输出全部评分**；参考=**量化后的编码器输入**（真无损=100）。
> 4. **线程池不是编码参数**：`x265:pools`/`frame-threads` 从参数面移除、不新增 HEIF 线程池设置；线程调度交给软件默认，R-078 如实登记。
> 5. **工作流改为三阶段串行**（参数事实 → 引擎+UI → 评分 → 收口），阶段内并行，每阶段一个可验收增量；详见 §8 与 `docs/m8-workflow.md`。

> **修订 v4（2026-10-28 联动补充）**
> 1. **控件形态与联动分级**：新增 `docs/m8-param-control-map.md`，逐键标注形态（M 模式切换 / N 数值 / D 下拉 / S 开关 / T 文本 / R 只读）与联动角色（锚点/依赖/独立）；本文新增 §4.6 摘要。
> 2. **RC 模式显式化（已定）**：HEIF {质量/CRF/QP}、libaom {质量/CQ Level}、SVT {质量/QP} 用模式下拉/单选选择路径（默认“质量”= 既有 `quality`，零行为变化），只显示当前路径数值行；无损时整组隐藏。
> 3. **新增布尔行用三态下拉（已定）** `{跟随默认/开/关}`；既有布尔行保持复选框。
> 4. **计数双口径（已定）**：评审稿 ≈40/≈50/10/≈42 为组行数（范围口径）；按控件/键展开 155 可编辑 + 2 禁用/隐藏 + 13 只读 = **170 行是目录/测试/门禁的唯一真源**（Stage A4 对账定稿）。
> 5. **开发纪律**：见 `docs/m8-discipline.md`——未“开工”不写实现；任务卡必须自包含/约束清晰；**子任务不得再次派发子任务**。

---

## 0. 事实底座（本次联网 + 本地源码取证）

版本全部以本机 vcpkg 安装/构建树核对（与 `AGENTS.md` §2 口径一致）：**libheif 1.23.5、x265 4.3、libjxl 0.12.0、libaom 3.15.1、SVT-AV1 4.2.0、OIIO 3.1.17、lcms2 2.19.1、highway 1.4.0**。关键结论（含来源；详细取证见各任务的工作笔记，均为 gitignored 的 `build/research/*.md`，开发时随任务补进提交说明/报告）：

| # | 事实 | 来源 |
|---|---|---|
| F1 | **x265 全参数可达**：libheif 的 x265 插件支持 `x265:<opt>` 透传（`encoder_x265.cc:531-534` → `x265_param_parse`，`:1022-1033`）。x265 4.3 的 `x265_param_parse` 接受 **239** 个名字；本构建（`ENABLE_ALPHA=0/MULTIVIEW=0/SCC=0`、无 SVT）运行时可用 **223**；减去被 libheif 事后覆盖的 3 个（`log-level`/`log`/`input-res`）⇒ **220 个真实可达**。 | 本地 x265 源 `source/common/param.cpp:927-1580`；libheif 插件源；x265 官方 CLI 文档「除非标注 CLI ONLY，均支持 x265_param_parse」 |
| F2 | **x265 的“插件固定项”要先于用户参数写入，可被覆盖**：`param_default_preset(slow,ssim)` → `param_apply_profile(mainstillpicture/main10-intra/main12-intra)` → CTU 自适应 → 固定工具开关（`info=0, limit-modes=0, limit-refs=0, rskip=0, rect=1, amp=1, aq-mode=1, psy-rd=1.0, psy-rdoq=1.0`）→ 用户参数循环 → `sourceWidth/Height/internalBitDepth/logLevel/fps`（最后写，不可覆盖）→ `encoder_open`。因此 `x265:profile`/`input-csp`/`total-frames`/`annexb`/VUI 组/`ctu` 等必须由 GUI 拦，不能交给 x265 报错。 | `encoder_x265.cc:753-1089` |
| F3 | **x265 校验不能靠库**：`x265_param_parse` 只查名字/字面值；跨字段冲突在 `x265_check_params`/`configure`/`enforceLevel` 才炸，而 `x265_check_params` 不在公开 API 表。错误文案默认被 libheif 静音（`logLevel=X265_LOG_NONE`）。⇒ 必须 **GUI 静态规则表 + 运行期预检**，编码器侧只做最后兜底。 | x265 `x265.h` API 表；libheif 插件 `:1024-1031` |
| F4 | **HEIF 原生面只有 7 个键**（`quality/lossless/preset/tune/tu-intra-depth/complexity/chroma`）+ 隐式 `logging-level`；`x265:*` 键读不回来（getter 不认）⇒ GUI 工作集是唯一真源。 | `encoder_x265.cc:220-301,454-474,562-594` |
| F5 | **AVIF libaom 原生 15 键**（`realtime/speed/threads/quality/lossless/chroma/tune/min-q/max-q/alpha-quality/alpha-min-q/alpha-max-q/lossless-alpha/auto-tiles/enable-intrabc`）+ `aom:<name>` 透传；透传在 `aom_codec_enc_init` 与全部内置控制**之后**应用（最后赢），设置期不校验、失败推迟到 encode。`aom_codec_set_option` 可用长名 **125** 个；cfg 级字段（`end-usage/usage/profile/bit-depth/lag/kf-*/full_still_picture_hdr/resize/superres`）**不是** option 名 → libheif 固定的 `AOM_Q + g_limit=1 + ALL_INTRA + lag=0` 不可动摇。 | `encoder_aom.cc:274-457,743-748,1232-1249`；libaom `av1_cx_iface.c:4427-4927`、`av1/arg_defs.c` |
| F6 | **AVIF SVT 原生面 10 个键**（`speed/threads/tile-rows/tile-cols/quality/lossless/qp/min-q/max-q/tune`；`qp` 无默认）；**没有 `svt:` 透传**（1.23.5 与 master 都没有，最新 tag v1.23.7，无 1.24/1.25）。`svt_av1_enc_parse_parameter(EbSvtAv1EncConfiguration*, name, value)` 接受 **121** 个名字、未知名字可即时失败 ⇒ 技术上可加性 overlay 补丁（约 70 行、3 个 hunk，`encoder_svt.cc:954→956` 之间插入；锚点 1.23.5→master 稳定）；**但用户 2026-10-28 裁定不打补丁**，SVT 仅保留原生 10 键并登记为库面限制。 | `encoder_svt.cc:181-326,535-588`；SVT `EbSvtAv1Enc.h:1138`、`enc_settings.c:2125-2456`；libheif GitHub tags/master；用户裁定（本轮对话） |
| F7 | **JXL 公开面 41 个 `JXL_ENC_FRAME_SETTING_*`**（0.12.0；0.11.2→0.12.0 仅新增 `OUTPUT_MODE=40`，公开函数零删除、无 `JXL_DEPRECATED`）+ 编码器级/基本面 API。当前产品暴露 24 个 frame setting + 4 个特例（共 28 键），缺的可暴露项约 14 个（`noise`、`group_order_center_x/y`、`upsampling_mode`、`intensity_target`/色调映射 4 项、`uses_original_profile`、`box_compress`、`expert_options+effort 11`、`buffering`、`output_mode`、`disable_perceptual_heuristics` 等；alpha 相关因“不支持 alpha”不做）。**三态（未设=交库默认）机制已存在**：`ParamDef::has_default=false` + “键不在 ParamSet = 不下发”。 | libjxl `lib/include/jxl/encode.h`（安装树与 buildtree SHA-256 相同）；`lib/jxl/encode.cc`；`lib/jxl/enc_*.cc` |
| F8 | **SSIMULACRA 2 没有现成可链接库**：权威仓库是 **`cloudinary/ssimulacra2`**（BSD-3-Clause + `PATENTS` 专利授权；v2.1 权重；自带 libjxl 0.8 vendored 源码，无 `find_package`/导出目标/`install()`）；libjxl 0.12.0 的 `tools/ssimulacra2.{h,cc}`（算法 2.1，权重与 Cloudinary v2.1 逐字一致）依赖 `lib/jxl` **内部**头，且在 libjxl 构建里挂在 **`JPEGXL_ENABLE_DEVTOOLS`** 下（`INTERNAL_TOOL_BINARIES`，不安装）⇒ vcpkg 的 libjxl 从不构建/安装它；**pinned vcpkg 无 `ssimulacra2` port**（独立 overlay port 需要自己编 libjxl 内部子集）。 | 本地 `vcpkg/buildtrees/libjxl/.../tools/ssimulacra2.*`、`tools/CMakeLists.txt`；cloudinary/ssimulacra2 GitHub（commit `81feacf6`；tags v2.0/v2.1）；microsoft/vcpkg ports |
| F9 | **SSIMULACRA 2 语义**：分数 ∈ (−inf, 100]（**负分合法**）；**逐样本相同 = 恰好 100.0**（无 clamp/nan）；**<8×8 时库内部静默返回 100**（CLI 报错），须自己拦；输入可为任意位深/ColorEncoding（库内部经 lcms2 线性化到 XYB，不是只收 8-bit sRGB）；**无两段式 API**（每次调用重新 Copy/变换/金字塔）。**关键陷阱：只有参考是“编码器实际收到的量化后整数样本”时，无损才得 100；拿内部 float32 帧当参考会让无损只得到 90 几分** ⇒ 参考必须在目标位深量化（与编码器同一 `simd::quantize8/16`/抖动口径）后再评价。 | libjxl `tools/ssimulacra2.cc/main.cc`；cloudinary README/权重；PyPI `ssimulacra2` 0.3.0 |
| F10 | **SSIMULACRA 2 成本**：参考实现严格单线程、无可复用准备；公开口径约 **104.5 ms/MP**（Apple M4 Pro，含 PNG 解码），x86 折算约 **120–245 ms/MP**（[inferred]）⇒ 24MP 单输出约 **3–6 s**；参考实现峰值内存约 **130–160 B/px**（一次性 Copy + 6 尺度金字塔，24MP≈3.5 GB）——比当前像素预算峰值大 4–5 倍。⇒ 我们的实现（官方源码 + 兼容层）必须**流式/分块 + AVX2 热点内核**，并以本地 16 核实测为准（Stage C1 spike）。 | libjxl tools；cloudinary README；SSIMULACRA 2 论文/生态基准（见 §12） |
| F11 | **产品内部面**：结果页 = `PageRun`（objectName `pp-run-*`），不是 `page_output`（配置页）；运行行是手搭 6 列（`mark|name|bar|size|speed|time|status`），冻结 pane 宽度 587/520 ⇒ **加第 7 个固定列在最小宽度下不可行**；`<out>.pp.json` sidecar **仅 `--dev` 写**；`regression.py` 只比对 `run-*.log`（N1–N10）与 stdout 表；基线里 0 条 `[debug]` ⇒ 新增 debug 行零 diff；金样 `SMOKE total=20` 与 UI `shots=12 pages=3` 冻结。 | `src/ui/page_run.cpp`、`src/dev_cli.cpp:492-564`、`tools/regression.py`、`tests/golden/smoke.py:371`、`tests/ui_smoke.py:268` |
| F12 | **参数引擎缺口**：`ParamType` 只有 Int/Float/Bool/Enum（无 String 行控件）；`cross_validate(values, format, tech)` 拿不到 **backend / 位深**；可见性/锁定谓词只看 `ParamSet`；高级区已按 UX-8 落地“自然高度 + 二级分组 + 搜索”（无 224px 限高），但分组词表只有 5 个、行控件无“启用/跟随默认”语义；预设经 `FormatState::extra` 可保存任意键（含 `x265:` 前缀）。 | `src/core/params.h`、`src/ui/paramform.cpp`、`src/ui/page_output_impl.h:305` |
| F13 | **用户评审裁定（2026-10-28）**：① x265 参数太多，**只选重要的**（暴露面以 `docs/m8-param-exposure-review.md` 的评审清单为准，逐项给重要性）；② SVT-AV1 **不打 overlay 补丁**（10 原生键 + 库面限制登记）；③ SSIMULACRA 2 质疑“为何不直接用 GitHub 上的 C++ 源码”，需在设计中解释“有源码≠可链接库”并给出“官方源码 + 兼容层”方案；④ 本稿为评审稿，**确认前不开工**。 | 用户本轮反馈（逐字记录于对话） |

---

## 1. 目标与非目标

### 1.1 目标（可验收）

1. **重要参数全暴露（评审清单制）**：HEIF（x265）、AVIF（libaom / SVT-AV1）、JPEG XL 三个 family 的**精选参数清单**（`docs/m8-param-exposure-review.md`）全部进入 GUI；每一项要么**可设置**、要么呈**只读**（上游/`libheif` 固定，显示当前生效值）、要么呈**禁用**（不合法/有害，附中文原因）。清单外的库参数不出现，但必须在评审稿/`param-catalog` 中按类登记“为什么不暴露”。**不再以“220/125 个全量”为验收口径。**
2. **模式暴露**：编码档位（HEIF profile / AVIF profile）、level/tier、preset/tune、速率控制模式（可达者）、JXL VarDCT/Modular、AVIF 后端选择——可达的做成控件，不可达的（如 libheif 固定 `AOM_Q`）做成只读行并注明“上游固定”。
3. **联动与预检（硬要求）**：所有已知非法/必失败/静默无效组合在**开始运行前**被机械拦截（禁用 + 原因 / 运行期预检错误），保证“提交给编码器的参数组合一定合法”；编码器入口保留最后防线；批量场景不允许“N 个文件 × 同一条编码错误”。
4. **SSIMULACRA 2 逐输出评分**：每个成功编码的输出产出一个 SSIMULACRA 2 分数（−inf–100，越高越接近编码输入；无损目标应为 100），**GUI 默认开启、覆盖全部 8 种输出格式**，显示在运行结果页的对应输出行；`--dev` sidecar 记录机器可读分数；不因评分失败而把输出判失败。
5. **零回归漂移**：新增参数默认“不设置”（不进 `ParamSet`）；不改变既有默认值与既有参数面行为；`ctest`、金样 20/20、`regression.py` zero diff、`--ui-smoke` 冻结末行保持；新增断言只增不减。

### 1.2 非目标（明确不做）

- 不做「直连 x265/libaom/SVT 原生 API」的架构改造：继续走 libheif 插件；**不改 libheif overlay、不给 SVT 补 `svt:` 透传**（用户裁定；SVT 仅 10 原生键并登记库面限制）。
- 不做 i18n/主题/预览/对比看图等负面清单功能；不改变 8 格式既有参数面语义（除本文点名的联动规则）。
- 不做 aom **cfg 级**字段（`end-usage/usage/profile/bit-depth/...`）的 overlay 暴露：与 libheif 的 still 语义打架、高风险低价值（登记为“已知不暴露 + 理由”）。
- 不把视频向参数“变成有效”：它们仍呈现（禁用/无效果标注），但不为它们设计视频流水线。
- 不对“仅元数据”模式评分（无重编码，分数是恒等式，徒增开销）。
- 不新增 GUI 侧 sidecar 写入（保持 `--dev` sidecar 现状；GUI 面用运行页/日志）。

---

## 2. 现状与差距（量化）

| 面 | 现在 | 建议暴露面（评审清单，组行计数） | 差距 |
|---|---|---|---|
| HEIF/x265 | 内省 7 键 + 顶部无损复选框；`x265:` 透传**无 GUI 入口** | **≈40 项**（现有 7 + 精选 33；从 220 个可达名中选“对单帧静止图有意义”的） | 缺 ~33 个键的 GUI 呈现与校验；其余按类登记不暴露 |
| AVIF/libaom | 内省 ≈10 可见（15 减 alpha/线程）；`aom:` 透传无 GUI 入口 | **≈50 项**（现有 10 + `aom:` 精选 40） | 缺 ~40 键 |
| AVIF/SVT-AV1 | 内省 10 键；`svt:` 是**假接线**（任何 `svt:` 键 ⇒ 整文件失败） | **10 项**（用户裁定不打补丁）；121 个 parse 名登记为库面限制 | 停止“假接线”：白名单移除 `svt:` + 文档/探针登记 |
| JXL | 24 frame setting + 4 特例（28 键） | **≈42 项**（现有 28 + 精选 14） | 缺 ~14 键 + 三态（auto/未设）语义 |
| 联动/校验 | `cross_rules()` 6 条；`cross_validate` 无 backend/位深上下文；编码器内 `fail()` 兜底 | 评审清单内每条 ✅ 相关规则（本文 §4.3） | 缺规则引擎、缺三态、缺预检批量拦截 |
| 结果评分 | 无 | — | 全新增 |

**“全暴露”的定义（本设计的口径，避免无限膨胀）**：
> 全暴露 = **评审清单内的参数 100% 可设置/只读/禁用三态呈现，且每一项状态与原因可复核**；清单外的库参数按类登记“不暴露理由”（视频向无效果 / 有害 / 文件副作用 / 不可达）。不等于“CLI 全量”，也不等于“220/125 个逐个占 GUI”。

---

## 3. 契约表（C1–C14，确认后冻结；子任务不得擅自变更）

| # | 契约 | 要点 |
|---|---|---|
| **C1** | **参数事实单源** | 每后端一份**精选参数目录**（新文件 `src/codecs/param_catalog/*`），**以评审稿 `docs/m8-param-exposure-review.md` 为唯一范围依据**：清单内每一项都要在目录里；清单外的库参数不建立行，但评审稿 §5 保留“按类不暴露”的登记。目录由 `tools/gen_param_catalog.py` 从上游源码机械抽取锚点（名字/域/默认）+ 人工补中文标签/分组/规则标记；生成物入库，脚本保留（可复现）。UI、预设、`--param`、日志快照、校验、编码器接线全部读同一份目录。目录条目 = `ParamDef` + 状态（`Settable/ReadOnly/Forbidden/NoEffect`）+ 所属分组 + 透传前缀/引擎键 + **控件形态（M/N/D/S/T/R）与联动角色（锚点/依赖/独立，见 §4.6）**。 |
| **C2** | **三态呈现** | 新增参数默认 **Unset（跟随编码器默认）**：`has_default=false`，不进 `default_params`/`fill_defaults`/日志快照。GUI 行有“启用”勾选（未勾 = 不下发；勾选才写值）；只读项显示生效值不可编辑；禁用项显示原因。**新增布尔行用三态下拉 `{跟随默认/开/关}`；RC 模式由模式控件统一选择路径（见 §4.6）**。既有 7（HEIF）/10（AVIF）/28（JXL）键维持今日语义与默认值**逐字不变**（保证零回归）。 |
| **C3** | **后端作用域** | `x265:` 只对 HEIF/x265 合法；`aom:` 只对 AVIF/libaom。**`svt:` 不再作为合法前缀**（SVT 无透传；出现即预检错误，与“假接线”清理一致）。跨后端出现即预检错误。 |
| **C4** | **模式先行、细项随后** | 编码模式类参数（profile/level/tier/preset/tune/rc-mode）为“模式下发”；静态目录声明应用顺序；与细项冲突时按 §4.3 规则禁用/联动，不允许“两个控件都亮但只有一个生效”。 |
| **C5** | **联动规则引擎** | 新 `src/core/param_rules.{h,cpp}`：规则以数据表声明（格式/后端/键集合/条件/动作=禁用/隐藏/锁定/强制/警告/错误，zh+en 文案）；UI 实时求值（置灰+原因）、`validate_params`（en）/`cross_validate`（zh）同源、运行期预检汇总报错。上下文结构 `ParamContext{format,backend,tech,bitdepth,lossless,width,height,...}` 以**加性重载**进 `params.h`（旧签名转发，6 个调用点零行为变化）。 |
| **C6** | **非法组合不提交** | 三阶段：① 运行前 `validate_run_config` 用规则引擎 + 尺寸/位深上下文全量校验，任一错误 ⇒ 拒绝运行并给中文可操作信息（批量只报一次）；② 逐文件期做“依赖图像尺寸”的检查（如 x265 `tu-intra-depth` 的合法性与 libheif 自适应 CTU 相关、JXL center_x/y 边界、x265 `min-cu-size` padding 警告），失败在编码前给出明确原因；③ 编码器入口二次断言（现有 `fail()` 模式）。**不允许**把非法组合放到逐文件编码期才失败。x265 侧校验分层：L0 静态 schema（类型/域/枚举）+ L1 名字/值字面量等价表（与 `x265_param_parse` 的 `BAD_NAME`/`BAD_VALUE` 100% 等价）+ L2 跨字段冲突规则（镜像 x265 4.3 `x265_check_params` 的 24 条可机械判定冲突，纯函数、可单测；上游升级需重核）。 |
| **C7** | **透传键规范化与白名单** | GUI 生成/接受的透传键一律 `prefix:engine-name`（如 `x265:crf`），**且必须在目录里**；目录标记每键的引擎名/类型/域；`x265:` 值按 x265 字符串语法（支持 `no-` 等归一，但 UI 只提供规范名）；CLI-ONLY 名字（`preset/tune/profile/qpfile` 等）永不生成。**清单外/未收录的 `x265:`/`aom:` 键视为未知参数 ⇒ 预检错误**（已裁定“完全封死”；老预设里的冷门手写键会被拒绝并给中文定位）。 |
| **C8** | **不做自由文本专家编辑器（v1 变更）** | 原设计的“插件原始参数多行文本框”**本波不做**：它等于绕过评审清单与三态/规则，与“避免非法组合”目标冲突。未收录参数一律不出现；确需专家通道时，仅允许通过预设 JSON/`--param` 提交**目录内**的键值（同一套校验）。后续如要做，必须带同一规则引擎校验并单独裁定。 |
| **C9** | **评分的参考口径** | 参考 = **编码器实际收到的量化后样本**（管线 float32 帧按目标位深用与编码器相同的 `simd::quantize8/16`（含可选 `dither8`）量化，灰度复制为 RGB）；失真 = 解码回读的产物像素。⇒ 真无损（逐位一致）**恰好 100.0**；HEIF/AVIF 的“码流无损”因 RGB↔YCbCr 舍入可得 <100（如实）。**两图统一归一到 sRGB 线性**后再评价（参考与失真走**同一条** ColorManager/lcms2 目标→sRGB 变换；目标本身是 sRGB 时退化为精确 sRGB EOTF，无 lcms 开销）——保证 P3/AdobeRGB/keep 源上的分数仍是 SSIMULACRA 2 的规范语义，而不是“同空间近似”。UI/文档明确写“评价编码保真度（对比编码器输入），不是对比源文件”。 |
| **C10** | **评分计算位置/线程/进度** | 在 worker 线程内、单输出编码成功之后、元数据写入之前同步计算（取消检查前后各一次）；**不新增 Stage / FileState / FileEvent 字段 / Warning**（全部冻结）；进度仍归属 Encode 段尾部；指标模块必须**流式/分块**处理，峰值 ≤ 现有 `2×frame` 像素预算（不重播种基线）；输出回读 = `probe_file(row.out)` + `decode_float()`（OIIO），回读失败/尺寸不符/解码异常 ⇒ `Unavailable`（不失败产物、不出 Warning）。 |
| **C11** | **结果数据面** | `OutputResult` **末尾加性**追加：`MetricState metric_state {Off,Ok,Unavailable,Failed}`、`double metric_ssimulacra2 = -1.0`、`double metric_ms = 0.0`、`std::string metric_note`；`RunConfig` 末尾追加 `bool quality_score=false`（GUI 设置默认 on，`--dev` 默认 off + `--score` 开启）；`FileResult`/`RunSummary`/`Timing` 零改动。 |
| **C12** | **结果呈现** | 运行页每个输出行下方加**隐藏式子行**（沿用 `reason_row/warn_row` 模式；新 objectName `pp-run-score-<i>-<j>` + `-text-`）：`质量 SSIMULACRA 2 · 92.31`，tooltip 含分数刻度释义 + 参考口径 + 备注；仅在 `metric_state==Ok` 显示；`Unavailable/Failed` 显示 `—（原因）`；`Off`/仅元数据不显示。不加第 7 固定列、不改表头、不改冻结 UI 行。 |
| **C13** | **证据面** | `--dev` sidecar 加**嵌套对象** `"metric": {name,version,state,score,ms,reference,colorspace,note}`（仅 `--dev`）；`SCHEMA.md` **加性**追加 sidecar 段与 `assert.score`（区间断言，绝不逐值断言）；金样**不新增 case**（`SMOKE total=20` 冻结），在 2–4 个既有 case 上加 `assert.score`；smoke 运行加 `--score`；`ui_smoke` 只加探针不删；截图不逐像素比对（视觉变化在报告说明）；回归默认不评分 ⇒ 零 diff。 |
| **C14** | **依赖与许可（v2 修订）** | **不新增 vcpkg 依赖、不改 overlay port、不升级任何库**（SVT 不打补丁已裁定）。SSIMULACRA 2 采用 **方案 A：官方 C++ 源码 + 兼容层**——把 libjxl 0.12.0 `tools/ssimulacra2.{h,cc}` 与 `gauss_blur.cc`（BSD-3-Clause，JPEG XL Project）**逐字纳入仓库**（新目录，保留版权头 + 算法版本常量 `kMetricVersion=1`），只写薄兼容层替换其 libjxl 内部依赖（Image3F/ImageF、状态、高斯模糊、ToXYB、线性化）；`PATENTS` 专利授权说明进关于页/许可清单。算法/权重/常量不重写，因此不是“自研算法”。 |

---

## 4. 参数目录、分组与联动（本设计的核心）

### 4.1 目录数据形态（C1）

新增目录（新文件，不动冻结表结构；`CMakeLists` 为源文件自动收编，无需逐文件登记）：

```
src/codecs/param_catalog/
  catalog.h/cpp          # CatalogRow/ParamCatalog API、查询、与内省合并、状态枚举
  jxl_catalog.inc        # JXL 新增/补全行（约 14）
  x265_catalog.inc       # x265 精选行（约 33 新增；现有 7 项由原生内省行接管）
  aom_catalog.inc        # libaom 原生补全 + aom: 精选（约 40 新增）
  svt_catalog.inc        # SVT 只读/模式行（10 原生键由内省提供；本文件只含只读展示行）
tools/gen_param_catalog.py   # 从上游源码抽取锚点（名字/域/默认）+ 生成 .inc（可复现，CI 不做网络）
```

`CatalogRow` 关键字段：`key`（UI/ParamSet 键）、`engine_key`（透传引擎名）、`ParamDef def`（类型/域/默认/中文标签/分组/tooltip）、`Scope`（Settable / ReadOnly / Forbidden / NoEffect）、`Reason_zh`、`RuleTags`（供规则引擎/预检快速过滤）。目录 API 由 `params.h` **加性**转发（`catalog_params(format,backend)`、`catalog_lookup(...)`）；HEIF/AVIF 合并进 `introspect_backends()` 的运行时技术参数表（UI/预设/`--param` 自动获得），JXL 合并进静态表路径。

> 计数口径：目录行数 = **评审稿 §1–§4 的暴露清单**。x265 约 33 个新增行（现有 7 个原生键不重复出行）；libaom 约 40 个新增（部分为“组”合并行）；JXL 约 14 个新增；SVT 不新增参数行（只有只读/提示行）。最终行数以目录实现为准，并在 `docs/m8-report.md` 记录“清单条目 ↔ 目录行”对账表。

### 4.2 分组（UI 二级折叠；扩词表）

新增分组常量（`params.h` 末尾加性，`kGroup*` 词表扩展）：`质量/码率`、`分析工具`、`心理视觉`、`量化/变换`、`环路滤波`、`分块/线程`、`码流/兼容性`、`调试/实验`、`只读（上游固定）`。映射上游分类（x265 官方 20 类、libaom/ SVT 文档分类）到上述 9 组；每个 group 默认只展开第一组，搜索跨组过滤（沿用现有 UX-8 机制）。

### 4.3 联动规则清单（初稿；实现时逐条落成规则表，含证据）

**JXL（R1–R22，按研究结论实现可落地的部分）**
- 无损 ⇒ `resampling=1`（已有锁定）+ `distance` 走无损路径（已有）；`modular_ma_tree_learning_percent=0` + 有损 Modular ⇒ **拦截**（编码期硬失败，库不拦）；
- `progressive_dc>0` 且（alpha 存在或 `resampling>1`）⇒ 禁用并提示“库会静默清零”；
- `group_order_center_x/y` 仅 `group_order=center`（1）时可见，且运行期预检 `< width/height`；
- `upsampling_mode` 仅 `resampling>1` 可见；`box_compress` 关 ⇒ `brotli_effort` 隐藏；`brotli_effort` 只在 `box_compress=1` 时有意义；
- `effort=11` 需 `expert_options=on`（否则库报错）；`codestream_level=5` + 16bit 无损 ⇒ 预检错误（需 level 10）；
- `disable_perceptual_heuristics=1` 需 `uses_original_profile=true`（否则库报错）；
- 有损 Modular ⇒ `palette_colors/lossy_palette/channel_colors_*` 隐藏（库静默清零）；`modular_color_space>0` 需 `color_transform≠XYB`；`modular_predictor` 3–6 在有损 Modular 下提示“会被库替换”；
- `output_mode=2` 加兼容性警示；`photon_noise` 域 ≥0；`palette_colors` 实际上限 70913（修文档）。
- 说明：`epf/gaborish` 的 `lossless ⇒ 强制` 规则**本波不做**（它们仍归 VarDCT-only，无损时本就不可见；把技术分类扩到 Modular 属于另一项变更，需先补显式锁——见研究结论 D2/D6，登记为后续）。

**HEIF/x265**
- 速率控制**三选一**：`quality`（原生，CRF=(100-q)/2）/ `x265:crf` / `x265:qp`；任一被勾选 ⇒ 其余禁用并说明覆盖关系；
- `x265:annexb`、`x265:input-csp`、`x265:total-frames`、VUI 组（`range/colorprim/transfer/colormatrix/chromaloc/display-window/crop-rect/video-signal-type-preset`）、`x265:log-level/log`、`x265:input-res`、`x265:ctu`（由 libheif 按图像尺寸自适应，覆盖可能违反上游稳定性约束）⇒ **禁止**（禁用 + 原因）；
- CLI-ONLY 名（`preset/tune/profile/qpfile/zonefile/...`）⇒ 不得生成（若从预设/原始编辑器进来 ⇒ 预检错误）；
- 文件副作用组（`nalu-file/csv/stats/analysis-*/lambda-file/film-grain/dhdr10-info/fovea-gaze-file/zones`）⇒ 禁止；
- 有害组（`uhd-bd/dolby-vision-profile/intra-refresh/pic-struct/bitrate/vbv-*`）⇒ 禁用 + 原因（会破坏 still 码流/改为 ABR）；
- **勘误（研究结论）**：x265 4.3 **没有 tiles 选项**，不存在“wpp vs tiles”冲突；libheif **不强制** `bframes=0`（slow preset ⇒ 4）、不强制 `repeat-headers`，只通过 `param_apply_profile` 侧效应把 `keyframeMax=1`。真正的互斥是 `slices>1` 与 `wpp/complexity`；`bframes` 等纯视频项归入“单帧无效果”组（可设但无实际收益）；
- **校验分层**：x265 的 `param_parse` 只查名字与字面量（**不查范围**：`ref=99/crf=100/tu-intra-depth=9` 都会“成功”），跨字段检查在非公开的 `x265_check_params`（`encoder_open` 内，libheif 折成一句 "x265 encoder could not be opened…"）⇒ 范围/枚举/24 条冲突全部由我们的 L0/L1/L2 表在**运行前**拦（见 C6）；依赖图像尺寸的 `tu-intra-depth`（与自适应 CTU 相关）放逐文件期校验；
- 可选诊断增强（P2，不默认开）：需要时 `heif_encoder_set_logging_level(enc, 0)` 打开 x265 日志并把 stderr 路由到运行日志；注意 Windows GUI 子系统 stderr 可能为 NULL（`AGENTS.md` §6.5 坑），实现前先验证；
- 互斥/依赖：`x265:slices>1` ⇒ 必须 `wpp=1` 且 `complexity<90`（`complexity≥90` 会把 wpp 关掉 ⇒ 组合必炸）；`x265:rd-refine=1` 需 `rd≥5` 且 `aq-mode≠0`；`x265:psy-rdoq` 需 `rdoq>0`；`x265:min-cu-size>8` 且宽高非整除 ⇒ 警告（x265 自动 padding/conformance window）；`x265:level-idc` + CRF ⇒ 警告（强制 VBV、非确定性）；`x265:tune` 的 psy/AQ 部分会被 libheif 覆盖 ⇒ tooltip 如实说明；
- **线程池不是编码参数（用户裁定）**：`x265:pools`/`frame-threads` 不出现在格式参数面，也不新增“HEIF 线程池”设置；线程调度交给软件默认；R-078（x265 默认池不入账）继续如实登记为已知限制；`wpp`/`slices` 属码流工具，按 §4.3/评审稿保留（`slices` 未入选，见评审稿 §1.3）。

**AVIF/libaom**
- `quality` 与 `aom:cq-level` 二选一（直通最后应用会覆盖）；`lossless` ⇒ 强制 `chroma=444`（已有）+ 锁 `aom:aq-mode=0`、`aom:enable-chroma-deltaq=0`，且 `tune≠iq`；
- `tune=iq` ⇒ 提示并“自动”呈现 `enable-qm/qm-*/sharpness=7/chroma-deltaq`（库内部行为）；
- `aom:auto-tiles=1` 与 `tile-columns/tile-rows` 互斥；`min-partition-size ≤ max-partition-size`；`cq-level ≤ 63`；`cpu-used` 域随 `realtime`；
- `matrix-coefficients=identity` 需 4:4:4（`input-chroma-subsampling-*` 本身禁止：只改码流信号不改平面布局）；
- 禁止：`input-chroma-subsampling-*`、`denoise-*`/`enable-dnl-denoising`（违反“无额外图像处理”）、`*-path`、`passes`、`film-grain-*`、`tune=vmaf*`（构建未开）；
- 只读：libheif 固定 `g_usage=ALL_INTRA`、`rc_end_usage=AOM_Q`、`g_limit=1`、`lag=0`、`kf disabled`、profile 由位深/色度推导（显示为只读行）；
- 线程：`threads` 与 E3 关系同现状（调度器覆盖），保留现状。

**AVIF/SVT-AV1（10 原生键，无 `svt:`）**
- `qp` 被启用 ⇒ `quality` 隐藏/禁用（qp 优先；`qp` 是 has_default=false 的三态行）；
- `lossless` 对 SVT 当前产品硬失败 ⇒ 禁用勾选并指路 libaom（审核修复已隐藏，本次保持 + tooltip）；
- profile/level/tier 由插件按色度/位深强制 ⇒ 只读显示；
- `tile-rows`/`tile-cols`（原生=**真实条数** 1/2/4/…/64，内部取 log2）与 Annex-A 约束（tiles≤128、cols≤16）在 tooltip/预检说明；
- `threads` 与 E3 关系同现状（调度器覆盖，保持隐藏）；
- **禁止出现 `svt:` 前缀键**（无透传通道；预检错误，与 C3 一致）；视频向参数（keyint/intra-period/pred-struct/lookahead/sframe-*/resize-*/superres-*/film-grain* 等）不建 GUI 行，按评审稿 §3 登记为“库面不可达/单帧无效果”。

### 4.4 UI：参数卡重构（在现有 UX-8 形态上增量）

1. **状态行**：每行左侧加“启用”勾选（仅新增目录行；既有 28 键不变），未勾选 = 跟随编码器默认（值控件置灰但显示库默认/文档默认，带“未下发”标记）；**新增布尔行用三态下拉（见 §4.6）**。
2. **状态徽标**：只读行显示“只读（libheif 固定）”；禁用行整行置灰 + tooltip 原因；无效果行标注“单帧无效果”。
3. **搜索/过滤**：现有搜索框扩展到“键/中文标签/引擎名”；加“只看已启用”“只看可设置”过滤。
4. ~~原始参数编辑器~~ **（v2 删除）**：本波不提供自由文本透传编辑器（会绕过评审清单/三态/规则，与“避免非法组合”冲突）；未收录参数一律不出现。专家通道仅限预设 JSON/`--param` 提交**目录内**键值并走同一校验（见 C7/C8）。
5. **运行前预检**：输出页底部/开始运行前，规则引擎产生**分组错误清单**（每个格式 tab 的错误数徽标 + 中文说明）；`开始运行` 拒绝并聚焦到第一个错误行。沿用现有 `cross_error` 红字机制并扩展为“错误列表 + 定位”。
6. **性能**：HEIF 目录 ≈40 组行（键展开 47）、AVIF ≈50 组行（键展开 61）、JXL ≈42 组行（键展开 43）——高级区仅在展开时构建（惰性 `build_rows`），搜索走缓存列表；页面仍只有一层滚动（现有 UX-8 已满足）。
7. **预设**：所有新键参与预设 v2 的 `outputs[i].params` 序列化（含 `x265:`/`aom:` 前缀键，`FormatState::extra` 已支持）；载入时未知/跨后端/未收录键按规则拒绝并提示；`quality_score` 属于**设置**不进预设。

### 4.5 引擎接线

- `enc_heif.cpp`：内省表 + 目录合并（`introspect_backends`）；透传循环按 C7 规范化并**作用域校验**；已知参数拒绝仍失败（保持），但预检已把绝大多数挡在运行前；错误文案映射中文；`complexity` 等插件特殊逻辑保持；无损强制 444 保持。
- `enc_jxl.cpp`：新增键接入（`JxlEncoderFrameSettingsSet{Option,FloatOption}`、`SetBasicInfo` 字段、`SetUpsamplingMode`、`AllowExpertOptions`、`AddBox` 压缩）；修正 `SetCodestreamLevel` 调用顺序（先 level 后 BasicInfo，见 F7/研究）；缺失键走“未设=不调用”；现有默认值路径不动。
- `params.cpp`/`params.h`：`ParamType::String` 加性枚举值 + 行控件；`ParamContext` 加性重载 `cross_validate`；`is_plugin_passthrough_key` 改为**按后端**判定；`declared_keys` 合并目录键；`has_default=false` 行不参与默认填充（已有）。
- `dev_cli.cpp`：修复已知审计缺陷（`--param` 对运行时内省后端/Libheif 透传键的类型解析：先 `introspect_backends` 建临时 FormatDef 再 `lookup_param_def`），使 `--param x265:crf=20`、`--param aom:cq-level=30` 可作证据入口；新增 `--score`（dev-only）。**`--param` 仅接受目录内键**；清单外键报“未收录参数”（与 C7 一致）。

### 4.6 控件形态与联动分级（v4 补充）

完整逐键表见 **`docs/m8-param-control-map.md`**。口径：

- **两维分类**：形态（M 模式切换 / N 数值 / D 下拉 / S 开关 / T 文本 / R 只读）× 联动角色（A 锚点 / P 依赖 / I 独立）。
- **路线锚点（可编辑 6 类）**：后端（AVIF libaom/SVT）、JXL 技术（VarDCT/Modular）、无损（`lossless`）、RC 模式、libaom `realtime`、JXL 码流输出模式（`buffering`/`output_mode`）；**档位/属性锚点**：preset、tune、位深、色度、profile/level/tier。
- **RC 模式显式化（已定）**：HEIF {质量/CRF/QP}、libaom {质量/CQ Level}、SVT {质量/QP} 用模式下拉/单选选择路径，只显示当前路径的数值行；默认“质量”= 既有 `quality`（零行为变化），无损时整组隐藏。这是实现细化，不改变参数范围。
- **新增布尔行用三态下拉（已定）** `{跟随默认/开/关}`（避免“启用勾选 + 开关”双复选框）；既有布尔行保持复选框。N/D 新行仍用“启用”勾选。
- **求值时机**：UI 实时（隐藏/置灰/锁定/警告）→ 运行前预检（错误汇总）→ 逐文件（尺寸/位深相关）→ 编码器兜底；规则表每行带 action/timing/zh+en 文案。
- **计数双口径（已定）**：评审稿 ≈40/≈50/10/≈42 是组行数（范围/报告口径）；按控件/键展开 155 可编辑 + 2 禁用/隐藏 + 13 只读 = **170 行是目录/测试/门禁唯一真源**（Stage A4 对账定稿）。

---

## 5. SSIMULACRA 2 设计（C9–C13）

### 5.1 算法与实现来源（“GitHub 有 C++ 源码 ≠ 可直接链接库”）

**先回答问题：为什么不直接链接 GitHub 上的 C++ 源码？** 因为两个上游仓库的 C++ 实现都**不是可链接的库**：

| 源码 | 事实 | 后果 |
|---|---|---|
| libjxl 0.12.0 `tools/ssimulacra2.{h,cc}` | 算法 2.1（BSD-3；权重与 Cloudinary v2.1 逐字一致），但 API 是 `jxl::ImageBundle`（`lib/jxl` **内部**类型），依赖 `lib/jxl/{image_bundle,enc_xyb,gauss_blur,status,…}` 内部头；在 libjxl 构建里挂在 **`JPEGXL_ENABLE_DEVTOOLS`** 下（`INTERNAL_TOOL_BINARIES`），`install(TARGETS ${TOOL_BINARIES})` 不含它 | vcpkg 的 libjxl **从不构建/安装**；安装树只有 `jxl/` 公共头 + `jxl.dll/jxl_cms.dll/jxl_threads.dll`，直接链接拿不到 |
| `cloudinary/ssimulacra2`（权威仓库，BSD-3 + `PATENTS`） | 公开头同样是 `jxl::ImageBundle`；仓库把 **libjxl 0.8 源码整体 vendored** 在 `src/lib/`，无 `find_package`/导出目标/`install()` | 要用就得再编一份 libjxl 0.8，与本项目 libjxl 0.12 **符号/版本冲突** |
| vcpkg 生态 | pinned tag（2026.07.29）**无 `ssimulacra2` port**；独立 overlay port 需要自己编 libjxl 内部子集 | 没有现成依赖可用 |

**所以“接入官方源码”只有三种工程形态，本设计选 A：**

| 方案 | 做法 | 优点 | 缺点/风险 |
|---|---|---|---|
| **A（推荐）官方源码 + 兼容层** | 把 libjxl `tools/ssimulacra2.{h,cc}` + `tools/gauss_blur.cc`（BSD-3，**逐字保留算法/权重/常量**）纳入 repo（新目录 `third_party/ssimulacra2/`），写约 200–300 行兼容层替换其内部依赖（Image3F/ImageF、Status、Gaussian blur、ToXYB、sRGB 线性化），图像缓冲换成我们的 float32 平面 | 用的是**官方实现本体**（不是重写算法）；零新增依赖；可控内存（参考实现 130–160 B/px、24MP 3.5GB 的问题消失）；可 AVX2 加速 | 兼容层需数值校验：PyPI `ssimulacra2` 0.3.0（BSD-3，独立实现）+（可选）dev-only libjxl 参考 CLI 做 oracle，容差 ≤0.1 / ≤0.01 |
| B（fallback）overlay 编译官方内部子集 | 从 pinned libjxl 源码只编 `ssimulacra2.cc + gauss_blur.cc + DEC 内部 TU + enc_xyb`，装静态库 + C ABI shim（`pp_ssim2_score_*`） | 最贴近官方二进制 | 内部头/子集编译（MSVC + hwy 1.4）**未验证**；参考实现两次 Copy + 全分辨率金字塔 ⇒ 24MP ≈3.5GB，仍要改源码才能进当前预算；每个 libjxl 升级要 rebase |
| C（否决）vendored 全 libjxl 0.8 | 直接编 cloudinary repo 自带源码 | 源码现成 | 双 libjxl 版本冲突/体积暴涨 |

- 采用 **A**：`third_party/ssimulacra2/` 保留 BSD-3 版权头 + 算法版本常量（`kMetricName="ssimulacra2"`, `kMetricVersion=1` = 算法 2.1）+ `PATENTS` 说明；`docs/m8-report.md` 记录“源文件哈希 ↔ libjxl commit 对照”，便于上游漂移审计。
- 若 Stage C1 数值门禁不达标，按 **B** 重做（fallback 门槛与证据写进报告）。
- CLI（libjxl 参考工具）仍不用于运行时：vcpkg 不构建它、需要临时 PNG 编码/解码、进程开销、不可取消；仅作 dev-only 真值 oracle。

### 5.2 数据流（管线内积分点）

```
decode → orient → flatten → color → [per target] encode → **score** → metawrite → mtime
                                                     │
   参考 = 本 target 的量化样本：管线 float32 帧按 out_bitdepth（+dither8，与编码器同一量化核）
          量化为整数网格 → 反归一化为 float → 转 sRGB 线性
   失真 = decode_float(row.out)（OIIO 回读，raw 目标空间值）→ 转 sRGB 线性
   两侧用**同一条**目标空间→sRGB 变换（目标=sRGB 时就是精确 sRGB EOTF，无 lcms2 开销）
```

- 逐输出计算（不是逐文件一次），**适用于全部 8 个输出格式**（含 JPEG/PNG/TIFF/WebP/BMP；无损产物应≈100）；灰度产物两侧复制为 RGB 后评价；尺寸不一致/回读失败/取消/任一边 <8px ⇒ `Unavailable`/`Off`（**不得**把 <8px 交给库——库会静默返回 100），不影响输出成功。
- 色彩归一实现：`core/colormanager.h` 现有 `ColorManager::transform(buf, src_icc, src_is_gray, target, opts, ...)` 已覆盖“任意 ICC/内建 profile → sRGB”的路径（`target=ColorTarget::SRGB`；P3/AdobeRGB 的 ICC 由 `load_target_icc()` 取得，`keep` 用源 ICC）。指标模块只吃**sRGB 线性 RGB**；sRGB 目标时由模块内精确 sRGB EOTF 一步完成（无 lcms）。该路径为**加性调用**，`colormanager.h` 不在 PP-FROZEN 清单内，无需解冻。
- 无损目标（JXL/PNG/TIFF/WebP-lossless 等）应得 **100.0**；HEIF/AVIF 的“码流无损”会因 RGB↔YCbCr 舍入 <100（如实显示，不写死 100）。
- 分数可为负（极差质量），显示/存储按实数处理；不做 clamp。
- 文件级复用：同一 target 位深的量化参考可在多输出间复用（若首版不做缓存，实测后决定；不影响正确性）。

### 5.3 性能与内存（设计目标，Stage C1/C2 实测）

- 参考实现口径 ≈104.5 ms/MP（M4 Pro，含 PNG 解码），x86 折算 ≈120–245 ms/MP（[inferred]）⇒ 24MP 约 3–6 s/输出；参考实现峰值内存 **130–160 B/px**（24MP≈3.5 GB，2 次 Copy + 金字塔），不可接受。本设计：
  - **流式/分块**：不物化整幅 XYB 6-scale 金字塔；按行带（带 5 行 halo）计算模糊/SSIM/边缘图并累加 108 范数，峰值内存 ≤1 帧额外（落在现有 2×frame 预算内）；
  - **AVX2 热点内核**：XYB 转换、5-tap 可分离模糊、2× 降采样、SSIM/边缘图统计；scalar 等价实现保留作测试对照；
  - 目标：实测并记录 ms/MP；若 24MP 单输出 >2 s（预计会发生），接受“批处理墙钟随并行 workers 摊薄”并如实披露；若用户不可接受，退路见 §11「评分性能退路」（快速模式/首输出评分，P2，不默认）。
- 计算位置：worker 线程、encode 后、metawrite 前；`is_cancelled` 前后检查 + 分块间检查（取消不拖秒级）；不新增线程、不改 `ΣE ≤ T` 口径。
- 像素预算：流式实现 **不改变** `2×frame` acquire 与 `need_bytes/peak_bytes` 日志（回归零 diff 的关键之一）。注：解码回读的产物帧（1 帧）落在现有预留内（编码期解码缓冲已释放），指标模块自身只允许小块 scratch；若实测必须额外 acquire，则只允许在“评分开启”的运行中出现（`--dev` 回归不评分 ⇒ 仍零 diff），并登记预算口径变化。

### 5.4 结果面

- `OutputResult` 加性字段（C11）；运行页输出行隐藏式子行（C12）；tooltip 文案：
  “SSIMULACRA 2（0–100，越高越接近编码输入）：90≈视觉无损、80≈极高、70≈高、50≈中等；本分数评价**编码/量化损失**（对比编码器输入），无损目标应为 100。”
- sidecar（仅 `--dev`）：
  ```json
  "metric": {"name":"ssimulacra2","version":1,"state":"ok","score":92.31,
             "ms":412.7,"reference":"encoder-input","colorspace":"sRGB","note":""}
  ```
- `pp_verify`：`assert.score = {"min": 99.0}` / `{"min":70,"max":100}` / `{"state":"ok"}`（区间断言，SCHEMA.md 加性段）。
- 日志：评分开启时每输出一条 `log_info("quality metric", ...)`（回归不带 `--score` ⇒ 零 diff）；失败仅为 `Unavailable/Failed` 记录，**不产生 Warning**。

### 5.5 验证（数值可信度）

1. **黄金对照**：同一组 5–8 对图像（覆盖 8/16 位、灰度、JPEG/JXL/HEIF/AVIF/PNG）分别用 ① 本实现 ② PyPI `ssimulacra2` 0.3.0（BSD-3，独立 Python 实现，装进 repo venv）计算，要求 |Δ|≤0.1（记录版本/命令/配对图像到 `m8-report.md`）；如可行，再用 **dev-only 构建的 libjxl 参考 CLI**（libjxl 源码 + `JPEGXL_ENABLE_DEVTOOLS=ON`，不进构建矩阵）复核 2–3 对，|Δ|≤0.01。两者任一超差 ⇒ 触发 §5.1 的 fallback 并记录归因。
2. **恒等**：同一图像自比 = 100.0（逐样本相同）；8×8/奇数尺寸/灰度/16bit 边界用例。
3. **无损语义**：JXL/PNG/TIFF 无损产物 ≥99.9（8-bit 源应为 100.0）；HEIF/AVIF 码流无损如实 <100（记录实测值）。
4. **取消/错误路径**：取消不产生半评分；解码失败 ⇒ `Unavailable` 且输出仍 `ok`。
5. **性能**：24MP 级样本测量 ms/MP 与峰值 RSS，写进报告（含“评分开/关”对批处理 wall time 的影响）。

---

## 6. UI 设计（运行页 / 输出页）

### 6.1 运行页（结果）——评分

- 现有行骨架 6 列不动；每个输出行下加隐藏子行（`pp-run-score-<i>-<j>` / `pp-run-score-text-<i>-<j>`，缩进 36px，`fk::kRowPitch`，默认隐藏）；`metric_state==Ok` 时显示 `质量 SSIMULACRA 2 · 92.31`，色阶 ≥90 绿 / 80–90 中性 / 70–80 琥珀 / <70 强调（复用 `theme.h` 语义色，必要时加性 token）；tooltip 见 §5.4。
- 文件组行不显示分数（v1 不做平均；若确认后要“运行平均分”，作为 P2 加 `RunSummary` 加性字段 + 总览卡第 9 项，需另行布局裁定）。
- `Unavailable/Failed`：显示 `—（原因）`；`Off`（评分关闭/仅元数据）：不显示子行（“未发生的量不显示”）。
- 现有 `run-geom`/`layout-audit`/`theme-equiv` 探针：新增行加进 `run-geom` 名称表（加性），不动既有断言；截图 12 张不增不减（视觉变化在报告说明）。

### 6.2 输出页（配置）——参数

- 见 §4.4。新增 objectNames：`pp-param-enable-<key>`、`pp-param-status-<i>`、`pp-raw-params-*`、`pp-rule-error-*`（全部加性；不重命名/删除既有 `pp-*`）。
- 只读/禁用/无效果三态视觉：沿用 `formkit` 的禁用态 + 行内 hint（theme 令牌），不新造样式体系。
- 高级区保持自然高度 + 二级分组 + 搜索（UX-8 已落地，本次只扩词表/行语义，不推翻布局）。

---

## 7. 冻结面与裁定登记（开工前先登记）

| 冻结面 | 改动 | 追认方式 |
|---|---|---|
| `core/types.h` | 零改动（评分字段不进 `Timing`/`WarningKind`） | 无需 |
| `core/pipeline.h` | `OutputResult`/`RunConfig` **末尾**加性字段 + `MetricState` 枚举（新类型声明在 OutputResult 前） | 加性 + `docs/m8-adjudications.md` |
| `core/params.h` | `ParamType::String` 加性枚举值；`ParamDef` 末尾加性字段（`requires_enable`）；`ParamContext` + `cross_validate` 加性重载；`kGroup*` 词表加性扩展 | 加性 + 追认；旧签名/旧行为零改动 |
| `codecs/encoder.h` | 零改动 | 无需 |
| `ui/paramform.h` | 加性方法（`set_backend_context`/启用态；无自由编辑器，见 C8） | 加性 + 追认 |
| `settings.h` | 末尾加性字段 `quality_score` | A28/M6-T11 先例 + 追认 |
| UI `pp-*` objectName | 只加不删（新增清单见 §6） | 冻结口径允许加性（`pp-run-warn-*`/`pp-run-reason-*` 先例） |
| `tests/golden/SCHEMA.md` | 加性追加 metric/sidecar/`assert.score` | 加性 + 追认 |
| `--dev` CLI 面 | 新增 `--score`（dev-only，加性） | 加性 + 追认 |
| 第三方源码 | `third_party/ssimulacra2/`（BSD-3 + PATENTS；官方算法 2.1 源码 + 兼容层） | 许可清单/关于页登记 + 追认；**不是依赖面变更**（不新增 vcpkg port、不改 overlay、不升库版本） |
| 回归基线 | **不重播种**（预期 zero diff）；如不可避免，先复核归因并单独裁定 | AGENTS §5.3 |

> 既有单测里钉死“tech 参数个数/format 参数个数”的断言（如 `test_params.cpp` 的 `size == params+1`）会随目录新增行做**语义适配**（不是放宽断言）；提交说明逐条列明。隐藏/未启用行不得进入 `public_values()`/预设/日志快照（三态语义的验收项）。

---

## 8. 实施计划（v3：三阶段串行、阶段内并行）

> **用户裁定工作流（2026-10-28）**：三阶段**串行穿行**——① 参数事实层 → ② 引擎 + UI → ③ SSIMULACRA 2 → 收口；**阶段内可并行**。每阶段结束 = **一个可验收增量**（构建 + 单测 + 门禁 + 提交 + 报告增量），不攒大爆炸。评分与参数面不并行推进（避免跨阶段接口/契约漂移）。
> 详细任务卡、写作用域、依赖图、门禁命令见 **`docs/m8-workflow.md`**。

### Stage A — 参数事实层（目录/规则/脚本；≈2–3 天，3 卡并行）

- A1 JXL 目录 + 规则数据（14 项新增 + R1–R22 可落地规则；每行含控件形态/联动角色，见 §4.6）；
- A2 x265 目录（≈33 新增 + 4 只读行）+ 规则数据/24 条冲突镜像 + 名称/值等价表（每行含形态/角色）；
- A3 libaom 目录（≈40 组行）+ SVT 只读/限制登记（10 键、无 `svt:`）（每行含形态/角色）；
- A4（Lead）目录 API/生成脚本/清单对账测试 + 单测不变量（含控件形态/角色完整性与计数对账）。
- **出口**：目录数据 + 生成脚本 + 单测（不变量/对账/范围）全绿；**零用户可见行为变化、回归 zero diff**；提交 1。

### Stage B — 引擎 + UI（≈3–4 天，引擎线与 UI 线并行 + Lead 集成）

- B1 引擎：`ParamType::String` 加性、三态语义、`ParamContext` + 规则引擎、后端作用域（清单外封死）、`enc_heif`/`enc_jxl` 接线、运行前预检、`--param` 类型修复、`pp_linkprobe` 扩展；
- B2 UI：ParamForm「启用/跟随默认」三态、新增布尔行三态下拉、RC 模式控件（G4）、只读/禁用原因、分组词表扩展、搜索过滤、预检错误清单/定位、预设往返、`quality_score` 设置行（仅框架）；
- （Lead 先冻结 `params.h` 加性契约与 UI 接口，再并行两线。）
- **出口**：GUI 可调全部清单参数 + 非法组合运行前拦截；`ctest`/金样 20/20/回归 zero diff/`ui-smoke` 冻结末行全绿；手动验收截图/说明进报告；提交 2。

### Stage C — SSIMULACRA 2（≈3–4 天，3 卡并行）

- C1 指标核心：`third_party/ssimulacra2/`（官方源码 + 兼容层）、streaming + scalar、oracle 数值门禁、单测（恒等/已知答案/灰度/16bit/尺寸/取消/确定性）、AVX2 决策；
- C2 管线/结果面：`RunConfig::quality_score` + `OutputResult` 字段、量化参考、`probe_file`+`decode_float`、sRGB 归一、sidecar `metric`、运行页评分子行、settings 开关、`--dev --score`；
- C3 证据/门禁：金样 `assert.score`（区间）、`pp_verify` + SCHEMA 加性、ui-smoke 探针、性能/内存实测报告；
- **出口**：每个输出显示分数（默认开）；真无损=100；HEIF/AVIF 码流无损如实；回归 zero diff；提交 3。

### Stage D — 收口（≈1 天）

- D1 `docs/m8-report.md`（交付/验收/偏差/未覆盖/复现）、`CHANGELOG.md`、`README.md`、`docs/param-catalog.md`、关于页/许可（BSD-3 + PATENTS）；
- D2 双平台构建 + 全门禁复跑（ctest/金样/回归/ui-smoke/format/tidy）；
- D3 提交 4（收口提交不入发行 tag，按发行纪律）。

**工期**：单人 **≈15–18 人日**；阶段内 2–3 个并行 agent + Lead 集成 **≈9–11 个工作日**。SSIMULACRA 2（Stage C）与“参数数据面”（Stage A）是最大不确定项；评分默认开 ⇒ Stage C 必须带性能/内存实测数据。

---

## 9. 验收门禁（逐条取证，进 `m8-report.md`）

| 门禁 | 命令 | 期望 |
|---|---|---|
| 构建 | `python tools/env.py run -- cmake --build build/release-dev` + release | 双树通过；`pp_linkprobe` 参数面断言（无 overlay 变更，SVT=10 键事实） |
| 单测 | `python tools/env.py run -- ctest --test-dir build/release-dev` | 全绿（新增 `test_param_catalog`/`test_param_control_map`/`test_param_rules`/`test_quality_metric` 等） |
| 金样 | `python tests/golden/smoke.py build/release-dev`（内部带 `--score`） | `SMOKE total=20 pass=20 fail=0`（含新增 `assert.score`） |
| 回归 | `python tools/regression.py` | **zero diff**（未启用评分；新参数默认不上报；info 行不变；预算行不变） |
| UI | `python tests/ui_smoke.py build/release-dev` | `UI-SMOKE OK shots=12 pages=3`；`layout-audit bad=0`；新增探针通过 |
| 格式 | `bash tools/ci-format.sh` | 0 violation |
| 静态 | `python tools/ci-tidy.py build/release-dev` | 棘轮不增 |
| 密钥 | 按 `docs/m6-report.md` §6 | 0 命中 |
| SSIM 数值 | 报告内命令（oracle 对照脚本） | |Δ| ≤ 0.1（PyPI oracle）；恒等=100；无损 ≥99.9 |
| 参数面 | `--dev` 证据清单（每后端键数/三态计数/控件形态） | 组行 HEIF ≈40、libaom ≈50、SVT 10、JXL ≈42；逐键形态/角色/状态可查（键展开计数见 `docs/m8-param-control-map.md` §9）；清单外参数按类登记（评审稿 §5） |
| 非法组合 | 预检测试用例表（每规则 ≥1 例） | 100% 在运行前被拦，中文原因可操作；无“逐文件重复失败” |
| 清单对账 | 评审稿条目 ↔ `param_catalog` 行 | 逐条对账表进 `m8-report.md`（不允许多/少而不说明） |

---

## 10. 风险与未决项（如实）

| # | 风险/未决 | 影响 | 缓解/待确认 |
|---|---|---|---|
| R1 | 评分计算成本（≈120–245 ms/MP，24MP 3–6 s/输出） | 批处理时间显著增加 | 流式 + AVX2 + 设置开关（默认 GUI 开）；报告实测 wall time；如不可接受再谈“按需评分/快速模式” |
| R2 | 官方源码 + 兼容层的数值偏差/兼容层 bug | 分数可信度 | Stage C1 oracle 对照（PyPI；可选 dev-only libjxl CLI）；容差与证据进报告；算法版本常量固定；不达标切方案 B |
| R3 | **SVT-AV1 全参数面不达标**（用户裁定不打补丁：只有 10 原生键，121 个 parse 名不可达） | 与“全参数暴露”需求有落差 | 已由用户裁定接受；文档/关于页/linkprobe 三处登记为“libheif 上游限制”；**不再作为缺陷追踪** |
| R4 | ≈40–50 行参数 UI 的可读性/性能 | 体验 | 三态 + 分组折叠 + 搜索 + 惰性构建；评审稿已按重要性分级 |
| R11 | 评审清单与上游真实能力/默认值漂移（x265/aom 升级） | 参数表失真 | 目录锚点由脚本从 pinned 源码抽取；linkprobe/单测断言锁定计数；升级时重跑抽取并 diff；清单变更走评审 |
| R5 | 部分非法组合只有编码期才能发现（x265 跨字段检查不在公开 API；libheif 把失败折叠成一句话） | 预检覆盖非 100% | L0/L1/L2 三层规则（L1 与 `param_parse` 的 `BAD_NAME/BAD_VALUE` 100% 等价；L2 镜像 24 条可机械判定冲突）；编码器失败文案中文化并含参数名/值；`--dev` 语料回归兜底；升级 x265 时重核 L2 表 |
| R6 | **评分默认开**带来的批处理耗时/峰值内存（用户已选默认开、8 格式全评） | 大批量墙钟变长、内存占用升高 | Stage C 必测 12/24MP 的 ms/MP、批处理 wall time、峰值 RSS 并写进报告；设置可关；流式实现保证不破像素预算 |
| R7 | “全暴露”口径被理解为 CLI 全量 | 期望落差 | 文档/UI 明确三态与不可达原因（§2 定义）；`docs/param-catalog.md` 逐键列出“不可达/上游固定” |
| R8 | 金样/回归证据面变化 | 门禁 | 全部加性；评分默认不在 `--dev`；不重播种；如不可避免先复核归因并单独裁定 |
| R9 | 非 sRGB 目标的 sRGB 归一开销/一致性 | 数值口径/性能 | 参考与失真走**同一条** ColorManager 变换（目标=sRGB 时零 lcms）；单测钉住“同变换下无损=100”；开销计入实测报告（相对指标本身可忽略） |
| R10 | 第三方源码/许可与 CI 20min 上限 | 构建/发行 | 只 vendored BSD-3 源码（不新增依赖/不改 overlay）；许可清单与 AppImage/zip 检查；评分相关 golden 仅 2–4 case |

**待用户确认的裁定点**见 §11。

---

## 11. 裁定记录（2026-10-28 交互问答已定）与剩余事项

| # | 裁定点 | 结论（已定） |
|---|---|---|
| D1 | 参数范围 | **按评审清单定稿**（x265 ≈40 / libaom ≈50 / SVT 10 / JXL ≈42），见 `docs/m8-param-exposure-review.md` |
| D2 | 清单外参数 | **完全封死**：未收录 `x265:`/`aom:` 键 = 预检错误 |
| D3 | SVT-AV1 补丁 | **不打补丁**：仅 10 原生键；121 个 parse 名登记为库面限制 |
| D4 | SSIMULACRA 2 接入 | **方案 A：官方源码 + 兼容层**（算法/权重逐字保留）；B 仅作数值门禁失败 fallback |
| D5 | 评分默认 | **GUI 默认开、8 种输出全部评分**；参考=**量化后编码器输入**（真无损=100）；结果页用输出行下隐藏子行 |
| D6 | 线程池 | **不作为格式参数**（`pools`/`frame-threads` 移除；不加 HEIF 线程池设置）；交给软件默认调度；R-078 如实登记 |
| D7 | 工作流 | **三阶段串行**（参数 → 引擎+UI → 评分 → 收口），阶段内并行；每阶段一个可验收增量 |
| D8 | 交付节奏 | 每阶段提交 + 验收，最后统一收口 |
| D9 | 控件形态/联动分级 | **RC 模式显式下拉、新增布尔行三态下拉（已定）**；计数双口径：报告 ≈142 组行，目录/测试/门禁以 170 键展开为唯一真源；详情 `docs/m8-param-control-map.md` |
| D10 | 开发纪律 | **按 `docs/m8-discipline.md`**：任务卡自包含/约束清晰；**子任务不得再次派发子任务**；未“开工”不写实现 |

**剩余事项**

1. **开工确认**：你回一句“开工”即按 §8 / `docs/m8-workflow.md` 进入 Stage A。
2. **评分性能退路（Stage C 实测后定，不阻塞开工）**：若 24MP 单输出耗时/批处理墙钟不可接受，可选 P2“快速评分（半分辨率，明确标注近似）”或“仅首输出评分”——默认**不做**，先以实测数据说话。
3. 评审清单如需个别增删，直接指出键名（例如“加 x265:scaling-list / 删 x265:ssim-rd”），我按增删更新目录后再开工。
4. **控件形态/联动分级（已确认）**：RC 模式显式下拉、新增布尔行三态下拉、计数双口径（组行 ≈142 / 键展开 170，后者为测试真源）随设计冻结；不再有异议项。
5. **开发纪律（已定）**：`docs/m8-discipline.md` 为开工前置条件；其中“任务卡必须自包含/约束清晰”和“子任务不得再次派发子任务”为额外纪律。

---

## 12. 参考与证据

**联网/文档**
- x265 CLI 文档（“除非 CLI ONLY 均支持 x265_param_parse”）：https://x265.readthedocs.io/en/master/cli.html
- libheif 仓库/插件源码（x265/aom/SVT）：https://github.com/strukturag/libheif（本地 vcpkg buildtree `v1.23.5-6368e7938c.clean`）
- libaom 仓库（`av1/av1_cx_iface.c`、`av1/arg_defs.c`、`aom/aomcx.h`）：https://aomedia.googlesource.com/aom
- SVT-AV1（`Source/API/EbSvtAv1Enc.h`、`Source/Lib/Globals/enc_settings.c`）：https://gitlab.com/AOMediaCodec/SVT-AV1
- libjxl 0.12.0 源码（`lib/include/jxl/encode.h`、`lib/jxl/encode.cc`、`tools/ssimulacra2.*`）：https://github.com/libjxl/libjxl/tree/v0.12.0
- SSIMULACRA 2 参考实现/权重（BSD-3 + PATENTS）：https://github.com/cloudinary/ssimulacra2 ；libjxl 0.12.0 内同算法源码：https://github.com/libjxl/libjxl/tree/v0.12.0/tools ；PyPI 独立实现（BSD-3，数值 oracle）：https://pypi.org/project/ssimulacra2/
- SSIMULACRA 2 论文：https://arxiv.org/abs/2309.02960
- 参数评审清单（本仓库）：`docs/m8-param-exposure-review.md`
- 控件形态与联动分级（本仓库）：`docs/m8-param-control-map.md`
- 开发纪律（本仓库）：`docs/m8-discipline.md`
- 开发 workflow（本仓库）：`docs/m8-workflow.md`

**仓库内既有证据**
- `docs/m8-param-exposure-review.md`（**本设计的参数评审清单**：逐库/逐项重要性/理由/不暴露分类）
- `docs/encoder-config-audit-2026-10-25.md`（84 格式条目 + 92 跨面条目；HEIF/AVIF/JXL 逐键矩阵与实测）
- `docs/encoder-audit-decisions-r2-2026-10-25.md`（alpha 不支持 / TIFF 多页不支持 / bug 二分口径）
- `docs/param-catalog.md`（参数清单草案，本设计将按评审清单重写）
- `docs/ux-review-14.md` §8 高级参数区（UX-8 已落地：自然高度 + 二级折叠 + 搜索）
- `src/core/params.h`、`src/codecs/enc_heif.cpp`、`src/codecs/enc_jxl.cpp`、`src/ui/paramform.cpp`、`src/ui/page_run.cpp`、`src/dev_cli.cpp`、`tools/regression.py`、`tests/golden/SCHEMA.md`
- 本次调研工作笔记（gitignored，开发时随报告引用）：`build/research/{codebase-params,codebase-scoring,jxl-params,x265-heif-params,avif-params,ssimulacra2}.md`

---

*本文是设计合同草案；确认后冻结为 M8 契约（C1–C14），实现期变更须走 `docs/m8-adjudications.md`。*
