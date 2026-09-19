// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — encoder interface (M0 frozen)
#pragma once
#include <OpenImageIO/imagebuf.h>
#include <filesystem>
#include <functional>
#include "core/params.h"
#include "core/types.h"

namespace pp {

struct MetadataPayloads {
    std::string exif_blob;    // TIFF blob (ExifData::copy, littleEndian)
    std::string xmp_rdf;      // XMP RDF xml
    std::string icc_profile;  // target or as-is profile bytes
};

struct EncodeRequest {
    OIIO::ImageBuf& img;             // float32, channels {1,2,3,4}
    const ParamSet& params;
    int out_bitdepth = 8;
    const MetadataPayloads& meta;
    std::filesystem::path out_path;
    std::function<bool()> cancelled;
};

struct EncodeResult {
    uint64_t bytes = 0;
    std::vector<Warning> warnings;
    Timing t;
};

class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual const FormatDef& format() const = 0;
    virtual EncodeResult encode(const EncodeRequest&) = 0;
};

}  // namespace pp
