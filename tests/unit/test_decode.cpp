// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T3 — decode layer unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.5 (PP-FROZEN header) / §4.3 / §5.
// Corpus expectations were captured from a live OIIO 3.1.14 probe of
// tests/golden (all 27 fixtures, see the M1-T3 report).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.

#include <OpenImageIO/imageio.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/types.h"
#include "decode/oiio_reader.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num(long long v) { return std::to_string(v); }

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

bool has_warning(const std::vector<pp::Warning>& ws, pp::WarningKind kind) {
    for (const pp::Warning& w : ws) {
        if (w.kind == kind) {
            return true;
        }
    }
    return false;
}

const char* warning_name(pp::WarningKind kind) {
    switch (kind) {
    case pp::WarningKind::DepthDowngrade: return "DepthDowngrade";
    case pp::WarningKind::LossyFromLossless: return "LossyFromLossless";
    case pp::WarningKind::MultipageTruncated: return "MultipageTruncated";
    case pp::WarningKind::AlphaFlattened: return "AlphaFlattened";
    case pp::WarningKind::NoIccAssumeSrgb: return "NoIccAssumeSrgb";
    case pp::WarningKind::MetadataDropped: return "MetadataDropped";
    case pp::WarningKind::TimeFieldMissing: return "TimeFieldMissing";
    case pp::WarningKind::GrayToRgbEncoded: return "GrayToRgbEncoded";
    }
    return "?";
}

enum Expect { Ok = 0, OpenError = 1, Cmyk = 2 };

// All 27 fixtures of tests/golden/{base,edge,meta}, in directory order.
struct Fixture {
    const char* rel;
    int expect;  // Ok / OpenError / Cmyk
    int width, height, channels, bitdepth;
    bool has_alpha, is_multipage, has_icc;
    const char* format;  // OIIO ImageInput::format_name()
};

const Fixture kFixtures[] = {
    // ---- base (16) ----
    {"base/anim.gif", Ok, 64, 64, 4, 8, true, true, false, "gif"},
    {"base/bmp24.bmp", Ok, 64, 64, 3, 8, false, false, false, "bmp"},
    {"base/gray16.png", Ok, 64, 64, 1, 16, false, false, false, "png"},
    {"base/gray16.tif", Ok, 64, 64, 1, 16, false, false, false, "tiff"},
    {"base/gray8.png", Ok, 64, 64, 1, 8, false, false, false, "png"},
    {"base/graya8.png", Ok, 64, 64, 2, 8, true, false, false, "png"},
    {"base/jxl8.jxl", Ok, 64, 64, 3, 8, false, false, true, "jpegxl"},
    {"base/multi.tif", Ok, 64, 64, 3, 8, false, true, false, "tiff"},
    {"base/photo.jpg", Ok, 64, 64, 3, 8, false, false, false, "jpeg"},
    {"base/rgb16.png", Ok, 64, 64, 3, 16, false, false, false, "png"},
    {"base/rgb16.tif", Ok, 64, 64, 3, 16, false, false, false, "tiff"},
    {"base/rgb8.png", Ok, 64, 64, 3, 8, false, false, false, "png"},
    {"base/rgb8.tif", Ok, 64, 64, 3, 8, false, false, false, "tiff"},
    {"base/rgba16.png", Ok, 64, 64, 4, 16, true, false, false, "png"},
    {"base/rgba8.png", Ok, 64, 64, 4, 8, true, false, false, "png"},
    {"base/targa.tga", Ok, 64, 64, 3, 8, false, false, false, "targa"},
    // ---- edge (4) ----
    {"edge/测试📸unicode.png", Ok, 64, 64, 3, 8, false, false, false, "png"},
    {"edge/cmyk.tif", Cmyk, 0, 0, 0, 0, false, false, false, ""},
    {"edge/corrupt_trunc.jpg", OpenError, 0, 0, 0, 0, false, false, false, ""},
    {"edge/corrupt_zero.png", OpenError, 0, 0, 0, 0, false, false, false, ""},
    // ---- meta (7) ----
    {"meta/avif_exif.avif", Ok, 64, 64, 3, 8, false, false, false, "heif"},
    {"meta/cmyk.tif", Cmyk, 0, 0, 0, 0, false, false, false, ""},
    {"meta/exif_full.jpg", Ok, 64, 64, 3, 8, false, false, false, "jpeg"},
    {"meta/heif_exif.heic", Ok, 64, 64, 3, 8, false, false, false, "heif"},
    {"meta/jxl_exif.jxl", Ok, 64, 64, 3, 8, false, false, true, "jpegxl"},
    {"meta/webp_lossless.webp", Ok, 64, 64, 3, 8, false, false, false, "webp"},
    {"meta/webp_lossy.webp", Ok, 64, 64, 3, 8, false, false, false, "webp"},
};

constexpr std::size_t kFixtureCount = sizeof(kFixtures) / sizeof(kFixtures[0]);

// Fixture entries that must expose a non-empty ICC profile (OIIO attribute
// "ICCProfile", 504 bytes each in the current corpus). M4-T2: libjxl 0.12.0 changed
// the composition of the synthesized sRGB approximation profile (it now carries the
// CICP tag; 0.11.2 did not), so the byte count moved 536 -> 504. The profile itself
// is unchanged in kind: CMM "jxl ", 11 standard tags, self-consistent size field.
const char* const kIccFixtures[] = {"base/jxl8.jxl", "meta/jxl_exif.jxl"};

}  // namespace

int main() {
    const fs::path corpus = find_corpus();
    if (corpus.empty()) {
        std::printf("FAIL corpus: tests/golden not found walking up from '%s'\n",
                    fs::current_path().string().c_str());
        return 1;
    }

    // ---- corpus coverage: the table must cover every fixture exactly once ----
    {
        std::vector<std::string> on_disk;
        std::error_code ec;
        for (const char* dir : {"base", "edge", "meta"}) {
            for (const fs::directory_entry& e : fs::directory_iterator(corpus / dir, ec)) {
                if (e.is_regular_file(ec)) {
                    on_disk.push_back(dir + std::string("/") +
                                      e.path().filename().string());
                }
            }
        }
        check(on_disk.size() == 27, "corpus/count",
              "expected 27 fixtures, found " + num(static_cast<long long>(on_disk.size())));
        check(kFixtureCount == 27, "corpus/table",
              "fixture table has " + num(static_cast<long long>(kFixtureCount)) + " entries");
        for (const std::string& rel : on_disk) {
            bool covered = false;
            for (const Fixture& f : kFixtures) {
                if (rel == f.rel) {
                    covered = true;
                    break;
                }
            }
            check(covered, "corpus/coverage", "fixture not covered by table: " + rel);
        }
    }

    // ---- probe: all 27 fixtures ----
    int icc_fixture_count = 0;
    for (const Fixture& f : kFixtures) {
        const fs::path path = corpus / f.rel;
        const std::string name = f.rel;
        const pp::ProbeOutcome po = pp::probe_file(path);

        if (f.expect != Ok) {
            check(!po.error.empty(), "probe/" + name, "expected probe error, got success");
            if (f.expect == Cmyk) {
                check(po.error == "CMYK input is not supported", "probe/" + name,
                      "expected 'CMYK input is not supported', got '" + po.error + "'");
            }
            // The same rejection must hold on the decode path.
            const pp::DecodeOutcome d = pp::decode_float(path, po.info);
            check(!d.error.empty(), "decode/" + name, "expected decode error, got success");
            check(!d.buf.initialized(), "decode/" + name, "failed decode must leave empty buf");
            if (f.expect == Cmyk) {
                check(d.error == "CMYK input is not supported", "decode/" + name,
                      "expected 'CMYK input is not supported', got '" + d.error + "'");
            }
            continue;
        }

        check(po.error.empty(), "probe/" + name, "unexpected probe error: " + po.error);
        check(po.first_spec.has_value(), "probe/" + name, "first_spec must be engaged");
        check(po.info.width == f.width, "probe/" + name,
              "width expect " + num(f.width) + ", got " + num(po.info.width));
        check(po.info.height == f.height, "probe/" + name,
              "height expect " + num(f.height) + ", got " + num(po.info.height));
        check(po.info.channels == f.channels, "probe/" + name,
              "channels expect " + num(f.channels) + ", got " + num(po.info.channels));
        check(po.info.src_bitdepth == f.bitdepth, "probe/" + name,
              "src_bitdepth expect " + num(f.bitdepth) + ", got " + num(po.info.src_bitdepth));
        check(po.info.has_alpha == f.has_alpha, "probe/" + name,
              "has_alpha expect " + std::string(f.has_alpha ? "true" : "false"));
        check(po.info.is_multipage == f.is_multipage, "probe/" + name,
              "is_multipage expect " + std::string(f.is_multipage ? "true" : "false"));
        check(po.info.has_icc == f.has_icc, "probe/" + name,
              "has_icc expect " + std::string(f.has_icc ? "true" : "false"));
        check(po.info.format == f.format, "probe/" + name,
              "format expect '" + std::string(f.format) + "', got '" + po.info.format + "'");
        check(po.first_spec->width == f.width && po.first_spec->height == f.height,
              "probe/" + name, "first_spec dimensions mismatch");

        if (f.has_icc) {
            ++icc_fixture_count;
        }

        // ---- decode: every probeable fixture, float32 ----
        const pp::DecodeOutcome d = pp::decode_float(path, po.info);
        check(d.error.empty(), "decode/" + name, "unexpected decode error: " + d.error);
        check(d.buf.initialized(), "decode/" + name, "buffer must be initialized");
        check(d.buf.pixels_valid(), "decode/" + name, "pixels must be valid");
        check(d.buf.localpixels() != nullptr, "decode/" + name, "expected local pixels");
        check(d.buf.nchannels() == f.channels, "decode/" + name,
              "buffer channels expect " + num(f.channels) + ", got " + num(d.buf.nchannels()));
        check(d.buf.spec().width == f.width && d.buf.spec().height == f.height,
              "decode/" + name, "buffer dimensions mismatch");
        check(d.buf.spec().format == OIIO::TypeDesc::FLOAT, "decode/" + name,
              "expected float32 buffer, got " + std::string(d.buf.spec().format.c_str()));
        check(d.buf.spec().format.size() == 4, "decode/" + name,
              "float32 channel size expect 4 bytes, got " + num(d.buf.spec().format.size()));
        check(has_warning(d.warnings, pp::WarningKind::MultipageTruncated) == f.is_multipage,
              "decode/" + name, "MultipageTruncated warning must match is_multipage");
        if (d.buf.pixels_valid()) {
            std::vector<float> px(static_cast<std::size_t>(d.buf.nchannels()), 0.0f);
            d.buf.getpixel(0, 0, 0, OIIO::span<float>(px.data(), px.size()));
            bool finite = true;
            for (float v : px) {
                finite = finite && std::isfinite(v);
            }
            check(finite, "decode/" + name, "pixel (0,0) must be finite");
        }
    }

    // ---- decode_float: explicit channel/dimension/type cases (§4.3) ----
    {
        struct ChannelCase {
            const char* rel;
            int channels;
        };
        const ChannelCase cases[] = {
            {"base/rgb8.png", 3}, {"base/graya8.png", 2},
            {"base/rgba16.png", 4}, {"base/gray16.png", 1},
        };
        for (const ChannelCase& c : cases) {
            const fs::path path = corpus / c.rel;
            const std::string name = std::string("channels/") + c.rel;
            const pp::ProbeOutcome po = pp::probe_file(path);
            check(po.error.empty() && po.info.channels == c.channels, name,
                  "probe channels expect " + num(c.channels) + ", got " + num(po.info.channels));
            const pp::DecodeOutcome d = pp::decode_float(path, po.info);
            check(d.error.empty(), name, "decode error: " + d.error);
            check(d.buf.nchannels() == c.channels, name,
                  "buffer channels expect " + num(c.channels) + ", got " + num(d.buf.nchannels()));
            check(d.buf.spec().format == OIIO::TypeFloat, name,
                  "TypeFloat check failed: " + std::string(d.buf.spec().format.c_str()));
            check(d.buf.spec().width == 64 && d.buf.spec().height == 64, name,
                  "dimensions expect 64x64, got " + num(d.buf.spec().width) + "x" +
                      num(d.buf.spec().height));
        }
    }

    // ---- multipage/animated: first page + warning ----
    {
        const char* multi[] = {"base/multi.tif", "base/anim.gif"};
        for (const char* rel : multi) {
            const fs::path path = corpus / rel;
            const std::string name = std::string("multipage/") + rel;
            const pp::ProbeOutcome po = pp::probe_file(path);
            check(po.error.empty() && po.info.is_multipage, name,
                  "expected is_multipage=true");
            const bool have_spec = po.first_spec.has_value();
            check(have_spec, name, "probe must yield first_spec: " + po.error);
            check(have_spec && po.first_spec->width == 64 && po.first_spec->height == 64, name,
                  "first page must be the 64x64 page");
            const pp::DecodeOutcome d = pp::decode_float(path, po.info);
            check(d.error.empty(), name, "decode error: " + d.error);
            check(has_warning(d.warnings, pp::WarningKind::MultipageTruncated), name,
                  "expected Warning{MultipageTruncated}");
            check(d.buf.spec().width == 64 && d.buf.spec().height == 64, name,
                  "decoded first page must be 64x64, got " + num(d.buf.spec().width) + "x" +
                      num(d.buf.spec().height));
            for (const pp::Warning& w : d.warnings) {
                std::printf("INFO warning %s: %s\n", name.c_str(), warning_name(w.kind));
            }
        }
        // A single-page input must not warn.
        const pp::ProbeOutcome po = pp::probe_file(corpus / "base/rgb8.png");
        const pp::DecodeOutcome d = pp::decode_float(corpus / "base/rgb8.png", po.info);
        check(d.warnings.empty(), "multipage/base/rgb8.png",
              "single-page input must not warn, got " +
                  num(static_cast<long long>(d.warnings.size())) + " warning(s)");
    }

    // ---- failure paths ----
    {
        const fs::path missing = corpus / "base" / "does_not_exist_12345.png";
        const pp::ProbeOutcome po = pp::probe_file(missing);
        check(!po.error.empty(), "fail/missing-probe", "nonexistent file must set error");
        check(!po.first_spec.has_value(), "fail/missing-probe",
              "nonexistent file must not yield a spec");
        const pp::DecodeOutcome d = pp::decode_float(missing, pp::ImageInfo{});
        check(!d.error.empty(), "fail/missing-decode", "nonexistent file must set error");
        check(!d.buf.initialized(), "fail/missing-decode", "buffer must stay empty");

        // CMYK (edge/cmyk.tif) is already asserted error-by-error in the sweep;
        // assert here that no pixels were produced.
        const fs::path cmyk = corpus / "edge" / "cmyk.tif";
        const pp::ProbeOutcome cp = pp::probe_file(cmyk);
        check(!cp.error.empty(), "fail/cmyk-probe", "CMYK input must set error");
        const pp::DecodeOutcome cd = pp::decode_float(cmyk, cp.info);
        check(!cd.error.empty(), "fail/cmyk-decode", "CMYK input must set error");
        check(cd.error == "CMYK input is not supported", "fail/cmyk-decode",
              "got '" + cd.error + "'");
        check(!cd.buf.initialized(), "fail/cmyk-decode", "CMYK must not produce a buffer");
    }

    // ---- ICC extraction (binary spec attribute) ----
    {
        int found = 0;
        for (const Fixture& f : kFixtures) {
            if (f.expect != Ok) {
                continue;
            }
            const pp::ProbeOutcome po = pp::probe_file(corpus / f.rel);
            if (!po.first_spec.has_value()) {
                check(false, std::string("icc/") + f.rel, "probe failed: " + po.error);
                continue;
            }
            const std::string icc = pp::icc_from_spec(*po.first_spec);
            bool expect_icc = false;
            for (const char* rel : kIccFixtures) {
                if (std::string(f.rel) == rel) {
                    expect_icc = true;
                }
            }
            if (expect_icc) {
                ++found;
                check(!icc.empty(), std::string("icc/") + f.rel,
                      "expected non-empty ICC bytes");
                check(icc.size() == 504, std::string("icc/") + f.rel,
                      "expected 504 ICC bytes, got " + num(static_cast<long long>(icc.size())));
            } else {
                check(icc.empty(), std::string("icc/") + f.rel,
                      "expected no ICC bytes, got " + num(static_cast<long long>(icc.size())));
            }
        }
        std::printf("INFO icc: %d of %d fixtures carry an ICC profile (%s)\n", found,
                    static_cast<int>(kFixtureCount), kIccFixtures[0]);
        check(found == 2, "icc/summary", "expected exactly 2 ICC fixtures, got " + num(found));
        check(icc_fixture_count == found, "icc/summary", "ImageInfo::has_icc disagrees");
        // Attribute absent -> empty.
        const OIIO::ImageSpec empty_spec;
        check(pp::icc_from_spec(empty_spec).empty(), "icc/empty-spec",
              "spec without ICCProfile must yield empty string");
    }

    // ---- orientation_from_spec ----
    {
        const pp::ProbeOutcome tif = pp::probe_file(corpus / "base/rgb8.tif");
        const pp::ProbeOutcome png = pp::probe_file(corpus / "base/rgb8.png");
        if (!tif.first_spec.has_value() || !png.first_spec.has_value()) {
            check(false, "orientation/corpus",
                  "probe failed: " + tif.error + png.error);
        } else {
            check(pp::orientation_from_spec(*tif.first_spec) == 1, "orientation/tiff-attr",
                  "rgb8.tif reports Orientation=1, got " +
                      num(pp::orientation_from_spec(*tif.first_spec)));
            check(pp::orientation_from_spec(*png.first_spec) == 1, "orientation/absent",
                  "missing Orientation attribute must default to 1");
        }

        OIIO::ImageSpec spec;
        check(pp::orientation_from_spec(spec) == 1, "orientation/default",
              "default spec must yield 1");
        for (int v : {1, 2, 3, 4, 5, 6, 7, 8}) {
            spec.attribute("Orientation", v);
            check(pp::orientation_from_spec(spec) == v, "orientation/value-" + num(v),
                  "expect " + num(v) + ", got " + num(pp::orientation_from_spec(spec)));
        }
        spec.attribute("Orientation", 0);
        check(pp::orientation_from_spec(spec) == 1, "orientation/out-of-range-0",
              "0 is outside 1..8 and must yield 1");
        spec.attribute("Orientation", 9);
        check(pp::orientation_from_spec(spec) == 1, "orientation/out-of-range-9",
              "9 is outside 1..8 and must yield 1");
    }

    std::printf("test_decode: %d checks, %d failure(s)\n", g_checks, g_failed);
    return g_failed;
}
