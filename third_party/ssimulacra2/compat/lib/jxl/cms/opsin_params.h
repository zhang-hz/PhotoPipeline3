// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// M8-C1 兼容层：官方 libjxl v0.12.0 `lib/jxl/cms/opsin_params.h` 的**逐字常量子集**
// （只保留 SSIMULACRA 2 的 ToXYB 路径实际使用的 opsin 吸光矩阵与偏置；数值逐字未改）。
// 其余 ScaleXYB/ColorCube 常量属于编码器色域脚手架，本兼容层用不到，未收录。
// 来源与许可见 third_party/ssimulacra2/README.md。

#ifndef LIB_JXL_CMS_OPSIN_PARAMS_H_
#define LIB_JXL_CMS_OPSIN_PARAMS_H_

#include <array>
#include <cstddef>

#include "lib/jxl/base/matrix_ops.h"

// Constants that define the XYB color space.

namespace jxl {
namespace cms {

// Parameters for opsin absorbance.
constexpr float kM02 = 0.078f;
constexpr float kM00 = 0.30f;
constexpr float kM01 = 1.0f - kM02 - kM00;

constexpr float kM12 = 0.078f;
constexpr float kM10 = 0.23f;
constexpr float kM11 = 1.0f - kM12 - kM10;

constexpr float kM20 = 0.24342268924547819f;
constexpr float kM21 = 0.20476744424496821f;
constexpr float kM22 = 1.0f - kM20 - kM21;

constexpr float kOpsinAbsorbanceBias0 = 0.0037930732552754493f;
constexpr float kOpsinAbsorbanceBias1 = kOpsinAbsorbanceBias0;
constexpr float kOpsinAbsorbanceBias2 = kOpsinAbsorbanceBias0;

// Opsin absorbance matrix is now frozen.
constexpr Matrix3x3 kOpsinAbsorbanceMatrix{
    {{kM00, kM01, kM02}, {kM10, kM11, kM12}, {kM20, kM21, kM22}}};

constexpr Vector3 kOpsinAbsorbanceBias = {
    kOpsinAbsorbanceBias0,
    kOpsinAbsorbanceBias1,
    kOpsinAbsorbanceBias2,
};

}  // namespace cms
}  // namespace jxl

#endif  // LIB_JXL_CMS_OPSIN_PARAMS_H_
