// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core types (M0 frozen)
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace pp {

enum class Stage { Probe, Decode, Orient, Color, Flatten, Encode, MetaWrite, Done };

enum class WarningKind {
    DepthDowngrade,      // bit-depth reduced for target codec
    LossyFromLossless,   // lossless source encoded lossy
    MultipageTruncated,  // multipage/animated input: first page only
    AlphaFlattened,      // alpha composited onto background
    NoIccAssumeSrgb,     // no ICC found, assumed sRGB
    MetadataDropped,     // target container has no metadata (BMP)
    TimeFieldMissing,    // time fields absent, shift skipped
    GrayToRgbEncoded,    // grayscale encoded as RGB (webp/heif/avif)
};

struct Warning {
    WarningKind kind;
    std::string detail;  // English, structured
};

struct Timing {
    double decode_ms = 0, orient_ms = 0, color_ms = 0, flatten_ms = 0,
           encode_ms = 0, metawrite_ms = 0, total_ms = 0;
};

struct ImageInfo {  // probe result
    int width = 0, height = 0;
    int channels = 0;        // {1,2,3,4}
    int src_bitdepth = 8;
    bool has_alpha = false;
    bool is_multipage = false;
    bool has_icc = false;
    std::string format;      // OIIO format name
};

using ParamValue = std::variant<std::monostate, bool, int64_t, double, std::string>;
using ParamSet  = std::map<std::string, ParamValue>;

}  // namespace pp
