// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T6 — encoder smoke tests (jpegli / libjxl / libwebp).
//
// Contract: docs/m1-tasks.md §3.8 (E1–E9 + parameter mapping tables) / §3.17
// (registry) / §4.6 (unit-test list). All images are synthesised in memory
// (64x64 float32 ImageBuf) — no input files are used; only the encoder outputs
// are written, under <cwd>/.cache/tmp/m1-t6/enc_smoke (§1.8, gitignored).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <lcms2.h>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>
#include <webp/decode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "core/logger.h"
#include "core/metadata.h"
#include "core/params.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
        std::fflush(stdout);
    }
}

// Measured values kept in the test output as evidence for the task report.
void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

std::string num(long long v) { return std::to_string(v); }

// ------------------------------------------------------------- synthetic image --
// Pixel values are generated on the 8-bit grid (u8/255) so lossless round-trips
// can be compared byte for byte; the pattern is smooth enough for the PSNR case.
struct Image {
    int width = 64, height = 64, channels = 3;
    std::vector<uint8_t> u8;   // ground truth (8-bit)
    std::vector<uint16_t> u16; // ground truth (16-bit view: u8 * 257)
    std::vector<float> f;      // what the encoder is handed
};

Image make_image(int w, int h, int channels) {
    Image img;
    img.width = w;
    img.height = h;
    img.channels = channels;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    img.u8.resize(n * static_cast<std::size_t>(channels));
    img.u16.resize(n * static_cast<std::size_t>(channels));
    img.f.resize(n * static_cast<std::size_t>(channels));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                   static_cast<std::size_t>(x)) *
                                  static_cast<std::size_t>(channels);
            uint8_t c[4] = {0, 0, 0, 255};
            c[0] = static_cast<uint8_t>((x * 255) / std::max(1, w - 1));
            c[1] = static_cast<uint8_t>((y * 255) / std::max(1, h - 1));
            c[2] = static_cast<uint8_t>(128 + static_cast<int>(100.0 * std::sin(0.15 * (x + y))));
            if (channels >= 4) {
                c[3] = static_cast<uint8_t>(255 - ((x * 255) / std::max(1, w - 1)) / 2);
            }
            for (int ch = 0; ch < channels; ++ch) {
                const std::size_t k = i + static_cast<std::size_t>(ch);
                img.u8[k] = c[ch];
                img.u16[k] = static_cast<uint16_t>(c[ch] * 257);
                img.f[k] = static_cast<float>(c[ch]) / 255.0f;
            }
        }
    }
    return img;
}

OIIO::ImageBuf to_buf(const Image &img) {
    OIIO::ImageSpec spec(img.width, img.height, img.channels, OIIO::TypeFloat);
    OIIO::ImageBuf buf(spec);
    OIIO::ROI roi(0, img.width, 0, img.height, 0, 1, 0, img.channels);
    buf.set_pixels(roi, OIIO::TypeFloat,
                   OIIO::span<const std::byte>(reinterpret_cast<const std::byte *>(img.f.data()),
                                               img.f.size() * sizeof(float)));
    return buf;
}

// ------------------------------------------------------------------- read back --
struct ReadBack {
    bool ok = false;
    std::string error;
    int width = 0, height = 0, channels = 0;
    std::vector<uint8_t> u8;
    std::vector<uint16_t> u16;
};

ReadBack read_back(const fs::path &p, int want_channels, const OIIO::TypeDesc &type) {
    ReadBack rb;
    OIIO::ImageBuf buf(p.string());
    if (!buf.read(0, 0, 0, want_channels, true, type)) {
        rb.error = buf.geterror();
        if (rb.error.empty()) {
            rb.error = "ImageBuf::read failed";
        }
        return rb;
    }
    const OIIO::ImageSpec &s = buf.spec();
    rb.width = s.width;
    rb.height = s.height;
    rb.channels = s.nchannels;
    const std::size_t n = static_cast<std::size_t>(s.width) * static_cast<std::size_t>(s.height) *
                          static_cast<std::size_t>(s.nchannels);
    OIIO::ROI roi(0, s.width, 0, s.height, 0, 1, 0, s.nchannels);
    if (type == OIIO::TypeDesc::UINT16) {
        rb.u16.assign(n, 0);
        if (!buf.get_pixels(roi, OIIO::TypeDesc::UINT16,
                            OIIO::span<std::byte>(reinterpret_cast<std::byte *>(rb.u16.data()),
                                                  rb.u16.size() * sizeof(uint16_t)))) {
            rb.error = buf.geterror();
            return rb;
        }
    } else {
        rb.u8.assign(n, 0);
        if (!buf.get_pixels(
                roi, type,
                OIIO::span<std::byte>(reinterpret_cast<std::byte *>(rb.u8.data()), rb.u8.size()))) {
            rb.error = buf.geterror();
            return rb;
        }
    }
    rb.ok = true;
    return rb;
}

OIIO::ImageBuf read_float(const fs::path &p, int want_channels) {
    OIIO::ImageBuf buf(p.string());
    buf.read(0, 0, 0, want_channels, true, OIIO::TypeDesc::FLOAT);
    return buf;
}

// Direct libwebp decode of an RGBA file: the OIIO webp reader premultiplies RGB
// by alpha, which hides the encoder's exact output (see the alpha test below).
struct WebpDecoded {
    bool ok = false;
    std::string error;
    int width = 0, height = 0, channels = 0;
    std::vector<uint8_t> rgba;
};

WebpDecoded decode_webp_rgba(const fs::path &p) {
    WebpDecoded out;
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        out.error = "cannot open " + p.string();
        return out;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string bytes = ss.str();
    int w = 0;
    int h = 0;
    uint8_t *dec =
        WebPDecodeRGBA(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), &w, &h);
    if (dec == nullptr) {
        out.error = "WebPDecodeRGBA failed";
        return out;
    }
    out.width = w;
    out.height = h;
    out.channels = 4;
    out.rgba.assign(dec, dec + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    WebPFree(dec);
    out.ok = true;
    return out;
}

std::string icc_of(const fs::path &p) {
    OIIO::ImageBuf buf;
    if (!buf.init_spec(p.string(), 0, 0)) {
        return {};
    }
    const OIIO::ParamValue *pv = buf.spec().find_attribute("ICCProfile");
    if (pv == nullptr || pv->type().basetype != OIIO::TypeDesc::UINT8 || pv->datasize() <= 0) {
        return {};
    }
    return std::string(static_cast<const char *>(pv->data()),
                       static_cast<std::size_t>(pv->datasize()));
}

std::string srgb_icc_bytes() {
    cmsHPROFILE p = cmsCreate_sRGBProfile();
    if (p == nullptr) {
        return {};
    }
    cmsUInt32Number need = 0;
    if (!cmsSaveProfileToMem(p, nullptr, &need) || need == 0) {
        cmsCloseProfile(p);
        return {};
    }
    std::string out(need, '\0');
    cmsUInt32Number written = need;
    if (!cmsSaveProfileToMem(p, out.data(), &written) || written == 0) {
        cmsCloseProfile(p);
        return {};
    }
    cmsCloseProfile(p);
    out.resize(written);
    return out;
}

std::string read_text(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Newest run-*.log inside dir (logger keeps up to 20, so sort by mtime).
fs::path newest_log(const fs::path &dir) {
    std::error_code ec;
    fs::path best;
    fs::file_time_type best_t{};
    for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file(ec) || e.path().extension() != ".log") {
            continue;
        }
        const fs::file_time_type t = e.last_write_time(ec);
        if (best.empty() || t > best_t) {
            best = e.path();
            best_t = t;
        }
    }
    return best;
}

// --------------------------------------------------------------- parameters --
pp::ParamSet params_for(const char *format, const char *backend, const char *tech, bool lossless) {
    const pp::FormatDef *f = pp::find_format(format);
    check(f != nullptr, std::string("params/find_format/") + format, "format table lookup failed");
    if (f == nullptr) {
        return {};
    }
    return pp::default_params(*f, backend, tech, lossless);
}

pp::EncodeResult run_encode(pp::IEncoder *enc, OIIO::ImageBuf &img, const pp::ParamSet &params,
                            const fs::path &out, const pp::MetadataPayloads &meta = {},
                            int out_bitdepth = 8, const std::string &tech_id = std::string()) {
    if (enc == nullptr) {
        pp::EncodeResult res;
        res.error = "make_encoder returned nullptr (registry lookup failed)";
        return res;
    }
    // M4-T5/§3.1：EncodeRequest 以 OutputTarget 取代 params/out_bitdepth/out_path/tech_id。
    // 本助手把入参原样装进 target（编码器只读 target.*，测例语义零变化）；
    // progress 留空（T6 接线位）、encode_threads=1（T7 接 alloc_threads）。
    pp::OutputTarget target;
    target.format_id = enc->format().id;
    target.tech_id = tech_id;
    target.params = params;
    target.out_bitdepth = out_bitdepth;
    target.out_path = out;
    target.supports_alpha = enc->format().supports_alpha;
    pp::EncodeRequest req{img, target, meta, {}, pp::ProgressFn{}, 1};
    return enc->encode(req);
}

bool same_u8(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b, std::string &detail) {
    if (a.size() != b.size()) {
        detail = "size mismatch " + num(static_cast<long long>(a.size())) + " vs " +
                 num(static_cast<long long>(b.size()));
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            detail = "first difference at byte " + num(static_cast<long long>(i)) + ": " +
                     num(a[i]) + " vs " + num(b[i]);
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    std::error_code ec;
    // §1.8: throw-away artefacts live under .cache/tmp (gitignored), so the
    // repository stays clean even when the binary is run from the repo root.
    const fs::path root = fs::current_path(ec) / ".cache" / "tmp" / "m1-t6" / "enc_smoke";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    pp::log_init(root / "log", pp::LogLevel::Warn);

    // ---------------------------------------------------------------- registry --
    {
        std::unique_ptr<pp::IEncoder> jpeg = pp::make_encoder("jpeg", "jpegli");
        std::unique_ptr<pp::IEncoder> jxl = pp::make_encoder("jxl", "libjxl");
        std::unique_ptr<pp::IEncoder> webp = pp::make_encoder("webp", "libwebp");
        check(jpeg != nullptr, "registry/make_encoder/jpeg",
              "make_encoder(jpeg,jpegli) == nullptr");
        check(jxl != nullptr, "registry/make_encoder/jxl", "make_encoder(jxl,libjxl) == nullptr");
        check(webp != nullptr, "registry/make_encoder/webp",
              "make_encoder(webp,libwebp) == nullptr");
        if (jpeg) {
            check(jpeg->format().id == "jpeg", "registry/format/jpeg", jpeg->format().id);
        }
        if (jxl) {
            check(jxl->format().id == "jxl", "registry/format/jxl", jxl->format().id);
        }
        if (webp) {
            check(webp->format().id == "webp", "registry/format/webp", webp->format().id);
        }
        // Empty backend_id → first registration of that format (§3.17).
        check(pp::make_encoder("jpeg", "") != nullptr, "registry/default_backend",
              "empty backend_id did not resolve to the first registration");
        check(pp::make_encoder("nosuchformat", "") == nullptr, "registry/unknown_format",
              "unknown format must yield nullptr");
        check(pp::make_encoder("jpeg", "nosuchbackend") == nullptr, "registry/unknown_backend",
              "unknown backend must yield nullptr");
        const std::vector<std::string> jpeg_backends = pp::registered_backends("jpeg");
        check(std::find(jpeg_backends.begin(), jpeg_backends.end(), "jpegli") !=
                  jpeg_backends.end(),
              "registry/registered_backends", "jpegli missing from registered_backends(jpeg)");
        // Duplicate registration keeps the first entry and reports false.
        check(!pp::register_encoder("jpeg", "jpegli", nullptr), "registry/duplicate",
              "duplicate/empty factory registration must return false");
    }

    const Image rgb = make_image(64, 64, 3);

    // ------------------------------------------- 1) three formats encode OK --
    {
        struct Case {
            const char *name;
            const char *format;
            const char *backend;
            const char *tech;
            const char *ext;
        };
        const Case cases[] = {
            {"jpeg", "jpeg", "jpegli", "dct", "jpg"},
            {"jxl", "jxl", "libjxl", "vardct", "jxl"},
            {"webp", "webp", "libwebp", "lossy", "webp"},
        };
        for (const Case &c : cases) {
            OIIO::ImageBuf buf = to_buf(rgb);
            std::unique_ptr<pp::IEncoder> enc = pp::make_encoder(c.format, c.backend);
            pp::ParamSet params = params_for(c.format, c.backend, c.tech, false);
            const fs::path out = root / (std::string("rgb.") + c.ext);
            pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
            const std::string tag = std::string("encode/") + c.name;
            check(res.error.empty(), tag, "unexpected error: " + res.error);
            check(res.bytes > 0, tag, "bytes == 0");
            check(fs::exists(out, ec) && fs::file_size(out, ec) == res.bytes, tag,
                  "output file missing or size != result.bytes");
            info(std::string(c.name) +
                 " rgb 64x64 bytes=" + num(static_cast<long long>(res.bytes)) +
                 " encode_ms=" + std::to_string(res.t.encode_ms));
        }
    }

    // ------------------------------------------------- 2) JPEG quality (E9) --
    {
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        OIIO::ImageBuf low = to_buf(rgb);
        OIIO::ImageBuf high = to_buf(rgb);
        pp::ParamSet p_low = params_for("jpeg", "jpegli", "dct", false);
        pp::ParamSet p_high = p_low;
        // param_* reading: quality_mode switches the active control; distance is
        // fed as an int64 on purpose (param_float widening).
        p_low["quality_mode"] = std::string("quality");
        p_low["quality"] = int64_t(30);
        p_high["quality_mode"] = std::string("quality");
        p_high["quality"] = int64_t(95);
        const pp::EncodeResult r_low = run_encode(enc.get(), low, p_low, root / "q30.jpg");
        const pp::EncodeResult r_high = run_encode(enc.get(), high, p_high, root / "q95.jpg");
        check(r_low.error.empty() && r_high.error.empty(), "jpeg/quality_mode",
              "quality path failed: " + r_low.error + r_high.error);
        check(r_low.bytes > 0 && r_low.bytes < r_high.bytes, "jpeg/quality_effect",
              "quality=30 (" + num(static_cast<long long>(r_low.bytes)) +
                  ") must be smaller than q95 (" + num(static_cast<long long>(r_high.bytes)) + ")");

        OIIO::ImageBuf d1 = to_buf(rgb);
        OIIO::ImageBuf d3 = to_buf(rgb);
        pp::ParamSet p_d1 = params_for("jpeg", "jpegli", "dct", false);
        pp::ParamSet p_d3 = p_d1;
        p_d3["distance"] = 3.0;
        const pp::EncodeResult r_d1 = run_encode(enc.get(), d1, p_d1, root / "d1.jpg");
        const pp::EncodeResult r_d3 = run_encode(enc.get(), d3, p_d3, root / "d3.jpg");
        check(r_d1.error.empty() && r_d3.error.empty(), "jpeg/distance_mode",
              "distance path failed: " + r_d1.error + r_d3.error);
        check(r_d1.bytes > r_d3.bytes, "jpeg/distance_effect",
              "distance=1.0 must be larger than distance=3.0");
        info("jpeg distance=1.0 bytes=" + num(static_cast<long long>(r_d1.bytes)) +
             " distance=3.0 bytes=" + num(static_cast<long long>(r_d3.bytes)));
    }

    // ------------------------------------------- 3) JPEG distance=1.0 PSNR --
    {
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        OIIO::ImageBuf buf = to_buf(rgb);
        pp::ParamSet params = params_for("jpeg", "jpegli", "dct", false);
        const fs::path out = root / "psnr_distance1.jpg";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
        check(res.error.empty(), "jpeg/psnr/encode", res.error);
        OIIO::ImageBuf ref = to_buf(rgb);
        OIIO::ImageBuf got = read_float(out, 3);
        const OIIO::ImageBufAlgo::CompareResults cr =
            OIIO::ImageBufAlgo::compare(ref, got, 0.0f, 0.0f);
        check(cr.PSNR >= 35.0, "jpeg/psnr/distance1",
              "PSNR " + std::to_string(cr.PSNR) + " dB < 35 dB (maxerror " +
                  std::to_string(cr.maxerror) + ")");
        info("jpeg distance=1.0 PSNR=" + std::to_string(cr.PSNR) + " dB");
    }

    // ------------------------------------- 4) JXL lossless bit-exact (modular) --
    {
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jxl", "libjxl");
        OIIO::ImageBuf buf = to_buf(rgb);
        pp::ParamSet params = params_for("jxl", "libjxl", "modular", true);
        check(pp::param_bool(params, "__lossless", false), "jxl/lossless/reserved_key",
              "default_params(modular, lossless=true) must set __lossless");
        const fs::path out = root / "lossless.jxl";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
        check(res.error.empty(), "jxl/lossless/encode", res.error);
        const ReadBack rb = read_back(out, 3, OIIO::TypeDesc::UINT8);
        check(rb.ok, "jxl/lossless/read", rb.error);
        std::string detail;
        check(rb.ok && rb.width == 64 && rb.height == 64 && rb.channels == 3,
              "jxl/lossless/geometry",
              "got " + num(rb.width) + "x" + num(rb.height) + "x" + num(rb.channels));
        check(rb.ok && same_u8(rb.u8, rgb.u8, detail), "jxl/lossless/exact", detail);
        info("jxl lossless modular bytes=" + num(static_cast<long long>(res.bytes)));
    }

    // ------------------------------------------ 5) WebP lossless bit-exact --
    {
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("webp", "libwebp");
        OIIO::ImageBuf buf = to_buf(rgb);
        pp::ParamSet params = params_for("webp", "libwebp", "lossless", true);
        const fs::path out = root / "lossless.webp";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
        check(res.error.empty(), "webp/lossless/encode", res.error);
        const ReadBack rb = read_back(out, 3, OIIO::TypeDesc::UINT8);
        check(rb.ok, "webp/lossless/read", rb.error);
        std::string detail;
        check(rb.ok && rb.width == 64 && rb.height == 64 && rb.channels == 3,
              "webp/lossless/geometry",
              "got " + num(rb.width) + "x" + num(rb.height) + "x" + num(rb.channels));
        check(rb.ok && same_u8(rb.u8, rgb.u8, detail), "webp/lossless/exact", detail);
        info("webp lossless bytes=" + num(static_cast<long long>(res.bytes)));
    }

    // --------------------------------------------- 6) alpha preserved (E4) --
    {
        const Image rgba = make_image(64, 64, 4);
        struct Case {
            const char *name;
            const char *format;
            const char *backend;
            const char *tech;
            const char *ext;
        };
        const Case cases[] = {
            {"jxl", "jxl", "libjxl", "modular", "jxl"},
            {"webp", "webp", "libwebp", "lossless", "webp"},
        };
        for (const Case &c : cases) {
            OIIO::ImageBuf buf = to_buf(rgba);
            std::unique_ptr<pp::IEncoder> enc = pp::make_encoder(c.format, c.backend);
            pp::ParamSet params = params_for(c.format, c.backend, c.tech, true);
            const fs::path out = root / (std::string("alpha.") + c.ext);
            const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
            const std::string tag = std::string("alpha/") + c.name;
            check(res.error.empty(), tag, "encode error: " + res.error);
            const ReadBack rb = read_back(out, 4, OIIO::TypeDesc::UINT8);
            check(rb.ok && rb.channels == 4, tag,
                  "alpha channel lost: channels=" + num(rb.channels) + " " + rb.error);
            // Alpha plane (channel 3) must survive losslessly.
            bool alpha_ok = rb.ok && rb.u8.size() == rgba.u8.size();
            std::string detail = "size mismatch";
            if (alpha_ok) {
                std::vector<uint8_t> got_a, want_a;
                for (std::size_t i = 3; i < rb.u8.size(); i += 4) {
                    got_a.push_back(rb.u8[i]);
                    want_a.push_back(rgba.u8[i]);
                }
                alpha_ok = same_u8(got_a, want_a, detail);
            }
            check(alpha_ok, tag + "/exact", detail);
            // Colour planes of a lossless encode. Read back through OIIO for JXL;
            // for WebP the OIIO reader returns RGB premultiplied by alpha
            // (measured: file (4,0,142,253) → OIIO (4,0,141,253)), so the exact
            // comparison uses the libwebp decoder instead (same bytes the encoder
            // wrote; see api-deltas in the M1-T6 report).
            if (std::string(c.name) == "webp") {
                const WebpDecoded wd = decode_webp_rgba(out);
                check(wd.ok, tag + "/libwebp_decode", wd.error);
                check(wd.ok && wd.width == 64 && wd.height == 64 && wd.channels == 4,
                      tag + "/libwebp_geometry",
                      "got " + num(wd.width) + "x" + num(wd.height) + "x" + num(wd.channels));
                std::string exact_detail;
                check(wd.ok && same_u8(wd.rgba, rgba.u8, exact_detail), tag + "/rgba_exact",
                      exact_detail);
            } else {
                std::string rgb_detail;
                check(rb.ok && same_u8(rb.u8, rgba.u8, rgb_detail), tag + "/rgba_exact",
                      rgb_detail);
            }
        }
    }

    // ------------------------------------------------- 7) grayscale JPEG (E4) --
    {
        const Image gray = make_image(64, 64, 1);
        OIIO::ImageBuf buf = to_buf(gray);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        pp::ParamSet params = params_for("jpeg", "jpegli", "dct", false);
        const fs::path out = root / "gray.jpg";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
        check(res.error.empty() && res.bytes > 0, "gray/jpeg/encode", res.error);
        const ReadBack rb = read_back(out, 1, OIIO::TypeDesc::UINT8);
        check(rb.ok && rb.channels == 1 && rb.width == 64 && rb.height == 64, "gray/jpeg/read",
              "expected 64x64x1, got " + num(rb.width) + "x" + num(rb.height) + "x" +
                  num(rb.channels) + " " + rb.error);
    }

    // ------------------------------- 8) arith_code rejected (E3/§3.8 error串) --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        pp::ParamSet params = params_for("jpeg", "jpegli", "dct", false);
        params["arith_code"] = true;
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, root / "arith.jpg");
        check(res.bytes == 0 && res.error.find("arith_code") != std::string::npos,
              "jpeg/arith_code",
              "expected error containing 'arith_code', got bytes=" +
                  num(static_cast<long long>(res.bytes)) + " error='" + res.error + "'");
        info("jpeg arith_code error='" + res.error + "'");
    }

    // --------------------- 9) unsupported out_bitdepth rejected (E3) --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> jpeg = pp::make_encoder("jpeg", "jpegli");
        std::unique_ptr<pp::IEncoder> webp = pp::make_encoder("webp", "libwebp");
        pp::ParamSet pj = params_for("jpeg", "jpegli", "dct", false);
        pp::ParamSet pw = params_for("webp", "webp", "lossy", false);
        const pp::EncodeResult rj = run_encode(jpeg.get(), buf, pj, root / "depth16.jpg", {}, 16);
        const pp::EncodeResult rw = run_encode(webp.get(), buf, pw, root / "depth16.webp", {}, 16);
        check(rj.bytes == 0 && !rj.error.empty(), "depth/jpeg16", "16-bit JPEG must fail");
        check(rw.bytes == 0 && !rw.error.empty(), "depth/webp16", "16-bit WebP must fail");
    }

    // ------------------------------------------- 10) JXL 16-bit lossless --
    {
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jxl", "libjxl");
        OIIO::ImageBuf buf = to_buf(rgb);
        pp::ParamSet params = params_for("jxl", "libjxl", "modular", true);
        const fs::path out = root / "lossless16.jxl";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out, {}, 16);
        check(res.error.empty() && res.bytes > 0, "jxl/16bit/encode", res.error);
        const ReadBack rb = read_back(out, 3, OIIO::TypeDesc::UINT16);
        check(rb.ok && rb.u16 == rgb.u16, "jxl/16bit/exact",
              "16-bit lossless round-trip differs" +
                  std::string(rb.ok ? "" : (" (" + rb.error + ")")));
    }

    // ------------------------------------- 11) unknown parameter → warning --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        pp::ParamSet params = params_for("jpeg", "jpegli", "dct", false);
        params["totally_unknown_key"] = int64_t(7);
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, root / "unknown_param.jpg");
        check(res.error.empty() && res.bytes > 0, "e9/unknown_key/still_succeeds", res.error);
        pp::log_shutdown(); // flush before scanning the log file
        const fs::path log = newest_log(root / "log");
        const std::string text = log.empty() ? std::string() : read_text(log);
        check(text.find("unknown parameter ignored") != std::string::npos &&
                  text.find("totally_unknown_key") != std::string::npos,
              "e9/unknown_key/warning",
              "no warning log line for the unknown key (log: " +
                  (log.empty() ? std::string("<none>") : log.string()) + ")");
        pp::log_init(root / "log", pp::LogLevel::Warn);
    }

    // ---------------------------------------------- 12) ICC embedded (E5) --
    {
        const std::string icc = srgb_icc_bytes();
        check(!icc.empty(), "icc/fixture", "cmsCreate_sRGBProfile produced no bytes");
        if (!icc.empty()) {
            pp::MetadataPayloads meta;
            meta.icc_profile = icc;
            struct Case {
                const char *name;
                const char *format;
                const char *backend;
                const char *tech;
                const char *ext;
            };
            const Case cases[] = {
                {"jpeg", "jpeg", "jpegli", "dct", "jpg"},
                {"jxl", "jxl", "libjxl", "modular", "jxl"},
                {"webp", "webp", "libwebp", "lossless", "webp"},
            };
            for (const Case &c : cases) {
                OIIO::ImageBuf buf = to_buf(rgb);
                std::unique_ptr<pp::IEncoder> enc = pp::make_encoder(c.format, c.backend);
                pp::ParamSet params = params_for(c.format, c.backend, c.tech, true);
                const fs::path out = root / (std::string("icc.") + c.ext);
                const pp::EncodeResult res = run_encode(enc.get(), buf, params, out, meta);
                const std::string tag = std::string("icc/") + c.name;
                check(res.error.empty() && res.bytes > 0, tag, "encode error: " + res.error);
                const std::string got = icc_of(out);
                check(!got.empty(), tag + "/readback", "ICC profile not found in the output file");
            }
        }
    }

    // ------------------------- 13) JXL Exif/xml boxes (E7, M0 box order) --
    {
        pp::MetadataPlan plan;
        plan.exif["Exif.Photo.DateTimeOriginal"] = std::string("2024:03:01 10:00:00");
        plan.exif["Exif.Image.Artist"] = std::string("M1-T6");
        plan.xmp["Xmp.dc.creator"] = std::string("PhotoPipeline");
        const pp::Payloads payloads = pp::make_payloads(plan);
        check(!payloads.exif_blob.empty(), "jxl/box/payload",
              "make_payloads produced no Exif blob");
        pp::MetadataPayloads meta;
        meta.exif_blob = payloads.exif_blob;
        meta.xmp_rdf = payloads.xmp_rdf;

        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jxl", "libjxl");
        pp::ParamSet params = params_for("jxl", "libjxl", "modular", true);
        const fs::path out = root / "boxes.jxl";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out, meta);
        check(res.error.empty() && res.bytes > 0, "jxl/box/encode", res.error);
        info("jxl boxes: exif_blob=" + num(static_cast<long long>(payloads.exif_blob.size())) +
             " xmp_rdf=" + num(static_cast<long long>(payloads.xmp_rdf.size())) +
             " file=" + num(static_cast<long long>(res.bytes)));
        // The BMFF box headers must be visible in the written container (§3.8 E7).
        const std::string file_bytes = read_text(out);
        check(file_bytes.find("Exif") != std::string::npos, "jxl/box/exif_box_present",
              "'Exif' box type not found in the container");
        if (!payloads.xmp_rdf.empty()) {
            check(file_bytes.find("xml ") != std::string::npos, "jxl/box/xmp_box_present",
                  "'xml ' box type not found in the container");
        }
        const pp::SourceMeta read = pp::read_metadata(out);
        const auto it = read.exif.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
        check(it != read.exif.end() && it->toString() == "2024:03:01 10:00:00",
              "jxl/box/exif_roundtrip",
              "DateTimeOriginal not read back (read error: '" + read.error + "')");
        // Pixels must still be intact with boxes present.
        const ReadBack rb = read_back(out, 3, OIIO::TypeDesc::UINT8);
        std::string detail;
        check(rb.ok && same_u8(rb.u8, rgb.u8, detail), "jxl/box/pixels", detail);
    }

    // ------------------------- 14) E9: sparse parameter set uses table defaults --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jpeg", "jpegli");
        pp::ParamSet sparse;      // param_* fallbacks; no __lossless key
        sparse["distance"] = 1.0; // Float
        const pp::EncodeResult res = run_encode(enc.get(), buf, sparse, root / "sparse.jpg");
        check(res.error.empty() && res.bytes > 0, "e9/sparse_params", res.error);
        // Reserved keys are never reported as unknown parameters.
        const fs::path log = newest_log(root / "log");
        const std::string text = log.empty() ? std::string() : read_text(log);
        check(text.find("__lossless") == std::string::npos, "e9/reserved_key_excluded",
              "__lossless must not be treated as an unknown parameter");
    }

    // ------------------- 15) E1: one encoder instance shared by 4 threads --
    {
        struct Job {
            const char *format;
            const char *backend;
            const char *tech;
            const char *ext;
        };
        const Job jobs[] = {
            {"jpeg", "jpegli", "dct", "jpg"},
            {"jxl", "libjxl", "vardct", "jxl"},
            {"webp", "libwebp", "lossy", "webp"},
        };
        for (const Job &j : jobs) {
            std::unique_ptr<pp::IEncoder> enc = pp::make_encoder(j.format, j.backend);
            pp::ParamSet params = params_for(j.format, j.backend, j.tech, false);
            std::atomic<int> ok{0};
            std::mutex err_mutex;
            std::string first_error;
            std::vector<std::thread> threads;
            for (int t = 0; t < 4; ++t) {
                threads.emplace_back([&, t] {
                    for (int i = 0; i < 2; ++i) {
                        OIIO::ImageBuf buf = to_buf(rgb); // per-thread input buffer
                        const fs::path out = root / ("shared_" + std::to_string(t) + "_" +
                                                     std::to_string(i) + "." + j.ext);
                        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out);
                        if (res.error.empty() && res.bytes > 0) {
                            ++ok;
                        } else {
                            std::lock_guard<std::mutex> lock(err_mutex);
                            if (first_error.empty()) {
                                first_error = res.error;
                            }
                        }
                    }
                });
            }
            for (std::thread &th : threads) {
                th.join();
            }
            check(ok.load() == 8, std::string("e1/shared_instance/") + j.format,
                  std::to_string(ok.load()) + "/8 concurrent encodes succeeded, first error: '" +
                      first_error + "'");
        }
    }

    // ---------------- 16) T6b: explicit tech_id selects lossy Modular --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jxl", "libjxl");
        // Lossy Modular is the case the old inference could not express: the
        // parameter set alone (modular keys + __lossless=false) used to leave the
        // library on the VarDCT default, so tech_id is now authoritative.
        pp::ParamSet params = params_for("jxl", "libjxl", "modular", false);
        params["distance"] = 1.0;
        const fs::path out = root / "modular_lossy.jxl";
        const pp::EncodeResult res = run_encode(enc.get(), buf, params, out, {}, 8, "modular");
        check(res.error.empty() && res.bytes > 0, "tech/modular_lossy/encode", res.error);
        OIIO::ImageBuf ref = to_buf(rgb);
        OIIO::ImageBuf got = read_float(out, 3);
        check(got.spec().width == 64 && got.spec().height == 64 && got.spec().nchannels == 3,
              "tech/modular_lossy/geometry",
              "got " + num(got.spec().width) + "x" + num(got.spec().height) + "x" +
                  num(got.spec().nchannels));
        const OIIO::ImageBufAlgo::CompareResults cr =
            OIIO::ImageBufAlgo::compare(ref, got, 0.0f, 0.0f);
        check(cr.PSNR >= 30.0, "tech/modular_lossy/psnr",
              "PSNR " + std::to_string(cr.PSNR) + " dB < 30 dB");
        check(cr.maxerror > 0.0, "tech/modular_lossy/is_lossy",
              "distance=1.0 produced a bit-exact result (lossless path taken?)");
        info("jxl tech_id=modular distance=1.0 bytes=" + num(static_cast<long long>(res.bytes)) +
             " PSNR=" + std::to_string(cr.PSNR) + " dB");
    }

    // ------------- 17) T6b: empty tech_id keeps the previous fallback --
    {
        OIIO::ImageBuf buf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> webp = pp::make_encoder("webp", "libwebp");
        pp::ParamSet wp = params_for("webp", "libwebp", "lossless", true);
        const fs::path wout = root / "fallback.webp";
        const pp::EncodeResult wres = run_encode(webp.get(), buf, wp, wout);
        check(wres.error.empty() && wres.bytes > 0, "tech/empty_fallback/webp", wres.error);
        const ReadBack wrb = read_back(wout, 3, OIIO::TypeDesc::UINT8);
        std::string wdetail;
        check(wrb.ok && same_u8(wrb.u8, rgb.u8, wdetail), "tech/empty_fallback/webp_exact",
              wdetail);

        OIIO::ImageBuf jbuf = to_buf(rgb);
        std::unique_ptr<pp::IEncoder> jxl = pp::make_encoder("jxl", "libjxl");
        pp::ParamSet jp = params_for("jxl", "libjxl", "modular", true);
        const fs::path jout = root / "fallback.jxl";
        const pp::EncodeResult jres = run_encode(jxl.get(), jbuf, jp, jout);
        check(jres.error.empty() && jres.bytes > 0, "tech/empty_fallback/jxl", jres.error);
        const ReadBack jrb = read_back(jout, 3, OIIO::TypeDesc::UINT8);
        std::string jdetail;
        check(jrb.ok && same_u8(jrb.u8, rgb.u8, jdetail), "tech/empty_fallback/jxl_exact", jdetail);
    }

    pp::log_shutdown();
    std::printf("SUMMARY checks=%d failed=%d\n", g_checks, g_failed);
    return g_failed;
}
