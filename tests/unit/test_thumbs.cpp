// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — probe + thumbnail unit tests (hand-written assertions).
//
// Contract: docs/m1b-tasks.md §2.1 (PP-FROZEN header) / §4.1:
//   * base/rgb8.png @96: rgba size, long edge <= target, from_embedded == false, no upscale;
//   * base/rgb16.tif: probe + thumbnail from the full-decode path;
//   * edge/ corrupt negatives: probe_ok == false with a non-empty error;
//   * meta/exif_full.jpg: probe_ok == true, thumbnail non-empty (from_embedded not asserted);
// plus the frozen §2.1 step-5 behaviours (no upscale, gray -> RGB, alpha -> white) and one
// synthetic fixture whose Exif IFD1 thumbnail exercises the embedded-preview path on real
// bytes (the shipped corpus contains no file with an embedded preview).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <exiv2/exiv2.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/thumbs.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const fs::path &p) { return p.string(); }

std::string dims(const pp::ThumbImage &t) {
    return std::to_string(t.width) + "x" + std::to_string(t.height) +
           " rgba=" + std::to_string(t.rgba.size());
}

// Repository corpus: walk up from the ctest working directory to find tests/golden.
fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec)) {
            return p / "tests" / "golden";
        }
        if (!p.has_parent_path() || p.parent_path() == p) {
            break;
        }
        p = p.parent_path();
    }
    return {};
}

// Scratch under the build dir (ctest runs in the build dir, like test_fsops).
fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

bool rgba_size_matches(const pp::ThumbImage &t) {
    return t.width > 0 && t.height > 0 &&
           t.rgba.size() ==
               static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4;
}

bool alpha_all_opaque(const pp::ThumbImage &t) {
    for (std::size_t i = 3; i < t.rgba.size(); i += 4) {
        if (t.rgba[i] != 255)
            return false;
    }
    return true;
}

bool gray_is_neutral(const pp::ThumbImage &t) {
    for (std::size_t i = 0; i + 3 < t.rgba.size(); i += 4) {
        if (t.rgba[i] != t.rgba[i + 1] || t.rgba[i] != t.rgba[i + 2])
            return false;
    }
    return true;
}

bool all_white(const pp::ThumbImage &t) {
    for (std::uint8_t v : t.rgba) {
        if (v != 255)
            return false;
    }
    return true;
}

// Attach tests/golden/base/photo.jpg as the IFD1 Exif thumbnail of a copy of
// meta/exif_full.jpg, so make_thumbnail() meets a real embedded preview.
fs::path build_preview_fixture(const fs::path &corpus, const fs::path &dir) {
    std::error_code ec;
    const fs::path host = dir / "host_with_preview.jpg";
    fs::copy_file(corpus / "meta" / "exif_full.jpg", host, fs::copy_options::overwrite_existing,
                  ec);
    if (ec)
        return {};
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(host.string());
        if (!image)
            return {};
        image->readMetadata();
        Exiv2::ExifData exif = image->exifData();
        Exiv2::ExifThumb thumb(exif);
        thumb.setJpegThumbnail((corpus / "base" / "photo.jpg").string(), Exiv2::URational(72, 1),
                               Exiv2::URational(72, 1), 2);
        image->setExifData(exif);
        image->writeMetadata();
    } catch (const Exiv2::Error &e) {
        std::printf("FAIL preview/fixture: exiv2: %s\n", e.what());
        ++g_failed;
        return {};
    } catch (...) {
        std::printf("FAIL preview/fixture: unknown exiv2 error\n");
        ++g_failed;
        return {};
    }
    return host;
}

} // namespace

int main() {
    const fs::path corpus = find_corpus();
    check(!corpus.empty(), "corpus/found",
          "tests/golden not found walking up from " + show(fs::current_path()));
    if (corpus.empty()) {
        std::printf("test_thumbs: FAILED (%d)\n", g_failed);
        return g_failed;
    }

    // ---- base/rgb8.png @96: probe ok, no upscale (64x64 source), RGBA size, no preview ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb8.png", 96);
        check(o.probe_ok, "rgb8/probe-ok", "error=" + o.error);
        check(o.error.empty(), "rgb8/probe-no-error", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "rgb8/thumb-non-empty", dims(o.thumb));
        check(o.thumb.width == 64 && o.thumb.height == 64, "rgb8/no-upscale-64x64",
              "expect 64x64, got " + dims(o.thumb));
        check(o.thumb.width <= 96 && o.thumb.height <= 96, "rgb8/long-edge-le-target",
              dims(o.thumb));
        check(rgba_size_matches(o.thumb), "rgb8/rgba-size", dims(o.thumb));
        check(alpha_all_opaque(o.thumb), "rgb8/alpha-composited-opaque", dims(o.thumb));
        check(!o.thumb.from_embedded, "rgb8/from-embedded-false", "expected full decode");
        check(o.info.width == 64 && o.info.height == 64 && o.info.channels == 3 &&
                  o.info.format == "png",
              "rgb8/probe-info",
              "w=" + std::to_string(o.info.width) + " h=" + std::to_string(o.info.height) +
                  " ch=" + std::to_string(o.info.channels) + " fmt=" + o.info.format);
    }

    // ---- base/rgb8.png @32: downscale honours the target ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb8.png", 32);
        check(o.probe_ok && o.thumb.width == 32 && o.thumb.height == 32 &&
                  rgba_size_matches(o.thumb),
              "rgb8/downscale-32x32", "expect 32x32, got " + dims(o.thumb) + " err=" + o.error);
    }

    // ---- base/rgb16.tif @96: 16-bit source, no upscale ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb16.tif", 96);
        check(o.probe_ok, "rgb16/probe-ok", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "rgb16/thumb-non-empty", dims(o.thumb));
        check(o.thumb.width == 64 && o.thumb.height == 64, "rgb16/no-upscale-64x64",
              "expect 64x64, got " + dims(o.thumb));
        check(rgba_size_matches(o.thumb), "rgb16/rgba-size", dims(o.thumb));
        check(o.info.src_bitdepth == 16, "rgb16/probe-bitdepth",
              "src_bitdepth=" + std::to_string(o.info.src_bitdepth));
    }

    // ---- gray -> RGB (§2.1 step 5) ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/gray8.png", 96);
        check(o.probe_ok && rgba_size_matches(o.thumb), "gray8/thumb-shape",
              dims(o.thumb) + " err=" + o.error);
        check(gray_is_neutral(o.thumb), "gray8/gray-to-rgb", "found a non-neutral pixel");
    }

    // ---- alpha -> white composite (§2.1 step 5) ----
    // base/rgba8.png is a checkerboard of fully transparent black and opaque white cells;
    // compositing on white must yield an all-white thumbnail.
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgba8.png", 96);
        check(o.probe_ok && rgba_size_matches(o.thumb), "rgba8/thumb-shape",
              dims(o.thumb) + " err=" + o.error);
        check(o.info.has_alpha, "rgba8/probe-alpha", "has_alpha=false");
        check(all_white(o.thumb), "rgba8/alpha-on-white", "found a non-white pixel");
    }

    // ---- edge/ corrupt negatives: probe fails, no thumbnail, error reported ----
    {
        const char *const bad[] = {"edge/corrupt_trunc.jpg", "edge/corrupt_zero.png"};
        for (const char *rel : bad) {
            const pp::ThumbOutcome o = pp::make_thumbnail(corpus / rel, 96);
            check(!o.probe_ok, std::string("corrupt/") + rel + "/probe-fails",
                  "probe_ok=true for " + std::string(rel));
            check(!o.error.empty(), std::string("corrupt/") + rel + "/error-set",
                  "error was empty for " + std::string(rel));
            check(o.thumb.rgba.empty(), std::string("corrupt/") + rel + "/no-thumb", dims(o.thumb));
        }
    }

    // ---- non-ASCII path survives the Exiv2/OIIO round trip ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "edge" / "测试📸unicode.png", 96);
        check(o.probe_ok && !o.thumb.rgba.empty(), "unicode/probe-and-thumb",
              "err=" + o.error + " " + dims(o.thumb));
    }

    // ---- meta/exif_full.jpg: probe ok; thumbnail required, from_embedded not asserted ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "meta/exif_full.jpg", 96);
        check(o.probe_ok, "exif-jpg/probe-ok", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "exif-jpg/thumb-non-empty", dims(o.thumb));
        check(rgba_size_matches(o.thumb) && o.thumb.width <= 96 && o.thumb.height <= 96,
              "exif-jpg/thumb-shape",
              dims(o.thumb) + " from_embedded=" + (o.thumb.from_embedded ? "true" : "false"));
    }

    // ---- embedded preview path (synthetic Exif IFD1 thumbnail) ----
    {
        const fs::path dir = make_temp_dir("thumbs_preview");
        const fs::path host = build_preview_fixture(corpus, dir);
        if (!host.empty()) {
            const pp::ThumbOutcome o = pp::make_thumbnail(host, 96);
            check(o.probe_ok, "preview/probe-ok", "error=" + o.error);
            check(o.thumb.from_embedded, "preview/from-embedded-true",
                  "expected the Exif IFD1 thumbnail to be used, got " + dims(o.thumb));
            check(o.thumb.width == 64 && o.thumb.height == 64 && rgba_size_matches(o.thumb),
                  "preview/thumb-64x64", "expect 64x64, got " + dims(o.thumb));
            // Downscaling the embedded preview must respect the target too.
            const pp::ThumbOutcome small = pp::make_thumbnail(host, 24);
            check(small.thumb.from_embedded && small.thumb.width == 24 && small.thumb.height == 24,
                  "preview/downscale-24x24", "got " + dims(small.thumb));
        }
    }

    if (g_failed == 0) {
        std::printf("test_thumbs: OK\n");
        return 0;
    }
    std::printf("test_thumbs: FAILED (%d)\n", g_failed);
    return g_failed;
}
