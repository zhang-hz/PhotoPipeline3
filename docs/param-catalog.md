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
| chroma | 色度采样 | enum | 444 / 422 / 420 | 444 | 核心 | |
| progressive | 渐进式 | bool | | true | 核心 | |
| optimize_coding | 哈夫曼表优化 | bool | | true | 高级 | |
| arith_code | 算术编码 | bool | | false | 高级 | tooltip 注明兼容性风险 |
| restart_in_rows | 重启间隔（行） | int | 0–64 | 0 | 高级 | |
| dct_method | DCT 方法 | enum | ISLOW / IFAST / FLOAT | ISLOW | 高级 | jpegli 内部浮点，语义按 libjpeg 兼容层核对 |
| smoothing_factor | 平滑 | int | 0–100 | 0 | 高级 | |
| xyb_mode | XYB 色彩量化 | bool | | false | 高级 | 实验性；启用需嵌 XYB ICC |

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
| epf | 边缘保持滤波 | int | 0–3 | 2 | 高级 | 无损 → 隐藏 |
| keep_invisible | 保留不可见像素 | bool | | true | 高级 | |
| responsive | 响应式渐进 | bool | | false | 高级 | |
| use_dct4/8/16 | DCT 块尺寸启用 | bool ×3 | | true | 高级 | 实现期核对枚举 |

### 2.3 技术：Modular（无损/混合）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 谓词 |
|---|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心 | **显式 schema 参数**（0.3.0/T13；控件 = 顶部「无损」复选框）；Modular + true 即真无损 |
| distance | 视觉距离 | float | 0–15 | 0 | 核心 | 0=完全无损；>0 为有损 Modular |
| color_transform | 色彩变换 | enum | None / YCoCg / XYB | YCoCg | 高级 | 无损+None 联动提示 |
| modular_group_size | 组尺寸 | int | -1–3 | -1(auto) | 高级 | |
| modular_predictor | 预测器 | int | -1–15 | -1(auto) | 高级 | |
| modular_palette | 调色板 | enum | auto / off / on | auto | 高级 | |
| modular_lossy_palette | 有损调色板 | bool | | false | 高级 | 无损 → 锁定 false |
| brotli_effort | Brotli 努力 | int | -1–11 | -1(auto) | 高级 | |

格式级：位深 `{8, 16}`；alpha 支持；始终写容器格式（元数据需要 box）。

## 3. HEIF — libheif + x265（单后端；参数运行时内省）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality | 质量 | int | 0–100 | 视觉透明档校准值（实现期实测） | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| chroma | 色度采样 | enum | 420 / 422 / 444 | 444 | 核心 |
| (其余内省项) | | | | | 高级 |

格式级：位深 `{8, 10}`（Main10）；alpha 支持。
⚠️ 内省若显示 x265 暴露面过窄 → 记录为 R4，接受"库面"口径。

## 4. AVIF — libheif + SVT-AV1 / libaom（双后端；参数运行时内省）

### 4.1 后端：SVT-AV1（默认）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality / cq | 质量/CQ | int | 0–100（映射） | 视觉透明档校准值 | 核心 |
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；实测 SVT-AV1 内省列表含 `lossless`；控件 = 顶部「无损」复选框） |
| preset / speed | 速度档 | int | 0–13 | 4 | 核心 |
| tier | 码流层级 | enum | 0 / 1 | 0 | 高级 |
| (其余内省项) | | | | | 高级 |

### 4.2 后端：libaom（参考级）

| key | 标签 | 类型 | 范围 | 预期默认 | 级别 |
|---|---|---|---|---|---|
| quality / cq-level | 质量 | int | 0–100 | 校准值 | 核心 |
| lossless | 无损 | bool | | false | 核心 |
| chroma | 色度采样 | enum | 420/422/444 | 444 | 高级 |
| usage / cpu-used | 速度 | int | 0–10 | 4 | 高级 |
| (其余内省项) | | | | | 高级 |

格式级：位深 `{8, 10}`；alpha 支持。后端选择本身作为参数记录进预设/日志。

## 5. WebP — libwebp（单后端，双技术）

### 5.1 技术：有损

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；控件 = 顶部「无损」复选框） |
| quality | 质量 | int | 1–100 | 90 | 核心 |
| sharp_yuv | 锐利色度上采样 | bool | | true | 核心 |
| method | 压缩方法 | int | 0–6 | 4 | 高级 |
| preset | 内容预设 | enum | default/photo/picture/drawing/icon/text | photo | 高级 |
| sns_strength | 空间噪声整形 | int | 0–100 | 50 | 高级 |
| filter_strength | 去噪滤波 | int | 0–100 | 20 | 高级 |
| autofilter | 自动滤波 | bool | | false | 高级 |
| pass | 通道数 | int | 1–10 | 1 | 高级 |

### 5.2 技术：无损

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| lossless | 无损 | bool | | false | 核心（0.3.0/T13 显式 schema 参数；控件 = 顶部「无损」复选框） |
| quality | 努力（借 quality 参数） | int | 0–100 | 80 | 核心 |
| exact | 保留透明区 RGB 原值 | bool | | true | 高级 |
| method | 压缩方法 | int | 0–6 | 4 | 高级 |

格式级：位深 `{8}`；alpha 支持。

## 6. PNG — OIIO（单后端，单技术）

| key | 标签 | 类型 | 范围 | 默认 | 级别 |
|---|---|---|---|---|---|
| compressionLevel | zlib 压缩级别 | int | 0–9 | 6 | 核心 |
| (interlace 等) | | | | | 高级 |

格式级：位深 `{8, 16}`；alpha 支持；灰度支持。
⚠️ 以 OIIO PNG 输出属性文档核对补全（R5 口径：OIIO 即库面）。

## 7. TIFF — OIIO（单后端，技术=压缩方案）

| key | 标签 | 类型 | 范围 | 默认 | 级别 | 谓词 |
|---|---|---|---|---|---|---|
| compression | 压缩方案（技术选择器） | enum | none / lzw / deflate / zstd / packbits / jpeg | lzw | 核心 | |
| quality | 质量（仅 jpeg-in-tiff） | int | 1–100 | 90 | 高级 | `compression=jpeg` 可见 |
| deflate_level | Deflate 级别 | int | 1–9 | 6 | 高级 | `compression=deflate` 可见 |
| zstd_level | ZStd 级别 | int | 1–22 | 9 | 高级 | `compression=zstd` 可见 |
| predictor | 预测器 | enum | none / horizontal / float | horizontal | 高级 | |
| (tile 参数等) | | | | | 高级 | 核对补全 |

格式级：位深 `{8, 16}`；alpha 支持；灰度支持。

## 8. BMP — OIIO（无参数）

格式级：位深 `{24}`；alpha 不支持（全局底色合成）；无元数据容器（EXIF 丢弃 + 日志）。

---

## 汇总

| 格式 | 后端 | 技术 | 参数数（约） | 位深 | alpha | 灰度 |
|---|---|---|---|---|---|---|
| JPEG | jpegli | 1 | 11 | 8 | ✗ | ✓ |
| JPEG XL | libjxl | 2 | 20 | 8/16 | ✓ | ✓ |
| HEIF | x265 | 1 | 3+（内省） | 8/10 | ✓ | 以RGB编码+日志 |
| AVIF | SVT-AV1 / libaom | 2 | 8+8（内省） | 8/10 | ✓ | 以RGB编码+日志 |
| WebP | libwebp | 2 | 12 | 8 | ✓ | 以RGB编码+日志 |
| PNG | OIIO | 1 | 2+ | 8/16 | ✓ | ✓ |
| TIFF | OIIO | 6(压缩方案) | 6+ | 8/16 | ✓ | ✓ |
| BMP | OIIO | 1 | 0 | 24 | ✗ | ✓ |

**合计约 75–95 项 + 格式级参数**，符合"全量用户参数（排除 API 内部/废弃/重复）"口径。

灰度规则（复审 S1）：原生灰度保留仅限 JPEG/PNG/TIFF/JXL 且"保持原样"模式；WebP/HEIF/AVIF 以 RGB 编码 + 日志；任何色彩转换统一走"源→RGB(A)"单一路径。
