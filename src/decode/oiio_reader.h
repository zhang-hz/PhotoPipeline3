// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/types.h"
#include <OpenImageIO/imagebuf.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pp {

struct ProbeOutcome {
    ImageInfo info;
    std::optional<OIIO::ImageSpec> first_spec;
    std::string error; // 非空=失败
};

// 打开读 spec，不解码像素；多页/动图 → info.is_multipage=true
ProbeOutcome probe_file(const std::filesystem::path &p);

struct DecodeOutcome {
    OIIO::ImageBuf buf; // float32；失败时 empty()
    std::vector<Warning> warnings;
    std::string error;
};

// 读为 float32；通道保持 {1,2,3,4}；CMYK(4ch separated) 或其它非 {1,2,3,4} 通道数 → error
// （冻结的 WarningKind 无合适枚举可表达"通道截断"，故不静默截断）
// 多页/动图取首页 + Warning{MultipageTruncated}
DecodeOutcome decode_float(const std::filesystem::path &p, const ImageInfo &info);

// 方向标签：从 OIIO spec 属性 "Orientation" 读取（1–8）；无 → 1
int orientation_from_spec(const OIIO::ImageSpec &spec);

// 内嵌 ICC：从 spec 的 "ICCProfile" 属性取字节；无 → 空
std::string icc_from_spec(const OIIO::ImageSpec &spec);

} // namespace pp
