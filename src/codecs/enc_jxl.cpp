// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T6 — JPEG XL encoder (libjxl).
//
// Contract: docs/m1-tasks.md §3.8 (E1–E9 + libjxl mapping table) / §4.6 / §5 /
// §7 (M0 facts). Failures return EncodeResult{bytes = 0, error = "<reason>"}
// (revised E8); no exception crosses the IEncoder boundary.
//
// E2: JxlEncoderSetParallelRunner(enc, nullptr, nullptr) — no internal threads.
// E5: ICC via JxlEncoderSetICCProfile, otherwise an explicit sRGB nclx encoding.
// E7: container always on; boxes in the M0-measured order
//     UseBoxes → AddBox("Exif", 4-byte TIFF offset + blob) → AddBox("xml ") →
//     CloseBoxes → CloseInput; empty payloads skip their box.
//
// M4-T2 排查（design §10 libjxl 0.12.0 行的「BUFFERING mode 3 等弃用项」）：本文件零清扫。
//   * JXL_ENC_FRAME_SETTING_BUFFERING 在 0.12 里只接受 0/1/2（3 = deprecated，见
//     lib/include/jxl/encode.h:352），而 kFrameSettings 表里从来没有 buffering 键，
//     编码路径不设置该选项 → 无弃用调用可删；
//   * JxlEncoderUseBoxes / JxlEncoderAddBox / JxlEncoderCloseBoxes 在 0.12 仍是现行
//     unstable API（encode.h 无 JXL_DEPRECATED 标记），E7 的 box 顺序与载荷不变。

#include <jxl/encode.h>

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

constexpr char kLogFile[] = "enc_jxl.cpp";
constexpr std::string_view kFormatId = "jxl";

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

const char* status_name(JxlEncoderStatus st) {
    return st == JXL_ENC_SUCCESS ? "SUCCESS"
                                 : (st == JXL_ENC_ERROR ? "ERROR" : "NEED_MORE_OUTPUT");
}

// ------------------------------------------------------------- params table --
enum class ParamKind { Int, Float, Bool };
enum class Tech { Both, VarDct, Modular };

// §3.8: every frozen-table key maps to exactly one JxlEncoderFrameSettingId.
// distance → JxlEncoderSetFrameDistance and codestream_level →
// JxlEncoderSetCodestreamLevel are handled separately (not frame options).
struct JxlParamMap {
    std::string_view key;
    JxlEncoderFrameSettingId id;
    ParamKind kind;
    Tech tech;
};

constexpr JxlParamMap kFrameSettings[] = {
    {"effort", JXL_ENC_FRAME_SETTING_EFFORT, ParamKind::Int, Tech::Both},
    {"decoding_speed", JXL_ENC_FRAME_SETTING_DECODING_SPEED, ParamKind::Int, Tech::Both},
    {"resampling", JXL_ENC_FRAME_SETTING_RESAMPLING, ParamKind::Int, Tech::Both},
    {"group_order", JXL_ENC_FRAME_SETTING_GROUP_ORDER, ParamKind::Int, Tech::Both},
    {"photon_noise", JXL_ENC_FRAME_SETTING_PHOTON_NOISE, ParamKind::Float, Tech::VarDct},
    {"epf", JXL_ENC_FRAME_SETTING_EPF, ParamKind::Int, Tech::VarDct},
    {"keep_invisible", JXL_ENC_FRAME_SETTING_KEEP_INVISIBLE, ParamKind::Bool, Tech::VarDct},
    {"dots", JXL_ENC_FRAME_SETTING_DOTS, ParamKind::Bool, Tech::VarDct},
    {"patches", JXL_ENC_FRAME_SETTING_PATCHES, ParamKind::Bool, Tech::VarDct},
    {"gaborish", JXL_ENC_FRAME_SETTING_GABORISH, ParamKind::Bool, Tech::VarDct},
    {"progressive_ac", JXL_ENC_FRAME_SETTING_PROGRESSIVE_AC, ParamKind::Bool, Tech::VarDct},
    {"qprogressive_ac", JXL_ENC_FRAME_SETTING_QPROGRESSIVE_AC, ParamKind::Bool, Tech::VarDct},
    {"progressive_dc", JXL_ENC_FRAME_SETTING_PROGRESSIVE_DC, ParamKind::Int, Tech::VarDct},
    {"modular_group_size", JXL_ENC_FRAME_SETTING_MODULAR_GROUP_SIZE, ParamKind::Int, Tech::Modular},
    {"modular_predictor", JXL_ENC_FRAME_SETTING_MODULAR_PREDICTOR, ParamKind::Int, Tech::Modular},
    {"modular_palette_colors", JXL_ENC_FRAME_SETTING_PALETTE_COLORS, ParamKind::Int, Tech::Modular},
    {"modular_lossy_palette", JXL_ENC_FRAME_SETTING_LOSSY_PALETTE, ParamKind::Bool, Tech::Modular},
    {"brotli_effort", JXL_ENC_FRAME_SETTING_BROTLI_EFFORT, ParamKind::Int, Tech::Modular},
    {"responsive", JXL_ENC_FRAME_SETTING_RESPONSIVE, ParamKind::Bool, Tech::Modular},
    {"channel_colors_global_percent", JXL_ENC_FRAME_SETTING_CHANNEL_COLORS_GLOBAL_PERCENT,
     ParamKind::Int, Tech::Modular},
    {"channel_colors_group_percent", JXL_ENC_FRAME_SETTING_CHANNEL_COLORS_GROUP_PERCENT,
     ParamKind::Int, Tech::Modular},
    {"modular_ma_tree_learning_percent", JXL_ENC_FRAME_SETTING_MODULAR_MA_TREE_LEARNING_PERCENT,
     ParamKind::Int, Tech::Modular},
    {"modular_nb_prev_channels", JXL_ENC_FRAME_SETTING_MODULAR_NB_PREV_CHANNELS, ParamKind::Int,
     Tech::Modular},
};

// Keys handled outside the frame-settings loop (still recognised for E9).
constexpr std::string_view kSpecialKeys[] = {"distance", "codestream_level", "color_transform"};

bool known_key(std::string_view key) {
    for (const JxlParamMap& m : kFrameSettings) {
        if (m.key == key) {
            return true;
        }
    }
    for (std::string_view k : kSpecialKeys) {
        if (k == key) {
            return true;
        }
    }
    return false;
}

// E9: unrecognised keys are ignored (never fatal) and recorded as a warning log
// line. WarningKind (M0 PP-FROZEN types.h) has no "unknown parameter" member and
// warnings must not carry encoder failures, so the log is the faithful channel.
void warn_unknown_params(const ParamSet& s) {
    for (const auto& [key, value] : s) {
        (void)value;
        if (key.rfind("__", 0) == 0) {
            continue;  // reserved keys (§3.4 convention)
        }
        if (!known_key(key)) {
            log_warn("Encode", kLogFile, "unknown parameter ignored",
                     {{"encoder", "libjxl"}, {"key", key}});
        }
    }
}

// ------------------------------------------------------------------ pixels --
struct Raster {
    int width = 0, height = 0, channels = 0;
    std::vector<float> px;  // interleaved float32
};

bool fetch_raster(const OIIO::ImageBuf& img, Raster& out, std::string& err) {
    if (!img.initialized()) {
        err = "input image buffer is not initialized";
        return false;
    }
    const OIIO::ImageSpec& spec = img.spec();
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
    const OIIO::span<std::byte> bytes(reinterpret_cast<std::byte*>(out.px.data()),
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
        return 0;  // also catches NaN
    }
    if (v >= 1.0f) {
        return 255;
    }
    return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

uint16_t to_u16(float v) {
    if (!(v > 0.0f)) {
        return 0;
    }
    if (v >= 1.0f) {
        return 65535;
    }
    return static_cast<uint16_t>(v * 65535.0f + 0.5f);
}

// ------------------------------------------------------------------ encoder --
struct JxlEncoderDeleter {
    void operator()(JxlEncoder* enc) const { JxlEncoderDestroy(enc); }
};
using JxlEncoderPtr = std::unique_ptr<JxlEncoder, JxlEncoderDeleter>;

class JxlEncoderImpl final : public IEncoder {
public:
    const FormatDef& format() const override {
        static const FormatDef* def = find_format(kFormatId);
        assert(def != nullptr);
        return *def;
    }

    EncodeResult encode(const EncodeRequest& req) override {
        // E8: no exception crosses the IEncoder boundary.
        try {
            return encode_impl(req);
        } catch (const std::exception& e) {
            return encode_error(std::string("jxl: internal error: ") + e.what());
        } catch (...) {
            return encode_error("jxl: unknown internal error");
        }
    }

private:
    static EncodeResult encode_impl(const EncodeRequest& req) {
        const auto t0 = std::chrono::steady_clock::now();
        const ParamSet& params = req.params;

        if (req.out_bitdepth != 8 && req.out_bitdepth != 16) {
            return encode_error("jxl: out_bitdepth " + std::to_string(req.out_bitdepth) +
                                " is not supported (8 or 16)");
        }
        Raster r;
        std::string err;
        if (!fetch_raster(req.img, r, err)) {
            return encode_error("jxl: " + err);
        }
        warn_unknown_params(params);

        // Lossless comes from the reserved key (§3.4); a modular parameter set
        // (built from the modular tech) also selects the Modular path.
        const bool lossless = param_bool(params, "__lossless", false);
        bool modular_params = false;
        for (const JxlParamMap& m : kFrameSettings) {
            if (m.tech == Tech::Modular && params.find(std::string(m.key)) != params.end()) {
                modular_params = true;
                break;
            }
        }
        // Tech selection (T6b): an explicit EncodeRequest::tech_id wins; empty or
        // unknown falls back to the previous inference (lossless, or the presence
        // of modular-only keys). This is what makes lossy Modular expressible.
        bool modular = false;
        if (req.tech_id == "modular") {
            modular = true;
        } else if (req.tech_id == "vardct") {
            modular = false;
        } else {
            modular = lossless || modular_params;
        }
        // libjxl's lossless mode always encodes the Modular path
        // (JxlEncoderSetFrameLossless overrides the mode), so modular-only
        // options stay applicable there.
        if (lossless) {
            modular = true;
        }
        const bool has_icc = !req.meta.icc_profile.empty();
        const bool gray = r.channels <= 2;
        const bool alpha = r.channels == 2 || r.channels == 4;
        assert(r.channels >= 1 && r.channels <= 4);

        JxlEncoderPtr enc(JxlEncoderCreate(nullptr));
        if (!enc) {
            return encode_error("jxl: JxlEncoderCreate failed");
        }
        // E2: single-threaded encoder (nullptr = no custom parallel runner).
        JxlEncoderStatus st = JxlEncoderSetParallelRunner(enc.get(), nullptr, nullptr);
        if (st != JXL_ENC_SUCCESS) {
            return encode_error(std::string("jxl: JxlEncoderSetParallelRunner failed: ") +
                                status_name(st));
        }
        // Container always on (E7: metadata boxes need BMFF).
        st = JxlEncoderUseContainer(enc.get(), JXL_TRUE);
        if (st != JXL_ENC_SUCCESS) {
            return encode_error(std::string("jxl: JxlEncoderUseContainer failed: ") + status_name(st));
        }

        // Boxes are announced before encoding starts (libjxl >= 0.11 requirement).
        const bool have_exif = !req.meta.exif_blob.empty();
        const bool have_xmp = !req.meta.xmp_rdf.empty();
        const bool any_box = have_exif || have_xmp;
        if (any_box) {
            st = JxlEncoderUseBoxes(enc.get());
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderUseBoxes failed: ") +
                                    status_name(st));
            }
        }

        JxlBasicInfo info;
        JxlEncoderInitBasicInfo(&info);
        info.xsize = static_cast<uint32_t>(r.width);
        info.ysize = static_cast<uint32_t>(r.height);
        info.bits_per_sample = static_cast<uint32_t>(req.out_bitdepth);
        info.exponent_bits_per_sample = 0;
        info.num_color_channels = gray ? 1 : 3;
        info.num_extra_channels = alpha ? 1 : 0;
        info.alpha_bits = alpha ? static_cast<uint32_t>(req.out_bitdepth) : 0;
        info.alpha_exponent_bits = 0;
        info.alpha_premultiplied = JXL_FALSE;
        // Lossless/modular keep the original profile; lossy VarDCT may use XYB.
        info.uses_original_profile = (lossless || modular || has_icc) ? JXL_TRUE : JXL_FALSE;
        st = JxlEncoderSetBasicInfo(enc.get(), &info);
        if (st != JXL_ENC_SUCCESS) {
            return encode_error(std::string("jxl: JxlEncoderSetBasicInfo failed: ") + status_name(st));
        }

        if (alpha) {
            JxlExtraChannelInfo ec;
            JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &ec);
            ec.bits_per_sample = static_cast<uint32_t>(req.out_bitdepth);
            ec.exponent_bits_per_sample = 0;
            ec.dim_shift = 0;
            ec.alpha_premultiplied = JXL_FALSE;
            st = JxlEncoderSetExtraChannelInfo(enc.get(), 0, &ec);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderSetExtraChannelInfo failed: ") +
                                    status_name(st));
            }
        }

        // E5: ICC profile when present, otherwise an explicit sRGB (nclx) encoding.
        if (has_icc) {
            st = JxlEncoderSetICCProfile(
                enc.get(), reinterpret_cast<const uint8_t*>(req.meta.icc_profile.data()),
                req.meta.icc_profile.size());
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderSetICCProfile failed: ") +
                                    status_name(st));
            }
        } else {
            JxlColorEncoding color;
            JxlColorEncodingSetToSRGB(&color, gray ? JXL_TRUE : JXL_FALSE);
            st = JxlEncoderSetColorEncoding(enc.get(), &color);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderSetColorEncoding failed: ") +
                                    status_name(st));
            }
        }

        // codestream_level is an encoder-level option (must precede encoding).
        const int64_t level = param_int(params, "codestream_level", 10);
        st = JxlEncoderSetCodestreamLevel(enc.get(), static_cast<int>(level));
        if (st != JXL_ENC_SUCCESS) {
            return encode_error("jxl: JxlEncoderSetCodestreamLevel(" + std::to_string(level) +
                                ") failed: " + status_name(st));
        }

        JxlEncoderFrameSettings* fs = JxlEncoderFrameSettingsCreate(enc.get(), nullptr);
        if (fs == nullptr) {
            return encode_error("jxl: JxlEncoderFrameSettingsCreate failed");
        }
        if (modular && !lossless) {
            st = JxlEncoderFrameSettingsSetOption(fs, JXL_ENC_FRAME_SETTING_MODULAR, 1);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: modular mode rejected: ") + status_name(st));
            }
        }

        // Mapped parameters: a key is only applied when the selected tech's
        // parameter set actually carries it (E9: absent key = library default).
        // The frozen tables encode "library default" as -1 for the three-state
        // options; libjxl 0.11.2 rejects an explicit -1 for several of them
        // (measured: CHANNEL_COLORS_GLOBAL_PERCENT → JXL_ENC_ERROR), so the
        // sentinel means "leave the library default in place".
        for (const JxlParamMap& m : kFrameSettings) {
            if (params.find(std::string(m.key)) == params.end()) {
                continue;
            }
            if (m.tech == Tech::VarDct && modular) {
                continue;  // VarDCT-only option: not applicable on the Modular path
            }
            if (m.tech == Tech::Modular && !modular) {
                continue;
            }
            switch (m.kind) {
                case ParamKind::Float: {
                    const double v = param_float(params, m.key, 0.0);
                    if (v < 0.0) {
                        continue;  // -1 = library default (e.g. photon noise off)
                    }
                    st = JxlEncoderFrameSettingsSetFloatOption(fs, m.id, static_cast<float>(v));
                    break;
                }
                case ParamKind::Bool: {
                    const int64_t v = param_bool(params, m.key, false) ? 1 : 0;
                    st = JxlEncoderFrameSettingsSetOption(fs, m.id, v);
                    break;
                }
                case ParamKind::Int: {
                    const int64_t v = param_int(params, m.key, 0);
                    if (v < 0) {
                        continue;  // -1 = library default
                    }
                    st = JxlEncoderFrameSettingsSetOption(fs, m.id, v);
                    break;
                }
            }
            if (st != JXL_ENC_SUCCESS) {
                return encode_error("jxl: option " + std::string(m.key) + " rejected: " +
                                    status_name(st));
            }
        }

        // Modular colour transform: YCoCg travels as a modular colour space, the
        // other three choices are the generic colour-transform option (§3.8 table).
        if (modular) {
            const std::string ct = param_str(params, "color_transform", "YCoCg");
            int64_t id = JXL_ENC_FRAME_SETTING_COLOR_TRANSFORM;
            int64_t value = 1;  // None
            if (ct == "XYB") {
                value = 0;
            } else if (ct == "None") {
                value = 1;
            } else if (ct == "YCbCr") {
                value = 2;
            } else {  // YCoCg (default)
                id = JXL_ENC_FRAME_SETTING_MODULAR_COLOR_SPACE;
                value = 6;
            }
            st = JxlEncoderFrameSettingsSetOption(fs, static_cast<JxlEncoderFrameSettingId>(id), value);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error("jxl: color_transform=" + ct + " rejected: " + status_name(st));
            }
        }

        // Lossless overrides distance/modular/colour-transform; distance is still
        // applied explicitly for the lossy paths (§3.8 table).
        if (lossless) {
            st = JxlEncoderSetFrameDistance(fs, 0.0f);
            if (st == JXL_ENC_SUCCESS) {
                st = JxlEncoderSetFrameLossless(fs, JXL_TRUE);
            }
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: lossless mode rejected: ") + status_name(st));
            }
        } else {
            const float distance = static_cast<float>(param_float(params, "distance", 1.0));
            st = JxlEncoderSetFrameDistance(fs, distance);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error("jxl: distance " + std::to_string(distance) +
                                    " rejected: " + status_name(st));
            }
        }

        // Interleaved pixel buffer in the requested output depth (E6 rounding).
        const std::size_t npix = static_cast<std::size_t>(r.width) *
                                 static_cast<std::size_t>(r.height) *
                                 static_cast<std::size_t>(r.channels);
        std::vector<uint8_t> pixels8;
        std::vector<uint16_t> pixels16;
        const void* pixel_data = nullptr;
        std::size_t pixel_bytes = 0;
        if (req.out_bitdepth == 16) {
            pixels16.resize(npix);
            for (std::size_t i = 0; i < npix; ++i) {
                pixels16[i] = to_u16(r.px[i]);
            }
            pixel_data = pixels16.data();
            pixel_bytes = pixels16.size() * sizeof(uint16_t);
        } else {
            pixels8.resize(npix);
            for (std::size_t i = 0; i < npix; ++i) {
                pixels8[i] = to_u8(r.px[i]);
            }
            pixel_data = pixels8.data();
            pixel_bytes = pixels8.size();
        }

        JxlPixelFormat pf{};
        pf.num_channels = static_cast<uint32_t>(r.channels);
        pf.data_type = (req.out_bitdepth == 16) ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8;
        pf.endianness = JXL_NATIVE_ENDIAN;
        pf.align = 0;

        st = JxlEncoderAddImageFrame(fs, &pf, pixel_data, pixel_bytes);
        if (st != JXL_ENC_SUCCESS) {
            return encode_error(std::string("jxl: JxlEncoderAddImageFrame failed: ") + status_name(st));
        }

        // E7: Exif box payload = 4-byte TIFF offset prefix + TIFF blob (M0 order).
        if (have_exif) {
            std::string box(4, '\0');
            box += req.meta.exif_blob;
            st = JxlEncoderAddBox(enc.get(), "Exif", reinterpret_cast<const uint8_t*>(box.data()),
                                  box.size(), JXL_FALSE);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderAddBox(Exif) failed: ") +
                                    status_name(st));
            }
        }
        if (have_xmp) {
            st = JxlEncoderAddBox(enc.get(), "xml ",
                                  reinterpret_cast<const uint8_t*>(req.meta.xmp_rdf.data()),
                                  req.meta.xmp_rdf.size(), JXL_FALSE);
            if (st != JXL_ENC_SUCCESS) {
                return encode_error(std::string("jxl: JxlEncoderAddBox(xml) failed: ") +
                                    status_name(st));
            }
        }
        if (any_box) {
            JxlEncoderCloseBoxes(enc.get());
        }
        JxlEncoderCloseInput(enc.get());

        std::vector<uint8_t> out(1u << 16);
        uint8_t* next = out.data();
        std::size_t avail = out.size();
        for (;;) {
            st = JxlEncoderProcessOutput(enc.get(), &next, &avail);
            if (st == JXL_ENC_SUCCESS) {
                break;
            }
            if (st != JXL_ENC_NEED_MORE_OUTPUT) {
                return encode_error(std::string("jxl: JxlEncoderProcessOutput failed: ") +
                                    status_name(st));
            }
            const std::size_t used = static_cast<std::size_t>(next - out.data());
            out.resize(out.size() * 2);
            next = out.data() + used;
            avail = out.size() - used;
        }
        const std::size_t total = static_cast<std::size_t>(next - out.data());
        if (total == 0) {
            return encode_error("jxl: empty output");
        }

        const std::string path = req.out_path.string();
        std::FILE* fp = std::fopen(path.c_str(), "wb");
        if (fp == nullptr) {
            return encode_error("jxl: cannot open output file: " + path);
        }
        const std::size_t written = std::fwrite(out.data(), 1, total, fp);
        const int close_rc = std::fclose(fp);
        if (written != total || close_rc != 0) {
            return encode_error("jxl: failed to write output file: " + path);
        }

        EncodeResult res;
        res.bytes = static_cast<uint64_t>(total);
        res.t.encode_ms = ms_since(t0);
        return res;
    }
};

std::unique_ptr<IEncoder> make_jxl() { return std::make_unique<JxlEncoderImpl>(); }

}  // namespace

PP_REGISTER_ENCODER("jxl", "libjxl", make_jxl);

}  // namespace pp

// Link anchor: referenced by encoders.cpp (static-library dead-stripping guard).
extern "C" void pp_link_encoder_jxl() {}
