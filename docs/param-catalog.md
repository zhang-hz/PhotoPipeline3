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
| distance | 视觉距离 | float | 0.3–3.0 | 1.0 | 核心 | `quality_mode=distance` 时可见 |
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

## 2. JPEG XL — libjxl（单后端，双技术）

### 2.1 公共（跨技术）

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| effort | 编码努力 | int | 1–10 | 7 | 核心 |
| decoding_speed | 解码速度预算 | int | 0–4 | 0 | 高级 |
| codestream_level | 码流级别 | enum | 5 / 10 | 10 | 高级 |

### 2.2 技术：VarDCT（有损）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 谓词 |
|---|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心 | **显式 schema 参数**（0.3.0/T13；控件 = 顶部「无损」复选框，不另行出行为）；true → `distance` 锁定 0（libjxl 自动走 Modular） |
| distance | 视觉距离 | float | 0–25 | 1.0 | 核心 | 无损开启 → 锁定 0（libjxl 自动切 Modular，界面提示） |
| photon_noise | 感光噪声模拟 (ISO) | float | 0–6400 | 0 | 高级 | 无损 → 隐藏 |
| epf | 边缘保持滤波 | int | -1–3 | 2 | 高级 | 无损 → 隐藏 |
| keep_invisible | 保留不可见像素 | bool | | true | 高级 | |
| dots | 点阵生成 | bool | | true | 高级 | R-316 补录（反向未列项） |
| patches | 图块复用 | bool | | true | 高级 | R-316 补录 |
| gaborish | Gaborish 滤波 | bool | | true | 高级 | R-316 补录 |
| progressive_ac | AC 渐进（谱进） | bool | | false | 高级 | R-316 补录 |
| qprogressive_ac | AC 渐进（量化位） | bool | | false | 高级 | R-316 补录 |
| progressive_dc | DC 渐进级数 | int | -1–2 | -1(auto) | 高级 | R-316 补录 |
| resampling | 编码前降采样 | enum | auto / 1 / 2 / 4 / 8 | auto | 高级 | 无损 → 隐藏；R-316 补录 |
| group_order | 256×256 组顺序 | enum | auto / scanline / center | auto | 高级 | 无损 → 隐藏；R-316 补录 |

R-316 勘误：旧表的 `use_dct4/8/16` 与 `responsive` **实现不存在 / 归属错位** ——
`use_dct*` 从未实现（libjxl 0.12 无对应选项）；`responsive` 是 **Modular** 技术的参数
（已移至 §2.3）。

### 2.3 技术：Modular（无损/混合）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 谓词 |
|---|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心 | **显式 schema 参数**（0.3.0/T13；控件 = 顶部「无损」复选框）；Modular + true 即真无损 |
| distance | 视觉距离 | float | 0–15 | 0 | 核心 | 0=完全无损；>0 为有损 Modular |
| color_transform | 色彩变换 | enum | None / YCoCg / XYB / YCbCr | YCoCg | 高级 | R-316 勘误：旧表漏 YCbCr（实现四选一） |
| modular_group_size | 组尺寸 | int | -1–3 | -1(auto) | 高级 | |
| modular_predictor | 预测器 | int | -1–15 | -1(auto) | 高级 | |
| modular_palette_colors | 调色板颜色数上限 | int | -1–4096 | -1(auto) | 高级 | R-316 勘误：旧表 `modular_palette`（auto/off/on）不存在，实现为整数上限 |
| modular_lossy_palette | 有损调色板 | bool | | false | 高级 | 无损 → 锁定 false |
| brotli_effort | Brotli 努力 | int | -1–11 | -1(auto) | 高级 | |
| responsive | 响应式渐进 | bool | | false | 高级 | （自 §2.2 移正：Modular 语义） |
| channel_colors_global_percent | 全局调色板阈值 % | int | -1–100 | -1(auto) | 高级 | R-316 补录 |
| channel_colors_group_percent | 局部调色板阈值 % | int | -1–100 | -1(auto) | 高级 | R-316 补录 |
| modular_ma_tree_learning_percent | MA 树学习比例 % | int | -1–200 | -1(auto) | 高级 | R-316 补录 |
| modular_nb_prev_channels | 前序通道属性数 | int | -1–11 | -1(auto) | 高级 | R-316 补录 |
| resampling | 编码前降采样 | enum | auto / 1 / 2 / 4 / 8 | auto | 高级 | R-316 补录 |
| group_order | 256×256 组顺序 | enum | auto / scanline / center | auto | 高级 | R-316 补录 |

格式级：位深 `{8, 16}`；alpha 支持；始终写容器格式（元数据需要 box）。

## 3. HEIF — libheif + x265（单后端；参数运行时内省）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality | 质量 | int | 0–100 | 视觉透明档校准值（实现期实测） | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| chroma | 色度采样 | enum | 420 / 422 / 444 | **420**（R-316 勘误：旧表写 444，实现缺省 420） | 核心 |
| preset | 画质预设 | enum |（插件值集） | 插件当前值 | 核心 |
| tune | 画质取向 | enum |（插件值集） | 插件当前值 | 核心（UX-10.2 提核心） |
| (其余内省项：tu-intra-depth / complexity …) | | | | | 高级 |

**编码档位（Profile，UX-10.1 / I4，M7-WP2 新增核心行）**：profile = 编码档位
（HEVC `Main / Main10 / Main12 / Main Still Picture`），决定解码兼容性，**不是** ICC profile。
libheif 插件不暴露 `profile` 参数 —— 档位由插件按位深自动推导（x265：8→Main（静止图→
Main Still Picture）、10→Main10、12→Main12；encoder_x265.cc:793-802）。UI 档位行
（`pp-profile-row`）呈现推导结果/接受显式选定，编码前经 `heif_profile_compatible` 反查，
不一致拒绝并报中文错误（`src/codecs/heif_profile.h` 单源规则）。

格式级：位深 `{8, 10, 12}`（R-316 勘误：旧表漏 12）；alpha 支持。
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
| tune | 画质取向 | enum |（插件值集） | 插件当前值 | 核心（UX-10.2 提核心） |
| chroma | 色度采样 | enum | 420 / 422 / 444 | 420 | 核心 |
| (其余内省项：threads / qp / min-q / max-q / tile-rows / tile-cols …) | | | | | 高级 |

R-316 勘误：旧表 `tier`（码流层级 0/1）**实现不存在**（SVT 插件内省面无此键），已删。

### 4.2 后端：libaom（参考级）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality / cq-level | 质量 | int | 0–100 | 校准值 | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| chroma | 色度采样 | enum | 420/422/444 | 420 | 高级 |
| usage / cpu-used | 速度 | int | 0–10 | 4 | 高级 |
| (其余内省项) | | | | | 高级 |

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
| qmin / qmax | 最小/最大量化因子 | int | 0–100 | 0 / 100 | 高级 |（交叉规则：qmin ≤ qmax） |
| emulate_jpeg_size | 模拟 JPEG 体积 | bool | | false | 高级 |
| low_memory | 低内存模式 | bool | | false | 高级 |

### 5.2 技术：无损

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；控件 = 顶部「无损」复选框） |
| quality | 努力（借 quality 参数） | int | 0–100 | 80 | 核心 |
| exact | 保留透明区 RGB 原值 | bool | | true | 高级 |
| method | 压缩方法 | int | 0–6 | 4 | 高级 |
| near_lossless | 近无损强度 | int | 0–100 | 100 | 高级 |（R-316 补录）

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
| JPEG XL | libjxl | 2 | 30+ | 8/16 | ✓ | ✓ |
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
