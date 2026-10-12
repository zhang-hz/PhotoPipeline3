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

#include "codecs/enc_common.h" // M7-WP8/B.7（R-156/162/165）：族公共面单源
#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/simd/simd.h" // M4-W5-T19：交织 + 量化热路径（§11.2）
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
// M7-WP8/B.7（R-156/162）：encode_error / ms_since 收编进 codecs/enc_common（单源）；
// 错误收尾口径 = 失败不落 error 级日志（归 pipeline 的 `encode failed` 行，消双记），
// Timing 由 encode() 外壳统一补填（成败皆填）。

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
// M7-WP8/B.7（R-156）：实现收编 enc_common::warn_unknown_params（键表仍由本编码器持有）。

// ------------------------------------------------------------------ pixels --
// M7-WP8/B.7（R-156/165）：Raster/fetch_raster 收编 enc_common（整帧 = img.roi() 单口径）。

// E6: standard rounding, no dithering, clamp to [0,1].
//
// M4-W5-T19 接线（design §11.2 热路径表第 2/5 行）：逐行「通道选择/展开（interleave）→
// float→u8（quantize8）」改由 **pp::simd::interleave** + **pp::simd::quantize8**
// （唯一运行期分派层）承载。原 `to_u8` 标量表达式不再保留第二副本：语义文书 =
// core/simd/simd.h 的 quantize 契约块，且 test_simd 的 `quantize8/matches-to_u8` 断言
// **独立复刻**了该表达式做对照。通道规则与 simd::interleave 一致：
//   gray（nch ≤ 2）→ comps = 1（取 src[0]，与原 `to_u8(px[0])` 同口径）；
//   nch = 3 → RGB 直通；nch = 4 → 丢弃 alpha（原循环同样只取前 3 通道）。

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
        // R-162 统一收尾口径（M7-WP8/B.7）：成败皆填 Timing（失败路径由本外壳补填）；
        // 错误级日志归 pipeline（消 heif/oiio 旧双记口径），本类只返回 EncodeResult。
        const auto t0 = std::chrono::steady_clock::now();
        EncodeResult res;
        try {
            res = encode_impl(req);
        } catch (const std::exception &e) {
            res = enc::encode_error(std::string("jpegli: internal error: ") + e.what());
        } catch (...) {
            res = enc::encode_error("jpegli: unknown internal error");
        }
        if (res.t.encode_ms <= 0.0)
            enc::finish_encode(res, t0);
        return res;
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
            return enc::encode_error("jpegli: out_bitdepth " +
                                     std::to_string(req.target.out_bitdepth) +
                                     " is not supported (jpeg is 8-bit only)");
        }
        const JpegliParams p = read_params(req.target.params);
        // §3.8: jpegli does not implement arithmetic coding (JPEGLI_ERROR).
        if (p.arith_code) {
            return enc::encode_error("arith_code is not supported by jpegli");
        }

        enc::Raster r;
        std::string err;
        if (!enc::fetch_raster(req.img, r, err)) {
            return enc::encode_error("jpegli: " + err);
        }
        enc::warn_unknown_params(req.target.params, "jpegli", kKnownKeys);

        // JPEG has no alpha channel: the caller already flattened 4/2-channel
        // input (E4, §5.5). 1/2-channel input is encoded as grayscale.
        // volatile: both are live across setjmp (ISO C clobbering rule).
        const volatile bool gray = r.channels <= 2;
        const volatile int comps = gray ? 1 : 3;
        assert(comps == 1 || comps == 3);

        // Constructed before setjmp so no destructor can be skipped by longjmp.
        std::vector<JSAMPLE> row(static_cast<std::size_t>(r.width) *
                                 static_cast<std::size_t>(comps));
        // M4-W5-T19：展开行缓冲（interleave 的目的面，L1 驻留）+ 量化输出面 = JSAMPLE 行。
        std::vector<float> rowf(static_cast<std::size_t>(r.width) *
                                static_cast<std::size_t>(comps));

        jpeg_compress_struct cinfo{};
        JpegliErrorMgr jerr{};
        jerr.msg[0] = '\0';
        cinfo.err = jpegli_std_error(&jerr.pub);
        jerr.pub.error_exit = jpegli_error_exit;
        jerr.pub.output_message = jpegli_output_message;

        // R-087：目标缓冲经**堆侧影子**跟踪 —— longjmp 兜底路径也能释放。
        // ISO C 规定 longjmp 返回后"setjmp 之后被修改的非 volatile 局部变量"值不确定；
        // 缓冲指针正是这种变量（由 jpegli_mem_dest 经指针回填，且回填发生在可能 longjmp
        // 的写扫描线/收尾阶段），旧实现的 longjmp 分支拿不到它 ⇒ 缓冲泄漏。
        // 改 volatile 又过不了 `&mem`（`unsigned char *volatile` ≠ `unsigned char **`）。
        // 故 setjmp 之前在堆上建影子结构：局部指针 `mem_dest` 此后**不再重新赋值**
        // （longjmp 后仍确定有效），库回填的是 `&mem_dest->p / &mem_dest->n`（堆内存
        // 不受 longjmp 规则影响）。
        struct MemDestShadow {
            unsigned char *p = nullptr;
            unsigned long n = 0;
        };
        MemDestShadow *const mem_dest = new MemDestShadow();

        // volatile: modified after setjmp, read on the longjmp path (ISO C rule).
        volatile bool created = false;
        if (setjmp(jerr.jump) != 0) {
            if (created) {
                jpegli_destroy_compress(&cinfo);
            }
            std::free(mem_dest->p); // R-087：兜底释放（旧实现此处泄漏）
            delete mem_dest;
            return enc::encode_error(std::string("jpegli: ") + jerr.msg);
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

        // R-087：mem 缓冲由 setjmp 之前的堆侧影子 mem_dest 跟踪（见上）。
        jpegli_mem_dest(&cinfo, &mem_dest->p, &mem_dest->n);
        jpegli_start_compress(&cinfo, TRUE);

        if (!req.meta.icc_profile.empty()) {
            // E5: ICC APP2 marker, written before the first scanline.
            jpegli_write_icc_profile(&cinfo,
                                     reinterpret_cast<const JOCTET *>(req.meta.icc_profile.data()),
                                     static_cast<unsigned int>(req.meta.icc_profile.size()));
        }

        for (int y = 0; y < r.height; ++y) {
            // R-163：`EncodeRequest::cancelled` 真实消费 —— 行级提前退出（写盘前无半成品）。
            if (req.cancelled && req.cancelled()) {
                jpegli_abort_compress(&cinfo);
                jpegli_destroy_compress(&cinfo);
                std::free(mem_dest->p);
                delete mem_dest;
                return enc::encode_error("jpegli: encode cancelled");
            }
            const float *src = r.px.data() + static_cast<std::size_t>(y) *
                                                 static_cast<std::size_t>(r.width) *
                                                 static_cast<std::size_t>(r.channels);
            // 展开（gray → 1 通道；RGB(A) → 3 通道）→ 量化到 JSAMPLE 行
            pp::simd::interleave(src, r.channels, rowf.data(), comps,
                                 static_cast<std::size_t>(r.width));
            pp::simd::quantize8(rowf.data(), row.data(),
                                static_cast<std::size_t>(r.width) *
                                    static_cast<std::size_t>(comps));
            JSAMPROW rows[1] = {row.data()};
            if (jpegli_write_scanlines(&cinfo, rows, 1) != 1) {
                jpegli_abort_compress(&cinfo);
                jpegli_destroy_compress(&cinfo);
                std::free(mem_dest->p);
                delete mem_dest;
                return enc::encode_error("jpegli: short scanline write");
            }
            // W1-T6：真实行级进度（§7.2 encode 段的 real 面；ProgressMux 负责节流/汇流）。
            // R-086：**0.95 封顶** —— 喂入循环之外还有压缩期（渐进 JPEG 的实际压缩在
            // jpegli_finish_compress 才发生），行循环内不得直达 1.0（与合成路径
            // progress.cpp 的 0.95 口径一致）。
            // R-087：回调包 try/catch —— 异常不得穿越 setjmp 区间（否则跳过 destroy/释放）。
            if (progress) {
                try {
                    progress(0.95f * (static_cast<float>(y + 1) / static_cast<float>(r.height)));
                } catch (...) {
                    // 进度回调不参与编码成败（§3.1 "尽力而为"）：吞掉并继续。
                    (void)0; // 吞掉是设计行为（R-087）；空语句仅为「有意吞掉」的显式化
                }
            }
        }
        jpegli_finish_compress(&cinfo);
        jpegli_destroy_compress(&cinfo);

        unsigned char *const mem = mem_dest->p;
        const unsigned long mem_size = mem_dest->n;
        delete mem_dest; // 指针本身不再使用（longjmp 已不可能：jpegli 生命周期已结束）

        if (mem == nullptr || mem_size == 0) {
            if (mem != nullptr) {
                std::free(mem);
            }
            return enc::encode_error("jpegli: empty output");
        }

        const std::string path = req.target.out_path.string();
        // R-003（M7-WP2 接线）：内存型产物走 enc::write_file（= fsops::write_atomic_file 的
        // 唯一转发点，M7-WP8/B.7 落盘单口径）—— 字节数校验 + 同目录 tmp→rename 原子落盘、
        // 失败清场；不再直写最终路径（半成品驻留 / Overwrite 先截断旧好文件两坑根治）。
        {
            std::string werr;
            const std::string_view payload(reinterpret_cast<const char *>(mem),
                                           static_cast<std::size_t>(mem_size));
            const bool ok = enc::write_file(req.target.out_path, payload, &werr);
            std::free(mem);
            if (!ok)
                return enc::encode_error("jpegli: failed to write output file: " + path + ": " +
                                         werr);
        }

        EncodeResult res;
        res.bytes = static_cast<uint64_t>(mem_size);
        res.t.encode_ms = enc::ms_since(t0);
        res.progress_reported =
            static_cast<bool>(progress); // §3.1：真实行级（回调存在即已逐行上报）
        return res;
    }
};

std::unique_ptr<IEncoder> make_jpegli() { return std::make_unique<JpegliEncoder>(); }

} // namespace

PP_REGISTER_ENCODER("jpeg", "jpegli", make_jpegli);

} // namespace pp

// R-051（M7-WP2）：原 `pp_link_encoder_jpegli` 链接锚点已删 —— 自注册由
// pp_core_registered 的 whole-archive 链接形态保证（CMakeLists §2b），不再需要
// 人为引用符号；注册缺失的诊断由 encoders.cpp 的期望键断言承担。
