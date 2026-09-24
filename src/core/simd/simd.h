// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd：AVX2 热路径的公共面（M4-T3 地基；5 对实现见 W5-T19）
//
// 依据：docs/v0.3.0-design.md §11.2（自研热路径 SIMD 化 + 运行期分派纪律）、
//       docs/v0.3.0-design.md §2 变更地图 `core/simd`（新，加性模块）。
//
// —— 编译期基线（§11.1）——
//   整仓基线 ISA = x86-64-v3 / /arch:AVX2，由 cmake/avx2.cmake（自研 targets）与
//   triplets/x64-windows-avx2.cmake、triplets/x64-linux-avx2.cmake（vcpkg 依赖）成对保证。
//   因此本模块**不是**「为兼容老机器而分派」——基线产物本身即 AVX2。
//
// —— 运行期分派注记（§11.2；本模块是整仓唯一一层运行期 ISA 分派）——
//   分派存在只为三件事：
//     ① `*_ref` 标量实现服务单测与调试，作为 `*_avx2` 实现的正确性基准
//        （flatten/interleave 逐位相等、quantize 舍入一致、transpose 逐位、downscale ≤1e-5）；
//     ② 缺失 AVX2 的宿主上给出**明确失败点**，而不是执行非法指令（SIGILL / 0xC000001D）；
//     ③ 后续可选加速（如 AVX-512 专项）的扩展点收在本文件，不外溢到调用方。
//   纪律：调用方（`pipeline` / `codecs` / `thumbs`）**不得**自己写 CPUID 或 ISA 宏分支；
//   需要 AVX2 路径时一律问本模块。
//
// —— 交付范围 ——
//   W0-T3：探测层 `cpu_has_avx2()`；
//   W5-T19：5 对热路径（flatten / quantize / transpose / downscale / interleave）
//   及其同签名 `*_avx2` / `*_ref` 实现、分派入口与一致性单测（见文件下半部分的
//   「§11.2 五对热路径」块）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace pp::simd {

// 本进程所在 CPU 是否真的支持 AVX2（CPUID 检测，含 OS 的 XSAVE/YMM 状态确认）。
//
// 判据（x86-64）：
//   leaf 1 ECX[27] OSXSAVE && ECX[28] AVX && XCR0[2:1]==11（XMM+YMM 由 OS 保存）
//   leaf 7 subleaf 0 EBX[5] AVX2
// 非 x86 架构恒返回 false（本仓库当前无该情形，留作显式失败点而非静默 UB）。
//
// 结果缓存于函数内静态量（线程安全初始化，实测成本 = 首次一次 CPUID）。
// 调用约定：热路径可在初始化期问一次并缓存布尔，不必逐帧调用。
bool cpu_has_avx2();

// ============================================================================================
// §11.2 五对热路径（M4-W5-T19 落地）
// ============================================================================================
// 纪律（逐字沿用上方注记）：
//   * 每对 `*_avx2` / `*_ref` **同签名**；`*_ref` 是标量实现，服务单测与调试（正确性基准），
//     `*_avx2` 是热路径实现；
//   * 调用方（pipeline / codecs / thumbs）一律走**无后缀的分派入口**，不得自行写 CPUID
//     或 ISA 宏分支（本模块是整仓唯一一层运行期 ISA 分派）；
//   * 一致性判据（§11.2 + 本任务出口）：flatten / interleave / transpose 逐位相等、
//     quantize 舍入一致、downscale 容差 ≤1e-5 —— 见 tests/unit/test_simd.cpp。
//
// **位精确口径（T19 落地说明）**：五对内部都用**同一算子序列** —— 需要乘加合一的地方
//   ref 用 `std::fma`（单次正确舍入）、avx2 用 `_mm256_fmadd_ps`；其余为同序 mul/add。
//   因此 ref 与 avx2 的差异在数学上恒为 0，单测按「逐位相等」断言 —— 不靠容差掩盖实现缺陷
//   （downscale 的 ≤1e-5 是 §11.2 的下限要求，实测 = 0，见 T19 selfChecks）。
//   语义等价性（接线后的硬要求）由单测 + 金样 20 对仲裁：各 `*_ref` 的语义逐字对齐其
//   **被替换的既有标量实现**（pipeline 的 flatten 循环 / 编码器的 to_u8·to_u16 / OIIO
//   IBA::resize 的 lanczos3 口径），逐处调用点注明来源。

// —— 1) flatten：alpha 合成到常量底色 + 原地缩通道（§4.3 / §5.5）——
//   src：npix×ch 交织 float32（ch ∈ {2,4}）；dst：npix×out_ch（out_ch = ch == 2 ? 1 : 3）。
//   语义逐字等于 pipeline 既有标量循环（src/core/pipeline.cpp flatten_alpha_in_place）：
//     a  = clamp(src[i*ch + ch - 1], 0, 1)
//     dst[i*out_ch + c] = fma(src[i*ch + c], a, bg * (1 - a))    （c < out_ch；bg 已 clamp）
//   **允许别名（src == dst = 原地缩通道）**：逐块推进（块内先读后写，见 kernels.cpp 的形态说明）；
//   out_ch < ch ⇒ 写指针恒落后于读指针（块 k 的写区 [24k, 24k+24) 与尚未读的源区间不相交），
//   与 §4.3「零额外帧」内存纪律一致。
void flatten_ref(const float *src, float *dst, std::size_t npix, int ch, float bg);
void flatten_avx2(const float *src, float *dst, std::size_t npix, int ch, float bg);
void flatten(const float *src, float *dst, std::size_t npix, int ch, float bg);

// —— 2) quantize：编码器入口的 float32 → 无符号整数样本（E6 口径：标准舍入、无抖动、clamp）——
//   语义逐字等于 enc_jpegli / enc_webp / enc_jxl 既有的 `to_u8` / `to_u16`：
//     v 非正（含 NaN）→ 0；v ≥ 1 → maxv；否则 (uint)(v * maxv + 0.5f)（float 乘加后截断）
//   quantize8：maxv = 255 固定（u8 输出面）；quantize16：maxv = (1<<bd)-1，bd ∈ 8/10/12/16
//   （heif/avif 的 10/12 位面亦走本函数，dst 为 u16 中间面，调用点自行降位存储）。
void quantize8_ref(const float *src, std::uint8_t *dst, std::size_t n);
void quantize8_avx2(const float *src, std::uint8_t *dst, std::size_t n);
void quantize8(const float *src, std::uint8_t *dst, std::size_t n);
void quantize16_ref(const float *src, std::uint16_t *dst, std::size_t n, int maxv);
void quantize16_avx2(const float *src, std::uint16_t *dst, std::size_t n, int maxv);
void quantize16(const float *src, std::uint16_t *dst, std::size_t n, int maxv);

// —— 3) transpose：8×8 分块平面转置（EXIF 5/7 的 orientation 转置面）——
//   src：sw×sh 行主序（src[y*sw + x]）→ dst：sh 宽 × sw 高（dst[x*sh + y] = src[y*sw + x]）。
//   纯数据搬运（无算术）⇒ 逐位相等是构造性成立的。单通道平面；多通道调用方自行逐通道调用。
//   非 8 对齐的右/下边缘走标量尾块（与分块部分逐位一致）。
void transpose8_ref(const float *src, int sw, int sh, float *dst);
void transpose8_avx2(const float *src, int sw, int sh, float *dst);
void transpose8(const float *src, int sw, int sh, float *dst);

// —— 4) downscale：LANCZOS3 两遍（水平 + 垂直）降采样（§6.1 预览降采样面）——
//   src：sw×sh 交织 float32（sc 通道）→ dst：dw×dh 交织 float32（dc 通道，逐通道独立）。
//   语义对齐 OIIO `ImageBufAlgo::resize(filtername="lanczos3")` 的口径（thumbs 的原实现）：
//     * ratio = dst/src；滤波半径 radi = ceil(1.5 / ratio)（**降采样时支撑按比例展宽** =
//       抗混叠面积滤波），抽头数 = 2*radi+1；
//     * 第 i 个抽头权重 = lanczos3(2 * ratio * (i - radi - (src_xf_frac - 0.5)))，其中
//       src_xf = (x + 0.5) * (sw/dw)，src_xf_frac = fract(src_xf)；
//       权重经 Σ 归一化（totalweight != 0 时逐项除以总和）；
//     * 源坐标越界按**边缘 clamp**（OIIO 的 WrapClamp）取样本；
//     * 两遍分离：先水平（得到 dw×sh 中间面），再垂直 → dw×dh。
//   与 OIIO 原实现的数值差异仅来自求和次序（OIIO 在单循环内按 j 外 i 内累加乘积项），
//   量级 ≈ 1 ulp/抽头；T19 实测（test_simd 的 OIIO 交叉断言 + bench）见 selfChecks。
//   不放大：ratio ≥ 1 时本函数同样可用（公式不变），但 thumbs 的 fit_target 已保证不放大。
void downscale_ref(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc);
void downscale_avx2(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc);
void downscale(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc);

// —— 5) interleave：编码器入参的通道交织/展开（通道选择 + 复制；纯搬运，无算术）——
//   src：npix×nch 交织 float32（nch ∈ {1,2,3,4}）→ dst：npix×out_ch 交织 float32（out_ch ∈
//   {1,3,4}）。 规则逐字等于 enc_webp 的既有展开循环（wave 2 的 out_channels 派生）与 enc_jpegli 的
//   行内通道选择：
//     out_ch == 1：dst[0] = src[0]                       （灰度；丢弃 alpha/多余通道）
//     out_ch == 3：R,G,B = nch ≥ 3 ? (src[0],src[1],src[2]) : (src[0],src[0],src[0])
//     out_ch == 4：R,G,B 同上；A = nch == 4 ? src[3] : (nch == 2 ? src[1] : 1.0f)
//   实现用 permute/broadcast/unpack；AVX2 的 gather 指令（vpgatherdd）吞吐约 1 元素/周期，
//   对本模块的「交织→交织」调用面（连续源、连续目标）是负优化，故不采用（如实记录）。
void interleave_ref(const float *src, int nch, float *dst, int out_ch, std::size_t npix);
void interleave_avx2(const float *src, int nch, float *dst, int out_ch, std::size_t npix);
void interleave(const float *src, int nch, float *dst, int out_ch, std::size_t npix);

} // namespace pp::simd
