# Apple Silicon / A20 Pro 的 HEVC Rext 4:4:4 10-bit 硬解：证据核验、A20 Pro 重新调研与一次性判定方案

> 状态：**v1（2026-10-30）**，取证 + 外部调研 + 方法检讨（**未改任何源码/测试**）。
> 触发问题（用户原话）：「证明 Apple Silicon（包括 A20 Pro 搭载的 AppleAVD 媒体引擎）在纯硬件 ASIC 层面原生支持 Main 4:4:4 10 … 可以从开源主流多媒体引擎源码提交记录、CoreVideo 硬件 DMA 专用像素格式定义以及本机 VideoToolbox 硬解强锁实测代码三个维度给出直接证据 … 给出分析。为什么你没有调研到。重新调研 A20 Pro 支持的硬件解码规格」。
> 关联：`docs/review-apple-heic-config.md`（T7 解剖 / 同配置生成 / 真机候选）、`docs/review-super-jxl-heif.md`（T5 兼容性）。
> 证据目录（gitignored）：`build/research-rext/`（逐条核验）、`build/research-a20/`（A20/iPhone 调研）、`build/analysis-apple/`（码流提取 + VT 强锁程序）、`super/iphone-test/`（真机测试包）。

---

## 0. 结论先行

**你给的三条证据，本机逐条核验后的结果是「部分成立、关键处有误」；它们可以支撑“Apple Silicon **Mac** 侧硬件支持 HEVC Rext 8–10bit 4:4:4”的较强推断，但都不能证明“A20 Pro（iPhone）在 ASIC 层原生支持 Main 4:4:4 10”，也不能算“纯硬件 ASIC”的直接证据。**

1. **Chromium/社区矩阵**：引用文本真实，但原文是 “**Apple Silicon Mac** supports …”，表格行是 `Apple Silicon (macOS)`，是**社区维护指南**（作者 StaZhu 是 Chromium 贡献者），**没有 iPhone/iOS 任何表述**。你引的 2026-08 commit `1b22d8ec…` **不存在**（官方镜像 422）；真实的是同作者 `3eaf2f4e5259ef902a25920f85794d2ab4805c4e`（2026-08-25），标题只讲 **HEVC Rext 编码**（encode-only），不是“encoding/decoding”。该 commit 的价值是另一件事：它说 Apple Silicon 的 Rext **编码**档位是“hardware-only VideoToolbox encode profile”（失败不回退软件编码）——这证明的是 **Mac 侧 Rext 编码是硬件**，不是解码。
2. **FFmpeg/CoreVideo**：`'x444'` / `'xf44'` 等 10-bit 双平面格式**真实存在**（macOS 11.3 与 iPhoneOS 16.5 SDK 头文件都确认），但 (a) 映射位置是 `libavutil/hwcontext_videotoolbox.c` 的 `AV_PIX_FMT_P410 ↔ 'x444'`，不是你说的 `libavcodec/videotoolbox.c` 的 `videotoolbox_pixfmt_map`、也不是 `YUV444P10`；(b) CVPixelBuffer.h 自己写着 *“CoreVideo does not provide support for all of these formats; this list just defines their names.”*；(c) FFmpeg 的 HEVC VT 会话用的是 **Enable** 而不是 Require 硬解 → 既不能证明、也不能强制硬件；(d) **双平面内存布局本身不构成 ASIC 证据**（软件解码器/VT 软件回退也可以产出这种格式）；(e) `kVTProfileLevel_HEVC_Main444_10_AutoLevel` **不是公开常量**（公开只有 Main/Main10/Main42210/Monochrome），Chromium 用的是私有字符串。
3. **Apple Silicon Mac 的实际结论**：**中等偏高可信**地支持 HEVC Rext 8–10bit 400/420/422/444 硬件解码（社区矩阵 + 2026-09 LKML AVD 驱动逆向的注释 “we can do up to 4:4:4 12 bit” + FFmpeg/CoreVideo 格式链 + Chromium 编码 commit 的 hardware-only Rext 档位）。但这仍是**间接证据**，不是 Apple 官方规格，更不是 ASIC 层直接测量。
4. **iPhone 18 Pro / A20 Pro：目前没有任何公开证据**（Apple Tech Specs 无 profile/level 矩阵；A20 Pro 拆解只讲 CPU/GPU/NPU；AVD 逆向只覆盖 M1–M3；iOS 侧只有 API 面：CoreVideo 有 x444 格式、iOS 17+ 才有 `RequireHardwareAcceleratedVideoDecoder`/`UsingHardwareAcceleratedVideoDecoder` 公开 API）。因此**“A20 Pro 纯硬件 ASIC 原生支持 Main 4:4:4 10”目前不可证**——正反都没有公开实测。**唯一裁决方式是真机 VT 强锁测试**（§5，工具已备好）。
5. **我上一轮为什么没调研到**：见 §4 方法检讨——我主要核对了 Apple 公开 API 常量（其中确实没有 Main444 profile-level 常量）与上一轮 web 调研，**错误地把“公开 API 没写”外推成“硬件可能不支持/兼容风险高”**；没有去核验 Chromium/FFmpeg/CoreVideo/LKML AVD 这条证据链，也没有把“macOS 证据”和“iPhone 证据”分栏。这是本轮要修正的核心。

---

## 1. 三条证据逐条核验（详细版见 `build/research-rext/findings.md`）

| # | 你的说法 | 核验结论 | 关键证据/出处 |
|---|---|---|---|
| A1 | Chromium `media/gpu/mac/vt_video_decode_accelerator_mac.cc` 是 macOS VT 解码实现 | **过时**：该文件已不在 Chromium main（最后一次改动 2024-03-06），现为 `video_toolbox_video_decoder.cc` + `video_toolbox_h265_accelerator.cc` + `video_toolbox_decompression_session*` | 目录 API 列表、路径历史、raw 404 |
| A2 | Chromium 官方技术矩阵记录“Apple Silicon supports HEVC RExt hardware decoding 8~10b 400/420/422/444” | **引用真实但范围被放大**：原文是 “**Apple Silicon Mac** supports …”，行标签 `Apple Silicon (macOS)`；社区指南非官方文档；README **无 iPhone/iOS 任何内容**；表格 8b/10b 400/420/422/444 全部 ✅、12b ❌（软件） | [README](https://raw.githubusercontent.com/StaZhu/enable-chromium-hevc-hardware-decoding/main/README.md) |
| A3 | Chromium 2026-08 commit `1b22d8ec…` “Enable HEVC Rext **encoding/decoding** on Apple Silicon VideoToolbox” | **SHA 不存在（伪造/错记）**：官方镜像 `422 No commit found`；真实 commit = `3eaf2f4e5259ef902a25920f85794d2ab4805c4e`（Sida Zhu，2026-08-25，r1685642），标题为 “Enable HEVC Rext **encoding** …”，**只涉及编码**；提交说明：Apple Silicon 的 Rext 编码是 “four **hardware-only** VideoToolbox encode profile combinations: 8-bit and 10-bit 4:2:2, plus 8-bit and 10-bit 4:4:4 … failed Rext probes are not advertised as software support because VideoToolbox does not provide software encoders for these profiles.” | [真实 commit](https://github.com/chromium/chromium/commit/3eaf2f4e5259ef902a25920f85794d2ab4805c4e)、`api.github.com/.../commits/1b22d8ec…` → 422 |
| A3b | Chromium 主线把 Rext 硬件解码直通标记为支持 | **部分成立**：当前 `video_toolbox_video_decoder.cc` 的 `GetSupportedVideoDecoderConfigs()` 确实登记了 `HEVCPROFILE_REXT`；但同文件注释明确 *“because we allow the OS software decoder”* → Chromium 允许系统软件解码器，登记 ≠ 硬解 | [video_toolbox_video_decoder.cc](https://raw.githubusercontent.com/chromium/chromium/main/media/gpu/mac/video_toolbox_video_decoder.cc) |
| A4 | FFmpeg `videotoolbox_pixfmt_map` 把 `AV_PIX_FMT_YUV444P10` 直接映射到 `'x444'` | **细节错误、实质部分成立**：无 `videotoolbox_pixfmt_map` 符号；真实映射在 `libavutil/hwcontext_videotoolbox.c` 的 `cv_pix_fmts[]`：`AV_PIX_FMT_P410 ↔ kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange ('x444')`（FullRange ↔ `'xf44'`），受 `HAVE_KCVPIXELFORMATTYPE_444YPCBCR10BIPLANARVIDEORANGE` 保护；`libavcodec/videotoolbox.c` 只是通过 `videotoolbox_best_pixel_format()` **请求**该格式。**且 FFmpeg 的 HEVC VT 会话用 `EnableHardwareAcceleratedVideoDecoder`（不是 Require）** → 允许软解、不能证明硬件 | [hwcontext_videotoolbox.c](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/hwcontext_videotoolbox.c)、[videotoolbox.c](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/videotoolbox.c) |
| A5 | CoreVideo 定义 `'x444'`（444 10-bit video-range）、`'xf44'`（full-range） | **TRUE**：macOS 11.3 与 iPhoneOS 16.5 SDK 头文件均确认四字符码；但头文件明说 *“this list just defines their names”*（只定义名字，不代表支持）；常量枚举没有逐项可用性注记，文档给出的 iOS4.0/macOS10.4 是枚举级可用性，不是硬件能力 | [macOS 11.3 头文件](https://raw.githubusercontent.com/phracker/MacOSX-SDKs/master/MacOSX11.3.sdk/System/Library/Frameworks/CoreVideo.framework/Versions/A/Headers/CVPixelBuffer.h)、[iPhoneOS16.5 头文件](https://raw.githubusercontent.com/theos/sdks/master/iPhoneOS16.5.sdk/System/Library/Frameworks/CoreVideo.framework/Headers/CVPixelBuffer.h) |
| A6 | VideoToolbox 有 Main 4:4:4 10 profile-level 常量 | **FALSE**：`kVTProfileLevel_HEVC_Main444_10_AutoLevel` 不存在；公开 HEVC profile-level = Main / Main10 / **Main42210**（macOS12.3/iOS15.4+）/ Monochrome(10)。Chromium 用**私有字符串** `"HEVC_Main444_AutoLevel"`/`"HEVC_Main44410_AutoLevel"`（注释 “Not in the public SDK headers”） | macOS 11.3 `VTCompressionProperties.h`；Apple docs（Main42210）；Chromium `vt_video_encode_accelerator_mac.mm` |
| A7 | “双平面布局 / x444 就是 AppleAVD 硬解的铁证” | **不成立（最多是旁证）**：没有任何 Apple 文档把 `'x444'` 绑定到 AppleAVD/ASIC/DMA；格式是普通 CoreVideo 像素格式定义；VT 软件解码器同样可以交付 VT 帧；FFmpeg 只 Enable、Chromium 明说允许软件解码器。**软硬判定必须用 `RequireHardwareAcceleratedVideoDecoder`/`UsingHardwareAcceleratedVideoDecoder` 之类的会话级证据** | 同上 + FFmpeg `Enable` 分支 + Chromium 注释 |
| A8 | 以上可外推到 iPhone/A20 Pro | **UNVERIFIED**：所有 Chromium/社区证据都是 macOS；Apple 不发布 iPhone 的 HEVC profile/level 矩阵；A20 Pro 拆解无媒体引擎细节；A 系列 AVD 无公开文档（只有 iOS 上存在 AppleAVD 的安全研究，例如 CVE-2022-22675 的 H.265 解析路径） | `build/research-a20/findings.md` §1/§5 |

---

## 2. 这些证据能证明什么、不能证明什么

**能证明（高可信）**
- Apple Silicon **Mac**（M 系列）的 VideoToolbox 路径**登记并接受** HEVC Rext 解码（`HEVCPROFILE_REXT`），且社区矩阵（作者为 Chromium 贡献者）实测/记录 Mac 上 8–10bit 400/420/422/444 Rext 走硬件；12bit 走软件。
- Apple Silicon **Mac** 的 Rext **编码**在 Chromium 里被明确要求“hardware session probe”才对外声明，且**没有软件编码回退** → Mac 侧 Rext 编码硬件能力（8/10bit 422/444）几乎可视为已证实。
- Apple Silicon 的 **AVD（Apple Video Decoder）** 硬件在 Linux 驱动逆向里被描述为 “H264/H265/VP9/AV1(M3+) … 8/10bit 420/422”，且 HEVC 驱动代码注释写着 “**we can do up to 4:4:4 12 bit** — not sure if v4l2 supports…”。→ Mac AVD 硬件能力至少到 4:4:4 12-bit（驱动尚未暴露 444/12bit）。
- CoreVideo/FFmpeg 生态确实有 10-bit 444 双平面格式与映射（macOS + iOS SDK 都有常量定义）。

**不能证明**
- ❌ “A20 Pro / iPhone 在 ASIC 层原生支持 Main 4:4:4 10”——没有任何 iOS/A 系列公开证据；Mac 结论不能平移。
- ❌ “纯硬件 ASIC”（连 Mac 都没有公开的 ASIC 级测量；社区矩阵/vt 行为/驱动注释都是间接证据）。
- ❌ “`'x444'` 输出 = 硬解”（软件也可产出该格式；FFmpeg 只 Enable）。
- ❌ “Chromium 2026-08 那个 commit 证明 decode”（真实 commit 是 encode-only；SHA 是错的）。
- ❌ “HEIC 与 HEVC 视频同一能力”（HEIC 走 ImageIO 静图路径，可能另有 profile/尺寸/容器限制，需单独测）。

---

## 3. 重新调研：Apple AVD / AVE 与 iPhone 18 Pro / A20 Pro

### 3.1 公开的 Apple AVD 逆向信息（2026-09）

- Linux 内核 AVD 驱动系列（Sofus Forstreuter，2026-09-18）：
  - 覆盖 **M1/M2/M3**，支持 AVD 的全部 codec：**H264、H265、VP9、AV1（仅 M3+）**；
  - “All codecs support **4:2:2, 4:2:0, in 10 and 8 bit**. AVD outputs in NV12, NV16, P010 and P210. **Support for 4:4:4 and 12 bit formats is not added yet.**”
  - HEVC 补丁代码注释：**“TODO: we can do up to 4:4:4 12 bit”**。
  - 来源：[cover letter](https://lore.kernel.org/all/20260918-avd-v1-0-49977931f455@icloud.com/)、[HEVC patch](https://lore.kernel.org/all/20260918-avd-v1-13-49977931f455@icloud.com/)。
- 结论：**M 系列 AVD 硬件解码能力（by RE）至少到 4:4:4 12-bit，但驱动只暴露 420/422 8/10bit**；这与社区矩阵“Mac 硬件 Rext 8-10b 444 可硬解”一致。**A 系列（iPhone）没有任何对应的公开逆向数据**。

### 3.2 iPhone 18 Pro / A20 Pro 的公开规格面

| 问题 | 结论 | 证据 |
|---|---|---|
| Apple 是否公布 iPhone 的 HEVC profile/level 矩阵？ | **没有**。Tech Specs 只有 “HEIF 拍摄 / HEVC、H.264、ProRes、ProRes RAW、AV1 播放 … / 4K120 Dolby Vision …”，无 profile/level | Apple iPhone 18 Pro Tech Specs（support.apple.com/en-us/148590） |
| A20 Pro 拆解是否披露媒体引擎？ | **没有**。公开拆解（ChipWise、Geekerwan）只讲 2nm、CPU/GPU/NPU、封装/散热，无 AVD 细节 | `build/research-a20/findings.md` §5 |
| A 系列是否使用同一 AVD IP？ | **未证实**（逆向只覆盖 M1–M3） | LKML 驱动范围 |
| iOS 上是否存在 AppleAVD？ | **存在**（安全研究 CVE-2022-22675 显示 AppleAVD 在 iOS 上解析 H.265，用于 iMessage 缩略图等） | [Project Zero RCA](https://googleprojectzero.github.io/0days-in-the-wild//0day-RCAs/2022/CVE-2022-22675.html) |
| iOS 是否有硬件解码标记 API？ | **iOS 17+ 公开**：`kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder`（会话创建失败若无法硬解）、`kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder`（回读是否硬解） | Apple docs（Require / UsingHardware，iOS 17+） |
| iOS 上“能播放”是否证明硬解？ | **不能**：VT HEVC 会静默回退软件解码（FFmpeg commit `bcff983`，2018-04-19） | [FFmpeg patch](https://github.com/FFmpeg/FFmpeg/commit/bcff983dc340e76518935111146c0e1daf4cb37b.patch) |
| iPhone 上 4:4:4 10-bit HEIC 能否打开？ | **UNVERIFIED**（正反都无公开报告）；Apple 自己的 HEIC 是 Main10 4:2:0 10-bit | T7 实测 + 无报告 |

### 3.3 结论分级

| 陈述 | 等级 | 说明 |
|---|---|---|
| (a) Apple Silicon **Mac** 硬解 HEVC Rext 8–10bit 4:4:4 | **中等偏高**（非 Apple 官方） | 社区矩阵 + LKML AVD “up to 4:4:4 12 bit” + Chromium 编码 commit 的 hardware-only Rext |
| (b) **iPhone 18 Pro / A20 Pro** 硬解 HEVC Rext 4:4:4 10-bit | **UNVERIFIED** | 无任何 iOS/A 系列公开实测；只有 API/格式面的“可能性” |
| (c) iPhone 打开 4:4:4 10-bit HEIC | **UNVERIFIED**（独立 gate） | ImageIO 静图路径与 VT 视频解码不是同一入口 |
| (d) “纯 ASIC 层原生支持” | **不可证** | 公开渠道没有 ASIC 级测量；只能做到“Require-hardware 会话成功 + UsingHardware=TRUE”的设备级证据 |

---

## 4. 为什么我上一轮没调研到（方法检讨，如实）

1. **证据层级混淆**：我把“VideoToolbox 公开 profile-level 常量里没有 Main444”当成“硬件很可能不支持 444/idc=4 的一个论据”。但 profile-level 常量主要是**编码侧**枚举，且 Rext 解码在 Chromium/FFmpeg 里本来就靠**私有字符串/格式协商**，**公开常量缺失 ≠ 硬件能力缺失**。这是本轮最需要修正的错误。
2. **调研通道不全**：上一轮只做了 Apple 文档 + 少量公开报道；没有系统核验 Chromium 源码/社区矩阵、FFmpeg 映射、CoreVideo SDK 头文件、Linux AVD 驱动逆向（LKML 2026-09）这些“外围但直接”的证据。
3. **macOS 与 iPhone 混层**：上一轮 web 调研其实已经给出“Apple Silicon Macs 具备 Rext 硬解 [medium-high]”，但我没有把它写进 T5 的判断逻辑，反而对 iPhone 用“文档没有 → 大概率不能硬解（60–70%）”的措辞，把“未知”说成了“倾向不行”。这对用户是误导。
4. **对引用未做二次核验**：本轮发现你给的 Chromium commit SHA/标题与 FFmpeg 函数名等都是**错记/误引**；我上一轮也没有核验就顺着“社区矩阵”这个方向引用，属于同样的问题。以后对“commit hash / 函数名 / 常量名”这类可机械验证的事实必须逐条比对原始源码。
5. **修正后的口径**：Apple Silicon **Mac** 侧确实有较强的 Rext 444 10-bit 硬件支持证据（非官方、非 ASIC 级）；**iPhone/A20 Pro 侧仍无公开证据**，不能因为 Mac 有就断言 iPhone 有——但也**不能**再用“Apple 文档没写”推断“iPhone 不支持”。正确状态是 **UNVERIFIED，需真机强锁实测**。

---

## 5. 一次性判定方案（工具已备好，真机跑一次即可定论）

### 5.1 工具包

`super/iphone-test/vt-probe/`（= `build/analysis-apple/` 的拷贝，gitignored）：

| 文件 | 内容 |
|---|---|
| `vt_hevc_probe.c` | 读取 Annex-B HEVC → `CMVideoFormatDescriptionCreateFromHEVCParameterSets` → 依次用 4 种 decoder spec（Require HW + 请求 `'x444'` 输出 / Require HW / Enable HW / 自动）创建 `VTDecompressionSession` → 解码 1 帧 → 打印会话属性 `UsingHardwareAcceleratedVideoDecoder` 与输出像素格式 |
| `t3_444_10bit_adobe_lvl60.h265` | **4:4:4 10-bit、profile_idc=4（Rext）**、2048×2048（来自 `super/T3.heic` tile，SPS level 补齐为 6.0） |
| `t4_444_10bit_p3_lvl60.h265` | 同上，Display P3（`super/T4.heic`） |
| `t5_420_10bit_rext.h265` | 4:2:0 10-bit、**idc=4**（“Main 4:2:0 10 Intra”）、3902×5854、P3（`super/T5.heic`，= 本仓现状 10-bit HEIC 配置） |
| `t7_420_10bit_main10.h265` | 4:2:0 10-bit、**idc=2（Main10）**、1024×576（`super/T7.heic` 的 tile，Apple 硬件编码） |
| `README-vt-probe.md` | macOS `clang` 编译命令、iOS（Xcode App）步骤、判读矩阵、限制 |

### 5.2 macOS 上跑（最快）

```bash
xcrun clang -O2 -o vt_hevc_probe vt_hevc_probe.c \
  -framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework CoreFoundation
./vt_hevc_probe t7_420_10bit_main10.h265
./vt_hevc_probe t5_420_10bit_rext.h265
./vt_hevc_probe t3_444_10bit_adobe_lvl60.h265
./vt_hevc_probe t4_444_10bit_p3_lvl60.h265
```

### 5.3 iPhone 18 Pro 上跑（最终裁决）

iOS **17+ 起公开**支持 `kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder` 与 `kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder`，因此不需要私有 API：

1. Xcode 新建 iOS App（SwiftUI），加入 `vt_hevc_probe.c` + 4 个 `.h265`（Copy Bundle Resources）；
2. 加桥接头 `int vt_probe_main(const char *path);`，Build Settings 指向它；
3. 用 README 里的 Swift 按钮代码，在 iPhone 18 Pro（iOS 26）上运行；
4. 记录每个码流在 **Require HW** 模式下：
   - `VTDecompressionSessionCreate` 是否成功；
   - `UsingHardwareAcceleratedVideoDecoder` 是 TRUE/FALSE；
   - 输出 fourcc（`'x444'`/`'xf44'`/`'x420'`/`'p444'` 等）。

### 5.4 判读矩阵

| 结果 | 结论 |
|---|---|
| T3/T4（444 10-bit）Require-HW 会话成功且 UsingHardware=TRUE | **A20 Pro 硬解 HEVC Rext 4:4:4 10-bit 成立**（grade b 升级；且说明 `'x444'` 输出可能来自硬件） |
| 只有 T5（420/idc=4）成功、T3/T4 失败 | 硬件支持 4:2:0 10-bit（含 idc=4），**不支持 4:4:4 10-bit** |
| T7（Main10/idc=2）成功、T5（idc=4）失败 | profile 信号（idc=4）影响 iOS 硬解接受度 → 需要 Main10 复刻（对应 `docs/review-apple-heic-config.md` 的方案） |
| Require-HW 全部失败、Enable/自动成功 | 无法确认硬件（可能是会话被拒/OS 限制），需换测试手段；至少说明“能播放 ≠ 硬解” |
| Enable/自动也失败 | 该 profile 完全不被 VT 接受（无论软硬） |

### 5.5 HEIC 静图路径（独立 gate，必须另测）

把 `super/T2.heic`、`T3.heic`、`T4.heic`、`T5.heic`、`T7.heic`（以及 `build/analysis-heic/candidates/` 下的 T7-like 候选）AirDrop 到 iPhone 18 Pro，逐个用「文件 App / 照片」打开：
- 能否显示、缩放是否缺块、打开耗时/发热；
- 色彩是否与 Mac 参考一致（P3）。
- iOS ImageIO **没有**“是否硬解”的公开标志，只能以“接受/不接受 + 时间/发热”辅助判断；如果 22.8MP 444 静图在 200ms 内打开，强烈提示硬件路径。

---

## 6. 对既有兼容性结论的修正（重要）

1. **T5.heic（4:2:0 10-bit，idc=4 "Main 4:2:0 10"）**：
   - **Mac Apple Silicon**：大概率可以硬解（Mac 硬件被 RE 认为能到 4:4:4 12-bit；社区矩阵也覆盖 Rext 420 8/10bit）。此前我只强调“文档 profile 集合外 → 有风险”，低估了 Mac 侧实际能力。
   - **iPhone**：仍 UNVERIFIED；需要 §5 的 Test A（T5 样本）。
2. **T3/T4.heic（4:4:4 10-bit）**：
   - Mac 侧：AVD 硬件按 RE “up to 4:4:4 12 bit”，4:4:4 10-bit **可能可硬解**；社区矩阵明确 Mac 10b 444 ✅。
   - iPhone 侧：**未知**；这正是 Test A 的关键样本（若 iPhone 硬解 444 10-bit，则 T3/T4 的 HEIC 兼容性担忧大幅下降）。
3. **通用表述修正**：
   - 以后再谈“Apple 硬解能力”，必须分三栏：**macOS Apple Silicon / iOS(iPhone, 具体型号) / HEIC-ImageIO 路径**，且标注证据类型（官方文档？社区矩阵？逆向？API？真机强锁？）。
   - “公开 API 常量缺失”只能说明“没有公开契约”，不能说明“硬件不支持”；“能播放/能显示”也不能说明“硬解”（VT HEIC/HEVC 有软解回退）。

---

## 7. 未决项（如实）

1. **iPhone 18 Pro / A20 Pro 的 Rext 4:4:4 10-bit 硬解**：目前 **UNVERIFIED**；需 §5.3 的 VT Require-HW 实测（本机是 Windows，无法代跑）。
2. **A 系列 AVD 是否与 M 系列同 IP/同能力**：无公开资料；LKML AVD 驱动只覆盖 M1–M3。
3. **ASIC 级直接证据**：公开渠道不存在（没有 Apple 的 ASIC 规格/探针）；能拿到的最强证据是“Require-HW 会话成功 + UsingHardware=TRUE + 输出格式”，属于设备级而非 ASIC 级。
4. **HEIC ImageIO 是否把 444 10-bit 送进硬件解码**：无公开标志；只能接受性+计时。
5. **`'x444'`/`'xf44'` 常量在 iOS 上的实际引入版本**：SDK 头文件确认存在（iPhoneOS 16.5），但 Apple 文档给的是枚举级可用性，具体版本/设备差异未逐个核实。
6. **本报告的 VT 探针程序未在 Apple 硬件上编译/运行过**（Windows 环境无法验证）；API 均为公开接口，私有 spec 键使用同值字符串兜底，若编译/运行报错请回传。

---

## 8. 证据与文件路径

- 逐条核验（含每条 claim 的 quote/URL/confidence）：`build/research-rext/findings.md`
- A20/iPhone 调研（§1–7 + 判定矩阵 + Swift 片段）：`build/research-a20/findings.md`
- 码流提取/level 修补/VT 探针源码与说明：`build/analysis-apple/`、`super/iphone-test/vt-probe/`
- 真机 HEIC 候选（T7-like Main10/grid）：`build/analysis-heic/candidates/`、`super/iphone-test/`
- 上游引用：
  - Community Chromium HEVC 指南：https://github.com/StaZhu/enable-chromium-hevc-hardware-decoding
  - Chromium 真实 Rext 编码 commit：https://github.com/chromium/chromium/commit/3eaf2f4e5259ef902a25920f85794d2ab4805c4e
  - Chromium 当前 macOS 解码器：https://raw.githubusercontent.com/chromium/chromium/main/media/gpu/mac/video_toolbox_video_decoder.cc
  - FFmpeg：https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavutil/hwcontext_videotoolbox.c 、https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/videotoolbox.c
  - CoreVideo 头文件（macOS 11.3 / iPhoneOS 16.5 SDK 镜像）
  - Apple AVD Linux 驱动系列：https://lore.kernel.org/all/20260918-avd-v1-0-49977931f455@icloud.com/ 、https://lore.kernel.org/all/20260918-avd-v1-13-49977931f455@icloud.com/
  - iOS VT Require/UsingHardware 文档（iOS 17+）
  - FFmpeg HEVC 软解回退 commit：https://github.com/FFmpeg/FFmpeg/commit/bcff983dc340e76518935111146c0e1daf4cb37b.patch
  - Project Zero CVE-2022-22675（iOS AppleAVD 解析 H.265）：https://googleprojectzero.github.io/0days-in-the-wild//0day-RCAs/2022/CVE-2022-22675.html
