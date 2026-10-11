// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T6 — WebP encoder (libwebp).
//
// Contract: docs/m1-tasks.md §3.8 (E1–E9 + libwebp mapping table) / §4.6 / §5.
// Failures return EncodeResult{bytes = 0, error = "<reason>"} (revised E8).
//
// One IEncoder covers both WebP techs: the lossy/lossless choice travels in the
// parameter set (reserved key "__lossless", §3.4), exactly as for JXL modular.
// E2→E3: WebPConfig::thread_level = (encode_threads > 1)（§3.1 线程映射义务，W1-T7 落地；
//   E==1 → 0 = 单线程，与 0.2 逐字一致）。
// E4: 4-channel input keeps its alpha (WebPPictureImportRGBA); WebP has no
//     grayscale mode, so 1/2-channel input is widened to RGB/RGBA by channel
//     replication (no colour processing; the pipeline already warns).
// E5: ICC goes into the RIFF "ICCP" chunk through WebPMuxSetChunk.
// E7: WebP carries no EXIF/XMP (the §3.7 exiv2 post-write path owns them).

#include <webp/encode.h>
#include <webp/mux.h>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "codecs/enc_common.h" // M7-WP8/B.7(R-156/162/165/167): shared helpers
#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/simd/simd.h" // M4-W5-T19：交织 + 量化热路径（§11.2）
#include "core/types.h"

namespace pp {
namespace {

constexpr char kLogFile[] = "enc_webp.cpp";
constexpr std::string_view kFormatId = "webp";

// Recognised parameter keys (E9; §3.8 libwebp mapping table).
constexpr std::string_view kKnownKeys[] = {
    "quality",
    "sharp_yuv",
    "method",
    "preset",
    "sns_strength",
    "filter_strength",
    "autofilter",
    "pass",
    "filter_sharpness",
    "filter_type",
    "segments",
    "alpha_compression",
    "alpha_filtering",
    "alpha_quality",
    "partitions",
    "partition_limit",
    "preprocessing",
    "qmin",
    "qmax",
    "emulate_jpeg_size",
    "low_memory",
    "exact",
    "near_lossless",
    // M4-T13：显式 schema 参数（src/core/params.h 的 kLosslessParamKey）——由下方 lossless
    // 取值消费；列在此处以免被误报为未知参数。
    // PP-FROZEN(0.3.0)：本行与下方 lossless 取值行按 T13 定稿冻结（内部管道键 __lossless 保留；
    // 取值优先序 = 显式 tech_id → 显式 schema 参数 → 内部管道键）。
    "lossless",
};

// E8 (revised): a failed encode yields bytes == 0 plus an English error string.
// M7-WP8/B.7 (R-156/162): encode_error / ms_since live in codecs/enc_common (single source);
// error-level logging belongs to pipeline (no double record); Timing is filled by the
// encode() wrapper for both success and failure.

// E9: unrecognised keys are ignored (never fatal) and recorded as a warning log
// line. WarningKind (M0 PP-FROZEN types.h) has no "unknown parameter" member and
// warnings must not carry encoder failures, so the log is the faithful channel.
// M7-WP8/B.7 (R-156): implementation moved to enc_common::warn_unknown_params.

// ------------------------------------------------------------------ pixels --
// M7-WP8/B.7 (R-156/165): Raster/fetch_raster live in enc_common (img.roi() basis).

// E6: standard rounding, no dithering, clamp to [0,1].
//
// M4-W5-T19 接线（design §11.2 热路径表第 2/5 行）：本 TU 的像素循环改由
// **pp::simd::interleave**（通道展开/选择，纯数据搬运）+ **pp::simd::quantize8**
// （float → u8，唯一运行期分派层）承载。原 `to_u8` 标量表达式不再保留第二副本：
// 语义文书 = core/simd/simd.h 的 quantize 契约块，且 test_simd 的
// `quantize8/matches-to_u8` 断言**独立复刻**了该表达式做对照。
// 通道规则与 simd::interleave 的契约逐条一致（interleave 的 out_ch=3/4 展开规则
// 就是本 TU 原 out_channels 循环的逐字形态：gray → RGB 复制；gray+alpha → RGBA(A=src[1])；
// RGB(A) → RGB(A) 直通；4→3 丢弃 alpha）。

class WebpEncoder final : public IEncoder {
public:
    const FormatDef &format() const override {
        static const FormatDef *def = find_format(kFormatId);
        assert(def != nullptr);
        return *def;
    }

    EncodeResult encode(const EncodeRequest &req) override {
        // E8: no exception crosses the IEncoder boundary.
        // R-162 unified end state (M7-WP8/B.7): Timing filled for success and failure alike;
        // error-level logging belongs to pipeline.
        const auto t0 = std::chrono::steady_clock::now();
        EncodeResult res;
        try {
            res = encode_impl(req);
        } catch (const std::exception &e) {
            res = enc::encode_error(std::string("webp: internal error: ") + e.what());
        } catch (...) {
            res = enc::encode_error("webp: unknown internal error");
        }
        if (res.t.encode_ms <= 0.0)
            enc::finish_encode(res, t0);
        return res;
    }

private:
    static EncodeResult encode_impl(const EncodeRequest &req) {
        const auto t0 = std::chrono::steady_clock::now();
        const ParamSet &params = req.target.params;

        if (req.target.out_bitdepth != 8) {
            return enc::encode_error("webp: out_bitdepth " +
                                     std::to_string(req.target.out_bitdepth) +
                                     " is not supported (webp is 8-bit only)");
        }
        enc::Raster r;
        std::string err;
        if (!enc::fetch_raster(req.img, r, err)) {
            return enc::encode_error("webp: " + err);
        }
        enc::warn_unknown_params(params, "libwebp", kKnownKeys);

        // Tech selection (T6b): an explicit EncodeRequest::tech_id wins; empty or
        // unknown falls back to the explicit schema parameter (§3.6/M4-T13,
        // kLosslessParamKey) and then to the reserved key __lossless (§3.4/§4.8).
        // 参数引擎保持两键同步（src/core/params.cpp apply_locks）。
        // R-295：两键读取统一走 pp::lossless_flag()（kLosslessKey/kLosslessParamKey 单源）。
        const bool lossless =
            (req.target.tech_id == "lossless")
                ? true
                : ((req.target.tech_id == "lossy") ? false : lossless_flag(params));
        const bool alpha = r.channels == 2 || r.channels == 4;

        WebPConfig cfg;
        if (!WebPConfigInit(&cfg)) {
            return enc::encode_error("webp: WebPConfigInit failed (encoder ABI mismatch)");
        }
        if (!lossless) {
            // Content preset first: WebPConfigPreset rewrites the lossy defaults,
            // every explicit parameter below wins over it (§3.8 table).
            const int64_t preset = param_int(params, "preset", 2);
            if (preset < 0 || preset > 5) {
                return enc::encode_error("webp: invalid preset value " + std::to_string(preset));
            }
            const float quality = static_cast<float>(param_float(params, "quality", 90.0));
            if (!WebPConfigPreset(&cfg, static_cast<WebPPreset>(preset), quality)) {
                return enc::encode_error("webp: WebPConfigPreset failed");
            }
            cfg.quality = quality;
            cfg.method = static_cast<int>(param_int(params, "method", 4));
            cfg.sns_strength = static_cast<int>(param_int(params, "sns_strength", 50));
            cfg.filter_strength = static_cast<int>(param_int(params, "filter_strength", 30));
            cfg.filter_sharpness = static_cast<int>(param_int(params, "filter_sharpness", 0));
            cfg.filter_type = param_bool(params, "filter_type", true) ? 1 : 0;
            cfg.autofilter = param_bool(params, "autofilter", false) ? 1 : 0;
            cfg.pass = static_cast<int>(param_int(params, "pass", 1));
            cfg.segments = static_cast<int>(param_int(params, "segments", 4));
            cfg.alpha_compression = param_bool(params, "alpha_compression", true) ? 1 : 0;
            cfg.alpha_filtering = static_cast<int>(param_int(params, "alpha_filtering", 1));
            cfg.alpha_quality = static_cast<int>(param_int(params, "alpha_quality", 100));
            cfg.partitions = static_cast<int>(param_int(params, "partitions", 0));
            cfg.partition_limit = static_cast<int>(param_int(params, "partition_limit", 0));
            cfg.preprocessing = static_cast<int>(param_int(params, "preprocessing", 0));
            cfg.qmin = static_cast<int>(param_int(params, "qmin", 0));
            cfg.qmax = static_cast<int>(param_int(params, "qmax", 100));
            cfg.emulate_jpeg_size = param_bool(params, "emulate_jpeg_size", false) ? 1 : 0;
            cfg.low_memory = param_bool(params, "low_memory", false) ? 1 : 0;
            cfg.use_sharp_yuv = param_bool(params, "sharp_yuv", true) ? 1 : 0;
        } else {
            cfg.lossless = 1;
            cfg.quality = static_cast<float>(param_float(params, "quality", 80.0));
            cfg.method = static_cast<int>(param_int(params, "method", 4));
            cfg.exact = param_bool(params, "exact", true) ? 1 : 0;
            cfg.near_lossless = static_cast<int>(param_int(params, "near_lossless", 100));
        }
        // —— E3 线程映射（§3.1 正文，W1-T7 落地）——
        //   webp = `thread_level = E>1`：E==1 → 0（单线程，0.2 行为逐字一致）；E>1 → 1。
        //   语义如实记录：`WebPConfig::thread_level` 是 libwebp 的**提示位**（0 = 不用多线程，
        //   1 = 允许内部多线程；libwebp 只用它决定是否开 worker，不提供线程数上界）→
        //   "E 路"在此退化为"是否并行"，实际并发由 libwebp 内部按图像尺寸决定。
        //   E3 契约（Σ活跃编码线程 ≤ T）在本格式上是**上界近似**：E>1 只可能发生在
        //   W*2 ≤ T 的浅队列分支（E = T/W ≥ 2），故即使 libwebp 用满，量级仍受 T 约束。
        cfg.thread_level = (req.encode_threads > 1) ? 1 : 0;
        // W1-T6（§7.3 口径）：webp = **合成进度面** —— 本编码器不调用 `req.progress`，
        // `EncodeResult::progress_reported` 保持 false；pipeline 的 ProgressMux 按 k[webp]
        // 时长估算出 `synthetic=true` 的进度（§7.3 表：WebP/HEIF/AVIF 无编码回调）。
        // 如实记录：libwebp 的 `WebPConfig` 确有 `progress_hook` 字段（webp/encode.h:354），但
        // §7.2/§7.3 表把 webp 归为合成面、未授权改口径 → 本任务不接线（偏差账已记，不擅改）。
        (void)req.progress;

        if (!WebPValidateConfig(&cfg)) {
            return enc::encode_error(
                std::string("webp: WebPValidateConfig rejected the parameter set") +
                (lossless ? " (lossless)" : " (lossy)"));
        }

        // E4: WebP has no grayscale mode. 1/2-channel input is widened by channel
        // replication only (the pipeline already emitted GrayToRgbEncoded).
        const int out_channels = (r.channels <= 2) ? (alpha ? 4 : 3) : r.channels;
        assert(out_channels == 3 || out_channels == 4);
        const std::size_t npix =
            static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height);
        std::vector<uint8_t> buf(npix * static_cast<std::size_t>(out_channels));
        // M4-W5-T19：逐行「展开（interleave）→ 量化（quantize8）」。行缓冲驻留 L1；
        // 与旧的"逐像素展开 + to_u8"逐位等价（同一表达式、同一通道规则）。
        std::vector<float> rowf(static_cast<std::size_t>(r.width) *
                                static_cast<std::size_t>(out_channels));
        for (int y = 0; y < r.height; ++y) {
            // R-163：`EncodeRequest::cancelled` 真实消费 —— 行级提前退出（写盘前无半成品）。
            if (req.cancelled && req.cancelled())
                return enc::encode_error("webp: encode cancelled");
            const float *src_row = r.px.data() + static_cast<std::size_t>(y) * r.width *
                                                     static_cast<std::size_t>(r.channels);
            uint8_t *dst_row = buf.data() + static_cast<std::size_t>(y) * r.width *
                                                static_cast<std::size_t>(out_channels);
            const std::size_t row_n =
                static_cast<std::size_t>(r.width) * static_cast<std::size_t>(out_channels);
            pp::simd::interleave(src_row, r.channels, rowf.data(), out_channels,
                                 static_cast<std::size_t>(r.width));
            pp::simd::quantize8(rowf.data(), dst_row, row_n);
        }

        WebPPicture pic;
        if (!WebPPictureInit(&pic)) {
            return enc::encode_error("webp: WebPPictureInit failed (encoder ABI mismatch)");
        }
        // R-167 (M7-WP8/B.7): RAII cleanup of library handles (old manual 5 exits removed).
        auto pic_guard = enc::on_scope_exit([&pic] { WebPPictureFree(&pic); });
        pic.use_argb = 1;
        pic.width = r.width;
        pic.height = r.height;
        const int stride = r.width * out_channels;
        const int imported = (out_channels == 4) ? WebPPictureImportRGBA(&pic, buf.data(), stride)
                                                 : WebPPictureImportRGB(&pic, buf.data(), stride);
        if (!imported) {
            const int code = pic.error_code;
            return enc::encode_error("webp: WebPPictureImport" +
                                     std::string(out_channels == 4 ? "RGBA" : "RGB") +
                                     " failed (error_code " + std::to_string(code) + ")");
        }

        WebPMemoryWriter writer;
        WebPMemoryWriterInit(&writer);
        auto writer_guard = enc::on_scope_exit([&writer] { WebPMemoryWriterClear(&writer); });
        pic.writer = WebPMemoryWrite;
        pic.custom_ptr = &writer;

        if (!WebPEncode(&cfg, &pic)) {
            const int code = pic.error_code;
            return enc::encode_error("webp: WebPEncode failed (error_code " + std::to_string(code) +
                                     ")");
        }
        if (writer.mem == nullptr || writer.size == 0) {
            return enc::encode_error("webp: empty output");
        }

        // E5: ICC profile as a RIFF "ICCP" chunk (WebPMux rebuilds VP8X if needed).
        std::vector<uint8_t> muxed;
        const uint8_t *out_bytes = writer.mem;
        std::size_t out_size = writer.size;
        if (!req.meta.icc_profile.empty()) {
            WebPData encoded;
            encoded.bytes = writer.mem;
            encoded.size = writer.size;
            WebPMux *mux = WebPMuxCreate(&encoded, 1);
            if (mux == nullptr) {
                return enc::encode_error("webp: WebPMuxCreate failed");
            }
            WebPData icc;
            icc.bytes = reinterpret_cast<const uint8_t *>(req.meta.icc_profile.data());
            icc.size = req.meta.icc_profile.size();
            WebPMuxError me = WebPMuxSetChunk(mux, "ICCP", &icc, 1);
            if (me != WEBP_MUX_OK) {
                WebPMuxDelete(mux);
                return enc::encode_error("webp: WebPMuxSetChunk(ICCP) failed (error " +
                                         std::to_string(static_cast<int>(me)) + ")");
            }
            WebPData assembled;
            WebPDataInit(&assembled);
            me = WebPMuxAssemble(mux, &assembled);
            if (me != WEBP_MUX_OK || assembled.bytes == nullptr || assembled.size == 0) {
                if (assembled.bytes != nullptr) {
                    WebPDataClear(&assembled);
                }
                WebPMuxDelete(mux);
                return enc::encode_error("webp: WebPMuxAssemble failed (error " +
                                         std::to_string(static_cast<int>(me)) + ")");
            }
            muxed.assign(assembled.bytes, assembled.bytes + assembled.size);
            WebPDataClear(&assembled);
            WebPMuxDelete(mux);
            out_bytes = muxed.data();
            out_size = muxed.size();
        }

        const std::string path = req.target.out_path.string();
        if (req.cancelled && req.cancelled()) {
            return enc::encode_error("webp: encode cancelled");
        }
        // R-003（M7-WP2 接线）：内存型产物走 enc::write_file（= write_atomic_file 唯一转发点，
        // M7-WP8/B.7 落盘单口径；原子落盘 + 失败清场）。
        {
            std::string werr;
            const std::string_view payload(reinterpret_cast<const char *>(out_bytes), out_size);
            const bool wok = enc::write_file(req.target.out_path, payload, &werr);
            if (!wok)
                return enc::encode_error("webp: failed to write output file: " + path + ": " +
                                         werr);
        }

        EncodeResult res;
        res.bytes = static_cast<uint64_t>(out_size);
        res.t.encode_ms = enc::ms_since(t0);
        return res;
    }
};

std::unique_ptr<IEncoder> make_webp() { return std::make_unique<WebpEncoder>(); }

} // namespace

PP_REGISTER_ENCODER("webp", "libwebp", make_webp);

} // namespace pp

// R-051（M7-WP2）：原 `pp_link_encoder_webp` 链接锚点已删，理由同 enc_jpegli.cpp 尾注。
