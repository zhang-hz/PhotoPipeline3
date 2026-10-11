// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "codecs/encoder.h"
#include "core/params.h"
#include <memory>
#include <string>
#include <vector>

namespace pp {

// format_id ∈ {jpeg,jxl,png,tiff,webp,bmp,heif,avif}；backend_id 可空（用默认后端）
// 不支持 → nullptr
std::unique_ptr<IEncoder> make_encoder(std::string_view format_id, std::string_view backend_id);

// libheif 运行时内省（R4）：返回该格式可用后端及其参数（转成 ParamDef）
// heif → x265 后端；avif → svt-av1/libaom 后端（实际可用者，运行时枚举）
// 非 libheif 格式 → 空
std::vector<BackendDef> introspect_backends(std::string_view format_id);

// 10bit 能力探测（T7 交付）：返回 "8" / "8,10" / "8,10,12"（按编码器实际支持）
std::string probe_bitdepth_support(std::string_view format_id, std::string_view backend_id);

// R-299（M7-WP2 加性）：注册表诊断 —— 返回 (format, backend) 经空串 tie-break 解析后
// 实际命中的 backend_id（未命中 → 空串）。空串 tie-break 规则见 encoders.cpp 文件头。
std::string resolved_backend(std::string_view format_id, std::string_view backend_id);

} // namespace pp
