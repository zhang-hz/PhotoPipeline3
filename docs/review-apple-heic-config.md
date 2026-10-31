# Apple 硬件编码 HEIC 参考（T7.heic）解剖 & PhotoPipeline 生成同配置的可行性

> 状态：**v1（2026-10-30）**，取证 + 可行性评审（**未改任何源码**）。
> 触发问题（用户原话）：「T7.heic 是苹果硬件编码生成的 heic，确保软件可以生成相同配置的。充分调研苹果芯片的硬解能力」。
> 关联：`docs/review-super-jxl-heif.md`（JXL 评分反常/HEIF 420-444/T5 兼容性）；本文件负责「Apple 硬件编码配置 → 本仓能否复刻 → 需要改什么」。
> 证据目录（gitignored）：`build/analysis/`（T7 解剖脚本）、`build/analysis-heic/`（原型 + 候选文件）、`build/research-web-apple/`（外部调研）。

---

## 1. T7.heic 是什么（客观事实，不依赖用户说法）

`super/T7.heic`，17,503,020 B（EXIF：FUJIFILM X-T20，2026-05-03 拍摄；2026-05-10 由 Apple 侧重新编码）。像素 5854×3902，10-bit。
**来源判定**：用户说明为「苹果硬件编码生成」。结构证据一致（Apple 的 HEIF writer 特征：grid + `heix`+`MiHA` brand + 536B Apple Display P3 ICC「Copyright Apple Inc., 2022」+ Exif/XMP），下述解剖按此前提进行。

**容器/项目结构**（自写 ISO-BMFF 解析）：

| 项 | 值 |
|---|---|
| ftyp | major **`heix`**；compatible **`mif1 MiHA miaf heix`** |
| 主图 | item 43 = **grid**（派生图），`dimg` 引用 item 1–42（42 个 tile） |
| grid 参数 | HEIF grid **version 0**：`rows=7, cols=6`，输出 **5854×3902**，tile **1024×576**（边缘 tile 填充/裁切） |
| tile | 42 个 `hvc1` item，每个只有 **1 条 NAL（type 20 = IDR_N_LP）**，4 字节长度前缀；参数集在共享 hvcC |
| ipco 属性 | `colr`(ICC prof) / `ispe`(1024×576, tile) / `ispe`(5854×3902, grid) / `irot`(identity) / `pixi`(10-bit) / **`hvcC`（全 tile 共享一份）** |
| ipma | 每个 tile 关联属性 `colr`+`ispe(tile)`+`hvcC`（均 essential）；grid 用 `colr`+`ispe(grid)`+`irot`+`pixi` |
| 元数据 | Exif item 44、XMP mime item 45，均 `cdsc` 关联到 grid |
| 色彩 | **ICC `Display P3`，536 B**，`Copyright Apple Inc., 2022`（Apple 系统内置 P3 profile） |

**HEVC 码流参数（每个 tile 相同，hvcC 与 SPS 双解析一致）**：

| 字段 | 值 |
|---|---|
| `general_profile_idc` | **2 = Main10** |
| `general_profile_compatibility_flags` | `0x20000000`（只声明兼容 profile 2） |
| `general_constraint_indicator_flags` | `0xb00000000000`（progressive=1, interlaced=0, non_packed=1, **frame_only=1**，其余为 0） |
| `general_level_idc` | **93 = Level 3.1**（针对 1024×576 tile，非整图） |
| chromaFormat / bit depth | **4:2:0 / 10-bit**（luma 与 chroma 均 10） |
| SPS 尺寸 | 1024×576（tile），`frame_only` 约束同上 |
| 容器品牌 | `heix` = 10-bit HEVC image brand |

## 2. 与 PhotoPipeline 当前 HEIC 输出的差异（同一图像内容对比）

本仓 10-bit HEIC（`super/T5.heic`，以及本次复编 `build/analysis/enc-heif/T1.heic`）：

| 项 | Apple T7.heic | PhotoPipeline（现状） | 差异性质 |
|---|---|---|---|
| 图像组织 | **grid**（6×7 tiles，1024×576） | **单图**（1 item，3902×5854） | 结构性 |
| HEVC profile | **Main10 (idc=2)** | **RExt (idc=4, "Main 4:2:0 10 Intra")** | 信号性 |
| profile compat flags | `0x20000000` | `0x08000000` | 信号性 |
| constraint flags | `0xb00000000000` | `0x000000000000`（hvcC 侧；SPS 侧另有值） | 信号性 |
| level | **3.1**（per tile） | **6.0**（整图 3902×5854） | 由 tiling 决定 |
| 位深/色度 | 10-bit / 4:2:0 | 10-bit / 4:2:0 | 一致 |
| 色彩 | Display P3 ICC（Apple 536B） | Display P3 ICC（lcms 580B） | 色度学一致、字节不同 |
| ftyp | `heix` + `mif1 MiHA miaf heix` | `heix` + `mif1 heix miaf` | 缺 `MiHA` 兼容 brand |
| 元数据 | Exif + XMP item | Exif + XMP item | 一致（均有） |
| 每 tile NAL | 1× IDR_N_LP | 单图亦为 1× IDR | 一致 |

⇒ 若要「生成相同配置」，需要补两件事：
1. **profile 信号**：让 x265 对 10-bit 4:2:0 静图输出 **Main10 (idc=2)** 而不是 RExt (idc=4)；
2. **grid 分块**：把大图拆成 1024×576 tile 的 HEIF grid（Apple 结构），使每 tile 的 level 自动落到 3.1。

## 3. 为什么现状是 idc=4（根因链，已在源码级确认）

- libheif 1.23.5 的 x265 插件对 10-bit 一律调用 `param_apply_profile(param, "main10-intra")`（`encoder_x265.cc:801`）。
- x265 4.3 的 `param_apply_profile()` 对任何 `-intra`/静图 profile 会**强制 `param->keyframeMax = 1`**；插件又固定 `param->totalFrames = 1`（`encoder_x265.cc:894`）。
- 于是 `determineLevel()`（`encoder/level.cpp:98-104`）中 `intraConstraintFlag = (keyframeMax<=1 || onePictureOnly) = true`：
  - 4:2:0 ≤10-bit → **`MAINREXT`（idc=4，“Main10 Intra”）**（源码注释原话 “note there is no 10bit still picture profile”）；
  - 只有非 intra 才走 `MAIN10`（idc=2）。
- 该逻辑对 **所有 10-bit 静图** 生效，所以本仓 10-bit HEIC 全部是 idc=4。

## 4. 「同配置」的可生成性（原型实验，已证明可行）

### 4.1 Main10 信号：用 3 个 x265 参数即可（另加 range 控制颜色；无需改 libheif/x265 源码）

原型（ctypes 驱动 libheif C API，与产品同一 libheif/x265 二进制）验证。最终可用参数（exact）：

```
x265:total-frames=2, x265:keyint=2, x265:repeat-headers=1, x265:range=limited
```

| 尝试 | 结果 |
|---|---|
| 默认（现状） | idc=4（RExt），单图正常 |
| `x265:total-frames=2` | 仍 idc=4（keyframeMax 被 profile 强制为 1） |
| `x265:total-frames=2` + `x265:keyint=2` | x265 内部已打出 “Main 10 profile”，但 **encode 失败**：libheif 静图路径从不调用 `x265_encoder_headers()`；keyframeMax>1 时 x265 不再强制 `bRepeatHeaders=1`（默认 0），**码流内没有 VPS/SPS/PPS** → libheif 的 `hevc_enc.cc` 解析不到 SPS、`encoded_width/height=0` → 报 “Invalid image size”（code 8/subcode 129）——注意此时 x265 日志已打印 “encoded 1 frames”，极具误导性 |
| **`x265:total-frames=2` + `x265:keyint=2` + `x265:repeat-headers=1`** | **成功**：x265 日志 “Main 10 profile, Level-3.1”，hvcC `profile_idc=2`、`compat=0x20000000`、level 3.1、4:2:0 10-bit；本地 libheif/libde265/OIIO/本仓 `--dev` 均解码正常 |

⇒ **Main10 信号可以在现有依赖上生成**，只需在编码前把这三个 x265 参数下发给插件（单帧静图语义无副作用；repeat-headers 只是让参数集随帧 in-band，与 Apple 自己的写法一致）。`x265:range=limited` 与 Apple 的隐含 limited range 对齐（颜色，见 4.2 颜色歧义）。

**遗留差异（如实）**：libheif 的 hvcC 写出器**硬编码 PTL constraint flags 为 0**（`hevc_boxes.cc` 不拷贝 x265 的 VPS constraint 位），而 Apple T7 是 `b00000000000`；x265 原始 SPS 该处为 `0x90…`（non_packed=0）vs Apple `0xb0…`（non_packed=1）。原型为了结构对比做了**长度不变的 PTL 位补丁**使 hvcC 与 T7 逐字节一致；**产品化正路**是给 x265 打 overlay 参数或修 libheif 写出器，而不是后处理改码流。该差异是否影响 iPhone 解码未知（需真机，见 §6）。

### 4.2 grid 分块：libheif 网格 API 可产出与 Apple 同构的文件

用 `heif_context_add_grid_image` + `heif_context_add_image_tile`（本仓无损 grid 路径已在用同一组 API）生成 6×7×1024×576 tile（输出 5854×3902，边缘复制填充到 6144×4032），每 tile 用 4.1 的 Main10 参数编码（x265 日志 84×“Main 10 profile, Level-3.1”），ICC 复用 **T7 的 Apple Display P3（536B）**。

**与 T7 的结构对比（`build/analysis-heic/T7_vs_candidates.md`）**：

| 字段 | Apple T7 | cand_grid_main10_applelike（生成） |
|---|---|---|
| ftyp | heix + mif1/MiHA/miaf/heix | 一致（含 brand 顺序，原型做了 ftyp 重排） |
| 主图/网格 | grid v0 6×7，5854×3902（8 字节 v0 form） | 一致 |
| tile 数 / ispe | 42 个 hidden hvc1 / 1024×576 + 5854×3902 | 一致 |
| 共享属性 | 1 hvcC + 1 colr 对所有 tile | 一致 |
| hvcC | Main10 idc=2，compat 0x20000000，constraint b0…，level 3.1，420/10bit | **前 60 字节与 T7 逐字节一致**（原型做了 2 字节 PTL 位归一；见 4.1 遗留差异） |
| SPS | 1024×576，4:2:0，10-bit，`num_short_term_ref_pic_sets=0`，**无 VUI** | 同（`num_short_term_ref_pic_sets=0`）；但 x265 总会写 SPS VUI（43 vs 35 B） |
| ICC | Display P3 536B（Apple 版权） | 一致（原型直接复用 T7 的 ICC 字节） |
| Exif/XMP | 有（item 44/45，cdsc→grid） | 原型未写（产品路径本仓已有元数据写入） |
| irot | 有（identity） | 原型无（无显示影响） |
| 本地解码 | — | libheif/libde265/OIIO/本仓 `--dev` 全部 OK |

**颜色歧义（如实）**：T7 是“ICC-only”文件（无 nclx box、无 VUI 颜色描述），**码流本身没有告诉解码器 RGB→YCbCr 用的是哪个矩阵**。原型编码用 BT.709，而 libheif 对未标注文件默认按 BT.601 解码 → applelike 变体只有 PSNR 41.18 dB；显式写 VUI CICP（primaries=12/transfer=13/matrix=1、limited range）后升到 **49.01 dB**。iOS 对 ICC-only HEIC 默认用什么矩阵未知，真机测试需对比 #1（applelike）与 #2（vui）的颜色。

### 4.3 候选文件（供 iPhone 18 Pro 真机测试）

文件已生成在 `build/analysis-heic/candidates/`，并**复制到 gitignored 的 `super/iphone-test/`**（含 T7 参考与测试说明），方便直接传到手机：

| 优先级 | 文件 | 大小 | 结构 | 测什么 |
|---|---|---|---|---|
| 1（主） | `cand_grid_main10_applelike.heic` | 10.89 MB | grid 5854×3902，6×7×1024×576，Main10 idc=2（L3.1/tile），P3 ICC 536B，**无 VUI CICP（与 T7 相同）** | 「Apple 同配置」是否被硬解接受 |
| 2 | `cand_grid_main10_vui.heic` | 10.89 MB | 同 #1 + VUI CICP（P3/sRGB/BT.709，limited） | 颜色歧义对照（iOS 是否依赖 VUI） |
| 3 | `cand_single_main10_applelike.heic` | 10.86 MB | 单图 5854×3902，Main10 idc=2，**L6.0**，P3 ICC，无 VUI | 22.8MP 单图 Main10 是否可开 |
| 4 | `cand_single_main10_vui.heic` | 10.86 MB | 同 #3 + VUI CICP | 单图颜色对照 |
| 5（对照） | `ctrl_single_rext_idc4.heic` | 9.35 MB | 单图 + **idc=4**（≈现产品 T5 配置） | 现状 profile 是否被拒 |
| 6（对照） | `ctrl_grid_rext_idc4.heic` | 9.37 MB | 同 grid 结构 + **idc=4** | 隔离 grid 与 profile 两个变量 |
| 7（对照） | `ctrl_single_8bit_mainstill.heic` | 9.58 MB | 8-bit + Main Still Picture（idc=3，L6.0） | 保守基线（应能开） |
| 参考 | `super/T7.heic` | 17.50 MB | Apple 硬件编码 | **阳性对照** |

所有候选均已在本地验证解码（libheif 进程内、OIIO、本仓 `--dev` 三路；对 T7 源像素 PSNR 40.7–49.0 dB，数字见 `build/analysis-heic/results.md` §5 与 `build/analysis-heic/finalize.log`）。

**真机测试协议（唯一裁决方式；本机是 Windows，传输可用 iCloud Drive 网页版 / OneDrive / USB「Apple Devices」/ 微信文件传输助手，避免经过会转码的通道）**：
1. 按上表顺序传到 iPhone 18 Pro，**逐个**在「照片」「文件 App」「快速查看」里打开；记录：缩略图是否出现、能否打开、打开耗时（硬解应 <1 s 且不发热；软解/回退明显慢）、缩放是否正常（grid 拼接是否缺块）、颜色是否与参考一致、失败时的确切提示文本。
2. 重点对比 #1 vs #2 vs #5/#6（颜色矩阵与 profile 两个变量）；#5/#6 vs #7 判定 idc=4 是否被拒。
3. 结果矩阵的解释见 `super/iphone-test/README-device-test.md`（“What result means what”）：例如 **#1 正常** → 工具链已能复刻 Apple 配置；**#1 失败 #2 正常** → 需补 VUI/nclx；**#1/#2 失败 #3/#4 正常** → grid 布局有问题；**#5/#6 失败 #7 正常** → 坐实 idc=4 问题。
4. 把「文件 → 缩略图/文件App/照片/颜色」结果反馈，据此决定产品化范围。

## 5. Apple 芯片硬解能力（外部调研，详见 `build/research-web-apple/findings.md`）

### 5.1 Apple 文档化的 HEVC profile 集合

- Apple 对 HEIF/静图**文档化的 HEVC profile 只有三个**：**Main、Main Still Picture、Main 10**（WWDC17 Session 503/513；[high]）。**RExt / `general_profile_idc=4`（“Main 10 Intra”）不在其中**。
- HEIF brand 规则：`heic` = Main / Main Still Picture；**`heix` = Main 10 或 format range extensions**（[nokiatech HEIF 技术页](https://nokiatech.github.io/heif/technical.html)、[MP4RA brands](https://mp4ra.org/registered-types/brands)）⇒ 10-bit 输出必须 `heix`（本仓现状 major brand = heix ✓）。
- T7 的码流是 **plain Main10（idc=2）**，不依赖任何 RExt 签名。

### 5.2 Apple 实际解码能力（比文档更宽，但 iPhone 无定论）

- **Apple Silicon Mac** 的 VideoToolbox 具备 HEVC **RExt 8–10bit 4:2:0/4:2:2/4:4:4** 的硬件解码能力（[medium-high，非 Apple 官方]）——证据：Chromium 社区兼容矩阵（行标签 `Apple Silicon (macOS)`，8b/10b 400/420/422/444 ✅）、Linux AVD 驱动逆向注释 “we can do up to 4:4:4 12 bit”（2026-09 LKML）、Chromium 2026-08 的 Rext **编码** hardware-only commit（`3eaf2f4e…`）以及 FFmpeg/CoreVideo 的 444 10-bit 格式链。**详细核验与方法检讨见 `docs/review-apple-hevc-rext-evidence.md`**。
- 但公开的 VideoToolbox profile-level 常量确实**没有** Main444（只有 Main/Main10/Main42210/Monochrome），Chromium 用私有字符串表示 Rext 444；这些常量主要描述编码侧契约，**不能据此推断解码硬件能力缺失**。
- **iPhone（A20 Pro）对 RExt 静图的硬解支持：没有任何 iOS/iPhone 专属公开证据（UNVERIFIED，正反都没有）**；Mac 证据不能平移到 iPhone。AppleAVD 在 iOS 上存在（CVE-2022-22675 的 H.265 解析路径可证），但 A 系列 AVD 的 profile 能力未公开；iOS 17+ 才有 `RequireHardwareAcceleratedVideoDecoder`/`UsingHardwareAcceleratedVideoDecoder` 公开 API 可做直接判定。
- 结论（如实）：**不能用“Apple 文档没写”推断 iPhone 不支持 idc=4/444，也不能声称已证明支持**；必须真机 VT 强锁测试（工具见 `docs/review-apple-hevc-rext-evidence.md` §5 与 `super/iphone-test/vt-probe/`）。

### 5.3 level / 单图尺寸：T5 与 Apple 的第二个独立差异

- HEVC Level 的 MaxLumaPs：Level 3.1 = 983,040；Level 5.1/5.2 = 8,912,896；**Level 6.0 = 35,651,584**。
- T5 单图 3902×5854 = 22,842,308 luma samples ⇒ **必须 Level 6.0**（现状确实 6.0）。
- Apple T7 用 42 个 1024×576 tile（589,824 samples/tile）⇒ 每 tile **Level 3.1**，整图按 grid 组合。
- Apple 的 **512×512 “system tiles”** 是已文档化的 HEIF 行为（每 tile 为独立 HEVC 帧，另有 320×240 缩略图；目的是并行解码/低内存/渐进加载/缩放预览）；T7 的 1024×576 tile 尺寸未见官方文档（[medium]）。

### 5.4 Apple 硬件编码器产出的 HEIC 配置（T7 实测 = 事实标准）

`Main10(idc=2)` + `compat 0x20000000` + `constraint 0xb0…（progressive/frame_only）` + **4:2:0 10-bit** + **HEIF grid（Apple 当前 writer 为 1024×576 tile）** + **Apple Display P3 ICC** + `heix` + `MiHA` brand + Exif/XMP。本仓要“同配置”，需要复刻的正是这一整组（见 §4/§6）。

### 5.5 iPhone 18 Pro / A20 Pro

- 2026-09-09 发布、2026-09-18 开售，A20 Pro（2nm）；**Apple 未公布 A20 的媒体引擎 profile/level 矩阵**（[unverified]）。官方 Tech Specs 只写 HEIF/HEVC/P3。

### 5.6 对「T5 能否硬解」的直接判断（批判性）

T5 与 Apple 参考有 **两个独立差异**：① **idc=4 vs idc=2**；② **单图 Level 6.0 vs grid Level 3.1**。二者都可能是硬解/快速解码的障碍，但**目前不能给倾向性概率**：Mac 侧硬件 Rext 支持证据较强，iPhone 侧则完全没有公开证据（UNVERIFIED）。
- T5 完全打不开的概率仍然较低——Apple 有软件 HEIF/HEVC 解码路径（WWDC17），最坏情况通常是慢/软解；
- **唯一裁决方式 = 真机 VT 强锁测试（HEVC 视频样本，`super/iphone-test/vt-probe/`）＋ HEIC AirDrop 测试**（`super/T2–T5,T7.heic` + `super/iphone-test/` 候选）。前者回答“A20 Pro 硬件是否接受 444/420 10-bit”，后者回答“ImageIO/Photos 是否接受这些 HEIC 文件与容器形态”。

## 6. 结论与下一步

### 6.1 两个问题的直接回答

1. **「软件可以生成与 T7 相同配置吗？」——可以（工具链级已证明）**：用现有 libheif 1.23.5 + x265 4.3，不需要改依赖源码，就能生成 **Main10(idc=2) + 6×7×1024×576 HEIF grid + P3 ICC + heix/MiHA** 的 10-bit HEIC；结构与 T7 对齐（§4.2），本地解码验证通过（PSNR≈48.85 dB @ 原型质量档）。
2. **「苹果芯片硬解能力」——见 §5 与 `docs/review-apple-hevc-rext-evidence.md`**：Apple Silicon **Mac** 硬件支持 HEVC Rext 8–10bit 4:4:4 有较强旁证（社区矩阵 + AVD 逆向 + Chromium Rext 编码 hardware-only commit；非 Apple 官方、非 ASIC 级）；**iPhone/A20 Pro 无任何公开证据（UNVERIFIED）**。T5 与 Apple 参考的差异是 **idc=4 vs 2** 与 **单图 L6.0 vs grid L3.1** 两个独立变量，**只能真机裁决**：VT 强锁（HEVC 码流，判定硬解能力）+ HEIC AirDrop（判定 ImageIO 接受度）。

### 6.2 产品化需要的两处改动（本轮**未**改码；已给出实现落点）

1. **HEIF「Apple 兼容」信号**：在 `enc_heif` 编码前向 libheif 插件下发 `x265:total-frames=2`、`x265:keyint=2`、`x265:repeat-headers=1`（§4.1 已验证）。落点：在 `enc_heif.cpp` 增加一个自定义参数（HEIF 参数面 = libheif 内省 + `catalog::param_defs`，加一行 catalog + encode 前消费即可，无需动冻结接口）；或先作为 M8 专家透传参数暴露（需 catalog 收录这三个 x265 键并放行规则）。
2. **lossy HEIF grid**：复用本仓已有的无损 grid 代码路径（`heif_context_add_grid_image` + `heif_context_add_image_tile`，`enc_heif.cpp:976-1100`），参数化 tile 尺寸（Apple 参考 = 1024×576）、边缘 clamp 填充、每 tile 10-bit 4:2:0 + 同一 encoder 参数；ICC 用本仓 lcms Display P3（色度学等价，不复制 Apple 的 536B profile）；brand 保留 `heix` 并补 `MiHA`；Exif/XMP 走现有 cdsc 元数据写路径。
   - 已知小差异（不影响生成、是否影响 iPhone 未知）：libheif 的 hvcC PTL constraint flags 硬编码为 0，而 Apple T7 为 `b0 00 00 00 00 00`；原型用长度不变 PTL 位补丁做了逐字节对齐。产品化建议在 x265/libheif 层解决（overlay 补丁/上游），不建议产品内做码流位手术。

### 6.3 下一步（需要用户动作 + 可选落码）

1. **真机验收（两条独立通道）**：
   - **HEVC 硬解能力**：运行 `super/iphone-test/vt-probe/` 的 VT 强锁探针（4 个码流：444 10-bit Rext ×2、420 10-bit idc=4、420 10-bit Main10），按 `docs/review-apple-hevc-rext-evidence.md` §5.4 判读；这是回答“A20 Pro 是否硬解 444 10-bit / idc=4 是否被拒”的直接证据。
   - **HEIC 接受度**：把 `super/T2–T5,T7.heic` 与 §4.3 的 T7-like 候选 AirDrop 到 iPhone，逐个用「文件 App / 照片」打开，记录能否显示/缩放/耗时/颜色。
2. **据结果落码**：若 `cand_grid_main10_vui.heic` 正常而 ctrl 不行 → 按 §6.2 两项都做；若 `cand_single_main10_vui.heic` 已正常 → 只做第 1 项（改动更小）；若 VT 强锁证明 444 10-bit 可硬解，则“Apple 兼容”方案还可以扩展到 444 目标。
3. 落码时按仓库纪律：引擎改动 + 单测 + 冒烟/金样证据 + 报告；真机验收结果进报告。

## 7. 未覆盖 / 未决项（如实）

1. **iPhone 18 Pro 真机未测**：T7 同配置是否被接受、T5 现状是否被拒，只有真机 AirDrop 测试能定论（§5.6 的概率判断不是证据）。候选文件已就绪（§4.3）。
2. **产品代码尚未改动**：本轮只做「可生成性」证明（`build/analysis-heic/`），`enc_heif` 的 Apple 兼容参数 + lossy grid 还没进产品；改动清单见 §6.2。
3. **PTL constraint flags 差异**：libheif hvcC 写出器与 Apple 的 `b0…` 不一致（原型用位补丁对齐）；对 iPhone 解码的实际影响未知，需真机/上游修复验证。
4. **原型候选的质量档较低**（为控制体积约 10.9 MB，T7 为 17.5 MB），且未写 Exif/XMP；它们只用于「同配置兼容性」测试，不是最终质量/元数据验收件。
5. **Apple A20 Pro 媒体引擎规格未公开**；iPhone 的 RExt/idc=4 支持面没有任何公开实测报告（正反都没有）。
