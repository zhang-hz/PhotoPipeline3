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
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <exiv2/exiv2.hpp>
#include <jpegli/encode.h>

#include "core/colormanager.h"
#include "core/pipeline.h"
#include "core/types.h"
#include "decode/oiio_reader.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
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

bool has_warning(const std::vector<pp::Warning> &ws, pp::WarningKind kind) {
    for (const pp::Warning &w : ws) {
        if (w.kind == kind) {
            return true;
        }
    }
    return false;
}

const char *warning_name(pp::WarningKind kind) {
    switch (kind) {
    case pp::WarningKind::DepthDowngrade:
        return "DepthDowngrade";
    case pp::WarningKind::LossyFromLossless:
        return "LossyFromLossless";
    case pp::WarningKind::MultipageTruncated:
        return "MultipageTruncated";
    case pp::WarningKind::AlphaFlattened:
        return "AlphaFlattened";
    case pp::WarningKind::NoIccAssumeSrgb:
        return "NoIccAssumeSrgb";
    case pp::WarningKind::MetadataDropped:
        return "MetadataDropped";
    case pp::WarningKind::TimeFieldMissing:
        return "TimeFieldMissing";
    case pp::WarningKind::GrayToRgbEncoded:
        return "GrayToRgbEncoded";
    case pp::WarningKind::EmbedIccUnsupported:
        return "EmbedIccUnsupported";
    }
    return "?";
}

enum Expect { Ok = 0, OpenError = 1, Cmyk = 2 };

// All 27 fixtures of tests/golden/{base,edge,meta}, in directory order.
struct Fixture {
    const char *rel;
    int expect; // Ok / OpenError / Cmyk
    int width, height, channels, bitdepth;
    bool has_alpha, is_multipage, has_icc;
    const char *format; // OIIO ImageInput::format_name()
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
const char *const kIccFixtures[] = {"base/jxl8.jxl", "meta/jxl_exif.jxl"};

} // namespace

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
        for (const char *dir : {"base", "edge", "meta"}) {
            for (const fs::directory_entry &e : fs::directory_iterator(corpus / dir, ec)) {
                if (e.is_regular_file(ec)) {
                    on_disk.push_back(dir + std::string("/") + e.path().filename().string());
                }
            }
        }
        check(on_disk.size() == 27, "corpus/count",
              "expected 27 fixtures, found " + num(static_cast<long long>(on_disk.size())));
        check(kFixtureCount == 27, "corpus/table",
              "fixture table has " + num(static_cast<long long>(kFixtureCount)) + " entries");
        for (const std::string &rel : on_disk) {
            bool covered = false;
            for (const Fixture &f : kFixtures) {
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
    for (const Fixture &f : kFixtures) {
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
        check(po.first_spec->width == f.width && po.first_spec->height == f.height, "probe/" + name,
              "first_spec dimensions mismatch");

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
        check(d.buf.spec().width == f.width && d.buf.spec().height == f.height, "decode/" + name,
              "buffer dimensions mismatch");
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
            const char *rel;
            int channels;
        };
        const ChannelCase cases[] = {
            {"base/rgb8.png", 3},
            {"base/graya8.png", 2},
            {"base/rgba16.png", 4},
            {"base/gray16.png", 1},
        };
        for (const ChannelCase &c : cases) {
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
        const char *multi[] = {"base/multi.tif", "base/anim.gif"};
        for (const char *rel : multi) {
            const fs::path path = corpus / rel;
            const std::string name = std::string("multipage/") + rel;
            const pp::ProbeOutcome po = pp::probe_file(path);
            check(po.error.empty() && po.info.is_multipage, name, "expected is_multipage=true");
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
            for (const pp::Warning &w : d.warnings) {
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
        for (const Fixture &f : kFixtures) {
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
            for (const char *rel : kIccFixtures) {
                if (std::string(f.rel) == rel) {
                    expect_icc = true;
                }
            }
            if (expect_icc) {
                ++found;
                check(!icc.empty(), std::string("icc/") + f.rel, "expected non-empty ICC bytes");
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
            check(false, "orientation/corpus", "probe failed: " + tif.error + png.error);
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

    // ---- D1（M7-R2A）：JPEG CMYK/YCCK 一致拒收 —— 合成夹具（勿动 tests/golden）----
    {
        namespace fsys = std::filesystem;
        const fsys::path tmp = fsys::temp_directory_path() / "pp-test_decode-m7r2a";
        std::error_code ec;
        fsys::create_directories(tmp, ec);

        // 合成 CMYK JPEG（勿动 tests/golden）：jpegli 写 JCS_CMYK + Adobe APP14 transform=0
        // （libjpeg 语义：jpegli_set_colorspace(JCS_CMYK) 置 write_Adobe_marker）。
        // YCCK 变体 = 只把 APP14 transform 字节 0→2（扫描数据不变，色彩信令变 YCbCrK）。
        auto write_cmyk = [&](const fsys::path &p, bool ycck) -> bool {
            struct jpeg_compress_struct cinfo;
            struct jpeg_error_mgr jerr;
            cinfo.err = jpegli_std_error(&jerr);
            jpegli_create_compress(&cinfo);
            unsigned char *out_buf = nullptr;
            unsigned long out_size = 0;
            jpegli_mem_dest(&cinfo, &out_buf, &out_size);
            cinfo.image_width = 24;
            cinfo.image_height = 16;
            cinfo.input_components = 4;
            cinfo.in_color_space = JCS_CMYK;
            jpegli_set_defaults(&cinfo);
            jpegli_set_colorspace(&cinfo, JCS_CMYK); // Adobe APP14 transform=0（CMYK 信令）
            cinfo.write_Adobe_marker = TRUE;
            jpegli_start_compress(&cinfo, TRUE);
            std::vector<unsigned char> row(std::size_t{24} * 4);
            for (int y = 0; y < 16; ++y) {
                for (int x = 0; x < 24; ++x) {
                    row[static_cast<std::size_t>(x) * 4 + 0] = static_cast<unsigned char>(x * 9);
                    row[static_cast<std::size_t>(x) * 4 + 1] = static_cast<unsigned char>(y * 13);
                    row[static_cast<std::size_t>(x) * 4 + 2] = static_cast<unsigned char>(x + y);
                    row[static_cast<std::size_t>(x) * 4 + 3] = static_cast<unsigned char>(x * y);
                }
                JSAMPROW rows[1] = {row.data()};
                jpegli_write_scanlines(&cinfo, rows, 1);
            }
            jpegli_finish_compress(&cinfo);
            std::vector<char> bytes(reinterpret_cast<char *>(out_buf),
                                    reinterpret_cast<char *>(out_buf) + out_size);
            jpegli_destroy_compress(&cinfo);
            std::free(out_buf);
            if (ycck) {
                // Adobe APP14 段布局：FF EE 00 0E 'A' 'd' 'o' 'b' 'e' <ver:2> <flags0:2>
                // <flags1:2> <transform:1> —— transform 位于段首 FF 起第 15 字节。
                bool patched = false;
                for (std::size_t i = 0; i + 15 < bytes.size(); ++i) {
                    const auto *u = reinterpret_cast<const unsigned char *>(bytes.data());
                    if (u[i] == 0xFF && u[i + 1] == 0xEE && u[i + 2] == 0x00 && u[i + 3] == 0x0E &&
                        u[i + 4] == 'A' && u[i + 5] == 'd' && u[i + 6] == 'o' && u[i + 7] == 'b' &&
                        u[i + 8] == 'e') {
                        bytes[i + 15] = 2; // transform = 2 (YCCK)
                        patched = true;
                        break;
                    }
                }
                if (!patched)
                    return false;
            }
            std::ofstream f(p, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            return f.good();
        };

        const fsys::path cmyk_jpg = tmp / "synth_cmyk.jpg";
        const fsys::path ycck_jpg = tmp / "synth_ycck.jpg";
        check(write_cmyk(cmyk_jpg, false), "d1/synth-cmyk",
              "CMYK JPEG 合成失败（OIIO 4 通道写入）");
        check(write_cmyk(ycck_jpg, true), "d1/synth-ycck",
              "YCCK 信令打补丁失败（APP14 transform）");

        struct Case {
            const char *name;
            fsys::path path;
            const char *colorspace; // OIIO 读回的 jpeg:ColorSpace 事实
        };
        const Case cases[] = {
            {"d1/cmyk", cmyk_jpg, "CMYK"},
            {"d1/ycck", ycck_jpg, "YCbCrK"},
        };
        for (const Case &c : cases) {
            if (!fsys::exists(c.path))
                continue; // 合成失败已在上方断言点名
            // 夹具事实钉住：OIIO 读回的 jpeg:ColorSpace 必须是信令值（YCCK = YCbCrK）。
            auto in = OIIO::ImageInput::open(c.path.string());
            check(in != nullptr, std::string(c.name) + "/open", "合成 JPEG 打不开");
            if (in) {
                const std::string cs = in->spec().get_string_attribute("jpeg:ColorSpace");
                check(cs == c.colorspace, std::string(c.name) + "/fixture-colorspace",
                      "jpeg:ColorSpace = '" + cs + "'，期望 '" + c.colorspace + "'");
                in->close();
            }
            // 拒收口径逐字复用（tools/baseline/golden.log:189 钉住，禁改文案）。
            const pp::ProbeOutcome po = pp::probe_file(c.path);
            check(po.error == "CMYK input is not supported", std::string(c.name) + "/probe",
                  "expected 'CMYK input is not supported', got '" + po.error + "'");
            const pp::DecodeOutcome d = pp::decode_float(c.path, po.info);
            check(d.error == "CMYK input is not supported", std::string(c.name) + "/decode",
                  "expected 'CMYK input is not supported', got '" + d.error + "'");
            check(!d.buf.initialized(), std::string(c.name) + "/no-buf", "拒收后不得产出像素缓冲");
        }
    }

    // ---- D2（M7-R2A）：Exif.Photo.ColorSpace(0xA001) → 无 ICC 时的 AdobeRGB 合成 ----
    {
        // ① 提取函数（纯 spec 面）：1→1、2→2、缺失/0/越界→0。
        OIIO::ImageSpec spec;
        check(pp::exif_colorspace_from_spec(spec) == 0, "d2/spec-absent", "缺失属性应为 0");
        spec.attribute("Exif:ColorSpace", 1);
        check(pp::exif_colorspace_from_spec(spec) == 1, "d2/spec-srgb", "1 应映射 1");
        spec.attribute("Exif:ColorSpace", 2);
        check(pp::exif_colorspace_from_spec(spec) == 2, "d2/spec-adobergb", "2 应映射 2");
        spec.attribute("Exif:ColorSpace", 0);
        check(pp::exif_colorspace_from_spec(spec) == 0, "d2/spec-zero", "0 应映射 0");
        spec.attribute("Exif:ColorSpace", 5);
        check(pp::exif_colorspace_from_spec(spec) == 0, "d2/spec-out-of-range", "越界值应映射 0");

        // ② 文件面：EXIF 真写进 JPEG 后，probe 的 first_spec 必须读回 2。
        namespace fsys = std::filesystem;
        const fsys::path tmp = fsys::temp_directory_path() / "pp-test_decode-m7r2a";
        std::error_code ec;
        fsys::create_directories(tmp, ec);
        const fsys::path adobe_jpg = tmp / "exif_adobe.jpg";
        {
            auto out = OIIO::ImageOutput::create(adobe_jpg.string());
            bool ok = out != nullptr;
            if (ok) {
                OIIO::ImageSpec wspec(32, 24, 3, OIIO::TypeUInt8);
                std::vector<unsigned char> px(std::size_t{32} * 24 * 3, 128);
                ok = out->open(adobe_jpg.string(), wspec) &&
                     out->write_image(OIIO::TypeUInt8, px.data());
                out->close();
            }
            check(ok, "d2/file-write", "测试 JPEG 合成失败");
            if (ok) {
                Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(adobe_jpg.string());
                img->readMetadata();
                img->exifData()["Exif.Photo.ColorSpace"] = static_cast<uint16_t>(2);
                img->writeMetadata();
            }
        }
        if (fsys::exists(adobe_jpg)) {
            const pp::ProbeOutcome po = pp::probe_file(adobe_jpg);
            check(po.error.empty(), "d2/file-probe", po.error);
            check(po.first_spec.has_value(), "d2/file-spec", "first_spec 缺失");
            if (po.first_spec) {
                check(pp::exif_colorspace_from_spec(*po.first_spec) == 2, "d2/file-exif-colorspace",
                      "EXIF ColorSpace=2 未从 spec 读回");
            }
        }

        // ③ 装配面（pipeline 的 src_icc 装配段）：无 ICC + EXIF=2 → 输出 ICC == 合成
        //    AdobeRGB profile 字节（=1/缺失 → 现状：keep 下无 ICC 可嵌）。
        std::string adobe_err;
        const std::string adobe_icc = pp::load_target_icc(pp::ColorTarget::AdobeRGB, adobe_err);
        check(!adobe_icc.empty(), "d2/adobe-profile", "AdobeRGB 合成入口失败: " + adobe_err);

        auto run_keep = [&](const fsys::path &src, const fsys::path &out_root) {
            pp::FileEntry fe;
            fe.src = src;
            fe.base_dir = src.parent_path();
            pp::RunConfig cfg;
            cfg.out_root = out_root;
            cfg.outputs.push_back(pp::OutputFormatSpec{"png", "oiio", "", pp::ParamSet{}, 8});
            cfg.color = pp::ColorTarget::KeepOriginal;
            cfg.conflict = pp::ConflictPolicy::Overwrite;
            cfg.output_template = "$dir/$file";
            return pp::run_one_file(fe, cfg, pp::EventFn{});
        };

        if (fsys::exists(adobe_jpg) && !adobe_icc.empty()) {
            const pp::FileOutcome oc = run_keep(adobe_jpg, tmp / "out-adobe");
            check(oc.verdict == pp::FileOutcome::Verdict::Done, "d2/e2e-verdict",
                  "verdict 非 Done");
            if (!oc.file.outputs.empty() && oc.file.outputs[0].ok) {
                const pp::ProbeOutcome outp = pp::probe_file(oc.file.outputs[0].out);
                const std::string out_icc =
                    outp.first_spec ? pp::icc_from_spec(*outp.first_spec) : std::string();
                check(out_icc == adobe_icc, "d2/e2e-icc-adobe",
                      "输出 ICC 与合成 AdobeRGB 字节不一致（" +
                          num(static_cast<long long>(out_icc.size())) + "B vs " +
                          num(static_cast<long long>(adobe_icc.size())) + "B）");
            } else {
                check(false, "d2/e2e-output", "输出未产出");
            }
        }

        // 对照（=1/缺失 → 现状）：同像素、EXIF=1 的 JPEG 在 keep 下无 ICC 可嵌。
        const fsys::path srgb_jpg = tmp / "exif_srgb.jpg";
        if (fsys::exists(adobe_jpg)) {
            std::error_code ec2;
            fsys::copy_file(adobe_jpg, srgb_jpg, fsys::copy_options::overwrite_existing, ec2);
            Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(srgb_jpg.string());
            img->readMetadata();
            img->exifData()["Exif.Photo.ColorSpace"] = static_cast<uint16_t>(1);
            img->writeMetadata();
            const pp::FileOutcome oc = run_keep(srgb_jpg, tmp / "out-srgb");
            check(oc.verdict == pp::FileOutcome::Verdict::Done, "d2/e2e-srgb-verdict",
                  "verdict 非 Done");
            if (!oc.file.outputs.empty() && oc.file.outputs[0].ok) {
                const pp::ProbeOutcome outp = pp::probe_file(oc.file.outputs[0].out);
                const std::string out_icc =
                    outp.first_spec ? pp::icc_from_spec(*outp.first_spec) : std::string();
                check(out_icc.empty(), "d2/e2e-srgb-no-icc",
                      "EXIF=1（sRGB）+ keep = 现状：不该合成 ICC，实得 " +
                          num(static_cast<long long>(out_icc.size())) + "B");
            }
        }
    }

    // ---- R-163 残余（E3，M7-R2A）：编码中取消的 verdict 归类单点化 ----
    // 编码器因 cancel 提前退出（返回 "... encode cancelled"）时，pipeline 必须归
    // **Cancelled**（不是 Failed）：verdict / 行状态 / error_kind 三面一致（set_output_state
    // + error_kind_of 既有机制）。取消时机 = 编码行循环内（cancelled() 前 10 次返回 false，
    // 之后 true —— 64 行 JPEG 的行级检查点保证取消发生在 encode 中段）。
    {
        namespace fsys = std::filesystem;
        const fsys::path tmp = fsys::temp_directory_path() / "pp-test_decode-m7r2a";
        std::error_code ec;
        fsys::create_directories(tmp, ec);
        const fsys::path src = tmp / "cancel_src.png";
        {
            auto out = OIIO::ImageOutput::create(src.string());
            bool ok = out != nullptr;
            if (ok) {
                OIIO::ImageSpec wspec(64, 64, 3, OIIO::TypeUInt8);
                std::vector<unsigned char> px(std::size_t{64} * 64 * 3, 90);
                ok = out->open(src.string(), wspec) && out->write_image(OIIO::TypeUInt8, px.data());
                out->close();
            }
            check(ok, "r163/fixture", "cancel 夹具合成失败");
        }
        if (fsys::exists(src)) {
            pp::FileEntry fe;
            fe.src = src;
            fe.base_dir = tmp;
            pp::RunConfig cfg;
            cfg.out_root = tmp / "out-cancel";
            cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "jpegli", "", pp::ParamSet{}, 8});
            cfg.conflict = pp::ConflictPolicy::Overwrite;
            cfg.output_template = "$dir/$file";
            int calls = 0;
            pp::RunScope scope;
            scope.cancelled = [&calls]() { return ++calls > 10; };
            pp::EventFn ev{nullptr, &scope};
            const pp::FileOutcome oc = pp::run_one_file(fe, cfg, ev);
            check(oc.verdict == pp::FileOutcome::Verdict::Cancelled, "r163/verdict",
                  "编码中取消应归 Cancelled");
            check(oc.file.cancelled, "r163/file-flag", "FileResult::cancelled 应为真");
            if (!oc.file.outputs.empty()) {
                const pp::OutputResult &row = oc.file.outputs[0];
                check(row.state == pp::OutputState::Cancelled, "r163/row-state",
                      "行状态应为 Cancelled（set_output_state 单点）");
                check(row.error_kind == pp::ErrorKind::Cancelled, "r163/row-kind",
                      "error_kind 应为 Cancelled（error_kind_of 单点）");
                check(row.error == "encode cancelled", "r163/row-error",
                      "取消行错误串应为规范化的 encode cancelled，got '" + row.error + "'");
            } else {
                check(false, "r163/rows", "无输出行");
            }
        }
    }

    std::printf("test_decode: %d checks, %d failure(s)\n", g_checks, g_failed);
    return g_failed;
}
