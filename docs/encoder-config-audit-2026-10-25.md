# PhotoPipeline 编码器配置全量审计报告

> 版本基线：**0.3.5.alpha2**（`CMakeLists.txt:19-21` 单源）  
> 审计日期：2026-10-25  
> 审计对象：8 个输出格式的全部编码参数、UI 暴露面、UI→编码器生效链、默认值与联动、静默降级/假桩、输出兼容性与合规、仅元数据（像素零改动）通路  
> 证据根：`build/encoder-audit/`（gitignored 取证目录；关键结论均在正文给出 `file:line` 与复现命令）  
> 构建树：`build/release-dev`（`PP_BUILD_DEV=ON`，含 `--dev` / `pp_verify`）  
> 工作树注记：版本串为 0.3.5.alpha2；工作树含未发布的 0.3.6-alpha1 修复（`enc_heif`/`pipeline`/`paramform` 等），`build/release-dev` 于 2026-10-25 11:20 构建，本报告以该二进制实测 + 当前源码行号为准（差异见 9.4/9.8）。

## 0. 阅读指南

- **结论先行**：第 1 章给出总体判定与最值得先修的问题；第 7 章是全部问题的总表（按严重度排序，含证据与修法）。
- **逐格式细节**：第 4 章按 JPEG / JPEG XL / PNG / TIFF / WebP / BMP / HEIF / AVIF 分节，每节含“参数全量矩阵 → 生效判定 → 发现 → 输出兼容性矩阵 → 未覆盖项”。
- **跨格式主题**：第 5 章回答“全参数暴露差距、默认值、联动缺失、静默降级/假桩、兼容性、仅元数据”六个横向问题。
- **证据口径**：`confirmed` = 代码/头文件/实测三者至少两条互证；`suspected` = 只有单一路径证据；`known` = 已在既有评审/裁定中登记（给出 `docs/...` 引用）。


## 目录

- **1. 执行摘要**（总判定、P0 清单、最优先修复、哪些链路是好的、门禁为何失明）
- **2. 审计范围、方法与判定口径**（三路证据法、严重度、覆盖矩阵、边界）
- **3. 配置通路模型**（schema → UI → spec → pipeline → encoder，五个丢失/改写点）
- **4. 逐格式审计**
  - 4.1 JPEG（jpegli / DCT）
  - 4.2 JPEG XL（libjxl / VarDCT + Modular）
  - 4.3 PNG（OIIO / Deflate）
  - 4.4 TIFF（OIIO / 压缩方案）
  - 4.5 WebP（libwebp / Lossy + Lossless）
  - 4.6 BMP（OIIO / Raw）
  - 4.7 HEIF（libheif / x265）
  - 4.8 AVIF（libheif / SVT-AV1 + libaom）
- **5. 跨格式主题**（全参数暴露、默认值、联动、假桩、兼容性、仅元数据、无损语义、预设、门禁）
- **6. 跨格式分项详细审计**（参数引擎 / 下发链 / 库面 / 兼容性 / 仅元数据 / 运行时实证）
- **7. 问题总表与修复优先级**（P0/P1/P2/P3 全量清单 + 三批修复顺序 + 机械探针建议）
- **8. 覆盖与未覆盖（诚实清单）**
- **9. 附录**（复现命令、参数索引、known vs new、口径修正、工具链版本、证据索引）

> 阅读建议：先读第 1 章与第 7.3 节；需要追证据时按 ID 跳到第 4/6 章对应小节。全文 600+ KB，建议用编辑器/浏览器打开并搜索 ID（如 `PNG-01`、`X-WIRING-02`）。

## 1. 执行摘要

### 1.1 总判定

**编码器不是"假接线"。** 逐参数追踪 + 运行时对照表明：8 个格式的绝大多数声明参数确实被编码器读取、映射到正确的库 API，并在产物字节上产生可观测差异；库面/类型/单位层面也没有发现"大面积空转"。真正的系统性问题不在"有没有调用库"，而在**四类语义错位**：

1. **alpha 语义端到端错位（P0）**：解码端 OIIO 默认把 alpha 关联（premultiply），管线却按未关联处理；不同编码器又各自假定不同的 alpha 语义。默认路径上：JPEG/BMP 的 `flatten` 二次相乘（半透明区偏暗），WebP/JXL（及同路径 HEIF/AVIF）把预乘 RGB 当未关联写入（查看器再乘一次）；PNG→PNG 则是低 alpha 量化误差 + alpha=0 处 RGB 不可逆归零（这一半单独可 P1）。TIFF 因"预乘数据 + associated 标签"恰好自洽。
2. **多页 TIFF 的元数据后写会搬入源文件额外 IFD（P0）**：≥3 页时产物第 3 页 IFD 指向文件外，页面不可解码，而运行结果仍 `ok=true`。
3. **"无损"不是一个真值（P1）**：WebP 的 `tech_id` 优先于 `lossless` 参数（`tech=lossy + lossless=true` 静默出有损，GUI 主路径自愈、CLI/预设可复现）；JXL 的 `tech=modular + 默认 distance=0` 在未勾无损时也产出无损；勾了无损仍可被 `resampling`（JXL）或 `near_lossless`（WebP）静默变成有损；HEIF/AVIF 的"无损"只是 YCbCr 码流层无损，RGB 往返仍有舍入（8 位约 49 dB，10 位 >60 dB，均非逐位）。
4. **UI 真值面与产物不一致（P1/P2 为主）**：BMP 位深下拉不驱动产物、WebP `preset` 完全无效、JXL 三个 Modular 参数用错 setter 一改就整文件失败、JPEG 色度采样在量化表之后设置导致 444 使用 420 表、AVIF 默认 `qp=0` 让 quality 失效且体积 7–32×、HEIF 默认 10 位实际是 RExt 档且「Main」档位是 100% 失败选项。

一句话：**"参数接线"整体合格；"语义/真值/默认值/联动"不合格。** 用户最容易被骗的不是"参数不存在"，而是"参数存在、UI 说生效、产物却是另一回事"。

### 1.2 关键数字（详见第 7 章总表）

- 审计覆盖：**8/8 格式**、**约 120 个 schema/运行时参数键**、**位深 8/10/12/16 + BMP 8/24 bpp**、**有损/无损/近无损**、**有 alpha/无 alpha/灰度**、**ICC/EXIF/XMP/GPS/方向**、**仅元数据 8 容器**。
- 独立实证：每个格式 ≥2 组参数对照；关键结论均用 `pp_verify`（16-bit 量化逐位）或纯 Python 原生字节解析复核；主控另行独立复核了 alpha、WebP preset/无损、JXL 无损+降采样、JPEG 色度量化表、TIFF 多页、HEIF quality、AVIF qp=0 等 25 组；另由独立复核代理对 7 条关键结论逐条复跑（见 9.8）。
- 发现问题（严重度归一后）：**P0 2 条**、**P1 17 条**、**P2 37 条**、**P3 28 条**（最终计数以第 7 章为准；`TIFF-03` 已撤回，`JXL-01`/`WEBP-01` 由 P0 降 P1）。
- 测试门禁失明点：`pp_verify` 的 exact 断言走与编码器相同的 OIIO 关联读取，**对 alpha 语义错误天然免疫**；金样 alpha 夹具只有 0/255（关联恒等点）；多页 TIFF 只有首页断言；WebP preset/JXL setter 类"参数无效"没有字节级探针。

### 1.3 两条 P0（必须先修）

| # | 问题 | 影响面 | 触发入口 | 证据 |
|---|---|---|---|---|
| P0-1 | 解码 alpha 关联 + 管线按未关联处理 | 默认路径：JPEG/BMP 半透明区二次相乘偏暗；WebP/JXL/HEIF/AVIF 预乘数据被当未关联；PNG 低 alpha 量化 + alpha=0 RGB 归零 | GUI 与 CLI 全路径（任何带半透明像素的源） | `PNG-01`、5.7、9.7 §H |
| P0-2 | 多页 TIFF 元数据后写搬入源额外 IFD | 产物多页；≥3 页时该页不可解码；`ok=true` | 任何多页 TIFF 源 → TIFF 输出 | `TIFF-01` |

> **降级说明（独立复核裁定，见 9.8）**：`JXL-01`（无损 + `resampling>1`）与 `WEBP-01`（`tech=lossy` + `lossless=true`）原标 P0，现降为 **P1**——前者需要用户显式把非默认 `resampling` 调离 1（默认 -1/1 不触发），后者 GUI 主路径会自愈（`paramform` 自动切技术），仅 CLI/手写预设可复现；两者仍是"无损承诺不可信"的高优问题。`TIFF-03` 经原生 strip 字节复核**撤回**（数据 premultiplied + `ExtraSamples=1` 自洽），不计入总表。

### 1.4 最优先修复清单（按"收益/风险"排序）

1. **alpha 端到端收口**：`oiio_reader` 请求 `oiio:UnassociatedAlpha=1`；OIIO 写端设置同名属性；复核 JXL `alpha_premultiplied`、WebP `ImportRGBA`、HEIF/AVIF 平面填充；`flatten` 公式与输入语义对齐。加"半透明 alpha 夹具 + 原生字节比较"探针（`pp_verify` 需支持 unassociated 读取）。
2. **AVIF 默认 `qp=0` 修复**：内省对无默认值的参数兜底成最小值 0，GUI 又把内省默认全量下发 → SVT 默认 CQP 0（体积 7–32×、quality 失效）。修 `param_def_from` 的三态默认 + GUI 只下发"有默认/用户改过"的键（`AVIF-01`）。
3. **TIFF 后写只许带 IFD0**：元数据载荷构造时剔除 `Exif.Thumbnail.*`/子图目录；metawrite 后断言 `subimage_count==1`（多页源）。
4. **JXL 无损锁死降采样/颜色变换**：`resampling` 无损时隐藏+锁 1，`cross_rules` 加规则，编码器兜底。
5. **WebP 无损单一真源**：技术下拉与复选框联动；`lossless=true` 时禁止/拒绝 lossy 技术；`near_lossless<100` 发 warning 或锁 100。
6. **JPEG 色度因子移到 `jpegli_set_defaults` 之后、`set_quality/set_distance` 之前**（照抄参考实现）。
7. **BMP 位深**：把 8/24 改为"按通道数只读展示"，或让编码器按所选值真正驱动 bpp；`DepthDowngrade` 用 `bitdepth_bps` 比较。
8. **WebP preset**：删行，或改成"默认值来源"并与手调联动。
9. **JXL 三个 Modular 参数改 float setter**（`channel_colors_*`/`modular_ma_tree_learning_percent`），`hi` 收到 100。
10. **TIFF 删 `ccittrle`/`predictor=float` 或加运行前红字拦截**。
11. **HEIF 默认位深 8、删「Main」死档位、`quality` 无损时锁定/说明**；AVIF 无损只在 libaom 可用，勾无损时自动切后端或禁用 SVT。
12. **给 warning 一个 UI 出口**：`WarningKind` 在 `src/ui` 零消费（`X-COMPLIANCE-08`），降档/丢元数据/拍平/丢 ICC 告警只进日志与侧车；否则所有"发 warning"的修法对用户仍不可见。

### 1.5 哪些链路是好的（避免误伤）

- **参数引擎的数据驱动骨架**：`default_params` 填齐当前技术全部键；`apply_locks` 的锁定/两键同步在静态格式上工作；`validate_params` 的范围/枚举/类型校验有效；未知键在运行前被 `cross_validate` 硬拒（比 E9 的"忽略"更安全）。
- **JPEG 的三个历史假控件**（arith_code/dct_method/xyb_mode）已用 locked 谓词禁用并给原因，实测不可改写；**TIFF predictor** 键名消费已修（none/horizontal 真实产生字节差）；**PNG compressionLevel** 键名已修（0/6/9 真实改变 FLEVEL 与体积）。
- **无 alpha 时真正逐位无损的路径**：PNG、TIFF、JXL（不含 resampling）、WebP 真无损路径在测试夹具上 `pp_verify exact` 通过；带 alpha 时四种格式各自受 5.7 的 alpha 语义影响（TIFF 自洽、PNG 低 alpha 有量化损失、WebP/JXL 预乘写入）。HEIF/AVIF 的"无损"是 YCbCr 码流层无损（8/10/12 位均非 RGB 逐位），12-bit 与 16 MP grid 边界处理明确、可解码；HEIF 无损强制 444 的联动正确。
- **仅元数据通路（像素层）**：JPEG（SOS 熵数据）、PNG（IDAT）、WebP（VP8/VP8L/ALPH 载荷）字节级不变，TIFF strip/tile 载荷逐字节一致（21 组实测）；失败走临时文件+rename，源文件只读，`out==src` 被拒。JXL/HEIF/AVIF/BMP 明确拒绝而非偷偷重编码。但仍有四个遗留：输出根落在输入树内会二次施加规则（P1）、隐私剥除只清 EXIF+XMP（P2）、元数据层被 Exiv2 规范化（非文件字节保真，P2）、仍强制 OIIO probe（P2）——详见 6.5。
- **颜色/元数据容器**：JPEG APP2 ICC、PNG iCCP/eXIf、TIFF tag 34675/Exif、WebP ICCP/EXIF/XMP、HEIF/AVIF nclx+ICC+Exif/XMP、JXL Exif/xml box 均结构合法；HEIF 的 nclx 从 ICC 反推且 VUI CICP 跟随，属于做得好的部分。

### 1.6 为什么现有门禁没发现

- `pp_verify` 的像素比较与编码器共享 OIIO 关联 alpha 的读端 → alpha 语义错误在两侧对称抵消，exact 仍绿；金样 alpha 夹具只有 0/255，正好是关联恒等点。
- 多页 TIFF 只断言首页；第 2..N 页没人枚举。
- WebP `preset`、JXL `color_transform`、BMP 位深等"无效参数"没有"改值必须改变字节"的机械探针；金样按固定参数出图，不比较"参数 A vs B"。
- `--dev` 对 HEIF/AVIF 的字符串化 `--param` 掩盖了数值参数的生效性（quality 固定成 90），使 CLI 取证链本身不可信。
- UI 侧没有"控件可见性/锁定"的运行时断言（除既有 `--ui-smoke` 探针），本报告的 UI 结论以代码路径为主。

## 2. 审计范围、方法与判定口径

### 2.1 审计对象

| 格式 | 后端 / 技术 | 参数面来源 |
|---|---|---|
| JPEG | jpegli / DCT | `src/core/format_tables.cpp` 静态表（15 项） |
| JPEG XL | libjxl / VarDCT、Modular | 静态表（VarDCT 16 项 + Modular 21 项） |
| PNG | OIIO / Deflate | 静态表（1 项） |
| TIFF | OIIO / Compression | 静态表（5 项） |
| WebP | libwebp / Lossy、Lossless | 静态表（Lossy 24 项 + Lossless 5 项） |
| BMP | OIIO / Raw | 静态表（0 项） |
| HEIF | libheif / x265 | 运行时内省 `heif_encoder_list_parameters` |
| AVIF | libheif / SVT-AV1、libaom | 运行时内省（两后端分别） |

格式级控制另行覆盖：位深、编码档位（HEIF/AVIF）、无损开关、后端/技术选择、色彩目标、alpha 拍平、灰度、8bit 抖动、线程预算、仅元数据模式。

### 2.2 三路证据法

1. **静态链路追踪**：`format_tables.cpp`（声明）→ `paramform.cpp`（UI 建行/谓词/锁定/取值）→ `page_output.cpp::spec_of`（下发）→ `build_output_specs`/`config_base`（默认/锁定/校验）→ `pipeline.cpp::plan_targets`（目标装配）→ `src/codecs/enc_*.cpp`（真实读取点）。每个参数都要求落到“读取点 + 实际 API 调用”两级证据。
2. **运行时实证**：`build/release-dev/photopipeline.exe --dev` 做参数对比编码（字节数/SHA-256/容器结构/头部字段），`pp_verify` 做 16-bit 量化逐位比较（`exact`）或 PSNR；容器解析用纯 Python（无 PIL/numpy）直接读 JPEG marker、PNG chunk、TIFF IFD、RIFF、ISO-BMFF box、JXL box。
3. **库面比对**：以 `vcpkg_installed/x64-windows-avx2/include/**` 与 `vcpkg/buildtrees/<port>/**` 的真实头文件/源码为准，逐项比对“库可配置面 vs schema/UI”，判断遗漏、假桩、单位/类型映射错误。

### 2.3 严重度口径

| 级别 | 判定 |
|---|---|
| **P0** | 破坏用户数据；把有损产物当无损交付且无法察觉；产物不可解码；仅元数据模式改动像素 |
| **P1** | 常见配置下参数不生效 / UI 与产物矛盾；默认值造成明显错误或明显非预期；输出兼容性破坏；色彩语义静默丢失 |
| **P2** | 边界或罕见配置；提示缺失、文案承诺超出实现；性能/体积次优；取证面失真 |
| **P3** | 文案、分组、注释、打磨级 |

> “假桩/不生效”单列一类：参数在 UI 可见、可改、可存预设，但编码器不读、读错类型、被后续覆盖，或只影响日志。判据不是“代码里有没有赋值”，而是“**改变该值是否改变产物**”（实测字节/像素/结构）。

### 2.4 覆盖矩阵

| 维度 | 覆盖 |
|---|---|
| 格式 | 8/8（JPEG、JXL、PNG、TIFF、WebP、BMP、HEIF、AVIF） |
| 静态 schema 参数 | 全部逐项建矩阵（含锁定/隐藏/高级项） |
| HEIF/AVIF 运行时参数 | 按插件真实参数表全量（x265 7 项、SVT 11 项、libaom 15 项） |
| 位深 | 8 / 10 / 12 / 16 / BMP 8·24 bpp（按格式能力） |
| alpha | 有 alpha / 无 alpha / alpha 拍平 / 10-bit+alpha 边界 |
| 灰度 | 原生灰度保留 / 灰度→RGB 路径 |
| 有损/无损 | 有损、无损、近无损、假无损边界 |
| 色彩 | keep / sRGB / P3 / AdobeRGB；ICC 嵌入与无 ICC 容器 |
| 元数据 | EXIF / XMP / GPS / 时间 / ICC / 方向；转码写入与仅元数据重写 |
| 仅元数据 | JPEG/PNG/TIFF/WebP 字节级验证；JXL/HEIF/AVIF/BMP 能力边界 |

### 2.5 审计边界（如实）

- **未运行 GUI 交互**：UI 暴露/可见性/锁定结论来自 `paramform.cpp`/`page_output.cpp` 源码与既有 `--ui-smoke` 探针，不是人工点选截图。凡“复选框可见”类结论均按代码级证据记。
- **未接第三方解码器矩阵**：兼容性用 OIIO 回读、`pp_verify` 与 Windows GDI+（BMP）等有限几路；未逐一验证 Photoshop/GIMP/ffmpeg/ImageMagick/浏览器。
- **未做超大图/超大文件压力**：>4 GB BMP、>16 MP HEIF 无损 grid、超大 ICC/EXIF 载荷只做代码路径核对，未全部实测。
- **`--dev` 与 GUI 默认面不同**（conflict、输出模板、部分默认值）：所有实测结论都标注了入口；涉及 GUI 默认值的结论以 `config_base`/`spec_of` 代码为准。
- **`--dev --param` 对 HEIF/AVIF 整数运行时参数存在类型缺陷**（见 5.x）：实测这些参数时必须用 v2 预设传 int，不能直接用 `--param key=int`；本报告已据此区分“参数本身不生效”与“CLI 传参类型不生效”。

## 3. 配置通路模型（先看这一张图）

```
format_tables.cpp / libheif introspection        ← 参数事实（key/type/default/range/visible/locked）
        │
paramform.cpp                                    ← 建行、谓词显隐、锁定禁用、无损复选框、values()
        │  values() = 非 "__" 键；隐藏行也写入；类型按 ParamDef
page_output.cpp::spec_of / snapshot_state         ← 叠加 st->extra（表单不认的键）、__lossless、__heif_profile
        │
config_base()  ────────────────┐
        │                      │（GUI 不经过 build_output_specs）
        │                      └─ mainwindow_run.cpp 只补 rules/workers/conflict 等
        ▼
plan_targets()                                   ← 只做格式/位深归属校验，不重跑默认/锁定/范围校验
        ▼
EncodeRequest.target.params                      ← 原样进入编码器
        ▼
enc_*.cpp                                        ← 逐键 param_int/param_str/... 读取；未知键只 log_warn
        ▼
库 API（jpegli/libjxl/OIIO/libwebp/libheif）      ← 真正决定产物
```

**这条链上八个已知的“丢失/改写点”**（逐条在第 5 章展开）：

1. **UI→spec**：`st->extra` 会保留表单不认的跨技术键；它们能过 run 前 `cross_validate`（同格式全技术键并集），但编码器可能不读（静默无效）。
2. **spec→编码器**：GUI 的 `config_base()` 不调用 `build_output_specs`，因此不重跑 `fill_defaults`/`apply_locks`/`validate_params`；锁定与默认值依赖表单 `refresh()` 已经算过。
3. **未知键**：`enc_common::warn_unknown_params` 只 `log_warn`，不进 `EncodeResult.warnings`，UI 运行行看不到。
4. **锁定/强制**：编码器内部还会二次强制（HEIF lossless→chroma 444、WebP tech 优先于 lossless 参数、JPEG PSNR 覆盖 distance），这些覆盖多数只写日志。
5. **元数据后写**：编码完成后 Exiv2 重写会改变最终文件大小；`EncodeResult::bytes`/运行行统计的是**编码产物大小**，不是最终落盘大小。
6. **alpha 缓冲**：解码端 OIIO 返回的是**关联（预乘）alpha** 的 RGB；管线没有在边界上归一，而各编码器/`flatten` 又按"未关联"假定处理。这是 P0 的根因，不在"参数"层，而在"像素语义"层。
7. **内省默认**：HEIF/AVIF 的 `ParamDef::def` 对"插件没有默认值"的可选参数取范围下限（如 SVT `qp=0`）；`paramform::make_defaults` 把它当用户值写入 `values`，`spec_of` 全量下发 → 默认配置被静默改写成"显式设 0"。
8. **warning 出口**：pipeline/编码器产生的 `WarningKind` 只进日志与 `.pp.json`；`src/ui` 全目录没有消费者，GUI 运行页/文件列表不显示。

## 4. 逐格式审计

> 每节由独立审计代理产出，含参数全量矩阵、逐参数生效判定、发现、输出兼容性矩阵与未覆盖项；证据行号相对仓库根。
>
> **严重度注记**：各节初始严重度按代理口径；主编在独立复核后统一归一（见 7.0）：`JXL-01`/`WEBP-01` 由 P0 降 P1，`TIFF-03` 撤回，P0 只保留 alpha 与多页 TIFF 两条。节内标题保留原始标定以便追溯。

### 4.1 JPEG（jpegli / DCT）

> 审计对象：`FormatDef{jpeg}` / `BackendDef{jpegli}` / `TechDef{dct}`，15 个声明参数 + 1 个内部管道键。
> 只读审计；所有临时脚本与产物在 `build/encoder-audit/`。
> 二进制：`build/release-dev/photopipeline.exe`（0.3.5.alpha2，PP_BUILD_DEV=ON）+ `pp_verify.exe`，全部经 `python tools/env.py run --` 启动。
> 库证据版本：jpegli = `google/jpegli@031a0077`（`vcpkg-overlay/libjpeg-turbo/portfile.cmake:19-25` 钉死），源码树 `vcpkg/buildtrees/libjpeg-turbo/src/56c1f52a20-cc63ed77f0.clean/**`；Exiv2 0.28.8。

#### 1. 结论

1. **有一个 P1 级真缺陷：色度采样在量化表算完之后才设置**，所以 `chroma=444`（默认值）/`422`/`440` 三种配置拿到的都是「按 4:2:0 算出来的量化表」。实测 `444/440/422/420` 四个产物的 DQT 段**逐字节相同**，且每张表的 64 个系数**全部**命中 `round(kGlobalScaleYCbCr × 1.22 × base[k])`（420 公式），命中率 64/64；命中 444 公式的只有 1/64、0/64、0/64。参考实现的调用顺序（`lib/extras/enc/jpegli.cc:416-453`）恰好相反。
2. **`lossless` 标志对 JPEG 完全无拦截、无提示**：`--lossless --format jpeg` 退出码 0、`ok=true`、产物 18467 B、与有损基线 SHA 完全相同、`warnings` 为空；`WarningKind::LossyFromLossless` 在 `src/` 里**没有任何生产者**（只在 `dev_cli.cpp:152` 做名字映射）。交互式 UI 不可达（无损复选框对该格式隐藏），但预设/CLI 可达，且 GUI 载入这类预设时是**静默把标志改回 false**（`paramform.cpp:1330-1336`）。
3. **`psnr_target>0` 静默覆盖 quality_mode/distance/quality（R-247 未修完：只有 tooltip）**。实测 `psnr_target=40` 时 `distance=0.5` 与 `distance=3.0` 两个产物**字节数与 SHA 完全相同**（13142 B / `C00154B2…`）。附带事实：`psnr_target=40` 实测只有 **35.35 dB**，`psnr_target=30` 实测 **26.15 dB**（库的距离搜索在系数域估计 PSNR，见 `encode_finish.cc:93-118`），即该参数既覆盖别人、自己也达不到目标。
4. **`cicp_transfer_function` 是个「伪装成色彩标记的量化表旋钮」**：唯一读取点是 `enc_jpegli.cpp:265`，库侧唯一作用是 `global_scale *= 0.4`（PQ=16）/`*= 0.5`（HLG=18）（`quant.cc:577-581,655-659`），**不写任何 CICP/色彩标记**。铁证：`cicp=18` 与 `distance=0.5` 的 DQT 三张表哈希完全相同（`3ac86e56/b1d247e4/d13a5d09`），体积也几乎相同（24631 vs 24628 B）；`cicp=16` 体积暴涨到 27168 B（+47%）。而参考实现是从 ICC/app_data **自动推导**这个码（`lib/extras/enc/jpegli.cc:407-415`），本产品却暴露成手填整数，填了还会和恒嵌的 sRGB ICC 互相矛盾。
5. **三个「假控件」现在都是「禁用而非隐藏」，这点是对的**：`arith_code` / `dct_method` / `xyb_mode` 用恒返回强制值的 locked 谓词钉住（`format_tables.cpp:69-70,343,364,378`），实测 `--param arith_code=true`、`dct_method=1/2`、`xyb_mode=true` 全部被静默改回默认且产物与基线同 SHA，引擎侧还有 `arith_code` 硬拒兜底（`enc_jpegli.cpp:197-199`）。`xyb_mode` 的锁定尤其有据：即使放开，`enc_jpegli` 也没有 XYB 像素变换与 `jpegli_set_input_format(FLOAT)`，产物会是色彩错误的 JPEG。
6. **默认值与 jpegli 参考实现逐项一致，无「视觉透明档」争议**：`distance=1.0 / quality=90 / chroma=444 / progressive=level 2 / optimize=true / adaptive_quant=true / std_quant=false / xyb=false` 与 `lib/extras/enc/jpegli.h:25-47`、`tools/cjpegli.cc:39-53,118` 全部对得上。问题不在默认值选得对不对，而在 **默认的 444 走的是错的量化表**（结论 1）。
7. **输出合规面没有硬伤**：8bit-only 有硬校验（`enc_jpegli.cpp:190-194` + `pipeline.cpp:433-440`，`--bitdepth 16` 直接 exit 2）；渐进扫描脚本 13/15 个 scan 全部满足 T.81 §B.2.3 的 DC-first/Ss≤Se/Ah-Al 递进规则；重启间隔 `restart_in_rows=2` 实测 DRI=64（=32 MCU/行 × 2）；ICC 走 APP2（589 B/1 chunk）、EXIF 走 Exiv2 后写 APP1，二者共存且 Exiv2 重写后仍保持 SOF2/13 scans；仅元数据模式经 `pp_verify` 逐位断言 **VERIFY OK**（不动像素）。EXIF >64KB 由 Exiv2 抛 `kerTooLargeJpegSegment`（`exiv2/src/jpgimage.cpp:766-767`）→ 该输出行记失败，不会静默截断。
8. **记账口径有一个小偏差**：`row.out_bytes` 取的是编码器字节数（`pipeline.cpp:1120`），不含其后写的 EXIF/XMP；实测同一产物侧车/SUMMARY 报 957 B，磁盘实际 1169 B（−18%）。

#### 2. 参数全量矩阵

UI 列依据：`paramform.cpp:796-800`（核心区按表序建行）+ `:742-794`（高级区按 `ParamDef.group` 分子组建行）+ `:287-321`（Enum→combo）/`:215-243`（Int→spin）/`:244-269`（Float→dspin）/`:270-286`（Bool→check）。`add_row` 对 15 个参数**无任何按 key 的跳过分支**（唯一例外是 `kLosslessParamKey`，JPEG 不声明），因此 15 个参数全部有行。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality_mode | 核心 combobox（`format_tables.cpp:286-294`） | `distance` | `distance` / `quality` | `enc_jpegli.cpp:272` | 是 | 谓词 `quality_mode_is_distance/_is_quality`（`format_tables.cpp:31-36`）只控制下方两行的可见性；**psnr_target>0 时本参数失效**（JPEG-03） |
| distance | 核心 float spin（`:295-303`） | `1.0` | `[0.3, 3.0]` step 0.1（库允许 0.0–25.0） | `enc_jpegli.cpp:275` | 条件 | `visible=quality_mode_is_distance`；`quality_mode=quality` 或 `psnr_target>0` 时不生效；范围收窄已自述为 UI 范围（R-294） |
| quality | 核心 int spin（`:304-310`） | `90` | `[1, 100]` | `enc_jpegli.cpp:273` | 条件 | `visible=quality_mode_is_quality`；走 `jpegli_set_quality` → 只写 2 张量化表（JPEG-05） |
| chroma | 核心 enum（`:311-322`） | `444` | `444/440/422/420` | `enc_jpegli.cpp:292-305` | 是（SOF 采样因子实测 1x1 / 1x2 / 2x1 / 2x2） | 无谓词；**但量化表与它无关**（JPEG-01）；灰度源下整段被 `if (!gray)` 跳过 → 静默无效（无提示） |
| progressive | 核心 bool（`:323-328`） | `true` | — | `enc_jpegli.cpp:281` | 是 | 只能表达 level 2 / level 0（库支持 0/1/2）；`locked=lock_true_when_progressive` 会把 optimize 钉成 true；交叉规则见 `params.cpp:276-286` |
| optimize_coding | 高级 bool（`:329-335`，组=编码工具） | `true` | — | `enc_jpegli.cpp:284` | 条件 | `locked=lock_true_when_progressive`（progressive=true 时**强制 true，用户设 false 被静默改写**，实测产物与基线同 SHA）；只有 `progressive=false` 才真正可关（实测 24344 B / SOF0） |
| arith_code | 高级 bool（`:336-343`） | `false` | — | `enc_jpegli.cpp:197`（守卫）/`jpegli` 不读 | 否 | `locked=lock_false_always` + 引擎硬拒（`encode.cc:263-265`，`jconfig.h:5` 未定义 `C_ARITH_CODING_SUPPORTED`）。实测 `--param arith_code=true` 被改回 false |
| restart_in_rows | 高级 int（`:344-351`） | `0` | `[0, 64]`（库上限 65535 行） | `enc_jpegli.cpp:285` | 是 | 单位=MCU 行；实测 `=2` → DRI=64（32 MCU/行）；`>65535` MCU 时被库静默截断（`encode.cc:376-381`）；绝对 `restart_interval`（MCU 数）未暴露 |
| dct_method | 高级 enum（`:352-364`） | `0`(ISLOW) | `ISLOW/IFAST/FLOAT` | `enc_jpegli.cpp:286`（写了但库不读） | 否 | `locked=lock_zero_always`；`encode.cc:68` 固定 `JDCT_FLOAT`，全库无读取点；实测三值同 SHA |
| smoothing_factor | 高级 int（`:365-370`） | `0` | `[0, 100]` | `enc_jpegli.cpp:287` | 是 | `encode.cc:272-273` 校验；实测 `=50` → 16331 B / PSNR 36.25 dB（明显低通） |
| xyb_mode | 高级 bool（`:371-378`） | `false` | — | `enc_jpegli.cpp:262-264` | 否 | `locked=lock_false_always`。**锁定有双重理由**：无 XYB ICC 路径，且 `enc_jpegli` 无 XYB 像素变换/`jpegli_set_input_format(FLOAT)`；实测 `--param xyb_mode=true` 被改回 false |
| adaptive_quantization | 高级 bool（`:379-384`） | `true` | — | `enc_jpegli.cpp:288` | 是 | 库默认 true（`encode.cc:695`）；实测 `=false` → 19171 B / PSNR 43.52 dB（更大更「准」，符合 AQ 的感知换 PSNR 语义） |
| std_quant_tables | 高级 bool（`:385-391`） | `false` | — | `enc_jpegli.cpp:266-268` | 是（**必须在 set_defaults 之前，已满足**） | 实测 `=true` → 17443 B / 39.88 dB，量化表降为 2 张（`quant.cc:675-681` 的 std 分支） |
| psnr_target | 高级 float（`:392-401`） | `0`（关） | `[0, 100]` | `enc_jpegli.cpp:277-280` | 条件 | **无 visible/无 locked，只有 tooltip 提示「静默覆盖」**（R-247）。>0 时覆盖上三项（实测同 SHA）；库 min/max distance 固定 0.1/25.0，`psnr>0` 时 `IsStreamingSupported=false`（`encode.cc:401-403`） |
| cicp_transfer_function | 高级 int（`:402-411`） | `2` | `[0, 18]` | `enc_jpegli.cpp:265`（**唯一读取点，且在 set_defaults 前，顺序正确**） | 条件 | 仅 16/18 改量化表缩放，其余码值无任何效果，且**不写色彩标记**（JPEG-04） |
| `__lossless`（内部管道键，非本格式声明） | 否（`update_selectors` 在无 lossless_capable 技术时隐藏复选框，`paramform.cpp:599-602`） | `false` | — | **无任何读取点**（`enc_jpegli.cpp:51-57` 键表与 `:85-103` 读取函数里都没有它） | 否 | `apply_locks` 永远会写它（`params.cpp:495-499`），但 JPEG 侧无人消费 → JPEG-02 |

#### 3. 发现

##### JPEG-01 [P1] 色度采样在量化表之后设置：`chroma=444/422/440` 实际使用 4:2:0 的量化表

- 证据：
  - `src/codecs/enc_jpegli.cpp:269-276`：先 `jpegli_set_defaults()`，紧接着 `jpegli_set_quality(...)` / `jpegli_set_distance(...)`；
  - `src/codecs/enc_jpegli.cpp:290-305`：色度采样因子在这之后才写 `cinfo.comp_info[0].h_samp_factor/v_samp_factor`；
  - `vcpkg/buildtrees/libjpeg-turbo/src/56c1f52a20-cc63ed77f0.clean/lib/jpegli/quant.cc:634-705`（`SetQuantMatrices`）在算表**当时**读 `IsYUV420(cinfo)`（`quant.cc:622-630`）：`global_scale *= 1.22`（`quant.cc:660-662`）、色度表再乘 `k420Rescale[k]≈0.28–0.41`（`quant.cc:694-696`）；
  - 参考实现的顺序相反：`lib/extras/enc/jpegli.cc:416-453` = `set_defaults` → 采样因子 → `set_psnr/set_distance`。
- 事实（可复核）：
  - 同一张 256×192 图，`chroma=444 / 440 / 422 / 420` 四个产物在 `distance=1.0` 下 DQT 三张表的 SHA 前缀**完全相同**：`77fb2860 / 3c7adf54 / 43ca24de`（`build/encoder-audit/jparse.py` 输出）。若顺序正确，444 与 420 的表必须不同。
  - 用 `quant.cc` 里的 `kBaseQuantMatrixYCbCr`（`:226`）、`kGlobalScaleYCbCr=1.73966010`（`:26`）、`k420GlobalScale=1.22`（`:424`）、`k420Rescale`（`:425`）复算（脚本 `build/encoder-audit/qcheck.py`，zigzag→natural 用 `common_internal.h:56-66` 的 `kJPEGNaturalOrder`；DQT 出码顺序见 `bitstream.cc` 的 `EncodeDQT` 里 `quantval[kJPEGNaturalOrder[j]]`）：444 产物的三张表对「420 公式」命中 **64/64、64/64、64/64**；对「原生 444 公式」命中 1/64、0/64、0/64。
  - 具体数值（luma 表前 6 个 zigzag 系数）：实际 `[3,4,4,6,4,6]`，420 公式 `[3,4,4,6,4,6]`（全等），原生 444 `[2,3,3,5,4,5]`。色度表：实际 `[2,4,4,7,6,7]` vs 原生 444 `[5,11,11,16,15,16]`。
- 影响：默认配置（444 + distance=1.0）的每个 JPEG 产物，亮度量化比「Butteraugli 距离 1.0」本应给出的**粗约 1.22 倍**（画质低于界面承诺的「1.0=视觉透明」），而色度表又比 444 本应使用的**细约 2.4 倍**（把码率浪费在色度上）。体积/画质因此既非 444 也未对齐 420 的语义：实测默认 18467 B / PSNR 42.55 dB。`chroma` 这项核心参数在「量化精度」意义上**对产物没有任何作用**，只改变采样因子。
- 建议：把 `enc_jpegli.cpp:290-305` 的色度因子设置**整体上移到 `jpegli_set_defaults()` 之后、`jpegli_set_quality/set_distance` 之前**（照抄 `lib/extras/enc/jpegli.cc:417-441`，含把 comp_info[1..2] 显式置 1×1）；并加一条机械探针（单测或金样）：同一输入 `chroma=444` 与 `chroma=420` 的 DQT 必须不同。
- 状态：confirmed；known=no。

##### JPEG-02 [P2] `lossless` 标志对 JPEG 无拦截、无警告：CLI/预设可以把有损 JPEG 标成无损

- 证据：
  - `src/core/format_tables.cpp:284`：`TechDef{.id="dct", .lossless_capable = false}`；
  - `src/core/params.cpp:219-289`（`cross_rules()`）里只有 tiff/webp/jpeg-progressive 三条规则，**没有** "lossless 但技术不支持无损" 的规则；
  - `src/codecs/enc_jpegli.cpp:51-57`（15 个已知键）与 `:85-103`（`read_params`）里没有 `lossless` / `__lossless`，编码器全程不读；
  - `src/ui/preset_io.cpp:466-468`：非声明格式只要 `lossless_flag(params)` 为真就写输出级 `"lossless": true`；`src/core/presets.cpp:44-89`（`validate_preset`）不会拒；
  - `src/ui/paramform.cpp:1328-1336`（`set_selection`）：lossless=true 但技术不可无损、且后端没有 lossless_capable 技术时，**把 lossless 直接改成 false**（无提示）；
  - `src/core/types.h:28` 的 `WarningKind::LossyFromLossless` 在 `src/` 全仓**只有 `dev_cli.cpp:152-153` 的名字映射**，无任何生产者。
- 事实（可复核）：`--dev grad_rgb8.png --format jpeg --lossless` → 退出码 0、`ok=true`、18467 B、`warnings` 空、SHA 与有损基线完全相同（`83E42B59…`）。注：`tests/golden/smoke.py:284-286` 三条冻结金样（multiformat-split/mirror/conflict）本身就带 `--lossless` 且含 jpeg 输出，所以「静默忽略」是既有冻结行为，不是新引入。
- 影响：手写/迁移出来的预设、或 `--dev --lossless`，会让上层（日志、预设、用户认知）认为产物无损，而产物是有损 JPEG（本例 18467 B 的有损图），**全程无一条 warning**。交互式 UI 无法勾出这个状态（复选框隐藏），但预设载入路径把标志静默清零同样是「不告知」。
- 建议：在 `pipeline.cpp:1739` 的 `build_output_specs` 里（或参数引擎加一条 cross 规则）判 `lossless_flag(params) && !tech->lossless_capable` → 输出一条**输出级 warning**（`WarningKind::LossyFromLossless` 已存在，直接用即可，无需动冻结枚举）并 `log_warn`；不要用硬错误（会破 3 条冻结金样，需先解冻）。同时在 `docs/param-catalog.md` §1 与 JPEG 格式卡 tooltip 写明「本格式无无损模式，该标志被忽略」。
- 状态：confirmed；known=no（`docs/deep-review-0.3.2.md` 的 JPEG 条目是 R-244/245/246/247，不含本条）。

##### JPEG-03 [P2] `psnr_target>0` 静默覆盖 quality_mode/distance/quality，且实测达不到目标（R-247 未修完 + 目标值不可信）

- 证据：
  - `src/codecs/enc_jpegli.cpp:271-280`：先按 quality_mode 调 `set_quality`/`set_distance`，再无条件 `if (p.psnr_target > 0.0) jpegli_set_psnr(...)`——两者互不排斥；
  - `lib/jpegli/encode_finish.cc:171-235`：`FindDistanceForPSNR` 用距离搜索决定最终距离，`QuantizetoPSNR` 会用搜索结果 `UpdateDistance` 覆盖量化表——即库语义就是「psnr 覆盖 distance」；
  - `src/core/format_tables.cpp:392-401`：该参数**没有** `visible` 也没有 `locked` 谓词，只在 tooltip 里自述「>0 后静默覆盖质量控制方式 / 视觉距离 / 质量三项」（R-247 的处置仅止于文案）。
- 事实（可复核）：`psnr_target=40` 下 `distance=0.5` 与 `distance=3.0` 的产物**字节数 13142 与 SHA `C00154B2…` 完全相同**。另测：`psnr_target=40` 实测 PSNR 35.35 dB（差 4.65），`psnr_target=30` 实测 26.15 dB（差 3.85）；库侧 `ComputePSNR`（`encode_finish.cc:93-118`）在 DCT 系数域按分量累加估计，与 `pp_verify` 的 RGB 采样域 PSNR 不是同一度量。
- 影响：用户在高级区设了 `psnr_target` 后，上方三个核心控件（质量控制方式/视觉距离/质量）**仍然可编辑、仍然显示、但完全不生效**；且即使只信 psnr 参数本身，实际画质也系统性地低于所填数字（本例约 4 dB）。「质量」语义在两条通道上都不闭合。
- 建议：加联动而不只是文案——`psnr_target > 0` 时把 `distance`/`quality` 两行 `visible=false`（或 `locked` 显示被覆盖后的实际距离），并把 `quality_mode` 一并锁定；若愿意改语义，把 PSNR 做成 `quality_mode` 的第三个枚举值（注意这是预设 JSON 的键值变更，需走裁定）。tooltip 里补一句「库在系数域估计 PSNR，实测可能低于目标」。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:516` R-247「`psnr_target` 静默覆盖 quality_mode/distance/quality，无谓词无提示」）。

##### JPEG-04 [P2] `cicp_transfer_function` 只改量化表缩放，不写任何色彩标记，且与恒嵌的 ICC 自相矛盾

- 证据：
  - `src/codecs/enc_jpegli.cpp:265`：`jpegli_set_cicp_transfer_function(&cinfo, p.cicp_transfer_function)`——本项目唯一读取点；
  - 库侧唯一消费点：`lib/jpegli/quant.cc:577-581`（`QuantValsToDistance`）与 `:655-659`（`SetQuantMatrices`）——`if (tf == PQ(16)) global_scale *= .4f; else if (tf == HLG(18)) global_scale *= .5f;`；全库 grep `cicp_transfer_function` 只命中 `encode.cc`（setter 与 `InitializeCompressParams` 默认 2）与 `quant.cc`；
  - 出码面：`bitstream.cc:68-71` 只写 JFIF/Adobe 两个可选标记，而 `encode.cc:71` 把 `write_JFIF_header` 置 FALSE → 产物**既无 APP0 也无 CICP**（本审计所有产物的 APP 段除 ICC APP2 / Exif APP1 外为空）；
  - 参考实现是**自动推导**：`lib/extras/enc/jpegli.cc:407-415` 从 app_data / ICC 的 `cicp` tag 取码再传入。
- 事实（可复核）：`--param cicp_transfer_function=18` 的三张 DQT 哈希 `3ac86e56/b1d247e4/d13a5d09` 与 `--param distance=0.5`（默认 cicp=2）**完全相同**，体积 24631 vs 24628 B——因为 `DistanceToScale(0.5,k)=0.5`（`quant.cc:556-558`，d<1.5 时线性）与 `global_scale×0.5` 等价，所以「HLG」在数值上就是「distance 减半」。`cicp=16`（×0.4）→ 27168 B（+47%）、PSNR 48.14 dB；`cicp=0` 与默认 `2` 产物同 SHA（无效果）。
- 影响：用户以为在「声明输入传递函数」，实际是把量化表整体调细/调粗（体积最多 +47%），而产物里既没有该声明、还带着 sRGB 的 APP2 ICC → 语义自相矛盾。另一方面，真正需要该缩放的 HDR 输入本产品不支持（负面清单），所以这个旋钮在现有产品里**没有正确用法**。
- 建议：把这一行降级为「不暴露」——`visible=false` 或常锁 2；若坚持保留，tooltip 必须写明「不写入任何色彩标记，仅缩放量化表；等效于改变视觉距离」，并考虑改为由 `colormanager` 的目标 ICC 自动推导（对齐参考实现）。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:134` 把 `cicp_transfer_function` 列入「同类高危键：表内自述多数码值无效」）。

##### JPEG-05 [P3] `quality_mode=quality` 走 `jpegli_set_quality` → 只写 2 张量化表（Cb/Cr 共用），与参考实现的 3 表路径不等价

- 证据：`src/codecs/enc_jpegli.cpp:272-273`（`jpegli_set_quality(&cinfo, p.quality, TRUE)`）；`lib/jpegli/encode.cc:865-872`（`set_quality` → `SetQuantMatrices(..., add_two_chroma_tables=false)`）vs `:841-847`（`set_distance` → `true`）；参考实现 `lib/extras/enc/jpegli.cc:448-450` 把 quality 先换算成 distance 再走 `set_distance(..., TRUE)`（3 张表）。
- 事实（可复核）：`quality=50` / `quality=95` 的产物 SOF 里 comp2/comp3 都是 `tq1`，DQT 段只有 **2 张表**（`jpeglib` 的 `quant_tbl_no` 相同）；而 `distance` 路径的产物有 3 张表（comp3 用 `tq2`）。体积/画质：`qual50` 9848 B / 34.77 dB，`qual95` 24977 B / 47.86 dB。
- 影响：同一「质量分」下，本产品与 jpegli 参考 CLI 的色度量化表不同（Cb 复用 Cr 的表），体积与色度画质不严格等价；tooltip 引用「参考 CLI 默认 90、允许 1–100」容易让人以为等价。
- 建议：若要严格对齐参考实现，改成 `jpegli_set_distance(&cinfo, jpegli_quality_to_distance(p.quality), TRUE)`（`encode.h:123` 已导出该换算）；否则在 tooltip 注明「色度共用一张量化表」。
- 状态：confirmed；known=no。

##### JPEG-06 [P3] 记账口径：`out_bytes` 只是编码字节数，不含后写的 EXIF/XMP（JPEG 957 → 实际 1169）

- 证据：`src/core/pipeline.cpp:1120`（`row.out_bytes = er.bytes;`）在元数据后写之前；`:1123-1149` 才是 `write_metadata_exiv2`（JPEG 的 `meta_path == "exiv2"`，`format_tables.cpp:415`）。
- 事实（可复核）：`--dev tests/golden/meta/exif_full.jpg --format jpeg` → 侧车 `out_bytes=957`、SUMMARY `bytes=957`，磁盘实际 **1169 B**（Exiv2 追加 APP1 Exif）；带 ICC 时同理（1563 → 1775）。
- 影响：UI 字节列/日志/SUMMARY 对 JPEG（以及其它 exiv2 后写格式）系统性偏小，本例 −18%；对体积估算与用户核对不友好。
- 建议：元数据写成功后（`pipeline.cpp:1164` 之前）用 `fs::file_size(row.out)` 覆盖 `row.out_bytes`，或追加一个 `final_bytes` 字段（`OutputResult` 在冻结面内，需走加性追认）。
- 状态：confirmed；known=no。

##### JPEG-07 [P3] 能力面收窄：库里有入口但 UI 无控件、或 UI 范围远窄于库能力（均已自述，登记以免被当成漏配）

- 证据：`vcpkg_installed/x64-windows-avx2/include/jpegli/encode.h:45-157` 的公开入口 vs `src/core/format_tables.cpp:285-411` 的 15 个参数。
- 事实：
  - 无 UI 的库入口：`jpegli_set_colorspace` / `jpegli_default_colorspace`（RGB/CMYK 直出）、`jpegli_set_linear_quality`（`encode.h:54`）、`jpegli_default_qtables` + `jpegli_add_quant_table`（自定义量化表，`encode.h:58,63`）、`jpegli_suppress_tables`、`jpegli_simple_progression`、`jpegli_set_input_format`（16bit/float 输入，`encode.h:140`）、`jpegli_write_raw_data`、`jpegli_write_coefficients`；`cinfo.write_JFIF_header` 恒为库默认 FALSE（`encode.cc:71`）→ 产物无 APP0；`cinfo.restart_interval`（绝对 MCU 数）也未暴露。
  - 范围收窄：`distance [0.3,3.0]`（库 0.0–25.0，`tools/cjpegli.cc:48-53`）、`restart_in_rows [0,64]`（库 ≤65535 行）、`progressive` 只能表达 level 0/2（库 0/1/2，`tools/cjpegli.cc:142-145`）。
- 影响：与「全参数暴露」原则（AGENTS §1.2-3）存在已知缺口；对「批量像素搬运器」定位而言，自定义量化表/CMYK 直出确实属负面清单边缘，但**范围收窄会让用户设不出库能做的事**（例如 distance=0.1 的近无损、level 1 的轻量渐进）。
- 建议：在 `docs/param-catalog.md` §1 末尾加一张「未暴露的库入口 + 理由」小表；`progressive` 若要支持 level 1，需把 Bool 改成 Enum（预设键变更 → 走裁定）。
- 状态：confirmed（库入口与表项对比是代码事实）；known=no。

#### 4. 输出兼容性与合规矩阵

实测命令统一为 `python tools/env.py run -- build/release-dev/photopipeline.exe --dev <src> --out <dir> --format jpeg --workers 1 [--param ...]`；结构由 `build/encoder-audit/jparse.py`（marker/APP/DQT/SOF/SOS 解析）与 `scancheck.py`（T.81 §B.2.3 扫描脚本校验）取证；画质用 `pp_verify.exe`（psnr 模式，阈值故意设 99 dB 以回读实测值）。

| 源/配置 | 产物结构（实测） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| rgb8 → default（distance=1.0, chroma=444, progressive） | SOF2 13 scans、3 comp 1×1、DQT 3 张、无 APP0 | OIIO+jpegli 解码通过；pp_verify PSNR 42.55 dB | 无 JFIF APP0（jpegli 默认行为，3 分量按 YCbCr 解释是通行约定）；量化表为 420 变体（JPEG-01） |
| rgb8 → chroma=420 | SOF2 13 scans、1 comp 2×2 | 同上（PSNR 31.22 dB） | 本配置下量化表恰好正确；体积 9995 B |
| rgb8 → chroma=440 | SOF2 **15 scans**、1 comp 1×2、色度 1×1 | OIIO+jpegli 解码通过（PSNR 41.82 dB） | 采样比合法（每 MCU 4 块 ≤10）；jpegli 的 `Downsample1x2`（`downsample.cc:83`）与 `DownsampleRow2x1` 均存在，440 是**真支持**而非被静默当 420；但量化表仍是 420 变体（JPEG-01） |
| rgb8 → chroma=422 | SOF2、1 comp 2×1，12503 B | 通过（31.25 dB） | 同上 |
| rgb8 → progressive=false, optimize=false | **SOF0** 1 scan，24344 B | 通过 | 合规基线（无渐进、固定 Huffman） |
| rgb8 → restart_in_rows=2 | SOF2 13 scans + **DRI=64** | 通过 | DRI=32 MCU/行×2，符合 tooltip 的「MCU 行」单位；`restart_interval` 与 progressive 共存无冲突 |
| rgb8 → std_quant_tables=true | SOF2、DQT **2 张**（std Annex K 缩放） | 通过（39.88 dB） | 标准表 + `DistanceToLinearQuality` 分支，符合 `quant.cc:675-681` |
| rgb8 → --color srgb | SOF2 13 scans + **APP2 ICC 589 B / 1 chunk** | 通过 | ICC 分块上限 65519 B/块（`encode.cc:37,1097-1098`），实测远未触及；Exiv2 后写不破坏 ICC（见下） |
| exif_full.jpg（64×64 带 EXIF）→ 同参数 | 编码 957 B + **APP1 Exif**；Exiv2 重写后仍 SOF2 13 scans（最终 1169 B） | 通过 | APP1 上限 65527 B，超限由 Exiv2 抛 `kerTooLargeJpegSegment`（`exiv2/src/jpgimage.cpp:766-767`）→ 该输出行判失败，不静默截断；XMP 上限 65504（`:793-794`） |
| exif_full.jpg → --color srgb | **APP1 Exif + APP2 ICC 共存**，SOF2 13 scans（1775 B） | 通过 | Exiv2 重写保留 ICC APP2（`jpgimage.cpp:816-818` 的 IccProfile 分支）；无标记丢失 |
| gray8.png | SOF2 **5 scans**、1 comp、1 张 DQT（7155 B） | 通过 | 灰度按 1 分量编码，未走 RGB（`enc_jpegli.cpp:211-212`）；`chroma` 对灰度静默无效 |
| gray16.png / rgb16.png | SOF2 8bit（`precision=8`）+ 侧车 warning `DepthDowngrade: source bit depth 16 -> output bit depth 8` | 通过 | 16→8 降档有明确 warning（`pipeline.cpp:1042-1047`），UI 位深行也锁 8 并显示「仅支持 8 位输出」（`page_output.cpp:647-652`）；`--bitdepth 16` 硬失败 exit 2 |
| rgba8.png | SOF2 3 comp（alpha 丢弃）+ warning `AlphaFlattened: alpha composited onto background 1` | 通过 | 拍平底色默认 1.0=白（`pipeline.h:86`），有 warning 且写进侧车 |
| exif_full.jpg → `--metadata-only --meta Exif.Image.Artist=...` | 原容器 SOF0/1 scan/DQT 不变（1650 → 1656 B） | `pp_verify` **exact → VERIFY OK** | 仅元数据模式**不动像素**（本格式通过；不涉及 JPEG-01，因为不重编码） |
| `--lossless --format jpeg` | 有损 SOF2 13 scans，18467 B，与有损基线同 SHA | 通过 | 无任何提示地把 lossless 请求丢给有损编码（JPEG-02） |

#### 5. 未覆盖与存疑

1. **缺第三方解码器交叉验证**：本机无 `ffmpeg` / `magick` / `djpeg` / `exiftool`，「标准工具可读」这一列的实证只覆盖 OIIO→jpegli 解码栈与 `pp_verify`（同一条解码栈）。`chroma=440`（SOF 1×2）与 13/15-scan 渐进脚本在**其它**解码器上的行为未经实测，仅由 T.81 §B.2.3 与 libjpeg 的采样因子语义推断为合法。
2. **`cjpegli` 参考二进制不可用**：`vcpkg-overlay/libjpeg-turbo/portfile.cmake` 关掉了 `JPEGLI_ENABLE_TOOLS`，所以「参考实现 444 应得的表」只能由 `quant.cc` 公式 + `qcheck.py` 反推（已 64/64 命中 420 公式），没有与真实 cjpegli 产物做字节级对照。
3. **UI 暴露面未做渲染取证**：审计为只读，未运行 GUI/ui-smoke。§2 的「UI 暴露」结论来自 `paramform.cpp` 建行逻辑的代码路径（15 个参数无跳过分支）与既有 ui-smoke 锚点，没有截图/控件树证据。
4. **EXIF/XMP 超 64KB 未实测**：只做了源码确认（Exiv2 抛 `kerTooLargeJpegSegment`），未构造 64KB+ EXIF 跑一遍看输出行错误文案。
5. **`dither8` 未实测**：`RunConfig.color_dither8` 只由输出页全局开关控制（`output_page_build.cpp:335-344`、`page_output.cpp:920`），`--dev` 无对应开关；代码上它进 `enc_jpegli.cpp:338-343` 的 `quantize8_dithered`，默认 false。
6. **alpha 拍平的像素正确性未做逐位断言**：只验到 warning 与字节数（10069 B）；底色混白的数值正确性未用 pp_verify 验证。
7. **psnr_target 低于目标的根因未拆解**：实测差 3.9–4.7 dB，归因于「库在 DCT 系数域按分量估计」（`encode_finish.cc:93-118`）与「pp_verify 在 RGB 采样域度量」的度量差异，但未逐项分离（例如纯灰度图上的差距未测）。
8. **`xyb_mode` 解锁后的行为未实测**（锁定态）；「解锁会得到色彩错误产物」的判断来自代码缺口（无 XYB 变换、无 `jpegli_set_input_format(FLOAT)`、无 XYB ICC），非实测。
9. **`quality_mode=quality` 与参考实现的等价性只到表数层面**（2 vs 3 张），未量化由此带来的体积/色度画质差。
10. **既有评审的 known 归因**：`docs/deep-review-0.3.2.md:513-516` 的 R-244/245/246（三个假控件）已由 locked 谓词修完，本审计复核为「已修且实测行为正确」，因此未作为 finding 复报；R-247 仍在（JPEG-03）。另 `docs/param-catalog.md:42-53` 与格式表逐项对照一致（R-316 的漂移已修）。小瑕疵：`format_tables.cpp:341` 引用的 `jconfig.h:13` 实际在第 5 行（`/* #undef C_ARITH_CODING_SUPPORTED */`），属文案行号漂移。

##### 复现命令（全部经 env.py；产物在 build/encoder-audit/）

```
# 建图（纯 stdlib，256×192 合成图）
python build/encoder-audit/mk_img.py

# 参数扫描（27 组，输出目录 build/encoder-audit/out/<case>/）
& "$PSHOME\powershell.exe" -NoProfile -File build/encoder-audit/sweep.ps1
python build/encoder-audit/jparse.py build/encoder-audit/out/base_d1_c444   # 结构
python build/encoder-audit/scancheck.py build/encoder-audit/out/base_d1_c444 # T.81 扫描脚本
python build/encoder-audit/qcheck.py build/encoder-audit/out/base_d1_c444/grad_rgb8.jpg  # DQT 归因

# 画质（阈值 99 只为回读实测 dB）
& "$PSHOME\powershell.exe" -NoProfile -File build/encoder-audit/psnr.ps1

# 形态矩阵（gray/alpha/16bit/ICC/EXIF/仅元数据）
& "$PSHOME\powershell.exe" -NoProfile -File build/encoder-audit/shapes.ps1

# 单条示例
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/in/grad_rgb8.png \
  --out build/encoder-audit/out/chroma420 --format jpeg --param chroma=420 --workers 1
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/vmeta/case/expected.json \
  build/encoder-audit/out2/metaonly/exif_full.jpg
```

### 4.2 JPEG XL（libjxl / VarDCT + Modular）

> 审计基线：`0.3.5.alpha2`，`build/release-dev/photopipeline.exe`（PP_BUILD_DEV=ON），libjxl **0.12.0**
> （`vcpkg_installed/x64-windows-avx2/include/jxl/version.h:21-23`；源码 `vcpkg/buildtrees/libjxl/src/v0.12.0-85f4156902.clean`）。
> 本审计只读：未修改 src/tests/docs/CMake；产物只在 `build/encoder-audit/` 下。
> 实测环境：`--dev` harness，`--workers 1`（⇒ `encode_threads=1` ⇒ 编码器内 runner 为 nullptr，E>1 分支本次未覆盖）。

#### 1. 结论

1. **P0 一条**：勾选「无损编码」后，`编码前降采样`（resampling）**仍然可见可改**，一旦设为 2/4/8，产物会被降采样再无损编码 —— 实测 `pp_verify` 报 `FAIL exact mismatch: 4788/12288`，而 UI/sidecar 依旧 `lossless=true`。这是"静默把有损当无损"。
2. **`color_transform` 是半假参数**：libjxl 的 C API 在入队帧时按 `uses_original_profile` **无条件覆写** `cparams.color_transform`（`encode.cc:923-930`），所以 `XYB`/`None`/`YCbCr` 三选一**产物逐字节相同**（纯 no-op）；唯一生效的 `YCoCg`（RCT 索引 6）恰好是**表默认值**，它把库的"逐组试 RCT"优化关掉，**无损产物体积 2.8×**（实测 70584 B vs 24907 B）。
3. **三个 Modular 高级参数是"一改就整文件失败"的死参数**：`modular_ma_tree_learning_percent`、`channel_colors_global_percent`、`channel_colors_group_percent` 在事实表里是 `Int`，但 libjxl 只在 `JxlEncoderFrameSettingsSetFloatOption` 实现它们；走 int setter 命中 `default:` → `JXL_ENC_ERROR`。实测三者各自 `option ... rejected: ERROR`。
4. **`codestream_level` 默认 10 让每个 JXL 产物都带 `jxll` box（level=10）**：libjxl 默认是 `-1`(auto)，且官方头文件明说"推荐用最小必需等级/level 5 以获得最广兼容"。实测默认产物多 9 字节 `jxll`（载荷 `0x0a`），显式 5 与 auto 逐字节相同。这是无收益的兼容性收窄。
5. **`distance=0`（VarDCT 合法 UI 取值）行为分裂**：无嵌入 ICC 时整文件失败（`distance 0.000000 rejected: ERROR`），有 ICC 时静默变成无损（libjxl `SetFrameDistance(0)` 内部调用 `SetFrameLossless(TRUE)`）。同一 UI 取值两种结局，且失败文案没有任何原因。
6. **`brotli_effort` 是纯装饰**（0 与 11 产物逐字节相同）；**`keep_invisible` 默认 true 覆盖库的 -1(auto)**，带 alpha 的有损产物 3.1× 体积（1041 B vs 331 B）；Modular 在"未勾无损 + 默认 distance=0"时**产物其实是无损**（UI 与产物矛盾）。
7. 正面项：无损（color=keep）在 **RGB8 / RGBA8 / gray8 / gray-α8 / RGB16 / gray16** 六种通道×位深组合上 `pp_verify` **逐位一致**；EXIF/XMP 以规范格式写入 `Exif`/`xml ` box（Exif 载荷 = 4 字节 TIFF 偏移 0 + `II*\0`）；容器结构合规、oiiotool 可读；ICC 与 CICP 不冲突。
8. 与 AGENTS.md §1.2「全参数暴露」有差距：libjxl 0.12 中尚有 6 个相关 frame setting（`extra_channel_resampling`、`group_order_center_x/y`、`buffering`、`disable_perceptual_heuristics`、`output_mode`）与 2 个编码器级函数（upsampling mode、extra channel distance）未进入事实表。

#### 2. 参数全量矩阵

事实表：`src/core/format_tables.cpp:416-601`（jxl 行）。UI 建行：`src/ui/paramform.cpp:742-801`（每个 ParamDef 一行；`lossless` 行不渲染，由复选框独占，`paramform.cpp:796-800`）。下发：`src/ui/page_output.cpp:706-737`（spec_of）。编码器读取点全部在 `src/codecs/enc_jxl.cpp`。

「生效」判定口径：**是** = 实测改动产物字节；**条件** = 只在特定内容/尺寸下可观察（本审计输入未触发）；**否** = 实测接受但产物不变或直接失败；**未知** = 本次未实测。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| vardct.effort | 是（核心行） | 7 | 1–10（`format_tables.cpp:104-119`） | `enc_jxl.cpp:77`→`:402` | 是 | effort=1→1069 B / 9→772 B（64×64 有损）；=11 被 `validate_params` 拒（`param 'effort': value 11 out of range [1, 10]`）。库默认 7（`encode.h:130`）。 |
| vardct.decoding_speed | 是（高级·解码预算） | 0 | 0–4（`:122-137`） | `enc_jxl.cpp:78`→`:402` | 是 | =4 使 24111→24802 B（体积↑、解码↑）。默认 0 = 库默认（`encode.h:136`）。 |
| vardct.codestream_level | 是（高级·编码工具） | **10** | auto(-1)/5/10（`:140-154`） | `enc_jxl.cpp:348-349` | 是 | 10→多 9 B `jxll`(0x0a)；5 与 auto 逐字节相同（979 B）。默认 10 与库默认 -1 不同 → 兼容性收窄（JXL-04）。 |
| vardct.distance | 是（核心行） | 1.0 | 0.0–25.0（`:157-173`） | `enc_jxl.cpp:448-453` | 是 | 0.5→1063 B、3→614 B；**0.0 → 整文件失败**（JXL-05）。无损时可见性隐藏但不锁定。 |
| vardct.photon_noise | 是（高级·噪声） | 0 | 0–6400（`:427-434`） | `enc_jxl.cpp:81`→`:389` | 是 | =200 → 24111→24121 B。Float 分支 `v<0` 跳过（库默认 0）。 |
| vardct.epf | 是（高级·编码工具） | 2 | -1–3（`:435-441`） | `enc_jxl.cpp:82`→`:402` | 是 | 默认 2 覆盖库的 -1(auto)；实测 -1→40038、0→39952、2(默认)→40001。 |
| vardct.keep_invisible | 是（高级·编码工具） | **true** | bool（`:442-447`） | `enc_jxl.cpp:83`→`:394` | 是 | bool 无三态；带 alpha 有损：true 1041 B / false 331 B（**3.1×**，JXL-08）。 |
| vardct.dots | 是（高级·编码工具） | true | bool（`:448-454`） | `enc_jxl.cpp:84`→`:394` | 条件 | dots=false 与默认逐字节相同（本输入未触发点阵）。 |
| vardct.patches | 是（高级·编码工具） | true | bool（`:455-461`） | `enc_jxl.cpp:85`→`:394` | 条件 | 同上。 |
| vardct.gaborish | 是（高级·编码工具） | true | bool（`:462-468`） | `enc_jxl.cpp:86`→`:394` | 是 | gaborish=false：40001→33084 B。 |
| vardct.progressive_ac | 是（高级·渐进） | false | bool（`:469-476`） | `enc_jxl.cpp:87`→`:394` | 是 | =true：40001→41274 B。 |
| vardct.qprogressive_ac | 是（高级·渐进） | false | bool（`:477-484`） | `enc_jxl.cpp:88`→`:394` | 是 | =true：24111→25081 B。 |
| vardct.progressive_dc | 是（高级·渐进） | -1 | -1–2（`:485-492`） | `enc_jxl.cpp:89`→`:402` | 是 | =2：24111→25765 B。 |
| vardct.resampling | 是（高级·编码工具） | -1 | auto/1/2/4/8（`:188-198`） | `enc_jxl.cpp:79`→`:402` | 是 | 无损时 `visible=not_lossless`（`:205`）但**不锁定**；实测 =1 与 auto 等价（distance=1）。**P0 见 JXL-01**。 |
| vardct.group_order | 是（高级·编码工具） | -1 | auto/scanline/center（`:209-215`） | `enc_jxl.cpp:80`→`:402` | 条件 | =1 与默认逐字节相同（64×64 只有 1 个 256×256 组，布局无差）。 |
| vardct.lossless | 否（由复选框独占，`paramform.cpp:796-800`） | false | bool（`:495-496`） | `enc_jxl.cpp:212`→`:234-236`,`:438-446` | 是 | 两键同步：`apply_locks`（`params.cpp:495-507`）。勾选 → 自动切 Modular（`paramform.cpp:1284-1310`）。 |
| modular.effort | 是（核心行） | 7 | 1–10（`:104-119`，`lossy_only=false` ⇒ 恒可见） | `enc_jxl.cpp:77`→`:402` | 是 | 无损时不隐藏（与 vardct 面不同）。 |
| modular.decoding_speed | 是（高级·解码预算） | 0 | 0–4（`:122-137`） | 同上 | 是 | 恒可见。 |
| modular.codestream_level | 是（高级·编码工具） | **10** | auto/5/10（`:140-154`） | `enc_jxl.cpp:348-349` | 是 | 见 JXL-04。 |
| modular.distance | 是（核心行） | **0.0** | 0.0–15.0（`:174-184`） | `enc_jxl.cpp:448` | 是 | 0=完全无损（库行为，`encode.cc:1593`）；无损时 `locked=lock_zero_when_lossless`（`format_tables.cpp:183,49-53`）。**未勾无损 + 默认 0 也产出无损**（JXL-07）。 |
| modular.color_transform | 是（高级·编码工具） | **YCoCg** | None/YCoCg/XYB/YCbCr（`:504-518`） | `enc_jxl.cpp:414-434` | **否（仅 YCoCg 部分生效）** | None/XYB/YCbCr 被 libjxl 覆写为 no-op；YCoCg 走 `MODULAR_COLOR_SPACE=6` 全局 RCT。无 auto 取值。**JXL-02**。 |
| modular.modular_group_size | 是（高级·编码工具） | -1 | -1–3（`:519-526`） | `enc_jxl.cpp:90`→`:402` | 是 | =0：35372→35334 B。 |
| modular.modular_predictor | 是（高级·编码工具） | -1 | -1–15（`:527-534`） | `enc_jxl.cpp:91`→`:402` | 是 | =0：35372→55384 B。 |
| modular.modular_palette_colors | 是（高级·编码工具） | -1 | -1–4096（`:535-541`） | `enc_jxl.cpp:92`→`:402` | 条件 | =0 与默认逐字节相同（本输入未启用调色板）。库上限 70913（`encode.cc:1791`），表收窄已注明。 |
| modular.modular_lossy_palette | 是（高级·编码工具） | false | bool（`:542-548`） | `enc_jxl.cpp:93`→`:394` | 是 | =true：35372→55384 B；无损时 `locked=lock_false_when_lossless`（`:548`），实测快照 `modular_lossy_palette=false`。 |
| modular.brotli_effort | 是（高级·编码工具） | -1 | -1–11（`:549-556`） | `enc_jxl.cpp:94`→`:402` | **否** | 0 与 11 产物逐字节相同（235 B）；只服务 brob/JPEG 重压缩，本工具 AddBox 传 `JXL_FALSE`（`enc_jxl.cpp:527,536`）。**JXL-09**。 |
| modular.responsive | 是（高级·编码工具） | false | bool（`:557-564`） | `enc_jxl.cpp:95`→`:394` | 是 | =true：35372→27949 B。 |
| modular.channel_colors_global_percent | 是（高级·编码工具） | -1 | -1–100（`:565-571`） | `enc_jxl.cpp:96-97`→`:402` | **否** | 设 0 → `option channel_colors_global_percent rejected: ERROR`（错 setter，**JXL-03**）。 |
| modular.channel_colors_group_percent | 是（高级·编码工具） | -1 | -1–100（`:572-578`） | `enc_jxl.cpp:98-99`→`:402` | **否** | 设 0 → 同上。 |
| modular.modular_ma_tree_learning_percent | 是（高级·编码工具） | -1 | -1–**200**（`:579-586`） | `enc_jxl.cpp:100-101`→`:402` | **否** | 设 50 → `option ... rejected: ERROR`（错 setter + 上限越界，JXL-03/JXL-11）。 |
| modular.modular_nb_prev_channels | 是（高级·编码工具） | -1 | -1–11（`:587-593`） | `enc_jxl.cpp:102-103`→`:402` | 条件 | =0 与默认逐字节相同（本输入 3 通道 + 有损路径未触发差异）。 |
| modular.resampling | 是（高级·编码工具，**无可见性谓词**） | -1 | auto/1/2/4/8（`:188-198,594`） | `enc_jxl.cpp:79`→`:402` | 是 | **无损时仍可见可改** → P0（JXL-01）。 |
| modular.group_order | 是（高级·编码工具） | -1 | auto/scanline/center（`:209-215,595`） | `enc_jxl.cpp:80`→`:402` | 条件 | 同 vardct。 |
| modular.lossless | 否（复选框独占） | false | bool（`:596-597`） | `enc_jxl.cpp:212`,`:438-446` | 是 | 无损 → `JxlEncoderSetFrameDistance(0)` + `SetFrameLossless(TRUE)`（`enc_jxl.cpp:439-441`）。 |
| out_bitdepth（UI 位深行） | 是（位深行，`page_output.cpp:640-654`） | **16**（`pipeline.cpp:1721-1722`） | 8/16（`format_tables.cpp:599`） | `enc_jxl.cpp:194-198,299` | 是 | 16→8 差异：无损 238→226 B；有损 40001→39999 B。16 位无损需 level 10（JXL-06）。 |
| tech（技术行） | 是（`paramform.cpp:476-498`） | vardct（首个技术，`params.cpp:332-341`） | vardct/modular | `enc_jxl.cpp:223-236` | 是 | 勾无损 → 自动切 modular；无损下非 lossless_capable 项置灰（`paramform.cpp:549-563`）。 |

未进入事实表的库参数（本工具相关）：`JXL_ENC_FRAME_SETTING_EXTRA_CHANNEL_RESAMPLING`（`encode.h:153`）、`GROUP_ORDER_CENTER_X/Y`（`:220,225`）、`BUFFERING`（`:356`）、`USE_FULL_IMAGE_HEURISTICS`（`:388`）、`DISABLE_PERCEPTUAL_HEURISTICS`（`:393`）、`OUTPUT_MODE`（`:418`）、`JxlEncoderSetUpsamplingMode`（`:1446`）、`JxlEncoderSetExtraChannelDistance`（`:1484`）。仅 JPEG 重建相关的 9 项（`JPEG_RECON_CFL`/`JPEG_COMPRESS_BOXES`/`JPEG_KEEP_*`/`NOISE`/`ALREADY_DOWNSAMPLED` 等）不适用，不计。

#### 3. 发现

##### JXL-01 [P0] 无损 + resampling：无损复选框亮着，像素却被降采样

> **主编归一（2026-10-25，独立复核后）**：本条按 **P1** 计入总表（见 7.0）——触发需要把非默认 `resampling` 显式调离 1；默认 `-1/1` 不产生该问题。

- 证据：`src/core/format_tables.cpp:188-207`（`jxl_resampling_param`：modular 分支 `visible` 为空谓词、`locked` 为空）；`src/core/params.cpp:419-423`（空谓词 ⇒ 恒可见）；`src/core/params.cpp:219-289`（`cross_rules()` 只有 tiff/webp/jpeg 三族规则，**无 jxl**）；`src/codecs/enc_jxl.cpp:214-219,373-382`（按 key 是否存在于 ParamSet 决定应用，与可见性无关）、`:438-446`（lossless 分支只做 `SetFrameDistance(0)`+`SetFrameLossless`）；`vcpkg/buildtrees/libjxl/src/v0.12.0-85f4156902.clean/lib/jxl/enc_params.h:144-149`（`SetLossless()` 只改 `modular_mode`/`butteraugli_distance`/`ec_distance`/`color_transform`，**不动 resampling**）；同目录 `lib/jxl/enc_frame.cc:104-117`（`resampling <= 0` 才归一为 1，显式 2 原样保留）。
- 事实：① UI 层——Modular 技术下 `resampling` 谓词为空 ⇒ 勾选「无损编码」后该行**仍显示、仍可编辑**（`paramform.cpp:972-993` 只按 `eval_visible` 隐藏，而该谓词恒真）；② 下发层——`spec_of()` 把 `form->values()` 全量写入 `spec.params`（`page_output.cpp:728-730`，隐藏行也在 `values` 里，`paramform.cpp:1362-1368` 只剔除 `__` 前缀键）；③ 引擎层——`default_params` 会填满全部键（`params.cpp:473-474`），`enc_jxl.cpp:373-382` 只看"键存在"，于是 `RESAMPLING=2` 被设置，`SetFrameLossless` 又不重置它 ⇒ libjxl 先 2×2 降采样再无损编码，解码端升采样。
- 影响：实测 `--format jxl --lossless --param resampling=2`（`tests/golden/base/rgb8.png`）产出 228 B，`pp_verify` → `VERIFY jxl-lossless FAIL exact mismatch: 4788/12288 samples differ; first at pixel (7,0) channel 0: input 0.000000 vs output 0.254871`；同参不带 resampling 为 238 B 且 `VERIFY ... OK`。用户视角：勾了「无损」，sidecar 写 `"lossless=true"`，日志零 warning，但产物只有 1/4 的像素是真的。这正是严重度定义里的 P0（静默把有损当无损）。触发路径不止 CLI：在 Modular 技术下直接改该行、或先在有损档设 2/4/8 再勾无损（`rebuild_params` 会把同键值带到 modular，`paramform.cpp:893-908`），以及载入 "lossless=true 且 resampling=2" 的预设。
- 建议：三处同时收口（任一处即可阻断，建议全上）——① `format_tables.cpp:188-207` 的 modular 分支补 `p.visible = not_lossless`（与 vardct 分支一致）并新增 `lock_one_when_lossless` 谓词（无损时强制 1，参照 `:49-53` 的 `lock_zero_when_lossless` 写法）；② `params.cpp` 的 `cross_rules()` 加 jxl 规则：`lossless && resampling > 1` → "无损模式下不能降采样"（走 `paramform.cpp:932-945` 的红字区）；③ `enc_jxl.cpp:438` 的 lossless 分支补一条 `JxlEncoderFrameSettingsSetOption(fs, RESAMPLING, 1)` 兜底 + `log_warn`。配套：`tests/unit/test_params` 加"lossless ⇒ resampling==1"断言，金样加一对 `jxl-lossless-resampling`（pixel exact）。

##### JXL-02 [P1] color_transform：三个取值被 libjxl 覆写成 no-op；默认 YCoCg 反而让无损体积 2.8×
- 证据：`vcpkg/buildtrees/libjxl/src/v0.12.0-85f4156902.clean/lib/jxl/encode.cc:923-930`（处理入队帧时按 `metadata.m.xyb_encoded` **无条件覆写** `input_frame->option_values.cparams.color_transform`，注释还留着 `TODO(zond): Figure out when to use kYCbCr instead`）；`lib/jxl/enc_params.h:144-149`（`SetLossless()` 强制 `color_transform = kNone`）；`lib/jxl/enc_modular.cc:903-919`（`colorspace >= 0` ⇒ 全局 RCT 一次；`colorspace < 0` 才让库自己试）+ `:1438-1443`（"试多个 RCT 取最优"的优化要求 `colorspace < 0`）；`src/core/format_tables.cpp:504-518`（4 个 choices、def=YCoCg、**无 auto**）；`src/codecs/enc_jxl.cpp:414-434`（YCoCg→`MODULAR_COLOR_SPACE=6`；XYB/None/YCbCr→`COLOR_TRANSFORM` 0/1/2）。
- 事实：libjxl 的 C API 路径把 `COLOR_TRANSFORM` 当作不可用旋钮（入队时按 `uses_original_profile` 重写），所以 `None`/`XYB`/`YCbCr` 三者**产物逐字节相同**。实测（256×256 自造图，`build/encoder-audit/jxl-rt/big.png`）：8bit 无损 `color_transform=YCoCg` → 70046 B；`None`/`YCbCr`/`XYB` → 全部 24254 B 且 MD5 相同（`69E3320778E4`）。16bit 无损（默认位深）：默认(YCoCg) 70584 B vs `None` 24907 B（**2.83×**）。有损 Modular（distance=2）下四者全同（35372 B），因为库自适应本来就选 RCT6。也就是说：**唯一生效的取值恰好是表默认值，而它把体积放大近 3 倍；其余三个取值是死值**，且没有任何 UI 取值能得到 libjxl 的默认行为。
- 影响：① 用户在 `XYB`/`YCbCr` 上看到 tooltip 承诺（"XYB 走前者""YCbCr 声明数据为 YCbCr"）却完全无效，无任何提示；② 默认配置的 JXL 无损产物比应得的大约 2.8 倍，直接违背"高质量+高性能"；③ 「全参数暴露」名存实亡（4 选 1 实为 2 种行为，且 1 种是坏的）。
- 建议：① `format_tables.cpp:504-518` 增加 `{"auto", int64_t(-1)}` 并把 `def` 改 `auto`；② `enc_jxl.cpp:414-434`：取到 `-1`/`auto` 时**不设置任何** color transform/colorspace（把库默认交还）；③ tooltip 改成事实："`JXL_ENC_FRAME_SETTING_COLOR_TRANSFORM`(XYB/None/YCbCr) 在当前 libjxl C API 下会被库按 uses_original_profile 覆写，选它们等于不设置；只有 YCoCg 会强制全局 RCT（通常更大）"。④ 若要保留 XYB，只能通过 `uses_original_profile=false`（即不嵌 ICC）从 VarDCT 侧获得，需在文档/UI 里说清。

##### JXL-03 [P1] 三个 Modular 参数用错 setter：一设置就整文件编码失败
- 证据：`src/codecs/enc_jxl.cpp:96-103`（三键均为 `ParamKind::Int`）、`:397-409`（Int → `JxlEncoderFrameSettingsSetOption`）；`vcpkg/buildtrees/libjxl/src/v0.12.0-85f4156902.clean/lib/jxl/encode.cc:1942-1958`、`:1960-1983`（三个 id **只在 `SetFloatOption` 里实现**）；同文件 `:1925-1928`（int setter 未列的 id 命中 `default:` → `"Unknown option"` → `JXL_ENC_ERROR`）；`vcpkg_installed/x64-windows-avx2/include/jxl/encode.h:1331-1333`（`JxlEncoderFrameSettingsSetFloatOption` 声明）、`:299-303`（MA 树比例的官方文档，只挂在 float 语义上）。
- 事实：`enc_jxl.cpp:399-401` 只对负值跳过（默认 -1 被跳过 ⇒ 默认不炸），因此这三个参数**默认看起来正常，用户一改成非默认值就整文件失败**。实测三条（`--tech modular --param distance=2`）：`modular_ma_tree_learning_percent=50` → `encode failed: jxl: option modular_ma_tree_learning_percent rejected: ERROR`；`channel_colors_global_percent=0` → 同构失败；`channel_colors_group_percent=0` → 同构失败。对照：`modular_predictor`/`modular_group_size`/`modular_lossy_palette`/`responsive` 均正常改变产物（见 §2）。
- 影响：3/20 个 Modular 参数是"死参数"，且失败信息只有 `ERROR`（libjxl 的真实原因是 "Unknown option"）。用户会认为"这个编码器坏了"。
- 建议：① `enc_jxl.cpp:100-101`、`:96-99` 三行改 `ParamKind::Float`（float setter 接受 -1..100，`encode.cc:1943`），或在 `ParamKind` 里加 `IntViaFloat` 显式表达；② `modular_ma_tree_learning_percent` 的 `hi` 从 200 收到 100（库校验 `value > 100` 即报错，`encode.cc:1943-1946`）；③ 补 `tests/unit/test_enc_jxl`：三键各取一个非默认值必须 `JXL_ENC_SUCCESS`。

##### JXL-04 [P1] codestream_level 默认 10：每个产物都写 jxll=10，兼容性无谓收窄
- 证据：`src/core/format_tables.cpp:140-154`（`def = int64_t(10)`，tooltip "catalog 默认 10"）；`src/codecs/enc_jxl.cpp:348-353`（`param_int(params,"codestream_level",10)` → `JxlEncoderSetCodestreamLevel`）；`vcpkg_installed/x64-windows-avx2/include/jxl/encode.h:1373-1375`（"Using the minimum required level, or level 5 in most cases, is **recommended for compatibility with all decoders**"）、`:1387-1389`（"Level 10 … **may be less widely supported**"）、`:1391-1392`（"The default value is -1"）；`vcpkg/buildtrees/.../lib/jxl/encode.cc:852-861`（`codestream_level != 5` ⇒ 在 ftyp 后写 `jxll` box，载荷 = 等级字节）。
- 事实：同一 8bit VarDCT 编码（`--bitdepth 8` 无关，实测 `tests/golden/base/rgb8.png`）：`codestream_level=5` → 979 B、box 序列 `JXL/ftyp/jxlc/Exif`；`=auto` → 979 B、与 5 **逐字节相同**；`=10` → 988 B、`JXL/ftyp/jxll(0x0a)/jxlc/Exif`。**默认（不传该参数）**产物一律带 `jxll=10`（`build/encoder-audit/jxl-box 解析：jxll=0a`）。libjxl 自身的默认是 `-1`（auto），即"按 basic info 选 5 或 10"。
- 影响：本工具产出的**每一个** JXL 文件都声明"需要 level 10"，而 18181-2 的 `jxll` box 语义就是"本码流依赖 level 10 特性"。只实现强制基线等级 5 的解码器/校验器会直接拒收；而本工具 100% 的输出（8/16bit RGB/灰度/alpha、无 CMYK、无 32bit、无 1G 像素）在 level 5 下完全够用。多 9 字节是次要的，**无收益的兼容性收窄**是主要的。
- 建议：`format_tables.cpp:145` 的 `def` 改 `int64_t(-1)`（保留 5/10 手选），tooltip 补一句"auto = 按内容自动选 5/10；10 仅在 16bit 无损等场景自动启用"。若担心回归基线，一次性重播种并在提交正文归因（当前金样断言未钉 codestream_level，实测没有字节级断言依赖）。

##### JXL-05 [P1] distance=0（VarDCT 合法 UI 值）无 ICC 时整文件失败、有 ICC 时静默变无损
- 证据：`src/core/format_tables.cpp:164-173`（vardct distance `lo = 0.0`，tooltip 明写"0.0 需配合 JxlEncoderSetFrameLossless 才是真无损"）；`src/codecs/enc_jxl.cpp:307`（`uses_original_profile = (lossless || modular || has_icc)`）、`:448-453`（无损关闭时也调 `JxlEncoderSetFrameDistance`）；`vcpkg/buildtrees/.../lib/jxl/encode.cc:1582-1597`（`distance == 0` ⇒ 内部 `return JxlEncoderSetFrameLossless(frame_settings, JXL_TRUE)`）、`:1570-1577`（`lossless && basic_info_set && metadata.m.xyb_encoded` ⇒ 报错 `"Set uses_original_profile=true for lossless encoding"`）。
- 事实：源无嵌入 ICC（`rgb8.png`，`--color keep` ⇒ `icc_to_embed` 为空 ⇒ `has_icc=false`）→ `uses_original_profile=false` → `xyb_encoded=true` → `--param distance=0` 实测：`encode failed: jxl: distance 0.000000 rejected: ERROR`（整文件失败，0 字节）。同一命令加 `--color srgb`（合成并嵌入 sRGB ICC，`has_icc=true`）→ 成功产出 807 B。即**同一 UI 取值在"源带 ICC"与"源不带 ICC"下结局完全不同**（失败 vs 静默切换成无损编码）。
- 影响：VarDCT 的核心参数拖到 0（UI 允许 0.0，tooltip 还暗示可行）会整文件失败，且用户只能看到 `ERROR`；换个带 ICC 的源又"成功"了——但产物其实是无损 Modular（体积/语义都变了），而 UI 仍显示 VarDCT/distance 0/未勾无损。
- 建议：① `format_tables.cpp:166` 的 `lo` 改 0.1（与 libjxl `kMinButteraugliDistance` 语义一致，`enc_frame.cc:98-100` 对非 modular 也会钳到该值）；② 或保留 0.0 但在 `cross_rules()` 加 jxl 规则：`!lossless && distance == 0` → 中文红字"距离 0 需要勾选无损"；③ `enc_jxl.cpp:448` 前加显式守卫，返回可读错误（如 `"jxl: distance 0 requires lossless mode"`）而不是把库错误码翻译成 `ERROR`。

##### JXL-06 [P2] level 5 + 无损 + 默认 16bit ⇒ 编码失败，且 libjxl 的诊断被吞掉
- 证据：`src/codecs/enc_jxl.cpp:348-353`（`JxlEncoderSetCodestreamLevel` 在 `JxlEncoderSetBasicInfo` **之后**才调用）；`vcpkg/buildtrees/.../lib/jxl/encode.cc:1416-1427`（等级校验发生在 `SetBasicInfo` 内，此刻 `codestream_level` 还是 -1 ⇒ 通过）、`:783-808`（首次写码流时再校验，失败时真实消息是 `"Codestream level verification for level 5 failed: Too high modular bit depth"`）；`:1330-1333`（`uses_original_profile` 且 `bits_per_sample > 12` ⇒ `modular_16_bit_buffer_sufficient=false` ⇒ 需 level 10）；`src/codecs/enc_jxl.cpp:551-558`（失败只报 `status_name()` = `"ERROR"`）。
- 事实：`--format jxl --lossless --param codestream_level=5`（默认 `--bitdepth 16`）→ `encode failed: jxl: JxlEncoderProcessOutput failed: ERROR`；加 `--bitdepth 8` → 226 B，`VERIFY OK`。即默认位深（16）与"兼容性最广"档（5）组合必然失败，且错误串不含任何原因。
- 影响：用户在"无损 + 最广兼容"这个看起来很自然的组合上撞墙，无从判断是位深还是等级的问题，也看不到 libjxl 给出的具体原因。
- 建议：① 把 `JxlEncoderSetCodestreamLevel` 移到 `SetBasicInfo` **之前**（`enc_jxl.cpp:348` 上移到 `:295` 前），让 libjxl 在 `SetBasicInfo` 内就报错，并把返回状态翻译成可读错误；② 或在设置 basic info 后调 `JxlEncoderGetRequiredCodestreamLevel()`（`encode.h:1410-1430`）预检：`required > level` 时返回 `"jxl: codestream_level=5 不足以承载 16 位无损（需要 level 10）"`；③ `status_name()` 之外补 `JxlEncoderGetError()`（`encode.h` 的 `JxlEncoderError`）与 libjxl 的 debug 串进日志。

##### JXL-07 [P2] Modular 未勾无损 + 默认 distance=0 ⇒ 产物其实是无损（UI 与产物语义相反）
- 证据：`src/core/format_tables.cpp:174-184`（modular distance `def = 0.0`）；`src/codecs/enc_jxl.cpp:447-453`（未勾无损也走 `JxlEncoderSetFrameDistance`）；`vcpkg/buildtrees/.../lib/jxl/encode.cc:1593-1595`（distance==0 ⇒ 自动 `SetFrameLossless(TRUE)`）；`lib/jxl/enc_params.h:124-141`（`IsLossless() = modular_mode && ModularPartIsLossless()`，distance==0 且非 XYB 即视为无损）。
- 事实：`--format jxl --tech modular`（不传 lossless、不传 distance、默认 bitdepth 16）产出 238 B，`pp_verify` → `VERIFY jxl-lossless OK`，且与 `--lossless` 产物**逐字节相同**（MD5 `4DFAD857356A`）；sidecar 里 `"lossless": false`。反向路径同样成立：在 Modular 技术下勾过无损再取消（技术不变 ⇒ `rebuild_params` 不跑），`apply_locks` 写下的 `distance=0` 会留在参数集里，产物仍是无损。
- 影响：用户以为在做"有损 Modular"（体积应当小），实际得到无损文件（体积大数倍）。不是数据损坏，但 UI 与产物语义相反，且"有损 Modular"实际上只有一个隐式入口（必须手动把 distance 抬到 >0）。
- 建议：① `format_tables.cpp:175` 的 `def` 改 0.1（或给 distance 加"0=无损"的枚举化/提示）；② 或在 `paramform` 里当 `tech==modular && distance==0 && !lossless` 时用 `cross_error` 红字提示"当前配置实际为无损输出"；③ 取消无损时把 distance 恢复为默认值（在 `apply_locks` 之外补一个"解锁复原"路径，属于参数引擎行为变更，需追认）。

##### JXL-08 [P2] keep_invisible 默认 true 覆盖库的 -1(auto)：带 alpha 的有损产物 3.1×
- 证据：`src/core/format_tables.cpp:442-447`（`def = true`，tooltip 自认"Bool 无法表达 -1，本表取 true（更保真）"）；`vcpkg_installed/.../jxl/encode.h:205-208`（`-1` = "1 if lossless, 0 if lossy"）；`src/codecs/enc_jxl.cpp:392-396`（bool 一律写 0/1，`-1` 三态不可达）。
- 事实：`rgba8.png` + VarDCT `distance=1`：`keep_invisible=true`（默认）1041 B；`false` 331 B（**3.14×**）。同类三态损失还有 `dots`/`patches`/`gaborish`/`responsive`（都只能 0/1），但实测 `dots=false`、`patches=false` 与默认逐字节相同（本输入未触发），`gaborish` 的库默认本就是开，影响小。
- 影响：所有带 alpha 的有损 JXL 产物默认保留不可见像素的 RGB，体积约为必要的 3 倍；用户需手动到高级区关闭（且默认值与"体积合理"冲突）。
- 建议：把 Bool 升级为三态枚举（`auto`/`on`/`off`，auto 映射 -1）并在 tooltip 里量化；最小改动是把默认改为 false（有损档）并保留开关，同时在 tooltip 写明"关掉可显著减小带 alpha 有损产物体积（实测约 3×）"。

##### JXL-09 [P2] brotli_effort 是装饰性参数（0 与 11 逐字节相同）
- 证据：`src/core/format_tables.cpp:549-556`（`group = 编码工具`，且 table 把它归到 `Tech::Modular`；tooltip 自认"头文件注明其服务 JPEG 重压缩与元数据 box，与 modular 编码无关"）；`vcpkg/buildtrees/.../lib/jxl/encode.cc:1684-1693`（只写 `cparams.brotli_effort` 与 `enc->brotli_effort`）；`vcpkg_installed/.../jxl/encode.h:329-333`（"for use in JPEG recompression and compressed metadata boxes (brob)"）；`src/codecs/enc_jxl.cpp:526-536`（`JxlEncoderAddBox(..., JXL_FALSE)` ⇒ 不压缩 box）。
- 事实：`--tech modular --param distance=1 --param brotli_effort=0` 与 `=11` 产出逐字节相同（均 235 B，MD5 `8B9609558DCA`）。本工具不做 JPEG 重压缩、AddBox 也不请求压缩 ⇒ 该值没有任何消费点。
- 影响：UI 暴露一个永远不会影响产物的旋钮（假桩），属于评审口径里的"装饰性参数"。
- 建议：删除该行；或改名/改 tooltip 为"brob/JPEG 重建专用（本工具不产出 ⇒ 不生效）"；若要真生效，`AddBox` 的 `compress_box` 传 `JXL_TRUE` 让 Exif/XMP 走 brob（需评估第三方解码器对 brob 的支持）。

##### JXL-10 [P2] 库支持但事实表/UI 未暴露的参数（本工具相关 6 + 2 项）
- 证据：`vcpkg_installed/x64-windows-avx2/include/jxl/encode.h:153`（`EXTRA_CHANNEL_RESAMPLING`）、`:220,225`（`GROUP_ORDER_CENTER_X/Y`）、`:356`（`BUFFERING`）、`:388`（`USE_FULL_IMAGE_HEURISTICS`）、`:393`（`DISABLE_PERCEPTUAL_HEURISTICS`）、`:418`（`OUTPUT_MODE`）、`:1446-1460`（`JxlEncoderSetUpsamplingMode`）、`:1484-1485`（`JxlEncoderSetExtraChannelDistance`）；`src/core/format_tables.cpp:416-601`（jxl 表无上述任何键）。
- 事实：表内 24 个 frame setting + 4 个特例，覆盖 libjxl 0.12 的 41 个 setting id 中所有与本工具相关的"常用"项，但缺 6 个 setting 与 2 个编码器级函数。其中 `extra_channel_resampling`（alpha 单独降采样）、`disable_perceptual_heuristics`（非感知编码）、`group_order_center_x/y`（`group_order=center` 的中心坐标，当前 UI 只能"默认中心"）会改变产物；`output_mode`/`buffering` 影响内存与 `ftyp` 版本兼容性。
- 影响：与 AGENTS.md §1.2 第 3 条「所用库的全部参数进入 GUI 可配置」不一致；`group_order=center` 实际只暴露了一半能力。
- 建议：补 `extra_channel_resampling`（Modular/VarDCT 通用，可见性跟随 alpha）、`group_order_center_x/y`（仅 `group_order==center` 时可见）、`disable_perceptual_heuristics`（高级·编码工具）；`output_mode`/`buffering` 作为性能档归"编码工具"，默认 auto。补齐后在 `docs/param-catalog.md` §2.2/§2.3 同步登记。

##### JXL-11 [P3] 范围/枚举边界与库不一致
- 证据：`src/core/format_tables.cpp:579-586`（`modular_ma_tree_learning_percent` `hi = 200`，tooltip 还写">100 亦允许"）vs `vcpkg/buildtrees/.../lib/jxl/encode.cc:1942-1946`（float setter `value > 100` 即报错）；`format_tables.cpp:535-541`（palette_colors `hi = 4096`）vs `encode.cc:1790-1794`（库允许 `-1..70913`）；`format_tables.cpp:177`（modular distance `hi = 15`）vs 库 `0..25`（`encode.h:1455`）。
- 事实：`>100` 那条是**放宽到库不接受的值**（修好 JXL-03 后会变成运行期失败面）；另两处是有意收窄且 tooltip 已注明，属可接受。
- 影响：JXL-03 修好后 101–200 仍会失败；其余无实际后果。
- 建议：`hi` 改 100；palette/distance 的收窄保留，tooltip 已说明。

##### JXL-12 [P3] 错误路径不带 libjxl 诊断（`status_name` 只有三个词）
- 证据：`src/codecs/enc_jxl.cpp:58-60`（`status_name` 只返回 `SUCCESS`/`ERROR`/`NEED_MORE_OUTPUT`）、`:406-409`、`:550-558`；`vcpkg_installed/.../jxl/encode.h:83-118`（`JxlEncoderError` 枚举 + `JxlEncoderGetError`）。
- 事实：本次实测的 4 类失败（`distance=0`、level 5 + 16bit 无损、三个错 setter 参数）在 UI/日志里**全部只显示 `ERROR`**，libjxl 的真实原因（如 `"Set uses_original_profile=true for lossless encoding"`、`"Unknown option"`、`"Codestream level verification for level 5 failed: Too high modular bit depth"`）一个都没透出。
- 影响：用户与维护者都无法自助排障，测试失败信息也失去诊断价值。
- 建议：给 `EncodeResult.error` 补 `JxlEncoderGetError(enc)` 的枚举转写（+debug 构建下的 libjxl 消息），或至少在 `log_error` 里带该码。

##### JXL-13 [P3] 分组与文案打磨
- 证据：`src/core/format_tables.cpp:435-441`（`epf` 归 `kGroupEncodeTools`，而 `photon_noise` 归 `kGroupNoise`；EPF 是边缘保持/降噪滤波器）；`src/ui/paramform.cpp:509-519`（无损复选框 tooltip 只说"开启后视觉距离按 0 处理"，不提 resampling/color_transform）；`format_tables.cpp:149-152`（`codestream_level` tooltip 写"5=兼容性最广"但没说默认 10 会牺牲兼容性）。
- 事实：默认值 10 与"推荐 5"的库建议相反（见 JXL-04），UI 未提示；无损 tooltip 未覆盖 P0 的 resampling 面。
- 影响：用户在不知情的情况下接受兼容性/体积损失。
- 建议：`epf` 归"噪声"组；无损 tooltip 补"无损会忽略降采样/视觉距离/颜色变换组合"；`codestream_level` tooltip 补"默认 10 会写 jxll box，兼容性较 5 窄"。

#### 4. 输出兼容性与合规矩阵

验证工具：`pp_verify`（libjxl 解码 + 16bit 量化逐位比较）与 `oiiotool --info -v`（OIIO `jpegxl` 读图器，`vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe`）。容器结构由纯 Python box 解析（`build/encoder-audit/jxl_box_parse.py`）。

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| `rgb8.png` → VarDCT 默认（16bit，level 10） | `JXL`/`ftyp`/`jxll(0x0a)`/`jxlc`/`Exif(102B)` | oiiotool：64×64 3ch uint16，`CICP: 1,13,0,1`；pp_verify 可解 | 低；唯一风险 = `jxll` 声明 level 10（JXL-04） |
| 同上 + `codestream_level=5` / `auto` | `JXL`/`ftyp`/`jxlc`/`Exif`（**无 jxll**） | 同上 | 无（两者逐字节相同） |
| `rgb8.png` → 16bit 无损（默认） | `JXL`/`ftyp`/`jxll(0x0a)`/`jxlc(95B)`/`Exif` | `VERIFY OK`（逐位一致） | 低；无 ICC，靠 nclx（oiiotool 显示 CICP + 合成 sRGB ICC） |
| `rgb8.png` → 8bit 无损 + level 5 | 无 jxll | `VERIFY OK` | 无 |
| `rgba8.png` → 16bit 无损 | 同上 + alpha 通道（`alpha_bits=16`） | oiiotool 4ch `R,G,B,A`；`VERIFY OK` | 无 |
| `gray8.png`/`gray16.png` → 无损 | 1ch `Y`；`CICP: 0,13,0,1`；无 ICC | 可读；`VERIFY OK` | 低（灰度 primaries=0 合法；OIIO 合成 `Gra_D65_Rel_SRG`） |
| `rgb16.png` → 无损（16bit） | 3ch uint16 | `VERIFY OK` | 无 |
| `exif_full.jpg` → 16bit 无损 | `Exif` box 214B：4B 偏移 `00000000` + `II*\0` TIFF | 可读（Exif 载荷格式为 JXL 容器约定的 TIFF 偏移前缀） | 无 |
| `jxl_exif.jxl` → 16bit 无损（JXL→JXL） | `Exif(214B)` + `xml (2669B)` 双 box 保留；XMP 内容为合法 `<?xpacket…` 包 | 可读 | 无（JXL 输入的 EXIF/XMP 能读回并重写） |
| `rgb8.png` → 无损 + `resampling=2` | `JXL`/`ftyp`/`jxll(0x0a)`/`jxlc(85B)`/`Exif(102B)` | **可解码但像素不等** `FAIL exact mismatch 4788/12288` | **高：名义无损实为降采样（JXL-01）** |
| `rgb8.png` → 无损 + `color_transform=YCbCr/XYB/None` | 与 `YCoCg` 相同的 box 序列 | 可解码；与 YCoCg **逐字节相同**（8bit 64×64） | 中：UI 参数名不副实（JXL-02） |
| 默认 16bit 无损（256×256 实测） | 同上 | `VERIFY OK` | 中：体积 70584B vs `None` 24907B（2.83×，JXL-02） |
| `lossless + codestream_level=5`（16bit） | **无产物**（编码失败） | — | 高：失败且无原因（JXL-06） |
| VarDCT `distance=0`（无 ICC / 有 ICC） | 无产物 / 正常容器 | — / 可读 | 高：失败或静默变无损（JXL-05） |

补充事实：所有产物均为容器形态（`JxlEncoderUseContainer(TRUE)`，`enc_jxl.cpp:277`），`jxlc` 码流首两字节 `ff0a`（裸码流签名）；CICP 与 ICC **不冲突**（无嵌入 ICC 时只有 nclx + OIIO 合成 ICC；`--color srgb` 时只有 lcms2 ICC、无 CICP —— 见 `t25` 的 oiiotool 输出）。

#### 5. 未覆盖与存疑

1. **独立解码器未验证**：本机没有 `djxl`/ImageMagick/exiftool；"可解码"仅由 libjxl 系两条路径（pp_verify 内部解码 + OIIO jxl 读图器，后者同样基于 libjxl）证明。`jxll=10` 对"只实现 level 5 的解码器"的拒收是**按 18181-2 语义 + libjxl 头文件措辞**推断（`encode.h:1373-1389`），未做第三方解码器实测 —— 该结论标 confirmed 的部分是"默认产物必带 jxll box 且载荷 0x0a"（实测），兼容性后果部分是推断。
2. **GUI 点击级未验证**（本次禁止 GUI/ui-smoke）："无损下 resampling 行仍可见可改"由代码判定（空谓词 ⇒ `eval_visible` 恒真，`params.cpp:419-423` + `format_tables.cpp:205` + `paramform.cpp:972-993`）与 CLI 等价路径实测支撑，未做真实点击。
3. **E>1 线程分支未覆盖**：全部实测 `--workers 1` ⇒ `encode_threads=1` ⇒ `JxlThreadParallelRunner` 未创建（`enc_jxl.cpp:251-264`）。
4. **8bit/16bit 无法由外部工具判别**：OIIO 一律读成 uint16；位深事实来自 `enc_jxl.cpp:299/303` 与产物体积差（无损 226B@8bit vs 238B@16bit）。未写 JXL 码流位解析。
5. **未测**：`dither8`（`--dev` 无开关）、alpha 的有损距离（未暴露 `JxlEncoderSetExtraChannelDistance`）、`output_mode`/`buffering`（未暴露）、超大图（>2^28 px）下的 level 选择、动画/多帧（本工具取首页）、CMYK 源（本工具禁 CMYK）。
6. **存疑（未查明机制）**：`modular_predictor=0` 与 `modular_lossy_palette=true` 在 256×256 图上产出**逐字节相同**（均 55384B，MD5 `5A75238840C8`），而基线为 35372B。两者语义无关，疑似落入库内同一条 fallback 路径；不影响"参数生效"的判定（确实改变了产物），但机制待查。
7. **正向观察（未列为发现）**：`--param` 未知键被 `cross_validate` 拒绝（实测 `--param lossy_palette=true` → `未知参数：lossy_palette`），拼写错误不会静默透传；`effort=11` 被 `validate_params` 拒（`value 11 out of range [1, 10]`）；无损时 `modular_lossy_palette` 被锁定为 false（快照实测）。这些是参数引擎正确的行为，记录以备对照。

##### 实测命令与产物（全部在 `build/encoder-audit/`）

```
# 通用形态（Windows / PowerShell；必须经 tools/env.py 转发 stdio）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev <输入> --out build/encoder-audit/jxl-rt/<tag> \
    --format jxl [--tech vardct|modular] [--lossless] [--bitdepth 8|16] [--color keep|srgb] \
    [--param key=value ...] --workers 1

# 像素级断言（root = 输入副本 + case/expected.json；golden_root = expected.json 上两级）
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/jxl-rt/v-rgb8/case/expected.json <产物>.jxl
# → "VERIFY jxl-lossless OK" / "FAIL exact mismatch: N/M samples differ"

# 容器结构（纯 Python）
python build/encoder-audit/jxl_box_parse.py "build/encoder-audit/jxl-rt/t13/*.jxl"
python build/encoder-audit/jxl_box_dump.py  "build/encoder-audit/jxl-rt/x2/jxl_exif.jxl"

# 第二读图器
python tools/env.py run -- vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe --info -v <产物>.jxl
```

| tag | 配置 | 结果 |
|---|---|---|
| t01/t02 | vardct effort 1 / 9 | 1069B / 772B（19ms / 58ms） |
| t03/t04 | vardct distance 0.5 / 3 | 1063B / 614B |
| t05 | vardct distance 0 | **失败** `distance 0.000000 rejected: ERROR` |
| t06 | lossless（默认 16bit） | 238B，`VERIFY OK` |
| t07 | lossless + resampling=2 | 228B，**`VERIFY FAIL`**（4788/12288 不同） |
| t08/t09/t10/t11 | lossless + color_transform YCbCr/None/XYB/YCoCg | 全部 238B、MD5 相同 |
| t12/t13/t14 | vardct level 5 / 10 / auto | 979B 无 jxll / 988B jxll=0a / 979B 无 jxll |
| t15/t16 | lossless level5 16bit / 8bit | **失败**（`ProcessOutput failed: ERROR`）/ 226B `VERIFY OK` |
| t17 | modular ma_tree=50 | **失败** `option ... rejected: ERROR` |
| t18/t19 | modular brotli_effort 0 / 11 | 235B（MD5 相同） |
| t20 | modular 默认（未勾无损） | 238B，`VERIFY OK`（= t06 字节） |
| t21–t24 | modular distance=1 + color_transform 四值 | 全同 235B |
| t25/t26 | vardct distance 0 / 1 + `--color srgb` | 807B 成功 / 1538B |
| t27/t28 | lossless + `--color srgb` / modular distance0 + `--color srgb` | 475B / 475B |
| b01–b04 | 256×256 无损 8bit + color_transform 四值 | 70046B(YCoCg) vs 24254B（其余三者 MD5 相同） |
| b08–b10 | 256×256 无损 8bit 默认 / 16bit 默认 / 16bit None | 70046B / 70584B / 24907B |
| c01–c09 | vardct 8bit vs 16bit、epf、gaborish、progressive_ac、keep_invisible、resampling、distance=12 | 见 §2 |
| d01–d10 / e-* | modular 各参数逐个开关 + 失败面确认 | 见 §2、JXL-03 |
| f-* | rgba8 + distance=1 + keep_invisible/dots/patches | 1041/1041/1041/(false)331 |
| m-* / x1 / x2 | 六种通道×位深无损（均 `VERIFY OK`）+ EXIF/XMP box 往返 | 见 §4 |

### 4.3 PNG（OIIO / Deflate）

> 只读审计，基线 0.3.5.alpha2 / `build/release-dev`（OpenImageIO **3.1.17.0**、libpng 1.6.58、zlib `Z_BEST_COMPRESSION=9`）。
> 所有实测命令均经 `python tools/env.py run -- build/release-dev/photopipeline.exe --dev ... --workers 1`；产物与取证脚本在 `build/encoder-audit/probe/`（gitignored）。
> 本文引用的库行号对应 `vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/png.imageio/`（若为 3.1.14 行号会显式标注）。

---

#### 1. 结论

1. **参数面只有 1 个（`compressionLevel`），且端到端真实生效**：UI 核心行 → `spec.params` → `enc_oiio.cpp:241-248` → OIIO `png:compressionLevel`（`pngoutput.cpp:164-169`）→ zlib。实测 0/6/9 三档的 zlib 头 FLEVEL 分别为 0/2/3（`78 01`/`78 9c`/`78 da`），文件体积 443,653 / 173,351 / 169,096 B（384×384 RGB8），**三档像素逐位相同**。deep-review 的 R-010（裸键名导致参数无效）确已修复，本审计未复现。
2. **但“全参数暴露”远未达成**：OIIO PNG writer 实际读取 13 类属性，本编码器只接 1 类；其中 `compression`（zlib 策略：filtered/huffman/rle/fixed/pngfast/none）与 `png:filter`（滤波掩码）是**真实内容面**，且 `docs/param-catalog.md:214` 自己就留着「⚠️ 以 OIIO PNG 输出属性文档核对补全」的待办。其余（`oiio:ColorSpace`、`ResolutionUnit/X/Y`、`DateTime`、`CICP`、`oiio:UnassociatedAlpha`）不是参数，而是**色彩/元数据/alpha 语义的缺口**（见 P0/P2）。
3. **P0：RGBA 半透明像素被静默改写**。OIIO PNG 读端默认把未关联 alpha **关联**（`pnginput.cpp:226-227,367-375`）——8 位文件在关联时就被量化到 8 位；本编码器把这份**已乘 alpha** 的缓冲量化到目标位深（`enc_oiio.cpp:341-351`）后交给 OIIO，写端又**反关联**（`pngoutput.cpp:240-241,361-367,317-331`，除以 alpha）。结果：alpha=0 的 RGB 全丢，alpha=1..4 的 RGB 严重跑飞（200,100,50 → 255,0,0 / 255,128,0 / 170,85,85 / 191,128,64），alpha=128 也有 ±1。**无任何告警**，而且 `pp_verify` 的 exact 断言读端同样走 OIIO 关联 → **结构性看不见**（金样 rgba 夹具 alpha 只有 0/255，正好是关联恒等点）。
4. **P2：色彩标记自相矛盾**。编码器从不设 `oiio:ColorSpace`，OIIO 取默认 `srgb_rec709_scene` → **每个 PNG 都写 sRGB+gAMA+cHRM**；有 ICC 时再写 iCCP，于是出现「iCCP 声明 Display P3 / AdobeRGB，同时 sRGB 块声明 sRGB」的产物（默认 `color=keep` + 源带非 sRGB ICC 即可复现）。PNG 3rd ed 的色彩块优先级表（cICP 1 > iCCP 2 > sRGB 3 > cHRM+gAMA 4）能救规范感知解码器，但旧解码器/非颜色管理工具会采信 sRGB → 偏色。
5. **P2：8 位源默认输出 16 位**（`default_bitdepth_for("png")=16`，`pipeline.cpp:1718-1734`）是共识文档的既定口径（`docs/brainstorm-consensus.md:68`），但 8 位源上每个样本精确等于 `v*257`（**零信息增益**），体积 +18.9%（lv6）/ +99.9%（lv0），且 OIIO PNG writer 完全不写 `sBIT`，下游无法看出“实质 8 位”。
6. **P2：灰度输出可携带 RGB ICC**（灰度源 + 源里带 sRGB iCCP → 输出 IHDR color type 0 而 iCCP 声明 `RGB `），色彩类型与 profile 不匹配且无告警；这正是 R-032 的写端镜像（R-032 只处理了读端 lcms2 拒绝）。
7. **P2：PNG 原生 ancillary 元数据整类不进 `MetadataPlan`**（`metadata.h:82-88` 只有 exif/xmp）——实测源 PNG 的 `pHYs`(2835 px/m)、`tIME`、`tEXt(Software)` 在重编码后全部消失；OIIO 本身支持 pHYs/tIME 写入（`png_pvt.h:725-762,535-551`），只是编码器没把 `ImageSpec` 的对应属性填进去。
8. **P3 两组打磨项**：恒写 14 字节空 `eXIf`（`png_pvt.h:783-788` 无条件 `png_set_eXIf_1`）与 `oFFs(0,0)`；「无损编码」复选框对 PNG 恒显示且**编码器从不读取**（默认还是未勾选）；tooltip 的 `png_pvt.h:615-617` 是 3.1.14 行号（3.1.17 为 626-628）。

---

#### 2. 参数全量矩阵

##### 2.1 生产参数面（schema + 格式级）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `compressionLevel` | 是（核心行 `pp-param-compressionLevel`，spin，lo=0 hi=9 step=1，行内注「范围 0–9」） | 6 | Int `0..9`（= zlib `Z_NO_COMPRESSION..Z_BEST_COMPRESSION`，`zlib.h:194-196`） | `src/codecs/enc_oiio.cpp:241-248`（`param_int` → `std::clamp(0,9)` → `spec.attribute("png:compressionLevel")`）；库消费 `pngoutput.cpp:164-169` | **是**（实测 FLEVEL 0/2/3；0/6/9 体积 443,653/173,351/169,096 B；像素逐位一致） | 无 `visible`/`locked` 谓词 → `apply_locks` 不改写（`params.cpp:479-519`）；`validate_params` 越界即拒（实测 `12`/`-1` → `param 'compressionLevel': value 12 out of range [0, 9]`，`params.cpp:534-542`）；编码器内 clamp 是二道防线（`enc_oiio.cpp:242-245`，UI/CLI 不可达） |
| `out_bitdepth`（格式级，非 ParamDef） | 是（位深行 `pp-bitdepth-combo`；选项 = `FormatDef.bitdepths` = {8,16}，默认 16） | 16 | 8 / 16 | `src/ui/page_output.cpp:731` → `pipeline.cpp:490` → `enc_oiio.cpp:211-218`（非 8/16 → 明确报错，不静默降级） | **是**（IHDR bd=8/16；16 位样本大端序正确；8 位源→16 位输出逐样本 = `v*257`） | 默认值单源 `default_bitdepth_for`（`pipeline.cpp:1718-1734`，UI 侧 `page_output.cpp:641`）；16→8 触发 `DepthDowngrade` 告警（`pipeline.cpp:1042-1047`）；8→16 **无任何提示、无 sBIT**（见 PNG-04） |
| `__lossless`（内部管道键） | 是（「无损编码」复选框 `pp-lossless-check`，PNG 恒显示；不进 `values()`） | false | bool | 写：`page_output.cpp:730`、`paramform.cpp:912`；读：**PNG 编码器不读**（`enc_oiio.cpp` 全文无 `lossless`/`__lossless`） | **否** | PNG 唯一技术 `deflate` 声明 `lossless_capable=true`（`format_tables.cpp:606`）→ `first_lossless_tech_in` 非空 → 复选框可见（`paramform.cpp:599-602`）；勾选前后产物逐字节相同（见 PNG-08） |
| `lossless`（显式 schema 参数） | 否（PNG 技术未声明；`paramform.cpp:797` 跳过建行） | — | — | — | 否（N/A） | `declares_lossless=false`（`params.cpp:318` 注释逐字写了「png/tiff/bmp」）；预设落盘走 `presets` 的 `outputs[i].lossless`（`preset_io.cpp:451-458`） |
| backend `oiio` / tech `deflate` | 否（单后端 + 单技术 → 两行都隐藏，`paramform.cpp:575-587`） | `oiio` / `deflate` | 各 1 项 | `find_backend`/`find_tech` 空 id → 首项（`params.cpp:350-366`） | 是（隐式） | 无切换路径 → 无“切换后旧值沿用”风险；`--backend/--tech` 也只有这两个值 |

##### 2.2 库面存在、但 schema/编码器未接（“全参数暴露”缺口）

| key（OIIO 属性） | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `compression`（zlib **策略**） | 否 | 空串 → `Z_DEFAULT_STRATEGY` | `default` / `filtered` / `huffman` / `rle` / `fixed` / `pngfast`(=level 1) / `none`(=level 0) | `pngoutput.cpp:170-191` | 否 | 与 `compressionLevel` **同名不同义**（TIFF 用同名属性选压缩方案）；对照片无害，对图形/图标 `rle` 有量级差异（实测同图 Sub 滤波下策略 RLE 247,360 B vs 默认 162,986 B） |
| `png:filter` | 否 | `PNG_NO_FILTERS`(0)（libpng 对 ≥8 位非调色板图实际展开为 `PNG_ALL_FILTERS`，`pngwutil.c:841-849`） | 0 / 8(NONE) / 16(SUB) / 32(UP) / 64(AVG) / 128(PAETH) / 248(ALL)（`png.h:1529-1536`） | `pngoutput.cpp:199-200` | 否 | 实测产物每行 filter ∈ {1,3}（自适应生效）；强制 SUB 可再省 5.7%（162,986 vs 172,922 B） |
| `oiio:ColorSpace` | 否 | `"srgb_rec709_scene"` | colorconfig 色彩空间名 | `png_pvt.h:638-695` | 否（取默认） | 默认值直接导致恒写 sRGB+gAMA+cHRM（PNG-02）；设置它也是唯一能抑制自动色彩块的通路 |
| `oiio:UnassociatedAlpha` | 否 | 0 | int | `pngoutput.cpp:240-241`（写）、`png_pvt.h:634-636` | 否（取默认） | 缺失 = 写端反关联（PNG-01 的写侧根因） |
| `ResolutionUnit` / `XResolution` / `YResolution` / `PixelAspectRatio` | 否 | 无 | string / float | `png_pvt.h:725-762`（→ `png_set_pHYs`） | 否（从不设置） | 输出永无 `pHYs`（PNG-06） |
| `DateTime` | 否 | 无 | string | `png_pvt.h:535-551`（→ `png_set_tIME`） | 否（从不设置） | 输出永无 `tIME`（PNG-06） |
| `CICP`（int[4]） | 否 | 无 | 4×int | `png_pvt.h:764-780` | 否 | 本项目无 HDR/CICP 语义；且 `wrote_colorspace=true` 时连自动推导也被短路 |
| `ICCProfile`（元数据注入，非用户参数） | 否（由色彩页目标决定） | 源 ICC（keep）/ 目标 ICC | bytes | `enc_oiio.cpp:300-306` | **条件**（有 ICC 才写） | 与 sRGB 块并存（PNG-02）；灰度图可带 RGB profile（PNG-05） |
| `oiio:dither` | 否 | 0 | int | `pngoutput.cpp:236-238` | 否（N/A） | 本编码器恒交 `UINT8` 且与 spec.format 一致 → OIIO 无类型转换 → 该 dither 不参与；8bit 抖动由 `pp::simd::quantize8_dithered` 承担（`enc_oiio.cpp:342-348`） |
| `png:linear_premult` | 否 | 0（全局属性） | int | `pngoutput.cpp:195-197` | 否（N/A） | 只在“线性空间做关联/反关联”时生效；本项目未启用，开关它反而会加重 PNG-01 |

---

#### 3. 发现

##### PNG-01 [P0] RGBA 半透明像素被静默改写：读端关联 + 写端反关联，全透明处 RGB 归零

- **证据**
  - 读端默认关联：`vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/png.imageio/pnginput.cpp:226-227`（`if (config.get_int_attribute("oiio:UnassociatedAlpha", 0) == 1) m_keep_unassociated_alpha = true;`）与 `:367-375`（`if (m_spec.alpha_channel != -1 && !m_keep_unassociated_alpha) associateAlpha(...)`）；关联算子在 `:297-310` 的 else 分支 `val[c] = val[c] * alpha;`，`DataArrayProxy<T,float>` 把结果**写回原生类型** ⇒ 8 位文件在关联这一步就量化到 8 位。
  - 解码器未传该配置：`src/decode/oiio_reader.cpp:234` `out.buf.read(0, 0, 0, spec.nchannels, true, OIIO::TypeDesc::FLOAT)`（无 `oiio:UnassociatedAlpha`，`ImageBuf` 也未带 config）。
  - 编码器在**已乘 alpha** 的缓冲上先量化：`src/codecs/enc_oiio.cpp:341-351`（`quantize8(...)` / `quantize16(...)` 写进 `pixels`），随后 `:379-382` 以 `UINT8/UINT16` 交给 OIIO。
  - 写端反关联：`pngoutput.cpp:240-241` `m_convert_alpha = m_spec.alpha_channel != -1 && !m_spec.get_int_attribute("oiio:UnassociatedAlpha", 0);`（我们的 spec 只设 `alpha_channel`，见 `enc_oiio.cpp:222-237`）→ `:361-367` 调 `deassociateAlpha`，`:317-331` 逐像素 `val[c] = data[c] / alpha`（`alpha != 0 && alpha != 1` 才做）。
  - 实测（8 位 RGBA 源，RGB 恒 `(200,100,50)`、alpha 0..255 斜坡，256×2，`--bitdepth 8`）：

    | alpha | 输入 RGB | 输出 RGB |
    |---|---|---|
    | 0 | (200,100,50) | **(0,0,0)** |
    | 1 | (200,100,50) | **(255,0,0)** |
    | 2 | (200,100,50) | **(255,128,0)** |
    | 3 | (200,100,50) | **(170,85,85)** |
    | 4 | (200,100,50) | **(191,128,64)** |
    | 16 | (200,100,50) | (207,96,48) |
    | 64 | (200,100,50) | (199,100,52) |
    | 128 | (200,100,50) | (199,100,50) |
    | 255 | (200,100,50) | (200,100,50) |

    16 位源同样命中：alpha=257（≈1/255）时 G `30000 → 30090`（+90/65535）；alpha=0 时 RGB 全 0。
  - 同源跨格式后果（同一 premultiplied 缓冲 + 同一 `flatten`）：`src/core/pipeline.cpp:1006-1035` 在 alpha-dropping 目标前调用 `flatten_alpha_in_place`（`:353-375`），其契约是**未关联** alpha 的合成式 `dst = src*a + bg*(1-a)`（`src/core/simd/simd.h:63-75`，公式在 `:71`）；缓冲已乘过一次 ⇒ **二次相乘**。实测 `alpha_src.png`（8×8，含 alpha=128/64/0 像素）→ BMP24：alpha=128 的 (100,50,25) 合成到白底得 **(152,140,134)**，正确值应为 (177,155,140)（二次相乘预测 152,141,132 —— 逐通道吻合）。
  - 门禁失明：`tools/pp_verify.cpp:77` 的 `load_oiio` 在 `:99` 用 `ImageInput::read_image(..., TypeDesc::FLOAT, ...)` 读**两侧**，输入侧与输出侧的关联都发生在 8 位原生类型上并互相抵消 ⇒ 对上述 766 个差异样本仍报 `VERIFY ... OK`（实测）；金样夹具 `rgba8.png`/`rgba16.png` 的 alpha 只有 {0,255}（关联恒等点），本身也测不出。
- **事实**：PNG（以及 TIFF/WebP，见 `tiffinput.cpp:903,1367,1394`、`webpinput.cpp:254-257`）解码 → OIIO 在**文件原生位深**上做 premultiply（8 位源即 8 位量化）→ 管线按 float32 携带这份 premultiplied 数据 → PNG 编码先量化到目标位深 → OIIO 写端再除以 alpha。误差被 `1/alpha` 放大，`alpha=0` 时乘出来的 RGB 已是 0、除法被跳过，于是**永久丢失**。
- **影响**：任何带半透明像素的 RGBA 源（抗锯齿边缘、投影、UI 素材、游戏贴图）转 PNG 后低 alpha 区 RGB 被改写：边缘在半透明底上会偏色/产生色晕，全透明区承载的 RGB 数据被清零且不可恢复。文件仍是合法 PNG（52 个产物 CRC/IDAT 全通过），所以**无声无息**。按本仓口径「P0 = 数据损坏 / 静默把有损当无损」应判 P0；若只看可见影响可降为 P1 —— 但 `alpha=0` 的 RGB 丢失不可逆，且 `--bitdepth 16` 也救不回来（读端已按源位深量化）。
- **建议**：把管线改成**直线 alpha 端到端**（一次改动同时修好 PNG 回环、`flatten` 与 JXL/WebP 的 `alpha_premultiplied=false` 声明不一致）：
  1. `src/decode/oiio_reader.cpp` 解 pixel 时用带 config 的 `ImageBuf::reset(file, 0, 0, nullptr, &config)`（`imagebuf.h:336-339`，第 5 参即 `const ImageSpec* config`）并 `config.attribute("oiio:UnassociatedAlpha", 1)`；
  2. `src/codecs/enc_oiio.cpp` 在 `ImageSpec` 上写 `spec.attribute("oiio:UnassociatedAlpha", 1)`（`pngoutput.cpp:240-241` 据此跳过反关联）；
  3. 复核 `flatten_alpha_in_place`/`simd::flatten` 的输入语义（改为未关联后即与公式一致，无需再改公式），并跑金样 20 对 + 回归；
  4. 补两条断言：① 半透明夹具（alpha∈{0,1,2,64,128}）+ **原生字节级**比较（pp_verify 建议提供 `oiio:UnassociatedAlpha=1` 的读取模式，否则永远看不见）；② 混合 alpha 源 → JPEG/BMP 的合成值断言（现 `alpha-jpeg` 只用 0/255 夹具）。

##### PNG-02 [P2] iCCP 与 sRGB/gAMA/cHRM 同时写入：非 sRGB ICC 时色彩标记自相矛盾

- **证据**
  - 编码器只写 ICC：`src/codecs/enc_oiio.cpp:300-306`（`spec.attribute("ICCProfile", ...)`），全文无 `oiio:ColorSpace`。
  - OIIO 取默认色彩空间：`png_pvt.h:638-639` `string_view colorspace = spec.get_string_attribute("oiio:ColorSpace", "srgb_rec709_scene");`；随后 `:689-695` `png_set_sRGB_gAMA_and_cHRM(sp, ip, PNG_sRGB_INTENT_ABSOLUTE)`；ICC 在同一函数更后面才写（`:697-710`），**不会**撤销已设置的 sRGB/gAMA/cHRM。
  - 实测（默认 `color=keep`，源 PNG 自带 Display P3 iCCP）：输出块 = `IHDR, iCCP(375B), sRGB(1B, intent=3), gAMA(45455), cHRM(sRGB 基色), eXIf(14B), oFFs, IDAT, IEND`；iCCP 解析出 `colorspace='RGB '`、`acsp` 有效。`--color p3` / `--color adobergb` 生成的输出同样是 `iCCP + sRGB + gAMA + cHRM`（iCCP 分别 580/576 B）。
  - 规范：PNG 3rd ed「Table 1 Color Chunk Priority」= cICP 1 > iCCP 2 > sRGB 3 > cHRM+gAMA 4，且「a single image contains more than one of these chunk types, the chunk with the lowest Priority number should take precedence and any higher-numbered chunk types should be ignored」（<https://www.w3.org/TR/png-3/>）。
- **事实**：PNG 输出恒带 sRGB 块（+ gAMA 0.45455 + sRGB cHRM）；只要嵌入的 ICC 不是 sRGB（P3/AdobeRGB/源 ICC），文件里就同时存在两套互斥的色彩声明。规范感知的解码器按优先级取 iCCP（正确），但旧解码器/简单查看器若采信 sRGB 块，会把 P3 数据当 sRGB 显示（明显过饱和）。
- **影响**：宽色域工作流的产物在部分工具里偏色；exiftool 类工具会同时报两个色彩块，用户无法判断哪个是准的。ICC 本身就是 sRGB 时（`--color srgb`）两块一致，只是冗余。
- **建议**：`enc_oiio.cpp` 的 PNG 分支按「有无 ICC」分派：有 ICC 时把 `oiio:ColorSpace` 设为一个 **不命中 colorconfig 任何分支** 的名字（使 `wrote_colorspace` 保持 false、gAMA/cHRM/sRGB 全部不写），让 iCCP 成为唯一色彩声明；无 ICC 时保留默认（当前行为就是正确的 sRGB 标记）。同时补块级断言（iCCP 存在时断言无 sRGB/gAMA/cHRM），避免回归。

##### PNG-03 [P2] “全参数暴露”缺口：`compression` 策略与 `png:filter` 未进 schema（其余 6 类库属性同样未接）

- **证据**
  - schema 只有一行：`src/core/format_tables.cpp:607-616`（`compressionLevel`，`advanced=false`、`group=kGroupCore`），`:618-620` 只有 bitdepths/alpha/gray/meta_path。
  - 库面消费点齐全：`pngoutput.cpp:170-191`（`compression` 策略，含 `pngfast`→`Z_BEST_SPEED`、`none`→level 0 的**覆盖**行为）、`:199-200`（`png:filter`）、`:195-197`（`png:linear_premult`）、`:236-238`（`oiio:dither`）。
  - 文档自认待办：`docs/param-catalog.md:214`「⚠️ 以 OIIO PNG 输出属性文档核对补全（R5 口径：OIIO 即库面）」。
  - 用户无法绕过：`--param png:filter=248` 实测直接被拒（`cross_validate` 规则③）——`error: 未知参数：png:filter`，`src/core/params.cpp:592-610`；因此“未暴露”= 彻底不可达，而不是“能传但没人读”。
  - 量级实测（384×384 RGB8，level 6，IDAT）：自适应滤波 172,922 B；强制 SUB 162,986 B（−5.7%）；同数据 RLE 策略 247,360 B / HUFFMAN_ONLY 250,918 B（策略对图形类内容可反向放大，故不应设默认值，但应可配）。
- **事实**：OIIO PNG writer 的真实属性面 = `png:compressionLevel` / `compression` / `png:filter` / `png:linear_premult` / `oiio:dither` / `oiio:UnassociatedAlpha` / `oiio:ColorSpace` / `ICCProfile` / `CICP` / `ResolutionUnit·X·Y·PixelAspectRatio` / `DateTime` / 任意 string→tEXt / Exif→eXIf；本项目接 1 项 + ICC 1 项。核心原则「全参数暴露（所用库的全部参数进入 GUI）」在本格式上完成度最低。
- **影响**：想要“更小体积/更快编码”的用户只有 0–9 一维旋钮；图形类 PNG（图标、截图、UI）无法选 `rle`/`filtered` 策略，无法强制滤波类型；也没有任何“无损/体积”取舍的第二维。
- **建议**：在 `format_tables.cpp` 的 png 技术下补两条 `ParamDef`（`advanced=true`，`group=kGroupEncodeTools`）：`compression_strategy`（Enum：`default/filtered/huffman/rle/fixed/pngfast/none` → 写 `spec.attribute("compression", ...)`）与 `png_filter`（Enum：`adaptive(248)/none(8)/sub(16)/up(32)/avg(64)/paeth(128)` → 写 `spec.attribute("png:filter", int)`）；注意 `compression` 与 TIFF 同名不同义，`enc_oiio.cpp` 的 PNG 分支必须只在 `format_id_=="png"` 时写它（TIFF 分支已在 `:249-296` 独占同名键），并在 tooltip 里写明 `pngfast`/`none` 会**覆盖** compressionLevel；同步 `docs/param-catalog.md` 的 ⚠️ 段落。

##### PNG-04 [P2] 8 位源默认输出 16 位：体积 +19%~+100%、零信息增益、无 sBIT、UI 无提示

- **证据**
  - 默认值单源：`src/core/pipeline.cpp:1723-1724` `if (format_id == "png") return 16;`；UI 侧 `src/ui/page_output.cpp:641` 取同源（`set_bitdepth_options` → `default_bitdepth_for`），选项集 `page_output_impl.h:107-109`（PNG 非 libheif → 直接 `f.bitdepths` = {8,16}）。
  - 口径来源：`docs/brainstorm-consensus.md:68`「输出位深是显式控件，默认值 = 高质量档（PNG/TIFF/JXL 默认 16 位…）」——即 16 位默认**是既定设计**，本发现只针对“8 位源上的代价与提示”。
  - 实测（384×384 RGB8 源）：8 位输出 173,351 B（lv6）/ 443,653 B（lv0）；16 位输出 206,061 B（+18.9%）/ 886,729 B（+99.9%）。16 位产物**每个样本恰为 `v*257`**（逐样本断言为真）⇒ 相对 8 位零信息增益；8→16 路径也没有任何提示（告警只在 `src > out` 时触发，`pipeline.cpp:1042-1047`）。
  - 无 sBIT：OIIO PNG writer 完全没有 sBIT 写入路径（`png_pvt.h` 只在 `:394` 留了一行注释掉的 `png_get_sBIT`）；PNG 1.0 §4.2.6 明确建议「encoders emit an sBIT chunk if it has converted the data from a lower sample depth」。
  - UI 也没有“源是 8 位/16 位”的信息：位深行注释位只在**只有 1 个选项**时才写字（`page_output.cpp:646-653`），PNG 有 2 个选项 → 注释位恒空。
- **事实**：默认 16 位在 8 位源上把样本字节翻倍、体积 +19%（默认 lv6）到 +100%（lv0），且产物对外宣称 16 bps 却无 sBIT 提示实际精度。
- **影响**：批量把 8 位截图/网页图转 PNG 的用户，默认得到约 1.2–2 倍体积且毫无收益；下游工具会把产物当 16 位高比特素材（有的处理链会据此选更重的算法）。
- **建议**：**不改默认**（既定共识），补两处信息面：① 位深行在“批次源位深统一且 < 所选位深”时给 10px 注释（如「源为 8 位，16 位输出体积 +约 20%，无额外精度」）；UI 目前只聚合了 alpha（`PageOutput::set_batch_has_alpha`，`mainwindow.cpp:2011`），源位深需要照同一形态补一个批量聚合；② OIIO 不支持 sBIT → 若要写，需自建 4/6 字节 `sBIT` 块（PNG 1.0 §4.2.6 允许，位置须在 IDAT 前），可放在 `metadata.cpp` 的 PNG 后写路径里裸块注入。若两者都不做，至少应在 `docs/param-catalog.md` 的 PNG 段记录这条已知取舍。

##### PNG-05 [P2] 灰度 PNG 输出可携带 RGB ICC（色彩类型与 profile 不匹配），无告警

- **证据**
  - keep 路径直接沿用源 ICC：`src/core/pipeline.cpp:883` `ctx.icc_to_embed = ctx.src_icc;`（`color=keep` 分支不校验 ICC 与通道数的匹配），`:973` 注入 `ctx.meta.icc_profile`。
  - 编码器原样写入：`enc_oiio.cpp:300-306`（不检查 `nch` 与 ICC 的 colorspace），`nch==1` 时 `spec.channelnames={"Y"}`（`:222-225`）→ OIIO 写 color type 0。
  - OIIO 写 iCCP 不校验色彩空间：`png_pvt.h:697-710` 仅按字节长度写 `png_set_iCCP`。
  - 实测：灰度 8 位源 + sRGB iCCP → 输出 `IHDR colortype=0, nchannels=1` 且 `iCCP(379B)` 解析出 `colorspace='RGB '`；整条链路 **0 warning**（`SUMMARY` 的 warnings 列为空）。把该产物再喂回管线并 `--color srgb` 才出现 `NoIccAssumeSrgb,GrayToRgbEncoded` 两条告警（读端 R-032 降级路径）。
- **事实**：写端不做「ICC 色彩空间 vs 图像通道」一致性校验，灰度产物能带 RGB profile；这是 R-032（读端 lcms2 拒绝 gray+RGB ICC）的镜像缺口，但写端连告警都没有。
- **影响**：色彩管理链读到这种文件会走“gray 数据 + RGB profile”的非常规组合（lcms2 需要 gray→RGB 兜底；部分工具直接忽略 profile 或用错）；用户以为“色彩随 ICC 保住了”，实际是矛盾标记。
- **建议**：`enc_oiio.cpp` 的 ICC 注入处（`:300-306`）加一致性校验：读 ICC header 的 `data colour space`（offset 16，4 字节）与 `nch` 比对（1 通道要求 `GRAY`，3/4 通道要求 `RGB `），不匹配时二选一 —— 优先按 `colormanager` 的既有降级口径升 RGB 并复用 `WarningKind::GrayToRgbEncoded`，或丢弃 ICC 并出一等 Warning（写路径只 log_info 不 warn 的教训见 R-171）；无论如何不能静默写矛盾块。

##### PNG-06 [P2] PNG 原生 ancillary 元数据（pHYs/tIME/tEXt）整类不进 plan：重编码后静默丢失

- **证据**
  - `MetadataPlan` 只有 EXIF/XMP：`src/core/metadata.h:82-88`（`Exiv2::ExifData exif; Exiv2::XmpData xmp;`），全仓 `metadata.cpp` 无 `Resolution/pHYs/DPI/tIME` 任何字样（grep 0 命中）。
  - 编码器不填对应 `ImageSpec` 属性：`enc_oiio.cpp:299-306` 只写 `ICCProfile`，没有 `ResolutionUnit/XResolution/YResolution/DateTime`。
  - OIIO 本可写：`png_pvt.h:725-762`（有 Resolution 属性就 `png_set_pHYs`）、`:535-551`（有 `DateTime` 就 `png_set_tIME`）、`:593-603`（任意 string 属性 → tEXt）。
  - 实测：源 `with_phys.png`（`pHYs=2835,2835,unit=1`、`tIME=2024-01-02 03:04:05`）→ 输出块 = `IHDR, sRGB, gAMA, cHRM, eXIf(14), oFFs, IDAT, IEND`：**pHYs/tIME 双双消失**；`tEXt("Software")`（金样夹具每个 PNG 都有）在重编码后也消失。
- **事实**：重编码路径的元数据面只有 EXIF/XMP；PNG 的原生物理尺寸/时间戳/文本块既不在 plan 里，也没被编码器透传，所以必然丢。仅元数据模式不受影响（整文件由 Exiv2 原地改写，实测 IDAT 逐字节一致）。
- **影响**：DPI/像素物理尺寸（打印、排版、Word/InDesign 置入）与 tIME（“最后修改时间”）在转码后丢失，且无 `MetadataDropped` 警告；用户只能在打印时发现尺寸变成 72dpi 默认。
- **建议**：在 `MetadataPlan` **末尾加性**追加分辨率与时间字段（`std::optional<Resolution> resolution` + `std::optional<std::string> png_time`），probe 从 OIIO spec（`spec.get_float_attribute("XResolution")` 等）取值，`enc_oiio` 写进 `ImageSpec`；tEXt（除 XMP 外的任意文本块）按“是否在 EXIF/XMP 面之外”决定透传或显式告警。这条同时修好 TIFF/WebP 的 DPI 面，属跨格式收益。

##### PNG-07 [P3] 恒写 14 字节空 eXIf 与 oFFs(0,0)，且空 eXIf 的留存与否取决于是否发生 Exiv2 重写

- **证据**
  - `png_pvt.h:783-788`：`encode_exif(spec, exifBlob, endian::big); png_set_eXIf_1(sp, ip, exifBlob.size(), ...)` —— **无条件**调用；空 ExifData 也会产出 14 字节最小 TIFF 头。
  - `png_pvt.h:630-632`：`png_set_oFFs(sp, ip, spec.x, spec.y, PNG_OFFSET_PIXEL)` —— 恒写，我们的 `spec.x/y = 0` → `oFFs(0,0)`。
  - 实测：无 EXIF 的源（`alpha_src.png`/`with_phys.png`）→ 输出含 `eXIf(14B)`，字节 `4d4d002a00000008000000000000`（IFD0 0 条目）；而发生了 Exiv2 重写的产物（`with_xmp.png`）**没有** eXIf —— 因为 `metadata.cpp:1023-1025` 在 `exif/xmp` 皆空时直接 return，不重写；反之 Exiv2 重写会按自身 EXIF 结构重发（无 EXIF 则不写）。
- **事实**：同一个编码器在不同元数据组合下产出“有空 eXIf”或“无 eXIf”两种结构，属可观察的不一致；空 eXIf 语义上是合法的空 IFD，但纯属噪声（14B + 让 exiftool 报“Exif 0 tags”）。
- **影响**：产物体积/结构噪声、元数据工具里出现空的 EXIF 段；不影响解码。`oFFs(0,0)` 同理（合法但冗余，部分校验器会提示）。
- **建议**：OIIO 无开关可关 ⇒ 在 `enc_oiio` 写盘后按需清理（读块表删掉空 eXIf / `oFFs` 两枚 ancillary 块后重写 CRC），或统一在 `metadata.cpp` 的 PNG 后写路径里做“空 eXIf 归一”；同时给 `docs/param-catalog.md` 记一条“PNG 恒有无害 ancillary 块”的说明。

##### PNG-08 [P3] 「无损编码」复选框对 PNG 恒显示、勾了没有任何效果、默认还是未勾选

- **证据**
  - 可见性判据是“该后端存在 lossless_capable 技术”：`src/ui/paramform.cpp:599-602`（`has_lossless = first_lossless_tech_in(b) != nullptr` → `setVisible`）；PNG 的 `deflate` 声明 `lossless_capable = true`（`format_tables.cpp:606`）⇒ 复选框恒显示。
  - 编码器不读无损：`src/codecs/enc_oiio.cpp` 全文无 `lossless` / `__lossless`；`apply_locks` 对 PNG 只同步内部键（`params.cpp:495-507`，因 `tech_declares_lossless(deflate)==false` 不写显式键）。
  - 实测：同一源 `--lossless` 与不带 `--lossless` 的产物字节数完全一致，`SUMMARY` 行只有 `lossless=true/false` 的文案差异。
  - 默认值：`paramform.cpp:424` `sel.lossless = false;` ⇒ 界面呈现“未勾选无损”，而 PNG 输出**恒无损**（`format_tables.cpp:606` + `pngoutput.cpp` 全链路无有损路径）。
- **事实**：控件语义与产物能力脱节 —— 一个恒真的属性被做成了可勾选且默认关闭的开关。
- **影响**：用户可能误以为“没勾 = PNG 会有损”，或反复勾选寻找体积变化；预设里也会落一个恒 false 的 `lossless` 字段（`preset_io.cpp:451-458`）。
- **建议**：对“技术恒无损且未声明显式参数”的格式（png/tiff/bmp）把复选框改为**只读勾选态 + 禁用 + tooltip 说明**（例如「PNG/Deflate 恒为无损，此开关仅对支持有损技术的格式生效」）；判据可用 `!tech_declares_lossless(t) && t->lossless_capable` 收在 paramform 的一处。若保留可勾选，则至少让 `enc_oiio` 读一次 `lossless_flag` 并在为 false 时打一条 info（当前是彻底不读）。

##### PNG-09 [P3] 参数 tooltip 的证据行号漂移：`png_pvt.h:615-617` 属 OIIO 3.1.14，实装 3.1.17 应为 626-628

- **证据**
  - `src/core/format_tables.cpp:611-615` 的 tooltip 同时引用两个版本的行号：`src/png.imageio/pngoutput.cpp:164-169`（**3.1.17** 正确）与 `png_pvt.h:615-617`（**3.1.14** 的 `png_set_IHDR(...PNG_INTERLACE_NONE...)` 在 `v3.1.14.0-21a4965ff8.clean/.../png_pvt.h:615-617`）；3.1.17 同一语句在 `png_pvt.h:626-628`。
  - 实装版本：`vcpkg_installed/x64-windows-avx2/include/OpenImageIO/oiioversion.h:45-47` = 3.1.17，`build/release-dev/OpenImageIO.dll` FileVersion = 3.1.17.0。
  - 结论本身正确：该插件确实**没有** interlace 属性、固定 `PNG_INTERLACE_NONE`（3.1.17 `png_pvt.h:626-628`；`pngoutput.cpp`/`png_pvt.h` 全文无 interlace 属性读取）。
- **事实**：deep-review R-010 已指出“实装 3.1.17 vs 注释自称 3.1.14”的版本漂移（`docs/deep-review-0.3.2.md:129`），`enc_oiio.cpp:4-5` 已订正，但格式表 tooltip 的这半句仍是 3.1.14 行号。
- **影响**：审计/维护者按图索骥会读到无关代码，弱化“证据可复核”纪律。
- **建议**：把 tooltip 的引用改为 `png_pvt.h:626-628`（3.1.17），并显式标注版本号（`OIIO 3.1.17`）；顺便把“无 interlace 属性 ⇒ 输出恒非隔行、不支持渐进显示”写清（当前只说“无 interlace 属性”）。

---

#### 4. 输出兼容性与合规矩阵

所有产物均通过独立结构校验（`build/encoder-audit/probe/validate_all.py`：52 个 PNG，签名/CRC/IHDR 首/IEND 尾/IDAT zlib 解压，**bad=0**）；像素用独立 Python 解析器（`zlib` + 自写反滤波）读取，不依赖 OIIO。

| 源/配置 | 产物结构（实测块序列） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| RGB8 PNG → PNG 8（默认参数） | `IHDR(bd8,ct2) eXIf(90) sRGB gAMA cHRM oFFs IDAT(124) IEND` | OIIO（pp_verify `OK`）、本仓解析器、zlib 均可 | 低（无 ICC 时 sRGB+gAMA+cHRM 与 sRGB 数据一致） |
| RGB8 → PNG 16 | `IHDR(bd16,ct2)`，逐样本 `= v*257` | 同上（`VERIFY png16-audit OK`） | 中：无 sBIT，对外宣称 16 bps 但实质 8 位；体积 +18.9%~+99.9% |
| RGB16 → PNG 16 | `IHDR(bd16,ct2)`，逐样本等于源 | `VERIFY png16-audit OK` | 低（大端序正确） |
| gray8 / gray16 → PNG | `IHDR(ct0)` 单通道 | `VERIFY gray16-audit OK` | 低 |
| gray+alpha8 → PNG 8/16 | `IHDR(ct4)` | OK | 低（夹具 alpha 只有 0/255） |
| **RGBA8（含半透明）→ PNG 8** | `IHDR(ct6)`，alpha 正确、**RGB 被改写**（alpha=0 → 0,0,0；alpha=1..4 严重偏移） | 结构可读、pp_verify 报 `OK`（门禁失明） | **高：像素保真（P0）**；文件本身合法 |
| RGBA16（含半透明）→ PNG 16 | 同上，误差 ≤ ~1/alpha 个 16 位 LSB；alpha=0 仍归零 | 可读 | 高（同上，量级小但不可逆） |
| RGB8 + `--color p3` / `adobergb` / 源带 P3 ICC（默认 keep） | `IHDR eXIf iCCP(375~580) sRGB(intent=3) gAMA(45455) cHRM(sRGB 基色) oFFs IDAT IEND` | 可读；颜色管理链按 PNG3 优先级取 iCCP | **中：色彩块互斥冲突（PNG-02）**；旧解码器可能采信 sRGB → 偏色 |
| 灰度 8 位 + 源带 sRGB iCCP → PNG 8 | `IHDR(ct0) iCCP(379, colorspace='RGB ') sRGB gAMA cHRM eXIf oFFs …` | 可读；lcms 组合非常规 | **中：ICC/色彩类型不匹配（PNG-05）** |
| JPEG(EXIF+GPS) → PNG 8 | `eXIf(202B)`：IFD0 `Artist=M0`，ExifIFD `DateTimeOriginal=2024:03:01 10:00:00`，GPSIFD `N 31°13'49.44" E 121°28'25.32"` | exiftool/Exiv2/OIIO 均可解析 | 低（EXIF 往返正常；eXIf 在 IDAT 前） |
| PNG(XMP) → PNG（触发 Exiv2 重写） | `eXIf iCCP iTXt(XML:com.adobe.xmp, 2503B) sRGB gAMA cHRM oFFs IDAT` | 可读；XMP 被规范化为完整 packet | 低（ICC/EXIF 均存活；体积因 XMP 规范化上涨） |
| 源 PNG 带 pHYs/tIME/tEXt → PNG 重编码 | 三者**全部消失**；新增空 `eXIf(14)`、`oFFs(0,0)` | 可读 | **中：DPI/时间戳/文本元数据静默丢失（PNG-06）** |
| 仅元数据模式（`--metadata-only`，PNG→PNG） | 源块序列保留（eXIf 前移），`IDAT` **逐字节一致**（sha256 `01f6ecb1…` 前后相同），像素逐位一致 | 可读 | 低（唯一元数据路径不动像素，符合 P0 红线） |
| 全部 52 个产物 | IHDR 首 / IEND 尾 / CRC 全对 / IDAT 可解压 | OIIO + 独立解析器 | 无结构性不合规 |

---

#### 5. 未覆盖与存疑

**未覆盖（成本/权限原因）**
1. **GUI 实跑与 ui-smoke 未做**（任务禁令）。「无损编码」复选框对 PNG 的实际可见性/禁用态是**代码推断**（`paramform.cpp:599-602` + `deflate.lossless_capable=true`），未截图取证；同理位深行的注释文案未在真机确认。
2. **大图/真照片**：体积与耗时数据来自 384×384 合成图（`build/encoder-audit/probe/photo384.png`，含 ±6 噪声）与 64×64 金样夹具；未测 100MP 级，也未测 16 位真照片源（16 位源的 8bit vs 16bit 体积比只会更大，未量化）。
3. **调色板 / 1·2·4 位 PNG 输入**：OIIO 读取时已归一为 8 位 RGB(A)，本审计未单独验证 sBIT/tRNS 类输入的语义。
4. **隔行（Adam7）源 PNG**：读端支持（`pnginput.cpp:327-334` 整帧回退），写端恒非隔行；未做隔行源→输出的实测。
5. **libpng 对 iCCP+sRGB 的告警**：OIIO 未把任何 libpng warning 透出到 `open()/write_image()` 的 error 串（实测日志无相关行）；是否是 OIIO 过滤掉了 libpng 的 “profile matches sRGB” 类消息**未查证**。
6. **色彩变换作用在 premultiplied 缓冲上**：`colormanager` 拿到的 RGB 已乘 alpha（PNG-01 的同一缓冲），理论上会让半透明像素的色彩变换结果偏暗；本次未设计可判定的实验（需要精确的已知变换矩阵），标记为**存疑**（suspected）。
7. **PNG→其它格式的 alpha 影响面**：只实测了 PNG→BMP（合成错误，见 PNG-01）与 PNG→TIFF（ExtraSamples=1、值已 premultiplied）；PNG→JXL/WebP 的 `alpha_premultiplied=false` 声明与数据不一致**由代码推断**（`enc_jxl.cpp:305,320`），未做 JXL/WebP 解码级验证。

**存疑**
8. **PNG-01 的严重度定级**：按「静默把有损当无损」应 P0，按“可见像素几乎透明”可 P1；本审计取 P0 并列出两种口径，请主控裁定。
9. **16 位默认的体积代价**是图像相关的：本仓口径（`brainstorm-consensus.md:68`）已定为 16 位，若主控认为“8 位源 +19% 体积”可接受，则 PNG-04 只剩“缺 sBIT/缺提示”的信息面问题（可降 P3）。
10. **空 `eXIf` 是否触发第三方校验器告警**未实测（本机无 exiftool/pngcheck）。

**实测命令与产物（可复核）**

```powershell
# 基础：金样夹具矩阵（8/16 位 × rgb/rgba/gray/graya）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png `
  --out build/encoder-audit/probe/matrix/rgb8_8 --format png --bitdepth 8 --workers 1
# compressionLevel 0/6/9 与 8/16 位体积矩阵（384×384 合成图）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/probe/photo384.png `
  --out build/encoder-audit/probe/cl_8_6 --format png --bitdepth 8 --param compressionLevel=6 --workers 1
# 越界拒绝 / 未知键拒绝
... --param compressionLevel=12      # → param 'compressionLevel': value 12 out of range [0, 9]
... --param png:filter=248           # → error: 未知参数：png:filter
# alpha 取证（半透明源 → PNG / BMP / TIFF）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/probe/alpha_ramp.png `
  --out build/encoder-audit/probe/ramp_out8 --format png --bitdepth 8 --workers 1
# 像素断言（官方 exact 语义）
python tools/env.py run -- build/release-dev/pp_verify.exe `
  build/encoder-audit/probe/v-png16/case/expected.json build/encoder-audit/probe/matrix/rgb16_16/rgb16.png
# 结构自检（52 个产物 CRC/IDAT）
python build/encoder-audit/probe/validate_all.py
```

产物/脚本：`build/encoder-audit/probe/{pngtool.py, matrix.py, cmp_alpha.py, check_levels.py, filter_experiment.py, icc_check.py, exif_dump.py, tiffread.py, validate_all.py, make_*.py}` 与同名子目录（全部 gitignored，未入库）。

### 4.4 TIFF（OIIO / 压缩方案）

> 审计对象：`format=tiff` / `backend=oiio` / `tech=codec`（0.3.5.alpha2，`build/release-dev/photopipeline.exe`）。
> 库版本（运行日志实测）：OIIO **3.1.17.0**、libtiff **4.7.2**、Exiv2 **0.28.9**。
> 实测命令一律经 `python tools/env.py run -- …`；产物与探针脚本在 `build/encoder-audit/tiff/`（gitignored）。
> 只读审计：未改动 `src/`、`tests/`、`docs/`、CMake。

#### 1. 结论（直说有没有问题）

1. **有一个 P0：多页 TIFF 源的额外页被逐字节搬进产物；≥3 页时搬出来的页 IFD 指向文件外，该页不可解码**（`ok=true` + 只有一条 "only the first page was decoded" 警告）。编码器本身只写 1 页（实测 encode 后 705 B），页是**元数据后写阶段**加进去的（最终 4622 B / 3 个 IFD）。
2. **`compression` 五档里 `ccittrle` 是必然失败的档**：libtiff 的 CCITT RLE 只接受 `BitsPerSample==1`，而本管线只出 8/16 bps ⇒ 4 组实测输入（gray8/gray16/rgb8/rgba8）全部在 `write_image` 阶段失败，用户得到的是 libtiff 英文底层错。
3. **带 alpha 的 TIFF 语义标反了**：产物 `ExtraSamples=1`（associated / 预乘），而管线数据是 straight alpha（全仓无预乘）。按 TIFF 6.0 规范合成时会按"RGB 已预乘"解释 → 透明区/边缘出现亮边。这是 JPEG/PNG/WebP/HEIF 都描述正确的同一个 alpha 在 TIFF 上被描述错。
4. **`deflate_level` 在 `predictor=none` 时被完全忽略**（1/6/9 三档产物逐字节相同，条带与 tile 皆然）；用 oiiotool 独立复现，确定不是本项目接线问题，而是 OIIO/libtiff 侧行为。默认 `predictor=horizontal` 下级别正常生效（28122 B → 8577 B）。
5. **两个"可选但不可用"的档位没有前置校验**：`predictor=float(3)` 与 `compression=ccittrle` 都能通过 `validate_params`/`cross_validate`、能存进预设，直到每个文件都解码完才失败；GUI 的"运行前拦截"（`mainwindow_run.cpp:338-357`）对它们不生效。
6. **TIFF 的"无损编码"复选框是空转控件**：默认未勾选、可见、单击零效果（勾/不勾产物只差时间戳字节）。TIFF 恒无损，控件状态却在暗示"有损"。
7. **产物不可复现且注入非规范 XMP**：编码器不设 `DateTime`，OIIO 自动写"编码时刻"（tag 306）并由 `encode_xmp` 派生 tag 700 的 XMP，其中 `xmp:CreateDate` 用的是 EXIF 日期格式而非 ISO 8601。两次同参编码只差时间戳的 2 个字节。
8. 正面结论：`lzw/zip/none/packbits` 四档全部像素逐位一致（`pp_verify` exact 全绿）；`predictor` none/horizontal 的用户覆盖**真实生效**（R-011 已闭合，字节差 50 KB vs 687 KB）；tile 的成对/16 倍数约束在 CLI 与 GUI 都被拦下；仅元数据模式下 strip 与 tile 的压缩载荷**逐字节保真**；`lzw/zip` + ICC + 8/16 bit + gray/grayalpha/rgb/rgba 结构均合规。

#### 2. 参数全量矩阵

`format=tiff` 的静态表（`src/core/format_tables.cpp:621-688`）：单后端 `oiio`、单技术 `codec`（label "Compression"，`lossless_capable=true`），参数共 5 条；位深集 `{8,16}`，`supports_alpha/supports_gray=true`，`meta_path="exiv2"`。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `compression` | 是（核心组，Enum 组合框；技术行因单技术被隐藏 `paramform.cpp:585-588`） | `"lzw"` | `none` / `lzw` / `zip` / `ccittrle` / `packbits` | `src/codecs/enc_oiio.cpp:250-254`（读→白名单→`spec.attribute("compression")`） | 是（`ccittrle` 例外：被读到但产物必然失败，见 TIFF-02） | 谓词单源 `compression_is_zip` / `compression_is_lzw_or_zip`（`format_tables.cpp:38-41`）驱动另两行的可见性；无 `cross_validate` 规则；`deflate`/`zip:9`/大写等别名在 encoder 前置拒绝（`enc_oiio.cpp:251-253`） |
| `deflate_level` | 是（高级折叠·编码工具组） | `6` | 1–9，step 1 | `src/codecs/enc_oiio.cpp:255-259`（仅 `compression=="zip"` 时写 `tiff:zipquality`） | 条件（仅 zip；且 `predictor=none` 时被 OIIO/libtiff 忽略，见 TIFF-04） | `visible ⇔ compression=="zip"`（`format_tables.cpp:642-650`）；越界由 `validate_params` 拒（`params.cpp:534-552`，实测 value 0/12 → exit 2）；OIIO 侧再 clamp 1–9（`tiffoutput.cpp:706-710`） |
| `predictor` | 是（高级折叠·编码工具组） | `2`（horizontal） | `none=1` / `horizontal=2` / `float=3` | `src/codecs/enc_oiio.cpp:260-279`（写 `tiff:Predictor`） | 条件（仅 lzw/zip 写入；`float` 直接报错；none/packbits/ccittrle 下静默忽略，见 TIFF-05） | `visible ⇔ compression∈{lzw,zip}`（`format_tables.cpp:667`）；键名 `tiff:Predictor` 在 OIIO 有真实消费点（`tiffoutput.cpp:1026-1029`）且晚于自动规则（`:685-701`）⇒ 用户覆盖生效（实测 tag 317=1 vs 2、字节差 13.7×） |
| `tiff_tile_width` | 是（高级折叠·编码工具组） | `0`（strip） | 0–4096，step 16 | `src/codecs/enc_oiio.cpp:280-295`（`spec.tile_width`） | 是 | 成对约束 + 16 倍数由 `cross_validate` 三条规则拦（`params.cpp:222-263`），GUI 红字（`paramform.cpp:932-945`）且运行前拦截（`mainwindow_run.cpp:338-357`）；实测 10 / 仅宽 / 仅高 → exit 2，256 / 16 / 1024 均出 tile 且像素 exact |
| `tiff_tile_height` | 同上 | `0` | 0–4096，step 16 | `src/codecs/enc_oiio.cpp:281-295` | 是 | 同上 |
| `out_bitdepth`（引擎字段，UI 为位深行） | 是（页签内位深行 `page_output.cpp:258-279`） | `16` | `8` / `16`（`FormatDef.bitdepths`） | `src/codecs/enc_oiio.cpp:211-217` | 是 | 默认 16 位 ⇒ 8 bit 源被升到 16 bit（共识 §3.4 明定、`pipeline.cpp:1718-1734`）；16→8 降档有 `DepthDowngrade` 警告（`pipeline.cpp:1042-1046`，实测 sidecar 命中） |
| `lossless`（无损复选框，非 TIFF 声明参数） | 是（默认未勾选 `page_output.cpp:718-720`） | `false` | on/off | **无读取点**（TIFF 技术不声明 `lossless` 参数） | 否（对产物零影响，见 TIFF-06） | 可见性判据 = "存在 lossless_capable 技术"（`paramform.cpp:599-602`）；`apply_locks` 只写内部键 `__lossless`（`params.cpp:495-507`） |

引擎侧**非参数**的 TIFF 写出面（用户不可控，但决定产物）：`ExtraSamples`（OIIO 自动，值取错，TIFF-03）、`Orientation=1`（`tiffoutput.cpp:593-594`）、`RowsPerStrip=32`（OIIO 默认）、`DateTime`（`tiffoutput.cpp:871-883` 自动填当前时间）、`XMP`（`:951-953`）、`Software`（`oiiotool` 会写、本管线不写）、`ICCProfile`（`enc_oiio.cpp:300-306`）。

#### 3. 发现

##### TIFF-01 [P0] 多页 TIFF 源的额外页被原样搬进产物；≥3 页时该页 IFD 指向文件外、不可解码

- 证据：
  - `src/codecs/enc_oiio.cpp:379` —— 编码器只调一次 `out->write_image(...)`（一个 subimage）；实测编码阶段产物 **705 B**（`build/encoder-audit/tiff/cases/multi3/logs/run-20261025-144416.log`：`output done … bytes=705 … metawrite_ms=26.67`），最终文件 **4622 B**。
  - `src/core/pipeline.cpp:1131` —— 编码后唯一写盘步骤是 `write_metadata_exiv2(row.out, …)`；`src/core/metadata.cpp:365-396`（`exiv2_apply`：`open→readMetadata→setExifData→writeMetadata`）。
  - 3 页源 `<src>` 3 个 IFD：`32x32 u8` / `48x48 RGB8 deflate` / `64x64 RGBA8 deflate`。产物 3 个 IFD，第 3 个 `StripOffsets = [2771273, 8]`，而文件总长 **4622 B**（越界）。
  - libtiff/Exiv2 读取该产物：`Warning: Directory Image2, entry 0x0111: Strip 0 is outside of the data area; ignored.`；OIIO 读第 3 页：`TIFFReadRawStrip failed reading line y=0: Read error at scanline 4294967295; got 0 bytes, expected 3634`（`oiiotool … --subimage 2`）。
  - 2 页源（`tests/golden/base/multi.tif`）：产物第 2 页 strip 载荷与源第 2 页 **逐字节相同**（源 `752:826` == 产物 `588:662`），且 `pp_verify` 断言 `Exif.Thumbnail.Compression` 存在、`Exif.Thumbnail.ImageWidth==32`、`Exif.Image.ImageWidth==64` 全部通过 ⇒ 源第 2 页被当成"缩略图"重发。
- 事实：`decode` 只解首页并记 `MultipageTruncated`（`oiio_reader.cpp:244-247`，实测 sidecar 命中）→ 编码器写单页 → **元数据后写阶段**把源里第 2..N 个 IFD 的目录项（含 `StripOffsets/ByteCounts`，以及第 2 页的真实压缩数据）一并搬进新文件；第 2 页数据被搬（因它被当作 thumbnail 数据区搬运），第 3 页只搬了目录项、没搬数据 ⇒ 偏移越界。
- 影响：用户选了"多页取首页"，拿到的却是 N 页文件；第 2 页是**未经本次参数/位深处理**的旧画面（本例仍是 deflate/8 bit，与所选 lzw/8 bit 不一致），第 3 页起不可解码。任何枚举子图的工具（OIIO/libtiff 遍历、扫描档批处理、PDF 封装）会在该页报错；`ok=true` 且无警告指向结构问题 ⇒ 用户不会发现。
- 建议：
  1. `src/core/metadata.cpp` 的元数据载荷构造：对 TIFF 输出只携带 **IFD0** 的标签，剔除 `Exif.Thumbnail.*` / `Exif.SubImage*` 一类跨目录键（或按新产物重新生成缩略图），禁止把源的多页目录搬进单页产物；
  2. `pipeline.cpp` 的 metawrite 前后加一条结构断言（TIFF：`subimage_count==1` 除非目标格式声明多页），失败走 `ok=false`；
  3. 金样/单测加机械化探针：多页 TIFF 输入 → 断言产物只有 1 个 IFD（`tests/golden/smoke/` 现有 tiff 用例不覆盖多页）。
- 状态：confirmed（现象、字节级搬运、越界、不可解码均实测）；`known=no`。归属：元数据后写路径（元数据代理），但**只在 TIFF 输出上**表现为容器结构损坏，故在格式侧登记。

##### TIFF-02 [P1] `ccittrle`：核心压缩方案里必然失败的选项

- 证据：
  - `src/core/format_tables.cpp:629-633` —— 五个 choices 含 `ccittrle`（核心组、无锁、无谓词）；`:625` 技术声明 `lossless_capable=true`。
  - `src/codecs/enc_oiio.cpp:58`（白名单含 `ccittrle`）、`:251-254`（只校验"名字合法"，随后原样下发给 OIIO）。
  - OIIO 映射：`tiffoutput.cpp:308` `{ COMPRESSION_CCITTRLE, "ccittrle" }`；libtiff `libtiff/tif_fax3.c:594-596`：`if (td->td_bitspersample != 1) { TIFFErrorExtR(… "Bits/sample must be 1 for Group 3/4 encoding/decoding"); }`；`vcpkg_installed/x64-windows-avx2/include/tiff.h:183` `#define COMPRESSION_CCITTRLE 2`。
  - 实测 4 组输入全部失败（16/8 bit、gray/RGB/RGBA 各覆盖），错误文本一致：`encode failed: OIIO write_image failed for …: TIFFWriteScanline failed writing line y=0,z=0 (Bits/sample must be 1 for Group 3/4 encoding/decoding)`；`cases/{ccittrle,rgba8_ccittrle,gray8_ccittrle,gray16_ccittrle}`，exit=1，目录内无残留 `.tif`（临时文件被 `discard_temp_file` 清掉，`enc_oiio.cpp:367-371`）。
  - 本管线永远不产 1 bps：`enc_oiio.cpp:211-217` 只接受 8/16，`format_tables.cpp:686` `bitdepths={8,16}`。
- 事实：UI 给出 5 个"压缩方案"，其中 1 个在任何源/位深/通道组合下都必失败；失败发生在整帧解码之后，错误是 libtiff 英文底层串，用户无法从 UI 得知"这个档需要 1 位样本"。
- 影响：选到它的批处理会把每个文件都跑完解码再失败（exit=1，一文件一行 error），大批量等于空转一轮；文档声称"五档可用"与实际四档可用不符。
- 建议：在 `format_tables.cpp` 删掉 `ccittrle` 选项（或保留但加 `visible={}` + `locked` 到"不可用"并给中文原因）；若要保留入口，则在 `cross_rules()`（`params.cpp:219-289`）加一条 `compression=="ccittrle" ⇒ 报错`，让 GUI 运行前拦截并给中文文案（"CCITT RLE 仅支持 1 位样本，本管线只输出 8/16 位"）。
- 状态：confirmed；`known=no`（`docs/deep-review-0.3.2.md:589`/`R-316` 只记录 param-catalog 曾漏列 `ccittrle`，未记录其不可用）。

##### TIFF-03 [撤回·不计入总表] 带 alpha 的 TIFF 把 straight alpha 标成 associated（`ExtraSamples=1`）

> **主编复核（2026-10-25，独立代理复跑）**：本条撤回。对同一 RGBA ramp 直接解析 TIFF strip 原始字节，alpha=128 处 RGB=`100,50,25`（=200×0.5，premultiplied），alpha=64 → `50,25,13`；tag 338=`1`（associated）。数据与标签**一致**，TIFF 是唯一自洽的 alpha 格式。原判据「`pp_verify exact` 绿 ⇒ 数据未预乘」不成立（读端同样按 associated 解释）。真正的跨格式问题是「管线预乘 vs 各编码器假定」，并入 `PNG-01`/5.7，不再单列。

- 证据：
  - `src/codecs/enc_oiio.cpp:222-237` —— 只设 `spec.channelnames` 与 `spec.alpha_channel`，**从不设** `oiio:UnassociatedAlpha`（全仓 grep 仅 `enc_jxl.cpp:305,320` 有 `alpha_premultiplied=JXL_FALSE`）。
  - `vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/tiff.imageio/tiffoutput.cpp:823-842`：`bool unass = m_spec.get_int_attribute("oiio:UnassociatedAlpha", 0); … extra[c] = unass ? EXTRASAMPLE_UNASSALPHA : EXTRASAMPLE_ASSOCALPHA;`
  - `vcpkg_installed/x64-windows-avx2/include/tiff.h:330-332`：`EXTRASAMPLE_ASSOCALPHA 1`（associated = 预乘）/ `EXTRASAMPLE_UNASSALPHA 2`。
  - 实测产物 tag 338：`rgba8`/`rgba16`/`rgba8_tile`/`graya8`/`graya16`/`rgba_p3_16` 全部 `=[1]`；同一批 `pp_verify` 像素 exact 全绿 ⇒ 像素值**没有**被预乘（否则 exact 不会过）。
  - 判别器：OIIO 自己的读端按标签解释（`tiffinput.cpp:1359-1367`：`EXTRASAMPLE_ASSOCALPHA → alpha_channel=c`，不做反预乘），所以 exact 断言发现不了这个错误。
- 事实：产物写的是"预乘"标签、装的是"非预乘"数据。遵守 TIFF 6.0 的合成器按 `out = RGB + B(1-a)` 处理，透明像素的 RGB 仍然很大 ⇒ 白边/亮边；与同源 PNG/WebP/JPEG 输出（格式定义即 straight）语义不一致。
- 影响：凡带 alpha 的 TIFF 在 Photoshop/GIMP/Krita/ImageMagick 一类按标签处理的工具里合成到背景时出现错误边缘；对"像素搬运工"的定位而言是语义级错误（像素没坏，描述坏了）。`supports_alpha=true` 的 TIFF 是文档明示的常规输出。
- 建议：`enc_oiio.cpp` 里对 2/4 通道输出加 `spec.attribute("oiio:UnassociatedAlpha", 1);`（或在写 `ExtraSamples` 时直接用 `spec.attribute("tiff:write_extrasamples", 1)` + 正确属性）；补一条金样/单测断言 tag 338 == 2，覆盖 `rgba8/graya8/rgba16`。
- 状态：confirmed；`known=no`（`docs/m1-tasks.md:642` 只记录 WebP 读端预乘坑，未涉及 TIFF 写端标签）。

##### TIFF-04 [P2] `deflate_level` 在 `predictor=none` 时被完全忽略（无提示）

- 证据：
  - 实测（`cases/strip_pred1_lvl1|strip_pred1_lvl6|strip_pred1_lvl9`，条带）：**268496 B / 载荷 sha `e6adff1676db38a5`，三档完全相同**；对照 `strip_pred2_lvl1=28122`、`strip_pred2_lvl9=8577`（载荷 sha 不同）。
  - 实测（tile 256，`cases/tile_zip_pred1|tile_lvl1|tile_lvl6|tile_lvl9`）：`predictor=1` 时 1/6/9 三档均 **253431 B / sha `e31be93452fd4efa`**；`predictor=2` 时 35859 vs 11534。
  - 独立复现（不经本项目）：`oiiotool in.png --compression zip:1 --attrib tiff:Predictor 1` 与 `zip:9 --attrib tiff:Predictor 1` 均 **164469 B / 同一 sha**；`--attrib tiff:Predictor 2` 时 14315 vs 6603。⇒ 不是 `enc_oiio` 键名/取值问题（键 `tiff:zipquality` 在 `tiffoutput.cpp:703` 有真实消费点）。
  - 疑点代码位置（机制未定论）：`tiffoutput.cpp:702-711`（`TIFFSetField(TIFFTAG_ZIPQUALITY, …)`）在 `:819`（`TIFFSetField(TIFFTAG_COMPRESSION, m_compression)`）**之前**执行；libtiff 的 ZIP 质量是编解码器伪标签（`libtiff/tif_zip.c:597-599` `ZIPVSetField`），且 `ZIPEncode` 有 libdeflate 快路径（`tif_zip.c:436-440` 用 `sp->zipquality` 映射级别）。
- 事实：`compression=zip` + `predictor=none` 这个合法组合下，用户设的 1–9 级被丢掉，产物固定在同一档（约 268 KB / 253 KB），而 `predictor=horizontal` 下级别正常。`validate_params`/`cross_validate` 都不涉及该组合，界面无任何提示。
- 影响：把 `predictor=none`（老读者兼容/追求吞吐）与 `deflate_level=9`（追求体积）一起用的用户，会得到"级别无效"的产物：同一张 512×384 图，horizontal+9 级是 8.5 KB，none+任意级是 268 KB，差 **31 倍**且无法通过调级别改善；参数表看起来"已接线"。
- 建议：单靠本项目改不了 libtiff，但可以做两件事收紧口径：① 在 `format_tables.cpp` 给 `deflate_level` 的 `visible` 加 `&& predictor != 1`（或对 `predictor=1` 显示一条注释"此组合下 Deflate 级别不生效"）；② 在 `enc_oiio.cpp` 的 zip 分支里，当 `predictor==1` 且 `deflate_level != 默认` 时 `note_param(… "deflate level ignored with predictor=none (OIIO/libtiff)")`，至少让日志可查；③ 上游：给 OIIO 报 issue（`tiffoutput.cpp:702-711` 顺序 / predictor 包装器与 zipquality 交互）。
- 状态：现象 confirmed（三档级别 × 两种排布 × oiiotool 独立复现）；机制 suspected（未在 libtiff 源码层定论，故不给因果断言）。`known=no`。

##### TIFF-05 [P2] `predictor=float(3)`：可选、可存预设、必然整文件失败

- 证据：
  - `src/core/format_tables.cpp:651-667` —— choices 含 `{"float", int64_t(3)}`，`visible=compression_is_lzw_or_zip`（即默认 lzw 下**可见可选**），tooltip 自述"在本管线不可用…选中即明确报错"。
  - `src/codecs/enc_oiio.cpp:272-279` —— `if (compression=="lzw"||"zip") { if (predictor==3) return fail("tiff: predictor=float (3) is not usable here — …") }`；错误文本实测命中（`cases/lzw_pred3`、`zip_pred3`：exit=1，`encode failed: tiff: predictor=float (3) is not usable here …`）。
  - 预设路径同样放行：`preset_pred3`（preset JSON `{"compression":"zip","predictor":3}`）→ 加载成功、exit=1 逐文件失败。
- 事实：`float` 档在"输出 8/16 位整型样本"的前提下永远不可用（libtiff `PREDICTOR_FLOATINGPOINT` 只对 `SAMPLEFORMAT_IEEEFP` 有效，`tiff.h:304-306`），但 UI 只在 tooltip 里说、控件仍可选；`cross_validate` 无该规则 ⇒ GUI 允许点"开始"，整批文件全部解码后逐条失败。
- 影响：一次误选 = 整批文件失败（每个文件都白解码一遍）；预设里若带着 `predictor=3`，换机器/换批次会静默复现。
- 建议：删掉 `float` 这一档（最省事、语义上也对：本管线无浮点样本输出）；若保留，则加进 `cross_rules()` 让 GUI 运行前红字拦截，并在 Combo 里禁用该项（`paramform.cpp` 已有"禁用项 + 原因"的现成机制，见 `update_tech_items_enabled` 同款做法）。
- 状态：confirmed；`known=no`（`docs/param-catalog.md:222` 只描述"选中即报错"，未视为缺陷；`deep-review R-011` 记录的是键名消费问题，已闭合）。

##### TIFF-06 [P2] "无损编码"复选框对 TIFF 是空转控件（可见、默认未勾、单击零效果）

- 证据：
  - `src/ui/paramform.cpp:599-602` —— `has_lossless = first_lossless_tech_in(b) != nullptr;` → TIFF 技术 `lossless_capable=true`（`format_tables.cpp:625`）⇒ 复选框**可见**；`sel.lossless` 构造期恒 `false`（`paramform.cpp:421-426`），预设/快照缺失时也回 `false`（`page_output.cpp:718-720` 与 `apply_state_to_form` 的 `have_spec=false` 分支）。
  - TIFF 技术不声明 `lossless` 参数（`format_tables.cpp:626-684` 五条参数无 `lossless`）；`apply_locks` 只同步内部键 `__lossless`（`params.cpp:495-507`），`enc_oiio` 不读它。
  - 实测：同图 `--lossless` 与不带该开关的产物，除 `DateTime`/XMP 的时间戳字节外**完全一致**（50314 B，差异仅 offset 319/320 与 655/656 的秒位）。
- 事实：TIFF 五种压缩方案全部无损（`none/lzw/zip/packbits` 是通用无损，`ccittrle` 虽不可用也是无损编解码器），所以"无损编码"在 TIFF 上恒真；控件却以"未勾选"呈现，勾选也不改变任何字节。
- 影响：默认界面就在暗示"TIFF 当前输出是有损的"（与"TIFF 里可能放 JPEG"的常识叠加更容易误解），用户会去做无意义的勾选；属于"UI 与产物矛盾"，但方向是"产物比标签更好"，无数据风险，故记 P2。
- 建议：对"技术本身无损且未声明 `lossless` 参数"的格式（tiff/png/bmp）把复选框呈现为**勾选 + 禁用 + 原因 tooltip**（"TIFF 恒为无损；压缩方案只影响体积/速度"），或直接隐藏；判据现成：`tech_declares_lossless()==false && tech.lossless_capable`（`params.cpp:192-199`）。
- 状态：confirmed；`known=no`。

##### TIFF-07 [P2] 编码器不设 `DateTime` → OIIO 写"编码时刻"并派生非规范 XMP，产物不可复现

- 证据：
  - `tiffoutput.cpp:871-883` —— `if (!m_spec.find_attribute("DateTime")) { time(&now); … m_spec.attribute("DateTime", date); }`（格式 `%Y:%m:%d %H:%M:%S`）；`:951-953` `encode_xmp(m_spec, true)` → `TIFFSetField(TIFFTAG_XMLPACKET, …)`；`src/libOpenImageIO/xmp.cpp:105` `{ "xmp:CreateDate", "DateTime", TypeDesc::STRING, DateConversion|TiffRedundant }`。
  - 实测产物：tag 306 `2026:10:25 14:37:37`；tag 700 383 B，内容 `… xmp:CreateDate="2026:10:25 14:37:37" …`（**XMP 要求的 ISO 8601 应是 `2026-10-25T14:37:37`**）；两次同参编码仅 offset 319/320、655/656 不同（秒位 + XMP 镜像）。
  - 隔离：`oiiotool in.png --compression lzw -o out.tif` 产出同样的 383 B XMP 与 tag 306 ⇒ 来自 OIIO，不是 Exiv2。
  - 条件性：源带元数据时 Exiv2 后写会清掉这些 tag（`jpeg_in_tiff` 产物无 tag 700；`--meta Exif.Image.Artist=PP2` 的产物既无 306 也无 700，`metadata.cpp:377-386` 的空容器 `clearExifData/clearXmpData`）⇒ 同一参数下"产物里有没有时间戳/XMP"取决于源格式，行为不一致。
- 事实：PP 的元数据策略是"源值 + 规则"，但编码器把 `DateTime` 留空，OIIO 用**本机当前时间**补上，并把它转成 XMP 写进产物。结果是（a）同输入同参不产出同字节（校验/回归/去重都会受影响）；（b）写入的时间在源里并不存在（与"元数据手术台、不加料"的定位冲突）；（c）XMP 日期值不合规，严格消费者可能丢弃/报错。
- 影响：批量转码无法做内容哈希去重/幂等校验；跨时区/跨机器时间戳漂移；下游读到"拍摄时间=转码时间"的错误元数据。
- 建议：`enc_oiio.cpp` 里显式写 `spec.attribute("DateTime", <源生效 DateTimeOriginal，缺失则空/固定值>)`，把"时间"重新纳入 PP 的单源控制；同时向上游报 OIIO 的 `DateConversion` 未把 EXIF 日期转 ISO 8601；元数据侧补一条"TIFF 输出不得出现 tag 306/700 的编码时刻"断言。
- 状态：confirmed；`known=yes`（`docs/m0-tasks.md:772` 的 D2 已记录"TIFF writer 默认写当前时间"这一事实，当时的处置是夹具侧钉 `DateTime=2024:01:01` 规避；**产物侧的不可复现与 XMP 非 ISO 8601 未登记**）。

##### TIFF-08 [P3] "暴露 OIIO 全部 TIFF 输出参数"与实际 5 条不符；工具注里的行号是 3.1.14 的

- 证据：
  - `docs/brainstorm-consensus.md:60` / `docs/design.md:490`（R5）：TIFF 由 OIIO 承载、"暴露其全部输出参数"。
  - 实际只有 5 条（`format_tables.cpp:626-684`）。OIIO 侧确实可写但未暴露的属性（有真实 `put_parameter` 分支）：`tiff:RowsPerStrip`（`tiffoutput.cpp:1038-1047`）、`XResolution/YResolution`（`:1083-1088`）、`tiff:write_iptc`（`:939-949`）、`tiff:bigtiff`（`:510-511`）、`tiff:write_extrasamples`（`:827`，与 TIFF-03 直接相关）、`planarconfig`（`:859-869`）。
  - 注里引用的行号已漂移：`format_tables.cpp:636-640` 引 "tiffoutput.cpp:660-673 / 301-338 / 339-344"，3.1.17 实际是 `668-681 / 303-346 / 346`；`:645-648` 引 `694-702`，实际 `702-711`；`:671-675` 引 `480-487,575-577`，实际 `482-489,584-…`。
- 事实：M0 裁定过"TIFF 5 条真实参数"（`docs/m0-tasks.md:922`），所以这是**口径漂移**（文档仍说"全部"、"加一行数据"式全暴露），不是实现 bug；行号漂移会让后续复核按图索骥失败。
- 影响：用户/维护者按文档预期找 `RowsPerStrip`（影响兼容性与体积）、DPI、BigTIFF 开关会找不到；错误行号增加复核成本。
- 建议：把 `design.md` R5 与 consensus 的措辞改成"暴露经 M0 裁定的 5 条参数 + 未暴露清单"；工具注行号统一改为 `tiffoutput.cpp` 函数名锚（`TIFFOutput::open` / `put_parameter`）而非绝对行号。
- 状态：confirmed；`known=yes`（`docs/deep-review-0.3.2.md:589` R-316 记录了 param-catalog 与实现的 TIFF 漂移，已按实现订正；"全部参数"表述与行号漂移未登记）。

##### TIFF-09 [P3] `enc_oiio` 的"未知参数忽略并告警"（E9）在正式路径上不可达

- 证据：
  - `src/codecs/enc_oiio.cpp:308-315` 遍历 `req.target.params`，未知键 → `note_param("unrecognised parameter ignored", …)`（warn）。
  - 但 `params.cpp:592-610` 的 `cross_validate` 规则③ 对未知键直接产出 `"未知参数：" + key`，而 `pipeline.cpp:1674-1679`（`validate_run_config`，`run_file` 在 `:626` 调用）会在**任何编码之前**把它变成失败。
  - 实测：`--param bogus=1` → 文件 failed、日志 `[pipeline] 未知参数：bogus`（`cases/unknown_param/logs/…`），编码器那条 warn 从未出现。
- 事实：E9 的"忽略 + warn"分支只在直接调用 `IEncoder::encode` 的单测里可能触发；产品路径上未知键是硬错误（这个行为本身更好）。
- 影响：注释/任务书宣称的"E9 检测 + 未知参数告警"给人"参数拼错会被宽容处理"的错觉；也让 `known_param_keys()`（`:107-121`）成为只服务测试的代码。
- 建议：保留硬错误行为，把 `enc_oiio.cpp:106-121/308-315` 的注释改为"仅防御直调 API；产品路径由 `cross_validate` 先行拒绝"，或把 E9 降级为 assert/调试日志。
- 状态：confirmed；`known=no`。

##### TIFF-10 [P3] 文案：核心行标签写着"（技术选择器）"、枚举值是英文

- 证据：`format_tables.cpp:627` label = `"压缩方案（技术选择器）"`（本格式只有单技术 `codec`，技术行被 `paramform.cpp:585-588` 隐藏，`compression` 是普通参数，不是技术选择器）；choices 的显示名 = 原值 `none/lzw/zip/ccittrle/packbits`（`:629-633`）与 `none/horizontal/float`（`:653-655`），`paramform.cpp:293-298` 只把 `auto` 翻成"自动"，其余原样显示。
- 事实：中文界面里出现"技术选择器"这一实现术语与英文枚举档位名；不影响功能。
- 影响：用户不知道 `horizontal`/`float` 是什么（tooltip 里有解释，但下拉项本身无中文）；"技术选择器"会让人找技术下拉。
- 建议：label 改为「压缩方案」；choices 显示名改中文（`无压缩/LZW/ZIP(Deflate)/CCITT RLE/PackBits`、`无/水平差分/浮点`），值不变（`ParamDef::choices` 的 first 即显示名，改动零风险）。
- 状态：confirmed；`known=no`。

#### 4. 输出兼容性与合规矩阵

工具集（本机可用）：OIIO 3.1.17（= libtiff 4.7.2）、Exiv2 0.28.9、`pp_verify`（Exiv2 + OIIO 解码）。Photoshop/GIMP/ImageMagick 不可用，未实测。

| 源/配置 | 产物结构（实测 tag） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| `big_rgb8.png` 512×384 → tiff / lzw / 16 bit（默认） | II，1 IFD，RGB，`BitsPerSample=[16,16,16]`，`Compression=5`，`Predictor=2`，`RowsPerStrip=32`，12 strips，`SampleFormat=[1,1,1]`，无 ICC，**有** XMP(383B)+`DateTime=now`，`Orientation=1` | OIIO/Exiv2 正常；`pp_verify` exact **OK** | 时间戳非源值、XMP 日期非 ISO 8601（TIFF-07）；`ExtraSamples` 不适用 |
| 同上 → zip | `Compression=8`（ADOBE_DEFLATE）+`Predictor=2`，11085 B | OK；exact OK | 无（`Compression=8` 为最常见 deflate 变体） |
| 同上 → none / packbits | `Compression=1`（1.18 MB）/ `Compression=32773` | OK；exact OK | 无 |
| 同上 → ccittrle（8/16 bit、gray/RGB/RGBA 四种组合） | **无产物**（临时文件已清） | 不可用 | `Bits/sample must be 1`（TIFF-02） |
| 同上 → predictor=1/2 | `Predictor=1`（687122 B）/`=2`（50314 B） | OK；exact OK | 用户覆盖真实生效（R-011 闭合） |
| 同上 → predictor=3 | 无产物（前置校验失败） | 不可用 | 假选项（TIFF-05） |
| 同上 → tile 256×256 / 16×16 / 1024×1024 | `TileWidth/TileLength` 对应值，`RowsPerStrip` 缺席，tiles=4/768/1 | OK；exact OK | tile 大于图像合法（1 tile）；16×16 产生 768 tiles（体积 203 KB vs 50 KB） |
| `big_rgba8.png` → lzw（8/16 bit）、rgba8+tile | 4 samples；`ExtraSamples=[1]`；`Photometric=2` | 可读；exact OK | **alpha 语义错**：应为 `2`（TIFF-03） |
| `graya8.png` → 8/16 bit | 2 samples；`Photometric=1`(MINISBLACK)；`ExtraSamples=[1]` | 可读；exact OK | 同上（TIFF-03） |
| `gray8.png` → 8 bit / tile 64 | 1 sample；`Photometric=1`；无 `ExtraSamples`；tile=64 | OK；exact OK | 无 |
| `--color srgb`（8/16 bit） | `ICCProfile`=588 B（`acsp`，`RGB `→`XYZ `，v4.4，11 tags） | OK；exact OK | 无（ICC 由 `spec.attribute("ICCProfile")` 正确落 tag 34675） |
| `--color p3` + RGBA16 | ICC=580 B；4 samples；`ExtraSamples=[1]` | OK（PSNR 断言不适用，`exact` 必失败属预期） | alpha 语义（TIFF-03）；色彩转换不在本格式范围 |
| 16 bit TIFF/PNG 源 → 16 bit | `BitsPerSample=[16,16,16]`，2 strips | OK；exact OK | 无 |
| 多页 TIFF 2 页 → tiff | **2 IFD**：页 1 = 重编码（lzw/16）；页 2 = 源第 2 页**逐字节拷贝**（32×32、8 bit、deflate，`Exif.Thumbnail.*` 存在） | 首末页都能读 | 违反"多页取首页"；夹带未经处理的旧页（TIFF-01） |
| 多页 TIFF 3 页 → tiff | **3 IFD**；第 3 页 `StripOffsets=[2771273,8]` 越界 | 第 3 页 **不可解码**（OIIO `Read error at scanline 4294967295`；Exiv2 `Strip 0 is outside of the data area`） | **结构损坏**（TIFF-01，P0） |
| 源 `Orientation=6` 的 TIFF（oiiotool 生成）→ tiff | 像素已旋转（512×384→384×512）、`Orientation=1`、ExifIFD 保留、XMP 2400 B 复制 | OK | 无（无双重旋转） |
| `cmyk.tif`（PHOTOMETRIC_SEPARATED） | 不产出（probe 阶段 `CMYK input is not supported`） | — | 符合"不做 CMYK"共识 |
| **仅元数据模式**：strip 8/16 bit、tile 256（lzw/zip+pred1）、2 页多页 | IFD/压缩/tile 结构全不变；strip/tile 载荷 sha **逐字节相同**；仅新增 tag 315(Artist)；2 页仍 2 页 | `pp_verify` exact + Artist 断言 **OK** | 无（仅元数据模式的 strip/tile 保真成立） |

#### 5. 未覆盖与存疑

**未覆盖（明确未做）**
1. **BigTIFF（>4 GB 未压缩）**未实测：OIIO 在 `image_bytes() > 4000 MiB` 时自动切 BigTIFF（`tiffoutput.cpp:510-513`），PP 无 `tiff:bigtiff` 开关也无提示 ⇒ 超大图产物可能变成部分旧读者不认的 BigTIFF（未取证，仅代码事实）。
2. **第三方看图器**（Photoshop/GIMP/Krita/ImageMagick/exiftool）全部不可用，兼容性结论只覆盖 OIIO/libtiff 4.7.2 + Exiv2 0.28.9；TIFF-03 的实际渲染后果（亮边）是规范推论 + 代码事实，未在商业软件里截图验证。
3. **GUI 实机未跑**：按要求未运行 GUI/ui-smoke。UI 面结论来自代码阅读（`paramform.cpp` / `page_output.cpp` / `output_page_build.cpp`）+ `smoke_probes.cpp:6547-6562` 已冻结的可见性断言 + CLI 侧同一参数引擎的实测。
4. **1 位（fax）TIFF 源**未测：理论上会被解码成 gray 再输出 8/16 位，`ccittrle` 仍不可用（因为没有 1 bps 输出路径）。
5. **JPEG-in-TIFF / LZMA / ZSTD 压缩的 TIFF 源**未测（OIIO 输出名表不含这些档，`tiffoutput.cpp:303-346`）。
6. **16 bit + tile + ICC + predictor=1 的全叠加**未单独测（只测了两两组合）。
7. **DPI/ResolutionUnit**：未构造带 DPI 的源，无法判断 `XResolution/YResolution` 是否经 Exiv2 后写保留（OIIO 侧不写，`put_parameter` 分支存在但 PP 不传）。
8. **3 页以上多页源的更深层机制**（Exiv2 是把它当 thumbnail 还是 2 号目录）只验证到"现象 + 第 3 页 IFD 越界 + Exif.Thumbnail 键存在"，未做 Exiv2 源码级定因。

**存疑**
1. **TIFF-04 的根因**：现象确定（predictor=none 时级别无效，oiiotool 也复现），但"为什么 predictor=horizontal 时级别又生效"没有在 libtiff 源码层定论。可疑链：`tiffoutput.cpp:702-711` 在 `:819` 之前设 `ZIPQUALITY`（此时 ZIP 编解码器尚未安装，伪标签可能未被接受），而 predictor 包装器的存在改变了后续 setup 路径。建议上游 issue，不建议本项目盲改。
2. **TIFF-01 的修复归属**：现象在 TIFF 容器上，但写入者是元数据后写阶段（`write_metadata_exiv2`）。若元数据代理另有"多页/缩略图策略"设计，需与格式侧合并裁定（本审计只登记"TIFF 产物不得带源额外 IFD"这一格式侧不变量）。
3. **TIFF-07 的"正确时间值"**：PP 的生效时间语义由元数据页的"原→生效"决定（`DateTimeOriginal` / 文件 mtime / 规则），编码器侧拿不到统一结论；若要修，需要主对话先定"TIFF 的 tag 306 应该写什么"。

**实测命令（可复核；`ROOT=C:/Data/Code/PhotoPipeline3`）**
```
# 参数对比（示例）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/tiff/inputs/big_rgb8.png \
  --out build/encoder-audit/tiff/cases/zip --format tiff --workers 1 --param compression=zip --param deflate_level=9
# 像素逐位断言（输入需复制为 <root>/input.png，expected.json 在 <root>/case/）
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/tiff/verify/<case>/case/expected.json <产物>
# 结构读取（纯 Python，无 PIL/numpy）
python build/encoder-audit/tiff/summary.py
python build/encoder-audit/tiff/cmpmetaonly.py        # 仅元数据模式 strip/tile 载荷 sha 对比
# 库侧隔离（不经本项目）
python tools/env.py run -- vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe \
  build/encoder-audit/tiff/inputs/big_rgb8.png --compression zip:9 --attrib tiff:Predictor 1 -o build/encoder-audit/tiff/oiio/x.tif
```
探针脚本：`build/encoder-audit/tiff/{runcases.py,runverify.py,runmeta.py,runmetaonly.py,runpreset.py,runbadpreset.py,tiffdump.py,summary.py,cmpmetaonly.py}`。

### 4.5 WebP（libwebp / Lossy + Lossless）

> 审计对象：`src/codecs/enc_webp.cpp`（356 行）＋ `src/core/format_tables.cpp` 的 webp 行（`690-861`）＋ 参数引擎 / UI / 下发链 / 元数据链。
> 运行时基线：`build/release-dev/photopipeline.exe`（PhotoPipeline 0.3.5.alpha2，PP_BUILD_DEV=ON）、libwebp 1.6.0（vcpkg `libwebp 1.6.0-bbdfbfe91d`，DLL：`build/release-dev/libwebp.dll`）。
> 实测产物与脚本全部落在 `build/encoder-audit/webp/`（gitignored）。审计为只读：未改 `src/`、`tests/`、`docs/`、CMake。

#### 1. 结论

1. **参数面本身是"真表驱动"且 24 个键全部有读取点**，没有"接受但完全不读"的声明内参数；容器层（VP8X/ICCP/EXIF/XMP、VP8/VP8L/ALPH）结构合法，ICC 经"编码器写 → Exiv2 后写"两跳后逐字节保真，仅元数据模式对 WebP 是逐位保真的（实测 `VERIFY OK`）。
2. **"无损"这一件事在 WebP 上有三个独立承载面（技术下拉 / schema 参数 `lossless` / 内部键 `__lossless`），而编码器只认技术下拉**：`enc_webp.cpp:152-155` 里 `tech_id` 优先。于是两种自相矛盾都能真实产出：`--lossless --tech lossy` 得到**有损产物但所有记录都说 `lossless=true`**（P0，`pp_verify` exact FAIL），`--tech lossless`（不勾无损）得到**逐位无损产物但记录都说 `lossless=false`**（P1）。
3. **`preset`（内容预设）是纯装饰控件**：`WebPConfigPreset` 改写的 5 个字段（sns/filter_strength/filter_sharpness/preprocessing/segments）随后被 `enc_webp.cpp:173-192` 全部无条件覆盖，实测 `preset=0` 与 `preset=5` 与默认**逐字节相同**（`23C6609BFC874BF6`）。tooltip 里"photo：sns 80、filter 30、sharpness 3"从未发生。
4. **`near_lossless` 是双向不可靠**：默认 `method=4` 下 24 组样本全部**字节不变**（静默空转）；只有落到 libwebp 的"纯 sub-green"变换（`vp8l_enc.c:1572-1573` 要求 `!use_palette && !use_predict`）才生效，一旦生效产物**非逐位无损**（实测 `104388/110592` 样本不同）却仍报 `lossless=true` 且无任何告警（P1）。
5. **两个高级旋钮在默认参数下被库静默作废**：`partitions`（`method≥3 && low_memory=false` ⇒ `num_parts` 被强制为 1，`webp_enc.c:119-126`）与 `preprocessing` 的位 2/位 4（`sharp_yuv=true` 走 sharp 转换，抖动分支不可达，`webp_enc.c:356-372`）。三者的 tooltip 都没有说这个前置条件。
6. **灰度/16bit/alpha 三条边界是干净的**：灰度源升 RGB 有 `GrayToRgbEncoded` 告警并嵌 sRGB ICC；16bit 源降 8bit 有 `DepthDowngrade` 告警、`--bitdepth 16` 直接显式报错（不静默降档）；RGBA 无损逐位保真（`VERIFY OK`）。
7. **参数面缺口（P3）**：库支持但未暴露 `image_hint` / `target_size` / `target_PSNR`；lossy `quality` 下界被收窄成 1（库允许 0，实测 `quality=0` 被参数引擎拒绝）。
8. **证据面缺口**：侧车 `.pp.json` 的 `tech` 在默认单格式路径下是空串，且没有"有效无损"字段——产物到底有损无损，只能靠人读 `params`+`tech` 反推，这恰好是 P0-01 能被"看起来正常"吞掉的原因。

---

#### 2. 参数全量矩阵

> `读取点` = 参数值被编码器消费的位置（`src/` 相对路径）。`生效` 依据第 5 节实测（同一输入、只改该参数、比较 SHA256）：**是**=字节改变；**条件**=依赖其它参数/内容；**否**=字节完全不变。
> 全部 27 条 ParamDef 声明（有损技术 22 条 + 无损技术 5 条；`quality`/`method`/`lossless` 各在两技术重复一次）⇒ **24 个唯一键**。本表把 `lossless`/`method` 合并成一行、`quality` 按两义拆成两行 ⇒ 25 行。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `lossless` | 是（顶部「无损编码」复选框，`pp-lossless-check`；**无行**，`paramform.cpp:797` 显式跳过） | false（两个技术都声明） | bool | `enc_webp.cpp:152-155`（经 `pp::lossless_flag`，两键单源 `params.cpp:307-310`） | 条件 | `tech_id` 优先于本参数：`tech=lossy` + `lossless=true` ⇒ **有损**（P0-01）；`tech=lossless` + `lossless=false` ⇒ **无损**（P1-02）。`apply_locks` 是两键唯一同步点（`params.cpp:495-507`） |
| `quality`（lossy 面） | 是（核心，「质量」） | 90 | int 1–100（库 0–100，`encode.h:99-103`） | `enc_webp.cpp:169`（读）、`:173`（写 cfg） | 是 | 实测 50→2622B / 90→14872B / 95→21764B / 100→29058B。下界 1 把库允许的 0 档关在门外（P3-12） |
| `quality`（lossless 面） | 是（核心，「努力（借 quality 参数）」） | 80 | int 0–100 | `enc_webp.cpp:195` | 是 | 无损语义 = 压缩努力（`encode.h:99-103`）：实测 0→19036B、80→10880B、100→10468B。**一键两义**（R-209 已知，tooltip 已标） |
| `sharp_yuv` | 是（核心） | true（库 0） | bool | `enc_webp.cpp:192` | 是 | 实测 false→7508B（真实生效）。副作用：它是 `preprocessing` 位 2/位 4 失效的直接原因（P2-07） |
| `method` | 是（高级/编码工具） | 4 | int 0–6 | `enc_webp.cpp:174`（lossy）/`:196`（lossless） | 是 | 实测 0→15988B、4→14872B、6→13942B；同时决定 `rd_opt_level` 与 `use_tokens`，进而**关掉 `partitions`**（P2-06） |
| `preset` | 是（高级/编码工具） | photo(2) | enum default/picture/photo/drawing/icon/text = 0–5 | `enc_webp.cpp:165`（`WebPConfigPreset`，`:170`） | **否** | 预设改写的 5 字段随后被 `:174-181,:187` 无条件覆盖；实测 0/2/5 三者字节相同（P1-03） |
| `sns_strength` | 是（高级/编码工具） | 50 | int 0–100 | `enc_webp.cpp:175` | 是 | 实测 0→15826B |
| `filter_strength` | 是（高级/编码工具） | 30（库 60） | int 0–100 | `enc_webp.cpp:176` | 是 | 实测 0→14868B；tooltip 称 30 是"preset=photo 生效值"，但预设不生效（P3-10） |
| `autofilter` | 是（高级/编码工具） | false | bool | `enc_webp.cpp:179` | 是 | 实测 true→14868B；与 `filter_strength` 同为 `use_filter` 判据（`webp_enc.c:151-152`） |
| `pass` | 是（高级/编码工具） | 1 | int 1–10 | `enc_webp.cpp:180` | 是 | 实测 3→14726B |
| `filter_sharpness` | 是（高级/编码工具） | 0 | int 0–7 | `enc_webp.cpp:177` | 是 | 实测 7→同体积但哈希不同（`4707F937C071B153`） |
| `filter_type` | 是（高级/编码工具） | true(strong) | bool | `enc_webp.cpp:178` | 是 | 实测 false→哈希不同（`70907D9BDBB53D67`）；仅 `filter_strength>0 \|\| autofilter` 时有意义（`encode.h:116-117`，tooltip 已写） |
| `segments` | 是（高级/编码工具） | 4 | int 1–4 | `enc_webp.cpp:181` | 是 | 实测 1→17882B |
| `alpha_compression` | 是（高级/编码工具） | true | bool | `enc_webp.cpp:182` | 是 | 实测 false→74858B（8 倍）；**仅 alpha 源有作用**，无 alpha 时不改变任何字节（库侧 `alpha_enc.c:385`） |
| `alpha_filtering` | 是（高级/编码工具） | 1(fast) | int 0–2 | `enc_webp.cpp:183` | 条件 | 0/1/2 在本样本上字节完全相同（`23B0D3AE60E66AF0`）：库把三值映射为**候选过滤器集合**（`alpha_enc.c:209-233,381-384`），FAST 与 BEST 的候选集是包含关系 ⇒ 常收敛到同一结果。样本退化，不能判定为"无效" |
| `alpha_quality` | 是（高级/编码工具） | 100 | int 0–100 | `enc_webp.cpp:184` | 是 | 实测 50→9668B；100 以下会量化 alpha（`alpha_enc.c:312,343-349`） |
| `partitions` | 是（高级/编码工具） | 0 | int 0–3 | `enc_webp.cpp:185`；库侧 `webp_enc.c:213` | 条件（默认参数下=否） | `webp_enc.c:119-126`：`!low_memory && use_tokens`（`method≥3`）⇒ `num_parts=1`，`config->partitions` 被丢弃。实测 3→字节不变；3+`low_memory=true`→14832B；3+`method=2`→15062B（P2-06） |
| `partition_limit` | 是（高级/编码工具） | 0 | int 0–100 | `enc_webp.cpp:186` | 是 | 实测 100→13798B |
| `preprocessing` | 是（高级/编码工具） | 0 | int 0–7（位组合） | `enc_webp.cpp:187` | 条件 | 位 1（segment-smooth）有效：1→15016B；位 2（抖动）在 `sharp_yuv=true` 下不可达；位 4 与 `sharp_yuv` 同义。实测 2/4→字节不变，`sharp_yuv=false`+2→7626B（P2-07） |
| `qmin` | 是（高级/编码工具） | 0 | int 0–100 | `enc_webp.cpp:188` | 条件 | q=90 时量化因子不触底，实测 20→字节不变（预期）；与 `qmax` 有跨字段规则（`params.cpp:264-275`，`qmin=90,qmax=10` 实测被拒） |
| `qmax` | 是（高级/编码工具） | 100 | int 0–100 | `enc_webp.cpp:189` | 是 | 实测 50→2622B（等价于压低质量） |
| `emulate_jpeg_size` | 是（高级/编码工具） | false | bool | `enc_webp.cpp:190` | 是 | 实测 true→18418B |
| `low_memory` | 是（高级/编码工具） | false | bool | `enc_webp.cpp:191` | 是 | 实测 true→14796B（库内改变 token 缓冲策略，顺带让 `partitions` 复活） |
| `exact`（lossless 技术） | 条件（`visible=is_lossless`，`format_tables.cpp:846`） | true（库 0） | bool | `enc_webp.cpp:197` | 条件 | 实测 RGBA 无损 true→54182B / false→54666B，`pp_verify` exact FAIL（665/262144 样本=透明区 RGB）。**lossy 技术没有本参数行**，编码器 lossy 分支也不赋值 ⇒ lossy 恒 `exact=0`（P2-08） |
| `near_lossless`（lossless 技术） | 是（高级/编码工具，无可见性谓词） | 100（关） | int 0–100 | `enc_webp.cpp:198` | 条件 | 默认 `method=4` 下 24 组实测**全部字节不变**；`method=6`+`quality=100` 时生效（33044B vs 59546B）且产物非逐位无损（P1-04 / P2-05） |

**技术/格式级事实**（`format_tables.cpp:690-861`）：`bitdepths={8}`、`supports_alpha=true`、`supports_gray=false`、`meta_path="exiv2"`、`runtime_introspected=false`、单后端 `libwebp`、双技术 `lossy`（`lossless_capable=false`）/`lossless`（`true`）。

---

#### 3. 发现

##### WEBP-01 [P0] 显式选择 lossy 技术 + 无损标志 ⇒ 有损产物被全程标记为"无损"

> **主编归一（2026-10-25，独立复核后）**：本条按 **P1** 计入总表（见 7.0）——GUI 主路径会自愈（`paramform` 自动切技术），仅 `--dev`/手写预设/`--outputs` 可复现。


- 证据：
  - `src/codecs/enc_webp.cpp:152-155`
    ```cpp
    const bool lossless =
        (req.target.tech_id == "lossless") ? true
            : ((req.target.tech_id == "lossy") ? false : lossless_flag(params));
    ```
  - `src/core/params.cpp:495-507`：`apply_locks()` 无条件把 `__lossless` 与 schema 参数 `lossless` 写成入参值（不检查技术是否可无损）。
  - `src/core/pipeline.cpp:1739-1767`（`build_output_specs`）与 `:486-492`（`plan_targets` 逐字段拷贝）都不校验"`lossless=true` ⇒ 技术 `lossless_capable`"。
  - `src/dev_cli.cpp:919`：CLI 头行用**全局** `lossless` 打印 `tech=%s lossless=%s`，与产物真实模式无关。
- 事实（实测，输入 256×256 RGB8 渐变+高频纹理，`--workers 1`）：
  - `--format webp --tech lossy --lossless` ⇒ 产物 `14872B / sha 23C6609BFC874BF6`，**与不加 `--lossless` 的默认有损产物逐字节相同**；CLI 头行打印 `tech=lossy lossless=true`，侧车 `params` 含 `lossless=true`，日志无 warning。
  - `pp_verify` 逐位断言：`VERIFY 04-lossy-with-lossless-flag FAIL exact mismatch: 179225/196608 samples differ`（与纯有损用例的失配数完全一致）。
  - 反向的等价形态：多输出 `--outputs webp:libwebp:lossy --lossless` 同样把 `lossless=true` 灌进 lossy 技术（`dev_cli.cpp:721-723,802`）。
- 影响：产物是有损 VP8，但参数快照、预设、侧车、CLI 摘要、pp_verify 的期望面全部声明"无损"。任何以"`lossless=true`"为判据的批处理复核（含本仓自己的金样/断言口径）都会把有损文件当无损放行；若该参数集被存成预设再分发，错误会跨会话传播。用户不可见（GUI 载入此类预设时会被 `paramform.cpp:1328-1336` 静默改技术，反而不复现），但 `--dev` 与预设 JSON 是项目自己的一等入口。
- 建议：
  1. `pipeline.cpp` 的 `build_output_specs`（`:1753` 之后）与 `plan_targets`（`:486` 之前）加硬校验：`if (lossless_flag(params) && !tech->lossless_capable) return fmt + ": lossless requested on a lossy-only tech '"+tech->id+"'";`。
  2. `enc_webp.cpp` 兜底：进入 lossy 分支前若 `lossless_flag(params)` 为真（且 `tech_id` 非空为 `"lossy"`）直接 `encode_error`，而不是静默取 tech。
  3. `dev_cli.cpp:919` 改用 `pp::lossless_flag(specs[0].params)` 打印"有效无损"，避免头行自证矛盾。
- 状态：confirmed；known=no（`docs/deep-review-0.3.2.md` 未命中；相关但不同：R-209 只讲 quality 一键两义）

##### WEBP-02 [P1] 选 Lossless 技术但不勾"无损" ⇒ 逐位无损产物，但复选框/侧车/日志都报"非无损"

- 证据：
  - `src/ui/paramform.cpp:1268-1282`（`user_set_tech`）只拒绝"无损开启时选非无损技术"，反向不处理：`sel.lossless` 保持 false 而 `sel.tech` 变成 `"lossless"`。
  - `src/ui/paramform.cpp:1284-1312`（`user_set_lossless`）：当技术本身已是 lossless 时 `memo.clear()`（`:1299-1301`），之后取消勾选走 `:1305-1307` 分支**保持技术不变** ⇒ GUI 可稳定复现"未勾无损 + Lossless 技术"。
  - `src/ui/page_output.cpp:706-737`（`spec_of`）把 `tech_id` 与 `__lossless` 一起下发；`enc_webp.cpp:152-155` 让 `tech_id` 赢。
- 事实（实测）：
  - `--format webp --tech lossless`（**不带** `--lossless`）⇒ `10880B / sha BADF6F9B7D936B40`，与 `--lossless` 的产物**逐字节相同**；侧车 `tech="lossless"`、`params` 含 `lossless=false`；CLI 头行 `tech=lossless lossless=false`。
  - `pp_verify` 逐位断言：`VERIFY 01-tech-lossless-noflag OK`（16bit 量化后逐位一致）。
- 影响：用户看到"无损编码"未勾选、日志写 `lossless=false`，实际拿到的是无损文件（本次样本 10880B vs 有损 14872B —— 无损反而更小，因为合成图更利于 VP8L；照片内容通常相反，无损会大数倍。无论哪种，"体积/画质预期"都建立在错误标志上）。与 WEBP-01 同根（三个承载面），但方向不同：这里多花了 CPU/可能多花体积，那里少了画质，共同后果是"无损标志不可信"。预设/侧车往返会把 `lossless=false` 固化，后续任何人按该字段判断都会错。
- 建议：把"无损"收敛成单一真源，二选一：
  - A（推荐）：让技术下拉驱动标志——选中 `lossless_capable` 技术时自动置 `sel.lossless=true`（`paramform.cpp:user_set_tech`），并同步 `apply_locks`；
  - B：保留复选框为唯一真源，禁止在 `lossless=false` 时停留在 lossless 技术（`user_set_tech` 拒绝 + 载入预设时归一到 lossy 技术）。
  两条都要在 `page_output.cpp:spec_of` 里断言 `tech_id` 与 `lossless` 一致，否则 UI 与产物仍会各说各话。
- 状态：confirmed；known=no

##### WEBP-03 [P1] `preset`（内容预设）对产物零影响——六个枚举值全都产出同一份字节

- 证据：
  - `src/codecs/enc_webp.cpp:163-192`：先 `WebPConfigPreset(&cfg, preset, quality)`（`:170`），紧接着**无条件**覆盖 `quality/method/sns_strength/filter_strength/filter_sharpness/filter_type/autofilter/pass/segments/...preprocessing/...`。
  - libwebp 侧预设只改 5 个字段（`vcpkg/buildtrees/libwebp/src/v1.6.0-bbdfbfe91d.clean/src/enc/config_enc.c:64-96`）：`sns_strength` / `filter_sharpness` / `filter_strength` / `preprocessing` / `segments` —— **全部**在覆盖清单里。
  - `src/core/format_tables.cpp:714-717` 的 tooltip 却承诺"photo：sns 80、filter 30、sharpness 3"。
- 事实（实测）：
  - `preset=0`（default）⇒ `14872B / 23C6609BFC874BF6`；`preset=5`（text）⇒ **完全相同**；`preset=2`（表默认）⇒ 完全相同。三个值与不带 `--param preset` 的默认产物三者同一 sha。
  - 同批实测证明"不是样本太简单"：`sns_strength=0`（preset 本应改的字段之一）立刻改变字节（`15826B / 5619764D2D8F0B5C`）。
- 影响：`内容预设` 是本格式唯一一个"选了完全没用"的控件。用户选 `text`/`icon`（期望保留锐度：库内 `sns=0, filter_strength=0, segments=2`）得到的其实是 `sns=50, filter=30, segments=4` 的默认档；反过来选 `photo` 也不会把 sns 提到 80。tooltip 还替它作证，属于"UI 文案承诺超出实现"。
- 建议：三选一（按代价排序）：
  1. **删行**（`format_tables.cpp:705-718`）：WebP 的"内容预设"在参数全暴露的模型下没有存在意义，且无法与手调共存；
  2. 让预设真正生效：把 `preset` 从"独立参数"改成"默认值来源"（`default_params` 里按 preset 生成那 5 个键的 def），UI 上把 preset 与那 5 个旋钮做联动（改 preset 即重填，手改即把 preset 置"自定义"）；
  3. 若坚持保留现状，至少把 tooltip 改成"当前实现下不影响产物（已知缺陷）"，别让文案继续承诺。
- 状态：confirmed；known=no（`R-209` 只处理同文件镜像去重与 quality 两义，未涉及预设覆盖顺序）

##### WEBP-04 [P1] `near_lossless<100` 一旦生效，"无损编码"就是假的：产物非逐位无损、无告警、侧车仍 `lossless=true`

- 证据：
  - `src/codecs/enc_webp.cpp:193-199`（lossless 分支）：`cfg.near_lossless = param_int(params, "near_lossless", 100);` —— 无任何检查/告警。
  - libwebp：`vp8l_enc.c:1570-1584` 在 `near_lossless<100` 时用 `VP8ApplyNearLossless()` 改写 `enc->argb`；`near_lossless_enc.c:113-147` 按 `limit_bits = 5 - quality/20` 逐级量化；`encode.h:143-144` 明写 `[0 = max loss .. 100 = off]`。
  - `src/core/format_tables.cpp:848-855` 的 tooltip **已如实说明**"<100 时…输出非严格无损"，但 UI 没有第二处提示（`src/ui` 全目录 grep `near_lossless`/`近无损` 命中 0 处），pipeline 也没有对应 `WarningKind`。
- 事实（实测，192×192 RGB 相关噪声图 `near-e.png`）：
  - `--lossless --param method=6 --param quality=100 --param near_lossless=0` ⇒ `33044B`；同参数 `near_lossless=100` ⇒ `59546B`。
  - `pp_verify` 逐位断言：`near_lossless=0` → `VERIFY ... FAIL exact mismatch: 104388/110592 samples differ`；`near_lossless=100` → `OK`。
  - 该用例的侧车：`params=[exact=true lossless=true method=6 near_lossless=0 quality=100]`，`warnings=[]`（空）。即"勾着无损 + 零告警 + 产物有损"。
- 影响：这是 P0 定义里的"静默把有损当无损"，只差一个前置条件（需用户显式把 `near_lossless` 调离 100，并落到会触发预处理的 `method/quality` 组合——见 WEBP-05）。后果与 WEBP-01 相同：无损承诺不可验证，且这次连"技术不匹配"这个线索都没有，`tech=lossless` + `lossless=true` 一切"自洽"。
- 建议：
  1. 新增 `WarningKind`（如 `NearLosslessApplied`），在 `enc_webp.cpp` lossless 分支 `near_lossless<100` 时通过 `EncodeResult::warnings` 上报；`pipeline` 已有的 `merge_warnings`（`pipeline.cpp:1069`）会自动把它送进侧车与 UI 结果列。
  2. UI 侧在 `near_lossless != 100` 时给行级警示（`paramform.cpp` 已有 `ppInvalid`/`note` 通道可用），或把"无损编码"复选框文案改成"无损（近无损强度≠100 时除外）"。
  3. 更彻底：把 `near_lossless` 当作独立技术（`near-lossless`）暴露，使"无损"标志只对应真无损。
- 状态：confirmed；known=no

##### WEBP-05 [P2] `near_lossless` 在默认参数下静默空转（24 组实测字节全等）

- 证据：
  - `vp8l_enc.c:1570-1573`：`use_near_lossless = (config->near_lossless < 100) && !enc->use_palette && !enc->use_predict;` —— 必须落在"纯 sub-green"变换上。
  - `vp8l_enc.c:389-404`：`method` 非 6 或 `quality<100` 时 `crunch_configs_size=1`，唯一的候选来自 `AnalyzeEntropy()` 的 `min_entropy_ix`（`vp8l_enc.c:215-221`，`kSpatial/kSpatialSubGreen/kPalette` 都带 `use_predict`/`use_palette`）。
  - `near_lossless_enc.c:128-138`：两维都 <64 或高 <3 的小图直接跳过预处理。
  - 编译面已确认不是 stub：`vcpkg/buildtrees/libwebp/x64-windows-avx2-rel/src/webp/config.h` 有 `#define WEBP_NEAR_LOSSLESS 1`，release `libwebp.pdb` 里能检索到 `VP8ApplyNearLossless`，`VP8LNearLosslessStub` 不存在。
- 事实（实测）：4 张内容迥异的 256×256 图（相关色+噪声、渐变、纯噪声、量化带）× `method∈{1,2,3,4,5,6}` × `near_lossless∈{100,50,0}` ＝ **24 组全部同 sha**；另加 RGB8/RGBA8 默认档 6 组亦全同。唯一找到的生效组合是 `method=6 & quality=100`（WEBP-04）。
- 影响：用户在高级区调"近无损强度"时，绝大多数配置下**一个字节都不会变**，且没有任何提示；而唯一会变的组合恰好是"无损变有损"。参数既不可预期也不可验证。
- 建议：tooltip 里补前置条件（`method=6 && quality=100` 才可能生效），或干脆把该参数锁到"生效组合"下（`locked` 谓词读 `method/quality`，不满足时禁用并给原因——项目已有"不静默灰"的 tooltip 惯例，`paramform.cpp:947-957`）。另建议在文档/README 注明小图（<64px 双向）不生效。
- 状态：confirmed（24 组 n100/n50 对照 + 16 次补充运行全部字节相同）；机制归因 suspected（`!use_predict` 门控是唯一能解释全部观测的读法，但未逐图反查 `min_entropy_ix` 实际取值）

##### WEBP-06 [P2] `partitions` 在默认参数下被 libwebp 强制回 1（参数被静默丢弃）

- 证据：
  - `webp_enc.c:213`：`enc->num_parts = 1 << config->partitions;`
  - `webp_enc.c:119-126`：
    ```c
    if (!config->low_memory) {
      enc->use_tokens = (enc->rd_opt_level >= RD_OPT_BASIC);  // need rd stats
      if (enc->use_tokens) { enc->num_parts = 1; }            // doesn't work with multi-partition
    }
    ```
    而 `rd_opt_level` 由 `method` 决定（`:104-107`，`method>=3` ⇒ `RD_OPT_BASIC`）⇒ 默认 `method=4 && low_memory=false` 时 `config->partitions` 被写死成 1。
  - `src/core/format_tables.cpp:788-793` tooltip 只写"log2 值 [0..3]；库默认 0（更易渐进解码）"，未提该前置条件。
- 事实（实测，同输入同其余参数）：`partitions=3` ⇒ `14872B / 23C6609BFC874BF6`（**与默认完全相同**）；`partitions=3 + low_memory=true` ⇒ `14832B / 735085C1CB359888`；`partitions=3 + method=2` ⇒ `15062B / 1E68D51BD6F3C7F7`。两个"解锁"方向都与代码门控一一对应。
- 影响：`Token 分区数 log2` 这一行在默认配置下是死的；用户以为自己在换取渐进解码能力/体积，实际什么也没发生。属于"读取后被 clamp/覆盖"的静默降级。
- 建议：给该参数加 `locked` 谓词（`method<3 || low_memory` 才可编辑，否则强制 1 并置灰 + 原因 tooltip），或在 tooltip 明写"`method≥3` 且未开低内存时库会忽略本项"。
- 状态：confirmed；known=no

##### WEBP-07 [P2] `preprocessing` 的位 2（抖动）与位 4 在默认 `sharp_yuv=true` 下不可达

- 证据：
  - `webp_enc.c:351-373`：`if (config->use_sharp_yuv || (config->preprocessing & 4)) { WebPPictureSharpARGBToYUVA(...) } else { if (config->preprocessing & 2) {...dithering...} WebPPictureARGBToYUVADithered(...) }` —— `sharp_yuv=1` 时 `preprocessing&2` 的抖动分支永远进不去；位 4 与 `sharp_yuv` 走同一条路径。
  - 唯一有效的位 1（segment-smooth）在 `analysis_enc.c:215`：`const int smooth = (enc->config->preprocessing & 1);`。
  - `src/core/format_tables.cpp:801-807` 的 tooltip 把参数描述成"0=none、1=segment-smooth、2=伪随机抖动，按位组合至 7"，未提与 `sharp_yuv` 的互斥。
- 事实（实测）：`preprocessing=2` ⇒ 字节不变；`preprocessing=4` ⇒ 字节不变；`preprocessing=1` ⇒ `15016B / E57B7AD9397BB1E0`（有效）；`preprocessing=2 + sharp_yuv=false` ⇒ `7626B / 86406C67385927D3`（抖动复活，同时证明根因）；`preprocessing=4 + sharp_yuv=false` ⇒ `14872B / 23C6609BFC874BF6`（与 `sharp_yuv=true` 逐字节相同，证明位 4 与 sharp_yuv 等价）。
- 影响：7 个取值里有 3 个（2/4/6）在默认配置下等于什么都不做；用户很可能为了"加抖动抗 banding"而打开位 2，结果纹丝不动。属于"UI 与产物矛盾 + 提示缺失"。
- 建议：在两处之一收口——(a) `format_tables.cpp:801` 改成两个 Bool（`preproc_smooth` / `preproc_dither`）并按 `sharp_yuv` 加 `locked`/`visible` 谓词；(b) tooltip 明写"位 2 仅在关闭锐利色度上采样时有效；位 4 等价于开启 sharp_yuv"。
- 状态：confirmed；known=no

##### WEBP-08 [P2] lossy 路径无法保留透明区 RGB；`exact` 只被 lossless 分支读取

- 证据：
  - `src/codecs/enc_webp.cpp:193-199`：`cfg.exact` **只在** lossless 分支赋值；lossy 分支（`:162-192`）不赋值 ⇒ 保持 `WebPConfigInit` 的 0（`config_enc.c:55`）。
  - `webp_enc.c:375-377`（lossy 路径）：`if (!config->exact) { WebPCleanupTransparentArea(pic); }` ⇒ alpha=0 区域的 RGB 被改写成邻域值。
  - `src/core/format_tables.cpp:840-846`：`exact` 只声明在 `lossless` 技术里，且 `visible = is_lossless`。
- 事实（实测）：`--tech lossy --param exact=true`（RGBA8 源）⇒ 参数快照 `params` 里确实出现 `exact=true`（`42-exact-on-lossy`），但产物 `9386B / 23B0D3AE60E66AF0` 与默认有损**逐字节相同** ⇒ 值未到达 `cfg.exact`，透明区清理照旧发生。对照：无损分支 `exact=false` 与 `true` 字节不同（`54666` vs `54182`），`pp_verify` exact 在 `exact=false` 时 FAIL（665 个样本差异，全在 alpha=0 处）。
- 影响：有损 WebP 会在**用户不可见**的地方改动像素（透明区 RGB），且无开关、无告警——与本项目"像素搬运工，中间不加料"的原则相悖。数据后果有限（alpha=0 不可见），但若下游按 RGBA 做抠图/合成/差异比对，会看到"我没改它却变了"。同时"`exact` 只在无损可见"让用户以为这问题只与无损有关。
- 建议：在 lossy 技术里也声明 `exact`（默认 false 保持库行为，或默认 true 与无损面一致），并在 `enc_webp.cpp` 的 lossy 分支补 `cfg.exact = param_bool(params,"exact",false) ? 1 : 0;`；若决定不支持，则至少在 `--param` 层拒绝跨技术键（见 WEBP-09），不要"收下就扔"。
- 状态：confirmed（代码路径 + `exact` 在 lossy 技术下字节等价性已实测）；"透明区被改写"的幅度未量化，见 §5 存疑 3

##### WEBP-09 [P2] 跨技术参数被接受、写进侧车/预设、却永远不被读取（无告警）

- 证据：
  - `src/dev_cli.cpp:384-393`（`set_param`）→ `pp::lookup_param_def`（`params.cpp:378-390`）在命中技术里找不到时**回退到后端全部技术的并集**。
  - `src/core/params.cpp:534-542`（`validate_params`）只遍历"选中技术"的 `params`，跨技术键不参与校验；`cross_validate` 的"未知参数"规则（`params.cpp:592-610`）按**格式级**键并集判定 ⇒ 也放行。
  - `enc_webp.cpp:49-78` 的 `kKnownKeys` 同时含 `exact`/`near_lossless`（两个技术共用一张白名单），`warn_unknown_params`（`:145`）因此不会报。
- 事实（实测）：
  - `--tech lossy --param near_lossless=0` ⇒ `exit 0`、产物 `14872B / 23C6609BFC874BF6`（= 默认有损），侧车 `params` 里赫然有 `near_lossless=0`，`warnings=[]`。
  - `--tech lossy --param exact=true` ⇒ 同上（`9386B`，`exact=true` 在快照里）。
  - 反面正对照：真正不存在的键 `--param bogus_key=7` 会被拒（`未知参数：bogus_key`）。
- 影响：预设/侧车会携带"看着生效、其实丢弃"的键；跨技术切换（GUI 会重建参数集，但预设/CLI 不会）后残留键没有任何提示。这是"静默降级"里最难自查的一类：产物没错，但配置记录是假的。
- 建议：`validate_params` 增加"键必须属于选中技术"的检查（已有 `find_param(*t,key)` 可直接用），错误文案区分"未知参数"与"当前技术不支持该参数"；或在 `entropy` 层面让 `enc_webp` 对不在本技术白名单里的键发 `log_warn`（现在白名单是超集，才导致沉默）。
- 状态：confirmed；known=no（相邻已知项：`docs/deep-review-0.3.2.md` R-164「参数白名单三源并存」）

##### WEBP-10 [P3] 文档与 UI 文案漂移（预设"生效值"、参数计数 23 vs 24）

- 证据：
  - `docs/param-catalog.md:179`：`filter_strength | … | **30**（R-316 勘误：旧表写 20，实现 = preset=photo 生效值 30）` —— 预设不生效（WEBP-03），30 只是表默认值。
  - `docs/param-catalog.md:245` 汇总表"WebP | libwebp | 2 | **23**"；实际唯一键 24 个（§5.1 表 22 键 + §5.2 的 `exact`/`near_lossless`）。
  - `src/core/format_tables.cpp:714-717`（preset tooltip）与 `:729-732`（filter_strength tooltip"preset=photo 生效值"）同源同错。
  - `src/core/format_tables.cpp:848-855`（near_lossless tooltip）承诺"<100 时启用 VP8L 近无损预处理"，未写"默认 method 下不生效"。
- 事实：以上均为可 grep 的静态事实；参数计数由 `format_tables.cpp:690-861` 逐条点算（22 + 5 条声明，去重 24 个唯一键）。
- 影响：文档是本仓"参数语义真源"（AGENTS §8），漂移会被下一个评审者当事实引用；计数差 1 属打磨级。
- 建议：随 WEBP-03/05 的修改同步修订 catalog 与 tooltip；计数改成 24（或改成"22+2，按技术分别计"以免再漂）。
- 状态：confirmed；known=yes（计数漂移已在 `docs/ux-review-14.md:463`「WebP … 汇总口径与 catalog 漂移」登记；"preset=photo 生效值"这一条未登记）

##### WEBP-11 [P3] 库支持但完全未暴露的三个参数：`image_hint` / `target_size` / `target_PSNR`

- 证据：
  - `vcpkg_installed/x64-windows-avx2/include/webp/encode.h:106-111`：`image_hint`（lossless only）、`target_size`（"if non-zero, set the desired target size in bytes"）、`target_PSNR`（"minimal distortion … takes precedence over target_size"）。
  - 消费点：`vp8l_enc.c:1868`（`image_hint == WEBP_HINT_GRAPH` 决定初始缓冲）、`frame_enc.c:54-64` + `webp_enc.c:118`（`do_search` 双通道率控）。
  - `src/codecs/enc_webp.cpp:49-78` 的 `kKnownKeys` 与 `format_tables.cpp` 的 webp 行都没有这三个键；`enc_webp` 也从不赋值 `cfg.image_hint/target_size/target_PSNR`。
- 事实：`image_hint` 只影响初始 buffer 估算（性能，不改画质）；`target_size`/`target_PSNR` 是真正的两遍率控，且会覆盖 quality 语义（`encode.h:108-111`）。
- 影响：与本项目"全参数暴露"原则（AGENTS §1.2-3）有偏差；`target_size` 是 WebP 用户最常用的"压到指定体积"能力，缺失属于能力缺口（虽与"质量优先"定位有取舍）。`image_hint` 仅性能，可继续不暴露。
- 建议：若采纳，`target_size`/`target_PSNR` 需成对设计（互斥 + 与 `quality` 的优先级说明 + UI 联动），并在 catalog 单列一节说明"为何暂不暴露 `image_hint`"；若决定不暴露，把理由写进 `docs/param-catalog.md` 的"排除项"。
- 状态：confirmed（代码/头文件事实）；是否应暴露属产品取舍

##### WEBP-12 [P3] lossy `quality` 下界被收窄为 1，库允许的 0 档被参数引擎拒绝

- 证据：`src/core/format_tables.cpp:244-247`（lossy 面 `p.lo = 1; p.hi = 100;`）；库侧 `WebPValidateConfig` 只查 `quality<0 || >100`（`config_enc.c:102`），`encode.h:99-100` 写"0 gives the smallest size"。
- 事实（实测）：`--param quality=0` ⇒ `photopipeline --dev: webp: invalid parameters: param 'quality': value 0 out of range [1, 100]`，退出码 2。
- 影响：极小体积档（预览/缩略图场景）无法通过本工具表达；属"过度收窄=能力被隐藏"的轻微形态。
- 建议：lossy 面 `p.lo = 0`（并在 tooltip 注明 0 档画质极低）；同时确认 `WebPConfigPreset` 接受 0（`config_enc.c:102` 已确认）。
- 状态：confirmed；known=no

##### WEBP-13 [P3] 侧车缺"有效技术/有效无损"字段：产物语义无法自证

- 证据：`src/dev_cli.cpp:465-519`（`sidecar_json`）只输出 `format/backend/tech/params/…`，其中 `tech` 直接取 `row.tech_id`（默认单格式路径为空串）；无 `effective_lossless` 之类字段。
- 事实（实测）：`00-lossy-default`、`02-lossless` 等默认路径的侧车 `tech=""`；`04-lossy-lossflag` 的侧车 `tech="lossy"` 但 `params` 含 `lossless=true`；`01-tech-lossless` 的侧车 `tech="lossless"` 但 `params` 含 `lossless=false`。
- 影响：任何自动化复核（含本仓的 `pp_verify` 期望面、回归基线）都无法仅凭侧车判断"这份产物是不是无损"；WEBP-01/02 因此不会在任何门禁里暴露。
- 建议：`sidecar_json` 增加 `effective_tech`（`select_tech()` 解析结果）与 `effective_lossless`（编码器视角：`tech=="lossless" || (tech.empty() && lossless_flag)`），并把 `tech` 的空值归一成有效技术。所有格式共享同一改动，收益不止 WebP。
- 状态：confirmed；known=no

---

#### 4. 输出兼容性与合规矩阵

> 结构由纯 Python RIFF/VP8X/VP8/VP8L/ALPH 解析器（`build/encoder-audit/webp/riff.py`）逐 chunk 读出；`RIFF size + 8 == file size` 全部通过。
> "标准工具可读"以 libwebp 1.6.0 官方解码器为准（`pp_verify` 的 WebP 路径显式用 `WebPDecodeRGB/RGBA`，见 `tools/pp_verify.cpp:19-21,120-131`）。**未**用独立 `webpinfo`/`dwebp` 复核：vcpkg 端口把它们关了（`vcpkg/buildtrees/libwebp/x64-windows-avx2-rel/CMakeCache.txt`：`WEBP_BUILD_CWEBP/DWEBP/WEBPINFO:BOOL=OFF`）。

| 源/配置 | 产物结构（逐 chunk） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| rgb8 有损默认（q=90, 无 ICC/EXIF） | `RIFF/WEBP/VP8 ` (14872B) | 是（解码+PSNR 29.04dB） | 无 VP8X/ICCP：按 WebP 规范"无 ICCP ⇒ sRGB"，与 `color=keep` 且源无 ICC 一致；侧车有 `NoIccAssumeSrgb` 告警（用 `--color` 时） |
| rgb8 有损 + ICC + EXIF + XMP | `VP8X(0x2c: ICC/EXIF/XMP) → ICCP(580B) → VP8  → EXIF(38B) → XMP (2519B)` | 是 | 合规：VP8X 必须是首个 chunk（已满足）；flags 与实存 chunk 一一对应；EXIF 载荷以 `49492a00`（II*\0）开头，符合 Exif-in-WebP 的 TIFF 头约定 |
| rgb8 无损默认 | `RIFF/WEBP/VP8L` (10880B)，`VP8L` 签名 `0x2F`、version=0 | 是（`pp_verify` exact OK） | 合规：无额外 chunk 时不写 VP8X（规范允许），alpha 位与实际一致 |
| rgb8 无损 + ICC + EXIF + XMP | `VP8X(0x2c) → ICCP → VP8L → EXIF → XMP` (57658B) | 是 | 同上 |
| rgba8 有损（alpha_quality=100） | `VP8X(0x10 alpha) → ALPH(66B, 头 0x01=无损压缩) → VP8 ` (9386B) | 是（RGBA 解码成功） | 合规；alpha 平面默认 100 = 无损，与 `alpha_compression=true` 一致 |
| rgba8 无损 | `RIFF/WEBP/VP8L` (54182B)，`VP8L` 内 alpha 位 = 1 | 是（`pp_verify` exact **OK** ⇒ RGBA 全通道逐位一致） | 合规 |
| rgba8 无损 + `exact=false` | 同 VP8L 结构（54666B） | 是 | **语义风险**：alpha=0 处 RGB 被替换，`pp_verify` exact FAIL（665 样本）。tooltip 已披露，属用户显式选择 |
| rgb16 源 → 默认 8bit | `VP8 ` (690B) + 侧车 `DepthDowngrade` | 是 | 合规（WebP 只有 8bit）；`--bitdepth 16` 显式报错不降档 |
| gray8 源 → WebP | `VP8X(0x20 ICC) → ICCP(588B) → VP8 ` (7352B)，侧车 `GrayToRgbEncoded` | 是 | 合规：升 RGB 后嵌 sRGB ICC，色彩链自洽 |
| 仅元数据模式（webp→webp，改 Artist） | 与原文件同结构（`VP8X/ICCP/VP8 /EXIF/XMP`），18060→18066B | 是 | **良好**：`pp_verify` exact **OK**（像素零改动）+ ICCP 载荷 sha 前后一致（`bff80718b989f63a`）+ `Exif.Image.Artist` 写入成功 |
| `--tech lossless`（未勾无损） | `VP8L`（10880B） | 是 | 结构合法、产物无损，但**记录声明有损**（WEBP-02） |
| `--tech lossy --lossless` | `VP8 `（14872B，与默认可逐字节相同） | 是 | **记录不可信**：`params.lossless=true` 而产物有损（WEBP-01） |
| `near_lossless=0 + method=6 + quality=100` | `VP8L`（33044B） | 是（可解码） | **承诺破裂**：结构是"无损容器"，像素非逐位无损，侧车零告警（WEBP-04） |

合规小节：所有用例的 RIFF 长度、chunk padding（奇数长度补 1 字节）、VP8X 首位置、flags↔chunk 一致性均正确；未发现编码产物不可解码或容器损坏。唯一"合规级"问题是**语义标注**（无损/有损标志与产物不符），不是字节级容器问题。

---

#### 5. 未覆盖与存疑

**未覆盖**
1. **GUI 实操未跑**（只读审计，禁 GUI / `--ui-smoke`）。所有 UI 结论来自 `paramform.cpp` / `page_output.cpp` 的代码路径 + `--dev` 复现，未做渲染取证。
2. **独立标准工具未复核**：`webpinfo`/`dwebp`/`cwebp` 在 vcpkg 端口里被关闭（CMakeCache 见上），容器正确性依赖自写 RIFF 解析 + libwebp 解码；未做第三方解码器（浏览器/ImageMagick）交叉验证。
3. **动图/多页/超大图**：WebP 动图在负面清单外（不产出），未测；`WEBP_MAX_DIMENSION`（16383）边界未测；多页 TIFF 源取首页的行为由 pipeline 统一处理，未单独以 WebP 为目标复测。
4. **性能/体积系统测量未做**：只记录单次字节数与 `encode_ms`（256×256 合成图 16–52 ms），未做质量-体积曲线、未做 `method/pass/low_memory` 的耗时对比。`thread_level`（`enc_webp.cpp:207`，E>1→1）未做多 worker 实测（`--workers 1` 全程）。
5. **EXIF Orientation 未测**：审计用源图无 Orientation 标签；方向旋转走 pipeline（`rotate_orientation`），与 WebP 容器无关，未做"写回后方向是否仍被 EXIF 消费者识别"的取证。
6. **ICC 语义未校验**：只比对字节与 `mntr` 描述符（ICC header bytes 12-15），未用 lcms2 反查 profile 的 primaries/TRC 是否真是 P3/sRGB。
7. **`alpha_filtering` 有效性未定论**：三值同字节（样本退化），未构造出"FAST 与 BEST 必然不同"的 alpha 平面。
8. **`qmin` 未构造出必然生效的样本**（q=90 时量化因子未触底属预期，但未验证 `qmin` 在某组合下确实改变输出）。

**存疑**
1. `near_lossless` 生效的**精确判据**未定：本文只给出"`!use_palette && !use_predict`"这一读法 + "`method=6 & quality=100` 实测生效"的实证边界；未逐图反查 `AnalyzeEntropy` 的 `min_entropy_ix`，因此"哪类内容一定触发"仍属推断（confidence=suspected 的那部分）。
2. `near-c`（纯噪声）在 `method=6 & quality=100` 下 `near_lossless=0/100` 仍字节相同——若按"暴力枚举所有变换"的读法，`kSubGreen` 配置应当被尝试并因近无损而显著变小，但未观察到。可能是该内容下近无损反而增大体积而未胜出，也可能是另一层门控；未定位。
3. WEBP-08 中"透明区被改写"的**幅度**：代码路径确定（`webp_enc.c:375-377`），但有损本身不可逐位对比，未量化改写前后的 RGB 差异，故该条影响描述取保守口径。

**复现命令**（全部经 `python tools/env.py run --`，工作目录 = 仓库根）
```powershell
# 输入（纯 Python 生成，无 PIL/numpy）
python build/encoder-audit/webp/mkpng.py
python build/encoder-audit/webp/mk_near_candidates.py; python build/encoder-audit/webp/mk_near_e.py

# P0：有损产物 + lossless=true
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/webp/inputs/rgb8.png `
  --out build/encoder-audit/webp/runs/04-lossy-lossflag --format webp --tech lossy --lossless --workers 1
# P1-02：Lossless 技术但不勾无损（与 --lossless 产物逐字节相同）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/webp/inputs/rgb8.png `
  --out build/encoder-audit/webp/runs/01-tech-lossless --format webp --tech lossless --workers 1
# P1-03：preset 无效（三个 sha 相同）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/webp/inputs/rgb8.png `
  --out build/encoder-audit/webp/eff/preset-text --format webp --param preset=5 --workers 1
# P1-04：near_lossless 生效后非逐位无损
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/webp/inputs/near-e.png `
  --out build/encoder-audit/webp/nearhunt/x-near-e-m6-q100-n0 --format webp --lossless --workers 1 `
  --param method=6 --param quality=100 --param near_lossless=0

# 逐位断言（golden_root = expected.json 上两级）
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/webp/verify/04-lossy-with-lossless-flag/case/expected.json build/encoder-audit/webp/runs/04-lossy-lossflag/rgb8.webp
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/webp/verify/near-e-m6q100-nl0/case/expected.json build/encoder-audit/webp/nearhunt/x-near-e-m6-q100-n0/near-e.webp

# 批量矩阵 / 参数有效性 / PSNR / 容器解析
powershell -NoProfile -ExecutionPolicy Bypass -File build/encoder-audit/webp/run_matrix.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build/encoder-audit/webp/eff_sweep.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File build/encoder-audit/webp/verify_pixels.ps1
python build/encoder-audit/webp/riff.py build/encoder-audit/webp/runs/30-p3-meta/rgb8.webp
```

**实测证据落点**：`build/encoder-audit/webp/runs/`（逐用例产物 + 侧车 + stdout）、`build/encoder-audit/webp/eff/`（单参数对照）、`build/encoder-audit/webp/nearhunt/`（near_lossless 生效性搜寻）、`build/encoder-audit/webp/verify/`（pp_verify 用例）、`build/encoder-audit/webp/matrix.json`（汇总）。

### 4.6 BMP（OIIO / Raw）

> 范围：`format_id="bmp"`、`backend_id="oiio"`、`tech_id="raw"` 的全参数面（表内 0 项 + 格式级位深 + 全局色彩/拍平/抖动）与下发链
> （`page_output.spec_of → config_base → build_output_specs → plan_targets → enc_oiio bmp 分支 → OIIO bmpoutput.cpp`）。
> 只读审计：未改任何源码/测试/文档；临时脚本与产物全部在 `build/encoder-audit/bmp/` 下。
> 版本：0.3.5.alpha2（`build/release-dev`，PP_BUILD_DEV=ON）。

#### 1. 结论

1. **"零参数"属实**：`format_tables.cpp` 的 bmp 行 `techs.raw.params = {}`，编码器 `enc_oiio.cpp` 的 bmp 分支除位深外不读任何 `params`；
   OIIO `bmpoutput.cpp` 本身也只有 `oiio:dither` / 分辨率几个通用头字段，没有编码旋钮。**无参数格式确实无需参数**，与 `ux-review-14.md:464`「BMP 零参数，正确」一致。
2. **但格式级"位深"下拉是无效参数（P1）**：`24` 与 `8` 在编码器里都只是 `out_type = UINT8`，产物 bpp 完全由**通道数**决定
   （1 通道→8bpp 索引、3 通道→24bpp）。实测 `rgb8` 选 24 与选 8 的 BMP **SHA-256 完全相同**；`gray8` 选 24 实产 **8bpp**；
   `rgb8` 选 8 实产 **24bpp** —— 两个方向都与 UI/表格声明矛盾。
3. **16-bit 源在默认 24 下静默降到 8 bps（P1）**：降档告警判据 `pipeline.cpp:1042` 把 **src_bitdepth(bps)=16** 与
   **out_bitdepth(bpp)=24** 直接比大小，单源换算函数 `pp::bitdepth_bps()` 全仓**零消费者**。实测同一 16-bit 源：`--bitdepth 8` 报
   `DepthDowngrade`，默认 `--bitdepth 24` **不报**，而两者产物字节完全一致（同为 8 bps）。
4. **keep（默认）+ 非 sRGB 源 ICC = 唯一色彩标记被丢且无 ICC 告警（P1）**：BMP 无法承载 ICC（writer 事实），但
   `EmbedIccUnsupported` 只在 `color != KeepOriginal` 时发。实测 Adobe RGB (1998) 打标 PNG：`--color keep` 产物**像素与源码值 0/4096 差异**
   （仍是 AdobeRGB 编码）、文件内 **0 处 ICC 字节**、告警只有 `MetadataDropped`；而 `--color adobergb` 产**同一 SHA-256 的字节**却发 ICC 告警
   —— 自证告警条件写错（该行为已在 `m7r2-plan.md:93` / adjudications R2F-C1 登记，但登记的"keep 不虚报"只对无 ICC/sRGB 源成立）。
5. **`EmbedIccUnsupported` 在取证面显示为 `"Unknown"`（P2）**：`dev_cli.cpp:148-167` 的 `warning_name()` 没有该 case，
   `--dev` 摘要与 `<out>.pp.json` 都写 `"kind":"Unknown"`，BMP 是唯一会触发它的格式。
6. **UI 侧零前置提示 + 文案承诺超出实现（P2）**：UI 从不读 `supports_alpha/supports_gray/meta_path`（`src/ui` 全无命中），
   BMP 页签不提示"alpha 将被拍平 / EXIF-XMP-ICC 会被丢弃 / 最高 8 bps"；全局"色彩目标"tooltip 却承诺"…按目标色域转换并**写入新的 ICC**"
   （`output_page_build.cpp:273-275`），对 BMP 恒不成立。
7. **"无损"复选框在 BMP 上恒无效（P3）**：`raw` 技术 `lossless_capable=true` ⇒ 复选框可见，勾选只写内部键 `__lossless`，
   BMP 无任何代码读它；实测 `--lossless` 与不带的产物 SHA-256 相同。
8. **兼容性总体良好**：24bpp 与 8bpp 索引（256 级灰阶调色板）产物都被 **Windows GDI+ 独立解码通过**（`Format24bppRgb` / `Format8bppIndexed`），
   行 4 字节对齐（65px 宽，补 1/3 字节）与 BMP→BMP 逐位往返（`pp_verify exact` OK）均通过。唯一结构偏差是 `biSizeImage` 写的是**像素数**
   而非字节数（上游 OIIO `bmpoutput.cpp:236`，如 64×64 24bpp 写 4096 而应为 12288/0），属合规偏差、GDI+/OIIO 均不受影响。

#### 2. 参数全量矩阵

BMP 的**表内参数集为空**（0 项）。下表把"格式级控制"与"对 BMP 产物有实际作用的全局项"一并列出，便于逐项核对 UI 与生效面。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| （表内参数）`techs[raw].params` | 无（`paramform` 建 0 行；核心区空、高级折叠行 `count_advanced()==0` 自行隐藏） | — | 空集 | `src/core/format_tables.cpp:866-867`；编码器侧 `src/codecs/enc_oiio.cpp:297`（`bmp has none at all`） | 是（无参数即无需参数） | 未声明键被拒：`params.cpp:599-609` 规则③ → 实测 `--param quality=90` / `--param lossless=true` 都报 `未知参数：…`；编码器另有 `enc_oiio.cpp:309-315` 未识别参数 warn（正常不可达）。**零高级项/零锁定项/零运行时内省项**（`runtime_introspected=false`，`format_tables.cpp:865`）。 |
| `out_bitdepth`（格式级） | 是：页签内"位深"下拉，选项 `8 位` / `24 位`，tooltip「输出位深（单位：bpp = 每像素位数）」 | `24`（`pipeline.cpp:1729-1730`；UI 同源 `page_output.cpp:641`） | `{8, 24}`（bpp 口径，`format_tables.cpp:874`） | `src/codecs/enc_oiio.cpp:205-209`（唯一读取点；`24/8` 都只用作合法性校验，随后 `out_type` 恒 `UINT8`） | **否**（两值产同一字节；bpp 由 `nchannels` 决定：`bmpoutput.cpp:225-234`） | 见 BMP-01/BMP-02。`bitdepth_note`（"X 仅支持 N 位输出"）只在**选项数==1** 时出现（`page_output.cpp:646-653`），BMP 有 2 项 ⇒ 无任何行内说明。校验面一致（`pipeline.cpp:433-440` / `params.cpp` 无特殊规则 / `presets.cpp:62-71`）。 |
| `lossless`（格式级，无参数载体） | 是：「无损编码」复选框**可见**（`raw.lossless_capable=true` ⇒ `first_lossless_tech_in` 非空 ⇒ `paramform.cpp:599-602` 置可见） | `false` | bool | **无读取点**（BMP 不声明该参数：`params.cpp:318` 注释 + `declares_lossless()`；编码器不读 `params`） | **否**（`--lossless` 与不带：SHA-256 相同） | 只写内部键 `__lossless`（`params.cpp:495-499`），预设落 `outputs[i].lossless`（`preset_io.cpp:466-468`）。见 BMP-06。 |
| （全局）色彩目标 `color` | 是：全局卡「色彩目标」下拉 = 保持原样 / sRGB / Display P3 / Adobe RGB (1998) | `keep`（`pipeline.h:83`）+ UI 默认第 0 项（`output_page_build.cpp:263-270`） | 4 档 | `pipeline.cpp:864-884`（keep ⇒ 不动像素、`icc_to_embed = src_icc`）、`pipeline.cpp:1157`（告警判据）、`enc_oiio.cpp:300-306`（把 ICC 放进 `spec.attribute("ICCProfile")`） | 条件：keep 不做变换（像素与源逐位相同）；其余三档走 lcms 真变换（实测 sRGB 档 4086/4096 像素改变，P3/AdobeRGB 档字节亦变） | BMP 侧 ICC **恒被 writer 丢弃**（`bmpoutput.cpp` 全文无 ICC 读取；`test_enc_oiio.cpp:899-923` 钉住）。见 BMP-03/BMP-05。 |
| （全局）alpha 合成底色 `flatten_gray` | 是：设置页滑杆 0..100 → `v/100`（`settings_dialog.cpp:84/134/806`） | `1.0`（`pipeline.h:86`） | 0..1 | `pipeline.cpp:1012`（`flatten_alpha_in_place`） | 是（仅当源含 alpha 且目标不支持 alpha；BMP `supports_alpha=false`） | 实测 `rgba8→bmp` 与 `graya8→bmp` 都先拍平（`AlphaFlattened` 告警），底色默认白（GDI+ 读 p00=`#FFFFFF`）。 |
| （全局）8bit 抖动 `color_dither8` | 是：全局卡「8bit 抖动」开关（`output_page_build.cpp:335-344`） | `false` | bool | `enc_oiio.cpp:343-348`（bps==1 分支 → `quantize8_dithered`） | 是（实测仅改 8bit 量化末位，不改结构） | 这是 BMP 上**唯一真实生效的量化调节**；`oiio:dither`（`bmpoutput.cpp:93`）未被本项目使用（我们自己量化到 UINT8，writer 不再降位）。 |
| `backend` / `tech` | 隐藏（各只有 1 项：`oiio` / `raw`；`paramform.cpp:574-587` 单项即隐藏行） | `oiio` / `raw` | 固定 | `enc_oiio.cpp:419`（`make_bmp`） | 是（唯一路径） | 显式 `--backend oiio --tech raw` 与省略等价（实测同字节）。 |

补充（不是参数、但影响产物）：`rotate_orientation` 默认 `true`（`pipeline.h:85`）—— BMP 无 EXIF 可写方向，旋转只能靠烘焙进像素；
关掉该开关 + 源带 Orientation≠1 时产物既未旋转也无方向标记（仅有 `MetadataDropped` 兜底告警）。本次未实测该组合（见 §5）。

#### 3. 发现

##### BMP-01 [P1] 位深下拉对 BMP 是无效参数：24/8 产同一字节，且两个方向都与产物矛盾

- 证据：
  - `src/core/format_tables.cpp:874` — `.bitdepths = {8, 24}`，同处注释（:868-873）声明"24 = RGB 24bpp … 8 = 8bpp（灰度源产物的真实口径；RGB 源下与 24 等价）"。
  - `src/codecs/enc_oiio.cpp:205-209` — bmp 分支只做合法性校验，随后 `out_type = OIIO::TypeDesc::UINT8;`（24 与 8 走同一条）。
  - `vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/bmp.imageio/bmpoutput.cpp:225-234` — 真实 bpp 只由通道数决定：
    `if (nchannels == 1) bpp = 8; else bpp = nchannels * 8;`（8bpp 仅 1 通道时出现，且带 256 级灰阶调色板 `:249-254`）。
  - `src/ui/page_output.cpp:263-268`（tooltip 只讲单位 bpp）、`:646-653`（注释位仅在选项数==1 时给文案）——BMP 页面上**没有任何说明**这两个值不改变产物。
- 事实：
  1. `rgb8.png → bmp`，`--bitdepth 24` 与 `--bitdepth 8` 的产物 **SHA-256 完全相同**（`3fba5f2fde66a705…`，均 12342 B，头 `bpp=24`）。
  2. `gray8.png → bmp --bitdepth 24`：产物 `bpp=8`、`off=1078`、256 项调色板 —— 用户选"24 位"得到 **8bpp 文件**。
  3. `rgb8.png → bmp --bitdepth 8`：产物仍 `bpp=24` —— 用户想"降到 8 位"得到的还是 24bpp。
  4. `rgb16.png` 同样：`--bitdepth 24` 与 `8` 产物字节一致。
  因果链：UI 下拉 → `spec_of` 写 `out_bitdepth`（`page_output.cpp:731`）→ `build_output_specs` 只做集合归属校验（`pipeline.cpp:1758-1764`）→ `plan_targets` 原样下发（`pipeline.cpp:490`）→ 编码器只校验、不分支 → OIIO writer 按通道数定 bpp。**该 int 从不驱动产物形态。**
- 影响：用户在 BMP 上做的唯一一个"质量/体积"选择完全无效；更糟的是文案与产物相反（选 24 得 8bpp、选 8 得 24bpp），
  且界面没有任何提示。这是"UI 与产物矛盾"的典型形态，不属打磨级。
- 建议：
  1. 首选（不动引擎语义，只改声明面）：把 bmp 行改为**按通道数条件化**的展示——`bitdepths` 保留 `{24}`（RGB 口径）并在 UI 位深行**置灰/只读 + 行内说明**
     "BMP 位深由通道数决定：彩色 24bpp / 灰度 8bpp"，即把 `page_output.cpp:646-653` 的注释位门槛从"选项数==1"放宽到"`fmt->id=="bmp"`"。
  2. 若保留 `{8,24}`：在 `enc_oiio.cpp:205-209` 之后加一条**逐输出 warning**（新 `WarningKind` 需走加性追认）或至少 `log_warn`：
     `"bmp bitdepth 8 requested but output is 24bpp (3 channels)"` / 反之；并在 UI 选 8 且源为 RGB 时给行内提示。
  3. 根治路线（与 R-211 强类型拆字段同批）：把"每像素位数"从 `out_bitdepth` 拆出为独立字段，避免同一个 int 双语义（受冻结面约束，需解冻裁定）。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:301` R-090「灰度 BMP 实产 8-bit vs schema 承诺 {24}」+ `:478` R-211「int 双语义」；
  R-090 的修复是**扩表 + 改文案**，未让任一值驱动 writer，故矛盾仍在；"RGB 源选 8 实产 24bpp"这一半为本轮新增事实）。

##### BMP-02 [P1] 16-bit 源在默认 24 bpp 下静默降到 8 bps：降档告警把 bps 与 bpp 直接比较

- 证据：
  - `src/core/pipeline.cpp:1042-1047` —
    `if (ctx.info.src_bitdepth > pt.target.out_bitdepth) { … "source bit depth " + … + " -> output bit depth " + std::to_string(pt.target.out_bitdepth) }`
  - `src/decode/oiio_reader.cpp:164` + `:108-111` — `src_bitdepth = bitdepth_from_format(spec.format)` = **每样本位数**（uint16 → 16）。
  - `src/core/params.h:61-69` / `src/core/params.cpp:393-411` — bmp 的 `out_bitdepth` 是**每像素位数**；换算单源 `bitdepth_bps("bmp",24)==8` 已存在。
  - 全仓 `bitdepth_bps` / `bitdepth_bpp` **无任何 src 消费者**（只在 `params.h/.cpp` 声明定义 + `tests/unit/test_params.cpp:927-943` 断言）。
- 事实（实测，`build/encoder-audit/bmp/grad/`）：自造 64×64 **真 16-bit**（4096 个不同码值）灰度 PNG：
  - `--bitdepth 8`（非默认）→ 告警 `DepthDowngrade: source bit depth 16 -> output bit depth 8` + `MetadataDropped`；
  - `--bitdepth 24`（**默认**）→ 只有 `MetadataDropped`，**无 DepthDowngrade**；
  - 两次产物 **SHA-256 相同**（`2C10460B39F0…`，5174 B，`bpp=8`），像素与"正确的 16→8 量化" **0/4096 偏差**（转换本身正确，缺的只是告警）。
  同理 `rgb16.png → bmp`（默认 24）：`src_bitdepth=16`，告警仅 `MetadataDropped`。
- 影响：项目自定规约「任何降档（位深/有损化）界面上显式警示」（AGENTS.md §4.5）在 BMP **默认路径上失效**：
  16-bit 源静默损失 8 位精度，用户只看到一条"无元数据容器"，无法区分"丢 EXIF"与"丢位深"。
- 建议：把判据改为按样本位深比较——在该处取 `const int out_bps = pp::bitdepth_bps(pt.fmt->id, pt.target.out_bitdepth);`
  再用 `ctx.info.src_bitdepth > out_bps`；同时告警文案对 bmp 标注单位（`-> output bit depth 8 bps / 24 bpp`），
  并补 `tests/unit/test_pipeline_contract.cpp` 的 bmp 16-bit 用例（断言 `DepthDowngrade` 在 24 与 8 两条路径都出现）。
  这也顺带让当前"死码"的 `bitdepth_bps` 有了唯一正确消费者。
- 状态：confirmed；known=no（R-211 只登记了 int 双语义本身，未登记其在 DepthDowngrade 判据上的后果）。

##### BMP-03 [P1] keep（默认）+ 非 sRGB 源 ICC：BMP 丢掉唯一色彩标记，却不发 ICC 告警

- 证据：
  - `src/core/pipeline.cpp:883-884` — `ctx.icc_to_embed = ctx.src_icc;`（keep 时把源 ICC 作为待嵌 profile 传给编码器）。
  - `src/core/pipeline.cpp:1150-1162` — `meta_path=="none"` 时：恒发 `MetadataDropped`；ICC 告警**只在** `ctx.cfg.color != ColorTarget::KeepOriginal`
    时追加（`:1157`）。
  - `src/codecs/enc_oiio.cpp:300-306` — 把 ICC 放进 `spec.attribute("ICCProfile", …)`；`bmpoutput.cpp` 全文无 ICC 读取点（V3 头 40 字节，
    `:219` `m_dib_header.size = WINDOWS_V3`），写入即丢。
  - `tests/unit/test_color.cpp:1756-1808` — 钉住"srgb 报 / keep 不报"，但用的是**无 ICC** 的 `rgb8.png`（`tools/gen_corpus.py:90` 无 profile 注入），
    因此该用例只覆盖了"没有 profile 可丢"的情形。
- 事实（实测，`build/encoder-audit/bmp/icc/`）：自造 64×64 RGB PNG，嵌 **Adobe RGB (1998)** 的 iCCP（取自
  `vcpkg/buildtrees/exiv2/.../test/data/small.icc`，560 B，`desc`="Adobe RGB (1998)"）：
  - `--format png --color keep` 产物 iCCP = **同一 560 B profile（逐字节相等）** ⇒ 管线确实解析并保留了源 profile；
  - `--format bmp --color keep`（默认）：产物 12342 B，头 40 B、像素起点 54，文件内 `b'ICC'`/`b'Adobe'` 命中数 **0/0**，
    像素与源 AdobeRGB 码值 **0/4096 差异**（未做任何色彩变换）；告警仅 `MetadataDropped`；
  - `--format bmp --color adobergb`：产物 **SHA-256 与上面完全相同**（`87BBE1B964484D79…`），却多一条
    `bmp output cannot embed the ICC profile (color target adobergb)`。
  即：**同样的字节、同样的"AdobeRGB 编码但无标记"产物，keep 路径不告警、显式档位路径告警**。
- 影响：摄影常见源（AdobeRGB / Display P3 打标）用默认"保持原样"导 BMP，得到的是**无标记的非 sRGB 像素**——
  Windows 照片/GDI+、浏览器、多数看图器按 sRGB 解释 ⇒ 颜色明显偏色（P3/AdobeRGB 饱和度差可达 ΔE 十余级），
  而用户看到的唯一提示是"bmp output carries no metadata container"（读起来像"丢 EXIF"）。
  这正是"静默把有损当无损"的邻域：像素没坏，色彩语义静默丢失。
- 建议：把告警判据从"色彩目标 ≠ keep"改为"**确实有 profile 被丢**"：
  `if (!ctx.icc_to_embed.empty())` 逐输出发 `EmbedIccUnsupported`（keep 时 `icc_to_embed==src_icc`，无 ICC 时不误报，
  现有 `test_color` 的 keep 用例仍绿）；文案区分两种情况：
  keep ⇒ `"bmp output cannot carry the source ICC profile (<desc>)；pixels stay in that space but untagged — viewers will assume sRGB"`；
  非 keep ⇒ 现状文案。若担心 sRGB 源噪声，可在 `ColorManager` 侧暴露"profile 是否等价 sRGB"的判据后只在非 sRGB 时报。
  同批建议：把 `bmp` 的 ICC 承载能力接入 UI（见 BMP-05），并在 `docs/param-catalog.md:229-233` 补一句"keep + 非 sRGB ⇒ 需要用户自行转换到 sRGB，否则产物无色彩标记"。
- 状态：confirmed；known=yes（`docs/m7r2-plan.md:93`「…keep **不报**（不虚报）」+ `docs/m7-adjudications.md:270` R2F-C1 / E2。
  登记的合理性只覆盖"没有 profile 可丢"的 keep；本轮实测证明**有 profile 可丢**时该口径变成假阴性，且与显式档位路径自相矛盾）。

##### BMP-04 [P2] `EmbedIccUnsupported` 在 `--dev` 摘要与 `.pp.json` 里显示为 `"Unknown"`

- 证据：
  - `src/core/types.h:26-38` — `WarningKind` 末尾已加性追加 `EmbedIccUnsupported`（M7-R2A/B3）。
  - `src/dev_cli.cpp:148-167` — `warning_name()` 的 `switch` 只有 8 个旧 case，缺 `EmbedIccUnsupported`，末尾 `return "Unknown";`。
  - `src/dev_cli.cpp:470`（sidecar `o["kind"] = warning_name(w.kind)`）与 `:442`（摘要行）两个消费点都吃这个 `"Unknown"`。
- 事实：`--format bmp --color srgb` 的 `.pp.json` 实测为
  `{"detail":"bmp output cannot embed the ICC profile (color target srgb)","kind":"Unknown"}`；摘要行同样显示 `Unknown`。
  BMP 是**唯一**能触发该 kind 的格式（`pipeline.cpp:1157` 只在 `meta_path=="none"` 分支）。
- 影响：取证面（用户/评审/`pp_verify` 的 `warnings_*` 断言）看到的是无意义字符串；若将来有金样写
  `"warnings_contain":["EmbedIccUnsupported"]`，会得到"未报告该告警"的错误结论。
- 建议：`src/dev_cli.cpp:148` 的 switch 补 `case pp::WarningKind::EmbedIccUnsupported: return "EmbedIccUnsupported";`；
  更彻底的做法是把 `warning_name` 移入 `src/core`（`types.h`/新 `warning_text.h`）做成单源，并让"枚举新增但 switch 未覆盖"
  在编译期可见（当前该 `switch` 无 `default` 分支、缺 case 仍编译通过，新增 kind 不会被任何门禁拦住 —— 本轮就是实例）。
- 状态：confirmed；known=no。

##### BMP-05 [P2] UI 零前置能力提示，且"色彩目标"tooltip 承诺写入新 ICC（BMP 恒不可能）

- 证据：
  - `src/ui/output_page_build.cpp:273-275` — 全局"色彩目标"tooltip：「保持原样 = 不改动嵌入的 ICC 色彩描述；
    **其余档位按目标色域转换并写入新的 ICC**。仅元数据模式下不生效」。
  - `src/ui` 全目录 grep `supports_alpha|supports_gray|meta_path` **零命中** ⇒ UI 从不展示逐格式能力（alpha/灰度/元数据容器）。
  - `src/ui/page_output.cpp:646-653` — 行内注释位只在 `allowed.size()==1` 时给"X 仅支持 N 位输出"；BMP 现在有 2 个选项 ⇒ 无提示。
  - `src/core/pipeline.cpp:1150-1163` 是唯一的告知通道（**跑完之后**的逐输出 warning → 只进日志尾/日志文件）。
- 事实：用户在选择 BMP、勾选含 EXIF 的 JPEG、或源带 ICC 时，**在运行前**看不到任何"BMP 会丢 EXIF/XMP/ICC、alpha 会被拍平、
  最高 8 bps"的提示；而"色彩目标"tooltip 明确承诺"写入新的 ICC"，对 BMP 是不成立的承诺（见 BMP-03 实测：文件内 0 处 ICC）。
- 影响：用户在错误的预期下产出文件（例如以为 P3 档会带 profile）；发现时数据已落盘。属"提示缺失 + 文案承诺超出实现"。
- 建议：
  1. 给 `FormatDef` 加**只读展示字段**（如 `capability_note`，加性、不改冻结签名）或用 UI 侧常量表，在 BMP 页签位深行注释位写
     「BMP：无 ICC/EXIF/XMP 容器；alpha 将按底色拍平；最高 8 位/样本」。
  2. 色彩目标 tooltip 改为条件文案：选中集合含 `meta_path=="none"` 的格式时追加「（当前所选格式无法写入 ICC：BMP）」，
     并同步到 `docs/param-catalog.md`。
  3. 可选：把 `MetadataDropped` 从"恒发"改为"确有内容被丢时才发"（`pipeline.cpp:1150-1153` 当前**无任何判据**，
     所有 BMP 输出都带这一条；实测 `rgb8.png` 这种无 EXIF/XMP/ICC 的源也发，稀释了信号）。
- 状态：confirmed（UI 未渲染，属代码级证据；实测证据为运行期告警确实只在事后出现）；known=no
  （`ux-review-14.md:418` 只讨论过"单一位深格式的下拉是纯装饰"，`:464` 判"BMP 零参数，正确"，未涉及能力提示与 tooltip 承诺）。

##### BMP-06 [P3] "无损编码"复选框在 BMP 上恒无效果（BMP 本来就无损）

- 证据：
  - `src/core/format_tables.cpp:866` — `TechDef{ .id="raw", .lossless_capable=true, .params={} }`。
  - `src/ui/paramform.cpp:599-602` — `has_lossless = first_lossless_tech_in(b) != nullptr` ⇒ BMP 的复选框**可见可勾**。
  - `src/core/params.cpp:495-507` — 勾选只写内部键 `__lossless`；`tech_declares_lossless(raw)==false` ⇒ 不写显式 `lossless` 参数（`:500`）。
  - BMP 编码器不读 `params`（`enc_oiio.cpp:297`），OIIO BMP 恒为无压缩/无损（`bmpoutput.cpp:223` `compression = NO_COMPRESSION`）。
- 事实：`--format bmp` 与 `--format bmp --lossless` 产物 **SHA-256 相同**（`3fba5f2fde66a705…`）。
- 影响：不产生错误产物，但多了一个永不生效的控件；勾选会进预设（`preset_io.cpp:466-468` 写 `outputs[i].lossless=true`），
  造成"预设里写了但行为不变"的认知噪音。属打磨级。
- 建议：`paramform.cpp:599` 的可见性判据补"且该技术声明了 `lossless` 参数或确实存在有损技术"（`declares_lossless()` 或
  `b->techs.size()>1`），BMP/PNG/TIFF 这类恒无损格式隐藏复选框；若产品口径要保留（"显式声明我选择无损"），
  至少给 tooltip「本格式恒为无损，此开关不改变产物」。
- 状态：confirmed；known=no。

##### BMP-07 [P3] 24bpp 产物的 `biSizeImage` 写的是像素数而非字节数（上游 OIIO 口径偏差）

- 证据：
  - `vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/bmp.imageio/bmpoutput.cpp:236` —
    `m_dib_header.isize = int32_t(m_spec.image_pixels());`（`image_pixels()` = 宽×高，**不是** `image_bytes()`）。
- 事实（实测头部 dump）：64×64 24bpp → `biSizeImage=4096`，而像素数据实际 **12288 B**（`biSizeImage` 按 BMP 规范应为
  图像字节数，或对 `BI_RGB` 可置 0）；65×7 24bpp → 455（实际 1372）；65×7 8bpp（行补 3 B）→ 455（实际 476）；
  1×1 8bpp → 1（实际 4）。`bfSize`（文件大小）与 `bfOffBits` 均正确。
- 影响：`BI_RGB` 读取器普遍按 `宽×高×bpp` 计算数据量，实测 **GDI+ 与 OIIO 读取器都不受影响**；
  但严格按 `biSizeImage` 做边界校验的解析器（部分嵌入式/自研解码器）可能把数据截断或报错。风险低、非本项目引入。
- 建议：不改上游；在本仓 `docs/param-catalog.md` 的 BMP 段记一条"已知上游偏差（OIIO 3.1.17）"，
  并在 `test_enc_oiio` 增加头部字段断言（`biSizeImage` 当前值），把它变成**受监控的上游事实**而非未知；
  若将来升级 OIIO 修好该字段，断言即提示。
- 状态：confirmed（字段值实测；对严格解析器的影响未实测，故影响面按 suspected 记）；known=no。

#### 4. 输出兼容性与合规矩阵

> 实测环境：`build/release-dev/photopipeline.exe`（0.3.5.alpha2）+ `pp_verify.exe`；独立解码器 = **Windows GDI+**（`System.Drawing.Bitmap`，
> 与 OIIO 无共享代码）；产物全部在 `build/encoder-audit/bmp/` 下。

| 源/配置 | 产物结构（实测） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| `rgb8.png` → bmp（默认 bd=24） | 12342 B；`bfOffBits=54`；`biSize=40`(V3)；`bpp=24`；`BI_RGB(0)`；无调色板；底向上；行 stride 192；`biSizeImage=4096`（应 12288） | GDI+ `Format24bppRgb` 读通（尺寸/像素格式正确）；OIIO 回读 3ch/UINT8；`pp_verify exact` **OK**（逐位一致） | `biSizeImage` 偏差（BMP-07）；无 ICC/EXIF/XMP（BMP-05） |
| `rgb8.png` → bmp `--bitdepth 8` | **与上一行完全相同的字节**（SHA-256 `3fba5f2f…`） | 同上 | 参数无效（BMP-01） |
| `gray8.png` → bmp bd=24 或 8 | 5174 B；`bfOffBits=1078`；`bpp=8` 索引；256 项灰阶调色板（identity, alpha=255）；`biClrUsed=biClrImportant=256`；stride 64；`biSizeImage=4096`（=实际） | GDI+ `Format8bppIndexed` 读通，`GetPixel` 回灰阶；`pp_verify exact` OK（1 通道对齐） | 与 UI 所选"24 位"矛盾（BMP-01） |
| `rgba8.png` → bmp bd=24 | 12342 B；**24bpp（3 通道）**；alpha 按底色 1.0 拍平（GDI+ 读 p00=`#FFFFFF`）；告警 `AlphaFlattened`+`MetadataDropped` | 读通 | 无（设计如此，有告警）；alpha 信息不可恢复 |
| `graya8.png` → bmp bd=24 | 5174 B；**8bpp 索引**（2ch→1ch 拍平）；`AlphaFlattened` | 读通 | 同上 |
| 真 16-bit 灰度（4096 码值）→ bmp bd=24（默认） | 5174 B；8 bps；量化正确（0/4096 偏差）；**无 `DepthDowngrade`** | 读通 | 静默降档（BMP-02） |
| 同上，bd=8 | 与上一行 **同字节**；有 `DepthDowngrade` | 读通 | 告警随"无效参数"变化（BMP-01/02） |
| AdobeRGB(1998) iCCP 源 → bmp `--color keep` | 12342 B；像素=源 AdobeRGB 码值（0/4096 差异）；文件内 **0 处 ICC**；仅 `MetadataDropped` | 可解码，但被当作 sRGB 解释（偏色） | 色彩语义静默丢失（BMP-03） |
| 同上 → bmp `--color adobergb` | **与 keep 同 SHA-256**；多一条 ICC 告警（kind 显示 `Unknown`） | 同上 | 告警条件与产物不对应（BMP-03/04） |
| `bmp24.bmp` → bmp（BMP 源） | 12342 B；24bpp；`pp_verify exact` **OK**（逐位往返） | 读通 | 无 |
| 65×7 rgb8 → bmp | 1426 B；stride 196（**+1 字节行补零**）；`biSizeImage=455`（实际 1372） | GDI+ 读通，末像素 `#C04280` 与生成器逐位一致；`pp_verify exact` OK | 行补零实现正确；`biSizeImage` 偏差 |
| 65×7 gray8 → bmp | 1554 B；stride 68（+3 字节补零）；8bpp 索引 | 同上，`pp_verify exact` OK | 同上 |
| 1×1 gray8 → bmp | 1082 B = 14+40+1024+4；stride 4；`biSizeImage=1` | 读通 | `biSizeImage` 偏差 |
| 70000×1 gray → bmp | **无产物**；`encode failed: OIIO open failed …: bmp image resolution may not exceed 65535x65535, you asked for 70000x1` | 不适用 | 维度上限（`bmpoutput.cpp:85-87` `{0,65535,0,65535,…}`）以明确错误报出，无静默降级；但 UI 无前置提示 |
| 多页 `multi.tif` → bmp | 12342 B（首页）；告警 `MultipageTruncated`+`MetadataDropped` | 读通 | 有告警，合规 |
| >4 GB（如 65535² 24bpp ≈ 12.9 GB） | 未实测；writer 有 `file_size >= 1<<32` 硬拒（`bmpoutput.cpp:95-99`） | — | 未覆盖（见 §5） |
| `--metadata-only` + `.bmp` 源 | 不产生产物；`format 'bmp' does not support metadata-only rewrite (zero re-encode)` | — | 显式拒绝（`meta_path=="none"` ⇒ `detail_metadata_only_supported("bmp")==false`，`tests/unit/test_metadata.cpp:1134`），符合"仅元数据不碰像素"红线 |

结构性结论：
- 容器 = `BITMAPFILEHEADER` + `BITMAPINFOHEADER`(40, V3) + 可选 256×4 调色板 + 底向上像素 + 4 字节行对齐；`bfSize`/`bfOffBits` 正确。
- 无 ICC/EXIF/XMP 槽位（V3 头无 profile 字段；OIIO 也不写 V5 头）⇒ 色彩/元数据只能靠**放弃**，产品口径应显式告知（BMP-03/05）。
- alpha：`FormatDef.supports_alpha=false` ⇒ 管线在 encode 前拍平（`pipeline.cpp:1006-1035`），**32bpp alpha 路径在本产品不可达**；
  即使可达，OIIO 写的是 `BI_RGB` 32bpp（第 4 字节在 Windows 语义下未定义/忽略），不构成合规 alpha 通道（见 §5）。
- 灰度：`supports_gray=true` ⇒ 1 通道原样进 writer ⇒ 8bpp 索引，语义与颜色都正确。

#### 5. 未覆盖与存疑

1. **UI 未实际渲染**：本次只读审计，未运行 GUI/`--ui-smoke`。凡"复选框可见/页签无提示"类结论均为**代码级**证据
   （`paramform.cpp:599-602`、`page_output.cpp:646-653`、`src/ui` 对 `supports_*`/`meta_path` 零引用），未截图取证。
2. **未实测 >4 GB 产物**（需要 >12.9 GB 的 65535×65535 源）；只引用 writer 的 `1<<32` 硬拒分支（`bmpoutput.cpp:95-99`）。
3. **未跨第三方工具对拍**：仅用 GDI+（Windows 原生解码器）与 OIIO（同库回读）两路。未测 Photoshop/GIMP/ffmpeg/ImageMagick，
   故"标准工具可读"结论的强度限于这两路。
4. **`biSizeImage` 的严格解析器后果未实测**（BMP-07 影响面按 suspected 记）。
5. **32bpp alpha 路径不可达但存在**：`bmpoutput.cpp:74-78` `supports("alpha")==true`，`:231` 对 4 通道写 `bpp=32` + `BI_RGB`。
   若将来有路径绕过 flatten（例如直接调编码器的单测/新前端），会产出"看似带 alpha、实际被忽略"的 32bpp BMP 且 `ok=true`——
   这是潜在假桩，当前**不是**产品路径，故未计为发现。
6. **方向语义**：`rotate_orientation` 默认 true ⇒ 旋转烘焙进像素，BMP 无 EXIF 也不影响观看；但用户关闭该开关 + 源带 Orientation≠1 时，
   产物既未旋转也无方向标记（仅 `MetadataDropped` 兜底）。未实测该组合，未计为发现。
7. **`out_bitdepth` 非法值的静默归一**：`page_output.cpp:610-645` 的 `set_bitdepth_options(forced, keep_if_valid=false)` 在
   `forced` 不在允许集时会**静默回落到默认 24**（例如把别的格式的 16 位预设套到 BMP）。预设文件路径有 `presets.cpp:62-71` 硬拒，
   但内存态 `apply_preset` 无告警。未构造出用户可达路径，故记存疑。
8. **`MetadataDropped` 恒发**：即使源完全没有 EXIF/XMP/ICC 也发（实测 `rgb8.png` 即如此，`tests/golden/smoke/bmp-exact.json` 也钉
   `warnings_exact:["MetadataDropped"]`）。是否改为"确有内容被丢才发"属产品口径，本次只作为 BMP-05 的建议提出。
9. **逐输出 warning 的 UI 曝光面**：`src/ui` 无任何 `Warning` 渲染代码（grep 零命中），告警只能经日志尾/日志文件看到。
   这是全局设计，不属 BMP 专属，未计为发现，但它是 BMP-03/05 影响被放大的原因。

#### 6. 复现命令（全部经 `tools/env.py run --`）

```powershell
# A. 位深无效性 + 灰度/彩色 bpp 反向矛盾
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png  --out build/encoder-audit/bmp/a24 --format bmp --bitdepth 24 --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png  --out build/encoder-audit/bmp/b08 --format bmp --bitdepth 8  --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/gray8.png --out build/encoder-audit/bmp/c24 --format bmp --bitdepth 24 --workers 1
#   a24/b08 的 rgb8.bmp 同 SHA-256；c24 头 bpp=8（见 build/encoder-audit/bmp/*.pp.json 与头部 dump）

# B. 16-bit 静默降档（默认 24 无告警 / bd8 有告警 / 同字节）
python build/encoder-audit/bmp/probe16.py make build/encoder-audit/bmp/grad
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/bmp/grad/grad16.png --out build/encoder-audit/bmp/grad/default24 --format bmp --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/bmp/grad/grad16.png --out build/encoder-audit/bmp/grad/bd8       --format bmp --bitdepth 8 --workers 1
python build/encoder-audit/bmp/probe16.py cmp build/encoder-audit/bmp/grad/grad16.png build/encoder-audit/bmp/grad/default24/grad16.bmp

# C. keep + AdobeRGB 源（丢 ICC 不告警 / 显式档位同字节却告警）
python build/encoder-audit/bmp/make_icc_src.py build/encoder-audit/bmp/icc/adobe_rgb.png
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/bmp/icc/adobe_rgb.png --out build/encoder-audit/bmp/icc/bmp_keep    --format bmp --color keep    --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/bmp/icc/adobe_rgb.png --out build/encoder-audit/bmp/icc/bmp_adobergb --format bmp --color adobergb --workers 1
python build/encoder-audit/bmp/check_icc.py build/encoder-audit/bmp/icc/png_keep/adobe_rgb.png build/encoder-audit/bmp/icc/bmp_keep/adobe_rgb.bmp build/encoder-audit/bmp/icc/bmp_srgb/adobe_rgb.bmp

# D. 结构/兼容（奇数宽补零、1x1、65535 上限、BMP 往返逐位一致）
python build/encoder-audit/bmp/make_png.py build/encoder-audit/bmp/tiny/w65_rgb.png 65 7 3
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/bmp/tiny/w65_rgb.png --out build/encoder-audit/bmp/tiny/w65_rgb/out --format bmp --bitdepth 24 --workers 1
python tools/env.py run -- build/release-dev/pp_verify.exe build/encoder-audit/bmp/tiny/w65_rgb/case/expected.json build/encoder-audit/bmp/tiny/w65_rgb/out/w65_rgb.bmp
```

关键产物（供复核）：`build/encoder-audit/bmp/a24|b08|c24|c08|d24|j24|e24|e08|f24/`、`grad/`、`icc/`、`tiny/`、`rt/`，
逐输出侧车 `<产物>.pp.json` 内含 `warnings` / `info.src_bitdepth` / `params`（BMP 恒为空串，即"零参数"的运行期证据）。

### 4.7 HEIF（libheif / x265）

> 审计对象：`format=heif`（单后端 `x265`，`BackendDef.runtime_introspected=true`、`techs={}`）。
> 实测环境：Windows / build/release-dev（0.3.5.alpha2，PP_BUILD_DEV=ON）、libheif 1.23.5、x265 4.3-vcpkg、libde265 解码。
> 所有命令经 `python tools/env.py run -- ...`；临时脚本与产物均在 `build/encoder-audit/`（gitignored）。未运行 cmake / git / GUI / ui-smoke。

#### 1. 结论

1. **参数面 = libheif 内省出的 7 个键**（`quality|lossless|preset|tune|tu-intra-depth|complexity|chroma`，`pp_linkprobe` 实时核对），全部有 UI 落点（无损为顶部复选框、其余 6 行）；库侧默认值经插件构造期 `x265_set_default_parameters` 落实，**GUI 默认 quality=50（= CRF 25）**，`enc_heif.cpp:502` 里的 `90` 只是异常回退值，不是默认值。
2. **有一个 P1：HEIF 档位下拉里的「Main（8 位 · 兼容性最好）」是死选项** —— 8 位静止图在 x265 里恒推导为 Main Still Picture（`profile_idc=3`），UI 的兼容性预检因 AVIF 侧 `Main` 命中而放行，编码期严格反查必然不一致 → 每个文件失败且只报「编码档位不一致」，不给原因。
3. **默认位深 10 位是最不兼容的默认**：不选位深时 HEIF 恒产 `general_profile_idc=4`（RExt）+ hvcC 只声明 RExt 兼容位（实测 `0x08000000`）+ `ftyp=heix`；选 8 位才得到 `profile_idc=3` + Main/Main10/MainStill 三兼容位（`0x70000000`）+ `ftyp=heic`。UI 把它标成「Main10」并提示「部分工具显示为 Main」，与产物 PTL 不符。
4. **`--param`（任何字符串化入口）的数值型参数不可用**：`quality=10/50/90/abc` 产物**逐字节相同**（都被回退值 90 覆盖，8279 B），`tu-intra-depth`/`complexity` 直接编码失败（`expected integer`），`lossless=true` 被静默忽略。GUI 走内省类型不受影响，但 dev harness 的参数取证不可信。
5. **无损联动齐备**：无损 → 强制 `chroma=444`（编码器参数 + 平面布局两处同源）→ 产物 `chroma_format=444`；`--lossless --param chroma=420` 与 `--lossless` 逐字节相同；>16 MP 走标准 HEIF grid（实测 4200×4096=17.2 MP：primary=`grid`+6 个 `hvc1` tile+`dimg`，可解码，PSNR 与单图无损完全一致 48.85 dB）；带 alpha 且 >16 MP 明确拒绝而不是产出打不开的文件。
6. **色彩标记一致且可证**：容器 `colr=nclx(1,13,6,full)`；P3/AdobeRGB/显式 sRGB 时容器只留 ICC（libheif 行为）而**码流 VUI CICP 精确跟随 ICC 推导**（实测 P3→primaries=12、AdobeRGB→2/transfer=4、sRGB→1/13），矩阵恒 6 且与 §BT.601 全范围平面转换逐字一致 —— 与既有单测 `test_color.cpp:1664 test_heif_vui_nclx` 结论一致。
7. **元数据通路正确**：EXIF 走 `heif_context_add_exif_metadata`（libheif 自行补 4 字节 offset，实测 Exif item 载荷前 4 字节为 `00 00 00 00` + `II*\0`），XMP 走 `add_XMP_metadata`（`mime` + `application/rdf+xml`），两者都有 `cdsc` 指向主图；**方向实测正确**（320×200 + `Orientation=6` 源 → 产物 `ispe=200×320`、Exif `Orientation=1`，不重复旋转）；HEIF **不支持**仅元数据零重编码（`format_supports_metadata_only=false`，`pipeline.cpp:1247`），选该模式直接失败而非偷偷重编码。
8. **7 个键全部真接线**，没有假桩；唯二的"效果缺失"都来自库/联动口径而非丢参：无损下 `quality` 零效果（实测字节相同）但 UI 不锁不提示；x265 的 `threads` 键根本不存在（E3 缺口，已知 R-078）。另 4 个 P2/P3 集中在默认值口径、档位标签/PTL 文案、日志误导与标签全英文。

#### 2. 参数全量矩阵

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `quality` | 核心行（Int spin，标签＝英文 key `quality`；`paramform.cpp:826-873`） | **50**（插件默认；CRF=(100-q)/2=25）`x265.cc:230` | 0–100（`x265.cc:233-234`） | `enc_heif.cpp:443-455`（Int 派发）→ `heif_encoder_set_parameter_integer`；回退 `enc_heif.cpp:501-504` 写死 90 | 条件 | 无损下**零效果**（x265 `bLossless` 覆盖 CRF，实测字节相同）；CLI 数值被回退 90 覆盖 |
| `lossless` | **无行**，由格式卡顶部「无损编码」复选框独占（`paramform.cpp:500-527`、`:796-799`） | false `x265.cc:243` | bool | `lossless_flag()`（`params.cpp:307-310`）→ `enc_heif.cpp:536-546` 显式 `heif_encoder_set_lossless`；`enc_heif.cpp:476-484` 通用 Bool 派发 | 是 | 强制 `chroma=444`（`enc_heif.cpp:554-567`）；>16 MP 转 grid；带 alpha>16 MP 拒绝；与 `__lossless` 双键同步 |
| `chroma` | 核心行（Enum 字符串 combo 420/422/444；无损下仅 444 可选、其余置灰带原因） | `"420"` `x265.cc:295` | 420/422/444（`x265.cc:183-185`） | `enc_heif.cpp:456-475`（Enum→字符串 setter）+ `:607-627`（平面布局同源） | 条件 | 选 444/422 后 `profile_idc=4`（RExt）；无损→444 硬联动；文档 `param-catalog.md:117` 已记 420 |
| `preset` | 核心行（combo 10 项，x265 专属白名单 `enc_heif.cpp:216-221`） | `"slow"` `x265.cc:251` | ultrafast→placebo（`x265.cc:172-175`） | `enc_heif.cpp:469-470` → `x265.cc:515-522`；`:791` `param_default_preset` | 是 | 实测：ultrafast 912 B / slow 1018 B / placebo 1017 B（同源 256×256 渐变）；无损下仍影响字节（24225→24222） |
| `tune` | 核心行（combo 4 项） | `"ssim"` `x265.cc:260` | psnr/ssim/grain/fastdecode（`x265.cc:177-180`，无 zerolatency） | `enc_heif.cpp:469-470` → `x265.cc:523-529` | 是 | 插件默认即 ssim；与 preset 同时传 |
| `tu-intra-depth` | 高级·编码工具（Int spin 1–4） | 2 `x265.cc:269` | 1–4（`x265.cc:272-273`） | `enc_heif.cpp:443-455` → `x265.cc:434-441`；应用 `:1002-1005` | 条件 | GUI/预设生效；**CLI 传字符串 → 编码失败**（`enc_heif.cpp:509-513`） |
| `complexity` | 高级·编码工具（Int spin 0–100） | **0**（插件 `has_default=false`，元数据声明 50 但不施加：`x265.cc:280-289`；内省取"当前值"→0，`enc_heif.cpp:161-177`） | 0–100 | 同上 → `x265.cc:442-449`；应用 `:1006-1021`（≥60 rd-refine/rd6、≥70 cu-lossless、≥90 wpp=0） | 条件 | 0 与 50 行为等价（均低于阈值）；UI 显示 0 与插件声明 50 不一致；CLI 传字符串即失败 |
| `threads` | **不存在**（内省面无此键） | 后端默认池 | — | `enc_heif.cpp:586-597`：`is_known("threads")==false` → 仅 `log_info`（实测日志：`backend exposes no 'threads' parameter; default pool kept {encode_threads=16}`） | 否 | E3 契约缺口（`docs/deep-review-0.3.2.md` R-078）；x265 默认按核数起线程池，不入 §8 Σ 计数 |
| `__heif_profile`（保留键，UI-only） | 「编码档位」核心行（`page_output.cpp:228-253`；选项表 `page_output_impl.h:214-234`） | 0＝自动（`page_output.cpp:357-361`） | 0–6 `HeifProfile`（`heif_profile.h:34-42`） | `enc_heif.cpp:641-666`（严格反查，不一致即失败） | 条件 | 不落预设/不进日志（`preset_io.cpp:388-416`、`params.cpp:632-644`）；`Main` 恒不一致；字符串值静默当 Auto |
| `x265:<opt>`（插件私有透传） | **无 UI 行**（只能手改预设 JSON 或 `--param`） | — | x265 参数面 | `enc_heif.cpp:520-534`（前缀匹配→`set_parameter_string`，拒绝即失败） | 是 | 实测 `x265:qp=30` 生效（730 B）；`x265:bogus=1` 明确失败；`params.cpp:172-178` 放行未知键判定 |
| `out_bitdepth`（格式级，非 ParamDef） | 位深行 combo 8/10/12（`page_output.cpp:255-281`） | **10**（`pipeline.cpp:1731-1732`；UI/CLI 同源 `page_output.cpp:641`） | {8,10,12} ∩ 运行时探测 | `enc_heif.cpp:384-387` + `:629`；落盘后回读校验 `:1080-1084`（R-314） | 是 | 10 位→`profile_idc=4`；8 位→3；12 位→4 且 `bitDepthLuma=12` 实测通过 |

#### 3. 发现

##### HEIF-01 [P1] 「Main（8 位 · 兼容性最好）」档位是死选项：选中后每个文件必失败且只报「编码档位不一致」

- 证据：
  - `src/codecs/heif_profile.h:51-59`：`if (!avif) { if (bit_depth <= 8) return still ? HeifProfile::MainStill : HeifProfile::Main; ... }` —— 本输出恒为静止图（`enc_heif.cpp:640`、`page_output.cpp:546` 传 `still=true`），故 8 位 HEVC 只能推出 **MainStill**，永远推不出 `Main`。
  - `src/codecs/heif_profile.h:114-117`：`const HeifProfile heif_p = derive_heif_profile(false,...); const HeifProfile avif_p = derive_heif_profile(true,...); if (heif_p == want || avif_p == want) return true;` —— 判据是"**任一格式**能产出即算兼容"，而 AVIF 8 位 4:2:0 恰好推出 `Main`，于是 HEIF 侧预检通过。
  - `src/codecs/enc_heif.cpp:647-654`：`const HeifProfile derived = derive_heif_profile(avif, bitdepth, chroma_idx, true); ... if (want_v != Auto && want_v != derived) { std::string why; (void)heif_profile_compatible(want_v, bitdepth, chroma_idx, true, &why); return fail(why.empty() ? std::string("编码档位不一致") : why); }` —— 严格反查用 `avif=false`，`why` 因上一条返回 true 而为空 → 报**无信息量**的兜底串。
  - `src/ui/page_output_impl.h:215-221`：HEIF 选项表含 `{HeifProfile::Main, "Main（8 位 · 兼容性最好）"}`；`src/ui/page_output.cpp:560-569` 因 `heif_profile_compatible(...)==true` 且 `profile_compat_hint(Main)==""`（`page_output_impl.h:261-263`），注释位保持 Hint 色（无警示）。
- 事实：用户在 HEIF 页签选「Main（8 位 · 兼容性最好）」→ 位深被限幅到 8（`page_output.cpp:610-645`，`profile_allowed_bitdepths(Main)={8}`）、色度被限到 420（`:490-518`）→ 档位注释不报错 → 开跑后 `enc_heif` 严格反查得 `derived=MainStill ≠ Main` → 每个文件 `ok=false`，错误串「编码档位不一致」。
- 影响：UI 第一项"A 兼容性最好"的档位是**100% 失败路径**；用户看到的是"莫名其妙失败"，且错误文案不含"当前会产出 Main Still Picture"这一唯一可操作信息（该信息本可由 `heif_profile_compatible` 的 `err_zh` 给出，但它对 HEIF 判为兼容）。反向也说明一个隐患：`heif_profile_compatible` 的签名缺 `avif` 参数，被预检与错误解释两处复用，语义不同源。
- 建议：① `page_output_impl.h:214-221` 删掉 HEIF 的 `Main` 项（或在 `derive_heif_profile` 里把 "Main 与 MainStill 视为同一可用族"，但产物 idc=3，标签必须同步）；② `heif_profile.h:97` 增加 `bool avif` 形参（或新增 `heif_profile_compatible_for(avif, ...)`），让预检与编码期用同一判据；③ `enc_heif.cpp:650-654` 在 `why` 为空时自建消息，至少带上 `derived` 与 `want_v` 的中文标签。
- 状态：confirmed（纯函数 + 判据链，两处代码可逐行复核；GUI 渲染受"禁止 GUI"限制未实测，见 §5）。known=no。

##### HEIF-02 [P2] `--param`（字符串化入口）数值型参数不可用：`quality` 被静默改写成 90，Int/Bool 键直接失败或静默失效

- 证据：
  - `src/dev_cli.cpp:384-393`：`const pp::ParamDef *def = pp::lookup_param_def(f, backend, tech, key); if (!def) { s[key] = raw; ... return true; }` —— HEIF 的静态表 `techs={}`（`src/core/format_tables.cpp:877-884`），`lookup_param_def` 对**所有** HEIF 键返回 nullptr（`src/core/params.cpp:378-390`），于是 `--param` 一律退化为**字符串**。
  - `src/codecs/enc_heif.cpp:443-448`：Int 分支 `const int64_t *iv = std::get_if<int64_t>(&it->second); if (!iv) { rejected = "expected integer"; break; }`；`:501-508` 仅 `quality`/`lossless` 有回退：`const int64_t v = param_int(req.target.params, "quality", 90); recovered = heif_encoder_set_lossy_quality(enc, (int)v)...` —— 而 `param_int` 对字符串值返回 fallback（`params.cpp:425-432`），即 **90**。
  - `src/codecs/enc_heif.cpp:509-513`：无回退的键（`tu-intra-depth`/`complexity`）直接 `return fail("parameter '...' rejected by backend: expected integer")`。
  - `src/codecs/enc_heif.cpp:505-507`：`lossless` 回退读 `param_bool(..., "lossless", false)`，字符串 `"true"` 同样落到 fallback false。
- 事实（实测，`build/encoder-audit/results/matrix.json`）：`--param quality=10`、`quality=50`、`quality=90`、`quality=abc` 四者产物**逐字节相同**（8279 B，sha256 前缀 `bbf59072c7b31933`），而"不带任何参数"的默认产物是 1018 B —— 说明四个值全被 90 覆盖（CRF 5）。同一 harness 下 `--param tu-intra-depth=3`、`--param complexity=60` 直接 `failed`（`expected integer`）；`--param lossless=true` 产物与纯有损**逐字节相同**（1018 B，sha `3d927bff66c82f8e`）。
- 影响：① dev harness（本仓唯一的无 GUI 取证入口）无法按请求设置 quality —— 任何 quality 取证/回归都会被固定成 90，且**失败是静默的**（退出 0、产物正常）；② 想调 `tu-intra-depth`/`complexity` 只能看编码失败；③ 用户在预设里写错类型不会被发现。GUI 路径（内省出的 int64/bool）不受影响，故不是用户主路径缺陷；但它使"参数已生效"的取证链在 CLI 侧失真。
- 建议：在 `dev_cli.cpp:set_param` 之前对 `runtime_introspected` 后端调用 `pp::introspect_backends()` 得到实时 `TechDef` 再查 def（或复用 `params.cpp:compute_declared_keys` 的思路）；同时把 `enc_heif.cpp:501-508` 的回退值从硬编码 90/false 改为"取内省 def 的默认值"或直接失败，避免"看似给了值实则固定"。
- 状态：confirmed（四组实测字节 + 代码路径）。known=no（`docs/param-catalog.md:115` 只写"视觉透明档校准值（实现期实测）"，未记此事）。

##### HEIF-03 [P2] 默认位深 10 位 → 默认产物是 RExt（`profile_idc=4`）、hvcC 只声明 RExt 兼容位；8 位才是 Main Still Picture

- 证据：
  - `src/core/pipeline.cpp:1731-1732`：`if (format_id == "heif" || format_id == "avif") return 10;` —— UI 与 `--dev` 同源（`page_output.cpp:641`、`dev_cli.cpp:796-798`）。
  - 实测（`build/encoder-audit/out/default_bd`，与显式 `--bitdepth 10` 逐字节相同，992 B / sha `11160f277478b2c8`）：`ftyp` 主品牌 `heix`；hvcC `general_profile_idc=4`、`general_profile_compatibility_flags=0x08000000`（仅 RExt 位）；SPS `profile_idc=4` + RExt 约束位非零。对照 8 位（`out/base8`，1018 B）：`ftyp=heic`、`profile_idc=3`、compat `0x70000000`（Main+Main10+MainStill 三位）。
  - 来源（x265 4.3）：`vcpkg/buildtrees/x265/src/4.3-038bcd4491.clean/source/encoder/level.cpp:81-113`（10 位 4:2:0 且 intra → `Profile::MAINREXT`；8 位静止图 → `MAINSTILLPICTURE`）、`:132-148`（RExt 只置 `profileCompatibilityFlag[MAINREXT]`，不置 Main10）。
  - 容器侧：libheif 生成 hvcC 时**跳过** 48 位约束标志（`vcpkg/buildtrees/libheif/src/v1.23.5-6368e7938c.clean/libheif/codecs/hevc_boxes.cc:622-632`：三处 `reader.skip_bits(16)`，`general_constraint_indicator_flags` 恒为 0），实测 hvcC 该 6 字节全 0，而 SPS 内 RExt 约束位非零。
- 事实：不碰位深直接输出 HEIF，得到的是"10 位 + RExt 档 + 容器只声明 RExt 兼容 + hvcC 无约束标志"的码流；改选 8 位则得到兼容面最广的 Main Still Picture 码流。项目自身解码链（libheif→libde265）可解：`pp_verify` 对 `default_bd` 实测 PSNR 53.67 dB、`big10_lossy`（17.2 MP 10 位）`VERIFY OK`。
- 影响：默认配置把"最广兼容"换成"更窄兼容 + 更大码流"（同一张 8 位源：默认 992 B vs 8 位 1018 B，体积接近，但解码器要求更高）；严格按 PTL 选解码器的工具（部分硬件/系统 HEIF 扩展只认 Main/Main10）可能直接拒绝，且用户没有任何提示（位深行只在"仅一项可选"时才出注释，`page_output.cpp:646-653`）。仓内已知问题「libde265 对部分 HEVC RExt 码流不可解码」（AGENTS.md §2）与本类信号同源，但本项目 10 位产物实测可解，故外挂解码器风险仅推断。
- 建议：`pipeline.cpp:1731-1732` 把 heif 默认改回 8（或仅当批量源为 >8 位时默认 10）；`page_output.cpp` 在选中 10/12 位时用位深行注释位常显「10 位 HEIF 为 RExt 档，老解码器可能不支持」；并把 8 位（Main Still Picture）作为"兼容性最好"的显式档位说明。
- 状态：confirmed（实测 PTL + x265 源码）；"外部工具拒绝"为推断（未在第三方解码器上实测）。

##### HEIF-04 [P2] 档位标签「Main10（10 位 · Intra）」与产物 PTL 不符；提示「部分工具显示为 Main」方向错误

- 证据：
  - `src/ui/page_output_impl.h:218`：`{pp::HeifProfile::Main10, "Main10（10 位 · Intra）"}`；`:266-268`：`"Main10：10 位，x265 静止图写 Main10 Intra 档（部分工具显示为 Main）"`。
  - 实际码流（`out/default_bd`、`out/b12`、`out/lossless10`）：`general_profile_idc = 4`（H.265 的 Format Range Extensions），`general_profile_compatibility_flags = 0x08000000`（只有 profile 4 的位）。
  - 项目注释其实已知：`src/codecs/heif_profile.h:76-79`（"x265 对静止图固定写 main10-intra 档 … general_profile_idc=4"）、`src/codecs/enc_heif.cpp:658-663`（"HEVC 的 4:4:4 … idc=4"）——但只对 4:4:4 写了补正日志，10 位 4:2:0 的日志仍是 `heif_profile_label(Main10)`。
- 事实：UI 用「Main10」这一 HEVC 档名描述一个 `profile_idc=4`、且不带 Main10 兼容位的码流；工具显示的会是 RExt / profile 4，而不是文案承诺的 "Main"。
- 影响：解码兼容性判断被误导 —— 用户以为选了"Main10"就有 Main10 级兼容面，实际兼容面是 RExt；排障时按文案去查 Main10 会得到相反结论。
- 建议：`page_output_impl.h:218/266-268` 文案改为「10 位 · RExt（x265 main10-intra）」并删掉"部分工具显示为 Main"；`enc_heif.cpp:655-665` 的 profile 日志把"10 位/4:2:0 也是 RExt"这一分支补齐（当前只在 `chroma_idx==3 && heif==` 时补正）。
- 状态：confirmed（实测 PTL + 文案对照）。known=部分（`docs/param-catalog.md:124-127` 沿用 Main10 口径）。

##### HEIF-05 [P2] 无损开启时 `quality` 行仍可见可调但完全无效果（实测字节相同），无锁定、无提示

- 证据：
  - 参数表侧：HEIF 无静态 ParamDef，`apply_locks` 在 `tech==nullptr` 时直接返回（`src/core/params.cpp:485-487`），因此**引擎侧不存在任何 HEIF 参数锁定**；`quality` 的 `visible`/`locked` 谓词也无从声明。
  - 输入侧：`x265.cc:997`（quality → `param->rc.rfConstant`）与 `:1000`（`param->bLossless = p.value_int`）同时施加，`bLossless=1` 时 CRF 不再决定码率。
  - 实测：`--lossless` → 24225 B / sha `7fe3f20e0538ebc9`；`--lossless --param quality=90` → 24225 B / **同一 sha**（质量值被 HEIF-02 强制成 90 后仍零差异）。对照 `--lossless --param preset=placebo` → 24222 B / sha `926b3b8eb62c00e6`（preset 在无损下**确实**影响字节）。
- 事实：无损模式下 quality 是死旋钮；同组的 preset/tune 仍是活旋钮。
- 影响：用户开无损后继续调「质量」（这是最容易被调的一项）得不到任何反馈 —— 不报错、不灰、不变体积，只能靠猜；反之也说明 UI 对"无损后哪些旋钮还有意义"没有区分。
- 建议：给 HEIF 的 `quality` 加**运行时锁定**（`enc_heif` 之外需要一条能表达 runtime 后端锁定的通路：可在 `introspect_backends` 生成 ParamDef 时按 `lossless_capable` 注入 `locked` 谓词，读 `is_lossless(s)` → 强制 0），或至少在该行注释位/ tooltip 常显「无损模式下无效」。
- 状态：confirmed（实测同 sha）。known=no。

##### HEIF-06 [P2] `threads` 键不存在 → x265 默认线程池不受 `encode_threads` 约束（E3 缺口，日志仅 info）

- 证据：
  - 内省面（实测 `pp_linkprobe`）：`params=7 … list=quality|lossless|preset|tune|tu-intra-depth|complexity|chroma` —— 无 `threads`；插件注册表 `x265.cc:220-301` 亦只注册这 7 项。
  - `src/codecs/enc_heif.cpp:586-597`：`if (is_known("threads")) {...} else { log_info(..., "backend exposes no 'threads' parameter; default pool kept", ...); }` —— 只有 info，没有 warning/UI 提示。
  - 实测运行日志（`build/encoder-audit/out/lossless8/logs/run-*.log`）：`[sched] thread alloc {alloc_files=1 encode_threads=16 sum_encode_threads=16 thread_budget=16}` 紧接 `[encode] backend exposes no 'threads' parameter; default pool kept {encode_threads=16}` —— 预算按 16 记，实际 x265 另起自己的池。
- 事实：HEIF（以及同为 x265 的路径）每并发文件各自持有 x265 默认线程池，`ΣE ≤ T` 与"编码器内部线程全关"（AGENTS.md §4.3）在该后端不成立。
- 影响：批处理 HEIF 时 CPU 严重超额订阅（W 个文件 × 核数线程），吞吐反而下降；进度/线程预算面板给出的数字与实际不符。
- 建议：x265 有 `x265:frame-threads`/`x265:pools` 私有参数可透传（`enc_heif.cpp:520-534` 已支持 `x265:` 前缀），可在 `is_known("threads")==false` 时退化为 `heif_encoder_set_parameter_string(enc, "x265:pools", "none"/"1")` 之类受控映射；无论做不做，至少把这条缺口从 `log_info` 提到 `log_warn` 并在 E3 读数行显式标注。
- 状态：confirmed（内省面 + 日志）。known=yes（`docs/deep-review-0.3.2.md` R-078「x265 默认池不入账」，原文定级 P3）。

##### HEIF-07 [P3] `complexity` 的 UI 默认值 0 与插件声明的默认 50 不一致（`has_default=false` 被当成"当前值 0"）

- 证据：
  - `x265.cc:280-289`：`p->name = kParam_complexity; p->integer.default_value = 50; p->has_default = false;` —— 声明了 50 但明确"没有默认值"，`x265_set_default_parameters`（`:597-616`）因此**不施加**该键。
  - `src/codecs/enc_heif.cpp:155-181`：Int 分支用 `heif_encoder_get_parameter_integer(enc, key, &current)` 取"当前值"当 `def`；插件对未设置的 complexity 返回 `parameter().value_int = 0`（`x265.cc:55`、`:468-471`）→ `d.def = 0`。
  - 应用阈值 `x265.cc:1009-1020`：只有 ≥60 / ≥70 / ≥90 才有动作 ⇒ 0 与 50 行为等价。
- 事实：UI 显示"复杂档 0"并把它当下发值发出去（`paramform.cpp:879-891` 用 `p.def` 填默认，`:1345-1360`/`snapshot_state` 下发），插件对此值不做任何事；面板数值既不是库声明默认（50），也不代表"未设置"。
- 影响：默认值语义失真（用户以为默认是 0 档、实际库默认是 50 档且两者都不生效）；若将来 UI 把 0 当"最省"来解释会进一步误导。无字节后果。
- 建议：`param_def_from` 增加一条：`if (!heif_encoder_has_default(enc, key))` 时把 `def` 取 `ParamDef` 声明值（可从 `heif_encoder_parameter_get_valid_integer_range` + 一张白名单表补 50）或标记为"未设置"并允许空值；至少把 complexity 的 tooltip 写明"0＝不启用任何加速/增强档；库声明默认 50，50 亦不生效"。
- 状态：confirmed（头/源 + 实测 UI 路径推断默认显示 0）。known=no（R-230 只修了"值集参数取当前值"的一半）。

##### HEIF-08 [P3] E9「未知键忽略并记日志」对 libheif 格式实际不可达：管线在 `validate_run_config` 就硬拒绝

- 证据：
  - `src/codecs/enc_heif.cpp:599-605`：`// E9: unrecognised keys are ignored (never an error) and logged, not warned.` + `note_param("unrecognised parameter ignored", ...)`。
  - `src/core/pipeline.cpp:1673-1679`（`validate_run_config`）→ `cross_validate(...)`；`src/core/params.cpp:592-611`：该格式 declared_keys 可判定（HEIF 走 `introspect_backends` 并集，`params.cpp:148-158`）时，未知键产出 `"未知参数：" + key` 并作为 run 级错误返回。
  - 实测：`--param foo=bar` → `error: 未知参数：foo`，整次运行退出 1（0 个产物），`enc_heif` 的 E9 分支未被执行。只有 `x265:`/`svt:`/`aom:` 前缀被 `is_plugin_passthrough_key` 放行（`params.cpp:172-178`）。
- 事实：两处契约互相矛盾（编码器：忽略并记日志；参数引擎：硬拒绝）。有效行为是硬拒绝 + 中文报错，且发生在**编码前**（用户能立刻发现）。
- 影响：行为本身更安全，但 `enc_heif` 的 E9 注释/日志成为不可达代码，评审与文档（`design.md` §3.8 T6 口径⑥）据此得出的"未知参数只记日志"结论失真；也意味着一旦内省失败（`determinable=false`，`params.cpp:150-151`）未知键才会真的静默通过 —— 恰是最需要报警的时候最安静。
- 建议：二选一并统一口径 —— 要么删掉 `enc_heif.cpp:599-605` 的 E9 分支与注释（承认"未知键=run 级失败"），要么把 `cross_validate` 的未知键降级为 warning 并由编码器出 E9 行（前者更省事且更安全，建议前者 + 在 `design.md` 更正）。
- 状态：confirmed（实测 + 两处代码）。known=no。

##### HEIF-09 [P3] 档位选择不落预设、不进日志：预设往返丢失显式档位约束

- 证据：
  - `src/ui/preset_io.cpp:388-396`（`params_to_json` 跳过 `"__"` 前缀）与 `:398-416`（`params_from_json` 同样跳过）⇒ `__heif_profile` 永久不落盘；`src/core/params.cpp:632-644`（`snapshot_params` 跳过 `"__"`）⇒ 不进日志参数快照。
  - 口径声明见 `src/ui/page_output_impl.h:203-204`、`src/codecs/heif_profile.h:44-47`（保留键不落预设、不校验、不进日志）。
  - 会话内可复原：`src/ui/page_output.cpp:357-361`（`snapshot_state` 写入）与 `:685-696`（`apply_state_to_form` 读回）。
- 事实：档位行是"核心选项"（UX-10.1），但其选择只在当前会话的格式页签快照里存在；保存预设 → 重开 → 档位回「自动」。
- 影响：① 用户的显式档位约束静默消失（若该约束本来会拦截错误配置，则预设加载后不再拦截）；② 事后审计产物无从得知当初选了哪个档位（日志只有"derived"）；③ 对 HEIF 而言 `derive` 只是 (位深,色度) 的函数，故产物通常仍等价，实际损失集中在"约束"而非"画面"。
- 建议：给档位行一个**显式 schema 字段**（如预设 outputs[i].profile，与 bitdepth 同级，不借用保留键），或允许 `__heif_profile` 进预设（需要 `params_from_json` 破例并对该键做范围校验）。
- 状态：confirmed（代码链 + 口径注释）。known=partial（`docs/ux-review-14.md` §10 未把"落预设"列进验收）。

##### HEIF-10 [P3] 7 个内省参数的行标签就是英文 key，与 `param-catalog.md` 的中文标签不一致

- 证据：`src/codecs/enc_heif.cpp:150-152`：`d.key = name; d.label = d.key;` —— UI 行标签直接复用 key（`paramform.cpp:826-873` 用 `p.label`）；对照 `docs/param-catalog.md:115-119` 宣称的标签是「质量 / 画质预设 / 画质取向 / 色度采样」。
- 事实：HEIF 页签的核心行显示 `quality` / `preset` / `tune` / `chroma`，高级区显示 `tu-intra-depth` / `complexity`，而文档与其它格式（jxl/webp/jpeg 全中文标签）都是中文。
- 影响：中文界面里唯一一页"全英文参数名"；文档与界面不一致，用户按文档找"色度采样"找不到对应行名。
- 建议：在 `param_def_from` 里加一张 key→中文标签的小表（7 个键，与 `param-catalog.md` 逐字一致），未命中的键仍回落 `key`。
- 状态：confirmed（代码 + 文档对照）。known=partial（UX-10.2 只做了核心/高级分组，未做标签）。

##### HEIF-11 [P3] 无损且未请求 chroma 时仍输出「lossless forces chroma 444 (requested 420 …)」——"requested 420" 是回退值不是用户请求

- 证据：`src/codecs/enc_heif.cpp:562-566`：`const std::string requested = param_str(req.target.params, "chroma", "420"); if (requested != "444") note_param("lossless forces chroma 444 (requested " + requested + ...)`。
- 实测日志（`out/lossless8/logs/run-20261025-144118.log`）：`[warning] … lossless forces chroma 444 (requested 420; 4:2:0/4:2:2 would discard chroma and is not pixel-lossless) {param=chroma value=420}` —— 该次运行**根本没有传 chroma**（`params=[]`）。
- 事实：未传 chroma 时用回退值 "420" 生成了一条"用户请求 420"的警告；GUI 侧总会带 chroma，所以只影响 `--dev`/预设缺键场景。
- 影响：日志误导（排障者会以为有人显式要求 4:2:0）；不改变产物（仍是 444）。
- 建议：`enc_heif.cpp:562` 改为先判 `params.find("chroma")`，缺键时输出「无损强制 4:4:4（未指定色度）」而不是伪造 requested。
- 状态：confirmed（实测日志）。known=no。

##### HEIF-12 [P3] 「无损」实测口径：8 位 4:4:4 往返 ≈48.9 dB（非像素级），10 位才 >60 dB；该口径只写在 tooltip

- 证据：
  - `src/codecs/enc_heif.cpp:722-727`（像素面：float RGB → **全范围 BT.601** YCbCr，量化到 8/10/12 位）；`heif_nclx.h` 头注与 `enc_heif.cpp:833-839`（nclx 矩阵恒 6、full_range=1）。
  - UI 披露位置：`src/ui/paramform.cpp:514-519`（无损复选框 tooltip）与 `src/ui/page_output.cpp:571-582`（档位注释 tooltip）。
  - 实测 `pp_verify`（exact / psnr）：`base8`（有损 420，默认 q50）47.56 dB；`lossless8` **48.85 dB**（exact FAIL）；`lossless10` **>60 dB 通过**；`lossless8` 与 `chroma444` 48.85 vs 47.93 dB 说明 8 位误差主项是 YCbCr 量化而非压缩。
- 事实：HEIF 的"无损"= HEVC 码流层（YCbCr 平面）无损；8 位 4:4:4 的 RGB↔YCbCr 往返本身就有 ~1 LSB 误差，故 pp_verify exact 必然失败（这是格式属性，不是编码缺陷）。项目已在 tooltip 如实写明。
- 影响：与 JXL/PNG 同名的「无损」在 HEIF 上语义不同；tooltip 之外（格式卡、预设名、日志行）没有任何"这是 YCbCr 域无损"的提示，`ok=true` 的产物容易被当成逐位无损留档。
- 建议：把该口径从 tooltip 提一行常显短句（如无损行右侧注释位「无损＝YCbCr 平面（8 位往返有舍入）」）；`res.warnings` 或日志里在 `want_lossless` 分支补一条 info，便于审计追溯。
- 状态：confirmed（实测 PSNR/exact）。known=partial（`docs/param-catalog.md` 未记；tooltip 已记）。

##### HEIF-13 [P3] `__heif_profile` 取值不做类型校验：字符串值被静默当作「自动」，档位约束静默失效

- 证据：`src/codecs/enc_heif.cpp:648-649`：`const auto want_v = static_cast<HeifProfile>(param_int(req.target.params, kHeifProfileParamKey, 0));`；`param_int` 对非 `int64_t` 值返回 fallback（`params.cpp:425-432`），不报错。
- 实测：`--param __heif_profile=1`（dev_cli 只能给字符串，见 HEIF-02）产物与 `base8` **逐字节相同**（1018 B / sha `3d927bff66c82f8e`）—— 档位被当 Auto，既未生效也未失败。
- 事实：档位键的"类型正确性"没有任何守卫；`validate_params` 对该格式整体跳过（`params.cpp:527-532`），`cross_validate` 又把 `"__"` 键排除在外（`:603-604`），于是错类型一路静默。
- 影响：任何非 GUI 来源（手写预设、脚本、未来 CLI 面）写错类型时，档位约束不生效且无提示 —— 与 HEIF-01 的"档位判定"叠加后，档位这一层在最需要它的场景下最不可靠。
- 建议：`enc_heif.cpp:648` 改成显式取键并校验类型：缺失→Auto；存在但非 int64→`fail("编码档位参数类型错误")`；且把取值范围 `0..6` 之外的值也判失败（现在 `static_cast<HeifProfile>(99)` 只会因"不等于 derived"而失败，文案不准）。
- 状态：confirmed（实测 + 代码）。known=no。

#### 4. 输出兼容性与合规矩阵

源/配置 → 产物结构（实测 `build/encoder-audit/out/*`，结构由纯 Python BMFF 解析器读出）：

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| rgb8 → 8 位 / 420 / 默认 q50 | `ftyp heic(mif1,heic,miaf)`；1×`hvc1`；hvcC `profile_idc=3`、compat `0x70000000`、chroma=420、bd=8；`colr=nclx(1,13,6,full)`；`ispe 256²`、`pixi 3`；VUI 1/13/6 full | libheif→libde265 实测可解（PSNR 47.56 dB） | 低。nclx 矩阵 6 与平面转换一致；transfer=13 与 matrix=6 组合少见但自洽（解码侧先逆矩阵再套 sRGB TRC） |
| rgb8 → **默认（10 位）** | `ftyp heix`；hvcC `profile_idc=4`、compat `0x08000000`、bd=10；SPS RExt 约束位非零但 hvcC 6 字节约束标志全 0 | 实测可解（53.67 dB）；17.2 MP 亦然（`VERIFY OK`） | **中**：容器 PTL 只声明 RExt、无 Main10 兼容位（x265 `level.cpp:143-144` + libheif `hevc_boxes.cc:628-630`）。严格按 hvcC 选解码器的工具可能拒绝（未实测第三方） |
| rgb8 → 12 位 / 420 | 同上，hvcC bd=12、`profile_idc=4`、level=60 | 实测可解（54.27 dB） | 同 10 位；12 位硬件面更窄 |
| rgb8 → 8 位 / `chroma=444`（有损） | hvcC chroma=444、`profile_idc=4`、compat `0x08000000`；VUI 1/13/6 | 实测可解（47.93 dB）；17.2 MP 444 有损亦 `VERIFY OK` | 中：4:4:4 即 RExt；查看器兼容性弱（UI 档位行 Auto 下已显示「Main 4:4:4」提示） |
| rgb8 → 8 位 / 422 | hvcC chroma=422、`profile_idc=4` | 实测可解（47.63 dB） | 中：4:2:2 老解码器支持面窄 |
| rgb8 → **无损（`--lossless`）** | hvcC chroma=444、`profile_idc=4`、`general_level_idc=255`（x265 在 `bLossless` 时固定写 Level 8.5，`level.cpp:160-167`）；VUI 1/13/6 | 实测可解，PSNR 48.85 dB（=8 位 YCbCr 往返上限），exact 必然 FAIL | 中：①`level_idc=255` 非 H.265 合法 level 值（解码器普遍忽略，但严格校验器会报）；②RExt；③"无损"非逐位 |
| rgb8 → 无损 + `--param chroma=420` | 与上一条**逐字节相同**（24225 B / 同 sha） | 同上 | 无额外风险：强制 444 生效并有 warning 记录 |
| rgb8 → 无损 / 10 位 | hvcC chroma=444、bd=10、level=255 | 实测 PSNR **>60 dB 通过** | 同上；10 位下往返误差小 4× |
| rgba8 → 8 位 / 420（alpha） | 2 items：主 `hvc1`(420) + alpha `hvc1`(mono, `profile_idc=4`)；`auxC=urn:mpeg:hevc:2015:auxid:1`、`auxl` 2→1；各自 `ispe`/`pixi`(3/1)；主图 `colr=nclx`；alpha 无 colour_description | 实测可解（48.06 dB，含 alpha 通道） | 低。alpha 走辅助图 + `auxC` 是标准做法；alpha 单色 RExt 同样受 hvcC 约束标志清零影响 |
| rgba8 → 10 位无损（alpha） | 同上，bd=10；`heix` | 实测 **>60 dB 通过** | 低 |
| rgb8 / 4200×4096（17.2 MP）→ 无损 | `primary=grid(4200×4096)` + 6×`hvc1`(2048²) + `dimg`；grid 与 tile 均带 `ispe`/`pixi`/`nclx`；tile hvcC 444/bd=8/level=255 | 实测可解，PSNR 48.85 dB —— 与小图无损**数值完全一致** | 低（标准 grid 派生图）。ICC 场景见下一行 |
| rgb8 → 无损 + `--color p3`（17.2 MP） | grid 主项带 `colr=prof(ICC 580 B)`；tile 同样带 ICC；VUI primaries=**12** | 实测可解 | 低：ICC 落在 grid 主项上（不丢），CICP 与 ICC 同源 |
| gray8 → 8 位 | 3 通道 420（**不保留灰度**）+ sRGB ICC(588 B) + `nclx` 被 ICC 取代；sidecar `warnings=[GrayToRgbEncoded]`、`color_dst=sRGB` | 可解；`pp_verify` 因通道数 1 vs 3 拒绝比较（按设计） | 低但需知情：文档口径"以 RGB 编码 + 日志"已兑现（`pipeline.cpp:856-863`） |
| rgb8 → `--color p3` / `adobergb` / `srgb` | 容器只写 `colr=prof(ICC)`（libheif 在 ICC 在场时丢弃 nclx，见 `heif_nclx.h:11-12`）；**码流 VUI**：P3→(12,13,6)、AdobeRGB→(**2**,**4**,6)、sRGB→(1,13,6)，full=1 | 实测可解（P3 PSNR 低是色彩空间换算所致，非缺陷） | 低：CICP 与 ICC 同源；AdobeRGB 用 primaries=2(unspecified) 不冒充 sRGB 是正确取向 |
| rgb8 + EXIF/XMP | items `1 hvc1`、`2 Exif`、`3 mime`；`cdsc` 2→1、3→1；Exif 载荷 = 4 字节 offset(0) + `II*\0…`（libheif 自行扫描补 offset，`context.cc:1882-1911`）；XMP 为 `application/rdf+xml` | 可解，`HEIF-AUDIT`/`AUDIT-XMP` 均在载荷内 | 低：符合 ISO/IEC 23008-12 Exif item 的 4 字节 offset 约定（`heif_metadata.h:78-85`） |
| 320×200 + `Orientation=6` 源 → 8 位 HEIF（默认开方向旋转） | `ispe=**200×320**`（像素已转 90°）；Exif item 内 `Orientation=**1**`（归一，不重复旋转）；无 `irot`/`imir` 属性 | 可解 | 低：方向变换在像素面完成并同步归 EXIF（`pipeline.cpp:969`），容器侧不叠加二次旋转 |

#### 5. 未覆盖与存疑

1. **GUI 交互未实测**（用户禁令：不跑 GUI / ui-smoke）：档位行渲染态、无损复选框联动、位深下拉默认项、`已改 N` 胶囊对档位的计数，全部按代码判定（`page_output.cpp` / `paramform.cpp`）。HEIF-01 的"选中后必失败"由纯函数链推出，未在界面上点一次。
2. **`__heif_profile` 无法从本审计的可用入口注入**（两重阻塞：dev_cli 只给字符串（HEIF-02）→ `param_int` 落回 Auto；预设读写都跳过 `"__"` 键（HEIF-09））。因此 HEIF-01 的"编码期拒绝"这一环没有端到端实测，仅有代码证据。
3. **16 MP grid 的"必要性"无法 A/B**：绕过 grid 需改 `src/codecs/enc_heif.cpp`（禁止）。只验证了 grid 产物可解码、像素与单图路径数值一致；`enc_heif.cpp:862-871` 关于"libde265 单图 >16 Mi 无损解码失败"的注释未独立复现。附带实测排除了相邻假设：**有损** 444 与 10 位在 17.2 MP 下均能正常解码，故该上限只在无损路径成立。
4. **12 位 + alpha 组合未逐一实测**（只测 8/10 位 + alpha；12 位无 alpha 已测）。
5. **x265 线程池实际并发数未采样**（无进程级线程观测）：HEIF-06 的"超额订阅"按日志口径（`encode_threads=16` 未入账）与 x265 默认池行为判定。
6. **第三方解码器兼容性未实测**：10 位/4:4:4 产物的 RExt PTL 与 hvcC 约束标志清零是否会导致 Windows HEIF 扩展/硬件解码拒绝，只有源码与规范推断（故 HEIF-03 的影响段标注为推断）。
7. **`complexity` 在 GUI 的默认显示值未截图核对**：按 `param_def_from` 取"插件当前值"的代码路径推定 0，与 `pp_linkprobe` 的 `params=7` 面一致但未打印默认值。
8. **未覆盖 AVIF**（另一格式的审计范围），尽管两者共用 `enc_heif.cpp`；本文只在"判据复用导致 HEIF 预检被 AVIF 命中"处提到 AVIF（HEIF-01）。
9. 实测命令与产物：`build/encoder-audit/tools/{mkimg,mkbig,runmatrix,heifbox,spsvui,redump,verify,orient}.py`；矩阵结果 `build/encoder-audit/results/matrix.json`；产物 `build/encoder-audit/out/<case>/`；`pp_verify` 用例 `build/encoder-audit/verify/<case>/`。命令一律 `python tools/env.py run -- build/release-dev/{photopipeline,pp_verify}.exe ...`。
10. **方向变换只测了"开启"路径**（默认开）：未测关闭旋转时 EXIF `Orientation=6` 是否原样保留 + 容器不添加 `irot`（HEIF 无 irot 写入面，可能依赖查看器读 EXIF，属跨格式问题）。

### 4.8 AVIF（libheif / SVT-AV1 + libaom）

> 审计基线：`0.3.5.alpha2`，`build/release-dev/photopipeline.exe`（`PP_BUILD_DEV=ON`）。
> 库版本实测（`<out>/logs/run-*.log` 的 version 行）：libheif **1.23.5**、svt-av1 **v4.2.0**、
> aom **v3.15.1**（`CONFIG_AV1_HIGHBITDEPTH=1`，`vcpkg/buildtrees/aom/x64-windows-avx2-rel/config/aom_config.h`）、
> exiv2 0.28.9、OIIO 3.1.17.0。
> 源码证据根：`vcpkg/buildtrees/libheif/src/v1.23.5-6368e7938c.clean/libheif/`（下文简称
> `plugins/encoder_svt.cc`、`plugins/encoder_aom.cc`、`codecs/avif_boxes.cc` 均指该前缀下的文件）、
> `vcpkg/buildtrees/svt-av1/src/v4.2.0-93c4505a5d.clean/Source/`。
> 本审计只读：未改 `src/`、`tests/`、`docs/`、CMake；产物全在 `build/encoder-audit/avif-rt/`。
> 实测入口：`python tools/env.py run -- build/release-dev/photopipeline.exe --dev …`（`--workers 1`），
> 结构解析用自写纯 Python 解析器（`build/encoder-audit/avif-rt/tools/avif_probe.py`，
> ISO-BMFF / av1C / colr / pixi / iref / iloc 逐字节读），像素断言用 `pp_verify`。

#### 1. 结论

1. **P1：AVIF + SVT-AV1 的 GUI 默认配置实际上以 `qp=0` 编码**。插件的 `qp` 是"可选覆盖"
   （`has_default=false`，未设置时 `get_parameter_integer` 返回 unsupported），内省把它变成
   `Int 0–63` 且**默认值回退到下限 0**（`src/codecs/enc_heif.cpp:177`），而 GUI 把内省默认值
   **全量**写进参数集（`src/ui/paramform.cpp:889`）→ 每次编码都会显式下 `qp=0` →
   SVT 走 `qp_set` 分支（`plugins/encoder_svt.cc:862-876`）→ **CQP 0.00**。
   实测同一张 256×192 图：默认（不带 `qp`）3079 B / CQP 31.50；GUI 默认参数集 **38214 B / CQP 0.00**
   （**12.4×**）；10 位下 1975 B → **62521 B（31.6×）**；独立复核在另一张 256×256 纹理上测得 17652 B → 127141 B（**7.2×**），量级一致，倍数随内容变化。且 `qp` 一旦在场，`quality` 完全失效
   （`qp=0,quality=30` 与 `qp=0,quality=50` 字节完全相同）。侧车 `warnings=[]`，界面无任何提示。
2. **P1：AVIF 选 SVT-AV1 时"无损编码"复选框可点，但按下必失败**：SVT 插件**没有 chroma 参数**
   （`plugins/encoder_svt.cc:264-273` 整段 `#if 0`），而 `enc_heif` 的无损路径要求
   `is_known("chroma")` 才能保证 4:4:4，否则直接 `fail`（`src/codecs/enc_heif.cpp:554-567`）。
   实测 `--lossless --backend svt-av1` → `encode failed: lossless requested but backend exposes no
   'chroma' parameter`。UI 侧该复选框由 `lossless_capable` 决定显示，而 SVT 插件声明
   `supports_lossless_compression = true`（`plugins/encoder_svt.cc:1309-1310`）→ 复选框必然出现。
   叠加事实：libsvt 本版**只接受 4:2:0**（`Source/Lib/Globals/enc_settings.c:464-467`
   "Only support 420 now"），所以 SVT 的"无损"在原理上不可达。
3. **P2：两个高级参数被"覆盖/钉死"，UI 上却都像能用**。① `threads`：参数循环先按用户值设，
   随后 E3 段无条件用 `req.encode_threads` 覆写（`src/codecs/enc_heif.cpp:586-597`），E 由调度器按
   `clamp(T/W,2,16)` 分配（`src/core/scheduler.cpp:137-143`）——本机 `workers=1` 时 `T=16,W=1 → E=16`，
   运行日志实测 `encode_threads=16`；`threads=1` 与 `threads=8` 产物逐字节相同，SVT 还报
   `Level of parallelism … [0-6] … Setting maximum parallelism level`。
   ② libaom `alpha-quality`："未设置则继承 quality"（`plugins/encoder_aom.cc:626-629`），但内省默认值就是
   当时的 quality=50，GUI 每次显式下发（`:588-596` 置 `alpha_quality_set`，编码期 `1064-1066` 用它替换
   quality）。实测 `quality=95`：带 `alpha-quality=50` → 8717 B；`=95` → 8760 B；**完全不传** → 8760 B
   （与 aq=95 同一 sha）⇒ 提高 quality 只让颜色变好，alpha 仍停在 Q50。
4. **P2：AVIF"编码档位"行的两个联动缺陷**。① `current_chroma()` 用 `param_int()` 读 **string 型**的
   `chroma`（`src/ui/page_output.cpp:463`），非 int64 一律回落 1 ⇒ UI 永远以为在 4:2:0：注释位恒显示
   "插件自选 → Main"（而 `chroma=444` 实测产物是 **High**，av1C `seq_profile=1`/`subsampling=(0,0)`）；
   用户手选 High 时 UI 判"不一致"标红（`:562`），但 `enc_heif` 用真实布局反查会成功
   （`src/codecs/enc_heif.cpp:641-654`）；`chroma==3` 的 "Main 4:4:4" 补正分支永不成立。
   ② `profile_allowed_bitdepths(Main) = {8}`（`src/ui/page_output_impl.h:243-245`）对 AVIF 误用（AV1 Main
   本含 10 位，下拉文案自己写的就是"Main（8/10 位 …）"，`:224`；实测 `aom 10 位 4:2:0` 产物 av1C
   `seq_profile=0, high_bitdepth=1`）⇒ 用户一选 Main，位深就被静默收窄到 8 位并回落，无任何说明
   （`src/ui/page_output.cpp:619-645`）。
5. **P2：SVT 的速度档 10–13 全被映射到 9，而默认值恰好是 12**。插件声明 `0–13`、默认 `12`
   （`plugins/encoder_svt.cc:194-200`）；libsvt 对 all-intra（AVIF 走 `avif=true`）把 `>M9` 全部映射到 M9
   （`Source/Lib/Globals/enc_handle.c:4295-4298`）。实测 SVT 自己打印 `Preset M12 is mapped to M9`
   （默认）/ `M10→M9` / `M13→M9`，且 `speed=10`、`speed=13` 产物与默认**同一 sha**。UI 的 14 档里有 4 档是死档。
6. **P2：三条"UI 允许但必失败/结构出格"的路**：① libaom `tune=iq` + 无损 → libheif 直接报错
   （`plugins/encoder_aom.cc:1129-1133`），实测失败信息是英文 libheif 文案；② 无损且 >16 MP 时
   **AVIF 也走 HEIF grid 派生项**（`src/codecs/enc_heif.cpp:873-878` 没有格式门），实测 4100×4100
   无损产物是 `grid` 主项 + 9 个 2048² 的 `av01` 瓦片（AVIF 生态主流实现不覆盖 grid）；
   ③ 无损 + alpha + >16 MP 直接失败，文案还是 "HEIF lossless …"。
7. **P2（工具面）/ P3（文档）**：`--dev --param` 对 AVIF **任何类型化参数都不可用**——`lookup_param_def`
   只查静态表，AVIF 静态 techs 为空 → 键被当字符串塞入（`src/dev_cli.cpp:384-393`）→ `enc_heif` 的
   Int/Bool 分支以 "expected integer/boolean" 拒绝整个文件；只有 `quality`/`lossless` 走专用回退，
   而 `quality` 的回退默认值是 **90**（`src/codecs/enc_heif.cpp:501-508`）→ 实测 `--param quality=70`
   产出与 `quality=90` 完全相同（10064 B），用户设置的 70 被静默替换。同源后果：
   `docs/param-catalog.md:144-161` 的 AVIF 章节与实现漂移（SVT 的 `chroma` 行、libaom 的级别/范围/
   默认、`{8,10}` 位深），见 AVIF-13。
8. 正面对照（逐项实测通过）：8/10/12 位（libaom）、8 位 + alpha（两端）、12 位 + alpha（libaom）、
   4:2:2/4:4:4（libaom）、无损（libaom，强制 4:4:4）、EXIF/XMP 注入、nclx 与 ICC 的一致性推导、
   SVT 12 位被运行时探测正确剔除（`supported=8,10`）、>16MP 位深回读校验。
   容器结构（ftyp/av1C/pixi/colr/auxC/iref/iloc）与像素语义一致，见 §4。

#### 2. 参数全量矩阵

事实源：静态表 `src/core/format_tables.cpp:886-894`（avif：`backends={svt-av1, libaom}`、
`bitdepths={8,10,12}`、`supports_alpha=true`、`supports_gray=false`、**techs 为空** ⇒ 参数全部来自
`introspect_backends()`/`introspect_live()`，`src/codecs/enc_heif.cpp:233-239,1286-1319`）。
UI 建行：`src/ui/paramform.cpp:742-801`（每 `ParamDef` 一行；`lossless` 行不渲染，由复选框独占，`:796-800`）。
下发：`src/ui/page_output.cpp:706-737`（`spec_of`，**全量**写 `form->values()`）→
`src/core/pipeline.cpp:1739-1767`（`build_output_specs`：`default_params`→`base_params`→`fill_defaults`→
`apply_locks`；对 AVIF **后三步全是空操作**，因为 `select_tech` 对空 techs 返回 nullptr，
`src/core/params.cpp:470-476,479-519,614-630`）→ `src/codecs/enc_heif.cpp:436-534`（按下发键名逐个
`heif_encoder_set_parameter_*`）。

「生效」口径：**是** = 实测改参数改字节；**条件** = 仅在特定组合/内容下可观察；**否** = 实测不生效或必失败；
**未知** = 本次未实测。

##### 2.1 SVT-AV1（默认后端；`format_tables.cpp:888-889` 首个）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `speed` | 是（核心行） | **12** | 0–13 Int（`plugins/encoder_svt.cc:194-200`） | `enc_heif.cpp:443-455` → `plugins/encoder_svt.cc:472,890` | 是 | 10–13 全部被 libsvt 映射到 9（`enc_handle.c:4295-4298`）；实测 sp0=1967 B/142 ms、sp4=1901 B、sp8=1980 B、sp10=sp13=默认=3079 B。**默认 12 落在死区**（AVIF-07） |
| `quality` | 是（核心行） | 50 | 0–100 Int | `enc_heif.cpp:436-455` → `plugins/encoder_svt.cc:457-459,866` | 条件 | 仅当 `qp` 未下发才生效（`866-867`）：默认 50→CQP 31.50/3079 B，90→CQP 6.25/10064 B；`qp` 在场时完全失效（AVIF-01） |
| `lossless` | 否（复选框独占） | false | Bool | `enc_heif.cpp:505-508,543` → `plugins/encoder_svt.cc:512-514` | **否** | 无损路径先因"无 chroma 参数"失败（`enc_heif.cpp:554-557`）（AVIF-02） |
| `chroma` | **否（插件未暴露）** | —（恒定 420） | `#if 0`（`plugins/encoder_svt.cc:264-273`）；`svt_query_input_colorspace2` 恒返回 420（`:689-695`） | `enc_heif.cpp:613-627` | 否 | 实测 `--param chroma=444`：产物与默认同 sha（3079 B），日志 `unrecognised parameter ignored` + `backend exposes no chroma parameter; forced 420` |
| `tune` | 是（核心行） | `psnr` | vq/psnr/ssim/iq/ms-ssim（`plugins/encoder_svt.cc:125`，SVT≥4 ⇒ 5 项全在） | `enc_heif.cpp:456-475` → `plugins/encoder_svt.cc:561-585,887` | 是 | 实测 q50 下：默认 psnr=3079 B、vq=3079 B（**不同 sha**）、ssim=3111 B、iq=4675 B；SVT 日志逐项确认 `9 / VQ|SSIM|IQ / all intra` |
| `threads` | 是（高级·编码工具） | 4 | 1–16 Int（`plugins/encoder_svt.cc:206-216`） | `enc_heif.cpp:586-597`（**覆写**）→ `plugins/encoder_svt.cc:471,851` | **否** | 恒被 `req.encode_threads` 覆盖为 E（本机 16，SVT 再 clamp 到 6）；实测 thr1/thr8 同字节（AVIF-03） |
| `tile-rows` | 是（高级） | 4 | 整数集 {1,2,4,8,16,32,64}（`plugins/encoder_svt.cc:219-227`；内省成 Enum，`enc_heif.cpp:163-171`） | `enc_heif.cpp:456-475` → `plugins/encoder_svt.cc:473,883` | 是 | 实测 1×1=2751 B vs 4×4=3079 B（插件默认 4 比 1 大 12%） |
| `tile-cols` | 是（高级） | 4 | 同上（`plugins/encoder_svt.cc:229-238`） | 同上 → `:474,884` | 是 | 同上 |
| `min-q` | 是（高级） | 0 | 0–63 Int（`plugins/encoder_svt.cc:288-299`） | `enc_heif.cpp:443-455` → `:469,880` | 未单独实测 | 与 `qp/quality` 共同决定 `min_qp_allowed` |
| `max-q` | 是（高级） | 63 | 0–63 Int（`:301-312`） | 同上 → `:470,881` | 是 | 实测 `max-q=20` → 5122 B（比默认 3079 B 大 = 提高质量下限） |
| `qp` | 是（高级） | **0（= 下限回退）** | 0–63 Int，`has_default=false`（`:275-286`）；未设置时 get 返回 unsupported（`:489-495`） | `enc_heif.cpp:443-455` → `plugins/encoder_svt.cc:463-467,862-876` | 是（语义错） | **AVIF-01 的根**：内省默认落 0 且总会下发 → CQP 0.00；实测 qp=31→3138 B/CQP 31.00、qp=63→608 B/CQP 63.00 |

##### 2.2 libaom（`format_tables.cpp:890-891` 第二个）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `speed` | 是（核心行） | 6 | 0–9 Int（`plugins/encoder_aom.cc:293-309`，aom≥3 → max 9） | `enc_heif.cpp:443-455` → `plugins/encoder_aom.cc:611,1185` | 是 | 实测 sp0=2159 B/326 ms、sp6=2807 B、sp9=4031 B |
| `quality` | 是（核心行） | 50 | 0–100 Int（`:330-341`） | `enc_heif.cpp:443-455` → `plugins/encoder_aom.cc:582-584,1143` | 是 | `AOM_Q` + `cq_level=((100-q)*63+50)/100`（`:1052,1143`）：实测 q50=2807 B、q90=8754 B、q70（preset）=4411 B |
| `lossless` | 否（复选框独占） | false | Bool（`:343-349`） | `enc_heif.cpp:505-508,543` → `plugins/encoder_aom.cc:519-535,1218-1220` | 是 | 实测 8 位 59513 B（8→10 位 97157 B）；同时把 `min_q/max_q/alpha_*_q` 归零（`:523-530`） |
| `chroma` | 是（核心行） | `420` | 420/422/444 String Enum（`:351-358,698-714`） | `enc_heif.cpp:607-627` → `plugins/encoder_aom.cc:840-851,963` | 是 | 实测 420=2807 B / 422=2798 B（av1C csub=(1,0)）/ 444=3197 B（csub=(0,0), profile 1） |
| `tune` | 是（核心行） | `auto` | auto/psnr/ssim/iq（`:199-202,360-367`；`iq` 由 `aom/aomcx.h:1788 #define AOM_HAVE_TUNE_IQ 1` 真正可用） | `enc_heif.cpp:456-475` → `plugins/encoder_aom.cc:716-741,1091-1127` | 是 | 实测 auto(=IQ，`:1120-1124`)＝iq=2807 B（同 sha）、psnr=2471 B、ssim=2486 B。**无损 + iq 必失败**（`:1129-1133`，AVIF-08） |
| `threads` | 是（高级） | **16（= `hardware_concurrency()`，随机器变）** | 1–64 Int（`:311-328`） | `enc_heif.cpp:586-597`（覆写）→ `plugins/encoder_aom.cc:610,1155` | **否** | 同 SVT（AVIF-03） |
| `min-q` | 是（高级） | 0 | 0–63 Int（`:369-380`） | `→ :608,1054-1055,1152` | 未单独实测 | 与 aom≤2.0.2 的特殊分支互斥（`:1150-1152`） |
| `max-q` | 是（高级） | 63 | 0–63 Int（`:382-393`） | `→ :609,1153` | 未单独实测 | — |
| `alpha-quality` | 是（高级·编码工具） | **50（= 当时的 quality）** | 0–100 Int，`has_default=false`（`:395-405`） | `→ :588-596,1064-1066` | 是 | 默认值被显式下发 → alpha 恒 Q50（AVIF-04）：实测 q95 下 aq50=8717 B vs aq95=8760 B vs 不下发=8760 B |
| `alpha-min-q` | 是（高级） | 0（= min-q） | 0–63 Int，无默认（`:407-417`） | `→ :597-601,1057-1060` | 条件 | 仅在 alpha 面且 `alpha_*_q_set` 时替换 `min_q/max_q` |
| `alpha-max-q` | 是（高级） | 63（= max-q） | 0–63 Int，无默认（`:419-429`） | 同上 | 条件 | 同上 |
| `lossless-alpha` | 是（高级） | false | Bool（`:431-437`） | `→ :655-663,1088-1089` | 条件 | 仅影响 alpha 面；实测对无 alpha 输入无差异（与 lossless 同 sha） |
| `auto-tiles` | 是（高级） | false | Bool（`:439-445`） | `→ :664-666,1196-1199` | 是 | 实测 true → 2856 B（≠2807 B） |
| `enable-intrabc` | 是（高级） | true | Bool（`:447-453`） | `→ :667-669,1230` | 条件 | 实测 false 与默认同 sha（自然图像无 intrabc 命中）——非"不生效"，是内容无关 |
| `realtime` | 是（高级） | false | Bool（`:285-291`） | `→ :672,996-998` | 是 | 实测 true → 3641 B（切 `AOM_USAGE_REALTIME`） |

##### 2.3 非插件参数（UI 行 / 引擎侧）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `out_bitdepth`（位深行） | 是（`page_output.cpp:256-281`） | **10**（`pipeline.cpp:1731-1732`） | 静态 {8,10,12} ∩ 运行时探测（`format_policy.h:47-56`） | `enc_heif.cpp:384-387,629`；探测 `:1321-1363`（逐位深真编码 64×64 回读） | 是 | 实测 svt 探测结果 `supported=8,10`（12 位失败于 `enc_settings.c:454-457`），CLI 明确拒绝：`avif/svt-av1 does not support 12-bit (supported: 8,10)`；libaom 8/10/12 全通（av1C `high_bitdepth/twelve_bit` 正确） |
| `backend`（后端行） | 是（`paramform.cpp:456-474`，2 后端） | `svt-av1` | svt-av1 / libaom | `enc_heif.cpp:81-100,397-401`（id_name 子串匹配） | 是 | `canonical_backend_id` 把运行时 `svt`/`aom` 归一为冻结 id |
| `tech`（技术行） | 否（单合成技术 `runtime`，`enc_heif.cpp:1304-1305`；行在 `techs.size()>1` 时隐藏，`paramform.cpp:585-587`） | `runtime` | — | 同上 | — | 无损技术选择在 AVIF 无意义 |
| `lossless`（复选框） | 是（`paramform.cpp:500-521` 建行、`:599-602` 按 `first_lossless_tech_in` 显隐；`supports_lossless_compression` 两端皆 true） | false | Bool | `params.cpp:307-310`；`page_output.cpp:730` 写 `__lossless`；`paramform.cpp:913-915` 同写显式 `lossless` | 见各后端 | 两端可点，但 SVT 一侧必失败（AVIF-02）；libaom 一侧强制 444（`enc_heif.cpp:554-567`） |
| `__heif_profile`（档位行） | 是（`page_output.cpp:230-253`；AVIF 4 档：自动/Main/High/Professional，`page_output_impl.h:222-227`） | `Auto` | 0–6 枚举 | `enc_heif.cpp:641-666`（反查 + 不一致即 `fail`） | 条件 | 与真实（位深,色度）推导不一致 → **硬失败**；UI 侧推导因 AVIF-05 用的是错的色度 |
| `threads`（E3 映射） | 见上（被列成一行） | E（调度器分配） | 1..16 | `scheduler.cpp:137-143` → `pipeline.cpp:108-111` → `enc_heif.cpp:586-597` | 是（但压过用户值） | 实测 `workers=1,T=16 → E=16` |

**未进入 UI 的库能力**（"全参数暴露"差距，均为**库限制**而非本仓漏接）：
libaom 侧没有任何 tile 控制（`plugins/encoder_aom.cc:1201` 明确 `TODO: set AV1E_SET_TILE_ROWS and
AV1E_SET_TILE_COLUMNS`），只有 `auto-tiles`；SVT 侧没有 `chroma`/alpha 专用质量；
`aom:`/`x265:`/`svt:` 私有透传键在 UI 无输入入口（只能走预设 JSON / `--param`，而 `--param` 见 AVIF-10）。

#### 3. 发现

##### AVIF-01 [P1] SVT-AV1 的默认 GUI 配置以 `qp=0`（CQP 0.00）编码，`quality` 失效，体积 7–32×（随图像内容）
- 证据：
  - `vcpkg/buildtrees/libheif/src/v1.23.5-6368e7938c.clean/libheif/plugins/encoder_svt.cc:275-286`
    （`p->name = kParam_qp; … p->has_default = false; minimum 0; maximum 63`）、
    `:489-495`（`if (!encoder->qp_set) return heif_error_unsupported_parameter;`）。
  - `src/codecs/enc_heif.cpp:177`
    `d.def = int64_t(have_current ? current : (have_min ? mn : 0));` —— get 失败（没"当前值"）时
    **默认值取范围下限 0**，不是"未设置"。
  - `src/ui/paramform.cpp:879-891`（`make_defaults()`：无静态 tech 时 `for (p : params) s[p.key] = p.def`）、
    `:1362-1368`（`public_values()` 只剔除 `__` 前缀）→ `src/ui/page_output.cpp:728-729`
    （`spec.params[k] = v` 全量下发）→ `src/core/pipeline.cpp:1747-1750`（`fill_defaults` 对 AVIF 是空操作）。
  - `plugins/encoder_svt.cc:463-467`（`qp` 置位并 `qp_set=true`）、`:862-876`
    （`if (encoder->qp_set) qp = encoder->qp;` → `svt_config.qp = quarter_qp_steps/4`）。
- 事实：同一 256×192 RGB8 输入，
  `--format avif --backend svt-av1 --bitdepth 8`（不带 `qp`）→ **3079 B，SVT 日志 `BRC mode / CQP
  Assignment : CQP / 31.50`**；把 GUI 会下发的整份参数集（`quality=50 qp=0 speed=12 threads=4
  tile-rows=4 tile-cols=4 min-q=0 max-q=63 tune=psnr`，经预设 JSON 保型）喂进去 → **38214 B，`CQP / 0.00`**；
  10 位（AVIF 的默认位深）同样对比：1975 B → **62521 B**。侧车
  `build/encoder-audit/avif-rt/out/svt-gui-8bit/grad_rgb8.avif.pp.json` 记录
  `params: "… qp=0 quality=50 …"`、`warnings: []`。把 `quality` 改成 30（`qp` 仍在）→ **与 quality=50 逐字节相同**。
- 影响：每个用默认设置导出的 AVIF（SVT 是默认后端）都会得到 QP 0 的近无损码流：体积 12–32×、
  编码/解码都更慢，而"质量"滑杆（核心行）完全不起作用。用户看到的是"AVIF 特别大"，
  没有任何日志/侧车/UI 提示异常（`qp` 的 `def` 与当前值相同，连"已改 N"计数都不亮，
  `page_output.cpp:394-403`）。
- 建议：把内省参数的"插件没有当前值"表达成第三态，而不是回退到下限：
  ① `ParamDef` 末尾**加性**追加字段（如 `bool def_is_implicit = false`），`enc_heif.cpp:177`
  在 `have_current == false` 时置真；② `paramform.cpp:888-889` 的 `make_defaults()` 跳过
  `def_is_implicit` 的键（只有用户动过才写入工作集）；③ `enc_heif.cpp` 的 Int 分支
  对"仍在隐式默认"的键不下发（需把该标志随 `ParamSet` 旁路传入，或改由 `ParamDef`
  在下发前比对 `def`，与 `page_output.cpp:394-403` 的"已改"判据同源）；
  ④ 兜底：`svt` 的 `qp` 在 `enc_heif` 侧一旦发现 `quality` 也在集合里就跳过 `qp`
  （语义上 `quality` 是用户面、`qp` 是专家面，二者同时在场应以用户面为准）。
  回归：`tests/unit/test_params` 加"内省默认不得被当成显式值"断言，金样加一条
  `avif-svt-default`（断言字节数与无 `qp` 路径一致）。
- 状态：confirmed；known=no。

##### AVIF-02 [P1] AVIF + SVT-AV1：「无损编码」复选框可点，但按下必失败
- 证据：
  - `plugins/encoder_svt.cc:264-273`：`chroma` 参数整段被 `#if 0` 关掉（`:131-136` 的
    `kParam_chroma_valid_values` 同样注释掉）；`:84` 注释 `// SVT-AV1 only supports 4:2:0 as of v3.0.0`；
    `:689-695` `svt_query_input_colorspace2` 恒返回 `heif_chroma_420`。
  - `src/codecs/enc_heif.cpp:554-557`
    `if (!is_known("chroma")) return fail("lossless requested but backend exposes no 'chroma'
    parameter; cannot guarantee 4:4:4 (true lossless)");`
  - UI 侧可见性：`src/ui/paramform.cpp:599-602`（`first_lossless_tech_in(b)` 决定复选框显隐；
    建行在 `:500-521`，且 `:514-519` 的 tooltip 已承诺"本格式无损强制 4:4:4"）→
    `TechDef::lossless_capable` 来自 `heif_encoder_descriptor_supports_lossless_compression`
    （`src/codecs/enc_heif.cpp:1306`），而 SVT 插件声明 `supports_lossless_compression = true`
    （`plugins/encoder_svt.cc:1309-1310`）。
  - 库面根因：libsvt 本版只接受 4:2:0（`vcpkg/buildtrees/svt-av1/src/v4.2.0-93c4505a5d.clean/
    Source/Lib/Globals/enc_settings.c:464-467`，"Only support 420 now"）。
- 事实：`--lossless --backend svt-av1` → 退出码 1，
  `error: encode failed: lossless requested but backend exposes no 'chroma' parameter; cannot
  guarantee 4:4:4 (true lossless)`；同参数换 `--backend libaom` → 成功（59513 B，av1C profile 1 / 4:4:4）。
  UI 一侧还会在勾选无损时把档位注释写成"无损 → 4:4:4"（`src/ui/page_output.cpp:571-579`）并
  声称"强制 4:4:4"，对 SVT 后端是双重误导（既做不到 444，也不会产出文件）。
- 影响：勾"无损"+AVIF（默认后端 SVT）→ 整个文件失败，无产物；用户会认为无损功能坏了。
- 建议：无损能力必须**按后端**判定，而不是按插件自报。在 `introspect_backends`
  （`src/codecs/enc_heif.cpp:1286-1319`）里把 `TechDef::lossless_capable` 收窄为
  "插件声明支持 **且** 暴露 `chroma`"，或在 `paramform` 的 `first_lossless_tech_in` 之上再加一层
  "该后端真能无损"的谓词；并对已勾选无损后切到 SVT 的路径给出禁用原因（tooltip）。
  若希望保留 SVT 的无损语义，只能接受 YCbCr-420 域无损（对 RGB 源有损），这必须显式改名
  （"码流无损（4:2:0 抽样）"）而不是沿用"无损"。回归：金样加 `avif-svt-lossless` 期望
  clean error + UI 复选框隐藏的断言（`--ui-smoke` 探针）。
- 状态：confirmed；known=no。

##### AVIF-03 [P2] `threads` 参数对 AVIF 恒被 pipeline 覆盖（UI 高级行是空转旋钮）
- 证据：`src/codecs/enc_heif.cpp:586-597`
  `const int want_threads = req.encode_threads >= 1 ? req.encode_threads : 1;
   heif_encoder_set_parameter_integer(enc, "threads", want_threads);`
  —— 位于参数循环（`:436-514`）之后，无条件覆盖用户值；`src/core/scheduler.cpp:137-143`
  `E = clamp(T / max(W,1), 2, 16)`；`src/core/pipeline.cpp:108-111` 只做 `<1` 兜底。
- 事实：`--workers 1`（T=16）时运行日志 `thread alloc {… encode_threads=16 …}`；
  参数集里 `threads=1` 与 `threads=8` 的产物**逐字节相同**（3079 B / sha `86625a29…`），
  且 SVT 打出 `Level of parallelism supports levels [0-6]. Setting maximum parallelism level.`
  然后 `Level of Parallelism: 6`（16 被 clamp）。参数循环原本确实设置了用户值
  （`plugins/encoder_svt.cc:471` / `plugins/encoder_aom.cc:610`），随后被覆盖。
- 影响：高级区的"编码工具 → threads"对 AVIF 完全无效；用户以为在限制内存/CPU 占用，实际是 E。
  另外 SVT 的 LP 上限只有 6，>6 的线程预算被浪费。
- 建议：三选一并写清 tooltip：① AVIF/HEIF 的 `threads` 行不渲染（它是 pipeline 所有物，
  `threads` 只该由调度器决定）；② 取 `min(user, E)` 并在 tooltip 注明"实际并发受调度器约束"；
  ③ 若保留用户值优先，则 E 的分配需与用户值对齐（`scheduler.cpp` 的 `alloc_threads` 已经是
  §8.2 契约，改动应慎重）。最小改动 = ①（渲不渲染由 `ParamDef` 的加性字段表达，
  例如 `engine_owned=true`）。
- 状态：confirmed；known=no（`enc_heif.cpp:569-579` 的注释只说 E3 映射义务，未声明会覆盖用户值）。

##### AVIF-04 [P2] libaom 的 alpha 质量被内省默认值钉死在 50，`quality` 提高时 alpha 不跟随
- 证据：`plugins/encoder_aom.cc:395-405`（`alpha-quality` `has_default=false`）、
  `:626-629`（get 返回 `alpha_quality_set ? alpha_quality : quality` → 内省"当前值"=50）、
  `src/codecs/enc_heif.cpp:177`（据此得 `def=50`）→ GUI 每次显式下发 →
  `plugins/encoder_aom.cc:588-596`（`alpha_quality_set = true`）→ `:1064-1066`
  `if (input_class == alpha && encoder->alpha_quality_set) quality = encoder->alpha_quality;`。
- 事实：RGBA8 源、`quality=95`：带 `alpha-quality=50`（GUI 默认形态）→ **8717 B**；
  `alpha-quality=95` → **8760 B**；**完全不带** alpha-quality → **8760 B 且与 aq=95 同一 sha**
  （`aom-q95-noalphaq` 与 `aom-alpha-q95`）。即"不设置"才等于继承 quality，而 GUI 永远"设置了"。
- 影响：用户把质量从 50 提到 95，颜色变好、alpha 仍停在 Q50 → 半透明边缘出现带状/噪点，
  且 alpha 是唯一肉眼可见的退化面（渐变/发丝边缘）。默认值下（quality=50）两者恰好相同，
  所以问题只在"用户提高质量"这一最常见动作时暴露。
- 建议：与 AVIF-01 同源修法——`has_default=false` 的内省参数（`alpha-quality`/`alpha-min-q`/
  `alpha-max-q`/`qp`）一律不进入默认工作集；或在 `enc_heif` 里当 `alpha-quality == quality 的
  默认值` 且 `quality` 已被用户改动时**不下发** alpha-quality（让插件继承）。
- 状态：confirmed；known=no。

##### AVIF-05 [P2] UI 把 string 型 `chroma` 当 int 读 → 档位注释/预校验与产物互相矛盾
- 证据：`src/ui/page_output.cpp:459-464`
  `return int(pp::param_int(st->form->values(), std::string("chroma"), 1));`
  —— `chroma` 在 libaom 插件里是 **string** 参数（`plugins/encoder_aom.cc:351-358,698-714`），
  内省后 `ParamType::Enum` 且 choices 是 `std::string`（`src/codecs/enc_heif.cpp:190-207`），
  `param_int()` 对非 `int64_t` 值只返回 fallback（`src/core/params.cpp:425-432`）⇒ 恒为 1（4:2:0）。
  消费点：`page_output.cpp:542-552`（推导档位注释）、`:490-517`（档位→色度限幅）、
  `:562`（`heif_profile_compatible(want, depth, chroma, true, &err)`）。
- 事实：`chroma=444` 的产物实测 `av1C seq_profile=1、chroma_subsampling=(0,0)、csp=2`（= AV1 High，
  4:4:4）；而 `derive_heif_profile(avif, 8, 1, true)` 的结果是 Main，于是
  ① 注释位显示"插件自选 → Main（兼容性最好）"（与产物 High 矛盾）；
  ② 用户手选"High（8/10 位 · 4:4:4）"→ `heif_profile_compatible(High, 8, 1, true)` 判否 →
  红字"所选编码档位「High（4:4:4）」与当前设置不一致…色度 4:2:0…"，但真编码会成功
  （`src/codecs/enc_heif.cpp:641-654` 用的是真实 `layout.chroma`）；
  ③ `page_output.cpp:548-552` 的 HEVC 4:4:4 补正分支（`chroma == 3`）永不成立。
- 影响：档位行是本格式唯一的"兼容性预期"控件，现在它在 4:4:4/4:2:2 配置下**说反话**：
  该报 High 时报 Main，该放行时标红。用户要么被劝退，要么被误导认为产物是 Main。
- 建议：`current_chroma()` 改读字符串并映射：`param_str(values,"chroma","420")` →
  `"420"→1, "422"→2, "444"→3`（与 `heif_profile.h:31` 的 chroma 编码一致）；
  对不暴露 `chroma` 的后端（SVT）退回 1。加单测：`chroma=444` 时 `current_chroma()==3` 且
  `update_profile_note` 推导为 High（可直接测纯函数 `derive_heif_profile`）。
- 状态：confirmed（代码链确定性；未在真实 GUI 点击复现，见 §5）；known=no。

##### AVIF-06 [P2] AVIF 选 Main 档 → 位深被静默从 10 位改写成 8 位（AV1 Main 本含 10 位）
- 证据：`src/ui/page_output_impl.h:239-256`
  `case HeifProfile::Main: case MainStill: return {8};`
  —— 两格式共用（HEVC 的 Main 确实只有 8 位），但 AVIF 的下拉文案是
  `:224` `"Main（8/10 位 · 4:2:0/4:0:0）"`；推导规则 `src/codecs/heif_profile.h:60-65`
  `if (chroma == 2 || bit_depth > 10) return Professional; if (chroma == 3) return High;
   return Main;`（10 位 4:2:0 → Main）。
  收窄动作：`src/ui/page_output.cpp:619-633`（`profile_allowed_bitdepths` 求交）→ `:634-645`
  （`keep_if_valid` 但新值不在 allowed → `resolved = default_bitdepth_for` → 再不在 → 8）。
- 事实：默认位深 = 10（`src/core/pipeline.cpp:1731-1732`）；用户在 AVIF 页签选"Main（8/10 位…）"→
  位深下拉立即从"10 位"变成只有"8 位"（`allowed = {8,10,12}∩probe ∩ {8} = {8}`），
  且代码路径不写任何注释/警告（`set_bitdepth_options` 的 note 只在 `allowed.size()==1` 时写
  "格式仅支持 N 位"，`:647-653` → 会显示"AVIF 仅支持 8 位输出"，**与事实相反**）。
  同路径的另一半证据：libaom 10 位 4:2:0 产物 av1C `seq_profile=0, high_bitdepth=1`（Main），
  说明 10 位 Main 完全合法。
- 影响：用户为了"兼容性最好"选 Main，结果输出从 10 位掉到 8 位（位深精度损失，不可逆），
  而界面还宣称"仅支持 8 位"。这是"默认值/联动造成明显错误"。
- 建议：把 `profile_allowed_bitdepths` 改成吃 `bool avif`：AVIF 的 `Main → {8,10}`、
  `High → {8,10}`、`Professional → {8,10,12}`；HEVC 保持现状。同时把
  `page_output.cpp:649-652` 的"仅支持 N 位"文案限定在**格式能力**而非档位限幅（档位限幅应走
  `profile_note` 的 warn 通道）。单测：`test_smoke` 的真值表加 AVIF Main→{8,10}。
- 状态：confirmed（代码链确定性；未在真实 GUI 点击复现，见 §5）；known=no。

##### AVIF-07 [P2] SVT 速度档 10–13 全部映射到 9，而默认值恰好落在死区 12
- 证据：`plugins/encoder_svt.cc:194-200`（`speed` 0–13，`default_value = 12`）；
  `vcpkg/buildtrees/svt-av1/src/v4.2.0-93c4505a5d.clean/Source/Lib/Globals/enc_handle.c:4295-4298`
  `if (scs->allintra) { if (enc_mode > ENC_M9) { SVT_WARN("Preset M%d is mapped to M9.\n"); enc_mode = ENC_M9; } }`
  —— AVIF 走 `svt_config.avif = true`（`plugins/encoder_svt.cc:940-943`）⇒ `allintra`（`enc_handle.c:4286-4291`）。
- 事实：默认（无 `speed`）SVT 打印 `Preset M12 is mapped to M9`；`speed=10` → `Preset M10 is mapped
  to M9`，`speed=13` → `Preset M13 is mapped to M9`；`speed=10`、`speed=13`、默认三者产物**同一 sha
  （86625a29…，3079 B）**，而 `speed=8` 是 1980 B、`speed=4` 是 1901 B。UI 的滑杆 0–13 里 10/11/12/13
  四个位置行为完全相同，默认值正好是其中之一。
- 影响：默认档位名不副实（"12"实际是 9）；用户微调速度看不到任何变化（还以为是缓存/无效果）。
- 建议：内省时对"all-intra/静止图"场景收窄上限（`param_def_from` 无法知道场景，
  可在 `introspect_backends` 里对 avif 后端把 `speed.hi` 设为 9，并在 tooltip 注明
  "静止图模式 libsvt 仅支持 0–9"），默认值随之变成 9；或保留 0–13 但把 >9 的档位标灰 + 原因。
- 状态：confirmed；known=no。

##### AVIF-08 [P2] libaom：`tune=iq` + 无损 = 必然失败（UI 允许该组合）
- 证据：`plugins/encoder_aom.cc:1129-1133`
  `if (is_lossless && effective_tune == AOM_TUNE_IQ) return heif_error_lossless_with_tune_iq;`
  （文案见 `:209-214`）；`iq` 在合法值表内（`:199-202`）→ UI 下拉可选；无损由复选框独立控制。
- 事实：预设 `{lossless:true, tune:"iq"}` → 退出码 1，
  `error: encode failed: heif_context_encode_image failed: Usage error: Invalid parameter value:
  AOM 'tune=iq' cannot be combined with lossless encoding because libaom enables chroma delta-q …`
  —— 整条链路只有英文库文案，且没有任何 UI 预校验（`cross_rules()` 里没有 heif/avif 规则，
  `src/core/params.cpp:219-289`）。
- 影响：同一页签上两个合法控件的组合必然失败；错误信息是英文、提及 libaom 内部机制，用户无法自救。
- 建议：在 `src/core/params.cpp` 的 `cross_rules()` 加一条 heif 系规则
  （`format_id="avif"`，keys={lossless, tune}，`lossless && tune=="iq"` → 中文提示"无损编码不能配
  tune=iq，请改用 ssim/psnr"），该规则会同时进 `cross_validate`（红字区）与 `validate_params`
  （预设校验）；另在 `apply_chroma_limits` 同一收敛点把无损下的 `tune` iq 项置灰并给原因
  （复用 `form->limit_enum_choices_text`）。
- 状态：confirmed；known=no。

##### AVIF-09 [P2] AVIF 无损 >16 MP 被塞进 HEIF grid 容器；无损 + alpha >16 MP 直接失败
- 证据：`src/codecs/enc_heif.cpp:872-878`
  `constexpr int64_t kSingleImageLosslessMaxSamples = 16LL * 1024 * 1024;
   const bool lossless_grid = want_lossless && luma_samples > kSingleImageLosslessMaxSamples;
   if (lossless_grid && has_alpha) return fail("HEIF lossless with alpha above 16 MP is not
   decodable by libde265/OIIO …");`
  —— **没有 `format_id_ == "heif"` 的门**；grid 分支 `:882-991` 用
  `heif_context_add_grid_image` / `heif_context_add_image_tile`（`#include <libheif/heif_tiling.h>`，
  `:18`）。注释里的理由是 libde265 的 HEVC 解码上限（`:862-871`），与 AV1 无关；
  连 UI 自己的文案都只把它算在 HEIF 头上：`src/ui/paramform.cpp:514-519`
  "本格式无损强制 4:4:4 … **HEIF** 超过 16 MP 会自动用标准 grid 分块以避开 libde265 的单图解码上限"。
- 事实：4100×4100（16.81 MP）RGB8 + `--lossless --backend libaom` → 成功，162232 B，容器结构实测：
  `ftyp avif compatible=[mif1,avif,miaf]`、10 个 item、`iref dimg from=1 to=[2..10]`、
  **item 1 = `grid`**（props `ispe(4100×4100)/pixi/colr`），item 2–10 = 9 个 `av01` 瓦片
  （`ispe 2048×2048`，同一个 `av1C`：profile 1、4:4:4、csp 2）。本工具自身可回读
  （`pp_verify … psnr 40` → `VERIFY big-ll OK`，走 libheif）。
  AVIF 参考实现 libavif 与主流浏览器解码器不实现 `grid` 派生项，故这类文件在第三方工具里
  很可能打不开（本机无 `avifdec`/`oiiotool`/`ffmpeg`，**未实测**，见 §5）。
  另外 alpha + >16 MP 时错误文案写的是 "HEIF …"，对 AVIF 用户是错的格式名。
- 影响：24–50 MP 相机图是常态，任何"AVIF 无损"批量任务都会产出 grid 形态（或对带 alpha 的
  直接失败），可能得到一堆本工具能开、别人打不开的文件 —— 属于"输出兼容性破坏"。
- 建议：把阈值分支限定在 `format_id_ == "heif"`（AV1 解码器无 16 MP 上限）；
  AVIF 一侧若确需分块，应使用 AVIF/MIAF 允许且生态支持的形态（当前最稳的是**不分块**，
  libaom/dav1d 可解 100 MP 级）。若保留 AVIF grid，必须在 UI/日志显式警告"非主流形态"。
  错误文案改为按格式拼（"AVIF/HEIF …"）。回归：金样加 `avif-lossless-big`（>16 MP，
  断言无 `grid` item）。
- 状态：confirmed（代码路径 + 容器结构实测；第三方解码器支持未实测）；known=no。

##### AVIF-10 [P2] `--dev --param` 对 AVIF 全类型化参数失效；`quality` 被静默替换成 90
- 证据：`src/dev_cli.cpp:384-393`
  `const pp::ParamDef *def = pp::lookup_param_def(f, backend, tech, key);
   if (!def) { s[key] = raw; /* 字符串 */ … }`
  —— `pp::lookup_param_def`（`src/core/params.cpp:378-390`）只查**静态** techs，
  而 avif 的 `FormatDef::backends[].techs` 为空（`src/core/format_tables.cpp:886-894`）⇒ 永远 `!def`；
  `src/codecs/enc_heif.cpp:443-454`（Int 分支：非 int64 → `rejected = "expected integer"` →
  `:509-513` 直接 `fail`）；专用回退 `:501-508` 用 `param_int(params,"quality",90)` /
  `param_bool(params,"lossless",false)` —— 字符串值取不到 → **落到 90 / false**。
- 事实：`--param qp=0` → `encode failed: parameter 'qp' rejected by backend: expected integer`；
  `--param speed=8` / `--param threads=8` / `--param alpha-quality=95` 同样失败；
  `--param quality=70` → **成功但按 90 编码**（`CQP 6.25`、10064 B，与 `quality=90` 逐字节相同），
  而走预设（JSON 类型推断出 int64，`src/ui/preset_io.cpp:236-241`）的 `quality=70` → 4411 B。
  `chroma`/`tune` 因为是字符串枚举反而可用。
- 影响：本格式的 CLI 验证面整体不可信（这也是本审计不得不改用"预设 JSON 模拟 GUI"取证的原因）；
  最危险的是 `quality` 的**静默替换**：用户以为在压 70，得到的是 90。
- 建议：`dev_cli.cpp:387` 对 `runtime_introspected` 后端改走 `pp::introspect_backends` 取 ParamDef
  （`src/codecs/encoders.h` 已导出），与 GUI 同源；`enc_heif.cpp:501-508` 的 quality 回退默认值
  改为插件的 50（或直接不 recovered，报错），避免"错值成功"。
- 状态：confirmed；known=no（属工具面，非发行面）。

##### AVIF-11 [P3] `svt:` 私有透传通道不存在：写进去必然整文件失败（`aom:` 才有）
- 证据：`src/codecs/enc_heif.cpp:520-534`（`pass_through = key.rfind("x265:",0)==0 ||
  key.rfind("svt:",0)==0 || key.rfind("aom:",0)==0`）、`src/core/params.cpp:174-178`
  （`is_plugin_passthrough_key` 同样放行 `svt:`）；但插件实现只有两处：
  `plugins/encoder_x265.cc:531`（`strncmp(name,"x265:",5)`）、`plugins/encoder_aom.cc:743-748`
  （`strncmp(name,"aom:",4)`）；`plugins/encoder_svt.cc:535-588` 的 `svt_set_parameter_string`
  只认 `tune`，**没有任何前缀分支**。
- 事实：`{"svt:rc-mode":"1"}` → 退出码 1
  `error: encode failed: plugin option 'svt:rc-mode' rejected by backend: Unsupported encoder
  parameter (heif_error code=5 subcode=2005)`；对照 `{"aom:cq-level":"20"}` → 成功 4857 B
  （`aom_codec_set_option` 生效），`x265:` 在 HEIF 侧有实现。
- 影响：预设/CLI 里写 `svt:` 键会**整文件失败**而不是"被忽略"；白名单让人以为 SVT 也有这条通道。
- 建议：从 `enc_heif.cpp:525-526` 与 `params.cpp:177` 的白名单里去掉 `svt:`；
  若要支持 SVT 私有参数，应显式走 `svt_av1_enc_parse_parameter`（libheif 插件未暴露该入口，需上游）。
- 状态：confirmed；known=no。

##### AVIF-12 [P3] "无损"在机器可读面没有任何痕迹：`--lossless` 产物的侧车连参数都不写
- 证据：`src/core/params.cpp:632-644`（`snapshot_params()` 跳过所有 `__` 前缀键）；
  dev harness 的 `--lossless` 只写内部键 `__lossless`（`src/codecs/enc_heif.cpp:536-545` 的注释
  把这件事说得很清楚："dev harness 的 `--lossless` 只写内部键 `__lossless`（静态格式表里 heif 的
  techs 为空，`apply_locks` 无法补出显式键）"）；GUI 路径才会同时写显式 `lossless`
  （`src/ui/paramform.cpp:913-915`、`src/ui/page_output.cpp:730`）。
- 事实：`--lossless --backend libaom` 的产物侧车实测
  `build/encoder-audit/avif-rt/out/aom-lossless8/grad_rgb8.avif.pp.json` 里
  **`"params": ""`、无 `lossless` 字段**（只有 `out_bytes: 59513`），而同一文件的像素实测
  与源**不是**逐位相同（`pp_verify exact` → 92551/147456 样本差 1 LSB，`0.600000 → 0.596078`；
  psnr 40 通过）。即：从产物侧车无法区分"无损"与"有损"，也无法知道"无损"是码流域而非像素域。
- 影响：自动化/回归面无法据侧车判断一次 AVIF 是否走无损；下游拿 `exact` 校验会莫名失败。
  （口径本身已在两处 tooltip 如实说明：`src/ui/paramform.cpp:509-519`、`src/ui/page_output.cpp:571-579`
  —— 本条只针对**可复核的机器面**缺失。）
- 建议：`spec_of()`/`build_output_specs()` 对 heif/avif 也补写显式 `lossless` 参数
  （或在 `snapshot_params` 里把 `__lossless` 映射成 `lossless`），并给侧车加
  `lossless_domain: "codestream"` 一类字段，让"无损"在产物侧车可复核。
- 状态：confirmed；known=no（`docs/ux-review-14.md` 讨论的是无损的 UI 口径，未涉及侧车记录）。

##### AVIF-13 [P3] `docs/param-catalog.md` 的 AVIF 章节与运行时内省面漂移（含一个根本不存在的参数）
- 证据：`docs/param-catalog.md:146` 把 SVT 的 `chroma` 列为**核心**参数、"预期默认 420/范围 420-422-444"
  —— 但 SVT 插件的 `chroma` 整段被 `#if 0`（`plugins/encoder_svt.cc:264-273`），
  运行时内省里根本没有这个键（本机实测 `chroma` 对 SVT 只在日志里留 `unrecognised parameter ignored`）；
  `docs/param-catalog.md:155-159`（libaom）把 `chroma` 写"高级"（实为核心，`enc_heif.cpp:219-226` 的白名单）、
  把速度写成 "usage / cpu-used  0–10  默认 4"（实为单键 `speed` 0–9 默认 6，`plugins/encoder_aom.cc:293-309`；
  `usage`/`cq-level` 都不是内省键，只能经 `aom:` 透传）、`docs/param-catalog.md:161` 写"位深 `{8,10}`"
  （实为 `{8,10,12}`，`src/core/format_tables.cpp:892`，且 12 位 libaom 实测可用）。
- 事实：按目录去核对 AVIF 参数面会得到三个错结论：SVT 有色度选择（没有）、libaom 色度是高级项
  （是核心行）、12 位不支持（支持）；`quality / cq-level` 并列也让人以为存在 `cq-level` 键。
  目录自己的免责声明是"HEIF/AVIF 仅列预期项，真源 = 运行时内省"（`docs/param-catalog.md:4`），
  但"预期项"的级别/范围/默认值仍被当作规格使用（AVIF-02 的 SVT 无损预期、AVIF-05 的色度预期都源自这里）。
- 影响：文档读者（含后续审计/维护者）会按不存在的参数面判定"功能缺失"或"UI 漏做"；
  本审计的多条"UI 暴露"结论必须先绕开这些条目才能对齐。
- 建议：把 §4 改成"**实测内省快照**"（每后端一张 `key/type/lo/hi/choices/default/核心-高级` 表，
  由 `pp_linkprobe` 或一个新的一次性内省导出工具生成，贴版本号），并显式标注"内省面随库版本变化，
  以快照为准"；删掉 SVT 的 `chroma` 行与 `cq-level`/`usage` 行。
- 状态：confirmed；known=no。

#### 4. 输出兼容性与合规矩阵

所有产物由 `tools/avif_probe.py` 逐字节解析（ftyp / meta / iinf / iloc / iref / iprp{ipco,ipma}）。
`av1C` 语义按 AV1 ISOBMFF binding：`byte2 = tier(1) high_bitdepth(1) twelve_bit(1) monochrome(1)
chroma_subsampling_x(1) chroma_subsampling_y(1) chroma_sample_position(2)`，
(1,1)=4:2:0、(1,0)=4:2:2、(0,0)=4:4:4（与 libheif 的解析一致：`codecs/avif_boxes.cc:37-70`、
`codecs/avif_boxes.h:71-87`）。ftyp 恒为 `major=avif minor=0 compatible=[mif1, avif, miaf]`。

| 源 / 配置 | 产物结构（实测） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| RGB8 → svt-av1 8 位（默认，无 `qp`） | 1 item `av01`；`av1C` profile 0 / 8 位 / (1,1) 420；`pixi [8,8,8]`；`colr nclx(1,13,6,full)`；3079 B | 本工具 pp_verify psnr30 OK；无外部工具（见 §5） | 无。CICP 与像素变换一致（matrix 6 / full range 与 `enc_heif.cpp:783-785` 的 BT.601 全范围式子同源） |
| RGB8 → svt-av1 8 位（**GUI 默认**，`qp=0`） | 同上但 38214 B（CQP 0.00） | — | **体积异常 12.4×**（AVIF-01），结构仍合规 |
| RGB8 → svt-av1 10 位（GUI 默认 10 位） | `av1C` profile 0 / high_bitdepth=1 / (1,1)；`pixi [10,10,10]`；62521 B（GUI）vs 1975 B（无 `qp`） | — | 同上（AVIF-01，31.6×） |
| RGB8 → libaom 8/10/12 位 | profile 0 / high=0|1 / twelve=0|1；`pixi [8|10|12]`；2807 / 2987 / 2897 B | pp_verify psnr30 OK | 无。12 位 → profile 2（Professional），与 AV1 规范一致 |
| RGBA8 → svt-av1 8 位 | 2 items：`av01`(color) + `av01`(alpha)；`iref auxl from=2 to=[1]`；`auxC urn:mpeg:mpegB:cicp:systems:auxiliary:alpha`；alpha item 复用同一 `av1C`(420, monochrome=0) 与 `pixi [8,8,8]`；1628 B | 本工具可读 | alpha 以 **4:2:0** 编码（`plugins/encoder_svt.cc:759-762` 强制 `EB_YUV420` + `yShift`），luma 全分辨率故 alpha 有效；av1C 与其码流自洽。无 aux 类型违规。**注意** alpha 质量 = color quality（SVT 无 alpha-quality 参数） |
| RGBA8 → libaom 8/10/12 位 | 2 items；alpha item 有**独立** `av1C`（monochrome=1）+ `pixi [N]` + `auxC`；2232 / 2397 / 2318 B | 本工具可读 | 合规。alpha 质量受 AVIF-04 影响 |
| RGBA8 → svt-av1 **10 位** | **无产物**：`encode failed: svt-av1 does not support 10-bit with alpha; use backend libaom`（`enc_heif.cpp:676-677` 的前置守卫） | — | 已知上游限制；UI 有预选 + 提示（`format_policy.h:78-94`、`page_output.cpp:745-787`），但**预选谓词只覆盖 10 位**（12 位在 SVT 上已被探测剔除，故实际不可达） |
| RGB8 → libaom `chroma=422` | `av1C` profile 2 / (1,0) 422 / csp=2；2798 B | 本工具可读 | 合规（AV1 profile 2）；但 UI 档位行会误报（AVIF-05） |
| RGB8 → libaom `chroma=444` | `av1C` profile 1 / (0,0) / csp=2；3197 B | 本工具可读 | 合规；UI 档位行误报 Main（AVIF-05） |
| RGB8 → libaom 无损（8 位） | `av1C` profile 1 / 444 / csp=2；`pixi [8,8,8]`；59513 B；像素非逐位（±1 LSB） | pp_verify psnr40 OK、exact FAIL（口径已由 `paramform.cpp:509-519` 的 tooltip 声明） | 合规（YCbCr 域无损）。需注意 `matrix=6` 把 444 的色度也按 BT.601 全范围承载 |
| RGBA8 → libaom 无损 | 2 items + 444 主图；46768 B | psnr40 OK | 合规 |
| RGB16 → libaom 无损 10 位 | 444 / high_bitdepth=1；97157 B | psnr40 OK | 合规 |
| 4100×4100（16.81 MP）→ libaom 无损 | **`grid` 主项 + 9×`av01`(2048²) 瓦片；`iref dimg`；162232 B** | 本工具（libheif）可读 | **AVIF 生态兼容风险**（AVIF-09）：grid 派生项非主流 AVIF 形态；未实测第三方解码器 |
| 灰度 → 两端（`supports_gray=false`） | 3 通道 420，`colr **prof**`（588 B ICC，**无 nclx**）；2192 / 2501 B | 本工具可读 | **风险**：源为灰度 PNG（带灰度 ICC）时，产物像素已是 RGB/YCbCr，但内嵌 ICC 仍是灰度 profile（传递链见 §5 未覆盖项）。同时 nclx 被 ICC 顶掉（`heif_nclx.h:11-12` 明确"ICC 在场时只写 colr/prof"），容器级 CICP 缺失，仅码流 VUI 有 |
| RGB8 + `--color srgb` | `colr prof`（588 B）；3388 B | 本工具可读 | 合规；nclx 丢失是 libheif 行为（`enc_heif.cpp:840-850` 同时 set nclx + ICC，容器只留 prof） |
| RGB8 + `--color p3` | `colr prof`（580 B）；3272 B | 本工具可读 | 合规；bitstream VUI 走 `nclx_for_icc` 推导（`heif_nclx.h:135-139` → primaries 12），但容器无 nclx 可复核（未解 OBU，见 §5） |
| RGB8 + EXIF + XMP（`--meta`） | 3 items：`av01`、`Exif`（载荷 = 4 字节偏移 0 + `II*\0`，符合 HEIF Exif item 规范）、`mime`（XMP packet）；5542 B | 本工具可读 | 合规；`pipeline.cpp:969` 把 `Exif.Image.Orientation` 归一为 1（像素已旋转），无双旋转风险 |
| AVIF 源 + `--metadata-only` | **不产生**：`format 'avif' does not support metadata-only rewrite (zero re-encode)` | — | 设计如此（`src/core/metadata.cpp:1113-1114`：Exiv2 对 BMFF 只读） |
| 任意 → `--lossless` + SVT | 无产物（AVIF-02） | — | UI 允许但必失败 |

#### 5. 未覆盖与存疑

1. **GUI 未实测**（任务禁止 `--ui-smoke`/GUI 启动）：AVIF-05/AVIF-06 的界面表现由代码链确定性推演
   （`current_chroma`→`param_int`→fallback、`profile_allowed_bitdepths`→`keep_if_valid`→回落），
   未在真实控件上点击复现。其余结论均有 `--dev` 实测或运行日志。
2. **第三方可读性未实测**：本机无 `avifdec` / `oiiotool` / `ffmpeg` / `magick`
   （`Get-Command` 全 absent），grid 产物（AVIF-09）与 4:2:2/4:4:4、12 位产物都只验证了
   本工具（OIIO/libheif）路径与容器结构合规性，没有用独立解码器交叉验证。
3. **码流 VUI / CICP 未解析**：容器 `colr nclx` 在 ICC 在场时被 libheif 丢掉，
   本文只能从 `plugins/encoder_svt.cc:816-830`、`plugins/encoder_aom.cc:1205-1214`
   推断 VUI 收到的是 `nclx_for_icc` 的结果；未写 AV1 OBU (sequence header) 解析器逐位核实。
4. **灰度 ICC 与 RGB 像素的一致性**：灰度源产物内嵌 588 B 灰度 ICC 但像素是 RGB 420（§4 表末第二行）。
   这是 colormanager/元数据传递链的行为，超出本格式参数面；建议由色彩/元数据审计段落接手确认。
5. **未实测的组合**：`workers>1` 时的 E 分配与 `threads` 覆盖（只测了 `--workers 1`）；
   <64 px 的小图（SVT 会 pad 到 64，`plugins/encoder_svt.cc:698-724`）；
   奇数尺寸（chroma 取整路径 `enc_heif.cpp:631-632`）；12 位 + alpha + libaom 的无损；
   libaom `min-q`/`max-q` 单独效果（只测了 SVT 的 `max-q`）；`auto-tiles` 在大图上的实际瓦片布局；
   `lossless-alpha` 在"主图有损 + alpha 无损"混合下的行为。
6. **`qp` 的语义歧义**：SVT 的 `qp` 与 `quality` 是两套并行控制面（`plugins/encoder_svt.cc:862-867`），
   本审计只证明"默认会被当显式值下发"，未验证"用户显式想用 qp 时 quality 该不该被忽略"的产品语义
   （建议按 AVIF-01 的 ④ 兜底：用户面优先）。
7. **存疑（suspected）**：SVT 的 alpha 面固定 4:2:0（`plugins/encoder_svt.cc:759-762`）是否会让
   alpha 在高频边缘产生色度无关损失 —— 因为 alpha 只用 Y 平面，理论上无影响，但未做像素级
   alpha-only 对比（需要构造 alpha 细节图并在 pp_verify 里只比 alpha 通道）。
8. **成本受限未做**：未对 24/48 MP 大图做有损 AVIF 完整矩阵（只有 16.81 MP 无损一例）；
   未做 speed/quality 的全扫描（只取代表点）。

---

##### 附：可复现命令与产物路径

```powershell
# 基线（默认参数，无 qp）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev `
  build/encoder-audit/avif-rt/in/grad_rgb8.png --out build/encoder-audit/avif-rt/out/svt-default8 `
  --format avif --backend svt-av1 --bitdepth 8 --workers 1
# GUI 等价参数集（预设 JSON 保型；qp=0 由内省默认带入）
python build/encoder-audit/avif-rt/tools/run_preset_cases.py gui
# 结构解析 / EXIF / 像素断言
python build/encoder-audit/avif-rt/tools/avif_probe.py dump <产物.avif>
python build/encoder-audit/avif-rt/tools/exifdump.py <产物.avif>
python build/encoder-audit/avif-rt/tools/run_verify.py
```

产物树：`build/encoder-audit/avif-rt/{in,out,logs,presets,verify,tools}`；
逐用例日志 `logs/<case>.log`，汇总 `logs/results.jsonl` / `logs/preset-results.jsonl`，
运行期文本日志在 `<out>/logs/run-*.log`（含 `thread alloc`、`bit depth probe`、
`unrecognised parameter ignored` 等点位）。

## 5. 跨格式主题

> 本章回答六个横向问题：全参数暴露差距、默认值合理性、联动/锁定缺失、静默降级与假桩、输出兼容性、仅元数据通路；最后给出"无损"语义分级与测试门禁失明点。逐格式证据见第 4 章对应章节。

### 5.1 "全参数暴露"达成度：没有任何格式做到 100%，差距分三类

"全参数暴露"在本仓的口径是 `AGENTS.md` §1.2 第 3 条"所用库的全部参数进入 GUI 可配置（核心置顶、高级折叠）"。逐库比对结果：

| 格式 | schema/UI 参数 | 库可配置面（与本工具相关） | 差距性质 |
|---|---|---|---|
| JPEG | 15 | jpegli `encode.h` 约 25 个公开入口 | **能力裁剪**：自定义量化表、scan script、绝对 `restart_interval`、`jpegli_set_input_format`、linear quality、colorspace 直出未暴露；`distance` 收窄到 0.3–3.0（库 0–25）、`restart_in_rows` 收窄到 64（库 65535）、`progressive` 只能 0/2（库 0/1/2） |
| JXL | 24 个 frame setting + 4 个特例 | libjxl 0.12 约 41 个 `JXL_ENC_FRAME_SETTING_*` + 2 个编码器级函数 | **能力裁剪**：`extra_channel_resampling`、`group_order_center_x/y`、`buffering`、`output_mode`、`disable_perceptual_heuristics`、upsampling mode、EC distance 未暴露；其中前两项会改变产物 |
| PNG | 1 | OIIO PNG writer 实际读取约 13 类属性 | **实现缺口（已自述 TODO）**：`compression`（zlib 策略）、`png:filter` 是真实内容面却未进 schema；`docs/param-catalog.md:214` 自己留着"以 OIIO PNG 输出属性文档核对补全"的待办 |
| TIFF | 5 | OIIO TIFF writer 约 12 个可写属性 | **裁剪 + 文档口径漂移**：`tiff:RowsPerStrip`、`tiff:bigtiff`、`planarconfig`、DPI、`tiff:write_extrasamples`、IPTC 未暴露；`design.md` 仍写"暴露其全部输出参数"，与 M0 的 5 条裁定冲突 |
| WebP | 24 | `WebPConfig` 约 27 个字段 | **小缺口**：`image_hint`、`target_size`、`target_PSNR` 未暴露；其余字段已接 |
| BMP | 0 | 无编码旋钮 | **正确**（零参数格式确实无需参数） |
| HEIF | 7（x265 插件全表） | x265 插件 7 个参数 | **全暴露**（运行时内省），另有 `x265:<opt>` 透传面只在预设/CLI 可见 |
| AVIF | SVT 11 / libaom 15（内省全表） | 两插件参数表 | **全暴露**，`svt:`/`aom:` 透传面同理不在 UI |

结论：**"全参数暴露"是"按产品负面清单裁剪后的子集"，不是字面全量。** 裁剪本身有合理性（CMYK/HDR/JPEG-in-TIFF 在负面清单内），但 PNG 的 `compression`/`filter`、JXL 的 `extra_channel_resampling`/`group_order_center_*`、TIFF 的 `RowsPerStrip`/BigTIFF 属于**用户可感知的体积/兼容性能力**，且没有在 UI/tooltip 说明"本产品不提供"。这类缺口应按 P2/P3 登记，不能再用"全参数暴露"作为已完成口径。

### 5.2 默认值：多数合理，五处值得改

**合理的默认**：JPEG `distance=1.0 + 444 + progressive + optimize + AQ`（与 jpegli 参考实现逐项一致）；JXL `effort=7`、VarDCT `distance=1.0`；WebP `lossy quality=90`、`method=4`；PNG `compressionLevel=6`；TIFF `lzw + predictor=horizontal`；HEIF/AVIF `chroma=420`（兼容面优先）。

**值得改的五处**：

1. **HEIF 默认 10 位 = RExt 档**（`HEIF-03`）：默认产物 `profile_idc=4`、hvcC 只声明 RExt 兼容位、`ftyp=heix`；8 位才是 Main Still Picture（`profile_idc=3`，compat `0x70000000`）。同一 8 位源默认 992 B、8 位 1018 B，体积几乎相同但兼容面更窄。默认应改 8，或在位深行常显"10 位 = RExt，老解码器可能不支持"。
2. **HEIF `quality=50`（CRF 25）与 catalog 的"视觉透明档校准值"不符**（`HEIF-02`/catalog §3）：GUI 默认取插件当前值 50，而 `enc_heif` 的异常回退值是 90，两条入口默认不一致；50 对照片是"中等"，不是本产品宣称的视觉透明档。应校准一个默认值（或至少让 GUI/CLI/回退三处同源）。
3. **JXL `codestream_level=10`**（`JXL-04`）：libjxl 默认 `-1(auto)`，官方头文件明确"推荐最小必需等级/level 5 以获得最广兼容"；当前每个产物都写 `jxll=0x0a`，对只实现 level 5 的解码器是无收益的拒收面。
4. **PNG/TIFF/JXL 默认 16 位（8 位源 +19%~+100% 体积、无 sBIT）**（`PNG-04`）：这是共识文档的既定口径（"默认值=高质量档"），对 16 位源是正确的；但对 8 位源是零信息增益的体积膨胀。**不建议直接改默认**（会破坏"不降级"承诺），建议补"源位深 < 所选位深"的行内提示 + sBIT。
5. **JXL `keep_invisible=true`**（`JXL-08`）：覆盖库的 `-1(auto)`，带 alpha 的有损产物默认体积约为必要的 3.1 倍；应改 false 或三态枚举。

另有两处"默认值不是默认值"的口径问题：WebP `filter_strength=30`/`sns=50` 被 tooltip 描述为"photo 预设生效值"，但 preset 根本不生效（`WEBP-03`）；HEIF `complexity` UI 显示 0（插件 `has_default=false`），catalog 声明 50（`HEIF-07`）。

### 5.3 联动/锁定：现有 5 条规则，缺的比有的多

**已实现且实测有效**：JPEG `progressive⇒optimize_coding`（锁定 true）；TIFF tile 宽/高"成对 + 16 倍数"；WebP `qmin≤qmax`；JXL 无损时 `distance` 锁 0、`modular_lossy_palette` 锁 false；HEIF 无损⇒`chroma=444`（参数+平面布局双处强制）。

**缺失的联动（按后果排序）**：

| 组合 | 后果 | 证据 |
|---|---|---|
| JXL `lossless × resampling>1` | **P1**：名义无损实际降采样（需显式非默认 resampling） | `JXL-01` |
| WebP `lossless × near_lossless<100` | **P1**：名义无损实际近无损 | `WEBP-04` |
| WebP `tech_id × lossless` | **P1**：技术优先于标志，双向语义相反（GUI 自愈，CLI/预设可复现） | `WEBP-01/02` |
| JPEG `psnr_target>0 × quality_mode/distance/quality` | 后三者静默失效 | `JPEG-03` |
| HEIF `lossless × quality` | 无损下 quality 零效果，UI 不锁 | `HEIF-05` |
| JXL `lossless × color_transform` | None/XYB/YCbCr 被库覆写为 no-op，UI 照常可选 | `JXL-02` |
| AVIF `backend=svt-av1 × bitdepth=10 × alpha` | UI 只黄字提示，运行前不拦，逐文件失败 | `X-PARAM-ENGINE-09` |
| AVIF `backend=svt-av1 × lossless` | 插件不暴露 chroma → 编码期硬失败；UI 不自动切 libaom | `HEIF-01` 同族（AVIF 章） |
| TIFF `compression=ccittrle` / `predictor=float` | 必失败选项，运行前不拦 | `TIFF-02/05` |
| BMP `bitdepth × channels` | 8/24 不驱动 bpp | `BMP-01` |
| PNG/TIFF/BMP `lossless 复选框` | 恒无损格式显示可勾控件，勾了无效 | `PNG-08`/`TIFF-06`/`BMP-06` |
| HEIF/AVIF `profile × bitdepth × chroma` | 只在编码期拒绝，无 cross rule | `X-PARAM-ENGINE-07` |

**根因**：`cross_rules()` 只有 5 条（`params.cpp:219-289`），而 `format_tables` 的 `visible`/`locked` 谓词只表达"单参数对其它参数"的简单关系；"参数组合 + 位深 + 后端 + alpha"这一层没有统一规则入口。参数引擎的谓词签名 `bool(const ParamSet&)` 拿不到 bitdepth/backend/alpha，是结构性限制。

### 5.4 静默降级与假桩：一张清单

> 判据：UI 可见可改、可存预设，但**改变该值不改变产物**（或只在极窄条件下改变），且没有等价于"失败/警告"的可见反馈。

| # | 控件 | 格式 | 症状 | 严重度 |
|---|---|---|---|---|
| 1 | `preset` | WebP | 六个枚举值字节全同；tooltip 承诺的 sns/sharpness 从未生效 | P1 |
| 2 | 位深 8/24 | BMP | RGB 源两档同字节；灰度源选 24 仍得 8bpp；16→8 降档不告警 | P1 |
| 3 | 「无损编码」复选框 | PNG/TIFF/BMP | 恒无损格式显示可勾控件，勾选零效果 | P2 |
| 4 | `color_transform` None/XYB/YCbCr | JXL | 被 libjxl 入队时覆写为 no-op；只有 YCoCg 生效（且让体积 2.8×） | P1 |
| 5 | `channel_colors_*` / `modular_ma_tree_learning_percent` | JXL | 用 int setter 调 float-only 选项，一改就整文件失败 | P1 |
| 6 | `brotli_effort` | JXL | 0 与 11 字节相同（只服务 brob/JPEG 重压缩） | P2 |
| 7 | `partitions` | WebP | 默认 `method≥3 && !low_memory` 时被库强制 1 | P2 |
| 8 | `preprocessing` 位 2/位 4 | WebP | `sharp_yuv=true` 时不可达 | P2 |
| 9 | `near_lossless` | WebP | 默认 method 下 24 组字节全同；生效时变成有损 | P2/P1 |
| 10 | `quality`（无损下） | HEIF | 字节完全相同，UI 不锁 | P2 |
| 11 | `threads` | HEIF/AVIF | 内省有则被调度器覆盖，无则不存在；用户改不动 | P2 |
| 12 | `ccittrle` / `predictor=float` | TIFF | 必失败选项 | P2 |
| 13 | `dct_method` / `arith_code` / `xyb_mode` | JPEG | 已锁定并给原因（**这是正确处置**，不是静默） | — |
| 14 | 跨技术键（`exact`/`near_lossless` 在 lossy 技术） | WebP | 接受、进侧车/预设、编码器不读、无告警 | P2 |
| 15 | 未知键 | 全部 | 运行前硬拒（比 E9 的"忽略"更安全）；但 `validate_params` 单独调用时不拒，CLI/bench 语义分裂 | P2/P3 |

**更根本的问题：warning 没有 UI 出口。** `src/ui/` 全目录对 `WarningKind` 零消费（`X-COMPLIANCE-08`）：`AlphaFlattened`、`DepthDowngrade`、`MetadataDropped`、`EmbedIccUnsupported` 等即使被 pipeline/编码器发出，也只进日志与 `.pp.json`；运行页/文件列表不显示。所有"发 warning 就算披露"的修法都必须先补这一层，否则仍是"日志里有、用户看不见"。

**"不静默灰"做得好的地方**：JPEG 三个锁定项、JXL 无损下 vardct-only 行隐藏、HEIF 无损下 chroma 非 444 置灰，都有 tooltip 原因。问题集中在"没有锁定谓词"和"编码器内部二次覆盖只写日志"两类。

### 5.5 输出兼容性与合规：结构多数合法，语义有硬伤

**逐格式结构结论**（详细矩阵见各格式章 §4）：

- **JPEG**：SOF0/SOF2、DQT/DHT/SOS、APP1 Exif、APP2 ICC 均合法；13/15-scan 渐进脚本满足 T.81 §B.2.3；无 APP0（jpegli 默认，3 分量按 YCbCr 解释是通行约定）；`restart_in_rows` 单位正确（MCU 行）。
- **JXL**：容器 `JXL/ftyp/jxll/jxlc/Exif/xml` 合法；CICP 与 ICC 不冲突；`jxll=10` 是兼容性收窄（非违规）；`codestream_level=5 + 16bit 无损` 会失败。
- **PNG**：IHDR/IDAT/IEND/CRC 全合法；iCCP + sRGB + gAMA + cHRM 同时存在（规范允许但语义矛盾）；空 eXIf 14B + oFFs(0,0) 属噪声；pHYs/tIME/tEXt 在重编码后丢失。
- **TIFF**：单页结构合法；多页源的额外 IFD 被搬入、≥3 页时越界不可解码（P0）；`ExtraSamples` 与数据语义见 5.7；DateTime/XMP 用编码时刻、XMP 日期非 ISO 8601，产物不可复现。
- **WebP**：VP8/VP8L/ALPH/VP8X/ICCP/EXIF/XMP 结构合法；ICC 经"编码器写 + Exiv2 后写"逐字节保真；有损透明区 RGB 被库清理（无开关）。
- **BMP**：`BITMAPFILEHEADER + BITMAPINFOHEADER(40) + 调色板 + 底向上像素 + 4 字节行对齐`合法；`biSizeImage` 写像素数而非字节数（上游 OIIO 偏差，GDI+/OIIO 不受影响）；无 ICC/EXIF/XMP。
- **HEIF**：`ftyp/heic|heix`、`hvc1`、`hvcC`、`colr=nclx`、`ispe/pixi`、alpha `auxC/auxl`、Exif/XMP item + `cdsc` 均合法；默认 10 位是 RExt PTL；无损 `level_idc=255`（非标准 level 值，解码器普遍忽略）。
- **AVIF**：`ftyp/avif`、`av01`、alpha aux、`colr`、Exif/XMP 合法；SVT 只支持 8/10 位、12 位与 10-bit+alpha 由 UI 探测/预选拦截；libaom 12 位可产。

**合规/兼容硬伤排序**：

1. **TIFF 多页结构损坏**（P0，见 `TIFF-01`）。
2. **alpha 语义跨格式错位**（P0，见 5.7 与 `PNG-01`）。
3. **HEIF 默认 RExt**（P2，默认产物对老解码器更窄）。
4. **PNG 色彩块自相矛盾**（P2，iCCP 与 sRGB/gAMA/cHRM 并存）。
5. **BMP 无 ICC 时 keep 非 sRGB 源静默丢标记**（P1，`BMP-03`）。
6. **TIFF DateTime 非源值 + XMP 非 ISO 8601**（P2，不可复现）。
7. **PNG ancillary 元数据丢失**（P2）。

### 5.6 仅元数据通路：四条像素零改动、三条硬拒绝、一条设计承诺未落地

| 容器 | 能力 | 像素码流证据 | 元数据 | 结论 |
|---|---|---|---|---|
| JPEG | ✅ | SOS 熵数据 782 B，SHA 前后相同 | EXIF/XMP via Exiv2 | **字节级不动像素** |
| PNG | ✅ | IDAT 拼接 124 B，SHA 前后相同 | eXIf/iTXt via Exiv2 | **字节级不动像素** |
| WebP | ✅ | VP8 chunk 306 B，SHA 前后相同 | ICCP/EXIF/XMP via Exiv2 | **字节级不动像素** |
| TIFF | ✅ | strip/tile 压缩载荷 SHA 前后相同；多页保持 | IFD/tag via Exiv2 | **载荷逐字节保真** |
| JXL | ❌ | — | — | 硬失败："does not support metadata-only rewrite"；`design.md` §5.5 承诺"box 级替换（码流 box 原样拷贝）"**未实现** |
| HEIF/AVIF | ❌ | — | — | v1 明确不支持（`design.md` §5.5 已写"v1 不支持"），硬失败而非偷偷重编码 |
| BMP | ❌ | — | — | 无元数据容器，硬失败 |

**安全面（已修复且实证）**：
- `out == src` 判据用 `fs::equivalent` + 绝对路径兜底（`pipeline.cpp:380-397`），重编码与仅元数据两条路径都拒绝；
- Exiv2 写回走"临时文件 + rename"（`exiv2_apply_atomic` / `rewrite_metadata_only`），失败清场、目标保持原样；写失败 `ok=false`（R-002 已修）；
- 源文件全程只读（`copy_file` 只读 src）。

**除像素不变之外的四个遗留问题**（详见 6.5）：

- **输出根落在输入树内会二次施加规则（P1）**：输出被下一轮收集为输入，时间偏移实测 +1d2h 跑两轮变成 +2d4h（`X-METADATA-ONLY-01`，R-031 升级）。
- **隐私剥除只清 EXIF+XMP（P2）**：IPTC/APP13、COM、未知 APPn、PNG 文本块仍留在产物里；"隐私"承诺不完整（`X-METADATA-ONLY-03`）。
- **元数据层不是字节保真（P2）**：无规则 pass-through 也会被 Exiv2 规范化（第三方 XMP 852→2695 B、PNG tEXt→iTXt、WebP 注入 VP8X）；"像素不变"成立，"文件字节不变"不成立（`X-METADATA-ONLY-04/05/09`）。
- **仍强制 OIIO probe（P2）**：CMYK TIFF/截断 JPEG 这类"纯元数据本不需解码"的源被拒，错误文案还带构建路径（`X-METADATA-ONLY-07`）。

**缺口/建议**：
1. **JXL box 级替换未落地（P3，按功能缺口计）**：要么实现（`JxlEncoder` 不适用；需 box 级读写库或自写 BMFF 解析），要么把 `design.md` §5.5 改成"v1 不支持"并在 UI 灰掉 JXL 源。
2. **UI 无前置能力提示**：批量里混入 HEIC/AVIF/JXL/BMP 时，仅元数据模式逐文件失败，用户只能在运行页看到；应在 `ready_to_start()` 预检源扩展名并列出不支持的文件。
3. **仅元数据不做颜色/方向**：正确（像素不动），但 UI 的"色彩目标/旋转方向"控件在仅元数据模式下仍显示（已禁用/置灰？需确认），避免用户以为会生效。

### 5.7 "无损"不是一件事：三级语义必须分开披露

| 级别 | 格式/路径 | 实测 | 说明 |
|---|---|---|---|
| **像素级无损（无 alpha）** | PNG、TIFF、JXL（不含 resampling）、WebP 真无损、BMP（无压缩） | `pp_verify exact` OK | RGB 逐位一致；带 alpha 时见 5.7 下方的 alpha 说明（TIFF 自洽、PNG 低 alpha 有量化损失、WebP/JXL 预乘写入） |
| **码流层无损（颜色转换有损）** | HEIF 8/10/12、AVIF/libaom 8/10/12 | 8 位噪声：122756/196608 样本不同（≈49 dB）；10 位：195021/196608 不同但幅度更小（>60 dB）；12 位更小 | HEVC/AV1 只保证 YCbCr 平面无损；RGB→YCbCr→RGB 的舍入不可逆。**这是格式属性，不是编码器 bug，但 UI 必须说清** |
| **假无损（有损伪装）** | JXL 无损 + resampling>1；WebP 无损 + near_lossless<100；WebP tech=lossy + lossless=true | `pp_verify exact` FAIL（4788/12288、25588/27648、179225/196608） | 参数组合/标志矛盾导致；必须锁定/拒绝/告警 |

> TIFF 的 alpha 语义：原生 strip 字节显示数据为 premultiplied，且 `ExtraSamples=1`（associated）——**标签与数据一致**；TIFF 是唯一自洽格式，不应单列为缺陷（原 `TIFF-03` 已撤回）。

**建议口径**（独立复核后：JXL/WebP 的假无损按 P1，HEIF/AVIF 的码流层无损按 P1 披露，不升 P0）：
- 对 HEIF/AVIF，格式卡/预设/日志统一写「无损（YCbCr 平面；RGB 往返有舍入）」，不要与 PNG/TIFF 的"逐位无损"共用同一个词；
- 对 JXL/WebP，把 `resampling`/`near_lossless` 在无损下锁定，或在勾无损时强制归 1/100；
- `lossless` 标志与 `tech_id` 只保留一个真源（见 5.3）。

### 5.8 预设与跨技术键：配置"存不下来/带错过去"

- **预设不覆盖全局色彩项**：`PresetData` 没有 `color_intent/gamut_mapping/dither8/linear_resample`（也没有 `metadata_only`）。保存再载入，这四项保持当前 UI 值 → "同一预设、不同产物"，`dither8` 最明显（`X-PARAM-ENGINE-08`）。
- **档位不落预设**：`__heif_profile` 是保留键，`params_to_json` 丢弃；HEIF/AVIF 的显式档位约束跨会话丢失（`HEIF-09`/`X-PARAM-ENGINE-07`）。
- **跨技术同键异义**：WebP `quality` 在 lossy 是质量分 90、在 lossless 是努力 80；切技术时 `rebuild_params` 只按"同键同类型"继承 → 90 被当成努力 90（`X-PARAM-ENGINE-05`）。
- **切技术丢编辑值**：JXL 从 VarDCT 切到 Modular 再切回，VarDCT-only 的 10 个高级参数被清回默认；`st->extra` 只兜预设带入的键，不兜 UI 编辑键（`X-PARAM-ENGINE-10`）。
- **隐藏行仍序列化/计数**：JPEG 默认 distance 模式下 `quality=90` 仍在 `values()`/预设里，改它会让"已改 N"+1，但用户看不到该行（`X-PARAM-ENGINE-14`）。
- **预设位深缺省 8**：`spec_from_json` 的 `bitdepth` 缺省 `8`，与 `default_bitdepth_for`（PNG/TIFF/JXL=16、HEIF/AVIF=10）不一致；裁剪掉该字段的预设会静默降位深（`X-PARAM-ENGINE-15`）。

### 5.9 测试门禁为什么没拦住

1. **`pp_verify` 的 alpha 读取与编码器同源**：输入/输出两侧都经 OIIO 关联 alpha，差异对称抵消；金样 `rgba8/rgba16` 夹具 alpha 只有 0/255（关联恒等点）。→ 建议加"半透明 alpha 夹具 + 原生字节比较"，或让 `pp_verify` 支持 `oiio:UnassociatedAlpha=1`。
2. **多页 TIFF 只断言首页**：现有 `tiff` 金样不枚举 subimage；`multi.tif` 只做首页像素。→ 建议加"多页 TIFF → 断言 subimage_count==1"。
3. **参数无效没有"改值必须改字节"的机械探针**：WebP `preset`、JXL `color_transform`/三个 setter、BMP 位深都靠人读代码；→ 建议每个参数加"默认 vs 非默认，产物 SHA 必须不同"的采样探针（对允许 no-op 的项维护白名单）。
4. **`--dev --param` 对 HEIF/AVIF 的整数参数是字符串**：quality 被回退 90、speed/qp 直接失败；这使"CLI 取证"本身不可信。→ 建议 `set_param` 对运行时内省后端先查 live ParamDef，或在 `enc_heif` 里接受字符串并显式报错而不是 fallback。
5. **UI 面没有运行时断言**：可见性/锁定/复选框状态只有 `--ui-smoke` 的少量探针；本报告的 UI 结论以代码路径为主，未做点击级验证。
6. **warning 无 UI 出口**：`WarningKind` 在 `src/ui` 零消费，所有降档/丢元数据/拍平告警只进日志与侧车（`X-COMPLIANCE-08`）。

### 5.10 横向判定（一句话）

**参数"接线"合格，语义"真值"不合格。** 最需要修的不是再加参数，而是：
- 把 alpha 语义、无损语义、位深单位、档位/技术/标志这些"跨参数真值"收敛成单一来源；
- 给"必失败选项"和"静默覆盖"加运行前规则；
- 给"UI 改了必须改变产物"加机械探针。

## 6. 跨格式分项详细审计

> 本章是第 5 章结论的分项证据：参数引擎/下发链/库面/兼容性/仅元数据/运行时实证。跨面条目的 ID 以 `X-` 开头，与第 4 章格式条目重复时以格式条目为准。
>
> **严重度注记**：`X-PARAM-ENGINE-01/02`、`X-WIRING-01/02`、`X-COMPLIANCE-01/02` 的 P0 标定与第 4 章条目重复（前两者对应 JXL-01/WEBP-04，WIRING-01 对应 WEBP-01，WIRING-02 为 HEIF CLI-only，COMPLIANCE-01/02 为 alpha 跨面），总表按 7.0 归一，不重复计入 P0。

### 6.1 参数引擎与 UI 表单（跨格式）

审计对象：`src/core/params.{h,cpp}`（参数引擎）、`src/core/format_tables.cpp`（参数事实表）、`src/ui/paramform.cpp`（表单）、`src/ui/page_output.cpp` + `output_page_build.cpp` + `preset_io.cpp` + `mainwindow_run.cpp`（装配/下发/序列化）、`src/codecs/enc_*.cpp`（读取点）。
只读审计：未修改任何源码；临时脚本与产物在 `build/encoder-audit/param-engine/`。

#### 1. 结论

1. **两个 P0 级"无损谎报"已实测复现**：JXL Modular 勾「无损」+ `resampling>1` → 4788/12288 样本不同；WebP 勾「无损」+ `near_lossless<100` → 25588/27648 样本不同。共同根因 = 这两个参数既没有 `locked` 谓词、也不在 `cross_validate` 规则表里，而 libjxl/libwebp 的"无损模式"**不会**覆盖它们（`SetLossless()` 只重置 modular/distance/ec_distance/color_transform，`libjxl enc_params.h:144-149`；libwebp 的 `near_lossless` 是编码前像素预处理，`vp8l_enc.c:1570-1577`）。设计文档 `docs/param-catalog.md:82` 写的是「resampling 无损 → 隐藏」，实现里 Modular 的 resampling `visible` 谓词为空 —— 文档与代码已经分叉。
2. `default_params` 对静态表**填齐**当前技术全部 schema 键（实测 jpeg 15/15、webp lossy 22/22、tiff 5/5、png 1/1、bmp 0），`fill_defaults` 只补缺不覆盖，`apply_locks` 以入参 `lossless` 为权威**无条件改写**两键（`params.cpp:495-507`）—— 对既有调用方无害（它们都先算同一 lossless），但"唯一写入点"的注释不成立：实际有 `apply_locks`/`paramform refresh`/`page_output spec_of` 三处写 `__lossless`。
3. `validate_params` 校验类型/范围/枚举，**不拒绝未知键**；未知键只在 `cross_validate` 规则③被拒（`params.cpp:599-610`）。两条通道职责分裂：`build_output_specs`（CLI/bench 装配链）只调 `validate_params`，未知键会一路进到编码器（E9 警告），最后靠每文件的 `validate_run_config → cross_validate` 才失败（实测 `--param bogus=1` → 该文件 failed："未知参数：bogus"）。
4. `cross_validate` 只有 5 条规则（TIFF tile 宽/高/成对、webp qmin≤qmax、jpeg progressive⇒optimize_coding）。任务点名的 psnr_target、near_lossless、lossless×resampling、lossless×color_transform、chroma×lossless、bitdepth×backend/alpha、deflate_level×compression、profile×bitdepth/chroma **全部没有规则**；其中 psnr_target 静默覆盖已实测（quality=10 与 100 输出逐字节相同），属已知 R-247。
5. UI 侧最关键的结构性缺陷是**无损标志双真相**：技术下拉选 `Lossless` 不会联动「无损编码」复选框（`paramform.cpp:1268-1282` 完全不碰 `sel.lossless`），而编码器以 `tech_id` 优先（`enc_webp.cpp:152-155`）→ 复选框未勾选、产物却是逐位无损（实测 a6）。JXL 反向同理：`tech=modular` + 复选框未勾 + distance 默认 0 仍然是逐位无损（实测 a7 与 a9 逐字节相同），预设里却记 `lossless=false`。
6. 隐藏行**仍然写入** `values()`/预设（`public_values()` 只过滤 `__` 前缀，`paramform.cpp:1362-1368`），并参与「已改 N」计数（`page_output.cpp:394-403`）；`st->extra` 只兜"预设带入但当前技术不认识"的键（`page_output.cpp:675-684`），**不兜**用户切技术时丢掉的键 → 切技术丢编辑值。
7. 全局项（dither8 / intent / gamut / linear / workers / thread_budget / stagger / flatten_gray / color）都暴露且接线可查（`output_page_build.cpp:258-369`、`settings_dialog.cpp:1184-1268`、`mainwindow_run.cpp:307-323`、`pipeline.cpp:904-911,1053-1061`）；但 intent/gamut/dither8/linear 与 metadata_only **不在 PresetData 里**，预设往返静默丢这五项。
8. 一个"UI 有、引擎永远不读"的实证：WebP `preset` 下拉。`preset=0/2/5` 三次编码输出**逐字节相同**（MD5 相同），因为 `WebPConfigPreset()` 设置的 sns/filter/sharpness/preprocessing 紧接着被显式参数逐个覆盖（`enc_webp.cpp:170-187`），而表默认值（sns 50、sharpness 0）与 tooltip 声称的 photo 档（sns 80、sharpness 3）矛盾。

#### 2. 参数全量矩阵

图例：UI 暴露 = ParamForm 是否建行（复选框独占的 `lossless` 单独标）；生效 ∈ 是/条件/否/未知。读取点 = 编码器实际读该键的位置（`src/` 相对路径）。

##### 2.1 JPEG（jpegli / dct，15 键）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality_mode | 是（核心） | distance | distance\|quality | enc_jpegli.cpp:87,272 | 是 | 被 psnr_target>0 覆盖 |
| distance | 是（核心，visible=quality_mode=distance） | 1.0 | [0.3,3.0] step 0.1 | enc_jpegli.cpp:88,275 | 条件 | 模式=quality 或 psnr_target>0 时无效/被覆盖 |
| quality | 是（核心，visible=quality_mode=quality） | 90 | [1,100] | enc_jpegli.cpp:89,273 | 条件 | 默认模式下隐藏但仍序列化；被 psnr_target 覆盖 |
| chroma | 是（核心） | 444 | 444/440/422/420 | enc_jpegli.cpp:90,290-305 | 是 | 无 lossless 概念（jpeg 无无损技术） |
| progressive | 是（核心） | true | bool | enc_jpegli.cpp:91,281 | 是 | true 时锁定 optimize_coding=true |
| optimize_coding | 是（高级，locked：progressive 时强制 true） | true | bool | enc_jpegli.cpp:92,284 | 条件 | `lock_true_when_progressive`（format_tables:44-48） |
| arith_code | 是（高级，locked false） | false | bool | enc_jpegli.cpp:93,197 | 否 | R-244 假控件：true 必报错 |
| restart_in_rows | 是（高级） | 0 | [0,64] | enc_jpegli.cpp:94,285 | 是 | UI 上限 64，库上限 65535 |
| dct_method | 是（高级，locked 0） | 0 | ISLOW/IFAST/FLOAT | enc_jpegli.cpp:95,286 | 否 | R-246 装饰性：jpegli 不读 |
| smoothing_factor | 是（高级） | 0 | [0,100] | enc_jpegli.cpp:96,287 | 是 | |
| xyb_mode | 是（高级，locked false） | false | bool | enc_jpegli.cpp:97,262 | 否 | R-245：无 XYB ICC 路径 |
| adaptive_quantization | 是（高级） | true | bool | enc_jpegli.cpp:98,288 | 是 | |
| std_quant_tables | 是（高级） | false | bool | enc_jpegli.cpp:99,266 | 是 | |
| psnr_target | 是（高级） | 0.0 | [0,100] step 0.5 | enc_jpegli.cpp:100,277 | 条件 | **>0 静默覆盖前三项**（R-247） |
| cicp_transfer_function | 是（高级） | 2 | [0,18] | enc_jpegli.cpp:101,265 | 条件 | 仅 16=PQ/18=HLG 改量化表，其余码值被接受无效果 |

##### 2.2 JPEG XL（libjxl）— VarDCT 16 键 / Modular 18 键

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| lossless | 复选框（不建行） | false | bool | enc_jxl.cpp:212,438-446 | 是 | 与 `__lossless` 由 apply_locks 同步 |
| effort | 是 | 7 | [1,10] | enc_jxl.cpp:77,402 | 是 | |
| decoding_speed | 是（高级/解码预算） | 0 | [0,4] | enc_jxl.cpp:78,402 | 是 | |
| codestream_level | 是（高级） | 10 | auto(-1)/5/10 | enc_jxl.cpp:348 | 是 | 编码器级选项 |
| distance | 是（核心） | vardct 1.0 / modular 0.0 | [0,25] / [0,15] | enc_jxl.cpp:448 | 条件 | lossless 时忽略；modular+lossless 锁定 0 |
| resampling | 是（高级，两种技术都可见） | -1 | auto/1/2/4/8 | enc_jxl.cpp:79,402 | 是 | **P0-01**：无损下仍生效（catalog:82 声称"无损→隐藏"，实现未隐藏） |
| group_order | 是（高级） | -1 | auto/scanline/center | enc_jxl.cpp:80,402 | 是 | 不影响画质 |
| photon_noise | 是（高级/噪声） | 0 | [0,6400] | enc_jxl.cpp:81,385 | 条件 | 仅 VarDCT；lossless 下隐藏 |
| epf | 是（高级） | 2 | [-1,3] | enc_jxl.cpp:82,398 | 条件 | 仅 VarDCT；lossless 下隐藏 |
| keep_invisible | 是（高级） | true | bool | enc_jxl.cpp:83,393 | 条件 | 仅 VarDCT |
| dots | 是（高级） | true | bool | enc_jxl.cpp:84,393 | 条件 | 仅 VarDCT |
| patches | 是（高级） | true | bool | enc_jxl.cpp:85,393 | 条件 | 仅 VarDCT |
| gaborish | 是（高级） | true | bool | enc_jxl.cpp:86,393 | 条件 | 仅 VarDCT |
| progressive_ac | 是（高级/渐进） | false | bool | enc_jxl.cpp:87,393 | 条件 | 仅 VarDCT |
| qprogressive_ac | 是（高级/渐进） | false | bool | enc_jxl.cpp:88,393 | 条件 | 仅 VarDCT |
| progressive_dc | 是（高级/渐进） | -1 | [-1,2] | enc_jxl.cpp:89,398 | 条件 | 仅 VarDCT |
| color_transform | 是（高级，仅 Modular） | YCoCg | None/YCoCg/XYB/YCbCr | enc_jxl.cpp:415-433 | 条件 | lossless 时 libjxl 的 SetLossless 覆写为 None（实测 XYB 仍逐位一致） |
| modular_group_size | 是（高级，仅 Modular） | -1 | [-1,3] | enc_jxl.cpp:90,398 | 条件 | |
| modular_predictor | 是（高级） | -1 | [-1,15] | enc_jxl.cpp:91,398 | 条件 | |
| modular_palette_colors | 是（高级） | -1 | [-1,4096] | enc_jxl.cpp:92,398 | 条件 | |
| modular_lossy_palette | 是（高级） | false | bool | enc_jxl.cpp:93,393 | 条件 | lossless 时锁定 false |
| brotli_effort | 是（高级） | -1 | [-1,11] | enc_jxl.cpp:94,398 | 否 | `jxl/encode.h:329-334`：只服务 JPEG 重压缩与 brob 元数据 box |
| responsive | 是（高级） | false | bool | enc_jxl.cpp:95,393 | 条件 | libjxl 在 progressive AC 开启时强制 1（enc_frame.cc:122-124） |
| channel_colors_global_percent | 是（高级） | -1 | [-1,100] | enc_jxl.cpp:96,398 | 条件 | -1 跳过（库拒绝显式 -1） |
| channel_colors_group_percent | 是（高级） | -1 | [-1,100] | enc_jxl.cpp:98,398 | 条件 | 同上 |
| modular_ma_tree_learning_percent | 是（高级） | -1 | [-1,200] | enc_jxl.cpp:100,398 | 条件 | |
| modular_nb_prev_channels | 是（高级） | -1 | [-1,11] | enc_jxl.cpp:102,398 | 条件 | |

##### 2.3 PNG / TIFF / BMP

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| png.compressionLevel | 是（核心） | 6 | [0,9] | enc_oiio.cpp:241,248 | 是 | 键名 `png:compressionLevel`（R-010） |
| tiff.compression | 是（核心，兼技术选择器） | lzw | none/lzw/zip/ccittrle/packbits | enc_oiio.cpp:250,254 | 是 | `deflate` 别名未进枚举（库接受，校验会拒） |
| tiff.deflate_level | 是（高级，visible=compression=zip） | 6 | [1,9] | enc_oiio.cpp:256,257 | 条件 | 非 zip 时隐藏且引擎不读，但仍序列化 |
| tiff.predictor | 是（高级，visible=lzw/zip） | 2 | none/horizontal/float | enc_oiio.cpp:260,278 | 条件 | float(3) 选中必报错（enc_oiio.cpp:272-278） |
| tiff.tiff_tile_width | 是（高级） | 0 | [0,4096] step 16 | enc_oiio.cpp:280,293 | 是 | 与高成对规则（params.cpp:248-263） |
| tiff.tiff_tile_height | 是（高级） | 0 | [0,4096] step 16 | enc_oiio.cpp:281,294 | 是 | 同上 |
| bmp.* | 无参数行 | — | — | enc_oiio.cpp:205-209（仅位深） | — | 位深 8 与 24 对 RGB 源产物逐字节相同（实测） |

##### 2.4 WebP（libwebp）— lossy 22 键 / lossless 5 键

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| lossless | 复选框（不建行） | false | bool | enc_webp.cpp:152-155 | 条件 | tech_id 优先于标志（P1-04） |
| quality | 是（核心） | lossy 90（质量分）/ lossless 80（努力） | lossy [1,100] / lossless [0,100] | enc_webp.cpp:169（有损）/195（无损） | 是 | 一键两义（R-209）；跨技术继承会改语义（P2-05） |
| sharp_yuv | 是（核心） | true | bool | enc_webp.cpp:192 | 条件 | 仅 lossy |
| method | 是（高级） | 4 | [0,6] | enc_webp.cpp:174,196 | 是 | |
| preset | 是（高级） | 2（photo） | default/picture/photo/drawing/icon/text | enc_webp.cpp:165,170 | 否 | **P2-06**：被后续显式赋值全覆盖，实测 0/2/5 字节相同 |
| sns_strength | 是（高级） | 50 | [0,100] | enc_webp.cpp:175 | 条件 | 仅 lossy；覆盖 preset 的 80 |
| filter_strength | 是（高级） | 30 | [0,100] | enc_webp.cpp:176 | 条件 | 仅 lossy |
| autofilter | 是（高级） | false | bool | enc_webp.cpp:179 | 条件 | 仅 lossy |
| pass | 是（高级） | 1 | [1,10] | enc_webp.cpp:180 | 条件 | 仅 lossy |
| filter_sharpness | 是（高级） | 0 | [0,7] | enc_webp.cpp:177 | 条件 | 需 filter_strength>0/autofilter |
| filter_type | 是（高级） | true | bool | enc_webp.cpp:178 | 条件 | 同上 |
| segments | 是（高级） | 4 | [1,4] | enc_webp.cpp:181 | 条件 | 仅 lossy |
| alpha_compression | 是（高级） | true | bool | enc_webp.cpp:182 | 条件 | 仅含 alpha 的输入 |
| alpha_filtering | 是（高级） | 1 | [0,2] | enc_webp.cpp:183 | 条件 | 仅含 alpha |
| alpha_quality | 是（高级） | 100 | [0,100] | enc_webp.cpp:184 | 条件 | 仅含 alpha |
| partitions | 是（高级） | 0 | [0,3] | enc_webp.cpp:185 | 条件 | 仅 lossy |
| partition_limit | 是（高级） | 0 | [0,100] | enc_webp.cpp:186 | 条件 | 仅 lossy |
| preprocessing | 是（高级） | 0 | [0,7] | enc_webp.cpp:187 | 条件 | 仅 lossy |
| qmin | 是（高级） | 0 | [0,100] | enc_webp.cpp:188 | 条件 | qmin≤qmax 规则（params.cpp:265-275） |
| qmax | 是（高级） | 100 | [0,100] | enc_webp.cpp:189 | 条件 | 同上 |
| emulate_jpeg_size | 是（高级） | false | bool | enc_webp.cpp:190 | 条件 | 仅 lossy |
| low_memory | 是（高级） | false | bool | enc_webp.cpp:191 | 条件 | 仅 lossy |
| exact | 是（高级，visible=is_lossless） | true | bool | enc_webp.cpp:197 | 条件 | 标志关闭但 tech=lossless 时隐藏却被消费 |
| near_lossless | 是（高级，**恒可见**） | 100 | [0,100] | enc_webp.cpp:198 | 是 | **P0-02**：<100 即编码前有损预处理 |

##### 2.5 HEIF / AVIF（libheif 运行时内省）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality | 是（核心） | 插件当前值 | 插件 int 域 | enc_heif.cpp:436-453（通用循环）+502 | 条件 | 插件拒绝即整文件失败（enc_heif.cpp:513） |
| lossless | 复选框（不建行；内省同名字段被独占） | false | bool | enc_heif.cpp:392,506,543 | 条件 | 无损强制 chroma=444（enc_heif.cpp:554-567） |
| chroma | 是（核心） | 插件当前值（420） | 420/422/444 | enc_heif.cpp:558,614,624 | 条件 | 无损时被 UI+引擎同时强制 444；UI 只置灰不改写 |
| preset | 是（核心，仅 x265 命中） | 插件当前值 | 插件值集 | enc_heif.cpp:470 | 条件 | SVT/libaom 无此键（白名单已按插件对表，enc_heif.cpp:216-221） |
| speed | 是（核心，仅 AVIF） | 插件当前值 | 插件值集/整型 | enc_heif.cpp:470 或 453 | 条件 | |
| tune | 是（核心） | 插件当前值 | 插件值集 | enc_heif.cpp:470 | 条件 | |
| 其余内省项（tu-intra-depth/complexity/qp/min-q/max-q/threads…） | 是（高级/编码工具） | 插件当前值 | 插件域 | enc_heif.cpp:436-514 | 条件 | threads 由调度器覆盖（enc_heif.cpp:586-591） |
| `__heif_profile` | 是（档位行，非 ParamForm 行） | 0=自动 | Auto/Main/Main10/Main12/MainStill、Auto/Main/High/Professional | enc_heif.cpp:649 | 条件 | 与位深/色度不一致 → 拒绝（enc_heif.cpp:650-654）；**不落预设**（P2-07） |
| `x265:`/`svt:`/`aom:` 透传键 | 否（无行） | — | 插件私有点位 | enc_heif.cpp:520-534 | 条件 | cross_validate 白名单放行（params.cpp:174-178）；预设可承载 |

##### 2.6 内部键

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `__lossless` | 否（内部管道键） | false | bool | enc_jxl.cpp:212 / enc_webp.cpp:155 / enc_heif.cpp:392（经 `lossless_flag`，params.cpp:307-310） | 是 | 三处写入点：apply_locks、paramform refresh、page_output spec_of |

#### 3. 发现

##### X-PARAM-ENGINE-01 [P0] JXL「无损」+ resampling>1 产出非逐位无损文件

- 证据：`src/core/format_tables.cpp:188-207`（`jxl_resampling_param`：`p.visible = lossy_only ? not_lossless : std::function<bool(const ParamSet &)>{}`）；`src/core/format_tables.cpp:594`（Modular 用 `jxl_resampling_param(false)` → visible 为空 = **恒可见**、无 locked）；`src/core/params.cpp:219-289`（cross_rules 只有 5 条，无任何 lossless×resampling 规则）；`src/codecs/enc_jxl.cpp:373-410`（帧选项循环对 `resampling` 不判 lossless 直接 `JxlEncoderFrameSettingsSetOption`）；`vcpkg_installed/x64-windows-avx2/include/jxl/encode.h:140-146`（"the image is downsampled before compression, and upsampled to original size in the decoder"）；`vcpkg/buildtrees/libjxl/src/v0.12.0-.../lib/jxl/enc_params.h:144-149`（`SetLossless()` 只重置 modular_mode/butteraugli_distance/ec_distance/color_transform，**不重置 resampling**）；`enc_frame.cc:458`（`frame_header->upsampling = cparams.resampling`）。
- 事实：UI 路径 = JXL 高级参数里把「编码前降采样」设为 2/4/8 → 勾「无损编码」（自动切 Modular，`format_tables.cpp:594` 的 resampling 行在 Modular 下没有 `not_lossless` 谓词，值随 `rebuild_params` 同类型继承，`paramform.cpp:897-905`）→ `apply_locks` 只锁 `distance`/`modular_lossy_palette`，不锁 resampling → 下发 `{lossless=true, resampling=2}` → enc_jxl 先设 RESAMPLING=2 再 `SetFrameLossless(true)` → libjxl 先 2×2 降采样再编 → 解码端升采样。实测：`--format jxl --tech modular --lossless --param resampling=2` → `VERIFY jxl-modular-lossless-resampling2 FAIL exact mismatch: 4788/12288 samples differ`。
- 影响：用户勾了「无损编码」，产物不是逐位无损（且实际分辨率被降采样后放大，细结构丢失）——正是"静默把有损当无损"。预设可保存该组合并反复复用；`cross_validate` 与 `on_start` 闸门都放行。
- 建议：在 `format_tables.cpp` 的 `jxl_resampling_param` 上区分技术：Modular 版加 `p.locked = lock_one_when_lossless`（返回 `ParamValue{int64_t(1)}`）或至少 `visible = not_lossless`；同时在 `params.cpp:cross_rules()` 增加规则 `{jxl, "", require_all_keys=false, {resampling, lossless}, 判 lossless_flag && resampling>1 → 中文错误}`，并给 `enc_jxl.cpp` 加防御（lossless 时强制 `RESAMPLING=1` 或直接报错）。同步修 `docs/param-catalog.md:82/106` 的口径。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-02 [P0] WebP「无损」+ near_lossless<100 产出非逐位无损文件

- 证据：`src/core/format_tables.cpp:848-855`（`near_lossless` 的 `.visible = {}, .locked = {}` —— 恒可见、无锁定，工具提示自认"<100 时启用 VP8L 近无损预处理、输出非严格无损"）；`src/codecs/enc_webp.cpp:193-199`（lossless 分支照样写入 `cfg.near_lossless`）；`vcpkg/buildtrees/libwebp/src/v1.6.0-.../src/enc/vp8l_enc.c:1570-1577`（`use_near_lossless = (config->near_lossless < 100) && !use_palette && !use_predict` → `VP8ApplyNearLossless`）；`src/core/params.cpp:479-519`（apply_locks 不触碰 near_lossless）。
- 事实：勾「无损」→ 技术自动切 Lossless → 高级参数里 near_lossless 默认 100，用户调到 0..99 → 下发 `{lossless=true, near_lossless=0}` → libwebp 在无调色板/无预测变换的图上执行近无损色量化。实测（96×96 噪声图）：`near_lossless=0` 12294 字节 / `near_lossless=100` 27758 字节，`pp_verify` 对 near_lossless=0 报 `FAIL exact mismatch: 25588/27648 samples differ`。注意此路径有库侧前置条件（`!use_palette && !use_predict`），小图/调色板图可能不触发（我在 64×64 图上复现为 0 差异）——但触发条件与图像内容相关，用户无法预知。
- 影响：勾着「无损编码」的产物被近无损预处理改像素；预设/批处理会把它套到所有图上，部分图无损、部分图有损，不可预期。
- 建议：`near_lossless` 增加 `locked`：lossless 时锁 100（`lock_value_when_lossless(100)`）；或在 `cross_rules()` 加 `{webp,"","",{lossless,near_lossless}, lossless && near_lossless<100 → 中文红字}`；`enc_webp.cpp` 无损分支在 `near_lossless<100` 时记 `note_param` 并在 `EncodeResult.warnings` 之外的日志外再失败或降级为显式"近无损"档，二选一（产品裁决）。tooltip 现文案已承认有损，应把"输出非严格无损"提到 label 或行内红字。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-03 [P1] psnr_target>0 静默压掉 quality_mode/distance/quality，UI 无禁用无红字

- 证据：`src/codecs/enc_jpegli.cpp:271-280`（先 `jpegli_set_quality`/`jpegli_set_distance`，随后 `if (psnr_target>0) jpegli_set_psnr(...)`）；`src/core/format_tables.cpp:392-401`（tooltip 自认"设为 >0 后静默覆盖质量控制方式/视觉距离/质量三项"；`locked` 为空）；`src/core/params.cpp:219-289`（无对应规则）。
- 事实：实测 `--param quality_mode=quality --param quality=10 --param psnr_target=30` 与 `quality=100` 两次输出 MD5 完全相同（E3C3A08A…），而 `psnr_target=0` 时 `quality=10` 得到另一种文件（5AC76F5E…，419 vs 484 字节）。即 quality 在该组合下**零影响**，UI 仍照常可编辑、照常计入「已改 N」、照常写进预设。
- 影响：用户调「质量」看产物不变，无法从界面得知 PSNR 目标接管了质量控制；批处理预设会带着一个无效的质量值到处跑。
- 建议：`psnr_target` 加 `locked`/`visible` 反向联动（`quality_mode`/`distance`/`quality` 在 `psnr_target>0` 时 `locked` 到当前值并置灰，禁用原因入 tooltip）；并在 `cross_rules()` 加一条提示级规则。R-247 已登记该问题，本审计补的是"实测证据 + 现行为"。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:516` R-247 / `docs/param-catalog.md:52`）。

##### X-PARAM-ENGINE-04 [P1] 无损标志双真相：`tech=Lossless` 与「无损编码」复选框互不同步，产物与 UI/预设相反

- 证据：`src/ui/paramform.cpp:1268-1282`（`user_set_tech` 只拦"lossless 已开时选非无损技术"，**从不**在选到 lossless_capable 技术时把 `sel.lossless` 置真）；`src/ui/paramform.cpp:571-604`（复选框 checked 只跟随 `sel.lossless`）；`src/codecs/enc_webp.cpp:152-155`（`tech_id=="lossless" → lossless=true`，无视参数）；`src/codecs/enc_jxl.cpp:224-236,448`（`tech_id=="modular"` 且标志 false → 不调 SetFrameLossless，但 distance 默认 0 → `IsLossless()` 为真，`enc_params.h:125-133`）；`src/ui/preset_io.cpp:466-468`（预设里 `lossless` 字段由参数决定）。
- 事实：WebP → 技术下拉选「Lossless」→ 复选框保持未勾选 → 编码走 lossless 分支。实测 a6：`tech=lossless lossless=false`（侧车 `params=... lossless=false`），`pp_verify` 判 **OK（逐位一致）**。JXL 反向：`--tech modular`（不传 --lossless）实测输出与 `--tech vardct --lossless` **逐字节相同**（MD5 4DFAD857…），即"未勾无损"的产物是真无损，而预设会记 `lossless=false`。
- 影响：界面显示的"是否无损"与产物事实脱钩；用户按复选框判断取舍（体积/兼容性）会判断错；预设导出/导入把错误标志固化。
- 建议：`user_set_tech` 里按 `t->lossless_capable` 反推 `sel.lossless`（选到 lossless_capable 即置真、选到有损技术即置假），或反过来在 `values()`/`spec_of()` 里让 `lossless` 由 `tech.lossless_capable && lossless_flag` 派生；`enc_webp.cpp:152-155` 的 tech 优先级需与 `lossless_flag` 单源化。同时把 `kLosslessKey` 的三处写入点（`params.cpp:495-507`、`paramform.cpp:912-915`、`page_output.cpp:730`）收敛。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-05 [P2] 切技术把同键异义值原样带过去（webp quality：质量分 90 → 压缩努力 90）

- 证据：`src/ui/paramform.cpp:894-908`（`rebuild_params` 只按 `find_param + same_typed_value` 继承旧值，不看语义/默认差异）；`src/core/format_tables.cpp:226-255`（同一 key `quality` 在 lossy 是 [1,100] 质量分 def 90、在 lossless 是 [0,100] 压缩努力 def 80）。
- 事实：webp 默认 lossy + quality=90 → 切到 Lossless 技术 → `quality=90` 被继承为"努力 90"（默认应为 80）→ 编码明显变慢、体积更小，但 UI 上没有任何"值已改"的语义提示（只有"≠def"的改值标记，而此时 90≠80 恰好会标记，用户却不知道原因）。
- 影响：跨技术切换产生"看起来没改、实际全变"的参数；预设在同一 key 上表达两种含义，跨技术复用即语义漂移。
- 建议：`rebuild_params` 对 `def.type/def.lo/def.hi` 或 `key` 语义不同（webp quality）的技术切换禁用继承，回落到新技术的 `def`；或拆键（受预设 v2 兼容约束，须走加性字段）。最小修法 = 在 `ParamDef` 上加 `rebind_on_tech_switch` 语义位。
- 状态：confirmed（代码路径确定；未做 GUI 级复现）；known=no。

##### X-PARAM-ENGINE-06 [P2] WebP `preset` 是死控件，tooltip 描述的效果不存在

- 证据：`src/codecs/enc_webp.cpp:162-192`（`WebPConfigPreset(&cfg, preset, quality)` 之后**逐字段覆盖** sns_strength/filter_strength/filter_sharpness/filter_type/autofilter/pass/segments/alpha_*/partitions/partition_limit/preprocessing/method/quality）；`vcpkg/buildtrees/libwebp/src/v1.6.0-.../src/enc/config_enc.c:71-76`（photo 档设置 sns=80、sharpness=3、filter=30、preprocessing|=2）；`src/core/format_tables.cpp:705-718`（tooltip 声称"photo：sns 80、filter 30、sharpness 3"），`format_tables.cpp:719-725,746-753`（表默认 sns=50、sharpness=0 覆盖之）。
- 事实：实测 `--param preset=0` / `2` / `5`（其余全默认）三次输出 MD5 **完全相同**（15AD1FFC…）。即该下拉对产物零影响，而 tooltip 却承诺会改三个参数。
- 影响：用户按内容类型选"文字/图标"期待体积/锐度变化，实际毫无变化；文档与实现两张皮。
- 建议：要么在 `enc_webp.cpp` 里只在用户未显式改过某字段时才让 preset 生效（需要"是否改过"的语义，当前 ParamSet 无此信息 → 更实际的修法是**删除 preset 行**或把它改成"调用一次 WebPConfigPreset 后不再覆盖 sns/sharpness"），要么把 tooltip 改成"仅作初始值，本表所有显式参数均会覆盖它"。推荐后者 + 视觉上标注"（不生效）"。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-07 [P2] 编码档位不落预设；profile×位深×色度没有交叉规则（只在编码期拒绝）

- 证据：`src/ui/page_output.cpp:733-735`（`spec_of` 写保留键 `__heif_profile`）；`src/ui/preset_io.cpp:388-396`（`params_to_json` 丢弃所有 `__` 前缀键）；`src/core/params.cpp:599-610`（未知键规则跳过 `__` 前缀）；`src/codecs/enc_heif.cpp:641-654`（档位不一致在**编码期**报中文错误）。
- 事实：用户选 AVIF「Professional」→ 保存预设 → JSON 里没有档位字段（`page_output_impl.h:203` 注释也确认"保留键：不落预设"）→ 重新载入回到「自动」；位深/色度则由预设恢复。若手写或旧预设给出与引擎推导不一致的组合（例如改 bitdepth=12 + chroma=444 后档位残留 Professional），`cross_validate` 不检查，`on_start` 闸门放行，直到每个文件编码时才失败。
- 影响：档位选择跨会话丢失（与"预设应完整复现配置"的预期相反）；不一致组合不是跑前阻断而是逐文件失败。
- 建议：把档位提升为 `OutputFormatSpec` 的正式字段（加性追加）并进 v2 序列化（或新增非 `__` 前缀的 `heif_profile` 键并加入 declared_keys）；同时在 `cross_rules()` 加 heif/avif 规则（档位 ∈ Auto 或 `heif_profile_compatible` 通过）。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-08 [P2] 预设不覆盖四个色彩全局项与「仅元数据」模式

- 证据：`src/core/presets.h:41-51`（`PresetData` 只有 outputs/output_template/split_by_format/color_target/conflict/rules）；`src/ui/page_output.cpp:1052-1065`（`collect_preset` 只写 color_target/conflict/template）；`src/ui/page_output.cpp:1025-1043`（`apply_preset` 只回填 color/conflict/template）；`src/ui/output_page_build.cpp:282-360`（intent/gamut/dither8/linear 四个控件）。
- 事实：输出页把「渲染意图 / 色域映射 / 8bit 抖动 / 线性光重采样」与「转换模式」都作为全局配置呈现，但 PresetData 没有对应字段 → 存预设再载入，这四项与 metadata_only 保持当前值（不恢复、也不提示）。
- 影响：一套"预设"不能完整复现一次运行的全局设置；dither8 会显著改变 8bit 产物像素，最容易造成"同样预设、不同产物"。
- 建议：`PresetData` 末尾加性追加 `color_intent/gamut_mapping/dither8/linear_resample`（+ 可选 `metadata_only`）并在 `preset_io` 读写（缺省 = 现行为，v2 向后兼容）。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-09 [P2] avif「svt-av1 + >8bit + alpha」只有 UI 提示，没有交叉规则

- 证据：`src/codecs/enc_heif.cpp:676-677`（该组合直接 `fail("svt-av1 does not support 10-bit with alpha; use backend libaom")`）；`src/ui/page_output.cpp:745-787`（`update_alpha_hint` / `evaluate_avif_alpha_preselect` 仅提示与**预选** libaom，`backend_user_set` 后不再改写）；`src/core/params.cpp:219-289`（无规则）。
- 事实：用户在 AVIF 页手动把后端改回 svt-av1，或预设/CLI 指定 svt-av1 + bitdepth=10 + 含 alpha 源 → UI 只显示黄字提示（"请改用 libaom 后端"），`on_start` 的 `cross_validate` 不拦，每文件在编码期失败。
- 影响：批处理整体失败（逐文件 error），用户须自己把"提示"与"失败原因"对上。
- 建议：`cross_validate` 增加 avif 规则（需要 bitdepth；可由 spec 传入或新增带 bitdepth 的重载）；或在 `ready_to_start()` 里对 alpha 组合做硬阻断。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-10 [P2] 切技术丢用户编辑值（`st->extra` 只兜预设键，不兜 UI 编辑键）

- 证据：`src/ui/paramform.cpp:894-908`（重建工作集 = 新技默认 + 仅"新技认识且同类型"的旧键）；`src/ui/page_output.cpp:675-684`（`st->extra` 只在 `apply_state_to_form`（预设落值路径）里收集"表单不认的键"）；`src/ui/page_output.cpp:706-737`（`spec_of` 把 extra 并回 params）。
- 事实：JXL 默认 VarDCT → 用户改 `photon_noise=200/epf=3/gaborish=false` 等 10 个 VarDCT-only 参数 → 勾「无损」自动切 Modular：这些键既不在新技 `values`、也不在 `st->extra`（extra 为空）→ 从 `spec_of` 消失；取消无损切回 VarDCT 时按键集合继承规则拿不到它们（`values` 里已无）→ **恢复默认**。预设路径同理（除预设本身携带的键）。
- 影响：一次无损勾选/取消就静默清掉一整组高级参数；用户只会觉得"我的设置没了"。
- 建议：`rebuild_params` 把旧技术不认识但**曾属于本格式**的键快照进 `st->extra`（或 ParamForm 内部保留 per-tech 值表），切回时还原；`snapshot_state` 已按 extra 合并，无需改下发链。
- 状态：confirmed（代码路径）；known=no。

##### X-PARAM-ENGINE-11 [P2] 未知键职责分裂：validate_params 不管，只有 cross_validate 管

- 证据：`src/core/params.cpp:534-552`（只遍历该技术 schema 键，未知键不参与 → 不报）；`src/core/params.cpp:592-610`（规则③未知参数拒绝；`is_plugin_passthrough_key` 对 heif/avif 放行）；`src/core/pipeline.cpp:1739-1767`（`build_output_specs` 只调 `validate_params`，不调 `cross_validate`）；`src/core/pipeline.cpp:1673-1679`（`validate_run_config` 才调 `cross_validate`，属每文件阶段①）。
- 事实：实测 `--format png --param bogus=1` → 装配链放行（编码器侧 E9 只记日志），随后该文件以 `error: 未知参数：bogus` 失败（`files=1 ok=0 failed=1`）。GUI 路径因 `on_start` 先调 `cross_validate` 而能在开跑前拦住，CLI/bench 则没有这个前置闸门。
- 影响：同一份配置在 GUI 被拦、在 `--dev`/bench 变成"每文件失败"；未知键还会先经过编码器（heif 透传键除外）。
- 建议：`validate_params` 末尾复用同一张 `declared_keys` 表做未知键判定（或 `build_output_specs` 补一次 `cross_validate`），保证"参数校验"单入口语义完整。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-12 [P3] 无效/装饰控件仍以正常控件暴露（4 类）

- 证据：`src/core/format_tables.cpp:336-343`（arith_code 锁 false）、`:352-364`（dct_method 锁 0）、`:371-378`（xyb_mode 锁 false）；`:549-556`（brotli_effort，`jxl/encode.h:329-334` 仅服务 JPEG 重压缩/brob box）；`src/ui/paramform.cpp:596-602` + `format_tables.cpp:606-607,625`（png/tiff/bmp 的 `lossless_capable=true` → 复选框显示，但这些技术不声明 `lossless` 参数、`enc_oiio.cpp` 也从不读它）。
- 事实：arith_code/xyb_mode/dct_method 被锁定并置灰（有 tooltip 原因，符合"不静默灰"）；brotli_effort 可编辑、被传进 libjxl，但对本管线产物无影响；png/tiff/bmp 的「无损编码」勾选只影响预设里的 `lossless` 字段（`preset_io.cpp:466-468` 的输出级字段），编码行为不变（这些格式本就无损）。
- 影响：用户以为在控制什么，实际不控制；把"无损"复选框当成质量/体积开关。
- 建议：brotli_effort 归组到"元数据/JPEG 重压缩"或直接隐藏；非声明格式的复选框改为只读文本（"本格式恒定无损"）而非可勾控件。
- 状态：confirmed；known=部分（R-244/245/246 已登记假控件，brotli/无损复选框无登记）。

##### X-PARAM-ENGINE-13 [P3] TIFF `predictor=float(3)` 仍在枚举里，选中必错

- 证据：`src/core/format_tables.cpp:651-667`（choices 含 `{"float", 3}`，tooltip 自认"选中即明确报错"）；`src/codecs/enc_oiio.cpp:272-278`（`predictor==3` → `fail("tiff: predictor=float (3) is not usable here ...")`）。
- 事实：8/16bit 整型输出路径下该档 100% 失败；UI 不禁用该选项（`limit_enum_choices` 只被 HEIF 色度使用）。
- 影响：边界配置下用户选到"看起来合法"的档位就整批失败。
- 建议：从 choices 删除 3，或在 ParamForm 构造期按格式禁用（`limit_enum_choices` 已有机制）。
- 状态：confirmed；known=yes（R-011 支线处置，`format_tables.cpp:663-666` 已注记）。

##### X-PARAM-ENGINE-14 [P3] 隐藏参数仍进 values/预设并计入「已改 N」

- 证据：`src/ui/paramform.cpp:1362-1368`（`public_values()` 只过滤 `__` 前缀）；`src/ui/page_output.cpp:394-403`（`changed_param_count` 遍历技术全部参数，不判 `visible`）；`src/ui/paramform.cpp:976,995-1002`（可见性与改值标记是两套判据）。
- 事实：jpeg 默认 `quality_mode=distance` → `quality` 行隐藏但值 90 恒在 `values()`/预设里；`quality` 改成 80 后「已改 N」+1，而用户当前看不到这一行。tiff `compression=lzw` 时 `deflate_level` 同样隐藏却序列化（改它也会计数）。
- 影响：「已改 N」与实际可见改动不一致；预设携带一批用户看不见的参数，换技术/换格式后可能含义变化。
- 建议：`changed_param_count` 只统计 `eval_visible` 为真的行；隐藏行仍序列化（保证路由安全）但打上"非当前生效"标记（或从 spec 里剔除未被当前技术消费的键）。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-15 [P3] 预设位深缺省 8，与 `default_bitdepth_for` 不一致

- 证据：`src/ui/preset_io.cpp:482`（`out.out_bitdepth = static_cast<int>(o.value("bitdepth").toInteger(8));`）；`src/core/pipeline.cpp:1718-1734`（jxl/png/tiff=16、heif/avif=10、bmp=24）；`src/ui/page_output.cpp:699`（落值路径 `set_bitdepth_options(..., keep_if_valid=false)`，非法则静默回默认）。
- 事实：手写/裁剪掉 `bitdepth` 字段的 v2 预设会被读成 8 位；对 jxl/png/tiff 等于把默认高质量档静默降一半位深；若该值不在交集（如 heif 后端不支持）则再静默落回默认。
- 影响：预设与 UI 默认值口径不一致；用户看不到"位深被改了"的提示。
- 建议：`spec_from_json` 缺省改走 `pp::default_bitdepth_for(format_id)`（core 已有单源），并在 `apply_state_to_form` 对"预设位深不可用→回退"给出可见提示。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-16 [P3] libheif 内省 Int 参数的无界范围污染注释与「自动」哨兵

- 证据：`src/codecs/enc_heif.cpp:172-181`（无 min/max 时 `lo=INT32_MIN, hi=INT32_MAX`）；`src/ui/paramform.cpp:215-226`（Int 分支 `spec.min=lround(p.lo)`、`spec.auto_magic = spec.min < 0`、`spec.note = range_note_text(p)`）；`src/ui/paramform.cpp:170-181`（`p.lo<0` → 注释写成"自动…hi"）。
- 事实：插件不公布边界的整型参数（如 `min-q`/`max-q`/`qp` 一类）在 UI 上显示范围注释「自动…2147483647」，且最小值被当成"编码器自选"哨兵（`specialValueText="自动"`）——与 JXL 的 `-1=自动` 语义无关。
- 影响：提示失真；当某个合法值恰好等于最小值时显示"自动"，误导用户。
- 建议：`auto_magic` 只在 `lo == -1`（或 schema 显式声明 magic 值）时启用；内省无界参数不写范围注释。
- 状态：confirmed；known=no。

##### X-PARAM-ENGINE-17 [P3] BMP 位深下拉的 8/24 两档对 RGB 源无可观测差异

- 证据：`src/codecs/enc_oiio.cpp:200-209`（bmp 两档都 `out_type = UINT8`）；`src/core/format_tables.cpp:868-875`（`.bitdepths = {8, 24}`，注释自认"RGB 源下与 24 等价"）。
- 事实：实测 `--format bmp --bitdepth 8` 与 `--bitdepth 24` 输出逐字节相同（MD5 B2E85890…，12342 字节）。
- 影响：用户以为在选色深，实际没有任何差别（灰度源才有 8bpp 之分）。
- 建议：位深行对 bmp 只在灰度输入时提供 8 档，或注释里直接说明"RGB 源两档等价"。
- 状态：confirmed；known=部分（R-090/R-211 已注记双语义）。

##### X-PARAM-ENGINE-18 [P3] `apply_locks` 以入参为准无条件改写无损两键；"唯一写入点"注释与实现不符

- 证据：`src/core/params.cpp:495-507`（`__lossless` 与 `lossless` 都按入参 `lossless` 覆盖，缺失或不等于入参即写）；`src/core/params.h:91-95`（注释宣称"`apply_locks()` 是引擎侧的同步点""唯一写入点"）；另外两处写入：`src/ui/paramform.cpp:912-915`、`src/ui/page_output.cpp:730`。
- 事实：任何调用方若传入与集合内相反的 lossless，`apply_locks` 会静默翻转用户/预设的值（现有调用方都先算同一值，故当前无漂移；但这是契约型风险）。三处写入意味着不变式由"约定"而非"单点"保证。
- 影响：新增调用方（未来导入、批处理、第三方）容易造出 `lossless=true` + `__lossless=false` 的分裂集合；`lossless_flag` 的优先级（显式参数优先）会掩盖一半的漂移。
- 建议：`apply_locks` 在两键互相矛盾时记 warn 并返回冲突列表（而不是静默改写）；把 `paramform`/`page_output` 的两处手写改为调用同一 helper；更新 `params.h` 注释口径。
- 状态：confirmed；known=no（R-295 只登记了字面量分裂）。

#### 4. 输出兼容性与合规矩阵

只列"参数组合改变产物结构/语义"的行；"标准工具可读"基于本项目实测（pp_verify/OIIO 解码）与库头文档，未用第三方查看器逐项验证。

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| JXL Modular + 无损 + resampling=2/4/8 | 码流仍合法；帧头 `upsampling=2/4/8`（libjxl enc_frame.cc:458），实际以 1/N 分辨率编码、解码端升采样 | 是（djxl/OIIO 正常解码，尺寸不变） | **"无损"声明为假**（像素被改 + 有效分辨率下降）；与 `docs/param-catalog.md:82`"无损→隐藏"的设计相反 |
| JXL Modular + 无损 + color_transform=XYB | libjxl `SetLossless()` 把色彩变换覆写为 None（enc_params.h:148），选项被静默忽略 | 是 | 低（文档需说明"无损下该选项无效"）；实测逐位一致 |
| WebP Lossless + near_lossless<100 | 仍为合法 VP8L（lossless 格式），但像素已被近无损预处理改过（条件：无调色板/无预测变换，vp8l_enc.c:1572） | 是 | **"无损"声明为假**；且是否触发取决于图像内容，不可预期 |
| WebP `tech=Lossless` 但「无损」未勾选 | 合法 VP8L 无损文件 | 是 | 预设 `lossless=false` 与产物事实相反（P1-04） |
| JXL `tech=modular` + 未勾无损 + distance=0 | 合法 JXL，`IsLossless()` 为真（enc_params.h:125-133） | 是 | 预设 `lossless=false` 与产物事实相反（P1-04） |
| HEIF/AVIF 无损 | **强制 4:4:4**（enc_heif.cpp:554-567）→ HEVC 为 Main 4:4:4 / AV1 High；且 ≤16MP 以外走 grid | 是 | 查看器兼容性弱于 4:2:0（tooltip 已告警）；YCbCr 域无损、RGB↔YCbCr 往返有舍入（非逐位） |
| HEIF/AVIF 档位与位深/色度不一致 | 编码期拒绝，不产出文件（enc_heif.cpp:650-654） | —（无产物） | 低（无坏文件），但属"逐文件失败"而非跑前阻断 |
| AVIF svt-av1 + >8bit + alpha | 编码期拒绝（enc_heif.cpp:676-677） | —（无产物） | 低（无坏文件），UX 上应跑前阻断 |
| TIFF tiled（tile 非 16 倍数 / 只设一边） | 编码期拒绝（enc_oiio.cpp:280-295 + params.cpp:222-263 双保险） | —（无产物） | 低 |
| TIFF predictor=float(3) | 编码期拒绝（enc_oiio.cpp:272-278） | —（无产物） | 低（但 UI 仍提供该选项） |
| BMP bitdepth=8（RGB 源） | 实际写 UINT8 RGB 24bpp，与 24 档逐字节相同 | 是 | 低（选项无意义，易误解为"8 位灰度"） |
| PNG/TIFF 默认 16 位（8 位源） | 16bps 容器写回 8 位精度的值 | 是 | 低（体积翻倍、无画质收益；属产品默认口径） |

#### 5. 未覆盖与存疑

1. **GUI 级复现缺失**：所有 UI 路径结论来自代码阅读（`paramform.cpp`/`page_output.cpp`），未启动 GUI（任务禁止）。"切技术丢值"（P2-10）、"quality 跨技术继承"（P2-05）未做界面级取证，因果链由 `rebuild_params` 代码确定。
2. **near_lossless 触发条件**：libwebp 仅在"无调色板且无预测变换"的 crunch 配置下执行 `VP8ApplyNearLossless`（`vp8l_enc.c:1570-1577`）。我在 64×64 像素图（`tests/golden/base/rgb8.png`）上设 `near_lossless=0` 复现为 **0 差异**；换成 96×96 随机噪声图才复现 25588/27648 差异。因此"哪些图会中招"依赖图像内容，P0 的证据是"存在可复现输入"，不是"所有图都会变"。
3. **未逐条实测的交叉约束**：chroma×lossless（HEIF）由 UI + 引擎双重强制 444，我读码确认但未跑"预设 lossless+420"的端到端（引擎会 `note_param` 后强制 444，属显式提示、不改语义）。deflate_level×compression 的"隐藏但仍序列化"由代码确认，未做预设往返实测。
4. **预设往返丢档位/色彩四项**（P2-07/P2-08）未做 GUI 存/取实测（`save_preset`/`load_preset` 需 GUI 或自写 JSON 驱动；`presets_dialog` 未在范围内）。`params_to_json` 丢弃 `__` 键是逐字可查的事实，`PresetData` 无对应字段也是结构体事实，故判 confirmed（丢字段），但"用户实际感知"未实测。
5. **库行为未覆盖**：libjxl `SetLossless` 的 resampling 保留是 v0.12.0 源码事实；若上游未来版本改动（例如 SetLossless 也重置 resampling），P0-01 会自愈 —— 当前 vcpkg 钉版为 0.12.0。
6. **`--param` 面**：本审计用 `--dev` 做的实测覆盖 jxl/webp/tiff/png/jpeg/bmp 的默认集与 5 个反例（resampling、near_lossless、psnr、preset、未知键）；heif/avif 的运行时内省面只读码，未跑 `--format heif/avif`（需依赖插件探测，且与本主题的单格式结论无关）。

##### 实测命令（可复核）

```
# P0-01
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png \
  --out build/encoder-audit/param-engine/a2 --format jxl --tech modular --lossless \
  --param resampling=2 --workers 1
python tools/env.py run -- build/release-dev/pp_verify.exe \
  build/encoder-audit/param-engine/a2/case/expected.json \
  build/encoder-audit/param-engine/a2/rgb8.jxl
# → VERIFY jxl-modular-lossless-resampling2 FAIL exact mismatch: 4788/12288 samples differ

# P0-02（噪声图由 build/encoder-audit/param-engine/gen_noise.py 生成）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/param-engine/noise.png \
  --out build/encoder-audit/param-engine/nl0 --format webp --tech lossless --lossless \
  --param near_lossless=0 --param method=4 --workers 1
python tools/env.py run -- build/release-dev/pp_verify.exe \
  build/encoder-audit/param-engine/nl0root/case/expected.json \
  build/encoder-audit/param-engine/nl0/noise.webp
# → FAIL exact mismatch: 25588/27648 samples differ（near_lossless=100 时 27758 字节且逐位一致）

# P1-03（psnr 覆盖）
… --format jpeg --param quality_mode=quality --param quality=10 --param psnr_target=30   # MD5 E3C3A08A…
… --format jpeg --param quality_mode=quality --param quality=100 --param psnr_target=30  # MD5 E3C3A08A…（相同）
… --format jpeg --param quality_mode=quality --param quality=10                          # MD5 5AC76F5E…（不同）

# P1-04（tech=lossless 未勾选 → 仍无损）
… --format webp --tech lossless --workers 1     # 侧车 tech=lossless lossless=false；pp_verify OK（逐位一致）

# P2-06（preset 死控件）
… --format webp --param preset=0 / 2 / 5        # 三次 MD5 均 15AD1FFC…
```

### 6.2 配置下发链与键矩阵（跨格式）

> 只读审计，基线 0.3.5.alpha2（`build/release-dev`，PP_BUILD_DEV=ON）。所有实测命令经
> `python tools/env.py run -- …`；产物落在 `build/encoder-audit/`。证据行号相对仓库根。

#### 1. 结论

1. **下发链的"搬运"是干净的，"语义"有三处断点**：`config_base → RunConfig.outputs →
   plan_targets → EncodeRequest.target.params` 全链**逐键透传、无改写**（`pipeline.cpp:486-491`
   只做赋值），`plan_targets` **不重新套默认/锁定**，`build_output_specs` 也只被 `--dev`/bench
   使用（GUI 不经它）。真正的问题在编码器读取面与 UI 的判定面。
2. **发现 2 例"静默把有损当无损"**：webp `--tech lossy --lossless` 产出 VP8（有损）而全链记
   `lossless=true`（实测 97239/110592 采样不一致、侧车 `params` 仍写 lossless=true）；heif
   `--param lossless=true` 产出有损默认档（3148 B）而侧车同样写 `lossless=true`。两者均**退出码 0**。
   前者根因在 `enc_webp.cpp:152-155`（`tech_id=="lossy"` 直接盖掉 lossless 标志），后者根因在
   `dev_cli.cpp:387-393`（静态表没有的键按**裸字符串**下发）+ `enc_heif.cpp:500-513`（recovery
   路径用硬编码 fallback）。
3. **1 个 UI 控件完全无效**：webp「内容预设」`preset`——编码器先 `WebPConfigPreset` 再逐项覆盖
   （`enc_webp.cpp:165-192`），而 libwebp 的 preset 只写 sns/filter/sharpness/segments/preprocessing
   这 5 个字段（`config_enc.c:64-96`）→ 实测 `preset=0` 与 `preset=2` 输出**逐字节相同**。
4. **HEIF/AVIF 档位不落预设**：`spec_of()` 把档位写进保留键 `__heif_profile`，`params_to_json`
   过滤所有 `__` 前缀键（`preset_io.cpp:388-396`）→ 预设保存后档位丢失，载入回「自动」；而
   `mainwindow.cpp:1909-1912` 启动即载入 `last_preset`，于是**跨会话丢档位**。
5. **预设不覆盖输出页四项色彩能力开关**（渲染意图/色域映射/8bit 抖动/线性光重采样）：
   `PresetData` 根本没有这些字段（`presets.h:41-51`）。其中三项（intent/gamut/dither8）
   **改变产物**，因此"预设 = 配置快照"在这个维度上不成立。
6. **UI 侧 1 个类型错读**：`current_chroma()` 用 `param_int` 读字符串枚举 `chroma`
   （`page_output.cpp:459-464` + `params.cpp:425-432`）→ 恒得 fallback `1`（4:2:0）。
   后果：档位推导说明恒按 4:2:0 算、HEVC「档位 + 4:4:4」的不兼容在 UI 上**不报红**
   （`heif_profile.h:106-113` 那段专门为此写的判定永不触发），到编码期才逐文件失败。
7. **E3 契约（`encoder.h:13`）逐条对上**：jxl=`JxlThreadParallelRunner(E)`、webp=`thread_level=E>1`、
   heif/avif=插件 `threads`、jpegli/oiio=无内部线程并记日志。缺口两处（均已登记）：x265 无
   `threads` 参数（`encoder_x265.cc:220-301` 的 7 个参数里确实没有）、OIIO 全局线程池
   `attribute("threads")` 未接管（`enc_oiio.cpp:174-188` 注释自述）。
8. **全局设置里没有编码项**：`AppSettings`（`core/settings.h:87-143`）只有 workers/thread_budget/
   stagger/budget_gb/flatten_gray/rotate_orientation 与元数据无关项；色彩目标与 8bit 抖动在
   **输出页全局卡**（`output_page_build.cpp:255-360`）并正确进 `RunConfig`。唯一"编码相关但不持久"
   的是：输出页的**路径模板**编辑不写回 `AppSettings`（`mainwindow.cpp:1454-1462` 只写
   last_format/last_out_root/last_preset），只有设置对话框那条路会持久化。

#### 2. 参数全量矩阵

列口径：**UI 暴露**=是否渲染成行/控件（`条件`=有 `visible` 谓词或仅在某技术下存在）；
**默认**=schema `ParamDef::def`（内省键=插件当前值）；**读取点**=编码器读取处 file:line；
**生效**=实测/代码可证的生效性（`条件`=仅在特定配置下被读或仅在该条件下影响产物）。
读取点若同时给出"映射表"处，表示经表驱动下发（`enc_jxl` 的 `kFrameSettings`、`enc_heif` 的内省循环）。

##### 2.1 JPEG（jpegli / dct，15 键，全部被读）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| jpeg.quality_mode | 是（核心） | "distance" | distance / quality | enc_jpegli.cpp:87,272-276 | 是 | 二选一联动 distance/quality 显隐 |
| jpeg.distance | 条件（quality_mode=distance） | 1.0 | [0.3,3.0] 步 0.1 | enc_jpegli.cpp:88,275 | 条件（且 psnr_target=0） | UI 范围窄于库 0–25（R-294） |
| jpeg.quality | 条件（quality_mode=quality） | 90 | [1,100] | enc_jpegli.cpp:89,273 | 条件（且 psnr_target=0） | — |
| jpeg.chroma | 是 | "444" | 444/440/422/420 | enc_jpegli.cpp:90,292-305 | 是 | 灰度源忽略采样因子 |
| jpeg.progressive | 是 | true | bool | enc_jpegli.cpp:91,281 | 是 | level=2 / 0 |
| jpeg.optimize_coding | 是（高级） | true | bool | enc_jpegli.cpp:92,284 | 条件 | `progressive=true` 时被 `lock_true_when_progressive` 钉 true（format_tables.cpp:44-48） |
| jpeg.arith_code | 是（高级，禁用） | false | bool | enc_jpegli.cpp:93,197-199 | 否 | 恒锁 false（format_tables.cpp:343）；true 会直接报错 |
| jpeg.restart_in_rows | 是 | 0 | [0,64] | enc_jpegli.cpp:94,285 | 是 | UI 上限 64 ≪ 库 65535 |
| jpeg.dct_method | 是（高级，禁用） | 0 | ISLOW/IFAST/FLOAT | enc_jpegli.cpp:95,286 | 否（装饰） | 恒锁 0；jpegli 固定浮点 DCT（R-246） |
| jpeg.smoothing_factor | 是 | 0 | [0,100] | enc_jpegli.cpp:96,287 | 是 | — |
| jpeg.xyb_mode | 是（高级，禁用） | false | bool | enc_jpegli.cpp:97,262-264 | 否 | 恒锁 false；全仓无 XYB ICC 路径（R-245） |
| jpeg.adaptive_quantization | 是 | true | bool | enc_jpegli.cpp:98,288 | 是 | — |
| jpeg.std_quant_tables | 是 | false | bool | enc_jpegli.cpp:99,266-268 | 是 | 须在 set_defaults 前调用 |
| jpeg.psnr_target | 是 | 0.0 | [0,100] | enc_jpegli.cpp:100,277-280 | 条件（>0） | **>0 时静默覆盖** quality_mode/distance/quality（R-247，已知） |
| jpeg.cicp_transfer_function | 是 | 2 | [0,18] | enc_jpegli.cpp:101,265 | 条件（仅 16/18 改量化表） | 其余码值被接受但无效果 |

##### 2.2 JPEG XL（libjxl，27 键，全部被读）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| jxl.effort | 是（核心） | 7 | [1,10] | enc_jxl.cpp:77（表驱动 373-410） | 是 | 库默认 7 |
| jxl.decoding_speed | 是（高级/解码预算） | 0 | [0,4] | enc_jxl.cpp:78 | 是 | — |
| jxl.codestream_level | 是（高级） | 10 | -1/5/10 | enc_jxl.cpp:348-353 | 是 | 库默认 -1（auto），表默认 10 |
| jxl.distance | 是 / 条件 | vardct 1.0；modular 0.0 | vardct[0,25]；modular[0,15] | enc_jxl.cpp:448-454 | 条件（!lossless） | 无损时被 `SetFrameLossless` 覆盖；modular 默认 0 ⇒ 未勾无损也真无损（X-WIRING-09） |
| jxl.photon_noise | 是（高级/噪声） | 0.0 | [0,6400] | enc_jxl.cpp:81,385-390 | 条件（!modular；值<0 跳过） | — |
| jxl.epf | 是（高级） | 2 | [-1,3] | enc_jxl.cpp:82 | 是（-1 跳过=库默认） | — |
| jxl.keep_invisible | 是（高级） | true | bool | enc_jxl.cpp:83 | 条件（varDCT） | 库用 -1 表默认，Bool 无法表达（表注） |
| jxl.dots | 是（高级） | true | bool | enc_jxl.cpp:84 | 条件（varDCT） | 同上 |
| jxl.patches | 是（高级） | true | bool | enc_jxl.cpp:85 | 条件（varDCT） | 同上 |
| jxl.gaborish | 是（高级） | true | bool | enc_jxl.cpp:86 | 条件（varDCT） | 同上 |
| jxl.progressive_ac | 是（高级/渐进） | false | bool | enc_jxl.cpp:87 | 条件（varDCT） | 表默认 false 顶替库 -1 |
| jxl.qprogressive_ac | 是（高级/渐进） | false | bool | enc_jxl.cpp:88 | 条件（varDCT） | — |
| jxl.progressive_dc | 是（高级/渐进） | -1 | [-1,2] | enc_jxl.cpp:89 | 条件（varDCT；-1 跳过） | — |
| jxl.resampling | 是（高级） | -1 | -1/1/2/4/8 | enc_jxl.cpp:79 | 条件（-1 跳过） | 真降采样会降有效分辨率 |
| jxl.group_order | 是（高级） | -1 | -1/0/1 | enc_jxl.cpp:80 | 条件（-1 跳过） | — |
| jxl.lossless | 是（复选框，无行） | false | bool | enc_jxl.cpp:212,438-446 | 是 | 与 `__lossless` 由 `apply_locks` 同步（params.cpp:495-506） |
| jxl.color_transform | 是（modular） | "YCoCg" | None/YCoCg/XYB/YCbCr | enc_jxl.cpp:415-434 | 条件（modular） | YCoCg 走 MODULAR_COLOR_SPACE(6)，其余走 COLOR_TRANSFORM |
| jxl.modular_group_size | 是（高级） | -1 | [-1,3] | enc_jxl.cpp:90 | 条件（modular；<0 跳过） | — |
| jxl.modular_predictor | 是（高级） | -1 | [-1,15] | enc_jxl.cpp:91 | 条件 | — |
| jxl.modular_palette_colors | 是（高级） | -1 | [-1,4096] | enc_jxl.cpp:92 | 条件 | UI 上限 4096（库未规定） |
| jxl.modular_lossy_palette | 是（高级） | false | bool | enc_jxl.cpp:93 | 条件（modular） | 无损时锁 false（format_tables.cpp:548） |
| jxl.brotli_effort | 是（高级） | -1 | [-1,11] | enc_jxl.cpp:94 | 条件（modular） | 库注：服务 JPEG 重压缩/元数据 box，与 modular 编码无关 |
| jxl.responsive | 是（高级） | false | bool | enc_jxl.cpp:95 | 条件（modular） | — |
| jxl.channel_colors_global_percent | 是（高级） | -1 | [-1,100] | enc_jxl.cpp:96 | 条件 | 实测 libjxl 显式传 -1 会报错 ⇒ 代码以"跳过"表默认 |
| jxl.channel_colors_group_percent | 是（高级） | -1 | [-1,100] | enc_jxl.cpp:98 | 条件 | 同上 |
| jxl.modular_ma_tree_learning_percent | 是（高级） | -1 | [-1,200] | enc_jxl.cpp:100 | 条件 | — |
| jxl.modular_nb_prev_channels | 是（高级） | -1 | [-1,11] | enc_jxl.cpp:102 | 条件 | — |

##### 2.3 PNG / TIFF / BMP（OIIO，6 键）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| png.compressionLevel | 是（核心） | 6 | [0,9] | enc_oiio.cpp:241-248 | 是（实测 0→221918 B / 9→78331 B） | 属性名 `png:compressionLevel`（R-010 已修） |
| tiff.compression | 是（核心，兼技术选择器） | "lzw" | none/lzw/zip/ccittrle/packbits | enc_oiio.cpp:250-254 | 是 | 未知名**直接失败**（不静默回退 deflate） |
| tiff.deflate_level | 条件（compression=zip） | 6 | [1,9] | enc_oiio.cpp:255-259 | 条件（实测 zip 1→109153 B / 9→106756 B） | 属性 `tiff:zipquality` |
| tiff.predictor | 条件（lzw/zip） | 2 | none1/horizontal2/float3 | enc_oiio.cpp:260-279 | 条件 | float(3) 在整型样本下**前置拒绝**并给中文原因 |
| tiff.tiff_tile_width | 是（高级） | 0 | [0,4096] 步 16 | enc_oiio.cpp:280-295 | 条件（>0） | 宽高必须同为 0 或同为 16 的倍数 |
| tiff.tiff_tile_height | 是（高级） | 0 | [0,4096] 步 16 | enc_oiio.cpp:281 | 条件（>0） | 同上 |
| bmp.（无参数） | — | — | — | enc_oiio.cpp:205-218 | — | 技术 raw 的 `params` 为空表（format_tables.cpp:866-867） |

##### 2.4 WebP（libwebp，24 键）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| webp.quality | 是（核心） | lossy 90 / lossless 80 | lossy[1,100] / lossless[0,100] | enc_webp.cpp:169-173（lossy）、195（lossless） | 是 | **一键两义**：lossy=质量分、lossless=压缩努力（库语义，表注 R-209） |
| webp.sharp_yuv | 是 | true | bool | enc_webp.cpp:192 | 条件（!lossless） | — |
| webp.method | 是（高级） | 4 | [0,6] | enc_webp.cpp:174 / 196 | 是 | — |
| webp.preset | 是（高级） | 2（photo） | 0..5 | enc_webp.cpp:165-172 | **否** | 预设写入的 5 个字段随后被 174-192 逐项覆盖 ⇒ 零效果（实测同字节，X-WIRING-03） |
| webp.sns_strength | 是（高级） | 50 | [0,100] | enc_webp.cpp:175 | 条件（!lossless） | — |
| webp.filter_strength | 是（高级） | 30 | [0,100] | enc_webp.cpp:176 | 条件（!lossless） | — |
| webp.autofilter | 是（高级） | false | bool | enc_webp.cpp:179 | 条件（!lossless） | — |
| webp.pass | 是（高级） | 1 | [1,10] | enc_webp.cpp:180 | 条件（!lossless） | — |
| webp.filter_sharpness | 是（高级） | 0 | [0,7] | enc_webp.cpp:177 | 条件（filter_strength>0 或 autofilter） | — |
| webp.filter_type | 是（高级） | true | bool | enc_webp.cpp:178 | 条件（同上） | false=simple / true=strong |
| webp.segments | 是（高级） | 4 | [1,4] | enc_webp.cpp:181 | 条件（!lossless） | — |
| webp.alpha_compression | 是（高级） | true | bool | enc_webp.cpp:182 | 条件（有 alpha） | — |
| webp.alpha_filtering | 是（高级） | 1 | [0,2] | enc_webp.cpp:183 | 条件（有 alpha） | — |
| webp.alpha_quality | 是（高级） | 100 | [0,100] | enc_webp.cpp:184 | 条件（有 alpha） | — |
| webp.partitions | 是（高级） | 0 | [0,3] | enc_webp.cpp:185 | 条件（!lossless） | log2 语义 |
| webp.partition_limit | 是（高级） | 0 | [0,100] | enc_webp.cpp:186 | 条件（!lossless） | — |
| webp.preprocessing | 是（高级） | 0 | [0,7] | enc_webp.cpp:187 | 条件（!lossless） | 按位组合 |
| webp.qmin | 是（高级） | 0 | [0,100] | enc_webp.cpp:188 | 条件（!lossless） | 交叉规则 qmin≤qmax（仅 webp+lossy，params.cpp:265-275） |
| webp.qmax | 是（高级） | 100 | [0,100] | enc_webp.cpp:189 | 条件（!lossless） | 同上 |
| webp.emulate_jpeg_size | 是（高级） | false | bool | enc_webp.cpp:190 | 条件（!lossless） | — |
| webp.low_memory | 是（高级） | false | bool | enc_webp.cpp:191 | 条件（!lossless） | — |
| webp.exact | 条件（visible=is_lossless） | true | bool | enc_webp.cpp:197 | 条件（lossless 且有 alpha） | 无损技术下常显（该技术只在 lossless 时被选） |
| webp.near_lossless | 是（高级） | 100 | [0,100] | enc_webp.cpp:198 | **否**（实测） | libwebp 仅当 `!use_palette && !use_predict` 才应用（vp8l_enc.c:1572-1573）⇒ method 0/1/2/6 × 0/50/100 全同字节（X-WIRING-10） |
| webp.lossless | 是（复选框） | false | bool | enc_webp.cpp:152-155 | 条件 | **tech_id 与标志冲突时 tech 赢**（P0，X-WIRING-01） |

##### 2.5 HEIF / AVIF（libheif 运行时内省，代表键）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| heif.quality | 是（核心） | 插件当前值（x265=50） | [0,100] | enc_heif.cpp:436-514（回落 501-504） | 是（GUI）/ **否**（`--param` 路径，X-WIRING-04） | 内省 ParamDef 的 `def` 取插件当前值（enc_heif.cpp:168-170） |
| heif.lossless | 是（复选框） | false | bool | enc_heif.cpp:392,505-507,543 | 是（`--lossless` 生效：3148→3221 B） | 无损强制 chroma=444（554-567） |
| heif.chroma | 是（核心） | "420" | 420/422/444（**字符串**枚举） | enc_heif.cpp:456-475,607-627 | 是（实测 444→3029 B） | 无损时强制 444；UI 侧读它读错（X-WIRING-08） |
| heif.preset | 是（x265 核心） | "slow" | 10 档 | enc_heif.cpp:469-470 | 是 | SVT/libaom 无此键（内省表隔离） |
| heif.tune | 是（核心） | "ssim" | psnr/ssim/grain/fastdecode | enc_heif.cpp:469-470 | 是 | — |
| heif.tu-intra-depth | 是（高级） | 2 | [1,4] | enc_heif.cpp:453 | 是 | 仅 x265 |
| heif.complexity | 是（高级） | 0 | [0,100] | enc_heif.cpp:453 | 是 | 插件 `has_default=false` ⇒ UI 默认退成最小值 0（enc_heif.cpp:177） |
| heif.speed | 是（SVT/aom 核心） | 插件当前 | [0,8]/[0,9] | enc_heif.cpp:453 | 是 | 仅 AVIF 系 |
| heif.threads | 是（高级） | 插件当前（逻辑核） | [1,64] | enc_heif.cpp:588（覆盖参数循环 453 的值） | **否**（被 `encode_threads` 无条件覆盖，X-WIRING-12） | x265 无此键 |
| heif.tile-rows / tile-cols / qp / min-q / max-q / alpha-quality … | 是（高级） | 插件当前 | 插件域 | enc_heif.cpp:453 | 是 | 整数选择集型走 Enum+int 分支（456-475） |
| `x265:*` / `svt:*` / `aom:*` | **否**（仅预设/CLI） | — | 任意字符串 | enc_heif.cpp:520-534 | 是 | 插件私有点位透传；`cross_validate` 豁免（params.cpp:174-178） |
| （无参数） | — | — | — | 静态表 techs 为空（format_tables.cpp:877-894） | — | CLI 路径 `params=[]`（X-WIRING-17） |

##### 2.6 非 schema 的隐藏键 / 内部键

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `__lossless` | 否（内部） | false | bool | params.cpp:307-310 `lossless_flag`；enc_jxl.cpp:212、enc_webp.cpp:152-155、enc_heif.cpp:392 | 是 | `apply_locks` 是唯一写入点（params.cpp:495-499）；`__` 前缀被未知参数校验/预设序列化/日志快照一致过滤 |
| `__heif_profile` | 是（「编码档位」下拉） | 0 = 自动 | 0..6 | enc_heif.cpp:648-654；写：page_output.cpp:359-361/733-735 | 是（会话内） | **不落预设**（preset_io.cpp:391-392）⇒ 跨会话丢失（X-WIRING-05） |

**统计**：静态表 73 键（jpeg 15 / jxl 27 / png 1 / tiff 5 / webp 24 / bmp 0）+ 2 个保留键；
全部键均被对应编码器**读取到**（`dead schema key`=0 例"完全没人读"）；
**读后无效果** 3 键（webp.preset、webp.near_lossless、jpeg.dct_method）+ **锁定不可达** 2 键
（jpeg.arith_code、jpeg.xyb_mode）；**只在特定条件读取** 22 键（见上表「条件」格）；
**编码器 fallback ≠ schema 默认** 4 处（X-WIRING-15）。

#### 3. 发现

##### X-WIRING-01 [P0] webp：`tech=lossy` + `lossless=true` 静默产出有损，全链仍记 lossless=true
- 证据：`src/codecs/enc_webp.cpp:152-155`
  `const bool lossless = (req.target.tech_id == "lossless") ? true : ((req.target.tech_id == "lossy") ? false : lossless_flag(params));`
  —— tech_id 优先于无损标志；`src/core/format_tables.cpp:834-835` 的 lossy 技术**也声明**了
  `lossless_param`，故 `apply_locks`（`src/core/params.cpp:500-506`）会把 `lossless=true` 写进该
  技术的参数集，链路上看不出矛盾。
- 事实：`python tools/env.py run -- build/release-dev/photopipeline.exe --dev
  build/encoder-audit/tex.png --out … --format webp --tech lossy --lossless` →
  输出 `RIFF/VP8 `（有损块），而 <out>/*.pp.json 的 `"params"` 写 `… lossless=true …`、`ok=true`、
  退出码 0；`pp_verify` exact 断言 **FAIL 97239/110592 samples differ**。对照组
  `--tech lossless --lossless` → `VP8L`、34 314 B、exact **OK**。
- 影响：任何以下发面（`--dev`、预设 JSON 手写 `"tech":"lossy"` + `"lossless":true`、将来任何直接
  调 `EncodeRequest` 的调用方）表达"无损"的配置都会得到有损产物，且侧车/日志/预设都记成无损
  ⇒ 用户按"无损"归档、后续以有损母版再做处理，属不可逆数据降级。
  当前 **GUI 不可达**：表单在 `lossless=true` 时会自动切到 lossless_capable 技术并把非能力项置灰
  （`src/ui/paramform.cpp:1274-1277、1289-1301、1328-1336`）——但这道守卫在 UI 层，不在引擎层。
  另注：`heif` 侧无此问题（lossless 覆盖 tech，见 `enc_heif.cpp:536-546`）。
- 建议：`enc_webp.cpp` 的 tech 解析改为"`tech_id=="lossless"` ⇒ 真；`tech_id=="lossy"` 且
  `lossless_flag(params)==true` ⇒ **返回 `enc::encode_error("tech 'lossy' conflicts with lossless=true")`**"
  （不静默选边）；或让 `lossless_flag` 优先、tech 只作缺省。同时在 `cross_validate`
  （`src/core/params.cpp:219-289`）加一条 (webp, lossy, lossless=true) 规则，让 `--dev` 在配置期
  就拒。回归口径：默认参数零变化（默认 lossless=false）。
- 状态：confirmed；known=no

##### X-WIRING-02 [P0] heif：`--param lossless=true` 静默产出有损（CLI 参数类型错位）
- 证据：`src/dev_cli.cpp:387-393` —— 静态表查不到的键按**裸字符串**写入：
  `const ParamDef *def = pp::lookup_param_def(...); if (!def) { s[key] = raw; … }`；
  `src/codecs/enc_heif.cpp:476-483` 的 Bool 分支 `std::get_if<bool>` 失败 → `rejected`，
  再走 recovery `enc_heif.cpp:505-507`：`param_bool(req.target.params,"lossless",false)` ——
  字符串值取不到 ⇒ **false** ⇒ `heif_encoder_set_lossless(enc, 0)`（有损）。
- 事实：`--format heif --param lossless=true` → 3148 B（与**不带任何参数**的有损默认档**逐字节相同**），
  侧车 `"params": "lossless=true"`、`ok=true`、退出码 0；同一图上 `--lossless` → 3221 B（真无损）。
  该格式静态表 `techs` 为空（`format_tables.cpp:877-884`），`lookup_param_def` 必然返回 nullptr
  （`params.cpp:378-390`）。
- 影响：用户以为加了 `lossless=true` 就是无损，实际拿到有损且无任何警告；产物与侧车/日志矛盾。
  仅 `--dev` 可达（GUI 走内省 ParamDef，值类型是 bool/int）。
- 建议：`dev_cli.cpp:387-393` 不要用裸字符串兜底 —— 未知键直接报错；对 libheif 系先用
  `pp::lookup_param_def` 的**内省并集**（`params.cpp:135-159 compute_declared_keys` 已有）解析类型，
  或让 `default_params`/`build_output_specs` 对 runtime_introspected 格式注入内省默认值。
- 状态：confirmed；known=no

##### X-WIRING-03 [P1] webp「内容预设」`preset` 完全无效（编码器读后被逐项覆盖）
- 证据：`src/codecs/enc_webp.cpp:165-172` 先 `WebPConfigPreset(&cfg, preset, quality)`，紧接着
  174-192 把 `method/sns_strength/filter_strength/filter_sharpness/filter_type/autofilter/pass/
  segments/alpha_*/partitions/partition_limit/preprocessing/qmin/qmax/emulate_jpeg_size/low_memory/
  sharp_yuv` 全部按参数重写；而 libwebp 的 preset 只写
  `sns_strength / filter_sharpness / filter_strength / preprocessing / segments`
  （`vcpkg/buildtrees/libwebp/src/v1.6.0-…/src/enc/config_enc.c:64-96`）——五个字段全在重写名单里。
- 事实：同一输入 `--param preset=0` 与 `--param preset=2` 输出 **SHA256 完全相同**
  （`build/encoder-audit/p0/photo.webp` vs `p2/photo.webp`），业务 bytes 均 108。
- 影响：UI 高级区「内容预设」是纯装饰控件；用户为"照片"调优的期望（sns 80 / sharpness 3）不会发生。
- 建议：二选一 —— ①`preset` 作为**铺底**：把 165-192 改成"预设之后只覆盖用户**显式偏离默认**的键"
  （需要 UI 传 deviate 集合，成本高）；②承认 `preset` 冗余并从 `format_tables.cpp:705-718` 删除该行
  （或锁定为不可改 + tooltip 说明"，与 arith_code 同处置）。推荐 ②（一行数据 + 一条 lock 谓词，
  零回归）。
- 状态：confirmed；known=no（`docs/param-catalog.md` 把 webp 全参标为"完整"，未记录此项）

##### X-WIRING-04 [P1] heif/avif：`--param quality=N` 静默用 90，其它整型/布尔键直接失败
- 证据：同 X-WIRING-02 的类型通路；`src/codecs/enc_heif.cpp:501-504` 的 recovery 用
  `param_int(req.target.params, "quality", 90)` —— 字符串值取不到 ⇒ **fallback 90** ⇒
  `heif_encoder_set_lossy_quality(enc, 90)`。
- 事实：`--param quality=10 / 50 / 90` 三次输出 **同一 SHA256**（10245 B）；不带参数 → 3148 B
  （插件默认 50）。`--param complexity=10`（非 quality/lossless 的整数键）→
  `encode failed: parameter 'complexity' rejected by backend: expected integer`、退出码 1。
  字符串键正常：`--param chroma=444` → 3029 B ≠ 3148 B。
- 影响：CLI 上"能设"与"设了有用"完全脱节：质量被钉死在 90，其余整型/布尔旋钮不可用；
  日志/侧车仍打印用户写的值。
- 建议：与 X-WIRING-02 同一修法（类型从内省解析）。另把 `enc_heif.cpp` 的 recovery 分支里的
  `90`/`false` 硬编码 fallback 改为**从内省 ParamDef 默认值取**，避免"用户值丢失后被静默替换成
  另一个数"。
- 状态：confirmed；known=no

##### X-WIRING-05 [P1] HEIF/AVIF「编码档位」不进预设，跨会话丢失
- 证据：写面 `src/ui/page_output.cpp:733-735`（`spec.params[kHeifProfileParamKey] = 档位序号`）与
  `:359-361`（snapshot）；序列化面 `src/ui/preset_io.cpp:388-396`：
  `if (key.rfind("__", 0) == 0) continue; // 保留键（__lossless）不落盘` —— 一刀切掉所有 `__` 键；
  读回面 `src/ui/page_output.cpp:686-696` 取不到即 `want=0`（自动）。
  设计注记见 `src/codecs/heif_profile.h:44-47`（明确写"不落预设"）。
- 事实：`collect_preset()` 产出的 spec **带** `__heif_profile`（`:1057-1058` 调 `spec_of`），
  `save_preset()` 丢弃；`load_preset()` 后档位行回到「自动」。启动路径
  `src/ui/mainwindow.cpp:1909-1912` 会自动载入 `last_preset` ⇒ **重启即丢**。
- 影响：用户选「Main12（12 位）」/「Professional（12 位 / 4:2:2）」保存预设，重开后被静默改成
  「自动」；因为档位是"反查校验"用的（`enc_heif.cpp:641-666` 不一致即拒），丢失后该校验也一并消失
  ⇒ 产物档位由位深/色度被动推导，与用户当初的选择可能不同且无任何提示。
- 建议：`preset_io.cpp` 不要再按 `__` 前缀一刀切：要么给档位一个**非保留键**落点
  （如 `outputs[i].heif_profile`，与 `lossless` 字段同型），要么显式把 `kHeifProfileParamKey`
  列进白名单并在 `spec_to_json/spec_from_json` 成对读写。后者改动最小，且不动冻结面。
- 状态：confirmed；known=no

##### X-WIRING-06 [P1] 预设不保存输出页四项色彩能力开关（其中三项改变产物）
- 证据：`src/core/presets.h:41-51` 的 `PresetData` 只有
  `outputs/output_template/split_by_format/color_target/conflict/rules`；
  `src/ui/page_output.cpp:1052-1065 collect_preset` 只写这些；`preset_io.cpp` 的
  `spec_to_json/load_v2` 也没有 `color_intent/gamut_mapping/color_dither8/linear_resample`；
  `apply_preset`（`page_output.cpp:983-1050`）不触碰这四个控件。
- 事实：这四项的下发通路本身是通的（`page_output.cpp:917-923` → `pipeline.cpp:907-911` →
  `colormanager.cpp:846,1001,1024` 与 `EncodeRequest::dither8`），只是**不参与预设**。
  `smoke_probes.cpp:5549-5631` 只断言"config_base 读回"，不覆盖预设往返。
- 影响：载入预设 ⇏ 恢复当初的色彩行为。换机/重启后 `dither8`/`gamut_mapping` 静默回到关，
  产物与"上次用这个预设得到的文件"不同（8bit 抖动能改变每个像素的低位）。
- 建议：把四项追加到 `PresetData` **末尾**（加性，冻结面登记）并在 `preset_io` 的 root 层读写
  （与 `color_target` 同层，不塞进 `outputs[i].params` —— 它们是全局项）。
- 状态：confirmed；known=no

##### X-WIRING-07 [P1] `current_chroma()` 用 `param_int` 读字符串枚举 ⇒ 恒取 4:2:0
- 证据：`src/ui/page_output.cpp:459-464`
  `return int(pp::param_int(st->form->values(), std::string("chroma"), 1));`
  而 `params.cpp:425-432` 的 `param_int` 只接受 `int64_t`，其它类型一律回落 fallback；
  libheif x265 插件的 `chroma` 是**字符串**参数（`vcpkg/buildtrees/libheif/src/v1.23.5-…/libheif/
  plugins/encoder_x265.cc:291-298`，`valid_values = {"420","422","444"}`），
  `param_def_from` 因此产出 `Enum` + `std::string` 值（`enc_heif.cpp:190-206`）。
- 事实：`current_chroma()` **恒返回 1**。三个下游全部被污染：
  ①`update_profile_note`（`page_output.cpp:544-552`）的档位推导恒按 4:2:0 算 ⇒ AVIF 选 4:4:4 时
   提示写「插件自选 → Main」而非 High/Professional；`if (id=="heif" && chroma==3)`（548-552 行，
   专门为 HEVC 4:4:4 补正的那段）**永不执行**；
  ②`heif_profile_compatible(want, depth, 1, true)` 因此**永远不会命中**
   `hevc_profiles && chroma == 3` 的拒绝分支（`heif_profile.h:106-113`）⇒ 用户先选 chroma=444
   再选「Main10」时 UI 显示"兼容"（无红字），开跑才被 `enc_heif.cpp:650-654` 逐文件拒绝；
  ③`apply_chroma_limits` 的档位→色度限幅按错误 chroma 判定。
- 影响：UI 的档位说明/兼容性红字与引擎判定不一致；一类"本可在 UI 拦下"的配置错误延后到批量运行
  才以整文件失败出现。
- 建议：新增 `param_str_choice` 或直接 `param_str(values,"chroma","420")` 后映射
  420→1/422→2/444→3（映射已存在于 `enc_heif.cpp:642-645` 与
  `page_output.cpp:503-511`，抽成一个纯函数 `chroma_index_of(std::string)` 放 `format_policy.h`
  供两处共用）。单测：`test_smoke` 真值表加"chroma=444 ⇒ compatible(Main10) 判负"。
- 状态：confirmed；known=no

##### X-WIRING-08 [P2] 无损复选框与技术选择不联动（反方向）：tech=modular/lossless + 未勾 ⇒ 产物无损而 spec 记 lossless=false
- 证据：`src/ui/paramform.cpp:1268-1282 user_set_tech` 只改技术、不改 `sel.lossless`；
  `enc_jxl.cpp:438-454`（modular + distance=0 ⇒ `SetFrameDistance(0)`；`jxl/encode.h:1453-1468`
  注明 0.0 = mathematically lossless）；`enc_webp.cpp:152-155`（`tech_id=="lossless"` ⇒ true）。
- 事实：`--format jxl --tech modular`（不带 `--lossless`）→ `pp_verify` exact **OK**（逐位一致），
  而侧车 `"params"` 里 `lossless=false`、`tech=modular`。webp 同理：技术下拉选 Lossless、
  复选框未勾 ⇒ 参数集 `lossless=false` 但 `cfg.lossless=1`。
- 影响：数据安全方向无害（产物无损），但 `lossless` 这个"显式 schema 参数"与产物脱钩 ⇒
  预设/日志/回归断言都可能据此判错（例如判定"这批是有损产物"从而决定是否可再压缩）。
- 建议：`user_set_tech` 在选中 `lossless_capable` 技术且其"默认距离/参数即无损"时**同步勾上**复选框
  （jxl modular + distance==0、webp lossless）；或让 `spec_of()` 用 `lossless_flag(params) ||
  tech_is_lossless_only(tech)` 归一后写显式键。前者改动小、UI 与产物一致。
- 状态：confirmed；known=no

##### X-WIRING-09 [P2] webp `near_lossless` 实质无效（libwebp 侧条件几乎不可满足）
- 证据：`src/codecs/enc_webp.cpp:198` 写入 `cfg.near_lossless`；libwebp 只在
  `use_near_lossless = (config->near_lossless < 100) && !enc->use_palette && !enc->use_predict`
  时应用（`vcpkg/buildtrees/libwebp/src/v1.6.0-…/src/enc/vp8l_enc.c:1572-1573`），而每个
  crunch 配置的 entropy 变换至少命中 palette 或 predict 之一（同文件 1552-1558、339-405）。
- 事实：`--format webp --tech lossless --lossless --param near_lossless=0|50|100` ×
  `method=0|1|2|6`（共 8 组）输出**全部逐字节相同**。
- 影响：「近无损强度」是"设了没反应"的控件；更要紧的是**反向风险不存在**（不会出现
  "勾了无损实际近无损"），所以不必按 P0 处理，但 UI 应如实。
- 建议：把该行标为 `advanced=false + locked=100`（禁用原因写 tooltip）或直接删行；
  若确认某些图像能触发，则至少补 tooltip"仅对无调色板且不走空间预测的图像生效"。
- 状态：confirmed（"未见生效"为该 libwebp 版本 + 该图像/方法矩阵实测）；known=no

##### X-WIRING-10 [P2] metadata_only 下冲突策略被静默改成 Skip，但下拉仍显示用户选择
- 证据：`src/ui/mainwindow_run.cpp:322-323`
  `if (cfg.metadata_only) cfg.conflict = pp::ConflictPolicy::Skip;`；
  而 `src/ui/page_output.cpp:789-805 update_metadata_only_state` 只置灰 tiles_host/
  param_card/color_combo（未触碰 `conflict_combo`），`output_page_build.cpp:241-254` 的
  「同名冲突」行保持可用且显示原值。
- 事实：配置 `metadata_only=true` + 下拉选「覆盖」→ RunConfig 里是 Skip；UI 全程显示「覆盖」。
  另有同类：`run_again` 也把 conflict 改 Skip（`mainwindow_run.cpp:434`）。
- 影响：用户以为冲突时覆盖，实际被跳过（或反之）；仅元数据模式的产物数量与预期不符且无提示。
- 建议：`update_metadata_only_state` 里一并把 conflict 行置灰 + tooltip 写"仅元数据模式恒跳过
  （不会覆盖已有文件）"（R-318"不静默灰"同口径），并让 `conflict()` 在该模式下回读生效值。
- 状态：confirmed；known=no（R-074 只修了策略本身，未修 UI 回显）

##### X-WIRING-11 [P2] HEIF/AVIF 内省 `threads` 被 E3 的 E 无条件覆盖
- 证据：`src/codecs/enc_heif.cpp:436-514` 先按用户 ParamSet 设 `threads`（462-470），
  随后 `:586-591` 用 `req.encode_threads` 再设一次（SVT/libaom 内省表确有 `threads`：
  `vcpkg/buildtrees/libheif/src/v1.23.5-…/plugins/encoder_svt.cc:119,207`、
  `encoder_aom.cc:190,313-318`）；该行在 UI 上是**普通高级行**（`enc_heif.cpp:219-221`
  的 advanced 白名单不含 threads ⇒ `advanced=true` 但可编辑）。
- 事实：用户在高级区把 `threads` 设成 1，调度器仍按 E 覆盖；只有 set 失败才有 note_param。
- 影响：UI 可改、值不生效、无提示（属"参数不生效"类，但只影响性能不影响产物）。
- 建议：两选一 —— ①表单把 `threads` 显示为**只读 + tooltip**"由线程预算（设置·运行）分配"；
  ②仅在用户显式改过时优先用户值（需要 deviate 位）。推荐 ①（与 `encode_threads` 的单一事实源一致）。
- 状态：confirmed；known=no（R-078 只登记了"ΣE 不成立"，未记录这条覆盖）

##### X-WIRING-12 [P2] 输出页路径模板编辑不持久化（重启回旧值），与设置对话框的同一设置两套源
- 证据：`src/ui/mainwindow.cpp:1454-1462 save_session` 只写
  `last_format / last_out_root / last_preset`，**不写** `settings.output_template`；
  启动期 `mainwindow.cpp:1894` 用 `settings.output_template` 覆盖输出页，
  而 `settings_dialog.cpp:1741-1742` 的「分文件夹默认结构」会写这两个字段。
- 事实：改模板 → 退出 → 重开：模板回到上次"设置对话框"里那份（无预设时即默认
  `$format/$dir/$file`）。`page_output.h:78-79` 的 `set_output_template` 是 programmatic（不发信号），
  也没有任何 `settings.output_template = page_output->output_template()` 的回写点。
- 影响：输出目录结构在会话间漂移；用户在同一台机器上重跑同一批文件会得到不同目录树。
- 建议：`save_session` 追加两行
  `d.settings.output_template = d.page_output->output_template().toStdString();
   d.settings.split_by_format = d.page_output->split_by_format();`
  （注意 `apply_pair` 已有模板校验，非法值落盘会被保留默认，安全）。
- 状态：confirmed；known=no

##### X-WIRING-13 [P2] 预设里的 avif 后端仍可能在后续被静默改写（R-066 余量）
- 证据：`src/ui/format_policy.h:85-94` 的预选条件只被 `in_preset_apply` 与
  `backend_user_set` 挡住；`src/ui/page_output.cpp:967-981 set_batch_has_alpha` **无条件清空**
  所有格式的 `backend_user_set`，且 `apply_state_to_form`（`:669`）用 `set_selection`（不发信号）
  落值时不会置位 `backend_user_set`。
- 事实：载入"svt-av1 + 10bit"预设后，只要批内 alpha 状态变化（加一个带 alpha 的文件）就会
  触发 `svt-av1 → libaom` 改写（`page_output.cpp:768-787`），UI 只显示提示行、不询问；
  再保存预设即写回 libaom。
- 影响：预设的后端字段不保真，且产物编码器与预设记录不同（画质/体积会变）。
- 建议：`apply_state_to_form` 在 `have_spec` 时置 `st->backend_user_set = true`（预设值 = 权威，
  与 R-066 立的口径一致）；或把 `in_preset_apply` 改成"一次预设生效期内"的粘性标志。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md` R-066）

##### X-WIRING-14 [P3] 编码器 fallback 默认与 schema 默认不一致（4 处）
- 证据/事实：
  ①`enc_jxl.cpp:448` `param_float(params,"distance",1.0)` vs modular 技术默认 **0.0**
   （`format_tables.cpp:175`）—— 若参数集缺 `distance`，modular 会变成有损；
  ②`enc_heif.cpp:502` `param_int(…,"quality",90)` vs 内省默认 **50**（`encoder_x265.cc:230`）；
  ③`enc_jxl.cpp:393` Bool fallback `false` vs 表默认 `true`（dots/patches/gaborish/keep_invisible，
   `format_tables.cpp:442-468`）—— 缺键时会把"编码器自选"变成"关闭"；
  ④`enc_jxl.cpp:398` Int fallback `0` vs 表 `effort` 默认 7/ `epf` 2。
- 影响：正常 GUI/`--dev` 路径都由 `default_params`/表单补全，故当前不可见；但任何直接构造
  `EncodeRequest` 的调用方（bench、单测、未来 API）会静默拿到与 schema 不同的默认值。
- 建议：fallback 不要写字面量，改为"查 `lookup_param_def(...)->def`"；或用 sentinel 表示"缺键 =
  库默认"（与 `-1` 三态口径统一）。
- 状态：confirmed（代码对照）；known=no

##### X-WIRING-15 [P3] GUI 不经 `build_output_specs`，装配链少一次 `validate_params`
- 证据：`pp::build_output_specs`（`pipeline.cpp:1739-1767`，含
  `default_params → base_params → fill_defaults → apply_locks → validate_params`）只被
  `dev_cli.cpp:802`、`dev_bench.cpp:216`、`tests/unit/test_scheduler_contract.cpp:1020` 调用；
  GUI 走 `page_output.cpp:706-737 spec_of`（表单工作集直接出 spec），运行前只做
  `cross_validate`（`mainwindow_run.cpp:341-358`）与位深归属（`pipeline.cpp:433-440`）。
- 事实：`docs/deep-review-0.3.2.md:938` 的收敛路线 ③ 把"config_base 也走 build_output_specs"
  列为待办；当前 GUI 与 CLI 的参数装配仍是两条实现。
- 影响：范围/枚举类越界值经手改预设（`params_from_json` 不做范围校验，`preset_io.cpp:398-416`）
  可进入 GUI 参数集并直达编码器；`validate_preset` 会在**载入**时挡一次，但会话内直接改 JSON 后
  重载才挡。风险低（编码器大多自带校验），但"两条装配链"本身是缺陷面。
- 建议：`spec_of()` 末尾改用 `pp::build_output_specs`（把 base_params 传成表单工作集），
  一次消灭双链；顺带让位深默认也走 `default_bitdepth_for` 单源。
- 状态：confirmed；known=yes（deep-review R-145 收敛路线 ③）

##### X-WIRING-16 [P3] 侧车/摘要里的 `tech` 在 `--dev` 单格式路径为空串，与 GUI 口径不同
- 证据：`build_output_specs`（`pipeline.cpp:1764`）原样透传 `r.tech_id`（`--dev` 不传即空），
  而 GUI 的 `spec_of` 写的是表单解析后的具体技术（`page_output.cpp:726`）。
- 事实：`--format jxl` 的侧车 `"tech": ""`；`--format jxl --tech vardct` 才是 `"vardct"`。
- 影响：日志/回归基线的 `tech` 字段语义不确定（空=未指定 vs 具体技术），跨工具比对时容易误判。
- 建议：`build_output_specs` 里对空 tech 做一次 `select_tech(...)->id` 归一（引擎侧已有该函数），
  或侧车统一输出 `tech_effective`。
- 状态：confirmed；known=no

##### X-WIRING-17 [P3] HEIF/AVIF 在 CLI 路径完全没有参数（`params=[]`），插件默认值无落盘记录
- 证据：`default_params` 对静态 `techs` 为空的格式只写内部键（`params.cpp:463-477`），
  `fill_defaults`（`:614-630`）无技术可遍历 ⇒ ParamSet 仅含 `__lossless`；`snapshot_params`
  （`:632-644`）过滤 `__` 键 ⇒ 快照为空。
- 事实：`--format heif` 的摘要 `params=[]`、侧车 `"params": ""`；实际生效的是插件默认
  （quality 50、preset slow、chroma 420 —— `encoder_x265.cc:228-298`），**没有任何一处记录**。
- 影响：出问题时无法从日志复现"当时用的什么质量"；与 GUI（全量内省键）口径不一致。
- 建议：`build_output_specs` 对 `runtime_introspected` 格式用 `introspect_backends()` 的结果补默认
  值（`compute_declared_keys` 已有同款内省调用与进程内缓存，可复用）。
- 状态：confirmed；known=no

##### X-WIRING-18 [P3] 色彩四选项的通路里有两个无效字段，且「线性光重采样」放在输出页却只影响预览
- 证据：`pipeline.cpp:907-911` 把四项写入 `ColorOptions`；`colormanager.cpp:842-844` 的消费分工注释
  自述 `dither8` 与 `linear_resample` **不在本层消费**（前者在编码器量化段，后者在显示面）；
  `thumbs.cpp:158-209` 才是 `linear_resample` 的真实消费者，入口是
  `mainwindow.cpp:1247 preview_panel->set_linear_resample(page_output->linear_resample())`。
- 事实：`linear_resample` 开关位于输出页全局卡（`output_page_build.cpp:347-359`），tooltip 已如实写
  "仅显示面…输出像素不受影响"；`RunConfig.linear_resample → ColorOptions.linear_resample` 无人读。
- 影响：无数据后果（tooltip 已披露），但两个死字段会误导后续接线者（改 `ColorOptions` 期待改变
  产物）；且"输出页 = 产物设置"的心智模型被打破。
- 建议：`ColorOptions` 删除这两个无消费字段（或加 `[[deprecated]]` 注释指向真实入口）；
  `linear_switch` 移到设置页（显示偏好）或标注"预览"前缀。
- 状态：confirmed；known=no

##### X-WIRING-19 [P3] `--param` 未知键在配置期不报错，而是逐文件失败
- 证据：`dev_cli.cpp:387-393` 未知键写裸字符串并只 `log_warn`；真正判负在
  `params.cpp:599-611`（cross_validate 规则③），而 `cross_validate` 只在
  `pipeline.cpp:1674-1679 validate_run_config`（每文件 stage_validate）与
  `mainwindow_run.cpp:344-348`（GUI 预检）被调用。
- 事实：`--format png --param foo=bar` → 每个文件 `error: 未知参数：foo`、退出码 1（不是启动期报错）。
- 影响：批量 1000 个文件会刷 1000 条同样的配置错误；`--dev` 的"参数错误应在开跑前失败"口径不成立。
- 建议：`set_param` 未知键直接返回错误（与 `--format` 未知同处置），或开跑前对 `reqs` 各 spec 跑一次
  `cross_validate`。
- 状态：confirmed；known=no

#### 4. 输出兼容性与合规矩阵

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| png → webp，`--tech lossless --lossless` | RIFF/VP8L（+VP8X/EXIF） | 是（VP8L 为标准无损块） | 无 |
| png → webp，`--tech lossy --lossless`（X-WIRING-01） | RIFF/**VP8 **（有损）+VP8X/**EXIF** | 是 | **口径不合规**：侧车 `lossless=true` 与码流矛盾；归档流程会误判为无损母版 |
| png → webp，`--tech lossy`（有损，带 EXIF 源） | 同上 + EXIF chunk（64×64 样本上 90 B） | 是 | 侧车与码流一致（此行为正确对照组） |
| png → jxl，`--tech vardct --lossless` | BMFF 容器（`JxlEncoderUseContainer(TRUE)`，enc_jxl.cpp:277） | 是（libjxl 容器合法） | 侧车 `tech=vardct` 与实际 codestream（modular）不符（X-WIRING-08 邻域） |
| png → jxl，`--tech modular`（未勾无损） | BMFF，modular codestream，distance=0 | 是 | 产物**逐位无损**但 `lossless=false`（X-WIRING-08） |
| png → heif，`--lossless` | HEIF/HEVC，`chroma` 强制 444，bitdepth 10 默认 | 是（libde265/OIIO 可开；>16MP 自动 grid，enc_heif.cpp:875-882） | 4:4:4 的查看器兼容性弱于 4:2:0（UI tooltip 已披露）；RGB↔YCbCr 往返有舍入 ⇒ **非逐位无损** |
| png → heif，`--param lossless=true`（X-WIRING-02） | HEIF/HEVC **有损**（chroma 420） | 是 | **口径不合规**：侧车写 lossless=true |
| png → tiff，`compression=zip,predictor=2` | TIFF strip + Deflate + 水平预测 | 是 | 无（`tiff:Predictor` 在 OIIO 自动规则之后生效，R-011 已复核） |
| png → tiff，`predictor=3` | **不产出**（前置拒绝） | — | 无（正确拒绝，非静默降级） |
| png → bmp，bitdepth 8/24 | BMP（无参数） | 是 | `out_bitdepth` 单位是 **bpp**（R-211），与其它格式 bps 同名不同义 |
| png → png（仅元数据） | 同容器重写，像素不动 | 是 | GUI 强制 conflict=Skip（X-WIRING-10），不会覆盖源/已有件 |
| 任何 → 任何（仅元数据，源=jxl/heif/bmp） | 该文件失败（`format_supports_metadata_only` 白名单） | — | 无（显式失败） |

#### 5. 未覆盖与存疑

1. **GUI 交互未实测**：本审计只读代码 + 用 `--dev`/`pp_verify` 验证引擎侧。GUI 相关结论
   （`current_chroma` 恒 1、档位下拉联动、conflict 行未置灰、预设丢档位）均为**代码路径推导**
   + 单元级真值表核对，未跑 `--ui-smoke`（任务禁止）。`smoke_probes.cpp:5932+` 已有档位行走查，
   但其断言不覆盖"chroma 类型"与"预设往返"。
2. **`dct_method` 的实际效力**：表与代码都声称"jpegli 不读该字段、实际恒为浮点 DCT"
   （`format_tables.cpp:352-364`、`enc_jpegli.cpp:286`），但代码确实写了
   `cinfo.dct_method = JDCT_ISLOW`（覆盖了 `jpegli_set_defaults` 的 `JDCT_FLOAT`）。我没有做
   "改为 JDCT_FLOAT 后字节对比"的实测（会改动代码），故该键的"生效=否"记为**代码/文档一致口径**，
   底层库是否真忽略未独立证实（suspected）。
3. **`near_lossless` 的"无效"是实测矩阵结论**（1 张纹理图 × method 0/1/2/6 × 0/50/100），
   不是"永远无效"的证明；根因（`!use_palette && !use_predict`）已从库源确认，但存在某些图像
   恰好选到 kSubGreen 而生效的可能，故建议按"如实标注"而非删除处理。
4. **`.heic` 侧车/回归基线未覆盖 HEIF 的 `params` 语义**：CLI 路径 `params=[]`（X-WIRING-17），
   因此"HEIF 参数是否下发"在既有金样里无法被发现——建议补一条 dev harness 断言。
5. **`encode_threads` 的 ΣE ≤ T 不变式**未实测（需并发跑 + 线程计数）；本审计只核对了
   "E 是否被映射"（逐编码器）与"映射是否如契约"（`encoder.h:13`），没有验证 E 的分配值本身。
6. **libheif 内省在无插件环境下的行为**（`introspect_live` 返回空）未测；代码路径是
   `param_def_from` 不会被调用 → GUI 参数卡只剩档位/位深两行。属环境依赖，未列入发现。
7. 未覆盖：`bench`（`dev_bench.cpp`）路径、`--outputs` 多格式 + 逐输出不同 lossless（CLI 无法表达，
   属已知限制）、AVIF 三后端（svt/libaom）的参数逐键实测（只做了代码对照）。

### 6.3 库可配置面 vs UI 暴露（全参数暴露）

> 范围：把 6 个静态格式（jpeg / jxl / png / tiff / webp / bmp）与 3 个 libheif 插件后端（x265 / svt-av1 / libaom）
> 的**库面**逐条枚举，与 `src/core/format_tables.cpp`（schema 唯一事实源）+ `src/ui/paramform.cpp`（渲染面）
> 做差集，回答「全参数暴露」（`docs/brainstorm-consensus.md:23`）是否成立。
>
> 证据口径：**库面**取 `vcpkg_installed/x64-windows-avx2/include/**` 头文件与 `vcpkg/buildtrees/<port>/**` 源码；
> **读取点**取 `src/codecs/enc_*.cpp`；**实测**取 0.3.5.alpha2 二进制（`build/release-dev/photopipeline.exe`，
> 经 `python tools/env.py run --`），并用预设 JSON 注入**类型化**参数以复刻 GUI 的参数集
> （`--dev --param` 会把 heif/avif 的键退化成字符串，见 §3 X-LIBRARY-11，不能代表 GUI）。
> 只读审计：本文件之外未改动任何仓库文件；探针产物在 `build/encoder-audit/probe/`。
>
> 版本事实：libjxl 0.12.0（`jxl/version.h`）、libheif 1.23.5（`libheif/heif_version.h`）、
> OpenImageIO 3.1.17（`OpenImageIO/oiioversion.h:45-47`）、libwebp 1.6.0、jpegli = JPEG_LIB_VERSION 62
> （`jpegli/jconfig.h`，`C_ARITH_CODING_SUPPORTED` 未定义）、libtiff 有 ZIP+JPEG 无 LZMA/ZSTD/WebP
> （`tiffconf.h:75,94,106`）。

---

#### 1. 结论（3-8 条，直说有没有问题）

1. **「全参数暴露」不成立**，但缺口分层清晰：schema 内的键 **100% 有 UI 行**（静态表 82 条声明行（jxl/webp 双技术镜像计数）+ 内省 32 键）；
   缺口全部落在 **schema ↔ 库** 边界：`png` 2 条、`tiff` 4 条、`webp` 4 条、`jxl` 8 个帧设置 + 2 个编码器级 API、
   `jpegli` 5 个 API。**没有"表里有行但 UI 渲染不出"的键**（渲染面本身是闭合的）。
2. **P1 实测（AVIF 默认后端 SVT-AV1）**：libheif 内省把**无默认值**的 `qp` 兜底成 `0`，而 ParamForm 会把内省默认
   全部写进工作集并逐次下发 ⇒ SVT 的 CQP 被 `qp=0` 钉死，**质量参数完全失效**。实测：`qp=0 & quality=20` 与
   `qp=0 & quality=90` 产物 **SHA256 相同**（`AF462C70…`，1029 B）；去掉 `qp` 后 quality=20 → CQP 50.50/580 B、
   quality=90 → CQP 6.25/1769 B。默认配置即按 QP0（近无损）编码，照片体积会显著膨胀。
3. **P1 实测（AVIF + SVT-AV1 + 无损 = 必然失败）**：SVT 插件的 `chroma` 注册被 `#if 0` 掉（`encoder_svt.cc:264-273`），
   而 `enc_heif` 的无损硬要求 `chroma` 可用 ⇒ 勾选「无损」后**每个文件失败**（实测 0 B + 英文错误）；
   同一格式换 libaom 后端则正常（479 B）。`enc_heif` 还把 SVT 的 `supports_lossless_compression=true` 当能力位
   ⇒ UI 无损复选框照常显示、档位行还提供 High/Professional（4:4:4 档），两条都是陷阱。
4. **死旋钮 4 个（实测证实）**：WebP `preset`（6 档产物逐字节相同）、JXL `brotli_effort`（本管线 box 不压缩、
   无 JPEG 重压缩）、TIFF `compression=ccittrle`（Bilevel-only，8/16 位样本必失败）、HEIF/AVIF 的 `svt:` 透传
   （插件无该前缀，必失败但 `params.cpp` 白名单放行）。另有 2 个已知必失败/无效档：TIFF `predictor=float`（R-011）、
   HEIF/AVIF 内省项 `threads`（被 `encode_threads` 后写覆盖，UI 值无效）。
5. **缺口性质判定**：只有两处属**有意裁剪**且有出处 —— ① HEIF/AVIF 插件私有旋钮（`docs/design.md:489`
   「已接受-库面」）；② 若干 UI 范围收窄（`format_tables.cpp` 的 R-294 注记与 tooltip）。其余全部是**遗漏**；
   而 `docs/design.md:490` 那条「OIIO 即库面，PNG 1 / TIFF 5 条真实参数（已穷尽）」的裁定**前提不成立**
   （PNG 至少还有 2 个输出属性、TIFF 还有 4 个），因此它保护不了这两个格式的缺口。
6. **默认值不一致 6 处**：JXL `codestream_level` 默认 10（库 -1/auto，且会强制容器）；JXL 7 个三态参数被 Bool 化后
   默认即显式下发（库默认 -1 = 编码器自选）；TIFF `compression` 默认 lzw（OIIO 默认 zip）；WebP `quality` 90（库 75）、
   `filter_strength` 30（库 60）、`sharp_yuv`/`exact` true（库 0）。后四项 tooltip 已记账，前两项没有。
7. **落盘与回归面**：HEIF/AVIF 的「编码档位」用保留键 `__heif_profile`，被 `preset_io.cpp` 的 `__` 前缀规则**丢弃**
   ⇒ 档位不进预设；`--dev` 对 libheif 数值参数**不可用**（静态表查不到 ⇒ 存成 string ⇒ `enc_heif` 报
   `expected integer`）⇒ 金样 20 对里 HEIF/AVIF 的参数面覆盖 ≈ 0（只有 `avif-lossy`/`heif-lossy` 无参数跑通）。

---

#### 2. 参数全量矩阵

> 「UI 暴露」列：**核心** / **高级**（折叠区）/ **锁定**（渲染但恒禁用，数据驱动 locked 谓词）/ **否**（表内无键）。
> 「读取点」= 编码器读该键的位置（file:line）。「生效」= 是 / 条件 / 否 / 未知。

##### 2.1 JPEG — jpegli / dct（15 键，`src/core/format_tables.cpp:279-415`）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality_mode | 核心 | distance | distance / quality | `src/codecs/enc_jpegli.cpp:87` | 是 | 二选一：`enc_jpegli.cpp:272-276` 只走一条 setter |
| distance | 核心 | 1.0 | 0.3–3.0（库 0–25） | `src/codecs/enc_jpegli.cpp:88` | 是 | `psnr_target>0` 时被静默覆盖（`:277-280`，R-247） |
| quality | 核心 | 90 | 1–100 | `src/codecs/enc_jpegli.cpp:89` | 是 | 同上被 psnr_target 覆盖 |
| chroma | 核心 | 444 | 444/440/422/420 | `src/codecs/enc_jpegli.cpp:90` | 是 | **实测四档 SOF 采样因子均正确**：444=(1,1)、440=(1,2)、422=(2,1)、420=(2,2)（Y 分量） |
| progressive | 核心 | true | bool | `src/codecs/enc_jpegli.cpp:91` | 是 | `:281` level=2/0；为 true 时锁 optimize_coding |
| optimize_coding | 高级 | true | bool | `src/codecs/enc_jpegli.cpp:92` | 条件 | `:284` 被 `progressive||optimize` 强制 TRUE（locked 谓词 `lock_true_when_progressive`） |
| arith_code | 锁定 | false | 恒 false | `src/codecs/enc_jpegli.cpp:93/197` | 否 | R-244：jpegli 硬拒（`lib/jpegli/encode.cc:263-265`），`jconfig.h` 无 C_ARITH_CODING_SUPPORTED |
| restart_in_rows | 高级 | 0 | 0–64（库 65535 行） | `src/codecs/enc_jpegli.cpp:94` | 是 | jpegli 换算 `min(MCUs_per_row×n,65535)`（`lib/jpegli/encode.cc:376-380`） |
| dct_method | 锁定 | 0 | ISLOW/IFAST/FLOAT | `src/codecs/enc_jpegli.cpp:95` | 否 | R-246：jpegli 固定浮点 DCT，字段仅兼容层 |
| smoothing_factor | 高级 | 0 | 0–100 | `src/codecs/enc_jpegli.cpp:96` | 是 | `lib/jpegli/encode.cc:442` 真实消费 |
| xyb_mode | 锁定 | false | 恒 false | `src/codecs/enc_jpegli.cpp:97` | 否 | R-245：无 XYB ICC 路径（库 API 存在：`jpegli/encode.h:132`） |
| adaptive_quantization | 高级 | true | bool | `src/codecs/enc_jpegli.cpp:98` | 是 | `jpegli/encode.h:146`；注意 440/422/420 下 luma 被抽样会**自动关掉 AQ**（`lib/jpegli/encode.cc:327-333`） |
| std_quant_tables | 高级 | false | bool | `src/codecs/enc_jpegli.cpp:99` | 是 | `jpegli/encode.h:157`；只能二选一（Annex K vs jpegli 自有），自定义表不可达 |
| psnr_target | 高级 | 0 | 0–100（库无上限） | `src/codecs/enc_jpegli.cpp:100` | 是 | `:277-280` 距离搜索；>0 时**覆盖** quality_mode/distance/quality |
| cicp_transfer_function | 高级 | 2 | 0–18 | `src/codecs/enc_jpegli.cpp:101` | 条件 | 仅 16=PQ / 18=HLG 改变默认量化表（`lib/jpegli/quant.cc`），其余码值被接受但无效果 |

##### 2.2 JPEG XL — libjxl / vardct|modular（34 条声明行 / 27 键 + 2 个编码器级项，`format_tables.cpp:416-601`）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| effort | 核心 | 7 | 1–10（库 1–11，11 需 expert） | `src/codecs/enc_jxl.cpp:77` | 是 | `:402` SetOption；11 不可达（见 X-LIBRARY-10） |
| decoding_speed | 高级 | 0 | 0–4 | `src/codecs/enc_jxl.cpp:78` | 是 | `:402` |
| resampling | 高级 | auto(-1) | auto/1/2/4/8 | `src/codecs/enc_jxl.cpp:79` | 是 | `:399-401` 负值跳过=库默认；>1 会**降有效分辨率** |
| group_order | 高级 | auto(-1) | auto/scanline/center | `src/codecs/enc_jxl.cpp:80` | 是 | 中心坐标不可设（CENTER_X/Y 未暴露） |
| photon_noise | 高级 | 0.0 | 0–6400（库无上限） | `src/codecs/enc_jxl.cpp:81` | 是 | `:386-389` 负值跳过 |
| epf | 高级 | 2 | -1–3 | `src/codecs/enc_jxl.cpp:82` | 是 | 表默认 2 ≠ 库默认 -1（catalog 明示） |
| keep_invisible | 高级 | true | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:83` | 是 | Bool 无法表达 -1；有损时库默认 0、本表发 1（体积↑） |
| dots | 高级 | true | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:84` | 是 | 同上 |
| patches | 高级 | true | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:85` | 是 | 同上 |
| gaborish | 高级 | true | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:86` | 是 | 同上 |
| progressive_ac | 高级 | false | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:87` | 是 | 默认发 0 = 强制关闭（库默认 -1 可能自选开启） |
| qprogressive_ac | 高级 | false | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:88` | 是 | 同上 |
| progressive_dc | 高级 | auto(-1) | -1–2 | `src/codecs/enc_jxl.cpp:89` | 是 | 三态正确（Int + 负值跳过） |
| modular_group_size | 高级 | auto(-1) | -1–3 | `src/codecs/enc_jxl.cpp:90` | 是 | 仅 modular 生效（`:380`） |
| modular_predictor | 高级 | auto(-1) | -1–15 | `src/codecs/enc_jxl.cpp:91` | 是 | 仅 modular |
| modular_palette_colors | 高级 | auto(-1) | -1–4096（库无上限） | `src/codecs/enc_jxl.cpp:92` | 是 | 仅 modular |
| modular_lossy_palette | 高级 | false | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:93` | 是 | 无损时 locked false |
| brotli_effort | 高级 | auto(-1) | -1–11 | `src/codecs/enc_jxl.cpp:94` | **否** | **假桩**：只服务 JPEG 重压缩/brob；本编码器 box 以 `JXL_FALSE` 写入（`:526-540`）→ 零效果 |
| responsive | 高级 | false | bool（库 -1/0/1） | `src/codecs/enc_jxl.cpp:95` | 是 | 仅 modular |
| channel_colors_global_percent | 高级 | auto(-1) | -1–100 | `src/codecs/enc_jxl.cpp:96` | 是 | 负值跳过（libjxl 拒绝显式 -1） |
| channel_colors_group_percent | 高级 | auto(-1) | -1–100 | `src/codecs/enc_jxl.cpp:98` | 是 | 同上 |
| modular_ma_tree_learning_percent | 高级 | auto(-1) | -1–200（库允许 >100） | `src/codecs/enc_jxl.cpp:100` | 是 | 仅 modular |
| modular_nb_prev_channels | 高级 | auto(-1) | -1–11 | `src/codecs/enc_jxl.cpp:102` | 是 | 仅 modular |
| distance | 核心 | vardct 1.0 / modular 0.0 | 0–25 / 0–15 | `src/codecs/enc_jxl.cpp:448` | 是 | 无损 → 固定 0.0 + SetFrameLossless（`:438-446`）；modular 上限 15 < 库 25 |
| codestream_level | 高级 | 10 | auto(-1)/5/10 | `src/codecs/enc_jxl.cpp:348` | 是 | **无条件下发 10**（库默认 -1）；level 10 会自动启用容器并放宽限制 |
| color_transform | 高级 | YCoCg | None/YCoCg/XYB/YCbCr | `src/codecs/enc_jxl.cpp:415` | 是 | 仅 modular（`:414`）；YCoCg 走 MODULAR_COLOR_SPACE=6，其余走 COLOR_TRANSFORM |
| lossless（显式） | 核心复选框 | false | bool | `src/codecs/enc_jxl.cpp:212` | 是 | 与内部 `__lossless` 由 `apply_locks` 同步（`src/core/params.cpp:479-519`） |
| （库面缺）JxlEncoderSetExtraChannelDistance | 否 | = frame distance | 0–25 | — | 否 | 有损 JXL 的 alpha 只能与颜色同距离（`jxl/encode.h:1470-1485`） |
| （库面缺）JxlEncoderAllowExpertOptions | 否 | — | — | — | 否 | 打开后可 effort=11（`jxl/encode.h:1562-1570`） |

##### 2.3 PNG — OIIO / deflate（1 键，`format_tables.cpp:602-620`）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| compressionLevel | 核心 | 6 | 0–9（OIIO 钳制） | `src/codecs/enc_oiio.cpp:241` | 是 | 写 `png:compressionLevel`（`pngoutput.cpp:164-169`）；R-010 已修键名 |
| （库面缺）`compression` 策略 | 否 | 空 = Z_DEFAULT_STRATEGY | default/filtered/huffman/rle/fixed/pngfast/none | — | 否 | `pngoutput.cpp:170-191`；照片常用 Z_FILTERED 不可选 |
| （库面缺）`png:filter` | 否 | PNG_NO_FILTERS | 0–0x0F（libpng filter 掩码） | — | 否 | `pngoutput.cpp:199-200` |

##### 2.4 TIFF — OIIO / codec（5 键，`format_tables.cpp:621-688`）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| compression | 核心 | lzw | none/lzw/zip/ccittrle/packbits | `src/codecs/enc_oiio.cpp:250` | 条件 | 实测 none/lzw/zip/packbits **成功**，**ccittrle 必失败**（Bilevel-only，"Bits/sample must be 1"） |
| deflate_level | 高级 | 6 | 1–9 | `src/codecs/enc_oiio.cpp:256` | 条件 | 仅 `compression=zip` 写 `tiff:zipquality`（`tiffoutput.cpp:702-711`） |
| predictor | 高级 | horizontal(2) | none/horizontal/float | `src/codecs/enc_oiio.cpp:260` | 条件 | 仅 lzw/zip；float(3) 对整型样本**必报错**（`:273-277`，R-011） |
| tiff_tile_width | 高级 | 0(strip) | 0–4096，须 16 倍数 | `src/codecs/enc_oiio.cpp:280` | 是 | 写 `spec.tile_width`；与 height 同 0 或同 >0 |
| tiff_tile_height | 高级 | 0(strip) | 0–4096，须 16 倍数 | `src/codecs/enc_oiio.cpp:281` | 是 | 同上 |
| （库面缺）`tiff:bigtiff` | 否 | 自动（>4000MB 切） | 0/1 | — | 否 | `tiffoutput.cpp:510-513`；只能在 4GB 边界被动切换 |
| （库面缺）`tiff:RowsPerStrip` | 否 | libtiff 默认 | int | — | 否 | `tiffoutput.cpp:1038`（属性路径）/`:719`（自动写回） |
| （库面缺）planarconfig / `tiff:planarconfig` | 否 | contig | contig/separate | — | 否 | `tiffoutput.cpp:849-850` |
| （库面缺）`tiff:multithread` | 否 | 1 | 0/1 | — | 否 | `tiffoutput.cpp:1509`；写盘并行不可关 |

##### 2.5 WebP — libwebp / lossy|lossless（27 条声明行 / 24 键，`format_tables.cpp:689-861`）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality（lossy） | 核心 | 90 | 1–100（库 0–100，默认 75） | `src/codecs/enc_webp.cpp:169` | 是 | `WebPValidateConfig` 0–100 |
| quality（lossless） | 核心 | 80 | 0–100（=压缩努力） | `src/codecs/enc_webp.cpp:195` | 是 | 一键两义（R-209） |
| method | 高级 | 4 | 0–6 | `src/codecs/enc_webp.cpp:174/196` | 是 | |
| preset | 高级 | photo(2) | default/picture/photo/drawing/icon/text | `src/codecs/enc_webp.cpp:165-172` | **否** | **假桩**：预设只改 sns/filter_strength/filter_sharpness/preprocessing/segments（`config_enc.c:64-96`），这 5 个随后全被 `:175-181` 覆盖 ⇒ 实测 6 档产物逐字节相同 |
| sharp_yuv | 核心 | true | bool（库默认 0） | `src/codecs/enc_webp.cpp:192` | 是 | 仅 lossy |
| sns_strength | 高级 | 50 | 0–100 | `src/codecs/enc_webp.cpp:175` | 是 | |
| filter_strength | 高级 | 30 | 0–100（库默认 60） | `src/codecs/enc_webp.cpp:176` | 是 | 0 时 filter_type/filter_sharpness 无意义 |
| autofilter | 高级 | false | bool | `src/codecs/enc_webp.cpp:179` | 是 | |
| pass | 高级 | 1 | 1–10 | `src/codecs/enc_webp.cpp:180` | 是 | |
| filter_sharpness | 高级 | 0 | 0–7 | `src/codecs/enc_webp.cpp:177` | 是 | |
| filter_type | 高级 | true(strong) | bool | `src/codecs/enc_webp.cpp:178` | 是 | |
| segments | 高级 | 4 | 1–4 | `src/codecs/enc_webp.cpp:181` | 是 | |
| alpha_compression | 高级 | true | bool | `src/codecs/enc_webp.cpp:182` | 是 | 仅 lossy 的 alpha 平面 |
| alpha_filtering | 高级 | 1 | 0–2 | `src/codecs/enc_webp.cpp:183` | 是 | |
| alpha_quality | 高级 | 100 | 0–100 | `src/codecs/enc_webp.cpp:184` | 是 | |
| partitions | 高级 | 0 | 0–3 | `src/codecs/enc_webp.cpp:185` | 是 | |
| partition_limit | 高级 | 0 | 0–100 | `src/codecs/enc_webp.cpp:186` | 是 | |
| preprocessing | 高级 | 0 | 0–7 | `src/codecs/enc_webp.cpp:187` | 是 | **注意**：覆盖掉 preset=photo 的 `|=2`（抖动） |
| qmin / qmax | 高级 | 0 / 100 | 0–100 | `src/codecs/enc_webp.cpp:188-189` | 是 | 交叉规则 qmin≤qmax（`src/core/params.cpp:264-275`） |
| emulate_jpeg_size | 高级 | false | bool | `src/codecs/enc_webp.cpp:190` | 是 | |
| low_memory | 高级 | false | bool | `src/codecs/enc_webp.cpp:191` | 是 | |
| exact | 高级 | true | bool（库默认 0） | `src/codecs/enc_webp.cpp:197` | 条件 | 仅 lossless |
| near_lossless | 高级 | 100 | 0–100 | `src/codecs/enc_webp.cpp:198` | 条件 | 仅 lossless；<100 = 非严格无损（tooltip 已警告） |
| （库面缺）image_hint | 否 | DEFAULT | DEFAULT/PICTURE/PHOTO/GRAPH | — | 否 | `webp/encode.h:88-94,106`（lossless-only hint） |
| （库面缺）target_size | 否 | 0 | ≥0 字节 | — | 否 | `webp/encode.h:108-109`；优先于 quality |
| （库面缺）target_PSNR | 否 | 0 | ≥0 dB | — | 否 | `webp/encode.h:110-111`；优先于 target_size |
| （库面缺）use_delta_palette | 否 | 0 | 0/1（头文件标 reserved） | — | 否 | `webp/encode.h:150` |

##### 2.6 BMP — OIIO / raw（0 键）与 HEIF/AVIF 通用项

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| （bmp）无参数 | — | — | — | `src/codecs/enc_oiio.cpp:297` | — | OIIO BMP writer 无输出属性；位深 8/24（bpp 口径） |
| `__heif_profile`（档位） | 核心（独立行） | Auto(0) | Auto/Main/Main10/…/High/Pro | `src/codecs/enc_heif.cpp:648-654` | 是 | 不进预设（`preset_io.cpp:388-396` 丢 `__` 前缀） |
| `__lossless` / `lossless` | 复选框 | false | bool | `src/codecs/enc_heif.cpp:392/543` | 是 | 强制 4:4:4（`:554-567`） |

##### 2.7 HEIF — libheif + x265（内省 7 键，实测 `pp_linkprobe` 列表）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality | 核心 | 50（插件默认） | 0–100 | `src/codecs/enc_heif.cpp:453/501-504` | 是 | 映射 crf=(100-q)/2（`encoder_x265.cc:992-998`） |
| lossless | 核心（复选框） | false | bool | `src/codecs/enc_heif.cpp:482/505-508/543` | 是 | 与 `__lossless` 双键读取 |
| preset | 核心 | slow | ultrafast…placebo | `src/codecs/enc_heif.cpp:470` | 是 | 插件默认 "slow"（`encoder_x265.cc:251`） |
| tune | 核心 | ssim | psnr/ssim/grain/fastdecode | `src/codecs/enc_heif.cpp:470` | 是 | |
| tu-intra-depth | 高级 | 2 | 1–4 | `src/codecs/enc_heif.cpp:453` | 是 | |
| complexity | 高级 | 0（插件无默认） | 0–100 | `src/codecs/enc_heif.cpp:453` | 条件 | 仅 ≥60/70/90 有动作（`encoder_x265.cc:1006-1021`），默认 0 = 无害 |
| chroma | 核心 | 420 | 420/422/444 | `src/codecs/enc_heif.cpp:470` | 是 | 无损强制 444；平面布局同步（`:610-627`） |
| （库面缺）`x265:*` 私有选项 | 否 | x265 默认 | 200+ 键（ctu/psy-rd/…） | `src/codecs/enc_heif.cpp:525-533` | 条件 | 仅预设/CLI 可达；GUI 无入口（design.md:489 有意裁剪） |

##### 2.8 AVIF — libheif + SVT-AV1 / libaom（内省 10 / 15 键）

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| quality（svt） | 核心 | 50（插件默认） | 0–100 | `src/codecs/enc_heif.cpp:453/501-504` | **条件** | **被 `qp` 覆盖**（实测 quality=20/90 产物相同，见 X-LIBRARY-01） |
| qp（svt） | 高级 | **0**（内省兜底=min） | 0–63，插件无默认 | `src/codecs/enc_heif.cpp:453` | 是 | 一旦在场即钉死 CQP；GUI 每次下发 ⇒ 质量失效 |
| speed（svt） | 核心 | 12 | 0–13 | `src/codecs/enc_heif.cpp:453` | 是 | |
| threads（svt） | 高级 | 4 | 1–16 | `src/codecs/enc_heif.cpp:588` | 条件 | **恒被覆盖**：`:586-591` 在参数循环之后用 `encode_threads` 再写一次 ⇒ UI 值无效（X-LIBRARY-20） |
| tile-rows / tile-cols（svt） | 高级 | 4 | {1,2,4,8,16,32,64} | `src/codecs/enc_heif.cpp:453` | 是 | 整数集 → 内省成 Enum |
| min-q / max-q（svt） | 高级 | 0 / 63 | 0–63 | `src/codecs/enc_heif.cpp:453` | 是 | 与默认一致，显式下发无害 |
| tune（svt） | 核心 | psnr | vq/psnr/ssim/iq/ms-ssim | `src/codecs/enc_heif.cpp:470` | 是 | |
| lossless（svt） | 核心（复选框） | false | bool | `src/codecs/enc_heif.cpp:543` | **否** | **必失败**：插件无 chroma（见 X-LIBRARY-02） |
| chroma（svt） | **否**（内省无此键） | 插件写死 420 | — | `src/codecs/enc_heif.cpp:613` | 否 | `encoder_svt.cc:264-273` 注册被 `#if 0` |
| quality / lossless / chroma / tune（aom） | 核心 | 50/false/420/auto | 插件值集 | `src/codecs/enc_heif.cpp:453-482` | 是 | aom 有 chroma ⇒ 4:4:4/无损可用（实测无损 479 B OK） |
| realtime / speed / threads（aom） | realtime 高级、speed 核心、threads 高级 | false/6/自动 | 0–9（或 0–8）/1–64 | `src/codecs/enc_heif.cpp:453` | 是 | speed 上限随 aom 版本（`encoder_aom.cc:301-307`） |
| min-q / max-q（aom） | 高级 | 0 / 63 | 0–63 | `src/codecs/enc_heif.cpp:453` | 是 | |
| alpha-quality / alpha-min-q / alpha-max-q（aom） | 高级 | **0 / 0 / 0**（内省兜底=min） | 0–100 / 0–63 / 0–63 | `src/codecs/enc_heif.cpp:453` | 条件 | 三者一旦在场，alpha 平面量化器被钉成 [0,0]（`encoder_aom.cc:1057-1062`）→ alpha 近无损；影响未量化（X-LIBRARY-18） |
| lossless-alpha / auto-tiles / enable-intrabc（aom） | 高级 | false/false/true | bool | `src/codecs/enc_heif.cpp:482` | 是 | |
| （库面缺）`aom:*` 私有选项 | 否 | aom 默认 | aom_codec_set_option 键集 | `src/codecs/enc_heif.cpp:525-533` | 条件 | 预设/CLI 可用（实测 `aom:cq-level=30` 成功） |
| （库面缺）`svt:*` 私有选项 | 否 | — | — | `src/codecs/enc_heif.cpp:525-533` | **否** | 插件无前缀分支 ⇒ 必失败（X-LIBRARY-05） |

##### 2.9 差集矩阵：库旋钮 | 库默认 | schema/UI | 判定 | 影响

| 库旋钮 | 库默认 | schema/UI | 判定 | 影响 |
|---|---|---|---|---|
| `jpegli_add_quant_table`（自定义量化表） | 无 | 无键（仅 `std_quant_tables` 布尔） | **遗漏** | 无法导入专业量化表／按表复现他人参数；P3 |
| `jpegli_set_linear_quality` | — | 无 | **遗漏** | 线性质量标度（与 libjpeg `-quality` 语义不同）不可用；P3 |
| `jpegli_simple_progression` / 自定义 scan 脚本 | level 2 | 只有 `progressive` 布尔 | **遗漏** | 渐进扫描脚本不可定制；P3 |
| `jpegli_suppress_tables` | FALSE | 无 | **遗漏** | 无法产"仅熵数据/缩写流"；P3 |
| `jpegli_set_colorspace` / `default_colorspace` | YCbCr（RGB 输入） | 无 | **有意**（管线恒 RGB→YCbCr） | 无 |
| `jpegli_set_input_format` | UINT8/native | 无 | **有意**（喂入恒 u8/u16 整型） | 无 |
| `jpegli_write_marker` / `write_m_header` | — | 无 | **有意**（元数据走 Exiv2 后写） | 无 |
| distance 上限 25 | 1.0 | 0.3–3.0 | **有意收窄**（R-294，tooltip 有出处） | 极端低码率档不可达；P3 |
| `restart_in_rows` 上限 65535 | 0 | 0–64 | **有意收窄**（同上） | 大文件重启粒度受限；P3 |
| `psnr_target` 无上限 | 0 | 0–100 | **有意收窄**（同上） | P3 |
| JXL effort 11（expert） | 7 | 1–10 | **遗漏**（需 `JxlEncoderAllowExpertOptions`） | 无损最省体积档不可达；P2 |
| `EXTRA_CHANNEL_RESAMPLING` | -1 | 无 | **遗漏** | alpha 不可单独降采样；P3 |
| `ALREADY_DOWNSAMPLED` | 0 | 无 | **不适用**（整帧喂入，不做预降采样） | 无 |
| `GROUP_ORDER_CENTER_X/Y` | -1（图心） | 无 | **遗漏** | 渐进中心固定为图心；P3 |
| `NOISE`（自适应噪声） | -1 | 无 | **有意**（头文件标"不推荐"，PHOTON_NOISE 取代） | 无 |
| `BUFFERING` / `OUTPUT_MODE` | -1 | 无 | **不适用**（非 chunked 输入） | 无 |
| `USE_FULL_IMAGE_HEURISTICS` / `DISABLE_PERCEPTUAL_HEURISTICS` | 1 / 0 | 无 | **遗漏**（专家档） | 无法彻底关感知启发；P3 |
| `JxlEncoderSetExtraChannelDistance` | = frame distance | 无 | **遗漏** | 有损 JXL 透明区质量不可单独控制；P2 |
| `JxlEncoderSetUpsamplingMode` | -1 | 无 | **遗漏** | resampling>1 时无法选最近邻/点阵；P3 |
| PNG `compression`（zlib 策略 7 档） | 空=Z_DEFAULT_STRATEGY | 无 | **遗漏** | 照片无法用 Z_FILTERED；体积/速度次优；P2 |
| PNG `png:filter` | PNG_NO_FILTERS | 无 | **遗漏** | 滤波策略不可调；P2 |
| PNG `oiio:dither` | 0 | 无（另有全局 dither8/TPDF） | **遗漏/替代** | 两套抖动不可选；P3 |
| PNG `png:linear_premult` / `oiio:UnassociatedAlpha` | — | 无 | **不适用**（不写预乘） | 无 |
| TIFF `tiff:bigtiff` | 自动 >4000MB | 无 | **遗漏** | 只能在 4GB 边界被动切 BigTIFF；P3 |
| TIFF `tiff:RowsPerStrip` | libtiff 默认 | 无 | **遗漏** | strip 粒度不可调（影响并行与内存）；P3 |
| TIFF `planarconfig` | contig | 无 | **遗漏** | 平面分离不可选；P3 |
| TIFF `tiff:multithread` | 1 | 无 | **遗漏** | 写盘并行不可关（确定性排查用）；P3 |
| TIFF `tiff:write_iptc` / `tiff:write_exif` / `SubFileType` | 0 / 1 / 0 | 无 | **有意**（IPTC 在负面清单；EXIF 走 Exiv2） | 无 |
| TIFF 的 jpeg/lzma/zstd/webp 压缩 | 编译期不可得 | 枚举 5 档 | **有意/不可得**（`tiffoutput.cpp:56` `ENABLE_JPEG_COMPRESSION 0`；`tiffconf.h` 无 LZMA/ZSTD/WEBP） | 无（OIIO 面就这 5 档） |
| WebP `image_hint` | DEFAULT | 无 | **遗漏** | 无损内容提示不可选；P3 |
| WebP `target_size` | 0 | 无 | **遗漏** | 按目标体积编码不可用；P2 |
| WebP `target_PSNR` | 0 | 无 | **遗漏** | 按 PSNR 编码不可用；P2 |
| WebP `use_delta_palette` | 0 | 无 | **遗漏**（头文件标 reserved） | P3 |
| WebP `show_compressed` | 0 | 无 | **有意**（调试用回读压缩数据） | 无 |
| WebP `thread_level` | 0 | 无（由 `encode_threads` 驱动） | **有意** | 无 |
| x265 私有选项（`x265:*`） | x265 默认 | 无 GUI 入口 | **有意**（`docs/design.md:489`） | 只能写预设 JSON；P2 |
| aom 私有选项（`aom:*`） | aom 默认 | 无 GUI 入口 | **有意**（同上） | 同上；`aom:` 前缀有效 |
| svt 私有选项（`svt:*`） | — | 白名单放行但无实现 | **假桩** | 写了必失败；P2 |
| SVT `chroma` | 插件写死 420 | 内省无键 | **库面限制**（非本仓裁剪） | AVIF/SVT 无 4:4:4、无无损；P1 |
| `heif_encoder_set_logging_level` / `has_default` | — | 无 | **不适用**（日志面/查询面） | 无 |

##### 2.10 若要做到「全参数暴露」，最小补齐清单

**A. 先修「有键但不生效/必失败」（不修则补参数没有意义）**

1. `qp`（SVT）：内省**无默认值**的参数不得兜底成 min 并强制下发 → 见 X-LIBRARY-01（`enc_heif.cpp:177` + `paramform.cpp:879-891`）。
2. AVIF/SVT 无损：按 `is_known("chroma")` 重算 `lossless_capable`，或无损时自动换 libaom → X-LIBRARY-02。
3. WebP `preset`：改成"套用一次预设到工作集"的按钮语义，或只在用户改值时下发字段 → X-LIBRARY-03。
4. 删 JXL `brotli_effort` 行（或置不可见 + 说明）→ X-LIBRARY-04。
5. 删 `svt:` 白名单（或实现前缀）→ X-LIBRARY-05。
6. 删 TIFF `ccittrle`（或 disabled + "仅 1bit 二值"）→ X-LIBRARY-06；同时处理 `predictor=float`（X-LIBRARY-17）。
7. `__heif_profile` 改独立 JSON 字段 → X-LIBRARY-12。
8. `dev_cli` 对 heif/avif 用内省表解析类型（一行：`lookup_param_def` 前先 `introspect_backends` 建临时 FormatDef）→ X-LIBRARY-11。

**B. 每格式最小键集（补齐后即可主张"库面 = UI 面"）**

| 格式 | 要补的键 | 类型/范围 | 落点 |
|---|---|---|---|
| png | `strategy`、`filter` | Enum{default,filtered,huffman,rle,fixed,pngfast,none}；Int 0–5/位掩码 | `format_tables.cpp` png tech + `enc_oiio.cpp:240-248` 写 `compression` / `png:filter` |
| tiff | `bigtiff`、`rows_per_strip`、`planar_separate`、`multithread` | Bool/Int/Bool/Bool | `enc_oiio.cpp:249-296` 写 `tiff:bigtiff` / `tiff:RowsPerStrip` / `tiff:planarconfig` / `tiff:multithread` |
| webp | `image_hint`、`target_size`、`target_psnr` | Enum{default,picture,photo,graph}、Int≥0、Float≥0 | `enc_webp.cpp:162-199` 写 `cfg.image_hint/target_size/target_PSNR` |
| jxl | `effort` 11、`alpha_distance`、`alpha_resampling`、`group_center_x/y`、`upsampling_mode`、`disable_perceptual_heuristics` | Int 11（+`JxlEncoderAllowExpertOptions`）、Float 0–25、Enum{-1,1,2,4,8}、Int、Enum{-1,0,1} | `enc_jxl.cpp:76-104` 表 + `:265-271` 打开 expert |
| jpeg | `quant_source`（std/jpegli/custom-JSON）、`linear_quality`、`suppress_tables`、`scan_script` | Enum/Int/Bool/Enum | `enc_jpegli.cpp:262-305` 调 `jpegli_add_quant_table` / `jpegli_set_linear_quality` / `jpegli_suppress_tables` / `jpegli_simple_progression` |
| heif/avif | GUI「插件私有键」自由表（`x265:` / `aom:`），或 x265 常用 10 键 + aom 常用 6 键的显式行 | key=value 文本区 | `paramform.cpp` 加一个透传行，`enc_heif.cpp:520-534` 已有消费端 |

**C. 机制缺口（比补键更根本）**

1. **三态类型**：库大量用 `-1` 表示"编码器自选"，而 schema 只有 Bool/Int；本轮 7 个 JXL 参数因此退化成 0/1（X-LIBRARY-15）。
   加 `ParamType::Tri`（或统一约定 Enum{-1,0,1} + "自动"显示）。
2. **"仅下发用户改动键"能力**：没有它，任何"库无默认值"的参数（svt qp、aom alpha-*）都会被永久强制（X-LIBRARY-01/19）。
3. **内省参数的分组/单位/工具提示**：内省出来的行 label = key（`enc_heif.cpp:152`），中文释疑缺失，属 P3 打磨但与"全参数可用"体验直接相关。

---

#### 3. 发现（按 P0→P3）

##### X-LIBRARY-01 [P1] AVIF/SVT-AV1：内省把无默认值的 `qp` 兜底成 0 并每次下发，CQP 被钉死、质量参数失效

- 证据：
  - `src/codecs/enc_heif.cpp:161-177`：`have_current = heif_encoder_get_parameter_integer(...)`；失败时 `d.def = int64_t(have_min ? mn : 0)` —— svt 的 `qp` getter 在未设置时**返回 unsupported**（`libheif/plugins/encoder_svt.cc:489-495`），故 `have_current=false`、`def=min=0`。
  - `src/ui/paramform.cpp:879-891`（`make_defaults` 把内省 `ParamDef::def` 全量写进工作集）→ `src/ui/page_output.cpp:706-736`（`spec_of` 全量下发）→ `src/core/pipeline.cpp:1747-1749`（`base_params` 覆盖）→ `src/codecs/enc_heif.cpp:453`（`heif_encoder_set_parameter_integer("qp",0)`）。
  - svt 侧语义：`encoder_svt.cc:466-470` 置 `qp_set=true`；编码时 `rc.qp` 生效并压过 quality 的 CQP。
- 事实（实测，预设 JSON 注入类型化参数）：
  - `qp=0 & quality=20` → 1029 B，SHA256 `AF462C70…`；`qp=0 & quality=90` → **1029 B，同一 SHA256**。
  - `qp=30 & quality=20` / `qp=30 & quality=90` → 均 613 B，同一 SHA256 `B5F2E608…`；SVT 日志两行都是 `CQP / 30.00`。
  - 对照（无 `qp`）：`quality=20` → `CQP / 50.50`、580 B；`quality=90` → `CQP / 6.25`、1769 B。
- 影响：AVIF 默认后端下「质量」滑杆**完全失效**；默认工作集（qp=0）等于强制近无损，照片体积成倍膨胀；界面显示的"质量 50"与产物无关。
- 建议：① `enc_heif.cpp:177` 的兜底改为"不产出 def"（缺键 = 不下发）；② ParamForm 记录"改值键集合"，`spec_of()` 只下发改动键 + 引擎必需键；③ 对 `qp` 与 `quality` 加交叉提示（两者互斥）。
- 状态：confirmed；known=no（`docs/param-catalog.md:147` 只把 qp 列为"高级内省项"，无互斥说明）。

##### X-LIBRARY-02 [P1] AVIF + SVT-AV1 + 无损必然失败；UI 无损复选框与 High/Professional 档位是陷阱

- 证据：
  - `libheif/plugins/encoder_svt.cc:264-273`：`chroma` 参数注册被 `#if 0` 关闭；`svt_set_parameter_string`（`:535-590`）只认 `tune`，无 chroma 分支；`svt_query_input_colorspace2` 恒返回 420。运行时实测（`pp_linkprobe`）：内省表 = `speed|threads|tile-rows|tile-cols|quality|lossless|qp|min-q|max-q|tune`（**无 chroma**）。
  - `src/codecs/enc_heif.cpp:554-561`：无损要求 `is_known("chroma")`，否则 `fail("lossless requested but backend exposes no 'chroma' parameter…")`。
  - 能力位：`encoder_svt.cc:1310` `supports_lossless_compression = true` → `src/codecs/enc_heif.cpp:1306` `lossless_capable=true` → `src/ui/paramform.cpp:599-602` **显示无损复选框**；档位行不按后端过滤（`src/ui/page_output.cpp:230-253`）。
- 事实（实测）：`--format avif --backend svt-av1 --lossless` → `failed 0 B`，错误即上述字符串；`--backend libaom --lossless` → `ok 479 B`。档位 High（4:4:4）在 `enc_heif.cpp:641-654` 反查时必然不一致（layout 恒 420）→ 同样失败。
- 影响：勾选无损（或选 High/Professional）后**每个文件失败**，错误是英文技术文案，UI 既不预警也不自动换后端；同名操作在 libaom 下正常 → 用户无法理解差异。
- 建议：`introspect_backends`（`enc_heif.cpp:1286-1310`）对 SVT 把 `lossless_capable` 与"4:4:4 可选"按 `is_known("chroma")` 重算；无损时自动切 libaom（参照 `evaluate_avif_alpha_preselect` 的既有做法）；档位项按后端限幅并在 tooltip 写原因。
- 状态：confirmed（实测 + 代码）；known=no。

##### X-LIBRARY-03 [P2] WebP「内容预设」是死旋钮：6 档产物逐字节相同

- 证据：`src/codecs/enc_webp.cpp:165-172` 先 `WebPConfigPreset(preset,quality)`，随后 `:174-192` 把预设会改的**全部 5 个字段**用显式参数覆盖；`libwebp/src/enc/config_enc.c:64-96` 证明预设只改 `sns_strength` / `filter_sharpness` / `filter_strength` / `preprocessing` / `segments`。
- 事实（实测）：`preset=0,1,2,3,4,5` 六次编码产物均 **224 B、SHA256 `7B867FAE…` 完全相同**。
- 影响：高级区「内容预设」调了没有任何变化；tooltip 声称"photo：sns 80、filter 30、sharpness 3" 与实际（用户可改的三项默认已是 50/30/0）不一致 —— 点击 photo 不会再套用一次。
- 建议：把 preset 改为按钮语义（`apply_webp_preset()` 写工作集并 emit changed），或改成"预设字段仅在用户未改动时下发"；否则删除该行。
- 状态：confirmed；known=no。

##### X-LIBRARY-04 [P2] JXL `brotli_effort` 在本管线恒无效果

- 证据：`src/codecs/enc_jxl.cpp:94` 把 `brotli_effort` 映射到 `JXL_ENC_FRAME_SETTING_BROTLI_EFFORT`；`vcpkg_installed/.../jxl/encode.h:329-334` 明确它"只用于 JPEG 重压缩与压缩元数据 box（brob）"；而本编码器的两个 box 都以 `compress=JXL_FALSE` 写入（`enc_jxl.cpp:526-527`、`:534-536`），且无 JPEG 重压缩路径。
- 事实：该键在下发链上"成功"（`SetOption` 返回 SUCCESS），但 libjxl 没有任何 brob/JPEG 场景消费它 ⇒ 输出逐字节不变。
- 影响：高级区存在一个"调了永远没反应"的参数，且其 tooltip 已自承"与 modular 编码无关"。
- 建议：从 `format_tables.cpp:549-556` 删除该行（或 `visible` 恒 false + tooltip 说明"本管线 box 未压缩，此参数无效"）。
- 状态：confirmed（代码链闭合，未做逐字节对照 —— 若需要可加一个 `brotli_effort=11` vs `0` 的对照，见 §5）。
- known=no。

##### X-LIBRARY-05 [P2] `svt:` 透传通道必然失败（白名单放行 + 插件无该前缀）

- 证据：`src/core/params.cpp:174-178` 把 `svt:` 前缀列为**合法键**（cross_validate 不报未知）；`src/codecs/enc_heif.cpp:525-533` 用 `heif_encoder_set_parameter_string` 透传；`libheif/plugins/encoder_svt.cc:535-590` **没有** `svt:` 分支（对比 `encoder_x265.cc:531-534` 有 `x265:`、`encoder_aom.cc:743-747` 有 `aom:`）。
- 事实（实测）：`--param svt:qp=30` → `failed`，错误 `plugin option 'svt:qp' rejected by backend: Unsupported encoder parameter (heif_error code=5 subcode=2005)`；同命令换 `aom:cq-level=30` / `x265:crf=20` → `ok`。
- 影响：预设/CLI 写 `svt:` 键 = 100% 失败（且失败发生在逐文件编码期，不是配置校验期）；白名单让用户以为合法。
- 建议：`is_plugin_passthrough_key` 去掉 `svt:`（或对 svt 后端把该前缀判为未知键），并在 `enc_heif.cpp:531` 处把 unsupported 降级为"忽略 + 日志"而不是整文件失败。
- 状态：confirmed；known=no。

##### X-LIBRARY-06 [P2] TIFF `compression=ccittrle` 恒失败（Bilevel-only），却是 UI 枚举项

- 证据：`src/core/format_tables.cpp:629-640` 枚举含 `ccittrle`；`src/codecs/enc_oiio.cpp:57-61` 只做名字白名单，不校验"位深必须 1 bit"；OIIO 把名字映射到 `COMPRESSION_CCITTRLE`（`tiffoutput.cpp:308`）后由 libtiff 拒绝。
- 事实（实测）：`rgb8.png → tiff, compression=ccittrle` → `failed 0 B`，错误 `TIFFWriteScanline failed writing line y=0,z=0 (Bits/sample must be 1 for Group 3/4 encoding/decoding)`；对照 none/lzw/zip/packbits 四档全部 `ok`（`EXIT 0`）。
- 影响：选该项 = 每个文件失败；而 `docs/param-catalog.md:220` 的 R-316 把它当作"漏录补上"，等于把一个不可用档正式纳入参数面。
- 建议：删除该枚举项，或 `locked/disabled` + tooltip「仅 1bit 二值样本可用，本管线输出 8/16 位」；`enc_oiio.cpp` 侧加前置校验给出中文文案。
- 状态：confirmed；known=no（catalog 视为已修项）。

##### X-LIBRARY-07 [P2] PNG：OIIO 的 4 个输出属性只用了 1 个（策略与滤波缺失）

- 证据：`libpng` 输出属性面 = `png:compressionLevel`（`pngoutput.cpp:164-169`）、`compression`（zlib 策略 7 档，`:170-191`）、`png:filter`（`:199-200`，默认 `PNG_NO_FILTERS`）、`oiio:dither`（`:236-238`）。本仓只写第一个：`src/codecs/enc_oiio.cpp:240-248`；schema 只有 1 键（`format_tables.cpp:607-616`）。
- 事实：PNG 输出的 zlib 策略永远是 `Z_DEFAULT_STRATEGY`、滤波永远 `PNG_NO_FILTERS`；照片类 PNG 常见的 `Z_FILTERED` + 自适应滤波组合不可达。
- 影响：PNG 体积/速度次优（P2「性能体积次优」）；且"全参数暴露"对 PNG 只剩 1 条，与 `docs/param-catalog.md:214` 自注"待按 OIIO 文档补全"一致 = 未完成。
- 建议：加 `png_strategy`（Enum 7 档，写 `compression`）与 `png_filter`（Enum none/sub/up/avg/paeth/adaptive，写 `png:filter` 的 libpng 掩码）。
- 状态：confirmed（库读取点 + 我方缺失均已核对）；known=部分（catalog 自认待补；`docs/design.md:490` 却标"已接受/已穷尽"）。

##### X-LIBRARY-08 [P3] `docs/design.md:490` 的「OIIO 即库面，PNG 1 / TIFF 5 条（已穷尽）」裁定前提不成立

- 证据：`docs/design.md:490`「R5 | OIIO PNG/TIFF 输出参数覆盖面 | **已接受** | …（M0 定稿：PNG 1 / TIFF 5 条真实参数，无后端项已剔除）」；实际库面见本文件 §2.3/§2.4（PNG 至少还有 2 条、TIFF 还有 4 条），`docs/param-catalog.md:214` 也自注"待补全"。
- 影响：该裁定的作用是"关掉这条风险"，但它建立在**未穷尽**的枚举上 ⇒ 缺口从此没人再查（本审计即为反证）。
- 建议：把 R5 改成"已接受但未穷尽，待补 §2.10-B 清单"，并在 `param-catalog` 中标注"未完成"状态。
- 状态：confirmed（文档 vs 源码）；known=yes（本条即对已知文档的纠偏）。

##### X-LIBRARY-09 [P2] JXL 缺 8 个帧设置 + 2 个编码器级 API（含 alpha 距离与 effort 11）

- 证据（库面）：`jxl/encode.h:153`（EXTRA_CHANNEL_RESAMPLING）、`:164`（ALREADY_DOWNSAMPLED）、`:220-225`（GROUP_ORDER_CENTER_X/Y）、`:356`（BUFFERING）、`:388-393`（USE_FULL_IMAGE_HEURISTICS / DISABLE_PERCEPTUAL_HEURISTICS）、`:418`（OUTPUT_MODE）、`:1484-1485`（`JxlEncoderSetExtraChannelDistance`）、`:1247-1249`（`JxlEncoderSetUpsamplingMode`）、`:1562-1570`（`JxlEncoderAllowExpertOptions` → effort 11）。本仓映射表 `src/codecs/enc_jxl.cpp:76-104` 不含这些；`effort` 上限 10（`format_tables.cpp:111`）。
- 事实：`enc_jxl.cpp` 从不调用 `SetExtraChannelDistance` ⇒ 有损 JXL 的 alpha 平面按 frame distance 编码（头文件 `:1472-1475`：未设置时"使用 `JxlEncoderSetFrameDistance` 的值"）——UI 无任何控件，也无提示；`effort=11` 需要 expert 开关，当前 schema 范围直接封死。
- 影响：① 有损 JXL 的透明区质量不可控（PNG/WebP 的 alpha 有独立旋钮，JXL 没有）；② 无损用户拿不到"最省体积"档（effort 11 是 pixel-lossless 专用档）；③ resampling>1 时无法选上采样方式；④ 渐进中心只能是图心。
- 建议：补 `effort` hi=11 + `JxlEncoderAllowExpertOptions`、`alpha_distance`、`group_center_x/y`、`upsampling_mode`、`disable_perceptual_heuristics`；`BUFFERING/OUTPUT_MODE/ALREADY_DOWNSAMPLED/NOISE/JPEG_*` 在 schema 注释里明确"不适用"（避免下次又被当遗漏补进来）。
- 状态：confirmed；known=no。

##### X-LIBRARY-10 [P2] jpegli 量化表自由度只有 1 个布尔；量化表/线性质量/扫描脚本不可达

- 证据（库面）：`jpegli/encode.h:63-65`（`jpegli_add_quant_table`）、`:54`（`jpegli_set_linear_quality`）、`:67`（`jpegli_simple_progression`）、`:69`（`jpegli_suppress_tables`）、`:49`（`jpegli_set_colorspace`）；本轮 `JPEG_LIB_VERSION 62`（`jpegli/jconfig.h`）故 `jpegli_default_qtables` 不可用（`encode.h:57-59` 有版本守卫）。本仓只有 `std_quant_tables` 布尔（`format_tables.cpp:385-391`），消费点 `enc_jpegli.cpp:266-268`。
- 事实：JPEG 量化表在"全参数暴露"口径下只有 **Annex K / jpegli 自有** 二选一；自定义表、线性质量标度、自定义渐进脚本、表抑制均不可达。
- 影响：无法复现专业相机/工具链的量化表；无法产缩写流（无表 JPEG）；`jpegli_set_linear_quality` 是 libjpeg `-quality` 语义的唯一入口，缺失会让"按 libjpeg quality 对齐"不可能。
- 建议：至少补 `linear_quality`（Bool 切换 `jpegli_set_linear_quality`）与 `quant_source`（Enum std/jpegli/custom + 64 数 JSON 导入 → `jpegli_add_quant_table`）；`suppress_tables`/`simple_progression` 可标注"不适用/低优先"。
- 状态：confirmed；known=no。

##### X-LIBRARY-11 [P2] HEIF/AVIF 插件私有旋钮无 GUI 入口；且 `--dev` 无法设置 libheif 数值参数

- 证据：
  - GUI：参数行只来自内省（`src/codecs/enc_heif.cpp:232-239`）；粘贴参数只收表内键（`src/ui/paramform.cpp:1154-1156` 的 `find_param` 命中才收，未知键 `continue`），`set_values` 同样（`:1349-1356`）。⇒ `x265:ctu=32`、`aom:cq-level=30` 这类键**没有 GUI 输入路径**。
  - `--dev`：`src/dev_cli.cpp:387-392` 对静态表查不到的键一律存成 **string**；heif/avif 的静态表 `techs` 为空（`format_tables.cpp:877-894`）⇒ 所有内省键都走这条路 ⇒ `enc_heif.cpp:443-448`（Int 分支 `std::get_if<int64_t>`）判 `expected integer` 并**整文件失败**。
  - 实测：`--format avif --backend svt-av1 --param qp=0` → `failed`，错误 `parameter 'qp' rejected by backend: expected integer`。
- 影响：① x265 的 200+ 选项、aom 的选项只能靠手写预设 JSON；② 金样/回归无法覆盖 HEIF/AVIF 的任何参数（当前两个金样都是"无参数"跑通）；③ 用户按 `--help` 的 `--param` 文档操作会直接踩失败。
- 建议：`dev_cli.cpp:387` 前对 heif/avif 先 `pp::introspect_backends(format)` 建一个临时 `FormatDef` 再查 def（`lookup_param_def` 已是单源）；GUI 侧在内省区末尾加一行"插件私有键（key=value）"文本控件，透传到 `enc_heif.cpp:520-534`。
- 状态：confirmed（实测 + 代码）；known=部分（GUI 缺入口属 `docs/design.md:489` 有意裁剪；`--dev` 的类型失败不是有意）。

##### X-LIBRARY-12 [P2] HEIF/AVIF 编码档位不进预设（`__heif_profile` 被 `__` 前缀规则丢弃）

- 证据：`src/codecs/heif_profile.h:47` `kHeifProfileParamKey = "__heif_profile"`；`src/ui/preset_io.cpp:388-396`（`params_to_json` 跳过 `__` 前缀）、`:408-414`（`params_from_json` 同样跳过）；而 `src/ui/page_output.cpp:359-361`、`:732-735` 把它写进 `spec.params`。
- 事实：档位选择只活在会话内与本次运行的 spec 里；存预设 → `__heif_profile` 被丢 → 重载回 Auto（`page_output.cpp:686-697` 找不到键时按 0=Auto）。
- 影响：用户显式选 Main10 / High 后保存预设，重载后静默回 Auto（若位深/色度恰好能推导出同档位则表面一致，否则配置漂移）；预设的"可复现"承诺对这一项不成立。
- 建议：档位改为 `outputs[i].heif_profile`（普通字段，不走 `__` 保留键），或把保留键白名单化（只允许 `__lossless` 被丢）。
- 状态：confirmed；known=no。

##### X-LIBRARY-13 [P2] `current_chroma()` 用 `param_int` 读字符串枚举 → 恒返回 420，档位注释/`Main 4:4:4` 标签恒错

- 证据：`src/ui/page_output.cpp:459-464` `return int(pp::param_int(st->form->values(), "chroma", 1));`；`src/core/params.cpp:425-432` `param_int` 只接受 `int64_t`，字符串值直接回落 fallback；而 heif/avif 的 `chroma` 内省为**字符串** Enum（`src/codecs/enc_heif.cpp:190-207` + `encoder_x265.cc:291-298` 声明 `heif_encoder_parameter_type_string`，值集 "420/422/444"）。
- 事实：`current_chroma()` 在 4:4:4 / 4:2:2 下都返回 1（420）⇒ `update_profile_note`（`:520-594`）的推导恒按 420 算：4:4:4 时显示"插件自选 → Main"；`if (id=="heif" && chroma==3)` 的 "Main 4:4:4（HEVC 4:4:4）" 分支**永不进入**；无损强制 444 后也仍显示 Main。
- 影响：档位注释与真实产物矛盾（真档位是 HEVC Main 4:4:4 / AV1 High），用户据此判断兼容性会判错。
- 建议：改成读字符串（`param_str(...,"chroma","420")` 再映射到 1/2/3），或抽一个 `chroma_index_of(ParamSet)` 纯函数单测钉住。
- 状态：confirmed（纯代码可判定；未跑 GUI，见 §5）；known=no。

##### X-LIBRARY-14 [P3] JXL 默认 `codestream_level=10`（库默认 -1）且无条件下发

- 证据：`src/core/format_tables.cpp:140-155`（`def = 10`，choices auto(-1)/5/10）；`src/codecs/enc_jxl.cpp:348-353` 无条件 `JxlEncoderSetCodestreamLevel(level)`；`jxl/encode.h:1338-1341`（level 10 会自动启用容器）、`:1395-1408`（level 10 = 解除限制）。
- 事实：所有 JXL 产物都以 level-10 头写出（即使 8/16 位 RGB 只需 level 5）；只支持 level 5 的解码器按规范可拒收；容器因此恒开（本仓本来就 `UseContainer(true)`，所以容器面不受影响）。
- 影响：兼容性面收窄（少见但真实），且这是**默认值**而非用户选择；`JxlEncoderGetRequiredCodestreamLevel` 已经能自动判定，却没用。
- 建议：默认改 -1（auto）；或编码前调 `JxlEncoderGetRequiredCodestreamLevel` 自动择档，并把 tooltip 从"catalog 默认 10"改为兼容性说明。
- 状态：confirmed（代码 + 头文件语义）；known=no（catalog 只记"默认 10"，无理由）。

##### X-LIBRARY-15 [P3] 7 个 JXL 三态参数被 Bool 化，默认值即显式下发（与库默认 -1 不同）

- 证据：`format_tables.cpp:442-492`（`keep_invisible`/`dots`/`patches`/`gaborish`/`progressive_ac`/`qprogressive_ac` 皆 Bool）+ `:557-564`（`responsive`）、`:542-548`（`modular_lossy_palette`）；`enc_jxl.cpp:392-396` Bool→0/1；库语义 `jxl/encode.h:179-208,227-242,267-270`（-1=编码器自选）。tooltip 已自承"Bool 无法表达 -1"。
- 事实：这些键在默认工作集里就存在 ⇒ 每次编码都发显式 0/1：`progressive_ac/qprogressive_ac` 默认发 0（强制关闭谱渐进，库默认可能自选开启）；`keep_invisible` 有损时发 1（库默认 0）⇒ 透明区 RGB 被保留、体积↑。
- 影响：无法回到"让编码器决定"；默认产物的渐进/体积特性与 libjxl 参考实现不同（可接受但应有意识）。
- 建议：改 Enum{auto(-1),off(0),on(1)}（与 `progressive_dc`/`resampling` 的现有做法一致）。
- 状态：confirmed；known=no（tooltip 提到限制，但未登记为缺口）。

##### X-LIBRARY-16 [P3] 若干 UI 范围窄于库（有出处，仍是差集）

- 证据：JPEG `distance` [0.3,3.0] vs 库 0–25（`format_tables.cpp:295-303`，R-294）；`restart_in_rows` [0,64] vs 65535（`:344-351`）；`psnr_target` hi=100（`:392-400`）；JXL modular `distance` hi=15 vs 25（`:174-184`）；`photon_noise` hi=6400、`modular_palette_colors` hi=4096、`modular_ma_tree_learning_percent` hi=200（库允许 >100）。
- 影响：专家档不可达；但每处 tooltip 都写了出处 ⇒ 属**有意收窄**，不是遗漏。
- 建议：保持，但可在 tooltip 加一句"如需超出请用预设 JSON 手填"（当前超范围值会被 `validate_params` 拒绝，故实际不可手填 → 若要支持需放宽 `hi`）。
- 状态：confirmed；known=yes（R-294 / tooltip）。

##### X-LIBRARY-17 [P3] TIFF `predictor=float` 是必失败档（假选项）

- 证据：`format_tables.cpp:651-667` 枚举含 `float`(3)；`src/codecs/enc_oiio.cpp:269-278` 对整型样本显式拒绝（"floating point predictor requires float samples"）。
- 影响：选中即整文件失败（文案清晰，但仍是"UI 提供必然失败的选项"）。
- 建议：从枚举移除，或 disabled + tooltip「本管线输出 8/16 位整型，不可用」。
- 状态：confirmed；known=yes（`docs/param-catalog.md:222` R-011 / `docs/deep-review-0.3.2.md` 抽核）。

##### X-LIBRARY-18 [P3] libaom 三个 alpha 旋钮无插件默认，被 GUI 以最小值 0 强制下发（同 X-LIBRARY-01 机制）

- 证据：`libheif/plugins/encoder_aom.cc:395-429`（`alpha-quality`/`alpha-min-q`/`alpha-max-q` 均 `has_default=false`，min=0）→ `src/codecs/enc_heif.cpp:177` 兜底 `def=0`；消费点 `encoder_aom.cc:1057-1062`：`alpha_min_q_set && alpha_max_q_set` 时 alpha 平面量化区间取 [alpha_min_q, alpha_max_q] = [0,0]，且 `alpha_quality_set` 时 quality 也取 0。
- 事实（实测，rgba8 → avif/libaom）：只给 `quality=50` → 676 B；再给 `alpha-quality=0/alpha-min-q=0/alpha-max-q=0` → 689 B（差异可测但小；样本仅 64×64）。
- 影响：含细节 alpha 的照片/图形 alpha 平面会被按量化器 0（近无损）编码 ⇒ 体积膨胀；机制与 X-LIBRARY-01 同源。
- 建议：同 X-LIBRARY-01（无默认值参数不得兜底下发）；确认修法时把 aom 的 alpha 三键一起覆盖。
- 状态：suspected（机制 confirmed，量级未用真实照片量化）；known=no。

##### X-LIBRARY-19 [P3] `metadata.cpp` 头部"Exiv2 写支持矩阵"与实装构建不符（文档漂移）

- 证据：`src/core/metadata.cpp:11-16` 断言 "This exiv2 build has no zlib (EXV_HAVE_LIBZ undefined) → PNG is not a registered image type … JPEG/TIFF/WebP 全读写；BMFF 只读"；实装 `vcpkg_installed/x64-windows-avx2/include/exiv2/exv_conf.h:52` `#define EXV_HAVE_LIBZ`、`:22` `#define EXV_ENABLE_BMFF`，且 `exiv2/pngimage.hpp` 在列；同文件 `:1034-1038` 又写"current vcpkg exiv2 port (features bmff,xmp,png → zlib) PNG is writable … R1 CLOSED"。
- 影响：同一文件两套口径，读代码/审矩阵的人无法判断真实能力（PNG 元数据到底能不能写、为什么会降级到 XMP 镜像）。实际矩阵应为：JPEG/TIFF/WebP/**PNG** 可读写（PNG 走 `eXIf`），HEIF/AVIF/JXL(BMFF) 只读。
- 建议：重写 `metadata.cpp:6-17` 的事实块为实测矩阵，并注明探测方式（`Exiv2::ImageFactory::getType`）。
- 状态：confirmed（头文件 vs 注释）；known=no。

##### X-LIBRARY-20 [P3] HEIF/AVIF 内省项 `threads` 恒被调度器值覆盖（UI 设了没用）

- 证据：`src/codecs/enc_heif.cpp:436-514` 先把参数集里的 `threads` 写进插件（svt/aom 内省表都含该键，实测 `pp_linkprobe` 列表分别在 av1 与 aom 面）；随后 `:586-591` 在**同一 encoder 上再写一次**：`heif_encoder_set_parameter_integer(enc, "threads", req.encode_threads)`。后写覆盖前写 ⇒ 用户设的 `threads` 永不生效。
- 事实（实测，仅弱佐证）：svt 预设 `threads=1` 与 `threads=16` 均打印 `Svt[info]: Level of Parallelism: 4`（`--workers 1`）；因 `lp` 并非 `threads` 的直接回显，此对照不构成强证明，强证据是上述调用顺序。
- 影响：高级区「线程数」对 HEIF/AVIF 是装饰项；用户为省 CPU 调低线程数不生效（实际并发由 `encode_threads` 决定）。E3 线程映射本身是有意设计（`docs/v0.3.0-design.md` §3.1），缺的是"UI 不提示该项被调度器接管"。
- 建议：`threads` 行置 `locked`（数据驱动锁）并写 tooltip「由调度器线程预算接管」；或在参数循环里跳过 `threads`。
- 状态：confirmed（代码顺序）；known=no。

---

#### 4. 输出兼容性与合规矩阵

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| jpeg / jpegli / 默认（444+progressive） | SOF2 渐进 + 3 分量 1×1；量化表 jpegli 自有 | 是（奇偶回读：本仓把产物再作为输入转 PNG 成功） | 无 |
| jpeg / `chroma=440` | SOF2；Y 采样 1×2，Cb/Cr 1×1（实测 SOF 逐字节核对） | 是（回读成功） | 合规（任意整数采样比合法），但 4:4:0 罕见，部分老播放器显示会偏色 |
| jpeg / `chroma=422|420` | Y 2×1 / 2×2 | 是 | 无 |
| jxl / 任意 | BMFF 容器（`JxlEncoderUseContainer(true)`）+ `Exif`/`xml ` box + **level 10** 码流头 | 是（libjxl）；严格 level-5-only 解码器**可拒收** | 中：level 10 非必需（X-LIBRARY-14）；容器恒开 |
| jxl / 无损 | Modular + `SetFrameLossless`，`uses_original_profile=1` | 是 | 无（金样 `jxl-lossless` 断言级覆盖） |
| png / 8·16 位 | IHDR 8/16bps；zlib 策略恒 DEFAULT、滤波恒 NO_FILTERS | 是 | 无（但体积次优，X-LIBRARY-07） |
| tiff / none·lzw·zip·packbits | 经典 TIFF（>4000MB 自动 BigTIFF，`tiffoutput.cpp:510-513`） | 是（lzw 回读成功） | 自动 BigTIFF 会让老式非 libtiff 读者失败（无 UI 提示）——P3 |
| tiff / `compression=ccittrle` | **无产物**（写第 0 行即失败） | — | 失败路径已清场（临时文件 + 不提交），无半成品（X-LIBRARY-06） |
| tiff / `predictor=float` | **无产物**（前置拒绝） | — | 同上（X-LIBRARY-17） |
| webp / lossy·lossless | RIFF；ICC 走 `ICCP` chunk（`WebPMuxSetChunk`，`enc_webp.cpp:291-324`） | 是 | 无；`near_lossless<100` 时"无损"实为非严格无损（tooltip 已警告） |
| heif / x265 | HEVC still；`hvcC` profile 按位深/色度（4:4:4 → Main 4:4:4）；无损强制 4:4:4 | 是（libde265 上限之外的 4:4:4 兼容性弱） | 中：4:4:4 查看器支持面窄（profile_note 已提示） |
| avif / svt-av1 | AV1 YUV420（插件写死） | 是 | **4:4:4 / 无损 / High 档不可得**（X-LIBRARY-02） |
| avif / libaom | AV1，色度可选 420/422/444 | 是 | 无 |
| avif / svt + alpha + >8bit | 前置拒绝（`enc_heif.cpp:676-677`，上游崩溃规避） | — | 已显式失败，无崩溃 |
| heif / avif 大图无损 | >16MP 自动 grid 分块（`enc_heif.cpp:873-964`） | 是（libde265 单图上限规避） | 分块是无损的必要妥协（tooltip 已写） |
| bmp | 24bpp RGB / 8bpp 灰度；无 ICC、无元数据 | 是 | 元数据丢弃属文档化行为（`metadata.cpp:1018-1022`） |

---

#### 5. 未覆盖与存疑

1. **未跑 GUI**（任务只读约束 + 禁 ui-smoke）：X-LIBRARY-13（`current_chroma` 恒 420）是纯静态代码判定（`param_int` 只吃 int64、heif chroma 是 string），未做窗口级复现；如需证据可加一个纯函数单测（`chroma_index_of`）。
2. **X-LIBRARY-04（brotli_effort）**未做逐字节对照（`brotli_effort=0` vs `11`）——代码链已闭合（box 未压缩、无 JPEG 重压缩），但"零字节差"属推断。
3. **X-LIBRARY-18（aom alpha 三键）**只有 64×64 小样本（676→689 B），量级未用真实带 alpha 照片量化 ⇒ 定 P3/suspected。
4. **未验证 JXL `SetExtraChannelDistance` 缺失的实际 alpha 误差**（需要对同一 RGBA 图分别用 frame distance=0 与 =2 编码并比对 alpha 平面的 PSNR/bit-exact）——结论"alpha 跟随 frame distance"依据头文件 `jxl/encode.h:1470-1475` 语义，未做像素级实测。
5. **`--dev` 与 GUI 的参数集不等价**是本轮最大的方法风险：`--dev --param` 对 heif/avif 的键一律退化成 string（X-LIBRARY-11），因此 §1 结论 2 的三条对照全部改用**预设 JSON（类型化）**注入；`dev_cli` 路径的 HEIF/AVIF 参数行为不能代表 GUI，反之亦然（金样走 `--dev`，其 `avif-lossy`/`heif-lossy` 因此是"零参数"路径）。
6. **未枚举 BMP/TIFF 的 OIIO 全局属性面**（`oiio:ColorSpace`、`oiio:BitsPerSample`、`tiff:ColorSpace` 等由管线色彩/位深逻辑隐式决定，不属"库可配置面 vs UI"的差集，故未列；如需要应另开一节核对色彩标签一致性）。
7. **Exiv2 矩阵**只核到「构建能力 + 代码注释」层（`exv_conf.h` + `metadata.cpp`），未逐格式实测写回（PNG `eXIf`、WebP EXIF chunk、BMFF 只读拒绝），该面属元数据章节的正题。
8. **未覆盖**：`heif_encoder_set_logging_level`/`has_default` 等查询/日志面 API、`jpegli_copy_critical_parameters`/`write_coefficients`/`write_raw_data`（转码/系数直写路径，与"解码→float→编码"架构不符，判为不适用）。

### 6.4 输出兼容性、色彩与像素管线（跨格式）

> 审计对象：`src/core/pipeline.cpp` 十阶段像素管线、`src/core/colormanager.*`、8 个编码器的产物结构、`src/core/metadata.*` 的容器写路径、`src/ui/*` 的降档提示面。
> 证据口径：代码/头文件/库源码行号 + `build/encoder-audit/compliance/**` 下的实测产物（纯 Python 字节解析 + OIIO `iinfo` + `pp_verify`）。
> 只读审计：未修改 `src/`、`tests/`、`docs/`、CMake；临时脚本与产物全部在 `build/encoder-audit/compliance/`。

#### 1. 结论

1. **管线顺序本身是对的**：`probe → plan → budget → decode → orient → color → meta → (per-target: flatten → encode → metawrite) → mtime`（`pipeline.cpp:1254-1282`），方向变换只做一次且把 `Orientation` 写回 1（实测 Orientation=6 的 TIFF → JPEG 尺寸 8×4→4×8、`Orientation: 1`，无二次旋转）；多页/动图只取首页并告警；仅元数据模式对 JPEG/PNG/WebP 实测 `pp_verify` 逐位一致；取消在阶段边界与编码行级都有检查点。
2. **最严重的问题：整条链路的 alpha 语义错位。** OIIO 解码器返回的是**关联（预乘）alpha** 的 RGB（`pnginput.cpp:363-367`、`webpinput.cpp:363-369`、`imagebuf` 的原生约定），但管线把它当**直通（非预乘）**用：拍平公式是 `c*a + bg*(1-a)`（`simd/kernels.cpp:84-92`），把已预乘的 `c` 又乘了一次 alpha。实测 RGBA→BMP 的半透明区 R 通道偏 22–37 级（a=192 时 `177` vs 正确 `214`；a=64 时 `204` vs `241`），整行像素 `(177,119,92)` vs 正确 `(214,138,101)`。这是 P0，且影响最常见的 RGBA→JPEG 路径。
3. **WebP 带 alpha 产物自身就是错的**：`enc_webp` 直接把预乘数据 `WebPPictureImportRGBA` 当直通写盘（`enc_webp.cpp:242-264`），实测 PNG→WebP→PNG 后 alpha=192 的像素由 `(200,100,50)` 变 `(151,74,39)`（正好乘了一次 alpha）。本项目自己的读回也偏暗。
4. **JXL / HEIF / AVIF 带 alpha 产物"对外错、对内自洽"**：三者都写 `alpha` 未预乘声明（`enc_jxl.cpp:305,320` 的 `JXL_FALSE`；`enc_heif.cpp` 全文无 `heif_image_set_premultiplied_alpha`，容器里也无 `prem` iref，实测字节扫描），但写入的是预乘数据 → 标准查看器显示偏暗；本项目内部因为"写预乘/读不预乘"互相抵消而看似正常。
5. **PNG 输出的色彩声明自相矛盾**：OIIO 的 PNG writer 在没有 `oiio:ColorSpace` 时默认 sRGB，先写 `sRGB+gAMA+cHRM`（`png_pvt.h:638-639,692`），再写 ICC（`png_pvt.h:697-709`）。实测 P3 产物 chunk 序列 = `iCCP(375) sRGB(1) gAMA(4) cHRM(32)` —— 同一文件里 sRGB 声明与 P3 ICC 打架，按 sRGB chunk 优先的读取器会显示过饱和。
6. **BMP 是"无 ICC 容器"，但没有色彩告警**：BMP 产物只有 V3 `BITMAPINFOHEADER`（`bmpoutput.cpp:200-234`，无 profile 槽），而 `EmbedIccUnsupported` 只在**非 keep** 目标下产生（`pipeline.cpp:1157-1162`）。实测"Display P3 源 + keep → BMP"只报 `MetadataDropped: bmp output carries no metadata container`，P3 像素静默进入"被假定 sRGB"的容器。
7. **降档告警有系统性缺口，且所有 WarningKind 在 GUI 无显示**：BMP 的 `out_bitdepth` 是 bpp（24），却参与 bps 比较 `src_bitdepth > out_bitdepth`（`pipeline.cpp:1042`）→ 16bit 源 → BMP **不报** `DepthDowngrade`（实测 sidecar 里 `src_bitdepth=16`、warnings 只有 `MetadataDropped`）。同时全 UI 目录 grep 不到任何 `WarningKind`（`src/ui/*` 零命中），降档/丢元数据/拍平告警只进日志与 sidecar。
8. **金样对 alpha 没有像素裁判，且 pp_verify 的 alpha 语义不对称**：`alpha-jpeg` 与 `multiformat-split` 的 jpeg 腿都是 `"pixel": {}`（不比较像素），所以拍平数学错误无裁判；而 `webp-lossless` 是 `exact` 且能通过，恰恰因为 pp_verify 对 WebP 用 libwebp（直通）而对其他格式用 OIIO（预乘）—— 修正 WebP 编码器后这条断言会**假失败**（`pp_verify.cpp:19-21,106-150`）。

#### 2. 参数全量矩阵

> 只列与像素管线/产物合规相关的键（完整逐格式参数表归"参数引擎"章节）。`读取点` 一律给 file:line；"生效"∈ 是/条件/否/未知。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| 位深（格式级，非 param） | 是（`pp-bitdepth-combo`，按格式能力锁定） | jpeg 8 / webp 8 / bmp 24 / png 16 / tiff 16 / jxl 16 / heif 10 / avif 10（`pipeline.cpp:1718-1734`） | 各格式见 `format_tables.cpp:413,599,618,686,859,874,882,892` | `pipeline.cpp:1042`、`enc_oiio.cpp:204-218`、`enc_jxl.cpp:299`、`enc_heif.cpp:629-630` | 条件 | bmp 的 8/24 是 **bpp**（`params.h:69,179`），其余是 bps；24 与 8 对彩色源同产 24bpp |
| lossless | 是（仅当后端有 `lossless_capable` 技术；**JPEG 隐藏**，`paramform.cpp:6-7`） | false | bool | `params.h:99-100`、`pipeline.cpp:1739-1756`、`enc_heif.cpp:506`、`enc_jxl.cpp:234` | 是 | `--dev --lossless` + JPEG 静默产有损（dev-only，见 X-COMPLIANCE-18） |
| color（色彩目标，非 param） | 是（`page_output` 全局卡） | keep | keep/srgb/p3/adobergb | `pipeline.cpp:864-955`、`colormanager.cpp:898-962` | 是 | 非 keep 时把目标 ICC 交给各容器；BMP 无处可放（只告警） |
| flatten_gray | 是（设置页滑杆 + 预览色块，`settings_dialog.cpp:1234-1239`） | 1.0（白） | 0.0–1.0 | `pipeline.cpp:1012`、`settings.cpp:161-164` | 是 | 公式错（P0），见 X-COMPLIANCE-01 |
| rotate_orientation | 是（设置页开关） | true | bool | `pipeline.cpp:828`、`mainwindow_run.cpp:312` | 是 | 旋转后强制 `Exif.Image.Orientation=1`（`pipeline.cpp:968-969`） |
| color_dither8 | 是（`pp-dither8-switch`，`output_page_build.cpp:335-345`） | false | bool | `page_output.cpp:920`、`enc_oiio.cpp:343-348`、`enc_webp.cpp:245-248` | 是 | 默认关 → 8bit 量化无抖动（banding） |
| gamut_mapping | 是（全局卡） | 0 | 0/1/2 | `page_output.cpp:921`、`colormanager.cpp:1000-1024`、`colormanager.cpp:720` | 条件 | 0 = 硬裁（无映射），超色域无告警 |
| color_intent | 是（全局卡） | 0 | 0–3 | `page_output.cpp:918`、`colormanager.cpp:916-923` | 是 | 渲染意图 + BPC 进 transform 缓存键 |
| linear_resample | 是（全局卡） | false | bool | `page_output.cpp:923`、`mainwindow.cpp:1247` | 条件 | 只影响预览/缩略图重采样，不影响产物 |
| png.compressionLevel | 是 | 6 | 0–9（越界 clamp 并告警） | `enc_oiio.cpp:241-248` | 是 | 键名已修为 `png:compressionLevel`（R-010） |
| tiff.compression | 是 | lzw | none/lzw/zip/ccittrle/packbits | `enc_oiio.cpp:250-259` | 是 | 未知名 → 明确失败（不静默回退 deflate） |
| tiff.deflate_level | 是 | 6 | 1–9 | `enc_oiio.cpp:255-259` | 条件 | 仅 compression=zip 时写 `tiff:zipquality` |
| tiff.predictor | 是 | 2 | 1/2/3 | `enc_oiio.cpp:260-279` | 条件 | 3(float) 必失败；compression=none/packbits 时整键静默忽略 |
| tiff.tiff_tile_width/height | 是（高级） | 0/0 | 0 或 16 的倍数 | `enc_oiio.cpp:280-295` | 是 | 单边 0 → 明确失败；实测 256×256 tile 可读 |
| jpeg.quality_mode/distance/quality | 是 | distance / 1.0 / 90 | enum / 0–25 / 1–100 | `enc_jpegli.cpp:87-89` | 是 | `psnr_target>0` 会静默覆盖三者（R-247） |
| jpeg.chroma | 是 | 444 | 444/440/422/420 | `enc_jpegli.cpp:90,290-300` | 是 | 默认 444（体积大，非缺陷） |
| jpeg.progressive / optimize_coding | 是 | true / true | bool | `enc_jpegli.cpp:91-92,281-284` | 是 | progressive 强制 optimize_coding |
| jpeg.arith_code / dct_method / xyb_mode | 是（应置灰） | false / 0 / false | — | `enc_jpegli.cpp:93,95,97` | 否 | 已知假接线/必失败（R-244/245/246） |
| webp.quality / method / lossless / exact / near_lossless | 是 | 90 / 4 / false / true / 100 | — | `enc_webp.cpp:169-198` | 是 | 设计口径见 `format_tables.cpp:226-276` |
| webp.alpha_compression/filtering/quality | 是（高级） | true / 1 / 100 | — | `enc_webp.cpp:182-184` | 是 | 只影响 alpha 平面编码，不解决预乘语义 |
| jxl.distance / effort / codestream_level | 是 | 1.0 / 7 / 10 | 0–25 / 1–10 / 5,10 | `enc_jxl.cpp:348,385-448` | 是 | lossless=true → 走 modular（`enc_jxl.cpp:234-236`） |
| jxl.modular.* / color_transform | 是（高级） | auto / YCoCg | — | `enc_jxl.cpp:393-415` | 条件 | 无损时部分档隐藏 |
| heif/avif.quality / lossless / chroma | 是（内省） | 90 / false / 420 | 0–100 / bool / 420,422,444 | `enc_heif.cpp:502-506,562-627` | 条件 | 无损强制 444；svt-av1 无 chroma 参数 → 无损直接失败 |
| heif/avif.profile（保留键 `__heif_profile`） | 是（`pp-profile-row`） | Auto | Auto/Main/Main10/… | `enc_heif.cpp:641-666`、`page_output.cpp:228-255` | 条件 | 与位深/色度不一致 → 拒绝编码（不静默出错档位） |
| sync_mtime（规则，非 param） | 是 | 见批量规则 | bool | `pipeline.cpp:1174-1184` | 是 | 只对 ok 行 mtime；失败只记 warn |
| metadata_only（模式） | 是 | false | bool | `pipeline.cpp:1288+`、`pipeline.cpp:1247-1249`、`metadata.cpp:1065+` | 条件 | heif/avif 不支持（实测明确拒绝） |

#### 3. 发现

##### X-COMPLIANCE-01 [P0] alpha 拍平把预乘缓冲当直通，半透明区整体偏暗

- 证据：
  - `src/decode/oiio_reader.cpp:234`：`out.buf.read(0, 0, 0, spec.nchannels, true, OIIO::TypeDesc::FLOAT)` —— 走 OIIO 原生约定。OIIO 的原生 alpha 形态是**关联（预乘）**：`png.imageio/pnginput.cpp:363-367`「PNG specifically dictates unassociated … Convert to associated unless we were requested not to」、`webp.imageio/webpinput.cpp:363-369`「Convert to the OIIO-native associated form」、`builtinplugins.rst:1942`「values passed through the OIIO APIs should use associated alpha」。
  - `src/core/simd/kernels.cpp:87-92`（`flatten_ref`，AVX2 同式 `:132-133`）：`a=clamp(alpha)`；`dst[c] = fma(src[c], a, bg*(1-a))` —— 对**直通**色才是正确合成式。
  - 实测（`build/encoder-audit/compliance/in/a_ramp.png`：色 (200,100,50)、alpha 255/192/128/64/32/16/8/0 → BMP 24bpp）：
    R 通道 `x=1(a=192)` 输出 **177**，正确合成 **214**，二次乘 alpha 的预测值恰好 176；`x=3(a=64)` 输出 **204**，正确 **241**（预测 204）；`x=4(a=32)` 输出 226 vs 正确 248。G/B 同规律（`x=1` 实测 `(177,119,92)`，正确 `(214,138,101)`，二次乘预测 `(176,120,91)`）。
  - 同一输入的中间态可直接观测：`a_ramp.png → TIFF16(uncompressed)` 的 R 分量 = `200*257, 151*257, 100*257, 50*257…`（即 `s*a`，证明解码后缓冲是预乘），见 §5 的实测命令。
- 事实：解码缓冲 `c = s·a` → 拍平算 `c·a + bg·(1−a) = s·a² + bg·(1−a)`（二次乘 alpha）→ 半透明像素比正确值暗 `s·a·(1−a)`（s=1、a=0.5 时暗 25% 满量程；本测 s≈0.78、a=0.5 时暗约 19%）。alpha 越"中间"越暗，alpha=0/255 两端不偏，所以缩略图上像"边缘发暗/半透明区域脏"，容易被当成有损压缩噪声。
- 影响：**所有 RGBA/灰度+alpha 源转 JPEG / BMP（即所有 `supports_alpha=false` 的格式）**都产出错误像素；`--dev` 与 GUI 都走同一条 `stage_encode_loop`。CG/UI 资源、Logo、截图水印这类带半透明的输入是主战场。告警只有 `AlphaFlattened`（只说"已合成到底色"），用户无法察觉数值错。
- 建议：在 `flatten_alpha_in_place`（`pipeline.cpp:353-375`）/`pp::simd::flatten*`（`simd/kernels.cpp:78-167`）二择一：①按预乘语义合成——`dst = src[c] + bg*(1-a)`（**推荐**，一行改动、无额外拷贝、与 OIIO 内部约定一致）；②在 decode 边界统一反预乘（`oiio:UnassociatedAlpha` 语义），但会给 PNG/TIFF 输出路径带来二次换算。修完必须补 `alpha-jpeg`/`multiformat-split` 的像素断言（见 X-COMPLIANCE-09）。
- 状态：confirmed；known=no

##### X-COMPLIANCE-02 [P0] WebP 带 alpha 输出把预乘数据当直通写入

- 证据：
  - `src/codecs/enc_webp.cpp:242-250`：逐行 `simd::interleave` + `quantize8` 后直接进 `buf`；`:263-264`：`WebPPictureImportRGBA(&pic, buf.data(), stride)` —— 全程无 alpha 反预乘。
  - WebP 规范/库语义：容器 alpha 是**非预乘**（`webpinput.cpp:363` 同款注释「WebP is unassociated alpha」）。
  - 实测：`a_ramp.png → webp(lossless) → png8`：`(200,100,50,192) → (151,74,39,192)`、`(200,100,50,128) → (100,50,26,128)` —— 正好乘了一次 alpha（s·a），且本项目自己读回也偏。
  - 反证（金样为何没抓到）：`tests/golden/smoke/webp-lossless.json` 对同一 alpha 输入断言 `pixel.mode=exact` 且**通过**；而 `pp_verify` 对 WebP 用 libwebp 直通解码（`pp_verify.cpp:106-144`）、对 PNG 用 OIIO 预乘解码（`pp_verify.cpp:77-104`），两者恰好相等 —— 这条 exact 断言实际上把预乘写盘锁成了"正确"。
- 事实：`c_buffer = s·a` → `quantize8(s·a)` → libwebp 视为直通存储 → 文件内颜色 = `s·a` → 任何标准解码器合成后偏暗（alpha=128 时蓝通道 50→26）。同时因为 OIIO 的 WebP reader 读回时又预乘一次，本项目的"输出→再输入"链路是 `s·a²`。
- 影响：所有带 alpha 的 WebP 产物（有损/无损皆然）颜色错误；网页/设计交付场景直接可见。属于"能生成但颜色错"的典型。
- 建议：`enc_webp.cpp` 在 `interleave/quantize8` 之前或之中做一次反预乘（`c/a`，`a==0` 时置 0）；更好的做法是在 pipeline 边界把"编码器 view"定义为直通（`EncodeRequest` 加 `alpha_is_associated` 或统一在 decode 后反预乘）。同步修 `pp_verify` 的 alpha 归一化（X-COMPLIANCE-10），否则修完会假失败。
- 状态：confirmed；known=no

##### X-COMPLIANCE-03 [P1] JXL 输出声明 alpha 非预乘，实际写入预乘数据

- 证据：
  - `src/codecs/enc_jxl.cpp:305`：`info.alpha_premultiplied = JXL_FALSE;`，`:320`：`ec.alpha_premultiplied = JXL_FALSE;`（两次都设 false）。
  - `src/codecs/enc_jxl.cpp:470-516`：像素面先 `pp::simd::quantize16(r.px.data()…)` 再 `JxlEncoderAddImageFrame(fs, &pf, pixel_data, …)` —— 数据直接取自工作缓冲（预乘，见 X-COMPLIANCE-01 的实测），全程无 `/a`。
  - 语义来源：`vcpkg_installed/x64-windows-avx2/include/jxl/codestream_header.h:225-229,291-294`「Whether the alpha channel is premultiplied」。
  - 实测：`a_ramp.png → jxl16(lossless) → tiff16` 的 R = `200,151,100,50,25,13,6`（= `s·a`，即文件里存的是预乘）；`jxl → png8` 却回到 `200/201/199`，说明 OIIO 的 JXL reader **不做**预乘（`jpegxl.imageio/jxlinput.cpp` 全文件无 alpha 关联处理），本项目的"读不预乘 + 写预乘"互相抵消。
- 事实：写：预乘送入 + 声明非预乘 → 文件不合规；读：本项目按非预乘取回（缓冲里带预乘值）→ 我们的 PNG/TIFF 输出因为又反预乘而"看起来正常"，但只要目标格式走 flatten（JPEG/BMP）就变成二次乘；第三方 JXL（真·非预乘）进本管线则会被反预乘一次（变亮/溢出）。
- 影响：JXL 产物在 Chrome/libjxl 系查看器里半透明区偏暗；本管线读第三方 JXL 时颜色偏亮。
- 建议：`enc_jxl.cpp` 二选一——反预乘后写入（与 WebP 同修）或 `alpha_premultiplied = JXL_TRUE`（语义正确但不改像素，最省事且立刻让查看器正确）；推荐前者以与其它容器统一。修完在 `tests/golden` 增加 JXL alpha 像素断言。
- 状态：confirmed；known=no

##### X-COMPLIANCE-04 [P1] HEIF/AVIF 输出同样缺 `prem` 声明（预乘数据 + 非预乘标记）

- 证据：
  - `src/codecs/enc_heif.cpp:778-790`：Y/Cb/Cr 由 `src[...]`（预乘 RGB）直接算：`y = 0.299r+0.587g+0.114b`；alpha 平面 `:794-798` 单独量化。`grep -i premul src/codecs/enc_heif.cpp` → **0 命中**。
  - libheif 侧：只有 `pixel_image->is_premultiplied_alpha()` 为真才写 `prem` 引用（`vcpkg/buildtrees/libheif/src/v1.23.5-6368e7938c.clean/libheif/context.cc:1787-1789`）；设置 API 存在但未被调用（`vcpkg_installed/x64-windows-avx2/include/libheif/heif_image.h:408-414` `heif_image_set_premultiplied_alpha`）。
  - 实测：`a_ramp.png → heif(lossless,444,8bit)` 的产物字节里 **无 `prem`**（扫描到 `auxl`/`auxC`/`colr`/`pixi`，无 `prem`）；`heic → tiff16` 的 R = `200,151,100,49,24,12,5`（= `s·a`）。
- 事实：文件里 YCbCr = 预乘色、alpha = a，且没有 `prem` 引用 ⇒ 按 HEIF 语义解码器应视为**非预乘**，合成时再乘一次 → 偏暗。OIIO 的 heif reader 依据 `is_premultiplied_alpha()` 决定是否补预乘（`heif.imageio/heifinput.cpp:389-399,580-593`），实测未补（缓冲就是 `s·a`），所以本项目内部链路同样"看似正常"。
- 影响：HEIF/AVIF 带 alpha 产物在遵循 `prem` 语义的查看器里偏暗；与本项目自身的一致性只是巧合。
- 建议：在 `heif_image_set_raw_color_profile` 附近调用 `heif_image_set_premultiplied_alpha(img, 1)`（若选"保持预乘"）**或**在写平面之前反预乘（与 WebP/JXL 统一）。二者都要在 `tests/golden` 增加 HEIF alpha 用例（现有金样无 4 通道 → heif/avif 用例，`enc_heif.cpp:736-737` 注释已自承）。
- 状态：confirmed（文件事实）；"标准查看器偏暗"按 `prem` 语义推定；known=no

##### X-COMPLIANCE-05 [P1] PNG 输出同时写 iCCP 与 sRGB/gAMA/cHRM，色彩声明自相矛盾

- 证据：
  - `vcpkg/buildtrees/openimageio/src/v3.1.17.0-7c90e77e3f.clean/src/png.imageio/png_pvt.h:638-639`：`string_view colorspace = spec.get_string_attribute("oiio:ColorSpace", "srgb_rec709_scene");` —— 本仓 `enc_oiio.cpp:221-237` 构造的 `ImageSpec` 从不设 `oiio:ColorSpace`，于是恒命中默认 sRGB。
  - `png_pvt.h:689-694`：`png_set_sRGB_gAMA_and_cHRM(sp, ip, PNG_sRGB_INTENT_ABSOLUTE);`；`png_pvt.h:697-709`：随后 `png_set_iCCP(...)`，两段都无条件执行。
  - 实测 chunk 序列（Display P3 目标产物）：`IHDR eXIf iCCP(375) sRGB(1) gAMA(4) cHRM(32) oFFs IDAT IEND`（`build/encoder-audit/compliance/out/A-p3-src/rgb8.png`、`out/ICC-keep-png/rgb8.png` 同）。
  - 规范面：PNG（ISO/IEC 15948 §11.3.3.5 / libpng 文档）规定 sRGB chunk 与 iCCP 不应同时出现；两者并存时语义冲突。
- 事实：目标色 = P3/AdobeRGB（或 keep 广色域源）时，产物一边用 ICC 说"我是 P3"，一边用 sRGB chunk + sRGB 的 gAMA/cHRM 说"我是 sRGB"。偏好 sRGB chunk 的读取器（部分浏览器/老版 Photoshop/libpng 先读 sRGB 的路径）会按 sRGB 解释 P3 数值 → 过饱和。
- 影响：广色域 PNG 交付的"颜色对不上"投诉；且这是**所有**带 ICC 的 PNG 产物（含 keep 重嵌源 ICC）的固定形态，不是边角。
- 建议：`enc_oiio.cpp` 给 PNG/其它 OIIO 输出的 `ImageSpec` 显式设 `spec.attribute("oiio:ColorSpace", <由 req.meta.icc_profile 推导>)`；对非 sRGB profile 需让 writer 走"ICC 优先、不写 sRGB/gAMA/cHRM"的路径（OIIO 侧可设 `oiio:ColorSpace` 为 `"ICC"`/`"linear"` 或改 writer 逻辑）。最小修法：在写 ICC 时把 `oiio:ColorSpace` 置空串以绕开 sRGB 分支，并在 `tests/unit/test_enc_oiio` 里断言"iCCP 存在 ⇒ 无 sRGB chunk"。
- 状态：confirmed；known=no

##### X-COMPLIANCE-06 [P1] BMP 无 ICC 槽，keep 目标下广色域像素静默变成"假定 sRGB"

- 证据：
  - `vcpkg/.../src/bmp.imageio/bmpoutput.cpp:200-234`：只写 `BITMAPFILEHEADER` + `WINDOWS_V3`（40 字节 `BITMAPINFOHEADER`），无 V4/V5、无 profile 字段（全文无 ICC/profile 命中）。
  - `src/core/pipeline.cpp:1150-1163`：`meta_path == "none"` 时只推 `MetadataDropped: bmp output carries no metadata container`；`EmbedIccUnsupported` 被 `if (ctx.cfg.color != ColorTarget::KeepOriginal)` 门控。
  - 实测：`Display P3 PNG（iCCP 580B）→ BMP`（keep）产物 `dib=40 bpp=24`、sidecar warnings 只有 `["MetadataDropped"]`，`color_src=ICC(source)`、`color_dst=keep`。
- 事实：keep 时 `icc_to_embed = src_icc`（`pipeline.cpp:883,906`），但 BMP 写不进 ICC，管线也不告警 → 文件里是 P3 数值、容器里没有任何色彩声明，读取器一律假定 sRGB（OIIO `iinfo` 实测报 `oiio:ColorSpace: "srgb_rec709_scene"`）。
- 影响：广色域源转 BMP 后颜色明显偏（P3→sRGB 解释约过饱和 20%+），且日志里只有"没有元数据容器"，用户不会想到"颜色信息被丢了"。
- 建议：两处小改：①`pipeline.cpp:1157` 的门控改为"目标色 ≠ keep **或** 存在 `icc_to_embed` 且源 ICC 非 sRGB"→ 都发 `EmbedIccUnsupported`（文案带 profile 名）；②更彻底：BMP 目标在 `color != keep` 时已转换，keep 时应在 UI 的格式卡上给出"BMP 不支持 ICC"提示（与 `allow_bitdepth` 的 note 同位）。
- 状态：confirmed；known=no（`docs/ux-review-14.md:653` 的 B3 只要求"ICC 回读测试覆盖 bmp"，并未登记"BMP 无 ICC 容器 + keep 不告警"；`docs/deep-review-0.3.2.md:301 R-090` 只谈位深口径）

##### X-COMPLIANCE-07 [P1] 16bit 源 → BMP（24bpp）没有降档告警：bpp/bps 双语义参与同一比较

- 证据：
  - `src/core/pipeline.cpp:1041-1047`：`if (ctx.info.src_bitdepth > pt.target.out_bitdepth) { ... DepthDowngrade ... }`。
  - `src/core/format_tables.cpp:868-874`：BMP `.bitdepths = {8, 24}`，注释自承"out_bitdepth 是每像素位数（bpp），其余格式是每样本位数（bps）"；`default_bitdepth_for("bmp")` = 24（`pipeline.cpp:1729-1730`）。
  - 实测：`rgb16.png（src_bitdepth=16）→ bmp`：sidecar `info.src_bitdepth=16`，`warnings=["MetadataDropped"]` —— **无** `DepthDowngrade`；同时 `16 → jpeg`（`out/C-rgb16-jpeg`）有 `DepthDowngrade`，`16 → webp`（`out/E-gray16-webp`）也有。
- 事实：16 > 24 为假 ⇒ 判断短路；BMP 实际是每通道 8 位（`enc_oiio.cpp:204-218` 无论 24/8 都写 `UINT8`）⇒ 真实发生了 16→8 的量化降档，却无任何告警。
- 影响：位深降档告警体系对 BMP 全格式失效（8/24 两个档都恒不告警）；用户以为"BMP 是无损容器，位深没变"。
- 建议：比较前先归一到 bps：`out_bps = (fmt->id=="bmp") ? 8 : out_bitdepth`（用 `pp::bitdepth_unit()`，`params.h:179-182`），或在 `OutputFormatSpec` 里存 bps 并在 UI/日志展示时再按 bpp 换算。顺手把 `bitdepth_unit_label` 用于该告警文案。
- 状态：confirmed；known=partial（`deep-review-0.3.2.md:301 R-090`、`:478 R-211` 登记了"单位双语义"，但**未**登记"降档告警漏报"这一后果）

##### X-COMPLIANCE-08 [P1] 所有降档/丢元数据/拍平告警在 GUI 完全没有显示通道

- 证据：
  - `grep WarningKind|DepthDowngrade|AlphaFlattened|EmbedIccUnsupported|MetadataDropped|MultipageTruncated src/ui/*` → **0 命中**（`src/ui` 24 个文件均无 `WarningKind`）。
  - 告警唯一出口：`pipeline.cpp:1188-1203` 的 `log_warn("output warning")` 与 sidecar（`dev_cli.cpp` 写 `.pp.json`）；PageRun 只有"日志尾 4 行"（`page_run.cpp:119,266-339`）与"0 字节/异常"胶囊（`page_run.cpp:700-757`）。
  - UI 只知道一件事：`batch_has_alpha`（`mainwindow.cpp:2011`），且只用于 AVIF 10bit+alpha 的提示（`page_output.cpp:745-766`、`format_policy.h:78-91`）；没有"位深降档"或"alpha 将被拍平"的提示（grep `batch_src_bitdepth` 0 命中）。
  - 与仓库自己的口径冲突：`AGENTS.md §4.5`「任何降档（位深/有损化）界面上**显式警示**」。
- 事实：用户在 GUI 里把 16bit 批次导出 JPEG、把 RGBA 导出 BMP、把 P3 keep 到 BMP，运行结束只看到"成功"；降档信息只存在于日志文件与 sidecar，UI 不呈现。
- 影响：静默降档（违反项目自订红线），且是 X-COMPLIANCE-06/07 这类"算错/漏判"问题得以长期存在的原因之一：即使算对了也没人看见。
- 建议：①PageRun 的输出行加"⚠ N 条告警"胶囊（数据源已有 `OutputResult::warnings`）；②`page_output` 在启动前按 `batch_has_alpha` + 目标 `supports_alpha` + 批次位深（需要新增 `set_batch_bitdepth`，探针在 `probe` 面已有 `info.src_bitdepth`）给出橙色提示行；③把 `warning→中文文案` 的映射放 `core/error_text.h` 附近的单源表（现在只有 dev_cli 的英文 kind 名）。
- 状态：confirmed；known=no

##### X-COMPLIANCE-09 [P1] 金样对"alpha 拍平"没有像素裁判

- 证据：
  - `tests/golden/smoke/alpha-jpeg.json`：`"assert": { "pixel": {}, ... }` —— `pixel` 为空对象，`pp_verify` 里 `pixel.value("mode")` 为空 → 跳过像素比较（`pp_verify.cpp:555-566`）。
  - `tests/golden/smoke/multiformat-split.json`：jpeg 腿同样 `"pixel": {}`，只有 webp 腿是 `exact`。
  - 因此 `alpha-jpeg` 唯一裁判是 `warnings_exact:["AlphaFlattened"]` + 一条 metadata。
- 事实：拍平数学（唯一会改像素的 alpha 路径）没有任何像素级断言；X-COMPLIANCE-01 因此可以长期"20/20 绿"。
- 影响：这不仅是测试缺口——它意味着**修复 01 时不会有人被金样挡住**，同时也没有正向保护，未来任何拍平回归都无人发现。
- 建议：给 `alpha-jpeg` 补 `pixel.mode = "psnr", threshold_db = 50`（JPEG 有损，不能 exact）并在 `multiformat-split` 的 jpeg 腿补同款；更稳的做法是新增一个 BMP 腿（BMP 无损，可 exact）专门钉拍平数学。参考值用"预乘/直通两式"的差分算例（如 alpha∈{0,64,128,192,255} 的 5×1 fixture）。
- 状态：confirmed；known=no

##### X-COMPLIANCE-10 [P1] `pp_verify` 的 alpha 加载语义按容器分叉，现有 exact 断言把 WebP 预乘 bug 锁死

- 证据：
  - `tools/pp_verify.cpp:19-21`：「WebP outputs are decoded with libwebp, never with OIIO: OIIO's WebP reader returns premultiplied RGB…」；`:146-150` `load_sample()` 对 `.webp` 走 `load_webp`（`WebPDecodeRGBA`，直通，`:106-144`），其它格式走 `load_oiio`（OIIO 预乘，`:77-104`）。
  - `compare_exact`（`:170-197`）逐样本比较两侧 16bit 量化值。
  - 实测：`a_ramp.png`（预乘后 `s·a`）vs 我们的 WebP 产物（libwebp 读出 `s·a`）→ `VERIFY alpha-webp OK`；而一个"正确"的 WebP（存直通 `s`）反而会与输入的预乘值不等 → FAIL。
- 事实：断言器的"参照侧"和"产物侧"用了两套 alpha 语义；当编码器写预乘时两者偶然相等，于是 bug 变成"期望行为"。修 X-COMPLIANCE-02/03/04 必须同时改这里，否则修完测试变红、按"不放宽断言"纪律会被迫回退。
- 影响：证据面失真（比缺测试更危险：它把错误钉成契约），并直接阻塞修复。
- 建议：在 `load_oiio`/`load_webp` 之后统一 alpha 归一化（推荐全部转成"直通"：对 OIIO 侧做 `/a`，`a==0` 置 0；libwebp 侧不用动），并在 `SCHEMA.md` 里写清"断言在直通空间比较"。同步更新 `webp-lossless`/`multiformat-split` 的期望（属"机械换锚"，需登记裁定）。
- 状态：confirmed；known=no

##### X-COMPLIANCE-11 [P2] 源无元数据时 TIFF 产物被注入 `DateTime=now` + XMP（并向下游扩散）

- 证据：
  - `vcpkg/.../src/tiff.imageio/tiffoutput.cpp:871-883`：「Automatically set date field if the client didn't supply it」→ `Sysutil::get_local_time` → `m_spec.attribute("DateTime", date)`；`:951-953` `encode_xmp(m_spec, true)` → `TIFFTAG_XMLPACKET`；`libOpenImageIO/xmp.cpp:105` 把 `DateTime` 映射成 `xmp:CreateDate`。
  - 本仓 `enc_oiio.cpp:221-237` 构造的 spec 不带 `DateTime`；`metadata.cpp:1023-1025` 在 `plan.exif`/`plan.xmp` 皆空时**直接 return**（所以这条注入不会被 Exiv2 覆写，而源有 EXIF 时 Exiv2 的后写会把 DateTime 换回源值并清掉注入的 XMP —— 实测对照：`out/or6-tiff/orient6.tif` 保留了源 `DateTime=2024:01:01` 且无 XMP 标签）。
  - 实测：无任何元数据的 8×4 PNG（76 B）→ TIFF 693 B，IFD0 含 `DateTime=2026:10:25 14:57:09` + tag 700 XMP **383 B**（toolkit `Adobe XMP Core 5.5`，`xmp:CreateDate` 同值）；该 TIFF 再转 JPEG 时，Exiv2 把这份 XMP 重新编码成 **2418 B** 的 APP1（产物 3069 B 的 79%）。
- 事实：`plan` 为空的批次（截图、渲染图、无 EXIF 素材）里，TIFF 输出恒被写入"当前修改时间 + XMP 创建时间"→ ①凭空造元数据（违反"中间不加料"与元数据保真）；②同一输入不同时刻运行产物字节不同（可复现性口径破）；③固定体积开销（本例 XMP 占 55%，下游再放大成 2.4 KB）。
- 影响：无 EXIF 素材批转 TIFF 时，所有产物带一个虚假的 CreateDate；用哈希/去重/时间线工具核对会得到错结论。
- 建议：`enc_oiio.cpp` 的 TIFF 分支显式设 `spec.attribute("DateTime", plan 的生效 DateTimeOriginal)`（无时间字段时传空串以阻止伪造），并在 `tests/unit/test_enc_oiio` 加断言"无元数据源的 TIFF 不含 XMP tag 或含确定值"。
- 状态：confirmed；known=no

##### X-COMPLIANCE-12 [P2] 默认位深对"体积/兼容性"有实代价且无提示：PNG 16bit、HEIF/AVIF 10bit

- 证据：
  - `src/core/pipeline.cpp:1718-1734`：`png → 16`、`heif/avif → 10`；UI 默认同源（`page_output.cpp:641` 调 `default_bitdepth_for`）。
  - 实测：8bit 源 → PNG 默认 16bit：455 B vs 显式 8bit 377 B（+21%，实测图；照片通常更高）；8bit JPEG 源 → HEIF/AVIF 默认 `uint10`（`iinfo` 报 `uint10 heif`，CICP `1,13,6,1`），即 HEVC **Main10**。
  - 无任何告警（升级不算降档，管线只在 `src > out` 时告警），UI 位深行只写"PNG 仅支持 N 位"这类**能力**说明（`page_output.cpp:646-653`）。
- 事实：默认值本身没错（"禁止降级"原则），但对 8bit 源是纯体积代价；对 HEIF/AVIF 默认 10bit 会把 8bit 内容推进 Main10（老解码器/部分系统相册不认），而 UI 的档位行默认"自动"，用户不一定会去点。
- 影响：批量转 PNG 体积明显增大（本例 +21%，照片通常 1.5–2×）；HEIF/AVIF 产物在部分目标设备打不开，且失败原因不可见。
- 建议：默认位深改为"跟随源"（`min(src_bitdepth, fmt_max)` 归一到格式支持集），或在 UI 位深行加一行灰字"源为 8 位，16 位仅增加体积"；HEIF/AVIF 在 8bit 源下把档位默认取 `Main` 并在档位行显示兼容性说明（`pp-profile-row` 已存在，`page_output.cpp:228-255`）。
- 状态：confirmed；known=partial（`docs/ux-review-14.md` §10 已登记"位深间接决定档位、UI 无档位行"，但 0.3.5 已补档位行；"默认 10bit 的兼容性代价"未登记）

##### X-COMPLIANCE-13 [P2] AVIF 无损在默认后端必然失败，报错文案指向错误原因

- 证据：
  - `src/codecs/enc_heif.cpp:614-627`：后端不暴露 `chroma` 参数时强制 420；`:875-878` 无损+420 → `fail("lossless requested but backend exposes no 'chroma' parameter; cannot guarantee 4:4:4")`。
  - 实测：`--format avif --lossless`（默认 svt-av1）失败并给出上述文案；`--backend libaom --lossless --param chroma=444` 成功且像素逐位一致（`pp_verify`/自研比对 maxdiff=0）。
  - UI 侧无损开关对 HEIF/AVIF 可见（`paramform.cpp:6-11` 按 `lossless_capable` 决定），色度行在无损时"硬联动 444"（`page_output.cpp:479-500`），但 svt-av1 的 chroma 是**运行时内省**得到的，UI 无法真的赋予它 444。
- 事实：用户在 AVIF+svt-av1 勾"无损"→ 运行期失败；文案说"后端没有 chroma 参数"，用户不知道结论是"换 libaom"。
- 影响：常见配置下的可用性缺口（无损 AVIF 是 AVIF 的主要卖点之一）；报错误导排查方向。
- 建议：在 UI 侧对 `avif ∧ lossless ∧ 后端无 chroma` 直接禁用/切换后端（复用 `evaluate_avif_alpha_preselect` 的模式，`page_output.cpp:768-784`）；`enc_heif` 的文案改成"svt-av1 不支持无损（无 4:4:4 控制），请改用 libaom 后端"。
- 状态：confirmed；known=no

##### X-COMPLIANCE-14 [P2] 色域映射默认关闭（硬裁），超色域转换无告警

- 证据：`src/core/colormanager.cpp:1000-1024` 只有 `opts.gamut_mapping != 0` 才做映射（`apply_gamut_mapping`，`:720`）；`RunConfig.gamut_mapping` 默认 0（`pipeline.h:103`），UI 默认档 0（`page_output.cpp:921`）；`colormanager` 不产生任何"超出目标色域"的 Warning。
- 事实：sRGB → P3 的转换对超出 P3 的负值/>1 值直接裁剪（lcms2 相对比色），高饱和色（纯红/纯青）会丢层次，且没有任何提示。
- 影响：广色域交付的常见失真；用户以为是编码器问题。
- 建议：变换后统计超色域样本比例，>0 时推一条新 `WarningKind`（或复用 `MetadataDropped` 不合适——建议加 `GamutClipped`）；或在 UI 把 `gamut_mapping` 默认改为"压缩映射"。
- 状态：confirmed；known=yes（`docs/ux-review-14.md` §14 表格第 14 项"色域映射缺位"，C1）

##### X-COMPLIANCE-15 [P2] RGBA PNG→PNG 8bit 不是逐位无损（预乘量化误差在低 alpha 放大）

- 证据：`enc_oiio.cpp:341-348` 把缓冲（预乘，见 01）按 8bit 量化 → OIIO `pngoutput` 再反预乘（`png_pvt.h:634-636` 设 `convert_alpha`，`:228` 调用 `deassociateAlpha`）→ `alpha` 小的像素被"先乘后除"放大误差。
- 实测：`a_ramp.png → png8 → png8`：a=64 时 `(200,100,50) → (199,100,52)`；a=32 → `(199,104,48)`；a=16 → `(207,96,48)`；a=8 → `(191,96,64)`（单通道最大偏 **14 级**）；a=0 处颜色为 `(0,0,0)`（预乘下本就无颜色信息，显示无影响）。
- 事实：无损容器 + 无损技术 + 8bit 输入 → 输出在本应无损的路径上丢精度；对半透明 PNG 素材（图标/贴图）是实质质量损失。
- 影响：用户会认为"PNG 无损"结果不无损；低 alpha 区域出现彩色噪点。
- 建议：与 01/02/03/04 一并解决（若管线改为在边界统一反预乘，本问题自动消失）；短期可在 8bit + 有 alpha 时提示"RGBA 8bit 会因 alpha 反预乘损失精度，建议 16bit"。
- 状态：confirmed；known=no

##### X-COMPLIANCE-16 [P3] 8bit 量化默认无抖动

- 证据：`pipeline.h:102` `color_dither8 = false`；`enc_oiio.cpp:343-348`、`enc_webp.cpp:245-250` 只在 `dither8` 为真时走 `quantize8_dithered`；UI 有开关但默认关（`output_page_build.cpp:335-345`）。
- 事实/影响：16→8bit 或平滑渐变源在默认配置下出现 banding（天空/棚拍背景），有损编码器会把这层台阶固化。
- 建议：保持默认关（"现行为逐字不变"），但把开关的 tooltip 写明"16bit→8bit 或多渐变图建议开"；或在 16bit 源 + 8bit 目标时自动建议。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:662` 的 C2；`docs/ux-review-14.md` §14 C2 同项）

##### X-COMPLIANCE-17 [P3] sidecar 里出现 `kind: "Unknown"`（dev 侧 kind→名字映射漏项）

- 证据：`src/dev_cli.cpp:156-157` 的 `warn_kind_name` switch 未包含 `WarningKind::EmbedIccUnsupported`（该枚举是 M7-R2A 追加的，`pipeline.h` 枚举末尾）；实测 `rgb8.png → bmp --color p3` 的 `.pp.json` 里该告警 `kind` 写成 `"Unknown"`。
- 事实/影响：sidecar 是回归/断言通道（`warnings_exact` 读的就是 kind 集合），未知 kind 会被断言面误判或漏判；`--dev` 是引擎验证入口，属证据面瑕疵。
- 建议：把 kind→字符串做成 `core/` 侧单源（dev_cli 只调用），并在 `test_smoke` 里断言"每个 kind 都有非 Unknown 名字"。
- 状态：confirmed；known=no

##### X-COMPLIANCE-18 [P3] `--lossless` 配 JPEG 静默输出有损文件

- 证据：`format_tables.cpp:284` JPEG 技术 `lossless_capable = false`；UI 因此隐藏无损行（`paramform.cpp:6-7`、`:500-509`）；但 `--dev --lossless --format jpeg` 照常成功（实测 513 B，参数表 `lossless=true`，warnings 为空），`enc_jpegli.cpp:87-101` 从不读 `lossless`。
- 事实/影响：只有 dev/CI 通道能触发；产出"标称无损、实为有损"的产物，若被脚本当基线会污染证据。
- 建议：`build_output_specs`/`validate_run_config` 对"格式无 lossless_capable 技术 + lossless=true"直接报错（与位置深校验同处），或在 dev CLI 打一条 warn。
- 状态：confirmed；known=no

##### X-COMPLIANCE-19 [P3] BMP 位深下拉的 8/24 对彩色源是同一产物

- 证据：`format_tables.cpp:868-874`、`enc_oiio.cpp:204-218`（24/8 都写 `UINT8`，实际 bpp 由通道数决定：4 通道→flatten→3 通道→24bpp；灰度→8bpp 调色板）；UI 下拉直接呈现 `8 位`/`24 位`（`page_output.cpp:600-607`），单一位深的说明只在 `allowed.size()==1` 时出现（`:646-653`），BMP 有 2 个档所以没有说明。
- 事实/影响：彩色源选 "8 位" 仍产 24bpp 文件，参数表面"不生效"。
- 建议：BMP 位深行加固定说明"8 = 8bpp（仅灰度源）/ 24 = 24bpp"，或按源通道数只留一个可选档。
- 状态：confirmed；known=yes（`deep-review-0.3.2.md:301 R-090`、`:478 R-211`）

##### X-COMPLIANCE-20 [P3] TIFF `predictor=3(float)` 暴露但必然失败；`compression=none` 时 predictor 静默失效

- 证据：`enc_oiio.cpp:269-279`：predictor 只接受 1/2/3，其中 3 直接 `fail(...)`（"libtiff's floating point predictor requires float samples"）；且 `spec.attribute("tiff:Predictor", ...)` 只在 `compression ∈ {lzw, zip}` 时写。
- 事实/影响：UI 提供三档（`format_tables.cpp:621-688` 的 predictor 枚举），选 float 必然失败；选 horizontal 但 compression=none/packbits 时静默无效果。
- 建议：predictor 行的可见/可选性按当前 compression 联动（或直接去掉 3 档，`SAMPLEFORMAT_IEEEFP` 不在本编码器输出面）；compression 非 lzw/zip 时把 predictor 行置灰并给原因。
- 状态：confirmed；known=partial（`deep-review-0.3.2.md:133 R-011` 记录过 predictor 键历史；float 档的前置拒绝是后加的明确失败，此处只登记"UI 仍暴露"）

#### 4. 输出兼容性与合规矩阵

> "标准工具可读"= OIIO 3.1.17 的 `iinfo`（本机唯一可用的独立读取工具）能打开并报出预期几何/位深；同时给纯字节结构结论。
> 所有行均有 `build/encoder-audit/compliance/out/**` 实测产物。

| 源/配置 | 产物结构（实测） | 标准工具可读 | 合规风险 |
|---|---|---|---|
| RGB8 PNG(无 ICC) → JPEG | SOI + APP1 Exif(96) + APP2 ICC（`color≠keep` 或源带 ICC 时，581–602B）+ SOF2 13 scans | 是 | 无（8bit 有损属预期）；progressive 默认开 |
| RGB16 PNG → JPEG 8 | SOF2 prec=8 | 是 | `DepthDowngrade` 有告警；无抖动 → banding |
| RGBA8 PNG → JPEG | SOF2 prec=8，3 通道 | 是 | **半透明区偏暗（X-COMPLIANCE-01）** |
| RGBA8 PNG → BMP | 24bpp、`BITMAPINFOHEADER` V3 | 是 | **同上（拍平数学）** + 无元数据容器（已告警） |
| RGBA8 PNG → PNG8 | `IHDR eXIf iCCP? sRGB gAMA cHRM oFFs IDAT` | 是 | 低 alpha 处单通道最大偏 14 级（X-COMPLIANCE-15）；有 ICC 时 sRGB chunk 冲突（05） |
| RGBA8 PNG → TIFF16 | `SamplesPerPixel=4, ExtraSamples=1(ASSOCALPHA), Predictor=2` | 是 | 自洽（预乘 + ASSOCALPHA 正确）；**源无任何元数据时**另被注入 DateTime/XMP（11） |
| RGBA8 PNG → WebP(lossless) | `VP8X(flags=0x10 alpha) VP8L` | 是 | **颜色 = 预乘当直通（02）：alpha=128 时 (200,100,50)→(100,50,26)** |
| RGBA8 PNG → JXL16(lossless) | `JXL ftyp jxll jxlc Exif?` | 是 | **`alpha_premultiplied=false` 但数据预乘（03）** |
| RGBA8 PNG → HEIF/AVIF | `ftyp heix/avif`, `meta(auxC=urn:mpeg:hevc:2015:auxid:1)`, 无 `prem` | 是 | **缺 prem 声明（04）** |
| Display P3 PNG(ICC) → PNG(keep) | `iCCP(580) + sRGB(1) + gAMA + cHRM` | 是（iinfo 同时报 ICC 与 `oiio:ColorSpace: srgb`） | **色彩声明矛盾（05）**：sRGB chunk 与 P3 ICC 并存 |
| Display P3 PNG(ICC) → BMP(keep) | `BITMAPINFOHEADER` V3，无 profile | 是 | **P3 像素无标记、无色彩告警（06）** |
| Display P3 PNG(ICC) → TIFF | `ICCProfile tag 34675 (580B)` | 是 | 正常（另加 11 的注入） |
| Display P3 PNG(ICC) → WebP | `VP8X flags=0x28 (ICC+EXIF) ICCP(580) EXIF` | 是 | 正常 |
| P3 目标（源 sRGB）→ HEIF/AVIF | `colr = [{'prof': 580B}]`（无 nclx）；CICP `1,13,6,1` | 是 | 正常（VUI 与 ICC 同源；忽略 ICC 的解码器只剩 VUI，亦一致） |
| 8bit PNG → BMP（默认 24） | 24bpp | 是 | 正常（体积 4×）；位深下拉 8/24 同效（19） |
| 16bit PNG → BMP（默认 24） | 24bpp，`UINT8` 采样 | 是 | **静默 16→8，无 `DepthDowngrade`（07）** |
| gray8 PNG → BMP | 8bpp + 256 项灰度调色板 | 是 | 正常 |
| gray16 PNG → WebP | `VP8L`（3 通道） | 是 | `GrayToRgbEncoded` + `DepthDowngrade` 均有告警（正确） |
| gray+alpha PNG → JPEG/BMP | 1 通道（BMP 8bpp 调色板） | 是 | 拍平数学同上（01）；仅 `AlphaFlattened` 告警 |
| 多页 TIFF(2 IFD) → PNG | 单页 IHDR | 是 | `MultipageTruncated` 有告警（正确） |
| Orientation=6 TIFF → JPEG | `4x8`（源 8x4）+ `Orientation: 1` | 是 | 正常（无二次旋转） |
| 任意 → TIFF(tiled 256×256) | `TileWidth/TileLength=256, Predictor=2, Compression=lzw` | 是 | 老查看器可能不认 tiled TIFF（libtiff 系可读） |
| 8bit JPEG → HEIF/AVIF（默认） | `uint10 heif`（Main10） | 是 | 兼容性代价 + 无提示（12） |
| 任意 → AVIF(lossless, 默认 svt-av1) | 无产物，`ok=0` | — | 明确失败但文案误导（13） |
| 任意 → AVIF(lossless, libaom, chroma=444) | `ftyp avif` + `av01` | 是 | 逐位一致（实测 maxdiff=0），但需手动选后端 |
| 仅元数据：JPEG / PNG / WebP | 容器重写（Exiv2） | 是 | `pp_verify` 逐位一致（JPEG/WebP 实测 OK；PNG 同为 Exiv2 重写路径） |
| 仅元数据：HEIF/AVIF | 拒绝（`format 'heif' does not support metadata-only rewrite`） | — | 明确报错；能力缺口已文档化 |

#### 5. 未覆盖与存疑

**未覆盖（能力/环境限制）**

1. **多输出（GUI 一次多格式）未能实测**：`--dev` 只接受单个 `--format`（`dev_cli.cpp` 的 `o.format = v` 覆盖式赋值），无法从 CLI 构造 2 个 target。多输出顺序/flatten 边界只做了代码审阅（`pipeline.cpp:978-1035`：`stable_sort` 把 `supports_alpha` 的 target 排前，第一个 dropping target 之前原地拍平一次），并间接由金样 `multiformat-split`（jpeg+webp 两输出）背书其结构面。
2. **取消语义未实测**：只有代码路径证据（`pipeline.cpp:786-789, 843-847, 1036-1039, 1098-1111` + 各编码器行级 `req.cancelled`）。仓库自述"场景矩阵 R2 A6 实测取消语义通过"，本次未复跑。
3. **没有第三方工具**（exiftool / magick / ffmpeg / heif-info / djxl 均不在 PATH），所以"标准工具可读"只能用 OIIO `iinfo` + 纯字节解析来判；HEIF `prem`、JXL `alpha_premultiplied` 的**实际显示效果**无法在本机用外部解码器复现，结论按规范语义推定。
4. **GUI 实际渲染**未观察（只读审计，未跑 `--ui-smoke`）；X-COMPLIANCE-08 的"UI 无告警显示"结论基于 `src/ui` 全量 grep 与 PageRun 代码，不含真实截图。
5. 未测：CMYK/JPEG CMYK（负面清单，probe 已拒）、RAW/DNG、GIF 输出（不支持）、EXR/浮点源、HDR(PQ/HLG) 路径。

**存疑（需进一步确认）**

1. **HEIF/AVIF 带 ICC 时容器只剩 `colr='prof'`**（`out/P-heif-p3`）：`heif_image_set_nclx_color_profile` 先设、`heif_image_set_raw_color_profile` 后设，libheif 只写了一条 `prof`。ICC 与 VUI（`output_nclx_profile`）同源推导，忽略 ICC 的解码器仍能拿到一致的 CICP，故未列为发现；但"HEIF 里应该几条 colr"的规范解读建议在下一轮用 `heif-info` 复核。
2. **JXL ICC 内嵌位置**（codestream 而非 box）无法用现有解析确认，仅从 `JxlEncoderSetICCProfile` 语义推断；JXL 产物里没有 `jbrd`/`ICC` box 属正常。
3. **X-COMPLIANCE-04 的"读者会偏暗"程度**取决于解码器是否读 `prem`（缺失 ⇒ 视为非预乘）；若某解码器默认假定预乘，则结果反而正确。文件事实（预乘数据 + 无 `prem`）是确定的，显示后果按规范推定。
4. `TIFF DateTime/XMP` 注入是否影响 `regression.py`：回归只比对日志/stdout 文本且 N1–N10 已规范化时间戳与体量，故未观察到回归影响；但"产物非确定性"这一事实未经跨小时两次运行对比（本次两次运行相隔 <1 分钟，只核到值等于运行时刻）。
5. `png_set_sRGB_gAMA_and_cHRM` 与 `png_set_iCCP` 并存时 libpng 到底写哪个优先，取决于 `png_write_info` 内部顺序；本机实测两者都写进了文件（chunk 序列可证），但不同 libpng 版本可能报警/丢弃，需在 CI 的 libpng 版本上复核一次。

**实测命令（可复核）**

```powershell
# 1) alpha 关联性探针：解码后的工作缓冲 = 预乘（R = 200*257, 151*257, 100*257 …）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev `
  build/encoder-audit/compliance/in/a_ramp.png `
  --out build/encoder-audit/compliance/out/tiff16 --format tiff --bitdepth 16 `
  --param compression=none --param predictor=1 --workers 1
python build/encoder-audit/compliance/containers.py build/encoder-audit/compliance/out/tiff16/a_ramp.tif

# 2) 拍平数学：BMP = 二次乘 alpha（`(177,119,92)`）而非正确合成（`(214,138,101)`）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev `
  build/encoder-audit/compliance/in/a_ramp.png --out build/encoder-audit/compliance/out/bmp --format bmp --workers 1

# 3) WebP alpha：PNG→WebP→PNG 后 (200,100,50,192) 变 (151,74,39,192)
python tools/env.py run -- build/release-dev/photopipeline.exe --dev `
  build/encoder-audit/compliance/in/a_ramp.png --out build/encoder-audit/compliance/out/rt-webp --format webp --lossless --workers 1
#    再以上一步产物为输入转回 png8，用 containers.py 的 png_pixels 比对

# 4) P3 → BMP（keep）：无 ICC、无色彩告警
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png `
  --out build/encoder-audit/compliance/out/A-p3-src --format png --bitdepth 8 --color p3 --workers 1
python tools/env.py run -- build/release-dev/photopipeline.exe --dev `
  build/encoder-audit/compliance/out/A-p3-src/rgb8.png `
  --out build/encoder-audit/compliance/out/B-p3-bmp-keep --format bmp --workers 1

# 5) 16bit → BMP 无 DepthDowngrade
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb16.png `
  --out build/encoder-audit/compliance/out/D-rgb16-bmp --format bmp --workers 1

# 6) 仅元数据模式逐位一致（JPEG/PNG/WebP 均 OK；HEIF 明确拒绝）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/meta/exif_full.jpg `
  --out build/encoder-audit/compliance/out/MO-jpg --metadata-only --format jpeg --workers 1
python tools/env.py run -- build/release-dev/pp_verify.exe `
  build/encoder-audit/compliance/verify/jpg/case/expected.json `
  build/encoder-audit/compliance/out/MO-jpg/exif_full.jpg

# 7) 金样为何没抓到 WebP 预乘：同一条 exact 断言在"直通空间"两侧偶然相等
python tools/env.py run -- build/release-dev/pp_verify.exe `
  build/encoder-audit/compliance/verify2/alpha-webp/case/expected.json `
  build/encoder-audit/compliance/out/rt-webp/a_ramp.webp    # -> VERIFY alpha-webp OK
```

### 6.5 仅元数据通路（像素零改动）

> 审计对象：`run_metadata_only`（`src/core/pipeline.cpp:1288`）→ `build_plan`（`src/core/metadata.cpp:817`）→
> `rewrite_metadata_only`（`src/core/metadata.cpp:1132`）→ `exiv2_apply{,_atomic}`（`src/core/metadata.cpp:365/401`）→
> `fsops` 原子提交（`src/core/fsops.cpp:535-585`）；以及 GUI（`src/ui/mainwindow_run.cpp`、`src/ui/page_output.cpp`、
> `src/ui/output_page_build.cpp`、`src/ui/meta_rules.cpp`）与 `--dev`（`src/dev_cli.cpp`）两条入口。
> 只读审计：未修改 `src/`、`tests/`、`docs/`、CMake；脚本与产物全部在 `build/encoder-audit/meta/`。
> 实测命令一律经 `python tools/env.py run -- ...`；逐容器字节解析脚本 `build/encoder-audit/meta/container_scan.py`
> （JPEG marker+entropy / PNG chunk+CRC / TIFF IFD+strip+tile / RIFF / ISO-BMFF / JXL box / BMP，纯 stdlib），
> 每例另跑 `pp_verify`（`assert.pixel.mode=exact` = 16bit 量化逐位一致）做第二证据。

#### 1. 结论（3-8 条，直说有没有问题）

1. **像素零改动成立**：8 个容器里真正支持仅元数据的 4 个（JPEG/PNG/TIFF/WebP）共 21 组实测，**压缩码流逐字节不变**
   （JPEG SOS 熵数据、PNG IDAT 拼接、TIFF strip/tile 载荷、WebP VP8/VP8L/ALPH 载荷；见 §3.0 表），
   且每组都跑了 `pp_verify`（exact 模式，逐例 `VERIFY ... OK`；两例手工补验见 04）。TIFF（LZW/多页）与 WebP（VP8X 齐备）的"无规则 pass-through"
   甚至整文件字节相同；JPEG 的 pass-through 只差 COM 段位移（见 09）。
   → **没有"静默把重编码当仅元数据"的 P0**。
2. **两个历史 P0 均已修复并被本次复核证实**：`out==src` 在 `same_output_as_source`（`pipeline.cpp:380-397`）拦下
   （三种 conflict 策略实测均拒绝、源 SHA 不变）；Exiv2 就地截断改为"临时副本 + rename"且写失败 `ok=false`
   （`metadata.cpp:401-427`、`1132-1160`；人为制造 commit 失败 → `failed`、无残留 tmp、目标未动）。
3. **但"零改动"只在像素层成立，元数据层不是字节保真**：无规则 pass-through 也会被 Exiv2 规范化——
   第三方 XMP 报文 852 B → 2695 B（`xmptk` 改写、xpacket padding 丢失）、PNG `tEXt(Description)` 变压缩 `iTXt` 且
   `eXIf`/`iCCP` 前移、JPEG `COM` 段前移并补 NUL、无 VP8X 的 WebP 被**注入 VP8X**（+18 B）。
   对"只替换容器内元数据"的承诺（`design.md:253-259`）是成立的，对"文件字节不变"不成立——需要文档/UI 说清。
4. **GUI 与 `--dev` 的仅元数据语义不完全一致**：GUI 把冲突策略**静默改成 Skip**（`mainwindow_run.cpp:322-323`），
   而"同名冲突"下拉仍显示用户选择（`page_output.cpp:789-815` 未触碰 `conflict_combo`）；`--dev` 默认 overwrite
   （`dev_cli.cpp:671`）且 `--conflict` 生效。→ 同一模式两条入口"覆盖/跳过"语义相反（known：X-WIRING-10）。
5. **隐私剥除只清 EXIF+XMP**：JPEG 的 `COM`、`APP15` 等未知段、以及 **IPTC/APP13** 全部留存（`metadata.cpp:1274-1279`
   只 `exif.clear()/xmp.clear()`；Exiv2 写回时按原 `psBlob` 重写 APP13）。实测："隐私剥除"后 ICC/APP15/COM 仍在产物里。
6. **`out_root` 落在输入树内会被下一轮收集成输入 → 规则二次施加**：实测同一时间偏移跑两轮得到
   `2024:03:03 14:00:00`（+2 天 4 小时）。`fsops::collect_inputs` 已有排除集重载（`fsops.cpp:439-445`）但
   UI/CLI 都没用（`filelistmodel.cpp:201`、`dev_cli.cpp:872`）。known：R-031（本次升级为"静默改错元数据"）。
7. **HEIF/AVIF/JXL/BMP 一律显式拒绝**（每文件 `ok=false`、不产生产物、无输出残留），拒绝理由正确
   （`metadata.cpp:1112-1115` 白名单 + `bmffimage.cpp:779-782` Exiv2 BMFF 写一律抛不支持）；要改这些容器的元数据
   只能走转码（实测 HEIC 转码后 `mdat` SHA 改变 = 重编码，元数据可写但像素变了）。
8. **能力边界外的失败是"显式失败"而非静默降级**——这一点做得对；遗留问题是失败原因层：仅元数据仍强制
   OIIO `probe`（`pipeline.cpp:1351-1354`），因此 CMYK TIFF、截断 JPEG 这类"纯元数据本不需要解码"的源被拒，
   且错误文案带 `vcpkg/buildtrees/libjpeg-turbo/...` 构建路径。

#### 2. 参数全量矩阵

> 仅元数据模式**不消费任何编码参数**（`cfg.outputs[0].format_id/params` 只作配置容器，引擎按源扩展名解析格式：
> `pipeline.cpp:1334-1344`、`1702-1716`），所以本矩阵列的是"决定仅元数据产出"的参数面：
> 模式/路径/冲突 + `BatchRules` + 相关全局设置。`生效` 列 = 该键在仅元数据通路里是否真正改变产物。

| key | UI 暴露 | 默认 | 范围/枚举 | 读取点 | 生效 | 联动/备注 |
|---|---|---|---|---|---|---|
| `metadata_only` | 输出页「转换模式」分段控件（转码/仅元数据） | false | 枚举 {转码, 仅元数据} | `pipeline.cpp:619-621`、`1654-1655`、`mainwindow_run.cpp:322` | 是 | 与 `outputs.size()==1` 互斥（`--dev` 多输出直接 exit 2）；**不进预设**（known X-PARAM-ENGINE-08） |
| `out_root` | 输出页「输出根目录」（绝对路径） | `settings.last_out_root`（`settings.cpp:135`） | 绝对路径 | `page_output.cpp:901-904`、`pipeline.cpp:1376` | 是 | 仅元数据也必须给（`page_output.cpp:1084-1088` 硬阻断）；落在输入树内会触发 §3 的 01 |
| `output_template` | 输出页「路径模板」 | `$format/$dir/$file`（`output_page_build.cpp:394-397`） | `$format/$dir/$file/$stem` … | `fsops.cpp:render_output_path`（`pipeline.cpp:1376`）、`path_ctx_for` `pipeline.cpp:1702-1716` | 是 | 仅元数据下 `$format`=**源格式 id**、`ext`=源扩展名（`pipeline.cpp:1704-1708`） |
| `conflict` | 输出页「同名冲突」下拉（自动加序号/跳过/覆盖） | rename（`output_page_build.cpp:242-247`）；`--dev`=overwrite（`dev_cli.cpp:671`） | 枚举 rename/skip/overwrite | `fsops.cpp:302-315`；GUI 覆盖于 `mainwindow_run.cpp:322-323` | 条件 | GUI 仅元数据恒 Skip（下拉被忽略）→ 见 02；`--dev` 尊重 `--conflict` |
| `rules.time_enabled` | 元数据页时间卡「启用」 | true | bool | `meta_rules.cpp:91`、`metadata.cpp:484-485` | 是 | 关闭 = 整卡不装配 |
| `rules.time_shift.mode` | 元数据页「Δ 偏移 / 时区语义」分段控件 | delta | 枚举 {delta, timezone} | `metadata.cpp:846-866`、`preset_io.cpp:336-345` | 是 | 时区语义才写 `OffsetTime*`（`metadata.cpp:853-864`） |
| `rules.time_shift.{years,months,days,hours,minutes,seconds}` | 元数据页 Δ 数字框 | 0 | int（日历步进 + 精确时长，`metadata.cpp:195-224`） | `preset_io.cpp:346-351`、`metadata.cpp:656-693` | 是 | 同时改 3 个 EXIF 时间键 + `Xmp.xmp.CreateDate/ModifyDate`（实测） |
| `rules.time_shift.{from_offset_min,to_offset_min}` | 元数据页「从/到」时区 | 0 | ±(1439) 分钟 | `metadata.cpp:747-765`、`533-555` | 是 | 墙钟实际改写成功才写 `OffsetTime*`（R-234） |
| `rules.gps_enabled` | 元数据页 GPS 卡「启用」 | true | bool | `meta_rules.cpp:92`、`metadata.cpp:486-489` | 是 | 关闭 = 整卡不装配 |
| `rules.gps.lat` / `lon` | 元数据页经纬度输入 | 无（nullopt） | [-90,90] / [-180,180] | `meta_rules.cpp:104-107`、`metadata.cpp:841-843` | 是 | 非法坐标 → 忽略 gps 只出黄字（`meta_rules.cpp:116`） |
| `rules.gps.altitude` | 元数据页海拔 | 无 | [-100000,100000] | `meta_rules.cpp:108`、`metadata.cpp:1211-1220` | 是 | 缺省时删旧 `GPSAltitude(Ref)` |
| `rules.gps.direction` | 元数据页方向 | 无 | [0,359.99] | `meta_rules.cpp:109`、`metadata.cpp:1222-1231` | 是 | 写 `GPSImgDirectionRef=T` |
| `rules.gps.timestamp` | 元数据页 GPS 时间 | 无 | `YYYY:MM:DD HH:MM:SS` | `meta_rules.cpp:110-113`、`metadata.cpp:1233-1257` | 条件 | 非法格式 → **保留**源 GPSDateStamp/TimeStamp + Warning（R-029） |
| `rules.gps_clear` | 元数据页「一键清除 GPS」 | false | bool | `meta_rules.cpp:100-101`、`metadata.cpp:839-840` | 是 | 优先于 `gps`；实测删除整套 GPSInfo |
| `rules.exif_edits[]` | 元数据页「标签编辑」表 | 空 | `{key,value,remove}`，key 必须 `Exif.` | `meta_rules.cpp:120-137`、`metadata.cpp:868-876`、`1285-1371` | 是 | 空值 = 删除（`meta_rules.cpp:128-130`）；非法 key/类型 → `Warning{MetadataDropped}`（R-084） |
| `rules.xmp_edits[]` | 同上 | 空 | key 必须 `Xmp.` | `meta_rules.cpp:133-135`、`metadata.cpp:1329-1370` | 是 | `Xmp.xmp:CreateDate` 形态经 `normalize_xmp_key` 归一（`metadata.cpp:82-91`） |
| `rules.strip_privacy` | 元数据页「隐私剥除」开关 | false | bool | `meta_rules.cpp:139`、`metadata.cpp:832-837` | 是 | 只清 EXIF+XMP（ICC/像素保留；IPTC/COM/APPn 留存 → 03） |
| `rules.privacy_enabled` | 元数据页隐私卡「启用」 | true | bool | `metadata.cpp:490-491` | 是 | **`strip_privacy`/`sync_mtime`/标签编辑都挂在这张卡下**（`meta_rules.cpp:119-141`） |
| `rules.sync_mtime` | 元数据页「时间同步」开关 | false | bool | `meta_rules.cpp:140`、`pipeline.cpp:1446-1452` | 条件 | 需 effective DateTimeOriginal 非空；失败仅 `log_warn`（ok 仍 true）→ 13 |
| `MetadataOverride.ignore_batch` | 元数据编辑器「忽略批量规则」 | false | 三态 | `metadata.cpp:477-505`（`effective_rules`） | 是 | 例外逐项覆盖批量；与 `build_plan` 同源 |
| `settings.rotate_orientation` | 设置页「EXIF 方向」勾选（`settings_dialog.cpp:1263-1271`） | true | bool | `mainwindow_run.cpp:312`、`settings.cpp:132` | 否 | 仅元数据不旋转像素（`pipeline.cpp:1364-1366` `shared_skip(Orient)`）→ 14 |
| `color`（色彩目标） | 输出页下拉，**仅元数据下置灰** | keep | keep/srgb/p3/adobergb | `page_output.cpp:789-804`、`916`；`pipeline.cpp:1157` | 否 | 置灰 + tooltip 说明（`page_output.cpp:799-803`），语义正确 |
| `settings.{workers,thread_budget,stagger_ms,budget_gb}` | 设置页 | 见 `settings.cpp:122-138` | — | `scheduler.cpp:418-419` | 条件 | 无解码/编码 ⇒ `budget` 不 acquire、`encode_threads` 空转（侧车 `decode_ms/encode_ms/color_ms/orient_ms` 恒 0） |
| 格式参数（`params`/`__lossless`/位深/后端/技术） | 输出页格式卡，**仅元数据下整卡置灰** | — | — | `page_output.cpp:906-915`（仍下发 1 项） | 否 | 引擎仅用其满足"outputs.size()==1"；`$format`/扩展名/能力校验全走源扩展名 |

#### 3. 发现（按 P0→P3；每条一个三级标题）

> 本节所有"实测"均在 `build/encoder-audit/meta/` 下可复现；证据表（码流 SHA256）附在 3.0。

##### 3.0 逐容器字节级实证表（码流 = 像素载荷）

命令模板（每例一条，`--out` 为 `build/encoder-audit/meta/<容器>/<case>`）：

```
python tools/env.py run -- build/release-dev/photopipeline.exe --dev <输入> --out <目录> \
    --format jpeg --metadata-only [--preset <preset.json> | --meta K=V] --workers 1
python build/encoder-audit/meta/container_scan.py diff <输入> <产物>
python tools/env.py run -- build/release-dev/pp_verify.exe <root>/case/expected.json <产物>
```

| 场景 | 码流字节 | SHA256(前 16) before | SHA256(前 16) after | 码流不变 | 整文件字节相同 | pp_verify |
|---|---|---|---|---|---|---|
| JPEG pass-through（EXIF+XMP+ICC+COM+APP15+缩略图+MakerNote） | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no（COM 挪位） | `VERIFY jpeg3-passthru OK` |
| JPEG 时间偏移（Δ+1d2h） | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no | `VERIFY jpeg3-time OK` |
| JPEG GPS 写入（南纬/东经/海拔/方向/时间戳） | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no | `VERIFY jpeg3-gps OK` |
| JPEG 隐私剥除 | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no | `VERIFY jpeg3-strip OK` |
| JPEG 删标签（Artist/MakerNote/XMP title） | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no | `VERIFY jpeg3-remove OK` |
| JPEG 第三方 XMP packet | 531 | `f230ca7d094b1567` | `f230ca7d094b1567` | YES | no（XMP 852→2695 B） | `VERIFY jpeg-xmp-thirdparty OK` |
| JPEG XMP 日期偏移（+1d2h） | 115 | `425cc6563cdeda90` | `425cc6563cdeda90` | YES | no | `VERIFY jpeg-xmpdate-time OK` |
| PNG pass-through | 202 | `dc081de90b59b0d5` | `dc081de90b59b0d5` | YES | no | `VERIFY png-passthru OK` |
| PNG 时间偏移 | 202 | `dc081de90b59b0d5` | `dc081de90b59b0d5` | YES | no | `VERIFY png-time OK` |
| PNG GPS 写入 | 202 | `dc081de90b59b0d5` | `dc081de90b59b0d5` | YES | no | `VERIFY png-gps OK` |
| PNG 隐私剥除 | 202 | `dc081de90b59b0d5` | `dc081de90b59b0d5` | YES | no | `VERIFY png-strip OK` |
| TIFF LZW pass-through | 1302 | `ac1d9e4cca64bfec` | `ac1d9e4cca64bfec` | YES | **YES** | `VERIFY tif-lzw-passthru OK` |
| TIFF LZW 时间偏移 | 1302 | `ac1d9e4cca64bfec` | `ac1d9e4cca64bfec` | YES | no | `VERIFY tif-lzw-time OK` |
| TIFF ZIP+32×32 分块 时间偏移 | 356 | `7c1af7ac18b69f9f` | `7c1af7ac18b69f9f` | YES | no | `VERIFY tif-ziptile-time OK` |
| TIFF 隐私剥除 | 1302 | `ac1d9e4cca64bfec` | `ac1d9e4cca64bfec` | YES | no | `VERIFY tif-lzw-strip OK` |
| TIFF 多页（2 IFD）pass-through | 246 | `bb06234e6a2d396c` | `bb06234e6a2d396c` | YES | **YES** | `VERIFY tif-multipage-passthru OK` |
| TIFF 多页 加 Artist | 246 | `bb06234e6a2d396c` | `bb06234e6a2d396c` | YES | no（2 IFD 保留） | `VERIFY tif-multipage-edit OK` |
| WebP pass-through（VP8X/ICCP/VP8/EXIF/XMP） | 88 | `5a1da3686f360eae` | `5a1da3686f360eae` | YES | **YES** | `VERIFY webp-passthru OK` |
| WebP 时间偏移 | 88 | `5a1da3686f360eae` | `5a1da3686f360eae` | YES | no | `VERIFY webp-time OK` |
| WebP 有损（无 VP8X）pass-through | 306 | `ebf4f33678e0be75` | `ebf4f33678e0be75` | YES | no（+VP8X 18 B） | `VERIFY webp-lossy-passthru OK` |
| WebP 无损（无 VP8X）pass-through | 50 | `8df7d83baf4c88c1` | `8df7d83baf4c88c1` | YES | no（+VP8X 18 B） | `VERIFY webp-lossless-passthru OK` |

> 注：第三方 XMP packet 那一例的 `VERIFY` 是同一夹具上另跑的 `jpeg-xmpdate-time`（XMP 日期偏移）结果；
> XMP packet 本身用 `craft_xmp.py` 构造后单独比对（见 04）。
> 码流不变的前提下，`pp_verify` exact 与会话记录中全部 20 例 `VERIFY ... OK` 一致；失败只出现在我写错断言的两次
> （`Orientation` 的 Exiv2 渲染是 `right, top`、XMP `dc:title` 是 `lang="x-default" ...`），改断言后 OK。

##### X-METADATA-ONLY-01 [P1] `out_root` 在输入树内时，输出会被下一轮收集为输入 → 规则二次施加（时间偏移叠加）

- 证据：`src/core/pipeline.cpp:1381-1383`（只拒绝 `out==src`；`out/out/...` 不拒绝）、`src/core/pipeline.cpp:1375-1376`
  （`path_ctx_for` + `render_output_path` 按 `rel_dir` 保形）、`src/ui/filelistmodel.cpp:201`
  （`base_dir = is_inside(src, base) ? base : src.parent_path()`）、`src/core/fsops.cpp:439-445`
  （排除集重载存在但默认 `kNoExclude`）、`src/dev_cli.cpp:872`（收集时不传排除集）。
- 事实：输入 `d/sub/case.jpg`、`out_root=d/out`、规则 Δ+1d2h，第一轮产物 `d/out/sub/case.jpg`；
  第二轮把 `d` 重新收集（`--base d` 复现 GUI 的 base_dir 语义）→ `d/out/sub/case.jpg` 被当作输入，
  产出 `d/out/out/sub/case.jpg`，实测时间变成 `2024:03:03 14:00:00`（= +2 天 4 小时）。
  纯 `--dev` 不带 `--base` 时因 `rel_dir` 退化为空而恰好被 `out==src` 守卫挡住（实测一次），
  也就是**GUI 的 base_dir 语义才把危险形状打开**。
- 影响：时间偏移/GPS/剥除被重复施加，产物元数据错而状态是 `ok`；用户以为只是"再跑一遍"。
- 建议：`filelistmodel` 收集与 `--dev` 收集都传入 `out_root` 作为排除根（`collect_inputs(..., {out_root}, ...)`），
  并在 `on_start` 对 `is_inside(out_root, 输入根)` 出一句硬提示（"输出目录位于输入目录内，下次收集会包含产物"）。
- 状态：confirmed；known=yes（`docs/deep-review-0.3.2.md:214` R-031，原判 P2；本次实测到"元数据被改错"，
  按"数据后果"升级 P1）。

##### X-METADATA-ONLY-02 [P1] GUI 仅元数据静默强制冲突策略 Skip，下拉仍显示用户选择；`--dev` 同模式走 overwrite

- 证据：`src/ui/mainwindow_run.cpp:322-323`
  `if (cfg.metadata_only) cfg.conflict = pp::ConflictPolicy::Skip;`；
  `src/ui/page_output.cpp:789-815`（`update_metadata_only_state` 只置灰 `tiles_host`/`param_card`/`color_combo`，
  不碰 `conflict_combo`）；`src/ui/output_page_build.cpp:235-254`（下拉默认"自动加序号"，仍可点）；
  `src/ui/page_output.cpp:924`（`config_base` 直接取下拉值）；对比 `src/dev_cli.cpp:671`（`--dev` 默认 Overwrite）
  与 `fsops.cpp:302-315`（Skip 命中即跳过、不写）。
- 事实：GUI 选「覆盖」→ `RunConfig.conflict=Skip` → 已存在的产物保持旧元数据、行状态 `skipped`；
  `--dev` 同参数下**真的覆盖**（实测 overwrite/skip/rename 三档行为与文档一致）。
- 影响：用户"改了时间规则再跑一遍"得到的是旧文件（状态 skipped 但下拉写着"覆盖"），以为已写入；
  CLI 取证与 GUI 结论相反。
- 建议：`update_metadata_only_state` 一并把 conflict 行置灰 + tooltip「仅元数据模式恒跳过（不覆盖已有文件）」，
  并让 `conflict()` 在该模式下回读生效值（`page_output.cpp:134-139`）；若确实需要覆盖，给"仅元数据也允许覆盖"的显式开关。
- 状态：confirmed；known=yes（`docs/encoder-config-audit-2026-10-25.md:3187` X-WIRING-10，原判 P2；
  `docs/m7-adjudications.md:87` B35 记录 R-074 的"仅 GUI 默认 Skip"裁定）。

##### X-METADATA-ONLY-03 [P2] 隐私剥除只清 EXIF+XMP：IPTC/COM/未知 APPn/PNG 文本块全部留存

- 证据：`src/core/metadata.cpp:1274-1279`（`strip_privacy` 只有 `exif.clear()`/`xmp.clear()`）；
  `src/core/metadata.cpp:1132-1160` → `exiv2_apply`（`metadata.cpp:377-386` 只 clear/set EXIF 与 XMP）；
  Exiv2 0.28.9 JPEG 写回：`vcpkg/buildtrees/exiv2/src/v0.28.9-5bd1622bc7.clean/src/jpgimage.cpp:846-887`
  （IPTC/APP13 按 `psBlob` 重写而非删除）、`891-911`（COM 原样写回）；
  PNG 侧 `pngimage.cpp:659-675`（不匹配元数据关键字的 `tEXt` 原样拷贝）。
- 事实：实测"隐私剥除"的 JPEG 产物仍含 `APP2/ICC`（应保留）、`APP15`（未知段，payload SHA 不变）、
  `COM("crafted comment")`；PNG 产物仍含 `tEXt(Comment)`/`tEXt(Software)` 与 `iCCP`。
  IPTC/APP13 本次没有 IPTC 夹具，按上述 Exiv2 代码路径判断同样留存（未删除 IPTC 的唯一入口）。
- 影响：把"隐私剥除"当分享前处理的用户会泄漏 IPTC 里的作者/城市/版权，以及 JPEG 注释/厂商私有段。
  UI 文案本身写的是「剥除全部 EXIF / XMP（保留 ICC 与像素）」（`src/ui/exif_editor.cpp:1522`），所以属提示缺口而非谎报。
- 建议：把 privacy strip 扩到"元数据全清"：JPEG 追加 `clearIptcData()`+`clearComment()`（并在 tooltip 写明），
  PNG 追加清 `tEXt/zTXt/iTXt`（除 XMP 已在 xmp 面）；或至少把 UI 文案改成「仅 EXIF/XMP；IPTC 与注释不会清除」。
- 状态：confirmed（COM/ICC/APP15/tEXt 实测；IPTC 为代码路径推断）；known=no。

##### X-METADATA-ONLY-04 [P2] 无规则 pass-through 也会重写第三方 XMP 报文（852 B → 2695 B，`xmptk` 被改写）

- 证据：`src/core/metadata.cpp:1008-1011`（`write_metadata_exiv2` 把容器直接交给 Exiv2）/`1132-1160`；
  Exiv2 `image.cpp:610-613`（`setXmpData` 置 `writeXmpFromPacket(false)`）+ `jpgimage.cpp:781-806`
  （`XmpParser::encode(...)` 后整段重写 APP1 XMP）。
- 事实：手写 XMP packet（自定义命名空间 `ppaudit:`、`rdf:Alt`、200 B padding、`x:xmptk="PP-Audit/1.0"`）
  经仅元数据 pass-through 后：属性值全部保留（`pp_verify` 断言 `Xmp.dc.title`/`Xmp.xmp.CreatorTool`/
  **自定义命名空间** `Xmp.ppaudit.customField=custom-value-42` 均读回一致，`VERIFY jpeg-xmp-thirdparty OK`），
  但 packet 由 852 B 变 2695 B，`xmptk` 变 `XMP Core 4.4.0-Exiv2`，padding 丢弃；文件 3677 → 5520 B。
  同一路径对 XMP 日期的时间偏移是正确的（`Xmp.xmp.CreateDate/ModifyDate` 2020-01-02T03:04:05+08:00
  → 2020-01-03T05:04:05+08:00，offset 保留，`VERIFY jpeg-xmpdate-time OK`）。
- 影响：对"元数据手术台"用户可接受（值未丢），但对"仅元数据=不该动别的"的预期不符：体积上涨、
  第三方工具的 in-place padding 失效；与 3.0 表里"整文件字节相同"的那些例子的差别正在这里。
- 建议：在 `design.md §5.5` 与 UI 说明里明确"XMP 会被 Exiv2 规范化（值不变、报文与体积可能变化）"；
  若要字节保真，写回时用 `setXmpPacket(原报文)`（Exiv2 支持 packet 优先）而不是 `setXmpData`。
- 状态：confirmed；known=partial（`docs/encoder-config-audit-2026-10-25.md:784` 已记"XMP 被规范化为完整 packet/
  体积上涨"，但那条是 PNG 转码路径；仅元数据 pass-through 的同一现象与体积数字未记录）。

##### X-METADATA-ONLY-05 [P3] 无 VP8X 的 WebP 在"纯 no-op"仅元数据下也被注入 VP8X（+18 B）

- 证据：`vcpkg/buildtrees/exiv2/src/v0.28.9-5bd1622bc7.clean/src/webpimage.cpp:283-336`
  （`/* Inject a VP8X chunk if one isn't available. */` 后写 10 B VP8X 并按 has_icc/has_xmp/has_exif 改 flags）。
- 事实：`webp_lossy.webp`（仅 `VP8 `，326 B）→ 产物 `VP8X+VP8 `（344 B）；`webp_lossless.webp`（仅 `VP8L`，70 B）
  → `VP8X+VP8L`；VP8/VP8L payload SHA 不变（`ebf4f33678e0be75`/`8df7d83baf4c88c1`），像素逐位一致（`VERIFY OK`）。
- 影响：明明是"没有任何元数据要写"的空跑，产物也变了（结构合法、可读，但文件指纹/体积变了）；
  对做哈希核对或写保护分发的用户是意外。**注意**：一旦真的要写 EXIF/XMP，注入 VP8X 并置 flag 是 WebP 容器规范
  要求的（`webpimage.cpp:303-336`），所以问题只在"空 plan 也走重写"这一条。
- 建议：`rewrite_metadata_only` 在 `plan.exif.empty() && plan.xmp.empty()` 且无 `strip_privacy` 时直接
  `copy_file(src,out)` 短路（与 `write_metadata_exiv2` 的空 plan 早退同口径，`metadata.cpp:1023-1025`）。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-06 [P2] 仅元数据写回零校验：PNG `eXIf` 落地检查只在转码后写路径

- 证据：`src/core/metadata.cpp:1030`（`write_metadata_exiv2` 用 `key_exif_landed` 复核 PNG 的 EXIF 是否真落地）、
  `metadata.cpp:430-456`（`key_exif_landed` 定义，全文仅此一处调用）；而 `rewrite_metadata_only`
  （`metadata.cpp:1132-1160`）写完只 `commit_temp_file`，没有任何读回；`pipeline.cpp:1433-1443` 也只处理错误串。
- 事实：exiv2 历史上就出现过"PNG eXIf 写入被静默丢弃"（R1，`metadata.cpp:11-15` 注释与 `1033-1050` 的镜像兜底），
  该兜底只在转码路径；仅元数据路径若遇到同类静默丢弃 → `ok=true` 而元数据没写进去。
- 影响：静默丢元数据仍报成功（当前 vcpkg exiv2 0.28.9 实测写入正常，属**防线缺口**而非现网故障）。
- 建议：`rewrite_metadata_only` 在 `is_png` 时复用 `key_exif_landed`（或对四容器统一"抽查 1–3 个改动键读回"），
  失败 → 清场 + `ok=false`。
- 状态：confirmed（代码路径）；known=no。

##### X-METADATA-ONLY-07 [P2] 仅元数据仍强制 OIIO `probe`：CMYK TIFF / 截断 JPEG 等纯元数据可处理的源被拒，且错误文案泄漏构建路径

- 证据：`src/core/pipeline.cpp:1351-1354`（`probe_file(fe.src)`，失败即 `fail`）、`1357-1358`（只用 `info.width/height` 建
  `ProgressMux`）；`src/core/pipeline.cpp:1364-1367`（解码/方向/色彩全部 `shared_skip`，即 probe 只为进度与行信息）。
- 事实：`cmyk.tif` → `probe failed: CMYK input is not supported`（`failed`，无产物）；`corrupt_trunc.jpg` →
  `probe failed: JPEG error: C:\Data\Code\PhotoPipeline3\vcpkg\buildtrees\libjpeg-turbo\src\56c1f52a20-f26ed (...)`；
  真正的元数据读（`read_metadata`，Exiv2）根本没被调用。
- 影响：印刷/扫描常见 CMYK TIFF 无法做仅元数据清理；错误文案把构建树绝对路径暴露给用户（截图/工单泄漏）。
- 建议：仅元数据把 `probe` 降级为"尽力而为"：失败时用 `read_metadata` 的 width/height 或 0×0 建进度，
  仅在 Exiv2 也读不出时才失败；`error_text_zh` 增加"probe 失败但仅元数据可继续"的旁路，并清洗 OIIO 错误里的绝对路径。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-08 [P2] UI 文案「仅元数据 = 原地写入」与实际（输出到 out_root、源只读、in-place 被引擎拒绝）矛盾

- 证据：`src/ui/output_page_build.cpp:124-128`
  `add_segment("转码")` / `add_segment("仅元数据")` + `new QLabel(tr("转码 = 生成新文件 · 仅元数据 = 原地写入"))`；
  `docs/design.md:253`（权威口径："同格式、零重编码，只替换容器内元数据，**输出到镜像目录（同名文件）**"）；
  `src/core/pipeline.cpp:1381-1383`（`out==src` 一律失败）；`src/ui/page_output.cpp:1084-1088`（仍强制要求输出根目录）。
- 事实：实测三种 conflict（overwrite/skip/rename）+ `out_root`=输入目录 → 全部
  `output path collides with the source file (source is read-only)`，源文件 SHA 与 mtime 不变；
  产物永远写在 `out_root/$format/$dir/$file` 下（GUI 默认模板 `$format/$dir/$file`，`output_page_build.cpp:394-397`）。
  即"原地写入"在两条入口都不可达。
- 影响：用户以为原图被就地更新（可能不再去取输出目录里的产物，或反过来担心原图被改）；与 design 口径也冲突。
- 建议：把说明行改成「仅元数据 = 同容器重写，产物写到输出目录（源文件只读）」；若确实要做"原地"，
  需引擎放开 `out==src` 的白名单并用同目录临时文件 + rename（当前 `rewrite_metadata_only` 的结构天然支持）。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-09 [P3] 容器层位移：JPEG `COM` 前移并补 NUL；PNG `eXIf`/`iCCP` 前移、`tEXt(Description)` 变压缩 `iTXt`

- 证据：Exiv2 `jpgimage.cpp:691-696`（只记第一个 COM）、`889-911`（COM 固定写在 SOF 前，末尾 `putb(0)` 补 NUL）、
  `pngimage.cpp:555-558`（`eXIf`/`iCCP` 一律 strip 后于 IHDR 之后重写）、`647-657`
  （`PngChunk::makeMetadataChunk(comment_, mdComment)` 生成新文本块）、`659-675`（`Description` 等关键字被剥离）。
- 事实：JPEG 实测 `COM(plain comment without nul)`（27 B，无 NUL）→ 28 B 带 NUL 且移到 SOF 前；
  同一文件里第二个 `COM(crafted comment)` 作为未知段原样保留。PNG 实测 `tEXt(Description "png description text")`
  → `iTXt(comp=1, 同文本)` 并前移到 IHDR 之后；`eXIf`/`iCCP` 前移但内容（EXIF 164 B / ICC profile 588 B）逐字节不变；
  `sRGB/gAMA/cHRM/oFFs/prVt/tEXt(Comment)/tEXt(Software)` 原样保留。
- 影响：容器段序/块类型变化，值不丢；严格的字节比对工具会报"文件变了"。
- 建议：文档化"段序不保证"；若要减少噪声，可在 `rewrite_metadata_only` 里对 PNG 只重写受影响的块
  （自写 chunk 级编辑器），或至少把这条写进 `design.md §5.5`。
- 状态：confirmed；known=partial（`docs/encoder-config-audit-2026-10-25.md:786` 已记 PNG eXIf 前移）。

##### X-METADATA-ONLY-10 [P3] 格式判定只看扩展名：PNG 命名 `.jpg` 会产出"扩展名与容器不符"的文件；`.jfif/.jpe` 被拒

- 证据：`src/core/pipeline.cpp:1687-1700`（`format_for_source_path` 只按扩展名映射，`.jpg/.jpeg→jpeg`、
  `.tif/.tiff→tiff`、`.heic/.heif→heif`、其余 `find_format(ext)`）、`1704-1708`（`ctx.ext = fe.src.extension()`）。
- 事实：把 `richpng2.png` 改名 `actually_png.jpg` 跑仅元数据 → `ok`，产物 3658 B **是 PNG**（IHDR/IDAT 校验通过、
  IDAT SHA 不变），文件名却是 `.jpg`；`.jfif` 源 → `cannot determine source format for metadata-only output: .jfif`。
- 影响：改名/导出工具留下的错扩展名会被"照扩展名再生产"，产物交给只认扩展名的下游会误判；
  `.jfif` 这类合法 JPEG 扩展名直接不可用。
- 建议：仅元数据改用**内容探测**定格式（`Exiv2::ImageFactory::getType` 或 OIIO probe 的 format 字段），
  扩展名只用于命名；`format_for_source_path` 补 `.jfif/.jpe/.dib/.avifs` 等别名。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-11 [P3] `--dev` 摘要在仅元数据下打印被忽略的目标格式与编码参数（证据面误导）

- 证据：`src/dev_cli.cpp:918-927`（`format=%s backend=... lossless=... params=[...]` 全部来自 `specs`）、
  `pipeline.cpp:1338-1344`（真实产物格式 = 源格式，`backend/tech` 清空）、侧车
  `build/encoder-audit/meta/jpeg/jpeg3-time/out/rich3.jpg.pp.json`（`format: "jpeg"`、`backend: ""`、
  `params` 仍是 jxl 的 15 个键）。
- 事实：`--format jxl` + `--metadata-only` 时摘要写 `format=jxl ... params=[codestream_level=10 ...]`，
  侧车则写 `format=jpeg, params=<jxl 键>`；两处都不代表实际发生的事。
- 影响：取证/回归日志里出现"以为跑了 jxl"的假读数；与"证据面只增不减、如实记录"的纪律冲突。
- 建议：`run_metadata_only` 模式下摘要/侧车打印 `source_format` 并把 `params` 置为 `(n/a: metadata-only)`。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-12 [P3] `sync_mtime` 失败只 warn（`ok` 仍 true），且用 `mktime` 本地时区解释 EXIF 墙钟

- 证据：`src/core/pipeline.cpp:1446-1452`（`sync_file_mtime` 失败只 `log_warn`，不影响 `row` 终态）；
  `src/core/metadata.cpp:1180`（`std::mktime(&tm)` 按本机本地时区换算）。
- 事实：墙钟 `2024:03:02 12:00:00` → 产物 mtime `2024-03-02 12:00:00`（本机 +08:00 下成立，实测），
  源文件 mtime 不变（实测）；但如果目标文件系统/进程时区不同（TZ 未设/跨时区批处理），
  同一 EXIF 墙钟会映射成不同 `epoch`，且失败时用户只看到一条日志、行状态仍是 `ok`。
- 影响：mtime 语义在跨时区环境不可复现；权限/占用导致设置失败不显性。
- 建议：mtime 换算显式用 `TimeShift` 的时区语义（OffsetTime 或 settings 时区），并在 UI 标注"按本机时区"；
  失败升格为 warning 进 `row.warnings`（与 `WarningKind` 通道一致）。
- 状态：confirmed（本地时区行为实测；跨时区未实测）；known=no。

##### X-METADATA-ONLY-13 [P3] 设置页「EXIF 方向」在仅元数据下不生效且无任何提示

- 证据：`src/ui/settings_dialog.cpp:1263-1271`（勾选 + tooltip「按 EXIF 方向自动旋转输出」）、
  `src/ui/mainwindow_run.cpp:312`（`cfg.rotate_orientation = d.settings.rotate_orientation`）、
  `src/core/pipeline.cpp:1364-1366`（仅元数据 `shared_skip(Stage::Orient)`，从不旋转）、
  `src/core/pipeline.cpp:1422-1423`（注释明确"不旋转 ⇒ 不丢 Orientation"）。
- 事实：仅元数据产物保留原 Orientation（`right, top` 实测），像素不动；旋转开关在整个模式里是死键。
- 影响：用户以为开关会让产物"摆正"，实际产物仍未旋转（显示方向由查看器按 Orientation 处理）。
- 建议：输出页说明行里补一句"仅元数据不旋转、方向标签保留"；或在设置页该行 tooltip 注明"仅元数据模式不适用"。
- 状态：confirmed；known=partial（`docs/encoder-config-audit-2026-10-25.md:2475` 提过"色彩/方向控件需确认"，
  色彩已置灰，方向仍未处理）。

##### X-METADATA-ONLY-14 [P3] 隐私剥除会删掉 `Orientation`（像素未旋转）→ 显示方向改变，与"不丢方向"的注释自相矛盾

- 证据：`src/core/metadata.cpp:1274-1279`（strip = `exif.clear()`，Orientation 一并消失）；
  `src/core/pipeline.cpp:1422-1423`（"No orientation clearing here: the pixels are not rotated in metadata-only mode,
  so dropping the tag would change how the image displays."）。
- 事实：实测 JPEG 隐私剥除后 `Exif.Image.Orientation` absent（`VERIFY jpeg3-strip OK`，断言含 Orientation absent）；
  TIFF 隐私剥除的 tag delta 也是 `removed:274 Orientation`。像素未旋转 → 查看器不再旋转 → 同一文件"看起来转了"。
- 影响：剥除后图片显示方向变了（尤其竖拍手机照片），用户会以为像素被改。
- 建议：隐私剥除时保留（或按像素实际方向规范化后写 1）`Orientation`，并在 UI 说明；
  若坚持全清，则在 `design.md §5.6` 写明"隐私剥除会同时移除方向标签，显示方向可能变化"。
- 状态：confirmed；known=no。

##### X-METADATA-ONLY-15 [P3] JXL 的"box 级替换"仍未落地（仅元数据恒拒绝），design 承诺与实现分叉

- 证据：`docs/design.md:258`（"JXL | box 级替换（码流 box 原样拷贝，重写 Exif/xml box）"）、
  `src/core/metadata.cpp:1112-1115`（白名单只有 jpeg/png/tiff/webp，其余 false）、`pipeline.cpp:1369-1372`
  （`format 'jxl' does not support metadata-only rewrite (zero re-encode)`）；`src/codecs/enc_jxl.cpp:276-296,522-527`
  （box 只在**编码**路径由 `JxlEncoderAddBox` 添加，没有对既有 JXL 的 box 级重写）。
- 事实：`jxl_exif.jxl` 在仅元数据下 `failed`、无产物（其余 3 个容器同样：heic/avif/bmp 均显式失败）。
- 影响：JXL 用户无法做零重编码的元数据清理；与设计承诺不一致。
- 建议：二选一——实现 box 级重写（自写 ISO-BMFF box 解析，码流 box 原样拷贝），或把 `design.md §5.5` 改成
  "v1 不支持"并在 UI 对 JXL 源给出前置提示（GUI 预检已有，`mainwindow_run.cpp:444-466`）。
- 状态：confirmed；known=yes（`docs/encoder-config-audit-2026-10-25.md:2463`、`:4713-4723`）。

#### 4. 输出兼容性与合规矩阵

| 源/配置 | 产物结构 | 标准工具可读 | 合规风险 |
|---|---|---|---|
| JPEG（EXIF+XMP+ICC+COM+APP15+缩略图+MakerNote）pass-through | 同容器；APP1/Exif·XMP、APP2/ICC 内容不变；COM 前移+NUL；SOS 熵数据逐字节不变 | 是（Exiv2 读回 + OIIO 解码 + `pp_verify` exact OK） | 低（段序与 COM 末尾 NUL 均属允许；EXIF 结构合规） |
| JPEG 时间偏移 / GPS / 删标签 / 隐私剥除 | APP1 重写（内容 >64KB 时 Exiv2 抛 `kerTooLargeJpegSegment`，`jpgimage.cpp:766-767`）；缩略图 839 B 与 MakerNote 40 B 逐字节保留 | 是（`Exif.Photo.DateTimeOriginal` = 偏移后值、GPS DMS 读回、Orientation `right, top` 保留） | 低 |
| JPEG 第三方 XMP packet | XMP APP1 重新序列化（852→2695 B、`xmptk` 改写、padding 丢）；值全保留 | 是 | 低（体积膨胀；in-place padding 语义丢失） |
| JPEG 隐私剥除 | EXIF/XMP 段移除；ICC/APP15/COM 留存 | 是 | 中（IPTC/COM 隐私残留，见 03） |
| PNG（eXIf/iCCP/iTXt-XMP + sRGB/gAMA/cHRM/oFFs/prVt/tEXt×3） | IDAT 逐字节不变；eXIf/iCCP 前移；`tEXt(Description)`→压缩 `iTXt`；其余块原样 | 是（CRC 全通过 + `pp_verify` exact OK + 元数据读回） | 低（块序/类型变化；值不丢） |
| TIFF LZW / ZIP+分块 / 多页 | IFD 结构重写，strip/tile 载荷逐字节不变；多页 2/2 保留；pass-through 整文件字节相同 | 是 | 低 |
| TIFF 隐私剥除 | IFD0 删 Artist/Orientation/Software/DateTime/XMP/ExifIFD；ICC 保留；strip 载荷不变 | 是 | 中（方向标签被删，见 14） |
| WebP（VP8X 齐备） | VP8X/ICCP/VP8/EXIF/XMP 结构不变；pass-through 整文件字节相同 | 是 | 低 |
| WebP（无 VP8X） | 注入 VP8X（+18 B），VP8/VP8L 不变 | 是（libwebp/浏览器均接受） | 低（写 EXIF 时 VP8X 是规范要求；仅 no-op 空跑属无谓改动，见 05） |
| HEIF / AVIF | 不产出（`format 'heif|avif' does not support metadata-only rewrite (zero re-encode)`） | — | 无（显式失败；要改元数据必须转码：实测 HEIC 转码后 `mdat` SHA 变化） |
| JXL / BMP | 不产出（同上；`detail_metadata_only_supported` 白名单 + `bmffimage.cpp:779-782` BMFF 只读） | — | 无（显式失败；JXL 设计承诺未落地见 15） |
| GIF / TGA / `.jfif` | 不产出（`cannot determine source format for metadata-only output`） | — | 无（显式失败；`.jfif` 属可用性缺口见 10） |
| CMYK TIFF / 截断 JPEG | 不产出（`probe failed: ...`，含 vcpkg 构建路径） | — | 低（无产物；文案需清洗见 07） |
| `out_root` = 输入目录（三种 conflict） | 不产出（`output path collides with the source file`），源 SHA/mtime 不变 | — | 无（R-001 守卫生效） |
| commit 失败（目标被目录占用） | 不产出、无 tmp 残留、`ok=false`、目标目录未动 | — | 无（R-002 守卫生效） |
| 批量混合（jpg/png/tif/webp/heic/jxl 一轮） | 4 个 ok（扩展名=源扩展名）+ 2 个 failed；侧车 `format`=源格式 | 是 | 低（GUI 有前置预检对话框 `mainwindow_run.cpp:444-466`；`--dev` 无预检） |
| workers=4 并发批量（6 JPEG+PNG+TIFF） | 8/8 ok；码流 SHA 全同 | 是 | 低 |

#### 5. 未覆盖与存疑

1. **IPTC/APP13 未做夹具实测**：判断"隐私剥除不清 IPTC"来自 Exiv2 代码路径（`jpgimage.cpp:846-887` + 本仓只
   `clearExifData/clearXmpData`），confidence=代码级 confirmed，但没有 IPTC 样本的字节级前后对照。
2. **MakerNote 内部偏移未验证**：实测 MakerNote 的**值字节**逐字节保留（40 B，SHA 同上），但厂商私有偏移是否
   仍指向正确内部结构没有验证（需要真实相机样本 + 厂商解析器）。
3. **非常规 EXIF 布局的边界**：手工构造的"值区放在 IFD1 之后 + IFD0 未按 tag 升序"样本出现
   "只做时间偏移时 Exiv2 非侵入写静默丢弃改动"（输出 5350→5350 B、值不变）；改成常规布局（值区在 IFD0/ExifIFD
   之后、IFD1 最后）后同一命令生效（5342→5343 B、值变）。两次样本同时改了多个变量，**未隔离到唯一触发条件**，
   故不列为缺陷，仅登记为 Exiv2 0.28.9 的边界行为；真实相机/软件产出的常规布局实测正常。
4. **未测容器/特性**：extended XMP（多 APP1 扩展段）、BigTIFF、JPEG 12-bit、PNG APNG（acTL/fcTL）、
   动画 WebP（ANIM/ANMF 已由 `container_scan` 识别但未做仅元数据回归）、JPEG 的 Photoshop IRB 多段、
   TIFF 的 SubIFD（本仓 `multi.tif` 只有 IFD 链，无 SubIFD）。
5. **失败注入只做了一例**（目标被同名目录占用 → commit 失败）；磁盘满、文件被独占、进程被杀（rename 前的
   tmp 清理）未做注入实测，结论依赖 `fsops.cpp:543-585` 的代码路径。
6. **GUI 未运行**（任务约束禁止 GUI/`--ui-smoke`）：所有 GUI 结论为代码阅读 + `--dev` 复现引擎行为；
   "下拉被忽略""文案矛盾"的最终观感需一次人工走查确认。
7. **取消（cancel）语义**：仅元数据在 `probe` 前后与 `rewrite` 前有取消点（`pipeline.cpp:1411-1412`），
   但 `rewrite_metadata_only` 内部（copy + exiv2 写 + rename）不可取消；未实测长耗时文件取消后的中间态。
8. **HEIF/AVIF 的元数据写入保真度只做了轻量验证**：转码 HEIC 后 `pp_verify` 断言
   `Exif.Image.Artist` 写入 + `DateTimeOriginal` 存在 + 像素 PSNR ≥30 dB 通过；XMP 是否保留、EXIF 其它字段
   是否逐项保留未做全量比对（这部分属转码路径，不是仅元数据通路）。
9. **能力探测缓存**：`detail_metadata_only_supported` 结果按进程缓存（`metadata.cpp:1099-1111`），
   本次未构造"exiv2 运行期能力变化"的场景（运行期不可变，属设计前提）。

### 6.6 运行时实证 A：JPEG/JXL/PNG/TIFF

> 目的：用"同输入、只改一个参数、比较产物字节/像素"的方式，验证"参数是否真的进入编码"。全部经 `python tools/env.py run --`；产物在 `build/encoder-audit/`。本表是主控独立复核与逐格式章节实测的汇总；逐条命令/侧车见第 4 章对应节与 9.7。

#### 1. 参数 → 可观测效果

| 格式 | 参数 | 对照 | 结果 | 判定 |
|---|---|---|---|---|
| JPEG | distance | 0.5 vs 3.0 | JPEG 不同体积/PSNR（如 distance 0.5 vs 3 明显不同） | 是 |
| JPEG | quality_mode=quality + quality | 50 vs 95 | 9848 vs 24977 B；DQT 2 张 | 是 |
| JPEG | psnr_target | 40 + distance 0.5 vs 3.0 | 同字节/同 SHA（13142 B） | **静默覆盖** |
| JPEG | chroma | 444/440/422/420 | SOF 采样因子 1×1/1×2/2×1/2×2；DQT 四者相同 | 采样生效、**量化表不随动** |
| JPEG | progressive | true/false | SOF2 13 scans vs SOF0 1 scan | 是 |
| JPEG | restart_in_rows | 0 vs 2 | 无 DRI vs DRI=64 | 是 |
| JPEG | smoothing_factor | 0 vs 50 | 18467 vs 16331 B（明显低通） | 是 |
| JPEG | adaptive_quantization | true/false | 19171 B / PSNR 43.52 vs 默认 | 是 |
| JPEG | std_quant_tables | false/true | DQT 2 张标准表 vs 3 张 jpegli 表 | 是 |
| JPEG | cicp_transfer_function | 2/16/18 | 613/623/623 B；16 体积 +47% | 条件（只改量化表） |
| JXL | effort | 1 vs 9 | 1069 vs 772 B | 是 |
| JXL | distance | 0.5 vs 3 | 1063 vs 614 B | 是 |
| JXL | resampling + lossless | 无损 + 2 | `pp_verify exact` FAIL 196604/196608 | **P0→P1 假无损** |
| JXL | color_transform | None/XYB/YCbCr vs YCoCg | 前三者同 SHA；YCoCg 体积不同 | 前三 no-op |
| JXL | channel_colors_global_percent | 50 | 编码失败 `rejected: ERROR` | **错 setter** |
| JXL | codestream_level | 5/10/auto | 979/988/979 B；默认带 jxll 0x0a | 是（默认收窄） |
| JXL | brotli_effort | 0 vs 11 | 同字节 | 否（装饰） |
| PNG | compressionLevel | 0/6/9 | 443653/173351/169096 B；FLEVEL 0/2/3；像素同 | 是 |
| PNG | out_bitdepth | 8 vs 16 | 样本 = v*257；体积 +18.9%（lv6） | 是（无 sBIT） |
| TIFF | compression | none/lzw/zip/packbits | 1180341/50314/11085/1174699 B | 是 |
| TIFF | compression=ccittrle | RGB8 | 失败：Bits/sample must be 1 | **必失败选项** |
| TIFF | predictor | none/horizontal/float | 687122/50314/失败 | 条件；float 必失败 |
| TIFF | deflate_level | 1/6/9 + predictor none | 三档同字节（被 OIIO/libtiff 忽略） | 条件失效 |
| TIFF | tile 256 / 10 / 仅单边 | — | 256 OK；10/单边 失败 | 是（约束有效） |

#### 2. 结论

- **JPEG/JXL/PNG/TIFF 的绝大多数参数真实进入库 API**；不存在"整表空转"。
- 失效集中在：JPEG `psnr_target` 覆盖、JPEG 色度量化表顺序、JXL 无损+resampling、JXL 三个错 setter、JXL brotli、TIFF ccittrle/predictor-float/deflate-level 组合。
- 逐格式的完整矩阵见第 4 章；关键产物的 SHA/PSNR 见各节表格。

### 6.7 运行时实证 B：WebP/BMP/HEIF/AVIF

> 目的：同输入、只改一个参数/配置，比较产物字节、容器结构与像素。全部经 `python tools/env.py run --`；产物在 `build/encoder-audit/`。本表是主控独立复核与逐格式章节实测的汇总；逐条命令/侧车见第 4 章对应节与 9.7。

#### 1. 参数/配置 → 可观测效果

| 格式 | 配置 | 对照 | 结果 | 判定 |
|---|---|---|---|---|
| WebP | preset | 0 / 2 / 5 | 三档同字节（14872 B，同 SHA） | **死控件** |
| WebP | tech=lossless（不勾无损） | vs 默认有损 | 10880 B；`pp_verify exact OK`；侧车 `lossless=false` | **产物无损、记录有损** |
| WebP | tech=lossy + lossless=true | vs 默认有损 | 同字节、chunk=VP8；侧车 `lossless=true` | **记录无损、产物有损（P1）** |
| WebP | near_lossless | 100 vs 50 + 无损 | 默认 method 下字节同；生效组合下 exact FAIL 104388/110592 | 条件生效、生效即非无损 |
| WebP | quality（lossy） | 50/90/95 | 2622/14872/21764 B | 是 |
| WebP | method | 0/4/6 | 15988/14872/13942 B | 是 |
| WebP | partitions | 3 vs 3+low_memory | 默认被库强制 1；low_memory 下 14832 B | 条件 |
| WebP | preprocessing | 1/2/4 | 1 改字节；2/4 在 sharp_yuv=true 下不变 | 条件 |
| WebP | exact（lossless） | true/false + RGBA | 54182/54666 B；exact=false 时透明区 RGB 被改 | 是 |
| BMP | out_bitdepth | 24 vs 8（RGB 源） | 同字节 12342 B，均 24bpp | **无效** |
| BMP | out_bitdepth | 24 vs 8（gray 源） | 同字节 5174 B，均 8bpp | **无效** |
| BMP | alpha | RGBA→BMP | 拍平；半透明区二次相乘偏暗 | **P0（alpha）** |
| BMP | 无 ICC | P3 源 + keep | 无 ICC、仅 MetadataDropped | 色彩语义静默丢失 |
| HEIF | bitdepth | 10 vs 8 | 10→`heix`/RExt/idc4；8→`heic`/idc3 | 是（默认 10 兼容窄） |
| HEIF | quality（GUI int） | 50 vs 90 | 30422 vs 76280 B | 是 |
| HEIF | quality（--param） | 10/50/90 | 三者 76280 B 同 SHA | **CLI 类型缺陷** |
| HEIF | lossless | 8bit | `pp_verify exact FAIL`（YCbCr 往返，≈48.9 dB） | 码流层无损，非 RGB 逐位 |
| HEIF | lossless + chroma=420 | vs lossless | 同字节（强制 444），日志有 warning，sidecar 无 | 是（提示级别低） |
| AVIF | backend svt vs libaom | 默认 | 体积/结构不同；SVT 默认 qp=0（GUI） | **默认 qp=0（P1）** |
| AVIF | bitdepth 12 | svt / libaom | svt 预检拒绝；libaom 成功 | 是（按后端探测） |
| AVIF | 10bit + alpha | svt / libaom | svt 失败；libaom 成功 678 B | 是（预选 libaom） |
| AVIF | lossless | svt / libaom | svt 失败（无 chroma）；libaom 成功 | 是（能力按后端） |
| AVIF | quality + qp=0 | quality 30/50 | 同字节（qp 覆盖） | **quality 失效** |

#### 2. 结论

- **WebP/BMP/HEIF/AVIF 没有"整表空转"**：quality/method/bitdepth/alpha 等核心参数真实进入库。
- 失效集中在：WebP preset/技术-无损双真相/near_lossless、BMP 位深与 alpha、HEIF CLI 类型/默认位深/无损语义、AVIF 默认 qp=0/SVT 无损与高 bitdepth+alpha。
- 逐格式完整矩阵与证据见第 4 章 4.5–4.8；原生字节/容器解析见 9.7。

## 7. 问题总表与修复优先级

### 7.0 严重度归一（主编裁定，2026-10-25）

各分章由独立代理产出，严重度口径在跨章之间略有差异。主编按统一口径归一如下：

- **P0 仅保留 2 条**：alpha 语义（跨格式，默认路径的像素/显示错误）与多页 TIFF 结构损坏（产物不可解码）。
- **JXL-01（无损+resampling）与 WEBP-01（tech=lossy+lossless）降为 P1**：前者需显式把非默认 `resampling` 调离 1；后者 GUI 主路径会自愈（`paramform` 自动切技术），仅 CLI/手写预设可复现。二者仍是「无损承诺不可信」的高优问题。
- **TIFF-03 撤回**：原生 strip 字节复核显示 alpha=128 处 RGB=100,50,25（premultiplied），与 `ExtraSamples=1`（associated）一致；原判据「pp_verify exact 绿 -> 数据未预乘」不成立（读端同样按 associated 解释）。该条不计入总表，TIFF 的 alpha 语义并入 5.7 的跨格式讨论。
- `X-*` 条目中与格式条目重复者按 P1 归一，不重复计入 P0。

> 计数口径：第 4 章格式条目为准（P0 2 / P1 17 / P2 37 / P3 28）；第 6 章 `X-*` 为跨面补充（P0 2 / P1 23 / P2 33 / P3 34），与格式条目重复的已在正文标注。

### 7.1 格式条目总表

| 级别 | ID | 标题 |
|---|---|---|
| P0 | PNG-01 | RGBA 半透明像素被静默改写：读端关联 + 写端反关联，全透明处 RGB 归零 |
| P0 | TIFF-01 | 多页 TIFF 源的额外页被原样搬进产物；≥3 页时该页 IFD 指向文件外、不可解码 |
| P1 | AVIF-01 | SVT-AV1 的默认 GUI 配置以 `qp=0`（CQP 0.00）编码，`quality` 失效，体积 7–32×（随图像内容） |
| P1 | AVIF-02 | AVIF + SVT-AV1：「无损编码」复选框可点，但按下必失败 |
| P1 | BMP-01 | 位深下拉对 BMP 是无效参数：24/8 产同一字节，且两个方向都与产物矛盾 |
| P1 | BMP-02 | 16-bit 源在默认 24 bpp 下静默降到 8 bps：降档告警把 bps 与 bpp 直接比较 |
| P1 | BMP-03 | keep（默认）+ 非 sRGB 源 ICC：BMP 丢掉唯一色彩标记，却不发 ICC 告警 |
| P1 | HEIF-01 | 「Main（8 位 · 兼容性最好）」档位是死选项：选中后每个文件必失败且只报「编码档位不一致」 |
| P1 | JPEG-01 | 色度采样在量化表之后设置：`chroma=444/422/440` 实际使用 4:2:0 的量化表 |
| P1 | JXL-01 | 无损 + resampling：无损复选框亮着，像素却被降采样 |
| P1 | JXL-02 | color_transform：三个取值被 libjxl 覆写成 no-op；默认 YCoCg 反而让无损体积 2.8× |
| P1 | JXL-03 | 三个 Modular 参数用错 setter：一设置就整文件编码失败 |
| P1 | JXL-04 | codestream_level 默认 10：每个产物都写 jxll=10，兼容性无谓收窄 |
| P1 | JXL-05 | distance=0（VarDCT 合法 UI 值）无 ICC 时整文件失败、有 ICC 时静默变无损 |
| P1 | TIFF-02 | `ccittrle`：核心压缩方案里必然失败的选项 |
| P1 | WEBP-01 | 显式选择 lossy 技术 + 无损标志 ⇒ 有损产物被全程标记为"无损" |
| P1 | WEBP-02 | 选 Lossless 技术但不勾"无损" ⇒ 逐位无损产物，但复选框/侧车/日志都报"非无损" |
| P1 | WEBP-03 | `preset`（内容预设）对产物零影响——六个枚举值全都产出同一份字节 |
| P1 | WEBP-04 | `near_lossless<100` 一旦生效，"无损编码"就是假的：产物非逐位无损、无告警、侧车仍 `lossless=true` |
| P2 | AVIF-03 | `threads` 参数对 AVIF 恒被 pipeline 覆盖（UI 高级行是空转旋钮） |
| P2 | AVIF-04 | libaom 的 alpha 质量被内省默认值钉死在 50，`quality` 提高时 alpha 不跟随 |
| P2 | AVIF-05 | UI 把 string 型 `chroma` 当 int 读 → 档位注释/预校验与产物互相矛盾 |
| P2 | AVIF-06 | AVIF 选 Main 档 → 位深被静默从 10 位改写成 8 位（AV1 Main 本含 10 位） |
| P2 | AVIF-07 | SVT 速度档 10–13 全部映射到 9，而默认值恰好落在死区 12 |
| P2 | AVIF-08 | libaom：`tune=iq` + 无损 = 必然失败（UI 允许该组合） |
| P2 | AVIF-09 | AVIF 无损 >16 MP 被塞进 HEIF grid 容器；无损 + alpha >16 MP 直接失败 |
| P2 | AVIF-10 | `--dev --param` 对 AVIF 全类型化参数失效；`quality` 被静默替换成 90 |
| P2 | BMP-04 | `EmbedIccUnsupported` 在 `--dev` 摘要与 `.pp.json` 里显示为 `"Unknown"` |
| P2 | BMP-05 | UI 零前置能力提示，且"色彩目标"tooltip 承诺写入新 ICC（BMP 恒不可能） |
| P2 | HEIF-02 | `--param`（字符串化入口）数值型参数不可用：`quality` 被静默改写成 90，Int/Bool 键直接失败或静默失效 |
| P2 | HEIF-03 | 默认位深 10 位 → 默认产物是 RExt（`profile_idc=4`）、hvcC 只声明 RExt 兼容位；8 位才是 Main Still Picture |
| P2 | HEIF-04 | 档位标签「Main10（10 位 · Intra）」与产物 PTL 不符；提示「部分工具显示为 Main」方向错误 |
| P2 | HEIF-05 | 无损开启时 `quality` 行仍可见可调但完全无效果（实测字节相同），无锁定、无提示 |
| P2 | HEIF-06 | `threads` 键不存在 → x265 默认线程池不受 `encode_threads` 约束（E3 缺口，日志仅 info） |
| P2 | JPEG-02 | `lossless` 标志对 JPEG 无拦截、无警告：CLI/预设可以把有损 JPEG 标成无损 |
| P2 | JPEG-03 | `psnr_target>0` 静默覆盖 quality_mode/distance/quality，且实测达不到目标（R-247 未修完 + 目标值不可信） |
| P2 | JPEG-04 | `cicp_transfer_function` 只改量化表缩放，不写任何色彩标记，且与恒嵌的 ICC 自相矛盾 |
| P2 | JXL-06 | level 5 + 无损 + 默认 16bit ⇒ 编码失败，且 libjxl 的诊断被吞掉 |
| P2 | JXL-07 | Modular 未勾无损 + 默认 distance=0 ⇒ 产物其实是无损（UI 与产物语义相反） |
| P2 | JXL-08 | keep_invisible 默认 true 覆盖库的 -1(auto)：带 alpha 的有损产物 3.1× |
| P2 | JXL-09 | brotli_effort 是装饰性参数（0 与 11 逐字节相同） |
| P2 | JXL-10 | 库支持但事实表/UI 未暴露的参数（本工具相关 6 + 2 项） |
| P2 | PNG-02 | iCCP 与 sRGB/gAMA/cHRM 同时写入：非 sRGB ICC 时色彩标记自相矛盾 |
| P2 | PNG-03 | “全参数暴露”缺口：`compression` 策略与 `png:filter` 未进 schema（其余 6 类库属性同样未接） |
| P2 | PNG-04 | 8 位源默认输出 16 位：体积 +19%~+100%、零信息增益、无 sBIT、UI 无提示 |
| P2 | PNG-05 | 灰度 PNG 输出可携带 RGB ICC（色彩类型与 profile 不匹配），无告警 |
| P2 | PNG-06 | PNG 原生 ancillary 元数据（pHYs/tIME/tEXt）整类不进 plan：重编码后静默丢失 |
| P2 | TIFF-04 | `deflate_level` 在 `predictor=none` 时被完全忽略（无提示） |
| P2 | TIFF-05 | `predictor=float(3)`：可选、可存预设、必然整文件失败 |
| P2 | TIFF-06 | "无损编码"复选框对 TIFF 是空转控件（可见、默认未勾、单击零效果） |
| P2 | TIFF-07 | 编码器不设 `DateTime` → OIIO 写"编码时刻"并派生非规范 XMP，产物不可复现 |
| P2 | WEBP-05 | `near_lossless` 在默认参数下静默空转（24 组实测字节全等） |
| P2 | WEBP-06 | `partitions` 在默认参数下被 libwebp 强制回 1（参数被静默丢弃） |
| P2 | WEBP-07 | `preprocessing` 的位 2（抖动）与位 4 在默认 `sharp_yuv=true` 下不可达 |
| P2 | WEBP-08 | lossy 路径无法保留透明区 RGB；`exact` 只被 lossless 分支读取 |
| P2 | WEBP-09 | 跨技术参数被接受、写进侧车/预设、却永远不被读取（无告警） |
| P3 | AVIF-11 | `svt:` 私有透传通道不存在：写进去必然整文件失败（`aom:` 才有） |
| P3 | AVIF-12 | "无损"在机器可读面没有任何痕迹：`--lossless` 产物的侧车连参数都不写 |
| P3 | AVIF-13 | `docs/param-catalog.md` 的 AVIF 章节与运行时内省面漂移（含一个根本不存在的参数） |
| P3 | BMP-06 | "无损编码"复选框在 BMP 上恒无效果（BMP 本来就无损） |
| P3 | BMP-07 | 24bpp 产物的 `biSizeImage` 写的是像素数而非字节数（上游 OIIO 口径偏差） |
| P3 | HEIF-07 | `complexity` 的 UI 默认值 0 与插件声明的默认 50 不一致（`has_default=false` 被当成"当前值 0"） |
| P3 | HEIF-08 | E9「未知键忽略并记日志」对 libheif 格式实际不可达：管线在 `validate_run_config` 就硬拒绝 |
| P3 | HEIF-09 | 档位选择不落预设、不进日志：预设往返丢失显式档位约束 |
| P3 | HEIF-10 | 7 个内省参数的行标签就是英文 key，与 `param-catalog.md` 的中文标签不一致 |
| P3 | HEIF-11 | 无损且未请求 chroma 时仍输出「lossless forces chroma 444 (requested 420 …)」——"requested 420" 是回退值不是用户请求 |
| P3 | HEIF-12 | 「无损」实测口径：8 位 4:4:4 往返 ≈48.9 dB（非像素级），10 位才 >60 dB；该口径只写在 tooltip |
| P3 | HEIF-13 | `__heif_profile` 取值不做类型校验：字符串值被静默当作「自动」，档位约束静默失效 |
| P3 | JPEG-05 | `quality_mode=quality` 走 `jpegli_set_quality` → 只写 2 张量化表（Cb/Cr 共用），与参考实现的 3 表路径不等价 |
| P3 | JPEG-06 | 记账口径：`out_bytes` 只是编码字节数，不含后写的 EXIF/XMP（JPEG 957 → 实际 1169） |
| P3 | JPEG-07 | 能力面收窄：库里有入口但 UI 无控件、或 UI 范围远窄于库能力（均已自述，登记以免被当成漏配） |
| P3 | JXL-11 | 范围/枚举边界与库不一致 |
| P3 | JXL-12 | 错误路径不带 libjxl 诊断（`status_name` 只有三个词） |
| P3 | JXL-13 | 分组与文案打磨 |
| P3 | PNG-07 | 恒写 14 字节空 eXIf 与 oFFs(0,0)，且空 eXIf 的留存与否取决于是否发生 Exiv2 重写 |
| P3 | PNG-08 | 「无损编码」复选框对 PNG 恒显示、勾了没有任何效果、默认还是未勾选 |
| P3 | PNG-09 | 参数 tooltip 的证据行号漂移：`png_pvt.h:615-617` 属 OIIO 3.1.14，实装 3.1.17 应为 626-628 |
| P3 | TIFF-08 | "暴露 OIIO 全部 TIFF 输出参数"与实际 5 条不符；工具注里的行号是 3.1.14 的 |
| P3 | TIFF-09 | `enc_oiio` 的"未知参数忽略并告警"（E9）在正式路径上不可达 |
| P3 | TIFF-10 | 文案：核心行标签写着"（技术选择器）"、枚举值是英文 |
| P3 | WEBP-10 | 文档与 UI 文案漂移（预设"生效值"、参数计数 23 vs 24） |
| P3 | WEBP-11 | 库支持但完全未暴露的三个参数：`image_hint` / `target_size` / `target_PSNR` |
| P3 | WEBP-12 | lossy `quality` 下界被收窄为 1，库允许的 0 档被参数引擎拒绝 |
| P3 | WEBP-13 | 侧车缺"有效技术/有效无损"字段：产物语义无法自证 |

### 7.2 跨面补充条目（X-*）

| 级别 | ID | 标题 |
|---|---|---|
| P0 | X-COMPLIANCE-01 | alpha 拍平把预乘缓冲当直通，半透明区整体偏暗 |
| P0 | X-COMPLIANCE-02 | WebP 带 alpha 输出把预乘数据当直通写入 |
| P1 | X-COMPLIANCE-03 | JXL 输出声明 alpha 非预乘，实际写入预乘数据 |
| P1 | X-COMPLIANCE-04 | HEIF/AVIF 输出同样缺 `prem` 声明（预乘数据 + 非预乘标记） |
| P1 | X-COMPLIANCE-05 | PNG 输出同时写 iCCP 与 sRGB/gAMA/cHRM，色彩声明自相矛盾 |
| P1 | X-COMPLIANCE-06 | BMP 无 ICC 槽，keep 目标下广色域像素静默变成"假定 sRGB" |
| P1 | X-COMPLIANCE-07 | 16bit 源 → BMP（24bpp）没有降档告警：bpp/bps 双语义参与同一比较 |
| P1 | X-COMPLIANCE-08 | 所有降档/丢元数据/拍平告警在 GUI 完全没有显示通道 |
| P1 | X-COMPLIANCE-09 | 金样对"alpha 拍平"没有像素裁判 |
| P1 | X-COMPLIANCE-10 | `pp_verify` 的 alpha 加载语义按容器分叉，现有 exact 断言把 WebP 预乘 bug 锁死 |
| P1 | X-LIBRARY-01 | AVIF/SVT-AV1：内省把无默认值的 `qp` 兜底成 0 并每次下发，CQP 被钉死、质量参数失效 |
| P1 | X-LIBRARY-02 | AVIF + SVT-AV1 + 无损必然失败；UI 无损复选框与 High/Professional 档位是陷阱 |
| P1 | X-METADATA-ONLY-01 | `out_root` 在输入树内时，输出会被下一轮收集为输入 → 规则二次施加（时间偏移叠加） |
| P1 | X-METADATA-ONLY-02 | GUI 仅元数据静默强制冲突策略 Skip，下拉仍显示用户选择；`--dev` 同模式走 overwrite |
| P1 | X-PARAM-ENGINE-01 | JXL「无损」+ resampling>1 产出非逐位无损文件 |
| P1 | X-PARAM-ENGINE-02 | WebP「无损」+ near_lossless<100 产出非逐位无损文件 |
| P1 | X-PARAM-ENGINE-03 | psnr_target>0 静默压掉 quality_mode/distance/quality，UI 无禁用无红字 |
| P1 | X-PARAM-ENGINE-04 | 无损标志双真相：`tech=Lossless` 与「无损编码」复选框互不同步，产物与 UI/预设相反 |
| P1 | X-WIRING-01 | webp：`tech=lossy` + `lossless=true` 静默产出有损，全链仍记 lossless=true |
| P1 | X-WIRING-02 | heif：`--param lossless=true` 静默产出有损（CLI 参数类型错位） |
| P1 | X-WIRING-03 | webp「内容预设」`preset` 完全无效（编码器读后被逐项覆盖） |
| P1 | X-WIRING-04 | heif/avif：`--param quality=N` 静默用 90，其它整型/布尔键直接失败 |
| P1 | X-WIRING-05 | HEIF/AVIF「编码档位」不进预设，跨会话丢失 |
| P1 | X-WIRING-06 | 预设不保存输出页四项色彩能力开关（其中三项改变产物） |
| P1 | X-WIRING-07 | `current_chroma()` 用 `param_int` 读字符串枚举 ⇒ 恒取 4:2:0 |
| P2 | X-COMPLIANCE-11 | 源无元数据时 TIFF 产物被注入 `DateTime=now` + XMP（并向下游扩散） |
| P2 | X-COMPLIANCE-12 | 默认位深对"体积/兼容性"有实代价且无提示：PNG 16bit、HEIF/AVIF 10bit |
| P2 | X-COMPLIANCE-13 | AVIF 无损在默认后端必然失败，报错文案指向错误原因 |
| P2 | X-COMPLIANCE-14 | 色域映射默认关闭（硬裁），超色域转换无告警 |
| P2 | X-COMPLIANCE-15 | RGBA PNG→PNG 8bit 不是逐位无损（预乘量化误差在低 alpha 放大） |
| P2 | X-LIBRARY-03 | WebP「内容预设」是死旋钮：6 档产物逐字节相同 |
| P2 | X-LIBRARY-04 | JXL `brotli_effort` 在本管线恒无效果 |
| P2 | X-LIBRARY-05 | `svt:` 透传通道必然失败（白名单放行 + 插件无该前缀） |
| P2 | X-LIBRARY-06 | TIFF `compression=ccittrle` 恒失败（Bilevel-only），却是 UI 枚举项 |
| P2 | X-LIBRARY-07 | PNG：OIIO 的 4 个输出属性只用了 1 个（策略与滤波缺失） |
| P2 | X-LIBRARY-09 | JXL 缺 8 个帧设置 + 2 个编码器级 API（含 alpha 距离与 effort 11） |
| P2 | X-LIBRARY-10 | jpegli 量化表自由度只有 1 个布尔；量化表/线性质量/扫描脚本不可达 |
| P2 | X-LIBRARY-11 | HEIF/AVIF 插件私有旋钮无 GUI 入口；且 `--dev` 无法设置 libheif 数值参数 |
| P2 | X-LIBRARY-12 | HEIF/AVIF 编码档位不进预设（`__heif_profile` 被 `__` 前缀规则丢弃） |
| P2 | X-LIBRARY-13 | `current_chroma()` 用 `param_int` 读字符串枚举 → 恒返回 420，档位注释/`Main 4:4:4` 标签恒错 |
| P2 | X-METADATA-ONLY-03 | 隐私剥除只清 EXIF+XMP：IPTC/COM/未知 APPn/PNG 文本块全部留存 |
| P2 | X-METADATA-ONLY-04 | 无规则 pass-through 也会重写第三方 XMP 报文（852 B → 2695 B，`xmptk` 被改写） |
| P2 | X-METADATA-ONLY-06 | 仅元数据写回零校验：PNG `eXIf` 落地检查只在转码后写路径 |
| P2 | X-METADATA-ONLY-07 | 仅元数据仍强制 OIIO `probe`：CMYK TIFF / 截断 JPEG 等纯元数据可处理的源被拒，且错误文案泄漏构建路径 |
| P2 | X-METADATA-ONLY-08 | UI 文案「仅元数据 = 原地写入」与实际（输出到 out_root、源只读、in-place 被引擎拒绝）矛盾 |
| P2 | X-PARAM-ENGINE-05 | 切技术把同键异义值原样带过去（webp quality：质量分 90 → 压缩努力 90） |
| P2 | X-PARAM-ENGINE-06 | WebP `preset` 是死控件，tooltip 描述的效果不存在 |
| P2 | X-PARAM-ENGINE-07 | 编码档位不落预设；profile×位深×色度没有交叉规则（只在编码期拒绝） |
| P2 | X-PARAM-ENGINE-08 | 预设不覆盖四个色彩全局项与「仅元数据」模式 |
| P2 | X-PARAM-ENGINE-09 | avif「svt-av1 + >8bit + alpha」只有 UI 提示，没有交叉规则 |
| P2 | X-PARAM-ENGINE-10 | 切技术丢用户编辑值（`st->extra` 只兜预设键，不兜 UI 编辑键） |
| P2 | X-PARAM-ENGINE-11 | 未知键职责分裂：validate_params 不管，只有 cross_validate 管 |
| P2 | X-WIRING-08 | 无损复选框与技术选择不联动（反方向）：tech=modular/lossless + 未勾 ⇒ 产物无损而 spec 记 lossless=false |
| P2 | X-WIRING-09 | webp `near_lossless` 实质无效（libwebp 侧条件几乎不可满足） |
| P2 | X-WIRING-10 | metadata_only 下冲突策略被静默改成 Skip，但下拉仍显示用户选择 |
| P2 | X-WIRING-11 | HEIF/AVIF 内省 `threads` 被 E3 的 E 无条件覆盖 |
| P2 | X-WIRING-12 | 输出页路径模板编辑不持久化（重启回旧值），与设置对话框的同一设置两套源 |
| P2 | X-WIRING-13 | 预设里的 avif 后端仍可能在后续被静默改写（R-066 余量） |
| P3 | X-COMPLIANCE-16 | 8bit 量化默认无抖动 |
| P3 | X-COMPLIANCE-17 | sidecar 里出现 `kind: "Unknown"`（dev 侧 kind→名字映射漏项） |
| P3 | X-COMPLIANCE-18 | `--lossless` 配 JPEG 静默输出有损文件 |
| P3 | X-COMPLIANCE-19 | BMP 位深下拉的 8/24 对彩色源是同一产物 |
| P3 | X-COMPLIANCE-20 | TIFF `predictor=3(float)` 暴露但必然失败；`compression=none` 时 predictor 静默失效 |
| P3 | X-LIBRARY-08 | `docs/design.md:490` 的「OIIO 即库面，PNG 1 / TIFF 5 条（已穷尽）」裁定前提不成立 |
| P3 | X-LIBRARY-14 | JXL 默认 `codestream_level=10`（库默认 -1）且无条件下发 |
| P3 | X-LIBRARY-15 | 7 个 JXL 三态参数被 Bool 化，默认值即显式下发（与库默认 -1 不同） |
| P3 | X-LIBRARY-16 | 若干 UI 范围窄于库（有出处，仍是差集） |
| P3 | X-LIBRARY-17 | TIFF `predictor=float` 是必失败档（假选项） |
| P3 | X-LIBRARY-18 | libaom 三个 alpha 旋钮无插件默认，被 GUI 以最小值 0 强制下发（同 X-LIBRARY-01 机制） |
| P3 | X-LIBRARY-19 | `metadata.cpp` 头部"Exiv2 写支持矩阵"与实装构建不符（文档漂移） |
| P3 | X-LIBRARY-20 | HEIF/AVIF 内省项 `threads` 恒被调度器值覆盖（UI 设了没用） |
| P3 | X-METADATA-ONLY-05 | 无 VP8X 的 WebP 在"纯 no-op"仅元数据下也被注入 VP8X（+18 B） |
| P3 | X-METADATA-ONLY-09 | 容器层位移：JPEG `COM` 前移并补 NUL；PNG `eXIf`/`iCCP` 前移、`tEXt(Description)` 变压缩 `iTXt` |
| P3 | X-METADATA-ONLY-10 | 格式判定只看扩展名：PNG 命名 `.jpg` 会产出"扩展名与容器不符"的文件；`.jfif/.jpe` 被拒 |
| P3 | X-METADATA-ONLY-11 | `--dev` 摘要在仅元数据下打印被忽略的目标格式与编码参数（证据面误导） |
| P3 | X-METADATA-ONLY-12 | `sync_mtime` 失败只 warn（`ok` 仍 true），且用 `mktime` 本地时区解释 EXIF 墙钟 |
| P3 | X-METADATA-ONLY-13 | 设置页「EXIF 方向」在仅元数据下不生效且无任何提示 |
| P3 | X-METADATA-ONLY-14 | 隐私剥除会删掉 `Orientation`（像素未旋转）→ 显示方向改变，与"不丢方向"的注释自相矛盾 |
| P3 | X-METADATA-ONLY-15 | JXL 的"box 级替换"仍未落地（仅元数据恒拒绝），design 承诺与实现分叉 |
| P3 | X-PARAM-ENGINE-12 | 无效/装饰控件仍以正常控件暴露（4 类） |
| P3 | X-PARAM-ENGINE-13 | TIFF `predictor=float(3)` 仍在枚举里，选中必错 |
| P3 | X-PARAM-ENGINE-14 | 隐藏参数仍进 values/预设并计入「已改 N」 |
| P3 | X-PARAM-ENGINE-15 | 预设位深缺省 8，与 `default_bitdepth_for` 不一致 |
| P3 | X-PARAM-ENGINE-16 | libheif 内省 Int 参数的无界范围污染注释与「自动」哨兵 |
| P3 | X-PARAM-ENGINE-17 | BMP 位深下拉的 8/24 两档对 RGB 源无可观测差异 |
| P3 | X-PARAM-ENGINE-18 | `apply_locks` 以入参为准无条件改写无损两键；"唯一写入点"注释与实现不符 |
| P3 | X-WIRING-14 | 编码器 fallback 默认与 schema 默认不一致（4 处） |
| P3 | X-WIRING-15 | GUI 不经 `build_output_specs`，装配链少一次 `validate_params` |
| P3 | X-WIRING-16 | 侧车/摘要里的 `tech` 在 `--dev` 单格式路径为空串，与 GUI 口径不同 |
| P3 | X-WIRING-17 | HEIF/AVIF 在 CLI 路径完全没有参数（`params=[]`），插件默认值无落盘记录 |
| P3 | X-WIRING-18 | 色彩四选项的通路里有两个无效字段，且「线性光重采样」放在输出页却只影响预览 |
| P3 | X-WIRING-19 | `--param` 未知键在配置期不报错，而是逐文件失败 |

### 7.3 修复顺序（主控建议）

### 7.3 修复顺序（主控建议）

> 原则：先修"数据会坏/承诺会假"的，再修"配置不生效"的，最后修"文案/取证面"。每批都给出最小验证探针；不要求一次做完。

#### 第 0 批：P0（数据完整性，必须先做）

| 序 | 动作 | 触及文件 | 最小验证 |
|---|---|---|---|
| 0.1 | **alpha 语义端到端收口**：解码请求 `oiio:UnassociatedAlpha=1`；OIIO 写端设置同名属性；JXL/WebP/HEIF/AVIF 的 alpha 声明与数据一致；`flatten` 输入语义对齐 | `src/decode/oiio_reader.cpp`、`src/codecs/enc_oiio.cpp`、`enc_jxl/enc_webp/enc_heif`、`core/simd/simd.h`、`core/pipeline.cpp` | 256×2 RGBA ramp（RGB 恒 200,100,50）→ PNG/BMP/JPEG/WebP/JXL：alpha=128 的 RGB 必须仍是 200/100/50（PNG/WebP/JXL 原生字节，BMP/JPEG 为合成值 228,178,153）；alpha=0 的 RGB 不得归零（PNG 若必须归零，需显式 warning） |
| 0.2 | **TIFF 后写只许 IFD0**：元数据载荷剔除 `Exif.Thumbnail.*`/子图目录；metawrite 后断言 `subimage_count==1` | `src/core/metadata.cpp`（`make_payloads`/`exiv2_apply`）、`src/core/pipeline.cpp`（metawrite 后） | `multi3.tif` → TIFF：产物只 1 个 subimage；若保留多页，第 2..N 页必须可解码且与源逐字节一致 |
| 0.3 | **JXL 无损锁死降采样**：Modular `resampling` 加 `visible=not_lossless` + locked 1；`cross_rules` 加 `lossless && resampling>1`；编码器兜底 | `core/format_tables.cpp`、`core/params.cpp`、`codecs/enc_jxl.cpp` | `--lossless --param resampling=2`：必须拒绝或产物 `pp_verify exact OK`；sidecar 不得出现 `lossless=true` + 非 exact |
| 0.4 | **WebP 无损单一真源**：技术下拉与复选框联动；`lossless=true` 时 lossy 技术必须拒绝；`near_lossless<100` 锁 100 或 warning | `ui/paramform.cpp`、`codecs/enc_webp.cpp`、`core/params.cpp` | `--tech lossy --lossless` 必须失败；`--tech lossless` 时复选框必须自动勾选；`near_lossless=50 + 无损` 必须拒绝或警告 |

#### 第 1 批：P1（常见配置无效/必失败/默认值明显不合理）

| 序 | 动作 | 最小验证 |
|---|---|---|
| 1.1 | **JPEG 色度因子移到量化表之前**（照抄参考实现顺序） | `chroma=444` 与 `420` 的 DQT 必须不同；默认 444 的 DQT 命中 444 公式 |
| 1.2 | **AVIF 默认 `qp=0` 修复**：可选参数 `has_default=false` 时不得把 0 当默认下发；`param_def_from` 对无默认的 Int 不要写 `def=min`；GUI 只下发用户改过/库有默认的键 | GUI 默认参数集（v2 预设模拟）与 `--dev` 默认产物体积差 <2×；`quality` 必须仍生效 |
| 1.3 | **BMP 位深**：8/24 改为按通道数只读展示，或让编码器真正驱动 bpp；`DepthDowngrade` 用 `bitdepth_bps` | RGB 源 8/24 不再同字节；16-bit 源默认 24 必须发 `DepthDowngrade` |
| 1.4 | **WebP `preset`**：删行，或改成"默认值来源"并与手调联动 | preset=0/2/5 必须产生不同字节（或控件不存在） |
| 1.5 | **JXL 三个 Modular setter**：`channel_colors_*`/`modular_ma_tree_learning_percent` 改 float setter，`hi` 收 100 | 三键各取非默认值必须 `JXL_ENC_SUCCESS` |
| 1.6 | **WebP 无损+近无损**：`near_lossless<100` 发 `EncodeResult.warnings` 或锁 100 | sidecar `warnings` 非空，或值不可设 |
| 1.7 | **HEIF 默认位深 8 / 删 Main 死档位 / quality 无损锁定** | 默认 HEIF `profile_idc=3`；HEIF Main 档位不再 100% 失败；无损下 quality 置灰 |
| 1.8 | **AVIF SVT 无损**：勾无损时自动切 libaom，或禁用 SVT 的复选框 | `--backend svt-av1 --lossless` 必须不再失败（或 UI 不可选） |
| 1.9 | **BMP keep + 非 sRGB ICC 告警**：判据改 `!icc_to_embed.empty()` | AdobeRGB 源 + keep → `EmbedIccUnsupported` |
| 1.10 | **TIFF `ccittrle`/`predictor=float` 删除或运行前拦截** | UI 不可选，或 `on_start` 中文红字 |
| 1.11 | **HEIF/AVIF "无损"语义披露**：格式卡/预设/日志写"YCbCr 平面无损" | 8-bit HEIF/AVIF 无损产物的 UI 文案不再暗示逐位无损 |
| 1.12 | **预设补齐全局项**：`PresetData` 加性追加 `color_intent/gamut_mapping/dither8/linear_resample`（+`metadata_only`）、档位字段；修 `bitdepth` 缺省 | 存/载预设后四项与档位逐字一致 |

#### 第 2 批：P2（边界/提示/取证面）

- JXL `codestream_level` 默认改 auto、`color_transform` 加 auto、`brotli_effort` 隐藏；
- WebP `partitions`/`preprocessing` 加前置条件提示或锁定；`threads`/`quality` 在 HEIF/AVIF 的覆盖加 UI 说明；
- PNG `compression` 策略 + `png:filter` 进 schema（高级）；sBIT/PHYs/tIME 纳入 plan 或明确丢弃；
- TIFF `DateTime` 用源生效时间、XMP 日期 ISO 8601；
- HEIF `complexity` 默认口径、`threads` E3 说明、profile 中文标签；
- AVIF `current_chroma()` 类型修复、Main 位深限幅修正、SVT speed 10–13 标记、`tune=iq`+无损拦截、grid 说明；
- `--dev --param` 对运行时内省后端先查 live ParamDef（quality/speed/qp 等）；
- 未知键在 `validate_params` 统一拒绝，或 `build_output_specs` 补 `cross_validate`；
- 侧车补 `effective_lossless`/`effective_tech` 字段（machine-readable）。

#### 第 3 批：P3（文案/打磨）

- `bmp bitdepth`/`无损复选框`/`dct_method` 等"装饰控件"统一只读或隐藏；
- 参数 tooltip 行号统一改函数名锚；HEIF 七个英文 key 补中文标签；
- `apply_locks` "唯一写入点"注释纠偏；`warnings` 的 `Unknown` case 补齐；
- `linear_resample` 从输出页移到预览设置（或标注"仅预览"）；
- 文档 `param-catalog`/`design.md` 的"全参数暴露/全部 TIFF 参数"口径改为"裁剪清单"。

#### 机械探针建议（一次性投入，长期防回潮）

1. **参数生效表探针**：每个 ParamDef 的默认值 vs 一个非默认值，产物 SHA 必须不同；允许 no-op 的项维护白名单（如 `brotli_effort`）。
2. **alpha 夹具**：RGBA8/RGBA16 半透明 ramp（alpha ∈ {0,1,2,64,128,255}），PNG/WebP/JXL 用原生字节断言，JPEG/BMP 用合成值断言；`pp_verify` 增加 unassociated 读取模式。
3. **多页 TIFF 夹具**：3 页源 → 断言产物 1 个 IFD 或每页可解码。
4. **预设往返**：save→load→`collect_preset` 逐键/逐字段相等（含 color_intent/gamut/dither8/档位/位深）。
5. **UI 状态断言**：`--ui-smoke` 增加"无损复选框与 tech 下拉联动""BMP 位深只读""AVIF 无损自动切 libaom"三条探针。

## 8. 覆盖与未覆盖（诚实清单）

### 8.1 覆盖矩阵

| 格式 | 参数面 | 位深 | alpha | 灰度 | 有损/无损 | 仅元数据 | 兼容性实证 |
|---|---|---|---|---|---|---|---|
| JPEG | 15/15 静态键 + 内部键 | 8（16 硬拒/降档告警） | 不支持→拍平（已实测） | 原生灰度（已实测） | 有损；无无损（`lossless` 被忽略，已实测） | ✅ 字节级 | OIIO+jpegli、T.81 扫描脚本校验、`pp_verify` |
| JXL | 24 frame settings + 4 特例 | 8/16 | ✅（RGBA/灰度+alpha 无损 exact） | ✅（1ch Y） | 真无损 / 有损 / **假无损边界（resampling）** | ❌ 硬失败（设计承诺未落地） | OIIO/libjxl、容器 box 解析、`pp_verify` |
| PNG | 1 静态键 + 6 类库属性比对 | 8/16 | ✅（但 alpha 语义 P0） | ✅ | 恒无损 | ✅ 字节级（IDAT） | OIIO、独立 Python PNG 解析、CRC/IDAT 校验 |
| TIFF | 5/5 静态键 + 7 类库属性比对 | 8/16 | ✅（语义见 5.7） | ✅ | 恒无损（ccittrle 不可用） | ✅ strip/tile 载荷 | OIIO/libtiff、Exiv2、纯 Python IFD 解析、`pp_verify` |
| WebP | 24/24 静态键（两技术） | 8（16 硬拒/降档告警） | ✅（exact/alpha_*） | ❌→RGB（有告警） | 真无损 / 有损 / **假无损边界（near_lossless/tech）** | ✅ 字节级（VP8） | libwebp 原生解码（ctypes）、WebPMux、`pp_verify` |
| BMP | 0（正确） | 8/24 bpp（无效下拉） | ❌→拍平（有告警） | ✅ 8bpp 索引 | 恒无损 | ❌ 无容器（硬失败） | Windows GDI+、OIIO、原生 BMP 头解析、`pp_verify` |
| HEIF | 7/7 内省键 + 档位 + 透传 | 8/10/12 | ✅（单图/alpha aux；>16MP+alpha 拒绝） | ❌→RGB（有告警） | 码流层无损（RGB 往返有舍入） | ❌ v1 不支持（硬失败） | OIIO/libheif/libde265、纯 Python BMFF 解析、`pp_verify` |
| AVIF | SVT 11 / libaom 15 内省键 + 档位 | 8/10（SVT）/ 8/10/12（libaom） | ✅（8bit 两端；10bit 仅 libaom；12bit libaom） | ❌→RGB | 码流层无损（libaom）；SVT 无损不可用 | ❌ v1 不支持（硬失败） | OIIO/libheif、纯 Python BMFF/av1C 解析、`pp_verify` |

### 8.2 明确未覆盖（按原因）

| 未覆盖项 | 原因 | 影响 |
|---|---|---|
| GUI 真实点击/截图 | 任务禁令（不跑 GUI/ui-smoke）；UI 结论为代码级 | 可见性/锁定/联动结论未经渲染像素验证；`--ui-smoke` 探针可补 |
| Photoshop / GIMP / Krita / ImageMagick / ffmpeg / 浏览器 | 本机不可用 | "标准工具可读"仅覆盖 OIIO/libtiff/libheif/libwebp 与 Windows GDI+；第三方对 RExt/440/13-scan 的行为为规范推断 |
| >4 GB BMP、BigTIFF、超大 ICC/EXIF | 成本/时间 | 只做代码路径核对（BMP `1<<32` 硬拒、OIIO BigTIFF 自动切换） |
| HEIF/AVIF 12-bit + alpha 全组合 | 组合爆炸 | 只测 8/10-bit alpha、12-bit 无 alpha；12-bit+alpha 未逐一验证 |
| HEIF/AVIF >16 MP grid 的第三方兼容 | 无第三方解码器矩阵 | grid 产物在本仓解码链可读；AVIF 生态对 grid 支持面未验证 |
| `encode_threads>1` 的编码器内部并发 | 全部实测 `--workers 1` | JXL runner/WebP thread_level/SVT threads 的 E>1 分支未测（E3 映射代码已核） |
| 动画/多帧（GIF/APNG/动图 WebP） | 产品口径取首页 | 未测多帧容器；只测多页 TIFF（发现 P0） |
| CMYK 输入 | 负面清单 | 只验证 probe 阶段明确拒绝 |
| Windows/Linux 双平台差异 | 本机 Windows | 路径/编码/库版本差异未覆盖；Linux 侧需另跑 |
| 预设 UI 全组合 | 组合爆炸 | 只验证了关键路径（无损/档位/位深/全局项），未穷举所有技术切换 |

### 8.3 测试门禁失明点（重复强调）

1. **alpha**：`pp_verify` 与编码器共享 OIIO 关联读端；金样 alpha 夹具只有 0/255。→ 本报告用原生字节复核才发现 PNG/BMP/JPEG/WebP/JXL 的 alpha 问题。
2. **多页 TIFF**：现有金样只断言首页；`multi.tif` 只做首页 exact。→ 3 页源的越界页无人发现。
3. **参数无效**：没有"改值必须改字节"的探针；WebP preset/JXL setter/BMP 位深都靠人读代码。
4. **`--dev` 类型**：HEIF/AVIF 整数参数被字符串化；quality 固定 90、speed/qp 失败。→ CLI 取证链本身不可信。
5. **UI 状态**：除少量 `--ui-smoke` 探针外，无"控件可见/禁用/联动"的运行时断言。
6. **第三方解码**：兼容性结论多为 OIIO 单栈，缺少独立解码器交叉验证。

### 8.4 证据根与可复现性

- 所有产物、脚本、逐输出侧车在 `build/encoder-audit/`（gitignored）：
  - `sections/`：本报告第 4/6 章的原始章节；
  - `draft/`：本报告的主控草稿与独立复核记录；
  - `jpeg/`、`jxl-rt/`、`probe/`、`tiff/`、`webp/`、`bmp/`、`heif`/`out/`、`avif-rt/`、`meta/`、`compliance/`：逐格式/兼容性取证；
  - `emp-a/`、`exp1/`：运行时实证；
  - `assemble.py`：本报告装配脚本（只读 sections，输出 docs/）。
- 报告中的 `file:line` 以 2026-10-25 的工作树为准；库行号对应 `vcpkg/buildtrees/<port>/src/<version>`，版本已在各章开头注明。

## 9. 附录

### 9.1 复现命令（全部经 `tools/env.py`）

```powershell
# 通用形态（PowerShell；必须经 env.py 转发 stdio）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev <输入> --out <目录> `
  --format <jpeg|jxl|png|tiff|webp|bmp|heif|avif> [--backend ...] [--tech ...] `
  [--lossless] [--bitdepth N] [--metadata-only] [--param key=value ...] --workers 1

# 像素级断言（输入复制为 <root>/input.png；expected.json 在 <root>/case/，golden_root = 其上两级）
python tools/env.py run -- build/release-dev/pp_verify.exe <root>/case/expected.json <产物>

# 容器/像素原生解析（本报告使用的纯 Python 工具）
python build/audit-probe/pngdecode.py          # PNG 原始 RGBA（反滤波）
python build/audit-probe/pngalpha.py           # 造 RGBA ramp
python build/audit-probe/dqt.py                # JPEG DQT 对比
python build/audit-probe/webpdecode.py         # libwebp ctypes 原始 RGBA
python build/audit-probe/jxlbox.py             # JXL box 序列
python build/audit-probe/jpegscan.py           # JPEG SOS 熵数据
python build/audit-probe/imgscan.py            # PNG/WebP/TIFF 图像载荷
python build/audit-probe/tiffalpha.py          # TIFF strip 原始字节

# 关键对照（本报告主控独立复核）
# 1) WebP preset 死控件
python tools/env.py run -- build/release-dev/photopipeline.exe --dev tests/golden/base/rgb8.png `
  --out build/audit-probe/webp-preset-2 --format webp --param preset=2 --workers 1   # 与 preset=0/4 同 SHA
# 2) JXL 无损 + resampling
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/audit-probe/near3/input.png `
  --out build/audit-probe/jxlres/out --format jxl --lossless --tech modular --param resampling=2 --workers 1
# 3) WebP 无损 + near_lossless
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/audit-probe/near3/input.png `
  --out build/audit-probe/near3/out --format webp --lossless --param near_lossless=50 --workers 1
# 4) JPEG 色度 DQT
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/audit-probe/near3/input.png `
  --out build/audit-probe/jpegchroma/444 --format jpeg --param chroma=444 --workers 1
# 5) TIFF 多页
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/encoder-audit/tiff/inputs/multi3.tif `
  --out build/audit-probe/tiffmulti --format tiff --bitdepth 8 --param compression=lzw --workers 1
python tools/env.py run -- vcpkg_installed/x64-windows-avx2/tools/openimageio/oiiotool.exe `
  build/audit-probe/tiffmulti/multi3.tif --subimage 2 --info -v
# 6) AVIF qp=0 默认
#    用 v2 预设模拟 GUI 参数集：{"format":"avif","backend":"svt-av1","bitdepth":10,
#    "params":{"qp":0,"quality":50,"speed":12,"tune":"psnr","threads":4,"tile-rows":4,"tile-cols":4}}
python tools/env.py run -- build/release-dev/photopipeline.exe --dev build/audit-probe/near3/input.png `
  --out build/audit-probe/avifqp2/qp --preset build/audit-probe/avifqp2/qp0.json --workers 1
```

### 9.2 参数键索引（按格式，详见第 4 章矩阵）

| 格式 | 键（去重） | 数量 | 生效判定摘要 |
|---|---|---|---|
| JPEG | quality_mode, distance, quality, chroma, progressive, optimize_coding, arith_code, restart_in_rows, dct_method, smoothing_factor, xyb_mode, adaptive_quantization, std_quant_tables, psnr_target, cicp_transfer_function | 15 | 全部有读取点；arith/dct/xyb 锁定无效（正确处置）；psnr 覆盖前三；chroma 只改采样因子（量化表 bug） |
| JXL | effort, decoding_speed, codestream_level, distance, photon_noise, epf, keep_invisible, dots, patches, gaborish, progressive_ac, qprogressive_ac, progressive_dc, resampling, group_order, color_transform, modular_group_size, modular_predictor, modular_palette_colors, modular_lossy_palette, brotli_effort, responsive, channel_colors_global_percent, channel_colors_group_percent, modular_ma_tree_learning_percent, modular_nb_prev_channels, lossless | 27 | 大部分有效；color_transform 仅 YCoCg 有效；3 个 int setter 必失败；resampling 无损下 P0；brotli 装饰 |
| PNG | compressionLevel | 1 | 有效（0/6/9 体积/FLEVEL 变化，像素 exact） |
| TIFF | compression, deflate_level, predictor, tiff_tile_width, tiff_tile_height | 5 | compression/predictor/tile 有效；ccittrle/predictor=float 必失败；deflate_level 在 predictor=none 时被忽略 |
| WebP | quality, sharp_yuv, method, preset, sns_strength, filter_strength, autofilter, pass, filter_sharpness, filter_type, segments, alpha_compression, alpha_filtering, alpha_quality, partitions, partition_limit, preprocessing, qmin, qmax, emulate_jpeg_size, low_memory, exact, near_lossless, lossless | 24 | 多数有效；preset 死；near_lossless 默认空转/生效即非无损；partitions/preprocessing 位受前置条件；exact 仅 lossless |
| BMP | （无） | 0 | 位深 8/24 无效；无损复选框无效 |
| HEIF | quality, lossless, chroma, preset, tune, tu-intra-depth, complexity (+threads 不存在) | 7 | 全部真接线；无损下 quality 无效；CLI 数值类型缺陷；默认 10 位 RExt |
| AVIF | SVT: speed, threads, tile-rows, tile-cols, quality, lossless, chroma(不暴露), qp, min-q, max-q, tune；libaom: 另含 alpha-quality/alpha-min-q/alpha-max-q/lossless-alpha/cpu-used 等 | 11+15 | 多数有效；GUI 默认 qp=0 灾难；threads 被覆盖；alpha-quality 钉 50；SVT 无损不可用 |

### 9.3 已知 vs 新发现（对照既有评审）

**已在 `docs/deep-review-0.3.2.md` / `docs/ux-review-14.md` 登记（本报告复核状态）**：

| 既有项 | 本报告复核 |
|---|---|
| R-010 PNG `compressionLevel` 键名 | **已修**，0/6/9 真实生效 |
| R-011 TIFF `tiff:predictor` 无人消费 | **已修**，none/horizontal 字节差 13.7× |
| R-244/245/246 JPEG 三假控件 | **已修**（locked + 原因），实测不可改写 |
| R-247 `psnr_target` 静默覆盖 | **仍在**（JPEG-03），只加了 tooltip |
| R-230 HEIF 内省默认值取值集首项 | **已修**（取插件当前值）；但引出 AVIF `qp=0` 新问题 |
| R-090/R-211 BMP 位深双语义 | **部分修**（扩表+文案），下拉仍不驱动产物（BMP-01/02） |
| R-001/R-002 输出覆盖源/Exiv2 吞错 | **已修**（`same_output_as_source` + 原子写 + `ok=false`），本报告实证 |
| R-078 x265 无 threads / 线程预算口径 | **仍在**（HEIF-06 / AVIF-03） |
| R-316 param-catalog 漂移 | 多数已修；TIFF/WebP/HEIF 仍有若干计数/标签漂移 |
| UX-14 A1（HEIF nclx 恒 sRGB） | **已修**（nclx 从 ICC 反推，VUI 跟随） |
| UX-10.1 档位行 | **已做**；但 HEIF Main 死档位、AVIF Main 位深限幅、`current_chroma` 类型错读是新增问题 |
| R-135 预设未知键丢失 | 已用 `st->extra` 兜住；但切技术丢编辑值（X-10）仍在 |

**本报告新增的高价值发现（未被既有评审登记）**：

- **alpha 语义端到端**（PNG-01 及跨格式）：原生字节复核，含 WebP/JXL 的预乘数据证据。
- **TIFF 多页结构损坏**（TIFF-01）：3 页源第 3 页越界不可解码。
- **JPEG 色度采样在量化表之后**（JPEG-01）：444 使用 420 表，DQT 64/64 命中。
- **JXL 无损 + resampling**（JXL-01）：UI 可达的假无损。
- **JXL 三个 Modular setter**（JXL-03）：一改就整文件失败。
- **JXL `color_transform` no-op + 默认 YCoCg 2.8×**（JXL-02）。
- **WebP `preset` 死控件**（WEBP-03）。
- **WebP 技术/无损双真相**（WEBP-01/02）。
- **AVIF GUI 默认 `qp=0`**（AVIF-01）：12–32× 体积、quality 失效。
- **HEIF 默认 10 位 = RExt**（HEIF-03）。
- **HEIF Main 死档位**（HEIF-01）。
- **HEIF/AVIF 无损非逐位**（HEIF-12 / AVIF 章）：RGB↔YCbCr 舍入。
- **预设不覆盖色彩全局项/档位**（X-08/X-07）。
- **`current_chroma()` 类型错读**（X-WIRING-07/AVIF-05）。

### 9.4 审计口径修正与存疑

1. **TIFF-03 已复核撤回**：该条原称"TIFF 写 straight 数据但标 associated（ExtraSamples=1）"。独立复核用同一 RGBA ramp 直接解析 TIFF strip 原始字节：alpha=128 处 RGB 为 `100,50,25`（= 200×0.5，premultiplied），alpha=64 → `50,25,13`，与 tag 338=1（associated）**一致**；`pp_verify exact` 通过不能证明 straight（读端同样按 associated 解释）。结论：TIFF 的标签与数据自洽，是**唯一自洽**的 alpha 格式；原条撤回，不再计入总表，TIFF 只作为 5.7 的对照出现。
2. **`--dev --param` 对 HEIF/AVIF 的类型缺陷**：本报告把"参数本身不生效"与"CLI 传参类型不生效"分开记；AVIF qp=0 的 GUI 路径用 v2 预设模拟（int64），不是直接 `--param`。
3. **HEIF/AVIF "无损"**：本报告按"码流层无损、RGB 往返有舍入"记，不把它与 PNG/TIFF 的逐位无损混为一谈；是否要求 RGB 逐位无损属产品裁决。
4. **参数计数**：不同章节按"唯一键"或"ParamDef 声明数"计数可能差 1–2（如 WebP 23/24、JXL 27/30），以第 4 章矩阵的键名为准；本报告不做强行统一。
5. **库行号**：OIIO 3.1.17 / libheif 1.23.5 / libjxl 0.12.0 / libwebp 1.6.0 / libtiff 4.7.2 / Exiv2 0.28.9；不同版本行号会漂移，复核时以函数名为主。

### 9.5 工具链版本（实测日志）

| 组件 | 版本 |
|---|---|
| PhotoPipeline | 0.3.5.alpha2 |
| Qt | 6.11.2 |
| OpenImageIO | 3.1.17.0 |
| libjxl | 0.12.0 |
| libheif | 1.23.5 |
| x265 | 4.3-vcpkg |
| SVT-AV1 | 4.2.0 |
| libaom | 3.15.1 |
| libwebp | 1.6.0 |
| libtiff | 4.7.2 |
| Exiv2 | 0.28.9 |
| lcms2 | 2.19 |

### 9.6 证据文件索引（gitignored，按需保留/清理）

| 路径 | 内容 |
|---|---|
| `build/encoder-audit/sections/*.md` | 第 4/6 章原始章节（15 份） |
| `build/encoder-audit/draft/*.md` | 主控摘要/方法/横向结论/修复顺序/覆盖/本附录 |
| `build/encoder-audit/assemble.py` | 报告装配脚本 |
| `build/encoder-audit/jpeg` / `jxl-rt` / `probe` / `tiff` / `webp` / `bmp` / `heif`/`out` / `avif-rt` | 逐格式产物、脚本、侧车 |
| `build/encoder-audit/meta` / `emp-a` / `exp1` | 仅元数据与运行时实证 |
| `build/audit-probe/` | 主控独立复核脚本与产物（alpha、DQT、box、preset、qp 等） |

> 说明：`build/` 被 `.gitignore` 排除；报告正文已内嵌关键证据（file:line + 数值），不依赖这些目录长期存在。若需复核，按 9.1 的命令重建。

### 9.7 主控独立复核记录（原生字节/参数对照，2026-10-25）

> 这些是主控在审计代理之外独立跑过的关键实证，用于交叉验证代理结论；全部经 `python tools/env.py run --`。

### A. WebP

1. `preset` 是否为假控件：
   ```
   photopipeline --dev tests\golden\base\rgb8.png --out build\audit-probe\webp-preset-<p> --format webp --param preset=<0|2|4> --workers 1
   ```
   结果：0/2/4 三档产物 **224 B、SHA-256 完全相同**（`7B867FAE3C0562F4…`）⇒ `preset` 对产物无影响。

2. `--tech lossless`（不勾无损）：
   ```
   --format webp --tech lossless
   ```
   结果：`pp_verify exact → VERIFY OK`；sidecar `params` 写 `lossless=false`、`tech=lossless` ⇒ 产物无损但参数面声明有损。

3. `near_lossless` 与无损：
   ```
   --format webp --lossless --param near_lossless=50
   ```
   256×256 随机噪声输入，`pp_verify exact` → `FAIL exact mismatch: 168542/196608 samples differ`，`warnings=[]` ⇒ 无损开关下产物非逐位无损且无告警。

### B. JPEG XL

4. 无损 + 降采样：
   ```
   --format jxl --lossless --tech modular --param resampling=2
   ```
   `pp_verify exact` → `FAIL exact mismatch: 196604/196608 samples differ`；侧车无 warning。

5. `color_transform` 四值（无损，256×256 噪声）：
   - `None/XYB/YCbCr` → 均 213913 B、同一 SHA；
   - `YCoCg` → 217098 B；
   - 四者 `pp_verify exact` 全 OK。
   ⇒ 前三值是 no-op；默认 YCoCg 改变体积。

6. 默认容器 box：
   ```
   python build/audit-probe/jxlbox.py
   ```
   → `signature/ftyp/jxll(9)/jxlc`，默认产物必带 `jxll`。

7. `--param channel_colors_global_percent=50`（modular）：
   → `encode failed: jxl: option channel_colors_global_percent rejected: ERROR`（错 setter）。

8. `distance=0`（VarDCT，无 ICC）：
   → `encode failed: jxl: distance 0.000000 rejected: ERROR`；加 `--color srgb` → 成功 807 B。

### C. JPEG

9. 四种色度采样的 DQT：
   ```
   python build/audit-probe/dqt.py
   ```
   → `444/440/422/420` 的 DQT 拼接 **195 B、SHA 相同**（`b957b9d1814e93ee…`），首系数 `[3,4,4,6,4,6]` ⇒ 量化表不随色度采样变化（与 JPEG-01 一致）。

10. `psnr_target` 覆盖：
    ```
    --format jpeg --param psnr_target=40 --param distance=<0.3|3.0>
    ```
    → 两产物 173104 B、SHA 完全相同（`7345CC3DA7BA7B2F…`）。

11. `chroma=440` 真支持：440 产物 69433 B vs 422 69419 B vs 420 51382 B ⇒ 采样确实生效（问题只在量化表）。

### D. TIFF

12. `compression=ccittrle`（RGB8）：
    → `encode failed: OIIO write_image failed … TIFFWriteScanline failed … (Bits/sample must be 1 for Group 3/4 encoding/decoding)`；`ok=false`，无残留半成品。

### E. HEIF / AVIF

13. HEIF `quality` 经 `--param`：
    ```
    --format heif --bitdepth 8 --param quality=<10|50|90>
    ```
    → 三者 **96722 B、同一 SHA**；日志显示 `unknown parameter key; forwarded to encoder`，值以 string 进入 ParamSet，`enc_heif` 的 `param_int` 回退 90 ⇒ CLI 整数运行时参数类型缺陷（GUI 走 int64，不受影响）。

14. HEIF 无损（`--lossless --bitdepth 8`）：`pp_verify exact → VERIFY OK`。

15. HEIF 无损 + `chroma=420`：强制 444，日志 `lossless forces chroma 444 (requested 420)`，**sidecar warnings 为空**。

16. HEIF 12-bit 无损（rgb16 输入）：`VERIFY OK`，伴随 `DepthDowngrade 16→12`。

17. AVIF 12-bit：
    - `--backend svt-av1` → 预检拒绝 `avif/svt-av1 does not support 12-bit (supported: 8,10)`；
    - `--backend libaom` → 成功 22784 B，`--lossless` 下 `VERIFY OK`。

18. AVIF 10-bit + alpha：
    - `svt-av1` → `encode failed: svt-av1 does not support 10-bit with alpha; use backend libaom`；
    - `libaom` → 成功 678 B。8-bit + alpha 两后端均成功（911 B / 676 B）。

### F. 仅元数据通路

19. JPEG：`--metadata-only --meta Exif.Image.Artist=AuditJPEG`，SOS 熵数据 782 B，SHA 前后相同（`d34ae8da…`）⇒ 像素码流逐字节不变。

20. PNG：IDAT 拼接 124 B，SHA 前后相同（`c5272a54…`）。

21. WebP：VP8 chunk 306 B，SHA 前后相同（`ebf4f336…`）。

22. TIFF：`pp_verify exact → VERIFY OK`（586 → 608 B，仅元数据变化）。

23. JXL：`--metadata-only` → `format 'jxl' does not support metadata-only rewrite (zero re-encode)`（设计 §5.5 承诺的 box 级替换未落地）。

### G. 杂项

24. PNG `--lossless` 与不带：455 B、同 SHA ⇒ 恒无损格式的“无损”开关无效果（BMP/TIFF 同理）。

25. BMP `--bitdepth 8` 与 `24`：RGB 源均 12342 B（24bpp）；gray 源均 5174 B（8bpp）⇒ 位深下拉不驱动产物（详见 BMP-01）。

### H. Alpha 语义（主控独立复核，2026-10-25）

统一输入：256×2 RGBA8，RGB 恒 `(200,100,50)`，alpha = x（0..255）。

1. **PNG 输出（原生字节解析，不经 OIIO）**：
   - alpha=0 → `(0,0,0,0)`；alpha=1 → `(255,0,0,1)`；alpha=2 → `(255,128,0,2)`；
   - alpha=16 → `(207,96,48,16)`；alpha=128 → `(199,100,50,128)`；alpha=255 → `(200,100,50,255)`。
   ⇒ 低 alpha 区 RGB 被不可逆改写（alpha=0 全丢）。

2. **JPEG 输出（flatten 到白底，再解码）**：alpha=128 处 `(177,152,139)`；正确未关联合成应为 `(228,178,153)`。
   **BMP 输出（原生字节）**：alpha=128 处 `(177,152,140)`；同一偏差。
   ⇒ `flatten` 公式 `src*a + bg*(1-a)` 作用在**已乘 alpha** 的缓冲上，二次相乘。

3. **WebP lossless 输出（libwebp `WebPDecodeRGBA` 原生解码）**：
   - alpha=0 → `(0,0,0,0)`；alpha=1 → `(1,0,0,1)`；alpha=16 → `(13,6,3,16)`；
   - alpha=128 → `(100,50,25,128)`；alpha=255 → `(200,100,50,255)`。
   ⇒ 存储的是 premultiplied RGB + unassociated alpha；任何按 WebP 规范合成的查看器都会再乘一次 alpha。

4. **JXL lossless 输出（`oiiotool --stats`）**：`Avg: 25700.00 12850.00 6425.00 32767.50 (of 65535)`
   ⇒ R 均值 ≈ 0.392 = 200/255 × 0.5（premultiplied），而 alpha 均值 0.5；码流声明 `alpha_premultiplied=false`。

5. 结论：**解码端 OIIO 默认关联 alpha，管线把这份 premultiplied 数据当未关联使用**。受影响的不是单一格式：
   - PNG 写端反关联 → 低 alpha 量化误差 + alpha=0 RGB 丢失；
   - WebP/JXL（以及同路径的 HEIF/AVIF）把 premultiplied RGB 当未关联写入 → 查看器二次相乘；
   - JPEG/BMP 的 `flatten` 二次相乘 → 半透明区域偏暗。
   这是一条**跨全部 alpha 通路**的 P0（数据损坏/静默改像素），且被 `pp_verify` 的 OIIO 对称读端掩盖（输入与输出都被关联，差异互相抵消）。

### 9.8 独立复核裁定（7 条关键结论）

| # | 结论 | 复核裁定 | 关键证据 | 归一后严重度 |
|---|---|---|---|---|
| 1 | alpha 语义端到端错位 | **upheld** | PNG 原生字节：alpha=0→(0,0,0)、1→(255,0,0)、128→(199,100,50)；BMP alpha=128→(177,152,140)（正确 227,177,152）；WebP `WebPDecodeRGBA` alpha=128→(100,50,25) | P0（拆分 PNG→PNG 的 P1 面） |
| 2 | TIFF 数据 straight / 标签 associated | **refuted** | TIFF strip alpha=128→(100,50,25)、tag338=1，数据与标签一致 | 撤回 |
| 3 | 多页 TIFF 第 3 页越界 | **upheld** | 4622 B / 3 IFD；IFD2 `StripOffsets=[2771273,8]`；`oiiotool --subimage 2` 报 `Read error at scanline 4294967295`，抽出全 0 图；`ok=true`、仅 `MultipageTruncated` | P0 |
| 4 | JXL 无损 + resampling | **downgrade** | `--lossless --param resampling=2` → exact FAIL 193371/196608；sidecar `lossless=true`、warnings=[]；默认 -1/1 不触发 | P1（原 P0） |
| 5 | WebP tech=lossy + lossless=true | **downgrade** | 与默认有损同 SHA、chunk=VP8；sidecar `lossless=true`；GUI `paramform` 自愈，仅 CLI/预设可复现 | P1（原 P0） |
| 6 | AVIF GUI 默认 qp=0 | **upheld** | 默认 17652 B/CQP31.5 vs GUI 等价预设 127141 B/CQP0（7.2×）；qp=0 时 quality=30/50 同 SHA；去 qp 后回到默认 | P1 |
| 7 | HEIF `--param quality` 失效 | **upheld** | 10/50/90 均 76280 B 同 SHA；typed 预设 1681/30422/76280；CLI 字符串→`param_int` 回退 90 | P2（CLI-only） |

> 复核代理另注：工作树含未提交的 0.3.6 改动（`enc_heif`/`pipeline`/`paramform` 等），但其复核涉及的 alpha/多页/`dev_cli`/`param_int` 路径均在现行源码逐行核对，实测与 0.3.5.alpha2 描述一致。
