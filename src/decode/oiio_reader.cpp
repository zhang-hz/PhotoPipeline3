// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T3 — OIIO decode layer: probe (spec only) + float32 decode.
//
// Contract: docs/m1-tasks.md §3.5 (PP-FROZEN header) / §4.3 / §5.
// Every OIIO API shape used below was verified against the vcpkg_installed
// OpenImageIO 3.1.14 headers plus a live probe of tests/golden (see the
// api-deltas section of the M1-T3 report).

#include "decode/oiio_reader.h"

#include <OpenImageIO/imageio.h>

#include <cassert>
#include <cstddef>
#include <string>
#include <string_view>

namespace pp {
namespace {

constexpr int kMinChannels = 1;
constexpr int kMaxChannels = 4;

// ASCII-only case-insensitive compare (OIIO metadata keys/values here are ASCII).
bool ascii_iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        auto lower = [](unsigned char c) {
            return static_cast<unsigned char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// OIIO 3.1.14's TIFF reader converts a separated (CMYK) TIFF to 3-channel pixel
// data but keeps the source colour model in the spec:
//   tiff:ColorSpace                = "CMYK"  (string)
//   tiff:PhotometricInterpretation = 5       (int, PHOTOMETRIC_SEPARATED)
// The alternate spellings are checked defensively (docs/m1-tasks.md §4.3
// assumed 4 separated channels + a "tiff:photometric" string; see api-deltas).
bool spec_is_cmyk(const OIIO::ImageSpec& spec) {
    if (ascii_iequals(spec.get_string_attribute("tiff:ColorSpace"), "CMYK")) {
        return true;
    }
    if (spec.get_int_attribute("tiff:PhotometricInterpretation", 0) == 5) {
        return true;
    }
    if (ascii_iequals(spec.get_string_attribute("tiff:Photometric"), "separated")) {
        return true;
    }
    if (ascii_iequals(spec.get_string_attribute("tiff:photometric"), "separated")) {
        return true;
    }
    return false;
}

// Source bit depth implied by the OIIO pixel data type (uint8 -> 8, half -> 16,
// uint16 -> 16, float -> 32; instances are per-channel).
int bitdepth_from_format(const OIIO::TypeDesc& t) noexcept {
    const std::size_t bytes = t.basesize();
    return bytes > 0 ? static_cast<int>(bytes) * 8 : 8;
}

// OIIO error strings may carry embedded newlines and duplicate nesting
// ("Could not open file: Could not open file ..."); keep the first line.
std::string first_line(std::string msg) {
    const std::size_t nl = msg.find('\n');
    if (nl != std::string::npos) {
        msg.resize(nl);
    }
    return msg;
}

std::string oiio_error_or(std::string fallback) {
    std::string err = first_line(OIIO::geterror());
    return err.empty() ? std::move(fallback) : err;
}

// OIIO keeps a thread-local error string; drop any stale entry so a reported
// failure can only come from the operation that just failed.
void clear_stale_oiio_error() { (void)OIIO::geterror(); }

}  // namespace

ProbeOutcome probe_file(const std::filesystem::path& p) {
    ProbeOutcome out;
    clear_stale_oiio_error();
    // std::filesystem::path::string() is UTF-8 on the M1 target (Linux), which
    // is what ImageInput::open() expects (unicode fixture: 测试📸unicode.png).
    const std::string file = p.string();

    auto in = OIIO::ImageInput::open(file);
    if (!in) {
        out.error = oiio_error_or("cannot open input file: " + file);
        return out;
    }

    const OIIO::ImageSpec spec = in->spec();  // copy: valid after close()
    out.info.width = spec.width;
    out.info.height = spec.height;
    out.info.channels = spec.nchannels;
    out.info.src_bitdepth = bitdepth_from_format(spec.format);
    // ImageInfo::channels is the channel layout: 2 = gray+alpha, 4 = RGBA.
    // (The GIF reader of OIIO 3.1.14 reports alpha_channel == 4 for a 4-channel
    // frame, i.e. an out-of-range index, hence the layout check as well.)
    out.info.has_alpha = spec.alpha_channel >= 0 || spec.nchannels == 2 || spec.nchannels == 4;
    out.info.has_icc = !icc_from_spec(spec).empty();
    const char* format_name = in->format_name();
    out.info.format = format_name ? format_name : "";

    // Multipage/animated detection without decoding pixels: a second subimage
    // must exist (TIFF directory / GIF frame). Restore subimage 0 afterwards.
    out.info.is_multipage = in->seek_subimage(1, 0);
    if (out.info.is_multipage) {
        in->seek_subimage(0, 0);
    }

    out.first_spec = spec;
    in->close();

    if (spec_is_cmyk(spec)) {
        // Rejected input, but the spec itself is valid and useful for logging.
        out.error = "CMYK input is not supported";
        return out;
    }
    return out;
}

DecodeOutcome decode_float(const std::filesystem::path& p, const ImageInfo& info) {
    DecodeOutcome out;
    clear_stale_oiio_error();
    const std::string file = p.string();

    // Read the spec first so unsupported inputs are rejected before any pixel
    // allocation (init_spec() does not read or allocate pixels).
    if (!out.buf.init_spec(file, 0, 0)) {
        // Take the message from the buffer itself: ImageBuf warns on destruction
        // if a pending per-buffer error was never retrieved.
        out.error = first_line(out.buf.geterror());
        if (out.error.empty()) {
            out.error = oiio_error_or("cannot read image spec: " + file);
        }
        out.buf.clear();
        return out;
    }

    const OIIO::ImageSpec spec = out.buf.spec();
    if (spec_is_cmyk(spec)) {
        out.error = "CMYK input is not supported";
        out.buf.clear();
        return out;
    }
    if (spec.nchannels < kMinChannels || spec.nchannels > kMaxChannels) {
        out.error = "unsupported channel count: " + std::to_string(spec.nchannels) +
                    " (expected 1..4)";
        out.buf.clear();
        return out;
    }

    // float32, all channels, local pixels. NOTE: the OIIO 3.1.14 six-argument
    // overload with chend == 0 segfaults (see api-deltas), so the channel range
    // is passed explicitly with force=true.
    if (!out.buf.read(0, 0, 0, spec.nchannels, true, OIIO::TypeDesc::FLOAT)) {
        out.error = first_line(out.buf.geterror());
        if (out.error.empty()) {
            out.error = oiio_error_or("failed to decode: " + file);
        }
        out.buf.clear();
        return out;
    }
    assert(out.buf.spec().format == OIIO::TypeDesc::FLOAT);

    if (info.is_multipage) {
        out.warnings.push_back(
            Warning{WarningKind::MultipageTruncated,
                    "multipage or animated input: only the first page was decoded"});
    }
    return out;
}

int orientation_from_spec(const OIIO::ImageSpec& spec) {
    const int orientation = spec.get_int_attribute("Orientation", 1);
    return (orientation >= 1 && orientation <= 8) ? orientation : 1;
}

std::string icc_from_spec(const OIIO::ImageSpec& spec) {
    // The ICC profile is a binary attribute stored as uint8[N]; a typed lookup
    // with the scalar TypeDesc::UINT8 does not match it (OIIO 3.1.14), so the
    // attribute is fetched untyped and its byte payload is copied out.
    const OIIO::ParamValue* pv = spec.find_attribute("ICCProfile");
    if (!pv || pv->type().basetype != OIIO::TypeDesc::UINT8) {
        return {};
    }
    const int bytes = pv->datasize();
    if (bytes <= 0 || !pv->data()) {
        return {};
    }
    return std::string(static_cast<const char*>(pv->data()), static_cast<std::size_t>(bytes));
}

}  // namespace pp
