# M7-R2 轮方案 —— 遗留 finding 处置、14 项技术设计定稿、波次/包/文件面计划与纪律

> **这份文档是什么**：M7-R2 轮（2026-10-12，版本口径 `CMakeLists.txt:19` = 0.3.3）的**方案真源**。
> 内容四块：① 遗留 finding 四类审视表（上一轮收口后剩余 finding 如何分桶）；② 14 项技术设计
> 定稿（色彩能力与解码语义面，R2A 实施）；③ 波次 / 包 / 文件面计划（R2A–R2F + R2REL）；④ 纪律。
> **出处收口**：`docs/m7-report.md` 与 `docs/m7-adjudications.md` 中「任务书 / 任务包」引述
> 自本文起指向本文件（附 A 给出 M7-WP 系分派书的引述内容摘录，换机可独立复核；各包逐条
> 交付与验证证据见 `docs/m7-evidence/` 与各 `build/m7r2-*/handoff.md`）。
> **编号命名空间**：`R-nnn` = `docs/deep-review-0.3.2.md`；`UX-n` = `docs/ux-review-14.md`
> （其内部色彩分级子项写「UX-14 A1/C1…」式全称）；`A/B/C/D/E`、`M7-ADJ-*`、`WP12-B*`、`WP13-B*`、
> `R2A-A*`、`M7R2B-*`、`R2C-W*`、`R2D1-A*`、`M7-R2-A/B`、`M7R2D2-*`、`M7R2E2-*`、`R2G-G*`、
> `R2F-X*`、`R2F-C*` = 裁定表编号
> （`docs/m7-adjudications.md`）；`D-n` = AGENTS §9 实现期发现缺陷号（如 D-1）。

---

## 0. 背景

M7-WP1…WP13 完成 331+14 条批量整改与文档收口后，剩余 finding 的性质并不单一：有一批是
**评审误识别**（前提即不成立），有一批受**平台 / 依赖 / 冻结面**约束只能按约束下口径收口，
有一批是**本轮可修**的行为尾单与能力缺口，其余是**结构性债**（登记，等独立窗口）。R2 轮
按四类分桶处置，行为面由金样 20 对 + 全语料回归 + ui_smoke 冻结行三重背书；文档面由本文件
+ `docs/m7-report.md` 台账终态 + `docs/m7-adjudications.md` 裁定续表承载。

---

## 1. 遗留 finding 四类审视表

去向口径（与 m7-report 覆盖台账 R2 终态四档一致）：**已修**（含提交哈希）/ **误识别关闭** /
**受约束口径闭环** / **登记**。逐条终态见 `docs/m7-report.md` 附录 A/B（331+14 全集）；
本表只给分桶判据与代表条目。

| 桶 | 判据 | 代表条目（R2 轮处置） |
|---|---|---|
| **① 本轮修复** | 评审发现属实、修法明确、不动引擎判据面或经授权换锚 | R-009/025/026/143/144/148/149/184/251/276/277/278/280/314、R-066/070/072/077/135/136/137/138/217/226/228/238/249/265/266/270/318、R-076/104/195/206/211/231、R-190/191/203/275/281/326、R-218（RTL）、UX-1 卡①–⑨（⑩–⑫ 运行页三卡 = WP6 `d99df1b` 已接）/ UX-2 全选入口 / UX-4 stretch / UX-6③ / UX-8 UI 半 / UX-10.1 / UX-11 / UX-12 重放链、UX-14 C1–C4 + A1 + B2 + D1–D3 |
| **② 误识别关闭** | 评审前提不成立（死码 / 无消费 / 任务书字面与实现矛盾），按证据证伪关闭，不为关闭而改码 | R-282 死码前提（BMP 分支 = §5.8 文档化 API + `test_post_write_formats` 钉住；`with_extension` = PP-FROZEN + test_fsops 4 例覆盖 → C1/C2 保留裁定）、R-011 证伪（`tiff:Predictor` 上游 OIIO 3.1.17 `tiffoutput.cpp:1026-1029` 真实消费；键名对齐已随 WP2 落地）、WP5-A10 任务书笔误（「一行」登记 → 实际两行才是可达做法，见附 A） |
| **③ 受约束口径闭环** | 外部约束（库事实 / 平台 API / 冻结面 / 宿主能力）使原修法不可达，在约束下把口径收干净并如实登记 | **B3 BMP ICC**：OIIO BMP writer 不承载 ICC（测试钉现状）→ 产品口径取「警示」（`WarningKind::EmbedIccUnsupported`，keep 不虚报）；表内 tooltip 无可标注面（FormatDef 无 tooltip 字段）——受约束不扩冻结面。**非 Windows 密钥**：DPAPI 仅 Windows → 非 Windows = `ppsec2:` 编码 + 0600，口径如实「编码不是加密」（引入 keyring 属负面清单禁扩依赖）。**Linux golden.log 移植**：Windows 宿主不做文本替换移植（R-015 反面先例）→ 归 Linux 宿主 / nightly `update_baseline` 通道重播种。**R-325 强类型**：非空容器包装 / bps-bpp 拆字段会改 PP-FROZEN 签名 → 维持「登记待解冻」，以 `validate_run_config` 校验器 + `file_state_of`/`set_output_state` 单点替代 |
| **④ 登记留后续** | 结构性重构 / 无判据面性能项 / 跨面设计题，本轮不做假收敛 | 大单元拆分余量（page_meta/page_output/mapwidget/SettingsDialog ctor/filelistmodel）、R-185/187/188/196–201/204/205、R-254/255/256/259、R-260/264/269/279 余量、R-319/327/329/330、R-068、R-133 消费端、R-137 余量、R-145/239 CLI 侧、R-108 UA、R-109 库化、R-281 余量、UX-12 像素档探针、R-317 不编数口径（M5-A24，非缺陷）、R-118 三处注释标注 |

复核零增量（判定「已修，无需再动」）：R-069 / R-142 / R-229 / R-241（git -S 取证 + 链路复核）。

---

## 2. 14 项技术设计定稿（色彩能力与解码语义面；R2A = `c540630` 实施）

> 定稿口径 = 主对话设计裁定 + R2A handoff「① 逐条改法与落点 / ② 裁定登记」；护栏断言逐条
> 在 `tests/unit/test_{color,simd,decode,thumbs}.cpp`，读数摘录见 `docs/m7-evidence/m7r2-a-gates.md`。
> **默认值 = 现状字节级一致**（四能力 opt-in、默认关）。

| # | 设计 | 定稿口径 | 判据 |
|---|---|---|---|
| 1 | **C1 色域映射**（UX-14 C1） | 三档：0 = 无操作（硬裁现状，逐字节不变）；1 = 软膝 `soft_knee`（knee=0.85 公式定稿、负侧对称）；2 = **Lab 空间 L 轴收敛**（保亮度、压色度，二分 16 步回投；hue 由构造保持）。只处理越界像素，alpha 不经手 | mode0 与旧签名逐字等价断言；ΔH′ 护栏：mode2 ≤ 0.05× 硬裁、mode1 满饱和 max/mean ≤ 0.85×、中饱和 max≤0.6×/mean≤0.5×（实测定标，R2A-A7） |
| 2 | **C2 8bit TPDF 抖动**（UX-14 C2） | 噪声 `0.5·(h1+h2−1)` ∈ ±0.5 LSB，整数哈希双 seed → 确定性/回归幂等；量化 = `((clamp·255+0.5)+noise)` 截断（算子序逐字）；接线 = jpegli/webp/jxl/oiio 的 to_u8 量化段；heif/avif 走 quantize16 面**不接**（登记） | ref/avx2/分派/强制标量四路逐位相等、\|dithered−plain\| ≤ 1 LSB；默认 false = quantize8 逐字不变 |
| 3 | **C3 渲染意图**（UX-14 C3） | 0 = RC+BPC（现状逐字）/ 1 = Perceptual / 2 = Absolute（明示不叠 BPC）/ 3 = Saturation（1/3 不叠 BPC，R2A-A5）；越界回退 0；变换缓存键并入 intent/flags | 4 档位 = 4 缓存键；intent=0 与旧签名逐字等价 |
| 4 | **C4 线性光重采样**（UX-14 C4） | **仅显示面**：`thumbs::scale_to_rgba8` 旗标为真时 sRGB EOTF → 线性 → LANCZOS3 → 逆 EOTF；`decode_preview_detailed` 末位缺省参数（加性）；默认 false 逐字不变 | 2×1 黑白→1×1 判别（sRGB 域 ≈128 / 线性 ≈188，差 >30）；省略参数 == 显式 false |
| 5 | **D1 JPEG CMYK/YCCK 一致拒收** | `spec_is_cmyk` 增查 `jpeg:ColorSpace ∈ {CMYK, YCbCrK}`；probe/decode 双面错误串逐字 `"CMYK input is not supported"`（golden.log:189 钉住未动） | 合成夹具（jpegli 写 JCS_CMYK + APP14 信令变体，**不动 tests/golden**）+ 双面逐字拒收断言 |
| 6 | **D2 Exif.Photo.ColorSpace(0xA001)** | 无 ICC 且 ==2 → 合成 AdobeRGB 入口（EXIF 声明像素实际所在空间，CIPA DC-008）；=1/缺失 → 现状 | spec 面 5 例 + 文件面（exiv2 真写回读）+ 端到端（keep 模式输出 ICC 字节比对） |
| 7 | **D3 HDR 诚实口径** | transfer∈{16,18}（PQ/HLG，含 BT.2100 基组合）的 miss 文案换锚「HDR（PQ/HLG/BT.2100）暂不支持：保留合成 ICC 处理，不做 sRGB 重解释」；其余 miss 文案逐字不动；**不建 HDR 管线、不拒收**（负面清单） | test_color `misses[]` 等价严格度换锚（7 改 + 3 增） |
| 8 | **UX-14 A1 nclx 精确码**（收口 E1） | `heif_context_encode_image` 显式传 `output_nclx_profile`（栈上 nclx 活过本次 encode，libheif 1.23.5 已查证）；sRGB→1、P3→12 | **码流 VUI 位流直读**（hvcC→SPS→RBSP，测试内位流解析器）：P3=12、sRGB=1；解码侧 `heif_image_get_nclx` 回填仅对照留证（随库漂移，不作判据，R2A-A8） |
| 9 | **UX-10.2 白名单逐插件对表** | x265 = quality/lossless/chroma/**preset**/tune；SVT-AV1/libaom = quality/lossless/chroma/**speed**/tune（双向错位不再靠「恰好不暴露」掩盖） | 内省断言 + ui_smoke 档位行 |
| 10 | **R-296 CICP 表驱动**（收口 E9） | `kCicpTable`（CicpRow{primaries,transfer,source,name,spec}）单源，`map_cicp_source`/`cicp_profile_spec` 都查表；加一对映射 = 加一行（联动点 ≤ 3） | test_cicp_mapping 全绿（既有五对行为逐字不变） |
| 11 | **R-302 构建级 FP 收缩关闭** | `src/core/simd/{kernels,resample}.cpp` 两热路径 TU：MSVC `/fp:precise`、GCC/Clang `-ffp-contract=off`，与既有 `#pragma STDC FP_CONTRACT OFF` 双保险（CMakeLists 唯一一处） | `build.ninja` 编译行 grep 命中自证 |
| 12 | **R-163 取消归类单点**（收口 E3） | encode 失败分支：`is_cancelled(…) || error 含 "encode cancelled"` → 行错误串规范化 `encode cancelled` → `ErrorKind::Cancelled` → `set_output_state(row, Cancelled)`（唯一迁移点）→ `Verdict::Cancelled` 短路 | run_one_file 行级取消四面一致断言（verdict/file/行 state/error_kind） |
| 13 | **B3 BMP 不承载 ICC 警示**（受约束项，收口 E2 产品口径） | `WarningKind` 末尾加性追加 `EmbedIccUnsupported`；`meta_path=="none"` 且色彩目标 ≠ keep 时逐输出报「bmp output cannot embed the ICC profile…」；keep **不报**（不虚报）；表内 tooltip 无可标注面 → 受约束登记 | test_bmp_icc_warning（srgb 报 / keep 不报） |
| 14 | **ΔE 护栏扩强**（UX-14 B1） | 18 样本（灰阶 + 记忆色 + 饱和 ramp）× sRGB↔P3↔AdobeRGB 往返 ΔE2000 ≤ 1.0；**跨库对拍**（UX-14 B2 收口）：lcms2 vs OIIO ≤ 1.5；Rec709 侧用 OIIO `color.h` 曲线（本 OIIO 无 "Rec709" 命名色域，R2A-A10） | 实测 worst 0.0032/0.0025（往返）、0.0012/0.00034/~5e-6（对拍） |

---

## 3. 波次 / 包 / 文件面计划

| 波次 | 包 | 提交 | 独占文件面（代表） | 内容 |
|---|---|---|---|---|
| 波 1（引擎/核心/工程，三包并行） | **R2A** 色彩能力与解码语义 | `c540630` | `src/core/colormanager.*`、`src/core/thumbs.*`、`src/core/simd/*`、`src/decode/oiio_reader.*`、`src/codecs/enc_heif.cpp` + `tests/unit/test_{color,simd,decode,thumbs}.cpp`；跨界最小加性：`core/types.h`（WarningKind）、`codecs/encoder.h`（dither8） | §2 的 14 项设计实施 |
| | **R2B** 核心与参数域尾单 | `5d19146` | `src/core/{scheduler,params,fsops,presets,settings,metadata}.*`、新头 `src/core/error_text.h`、`tools/regression.py` + 7 个 core 单测 | R-076/231/206/195/211/007/104、error_text 中文单源、regression E8、R-325 核对 |
| | **R2C** CI 与发布链 | `210fce5` | `.github/workflows/**`、`tools/ci-*.{py,sh}`、`tools/make_winzip.py`、`tools/make_appimage.sh`、`docs/m7-evidence/**`（新建） | warm-cache YAML 非法修复、发布链预算取证、winzip 五门禁、证据留存机制、M7-GATE 索引对照表 |
| 波次收口门禁 | **R2-gate** | `09b5340` | `tools/baseline/*`（回归基线） | 132 行 = 66 对逐行归因重播种（A1 nclx：62 对行级 +20B + 汇总级联 4 对），见 `docs/m7-evidence/m7r2-gate-regression-reseed.md` |
| 波 2（UI 行为尾单，两包并行） | **R2D1** 主窗/列表/预览/分类/page_meta | `6e001da` | `src/ui/{mainwindow*,filelistmodel,preview_panel,classify_panel,page_meta,icons}.*`、`assets/icons/check-all.svg`；`smoke_probes.cpp` 仅 M7-R2-B 授权两处换锚 | R-009/025/026/143/144/148/149/184/251/276/277/278/280/314（非区间，R-279 不在列）、0 字节补齐、UX-1①–⑤、UX-2、UX-4 stretch |
| | **R2D2** 输出/运行/表单/编辑器/设置 | `27c9f40` | `src/ui/{page_output,page_run,paramform,exif_editor,settings_dialog}.*`、`src/ui/theme.h`；`smoke_probes.cpp` 仅 M7-R2-A 授权 spin-group 换锚 | R-066/070/072/077/133/135–138/226/228/238/249/265/266/318、UX-1⑥–⑨（⑩–⑫ = WP6 `d99df1b` 已接）、UX-8/10.1/11/12、UX-6③、R-099/217/270、X5 |
| 波 3（RTL 与结构拆分 + 常驻探针） | **R2E2** | `52b9870` | `src/ui/{page_output,filelistmodel,settings_dialog,exif_editor,mainwindow,page_run,smoke_probes}.*`、`tests/unit/test_smoke.cpp`、新头 `src/ui/{format_policy.h,leading_indent.h}` | R-218 RTL 镜像、第 3 波常驻探针 5 条、B.3 纯核 + R-190/191/203/275/281/326 |
| 波 4（台账终稿与文档） | **R2F** | `8a541de` | `docs/**`、`CHANGELOG.md`、`README.md`（+ `src/ui/smoke_probes.cpp` 仅第 3 波遗留探针缺口，若有——本包任务清单未含探针实现，规格入 handoff「探针需求」） | 上轮校读 18 条归并 10 主题闭环、m7r2-plan.md 出处收口、覆盖台账 R2 终态、证据摘录入 `docs/m7-evidence/`、CHANGELOG/README 口径、误识别/受约束登记 |
| 终门禁收口 | **R2G**（git 主题 `M7-R2-GATE`，勿与 R2-gate `09b5340` 重播种包混） | `6a6e043` | 门禁树 `build/release-dev` + tidy fallout 修复面 | 八项判据全绿 + 修码 12/消音 11（R2G-G1…G4）+ 回归瞬态锁 **D-1** 登记（R2G-G5） |
| 发行 | **R2REL** | （另行） | 发行链（`docs/m7-evidence/release-runbook.md`） | 按 runbook 发布 0.3.3+（失败即停纪律） |

---

## 4. 纪律（R2 轮共同遵守；与 AGENTS.md §5/§6 同口径）

1. **冻结面只走「加性 + 追认」**：新声明追加到文件/结构体/枚举末尾，旧签名保留转发，既有行为
   零改动；逐处登记进各包 handoff「裁定登记」并汇入 `docs/m7-adjudications.md`。
2. **金样/夹具零改动；断言只增不减**；冻结行逐字不动（`SMOKE total=20 pass=20 fail=0`、
   `UI-SMOKE OK shots=12 pages=3`、`--version` 单行、`pp_verify` 的 `VERIFY` 行式）。
   机械换锚（语义不变、严格度不降）须登记（先例：M7-R2-A spin-group、M7-R2-B map 高度锚）。
3. **默认值 = 现状字节级一致**：能力类新选项一律 opt-in、默认关（C1–C4 四能力默认 0/false）。
4. **引擎判据不动**：`tools/baseline/golden.log`、回归归一化规则 N1–N9、金样断言面零放宽；
   回归重播种前逐行归因入册（R-015）。
5. **密钥/凭据永不入库**；构建与打包不联网；不升级 vcpkg/Qt/任何依赖版本。
6. **负面清单不扩义**（AGENTS §1.3）：D3 只做「诚实口径文案」，不建 HDR 管线；D1 只一致拒收
   CMYK，不加 CMYK 转换。
7. **源文件只读**；输出不得覆盖源文件（R-001/002 语义延续）。
8. **临时探针放 `build/<task-id>/` 不入库**；常驻 ui_smoke 探针规格写进 handoff「探针需求」，
   由后续波次统一落地。
9. **门禁自查**（格式钉版只格式化本包改动文件、相关单测、金样、ui_smoke）后收工；
   全量回归 / tidy 棘轮归波次收口统一把关。
10. **如实汇报**：失败、偏差、未覆盖、登记不修，都写进 handoff 残余项；不粉饰。

---

## 附 A. 「任务书」引述出处对照（换机可复核的引述内容摘录）

> `docs/m7-report.md` / `docs/m7-adjudications.md` 中凡「任务书 / 任务包（会话内分派书）」
> 引述，出处统一指向本表；原始分派书未入 docs/，摘录自各 `build/m7-wpN/handoff.md`（gitignored）
> 并在此逐字留存。R2 轮各包的任务书 = 本文件 §2/§3（波次/包/文件面 + 14 项设计定稿）。

| 引述点 | 分派书原文（摘录） | handoff 出处 |
|---|---|---|
| WP5-A10：CMakeLists「一行」 | 「实际登记 secret.cpp **两行**（pp_core glob 收录 + UI REMOVE_ITEM 剔除）而非任务书的『一行』」——两种单行做法均不可达（LNK2005 / 与 M2-T13「废除锚点 TU」裁定相抵） | `build/m7-wp5/handoff.md` A10 |
| WP8 裁定 8：「新文件」仅列三个 .cpp | 「跨 TU 衔接用 extern 声明（main.cpp 声明 pp::dev::run_dev/run_ui_smoke；dev_cli.cpp 声明 pp::dev::run_bench）——不新增第四个头文件（任务书『新文件』仅列三个 .cpp）」 | `build/m7-wp8/handoff.md` 裁定 8 |
| WP7 W7-A9：UX-5 取点面保留 | 「GPS 卡关 = 规则编辑面/清除钮置灰，**取点面（地图/搜索/联想/取中心/读文件）保持可用**」（『选点/联想自动勾上』任务包明确保留） | `build/m7-wp7/handoff.md` W7-A9 |
| WP11 README 清单含色彩四项 | 「WP10 色彩行为未交付（colormanager `(void)opts;`）——README 已按『不显示没发生的量』处理；若 WP10 后续落地，README/CHANGELOG 的『已知』条与 UX-14 C1–C4 台账需同步更新」；任务包 README 清单列了「色域映射/抖动/渲染意图/线性光重采样 opt-in 项」 | `build/m7-wp11/handoff.md` ③-4 / `docs/m7-report.md` §3.1 |
| WP11 沿用「33 条」 | 任务包沿用评审日 `docs/ux-review-14.md:5` 的 `ctest 33/33` 基线数（实测 35/34 见 m7-report §3.2） | `build/m7-wp11/handoff.md` ① R-115 行 |
| R2 轮各包任务书 | §2 十四项设计定稿 + §3 波次/包/文件面计划（本文件即出处） | 本文件 |
| R2 任务书「X 系」实现改案 | **X1** = paramform UI 重做（子分组二级折叠/整行折叠头/折叠区行宽 160，UX-8，R2D2 `27c9f40` 落地）；**X3** = UX-1 右键菜单扩面（卡⑥–⑨ 与任务卡行级动作；⑩–⑫ 运行页三卡 WP6 已接；R2D1/R2D2 落地）；**X5** = `theme.h` 公共 warn 钩子 `kFkWarn` + field_qss 规则（R2D2 `27c9f40`，adjudications M7R2D2-5）；**X6** = UX-10.1 档位↔色度限幅联动（R2D2 `27c9f40`） | R2D1/R2D2 handoff ①②（`build/m7r2-d1/handoff.md`、`build/m7r2-d2/handoff.md`）；编号为会话内任务书 X 系，此处留存定义 |
| 「WPn 跨界需求 k」对照 | WP5 跨界 1 = `core/settings.h default_class_file()` 空 data_dir 退化 CWD 相对名（→ R2B `5d19146` 收口）；WP5 跨界 2 = mainwindow 预设路径空目录病灶（→ R2D1 `6e001da` 守卫）；WP5 跨界 5 = `src/platform/mica.h` 主题切换监听 v1 口径维持（附录 D）；WP4 跨界需求 1 = UX-6① spec §5 R1 两档例外（→ D7）；WP3 跨界需求 2 = 无 `install()` 设计取舍（→ D9）；WP8 跨界需求 3 = 线程读数估算口径（→ D10） | 各 `build/m7-wpN/handoff.md` ③「跨界需求」节；编号随包内条序，此处留存定义 |

---

*本文件随 R2 轮交付维护；修订请在提交正文说明。R2 逐条交付与验证证据：`docs/m7-report.md`
「R2 轮交付」章节 + `docs/m7-evidence/`。*
