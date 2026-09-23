// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T6 — JPEG encoder (jpegli).
//
// Contract: docs/m1-tasks.md §3.8 (IEncoder contract E1–E9 + jpegli parameter
// mapping table) / §4.6 / §5 / §7 (M0 facts); E8 in its M1-T6 revised form:
// failures return EncodeResult{bytes = 0, error = "<reason>"} and never let an
// exception escape (the additive `error` field is landed by T7).
// All jpegli API shapes below were verified against the installed headers
// vcpkg_installed/x64-linux/include/jpegli/encode.h (see api-deltas in the
// M1-T6 report: jpegli_enable_adaptive_quantization / jpegli_use_standard_quant_tables
// / jpegli_mem_dest / jpegli_write_icc_profile; no jpegli_set_optimize_coding).
//
// E2: jpegli is single-threaded by construction (no thread controls in its API).
// E3: JPEG output is 8-bit only; any other out_bitdepth is an error.
// E5: ICC goes through jpegli_write_icc_profile (APP2 marker).
// E7: JPEG carries no EXIF/XMP (the §3.7 exiv2 post-write path owns them).

#include <jpegli/encode.h>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>

#include <cassert>
#include <chrono>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/types.h"

namespace pp {
namespace {

constexpr char kLogFile[] = "enc_jpegli.cpp";
constexpr std::string_view kFormatId = "jpeg";

// Recognised parameter keys (E9; §3.8 jpegli mapping table, 15 keys).
constexpr std::string_view kKnownKeys[] = {
    "quality_mode",     "distance",        "quality",
    "chroma",           "progressive",     "optimize_coding",
    "arith_code",       "restart_in_rows", "dct_method",
    "smoothing_factor", "xyb_mode",        "adaptive_quantization",
    "std_quant_tables", "psnr_target",     "cicp_transfer_function",
};

// E8 (revised): a failed encode yields bytes == 0 plus an English error string.
EncodeResult encode_error(std::string msg) {
    EncodeResult res;
    res.bytes = 0;
    res.error = std::move(msg);
    return res;
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// --------------------------------------------------------------- parameters --
// E9: every value is read through param_*; a missing key falls back to the
// frozen table default (src/core/format_tables.cpp, jpeg/jpegli/dct).
struct JpegliParams {
    std::string quality_mode;
    double distance = 1.0;
    int quality = 90;
    std::string chroma;
    bool progressive = true;
    bool optimize_coding = true;
    bool arith_code = false;
    int restart_in_rows = 0;
    int dct_method = 0; // JDCT_ISLOW
    int smoothing_factor = 0;
    bool xyb_mode = false;
    bool adaptive_quantization = true;
    bool std_quant_tables = false;
    double psnr_target = 0.0;
    int cicp_transfer_function = 2;
};

JpegliParams read_params(const ParamSet &s) {
    JpegliParams p;
    p.quality_mode = param_str(s, "quality_mode", "distance");
    p.distance = param_float(s, "distance", 1.0);
    p.quality = static_cast<int>(param_int(s, "quality", 90));
    p.chroma = param_str(s, "chroma", "444");
    p.progressive = param_bool(s, "progressive", true);
    p.optimize_coding = param_bool(s, "optimize_coding", true);
    p.arith_code = param_bool(s, "arith_code", false);
    p.restart_in_rows = static_cast<int>(param_int(s, "restart_in_rows", 0));
    p.dct_method = static_cast<int>(param_int(s, "dct_method", 0));
    p.smoothing_factor = static_cast<int>(param_int(s, "smoothing_factor", 0));
    p.xyb_mode = param_bool(s, "xyb_mode", false);
    p.adaptive_quantization = param_bool(s, "adaptive_quantization", true);
    p.std_quant_tables = param_bool(s, "std_quant_tables", false);
    p.psnr_target = param_float(s, "psnr_target", 0.0);
    p.cicp_transfer_function = static_cast<int>(param_int(s, "cicp_transfer_function", 2));
    return p;
}

// E9: unrecognised keys are ignored (never fatal) and recorded as a warning log line.
// M2-T4 NOTE(design): the user-visible report for an unknown *configuration* key belongs to the
// configuration-validation channel (§2.7 cross_validate, M2-T5), not to EncodeResult.warnings —
// WarningKind (M0 PP-FROZEN types.h) carries per-file processing facts and, per the M1 ruling,
// deliberately has no "configuration defect" member. This log line stays as the encoder-side
// evidence; see api-deltas in the M2-T4 report.
void warn_unknown_params(const ParamSet &s) {
    for (const auto &[key, value] : s) {
        (void)value;
        if (key.rfind("__", 0) == 0) {
            continue; // reserved keys (§3.4 convention)
        }
        bool known = false;
        for (std::string_view k : kKnownKeys) {
            if (key == k) {
                known = true;
                break;
            }
        }
        if (!known) {
            log_warn("Encode", kLogFile, "unknown parameter ignored",
                     {{"encoder", "jpegli"}, {"key", key}});
        }
    }
}

// ------------------------------------------------------------------ pixels --
struct Raster {
    int width = 0, height = 0, channels = 0;
    std::vector<float> px; // interleaved float32
};

bool fetch_raster(const OIIO::ImageBuf &img, Raster &out, std::string &err) {
    if (!img.initialized()) {
        err = "input image buffer is not initialized";
        return false;
    }
    const OIIO::ImageSpec &spec = img.spec();
    const int channels = spec.nchannels;
    if (channels < 1 || channels > 4) {
        err = "unsupported channel count: " + std::to_string(channels) + " (expected 1..4)";
        return false;
    }
    const OIIO::ROI roi = img.roi();
    const int w = static_cast<int>(roi.width());
    const int h = static_cast<int>(roi.height());
    if (w <= 0 || h <= 0) {
        err = "empty image region";
        return false;
    }
    out.width = w;
    out.height = h;
    out.channels = channels;
    out.px.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                      static_cast<std::size_t>(channels),
                  0.0f);
    const OIIO::span<std::byte> bytes(reinterpret_cast<std::byte *>(out.px.data()),
                                      out.px.size() * sizeof(float));
    if (!img.get_pixels(roi, OIIO::TypeDesc::FLOAT, bytes)) {
        err = img.geterror();
        if (err.empty()) {
            err = "ImageBuf::get_pixels failed";
        }
        return false;
    }
    return true;
}

// E6: standard rounding, no dithering, clamp to [0,1].
uint8_t to_u8(float v) {
    if (!(v > 0.0f)) {
        return 0; // also catches NaN
    }
    if (v >= 1.0f) {
        return 255;
    }
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

// ---------------------------------------------------------------- libjpeg --
// libjpeg exits the process through error_exit() by default; the longjmp below
// keeps a bad parameter or an allocation failure inside the encoder (E8).
struct JpegliErrorMgr {
    jpeg_error_mgr pub;
    jmp_buf jump;
    char msg[JMSG_LENGTH_MAX];
};

void jpegli_error_exit(j_common_ptr cinfo) {
    auto *mgr = reinterpret_cast<JpegliErrorMgr *>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, mgr->msg);
    std::longjmp(mgr->jump, 1);
}

void jpegli_output_message(j_common_ptr cinfo) {
    auto *mgr = reinterpret_cast<JpegliErrorMgr *>(cinfo->err);
    (*cinfo->err->format_message)(cinfo, mgr->msg);
    log_warn("Encode", kLogFile, "jpegli message", {{"text", mgr->msg}});
}

class JpegliEncoder final : public IEncoder {
public:
    const FormatDef &format() const override {
        static const FormatDef *def = find_format(kFormatId);
        assert(def != nullptr);
        return *def;
    }

    EncodeResult encode(const EncodeRequest &req) override {
        // E8: no exception crosses the IEncoder boundary.
        try {
            return encode_impl(req);
        } catch (const std::exception &e) {
            return encode_error(std::string("jpegli: internal error: ") + e.what());
        } catch (...) {
            return encode_error("jpegli: unknown internal error");
        }
    }

private:
    static EncodeResult encode_impl(const EncodeRequest &req) {
        const auto t0 = std::chrono::steady_clock::now();
        // W1-T6 接线（design §3.1 / §7.2）：jpegli = **真实行级进度** —— 每条
        // `jpegli_write_scanlines` 成功后上报 rows/height，故 `progress_reported=true`
        // （进度事件由 ProgressMux 节流汇流，本处逐行上报不落盘、不打印）。
        // —— E3 线程映射（§3.1 正文，W1-T7 落地）——
        //   §3.1：jpegli = **无内部线程**（本库公开 C API 里没有任何线程/并行旋钮；编码是
        //   单线程行循环）→ 映射义务 = "如实记录 E 不生效"：
        //     E == 1（深队列分支，= 0.2 行为）→ 不产生任何日志；E > 1（浅队列分支，调度器
        //     已把 E 放大到 2..16）→ 一条 info 行注明本编码器不吃 E（如实记录，不静默）。
        const ProgressFn &progress = req.progress;
        if (req.encode_threads > 1) {
            log_info(
                "Encode", kLogFile, "encoder has no internal threading; encode_threads ignored",
                {{"encoder", "jpegli"}, {"encode_threads", std::to_string(req.encode_threads)}});
        }
        if (req.target.out_bitdepth != 8) {
            return encode_error("jpegli: out_bitdepth " + std::to_string(req.target.out_bitdepth) +
                                " is not supported (jpeg is 8-bit only)");
        }
        const JpegliParams p = read_params(req.target.params);
        // §3.8: jpegli does not implement arithmetic coding (JPEGLI_ERROR).
        if (p.arith_code) {
            return encode_error("arith_code is not supported by jpegli");
        }

        Raster r;
        std::string err;
        if (!fetch_raster(req.img, r, err)) {
            return encode_error("jpegli: " + err);
        }
        warn_unknown_params(req.target.params);

        // JPEG has no alpha channel: the caller already flattened 4/2-channel
        // input (E4, §5.5). 1/2-channel input is encoded as grayscale.
        // volatile: both are live across setjmp (ISO C clobbering rule).
        const volatile bool gray = r.channels <= 2;
        const volatile int comps = gray ? 1 : 3;
        assert(comps == 1 || comps == 3);

        // Constructed before setjmp so no destructor can be skipped by longjmp.
        std::vector<JSAMPLE> row(static_cast<std::size_t>(r.width) *
                                 static_cast<std::size_t>(comps));

        jpeg_compress_struct cinfo{};
        JpegliErrorMgr jerr{};
        jerr.msg[0] = '\0';
        cinfo.err = jpegli_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegli_error_exit;
        jerr.pub.output_message = jpegli_output_message;

        // volatile: modified after setjmp, read on the longjmp path (ISO C rule).
        volatile bool created = false;
        if (setjmp(jerr.jump) != 0) {
            if (created) {
                jpegli_destroy_compress(&cinfo);
            }
            return encode_error(std::string("jpegli: ") + jerr.msg);
        }

        jpegli_create_compress(&cinfo);
        created = true;
        cinfo.image_width = static_cast<JDIMENSION>(r.width);
        cinfo.image_height = static_cast<JDIMENSION>(r.height);
        cinfo.input_components = comps;
        cinfo.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;

        // Switches that must precede jpegli_set_defaults (jpegli/encode.h:130-138,156-157).
        if (p.xyb_mode) {
            jpegli_set_xyb_mode(&cinfo);
        }
        jpegli_set_cicp_transfer_function(&cinfo, p.cicp_transfer_function);
        if (p.std_quant_tables) {
            jpegli_use_standard_quant_tables(&cinfo);
        }
        jpegli_set_defaults(&cinfo);

        // quality_mode selects exactly one of the two quality controls (§3.8).
        if (p.quality_mode == "quality") {
            jpegli_set_quality(&cinfo, p.quality, TRUE);
        } else {
            jpegli_set_distance(&cinfo, static_cast<float>(p.distance), TRUE);
        }
        if (p.psnr_target > 0.0) {
            // Reference defaults: tolerance 0.01, min 0.1, max 25.0 (lib/jpegli/encode.cc:84-87).
            jpegli_set_psnr(&cinfo, static_cast<float>(p.psnr_target), 0.01f, 0.1f, 25.0f);
        }
        jpegli_set_progressive_level(&cinfo, p.progressive ? 2 : 0);
        // §3.8: progressive > 0 forces optimize_coding on (table tooltip) — there
        // is no jpegli_set_optimize_coding(), so the cinfo field is written.
        cinfo.optimize_coding = (p.progressive || p.optimize_coding) ? TRUE : FALSE;
        cinfo.restart_in_rows = p.restart_in_rows;
        cinfo.dct_method = static_cast<J_DCT_METHOD>(p.dct_method);
        cinfo.smoothing_factor = p.smoothing_factor;
        jpegli_enable_adaptive_quantization(&cinfo, p.adaptive_quantization ? TRUE : FALSE);

        // Chroma subsampling on the luma component (§3.8 table):
        // 444 → 1,1 | 440 → 1,2 | 422 → 2,1 | 420 → 2,2 (jpeglib.h comp_info[]).
        if (!gray) {
            int h_samp = 1;
            int v_samp = 1;
            if (p.chroma == "440") {
                v_samp = 2;
            } else if (p.chroma == "422") {
                h_samp = 2;
            } else if (p.chroma == "420") {
                h_samp = 2;
                v_samp = 2;
            }
            cinfo.comp_info[0].h_samp_factor = h_samp;
            cinfo.comp_info[0].v_samp_factor = v_samp;
        }

        unsigned char *mem = nullptr;
        unsigned long mem_size = 0;
        jpegli_mem_dest(&cinfo, &mem, &mem_size);
        jpegli_start_compress(&cinfo, TRUE);

        if (!req.meta.icc_profile.empty()) {
            // E5: ICC APP2 marker, written before the first scanline.
            jpegli_write_icc_profile(&cinfo,
                                     reinterpret_cast<const JOCTET *>(req.meta.icc_profile.data()),
                                     static_cast<unsigned int>(req.meta.icc_profile.size()));
        }

        for (int y = 0; y < r.height; ++y) {
            const float *src = r.px.data() + static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(r.width) *
                                                 static_cast<std::size_t>(r.channels);
            for (int x = 0; x < r.width; ++x) {
                const float *px =
                    src + static_cast<std::size_t>(x) * static_cast<std::size_t>(r.channels);
                if (gray) {
                    row[static_cast<std::size_t>(x)] = to_u8(px[0]);
                } else {
                    row[static_cast<std::size_t>(x) * 3 + 0] = to_u8(px[0]);
                    row[static_cast<std::size_t>(x) * 3 + 1] = to_u8(px[1]);
                    row[static_cast<std::size_t>(x) * 3 + 2] = to_u8(px[2]);
                }
            }
            JSAMPROW rows[1] = {row.data()};
            if (jpegli_write_scanlines(&cinfo, rows, 1) != 1) {
                jpegli_abort_compress(&cinfo);
                jpegli_destroy_compress(&cinfo);
                if (mem != nullptr) {
                    std::free(mem);
                }
                return encode_error("jpegli: short scanline write");
            }
            // W1-T6：真实行级进度（§7.2 encode 段的 real 面；ProgressMux 负责节流/汇流）
            if (progress)
                progress(static_cast<float>(y + 1) / static_cast<float>(r.height));
        }
        jpegli_finish_compress(&cinfo);
        jpegli_destroy_compress(&cinfo);

        if (mem == nullptr || mem_size == 0) {
            if (mem != nullptr) {
                std::free(mem);
            }
            return encode_error("jpegli: empty output");
        }

        const std::string path = req.target.out_path.string();
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        if (fp == nullptr) {
            std::free(mem);
            return encode_error("jpegli: cannot open output file: " + path);
        }
        const std::size_t written = std::fwrite(mem, 1, mem_size, fp);
        const int close_rc = std::fclose(fp);
        std::free(mem);
        if (written != mem_size || close_rc != 0) {
            return encode_error("jpegli: failed to write output file: " + path);
        }

        EncodeResult res;
        res.bytes = static_cast<uint64_t>(mem_size);
        res.t.encode_ms = ms_since(t0);
        res.progress_reported =
            static_cast<bool>(progress); // §3.1：真实行级（回调存在即已逐行上报）
        return res;
    }
};

std::unique_ptr<IEncoder> make_jpegli() { return std::make_unique<JpegliEncoder>(); }

} // namespace

PP_REGISTER_ENCODER("jpeg", "jpegli", make_jpegli);

} // namespace pp

// Link anchor: referenced by encoders.cpp so the linker pulls this (otherwise
// unreferenced) static-library member in and the registration above actually
// runs. See the note in encoders.cpp.
extern "C" void pp_link_encoder_jpegli() {}
