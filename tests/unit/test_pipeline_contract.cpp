// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T8 — pipeline contract unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.9 (PP-FROZEN) / §4.8 / §5.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <exiv2/exiv2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/pipeline.h"
#include "core/pixelbudget.h"
#include "decode/oiio_reader.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec))
            return p / "tests" / "golden";
        if (!p.has_parent_path() || p.parent_path() == p)
            break;
        p = p.parent_path();
    }
    return {};
}

fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

bool has_warning(const pp::FileResult &r, pp::WarningKind k) {
    for (const pp::Warning &w : r.warnings) {
        if (w.kind == k)
            return true;
    }
    return false;
}

// 0.3.0：逐输出行的告警查询（行号 = 配置顺序）
bool has_row_warning(const pp::FileResult &r, std::size_t row, pp::WarningKind k) {
    if (row >= r.outputs.size())
        return false;
    for (const pp::Warning &w : r.outputs[row].warnings) {
        if (w.kind == k)
            return true;
    }
    return false;
}

struct OutInfo {
    bool ok = false;
    int width = 0, height = 0, channels = 0, bitdepth = 0;
};

OutInfo read_info(const fs::path &p) {
    OutInfo o;
    auto in = OIIO::ImageInput::open(p.string());
    if (!in)
        return o;
    const OIIO::ImageSpec spec = in->spec();
    o.width = spec.width;
    o.height = spec.height;
    o.channels = spec.nchannels;
    o.bitdepth = static_cast<int>(spec.format.basesize()) * 8;
    o.ok = true;
    in->close();
    return o;
}

std::vector<float> read_pixels(const fs::path &p, int &w, int &h, int &ch) {
    std::vector<float> px;
    auto in = OIIO::ImageInput::open(p.string());
    if (!in)
        return px;
    const OIIO::ImageSpec spec = in->spec();
    w = spec.width;
    h = spec.height;
    ch = spec.nchannels;
    px.assign(static_cast<std::size_t>(w) * h * ch, 0.0f);
    if (!in->read_image(0, 0, 0, ch, OIIO::TypeDesc::FLOAT, px.data()))
        px.clear();
    in->close();
    return px;
}

bool same_pixels(const fs::path &a, const fs::path &b, std::string &detail) {
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
        if (std::lround(ap[i] * 65535.0f) != std::lround(bp[i] * 65535.0f))
            ++diff;
    }
    detail = std::to_string(diff) + " of " + std::to_string(ap.size()) + " samples differ";
    return diff == 0;
}

// ---------------------------------------------------------------------------
// run helper（0.3.0：多输出。RunSpec.extra_outputs 追加输出；output_template 默认 0.2 兼容形态，
// 既有断言依赖该结构；多格式/模板用例显式覆盖）
// ---------------------------------------------------------------------------
struct RunSpec {
    std::string format = "jpeg", backend, tech;
    bool lossless = false;
    int bitdepth = 8;
    pp::ParamSet params;
    std::vector<pp::OutputFormatSpec> extra_outputs; // 追加输出（0.3.0 多格式）
    std::string output_template = "$dir/$file";      // 0.2 兼容（既有断言依赖）
    pp::ColorTarget color = pp::ColorTarget::KeepOriginal;
    pp::ConflictPolicy conflict = pp::ConflictPolicy::Overwrite;
    bool rotate = true;
    double flatten = 1.0;
    pp::BatchRules rules;
    bool metadata_only = false;
    pp::PixelBudget *budget = nullptr;
    bool cancelled = false;
    std::vector<std::filesystem::path> reserved; // 批内已分配输出路径（§4.4）
    // M2-T4: per-stage hook; the metadata-write-failure case sabotages the container at Writing.
    std::function<void(pp::FileState)> on_stage;
};

// 追加输出（format/backend/tech/bitdepth/params 五元组）构造助手
pp::OutputFormatSpec out_spec(std::string format, std::string backend, std::string tech,
                              int bitdepth, pp::ParamSet params = {}) {
    return pp::OutputFormatSpec{std::move(format), std::move(backend), std::move(tech),
                                std::move(params), bitdepth};
}

pp::FileOutcome run_spec_outcome(const fs::path &src, const fs::path &base,
                                 const fs::path &out_root, const RunSpec &s) {
    pp::FileEntry fe;
    fe.src = src;
    fe.base_dir = base;
    pp::RunConfig cfg;
    cfg.out_root = out_root;
    // 每输出参数按冻结引擎契约由调用方给出：无损语义由 default_params 的保留键承载，
    // 用例用 lossless 开关显式补上（pipeline 不再自行注入该键 —— 主对话裁定 B）。
    pp::ParamSet params = s.params;
    if (s.lossless && params.find("__lossless") == params.end())
        params["__lossless"] = true;
    cfg.outputs.push_back(pp::OutputFormatSpec{s.format, s.backend, s.tech, params, s.bitdepth});
    for (const pp::OutputFormatSpec &o : s.extra_outputs)
        cfg.outputs.push_back(o);
    cfg.color = s.color;
    cfg.conflict = s.conflict;
    cfg.rotate_orientation = s.rotate;
    cfg.flatten_gray = s.flatten;
    cfg.rules = s.rules;
    cfg.metadata_only = s.metadata_only;
    cfg.output_template = s.output_template;

    pp::PixelBudget local_budget(pp::PixelBudget::default_capacity_bytes());
    pp::RunScope scope;
    scope.budget = s.budget != nullptr ? s.budget : &local_budget;
    scope.reserved = s.reserved;
    if (s.cancelled)
        scope.cancelled = [] { return true; };
    pp::EventFn events{[&s](const pp::FileEvent &e) {
                           if (s.on_stage)
                               s.on_stage(e.state);
                       },
                       &scope};
    if (s.metadata_only)
        return pp::run_metadata_only(fe, cfg, events);
    return pp::run_one_file(fe, cfg, events);
}

pp::FileResult run_spec(const fs::path &src, const fs::path &base, const fs::path &out_root,
                        const RunSpec &s) {
    return run_spec_outcome(src, base, out_root, s).file;
}

bool write_oriented_tiff(const fs::path &p, int w, int h, int orientation) {
    auto out = OIIO::ImageOutput::create(p.string());
    if (!out)
        return false;
    OIIO::ImageSpec spec(w, h, 3, OIIO::TypeDesc::UINT8);
    spec.channelnames = {"R", "G", "B"};
    if (orientation > 1)
        spec.attribute("Orientation", orientation);
    if (!out->open(p.string(), spec))
        return false;
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

std::string read_exif_artist(const fs::path &p) {
    try {
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(p.string());
        img->readMetadata();
        const Exiv2::ExifData exif = img->exifData();
        const auto it = exif.findKey(Exiv2::ExifKey("Exif.Image.Artist"));
        if (it == exif.end())
            return {};
        return it->print(&exif);
    } catch (const std::exception &e) {
        return std::string("error: ") + e.what();
    }
}

} // namespace

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
        check(r.out == tmp / "mirror" / "base" / "rgb8.jpg", "mirror/path",
              "got " + r.out.string());
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
        pp::PixelBudget tight(frame); // needs 2×frame
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        s.budget = &tight;
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "budget-tight", s);
        check(!r.ok && r.error.find("pixel budget") != std::string::npos, "budget/over-capacity",
              r.error);
        check(tight.used() == 0, "budget/not-consumed", "used=" + std::to_string(tight.used()));

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
        check(!r.ok && r.error.find("unknown output format") != std::string::npos, "unknown-format",
              r.error);

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

    // ---- T. metadata write failure → file-level warning via the explicit writer channel (M2-T4)
    // ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        pp::TagEdit e;
        e.key = "Exif.Image.Artist";
        e.value = std::string("M2-T4");
        s.rules.exif_edits.push_back(e); // non-empty plan: the writer really runs

        // Sabotage the freshly encoded container at the Writing stage (the pipeline calls on_stage
        // before write_metadata_exiv2): replacing the output file with a directory makes every
        // Exiv2 open/write attempt fail deterministically, without relying on permissions.
        const fs::path sabotage = tmp / "meta-write-fail" / "base" / "rgb8.jpg";
        s.on_stage = [sabotage](pp::FileState st) {
            if (st != pp::FileState::Writing)
                return;
            std::error_code ec;
            fs::remove(sabotage, ec);
            fs::create_directory(sabotage, ec);
        };
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "meta-write-fail", s);

        check(r.ok, "meta-write-fail/non-fatal", r.error);
        std::size_t dropped = 0;
        std::string detail;
        for (const pp::Warning &w : r.warnings) {
            if (w.kind == pp::WarningKind::MetadataDropped) {
                ++dropped;
                detail = w.detail;
            }
        }
        // Exactly one: the legacy plan.warnings entry and the writer's explicit message must be
        // merged with dedup, never shown twice.
        check(dropped == 1, "meta-write-fail/warning-count",
              "expected exactly 1 MetadataDropped, got " + std::to_string(dropped));
        check(detail.find("metadata dropped") != std::string::npos, "meta-write-fail/detail",
              "writer detail missing, got '" + detail + "'");
        std::printf("info meta-write-fail: warnings=%zu detail=%s\n", r.warnings.size(),
                    detail.c_str());
    }

    // ---- U. cross-field parameter constraints → per-file failure, error = first message (M2-T5)
    // ---- run_one_file / run_metadata_only are the single-file entry points the `--dev` harness
    // drives; the guard is evaluated on the params as handed in (callers that normalize via
    // apply_locks, e.g. the GUI and main.cpp, never reach this state — this covers the engine
    // boundary).
    {
        RunSpec s;
        s.format = "jpeg";
        s.bitdepth = 8;
        s.params = pp::default_params(*pp::find_format("jpeg"), "jpegli", "dct", false);
        s.params["optimize_coding"] = false; // progressive defaults to true → illegal pair
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "cross-jpeg", s);
        check(!r.ok && r.error == "启用渐进式时必须启用哈夫曼表优化", "cross-param/jpeg",
              "error=[" + r.error + "]");
        std::error_code ec;
        check(!fs::exists(tmp / "cross-jpeg" / "base" / "rgb8.jpg", ec), "cross-param/no-output",
              "output was written despite a cross-field violation");

        RunSpec u = s;
        u.params = pp::default_params(*pp::find_format("jpeg"), "jpegli", "dct", false);
        u.params["bogus"] = std::string("1");
        const pp::FileResult ru = run_spec(base / "rgb8.png", corpus, tmp / "cross-unknown", u);
        check(!ru.ok && ru.error == "未知参数：bogus", "cross-param/unknown",
              "error=[" + ru.error + "]");

        // metadata-only path goes through the same guard
        RunSpec m;
        m.format = "jpeg";
        m.metadata_only = true;
        m.params = u.params;
        const pp::FileResult rm = run_spec(base / "rgb8.png", corpus, tmp / "cross-meta", m);
        check(!rm.ok && rm.error == "未知参数：bogus", "cross-param/metadata-only",
              "error=[" + rm.error + "]");

        // clean defaults (the golden-smoke shape) must pass through untouched
        RunSpec clean;
        clean.format = "jpeg";
        clean.params = pp::default_params(*pp::find_format("jpeg"), "jpegli", "dct", false);
        const pp::FileResult rc = run_spec(base / "rgb8.png", corpus, tmp / "cross-clean", clean);
        check(rc.ok, "cross-param/defaults-clean", rc.error);
    }

    // ==========================================================================
    // 0.3.0 多输出（M4-T5 §4.1/§4.2/§4.3/§4.4）
    // ==========================================================================

    // ---- V. 解码共享：一个源只解一次 → 全局（decode）告警在文件级只出现一次 ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileResult r = run_spec(base / "multi.tif", corpus, tmp / "multi-decode", s);
        check(r.ok, "multi/ok", r.error);
        check(r.outputs.size() == 2, "multi/rows-two", std::to_string(r.outputs.size()));
        check(r.outputs[0].ok && r.outputs[1].ok, "multi/rows-ok", "rows not both ok");
        std::size_t truncated = 0;
        for (const pp::Warning &w : r.warnings) {
            if (w.kind == pp::WarningKind::MultipageTruncated)
                ++truncated;
        }
        check(truncated == 1, "multi/decode-once",
              "多输出下解码告警在文件级应只出现一次（解码共享），got " + std::to_string(truncated));
        check(r.out == r.outputs[0].out, "multi/legacy-out-equals-first",
              "聚合 out 应等于首个输出（0.2 兼容口径）");
        uint64_t sum = 0;
        for (const pp::OutputResult &o : r.outputs)
            sum += o.out_bytes;
        check(r.out_bytes == sum, "multi/bytes-sum",
              std::to_string(r.out_bytes) + " != " + std::to_string(sum));
        check(read_info(r.outputs[0].out).ok && read_info(r.outputs[1].out).ok,
              "multi/rows-readable", "an output is unreadable");
    }

    // ---- W. 路径模板 $format/$dir/$file（§4.2 分文件夹形态）----
    {
        RunSpec s;
        s.format = "jpeg";
        s.output_template = "$format/$dir/$file";
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "multi-split", s);
        check(r.ok, "multi-template/ok", r.error);
        check(r.outputs[0].out == tmp / "multi-split" / "jpeg" / "base" / "rgb8.jpg",
              "multi-template/jpeg", r.outputs[0].out.string());
        check(r.outputs[1].out == tmp / "multi-split" / "webp" / "base" / "rgb8.webp",
              "multi-template/webp", r.outputs[1].out.string());
    }

    // ---- X. 逐输出独立冲突（§4.4）：reserved 命中只影响对应的那个 out_path ----
    {
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        s.conflict = pp::ConflictPolicy::Rename;
        s.reserved.push_back(tmp / "multi-conflict" / "base" / "rgb8.jpg");
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "multi-conflict", s);
        check(r.ok, "multi-conflict/ok", r.error);
        check(r.outputs[0].out == tmp / "multi-conflict" / "base" / "rgb8 (1).jpg",
              "multi-conflict/jpeg-renamed", r.outputs[0].out.string());
        check(r.outputs[1].out == tmp / "multi-conflict" / "base" / "rgb8.webp",
              "multi-conflict/webp-untouched", r.outputs[1].out.string());
    }

    // ---- Y. 聚合三态：全成功 Done / 任一失败 DoneWithErrors / 全失败 Failed ----
    {
        // 确定性制造单个输出失败：在目标路径上预建目录（编码器的 fopen/open 必失败）
        const fs::path out_root = tmp / "verdict-partial";
        std::error_code ec;
        fs::create_directories(out_root / "base" / "rgb8.bmp", ec);
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("bmp", "oiio", "", 24));
        const pp::FileOutcome oc = run_spec_outcome(base / "rgb8.png", corpus, out_root, s);
        check(oc.verdict == pp::FileOutcome::Verdict::DoneWithErrors, "verdict/partial",
              "verdict 应为 DoneWithErrors");
        check(!oc.file.ok && !oc.file.error.empty(), "verdict/partial-flags",
              "部分失败时 ok 必须为 false 且 error 非空");
        check(oc.file.outputs.size() == 2 && oc.file.outputs[0].ok && !oc.file.outputs[1].ok,
              "verdict/partial-rows", "应恰好 1 成功 + 1 失败");
        check(fs::exists(out_root / "base" / "rgb8.jpg", ec), "verdict/partial-good-output",
              "成功的输出必须落盘");
    }
    {
        const fs::path out_root = tmp / "verdict-failed";
        std::error_code ec;
        fs::create_directories(out_root / "base" / "rgb8.jpg", ec);
        fs::create_directories(out_root / "base" / "rgb8.png", ec);
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("png", "oiio", "", 8));
        const pp::FileOutcome oc = run_spec_outcome(base / "rgb8.png", corpus, out_root, s);
        check(oc.verdict == pp::FileOutcome::Verdict::Failed, "verdict/all-failed",
              "verdict 应为 Failed");
        check(oc.file.error.find("encode failed") != std::string::npos, "verdict/all-failed-error",
              "error=[" + oc.file.error + "]");
        check(!oc.file.outputs[0].ok && !oc.file.outputs[1].ok, "verdict/all-failed-rows",
              "两行都应失败");
        check(!oc.file.ok && !oc.file.skipped && !oc.file.cancelled, "verdict/all-failed-flags",
              "flags 应为失败态");
    }
    {
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileOutcome oc =
            run_spec_outcome(base / "rgb8.png", corpus, tmp / "verdict-done", s);
        check(oc.verdict == pp::FileOutcome::Verdict::Done && oc.file.ok, "verdict/done",
              "verdict 应为 Done");
    }

    // ---- Z. metadata_only × 多输出：硬校验互斥（§3.2）----
    {
        RunSpec s;
        s.format = "jpeg";
        s.metadata_only = true;
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileOutcome oc =
            run_spec_outcome(base / "photo.jpg", corpus, tmp / "meta-mutex", s);
        check(!oc.file.ok && oc.file.error.find("requires exactly one output") != std::string::npos,
              "meta-mutex/run-metadata-only", "error=[" + oc.file.error + "]");
    }
    {
        // run_one_file 自身的互斥闸（不经 helper 分派）。复核项 11：消息应指明这是"入口用错"，
        // 不再借 run_metadata_only 的"requires exactly one output"措辞（1 输出时那措辞会误导）。
        pp::FileEntry fe;
        fe.src = base / "photo.jpg";
        fe.base_dir = corpus;
        pp::RunConfig cfg;
        cfg.out_root = tmp / "meta-mutex-one";
        cfg.metadata_only = true;
        cfg.output_template = "$dir/$file";
        cfg.outputs.push_back(out_spec("jpeg", "jpegli", "", 8)); // 仅 1 项也必须拒绝
        const pp::FileOutcome oc = pp::run_one_file(fe, cfg, pp::EventFn{});
        check(!oc.file.ok && oc.file.error.find("must not be called in metadata-only mode") !=
                                 std::string::npos,
              "meta-mutex/run-one-file", "error=[" + oc.file.error + "]");
    }
    {
        // outputs 为空 → 硬校验（§3.2：≥1 项）
        pp::FileEntry fe;
        fe.src = base / "photo.jpg";
        fe.base_dir = corpus;
        pp::RunConfig cfg;
        cfg.out_root = tmp / "no-output";
        const pp::FileOutcome oc = pp::run_one_file(fe, cfg, pp::EventFn{});
        check(!oc.file.ok && oc.file.error.find("no output configured") != std::string::npos,
              "no-output/guard", "error=[" + oc.file.error + "]");
    }
    {
        // 非法路径模板 → 该文件失败（ready_to_start 之外的 engine 级防线）
        pp::FileEntry fe;
        fe.src = base / "rgb8.png";
        fe.base_dir = corpus;
        pp::RunConfig cfg;
        cfg.out_root = tmp / "bad-template";
        cfg.output_template = "$format/../$file";
        cfg.outputs.push_back(out_spec("jpeg", "jpegli", "", 8));
        const pp::FileOutcome oc = pp::run_one_file(fe, cfg, pp::EventFn{});
        check(!oc.file.ok && oc.file.error.find("output template") != std::string::npos,
              "bad-template/guard", "error=[" + oc.file.error + "]");
    }

    // ---- AA. mtime 同步：生效值只算一次、逐输出落盘同一时刻 ----
    {
        const fs::path src = corpus / "meta" / "exif_full.jpg";
        RunSpec multi;
        multi.format = "jpeg";
        multi.bitdepth = 8;
        multi.rules.sync_mtime = true;
        multi.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileOutcome oc = run_spec_outcome(src, corpus, tmp / "mtime-multi", multi);
        check(oc.file.ok, "mtime/multi-ok", oc.file.error);

        RunSpec single;
        single.format = "jpeg";
        single.bitdepth = 8;
        single.rules.sync_mtime = true;
        const pp::FileOutcome oc1 = run_spec_outcome(src, corpus, tmp / "mtime-single", single);
        check(oc1.file.ok, "mtime/single-ok", oc1.file.error);

        RunSpec off;
        off.format = "jpeg";
        off.bitdepth = 8; // 不同步 mtime → 产物 mtime = 当前时间
        const pp::FileOutcome oc2 = run_spec_outcome(src, corpus, tmp / "mtime-off", off);
        check(oc2.file.ok, "mtime/off-ok", oc2.file.error);

        std::error_code ec;
        const fs::file_time_type t_jpeg = fs::last_write_time(oc.file.outputs[0].out, ec);
        const fs::file_time_type t_webp = fs::last_write_time(oc.file.outputs[1].out, ec);
        const fs::file_time_type t_single = fs::last_write_time(oc1.file.out, ec);
        const fs::file_time_type t_now = fs::last_write_time(oc2.file.out, ec);
        check(!ec && t_jpeg == t_webp, "mtime/shared-value",
              "两输出的 mtime 应取自同一次生效值计算（§4.1）");
        check(t_jpeg == t_single, "mtime/same-as-single",
              "多输出与单输出的同步结果应一致（同一 effective DateTimeOriginal）");
        check(t_jpeg != t_now, "mtime/not-current-time", "同步后的 mtime 不应等于未同步的当前时间");
    }

    // ---- AB. flatten 编排矩阵（§4.3）----
    {
        // (1) 全 alpha-preserving：[webp(lossless), jxl(modular,lossless)] → 不 flatten、都保留
        // alpha
        pp::ParamSet jxl_params;
        jxl_params["__lossless"] = true;
        RunSpec s;
        s.format = "webp";
        s.backend = "libwebp";
        s.tech = "lossless";
        s.lossless = true;
        s.bitdepth = 8;
        s.extra_outputs.push_back(out_spec("jxl", "libjxl", "modular", 16, jxl_params));
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "flat-none", s);
        check(r.ok, "flatten/none-ok", r.error);
        check(!has_warning(r, pp::WarningKind::AlphaFlattened), "flatten/none-no-warning",
              "全支持 alpha 的 targets 不应拍平");
        const OutInfo w = read_info(r.outputs[0].out);
        const OutInfo j = read_info(r.outputs[1].out);
        check(w.ok && w.channels == 4, "flatten/none-webp-alpha",
              "channels=" + std::to_string(w.channels));
        check(j.ok && j.channels == 4, "flatten/none-jxl-alpha",
              "channels=" + std::to_string(j.channels));
        std::string detail;
        check(same_pixels(base / "rgba8.png", r.outputs[0].out, detail), "flatten/none-webp-exact",
              detail);
    }
    {
        // (2) [jpeg, webp]：jpeg（不支持 alpha）拍平且告警；webp 保持 alpha（§4.3 排序证据）
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "flat-jpeg-webp", s);
        check(r.ok, "flatten/jpeg-webp-ok", r.error);
        std::size_t jpeg_warn = 0, webp_warn = 0;
        for (const pp::Warning &x : r.outputs[0].warnings) {
            if (x.kind == pp::WarningKind::AlphaFlattened)
                ++jpeg_warn;
        }
        for (const pp::Warning &x : r.outputs[1].warnings) {
            if (x.kind == pp::WarningKind::AlphaFlattened)
                ++webp_warn;
        }
        check(jpeg_warn == 1, "flatten/jpeg-row-warning", std::to_string(jpeg_warn));
        check(webp_warn == 0, "flatten/webp-row-no-warning", std::to_string(webp_warn));
        check(read_info(r.outputs[0].out).channels == 3, "flatten/jpeg-3ch", "jpeg 应为 3 通道");
        check(read_info(r.outputs[1].out).channels == 4, "flatten/webp-4ch",
              "webp 必须拿到未拍平的像素（4 通道；§4.3 排序义务）");
        check(r.t.flatten_ms > 0, "flatten/time-recorded",
              "flatten_ms 应记录在文件级计时（一次共享拍平）");
    }
    {
        // (2b) 配置顺序颠倒 [webp, jpeg] → 结果与 (2) 相同（内部按 supports_alpha 稳定排序）
        RunSpec s;
        s.format = "webp";
        s.backend = "libwebp";
        s.tech = "lossless";
        s.lossless = true;
        s.extra_outputs.push_back(out_spec("jpeg", "jpegli", "", 8));
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "flat-webp-jpeg", s);
        check(r.ok, "flatten/reversed-ok", r.error);
        check(read_info(r.outputs[0].out).channels == 4, "flatten/reversed-webp-4ch",
              "配置顺序不影响编码顺序（webp 先编码、保留 alpha）");
        check(read_info(r.outputs[1].out).channels == 3, "flatten/reversed-jpeg-3ch",
              "jpeg 后编码、拿到拍平像素");
        check(r.outputs[0].out.extension() == ".webp", "flatten/reversed-out-index",
              "FileResult.outputs 仍按配置顺序记录");
    }
    {
        // (3) [jpeg, bmp] 全不支持 alpha → 首个 encode 前拍平，两行都告警
        RunSpec s;
        s.format = "jpeg";
        s.extra_outputs.push_back(out_spec("bmp", "oiio", "", 24));
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "flat-jpeg-bmp", s);
        check(r.ok, "flatten/jpeg-bmp-ok", r.error);
        std::size_t warns = 0;
        for (const pp::OutputResult &o : r.outputs) {
            for (const pp::Warning &x : o.warnings) {
                if (x.kind == pp::WarningKind::AlphaFlattened)
                    ++warns;
            }
        }
        check(warns == 2, "flatten/all-dropping-warn", std::to_string(warns));
        check(read_info(r.outputs[0].out).channels == 3 &&
                  read_info(r.outputs[1].out).channels == 3,
              "flatten/all-dropping-3ch", "两输出都应为拍平后的 3 通道");
    }
    {
        // (4) 灰度 + 混合灰度支持：[png(支持灰度), webp(不支持)] → color 全局一次（RGB）。
        //     png 格式本身支持灰度，但在共享全局变换下被动升到 RGB —— 复核项 9 之后它也有
        //     GrayToRgbEncoded 告警（措辞标注 shared color transform 以示区别）；
        //     webp 行仍是 v0.2 的逐格式告警（§4.8）。
        RunSpec s;
        s.format = "png";
        s.bitdepth = 8;
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "", 8));
        const pp::FileResult r = run_spec(base / "gray8.png", corpus, tmp / "gray-mixed", s);
        check(r.ok, "gray-mixed/ok", r.error);
        check(has_row_warning(r, 1, pp::WarningKind::GrayToRgbEncoded), "gray-mixed/webp-warning",
              "webp 不支持灰度，应报 GrayToRgbEncoded");
        check(has_row_warning(r, 0, pp::WarningKind::GrayToRgbEncoded),
              "gray-mixed/png-shared-warning",
              "png 支持灰度但被共享变换升到 RGB → 应报 GrayToRgbEncoded(shared)");
        bool shared_text = false;
        for (const pp::Warning &w : r.outputs[0].warnings) {
            if (w.kind == pp::WarningKind::GrayToRgbEncoded &&
                w.detail.find("shared color transform") != std::string::npos) {
                shared_text = true;
            }
        }
        check(shared_text, "gray-mixed/png-shared-detail",
              "png 行告警应注明 shared color transform");
        // color 是全局步骤（D5）：任一 target 需要 RGB → 全体共享 RGB 像素
        check(read_info(r.outputs[0].out).channels == 3, "gray-mixed/png-rgb",
              "全局 sRGB 变换后 png 也应是 3 通道");
        check(read_info(r.outputs[1].out).channels == 3, "gray-mixed/webp-rgb", "webp 应为 3 通道");
    }
    {
        // (4b) 单输出不触发共享升维告警：灰度 → png（支持灰度）应保持 1 通道且无告警
        RunSpec s;
        s.format = "png";
        s.bitdepth = 8;
        const pp::FileResult r = run_spec(base / "gray8.png", corpus, tmp / "gray-png-only", s);
        check(r.ok, "gray-single/ok", r.error);
        check(!has_warning(r, pp::WarningKind::GrayToRgbEncoded), "gray-single/no-warning",
              "单输出 + 支持灰度的格式不应报 GrayToRgbEncoded（0.2 语义）");
        check(read_info(r.out).channels == 1, "gray-single/1ch", "应保持 1 通道");
    }
    {
        // (5) 原地 flatten 的像素等价性（§4.3 内存纪律 + 底色规则沿 v0.2）：
        //     rgba8.png → BMP（无损、不支持 alpha）与测试内独立计算的**白底合成**逐位比较 ——
        //     证明"工作缓冲内原地合成"与 0.2 的 dst
        //     缓冲合成结果逐位一致（既无第三帧，也没改像素）。
        RunSpec s;
        s.format = "bmp";
        s.bitdepth = 24;
        const pp::FileResult r = run_spec(base / "rgba8.png", corpus, tmp / "flat-exact", s);
        check(r.ok, "flatten/exact-ok", r.error);
        int iw = 0, ih = 0, ic = 0;
        const std::vector<float> in = read_pixels(base / "rgba8.png", iw, ih, ic);
        int ow = 0, oh = 0, oc = 0;
        const std::vector<float> out = read_pixels(r.out, ow, oh, oc);
        check(ic == 4 && oc == 3 && iw == ow && ih == oh, "flatten/exact-geometry",
              "in=" + std::to_string(ic) + "ch " + std::to_string(iw) + "x" + std::to_string(ih) +
                  " out=" + std::to_string(oc) + "ch " + std::to_string(ow) + "x" +
                  std::to_string(oh));
        std::size_t diff = 0;
        if (ic == 4 && oc == 3 && iw == ow && ih == oh) {
            const float bg = 1.0f; // §3.2 flatten_gray 默认白底（v0.2 语义）
            for (int y = 0; y < ih; ++y) {
                for (int x = 0; x < iw; ++x) {
                    const std::size_t si = (static_cast<std::size_t>(y) * iw + x) * 4;
                    const std::size_t di = (static_cast<std::size_t>(y) * iw + x) * 3;
                    const float a = std::clamp(in[si + 3], 0.0f, 1.0f);
                    for (int c = 0; c < 3; ++c) {
                        const float want =
                            in[si + static_cast<std::size_t>(c)] * a + bg * (1.0f - a);
                        if (std::lround(want * 255.0f) !=
                            std::lround(out[di + static_cast<std::size_t>(c)] * 255.0f)) {
                            ++diff;
                        }
                    }
                }
            }
        }
        check(diff == 0, "flatten/exact-pixels",
              std::to_string(diff) + " of " +
                  std::to_string(static_cast<std::size_t>(ow) * oh * 3) + " samples differ");
    }

    // ---- AC. EXIF 方向矩阵 2–8（§5.3；复核项 1 的 orient 实现修订后逐方向像素验证）----
    // 每个方向都与 EXIF 2.32 的映射逐位比对（16×8 的确定性梯度 TIFF + Orientation 标签），
    // 覆盖 5（transpose）/7（rotate180∘transpose）这两条被重写的两段路径。
    {
        const int W = 16, H = 8;
        for (int o = 2; o <= 8; ++o) {
            const std::string tag = "orient-matrix/" + std::to_string(o);
            const fs::path src = tmp / ("orient-src-" + std::to_string(o) + ".tif");
            if (!write_oriented_tiff(src, W, H, o)) {
                check(false, tag + "/setup", "cannot synthesize the oriented fixture");
                continue;
            }
            const pp::ProbeOutcome po = pp::probe_file(src);
            check(po.first_spec.has_value() && pp::orientation_from_spec(*po.first_spec) == o,
                  tag + "/fixture", "synthesized TIFF lost its Orientation tag");
            RunSpec s;
            s.format = "png";
            s.bitdepth = 8;
            s.rotate = true;
            const pp::FileResult r =
                run_spec(src, tmp, tmp / ("orient-out-" + std::to_string(o)), s);
            check(r.ok, tag + "/ok", r.error);
            int iw = 0, ih = 0, ic = 0;
            const std::vector<float> in = read_pixels(src, iw, ih, ic);
            int ow = 0, oh = 0, oc = 0;
            const std::vector<float> out = read_pixels(r.out, ow, oh, oc);
            const int ew = (o >= 5) ? H : W;
            const int eh = (o >= 5) ? W : H;
            check(iw == W && ih == H && ic == 3 && ow == ew && oh == eh && oc == 3,
                  tag + "/geometry",
                  "in=" + std::to_string(iw) + "x" + std::to_string(ih) +
                      " out=" + std::to_string(ow) + "x" + std::to_string(oh));
            std::size_t diff = 0;
            if (iw == W && ih == H && ic == 3 && ow == ew && oh == eh && oc == 3) {
                for (int y = 0; y < oh; ++y) {
                    for (int x = 0; x < ow; ++x) {
                        int sx = 0, sy = 0;
                        switch (o) {
                        case 2: // 水平镜像
                            sx = W - 1 - x;
                            sy = y;
                            break;
                        case 3: // 旋转 180°
                            sx = W - 1 - x;
                            sy = H - 1 - y;
                            break;
                        case 4: // 垂直镜像
                            sx = x;
                            sy = H - 1 - y;
                            break;
                        case 5: // 主对角线镜像（transpose）
                            sx = y;
                            sy = x;
                            break;
                        case 6: // 顺时针 90°
                            sx = y;
                            sy = H - 1 - x;
                            break;
                        case 7: // 副对角线镜像（rotate180∘transpose）
                            sx = W - 1 - y;
                            sy = H - 1 - x;
                            break;
                        case 8: // 顺时针 270°
                            sx = W - 1 - y;
                            sy = x;
                            break;
                        default:
                            break;
                        }
                        for (int c = 0; c < 3; ++c) {
                            const float want = in[(static_cast<std::size_t>(sy) * W + sx) * 3 + c];
                            const float got = out[(static_cast<std::size_t>(y) * ow + x) * 3 + c];
                            if (std::lround(want * 255.0f) != std::lround(got * 255.0f)) {
                                ++diff;
                            }
                        }
                    }
                }
            }
            check(diff == 0, tag + "/pixels", std::to_string(diff) + " samples differ");
        }
    }

    // ---- AD. 一源多输出撞同一 desired（§4.4；复核项 2）：逐输出独立 rename，不互相覆盖 ----
    {
        pp::ParamSet lossless_params;
        lossless_params["__lossless"] = true;
        RunSpec s;
        s.format = "webp";
        s.backend = "libwebp";
        s.tech = "lossy";
        s.bitdepth = 8;
        s.conflict = pp::ConflictPolicy::Rename;
        s.extra_outputs.push_back(out_spec("webp", "libwebp", "lossless", 8, lossless_params));
        const pp::FileResult r = run_spec(base / "rgb8.png", corpus, tmp / "dup-target", s);
        check(r.ok, "dup-target/ok", r.error);
        check(r.outputs.size() == 2 && r.outputs[0].out != r.outputs[1].out,
              "dup-target/distinct-paths", "两个输出必须落到不同路径（不得互相覆盖）");
        check(r.outputs[0].out == tmp / "dup-target" / "base" / "rgb8.webp" &&
                  r.outputs[1].out == tmp / "dup-target" / "base" / "rgb8 (1).webp",
              "dup-target/rename-sequence",
              "got " + r.outputs[0].out.string() + " + " + r.outputs[1].out.string());
        std::error_code ec;
        check(fs::exists(r.outputs[0].out, ec) && fs::exists(r.outputs[1].out, ec),
              "dup-target/both-written", "两个产物都应落盘");
        check(r.out_bytes == r.outputs[0].out_bytes + r.outputs[1].out_bytes, "dup-target/bytes",
              std::to_string(r.out_bytes) + " != " + std::to_string(r.outputs[0].out_bytes) + "+" +
                  std::to_string(r.outputs[1].out_bytes));
    }

    if (g_failed == 0) {
        std::printf("test_pipeline_contract: all checks passed\n");
    } else {
        std::printf("test_pipeline_contract: %d check(s) failed\n", g_failed);
    }
    return g_failed;
}
