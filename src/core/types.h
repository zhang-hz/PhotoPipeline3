// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core types (M0 frozen)
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `core/types.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/types.h` | `Warning` 追加枚举值 `SyntheticProgress`? 否——进度非警告。追加 `Warning::MetadataDropped` 语义不变；`Timing` 不变 |
// clang-format on
//   → 本文件 0.3.0 **零结构改动**（无 0.3.0 冻结形态需要落注）；维持 PP-FROZEN(M0)。
//   进度不进 Warning：由 §3.3 的 `ProgressInfo.synthetic` 承载（UI 斜纹，§7.3）。
//   T6/T7/T8 落地时不得向 WarningKind 追加进度类枚举值。
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pp {

enum class Stage { Probe, Decode, Orient, Color, Flatten, Encode, MetaWrite, Done };

// PP-THAWED(0.3.0-M4-D20) §3.6：本枚举**保持只读**（不加 SyntheticProgress；既有值含
// MetadataDropped 语义逐字不变）。进度语义见 scheduler.h 的 ProgressInfo（§3.3）。
enum class WarningKind {
    DepthDowngrade,     // bit-depth reduced for target codec
    LossyFromLossless,  // lossless source encoded lossy
    MultipageTruncated, // multipage/animated input: first page only
    AlphaFlattened,     // alpha composited onto background
    NoIccAssumeSrgb,    // no ICC found, assumed sRGB
    MetadataDropped,    // target container has no metadata (BMP)
    TimeFieldMissing,   // time fields absent, shift skipped
    GrayToRgbEncoded,   // grayscale encoded as RGB (webp/heif/avif)
};

struct Warning {
    WarningKind kind;
    std::string detail; // English, structured
};

struct Timing {
    double decode_ms = 0, orient_ms = 0, color_ms = 0, flatten_ms = 0, encode_ms = 0,
           metawrite_ms = 0, total_ms = 0;
};

struct ImageInfo { // probe result
    int width = 0, height = 0;
    int channels = 0; // {1,2,3,4}
    int src_bitdepth = 8;
    bool has_alpha = false;
    bool is_multipage = false;
    bool has_icc = false;
    std::string format; // OIIO format name
};

using ParamValue = std::variant<std::monostate, bool, int64_t, double, std::string>;
using ParamSet = std::map<std::string, ParamValue>;

} // namespace pp
