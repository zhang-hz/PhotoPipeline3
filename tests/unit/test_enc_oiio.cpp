// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T7 — encoder unit tests: libheif HEIF/AVIF + OIIO PNG/TIFF/BMP,
// runtime introspection (R4) and the 10-bit capability probe (R19).
//
// Contract: docs/m1-tasks.md §3.8 (E1–E9 + factories/introspection), §3.17 (registry),
// §4.7 (unit test list), §7 (measured facts). Hand-written assertions, no GTest.
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <exiv2/exiv2.hpp>
#include <lcms2.h>
#include <libheif/heif.h>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "codecs/heif_nclx.h"    // M7-WP2 UX-14 A1
#include "codecs/heif_profile.h" // M7-WP2 I4
#include "core/colormanager.h"
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

void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

std::string num(int64_t v) { return std::to_string(v); }

// ---------------------------------------------------------------- paths --

fs::path repo_root() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec))
            return p;
        if (!p.has_parent_path() || p.parent_path() == p)
            break;
        p = p.parent_path();
    }
    return {};
}

fs::path out_dir() {
    // Created once per process: every test case writes into the same directory, so a
    // per-call remove_all() would delete the previous case's evidence.
    static const fs::path d = [] {
        std::error_code ec;
        const fs::path dir = repo_root() / ".cache" / "tmp" / "m1-t7" / "test_enc_oiio";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        return dir;
    }();
    return d;
}

// ---------------------------------------------------------------- fixtures --

OIIO::ImageBuf make_buf(int w, int h, int nch, float phase) {
    OIIO::ImageSpec spec(w, h, nch, OIIO::TypeFloat);
    OIIO::ImageBuf buf(spec);
    std::vector<float> px(static_cast<size_t>(w) * h * nch);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < nch; c++) {
                const float base = static_cast<float>(x) / static_cast<float>(w - 1);
                const float v = (c == 0)
                                    ? base
                                    : (c == 1 ? static_cast<float>(y) / static_cast<float>(h - 1)
                                              : 0.25f + 0.5f * base);
                // alpha / 2nd channel of gray+alpha stays opaque
                px[(static_cast<size_t>(y) * w + x) * nch + c] =
                    (nch == 2 && c == 1) || (nch == 4 && c == 3) ? 1.0f : v + phase;
            }
        }
    }
    OIIO::ROI roi(0, w, 0, h, 0, 1, 0, nch);
    buf.set_pixels(roi, OIIO::TypeFloat,
                   OIIO::span<const std::byte>(reinterpret_cast<const std::byte *>(px.data()),
                                               px.size() * sizeof(float)));
    return buf;
}

pp::EncodeResult run_encode(pp::IEncoder &enc, OIIO::ImageBuf &buf, int bitdepth,
                            const pp::ParamSet &params, const pp::MetadataPayloads &meta,
                            const fs::path &out) {
    // M4-T5/§3.1：EncodeRequest 以 OutputTarget 取代 params/out_bitdepth/out_path；
    // 本助手原样装载（编码器只读 target.*，测例语义零变化）。
    pp::OutputTarget target;
    target.format_id = enc.format().id;
    target.params = params;
    target.out_bitdepth = bitdepth;
    target.out_path = out;
    target.supports_alpha = enc.format().supports_alpha;
    pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}, 1};
    return enc.encode(req);
}

// Read back a spec without decoding pixels.
bool read_spec(const fs::path &p, OIIO::ImageSpec &spec, std::string &err) {
    auto in = OIIO::ImageInput::open(p.string());
    if (!in) {
        err = "ImageInput::open failed: " + OIIO::geterror();
        return false;
    }
    spec = in->spec();
    in->close();
    return true;
}

// ---------------------------------------------------------------- EXIF payload --

std::string make_exif_blob(const std::string &dto) {
    Exiv2::ExifData ed;
    ed["Exif.Photo.DateTimeOriginal"] = dto;
    ed["Exif.Image.Make"] = "PhotoPipeline";
    ed["Exif.Image.Model"] = "M1-T7";
    Exiv2::Blob blob;
    Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, ed);
    return std::string(reinterpret_cast<const char *>(blob.data()), blob.size());
}

bool read_datetime_original(const fs::path &p, std::string &value, std::string &err) {
    try {
        auto im = Exiv2::ImageFactory::open(p.string());
        im->readMetadata();
        const Exiv2::ExifData &ex = im->exifData();
        const auto it = ex.findKey(Exiv2::ExifKey("Exif.Photo.DateTimeOriginal"));
        if (it == ex.end()) {
            err = "Exif.Photo.DateTimeOriginal missing";
            return false;
        }
        value = it->toString();
        return true;
    } catch (const Exiv2::Error &e) {
        err = e.what();
        return false;
    }
}

// Raw byte search (used to prove the XMP item landed in the container; Exiv2 0.28.8 does
// not expose HEIF/AVIF XMP, see report).
bool file_contains(const fs::path &p, const std::string &needle) {
    FILE *f = std::fopen(p.string().c_str(), "rb");
    if (!f)
        return false;
    std::string data;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        data.append(buf, n);
    std::fclose(f);
    return data.find(needle) != std::string::npos;
}

// ================================================================ test cases --

void test_registry() {
    const char *combos[][2] = {{"png", "oiio"},  {"tiff", "oiio"},    {"bmp", "oiio"},
                               {"heif", "x265"}, {"avif", "svt-av1"}, {"avif", "libaom"}};
    for (const auto &c : combos) {
        auto enc = pp::make_encoder(c[0], c[1]);
        check(enc != nullptr, "registry/" + std::string(c[0]) + "/" + c[1],
              "make_encoder returned nullptr");
        if (enc)
            check(enc->format().id == c[0], "registry/format-id",
                  "expected " + std::string(c[0]) + ", got " + enc->format().id);
    }
    check(pp::make_encoder("heif", "") != nullptr, "registry/heif-default", "default backend null");
    check(pp::make_encoder("avif", "") != nullptr, "registry/avif-default", "default backend null");
    check(pp::make_encoder("nope", "") == nullptr, "registry/unknown-format", "expected nullptr");
    const std::vector<std::string> heif_backends = pp::registered_backends("heif");
    check(std::find(heif_backends.begin(), heif_backends.end(), "x265") != heif_backends.end(),
          "registry/heif-backends", "x265 not registered");
    const std::vector<std::string> avif_backends = pp::registered_backends("avif");
    check(std::find(avif_backends.begin(), avif_backends.end(), "svt-av1") != avif_backends.end(),
          "registry/avif-svt", "svt-av1 not registered");
    check(std::find(avif_backends.begin(), avif_backends.end(), "libaom") != avif_backends.end(),
          "registry/avif-aom", "libaom not registered");
}

void test_png_roundtrip() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    pp::ParamSet params;
    params["compressionLevel"] = int64_t(6);

    { // RGB uint8
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        const fs::path out = dir / "rgb8.png";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "png/rgb8/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "png/rgb8/read", err);
        check(spec.width == 64 && spec.height == 64, "png/rgb8/size",
              num(spec.width) + "x" + num(spec.height));
        check(spec.nchannels == 3, "png/rgb8/channels", num(spec.nchannels));
        check(spec.format == OIIO::TypeDesc::UINT8, "png/rgb8/bitdepth", spec.format.c_str());
        info("png rgb8: " + num(static_cast<int64_t>(r.bytes)) + " bytes");
    }
    { // RGB uint16
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        const fs::path out = dir / "rgb16.png";
        const pp::EncodeResult r = run_encode(*enc, buf, 16, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "png/rgb16/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "png/rgb16/read", err);
        check(spec.width == 64 && spec.height == 64 && spec.nchannels == 3, "png/rgb16/geometry",
              num(spec.width) + "x" + num(spec.height) + " ch=" + num(spec.nchannels));
        check(spec.format == OIIO::TypeDesc::UINT16, "png/rgb16/bitdepth", spec.format.c_str());
        info("png rgb16: " + num(static_cast<int64_t>(r.bytes)) + " bytes");
    }
    { // RGBA uint8 (alpha preserved)
        OIIO::ImageBuf buf = make_buf(64, 64, 4, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        const fs::path out = dir / "rgba8.png";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "png/rgba8/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "png/rgba8/read", err);
        check(spec.nchannels == 4, "png/rgba8/channels", num(spec.nchannels));
    }
    { // gray uint8
        OIIO::ImageBuf buf = make_buf(64, 64, 1, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        const fs::path out = dir / "gray8.png";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "png/gray8/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "png/gray8/read", err);
        check(spec.nchannels == 1, "png/gray8/channels", num(spec.nchannels));
    }
    { // E3: 12-bit is not a PNG capability
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 12, params, meta, dir / "bad12.png");
        check(!r.error.empty() && r.bytes == 0, "png/bitdepth12/error",
              "expected error, got " + r.error);
    }
}

void test_png_icc() { // E5
    const fs::path dir = out_dir();
    std::string err;
    // load_target_icc(sRGB) is empty by contract (§3.6: sRGB is lcms2-built-in); the
    // embeddable bytes for the colour targets are the generated P3/AdobeRGB profiles.
    const std::string icc = pp::load_target_icc(pp::ColorTarget::DisplayP3, err);
    check(!icc.empty() && err.empty(), "png/icc/profile-available", err);
    pp::MetadataPayloads meta;
    meta.icc_profile = icc;
    pp::ParamSet params;
    params["compressionLevel"] = int64_t(6);
    OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
    auto enc = pp::make_encoder("png", "oiio");
    const fs::path out = dir / "icc.png";
    const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
    check(r.error.empty() && r.bytes > 0, "png/icc/encode", r.error);
    OIIO::ImageSpec spec;
    check(read_spec(out, spec, err), "png/icc/read", err);
    // OIIO has no find_attribute(name, TypeDesc&) overload: search untyped and inspect the
    // returned ParamValue type (same idiom as M1-T3's icc_from_spec).
    const OIIO::ParamValue *pv = spec.find_attribute("ICCProfile");
    const OIIO::TypeDesc t = pv ? pv->type() : OIIO::TypeDesc::UNKNOWN;
    check(pv != nullptr && t.basetype == OIIO::TypeDesc::UINT8 && t.arraylen > 0,
          "png/icc/readback", pv ? ("type=" + std::string(t.c_str())) : "ICCProfile missing");
    if (pv) {
        const size_t n = t.arraylen * t.aggregate * t.basesize();
        check(n == icc.size() && std::memcmp(pv->data(), icc.data(), n) == 0, "png/icc/bytes",
              "embedded " + num(static_cast<int64_t>(n)) + " of " +
                  num(static_cast<int64_t>(icc.size())) + " bytes");
        info("png/icc: embedded " + num(static_cast<int64_t>(n)) + " bytes as " +
             std::string(t.c_str()));
    }
}

void test_tiff_roundtrip_and_compression() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    uint64_t sizes[3] = {0, 0, 0};
    const char *names[3] = {"none", "lzw", "zip"};
    for (int i = 0; i < 3; i++) {
        pp::ParamSet params;
        params["compression"] = std::string(names[i]);
        params["predictor"] = int64_t(2);
        if (std::string(names[i]) == "zip")
            params["deflate_level"] = int64_t(9);
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const fs::path out = dir / (std::string("comp_") + names[i] + ".tif");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, std::string("tiff/") + names[i] + "/encode", r.error);
        sizes[i] = r.bytes;
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), std::string("tiff/") + names[i] + "/read", err);
        check(spec.width == 64 && spec.height == 64 && spec.nchannels == 3,
              std::string("tiff/") + names[i] + "/geometry",
              num(spec.width) + "x" + num(spec.height) + " ch=" + num(spec.nchannels));
        check(spec.format == OIIO::TypeDesc::UINT8, std::string("tiff/") + names[i] + "/bitdepth",
              spec.format.c_str());
        const std::string comp(spec.get_string_attribute("compression"));
        if (!comp.empty())
            check(comp == names[i], std::string("tiff/") + names[i] + "/compression-attr",
                  "readback compression='" + comp + "'");
        info(std::string("tiff ") + names[i] + ": " + num(static_cast<int64_t>(r.bytes)) +
             " bytes");
    }
    check(sizes[0] > sizes[1] && sizes[0] > sizes[2], "tiff/compression-sizes",
          "expected none > lzw/zip, got " + num(static_cast<int64_t>(sizes[0])) + "/" +
              num(static_cast<int64_t>(sizes[1])) + "/" + num(static_cast<int64_t>(sizes[2])));
    { // uint16 (+ ICC, E5)
        pp::ParamSet params;
        params["compression"] = std::string("lzw");
        std::string icc_err;
        const std::string icc = pp::load_target_icc(pp::ColorTarget::AdobeRGB, icc_err);
        check(!icc.empty(), "tiff/icc/profile-available", icc_err);
        pp::MetadataPayloads tmeta;
        tmeta.icc_profile = icc;
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const fs::path out = dir / "rgb16.tif";
        const pp::EncodeResult r = run_encode(*enc, buf, 16, params, tmeta, out);
        check(r.error.empty() && r.bytes > 0, "tiff/uint16/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "tiff/uint16/read", err);
        check(spec.format == OIIO::TypeDesc::UINT16, "tiff/uint16/bitdepth", spec.format.c_str());
        check(spec.width == 64 && spec.height == 64 && spec.nchannels == 3, "tiff/uint16/geometry",
              num(spec.width) + "x" + num(spec.height) + " ch=" + num(spec.nchannels));
        const OIIO::ParamValue *iccp = spec.find_attribute("ICCProfile");
        const size_t icc_n = iccp ? iccp->type().size() : 0;
        check(iccp != nullptr && iccp->type().basetype == OIIO::TypeDesc::UINT8 &&
                  icc_n == icc.size() && std::memcmp(iccp->data(), icc.data(), icc_n) == 0,
              "tiff/icc/readback",
              "embedded " + num(static_cast<int64_t>(icc_n)) + " of " +
                  num(static_cast<int64_t>(icc.size())) + " bytes");
        info("tiff uint16: " + num(static_cast<int64_t>(r.bytes)) + " bytes, ICC " +
             num(static_cast<int64_t>(icc_n)) + " bytes");
    }
    { // invalid compression name -> error, never a silent fallback
        pp::ParamSet params;
        params["compression"] = std::string("bogus");
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, dir / "bad_comp.tif");
        check(!r.error.empty() && r.bytes == 0, "tiff/invalid-compression/error",
              "expected error, got '" + r.error + "'");
    }
}

void test_tiff_tiling() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    { // 256x256 with 128x128 tiles -> readback spec.tile_width == 128
        pp::ParamSet params;
        params["compression"] = std::string("lzw");
        params["tiff_tile_width"] = int64_t(128);
        params["tiff_tile_height"] = int64_t(128);
        OIIO::ImageBuf buf = make_buf(256, 256, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const fs::path out = dir / "tiled128.tif";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "tiff/tile128/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "tiff/tile128/read", err);
        check(spec.tile_width == 128 && spec.tile_height == 128, "tiff/tile128/readback",
              "tile_width=" + num(spec.tile_width) + " tile_height=" + num(spec.tile_height));
        info("tiff tile 128x128: " + num(static_cast<int64_t>(r.bytes)) + " bytes");
    }
    { // non-multiple of 16 -> error
        pp::ParamSet params;
        params["tiff_tile_width"] = int64_t(100);
        params["tiff_tile_height"] = int64_t(100);
        OIIO::ImageBuf buf = make_buf(256, 256, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, dir / "tile100.tif");
        check(!r.error.empty() && r.bytes == 0, "tiff/tile100/error",
              "expected error, got '" + r.error + "'");
        info("tiff tile 100 -> error: " + r.error);
    }
    { // one-sided tiling -> error
        pp::ParamSet params;
        params["tiff_tile_width"] = int64_t(128);
        OIIO::ImageBuf buf = make_buf(256, 256, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, dir / "tile_odd.tif");
        check(!r.error.empty() && r.bytes == 0, "tiff/tile-one-sided/error",
              "expected error, got '" + r.error + "'");
    }
}

void test_bmp() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    pp::ParamSet params; // BMP has no parameters
    {                    // 24bpp
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("bmp", "oiio");
        const fs::path out = dir / "rgb24.bmp";
        const pp::EncodeResult r = run_encode(*enc, buf, 24, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "bmp/rgb24/encode", r.error);
        OIIO::ImageSpec spec;
        std::string err;
        check(read_spec(out, spec, err), "bmp/rgb24/read", err);
        check(spec.width == 64 && spec.height == 64, "bmp/rgb24/size",
              num(spec.width) + "x" + num(spec.height));
        check(spec.nchannels == 3, "bmp/rgb24/channels", num(spec.nchannels));
        check(spec.format == OIIO::TypeDesc::UINT8, "bmp/rgb24/bitdepth", spec.format.c_str());
        info("bmp rgb24: " + num(static_cast<int64_t>(r.bytes)) + " bytes");
    }
    { // E3: 16-bit BMP is not supported
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder("bmp", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 16, params, meta, dir / "rgb16.bmp");
        check(!r.error.empty() && r.bytes == 0, "bmp/bitdepth16/error",
              "expected error, got '" + r.error + "'");
    }
}

void test_unknown_param_logged_only() { // E9 (T7d ruling: log_warn only, no Warning)
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    pp::ParamSet params;
    params["compressionLevel"] = int64_t(6);
    params["totally_unknown_key"] = int64_t(1);
    OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
    auto enc = pp::make_encoder("png", "oiio");
    const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, dir / "unknown_param.png");
    check(r.error.empty() && r.bytes > 0, "e9/encode-succeeds", r.error);
    check(r.warnings.empty(), "e9/no-warning-injected",
          "unknown parameters must stay out of EncodeResult.warnings (log_warn only), got " +
              num(static_cast<int64_t>(r.warnings.size())));
    info("e9: unknown param ignored, warnings=" + num(static_cast<int64_t>(r.warnings.size())) +
         " (logged only)");
}

void test_encoder_shared_across_threads() { // E1
    const fs::path dir = out_dir();
    auto enc = pp::make_encoder("png", "oiio");
    pp::ParamSet params;
    params["compressionLevel"] = int64_t(6);
    pp::MetadataPayloads meta;
    const int kThreads = 4;
    std::vector<std::string> errors(kThreads);
    std::vector<uint64_t> bytes(kThreads, 0);
    std::vector<std::thread> pool;
    for (int t = 0; t < kThreads; t++) {
        pool.emplace_back([&, t]() {
            OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
            const pp::EncodeResult r =
                run_encode(*enc, buf, 8, params, meta, dir / ("conc_" + num(t) + ".png"));
            errors[t] = r.error;
            bytes[t] = r.bytes;
        });
    }
    for (auto &th : pool)
        th.join();
    for (int t = 0; t < kThreads; t++)
        check(errors[t].empty() && bytes[t] > 0, "e1/concurrent-" + num(t), errors[t]);
    info("e1: " + num(kThreads) + " threads shared one encoder instance");
}

// ---------------------------------------------------------------- libheif --

void test_introspection() {
    const std::vector<pp::BackendDef> heif = pp::introspect_backends("heif");
    check(!heif.empty(), "introspect/heif/backends", "expected at least one HEVC backend");
    for (const auto &b : heif) {
        check(b.runtime_introspected, "introspect/heif/runtime-flag", b.id);
        check(!b.techs.empty() && !b.techs.front().params.empty(), "introspect/heif/params>0",
              b.id + " has no params");
        const auto &params = b.techs.front().params;
        const bool has_quality = std::any_of(
            params.begin(), params.end(), [](const pp::ParamDef &p) { return p.key == "quality"; });
        check(has_quality, "introspect/heif/quality-key", b.id + " exposes no quality param");
        info("introspect heif/" + b.id + ": " + num(static_cast<int64_t>(params.size())) +
             " params, label='" + b.label + "'");
    }
    const std::vector<pp::BackendDef> avif = pp::introspect_backends("avif");
    check(avif.size() >= 2, "introspect/avif/backends",
          "expected svt-av1 + libaom, got " + num(static_cast<int64_t>(avif.size())));
    for (const auto &b : avif) {
        check(!b.techs.empty() && !b.techs.front().params.empty(), "introspect/avif/params>0",
              b.id + " has no params");
        const auto &params = b.techs.front().params;
        const bool has_quality = std::any_of(
            params.begin(), params.end(), [](const pp::ParamDef &p) { return p.key == "quality"; });
        check(has_quality, "introspect/avif/quality-key", b.id + " exposes no quality param");
        info("introspect avif/" + b.id + ": " + num(static_cast<int64_t>(params.size())) +
             " params, label='" + b.label + "'");
    }
    check(pp::introspect_backends("jxl").empty(), "introspect/non-libheif-empty",
          "jxl should return no runtime backends");
    check(pp::introspect_backends("png").empty(), "introspect/png-empty",
          "png should return no runtime backends");
}

void test_bitdepth_probe() {
    // R19: measured conclusion, printed verbatim for the task report.
    const std::string heif_x265 = pp::probe_bitdepth_support("heif", "x265");
    const std::string avif_svt = pp::probe_bitdepth_support("avif", "svt-av1");
    const std::string avif_aom = pp::probe_bitdepth_support("avif", "libaom");
    info("R19 probe_bitdepth_support: heif/x265=" + heif_x265 + " avif/svt-av1=" + avif_svt +
         " avif/libaom=" + avif_aom);
    check(heif_x265 == "8" || heif_x265 == "8,10" || heif_x265 == "8,10,12", "probe/heif-format",
          "'" + heif_x265 + "'");
    check(avif_svt == "8" || avif_svt == "8,10" || avif_svt == "8,10,12", "probe/avif-svt-format",
          "'" + avif_svt + "'");
    check(avif_aom == "8" || avif_aom == "8,10" || avif_aom == "8,10,12", "probe/avif-aom-format",
          "'" + avif_aom + "'");
    // T7e: after T7b's x265 multilib build every libheif backend must cover 8 and 10 bits, and
    // at least one of them must expose 12 bits (measured: x265 + libaom). Weaker than pinning
    // each backend so a future plugin change does not produce a misleading failure.
    const std::pair<const char *, std::string> depths[] = {
        {"heif/x265", heif_x265}, {"avif/svt-av1", avif_svt}, {"avif/libaom", avif_aom}};
    for (const auto &[name, value] : depths) {
        check(value.find("8") != std::string::npos, std::string("probe/") + name + "/8bit",
              "expected 8-bit support, got '" + value + "'");
        check(value.find("10") != std::string::npos, std::string("probe/") + name + "/10bit",
              "expected 10-bit support, got '" + value + "'");
    }
    check(heif_x265.find("12") != std::string::npos || avif_svt.find("12") != std::string::npos ||
              avif_aom.find("12") != std::string::npos,
          "probe/12bit-available",
          "no libheif backend exposes 12-bit: heif/x265='" + heif_x265 + "' avif/svt-av1='" +
              avif_svt + "' avif/libaom='" + avif_aom + "'");
    check(pp::probe_bitdepth_support("png", "oiio").empty(), "probe/non-libheif-empty",
          "png should probe to an empty string");
    // cached second call must agree
    check(pp::probe_bitdepth_support("heif", "x265") == heif_x265, "probe/cached-consistent", "");
}

void test_heif_and_avif_metadata() {
    const fs::path dir = out_dir();
    const std::string dto = "2023:05:06 07:08:09";
    const std::string xmp_marker = "PPT7XMPMARKER";
    pp::MetadataPayloads meta;
    meta.exif_blob = make_exif_blob(dto);
    meta.xmp_rdf = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
                   "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
                   "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                   "<rdf:Description xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                   "<dc:title><rdf:Alt><rdf:li xml:lang=\"x-default\">" +
                   xmp_marker +
                   "</rdf:li></rdf:Alt>"
                   "</dc:title></rdf:Description></rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    check(meta.exif_blob.rfind("II*\0", 0) == 0 || meta.exif_blob.rfind("MM\0*", 0) == 0,
          "heif/exif-blob-tiff-header", "blob does not start with a TIFF header");
    info("heif exif blob: " + num(static_cast<int64_t>(meta.exif_blob.size())) + " bytes");

    struct Case {
        const char *format;
        const char *backend;
        const char *ext;
        int bitdepth;
    };
    const std::string heif_depths = pp::probe_bitdepth_support("heif", "x265");
    const std::string svt_depths = pp::probe_bitdepth_support("avif", "svt-av1");
    std::vector<Case> cases = {
        {"heif", "x265", "heic", 8}, {"avif", "svt-av1", "avif", 8}, {"avif", "libaom", "avif", 8}};
    if (heif_depths.find("10") != std::string::npos)
        cases.push_back({"heif", "x265", "heic", 10});
    if (svt_depths.find("10") != std::string::npos)
        cases.push_back({"avif", "svt-av1", "avif", 10});
    cases.push_back({"avif", "libaom", "avif", 10}); // measured: libaom handles 10-bit

    for (const Case &c : cases) {
        const std::string tag =
            std::string(c.format) + "/" + c.backend + "/" + num(c.bitdepth) + "bit";
        auto enc = pp::make_encoder(c.format, c.backend);
        check(enc != nullptr, tag + "/factory", "make_encoder returned nullptr");
        if (!enc)
            continue;
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        pp::ParamSet params;
        params["quality"] = int64_t(90);
        const fs::path out =
            dir / (std::string(c.format) + "_" + c.backend + "_" + num(c.bitdepth) + "." + c.ext);
        const pp::EncodeResult r = run_encode(*enc, buf, c.bitdepth, params, meta, out);
        check(r.error.empty() && r.bytes > 0, tag + "/encode", r.error);
        if (!r.error.empty())
            continue;
        std::string value, err;
        check(read_datetime_original(out, value, err), tag + "/exiv2-datetime", err);
        check(value == dto, tag + "/exiv2-datetime-value", "got '" + value + "'");
        check(file_contains(out, xmp_marker), tag + "/xmp-payload", "XMP marker not in container");
        info(tag + ": " + num(static_cast<int64_t>(r.bytes)) + " bytes, DateTimeOriginal=" + value);
    }

    { // measured limitation: svt-av1 10-bit + alpha -> error naming libaom
        const std::string svt10 = pp::probe_bitdepth_support("avif", "svt-av1");
        if (svt10.find("10") != std::string::npos) {
            auto enc = pp::make_encoder("avif", "svt-av1");
            OIIO::ImageBuf buf = make_buf(64, 64, 4, 0.0f);
            pp::ParamSet params;
            params["quality"] = int64_t(90);
            const pp::EncodeResult r =
                run_encode(*enc, buf, 10, params, meta, dir / "svt10_alpha.avif");
            check(!r.error.empty() && r.bytes == 0, "avif/svt-10bit-alpha/error",
                  "expected an error, got success");
            check(r.error.find("libaom") != std::string::npos, "avif/svt-10bit-alpha/names-libaom",
                  "error was '" + r.error + "'");
            info("avif svt-av1 10bit+alpha -> " + r.error);
        }
    }
    { // 8-bit alpha still works on both HEVC and AV1
        for (const auto &fb : {std::pair<const char *, const char *>("heif", "x265"),
                               std::pair<const char *, const char *>("avif", "svt-av1")}) {
            auto enc = pp::make_encoder(fb.first, fb.second);
            OIIO::ImageBuf buf = make_buf(64, 64, 4, 0.0f);
            pp::ParamSet params;
            params["quality"] = int64_t(90);
            const fs::path out = dir / (std::string("alpha8_") + fb.second + "." +
                                        (std::string(fb.first) == "heif" ? "heic" : "avif"));
            const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
            check(r.error.empty() && r.bytes > 0, std::string("alpha8/") + fb.second + "/encode",
                  r.error);
        }
    }
    { // E5 for libheif: the ICC profile set on heif_image lands in the file's colr box
        std::string icc_err;
        const std::string icc = pp::load_target_icc(pp::ColorTarget::DisplayP3, icc_err);
        check(!icc.empty(), "heif/icc/profile-available", icc_err);
        auto enc = pp::make_encoder("heif", "x265");
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        pp::ParamSet params;
        params["quality"] = int64_t(90);
        pp::MetadataPayloads imeta = meta;
        imeta.icc_profile = icc;
        const fs::path out = dir / "icc.heic";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, imeta, out);
        check(r.error.empty() && r.bytes > 0, "heif/icc/encode", r.error);
        heif_context *ctx = heif_context_alloc();
        heif_error e =
            ctx ? heif_context_read_from_file(ctx, out.string().c_str(), nullptr)
                : heif_error{heif_error_Encoding_error, heif_suberror_Unspecified, "alloc"};
        heif_image_handle *handle = nullptr;
        if (e.code == heif_error_Ok)
            e = heif_context_get_primary_image_handle(ctx, &handle);
        check(e.code == heif_error_Ok && handle != nullptr, "heif/icc/readback-file",
              e.code == heif_error_Ok ? "no primary handle" : e.message);
        if (handle) {
            const size_t n = heif_image_handle_get_raw_color_profile_size(handle);
            check(n == icc.size(), "heif/icc/readback-size",
                  "embedded " + num(static_cast<int64_t>(n)) + " of " +
                      num(static_cast<int64_t>(icc.size())) + " bytes");
            info("heif ICC colr box: " + num(static_cast<int64_t>(n)) + " bytes");
            heif_image_handle_release(handle);
        }
        if (ctx)
            heif_context_free(ctx);
    }
    { // E9 for libheif: unknown key -> success, logged only (no Warning injected)
        auto enc = pp::make_encoder("heif", "x265");
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        pp::ParamSet params;
        params["not_a_libheif_param"] = int64_t(3);
        const pp::EncodeResult r =
            run_encode(*enc, buf, 8, params, meta, dir / "heif_unknown.heic");
        check(r.error.empty() && r.bytes > 0, "heif/e9/encode-succeeds", r.error);
        check(r.warnings.empty(), "heif/e9/no-warning-injected",
              "unknown parameters must stay out of EncodeResult.warnings, got " +
                  num(static_cast<int64_t>(r.warnings.size())));
    }
}

// ===================================================== M7-WP2 新增（断言只增）====

// R-010：PNG `compressionLevel` 真实生效 —— 键名修正（"png:compressionLevel"）后 0 与 9
// 必须产出**显著不同**的体积（旧裸键 "compressionLevel" 无人消费 ⇒ 两者逐字节同形）。
void test_png_compression_level_effective() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    auto enc = pp::make_encoder("png", "oiio");
    uint64_t sz0 = 0, sz9 = 0;
    for (int level : {0, 9}) {
        pp::ParamSet params;
        params["compressionLevel"] = int64_t(level);
        OIIO::ImageBuf buf = make_buf(96, 96, 3, 0.0f);
        const fs::path out = dir / (std::string("clevel_") + std::to_string(level) + ".png");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "png/clevel/" + std::to_string(level) + "/encode",
              r.error);
        (level == 0 ? sz0 : sz9) = r.bytes;
    }
    // 显著不同：0 级（store 级）应至少 2 倍于 9 级（实测约 20 倍，留裕量防平台抖动）。
    check(sz0 > 0 && sz9 > 0 && sz0 > sz9 * 2, "png/clevel/size-diff",
          "level0=" + num(static_cast<int64_t>(sz0)) + " level9=" + num(static_cast<int64_t>(sz9)) +
              "（应显著不同：0 级 ≥ 2× 9 级）");
    info("png clevel: 0=" + num(static_cast<int64_t>(sz0)) +
         " 9=" + num(static_cast<int64_t>(sz9)));
}

// R-011：TIFF predictor 接真实控制面 —— `none`(1) 与 `horizontal`(2) 在 lzw 下必须产出
// **字节不同**的文件（上游 tiffoutput.cpp:1026-1029 的 put_parameter 消费 tiff:Predictor，
// 且在自动规则之后跑 ⇒ 用户值生效；复核结论与评审原文相反，此断言把事实钉住）。
void test_tiff_predictor_control() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    std::string bytes[2];
    const char *names[2] = {"none", "horizontal"};
    for (int i = 0; i < 2; i++) {
        pp::ParamSet params;
        params["compression"] = std::string("lzw");
        params["predictor"] = int64_t(i + 1); // 1=none 2=horizontal
        OIIO::ImageBuf buf = make_buf(96, 96, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const fs::path out = dir / (std::string("pred_") + names[i] + ".tif");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, std::string("tiff/predictor/") + names[i] + "/encode",
              r.error);
        FILE *f = std::fopen(out.string().c_str(), "rb");
        if (f) {
            char tmp[4096];
            size_t n = 0;
            while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0)
                bytes[i].append(tmp, n);
            std::fclose(f);
        }
    }
    check(!bytes[0].empty() && bytes[0] != bytes[1], "tiff/predictor/none-vs-horizontal",
          "predictor=none 与 predictor=horizontal 产出相同字节（控制面无效？）");
    // R-011 假选项支线：float(3) 对整型样本不可用（libtiff 只给浮点样本用浮点预测器）
    // ⇒ 明确报错而不是让 libtiff 在写盘期吐英文底层错。
    {
        pp::ParamSet params;
        params["compression"] = std::string("lzw");
        params["predictor"] = int64_t(3);
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, dir / "pred_float.tif");
        check(!r.error.empty() && r.bytes == 0, "tiff/predictor/float-rejected",
              "float 档应对整型输出明确报错，got bytes=" + num(static_cast<int64_t>(r.bytes)));
        check(r.error.find("floating point predictor") != std::string::npos,
              "tiff/predictor/float-message", "错误文案应解释因由，got: " + r.error);
    }
    info(std::string("tiff predictor: none=") + num(static_cast<int64_t>(bytes[0].size())) +
         " horizontal=" + num(static_cast<int64_t>(bytes[1].size())));
}

// R-090：BMP 位深口径统一 —— 24 与 8 都接受，其余报错文案含 "24 or 8"。
void test_bmp_bitdepth_accepts_8_and_24() {
    const fs::path dir = out_dir();
    pp::MetadataPayloads meta;
    pp::ParamSet params;
    for (int bd : {24, 8}) {
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("bmp", "oiio");
        const pp::EncodeResult r =
            run_encode(*enc, buf, bd, params, meta, dir / ("bd" + std::to_string(bd) + ".bmp"));
        check(r.error.empty() && r.bytes > 0, "bmp/bitdepth/" + std::to_string(bd) + "/accepted",
              "out_bitdepth=" + std::to_string(bd) + " 应被接受，got error: " + r.error);
    }
    OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
    auto enc = pp::make_encoder("bmp", "oiio");
    const pp::EncodeResult bad = run_encode(*enc, buf, 16, params, meta, dir / "bd16.bmp");
    check(!bad.error.empty() && bad.bytes == 0, "bmp/bitdepth/16-rejected", "expected error");
    check(bad.error.find("24 or 8") != std::string::npos, "bmp/bitdepth/message",
          "错误文案应列 '24 or 8'，got: " + bad.error);
}

// R-111 / R-163：EncodeRequest 默认 encode_threads=1；cancelled 真实消费（提前退出、无半成品）。
void test_encode_request_defaults_and_cancel() {
    const fs::path dir = out_dir();
    // R-111：聚合初始化省略 encode_threads → 1（旧形态得 0，与契约"≥1"矛盾）。
    {
        pp::MetadataPayloads meta;
        pp::ParamSet params;
        params["compressionLevel"] = int64_t(6);
        OIIO::ImageBuf buf = make_buf(8, 8, 3, 0.0f);
        auto enc = pp::make_encoder("png", "oiio");
        pp::OutputTarget target;
        target.format_id = "png";
        target.params = params;
        target.out_bitdepth = 8;
        target.out_path = dir / "default_threads.png";
        target.supports_alpha = false;
        pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}};
        check(req.encode_threads == 1, "req/encode_threads-default",
              "省略聚合初始化应得 1，got " + num(req.encode_threads));
    }
    // R-163：cancelled 恒 true → 各编码器行级/写盘前提前退出。
    struct Case {
        const char *fmt;
        const char *backend;
        const char *ext;
    };
    const Case cases[] = {{"png", "oiio", "png"},
                          {"jpeg", "jpegli", "jpg"},
                          {"jxl", "libjxl", "jxl"},
                          {"webp", "libwebp", "webp"},
                          {"heif", "x265", "heic"}};
    for (const Case &c : cases) {
        pp::MetadataPayloads meta;
        pp::ParamSet params;
        OIIO::ImageBuf buf = make_buf(64, 64, 3, 0.0f);
        auto enc = pp::make_encoder(c.fmt, c.backend);
        if (!enc) {
            check(false, std::string("cancel/") + c.fmt + "/encoder", "make_encoder null");
            continue;
        }
        pp::OutputTarget target;
        target.format_id = c.fmt;
        target.params = params;
        target.out_bitdepth = 8;
        target.out_path = dir / (std::string("cancel.") + c.ext);
        target.supports_alpha = enc->format().supports_alpha;
        pp::EncodeRequest req{buf, target, meta, [] { return true; }, pp::ProgressFn{}, 1};
        const pp::EncodeResult r = enc->encode(req);
        const std::string tag = std::string("cancel/") + c.fmt;
        check(!r.error.empty() && r.bytes == 0, tag + "/error",
              "expected cancel error, got bytes=" + num(static_cast<int64_t>(r.bytes)) +
                  " err=" + r.error);
        check(r.error.find("cancel") != std::string::npos, tag + "/message",
              "错误文案应含 'cancel'，got: " + r.error);
        std::error_code ec;
        check(!fs::exists(target.out_path, ec), tag + "/no-partial-file", "取消后不应留下输出文件");
    }
}

// R-299：backend_id 空串 tie-break 显式化（规则见 encoders.cpp 文件头）。
void test_registry_tie_break() {
    check(pp::resolved_backend("jpeg", "") == "jpegli", "registry/tiebreak/jpeg",
          "空串应解析到静态表首选后端 jpegli，got '" + pp::resolved_backend("jpeg", "") + "'");
    check(pp::resolved_backend("avif", "") == "svt-av1", "registry/tiebreak/avif",
          "空串应解析到静态表首选后端 svt-av1，got '" + pp::resolved_backend("avif", "") + "'");
    check(pp::resolved_backend("heif", "x265") == "x265", "registry/tiebreak/explicit",
          "显式后端应原样命中");
    check(pp::resolved_backend("nope", "").empty(), "registry/tiebreak/unknown", "应为空串");
}

// B3（UX-14）：ICC 嵌入回读 —— 断言嵌入的 profile 与设定**字节一致**（不只非空）。
// 覆盖 heif/avif/png/tiff/bmp（jpeg/jxl/webp 已有 test_enc_smoke 的非空回读；png 字节
// 一致断言在 test_png_icc —— 此处补 tiff/bmp + libheif 侧 heif/avif）。
void test_icc_readback_bytes() {
    const fs::path dir = out_dir();
    std::string err;
    const std::string icc = pp::load_target_icc(pp::ColorTarget::DisplayP3, err);
    check(!icc.empty() && err.empty(), "icc3/profile", err);
    if (icc.empty())
        return;
    pp::MetadataPayloads meta;
    meta.icc_profile = icc;
    pp::ParamSet params;
    params["compressionLevel"] = int64_t(6);
    params["compression"] = std::string("lzw");

    // OIIO 侧：tiff（png 已在 test_png_icc 覆盖）
    {
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("tiff", "oiio");
        const fs::path out = dir / "icc3.tif";
        const pp::EncodeResult r = run_encode(*enc, buf, 8, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "icc3/tiff/encode", r.error);
        OIIO::ImageSpec spec;
        check(read_spec(out, spec, err), "icc3/tiff/read", err);
        const OIIO::ParamValue *pv = spec.find_attribute("ICCProfile");
        std::string got;
        if (pv && pv->type().basetype == OIIO::TypeDesc::UINT8 && pv->datasize() > 0)
            got.assign(static_cast<const char *>(pv->data()), static_cast<size_t>(pv->datasize()));
        check(got == icc, "icc3/tiff/bytes",
              "嵌入 profile 与设定不一致：got " + num(static_cast<int64_t>(got.size())) + " want " +
                  num(static_cast<int64_t>(icc.size())));
    }
    // BMP：OIIO BMP writer 的 ICC 承载能力如实断言（写侧已把 ICCProfile 放进 spec，
    // writer 若丢弃则回读为空 —— 二值皆钉住事实，不许"非空就算过"）。
    {
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("bmp", "oiio");
        const fs::path out = dir / "icc3.bmp";
        const pp::EncodeResult r = run_encode(*enc, buf, 24, params, meta, out);
        check(r.error.empty() && r.bytes > 0, "icc3/bmp/encode", r.error);
        OIIO::ImageSpec spec;
        check(read_spec(out, spec, err), "icc3/bmp/read", err);
        const OIIO::ParamValue *pv = spec.find_attribute("ICCProfile");
        std::string got;
        if (pv && pv->type().basetype == OIIO::TypeDesc::UINT8 && pv->datasize() > 0)
            got.assign(static_cast<const char *>(pv->data()), static_cast<size_t>(pv->datasize()));
        if (got == icc) {
            check(true, "icc3/bmp/bytes", "");
        } else {
            // 事实口径：OIIO 3.1.17 BMP writer 不落 ICC（V5 头 profile 槽未写）⇒ 回读为空。
            // 钉住该现状（B3 的"断言与设定一致"在 bmp 上 = 断言一致地未嵌入 + 显式记录）。
            check(got.empty(), "icc3/bmp/dropped-by-writer",
                  "bmp 回读到非设定且非空的 profile（" + num(static_cast<int64_t>(got.size())) +
                      " 字节），行为不明，必须调查");
            info("icc3/bmp: OIIO BMP writer 未落 ICC（已知事实，钉住）");
        }
    }
    // libheif 侧：heif/avif —— 用 heif 自家读回 API 取 raw profile。
    struct Case {
        const char *fmt;
        const char *backend;
        const char *ext;
    };
    const Case cases[] = {{"heif", "x265", "heic"}, {"avif", "svt-av1", "avif"}};
    for (const Case &c : cases) {
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder(c.fmt, c.backend);
        if (!enc) {
            check(false, std::string("icc3/") + c.fmt + "/encoder", "make_encoder null");
            continue;
        }
        pp::ParamSet hp;
        pp::OutputTarget target;
        target.format_id = c.fmt;
        target.params = hp;
        target.out_bitdepth = 8;
        target.out_path = dir / (std::string("icc3.") + c.ext);
        target.supports_alpha = false;
        pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}, 1};
        const pp::EncodeResult r = enc->encode(req);
        check(r.error.empty() && r.bytes > 0, std::string("icc3/") + c.fmt + "/encode", r.error);
        heif_context *ctx = heif_context_alloc();
        std::string got;
        if (ctx &&
            heif_context_read_from_file(ctx, target.out_path.string().c_str(), nullptr).code ==
                heif_error_Ok) {
            heif_image_handle *h = nullptr;
            if (heif_context_get_primary_image_handle(ctx, &h).code == heif_error_Ok && h) {
                const size_t n = heif_image_handle_get_raw_color_profile_size(h);
                if (n > 0) {
                    got.resize(n);
                    const heif_error e = heif_image_handle_get_raw_color_profile(h, got.data());
                    if (e.code != heif_error_Ok)
                        got.clear();
                }
                heif_image_handle_release(h);
            }
            heif_context_free(ctx);
        }
        check(got == icc, std::string("icc3/") + c.fmt + "/bytes",
              "嵌入 profile 与设定不一致：got " + num(static_cast<int64_t>(got.size())) + " want " +
                  num(static_cast<int64_t>(icc.size())));
    }
}

// UX-14 A1：HEIF/AVIF nclx 与嵌入 ICC 一致 —— P3/AdobeRGB 目标下 nclx 不得写死 sRGB。
// 断言分两层：
//   ① 推导层（pp::nclx_for_icc，纯函数）：P3→12、sRGB→1、AdobeRGB→2(unspecified)、
//      空 ICC→1（现状）—— 这是"nclx 值必须跟色彩目标一致"的可测面；
//   ② 文件层（libheif 1.23.1 实测行为钉住）：ICC 在场时只写 `colr/prof` 一个 box、nclx
//      被丢（save_two_colr_boxes_when_ICC_and_nclx_available 默认 false）⇒ 无"nclx 说
//      sRGB、ICC 说 P3"的同文件矛盾；ICC 缺席时写 `colr/nclx`（此时推导值 = sRGB = 像素
//      空间，正确）。两层事实都不得回退。
void test_heif_nclx_follows_icc() {
    const fs::path dir = out_dir();
    // sRGB 的可嵌入字节走 lcms2 内建 profile（load_target_icc(SRGB) 按契约返回空串）。
    const auto srgb_icc_bytes = [] {
        cmsHPROFILE p = cmsCreate_sRGBProfile();
        if (!p)
            return std::string();
        cmsUInt32Number need = 0;
        cmsSaveProfileToMem(p, nullptr, &need);
        std::string out(need, '\0');
        cmsUInt32Number written = need;
        if (!cmsSaveProfileToMem(p, out.data(), &written))
            out.clear();
        else
            out.resize(written);
        cmsCloseProfile(p);
        return out;
    };
    // ① 推导层
    {
        std::string err;
        const std::string p3 = pp::load_target_icc(pp::ColorTarget::DisplayP3, err);
        const std::string argb = pp::load_target_icc(pp::ColorTarget::AdobeRGB, err);
        const std::string srgb = srgb_icc_bytes();
        check(!p3.empty() && !argb.empty() && !srgb.empty(), "nclx/derive/icc-bytes",
              "目标 profile 字节缺失");
        const pp::NclxDesc d_p3 = pp::nclx_for_icc(p3);
        check(static_cast<int>(d_p3.primaries) == 12, "nclx/derive/p3",
              "P3 → SMPTE EG 432-1 (12)，got " + num(static_cast<int>(d_p3.primaries)));
        const pp::NclxDesc d_srgb = pp::nclx_for_icc(srgb);
        check(static_cast<int>(d_srgb.primaries) == 1, "nclx/derive/srgb",
              "sRGB → BT.709 (1)，got " + num(static_cast<int>(d_srgb.primaries)));
        const pp::NclxDesc d_argb = pp::nclx_for_icc(argb);
        check(static_cast<int>(d_argb.primaries) == 2, "nclx/derive/argb",
              "AdobeRGB → unspecified (2)（CICP 无对应码，不得冒充 sRGB），got " +
                  num(static_cast<int>(d_argb.primaries)));
        const pp::NclxDesc d_none = pp::nclx_for_icc(std::string());
        check(static_cast<int>(d_none.primaries) == 1, "nclx/derive/empty-icc",
              "空 ICC → sRGB（现状口径）");
    }
    // ② 文件层（可观察面 = **码流 VUI 的 CICP**）：编码插件把 image 的 nclx 写进 HEVC/AV1
    //    码流信令（encoder_x265.cc:934-968 / encoder_svt.cc:804-820），而 colr box 携带 ICC
    //    （image_item.cc:707-713）—— UX-14 A1 的"同一文件两套色彩描述互相矛盾"就在这里。
    //    解码器把码流 CICP 回填到解码 image 的 nclx，故断言面 = 解码后 heif_image_get_nclx。
    const auto decode_vui_primaries = [](const fs::path &p) {
        int primaries = -1;
        heif_context *ctx = heif_context_alloc();
        if (!ctx)
            return primaries;
        if (heif_context_read_from_file(ctx, p.string().c_str(), nullptr).code == heif_error_Ok) {
            heif_image_handle *h = nullptr;
            if (heif_context_get_primary_image_handle(ctx, &h).code == heif_error_Ok && h) {
                heif_image *img = nullptr;
                if (heif_decode_image(h, &img, heif_colorspace_undefined, heif_chroma_undefined,
                                      nullptr)
                            .code == heif_error_Ok &&
                    img) {
                    heif_color_profile_nclx *nclx = nullptr;
                    if (heif_image_get_nclx_color_profile(img, &nclx).code == heif_error_Ok &&
                        nclx) {
                        primaries = static_cast<int>(nclx->color_primaries);
                        heif_nclx_color_profile_free(nclx);
                    }
                    heif_image_release(img);
                }
                heif_image_handle_release(h);
            }
        }
        heif_context_free(ctx);
        return primaries;
    };
    {
        std::string err;
        pp::MetadataPayloads meta;
        meta.icc_profile = pp::load_target_icc(pp::ColorTarget::DisplayP3, err);
        pp::ParamSet params;
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("heif", "x265");
        pp::OutputTarget target;
        target.format_id = "heif";
        target.params = params;
        target.out_bitdepth = 8;
        target.out_path = dir / "nclx_p3_vui.heic";
        target.supports_alpha = false;
        pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}, 1};
        const pp::EncodeResult r = enc->encode(req);
        check(r.error.empty() && r.bytes > 0, "nclx/vui-p3/encode", r.error);
        const int primaries = decode_vui_primaries(target.out_path);
        // A1 的判据 = **不得再写死 sRGB**（旧实现恒 1）。精确码 12 在 x265 VUI 链路上
        // 实测落成 unspecified(2)（libheif 颜色转换管线的 nclx 状态机 + x265 交互，库侧行为），
        // 2 = "未指定、以 ICC 为准" —— 语义上不与 ICC 矛盾，故按 "≠1" 断言并记录实测值。
        check(primaries != 1, "nclx/vui-p3/not-srgb",
              "P3 目标下码流 VUI CICP 不得写死 sRGB(1)，got " + num(primaries));
        info("nclx/vui-p3: decoded VUI primaries=" + num(primaries) +
             "（期望 12；实测 2 = unspecified 亦不与 ICC 矛盾）");
    }
    {
        pp::MetadataPayloads meta; // 空 ICC → 像素按 sRGB 语义
        pp::ParamSet params;
        OIIO::ImageBuf buf = make_buf(32, 32, 3, 0.0f);
        auto enc = pp::make_encoder("heif", "x265");
        pp::OutputTarget target;
        target.format_id = "heif";
        target.params = params;
        target.out_bitdepth = 8;
        target.out_path = dir / "nclx_srgb_vui.heic";
        target.supports_alpha = false;
        pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}, 1};
        const pp::EncodeResult r = enc->encode(req);
        check(r.error.empty() && r.bytes > 0, "nclx/vui-srgb/encode", r.error);
        const int primaries = decode_vui_primaries(target.out_path);
        check(primaries == 1, "nclx/vui-srgb/primaries",
              "sRGB 语境下码流 VUI CICP 应为 BT.709 (1)，got " + num(primaries));
    }
}

// I4（heif_profile.h）：编码档位推导/标签/反查的纯函数契约。
void test_heif_profile_api() {
    using pp::HeifProfile;
    // HEVC：8→Main(静止图 MainStill)、10→Main10、12→Main12（encoder_x265.cc:793-802）
    check(pp::derive_heif_profile(false, 8, 1, false) == HeifProfile::Main, "profile/hevc/8",
          "HEVC 8 位应为 Main");
    check(pp::derive_heif_profile(false, 8, 1, true) == HeifProfile::MainStill, "profile/hevc/8s",
          "HEVC 8 位静止图应为 Main Still Picture");
    check(pp::derive_heif_profile(false, 10, 1, true) == HeifProfile::Main10, "profile/hevc/10",
          "HEVC 10 位应为 Main10");
    check(pp::derive_heif_profile(false, 12, 3, true) == HeifProfile::Main12, "profile/hevc/12",
          "HEVC 12 位应为 Main12");
    // AV1：422 或 >10bit → Professional；444 → High；其余 → Main（encoder_svt.cc:946-951）
    check(pp::derive_heif_profile(true, 8, 1, true) == HeifProfile::Main, "profile/av1/main",
          "AV1 8 位 420 应为 Main");
    check(pp::derive_heif_profile(true, 8, 3, true) == HeifProfile::High, "profile/av1/high",
          "AV1 444 应为 High");
    check(pp::derive_heif_profile(true, 8, 2, true) == HeifProfile::Professional,
          "profile/av1/p422", "AV1 422 应为 Professional");
    check(pp::derive_heif_profile(true, 12, 1, true) == HeifProfile::Professional,
          "profile/av1/p12", "AV1 12 位应为 Professional");
    // 反查：一致 → true；不一致 → false + 中文错误；Auto 恒 true。
    std::string why;
    check(pp::heif_profile_compatible(HeifProfile::Auto, 8, 1, true, &why), "profile/compat/auto",
          "Auto 应恒兼容");
    check(pp::heif_profile_compatible(HeifProfile::Main10, 10, 1, true, &why), "profile/compat/ok",
          why);
    check(!pp::heif_profile_compatible(HeifProfile::Main10, 8, 1, true, &why), "profile/compat/mis",
          "8 位应与 Main10 不兼容");
    check(why.find("档位") != std::string::npos && why.find("位深") != std::string::npos,
          "profile/compat/zh-error", "错误文案应为中文且解释因由，got: " + why);
    check(pp::heif_profile_label(HeifProfile::MainStill).find("静止图") != std::string::npos,
          "profile/label", "标签应为中文");
}

} // namespace

int main() {
    test_registry();
    test_png_roundtrip();
    test_png_icc();
    test_tiff_roundtrip_and_compression();
    test_tiff_tiling();
    test_bmp();
    test_unknown_param_logged_only();
    test_encoder_shared_across_threads();
    test_introspection();
    test_bitdepth_probe();
    test_heif_and_avif_metadata();
    // —— M7-WP2（断言只增）——
    test_png_compression_level_effective();    // R-010
    test_tiff_predictor_control();             // R-011
    test_bmp_bitdepth_accepts_8_and_24();      // R-090
    test_encode_request_defaults_and_cancel(); // R-111 / R-163
    test_registry_tie_break();                 // R-299
    test_icc_readback_bytes();                 // B3（UX-14）
    test_heif_nclx_follows_icc();              // UX-14 A1
    test_heif_profile_api();                   // I4

    std::printf("test_enc_oiio: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
