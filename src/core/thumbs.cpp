// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — probe + thumbnail (contract: docs/m1b-tasks.md §2.1 PP-FROZEN).
//
// Pipeline (docs/m1b-tasks.md §2.1):
//   1) probe_file(src): failure -> {probe_ok=false, error};
//   2) embedded preview (Exiv2 PreviewManager, largest preview with long edge >= 64)
//      decoded through an OIIO in-memory reader and resampled to the target;
//   3) otherwise a full float32 decode (pp::decode_float) and resample to the target;
//   4) thumbnail failure after a successful probe -> empty thumb, empty error;
//   5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white.
//
// API facts checked against the installed headers (vcpkg_installed/x64-linux/include) and
// the Exiv2 0.28.8 sources (src/preview.cpp, official repository):
//   * Exiv2::PreviewManager::getPreviewProperties() is sorted ascending by pixel count and
//     has already decoded/validated the preview dimensions (Loader::readDimensions()).
//   * OIIO: Filesystem::IOMemReader + ImageInput::open(name, config, ioproxy) for memory
//     decoding, ImageBufAlgo::resample() for the box-ish resample (bilinear, fast).

#include "core/thumbs.h"

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <exiv2/exiv2.hpp>
#include <exiv2/preview.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "decode/oiio_reader.h"

namespace pp {
namespace {

// §2.1 step 2: only embedded previews whose long edge is >= 64 px are considered.
constexpr std::size_t kMinPreviewLongEdge = 64;

struct TargetSize {
    int w = 0;
    int h = 0;
};

// §2.1 step 5: scale = min(target/w, target/h, 1.0) — never upscale.
TargetSize fit_target(int w, int h, int target) {
    assert(w > 0 && h > 0 && target > 0);
    const double scale =
        std::min({1.0, static_cast<double>(target) / w, static_cast<double>(target) / h});
    if (scale >= 1.0)
        return {w, h};
    TargetSize ts;
    ts.w = std::clamp(static_cast<int>(std::lround(w * scale)), 1, target);
    ts.h = std::clamp(static_cast<int>(std::lround(h * scale)), 1, target);
    return ts;
}

std::uint8_t to_u8(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// Decode 1..4 channel float pixels into 8-bit RGBA on a white background, downsampled so
// that the longest edge is <= target (steps 4/5). Returns false if no thumbnail can be made.
bool to_rgba8(const OIIO::ImageBuf &src, int target, ThumbImage &out) {
    if (!src.initialized())
        return false;
    const OIIO::ImageSpec &spec = src.spec();
    const int sw = spec.width;
    const int sh = spec.height;
    const int nch = spec.nchannels;
    if (sw <= 0 || sh <= 0)
        return false;
    assert(nch >= 1 && nch <= 4); // pp::decode_float()/the preview reader enforce 1..4
    if (nch < 1 || nch > 4)
        return false;

    const TargetSize ts = fit_target(sw, sh, target);
    const OIIO::ROI roi(0, ts.w, 0, ts.h); // destination ROI (== whole image)

    OIIO::ImageBuf scaled;
    const OIIO::ImageBuf *work = &src;
    if (ts.w != sw || ts.h != sh) {
        OIIO::ImageBuf dst(OIIO::ImageSpec(ts.w, ts.h, nch, OIIO::TypeFloat));
        if (!OIIO::ImageBufAlgo::resample(dst, src, /*interpolate=*/true, roi))
            return false;
        scaled = std::move(dst);
        work = &scaled;
    }

    const std::size_t pixels = static_cast<std::size_t>(ts.w) * static_cast<std::size_t>(ts.h);
    std::vector<float> px(pixels * static_cast<std::size_t>(nch));
    if (!work->get_pixels(roi, OIIO::TypeFloat, px.data()))
        return false;

    out.width = ts.w;
    out.height = ts.h;
    out.rgba.assign(pixels * 4, 255); // alpha is composited away -> always 255
    for (std::size_t i = 0; i < pixels; ++i) {
        const float *p = px.data() + i * static_cast<std::size_t>(nch);
        float r = p[0];
        float g = p[0];
        float b = p[0];
        float a = 1.0f;
        if (nch >= 3) {
            g = p[1];
            b = p[2];
            if (nch == 4)
                a = p[3];
        } else if (nch == 2) {
            a = p[1]; // gray + alpha
        }
        // alpha -> white composite (§2.1 step 5)
        a = std::clamp(a, 0.0f, 1.0f);
        if (a < 1.0f) {
            const float bg = 1.0f - a;
            r = r * a + bg;
            g = g * a + bg;
            b = b * a + bg;
        }
        std::uint8_t *q = out.rgba.data() + i * 4;
        q[0] = to_u8(r);
        q[1] = to_u8(g);
        q[2] = to_u8(b);
    }
    return true;
}

// Decode an in-memory embedded preview to float32 and turn it into a thumbnail. `data` must
// stay alive for the call (the ImageInput borrows it through the IOProxy).
bool preview_bytes_to_thumb(const void *data, std::size_t size, const std::string &name_hint,
                            int target, ThumbImage &out) {
    if (data == nullptr || size == 0)
        return false;

    OIIO::Filesystem::IOMemReader reader(data, size);
    auto in = OIIO::ImageInput::open(name_hint, nullptr, &reader);
    if (!in)
        return false;

    const OIIO::ImageSpec spec = in->spec(); // copy: still valid after close()
    if (spec.width <= 0 || spec.height <= 0 || spec.nchannels < 1 || spec.nchannels > 4) {
        in->close();
        return false;
    }
    std::vector<float> px(static_cast<std::size_t>(spec.width) *
                          static_cast<std::size_t>(spec.height) *
                          static_cast<std::size_t>(spec.nchannels));
    const bool ok = in->read_image(0, 0, 0, spec.nchannels, OIIO::TypeFloat, px.data());
    in->close();
    if (!ok)
        return false;

    // The spec must describe the memory actually handed to the ImageBuf: float32, own copy.
    OIIO::ImageSpec fspec = spec;
    fspec.set_format(OIIO::TypeFloat);
    OIIO::ImageBuf buf(fspec);
    if (!buf.set_pixels(OIIO::ROI(0, spec.width, 0, spec.height), OIIO::TypeFloat, px.data())) {
        return false;
    }
    return to_rgba8(buf, target, out);
}

// §2.1 step 2: largest embedded preview with a long edge >= 64 px. Exiv2 fills width/height
// by decoding the preview itself, so 0 (unknown) means "cannot check the rule" -> skip; the
// full decode below still produces a valid thumbnail in that case.
bool find_preview(const Exiv2::Image &image, Exiv2::PreviewProperties &out) {
    const Exiv2::PreviewManager manager(image);
    const Exiv2::PreviewPropertiesList props = manager.getPreviewProperties(); // ascending
    for (auto it = props.rbegin(); it != props.rend(); ++it) {
        if (it->width_ == 0 || it->height_ == 0)
            continue;
        if (std::max(it->width_, it->height_) < kMinPreviewLongEdge)
            continue;
        out = *it;
        return true;
    }
    return false;
}

// Returns true only when a usable thumbnail was produced from an embedded preview; every
// failure (no Exiv2 support for the container, no preview, undecodable preview) falls
// through silently to the full decode (step 3).
bool try_embedded_preview(const std::filesystem::path &src, int target, ThumbImage &out) {
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(src.string());
        if (!image)
            return false;
        image->readMetadata();

        Exiv2::PreviewProperties props;
        if (!find_preview(*image, props))
            return false;

        const Exiv2::PreviewManager manager(*image);
        const Exiv2::PreviewImage preview = manager.getPreviewImage(props);
        if (preview.pData() == nullptr || preview.size() == 0)
            return false;

        std::string hint = "embedded-preview";
        hint += preview.extension().empty() ? std::string(".jpg") : preview.extension();
        return preview_bytes_to_thumb(preview.pData(), preview.size(), hint, target, out);
    } catch (const Exiv2::Error &) {
        return false;
    } catch (const std::exception &) {
        return false;
    } catch (...) {
        return false;
    }
}

} // namespace

ThumbOutcome make_thumbnail(const std::filesystem::path &src, int target_long_edge) {
    assert(target_long_edge > 0); // internal invariant (UI default: 96)
    if (target_long_edge <= 0)
        target_long_edge = 1; // defensive; keeps fit_target's clamp sane

    ThumbOutcome out;

    // 1) probe
    const ProbeOutcome probe = probe_file(src);
    out.info = probe.info;
    if (!probe.error.empty()) {
        out.probe_ok = false;
        out.error = probe.error;
        return out;
    }
    out.probe_ok = true;

    // 2) embedded preview first
    ThumbImage thumb;
    if (try_embedded_preview(src, target_long_edge, thumb)) {
        thumb.from_embedded = true;
        out.thumb = std::move(thumb);
        return out;
    }

    // 3) full float32 decode + resample
    // NOTE(perf): a full float32 decode of a large input (tens of MP) costs hundreds of MB per
    //           in-flight thumbnail but stays inside the thumbnail memory budget; a
    //           reduced-resolution decode (ImageCache/mip level) is not needed for M2.
    const DecodeOutcome decoded = decode_float(src, probe.info);
    if (!decoded.error.empty() || !decoded.buf.initialized()) {
        return out; // 4) thumb empty, error stays empty (probe itself succeeded)
    }
    thumb = ThumbImage{};
    if (to_rgba8(decoded.buf, target_long_edge, thumb)) {
        thumb.from_embedded = false;
        out.thumb = std::move(thumb);
    }
    return out;
}

} // namespace pp
