# PhotoPipeline 参数清单（草案 v0.1）

> 状态：**草案**——静态表格式为设计预估，实现期逐 API 头文件核对定稿；
> HEIF / AVIF 全部参数由 libheif **运行时内省**生成（`heif_encoder_list_parameters`），本清单仅列预期项；
> 所有参数的可见性/锁定谓词逻辑见 `design.md` §2.1。
>
> 通用规则：
> - `位深` 是格式级参数（放全局区，可选值随格式联动），不在各技术参数组内
> - `无损` 是 **lossless-capable 技术上的显式 schema 参数**（key = `lossless`，bool，默认 false。
>   0.3.0/M4-T13 起；`src/core/params.h` 的 `kLosslessParamKey`）：
>   - **用户面**：可见可设 —— 控件 = 格式参数卡顶部的「无损」复选框（该参数不另出行为，
>     避免同一布尔出两个控件）；随预设 v2 的 `outputs[i].params.lossless` 序列化落盘。
>   - **声明面**：JXL（§2.2/§2.3）与 WebP（§5.1/§5.2）由静态表逐技术声明；HEIF（§3）与 AVIF
>     **两个后端**（§4.1 SVT-AV1 / §4.2 libaom）由 libheif 运行时内省提供同名字段
>     （实测内省列表：x265 与 SVT-AV1 的 `heif_encoder_list_parameters` 均含 `lossless`；
>     见 `pp_linkprobe` 的 `libheif-params-*` 行）；JPEG/PNG/TIFF/BMP **不声明**该参数。
>   - **引擎面**：另有**内部管道键** `__lossless`（谓词 `eval_visible`/`eval_lock` 与 enc_jxl/
>     enc_webp 的既有输入；不落盘、不显示、不进 `values()`）。两键由 `apply_locks()` 同步，
>     读取走 `pp::lossless_flag()`（显式参数优先 → 内部键 → false）。
>     例外口径（T13 复核项 6）：**enc_webp 的读取链多一段前导** ——
>     `tech_id`（`"lossless"`→true / `"lossy"`→false，0.2 起冻结的编码器分派）→ 显式参数 →
>     内部键。即 tech 被显式钉死时以 tech 为准；UI 侧因"无损 ⇒ 自动切 lossless_capable 技术"
>     不变式不会产生分歧，只有手写/迁移出的矛盾预设才会（UI 载入时经表单自愈，CLI 路径不自愈）。
>   - **非声明格式的保真**（T13 复核项 1）：JPEG/PNG/TIFF/BMP 上 0.2 顶层 `lossless` 由输出级
>     `outputs[i].lossless` JSON 字段承载（它不冒充可校验参数，故不进本清单的键表）。
>   - 勾选后锁定谓词生效（锁定项显示但禁用）
> - 默认值统一遵循**视觉透明档**（共识决策）
>
> 0.3.0（M4-T13）同步记录：本清单的表格是**设计期草案**，实现期以 `src/core/format_tables.cpp`
> 为唯一事实源（下表已补入 T13 新增的 `lossless` 行；其余条目的 key/范围在 M0-PT1 起即以头文件
> 核对为准，与草案行数不必一一对应）。

---

## 1. JPEG — jpeg-li（单后端，单技术）

| key | 标签 | 类型 | 范围/选项 | 默认 | 级别 | 备注 |
|---|---|---|---|---|---|---|
| quality_mode | 质量控制方式 | enum | distance / quality | distance | 核心 | 二选一联动下方控件 |
| distance | 视觉距离 | float | **0.1–3.0** | 1.0 | 核心 | `quality_mode=distance` 时可见。**0.4.0/M12 修订（R-294）**：UI 下限 0.3 → **0.1**（`src/core/format_tables.cpp` 的 jpegli `distance` `.lo`），与出厂 `default` 的 JPEG 自适应策略「1 以下步长 0.1 精扫到 0.1」对齐——旧下限会让 0.1–0.3 段不可达；tooltip 同步为 [0.1, 3.0] |
| quality | 质量 | int | 1–100 | 90 | 核心 | `quality_mode=quality` 时可见 |
| chroma | 色度采样 | enum | 444 / 440 / 422 / 420 | 444 | 核心 | R-316 勘误：旧表漏 440（实现四选一，format_tables 为准） |
| progressive | 渐进式 | bool | | true | 核心 | |
| optimize_coding | 哈夫曼表优化 | bool | | true | 高级 | 渐进式开启时锁定 true |
| arith_code | 算术编码 | bool | | false | 高级 | **已禁用（R-244）**：jpegli 硬拒算术编码（勾选 100% 失败），锁定 false + 禁用原因 |
| restart_in_rows | 重启间隔（行） | int | 0–64 | 0 | 高级 | |
| dct_method | DCT 方法 | enum | ISLOW / IFAST / FLOAT | ISLOW | 高级 | **已禁用（R-246）**：jpegli 固定浮点 DCT，选择不改变输出（装饰性），锁定 0 |
| smoothing_factor | 平滑 | int | 0–100 | 0 | 高级 | |
| xyb_mode | XYB 色彩量化 | bool | | false | 高级 | **已禁用（R-245）**：承诺"需嵌 XYB ICC"但产品无该路径，锁定 false + 禁用原因 |
| adaptive_quantization | 自适应量化 | bool | | true | 高级 | R-316 补录（反向未列项） |
| std_quant_tables | 标准量化表 | bool | | false | 高级 | R-316 补录 |
| psnr_target | PSNR 目标 | float | 0–100 | 0 | 高级 | R-316 补录；**联动提示（R-247）**：>0 静默覆盖 quality_mode/distance/quality |
| cicp_transfer_function | CICP 传递函数 | int | 0–18 | 2 | 高级 | R-316 补录；仅 16=PQ、18=HLG 有实际效果 |

格式级：位深 `{8}`（jpeg-li 仅 8 位输出，已查证）；alpha 不支持 → 全局底色合成。jpegli 同时是**全程序唯一 libjpeg**（OIIO 的 JPEG 解码亦由其执行，复审 S2）。

> **M12 更新（0.4.0，R-294 修订，2026-11-01）**：JPEG `distance` 的 UI 范围由 **0.3–3.0** 放宽为 **0.1–3.0**
> （`src/core/format_tables.cpp` 的 jpegli `distance` `.lo` 0.3 → 0.1，tooltip 同步 `[0.1, 3.0]`）。原因：出厂
> `default` 预设的 JPEG 自适应策略「distance 从 3.0 降，1 以下步长 0.1 精扫到 **0.1**」需要 0.1 段可达；
> 旧下限会让 0.1–0.3 段不可达（库允许仍是 0.0–25.0，UI 收窄口径不变）。设计/策略真源 =
> [`docs/adaptive-policy.md`](adaptive-policy.md) §6.1/§10、[`docs/format-presets.md`](format-presets.md) §7.1；
> 裁定见 [`docs/m12-adjudications.md`](m12-adjudications.md) A6。

## 2. JPEG XL — libjxl（单后端，双技术）

> **M10 / W2-T5 更新（0.4.0 C22，2026-10-31）**：本节的键集合/默认值/作用域以
> `src/core/format_tables.cpp` 的 jxl FormatDef + `src/codecs/param_catalog/jxl_catalog.inc`
> 为准。W2-T5 把 43 键治理为 **31 键**（删除 12 个结构性 no-op 行），并修正三态、
> `epf` 默认、`brotli_effort` 作用域、`codestream_level` 只读、`color_transform` 枚举、
> `channel_colors_*` 类型与技术作用域。设计真源 = `docs/v0.4.0-design.md` §7。

### 2.1 公共（跨技术）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 说明 |
|---|---|---|---|---|---|---|
| effort | 编码努力 | int | 1–11 | 7 | 核心 | 11 = 像素无损极限档；编码器自动 `AllowExpertOptions`（W2-T5 起无 expert_options 行） |
| decoding_speed | 解码速度预算 | int | 0–4 | 0 | 高级 | 0=最慢解码/最好密度 |
| codestream_level | 码流级别（自动，只读） | enum | auto(-1) | auto | 高级/只读 | 实际 level 由库按图像推导（8/16 位 VarDCT→5；16 位无损→10）；不可编辑 |
| distance | 视觉距离 | float | V:0–25 / M:0–15 | V:1.0 / M:0.0 | 核心 | 两技术各自默认见 §2.2/§2.3 |
| lossless | 无损 | bool | — | false | 核心 | 显式 schema 参数（顶部「无损」复选框）；true ⇒ Modular |
| resampling | 编码前降采样 | enum | auto/1/2/4/8 | auto | 高级·**实验/危险** | 会丢分辨率；红字 tooltip；禁止作为自适应目标 |
| brotli_effort | Brotli 努力 | int | -1–11 | -1 | 高级 | **仅 `box_compress=1` 可见**（J5）；作用域 Both（W2-T5 修） |
| box_compress | 元数据 box 压缩 | bool | — | false | 高级 | 元数据工具（Exif/XMP 的 brob 压缩） |
| upsampling_mode | 解码升采样核 | enum | 自动/0/1 | 自动 | 高级·**实验/危险** | 仅 resampling=2/4/8；真实改码流（红字） |
| output_mode | 输出流模式 | enum | 0/1/2 | 0 | 高级·**实验/危险** | mode2=乱序 jxlp，老解码器不兼容 |
| disable_perceptual_heuristics | 关闭感知优化 | bool | — | false | 高级·**实验/危险** | 仅 non-XYB（无损/Modular）可用；有损 VarDCT 必报错 |
| intensity_target | HDR · 峰值亮度 | float | 0–10000 | 0 | 高级·HDR 折叠 | **会改变像素与评分**（b17 +2.80），红字；SDR 勿动 |
| min_nits | HDR · 最低亮度 | float | 0–10000 | 0 | 高级·HDR 折叠 | 色调映射提示 |
| relative_to_max_display | HDR · 相对最大显示 | bool | — | false | 高级·HDR 折叠 | linear_below 的单位 |
| linear_below | HDR · 线性阈值 | float | 0–10000 | 0 | 高级·HDR 折叠 | 色调映射提示 |
| intrinsic_size | 推荐显示尺寸 | text | 宽x高 | 空 | 高级 | 元数据工具（容器字段；J14） |
| frame_name | 帧名称 | text | ≤1071B | 空 | 高级 | 元数据工具；非空禁用 fast-lossless（J13） |

### 2.2 技术：VarDCT（有损）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 说明/谓词 |
|---|---|---|---|---|---|---|
| distance | 视觉距离 | float | 0–25 | 1.0 | 核心 | 无损 → 隐藏（库恒走 Modular） |
| photon_noise | 感光噪声 | float | 0–6400 | 0 | 高级·**实验/危险** | 解码端合成颗粒，实测评分 −3.34；红字 |
| epf | 边缘保持滤波 | int | -1–3 | **-1（跟随库启发式）** | 高级 | W2-T5 默认 2→-1；显式 0–3 仍有效 |
| gaborish | Gaborish 滤波 | bool（三态） | 跟随默认/开/关 | **未设置=不下发** | 高级 | W2-T5 三态；库按 effort/distance 启发式 |
| dots | 点阵生成 | bool（三态） | 跟随默认/开/关 | **未设置=不下发** | 高级 | 库阈值 distance≥3.0 |
| patches | 图块复用 | bool（三态） | 跟随默认/开/关 | **未设置=不下发** | 高级 | 库按内容检测贴片 |

> 三态机制 = M8 的 `has_default=false`：键不在 `ParamSet` ⇒ 不下发；旧预设里显式
> true/false 仍逐字生效。GUI 呈现为「跟随默认/开/关」三态下拉。
> `epf` 未改三态：int 的 `-1` 本身即库「auto」语义（与三态等价，按设计选择未设置/自动呈现）。

### 2.3 技术：Modular（无损/混合）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 说明/谓词 |
|---|---|---|---|---|---|---|
| distance | 视觉距离 | float | 0–15 | 0 | 核心 | 0=完全无损；>0 为有损 Modular；无损 → 锁定 0 |
| color_transform | 色彩变换 | enum | auto / YCoCg | auto | 高级 | W2-T5 收窄；None/XYB/YCbCr 是 C-API no-op，已删 |
| modular_group_size | 组尺寸 | int | -1–3 | -1(auto) | 高级 | 核心 11 键之一 |
| modular_predictor | 预测器 | int | -1–15 | -1(auto) | 高级 | 核心 11 键之一 |
| modular_palette_colors | 调色板颜色数上限 | int | -1–4096 | -1(auto) | 高级 | 有损 Modular 下库清零（J9 隐藏） |
| modular_lossy_palette | 有损调色板 | bool | — | false | 高级 | 无损锁定 false；有损 Modular 下隐藏（J9） |
| channel_colors_global_percent | 全局调色板阈值 % | **float** | -1–100 | -1(auto) | 高级 | W2-T5 Int→Float（库只有 Float setter） |
| channel_colors_group_percent | 局部调色板阈值 % | **float** | -1–100 | -1(auto) | 高级 | 同上 |
| modular_ma_tree_learning_percent | MA 树学习比例 % | float | -1–100 | -1(auto) | 高级 | 核心 11 键之一；有损 Modular 下 =0 硬失败（J9） |
| modular_nb_prev_channels | 前序通道属性数 | int | -1–11 | -1(auto) | 高级 | — |

### 2.4 已删除行（12，W2-T5）与历史预设

删除依据 = 结构性 no-op / 未兑现能力（侦察报告 §1.6；逐条证据见
`build/design-recon/jxl/report.md` §1.2）：

| key | 原因 |
|---|---|
| keep_invisible | 产品不输出 alpha，逐字节 no-op（b11） |
| noise | 库阈值 99 ⇒ 恒不启用（b15 逐字节） |
| expert_options | 纯门；effort=11 现自动 AllowExpertOptions |
| uses_original_profile | 只剩「无效果/报错」两态（有损 true 被 M9 拒绝，b32） |
| group_order / group_order_center_x / group_order_center_y | 渐进渲染存放顺序，批量转码无价值 |
| progressive_ac / qprogressive_ac / progressive_dc | 只影响渐进解码分层，不改最终画质 |
| responsive | 渐进/流式专用，批量转码无价值 |
| buffering | 产品整图输入路径 no-op（b25/b51 逐字节） |

历史预设/`--param` 携带上述键时**显式失败**，不静默接受、不静默套默认：
运行前预检给中文「已移除参数：<key>（原因），请从预设/参数集中删除该键」（`param_rules`
J19）；编码器兜底也以英文点名拒绝。预设加载通道（`validate_preset`）对同一键以既有的
「未知参数：<key>」拒绝（同一语义，文案差异见 `build/m10/w2-jxl/report.md` 未决项）。

### 2.5 技术作用域（W2-T5 修复）

* **VarDCT-only**：`photon_noise`、`epf`、`gaborish`、`dots`、`patches`；Modular 路径下
  显式隐藏/置灰（不再静默跳过）。
* **Modular-only**：`color_transform`、`modular_*`、`channel_colors_*`；VarDCT 路径下同理。
* **禁止静默切技术**：技术只由显式 `tech` / `lossless` 决定；预设里残留跨技术键
  ⇒ 运行前预检中文错误（`param_rules` J1），编码器也显式失败（不再按
  「存在 Modular 键」把整条输出切成 Modular）。
* 其余键（`resampling`/`brotli_effort`/`output_mode`/`disable_perceptual_heuristics` 与
  目录 Both 行）两技术通用。

### 2.6 实验/危险组与自适应

「实验/危险」5 键 = `resampling`/`upsampling_mode`/`output_mode`/
`disable_perceptual_heuristics`/`photon_noise`：红字 tooltip（`【实验/危险 · 禁止作为
自适应目标】`）、默认折叠；**注册表禁止作为自适应搜索目标**（`docs/v0.4.0-design.md`
§9.4 禁止表）。分组字段即机器可读标记（GUI 现有组名「调试/实验」，见
`docs/m8-param-control-map.md` §6.4）。
HDR 4 键（`intensity_target`/`min_nits`/`relative_to_max_display`/`linear_below`）归入
「心理视觉」折叠组，标签带 `HDR ·` 前缀；`intensity_target` 红字注明会改变像素与评分。

格式级：位深 `{8, 16}`；alpha **不支持**（输入在色彩转换前拍平；全格式同口径）；
始终写容器格式（元数据需要 box）。<!-- W2-T5 修订 R-316 漂移：旧文写"alpha 支持"，与
format_tables 的 supports_alpha=false 不符。 -->

## 3. HEIF — libheif + x265（单后端；参数运行时内省）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality | 质量 | int | 0–100 | 视觉透明档校准值（实现期实测） | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| chroma | 色度采样 | enum | 420 / 422 / 444 | **420**（R-316 勘误：旧表写 444，实现缺省 420） | 核心 |
| preset | 画质预设 | enum |（插件值集） | 插件当前值 | 核心 |
| tune | 画质取向 | enum | psnr / ssim / grain / fastdecode / animation（仿真）/ zerolatency·vmaf（置灰） | ssim | 核心（UX-10.2 提核心；W2-T3/C20） |
| (其余内省项：tu-intra-depth / complexity …) | | | | | 高级 |

**编码档位（Profile，UX-10.1 / I4，M7-WP2 新增核心行；M9 校正为真实信号；W2-T2 补 still 变体）**：
profile = **HEVC 真实信号档位**（hvcC `general_profile_idc` + 色度 + 位深），决定解码兼容性，
**不是** ICC profile。libheif 插件不暴露 profile 参数 —— 原生信号由插件按位深/色度/静止图推导
（x265 `determineLevel`：8 位 4:2:0 单帧→Main Still Picture idc3；8 位 4:2:0 intra→RExt
idc4；8 位 4:2:0 非 intra→Main idc1；10 位 4:2:0 intra→RExt idc4；10 位 4:2:0 非 intra→
Main10 idc2；4:2:2/4:4:4/12 位→RExt idc4），非原生信号由 `enc_heif` 注入内部 x265 参数
（`total-frames=2` / `keyint=2` / `repeat-headers=1`，见 `docs/m9-adjudications.md` A3）。
UI 档位行（`pp-profile-row`）的可选项 = **自动 + 12 个既有真实信号（Main / Main10 / RExt 9 项 /
MainStill）+ 6 个 still 变体(16–21)，共 19 项**（`Main12` 是兼容别名，不在下拉内；AVIF 页签另有
Auto/Main/High/Professional 4 项）；编码前经反查，编码后再用 **hvcC + SPS** 回读校验 idc/level/chroma/bit-depth 与
still 语义，名不副实即失败。单源规则与映射见 `src/codecs/heif_profile.h`。旧预设编号 0–6 不重排
（`__heif_profile` 由 preset_io 持久化），新编号只追加（16–21）。

**W2-T2（C18）still 变体 16–21（编号追加，不重排）**：

| 编号 | 枚举 | UI 标签 | idc | 色度 | 位深 | still 语义 | 编码路径 | 可用性 |
|---|---|---|---|---|---|---|---|---|
| 16 | `Main10Still` | Main 10 Still Picture（idc 2 · 10 位 · 4:2:0） | 2 | 420 | 10 | PTL **无** still 位（非 RExt 的 44 位是保留位）→ 只能靠 SPS DPB 0/0 | 同 Main10 三项注入 | **不可用**（SPS DPB 实测 4/2 → UI 置灰，编码后断言失败，理由如实） |
| 17 | `Main422_10Still` | Main 4:2:2 10 Still Picture（idc 4） | 4 | 422 | 10 | SPS `intra=1` + `one_picture_only=1`（+ max_10/422 上界位） | 插件原生 | 可用 |
| 18 | `Main422_12Still` | Main 4:2:2 12 Still Picture（idc 4） | 4 | 422 | 12 | 同上 | 插件原生 | 可用 |
| 19 | `Main444_Still` | Main 4:4:4 Still Picture（idc 4） | 4 | 444 | 8 | 同上（标准名无 “8” 后缀） | 插件原生 | 可用 |
| 20 | `Main444_10Still` | Main 4:4:4 10 Still Picture（idc 4） | 4 | 444 | 10 | 同上 | 插件原生 | 可用（**与 Rext444_10 是同一真实信号**） |
| 21 | `Main444_12Still` | Main 4:4:4 12 Still Picture（idc 4） | 4 | 444 | 12 | 同上 | 插件原生 | 可用 |

- **为什么必须解析 SPS**：libheif 生成 hvcC 时**跳过 48 位 constraint**（`hevc_boxes.cc:628-630`），
  实测所有产物 hvcC 的 constraint 字节 `000000000000` → still 语义只在 SPS。`heif_capability`
  因此新增 `heif_parse_sps_ptl`（从 hvcC 内嵌 SPS NAL 原始字节、去 `00 00 03` 防竞争字节，
  解析 idc + 48 位 constraint + DPB）；编码后对 16–21 断言，失败给中文错误。
- **Main10Still 不可用是实测结论，不是实现缺口**：x265 4.3 `level.cpp:100` 注明 10 位没有
  still profile；要写 idc=2 必须走非 intra 参数，SPS 的 DPB 就是 4/2（要求 0/0）。UI 置灰 +
  原因；预设/CLI 带 16 进来时编码后断言会给出同一份理由（不静默、不伪造 SPS）。
- **既有标签修正**（信号与编号不变）：7 `Rext420_8` → 「Main Intra（4:2:0 8；idc 4；非 still）」
  （标准没有 Main 4:2:0 8）；10 `Rext422_8` → 「Main 4:2:2 10（8 位信号；idc 4）」；
  13 `Rext444_8` → 「Main 4:4:4（8 位；idc 4）」。
- **Apple 兼容性（如实，不做承诺）**：Main / Main Still / Main 10 文档化；**RExt 档不在 Apple
  文档集内 → UI warn「RExt 档，iPhone 接受度未验证」**；Main 10 Still 中性说明（本后端不可用）。
- **C19 下拉「看不全」**：档位/参数下拉的弹出层按最长项自适应宽度（受屏幕可用宽 80% 约束）+
  item 尾部省略 + item tooltip 全文（置灰项的原因 tooltip 保留在前）。实现单源 =
  `src/ui/paramform.cpp` 的 `pp::ui::combo_popup_autosize`（通用 combo 构造点调用），
  `page_output.cpp` 的档位下拉复用同一实现。标签本身是短标签，完整信号说明进 tooltip
  （`pp::heif_profile_tooltip`）。
- **探针口径**：新增档位打破了「档位下拉末项 = MainStill」的既有 `--ui-smoke` 约定 →
  档位遍历探针必须**按 value 定位**（`findData(档位编号)`），不得依赖下标/末项。

**Level（`x265:level-idc`，W2-T1 起核心区）**：按 x265 4.3 表做「8 像素补齐后的
MaxLumaPs + 宽高 ≤ sqrt(8*MaxLumaPs)」预校验；显式 Level 装不下整图时，`heif_grid=自动`
会按 Level 自动分块（每 tile 单独满足 Level），`heif_grid=关闭` 给中文可操作错误（点样本数/
上限/最小可行 Level/启用自动分块）。合法 Level：10/20/21/30/31/40/41/
50/51/52/60/61/62/63/70/71/72（85=无损专用）；6.3/7.x 是 x265 4.3 真实支持的 Level。
**语义要点：grid 输出时 Level 约束的是每个 tile，不是整图**（grid 是派生图）。
**still 单图档位 + Level>5.1（C17' / 规则 H8）**：Main Still Picture（idc 3）与 RExt still
变体（16..21）搭配显式 Level>5.1 时，tile 规划按 still 图互操作上界 **Level 5.1**
（3GPP TS 26.140 §4.5 / TS 26.141 §4.2：HEIF 'heic'+'MiHB' 互操作档 = HEVC Main /
Main Tier / Level 5.1，媒体类型参数 `itemTypes="hvc1.1.2.L153.B0"`），**编码 Level 仍按用户值、
不静默改参数**；行内 H8 + 运行日志 warn 说明该偏差。注意：5.1 是**应用格式/互操作上界**，
不是 H.265 profile 强制（H.265 V9 的 still picture profile 没有 Level 上限，详见
`docs/heif-tile-sizing.md` 证据表）。

**分块（grid，W2-T1 新增普通目录行，C16/C17'）**：

| key | 标签 | 类型 | 范围/选项 | 默认 | 级别 | 说明 |
|---|---|---|---|---|---|---|
| `heif_grid` | 分块模式 | enum | 关闭 / **自动** / 总是 | 自动（未设置） | 高级 | 自动=显式 Level 装不下整图时自动分块；Level=自动 时常规尺寸单图，仅整图超出 x265 最高 Level 7.2 容量（MaxLumaPs=142,606,336 且宽高 ≤ sqrt(8×M)）才自动分块；总是=强制分块（tile 尺寸由 Level 容量推导，每 tile 由 x265 自动选 Level）；关闭=单图，超 Level 预检报错 |
| `heif_tile_w` | Tile 宽 | int | 0=自动；64–8192（8 向下对齐） | 0 | 高级 | 仅 `heif_grid≠关闭` 可见；单轴 0 表示该轴继续自动求解 |
| `heif_tile_h` | Tile 高 | int | 同上 | 0 | 高级 | 同上 |

- **默认产物零变化**：`heif_grid` 缺省 = 自动，且 Level=自动 + 常规尺寸 → 单图，与 HEAD
  逐字节一致（W2-T1 的 SHA256 门禁）。
- **tile 自动推导（C17'，0.4.0.alpha2；删除旧 Apple 1024×576 固定基准）**：tile 尺寸由
  **规划 Level 的 MaxLumaPs(M) + 单边上限 D=floor(sqrt(8M))** 与图像宽高推导——
  在 8 像素对齐、[64, min(图宽, D)] 内枚举 tw，`th = align8(min(图高, D, floor(M/tw)))`，
  取面积最大者（`tw*th ≤ M`，可恰好等于 M），面积相同取 `|tw/th − 图宽高比|` 最小者；
  无任何固定基准/魔法常数。示例：3902×5854 + L3.1 → **768×1280 = 983,040 = L3.1
  MaxLumaPs 整值**（6×5）；L3.0 → 576×960（7×7）；L1.0 → 144×256（28×23）。
  `Level=自动` 的规划上界 = still 互操作档 Level 5.1（M=8,912,896）。`rows×cols==1`
  → 降级单图 + info 日志。公式与规范证据：`docs/heif-tile-sizing.md`。
- **手动覆盖**：非 8 倍数按 8 向下对齐（行内提示）；<64 或 >8192 → 预检错误；显式 Level 下
  `tw*th > MaxLumaPs` 或 `tw/th > sqrt(8*MaxLumaPs)` → 中文预检错误（点名上限、
  最大可行 tile、三条出路：改自动 / 提高 Level / 关闭 grid），不静默降级。
- **MIAF 奇偶**：4:2:0 需宽高偶数、4:2:2 需宽偶数（7.3.11.4.2）→ 只 warn 不阻断
  （801×601→grid 的既有单测行为保留）。
- **grid 结构上限（M10 收口补登记）**：HEIF grid 的 `rows`/`cols` 字段是 u8+1 ⇒ **单边 ≤256 且
  `rows×cols ≤65535`**；超限给中文 `error_zh`（点名上限与实际 tile 数），**不写坏件**
  （实现 `kMaxGridSide=256` / `kMaxGridTiles=65535`，`src/codecs/heif_capability.cpp`）。
- **Level=自动 + 手动 tile ⇒ warn（规则 H5，M10 收口补登记；C17' 更新文案）**：该组合**允许
  但告警** —— tile 尺寸用用户值、每 tile 的 Level 由 x265 自动选择（规划上界为 still 互操作档
  Level 5.1），「iPhone 接受度未验证」；只提示、不改 tile（实现 `src/core/param_rules.cpp`
  的 `H5`，与 UI note 同源）。
- **规划摘要**：运行日志输出 `tile`（每 tile 尺寸）/ `grid`（cols×rows）/ `tile_level`
  （显式 Level 或「自动（x265 逐 tile 选择）」）/ `tiles`（总 tile 数）/ `mode`，
  以及 still 上界压缩的 `planned_grid`/`planning_level`/`warning`；UI 行内
  note 用同一份纯函数结果（不编造未发生的数值）。
- **兼容别名**：`__heif_grid`（0=关闭/1=总是/2=自动）保留为只读兼容读别名，旧预设 /
  `--dev --param __heif_grid=…` 仍生效；新配置一律写 `heif_grid`（0=关闭/1=自动/2=总是）。
  两键同时出现时 `heif_grid` 优先并记日志。

**tune（W2-T3 / C20；不 patch 依赖）**：键仍是 `tune`（预设兼容关键），下拉扩为 7 项 ——
4 原生 + 1 仿真 + 2 置灰（"不撒谎且可解释"：置灰项保留位置，原因进 item tooltip）：

| 值 | 类别 | 引擎处理 / 原因 |
|---|---|---|
| `psnr` / `ssim` / `grain` / `fastdecode` | 原生 | 原样 `heif_encoder_set_parameter_string("tune", v)`（插件白名单 4 值） |
| `animation` | 仿真 | 插件 tune 保持默认 `ssim`；引擎在**用户透传之前**注入 `x265:psy-rd=0.4` / `x265:aq-strength=0.4` / `x265:deblock=1:1`（x265 tune=animation 的静止图相关效果）。用户显式覆盖这些键 ⇒ 后写覆盖（以用户为准）；tooltip 注明「近似仿真」 |
| `zerolatency` | 置灰 | 单帧无实际效果（只改线程/GOP 类）；插件白名单也未收录 |
| `vmaf` | 置灰 | x265 4.3 `tune=vmaf` 是空实现（`param.cpp:710-713`），不产生任何效果 |

- **别名归一**：`fast-decode` / `zero-latency` 是 x265 别名，插件白名单不收且不单列。
- **机制 tooltip（如实）**：插件在 preset/tune 之后**强制** `aq-mode=1, psy-rd=1.0,
  psy-rdoq=1.0`（`encoder_x265.cc:923-933`），故 x265 tune 的 psy/AQ 部分被覆盖；残留差异 =
  psnr 的 `aq-strength=0` / grain 的 `sao=0` / fastdecode 的 `deblock+sao=0`。实测分数
  （1600×1200 10bit 420 q75）：psnr 93.64 / ssim 92.83 / grain 91.89 / fastdecode 92.77。
- **单源事实表**：`src/codecs/enc_tune_facts.{h,cpp}`（后端 → 值 → 类别(原生/仿真/置灰) →
  引擎处理 → 中文原因）。运行时内省仍是原生集合第一来源；一致性由
  `tests/unit/test_heif_tune.cpp` 机械断言：原生事实 ⊆ 插件接受集、插件接受集 ⊆ 事实表、
  仿真/置灰值 ∉ 插件接受集（依赖升级静默漂移会红）。
- **置灰呈现**：`paramform` 对 `tune` 的 Disabled 项用 `QStandardItem::setEnabled(false)` +
  item tooltip（【不可用】+ 原因）；预检（G8）与编码器对置灰值给同一份中文错误。

**速率控制（RC 模式，W2-T4 / C21）**：`RcMode{Quality,Crf,Qp,CqLevel}` 仍是显式模式下拉
（HEIF 质量/CRF/QP；libaom 质量/CQ Level；SVT 质量/QP），默认「质量」= 既有 `quality` 路径
（零行为变化）。W2-T4 增量：

- **数值行位置**：`quality` / `x265:crf` / `x265:qp`（以及 libaom `aom:cq-level`、SVT `qp`）
  由同一工厂重建到**核心区**，紧随 `pp-param-rcmode-<format>` 模式行（顺序 = 模式下拉顺序）；
  非当前路径的数值行隐藏（`m8_row_visible`），行对象常驻 ⇒ 切模式不重建、objectName
  （`pp-param-row-x265:crf` 等）不变。默认质量模式下 CRF/QP 本就隐藏 ⇒ 默认布局不增行。
- **质量行标签**：「质量（0–100；CRF=(100−q)/2）」（仅 x265；quality=50 ⇔ CRF 25 已实测
  字节相同）。标签轨 80px 省略，全文与映射说明进 tooltip。
- **动态域**：`x265:crf`/`x265:qp` 合法域随位深 —— 8bit `0..51`、10bit `−12..51`、
  12bit `−24..51`（x265 `param.cpp:1740,1875-1879`）。位深切换时 UI lo/hi 与行内注记
  「位深 N 位域 −12..51」同步；预设/CLI 越界值由预检 X1C 报中文错误（不静默钳值）。
- **预检**：`x265:qp` + 显式 `x265:level-idc`（>0）→ X12 **错误**（"恒定 QP 与显式 Level
  不兼容：请改用 CRF/质量模式，或把 Level 设为自动"；编码器侧兜底同一文案）。
  `x265:crf` + 显式 Level → X12 **告警**（x265 强制 VBV，码率/结果非确定）。
- **预设兼容**：键名不变（`quality`/`x265:crf`/`x265:qp`），模式由"存在的键"推断（同时出现
  crf+qp ⇒ 预检错误）；切模式时非激活键保留在工作集但**不下发、不落盘**（`public_values`）。

**运行期保留键（`__` 前缀，不落预设；`docs/m9-adjudications.md` A2/A10）**：
| key | 含义 |
|---|---|
| `__heif_profile` | 编码信号档位（int；0=Auto 原生；1=Main idc1；2=Main10 idc2；3=Main12 legacy；4=MainStill idc3；7–9=RExt420_8/10/12；10–12=RExt422_8/10/12；13–15=RExt444_8/10/12；16=Main10Still（实测不可用）；17–21=Main422_10Still/Main422_12Still/Main444_Still/Main444_10Still/Main444_12Still）。旧版本读 16–21 走越界显式错误（不静默） |
| `__heif_grid` | 兼容读别名（W2-T1 起）：HEIF 有损 grid 模式（0=关闭；1=总是 Apple 式分块；2=自动，缺省）；新键 `heif_grid` 优先，语义见上表 |

格式级：位深 `{8, 10, 12}`（R-316 勘误：旧表漏 12）；alpha 支持（grid 除外；有损 grid +
alpha 在引擎侧明确拒绝，不写入坏件）。
⚠️ 内省若显示 x265 暴露面过窄 → 记录为 R4，接受"库面"口径。

## 4. AVIF — libheif + SVT-AV1 / libaom（双后端；参数运行时内省）

**编码档位（Profile，UX-10.1 / I4）**：AV1 档位 = `Main / High / Professional`，由插件按
色度/位深自动推导（SVT：4:2:2 或 >10bit → Professional、4:4:4 → High、其余 → Main；
encoder_svt.cc:946-951）。档位行与反查同 HEIF（§3）。

### 4.1 后端：SVT-AV1（默认）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality | 质量 | int | 0–100（映射） | 视觉透明档校准值 | 核心 |
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；实测 SVT-AV1 内省列表含 `lossless`；控件 = 顶部「无损」复选框） |
| speed | 速度档 | int |（插件值集） | 插件当前值 | 核心（UX-10.2 提核心；R-316 勘误：SVT **无 `preset` 参数**，旧表 `preset/speed` 并列是错位） |
| tune | 画质取向 | enum | vq / psnr / ssim / iq / ms-ssim（5/5 原生；W2-T3 补中文标签） | psnr | 核心（UX-10.2 提核心） |
| chroma | 色度采样 | enum | 420 / 422 / 444 | 420 | 核心 |
| (其余内省项：threads / qp / min-q / max-q / tile-rows / tile-cols …) | | | | | 高级 |

- **W2-T3（C20）联动提示**：SVT 的 `tune=iq` / `tune=ms-ssim` 会把 BRC 从 **CQP 切到 CRF**
  （`encoder_svt.cc:873`）——`qp` 路径的实际码率不再由 qp 决定；`vq/psnr/ssim` 保持 CQP。
  该提示在 UI 行 tooltip 与预检（G8 告警）双写；5 值全部为插件原生（无仿真/置灰项）。

R-316 勘误：旧表 `tier`（码流层级 0/1）**实现不存在**（SVT 插件内省面无此键），已删。

### 4.2 后端：libaom（参考级）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality / cq-level | 质量 | int | 0–100 | 校准值 | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| tune | 画质取向 | enum | auto / psnr / ssim / iq（原生）+ ssimulacra2 / vmaf_saliency_map（仿真）+ vmaf / butteraugli / cdef-dist（置灰） | auto | 核心（W2-T3/C20） |
| chroma | 色度采样 | enum | 420/422/444 | 420 | 高级 |
| usage / cpu-used | 速度 | int | 0–10 | 4 | 高级 |
| (其余内省项) | | | | | 高级 |

- **W2-T3（C20）libaom tune 通道**：原生 4 值走插件；`ssimulacra2` /
  `vmaf_saliency_map` 插件白名单不认，由引擎内部走 **`aom:tune=<v>`** custom option
  （libheif aom 插件在全部内置控制之后应用，最后生效）——**不新增 `aom:tune` 目录行**
  （避免双控件），预设键仍只有 `tune`。本构建实测两者均可编码。
- **置灰项**：`vmaf`（含 `vmaf_with_*`/`vmaf_neg`）/ `butteraugli` / `cdef-dist` 因
  `CONFIG_TUNE_*` 未启用，编码期被库拒绝 → 下拉保留位置 + 置灰 + 原因，预检/编码器给中文错误。
- **联动**：`tune=ssimulacra2` 会启用库的 QM/锐化自动项，可能与手工
  `aom:enable-qm`/`qm-min`/`qm-max`/`sharpness` 重叠（预检 G8 告警；`tune=iq` 见 A3）。

格式级：位深 `{8, 10}`；alpha 支持。后端选择本身作为参数记录进预设/日志。

## 5. WebP — libwebp（单后端，双技术）

> **一键两义（R-209）**：key `quality` 在有损技术下是**质量分**、在无损技术下是**压缩努力**
> （WebPConfig::quality 的库语义如此）。key 保留不拆（拆 = 预设序列化键变更），两面标签/
> tooltip 已注明各自语义（format_tables 的 webp_quality_param 工厂单点声明）。

### 5.1 技术：有损

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；控件 = 顶部「无损」复选框） |
| quality | 质量 | int | 1–100 | 90 | 核心 |
| sharp_yuv | 锐利色度上采样 | bool | | true | 核心 |
| method | 压缩方法 | int | 0–6 | 4 | 高级 |
| preset | 内容预设 | enum | default/picture/photo/drawing/icon/text | photo | 高级 |
| sns_strength | 空间噪声整形 | int | 0–100 | 50 | 高级 |
| filter_strength | 去噪滤波 | int | 0–100 | **30**（R-316 勘误：旧表写 20，实现 = preset=photo 生效值 30） | 高级 |
| autofilter | 自动滤波 | bool | | false | 高级 |
| pass | 通道数 | int | 1–10 | 1 | 高级 |
| filter_sharpness | 滤波锐度 | int | 0–7 | 0 | 高级 |
| filter_type | 滤波类型（强/简） | bool | | true | 高级 |
| segments | 段数 | int | 1–4 | 4 | 高级 |
| alpha_compression | Alpha 压缩 | bool | | true | 高级 |
| alpha_filtering | Alpha 预测滤波 | int | 0–2 | 1 | 高级 |
| alpha_quality | Alpha 质量 | int | 0–100 | 100 | 高级 |
| partitions | Token 分区数 log2 | int | 0–3 | 0 | 高级 |
| partition_limit | 分区退化上限 | int | 0–100 | 0 | 高级 |
| preprocessing | 预处理滤波 | int | 0–7 | 0 | 高级 |
| qmin / qmax | 最小/最大量化因子 | int | 0–100 | 0 / 100 | 高级（交叉规则：qmin ≤ qmax） |
| emulate_jpeg_size | 模拟 JPEG 体积 | bool | | false | 高级 |
| low_memory | 低内存模式 | bool | | false | 高级 |

### 5.2 技术：无损

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；控件 = 顶部「无损」复选框） |
| quality | 努力（借 quality 参数） | int | 0–100 | 80 | 核心 |
| exact | 保留透明区 RGB 原值 | bool | | true | 高级 |
| method | 压缩方法 | int | 0–6 | 4 | 高级 |
| near_lossless | 近无损强度 | int | 0–100 | 100 | 高级（R-316 补录） |

格式级：位深 `{8}`；alpha 支持。

## 6. PNG — OIIO（单后端，单技术）

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| compressionLevel | zlib 压缩级别 | int | 0–9 | 6 | 核心 |（R-010 修复：写入 OIIO 属性键 `png:compressionLevel` —— 旧裸键无人消费、调 0–9 无效；0 与 9 体积显著不同，test_enc_oiio 钉住）

格式级：位深 `{8, 16}`；alpha 支持；灰度支持。
⚠️ 以 OIIO PNG 输出属性文档核对补全（R5 口径：OIIO 即库面）。

## 7. TIFF — OIIO（单后端，技术=压缩方案）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 谓词 |
|---|---|---|---|---|---|---|
| compression | 压缩方案（技术选择器） | enum | none / lzw / zip / ccittrle / packbits | lzw | 核心 | R-316 勘误：旧表 `deflate/zstd/jpeg` 不存在（zip = deflate 别名上游只认 zip；zstd/jpg-in-tiff 未接）、漏 `ccittrle` |
| deflate_level | Deflate 级别 | int | 1–9 | 6 | 高级 | `compression=zip` 可见 |
| predictor | 预测器 | enum | none / horizontal / float | horizontal | 高级 | lzw/zip 可见；控制面真实生效（none/horizontal 字节不同，R-011 复核）；**float 对整型输出不可用**（libtiff 浮点预测器仅浮点样本），选中即明确报错 |
| tiff_tile_width | Tile 宽度 | int | 0–4096（16 的倍数） | 0(strip) | 高级 | |
| tiff_tile_height | Tile 高度 | int | 0–4096（16 的倍数） | 0(strip) | 高级 | |

R-316 勘误：旧表 `quality`（jpeg-in-tiff）与 `zstd_level` **实现不存在**，已删。
格式级：位深 `{8, 16}`；alpha 支持；灰度支持。

## 8. BMP — OIIO（无参数）

格式级：位深 **`{8, 24}`**（R-090 勘误：编码器实际接受 24 与 8 —— 24 = RGB 24bpp（每样本
8 位，default_bitdepth=24）、8 = 灰度 8bpp 的真实口径；旧表只列 {24} 与实现矛盾）；
alpha 不支持（全局底色合成）；无元数据容器（EXIF 丢弃 + 日志；OIIO BMP writer 不落 ICC）。

---

## 汇总

| 格式 | 后端 | 技术 | 参数数（约） | 位深 | alpha | 灰度 |
|---|---|---|---|---|---|---|
| JPEG | jpegli | 1 | 15 | 8 | ✗ | ✓ |
| JPEG XL | libjxl | 2 | 31 | 8/16 | ✗ | ✓ |
| HEIF | x265 | 1 | 3+（内省）+档位行 | 8/10/12 | ✓ | 以RGB编码+日志 |
| AVIF | SVT-AV1 / libaom | 2 | 内省 + 档位行 | 8/10/12 | ✓ | 以RGB编码+日志 |
| WebP | libwebp | 2 | 23 | 8 | ✓ | 以RGB编码+日志 |
| PNG | OIIO | 1 | 1 | 8/16 | ✓ | ✓ |
| TIFF | OIIO | 5(压缩方案) | 5 | 8/16 | ✓ | ✓ |
| BMP | OIIO | 1 | 0 | 8/24 | ✗ | ✓ |

**合计约 80–100 项 + 格式级参数**，符合"全量用户参数（排除 API 内部/废弃/重复）"口径。
（R-316 修订：本表与 `src/core/format_tables.cpp` 逐键对齐；"不存在"的键已删、
"反向未列"的键已补、假接线/禁用项已标注 —— 与 deep-review §G2 抽核口径一致。）

灰度规则（复审 S1）：原生灰度保留仅限 JPEG/PNG/TIFF/JXL 且"保持原样"模式；WebP/HEIF/AVIF 以 RGB 编码 + 日志；任何色彩转换统一走"源→RGB(A)"单一路径。

## M8 更新（2026-10-28 起草；**0.3.6.alpha1 首个发行件**，2026-10-29）

本清单为 0.3.x 草案，M8 起以**评审稿 + 目录**为准：

- 参数范围：`docs/m8-param-exposure-review.md`（x265 ≈40 / libaom ≈50 / SVT 10 / JXL ≈42 组行；键展开 116 行；W2-T5 后 JXL 43→31 键、目录总行 116→**117**（HEIF +3、JXL −2），见 `build/m10/w2-jxl/report.md`）
- 控件形态/联动：`docs/m8-param-control-map.md`（M/N/D/S/T/R × 锚点/依赖/独立；X/A/S/J 规则）
- 机器可读目录：`src/codecs/param_catalog/*_catalog.inc`（由 `tools/param_catalog_specs/*.json` + `tools/gen_param_catalog.py` 生成）
- 实现与验收：`docs/m8-report.md`；新增参数默认 **不设置**（跟随编码器默认），确定性行为与既有 7/10/28 键不变。
- 发行口径：`0.3.6.alpha1`（版本单源 `CMakeLists.txt:19-24` = 数字段 0.3.6 + 标签 ".alpha1"）；
  逐条变更见 [CHANGELOG.md](../CHANGELOG.md) `[0.3.6.alpha1]`，发布验收见
  [docs/0.3.6.alpha1-report.md](0.3.6.alpha1-report.md)。

## M12 更新（0.4.0 正式版，2026-11-01）

- **JPEG `distance` UI 范围 0.3–3.0 → 0.1–3.0**（R-294 修订）：`src/core/format_tables.cpp` jpegli
  `distance` 的 `.lo` 0.3 → 0.1、tooltip 同步 `[0.1, 3.0]`；库允许范围仍是 0.0–25.0（UI 收窄口径不变）。
  动因 = 出厂 `default` 的 JPEG 自适应策略「1 以下步长 0.1 精扫到 0.1」；否则 0.1–0.3 段不可达。
  真源：[docs/adaptive-policy.md](adaptive-policy.md) §6.1/§10、[docs/format-presets.md](format-presets.md) §7.1；
  裁定：[docs/m12-adjudications.md](m12-adjudications.md) A6。本清单 §1 行内注记同步（表格上方已加 M12 更新块）。
- **本批未改其它参数行/默认值**（参数目录仍为 117 行；`jpegli` 其余键与 M10/M11 一致）。逐条变更见
  [CHANGELOG.md](../CHANGELOG.md) `[0.4.0]`。
