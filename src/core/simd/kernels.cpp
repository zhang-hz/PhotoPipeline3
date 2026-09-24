// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd 热路径内核（M4-W5-T19）：flatten / quantize / transpose / interleave
//
// 契约（同签名、逐位相等判据、唯一分派层纪律、位精确口径）逐字见 core/simd/simd.h 的
// 「§11.2 五对热路径」块。本 TU 承载 4 对；第 5 对 downscale 在 core/simd/resample.cpp，
// CPUID 探测层在 core/simd/simd.cpp。
//
// 实现注记（T19）：
//   * 每对 `*_ref`（标量基准）与 `*_avx2`（热路径）**用同一算子序列**：乘加合一的地方
//     ref = std::fma / avx2 = _mm256_fmadd_ps，其余同序 mul/add —— 差异恒为 0，
//     故单测按「逐位相等」断言（容差不会替实现缺陷打掩护）。
//   * 分派入口（无后缀名）只做一次 `cpu_has_avx2()` 判断：这是整仓唯一一层运行期 ISA
//     分派（§11.2 纪律），调用方不得自行写 CPUID/ISA 宏分支。

// —— 编译期闸门（§11.1，与 simd.cpp 同款）——
// MSVC /arch:AVX2 与 GCC/Clang -march=x86-64-v3 都定义 __AVX2__；缺了它说明 flags 链断了
// （cmake/avx2.cmake 未生效），宁可构建失败，也不要产出"看似成功、实则退回 SSE2"的产物。
#if !defined(__AVX2__)
#error                                                                                             \
    "PP 0.3.0 基线要求 AVX2（§11.1）：core/simd 的 *_avx2 内核未取得 __AVX2__ —— 查 cmake/avx2.cmake。"
#endif

#include "core/simd/simd.h"

#include <immintrin.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace pp::simd {
namespace {

// ============================================================================================
// 共用 lane 工具
// ============================================================================================

// RGBA 交织的 4 个 YMM（= 8 像素）→ RGB 交织的 3 个 YMM（= 24 浮点），顺序写到 dst[0..24)。
// 纯 lane 选择（vpermps + vblendps），无任何算术 ⇒ 位精确。flatten(ch=4) 与 interleave(4→3)
// 共用同一份实现（两条路径的 3 通道落位口径必须一致，否则金样会出现"同一像素两套展开"）。
inline void pack_rgba8_to_rgb8(__m256 o0, __m256 o1, __m256 o2, __m256 o3, float *dst) {
    // 目标 24 浮点 = [R0 G0 B0 | R1 G1 B1 | … | R7 G7 B7]
    //   out0 = [o0[0],o0[1],o0[2], o0[4],o0[5],o0[6], o1[0],o1[1]]
    //   out1 = [o1[2], o1[4],o1[5],o1[6], o2[0],o2[1],o2[2], o2[4]]
    //   out2 = [o2[5],o2[6], o3[0],o3[1],o3[2], o3[4],o3[5],o3[6]]
    const __m256i ia0 = _mm256_setr_epi32(0, 1, 2, 4, 5, 6, 0, 0);
    const __m256i ib0 = _mm256_setr_epi32(0, 0, 0, 0, 0, 0, 0, 1);
    const __m256i ia1 = _mm256_setr_epi32(2, 4, 5, 6, 0, 0, 0, 0);
    const __m256i ib1 = _mm256_setr_epi32(0, 0, 0, 0, 0, 1, 2, 4);
    const __m256i ia2 = _mm256_setr_epi32(5, 6, 0, 0, 0, 0, 0, 0);
    const __m256i ib2 = _mm256_setr_epi32(0, 0, 0, 1, 2, 4, 5, 6);
    const __m256 m0 = _mm256_castsi256_ps(_mm256_setr_epi32(0, 0, 0, 0, 0, 0, -1, -1));
    const __m256 m1 = _mm256_castsi256_ps(_mm256_setr_epi32(0, 0, 0, 0, -1, -1, -1, -1));
    const __m256 m2 = _mm256_castsi256_ps(_mm256_setr_epi32(0, 0, -1, -1, -1, -1, -1, -1));

    const __m256 w0 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(o0, ia0),
                                       _mm256_permutevar8x32_ps(o1, ib0), m0);
    const __m256 w1 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(o1, ia1),
                                       _mm256_permutevar8x32_ps(o2, ib1), m1);
    const __m256 w2 = _mm256_blendv_ps(_mm256_permutevar8x32_ps(o2, ia2),
                                       _mm256_permutevar8x32_ps(o3, ib2), m2);
    _mm256_storeu_ps(dst + 0, w0);
    _mm256_storeu_ps(dst + 8, w1);
    _mm256_storeu_ps(dst + 16, w2);
}

// float32 [0,1] → uint8（8 个样本），语义 = simd.h 的 quantize8 契约。
inline __m128i quantize8_avx2_8(__m256 v) {
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 ord = _mm256_cmp_ps(v, v, _CMP_ORD_Q); // NaN 车道 → 全 0
    __m256 c = _mm256_min_ps(_mm256_max_ps(v, zero), one); // 负 → 0；> 1 → 1
    c = _mm256_and_ps(c, ord);                             // NaN → 0（与标量的 !(v>0) 同判据）
    const __m256 q =
        _mm256_add_ps(_mm256_mul_ps(c, _mm256_set1_ps(255.0f)), _mm256_set1_ps(0.5f));
    const __m256i iv = _mm256_cvttps_epi32(q); // 截断（q ≥ 0 ⇒ 等于 floor）
    const __m128i lo = _mm256_castsi256_si128(iv);
    const __m128i hi = _mm256_extracti128_si256(iv, 1);
    const __m128i p16 = _mm_packus_epi32(lo, hi); // 8×u16（值 ≤ 255）
    return _mm_packus_epi16(p16, p16);            // 低 8 字节 = 8 个样本
}

} // namespace

// ============================================================================================
// 1) flatten —— alpha 合成到常量底色 + 原地缩通道
// ============================================================================================
// ref = 既有 pipeline 标量循环的逐字重写（src/core/pipeline.cpp flatten_alpha_in_place）：
//   逐像素 a = clamp(alpha,0,1)；t = bg*(1-a)；dst[c] = fma(src[c], a, t)。
void flatten_ref(const float *src, float *dst, std::size_t npix, int ch, float bg) {
    assert(ch == 2 || ch == 4);
    if (ch != 2 && ch != 4)
        return;
    const int out_ch = (ch == 2) ? 1 : 3;
    const float b = std::clamp(bg, 0.0f, 1.0f);
    for (std::size_t i = 0; i < npix; ++i) {
        const std::size_t s = i * static_cast<std::size_t>(ch);
        const std::size_t d = i * static_cast<std::size_t>(out_ch);
        const float a = std::clamp(src[s + static_cast<std::size_t>(ch - 1)], 0.0f, 1.0f);
        const float t = b * (1.0f - a);
        for (int c = 0; c < out_ch; ++c)
            dst[d + static_cast<std::size_t>(c)] =
                std::fma(src[s + static_cast<std::size_t>(c)], a, t);
    }
}

void flatten_avx2(const float *src, float *dst, std::size_t npix, int ch, float bg) {
    assert(ch == 2 || ch == 4);
    if (ch != 2 && ch != 4)
        return;
    const int out_ch = (ch == 2) ? 1 : 3;
    const float bgc = std::clamp(bg, 0.0f, 1.0f);
    const __m256 bg8 = _mm256_set1_ps(bgc);
    const __m256 one8 = _mm256_set1_ps(1.0f);
    const __m256 zero8 = _mm256_setzero_ps();
    const __m128 b4 = _mm_set1_ps(bgc);
    const __m128 one4 = _mm_set1_ps(1.0f);
    const __m128 zero4 = _mm_setzero_ps();
    // 逐像素 alpha 广播（std::clamp 的 NaN → v 同判据：MAXPS/MINPS 任一操作数为 NaN 时
    // 返回**第二操作数**，故把待夹值放第二位）。
    const auto clamp_alpha = [](__m128 a, __m128 z, __m128 o) {
        return _mm_min_ps(o, _mm_max_ps(z, a));
    };

    std::size_t i = 0;
    if (ch == 4) {
        // 形态说明（T19 实测裁定，§11.2 原文写"8px/迭代"）：**4 像素/迭代（2 条 YMM 链）**。
        // 三组候选实测对照（T19 selfChecks 的对照读数）：
        //   ① 8px/迭代 + vpermps/blend 打包：6 vpermps + 3 blend 把 shuffle 端口打满 → 最慢；
        //   ② 2px/迭代 YMM + vextractf128：中；
        //   ③ 2px/迭代纯 XMM（零 shuffle）：与②相当偏慢（uop 更多）。
        // 本形态 = ②的 2× 展开（4 像素/迭代、两条独立依赖链 ⇒ ILP），每 2 像素用 1 次
        // vshufps（alpha 广播）+ 1 次 vextractf128（取高半区），输出用 16B 存储。
        // **尾随垃圾车道**由下一次存储/标量尾覆盖（每像素写 4 浮点、前进 3 浮点）⇒ 不越界。
        // 语义与判据（逐位相等 / 原地别名 / NaN 口径）不变。
        for (; i + 4 < npix; i += 4) {
            const float *s0 = src + i * 4;
            const __m256 y0 = _mm256_loadu_ps(s0);     // 像素 i, i+1
            const __m256 y1 = _mm256_loadu_ps(s0 + 8); // 像素 i+2, i+3
            const __m256 a0 = _mm256_min_ps(
                one8, _mm256_max_ps(zero8, _mm256_shuffle_ps(y0, y0, 0xFF)));
            const __m256 a1 = _mm256_min_ps(
                one8, _mm256_max_ps(zero8, _mm256_shuffle_ps(y1, y1, 0xFF)));
            const __m256 w0 =
                _mm256_fmadd_ps(y0, a0, _mm256_mul_ps(bg8, _mm256_sub_ps(one8, a0)));
            const __m256 w1 =
                _mm256_fmadd_ps(y1, a1, _mm256_mul_ps(bg8, _mm256_sub_ps(one8, a1)));
            _mm_storeu_ps(dst + i * 3, _mm256_castps256_ps128(w0));       // R0 G0 B0 (+垃圾)
            _mm_storeu_ps(dst + i * 3 + 3, _mm256_extractf128_ps(w0, 1)); // R1 G1 B1 (+垃圾)
            _mm_storeu_ps(dst + i * 3 + 6, _mm256_castps256_ps128(w1));   // R2 G2 B2 (+垃圾)
            _mm_storeu_ps(dst + i * 3 + 9, _mm256_extractf128_ps(w1, 1)); // R3 G3 B3 (+垃圾)
        }
        for (; i + 2 < npix; i += 2) { // 余下的成对像素
            const float *s0 = src + i * 4;
            const __m256 y = _mm256_loadu_ps(s0); // [R0 G0 B0 A0 R1 G1 B1 A1]
            const __m256 a =
                _mm256_min_ps(one8, _mm256_max_ps(zero8, _mm256_shuffle_ps(y, y, 0xFF)));
            const __m256 w = _mm256_fmadd_ps(y, a, _mm256_mul_ps(bg8, _mm256_sub_ps(one8, a)));
            _mm_storeu_ps(dst + i * 3, _mm256_castps256_ps128(w));
            _mm_storeu_ps(dst + i * 3 + 3, _mm256_extractf128_ps(w, 1));
        }
    } else {
        // ch == 2（灰度 + alpha）→ 1 通道：4 像素/迭代 = 8 浮点 = 2 个 XMM。
        // 每 128 位半区 [V A] ⇒ alpha 广播 imm = _MM_SHUFFLE(3,3,1,1)（[A0,A0,A1,A1]）。
        for (; i + 4 <= npix; i += 4) {
            const __m128 x0 = _mm_loadu_ps(src + i * 2);     // 像素 0,1
            const __m128 x1 = _mm_loadu_ps(src + i * 2 + 4); // 像素 2,3
            const __m128 a0 = clamp_alpha(_mm_shuffle_ps(x0, x0, _MM_SHUFFLE(3, 3, 1, 1)), zero4,
                                          one4);
            const __m128 a1 = clamp_alpha(_mm_shuffle_ps(x1, x1, _MM_SHUFFLE(3, 3, 1, 1)), zero4,
                                          one4);
            const __m128 o0 = _mm_fmadd_ps(x0, a0, _mm_mul_ps(b4, _mm_sub_ps(one4, a0)));
            const __m128 o1 = _mm_fmadd_ps(x1, a1, _mm_mul_ps(b4, _mm_sub_ps(one4, a1)));
            // 每半区取车道 0,2 → [V0 V1 V2 V3]（一次 16B 存储，无垃圾车道）
            _mm_storeu_ps(dst + i, _mm_shuffle_ps(o0, o1, _MM_SHUFFLE(2, 0, 2, 0)));
        }
    }
    if (i < npix) // 尾块：同一语义的标量面（逐位一致）
        flatten_ref(src + i * static_cast<std::size_t>(ch),
                    dst + i * static_cast<std::size_t>(out_ch), npix - i, ch, bg);
}

void flatten(const float *src, float *dst, std::size_t npix, int ch, float bg) {
    if (cpu_has_avx2())
        flatten_avx2(src, dst, npix, ch, bg);
    else
        flatten_ref(src, dst, npix, ch, bg);
}

// ============================================================================================
// 2) quantize —— 编码器入口 float32 → 无符号整数样本（E6 口径）
// ============================================================================================
// ref = 既有 to_u8 / to_u16 的逐字重写（enc_jpegli / enc_webp / enc_jxl 三处同一形态）：
//   v 非正（含 NaN）→ 0；v ≥ 1 → maxv；否则 (uint)(v * maxv + 0.5f)（float 乘加后截断）。
void quantize8_ref(const float *src, std::uint8_t *dst, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        const float v = src[i];
        const float c = (v > 0.0f) ? (v < 1.0f ? v : 1.0f) : 0.0f; // NaN → 0
        dst[i] = static_cast<std::uint8_t>(c * 255.0f + 0.5f);
    }
}

void quantize8_avx2(const float *src, std::uint8_t *dst, std::size_t n) {
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m128i p8 = quantize8_avx2_8(_mm256_loadu_ps(src + i));
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + i), p8);
    }
    if (i < n)
        quantize8_ref(src + i, dst + i, n - i);
}

void quantize16_ref(const float *src, std::uint16_t *dst, std::size_t n, int maxv) {
    const float mx = static_cast<float>(maxv);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = src[i];
        const float c = (v > 0.0f) ? (v < 1.0f ? v : 1.0f) : 0.0f;
        dst[i] = static_cast<std::uint16_t>(c * mx + 0.5f);
    }
}

void quantize16_avx2(const float *src, std::uint16_t *dst, std::size_t n, int maxv) {
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 scale = _mm256_set1_ps(static_cast<float>(maxv));
    const __m256 half = _mm256_set1_ps(0.5f);
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        const __m256 v = _mm256_loadu_ps(src + i);
        const __m256 ord = _mm256_cmp_ps(v, v, _CMP_ORD_Q);
        __m256 c = _mm256_min_ps(_mm256_max_ps(v, zero), one);
        c = _mm256_and_ps(c, ord);
        const __m256i iv = _mm256_cvttps_epi32(_mm256_add_ps(_mm256_mul_ps(c, scale), half));
        const __m128i p16 = _mm_packus_epi32(_mm256_castsi256_si128(iv),
                                             _mm256_extracti128_si256(iv, 1));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), p16);
    }
    if (i < n)
        quantize16_ref(src + i, dst + i, n - i, maxv);
}

void quantize8(const float *src, std::uint8_t *dst, std::size_t n) {
    if (cpu_has_avx2())
        quantize8_avx2(src, dst, n);
    else
        quantize8_ref(src, dst, n);
}

void quantize16(const float *src, std::uint16_t *dst, std::size_t n, int maxv) {
    if (cpu_has_avx2())
        quantize16_avx2(src, dst, n, maxv);
    else
        quantize16_ref(src, dst, n, maxv);
}

// ============================================================================================
// 3) transpose —— 8×8 分块平面转置（EXIF 5/7 的 orientation 面）
// ============================================================================================
// 纯数据搬运（无算术）⇒ ref 与 avx2 的逐位相等是构造性的。
void transpose8_ref(const float *src, int sw, int sh, float *dst) {
    assert(sw > 0 && sh > 0);
    for (int y = 0; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            dst[static_cast<std::size_t>(x) * static_cast<std::size_t>(sh) +
                static_cast<std::size_t>(y)] =
                src[static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) +
                    static_cast<std::size_t>(x)];
        }
    }
}

void transpose8_avx2(const float *src, int sw, int sh, float *dst) {
    assert(sw > 0 && sh > 0);
    int by = 0;
    for (; by + 8 <= sh; by += 8) {
        int bx = 0;
        for (; bx + 8 <= sw; bx += 8) {
            // 载入 8 行 × 8 列（行步长 sw）
            __m256 r0 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 0) * sw + bx);
            __m256 r1 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 1) * sw + bx);
            __m256 r2 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 2) * sw + bx);
            __m256 r3 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 3) * sw + bx);
            __m256 r4 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 4) * sw + bx);
            __m256 r5 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 5) * sw + bx);
            __m256 r6 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 6) * sw + bx);
            __m256 r7 = _mm256_loadu_ps(src + static_cast<std::size_t>(by + 7) * sw + bx);
            // 标准 8×8 float 转置：unpack → shuffle → permute2f128（三段各 8/16 条指令）
            const __m256 t0 = _mm256_unpacklo_ps(r0, r1);
            const __m256 t1 = _mm256_unpackhi_ps(r0, r1);
            const __m256 t2 = _mm256_unpacklo_ps(r2, r3);
            const __m256 t3 = _mm256_unpackhi_ps(r2, r3);
            const __m256 t4 = _mm256_unpacklo_ps(r4, r5);
            const __m256 t5 = _mm256_unpackhi_ps(r4, r5);
            const __m256 t6 = _mm256_unpacklo_ps(r6, r7);
            const __m256 t7 = _mm256_unpackhi_ps(r6, r7);
            const __m256 s0 = _mm256_shuffle_ps(t0, t2, 0x44);
            const __m256 s1 = _mm256_shuffle_ps(t0, t2, 0xEE);
            const __m256 s2 = _mm256_shuffle_ps(t1, t3, 0x44);
            const __m256 s3 = _mm256_shuffle_ps(t1, t3, 0xEE);
            const __m256 s4 = _mm256_shuffle_ps(t4, t6, 0x44);
            const __m256 s5 = _mm256_shuffle_ps(t4, t6, 0xEE);
            const __m256 s6 = _mm256_shuffle_ps(t5, t7, 0x44);
            const __m256 s7 = _mm256_shuffle_ps(t5, t7, 0xEE);
            r0 = _mm256_permute2f128_ps(s0, s4, 0x20);
            r1 = _mm256_permute2f128_ps(s1, s5, 0x20);
            r2 = _mm256_permute2f128_ps(s2, s6, 0x20);
            r3 = _mm256_permute2f128_ps(s3, s7, 0x20);
            r4 = _mm256_permute2f128_ps(s0, s4, 0x31);
            r5 = _mm256_permute2f128_ps(s1, s5, 0x31);
            r6 = _mm256_permute2f128_ps(s2, s6, 0x31);
            r7 = _mm256_permute2f128_ps(s3, s7, 0x31);
            // 转置块的列 c（= 寄存器 r_c）→ dst 行 (bx + c)，连续 8 浮点（列 by..by+7）
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 0) * sh + by, r0);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 1) * sh + by, r1);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 2) * sh + by, r2);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 3) * sh + by, r3);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 4) * sh + by, r4);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 5) * sh + by, r5);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 6) * sh + by, r6);
            _mm256_storeu_ps(dst + static_cast<std::size_t>(bx + 7) * sh + by, r7);
        }
        // 右侧尾列（bx..sw-1）：标量（与分块部分逐位一致）
        for (int y = by; y < by + 8; ++y) {
            for (int x = bx; x < sw; ++x) {
                dst[static_cast<std::size_t>(x) * sh + y] =
                    src[static_cast<std::size_t>(y) * sw + x];
            }
        }
    }
    // 下侧尾行：整行标量
    for (int y = by; y < sh; ++y) {
        for (int x = 0; x < sw; ++x) {
            dst[static_cast<std::size_t>(x) * sh + y] =
                src[static_cast<std::size_t>(y) * sw + x];
        }
    }
}

void transpose8(const float *src, int sw, int sh, float *dst) {
    if (cpu_has_avx2())
        transpose8_avx2(src, sw, sh, dst);
    else
        transpose8_ref(src, sw, sh, dst);
}

// ============================================================================================
// 4) interleave —— 编码器入参的通道交织/展开（通道选择 + 复制；纯搬运）
// ============================================================================================
void interleave_ref(const float *src, int nch, float *dst, int out_ch, std::size_t npix) {
    assert(nch >= 1 && nch <= 4 && (out_ch == 1 || out_ch == 3 || out_ch == 4));
    const float opaque = 1.0f;
    for (std::size_t i = 0; i < npix; ++i) {
        const float *p = src + i * static_cast<std::size_t>(nch);
        float *q = dst + i * static_cast<std::size_t>(out_ch);
        if (out_ch == 1) {
            q[0] = p[0];
            continue;
        }
        q[0] = p[0];
        q[1] = (nch >= 3) ? p[1] : p[0];
        q[2] = (nch >= 3) ? p[2] : p[0];
        if (out_ch == 4) {
            if (nch == 4)
                q[3] = p[3];
            else if (nch == 2)
                q[3] = p[1];
            else
                q[3] = opaque;
        }
    }
}

void interleave_avx2(const float *src, int nch, float *dst, int out_ch, std::size_t npix) {
    assert(nch >= 1 && nch <= 4 && (out_ch == 1 || out_ch == 3 || out_ch == 4));
    std::size_t i = 0;
    if (out_ch == nch) { // 等通道直通（1→1 / 2→2 / 3→3 / 4→4）：纯拷贝（无 lane 编排）
        const std::size_t total = npix * static_cast<std::size_t>(nch);
        for (; i + 8 <= total; i += 8)
            _mm256_storeu_ps(dst + i, _mm256_loadu_ps(src + i));
        if (i < total)
            std::memcpy(dst + i, src + i, (total - i) * sizeof(float));
        return;
    } else if (nch == 4 && out_ch == 3) { // RGBA → RGB（丢弃 alpha；纯搬运）
        // 与 flatten 同一手法（T19 实测：vpermps/blend 打包把 shuffle 端口打满，实测最慢）：
        // 每像素 1×16B 载入 + 1×16B 存储，**尾随垃圾车道由下一像素的存储覆盖**
        // （写 4 浮点、前进 3 浮点）；末像素留给标量尾 ⇒ 不越界。零 shuffle。
        for (; i + 1 < npix; i += 1)
            _mm_storeu_ps(dst + i * 3, _mm_loadu_ps(src + i * 4));
    } else if (nch == 1 && out_ch == 3) { // 灰度 → RGB（复制三份）
        const __m256i idx = _mm256_setr_epi32(0, 0, 0, 1, 1, 1, 2, 2);
        for (; i + 8 <= npix; i += 8) {
            const __m256 v = _mm256_loadu_ps(src + i);
            _mm256_storeu_ps(dst + i * 3 + 0, _mm256_permutevar8x32_ps(v, idx));
            _mm256_storeu_ps(dst + i * 3 + 8, _mm256_permutevar8x32_ps(v, _mm256_setr_epi32(2, 3, 3, 3, 4, 4, 4, 5)));
            _mm256_storeu_ps(dst + i * 3 + 16, _mm256_permutevar8x32_ps(v, _mm256_setr_epi32(5, 5, 6, 6, 6, 7, 7, 7)));
        }
    } else if (nch == 1 && out_ch == 4) { // 灰度 → RGBA（RGB 复制 + A = 1）
        const __m256 one = _mm256_set1_ps(1.0f);
        const __m256i i0 = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1);
        const __m256i i1 = _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
        const __m256i i2 = _mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5);
        const __m256i i3 = _mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);
        const __m256 m = _mm256_castsi256_ps(_mm256_setr_epi32(0, 0, 0, -1, 0, 0, 0, -1));
        for (; i + 8 <= npix; i += 8) {
            const __m256 v = _mm256_loadu_ps(src + i);
            float *q = dst + i * 4;
            _mm256_storeu_ps(q + 0, _mm256_blendv_ps(_mm256_permutevar8x32_ps(v, i0), one, m));
            _mm256_storeu_ps(q + 8, _mm256_blendv_ps(_mm256_permutevar8x32_ps(v, i1), one, m));
            _mm256_storeu_ps(q + 16, _mm256_blendv_ps(_mm256_permutevar8x32_ps(v, i2), one, m));
            _mm256_storeu_ps(q + 24, _mm256_blendv_ps(_mm256_permutevar8x32_ps(v, i3), one, m));
        }
    } else if (nch == 2 && out_ch == 4) { // 灰度 + alpha → RGBA（RGB 复制 + A = src[1]）
        const __m256i idx0 = _mm256_setr_epi32(0, 0, 0, 1, 2, 2, 2, 3); // 像素 0,1
        const __m256i idx1 = _mm256_setr_epi32(4, 4, 4, 5, 6, 6, 6, 7); // 像素 2,3
        for (; i + 4 <= npix; i += 4) { // 4 像素 = 8 浮点 = 1 个 YMM → 2 个 YMM（16 浮点）
            const __m256 v = _mm256_loadu_ps(src + i * 2);
            _mm256_storeu_ps(dst + i * 4, _mm256_permutevar8x32_ps(v, idx0));
            _mm256_storeu_ps(dst + i * 4 + 8, _mm256_permutevar8x32_ps(v, idx1));
        }
    } else if (nch == 2 && out_ch == 1) { // 灰度 + alpha → 灰度（取偶数车道）
        const __m256i idx = _mm256_setr_epi32(0, 2, 4, 6, 0, 0, 0, 0);
        for (; i + 8 <= npix; i += 8) { // 8 像素 = 16 浮点 = 2 个 YMM → 1 个 YMM 结果
            const __m256 v0 = _mm256_loadu_ps(src + i * 2);
            const __m256 v1 = _mm256_loadu_ps(src + i * 2 + 8);
            const __m256 g0 = _mm256_permutevar8x32_ps(v0, idx);
            const __m256 g1 = _mm256_permutevar8x32_ps(v1, idx);
            _mm256_storeu_ps(dst + i, _mm256_permute2f128_ps(g0, g1, 0x20));
        }
    } else {
        // 其余形态（2→3 / 3→4 / 3→1 / 4→1）：语义由 ref 承载（同一份规则）。
        // 如实记录：这几组在当前编码器调用面**无调用点**（webp：1→3/2→4/3→3/4→4；
        // jpegli：2→1/4→3/1→1/3→3），故不为它们引入跨寄存器 lane 编排的复杂度。
        interleave_ref(src, nch, dst, out_ch, npix);
        return;
    }
    if (i < npix) // 尾块：同一语义的标量面
        interleave_ref(src + i * static_cast<std::size_t>(nch),
                       nch, dst + i * static_cast<std::size_t>(out_ch), out_ch, npix - i);
}

void interleave(const float *src, int nch, float *dst, int out_ch, std::size_t npix) {
    if (cpu_has_avx2())
        interleave_avx2(src, nch, dst, out_ch, npix);
    else
        interleave_ref(src, nch, dst, out_ch, npix);
}

} // namespace pp::simd
