// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — probe + thumbnail (M1b frozen; UI-facing, core layer, no Qt)
#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include "core/types.h"

namespace pp {

struct ThumbImage {
    int width = 0, height = 0;       // returned size (longest edge <= target)
    std::vector<uint8_t> rgba;       // width*height*4, 8-bit, sRGB-assumed, alpha on white
    bool from_embedded = false;      // true = embedded preview used
};

struct ThumbOutcome {
    pp::ImageInfo info;              // probe result (meaningful when probe_ok)
    bool probe_ok = false;
    std::string error;               // non-empty = probe failure (row error state)
    pp::ThumbImage thumb;            // empty = thumbnail unavailable (probe still ok)
};

// Thread-safe, stateless: probe + thumbnail in one call.
//  1) probe_file(src): failure -> {probe_ok=false, error}
//  2) embedded preview: Exiv2 PreviewManager, largest preview with long edge >= 64;
//     decode its bytes via OIIO (memory reader), resample to target
//  3) no/failed preview -> full float32 decode + resample to target
//  4) thumb failure with probe_ok -> thumb empty, error stays empty
//  5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white
ThumbOutcome make_thumbnail(const std::filesystem::path& src, int target_long_edge);

}  // namespace pp
