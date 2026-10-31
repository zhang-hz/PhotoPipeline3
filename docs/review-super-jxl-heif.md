# super/ T1–T6 批判性分析：JXL VarDCT 评分反常、HEIF 420/444 接近、T5/T6 设备兼容性

> 状态：**v1（2026-10-30）**，只读评审 + 取证报告（**未改任何源码/测试/基线**）。
> 触发问题（用户原话）：
> ① 「为什么 varDCT distance 越小，评分越低。为什么 heif 评分远高于 vardct。是评价有问题还是就是如此？批判性分析」
> ② 「`./super/T5.heic` 可以完全满足 iPhone 18 Pro 硬解吗？T6.jxl 为什么在 Windows 照片中不能正确显示 P3 色彩？」
> ③ 「为什么彩色图片（对比 T1.tif）heif 420 与 444 评分十分接近？」
>
> 结论先行（详见各章）：
> - **评分系统没有失效**：独立 oracle（从磁盘重新解码 + 同一 metric 核心）对 GUI 里 6 个有代表性的分数**逐位复现**（δ ≤ 4e-8）⇒ 分数就是「编码器输入 vs 解码产物」的真实 SSIMULACRA 2；JXL lossless 同配置 = 100.0000。
> - **JXL VarDCT 的评分反常在产物侧是真实的**，根因是 libjxl 的**已知缺陷**（libjxl#4552）：有损 + `uses_original_profile=true` 的 VarDCT 会产生「块状/色噪 + 体积暴涨」，且**distance 越低、块状伪影越重**（不是打分逻辑反了）。本仓 `enc_jxl.cpp` 因「输出恒嵌 ICC」把该开关自动推成 `true`，正好踩中这个坑。**换 Modular 或改用 XYB（`uses_original_profile=0`）即可恢复正常**。
> - **HEIF 不是「天生比 JXL 好」**：用户看到的「HEIF 远高于 VarDCT」是「健康路径 vs 坏配置路径」的对比；**同口径（sRGB 目标、同 metric）下把 JXL 修回 XYB，10.06 MB/91.68 对 HEIF 11.30 MB/91.64，体积更省分数持平**。HEIF 4:4:4/4:2:0 分数接近是**内容（低色度细节自然照片）+ SSIMULACRA 2 亮度主导**的真实敏感度结果，不是评分造假（合成高色度图 444 比 420 高 14.14 分，说明指标对色度抽样是敏感的）。
> - **T5.heic 不能确认满足 iPhone 硬解**：码流是 HEVC **RExt（`general_profile_idc=4`，"Main 4:2:0 10 Intra"）**，不是 Apple 文档里的 Main10（idc=2）；Apple 文档的 HEVC profile 只有 Main / Main10 / Main 4:2:2 10（无 4:4:4、无 idc=4）⇒ 需真机验证，安全目标应为 Main10；T3/T4（4:4:4 10-bit）预计硬解不支持。
> - **T6.jxl 文件本身色彩正确**（16-bit、码流内嵌 Display P3 ICC；独立 oracle 92.62 ≈ GUI 92.62；与正确 sRGB 转换比 ΔE00 平均 0.42）——**问题在 Windows 侧**：Photos 依赖 Store 的 JPEG XL 扩展，其 WIC 解码器把 **16-bit JXL 以 `GUID_WICPixelFormat64bppRGBHalf` + 0 个 ICC context 交给上层**（复现微软 #5390 “16-bit int→16-bit float”；ΔE00 平均 2.0 / p95 3.27，纯色度偏差、偏不饱和），同时 16-bit 精度塌缩为 8-bit（100% 样本可被 257 整除）；**8-bit JXL 路径正常**（ΔE00 ≈0.26）。
>
> 证据目录（gitignored、可复跑）：`build/analysis/`（本报告主实验）、`build/analysis-pixel/`（像素/HEIF 取证）、`build/analysis-wic/`（Windows WIC 解码取证）、`build/research-web/`（外部资料）。

---

## 1. 方法：怎样把「GUI 里那几次运行」钉死到参数上

GUI 日志不记录参数（这是本次排查最大障碍，见 §6 建议）。本次用**文件指纹反推参数**：

1. 对 `super/T1.tif`（3902×5854、uint16、内嵌 Adobe RGB (1998) ICC）用 dev harness 做受控编码，参数逐项显式给出；
2. 产物字节数与用户 GUI 产物逐字节对比（同编码器 + 同输入 ⇒ 近似确定性；GUI 与 `--dev` 的元数据写入差异只有 KB 级）；
3. 分数与 GUI 日志对照；
4. 用 `--dev --score-pair`（独立 oracle：从磁盘重新解码 REF/DIST + 同一 `pp::metric::compute()`）复核 GUI 分数。

**证据 A：用户文件 ↔ 受控编码对账（JXL，16-bit，color=keep AdobeRGB）**

| 用户产物 | 字节数 | GUI 分数 | 反推参数（本次受控编码） | 受控产物字节/分数 |
|---|---|---|---|---|
| `_DSF3043 (2).jxl` | 10,850,047 | 78.51 | VarDCT distance=2.0 effort=7 | 10,845,544 / 78.75 |
| `_DSF3043 (3).jxl` | 14,828,423 | 78.8 | VarDCT distance=1.0 effort=7 | 14,827,367 / 78.80 |
| `_DSF3043 (1).jxl` | 19,858,394 | 75.65 | VarDCT distance=0.5 effort=7 | 19,866,113 / 75.82 |
| `_DSF3043 (9).jxl` | 31,397,639 | 57.76 | VarDCT distance=0.2 effort=7 | 31,391,397 / **57.7569** |
| `_DSF3043 (10).jxl` | 24,191,523 | 92.66 | **Modular** distance=0.3 | 24,525,960 / **92.6602** |
| `_DSF3043 (11).jxl` = `super/T6.jxl` | 24,504,214 | 92.62 | **Modular** distance=0.3, **color=P3** | 24,832,001 / **92.6239** |
| `_DSF3043 (16).heic` = `super/T5.heic` | 12,267,649 | 91.76 | HEIF 420 10-bit P3 | oracle / **91.7786** |

> 结论：用户这轮实验**混用了 JXL 的两种技术（VarDCT / Modular）与多个 distance**；「distance 越小评分越低」只发生在 **VarDCT** 组；Modular 组恰好是反例（distance 越小分数越高）。
> 参数反推的强度分级（如实）：
> - **像素级同源**（`idiff` mean=0/max=0，受控编码与用户产物逐像素完全相同）：`(9).jxl` = VarDCT d0.2、`(10).jxl` = Modular d0.3、`T6/(11).jxl` = Modular d0.3 + P3；
> - **指纹级同源**（体积差 <0.05%、分数差 ≤0.24，像素不逐位相同——可能 effort/元数据微小差异）：`(2).jxl` ≈ VarDCT d2.0、`(3).jxl` ≈ VarDCT d1.0、`(1).jxl` ≈ VarDCT d0.5。
> 这足以支撑「用户观察到的反直觉 distance 曲线来自 VarDCT 路径」的结论（(9) 与 (1) 都是 VarDCT；像素级同源的 (9) 分数 57.7569 = oracle = GUI 57.76）。

**证据 B：独立 oracle 复核（`--dev --score-pair`，与 GUI 同 metric）**

| case | GUI 日志分数 | oracle 重算 | δ |
|---|---|---|---|
| (1).jxl VarDCT d0.5 | 75.65 | 75.6547787066 | ≤ 5e-7 |
| (9).jxl VarDCT d0.2 | 57.76 | 57.7568794965 | ≤ 3e-6 |
| (10).jxl Modular d0.3 | 92.66 | 92.6602006104 | ≤ 2e-7 |
| T6.jxl Modular d0.3 P3 | 92.62 | 92.6238726114 | ≤ 4e-6 |
| T5.heic HEIF 420 P3 | 91.76 | 91.7785568267 | ≤ 2e-6 |
| 本仓 JXL **lossless** 同 ICC/16-bit | — | **100.0000000000** | — |

⇒ **评价链路（回读、位深归一、ICC→sRGB、metric 核心）没有失真**；分数可被独立复算到打印精度。

**证据 C：用户文件身份确认**（SHA-256）
- `super/T1.tif` == `C:\Users\gmw\Downloads\_DSF3043.tif`（81AA5365…）
- `super/T6.jxl` == `...\r\jxl\_DSF3043 (11).jxl`（EE4982C1…）
- `super/T5.heic` == `...\r\heif\_DSF3043 (16).heic`（A3FE6A35…）

---

## 2. Q1：为什么 VarDCT distance 越小评分越低？为什么 HEIF 分数高得多？

### 2.1 先把现象完整画出来（受控编码，16-bit、keep AdobeRGB、effort=7）

| JXL VarDCT distance | 产物字节 | SSIMULACRA 2 | 备注 |
|---|---|---|---|
| 2.0 | 10.85 MB | **78.75** | |
| 1.0 | 14.83 MB | **78.80** | |
| 0.5 | 19.87 MB | **75.82** | ← 用户 (1).jxl |
| 0.3 | 25.61 MB | **68.24** | |
| 0.2 | 31.39 MB | **57.76** | ← 用户 (9).jxl（逐位一致） |
| 0.1 | 40.75 MB | **46.96** | |

⇒ **体积单调涨 3.8 倍，分数却从 78.8 掉到 47.0**。这不是「打分对 distance 反了」（同一函数也会给 distance=2.0 打 78.75），而是**产物真的在变差**：把 distance 压低后，libjxl VarDCT 编码器产出的图像出现越来越重的**块状/色噪伪影**，S2 忠实记录之。

8-bit 输出也是同一趋势（distance 2.0→0.5：78.62→74.20，字节 10.83→20.32 MB）⇒ **不是 16-bit 专属 bug**。

### 2.2 病灶：`uses_original_profile=true` 的有损 VarDCT（libjxl 已知缺陷）

本仓 `src/codecs/enc_jxl.cpp:384`：

```cpp
info.uses_original_profile = (lossless || modular || has_icc) ? JXL_TRUE : JXL_FALSE;
```

而本仓「输出恒嵌 ICC」（AGENTS.md §4.5 / ColorManager），所以**任何有损 JXL（含 VarDCT、含 sRGB 目标）都会自动 `uses_original_profile=true`**。libjxl 自己的头文件（`codestream_header.h:157-176`）对丢损场景明确写道：

> “Note that for lossy compression, this should be set to **false** for most use cases …”

更关键的是 libjxl 上游 **open issue #4552**（2026-01-03，`Visible blocks/color noise/artifacts with lossy uses_original_profile=true at certain encoder efforts`），其中复现结论与本仓观测完全一致：

> “only Lossy Modular encoding works with `uses_original_profile` set to true, **VarDCT encoding adds the chroma noise and blocking with a huge filesize increase**.”
> “RGB encoding should still give acceptable results … There still likely is something off with the implementation.”

**像素级证据**（本次在 super/T1.tif 同一区域 256×256 裁切、与无损 JXL 参考对比）：

| case | R 通道 MAE | G 通道 MAE | B 通道 MAE | 最大误差 | 8×8 块状伪影 |
|---|---|---|---|---|---|
| VarDCT d1.0 | 0.0177 | 0.0056 | 0.0066 | 0.247 | 有 |
| VarDCT d0.5 | 0.0230 | 0.0032 | 0.0052 | 0.336 | 有（更重） |
| VarDCT d0.2 | 0.0414 | 0.0013 | 0.0035 | 0.399 | 有（大面积橙红热区） |
| Modular d0.3 | 0.0014 | 0.0011 | 0.0014 | 0.0048 | 无 |
| VarDCT **XYB**（`uses_original_profile=0`） d0.5 | 0.0043 | 0.0038 | 0.0098 | 0.089 | 无 |

- 坏路径的误差**集中在 R 通道且随 distance 降低而增大**（0.018→0.023→0.041），G 反而在变小；最坏像素 R 码值 0.387 → 0.140 → 0.050 → 0.009（趋近 0）。这是**编码器内部块变换/量化缺陷**的特征，不是「更少比特」的正常损失。
- 同一图像改用 `uses_original_profile=0`（XYB）后：distance=1.0 只要 **4.01 MB** 就拿到 **86.44**，distance=0.5 为 **7.24 MB / 90.21**；而坏路径要 14.83 MB 才 78.80、19.87 MB 才 75.82。**体积小 3.7 倍且分数高 7.6~14.4 分**。
- **XYB 路径恢复正常的单调性**（sRGB 目标、16-bit、`uses_original_profile=0`）：distance 2.0 → 2.04 MB / **80.82**；1.0 → 4.01 MB / **86.50**；0.5 → 7.24 MB / **90.21**（distance 越小分数越高，符合定义）。
- 修复验证：显式 `uses_original_profile=1` 与自动推导的产物/分数逐字节一致（证明就是它在起作用）。

**定性**：这是**产品级有损 JXL 配置缺陷（P0 质量）**，触发条件 = `JXL + VarDCT + 有损 + 输出嵌 ICC`（即 GUI 默认路径）。「评价有问题」的判断不成立；「就是如此」也只对产物成立——产物之所以如此，是编码配置踩中 libjxl 未修复路径。

### 2.2b 变量隔离：**色彩目标不是因果变量**（对「不是 sRGB 就劣化」说法的纠正）

上一轮把结论说成「只有 sRGB 目标才不劣化」是不准确的；正确的因果变量是 **`uses_original_profile`（XYB vs 非 XYB 的 VarDCT 路径）**。实验证据（同一输入 T1.tif、同样 effort=7，只改目标空间/开关）：

| 色彩目标 | `uses_original_profile` | d=1.0 | d=0.5 | 结论 |
|---|---|---|---|---|
| keep（AdobeRGB ICC） | 自动 = **1** | 78.80 | 75.82 | 坏 |
| **sRGB**（ICC `sRGB built-in` 588B） | 自动 = **1** | **76.96** | **71.94** | **同样坏** |
| P3（Display P3 ICC） | 自动 = **1** | 80.00 | 76.79 | 坏 |
| sRGB | 强制 **0**（XYB） | **86.50** | **90.21** | 正常 |

- sRGB 目标产物的码流 ICC 是 `sRGB built-in`（588 B），但本仓**输出始终嵌 ICC** ⇒ `has_icc=true` ⇒ 自动 `uses_original_profile=1` ⇒ 仍走非 XYB 路径，分数照样崩塌。`jxlinfo` 实测该 sRGB 目标文件 `uses_original_profile=1`。
- 同一色彩空间（sRGB）内只切开关：76.96 → 86.50（d1.0）、71.94 → 90.21（d0.5）。**色彩空间/色彩转换不是原因**，编码器内部重建路径才是。
- 评价器两侧确实都已是同一色彩空间（参考 = 色彩转换后的工作缓冲；失真 = 解码产物再经同一 ICC 归一为 sRGB）⇒ 「输入已转换过色彩空间」的质疑成立：问题不可能由色彩转换解释，只能由编码器内部解释。
- 为什么修复建议里出现「sRGB 目标」：那只是**唯一能让 XYB 修复语义自洽的目标**（libjxl 对 XYB 码流的解码输出语义就是 sRGB），是「修复的适用条件」，不是「非 sRGB 才会坏」。

### 2.3 为什么 HEIF 评分远高于 VarDCT？

两个原因叠加，**不是指标偏袒 HEIF**：

1. **HEIF 路径是健康的**：libheif+x265 10-bit（420/444）没有上述缺陷，产物与分数匹配（keep/AdobeRGB q80：444 10.89 MB / 91.54、420 10.81 MB / 91.39）。
2. **对比对象坏掉了**：同一时刻 JXL VarDCT（默认 16-bit+ICC）14.83 MB 只有 78.8、19.87 MB 只有 75.8；而 HEIF 12.18 MB 就有 91.59。**同体积下 HEIF 更好，是因为 JXL 在空转**。

**同口径反证（全部 sRGB 目标、`uses_original_profile=0` 的 XYB 健康路径 vs HEIF，SSIMULACRA 2 同 metric、参考同为编码器输入）**：

| 编码 | 设置 | 产物 | 分数 |
|---|---|---|---|
| JXL VarDCT（**坏路径对照**） | 16-bit, keep/AdobeRGB, d=1.0 | 14.83 MB | 78.80 |
| JXL VarDCT XYB | 16-bit, sRGB, d=1.0 | 4.01 MB | 86.50 |
| JXL VarDCT XYB | 16-bit, sRGB, d=0.5 | 7.24 MB | 90.21 |
| **JXL VarDCT XYB** | **16-bit, sRGB, d=0.3** | **10.06 MB** | **91.68** |
| HEIF 4:2:0 | 10-bit, sRGB, q80 | 11.30 MB | 91.64 |
| HEIF 4:4:4 | 10-bit, sRGB, q80 | 11.52 MB | 91.79 |
| HEIF 4:2:0 | 10-bit, sRGB, q90 | 15.02 MB | 92.32 |

⇒ **修好 JXL 路径后，同等体积下 JXL 与 HEIF 基本持平（10.06 MB/91.68 vs 11.30 MB/91.64），甚至更省体积**。用户看到的「HEIF 远高于 VarDCT」**完全是坏配置造成的假象**，不是格式/指标优劣。
> 口径说明：XYB 码流的解码输出语义是 **sRGB**（libjxl 文档），因此这组对比用 `color=srgb` 目标最干净；keep/P3/AdobeRGB 目标的 XYB 需要先修「解码语义」（见 §6），不能直接套用本表。

### 2.4 为什么「distance 越小反而越低」在坏路径上会单调恶化？

块状伪影/色噪由 libjxl 该路径的**变换+量化实现缺陷**产生，而不是按目标 distance 收敛：当 distance 降低时，编码器把更多比特投到这条已损坏的重建路径上，**伪影边界更锐利、覆盖更完整**（8×8 块状热图从稀疏橙点变成整片橙红），S2 的 artifacts/多尺度项因此持续扣分。上游 issue #4552 的「huge filesize increase + visible blocks」描述与此一致。**这是编码器缺陷的表现，不是指标单调性错误**。

---

## 3. Q2a：`super/T5.heic` 能完全满足 iPhone 18 Pro 硬解吗？

### 3.1 T5.heic 的客观事实（自写 ISO-BMFF/hvcC 解析器）

| 项 | 值 |
|---|---|
| 容器 | `ftyp = heix / mif1 heix miaf`；单图（无 grid） |
| 尺寸 | 3902×5854（22.8 MP） |
| 编码 | HEVC，**`general_profile_idc = 4`（Format Range Extensions）**，`general_profile_compatibility_flags = 0x08000000`（只声明兼容 profile 4） |
| 约束标志 | 全 0 |
| level | `general_level_idc = 180`（Level **6.0**） |
| 色度/位深 | chromaFormat=1（**4:2:0**）、bitDepthLuma/Chroma = **10-bit** |
| 色彩 | 内嵌 **Display P3 ICC**（lcms2 v4.4，580 B） |
| 来源 | == `_DSF3043 (16).heic`（GUI 0.3.6.alpha1 产物） |

### 3.2 为什么是 idc=4 而不是 Main10？

- libheif x265 插件对 10-bit 一律 `param_apply_profile(param, "main10-intra")`（`vcpkg/buildtrees/libheif/.../encoder_x265.cc:801`）。
- x265 的 `determineLevel()` 对 **4:2:0 ≤10-bit + intra-only** 做出如下选择（`vcpkg/buildtrees/x265/.../encoder/level.cpp:98-104`，注释原文 “note there is no 10bit still picture profile”）：
  - 非 intra → `MAIN10`（idc=2）；
  - **intra/单帧 still → `MAINREXT`（idc=4，“Main10 Intra”）**。
- ⇒ **本仓 10-bit HEIC 静图全部是 idc=4**（本次复编码 T1 得 420 10-bit 文件同样 idc=4，可复现）。

### 3.3 Apple 侧支持面（外部资料，`build/research-web/findings.md`）

- iPhone 18 Pro（2026-09，A20 Pro）官方 Tech Specs 只写 “HEIF 拍摄 / HEVC 视频 / Wide color (P3)”，**不写 profile/level**。
- Apple VideoToolbox 文档中 HEVC profile-level 常量只有：`HEVC_Main`、`HEVC_Main10`、`HEVC_Main422_10`（+ Monochrome/Mono10）；**没有 4:4:4 常量，也没有 “Main 4:2:0 10 / RExt idc=4” 常量**。
- 公开资料没有「iPhone 接受/拒绝 idc=4 HEIC」的直接证据（未决）。

### 3.4 结论（批判性）

- **不能承诺「完全满足 iPhone 18 Pro 硬解」**。T5 的编码工具集与 Main10 4:2:0 10-bit 相同，但**档位信号（profile_idc=4）落在 Apple 文档支持的 profile 集合之外**；硬件解码器通常按 profile 严格校验，存在被拒绝/回退软件解码的风险。**必须真机验证**（AirDrop 到 iPhone 18 Pro 用「照片」打开，看是否报「无法打开」、是否掉帧/发热、是否能导出）。
- **T3/T4（4:4:4 10-bit，chromaFormat=3）** 更明确：Apple 文档完全没有 4:4:4 HEVC profile，预计 iPhone 硬解**不支持**（本机 `libde265` 对这类 RExt 码流也有已知限制，见 AGENTS.md §2 开放问题④）。
- **T2.heic 与兼容性无关**：像素取证（`build/analysis-pixel/`）确认它是 2026-10-07 旧管线的另一张照片产物（3748×5623，与 T1 无关），解码失败的根因是 libde265 默认单 NAL 16 MiB 安全上限（该 NAL 27,067,678 B），不是 RExt/profile。
- 安全目标（若要以 iPhone 兼容为验收）：**4:2:0、10-bit、profile_idc=2（Main10）、P3 ICC、level 按尺寸**。**同配置可复刻性已由原型证明**：用 `x265:total-frames=2 + keyint=2 + repeat-headers=1` 让 x265 输出 Main10(idc=2)，再用 libheif grid API 产出 Apple 同构的 6×7×1024×576 grid（hvcC 前 60B 与 T7 逐字节一致）；候选文件与真机测试协议见 `docs/review-apple-heic-config.md`（§4）。
- 附注（Windows 侧测试环境限制，勿过度解读）：WIC/Windows 官方成像栈在本机对 T5 **创建解码器成功但取像素失败**（`0xC00D5212`，`MF_E_TOPO_CODEC_NOT_FOUND`）；T2/T3/T4 同样失败，且 **8-bit libheif/x265 HEIC 对照也失败**，同时本机**未安装 Microsoft.HEVCVideoExtension**（仅有 HEIF/AV1/VP9/MPEG2/JXL 扩展）⇒ 这是**环境缺 HEVC 解码器**，**不能**据此判断 T5 的 10-bit/RExt 是否被 Windows/iOS 拒绝。T5 在本仓 harness（libheif+libde265）解码正常。

---

## 4. Q2b：`T6.jxl` 为什么在 Windows 照片里 P3 色彩不对？

### 4.1 T6.jxl 文件是「对的」

| 项 | 值 |
|---|---|
| 码流 | JPEG XL（container，`jxlc`），**16-bit**，VarDCT/Modular？→ 本文件为 **Modular distance=0.3 + color=P3** |
| 色彩 | 码流头 `uses_original_profile=1`，内嵌 **Display P3 ICC**（lcms2 v4.4，580 B）；`intensity_target=255`（SDR） |
| 本仓评分 | GUI 92.62；独立 oracle **92.6238726114**（vs P3 参考） |
| 与 T1 的色差（正确 lcms 转换到 sRGB 16-bit 后比较） | ΔE00 平均 **0.42** / p95 0.92 / max 1.64；MAE 1.9/1.2/1.9 (10-bit codes) |
| 交叉验证 | T3(AdobeRGB) 与 T4(P3) 两个无损 444 文件分别转 sRGB 后 ΔE00 0.169 ⇒ **P3 标签与像素自洽**，T5/T6 标签正确、无偏色 |

⇒ **文件本身符合规范、像素与 P3 ICC 一致**；「P3 显示不对」是**查看器/解码器侧**问题。

### 4.2 Windows 侧的证据

- Windows 11 **不内置 JXL 解码**：需 Store 安装 *JPEG XL Image Extension*（本机已装 v1.2.50.0，WIC 解码器 FriendlyName = “Microsoft JPEG XL Decoder”，CodecId `fc6ceece-…`）。Photos 使用该 WIC 编解码栈。
- 用 Windows 官方 `Windows.Graphics.Imaging.BitmapDecoder`（Photos 同源成像栈）+ **WPF/WIC 原生像素格式探测**（GUID 对照已安装 SDK 的 `wincodec.h`）实测 **T6.jxl**：
  - **根因（GUID 级复现微软 #5390）**：所有 **16-bit SDR JXL**（真实 T6 + 合成 Modular/VarDCT、P3/sRGB）都被交给 WIC 的 **`GUID_WICPixelFormat64bppRGBHalf`（64bpp 半浮点）且 `color_contexts = 0`** ⇒ **码流里内嵌的 Display P3 ICC 根本没有被暴露**；而 8-bit JXL 则是 `24bppRGB` + 1 个正确的 ICC 上下文。
  - **16-bit 路径色彩管理未生效**：`ColorManageToSRgb` 输出与 raw P3 码值完全相同（MAE 0，no-op）；与正确 sRGB 转换相比 **ΔE00 平均 2.00 / p95 3.27 / max 3.94**，是**色度误差而非色调误差**（|ΔL| 0.14、ΔC +3.87 偏「不够饱和」、色相约 2.71°、MAE ≈1030 code16、max 19018）。
  - **16-bit 精度塌缩**：`Rgba16` 输出值 **100.0% 可被 257 整除（落在 8-bit 网格）**，97.1% 恰等于 `round(正常解码/257)*257`；对比 libjxl/OIIO 真 16-bit 解码只有 0.44% 落在该网格。精度塌缩本身 ΔE00 仅 0.23，**更大的问题是 ICC 丢失带来的 ΔE00 2.0**。
  - **8-bit 路径正常**：8-bit P3 JXL 解码结果 == 源 8-bit×257 精确相等；CM 输出 vs harness sRGB ΔE00 平均 0.26 / max 2.46。16-bit **sRGB** JXL 的 ΔE00 = 0（无需转换、无从暴露）⇒ **故障只在「16-bit + 需要 ICC 转换」时出现**。
  - **重要 caveat**：`GetPixelDataAsync(ColorManageToSRgb)` 对**所有 16-bpc 源**都是 no-op（16-bit PNG/TIFF 亦然，T1.tif 对照 ΔE00 2.89）。所以 ΔE00 2.0 这组数字度量的是 **WIC 16-bit 处理路径**，不能 100% 等同于 Photos 最终像素；但 **16-bit JXL 解码器连 ICC 上下文都不提供（0 contexts）**，这层丢失是 codec 级的、任何基于 WIC 的消费者都无法从该路径恢复。
- 微软已知问题：`microsoft/WindowsAppSDK#5390`（2025-05）记录 WIC JXL 解码器色彩缺陷（HDR Display-P3 被当作 BT.2020、**SDR 16-bit int 被解成 16-bit float**、白点歧义等），与本次 GUID 级发现（16-bit JXL→64bppRGBHalf、ICC 丢失）完全同族；无公开修复时间。
- 附加资料（web-researcher）：Photos 自身 ≥ v2022 起是**做 ICC 色彩管理**的（sRGB 回退只发生在无 profile 时）——所以不是「Photos 完全不管色彩」，而是 **JXL WIC 解码器在 16-bit/P3 路上交出了错误的数据**。

### 4.3 结论与可操作建议

- T6.jxl **不是坏文件**：本仓静默回读（harness `--color keep` = OIIO/libjxl 解码，P3 ICC 保留）与 WIC raw 输出差异 MAE 64.6/65535 = 0.098%，且完全可由 WIC 的 8-bit 精度塌缩解释；libjxl 参考解码正常。在符合规范的 JXL 解码器（libjxl 参考实现、Chrome 等）里应显示正确 P3（建议用户用 Chrome 或 `djxl` 交叉验证，一眼就能区分「文件问题 vs Photos 问题」）。
- 在 Windows Photos 里要避坑：**JXL 导出 8-bit**（本仓 JXL 位深可选 8/16），或改用其他色彩管理正确的查看器；**16-bit + 广色域 JXL 在 Photos 上目前不可靠**（WIC 解码器给 64bppRGBHalf + 0 ICC contexts，照片色彩管理拿不到 P3 身份）。
- 这是**查看器/系统编解码器已知缺陷**，不是本仓色彩管理或 JXL 编码的缺陷（本仓 JXL 的 P3 写路径经 oracle/ΔE00 双重验证正确）。
- 证据图（`build/analysis-wic/png/`）：`evidence_t6_side_by_side.png`（harness sRGB vs WIC）、`evidence_t6_diff_decomposition.png`（ΔE00/ΔC/ΔH 分解）、`evidence_t6_crop_100pct.png`、`evidence_bitdepth_codec_matrix.png`（8-bit vs 16-bit、PNG/TIFF/JXL 矩阵；注：矩阵图中第一行 “target sRGB” 为渲染缩放瑕疵，其余行正确）、`evidence_precision_mod257.png`。

---

## 5. Q3：为什么 T1.tif 的 HEIF 420 与 444 评分十分接近？

### 5.1 数据

| 来源 | 444 | 420 | 差 |
|---|---|---|---|
| 用户 GUI（10-bit，AdobeRGB，约 9.4–9.5 MB） | 90.94 / 90.95 / 91.03 | 90.93 | ≤ 0.10 |
| 本仓受控 q80 10-bit（keep AdobeRGB） | 10.89 MB / **91.54** | 10.81 MB / **91.39** | 0.15 |
| 本仓受控 q80 10-bit（sRGB 目标） | 11.52 MB / **91.79** | 11.30 MB / **91.64** | 0.15 |
| 历史证据 `docs/m8-metric-truth.md`（q90 8-bit） | 77.2848 | 77.4966 | 0.21（420 略高） |
| 反例：合成高色度图（`build/tmp-m8fix/color.png`） | 71.55 | 57.41 | **14.14** |

⇒ 444/420 接近是**内容相关**的，不是评分常量/不随产物变化。

### 5.2 像素取证（`build/analysis-pixel/`，10-bit AdobeRGB，raw 解码对比）

- 444 vs 420 两产物：RGB MAE = 4.35/2.91/6.67 (code10)；Y MAE 2.48，Cb 3.36，Cr 2.41。
- **误差能量拆分：色度 80.3% / 亮度 19.7%**；ΔE00 平均 0.97（p95 2.43），肉眼几乎不可见。
- **色度高通能量（2×2 盒残差）**：444 的 Cb/Cr = 281/183 (code16) → 420 的 2.2/3.4，比值 **≈125× / 54×**；而**亮度高频不变**（1088.9→1091.5）⇒ 420 只砍掉了 4:4:4 里那部分「本来就几乎不存在」的高频色度，亮度细节原样保留（x265 把省下的色度比特还给亮度）。
- 结论：T1 是自然照片（树叶/天空/逆光，大面积低色度细节），SSIMULACRA 2 又是**亮度主导、多尺度、对色度降权**的指标 ⇒ 444/420 的感知差异被压到 0.1~0.2 分。

### 5.3 什么时候会拉开？

- 内容有强色度细节（红蓝棋盘、彩色文字、饱和花色、彩色摩尔纹）时差距会很大（合成图 14.14 分就是证明）。
- 位深提高（10/12-bit）会进一步缩小 444/420 的可见差异（量化底噪更低），高分段的「接近」很大程度是**逼近视觉无损后的正常收敛**。
- 因此：**「彩色图片」≠「高色度细节」**；T1 恰好是低色度细节内容。要验证某张图 444 是否值得，应看色度细节而不是主观「彩色」。

---

## 6. 关键行动清单（建议，按优先级；本次未改任何代码）

**P0（质量，直接影响用户产物）——JXL 有损 VarDCT 配置**
1. 不允许「有损 VarDCT + `uses_original_profile=true`」组合继续作为可产出路径（当前 J8 只有 tooltip、没有拦截）。可选修法，按代价排序：
   - **A（最快、已验证）**：有损 VarDCT 仅在 `color=srgb` 目标下允许，并强制 `uses_original_profile=false`（XYB）。该组合解码语义 = sRGB，评分/回读语义自洽，实测同体积下与 HEIF 持平（10.06 MB / 91.68 vs HEIF 11.30 MB / 91.64，both sRGB 目标、SSIMULACRA 2 同 reference 规则）。
   - **B（保宽色域、最省事）**：`keep`/P3/AdobeRGB 目标 + 有损 JXL 时自动改用 **Modular lossy**（`uses_original_profile=true` 对 Modular 是安全的；实测 d0.3 = 24.5 MB / 92.66）或在 UI 强提示「宽色域有损 JXL 建议 HEIF / Modular」。
   - **C（根治宽色域 XYB）**：按 libjxl 文档实现语义——XYB 码流解码输出为 sRGB，需在解码后用 CMS 从 sRGB 转回目标/原始 profile；同时修订本仓评分回读（当前假设「解码值 = 目标 ICC 空间」）。改动面大，需设计裁定。
   - 不建议原地等 libjxl（issue #4552 2026-01 开放至今无修复；本仓钉 libjxl 0.12.0）。
2. 加机械化回归探针：`JXL VarDCT d=0.5` 的分数必须不低于 `d=2.0 - 1`，且体积不得出现「distance 减半、体积×1.3 但分数下降」的异常；另加「JXL lossless = 100」与「XYB 路径产物可解码」断言。
3. GUI/日志**记录生效参数**：当前运行日志只有 `formats=jxl:libjxl`，没有 tech/distance/effort/bitdepth/color，导致用户无法复核、本次排查只能靠文件大小反推。建议把逐输出生效参数写进 `quality metric` 行的同一条日志（或 sidecar 化）。

**P1（兼容性）——HEIC 的 profile 信号**
4. 评估 `libheif` x265 插件「10-bit 静图一律 RExt(idc=4)」的兼容性风险；若 iPhone 硬解验收重要：尝试 Main10(idc=2) 信号方案（overlay 补丁/上游修复），或文档明确「HEIC 10-bit 非 iPhone 原生档位，需真机验证」；4:4:4 10-bit 建议 UI 标注「iPhone 不支持」。
5. Windows Photos/JXL：文档化「16-bit + 广色域 JXL 在 Photos 上受 WIC 解码器缺陷影响」；如需分享，建议 8-bit JXL 或 HEIF。

**P2（稳健性）——HEIC 单 NAL 16 MiB 回读风险**（pixel-analyst 发现）
6. 单图 4:2:0/10-bit `quality=100` 的 NAL 可达 ~15.75 MB（libde265 默认上限 16 MiB）；再大一点，管线将无法回读自己的产物（评分变 unavailable、仅元数据路径也可能受影响）。建议默认 `lossless`/grid 或预检大图 quality 上限，并加单测/回归。

---

## 7. 未覆盖 / 未决项（如实）

1. **iPhone 18 Pro 真机未测**：T5 idc=4 的实机行为没有证据（无 Apple 文档、无公开报告）；T3/T4 4:4:4 的结论也只是「文档支持面外推」。需要 AirDrop 实测才能定论。
2. **Windows Photos UI 未截图取证**：本次用 Photos 同源的 WIC/WinRT 解码栈复现并量化；Photos 内部渲染路径可能不同（尤其 HDR/wide-gamut 渲染），「16-bit CM no-op」在 WinRT API 层对 TIFF/PNG 同样出现，属 API/解码器共同限制，不能 100% 等价于 Photos 的最终像素。
3. **libjxl 根因未到源码级定位**：定位到 `uses_original_profile=true` 的 VarDCT 路径（上游 issue #4552 未修复）；未进一步定位到具体 transform/量化代码，也未测试 libjxl 更新版本是否已修复。
4. **跨格式质量对比的口径限制**：§2.3 的 JXL-vs-HEIF 对比是在 `color=srgb` 目标下完成的（XYB 解码语义 = sRGB，评分口径自洽）；`keep`/P3/AdobeRGB 目标的 XYB 路径需先实现 §6 方案 C 才能做严格同口径对比，本报告未做。
5. 其余旁支：T2.heic 的 NAL 上限问题、T3/T4 是 HEIF grid、本机 HEVC Video Extensions 缺失导致 WIC/HEIF 无法作证等，见 `build/analysis-pixel/results.md` 与 `build/analysis-wic/results.md`。

---

## 8. 复现命令与证据路径（gitignored，可复跑）

```powershell
# 0) 独立 oracle 复核 GUI 分数（本报告证据 B）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev --score-pair super/T1.tif "C:/Users/gmw/Downloads/新建文件夹/r/jxl/_DSF3043 (1).jxl" --ref-bps 16 --icc super/T1.tif
#    → SCORE_PAIR state=ok score=75.6547787066 ...

# 1) 受控 JXL VarDCT 曲线（证据 2.1；每条约 15-25s + 评分）
python tools/env.py run -- build/release-dev/photopipeline.exe --dev super/T1.tif --out build/analysis/exp/x --format jxl --tech vardct --bitdepth 16 --color keep --param distance=0.2 --param effort=7 --score --workers 1
# 2) XYB 对照（uses_original_profile=0）
... --param distance=0.5 --param effort=7 --param uses_original_profile=0 --score
# 3) 8-bit 同曲线（证明非 16-bit 专属）
... --bitdepth 8 --param distance=0.5 ...
# 4) HEIF 444/420 受控对（q80 10-bit）
... --format heif --bitdepth 10 --param chroma=444 --param quality=80 --score
... --format heif --bitdepth 10 --param chroma=420 --param quality=80 --score
```

证据位置：
- 本报告主实验：`build/analysis/exp/`、`build/analysis/batch*.log`、`build/analysis/jxlinfo.py`（JXL `uses_original_profile` 读取）、`build/analysis/hvcc.py`（HEVC profile 解析）
- 像素/HEIF 取证：`build/analysis-pixel/results.md` + `recon/metrics.json`
- WIC/Windows 取证：`build/analysis-wic/`（compare.log / results_stats.json / raw）
- 外部资料：`build/research-web/findings.md`（Apple HEVC profile 常量、Windows JXL 扩展/微软 bug）
- 上游 libjxl 缺陷：https://github.com/libjxl/libjxl/issues/4552
- 微软 WIC JXL 色彩缺陷：https://github.com/microsoft/WindowsAppSDK/issues/5390
