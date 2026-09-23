// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T7: OpenImageIO encoders (PNG / TIFF / BMP).
//
// Mapping (docs/m1-tasks.md §3.8 + §7, verified against OIIO 3.1.14):
//   PNG  : compressionLevel -> ImageSpec attribute "compressionLevel" (clamped 0..9)
//   TIFF : compression -> ImageSpec attribute "compression" (name table:
//          none/lzw/zip/ccittrle/packbits; an unknown name is an ERROR here, OIIO would
//          silently fall back to deflate), deflate_level -> "tiff:zipquality",
//          predictor -> "tiff:predictor", tiling -> spec.tile_width/height
//          (both > 0 and multiples of 16, otherwise ERROR)
//   BMP  : no parameters
//   ICC  : ImageSpec attribute "ICCProfile" (uint8 array); EXIF/XMP are NOT written here
//          (E7: those go through the Exiv2 post-write path of §3.7).
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "core/logger.h"
#include "core/params.h"

namespace pp {
namespace {

constexpr std::string_view kStage = "encode";
constexpr std::string_view kFile = "enc_oiio.cpp";

double ms_since(const std::chrono::steady_clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Log-only rendering of a parameter value (run-log fields for E9 notes).
std::string param_value_text(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.10g", *d);
        return buf;
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return *s;
    return "<empty>";
}

// OIIO 3.1.14 TIFF writer name table (tiffoutput.cpp:301-338); "deflate" is only an alias
// accepted by ImageSpec::decode_compression_metadata, unknown names silently fall back to
// deflate -> we validate up front (task book: no silent fallback).
bool tiff_compression_known(std::string_view name) {
    static constexpr std::string_view kNames[] = {"none", "lzw", "zip", "ccittrle", "packbits"};
    return std::any_of(std::begin(kNames), std::end(kNames),
                       [name](std::string_view n) { return n == name; });
}

int quantize(float v, int maxv) {
    const float x = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int>(std::lround(x * static_cast<float>(maxv)));
}

// Parameter keys declared by the static format table for (format, backend) -> E9 detection.
std::vector<std::string> known_param_keys(std::string_view format_id, std::string_view backend_id) {
    std::vector<std::string> keys;
    const FormatDef *f = find_format(format_id);
    if (!f)
        return keys;
    const BackendDef *b = find_backend(*f, backend_id);
    if (!b)
        return keys;
    const TechDef *t = find_tech(*b, "");
    if (!t)
        return keys;
    for (const ParamDef &p : t->params)
        keys.push_back(p.key);
    return keys;
}

class OiioEncoder final : public IEncoder {
public:
    OiioEncoder(std::string format_id, std::string backend_id)
        : format_id_(std::move(format_id)), backend_id_(std::move(backend_id)) {}

    const FormatDef &format() const override {
        const FormatDef *f = find_format(format_id_);
        assert(f != nullptr);
        static const FormatDef kFallback{};
        return f ? *f : kFallback;
    }

    EncodeResult encode(const EncodeRequest &req) override;

private:
    // E1: immutable -> safe to share one instance across worker threads.
    const std::string format_id_;
    const std::string backend_id_;
};

EncodeResult OiioEncoder::encode(const EncodeRequest &req) {
    EncodeResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&]() {
        res.t.encode_ms = ms_since(t0);
        res.t.total_ms = res.t.encode_ms;
        return res;
    };
    auto fail = [&](const std::string &msg) {
        log_error(kStage, kFile, "encode failed",
                  {{"format", format_id_}, {"error", msg}, {"path", req.out_path.string()}});
        res.bytes = 0;
        res.error = msg;
        return finish();
    };
    // E9 / parameter problems are configuration defects: log them (structured), never turn
    // them into EncodeResult.warnings — those are reserved for per-image quality or semantic
    // deviations (main-dialogue ruling, §3.8 T6 落地口径 ⑥).
    auto note_param = [&](const std::string &msg, const std::string &key,
                          const std::string &value) {
        log_warn(kStage, kFile, msg, {{"format", format_id_}, {"param", key}, {"value", value}});
    };

    try {
        if (!req.img.initialized() || req.img.spec().width <= 0 || req.img.spec().height <= 0)
            return fail("empty input image");
        const OIIO::ImageSpec &ispec = req.img.spec();
        const int w = ispec.width, h = ispec.height, nch = ispec.nchannels;
        assert(w > 0 && h > 0);
        assert(nch >= 1 && nch <= 4);
        if (nch < 1 || nch > 4)
            return fail("unsupported channel count " + std::to_string(nch) + " (expected 1..4)");

        // ---- output data type (E3: unsupported bit depth is an error, never a silent downgrade)
        OIIO::TypeDesc out_type = OIIO::TypeDesc::UINT8;
        if (format_id_ == "bmp") {
            if (req.out_bitdepth != 24 && req.out_bitdepth != 8)
                return fail("bmp: unsupported bitdepth " + std::to_string(req.out_bitdepth) +
                            " (expected 24)");
            out_type = OIIO::TypeDesc::UINT8;
        } else {
            if (req.out_bitdepth == 8)
                out_type = OIIO::TypeDesc::UINT8;
            else if (req.out_bitdepth == 16)
                out_type = OIIO::TypeDesc::UINT16;
            else
                return fail(format_id_ + ": unsupported bitdepth " +
                            std::to_string(req.out_bitdepth) + " (expected 8 or 16)");
        }
        const int bps = static_cast<int>(out_type.size());
        const int maxv = (out_type == OIIO::TypeDesc::UINT8) ? 255 : 65535;

        OIIO::ImageSpec spec(w, h, nch, out_type);
        switch (nch) {
        case 1:
            spec.channelnames = {"Y"};
            break;
        case 2:
            spec.channelnames = {"Y", "A"};
            spec.alpha_channel = 1;
            break;
        case 3:
            spec.channelnames = {"R", "G", "B"};
            break;
        default:
            spec.channelnames = {"R", "G", "B", "A"};
            spec.alpha_channel = 3;
            break;
        }

        // ---- format parameters ----
        if (format_id_ == "png") {
            int64_t level = param_int(req.params, "compressionLevel", 6);
            const int clamped = static_cast<int>(std::clamp<int64_t>(level, 0, 9));
            if (clamped != level)
                note_param("compressionLevel clamped", "compressionLevel",
                           std::to_string(level) + " -> " + std::to_string(clamped));
            spec.attribute("compressionLevel", clamped);
        } else if (format_id_ == "tiff") {
            const std::string compression = param_str(req.params, "compression", "lzw");
            if (!tiff_compression_known(compression))
                return fail("tiff: invalid compression '" + compression +
                            "' (allowed: none, lzw, zip, ccittrle, packbits)");
            spec.attribute("compression", compression);
            if (compression == "zip") {
                const int64_t level = param_int(req.params, "deflate_level", 6);
                spec.attribute("tiff:zipquality",
                               static_cast<int>(std::clamp<int64_t>(level, 1, 9)));
            }
            const int64_t predictor = param_int(req.params, "predictor", 2);
            if (compression == "lzw" || compression == "zip")
                spec.attribute("tiff:predictor", static_cast<int>(predictor));
            const int64_t tw = param_int(req.params, "tiff_tile_width", 0);
            const int64_t th = param_int(req.params, "tiff_tile_height", 0);
            if ((tw > 0) != (th > 0))
                return fail("tiff: tile width and height must both be > 0 (tiled) or both be 0 "
                            "(strips); got width=" +
                            std::to_string(tw) + " height=" + std::to_string(th));
            if (tw > 0) {
                if (tw % 16 != 0)
                    return fail("tiff: tile width " + std::to_string(tw) +
                                " must be a positive multiple of 16");
                if (th % 16 != 0)
                    return fail("tiff: tile height " + std::to_string(th) +
                                " must be a positive multiple of 16");
                spec.tile_width = static_cast<int>(tw);
                spec.tile_height = static_cast<int>(th);
            }
        }
        // png/tiff/bmp: no other parameters (bmp has none at all)

        // ---- ICC (E5); EXIF/XMP deliberately not written here (E7) ----
        if (!req.meta.icc_profile.empty()) {
            const size_t n = req.meta.icc_profile.size();
            spec.attribute(
                "ICCProfile", OIIO::TypeDesc(OIIO::TypeDesc::UINT8, static_cast<int>(n)),
                OIIO::cspan<std::byte>(
                    reinterpret_cast<const std::byte *>(req.meta.icc_profile.data()), n));
        }

        // ---- E9: unknown parameters are ignored with a warning ----
        const std::vector<std::string> known = known_param_keys(format_id_, backend_id_);
        for (const auto &[key, value] : req.params) {
            if (key.rfind("__", 0) == 0)
                continue; // reserved keys (§3.4)
            if (std::find(known.begin(), known.end(), key) == known.end())
                note_param("unrecognised parameter ignored", key, param_value_text(value));
        }

        // ---- pixels: float32 -> target integer type, one conversion only (R11) ----
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * nch * bps);
        for (OIIO::ImageBuf::ConstIterator<float> it(req.img); !it.done(); ++it) {
            const int x = it.x() - ispec.x;
            const int y = it.y() - ispec.y;
            if (x < 0 || y < 0 || x >= w || y >= h)
                continue;
            uint8_t *dst = pixels.data() + (static_cast<size_t>(y) * w + x) * nch * bps;
            for (int c = 0; c < nch; c++) {
                const int q = quantize(it[c], maxv);
                if (bps == 1)
                    dst[c] = static_cast<uint8_t>(q);
                else
                    reinterpret_cast<uint16_t *>(dst)[c] = static_cast<uint16_t>(q);
            }
        }

        // ---- write through OIIO ----
        std::unique_ptr<OIIO::ImageOutput> out = OIIO::ImageOutput::create(format_id_);
        if (!out)
            return fail("OpenImageIO has no output plugin for '" + format_id_ + "'");
        if (!out->open(req.out_path.string(), spec))
            return fail("OIIO open failed for " + req.out_path.string() + ": " + out->geterror());
        const OIIO::stride_t xstride = static_cast<OIIO::stride_t>(nch) * bps;
        const OIIO::stride_t ystride = xstride * w;
        if (!out->write_image(out_type, pixels.data(), xstride, ystride, OIIO::AutoStride))
            return fail("OIIO write_image failed for " + req.out_path.string() + ": " +
                        out->geterror());
        if (!out->close())
            return fail("OIIO close failed for " + req.out_path.string() + ": " + out->geterror());

        std::error_code ec;
        const uint64_t bytes = static_cast<uint64_t>(std::filesystem::file_size(req.out_path, ec));
        if (ec || bytes == 0)
            return fail("output file missing or empty: " + req.out_path.string());
        res.bytes = bytes;
        log_debug(kStage, kFile, "encoded",
                  {{"format", format_id_},
                   {"size", std::to_string(w) + "x" + std::to_string(h)},
                   {"channels", std::to_string(nch)},
                   {"bitdepth", std::to_string(bps * 8)},
                   {"bytes", std::to_string(res.bytes)}});
        return finish();
    } catch (const std::exception &ex) {
        return fail(std::string("exception: ") + ex.what());
    } catch (...) {
        return fail("unknown exception");
    }
}

std::unique_ptr<IEncoder> make_png() { return std::make_unique<OiioEncoder>("png", "oiio"); }
std::unique_ptr<IEncoder> make_tiff() { return std::make_unique<OiioEncoder>("tiff", "oiio"); }
std::unique_ptr<IEncoder> make_bmp() { return std::make_unique<OiioEncoder>("bmp", "oiio"); }

// Static self-registration (§3.17); factories live in this anonymous namespace, so the
// macro must be expanded inside namespace pp for the pasted name to resolve.
PP_REGISTER_ENCODER("png", "oiio", make_png);
PP_REGISTER_ENCODER("tiff", "oiio", make_tiff);
PP_REGISTER_ENCODER("bmp", "oiio", make_bmp);

} // namespace

// No link anchor here (M2-T13): self-registration is guaranteed by the link form, not by
// a referenced symbol — every consumer links pp_core whole-archive via the
// pp_core_registered interface target (CMakeLists §2b), so this TU's PP_REGISTER_ENCODER
// initialiser always runs.
} // namespace pp
