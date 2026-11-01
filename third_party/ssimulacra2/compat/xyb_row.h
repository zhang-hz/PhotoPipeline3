// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M10-W1-A2 —— 兼容层：线性 sRGB → XYB 的**行级**标量实现（单源）。
//
// 本头文件把原先只在 compat/enc_xyb_impl.cc 内部使用的行转换提升为兼容层单源：
//   * 官方路径（enc_xyb_impl.cc 的 ToXYB，供 vendored 官方 ComputeSSIMULACRA2 使用）；
//   * 流式路径（src/core/metric/metric.cpp，供 W1-A2 分块指标使用）。
// 两条路径必须逐字共用一个公式，防止 oracle 对拍时出现“两套 XYB”的漂移。
//
// 公式/常量逐字对应 libjxl v0.12.0：
//   * `lib/jxl/enc_xyb.cc`：OpsinAbsorbance / LinearRGBToXYB / LinearSRGBToXYB /
//     ComputePremulAbsorb 的线性 sRGB 直通路径（官方在该路径用 Highway SIMD）；
//   * `lib/jxl/base/fast_math-inl.h`：CubeRootAndAdd（6 ulp 快速 cbrt 近似）。
//
// 与官方 SIMD 路径的数值关系：项目 ISA 基线 = x86-64-v3，运行时选中的 Highway target
// 即 AVX2；AVX2 上 Mul/MulAdd 为单次舍入 FMA。标量实现用 std::fma 复现同一舍入序，
// 因此与官方 AVX2 路径逐位一致（未用 Highway 实现，避免 ToXYB 变成第二个多目标 TU）。

#ifndef TOOLS_COMPAT_XYB_ROW_H_
#define TOOLS_COMPAT_XYB_ROW_H_

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "lib/jxl/cms/opsin_params.h"

namespace jpegxl {
namespace tools {

// 兼容层的 opsin 预乘参数（官方 ComputePremulAbsorb 的标量版）。
struct XybPremulAbsorb {
    float matrix[3][3];
    float add[3];
    float bias[3];
};

// libjxl 的 CubeRootAndAdd：返回 cbrt(x) + add（max 6 ulp）。
// 位运算 + Newton-Raphson 三次 + 一次最终修正，逐式对应 fast_math-inl.h。
inline float ScalarCubeRootAndAdd(float x, float add) {
    constexpr uint32_t kExpBias = 0x54800000u; // cast(1.) + cast(1.) / 3
    constexpr uint32_t kExpMul = 0x002AAAAAu;  // shifted 1/3

    uint32_t m1;
    std::memcpy(&m1, &x, sizeof(m1));
    // Multiply exponent by -1/3；0 的指数为 0，特判置 0（与 IfThenZeroElse 同义）。
    const uint32_t m2 = (m1 == 0u) ? 0u : (kExpBias - (((m1 >> 23) * kExpMul) & 0xFFFFFFFFu));
    float r;
    std::memcpy(&r, &m2, sizeof(r));

    const float xa_3 = (1.0f / 3.0f) * x;
    for (int i = 0; i < 3; i++) {
        const float r2 = r * r;
        // NegMulAdd(xa_3, r2 * r2, Mul(4/3, r)) = -(xa_3 * (r2*r2)) + (4/3)*r
        r = std::fma(-xa_3, r2 * r2, (4.0f / 3.0f) * r);
    }
    // Final iteration
    float r2 = r * r;
    const float neg_mul_add = std::fma(-x, r2 * r2, r);
    r = std::fma(1.0f / 3.0f, neg_mul_add, r);
    r2 = r * r;
    return std::fma(r2, x, add);
}

// 官方 ComputePremulAbsorb 的标量版：premul_absorb[j][i] = matrix[j][i] * (it / 255)；
// add[i] = -cbrtf(bias[i])。
inline XybPremulAbsorb ComputeXybPremulAbsorb(float intensity_target) {
    XybPremulAbsorb out{};
    const float mul = intensity_target / 255.0f;
    for (size_t j = 0; j < 3; ++j) {
        for (size_t i = 0; i < 3; ++i) {
            out.matrix[j][i] = jxl::cms::kOpsinAbsorbanceMatrix[j][i] * mul;
        }
    }
    for (size_t i = 0; i < 3; ++i) {
        out.bias[i] = jxl::cms::kOpsinAbsorbanceBias[i];
        out.add[i] = -std::cbrt(jxl::cms::kOpsinAbsorbanceBias[i]);
    }
    return out;
}

// 官方 OpsinAbsorbance + ZeroIfNegative + CubeRootAndAdd + StoreXYB 的标量重排。
// 输入输出都是 3 条**平面**行（同一像素位置跨行读取）；原地转换合法。
inline void LinearRgbRowToXyb(float *JXL_RESTRICT row0, float *JXL_RESTRICT row1,
                              float *JXL_RESTRICT row2, const XybPremulAbsorb &pa, size_t xsize) {
    for (size_t x = 0; x < xsize; ++x) {
        const float r = row0[x];
        const float g = row1[x];
        const float b = row2[x];

        // OpsinAbsorbance：MulAdd(m0, r, MulAdd(m1, g, MulAdd(m2, b, bias)))
        float mixed0 =
            std::fma(pa.matrix[0][0], r,
                     std::fma(pa.matrix[0][1], g, std::fma(pa.matrix[0][2], b, pa.bias[0])));
        float mixed1 =
            std::fma(pa.matrix[1][0], r,
                     std::fma(pa.matrix[1][1], g, std::fma(pa.matrix[1][2], b, pa.bias[1])));
        float mixed2 =
            std::fma(pa.matrix[2][0], r,
                     std::fma(pa.matrix[2][1], g, std::fma(pa.matrix[2][2], b, pa.bias[2])));

        // mixed* should be non-negative even for wide-gamut, so clamp to zero.
        if (mixed0 < 0.0f)
            mixed0 = 0.0f;
        if (mixed1 < 0.0f)
            mixed1 = 0.0f;
        if (mixed2 < 0.0f)
            mixed2 = 0.0f;

        const float t0 = ScalarCubeRootAndAdd(mixed0, pa.add[0]);
        const float t1 = ScalarCubeRootAndAdd(mixed1, pa.add[1]);
        const float t2 = ScalarCubeRootAndAdd(mixed2, pa.add[2]);

        // StoreXYB: X = 0.5 * (r - g); Y = 0.5 * (r + g); B = b。
        row0[x] = 0.5f * (t0 - t1);
        row1[x] = 0.5f * (t0 + t1);
        row2[x] = t2;
    }
}

} // namespace tools
} // namespace jpegxl

#endif // TOOLS_COMPAT_XYB_ROW_H_
