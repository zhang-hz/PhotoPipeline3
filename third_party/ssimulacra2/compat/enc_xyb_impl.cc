// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M8-C1 —— 兼容层：线性 sRGB → XYB（ToXYB 入口）。
//
// 行级公式（CubeRootAndAdd / OpsinAbsorbance / StoreXYB）已提升到同目录
// compat/xyb_row.h 作为**单源**（M10-W1-A2 起流式指标与官方路径共用同一份公式，
// 防止两套 XYB 导致 oracle 对拍漂移）。本文件只保留 ToXYB 入口的包围逻辑：
// 支持面（最小兼容）只支持契约输入 sRGB 线性；sRGB gamma 输入由
// ImageBundle::TransformTo 先行线性化；其他 ColorEncoding / black 通道返回错误，
// 绝不静默错算（不伪造分数）。

#include "lib/jxl/enc_xyb.h"

#include <jxl/cms_interface.h>

#include <cstddef>

#include "lib/jxl/base/status.h"
#include "lib/jxl/image.h"
#include "xyb_row.h"

namespace jxl {

Status ToXYB(const ColorEncoding &c_current, float intensity_target, const ImageF *black,
             ThreadPool * /*pool*/, Image3F *JXL_RESTRICT image, const JxlCmsInterface & /*cms*/,
             Image3F *JXL_RESTRICT linear) {
    if (image == nullptr)
        return JXL_FAILURE("compat ToXYB: null image");
    if (black != nullptr) {
        return JXL_FAILURE("compat ToXYB: black (CMYK) channel not supported");
    }
    // 官方：线性 sRGB 直通（LinearSRGBToXYB），不复制 image。
    if (!ColorEncoding::LinearSRGB(c_current.IsGray()).SameColorEncoding(c_current)) {
        return JXL_FAILURE("compat ToXYB: input must be linear sRGB");
    }
    if (linear != nullptr) {
        JXL_RETURN_IF_ERROR(CopyImageTo(*image, linear));
    }

    const jpegxl::tools::XybPremulAbsorb pa =
        jpegxl::tools::ComputeXybPremulAbsorb(intensity_target);
    for (size_t y = 0; y < image->ysize(); ++y) {
        float *JXL_RESTRICT row0 = image->PlaneRow(0, y);
        float *JXL_RESTRICT row1 = image->PlaneRow(1, y);
        float *JXL_RESTRICT row2 = image->PlaneRow(2, y);
        jpegxl::tools::LinearRgbRowToXyb(row0, row1, row2, pa, image->xsize());
    }
    return true;
}

} // namespace jxl
