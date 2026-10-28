// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M8-C1 —— 兼容层：线性 sRGB → XYB 的标量实现。
//
// 公式/常量逐字对应 libjxl v0.12.0：
//   * `lib/jxl/enc_xyb.cc`：OpsinAbsorbance / LinearRGBToXYB / LinearSRGBToXYB /
//     ComputePremulAbsorb 的线性 sRGB 直通路径（官方在该路径用 Highway SIMD）；
//   * `lib/jxl/base/fast_math-inl.h`：CubeRootAndAdd（6 ulp 快速 cbrt 近似）。
//
// 与官方 SIMD 路径的数值关系：项目 ISA 基线 = x86-64-v3，运行时选中的 Highway target
// 即 AVX2；AVX2 上 Mul/MulAdd 为单次舍入 FMA。标量实现用 std::fma 复现同一舍入序，
// 因此与官方 AVX2 路径逐位一致（未用 Highway 实现，避免 ToXYB 变成第二个多目标 TU）。
//
// 支持面（最小兼容）：只支持契约输入 sRGB 线性；sRGB gamma 输入由
// ImageBundle::TransformTo 先行线性化；其他 ColorEncoding / black 通道返回错误，
// 绝不静默错算（不伪造分数）。

#include "lib/jxl/enc_xyb.h"

#include <jxl/cms_interface.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "lib/jxl/base/status.h"
#include "lib/jxl/cms/opsin_params.h"
#include "lib/jxl/image.h"

namespace jxl {
namespace {

// libjxl 的 CubeRootAndAdd：返回 cbrt(x) + add（max 6 ulp）。
// 位运算 + Newton-Raphson 三次 + 一次最终修正，逐式对应 fast_math-inl.h。
float ScalarCubeRootAndAdd(float x, float add) {
  constexpr uint32_t kExpBias = 0x54800000u;  // cast(1.) + cast(1.) / 3
  constexpr uint32_t kExpMul = 0x002AAAAAu;   // shifted 1/3

  uint32_t m1;
  std::memcpy(&m1, &x, sizeof(m1));
  // Multiply exponent by -1/3；0 的指数为 0，特判置 0（与 IfThenZeroElse 同义）。
  const uint32_t m2 =
      (m1 == 0u) ? 0u : (kExpBias - (((m1 >> 23) * kExpMul) & 0xFFFFFFFFu));
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
struct PremulAbsorb {
  float matrix[3][3];
  float add[3];
  float bias[3];
};

PremulAbsorb ComputePremulAbsorbCompat(float intensity_target) {
  PremulAbsorb out{};
  const float mul = intensity_target / 255.0f;
  for (size_t j = 0; j < 3; ++j) {
    for (size_t i = 0; i < 3; ++i) {
      out.matrix[j][i] = cms::kOpsinAbsorbanceMatrix[j][i] * mul;
    }
  }
  for (size_t i = 0; i < 3; ++i) {
    out.bias[i] = cms::kOpsinAbsorbanceBias[i];
    out.add[i] = -std::cbrt(cms::kOpsinAbsorbanceBias[i]);
  }
  return out;
}

// 官方 OpsinAbsorbance + ZeroIfNegative + CubeRootAndAdd + StoreXYB 的标量重排。
void LinearRGBRowToXYBCompat(float* JXL_RESTRICT row0,
                             float* JXL_RESTRICT row1,
                             float* JXL_RESTRICT row2,
                             const PremulAbsorb& pa, size_t xsize) {
  for (size_t x = 0; x < xsize; ++x) {
    const float r = row0[x];
    const float g = row1[x];
    const float b = row2[x];

    // OpsinAbsorbance：MulAdd(m0, r, MulAdd(m1, g, MulAdd(m2, b, bias)))
    float mixed0 =
        std::fma(pa.matrix[0][0], r,
                 std::fma(pa.matrix[0][1], g,
                          std::fma(pa.matrix[0][2], b, pa.bias[0])));
    float mixed1 =
        std::fma(pa.matrix[1][0], r,
                 std::fma(pa.matrix[1][1], g,
                          std::fma(pa.matrix[1][2], b, pa.bias[1])));
    float mixed2 =
        std::fma(pa.matrix[2][0], r,
                 std::fma(pa.matrix[2][1], g,
                          std::fma(pa.matrix[2][2], b, pa.bias[2])));

    // mixed* should be non-negative even for wide-gamut, so clamp to zero.
    if (mixed0 < 0.0f) mixed0 = 0.0f;
    if (mixed1 < 0.0f) mixed1 = 0.0f;
    if (mixed2 < 0.0f) mixed2 = 0.0f;

    const float t0 = ScalarCubeRootAndAdd(mixed0, pa.add[0]);
    const float t1 = ScalarCubeRootAndAdd(mixed1, pa.add[1]);
    const float t2 = ScalarCubeRootAndAdd(mixed2, pa.add[2]);

    // StoreXYB: X = 0.5 * (r - g); Y = 0.5 * (r + g); B = b。
    row0[x] = 0.5f * (t0 - t1);
    row1[x] = 0.5f * (t0 + t1);
    row2[x] = t2;
  }
}

}  // namespace

Status ToXYB(const ColorEncoding& c_current, float intensity_target,
             const ImageF* black, ThreadPool* /*pool*/,
             Image3F* JXL_RESTRICT image, const JxlCmsInterface& /*cms*/,
             Image3F* JXL_RESTRICT linear) {
  if (image == nullptr) return JXL_FAILURE("compat ToXYB: null image");
  if (black != nullptr) {
    return JXL_FAILURE("compat ToXYB: black (CMYK) channel not supported");
  }
  // 官方：线性 sRGB 直通（LinearSRGBToXYB），不复制 image。
  if (!ColorEncoding::LinearSRGB(c_current.IsGray())
           .SameColorEncoding(c_current)) {
    return JXL_FAILURE("compat ToXYB: input must be linear sRGB");
  }
  if (linear != nullptr) {
    JXL_RETURN_IF_ERROR(CopyImageTo(*image, linear));
  }

  const PremulAbsorb pa = ComputePremulAbsorbCompat(intensity_target);
  for (size_t y = 0; y < image->ysize(); ++y) {
    float* JXL_RESTRICT row0 = image->PlaneRow(0, y);
    float* JXL_RESTRICT row1 = image->PlaneRow(1, y);
    float* JXL_RESTRICT row2 = image->PlaneRow(2, y);
    LinearRGBRowToXYBCompat(row0, row1, row2, pa, image->xsize());
  }
  return true;
}

}  // namespace jxl
