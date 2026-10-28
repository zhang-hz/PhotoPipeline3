// SPDX-License-Identifier: BSD-3-Clause
// M8-C1 兼容层：官方 lib/jxl/enc_xyb.h 的裁剪替身（仅 ToXYB；实现见 compat/enc_xyb_impl.cc）。
//
// 官方 ToXYB 有「线性 sRGB 直通 / sRGB gamma 快路径 / 任意 ICC 经 lcms2」三条路径。
// 本兼容层只承诺契约输入（sRGB 线性）对应的 LinearSRGBToXYB 路径，公式与常量逐字取自
// libjxl v0.12.0 lib/jxl/enc_xyb.cc + base/fast_math-inl.h（CubeRootAndAdd）。
#pragma once
#include "lib/jxl/compat_core.h"

namespace jxl {

Status ToXYB(const ColorEncoding& c_current, float intensity_target,
             const ImageF* black, ThreadPool* pool,
             Image3F* JXL_RESTRICT image, const JxlCmsInterface& cms,
             Image3F* JXL_RESTRICT linear);

}  // namespace jxl
