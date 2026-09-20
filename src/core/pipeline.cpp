// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T8: single-file pipeline (docs/m1-tasks.md §3.9 / §4.8 / §5).
//
// Stage order (frozen): probe → budget.acquire(2×frame) → decode → orient →
// color → flatten → encode → metawrite → mtime → release.
//
// Orchestration rulings baked in here:
//   * gray/ICC (§4.8): src_is_gray = channels ∈ {1,2}; gray into a format without gray
//     support gets Warning{GrayToRgbEncoded} and is transformed with an effective sRGB
//     target even when the user asked to keep the original; a format that supports gray
//     with KeepOriginal is NOT transformed at all; the ICC handed to the encoder is always
//     ColorOutcome::icc_to_embed (KeepOriginal → source ICC or nothing, T4 ruling ③).
//   * alpha (§3.5 ⑥): has_alpha = buffer layout 2/4 (GIF alpha_channel quirk safe); when the
//     target cannot carry alpha the buffer is composited onto cfg.flatten_gray and
//     Warning{AlphaFlattened} is reported.
//   * metadata (§4.8): build_plan → payloads; the Exiv2 post-write path is used for
//     meta_path == "exiv2", BMP ("none") silently drops metadata (the pipeline adds
//     Warning{MetadataDropped}), JXL/HEIF/AVIF ("jxl-box"/"libheif") are injected inside the
//     encoder via MetadataPayloads (E7). Metadata failures are never fatal.
//   * reserved key (§3.4): every effective ParamSet carries "__lossless" = cfg.lossless and
//     passes through apply_locks() before it reaches the encoder.
//
// The pixel budget is acquired *here* (not in the scheduler): run_one_file is the single-file
// entry point and owns the whole probe→…→release sequence, so an early return or a throw can
// never leak an acquisition (see the Scheduler header for the mapping to §3.10).

#include "core/pipeline.h"

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "codecs/encoders.h"
#include "core/colormanager.h"
#include "core/fsops.h"
#include "core/logger.h"
#include "core/metadata.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "decode/oiio_reader.h"

namespace pp {

// Defined in metadata.cpp (runtime capability probe; intentionally outside the frozen
// header). The strong definition of the frozen predicate below delegates to it (§4.8).
bool detail_metadata_only_supported(std::string_view format_id);

namespace {

constexpr std::string_view kStage = "pipeline";
constexpr std::string_view kFile = "pipeline.cpp";

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string fmt_double(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

// ---------------------------------------------------------------------------
// pixel helpers
// ---------------------------------------------------------------------------

bool read_float_pixels(const OIIO::ImageBuf& buf, std::vector<float>& out, std::string& err) {
    const OIIO::ImageSpec& spec = buf.spec();
    if (spec.width <= 0 || spec.height <= 0 || spec.nchannels <= 0) {
        err = "empty image buffer";
        return false;
    }
    out.assign(static_cast<std::size_t>(spec.width) * static_cast<std::size_t>(spec.height) *
                   static_cast<std::size_t>(spec.nchannels),
               0.0f);
    if (!buf.get_pixels(buf.roi(), OIIO::TypeDesc::FLOAT, out.data())) {
        err = "cannot read pixels: " + buf.geterror();
        return false;
    }
    return true;
}

// EXIF orientation 1–8 → the transform that brings the stored pixels upright.
// Mapping (EXIF 2.32 + OIIO's rotate90 = 90° clockwise, imagebufalgo.h:499-518):
//   2 flop · 3 rotate180 · 4 flip · 5 flop∘rotate90 · 6 rotate90 · 7 flip∘rotate90 · 8 rotate270
bool orient_buf(OIIO::ImageBuf& buf, int orientation, std::string& err) {
    if (orientation <= 1 || orientation > 8) return true;
    OIIO::ImageBuf tmp;
    OIIO::ImageBuf pre;
    bool ok = false;
    switch (orientation) {
        case 2: ok = OIIO::ImageBufAlgo::flop(tmp, buf); break;
        case 3: ok = OIIO::ImageBufAlgo::rotate180(tmp, buf); break;
        case 4: ok = OIIO::ImageBufAlgo::flip(tmp, buf); break;
        case 5:
            ok = OIIO::ImageBufAlgo::rotate90(pre, buf) && OIIO::ImageBufAlgo::flop(tmp, pre);
            break;
        case 6: ok = OIIO::ImageBufAlgo::rotate90(tmp, buf); break;
        case 7:
            ok = OIIO::ImageBufAlgo::rotate90(pre, buf) && OIIO::ImageBufAlgo::flip(tmp, pre);
            break;
        case 8: ok = OIIO::ImageBufAlgo::rotate270(tmp, buf); break;
        default: return true;
    }
    if (!ok) {
        err = "ImageBufAlgo orientation transform failed: " + buf.geterror();
        return false;
    }
    buf = std::move(tmp);
    return true;
}

// Composite alpha onto a constant background (§5.5). 2 channels → gray, 4 → RGB.
bool flatten_alpha(OIIO::ImageBuf& buf, float background, std::string& err) {
    const OIIO::ImageSpec& spec = buf.spec();
    const int w = spec.width, h = spec.height, ch = spec.nchannels;
    if (ch != 2 && ch != 4) {
        err = "flatten called without an alpha channel (channels=" + std::to_string(ch) + ")";
        return false;
    }
    const int out_ch = (ch == 2) ? 1 : 3;
    std::vector<float> src;
    if (!read_float_pixels(buf, src, err)) return false;
    const std::size_t npix = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    std::vector<float> dst(npix * static_cast<std::size_t>(out_ch), 0.0f);
    const float bg = std::clamp(background, 0.0f, 1.0f);
    for (std::size_t i = 0; i < npix; ++i) {
        const float a = std::clamp(src[i * static_cast<std::size_t>(ch) + (ch - 1)], 0.0f, 1.0f);
        for (int c = 0; c < out_ch; ++c) {
            dst[i * static_cast<std::size_t>(out_ch) + static_cast<std::size_t>(c)] =
                src[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] * a +
                bg * (1.0f - a);
        }
    }
    OIIO::ImageSpec ospec(w, h, out_ch, OIIO::TypeDesc::FLOAT);
    ospec.channelnames = (out_ch == 1) ? std::vector<std::string>{"Y"}
                                       : std::vector<std::string>{"R", "G", "B"};
    OIIO::ImageBuf out(ospec);
    if (!out.set_pixels(out.roi(), OIIO::TypeDesc::FLOAT, dst.data())) {
        err = "cannot store composited pixels: " + out.geterror();
        return false;
    }
    buf = std::move(out);
    return true;
}

bool is_cancelled(const std::function<bool()>& fn) { return fn && fn(); }

void merge_warnings(std::vector<Warning>& dst, const std::vector<Warning>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

// M2-T4 (#8): the metadata writer reports its failure/degradation messages through an explicit
// out-parameter as well. Merge them into the file warnings, dropping the messages the legacy
// plan.warnings channel already contributed (identical kind + detail) so the dual channel is
// never visible twice. New-only: nothing already in `dst` is removed or reordered.
void merge_writer_warnings(std::vector<Warning>& dst, const std::vector<std::string>& details) {
    for (const std::string& detail : details) {
        const bool duplicate = std::any_of(dst.begin(), dst.end(), [&detail](const Warning& w) {
            return w.kind == WarningKind::MetadataDropped && w.detail == detail;
        });
        if (!duplicate) {
            dst.push_back(Warning{WarningKind::MetadataDropped, detail});
        }
    }
}

// M2-T5 §2.7 (--dev 校验调用点): cross-field constraints the per-key predicates cannot
// express. The GUI and the `--dev` harness both block earlier, but run_one_file /
// run_metadata_only are the single-file entry points — the guard here keeps the per-file
// verdict (error = first message) identical for every caller, and the dev harness counts
// the failed file into its exit code. Empty first message = OK.
std::string first_cross_error(const RunConfig& cfg) {
    const std::vector<std::string> msgs =
        cross_validate(cfg.params, cfg.format_id, cfg.tech_id);
    return msgs.empty() ? std::string() : msgs.front();
}

}  // namespace

// ---------------------------------------------------------------------------
// §3.9 frozen predicate (strong definition overriding the weak fallback in metadata.cpp)
// ---------------------------------------------------------------------------
bool format_supports_metadata_only(std::string_view format_id) {
    return detail_metadata_only_supported(format_id);
}

// ---------------------------------------------------------------------------
// run_one_file
// ---------------------------------------------------------------------------
FileResult run_one_file(FileEntry& fe, const RunConfig& cfg, IEncoder* enc, PixelBudget* budget,
                        const std::vector<std::filesystem::path>& reserved,
                        const std::function<bool()>& cancelled,
                        const std::function<void(FileState)>& on_stage) {
    namespace fs = std::filesystem;

    FileResult res;
    res.src = fe.src;
    const Clock::time_point t_start = Clock::now();
    // Total time is stamped on every return path. NOTE: an RAII guard cannot be used here —
    // the returned FileResult is copied before the local is destroyed, so the stamp would land
    // on the discarded copy (found while running the 48MP drumbeat: per-file ms printed 0).
    const auto done = [&res, &t_start]() -> FileResult {
        res.t.total_ms = ms_since(t_start);
        return res;
    };

    const auto stage = [&on_stage](FileState s) {
        if (on_stage) on_stage(s);
    };

    // Budget guard: released on every exit path, including exceptions.
    uint64_t need_bytes = 0;
    bool budget_held = false;
    struct BudgetGuard {
        PixelBudget* b;
        uint64_t bytes;
        bool& held;
        ~BudgetGuard() {
            if (held && b) b->release(bytes);
        }
    } budget_guard{budget, 0, budget_held};

    const auto fail = [&res, &t_start](std::string msg) -> FileResult {
        res.ok = false;
        res.error = std::move(msg);
        res.t.total_ms = ms_since(t_start);
        log_error(kStage, kFile, res.error, {{"src", res.src.string()}});
        return res;
    };

    try {
        // ---- cross-field parameter constraints (§2.7, M2-T5): fail with the first message ----
        if (const std::string cross_err = first_cross_error(cfg); !cross_err.empty())
            return fail(cross_err);

        // ---- probe (§5.1): spec only, no pixels ----
        stage(FileState::Probing);
        ProbeOutcome po = probe_file(fe.src);
        fe.probe_done = true;
        if (!po.error.empty()) {
            return fail("probe failed: " + po.error);
        }
        fe.info = po.info;
        res.info = po.info;
        const OIIO::ImageSpec* spec = po.first_spec ? &*po.first_spec : nullptr;
        const int orientation = spec ? orientation_from_spec(*spec) : 1;
        std::string src_icc = spec ? icc_from_spec(*spec) : std::string();

        // EXIF summary (time / GPS / ICC / Orientation). A read failure is NOT fatal (§4.8).
        SourceMeta srcmeta = read_metadata(fe.src);
        if (!srcmeta.error.empty()) {
            log_warn(kStage, kFile, "source metadata unavailable; continuing with empty metadata",
                     {{"src", fe.src.string()}, {"error", srcmeta.error}});
        }
        if (src_icc.empty()) src_icc = srcmeta.icc;  // R13 chain: embedded ICC first
        // TODO(M2): R13's middle step (OIIO CICP / oiio:ColorSpace → lcms2 profile) is log-only
        // in M1: a source without an embedded ICC is assumed sRGB (Warning via ColorManager when
        // a transform runs). Implement the CICP⇄lcms2 mapping together with the UI color page.
        log_debug(kStage, kFile, "probe done",
                  {{"src", fe.src.string()},
                   {"size", std::to_string(fe.info.width) + "x" + std::to_string(fe.info.height)},
                   {"channels", std::to_string(fe.info.channels)},
                   {"bitdepth", std::to_string(fe.info.src_bitdepth)},
                   {"orientation", std::to_string(orientation)},
                   {"icc", src_icc.empty() ? "none" : std::to_string(src_icc.size()) + "B"},
                   {"multipage", fe.info.is_multipage ? "true" : "false"}});

        // ---- output format checks ----
        const FormatDef* fmt = find_format(cfg.format_id);
        if (!fmt) return fail("unknown output format '" + cfg.format_id + "'");
        if (std::find(fmt->bitdepths.begin(), fmt->bitdepths.end(), cfg.out_bitdepth) ==
            fmt->bitdepths.end()) {
            // §3.8 T7 ruling ①: unsupported bit depths are an explicit error, never a silent
            // downgrade (runtime probe intersection happens in the harness/UI).
            return fail("output bit depth " + std::to_string(cfg.out_bitdepth) +
                        " is not supported by format '" + fmt->id + "'");
        }

        // ---- output path (§3.2) ----
        const fs::path desired = mirror_path(fe.src, fe.base_dir, cfg.out_root, fmt->ext);
        std::string conflict_err;
        const OutputPlan out_plan = resolve_conflict(desired, cfg.conflict, reserved, conflict_err);
        if (!conflict_err.empty()) return fail("output path: " + conflict_err);
        res.out = out_plan.out_path;
        if (out_plan.skip) {
            res.skipped = true;
            log_info(kStage, kFile, "skipped: output exists",
                     {{"src", fe.src.string()}, {"out", res.out.string()}});
            return done();
        }
        {
            std::error_code ec;
            const fs::path parent = res.out.parent_path();
            if (!parent.empty()) fs::create_directories(parent, ec);
            if (ec) return fail("cannot create output directory '" + parent.string() +
                                "': " + ec.message());
        }

        // ---- pixel budget (§3.3/§3.10): 2× float32 frame (rotate/composite peak, G2) ----
        need_bytes = 2 * PixelBudget::frame_bytes(fe.info.width, fe.info.height, fe.info.channels);
        budget_guard.bytes = need_bytes;
        if (budget) {
            if (!budget->acquire(need_bytes, cancelled)) {
                if (is_cancelled(cancelled)) {
                    res.cancelled = true;
                    return done();
                }
                return fail("pixel budget exceeded: need " + std::to_string(need_bytes) +
                            " B, capacity " + std::to_string(budget->capacity()) + " B");
            }
            budget_held = true;
            // Drumbeat evidence: the 2× frame peak reservation actually made it through the pool.
            log_info(kStage, kFile, "pixel budget acquired",
                     {{"src", fe.src.string()},
                      {"need_bytes", std::to_string(need_bytes)},
                      {"capacity_bytes", std::to_string(budget->capacity())},
                      {"used_bytes", std::to_string(budget->used())},
                      {"peak_bytes", std::to_string(budget->peak())}});
        }
        if (is_cancelled(cancelled)) {
            res.cancelled = true;
            return done();
        }

        // ---- decode (§5.2) ----
        stage(FileState::Decoding);
        Clock::time_point t0 = Clock::now();
        DecodeOutcome dec = decode_float(fe.src, fe.info);
        res.t.decode_ms = ms_since(t0);
        if (!dec.error.empty()) return fail("decode failed: " + dec.error);
        merge_warnings(res.warnings, dec.warnings);  // MultipageTruncated
        OIIO::ImageBuf& buf = dec.buf;
        if (!buf.initialized()) return fail("decode produced an empty buffer");

        // ---- orient (§5.3) ----
        bool rotated = false;
        if (cfg.rotate_orientation && orientation != 1) {
            stage(FileState::Orienting);
            t0 = Clock::now();
            std::string oerr;
            if (!orient_buf(buf, orientation, oerr)) return fail("orient failed: " + oerr);
            res.t.orient_ms = ms_since(t0);
            rotated = true;
        }
        if (is_cancelled(cancelled)) {
            res.cancelled = true;
            return done();
        }

        // ---- color (§5.4 + §4.8 gray/ICC rulings) ----
        const bool src_is_gray = (fe.info.channels == 1 || fe.info.channels == 2);
        ColorTarget eff_target = cfg.color_target;
        if (src_is_gray && !fmt->supports_gray) {
            res.warnings.push_back(Warning{WarningKind::GrayToRgbEncoded,
                                           "grayscale source encoded as RGB for format '" +
                                               fmt->id + "'"});
            if (eff_target == ColorTarget::KeepOriginal) {
                // Grayscale pixels must not reach webp/heif/avif: use sRGB as the effective
                // target even though the user kept the original (§4.8).
                eff_target = ColorTarget::SRGB;
            }
        }
        std::string icc_to_embed = src_icc;
        if (eff_target != ColorTarget::KeepOriginal) {
            stage(FileState::Coloring);
            t0 = Clock::now();
            ColorOutcome co =
                ColorManager::instance().transform(buf, src_icc, src_is_gray, eff_target);
            res.t.color_ms = ms_since(t0);
            if (!co.error.empty()) return fail("color transform failed: " + co.error);
            merge_warnings(res.warnings, co.warnings);
            icc_to_embed = co.icc_to_embed;
            res.color_src = co.src_desc;
            res.color_dst = co.dst_desc;
        } else {
            // Pixels untouched: keep the source profile as-is (T4 ruling ③).
            res.color_src = src_icc.empty() ? (src_is_gray ? "assumed gray sRGB" : "assumed sRGB")
                                            : "ICC(source)";
            res.color_dst = "keep";
        }
        log_debug(kStage, kFile, "color decision",
                  {{"src", res.color_src},
                   {"dst", res.color_dst},
                   {"embed_icc", icc_to_embed.empty() ? "none"
                                                      : std::to_string(icc_to_embed.size()) + "B"}});

        // ---- flatten (§5.5, §3.5 ⑥ has_alpha semantics) ----
        {
            const int cur_ch = buf.spec().nchannels;
            const bool has_alpha = (cur_ch == 2 || cur_ch == 4);
            if (has_alpha && !fmt->supports_alpha) {
                stage(FileState::Flattening);
                t0 = Clock::now();
                std::string ferr;
                if (!flatten_alpha(buf, static_cast<float>(cfg.flatten_gray), ferr)) {
                    return fail("flatten failed: " + ferr);
                }
                res.t.flatten_ms = ms_since(t0);
                res.warnings.push_back(
                    Warning{WarningKind::AlphaFlattened,
                            "alpha composited onto background " + fmt_double(cfg.flatten_gray) +
                                " for format '" + fmt->id + "'"});
            }
        }
        if (is_cancelled(cancelled)) {
            res.cancelled = true;
            return done();
        }

        // ---- metadata plan / payloads (§3.7, §4.8) ----
        MetadataPlan meta_plan = build_plan(srcmeta, cfg.rules, fe.exception);
        if (rotated && !meta_plan.exif.empty()) {
            meta_plan.exif["Exif.Image.Orientation"] = static_cast<uint16_t>(1);  // §5.3: clear the tag
        }
        const Payloads payloads = make_payloads(meta_plan);
        MetadataPayloads meta;
        meta.exif_blob = payloads.exif_blob;  // consumed by JXL/HEIF/AVIF only (E7)
        meta.xmp_rdf = payloads.xmp_rdf;
        meta.icc_profile = icc_to_embed;

        // ---- effective parameters (§3.4 reserved key + §4.2 locks) ----
        ParamSet params = cfg.params;
        params["__lossless"] = cfg.lossless;
        const std::vector<std::string> locked =
            apply_locks(*fmt, cfg.backend_id, cfg.tech_id, cfg.lossless, params);
        if (!locked.empty()) {
            log_debug(kStage, kFile, "locked parameters forced",
                      {{"keys", std::to_string(locked.size())}});
        }
        log_debug(kStage, kFile, "effective params", {{"snapshot", snapshot_params(params)}});

        // ---- bit depth note (§5.7) ----
        if (fe.info.src_bitdepth > cfg.out_bitdepth) {
            res.warnings.push_back(
                Warning{WarningKind::DepthDowngrade,
                        "source bit depth " + std::to_string(fe.info.src_bitdepth) +
                            " -> output bit depth " + std::to_string(cfg.out_bitdepth)});
        }

        // ---- encode (§5.7) ----
        if (!enc) {
            return fail("no encoder registered for format '" + cfg.format_id + "' (backend '" +
                        cfg.backend_id + "')");
        }
        stage(FileState::Encoding);
        t0 = Clock::now();
        // EncodeRequest field order (encoder.h): img, params, out_bitdepth, meta, out_path,
        // cancelled, tech_id — the T6b additive field is appended last.
        EncodeRequest req{buf, params, cfg.out_bitdepth, meta, res.out, cancelled, cfg.tech_id};
        const EncodeResult er = enc->encode(req);
        res.t.encode_ms = ms_since(t0);
        merge_warnings(res.warnings, er.warnings);
        if (!er.error.empty() || er.bytes == 0) {
            return fail("encode failed: " +
                        (er.error.empty() ? std::string("encoder produced no data") : er.error));
        }
        res.out_bytes = er.bytes;

        // ---- metadata write (§5.8, §4.8) ----
        stage(FileState::Writing);
        t0 = Clock::now();
        std::string meta_err;
        // M2-T4 (#6/#8): the writer's explicit failure/degradation channel; plan.warnings below
        // stays byte-identical to M1 (semantic freeze) — the two are merged with dedup.
        std::vector<std::string> writer_warnings;
        if (fmt->meta_path == "exiv2") {
            meta_err = write_metadata_exiv2(res.out, meta_plan, payloads, &writer_warnings);
        }
        merge_warnings(res.warnings, meta_plan.warnings);  // legacy channel (frozen content)
        merge_writer_warnings(res.warnings, writer_warnings);
        // Defensive branch: write_metadata_exiv2() reports through the channels above and always
        // returns an empty string today, so meta_err is normally empty; a future revision may
        // return the error string (the frozen signature already allows it).
        if (!meta_err.empty()) {
            res.warnings.push_back(Warning{WarningKind::MetadataDropped, meta_err});
            log_warn(kStage, kFile, "metadata write failed (non-fatal)",
                     {{"out", res.out.string()}, {"error", meta_err}});
        } else if (fmt->meta_path == "none") {
            // BMP has no metadata container; T5 skips silently, the pipeline reports it (§4.8).
            res.warnings.push_back(Warning{WarningKind::MetadataDropped,
                                           "bmp output carries no metadata container"});
        }
        res.t.metawrite_ms = ms_since(t0);

        // ---- mtime sync (§5.9) ----
        if (cfg.rules.sync_mtime && !meta_plan.datetime_original.empty()) {
            const std::string merr = sync_file_mtime(res.out, meta_plan.datetime_original);
            if (!merr.empty()) {
                log_warn(kStage, kFile, "mtime sync failed",
                         {{"out", res.out.string()}, {"error", merr}});
            }
        }

        res.ok = true;
        log_info(kStage, kFile, "file done",
                 {{"src", fe.src.string()},
                  {"out", res.out.string()},
                  {"bytes", std::to_string(res.out_bytes)},
                  {"decode_ms", fmt_double(res.t.decode_ms)},
                  {"orient_ms", fmt_double(res.t.orient_ms)},
                  {"color_ms", fmt_double(res.t.color_ms)},
                  {"flatten_ms", fmt_double(res.t.flatten_ms)},
                  {"encode_ms", fmt_double(res.t.encode_ms)},
                  {"metawrite_ms", fmt_double(res.t.metawrite_ms)},
                  {"warnings", std::to_string(res.warnings.size())}});
        for (const Warning& w : res.warnings) {
            log_warn(kStage, kFile, "output warning",
                     {{"src", fe.src.string()}, {"detail", w.detail}});
        }
        return done();
    } catch (const std::exception& e) {
        return fail(std::string("unexpected exception: ") + e.what());
    } catch (...) {
        return fail("unexpected non-standard exception");
    }
}

// ---------------------------------------------------------------------------
// run_metadata_only (§3.9: zero re-encode, JPEG/PNG/TIFF/WebP only)
// ---------------------------------------------------------------------------
FileResult run_metadata_only(FileEntry& fe, const RunConfig& cfg,
                             const std::vector<std::filesystem::path>& reserved,
                             const std::function<bool()>& cancelled,
                             const std::function<void(FileState)>& on_stage) {
    namespace fs = std::filesystem;

    FileResult res;
    res.src = fe.src;
    const Clock::time_point t_start = Clock::now();
    // Total time is stamped on every return path. NOTE: an RAII guard cannot be used here —
    // the returned FileResult is copied before the local is destroyed, so the stamp would land
    // on the discarded copy (found while running the 48MP drumbeat: per-file ms printed 0).
    const auto done = [&res, &t_start]() -> FileResult {
        res.t.total_ms = ms_since(t_start);
        return res;
    };

    const auto stage = [&on_stage](FileState s) {
        if (on_stage) on_stage(s);
    };
    const auto fail = [&res, &t_start](std::string msg) -> FileResult {
        res.ok = false;
        res.error = std::move(msg);
        res.t.total_ms = ms_since(t_start);
        log_error(kStage, kFile, res.error, {{"src", res.src.string()}});
        return res;
    };

    try {
        // M2-T5 §2.7：交叉参数约束（仅元数据路径同样是"该文件失败，error=首条消息"）
        if (const std::string cross_err = first_cross_error(cfg); !cross_err.empty())
            return fail(cross_err);

        stage(FileState::Probing);
        ProbeOutcome po = probe_file(fe.src);
        fe.probe_done = true;
        if (!po.error.empty()) {
            return fail("probe failed: " + po.error);
        }
        fe.info = po.info;
        res.info = po.info;

        const FormatDef* fmt = find_format(cfg.format_id);
        if (!fmt) return fail("unknown output format '" + cfg.format_id + "'");
        if (!format_supports_metadata_only(fmt->id)) {
            return fail("format '" + fmt->id +
                        "' does not support metadata-only rewrite (zero re-encode)");
        }
        if (is_cancelled(cancelled)) {
            res.cancelled = true;
            return done();
        }

        // Same container: keep the source extension (with_extension "" = keep, §3.2 ①).
        const fs::path desired = mirror_path(fe.src, fe.base_dir, cfg.out_root, "");
        std::string conflict_err;
        const OutputPlan out_plan = resolve_conflict(desired, cfg.conflict, reserved, conflict_err);
        if (!conflict_err.empty()) return fail("output path: " + conflict_err);
        res.out = out_plan.out_path;
        if (out_plan.skip) {
            res.skipped = true;
            return done();
        }
        {
            std::error_code ec;
            const fs::path parent = res.out.parent_path();
            if (!parent.empty()) fs::create_directories(parent, ec);
            if (ec) return fail("cannot create output directory: " + ec.message());
        }

        SourceMeta srcmeta = read_metadata(fe.src);
        if (!srcmeta.error.empty()) {
            log_warn(kStage, kFile, "source metadata unavailable; continuing with empty metadata",
                     {{"src", fe.src.string()}, {"error", srcmeta.error}});
        }
        MetadataPlan meta_plan = build_plan(srcmeta, cfg.rules, fe.exception);
        // No orientation clearing here: the pixels are not rotated in metadata-only mode, so
        // dropping the tag would change how the image displays.

        const Payloads payloads = make_payloads(meta_plan);
        stage(FileState::Writing);
        const Clock::time_point t0 = Clock::now();
        const std::string err = rewrite_metadata_only(fe.src, res.out, meta_plan, payloads);
        res.t.metawrite_ms = ms_since(t0);
        merge_warnings(res.warnings, meta_plan.warnings);
        if (!err.empty()) return fail("metadata-only rewrite failed: " + err);

        if (cfg.rules.sync_mtime && !meta_plan.datetime_original.empty()) {
            const std::string merr = sync_file_mtime(res.out, meta_plan.datetime_original);
            if (!merr.empty()) {
                log_warn(kStage, kFile, "mtime sync failed",
                         {{"out", res.out.string()}, {"error", merr}});
            }
        }

        std::error_code ec;
        res.out_bytes = fs::file_size(res.out, ec);
        if (ec) res.out_bytes = 0;
        res.ok = true;
        log_info(kStage, kFile, "metadata-only done",
                 {{"src", fe.src.string()},
                  {"out", res.out.string()},
                  {"bytes", std::to_string(res.out_bytes)},
                  {"metawrite_ms", fmt_double(res.t.metawrite_ms)},
                  {"warnings", std::to_string(res.warnings.size())}});
        return done();
    } catch (const std::exception& e) {
        return fail(std::string("unexpected exception: ") + e.what());
    } catch (...) {
        return fail("unexpected non-standard exception");
    }
}

}  // namespace pp
