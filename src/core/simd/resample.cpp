// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd 热路径内核（M4-W5-T19）：downscale（LANCZOS3 两遍降采样）
//
// 契约见 core/simd/simd.h 的「4) downscale」块。语义逐条对齐 thumbs 的原实现
// （OIIO `ImageBufAlgo::resize(filtername="lanczos3")`）：支撑按降采样比例展宽、
// 权重归一化、边缘 clamp、两遍分离（水平 → 垂直）。
//
// 权重与滤波器的算术逐式对齐 OIIO 源码（本机 .cache/t2-patcheck/oiio/src/
// src/libutil/filter.cpp:433-478+841-862 的 FilterLanczos3_1D/2D 与 FilterDesc 表、
// src/libOpenImageIO/imagebufalgo_xform.cpp:592-760+826-853 的 resize_/get_resize_filter）：
//   * get_resize_filter：未给 filterwidth 时 w = fd.width × max(1, ratio)，
//     而 FilterDesc 表里 "lanczos3" 的 width = **6**（不是 3）⇒ 降采样时 w = 6；
//   * xfilt(u) = lanczos3(u × m_scale)，m_scale = 6 / w（w = 6 ⇒ 降采样时 m_scale = 1）；
//   * filterrad = w / 2；rad = ceil(filterrad / ratio)；taps = 2*rad + 1；
//   * src_f = ((x + 0.5) × (1/dst)) × src —— 逐式见 build_axis；
//   * 权重和 != 0 时逐项除以权重和；源索引越界 clamp 到 [0, n-1]。
// 与 OIIO 的**唯一**数值差异 = 求和次序（OIIO 在单循环里按 j 外 i 内累加 wy*wx*src），
// 量级 ≈ 1 ulp/抽头；test_simd 的 OIIO 交叉断言给出实测上界。

// —— 编译期闸门（§11.1，与 simd.cpp 同款）——
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
#include <vector>

namespace pp::simd {
namespace {

// OIIO FilterLanczos3_1D::lanczos3 的逐字重写（含算子次序：a/(x²π²) 先除后乘）。
inline float lanczos3(float x) {
    const float a = 3.0f; // Lanczos 3 lobe
    const float ainv = 1.0f / a;
    const float pi = 3.14159265358979323846f;
    x = std::fabs(x);
    if (x > a)
        return 0.0f;
    if (x < 0.0001f)
        return 1.0f;
    const float s1 = std::sin(x * ainv * pi);       // sin(x·π/a)
    const float s3 = (-4.0f * s1 * s1 + 3.0f) * s1; // sin(3x·π/a) == sin(x·π)
    return a / (x * x * (pi * pi)) * s1 * s3;
}

// 一维重采样权重表：每个输出位置的 taps 个权重 + 对应的（已 clamp 的）源索引。
struct Axis {
    int taps = 0;
    std::vector<float> w; // [dst_n * taps]
    std::vector<int> idx; // [dst_n * taps]
};

void build_axis(int src_n, int dst_n, Axis &ax) {
    assert(src_n > 0 && dst_n > 0);
    constexpr float kDescWidth = 6.0f; // FilterDesc["lanczos3"].width（lanczos3 = 半径 3）
    const float ratio = static_cast<float>(dst_n) / static_cast<float>(src_n);
    // get_resize_filter：未给 filterwidth ⇒ w = 6 × max(1, ratio)；m_scale = 6 / w。
    const float filter_w = kDescWidth * std::max(1.0f, ratio);
    const float m_scale = kDescWidth / filter_w;
    const int rad = static_cast<int>(std::ceil((filter_w / 2.0f) / ratio));
    ax.taps = 2 * rad + 1;
    const float dpix = 1.0f / static_cast<float>(dst_n);
    ax.w.assign(static_cast<std::size_t>(dst_n) * static_cast<std::size_t>(ax.taps), 0.0f);
    ax.idx.assign(static_cast<std::size_t>(dst_n) * static_cast<std::size_t>(ax.taps), 0);
    for (int x = 0; x < dst_n; ++x) {
        const float s = (static_cast<float>(x) + 0.5f) * dpix;
        const float src_f = s * static_cast<float>(src_n);
        const int base = static_cast<int>(std::floor(src_f));
        const float frac = src_f - static_cast<float>(base);
        float *w = ax.w.data() + static_cast<std::size_t>(x) * ax.taps;
        int *idx = ax.idx.data() + static_cast<std::size_t>(x) * ax.taps;
        float total = 0.0f;
        for (int i = 0; i < ax.taps; ++i) {
            const float u =
                ratio * (static_cast<float>(i) - static_cast<float>(rad) - (frac - 0.5f));
            w[i] = lanczos3(u * m_scale);
            total += w[i];
        }
        if (total != 0.0f)
            for (int i = 0; i < ax.taps; ++i)
                w[i] /= total;
        for (int i = 0; i < ax.taps; ++i)
            idx[i] = std::clamp(base + i - rad, 0, src_n - 1);
    }
}

// 水平一行（标量全通道）：逐输出像素、逐通道按抽头升序累加（std::fma，单次舍入）。
// ref 的全部行与 avx2 的**最后一行**都走这里 —— 两条实现逐位一致的口径载体。
void hrow_scalar(const float *srow, int sc, const Axis &ax, float *trow, int dc) {
    for (int x = 0; x < static_cast<int>(ax.w.size() / static_cast<std::size_t>(ax.taps)); ++x) {
        const float *w = ax.w.data() + static_cast<std::size_t>(x) * ax.taps;
        const int *idx = ax.idx.data() + static_cast<std::size_t>(x) * ax.taps;
        for (int c = 0; c < dc; ++c) {
            float acc = 0.0f;
            for (int i = 0; i < ax.taps; ++i)
                acc = std::fma(w[i], srow[static_cast<std::size_t>(idx[i]) * sc + c], acc);
            trow[static_cast<std::size_t>(x) * dc + c] = acc;
        }
    }
}

// 垂直一行：dst_row[k] = Σ_j wy_j · tmp[iy_j][k]（j 升序）；标量面（avx2 的尾块共用）。
void vrow_tail(const std::vector<float> &tmp, std::size_t row_len, const float *wy, const int *iy,
               int taps, std::size_t k0, float *drow) {
    for (std::size_t k = k0; k < row_len; ++k) {
        float acc = 0.0f;
        for (int j = 0; j < taps; ++j)
            acc = std::fma(wy[j], tmp[static_cast<std::size_t>(iy[j]) * row_len + k], acc);
        drow[k] = acc;
    }
}

} // namespace

void downscale_ref(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc) {
    assert(sw > 0 && sh > 0 && dw > 0 && dh > 0 && sc == dc && sc >= 1 && sc <= 4);
    Axis ax;
    Axis ay;
    build_axis(sw, dw, ax);
    build_axis(sh, dh, ay);
    const std::size_t row_len = static_cast<std::size_t>(dw) * static_cast<std::size_t>(dc);
    std::vector<float> tmp(static_cast<std::size_t>(sh) * row_len);

    // 水平：src(sw×sh, sc) → tmp(dw×sh, dc)
    for (int y = 0; y < sh; ++y) {
        hrow_scalar(src + static_cast<std::size_t>(y) * sw * sc, sc, ax,
                    tmp.data() + static_cast<std::size_t>(y) * row_len, dc);
    }
    // 垂直：tmp(dw×sh) → dst(dw×dh)
    for (int y = 0; y < dh; ++y) {
        const float *wy = ay.w.data() + static_cast<std::size_t>(y) * ay.taps;
        const int *iy = ay.idx.data() + static_cast<std::size_t>(y) * ay.taps;
        float *drow = dst + static_cast<std::size_t>(y) * row_len;
        vrow_tail(tmp, row_len, wy, iy, ay.taps, 0, drow);
    }
}

void downscale_avx2(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc) {
    assert(sw > 0 && sh > 0 && dw > 0 && dh > 0 && sc == dc && sc >= 1 && sc <= 4);
    Axis ax;
    Axis ay;
    build_axis(sw, dw, ax);
    build_axis(sh, dh, ay);
    const std::size_t row_len = static_cast<std::size_t>(dw) * static_cast<std::size_t>(dc);
    std::vector<float> tmp(static_cast<std::size_t>(sh) * row_len);

    // —— 水平（按通道打包：一个输出像素的 sc 个通道落在同一个 128 位寄存器）——
    // sc == 4 走整存；sc < 4 用掩码存（避免写出 4 个浮点覆盖同行下一个像素）。
    const __m128i mask =
        _mm_setr_epi32(sc > 0 ? -1 : 0, sc > 1 ? -1 : 0, sc > 2 ? -1 : 0, sc > 3 ? -1 : 0);
    const int simd_rows = sh > 0 ? sh - 1 : 0; // 最后一行留标量：4 浮点载入会越过源缓冲末尾
    for (int y = 0; y < simd_rows; ++y) {
        const float *srow = src + static_cast<std::size_t>(y) * sw * sc;
        float *trow = tmp.data() + static_cast<std::size_t>(y) * row_len;
        for (int x = 0; x < dw; ++x) {
            const float *w = ax.w.data() + static_cast<std::size_t>(x) * ax.taps;
            const int *idx = ax.idx.data() + static_cast<std::size_t>(x) * ax.taps;
            __m128 acc = _mm_setzero_ps();
            for (int i = 0; i < ax.taps; ++i) {
                acc = _mm_fmadd_ps(_mm_set1_ps(w[i]),
                                   _mm_loadu_ps(srow + static_cast<std::size_t>(idx[i]) * sc), acc);
            }
            float *q = trow + static_cast<std::size_t>(x) * dc;
            if (sc == 4)
                _mm_storeu_ps(q, acc);
            else
                _mm_maskstore_ps(q, mask, acc);
        }
    }
    if (simd_rows < sh) {
        hrow_scalar(src + static_cast<std::size_t>(sh - 1) * sw * sc, sc, ax,
                    tmp.data() + static_cast<std::size_t>(sh - 1) * row_len, dc);
    }

    // —— 垂直（按行连续 8 浮点打包；同一输出像素的各通道共享同一组 y 权重）——
    for (int y = 0; y < dh; ++y) {
        const float *wy = ay.w.data() + static_cast<std::size_t>(y) * ay.taps;
        const int *iy = ay.idx.data() + static_cast<std::size_t>(y) * ay.taps;
        float *drow = dst + static_cast<std::size_t>(y) * row_len;
        std::size_t k = 0;
        for (; k + 8 <= row_len; k += 8) {
            __m256 acc = _mm256_setzero_ps();
            for (int j = 0; j < ay.taps; ++j) {
                acc = _mm256_fmadd_ps(
                    _mm256_set1_ps(wy[j]),
                    _mm256_loadu_ps(tmp.data() + static_cast<std::size_t>(iy[j]) * row_len + k),
                    acc);
            }
            _mm256_storeu_ps(drow + k, acc);
        }
        if (k < row_len)
            vrow_tail(tmp, row_len, wy, iy, ay.taps, k, drow);
    }
}

void downscale(const float *src, int sw, int sh, int sc, float *dst, int dw, int dh, int dc) {
    if (cpu_has_avx2())
        downscale_avx2(src, sw, sh, sc, dst, dw, dh, dc);
    else
        downscale_ref(src, sw, sh, sc, dst, dw, dh, dc);
}

} // namespace pp::simd
