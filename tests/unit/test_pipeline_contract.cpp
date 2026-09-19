// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T8 — pipeline contract unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.9 (PP-FROZEN) / §4.8 / §5.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <exiv2/exiv2.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/pipeline.h"
#include "core/pixelbudget.h"
#include "decode/oiio_reader.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string& case_name, const std::string& detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec)) return p / "tests" / "golden";
        if (!p.has_parent_path() || p.parent_path() == p) break;
        p = p.parent_path();
    }
    return {};
}

fs::path make_temp_dir(const std::string& name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

bool has_warning(const pp::FileResult& r, pp::WarningKind k) {
    for (const pp::Warning& w : r.warnings) {
        if (w.kind == k) return true;
    }
    return false;
}

struct OutInfo {
    bool ok = false;
    int width = 0, height = 0, channels = 0, bitdepth = 0;
};

OutInfo read_info(const fs::path& p) {
    OutInfo o;
    auto in = OIIO::ImageInput::open(p.string());
    if (!in) return o;
    const OIIO::ImageSpec spec = in->spec();
    o.width = spec.width;
    o.height = spec.height;
    o.channels = spec.nchannels;
    o.bitdepth = static_cast<int>(spec.format.basesize()) * 8;
    o.ok = true;
    in->close();
    return o;
}

std::vector<float> read_pixels(const fs::path& p, int& w, int& h, int& ch) {
    std::vector<float> px;
    auto in = OIIO::ImageInput::open(p.string());
    if (!in) return px;
    const OIIO::ImageSpec spec = in->spec();
    w = spec.width;
    h = spec.height;
    ch = spec.nchannels;
    px.assign(static_cast<std::size_t>(w) * h * ch, 0.0f);
    if (!in->read_image(0, 0, 0, ch, OIIO::TypeDesc::FLOAT, px.data())) px.clear();
    in->close();
    return px;
}

bool same_pixels(const fs::path& a, const fs::path& b, std::string& detail) {
    int aw = 0, ah = 0, ac = 0, bw = 0, bh = 0, bc = 0;
    const std::vector<float> ap = read_pixels(a, aw, ah, ac);
    const std::vector<float> bp = read_pixels(b, bw, bh, bc);
    if (ap.empty() || bp.empty()) {
        detail = "cannot read pixels";
        return false;
    }
    if (aw != bw || ah != bh || ac != bc) {
        detail = "geometry/channels differ";
        return false;
    }
    std::size_t diff = 0;
    for (std::size_t i = 0; i < ap.size(); ++i) {
        if (std::lround(ap[i] * 65535.0f) != std::lround(bp[i] * 65535.0f)) ++diff;
    }
    detail = std::to_string(diff) + " of " + std::to_string(ap.size()) + " samples differ";
    return diff == 0;
}

// ---------------------------------------------------------------------------
// run helper
// ---------------------------------------------------------------------------
struct RunSpec {
    std::string format = "jpeg", backend, tech;
    bool lossless = false;
    int bitdepth = 8;
    pp::ParamSet params;
    pp::ColorTarget color = pp::ColorTarget::KeepOriginal;
    pp::ConflictPolicy conflict = pp::ConflictPolicy::Overwrite;
    bool rotate = true;
    double flatten = 1.0;
    pp::BatchRules rules;
    bool metadata_only = false;
    pp::PixelBudget* budget = nullptr;
    bool cancelled = false;
};

pp::FileResult run_spec(const fs::path& src, const fs::path& base, const fs::path& out_root,
                        const RunSpec& s) {
    pp::FileEntry fe;
    fe.src = src;
    fe.base_dir = base;
    pp::RunConfig cfg;
    cfg.out_root = out_root;
    cfg.format_id = s.format;
    cfg.backend_id = s.backend;
    cfg.tech_id = s.tech;
    cfg.lossless = s.lossless;
    cfg.params = s.params;
    cfg.out_bitdepth = s.bitdepth;
    cfg.color_target = s.color;
    cfg.conflict = s.conflict;
    cfg.rotate_orientation = s.rotate;
    cfg.flatten_gray = s.flatten;
    cfg.rules = s.rules;
    cfg.metadata_only = s.metadata_only;
    const std::function<bool()> cancelled = s.cancelled ? std::function<bool()>([] { return true; })
                                                        : std::function<bool()>();
    if (s.metadata_only) {
        return pp::run_metadata_only(fe, cfg, {}, cancelled, nullptr);
    }
    std::unique_ptr<pp::IEncoder> enc = pp::make_encoder(cfg.format_id, cfg.backend_id);
    pp::PixelBudget local_budget(pp::PixelBudget::default_capacity_bytes());
    return pp::run_one_file(fe, cfg, enc.get(), s.budget != nullptr ? s.budget : &local_budget, {},
                            cancelled, nullptr);
}

bool write_oriented_tiff(const fs::path& p, int w, int h, int orientation) {
    auto out = OIIO::ImageOutput::create(p.string());
    if (!out) return false;
    OIIO::ImageSpec spec(w, h, 3, OIIO::TypeDesc::UINT8);
    spec.channelnames = {"R", "G", "B"};
    if (orientation > 1) spec.attribute("Orientation", orientation);
    if (!out->open(p.string(), spec)) return false;
    std::vector<unsigned char> px(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
            px[i] = static_cast<unsigned char>((x * 255) / (w - 1));
            px[i + 1] = static_cast<unsigned char>((y * 255) / (h - 1));
            px[i + 2] = static_cast<unsigned char>(((x + y) * 255) / (w + h - 2));
        }
    }
    const bool ok = out->write_image(OIIO::TypeDesc::UINT8, px.data());
    out->close();
    return ok;
}

std::string read_exif_artist(const fs::path& p) {
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(p.string());
        img->readMetadata();
        const Exiv2::ExifData exif = img->exifData();
        const auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
        if (it == exif.end()) return {};
        return it->print(&exif);
    } catch (const std::exception& e) {
        return std::string("error: ") + e.what();
    }
}

}  // namespace

int main() {
    const fs::path corpus = find_corpus();
    if (corpus.empty()) {
        std::printf("FAIL setup: tests/golden not found from %s\n",
                    fs::current_path().string().c_str());
        return 1;
    }
    const fs::path base = corpus / "base";
    const fs::path tmp = make_temp_dir("pipeline");

    // ---- A. mirror path (out_root / relative-to-base, extension swapped) ----
    {
        RunSpec s;
        s.format = "jpeg";
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "mirror", s);
        check(r.ok, "mirror/ok", r.error);
        check(r.out == tmp / "mirror" / "base" / "rgb8.jpg",
              "mirror/path", "got " + r.out.string());
        check(read_info(r.out).ok, "mirror/readable", r.out.string());
    }

    // ---- B/C/D. conflict policies ----
    {
        const fs::path out = tmp / "conflict";
        std::error_code ec;
        fs::create_directories(out / "base", ec);
        const fs::path desired = out / "base" / "rgb8.jpg";
        {
            std::ofstream f(desired, std::ios::binary | std::ios::trunc);
            f << "stale";
        }
        RunSpec skip;
        skip.conflict = pp::ConflictPolicy::Skip;
        const pp::FileResult rs = run_spec(base / "rgb8.png", corpus, out, skip);
        check(rs.skipped && !rs.ok, "conflict/skip-flag", rs.error);
        std::ifstream in(desired, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        check(content == "stale", "conflict/skip-keeps-file", content);

        RunSpec rename;
        rename.conflict = pp::ConflictPolicy::Rename;
        const pp::FileResult rr = run_spec(base / "rgb8.png", corpus, out, rename);
        check(rr.ok, "conflict/rename-ok", rr.error);
        check(rr.out == out / "base" / "rgb8 (1).jpg", "conflict/rename-path",
              "got " + rr.out.string());

        RunSpec overwrite;
        overwrite.conflict = pp::ConflictPolicy::Overwrite;
        const pp::FileResult ro = run_spec(base / "rgb8.png", corpus, out, overwrite);
        check(ro.ok && ro.out == desired, "conflict/overwrite-path", ro.out.string());
        check(read_info(desired).ok, "conflict/overwrite-valid", "overwritten file unreadable");
    }

    // ---- E. grayscale into WebP → GrayToRgbEncoded, RGB output ----
    {
        RunSpec s;
        s.format = "webp";
        s.tech = "lossless";
        s.lossless = true;
        s.bitdepth = 8;
        const pp::FileResult r = run_spec(base / "gray8.png", corpus, tmp / "gray-webp", s);
        check(r.ok, "gray-webp/ok", r.error);
        check(has_warning(r, pp::WarningKind::GrayToRgbEncoded), "gray-webp/warning",
              "GrayToRgbEncoded missing");
        const OutInfo oi = read_info(r.out);
        check(oi.ok && oi.channels == 3, "gray-webp/3channels",
              "channels=" + std::to_string(oi.channels));
    }

    // ---- F. alpha into JPEG → AlphaFlattened, 3 channels ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "alpha-jpeg", s);
        check(r.ok, "alpha-jpeg/ok", r.error);
        check(has_warning(r, pp::WarningKind::AlphaFlattened), "alpha-jpeg/warning",
              "AlphaFlattened missing");
        const OutInfo oi = read_info(r.out);
        check(oi.ok && oi.channels == 3, "alpha-jpeg/3channels",
              "channels=" + std::to_string(oi.channels));
    }

    // ---- G. alpha into BMP → AlphaFlattened + MetadataDropped ----
    {
        RunSpec s;
        s.format = "bmp";
        s.bitdepth = 24;
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "alpha-bmp", s);
        check(r.ok, "alpha-bmp/ok", r.error);
        check(has_warning(r, pp::WarningKind::AlphaFlattened), "alpha-bmp/alpha-warning",
              "AlphaFlattened missing");
        check(has_warning(r, pp::WarningKind::MetadataDropped), "alpha-bmp/meta-warning",
              "MetadataDropped missing");
    }

    // ---- H. multipage input → MultipageTruncated ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        const pp::FileResult r = run_spec(base / "multi.tif", corpus, tmp / "multipage", s);
        check(r.ok, "multipage/ok", r.error);
        check(has_warning(r, pp::WarningKind::MultipageTruncated), "multipage/warning",
              "MultipageTruncated missing");
    }

    // ---- I. 16-bit source into an 8-bit format → DepthDowngrade ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        const pp::FileResult r = run_spec(base / "rgb16.png", corpus, tmp / "depth", s);
        check(r.ok, "depth/ok", r.error);
        check(has_warning(r, pp::WarningKind::DepthDowngrade), "depth/warning",
              "DepthDowngrade missing");
    }

    // ---- J. BMP output always reports MetadataDropped ----
    {
        RunSpec s;
        s.format = "bmp";
        s.bitdepth = 24;
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "bmp-meta", s);
        check(r.ok, "bmp-meta/ok", r.error);
        check(has_warning(r, pp::WarningKind::MetadataDropped), "bmp-meta/warning",
              "MetadataDropped missing");
    }

    // ---- K. cancel semantics: no output is produced ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.cancelled = true;
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "cancel", s);
        check(r.cancelled && !r.ok && r.error.empty(), "cancel/flag", r.error);
        std::error_code ec;
        check(!fs::exists(tmp / "cancel" / "base" / "rgb8.jpg", ec), "cancel/no-output",
              "output was written despite cancellation");
    }

    // ---- L/M. pixel budget: over-capacity fails fast, normal runs release ----
    {
        const uint64_t frame = pp::PixelBudget::frame_bytes(64, 64, 3);
        pp::PixelBudget tight(frame);  // needs 2×frame
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        s.budget = &tight;
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "budget-tight", s);
        check(!r.ok && r.error.find("pixel budget") != std::string::npos, "budget/over-capacity",
              r.error);
        check(tight.used() == 0, "budget/not-consumed",
              "used=" + std::to_string(tight.used()));

        pp::PixelBudget wide(64ull * 1024 * 1024);
        RunSpec s2;
        s2.format = "jpeg";
        s2.bitdepth = 8;
        s2.budget = &wide;
        const pp::FileResult r2 = run_spec(base / "rgb8.png", corpus, tmp / "budget-wide", s2);
        check(r2.ok, "budget/wide-ok", r2.error);
        check(wide.used() == 0, "budget/released", "used=" + std::to_string(wide.used()));
        check(wide.peak() == 2 * frame, "budget/peak-2x",
              "peak=" + std::to_string(wide.peak()) + " expected=" + std::to_string(2 * frame));
        check(wide.peak() <= wide.capacity(), "budget/within-capacity", "peak > capacity");
    }

    // ---- N. metadata-only capability matrix ----
    {
        check(pp::format_supports_metadata_only("jpeg"), "meta-only/jpeg", "expected true");
        check(pp::format_supports_metadata_only("png"), "meta-only/png", "expected true");
        check(pp::format_supports_metadata_only("tiff"), "meta-only/tiff", "expected true");
        check(pp::format_supports_metadata_only("webp"), "meta-only/webp", "expected true");
        check(!pp::format_supports_metadata_only("jxl"), "meta-only/jxl", "expected false");
        check(!pp::format_supports_metadata_only("heif"), "meta-only/heif", "expected false");
        check(!pp::format_supports_metadata_only("avif"), "meta-only/avif", "expected false");
        check(!pp::format_supports_metadata_only("bmp"), "meta-only/bmp", "expected false");
    }

    // ---- O. EXIF orientation → pixel rotation (and no rotation when disabled) ----
    {
        const fs::path oriented = tmp / "oriented.tif";
        if (!write_oriented_tiff(oriented, 64, 32, 6)) {
            check(false, "orient/setup", "cannot synthesize the oriented fixture");
        } else {
            const pp::ProbeOutcome po = pp::probe_file(oriented);
            check(po.first_spec.has_value() && pp::orientation_from_spec(*po.first_spec) == 6,
                  "orient/fixture", "synthesized TIFF lost its Orientation tag");
            RunSpec s;
            s.format = "jpeg";
            s.bitdepth = 8;
            const pp::FileResult r = run_spec(oriented, tmp, tmp / "orient-on", s);
            check(r.ok, "orient/ok", r.error);
            const OutInfo oi = read_info(r.out);
            check(oi.ok && oi.width == 32 && oi.height == 64, "orient/rotated",
                  std::to_string(oi.width) + "x" + std::to_string(oi.height));

            RunSpec off = s;
            off.rotate = false;
            const pp::FileResult r2 = run_spec(oriented, tmp, tmp / "orient-off", off);
            check(r2.ok, "orient-off/ok", r2.error);
            const OutInfo oi2 = read_info(r2.out);
            check(oi2.ok && oi2.width == 64 && oi2.height == 32, "orient-off/kept",
                  std::to_string(oi2.width) + "x" + std::to_string(oi2.height));
        }
    }

    // ---- P. metadata-only round trip (same container, Artist edit, pixels intact) ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.metadata_only = true;
        pp::TagEdit e;
        e.key = "Exif.Image.Artist";
        e.value = std::string("M1-T8");
        s.rules.exif_edits.push_back(e);
        const pp::FileResult r = run_spec(base / "photo.jpg", corpus, tmp / "meta-only", s);
        check(r.ok, "meta-only/ok", r.error);
        check(read_exif_artist(r.out) == "M1-T8", "meta-only/artist",
              "got '" + read_exif_artist(r.out) + "'");
        std::string detail;
        check(same_pixels(base / "photo.jpg", r.out, detail), "meta-only/pixels", detail);
    }

    // ---- Q. tech_id / lossless passthrough (JXL modular lossless round trip) ----
    {
        RunSpec s;
        s.format = "jxl";
        s.tech = "modular";
        s.lossless = true;
        s.bitdepth = 16;
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "jxl-modular", s);
        check(r.ok, "jxl-modular/ok", r.error);
        std::string detail;
        check(same_pixels(base / "rgb8.png", r.out, detail), "jxl-modular/exact", detail);
    }

    // ---- R. unknown format / missing encoder ----
    {
        RunSpec s;
        s.format = "nope";
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "unknown-fmt", s);
        check(!r.ok && r.error.find("unknown output format") != std::string::npos,
              "unknown-format", r.error);

        RunSpec s2;
        s2.format = "jpeg";
        s2.backend = "does-not-exist";
        const pp::FileResult r2 = run_spec(base / "rgb8.png", corpus, tmp / "unknown-backend", s2);
        check(!r2.ok && r2.error.find("no encoder registered") != std::string::npos,
              "unknown-backend", r2.error);
    }

    // ---- S. color transform path: keep → no ICC assumption warning; srgb → warning ----
    {
        RunSpec keep;
        keep.format = "jpeg";
        const pp::FileResult rk = run_spec(base / "rgb8.png", corpus, tmp / "color-keep", keep);
        check(rk.ok && !has_warning(rk, pp::WarningKind::NoIccAssumeSrgb), "color/keep-no-warning",
              rk.error);

        RunSpec srgb = keep;
        srgb.color = pp::ColorTarget::SRGB;
        const pp::FileResult rs = run_spec(base / "rgb8.png", corpus, tmp / "color-srgb", srgb);
        check(rs.ok, "color/srgb-ok", rs.error);
        check(has_warning(rs, pp::WarningKind::NoIccAssumeSrgb), "color/srgb-warning",
              "NoIccAssumeSrgb missing for an ICC-less source");
    }

    if (g_failed == 0) {
        std::printf("test_pipeline_contract: all checks passed\n");
    } else {
        std::printf("test_pipeline_contract: %d check(s) failed\n", g_failed);
    }
    return g_failed;
}
