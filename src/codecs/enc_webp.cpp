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

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "core/logger.h"
#include "core/params.h"
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

// E9: unrecognised keys are ignored (never fatal) and recorded as a warning log
// line. WarningKind (M0 PP-FROZEN types.h) has no "unknown parameter" member and
// warnings must not carry encoder failures, so the log is the faithful channel.
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
                     {{"encoder", "libwebp"}, {"key", key}});
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

class WebpEncoder final : public IEncoder {
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
            return encode_error(std::string("webp: internal error: ") + e.what());
        } catch (...) {
            return encode_error("webp: unknown internal error");
        }
    }

private:
    static EncodeResult encode_impl(const EncodeRequest &req) {
        const auto t0 = std::chrono::steady_clock::now();
        const ParamSet &params = req.target.params;

        if (req.target.out_bitdepth != 8) {
            return encode_error("webp: out_bitdepth " + std::to_string(req.target.out_bitdepth) +
                                " is not supported (webp is 8-bit only)");
        }
        Raster r;
        std::string err;
        if (!fetch_raster(req.img, r, err)) {
            return encode_error("webp: " + err);
        }
        warn_unknown_params(params);

        // Tech selection (T6b): an explicit EncodeRequest::tech_id wins; empty or
        // unknown falls back to the reserved key __lossless (§3.4/§4.8).
        const bool lossless =
            (req.target.tech_id == "lossless")
                ? true
                : ((req.target.tech_id == "lossy") ? false
                                                   : param_bool(params, "__lossless", false));
        const bool alpha = r.channels == 2 || r.channels == 4;

        WebPConfig cfg;
        if (!WebPConfigInit(&cfg)) {
            return encode_error("webp: WebPConfigInit failed (encoder ABI mismatch)");
        }
        if (!lossless) {
            // Content preset first: WebPConfigPreset rewrites the lossy defaults,
            // every explicit parameter below wins over it (§3.8 table).
            const int64_t preset = param_int(params, "preset", 2);
            if (preset < 0 || preset > 5) {
                return encode_error("webp: invalid preset value " + std::to_string(preset));
            }
            const float quality = static_cast<float>(param_float(params, "quality", 90.0));
            if (!WebPConfigPreset(&cfg, static_cast<WebPPreset>(preset), quality)) {
                return encode_error("webp: WebPConfigPreset failed");
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
            return encode_error(std::string("webp: WebPValidateConfig rejected the parameter set") +
                                (lossless ? " (lossless)" : " (lossy)"));
        }

        // E4: WebP has no grayscale mode. 1/2-channel input is widened by channel
        // replication only (the pipeline already emitted GrayToRgbEncoded).
        const int out_channels = (r.channels <= 2) ? (alpha ? 4 : 3) : r.channels;
        assert(out_channels == 3 || out_channels == 4);
        const std::size_t npix =
            static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height);
        std::vector<uint8_t> buf(npix * static_cast<std::size_t>(out_channels));
        for (std::size_t i = 0; i < npix; ++i) {
            const float *src = r.px.data() + i * static_cast<std::size_t>(r.channels);
            uint8_t *dst = buf.data() + i * static_cast<std::size_t>(out_channels);
            const uint8_t g = to_u8(src[0]);
            if (r.channels == 1) {
                dst[0] = g;
                dst[1] = g;
                dst[2] = g;
            } else if (r.channels == 2) {
                dst[0] = g;
                dst[1] = g;
                dst[2] = g;
                dst[3] = to_u8(src[1]);
            } else if (r.channels == 3) {
                dst[0] = to_u8(src[0]);
                dst[1] = to_u8(src[1]);
                dst[2] = to_u8(src[2]);
            } else {
                dst[0] = to_u8(src[0]);
                dst[1] = to_u8(src[1]);
                dst[2] = to_u8(src[2]);
                dst[3] = to_u8(src[3]); // alpha preserved (E4)
            }
        }

        WebPPicture pic;
        if (!WebPPictureInit(&pic)) {
            return encode_error("webp: WebPPictureInit failed (encoder ABI mismatch)");
        }
        pic.use_argb = 1;
        pic.width = r.width;
        pic.height = r.height;
        const int stride = r.width * out_channels;
        const int imported = (out_channels == 4) ? WebPPictureImportRGBA(&pic, buf.data(), stride)
                                                 : WebPPictureImportRGB(&pic, buf.data(), stride);
        if (!imported) {
            const int code = pic.error_code;
            WebPPictureFree(&pic);
            return encode_error("webp: WebPPictureImport" +
                                std::string(out_channels == 4 ? "RGBA" : "RGB") +
                                " failed (error_code " + std::to_string(code) + ")");
        }

        WebPMemoryWriter writer;
        WebPMemoryWriterInit(&writer);
        pic.writer = WebPMemoryWrite;
        pic.custom_ptr = &writer;

        if (!WebPEncode(&cfg, &pic)) {
            const int code = pic.error_code;
            WebPPictureFree(&pic);
            WebPMemoryWriterClear(&writer);
            return encode_error("webp: WebPEncode failed (error_code " + std::to_string(code) +
                                ")");
        }
        WebPPictureFree(&pic);
        if (writer.mem == nullptr || writer.size == 0) {
            WebPMemoryWriterClear(&writer);
            return encode_error("webp: empty output");
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
                WebPMemoryWriterClear(&writer);
                return encode_error("webp: WebPMuxCreate failed");
            }
            WebPData icc;
            icc.bytes = reinterpret_cast<const uint8_t *>(req.meta.icc_profile.data());
            icc.size = req.meta.icc_profile.size();
            WebPMuxError me = WebPMuxSetChunk(mux, "ICCP", &icc, 1);
            if (me != WEBP_MUX_OK) {
                WebPMuxDelete(mux);
                WebPMemoryWriterClear(&writer);
                return encode_error("webp: WebPMuxSetChunk(ICCP) failed (error " +
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
                WebPMemoryWriterClear(&writer);
                return encode_error("webp: WebPMuxAssemble failed (error " +
                                    std::to_string(static_cast<int>(me)) + ")");
            }
            muxed.assign(assembled.bytes, assembled.bytes + assembled.size);
            WebPDataClear(&assembled);
            WebPMuxDelete(mux);
            out_bytes = muxed.data();
            out_size = muxed.size();
        }

        const std::string path = req.target.out_path.string();
        std::FILE *fp = std::fopen(path.c_str(), "wb");
        if (fp == nullptr) {
            WebPMemoryWriterClear(&writer);
            return encode_error("webp: cannot open output file: " + path);
        }
        const std::size_t written = std::fwrite(out_bytes, 1, out_size, fp);
        const int close_rc = std::fclose(fp);
        WebPMemoryWriterClear(&writer);
        if (written != out_size || close_rc != 0) {
            return encode_error("webp: failed to write output file: " + path);
        }

        EncodeResult res;
        res.bytes = static_cast<uint64_t>(out_size);
        res.t.encode_ms = ms_since(t0);
        return res;
    }
};

std::unique_ptr<IEncoder> make_webp() { return std::make_unique<WebpEncoder>(); }

} // namespace

PP_REGISTER_ENCODER("webp", "libwebp", make_webp);

} // namespace pp

// Link anchor: referenced by encoders.cpp (static-library dead-stripping guard).
extern "C" void pp_link_encoder_webp() {}
