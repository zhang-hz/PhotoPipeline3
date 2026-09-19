// PP-FROZEN(structure): 只允许替换 /* PP-PLACEHOLDER */ 注释处的内容
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/params.h"

namespace pp {

const std::vector<FormatDef>& static_formats() {
    static const std::vector<FormatDef> fmts = {
        FormatDef{
            .id = "jpeg", .label = "JPEG", .ext = "jpg",
            .backends = { BackendDef{
                .id = "jpegli", .label = "jpeg-li", .runtime_introspected = false,
                .techs = { TechDef{
                    .id = "dct", .label = "DCT", .lossless_capable = false,
                    .params = {
                        /* PP-PLACEHOLDER(jpeg): SUB-F fills from headers */
                    } } } } },
            .bitdepths = {8},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "jxl", .label = "JPEG XL", .ext = "jxl",
            .backends = { BackendDef{
                .id = "libjxl", .label = "libjxl", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "vardct", .label = "VarDCT", .lossless_capable = false,
                             .params = { /* PP-PLACEHOLDER(jxl-vardct) */ } },
                    TechDef{ .id = "modular", .label = "Modular", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(jxl-modular) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "jxl-box" },
        FormatDef{
            .id = "png", .label = "PNG", .ext = "png",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "deflate", .label = "Deflate", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(png) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "tiff", .label = "TIFF", .ext = "tif",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "codec", .label = "Compression", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(tiff) */ } } } } },
            .bitdepths = {8, 16},
            .supports_alpha = true, .supports_gray = true,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "webp", .label = "WebP", .ext = "webp",
            .backends = { BackendDef{
                .id = "libwebp", .label = "libwebp", .runtime_introspected = false,
                .techs = {
                    TechDef{ .id = "lossy", .label = "Lossy", .lossless_capable = false,
                             .params = { /* PP-PLACEHOLDER(webp-lossy) */ } },
                    TechDef{ .id = "lossless", .label = "Lossless", .lossless_capable = true,
                             .params = { /* PP-PLACEHOLDER(webp-lossless) */ } } } } },
            .bitdepths = {8},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "exiv2" },
        FormatDef{
            .id = "bmp", .label = "BMP", .ext = "bmp",
            .backends = { BackendDef{
                .id = "oiio", .label = "OIIO", .runtime_introspected = false,
                .techs = { TechDef{ .id = "raw", .label = "Raw", .lossless_capable = true,
                             .params = {} } } } },
            .bitdepths = {24},
            .supports_alpha = false, .supports_gray = true,
            .meta_path = "none" },
        FormatDef{
            .id = "heif", .label = "HEIF", .ext = "heic",
            .backends = { BackendDef{
                .id = "x265", .label = "x265", .runtime_introspected = true,
                .techs = {} } },
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
        FormatDef{
            .id = "avif", .label = "AVIF", .ext = "avif",
            .backends = {
                BackendDef{ .id = "svt-av1", .label = "SVT-AV1", .runtime_introspected = true,
                            .techs = {} },
                BackendDef{ .id = "libaom", .label = "libaom", .runtime_introspected = true,
                            .techs = {} } },
            .bitdepths = {8, 10},
            .supports_alpha = true, .supports_gray = false,
            .meta_path = "libheif" },
    };
    return fmts;
}

}  // namespace pp
