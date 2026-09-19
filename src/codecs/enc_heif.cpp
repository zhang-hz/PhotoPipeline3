// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T7: libheif encoders (HEIF/HEVC + AVIF/AV1), runtime parameter
// introspection (R4) and the 10-bit capability probe (R19).
//
// Design notes (facts measured on libheif 1.23.1, see docs/m1-tasks.md §7):
//   * `heif_get_encoder_descriptors(format, name_filter, out, count)` is a 4-argument free
//     function (no context) in 1.23.1; the name filter is a substring of the encoder's
//     `id_name`.
//   * Parameter names are NEVER hard-coded beyond the semantic ones named by the task book
//     (quality / lossless / chroma / preset): the ParamSet -> encoder mapping iterates
//     `heif_encoder_list_parameters()` and dispatches by the *runtime* parameter type.
//   * Pixels are handed to libheif as Y/Cb/Cr planes (§3.8 mapping table). The RGB -> YCbCr
//     matrix is full-range BT.601 (matrix_coefficients = 6 / SMPTE 170M), which is what the
//     nclx profile written next to the planes declares.
#include <libheif/heif.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "core/logger.h"
#include "core/params.h"

namespace pp {
namespace {

constexpr std::string_view kStage = "encode";
constexpr std::string_view kFile  = "enc_heif.cpp";

// ---------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------

double ms_since(const std::chrono::steady_clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::string heif_error_text(const heif_error& e) {
    std::string s = (e.message && *e.message) ? e.message : "unspecified libheif error";
    s += " (heif_error code=" + std::to_string(static_cast<int>(e.code)) +
         " subcode=" + std::to_string(static_cast<int>(e.subcode)) + ")";
    return s;
}

constexpr heif_error kHeifOk{heif_error_Ok, heif_suberror_Unspecified, nullptr};

bool libheif_format(std::string_view format_id, heif_compression_format& out) {
    if (format_id == "heif") { out = heif_compression_HEVC; return true; }
    if (format_id == "avif") { out = heif_compression_AV1;  return true; }
    return false;
}

// backend_id (frozen format table) -> substring of heif_encoder_descriptor_get_id_name()
std::string_view backend_match(std::string_view backend_id) {
    if (backend_id == "x265")    return "x265";
    if (backend_id == "svt-av1") return "svt";
    if (backend_id == "libaom")  return "aom";
    return backend_id;
}

// runtime id_name -> frozen backend_id (unknown encoders keep their runtime name)
std::string canonical_backend_id(std::string_view id_name) {
    if (id_name.find("x265") != std::string_view::npos) return "x265";
    if (id_name.find("svt")  != std::string_view::npos) return "svt-av1";
    if (id_name.find("aom")  != std::string_view::npos) return "libaom";
    return std::string(id_name);
}

// Descriptors are enumerated per call (the list is owned by libheif and stable for the
// process lifetime, but the returned pointer array is ours).
std::vector<const heif_encoder_descriptor*> enumerate_descriptors(heif_compression_format fmt) {
    const heif_encoder_descriptor* raw[32] = {};
    const int n = heif_get_encoder_descriptors(fmt, nullptr, raw, 32);
    std::vector<const heif_encoder_descriptor*> out;
    for (int i = 0; i < n && i < 32; i++)
        if (raw[i]) out.push_back(raw[i]);
    return out;
}

const heif_encoder_descriptor* find_descriptor(heif_compression_format fmt,
                                               std::string_view backend_id) {
    const std::vector<const heif_encoder_descriptor*> descs = enumerate_descriptors(fmt);
    if (descs.empty()) return nullptr;
    const std::string_view want = backend_match(backend_id);
    if (backend_id.empty() || want.empty()) return descs.front();  // empty = highest priority
    for (const heif_encoder_descriptor* d : descs) {
        const char* id_name = heif_encoder_descriptor_get_id_name(d);
        if (id_name && std::string_view(id_name).find(want) != std::string_view::npos) return d;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// runtime parameter introspection -> ParamDef
// ---------------------------------------------------------------------------

ParamDef param_def_from(heif_encoder* enc, const heif_encoder_parameter* p) {
    ParamDef d;
    const char* name = heif_encoder_parameter_get_name(p);
    d.key   = name ? name : "";
    d.label = d.key;
    const heif_encoder_parameter_type t = heif_encoder_parameter_get_type(p);
    std::string type_desc;
    if (t == heif_encoder_parameter_type_integer) {
        int have_min = 0, have_max = 0, mn = 0, mx = 0, nvalid = 0;
        const int* values = nullptr;
        heif_encoder_parameter_get_valid_integer_values(p, &have_min, &have_max, &mn, &mx,
                                                        &nvalid, &values);
        int current = 0;
        const bool have_current =
            heif_encoder_get_parameter_integer(enc, d.key.c_str(), &current).code == heif_error_Ok;
        if (nvalid > 0 && values) {
            // fixed value set -> Enum with integer choices (e.g. svt tile-rows)
            d.type = ParamType::Enum;
            for (int i = 0; i < nvalid; i++)
                d.choices.emplace_back(std::to_string(values[i]), int64_t(values[i]));
            d.def = int64_t(values[0]);
            type_desc = "integer set";
        } else {
            d.type = ParamType::Int;
            d.lo = have_min ? static_cast<double>(mn) : static_cast<double>(INT32_MIN);
            d.hi = have_max ? static_cast<double>(mx) : static_cast<double>(INT32_MAX);
            d.step = 1;
            d.def = int64_t(have_current ? current : (have_min ? mn : 0));
            type_desc = have_min || have_max
                            ? "integer [" + std::to_string(mn) + "," + std::to_string(mx) + "]"
                            : "integer";
        }
    } else if (t == heif_encoder_parameter_type_boolean) {
        d.type = ParamType::Bool;
        int current = 0;
        d.def = heif_encoder_get_parameter_boolean(enc, d.key.c_str(), &current).code == heif_error_Ok
                    ? (current != 0)
                    : false;
        type_desc = "boolean";
    } else {
        d.type = ParamType::Enum;
        const char* const* strings = nullptr;
        if (heif_encoder_parameter_get_valid_string_values(p, &strings).code == heif_error_Ok &&
            strings) {
            for (int i = 0; strings[i]; i++)
                d.choices.emplace_back(strings[i], std::string(strings[i]));
        }
        char buf[512] = {};
        std::string current;
        if (heif_encoder_get_parameter_string(enc, d.key.c_str(), buf, sizeof(buf)).code == heif_error_Ok)
            current = buf;
        if (current.empty() && !d.choices.empty())
            current = std::get<std::string>(d.choices.front().second);
        d.def = current;
        type_desc = d.choices.empty() ? "string (unconstrained)" : "string set";
    }
    // Semantic core parameters stay in the visible tier; plugin knobs go to "advanced".
    d.advanced = !(d.key == "quality" || d.key == "lossless" || d.key == "chroma" ||
                   d.key == "preset");
    d.tooltip = "libheif 运行时内省参数（heif_encoder_list_parameters，类型 " + type_desc +
                "）：" + d.key + "。名称/范围/默认值均来自当前编码插件，M1 不硬编码。";
    return d;
}

// Live introspection of one encoder instance: the encoder's own parameter list.
std::vector<ParamDef> introspect_live(heif_encoder* enc) {
    std::vector<ParamDef> out;
    const heif_encoder_parameter* const* ps = heif_encoder_list_parameters(enc);
    for (int i = 0; ps && ps[i]; i++) out.push_back(param_def_from(enc, ps[i]));
    return out;
}

// ---------------------------------------------------------------------------
// YCbCr plane filling (full-range BT.601)
// ---------------------------------------------------------------------------

struct ChromaLayout {
    heif_chroma chroma = heif_chroma_420;
    int cw_div = 2, ch_div = 2;  // chroma width/height divisors, ceil() at odd sizes
    int cw_off = 1, ch_off = 1;  // rounding offsets (x + off) / div
};

ChromaLayout layout_for(heif_chroma c) {
    ChromaLayout l;
    l.chroma = c;
    if (c == heif_chroma_444) { l.cw_div = 1; l.ch_div = 1; l.cw_off = 0; l.ch_off = 0; }
    else if (c == heif_chroma_422) { l.cw_div = 2; l.ch_div = 1; l.cw_off = 1; l.ch_off = 0; }
    return l;
}

int quantize(float v, int maxv) {
    const float x = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int>(std::lround(x * static_cast<float>(maxv)));
}

void store_sample(uint8_t* base, size_t stride, int bitdepth, int x, int y, int value) {
    if (bitdepth > 8)
        reinterpret_cast<uint16_t*>(base + static_cast<size_t>(y) * stride)[x] =
            static_cast<uint16_t>(value);
    else
        base[static_cast<size_t>(y) * stride + x] = static_cast<uint8_t>(value);
}

float load_channel(const OIIO::ImageBuf::ConstIterator<float>& it, int channel) {
    return it[channel];
}

}  // namespace

// ---------------------------------------------------------------------------
// encoder
// ---------------------------------------------------------------------------

namespace {

class HeifEncoder final : public IEncoder {
public:
    HeifEncoder(std::string format_id, std::string backend_id)
        : format_id_(std::move(format_id)), backend_id_(std::move(backend_id)) {}

    const FormatDef& format() const override {
        const FormatDef* f = find_format(format_id_);
        assert(f != nullptr);
        static const FormatDef kFallback{};
        return f ? *f : kFallback;
    }

    EncodeResult encode(const EncodeRequest& req) override;

private:
    // E1: immutable -> one instance may serve several workers concurrently.
    const std::string format_id_;
    const std::string backend_id_;
};

EncodeResult HeifEncoder::encode(const EncodeRequest& req) {
    EncodeResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&]() {
        res.t.encode_ms = ms_since(t0);
        res.t.total_ms  = res.t.encode_ms;
        return res;
    };
    auto fail = [&](const std::string& msg) {
        log_error(kStage, kFile, "encode failed",
                  {{"format", format_id_}, {"backend", backend_id_}, {"error", msg}});
        res.bytes = 0;
        res.error = msg;
        return finish();
    };
    auto note = [&](const std::string& msg) {
        log_warn(kStage, kFile, msg, {{"format", format_id_}, {"backend", backend_id_}});
        // TODO(M2): WarningKind has no "parameter ignored" value, so E9 notes ride on
        // MetadataDropped; add a dedicated kind (e.g. ParamIgnored) in M2 and re-map.
        res.warnings.push_back(Warning{WarningKind::MetadataDropped, msg});
    };

    try {
        if (!req.img.initialized() || req.img.spec().width <= 0 || req.img.spec().height <= 0)
            return fail("empty input image");
        const OIIO::ImageSpec& ispec = req.img.spec();
        const int w = ispec.width, h = ispec.height, nch = ispec.nchannels;
        assert(w > 0 && h > 0);
        assert(nch >= 1 && nch <= 4);
        if (nch < 1 || nch > 4)
            return fail("unsupported channel count " + std::to_string(nch) + " (expected 1..4)");
        if (req.out_bitdepth != 8 && req.out_bitdepth != 10 && req.out_bitdepth != 12)
            return fail("unsupported bitdepth " + std::to_string(req.out_bitdepth) +
                        " for " + format_id_ + " (expected 8, 10 or 12)");

        heif_compression_format fmt = heif_compression_HEVC;
        if (!libheif_format(format_id_, fmt))
            return fail("not a libheif format: " + format_id_);
        const heif_encoder_descriptor* desc = find_descriptor(fmt, backend_id_);
        if (!desc)
            return fail("no libheif encoder available for backend '" + backend_id_ + "' (" +
                        format_id_ + ")");

        heif_context* ctx = heif_context_alloc();
        if (!ctx) return fail("heif_context_alloc() failed");
        struct CtxGuard {
            heif_context* c;
            ~CtxGuard() { if (c) heif_context_free(c); }
        } ctx_guard{ctx};

        heif_encoder* enc = nullptr;
        heif_error e = heif_context_get_encoder(ctx, desc, &enc);
        if (e.code != heif_error_Ok || !enc)
            return fail("heif_context_get_encoder(" + backend_id_ + ") failed: " + heif_error_text(e));
        struct EncGuard {
            heif_encoder* e;
            ~EncGuard() { if (e) heif_encoder_release(e); }
        } enc_guard{enc};

        // ---- parameters: map the ParamSet through the encoder's own parameter list ----
        const std::vector<ParamDef> live = introspect_live(enc);
        std::vector<std::string> known;
        known.reserve(live.size());
        for (const ParamDef& p : live) known.push_back(p.key);
        auto is_known = [&known](const std::string& k) {
            return std::find(known.begin(), known.end(), k) != known.end();
        };

        for (const ParamDef& p : live) {
            const auto it = req.params.find(p.key);
            if (it == req.params.end()) continue;
            heif_error se = kHeifOk;
            std::string rejected;
            switch (p.type) {
                case ParamType::Int:
                case ParamType::Enum: {
                    const int64_t* iv = std::get_if<int64_t>(&it->second);
                    if (!iv) { rejected = "expected integer"; break; }
                    if (*iv < INT32_MIN || *iv > INT32_MAX) { rejected = "out of integer range"; break; }
                    se = heif_encoder_set_parameter_integer(enc, p.key.c_str(),
                                                            static_cast<int>(*iv));
                    break;
                }
                case ParamType::Bool: {
                    const bool* bv = std::get_if<bool>(&it->second);
                    if (!bv) { rejected = "expected boolean"; break; }
                    se = heif_encoder_set_parameter_boolean(enc, p.key.c_str(), *bv ? 1 : 0);
                    break;
                }
                case ParamType::Float: {
                    const double* dv = std::get_if<double>(&it->second);
                    const int64_t* iv = std::get_if<int64_t>(&it->second);
                    if (!dv && !iv) { rejected = "expected number"; break; }
                    se = heif_encoder_set_parameter_integer(
                        enc, p.key.c_str(),
                        static_cast<int>(std::lround(dv ? *dv : static_cast<double>(*iv))));
                    break;
                }
            }
            if (rejected.empty() && se.code != heif_error_Ok) rejected = heif_error_text(se);
            if (rejected.empty()) continue;
            // Specialised setters exist for two semantic parameters; use them as fallback so a
            // plugin that exposes the key but rejects the generic path still honours it.
            bool recovered = false;
            if (p.key == "quality") {
                const int64_t v = param_int(req.params, "quality", 90);
                recovered = heif_encoder_set_lossy_quality(enc, static_cast<int>(v)).code == heif_error_Ok;
            } else if (p.key == "lossless") {
                const bool v = param_bool(req.params, "lossless", false);
                recovered = heif_encoder_set_lossless(enc, v ? 1 : 0).code == heif_error_Ok;
            }
            if (!recovered)
                note("parameter '" + p.key + "' ignored by backend '" + backend_id_ +
                     "': " + rejected);
        }

        // E2: all internal threading off. The x265 plugin exposes no "threads" parameter
        // (measured), so for that backend the encoder keeps its default pool (TODO(M2)).
        if (is_known("threads")) {
            const heif_error te = heif_encoder_set_parameter_integer(enc, "threads", 1);
            if (te.code != heif_error_Ok) note(std::string("could not force threads=1: ") + heif_error_text(te));
        } else {
            log_info(kStage, kFile, "backend exposes no 'threads' parameter; default pool kept",
                     {{"format", format_id_}, {"backend", backend_id_}});
        }

        // E9: unrecognised keys are ignored with a warning (never an error).
        for (const auto& [key, value] : req.params) {
            (void)value;
            if (key.rfind("__", 0) == 0) continue;  // reserved keys (§3.4)
            if (!is_known(key)) note("unrecognised parameter '" + key + "' ignored");
        }

        // ---- chroma layout ----
        ChromaLayout layout = layout_for(heif_chroma_420);
        if (is_known("chroma")) {
            const std::string c = param_str(req.params, "chroma", "420");
            if (c == "444")      layout = layout_for(heif_chroma_444);
            else if (c == "422") layout = layout_for(heif_chroma_422);
            else if (c == "420") layout = layout_for(heif_chroma_420);
            else note("unknown chroma value '" + c + "', using 420");
        } else {
            const std::string c = param_str(req.params, "chroma", "");
            if (!c.empty() && c != "420")
                note("backend '" + backend_id_ + "' exposes no chroma parameter; forced 420");
        }

        const int bitdepth = req.out_bitdepth;
        const int maxv = (1 << bitdepth) - 1;
        const int cw = (w + layout.cw_off) / layout.cw_div;
        const int ch = (h + layout.ch_off) / layout.ch_div;
        const bool has_alpha = (nch == 2 || nch == 4);

        // Known upstream limitation (measured with SVT-AV1 4.1.0 through libheif 1.23.1):
        // a >8-bit encode that carries an alpha plane fails inside the SVT plugin AND leaves
        // the heap corrupted, so the very next allocation/free aborts the process (observed:
        // "corrupted size vs. prev_size" in x265/libheif teardown). Refuse the combination up
        // front so callers get a clean EncodeResult error instead of a crash.
        // Isolation probe: .cache/tmp/m1-t7/scratch.cpp ("svtalpha" aborts, "svt", "svt12",
        // "x265alpha" are clean). TODO(M2): re-probe and drop once upstream fixes it.
        if (format_id_ == "avif" && backend_id_ == "svt-av1" && bitdepth > 8 && has_alpha)
            return fail("svt-av1 does not support 10-bit with alpha; use backend libaom");

        heif_image* img = nullptr;
        e = heif_image_create(w, h, heif_colorspace_YCbCr, layout.chroma, &img);
        if (e.code != heif_error_Ok || !img)
            return fail("heif_image_create failed: " + heif_error_text(e));
        struct ImgGuard {
            heif_image* i;
            ~ImgGuard() { if (i) heif_image_release(i); }
        } img_guard{img};

        struct PlaneRef {
            heif_channel ch;
            int w, h;
            uint8_t* base = nullptr;
            size_t stride = 0;
        };
        PlaneRef planes[4];
        int nplanes = 0;
        planes[nplanes++] = {heif_channel_Y,  w,  h,  nullptr, 0};
        planes[nplanes++] = {heif_channel_Cb, cw, ch, nullptr, 0};
        planes[nplanes++] = {heif_channel_Cr, cw, ch, nullptr, 0};
        if (has_alpha) planes[nplanes++] = {heif_channel_Alpha, w, h, nullptr, 0};
        for (int i = 0; i < nplanes; i++) {
            e = heif_image_add_plane(img, planes[i].ch, planes[i].w, planes[i].h, bitdepth);
            if (e.code != heif_error_Ok) {
                std::string msg = std::string("heif_image_add_plane(") +
                                  std::to_string(static_cast<int>(planes[i].ch)) + ", " +
                                  std::to_string(planes[i].w) + "x" +
                                  std::to_string(planes[i].h) + ", " + std::to_string(bitdepth) +
                                  " bit) failed: " + heif_error_text(e);
                if (bitdepth > 8)
                    msg += "; encoder does not support " + std::to_string(bitdepth) + "-bit input";
                return fail(msg);
            }
            planes[i].base = heif_image_get_plane2(img, planes[i].ch, &planes[i].stride);
            if (!planes[i].base)
                return fail("heif_image_get_plane2 returned no buffer for plane " +
                            std::to_string(static_cast<int>(planes[i].ch)));
        }

        // ---- pixels: float32 RGB(A)/gray(A) -> full-range BT.601 YCbCr ----
        std::vector<float> cb_full(static_cast<size_t>(w) * h);
        std::vector<float> cr_full(static_cast<size_t>(w) * h);
        for (OIIO::ImageBuf::ConstIterator<float> it(req.img); !it.done(); ++it) {
            const int x = it.x() - ispec.x;
            const int y = it.y() - ispec.y;
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            float r, g, b;
            if (nch >= 3) {
                r = load_channel(it, 0);
                g = load_channel(it, 1);
                b = load_channel(it, 2);
            } else {
                r = g = b = load_channel(it, 0);
            }
            const size_t idx = static_cast<size_t>(y) * w + x;
            store_sample(planes[0].base, planes[0].stride, bitdepth, x, y,
                         quantize(0.299f * r + 0.587f * g + 0.114f * b, maxv));
            cb_full[idx] = -0.168736f * r - 0.331264f * g + 0.5f * b + 0.5f;
            cr_full[idx] = 0.5f * r - 0.418688f * g - 0.081312f * b + 0.5f;
            if (has_alpha)
                store_sample(planes[3].base, planes[3].stride, bitdepth, x, y,
                             quantize(load_channel(it, nch - 1), maxv));
        }
        // box-downsample the chroma planes (2x2 for 420, 2x1 for 422, none for 444)
        for (int cy = 0; cy < ch; cy++) {
            for (int cx = 0; cx < cw; cx++) {
                float scb = 0.0f, scr = 0.0f;
                int n = 0;
                for (int dy = 0; dy < layout.ch_div; dy++) {
                    const int sy = cy * layout.ch_div + dy;
                    if (sy >= h) break;
                    for (int dx = 0; dx < layout.cw_div; dx++) {
                        const int sx = cx * layout.cw_div + dx;
                        if (sx >= w) break;
                        const size_t idx = static_cast<size_t>(sy) * w + sx;
                        scb += cb_full[idx];
                        scr += cr_full[idx];
                        n++;
                    }
                }
                const float inv = n > 0 ? 1.0f / static_cast<float>(n) : 0.0f;
                store_sample(planes[1].base, planes[1].stride, bitdepth, cx, cy,
                             quantize(scb * inv, maxv));
                store_sample(planes[2].base, planes[2].stride, bitdepth, cx, cy,
                             quantize(scr * inv, maxv));
            }
        }

        // ---- colour description: nclx + optional ICC (E5) ----
        heif_color_profile_nclx nclx{};
        nclx.version = 1;
        nclx.color_primaries          = heif_color_primaries_ITU_R_BT_709_5;          // sRGB primaries
        nclx.transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;   // sRGB TRC
        nclx.matrix_coefficients      = heif_matrix_coefficients_ITU_R_BT_601_6;      // matrix used above
        nclx.full_range_flag          = 1;
        const heif_error ne = heif_image_set_nclx_color_profile(img, &nclx);
        if (ne.code != heif_error_Ok)
            log_warn(kStage, kFile, "heif_image_set_nclx_color_profile failed",
                     {{"format", format_id_}, {"backend", backend_id_},
                      {"error", heif_error_text(ne)}});
        if (!req.meta.icc_profile.empty()) {
            const heif_error ie = heif_image_set_raw_color_profile(
                img, "prof", req.meta.icc_profile.data(), req.meta.icc_profile.size());
            if (ie.code != heif_error_Ok)
                note("ICC profile not embedded: " + heif_error_text(ie));
        }

        // ---- encode ----
        heif_image_handle* handle = nullptr;
        e = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
        if (e.code != heif_error_Ok) {
            std::string msg = "heif_context_encode_image failed: " + heif_error_text(e);
            if (format_id_ == "avif" && backend_id_ == "svt-av1" && bitdepth == 10 && has_alpha)
                msg += " [svt-av1 does not support 10-bit with alpha; use backend libaom]";
            else if (format_id_ == "heif" && bitdepth > 8)
                msg += " [encoder does not support 10-bit input]";
            return fail(msg);
        }
        struct HandleGuard {
            heif_image_handle* h;
            ~HandleGuard() { if (h) heif_image_handle_release(h); }
        } handle_guard{handle};

        // ---- metadata (E7: strictly before heif_context_write) ----
        if (!req.meta.exif_blob.empty()) {
            const heif_error me = heif_context_add_exif_metadata(ctx, handle,
                                                                 req.meta.exif_blob.data(),
                                                                 static_cast<int>(req.meta.exif_blob.size()));
            if (me.code != heif_error_Ok)
                note("EXIF metadata dropped: " + heif_error_text(me));
        }
        if (!req.meta.xmp_rdf.empty()) {
            const heif_error me = heif_context_add_XMP_metadata(ctx, handle,
                                                                req.meta.xmp_rdf.data(),
                                                                static_cast<int>(req.meta.xmp_rdf.size()));
            if (me.code != heif_error_Ok)
                note("XMP metadata dropped: " + heif_error_text(me));
        }

        // ---- write ----
        struct Sink {
            FILE* f = nullptr;
            uint64_t bytes = 0;
            std::string error;
        } sink;
        sink.f = std::fopen(req.out_path.string().c_str(), "wb");
        if (!sink.f) return fail("cannot open output file: " + req.out_path.string());
        heif_writer writer{};
        writer.writer_api_version = 1;
        writer.write = [](heif_context*, const void* data, size_t size, void* userdata) -> heif_error {
            Sink* s = static_cast<Sink*>(userdata);
            if (std::fwrite(data, 1, size, s->f) != size) {
                s->error = "short write";
                return heif_error{heif_error_Encoding_error, heif_suberror_Unspecified, "short write"};
            }
            s->bytes += size;
            return kHeifOk;
        };
        e = heif_context_write(ctx, &writer, &sink);
        const int close_rc = std::fclose(sink.f);
        sink.f = nullptr;
        if (e.code != heif_error_Ok)
            return fail("heif_context_write failed: " + heif_error_text(e));
        if (!sink.error.empty()) return fail("write error: " + sink.error);
        if (close_rc != 0) return fail("fclose failed for " + req.out_path.string());
        if (sink.bytes == 0) return fail("heif_context_write produced 0 bytes");

        res.bytes = sink.bytes;
        log_debug(kStage, kFile, "encoded",
                  {{"format", format_id_}, {"backend", backend_id_},
                   {"size", std::to_string(w) + "x" + std::to_string(h)},
                   {"channels", std::to_string(nch)},
                   {"bitdepth", std::to_string(bitdepth)},
                   {"bytes", std::to_string(res.bytes)}});
        return finish();
    } catch (const std::exception& ex) {
        return fail(std::string("exception: ") + ex.what());
    } catch (...) {
        return fail("unknown exception");
    }
}

// ---------------------------------------------------------------------------
// factories + static registration (§3.17)
// ---------------------------------------------------------------------------

std::unique_ptr<IEncoder> make_heif_x265() { return std::make_unique<HeifEncoder>("heif", "x265"); }
std::unique_ptr<IEncoder> make_avif_svt()  { return std::make_unique<HeifEncoder>("avif", "svt-av1"); }
std::unique_ptr<IEncoder> make_avif_aom()  { return std::make_unique<HeifEncoder>("avif", "libaom"); }

}  // namespace

// Static self-registration (§3.17). The macro pastes the factory name, so it needs
// unqualified access to the anonymous-namespace factories above -> expand inside namespace pp.
PP_REGISTER_ENCODER("heif", "x265", make_heif_x265);
PP_REGISTER_ENCODER("avif", "svt-av1", make_avif_svt);
PP_REGISTER_ENCODER("avif", "libaom", make_avif_aom);

// Link anchors: pp_core is linked with $<LINK_LIBRARY:WHOLE_ARCHIVE> by every consumer
// (T7c ruling) so both T7 TUs register unconditionally; the anchors are kept as a
// redundant safety net and also keep the two T7 TUs together in any ad-hoc link.
// TODO(M2): drop the anchors once the whole-archive link is the only supported form.
void t7_encoder_link_anchor_oiio();  // defined in enc_oiio.cpp
void t7_encoder_link_anchor_heif() { t7_encoder_link_anchor_oiio(); }

namespace {

// ---------------------------------------------------------------------------
// 10-bit capability probe (R19) — real encode + decode round trip on a 64x64 image
// ---------------------------------------------------------------------------

heif_error probe_sink_write(heif_context*, const void* data, size_t size, void* userdata) {
    auto* blob = static_cast<std::vector<uint8_t>*>(userdata);
    blob->insert(blob->end(), static_cast<const uint8_t*>(data),
                 static_cast<const uint8_t*>(data) + size);
    return kHeifOk;
}

// Returns true when a 64x64 image with `bitdepth`-bit planes encodes AND decodes back with
// the same per-channel bit depth.
bool bitdepth_roundtrip_ok(heif_compression_format fmt, std::string_view backend_id, int bitdepth,
                           std::string& why) {
    const heif_encoder_descriptor* desc = find_descriptor(fmt, backend_id);
    if (!desc) { why = "no encoder descriptor"; return false; }
    heif_context* ctx = heif_context_alloc();
    if (!ctx) { why = "heif_context_alloc failed"; return false; }
    heif_encoder* enc = nullptr;
    heif_error e = heif_context_get_encoder(ctx, desc, &enc);
    if (e.code != heif_error_Ok || !enc) {
        why = "get_encoder: " + heif_error_text(e);
        heif_context_free(ctx);
        return false;
    }
    const int W = 64, H = 64, cw = 32, ch = 32, maxv = (1 << bitdepth) - 1;
    heif_image* img = nullptr;
    bool ok = false;
    std::vector<uint8_t> blob;
    heif_image_handle* handle = nullptr;
    heif_context* rctx = nullptr;
    heif_image_handle* rhandle = nullptr;
    heif_image* decoded = nullptr;
    do {
        e = heif_image_create(W, H, heif_colorspace_YCbCr, heif_chroma_420, &img);
        if (e.code != heif_error_Ok) { why = "image_create: " + heif_error_text(e); break; }
        const heif_channel chans[3] = {heif_channel_Y, heif_channel_Cb, heif_channel_Cr};
        const int ws[3] = {W, cw, cw}, hs[3] = {H, ch, ch};
        bool planes_ok = true;
        for (int i = 0; i < 3; i++) {
            e = heif_image_add_plane(img, chans[i], ws[i], hs[i], bitdepth);
            if (e.code != heif_error_Ok) {
                why = "add_plane " + std::to_string(bitdepth) + "-bit: " + heif_error_text(e);
                planes_ok = false;
                break;
            }
            size_t stride = 0;
            uint8_t* base = heif_image_get_plane2(img, chans[i], &stride);
            if (!base) { why = "no plane buffer"; planes_ok = false; break; }
            for (int y = 0; y < hs[i]; y++)
                for (int x = 0; x < ws[i]; x++)
                    store_sample(base, stride, bitdepth, x, y, (x * maxv) / (ws[i] - 1));
        }
        if (!planes_ok) break;
        heif_color_profile_nclx nclx{};
        nclx.version = 1;
        nclx.color_primaries          = heif_color_primaries_ITU_R_BT_709_5;
        nclx.transfer_characteristics = heif_transfer_characteristic_IEC_61966_2_1;
        nclx.matrix_coefficients      = heif_matrix_coefficients_ITU_R_BT_601_6;
        nclx.full_range_flag          = 1;
        heif_image_set_nclx_color_profile(img, &nclx);
        (void)heif_encoder_set_lossy_quality(enc, 90);  // best effort quality baseline
        e = heif_context_encode_image(ctx, img, enc, nullptr, &handle);
        if (e.code != heif_error_Ok) { why = "encode: " + heif_error_text(e); break; }
        heif_writer writer{};
        writer.writer_api_version = 1;
        writer.write = probe_sink_write;
        e = heif_context_write(ctx, &writer, &blob);
        if (e.code != heif_error_Ok || blob.empty()) {
            why = "write: " + heif_error_text(e);
            break;
        }
        // read back
        rctx = heif_context_alloc();
        if (!rctx) { why = "readback context alloc failed"; break; }
        e = heif_context_read_from_memory_without_copy(rctx, blob.data(), blob.size(), nullptr);
        if (e.code != heif_error_Ok) { why = "readback: " + heif_error_text(e); break; }
        e = heif_context_get_primary_image_handle(rctx, &rhandle);
        if (e.code != heif_error_Ok) { why = "readback handle: " + heif_error_text(e); break; }
        e = heif_decode_image(rhandle, &decoded, heif_colorspace_undefined, heif_chroma_undefined, nullptr);
        if (e.code != heif_error_Ok) { why = "decode: " + heif_error_text(e); break; }
        const int got = heif_image_get_bits_per_pixel_range(decoded, heif_channel_Y);
        if (got != bitdepth) {
            why = "decoded bit depth " + std::to_string(got) + " != " + std::to_string(bitdepth);
            break;
        }
        ok = true;
    } while (false);

    if (decoded) heif_image_release(decoded);
    if (rhandle) heif_image_handle_release(rhandle);
    if (rctx) heif_context_free(rctx);
    if (handle) heif_image_handle_release(handle);
    if (img) heif_image_release(img);
    heif_encoder_release(enc);
    heif_context_free(ctx);
    return ok;
}

std::mutex& probe_mutex() {
    static std::mutex m;
    return m;
}
std::map<std::string, std::string>& probe_cache() {
    static std::map<std::string, std::string> c;
    return c;
}

}  // namespace

std::vector<BackendDef> introspect_backends(std::string_view format_id) {
    std::vector<BackendDef> out;
    heif_compression_format fmt = heif_compression_HEVC;
    if (!libheif_format(format_id, fmt)) return out;  // non-libheif -> empty

    for (const heif_encoder_descriptor* desc : enumerate_descriptors(fmt)) {
        BackendDef b;
        const char* id_name = heif_encoder_descriptor_get_id_name(desc);
        const char* long_name = heif_encoder_descriptor_get_name(desc);
        b.id = canonical_backend_id(id_name ? id_name : "");
        b.label = (long_name && *long_name) ? long_name : b.id;
        b.runtime_introspected = true;

        heif_context* ctx = heif_context_alloc();
        heif_encoder* enc = nullptr;
        if (ctx && heif_context_get_encoder(ctx, desc, &enc).code == heif_error_Ok && enc) {
            TechDef t;
            t.id = "runtime";  // single synthetic tech: params come from the codec plugin
            t.label = b.label;
            t.lossless_capable = heif_encoder_descriptor_supports_lossless_compression(desc) != 0;
            t.params = introspect_live(enc);
            b.techs.push_back(std::move(t));
            heif_encoder_release(enc);
        } else {
            log_warn(kStage, kFile, "could not instantiate encoder for introspection",
                     {{"format", format_id}, {"backend", b.id}});
        }
        if (ctx) heif_context_free(ctx);
        out.push_back(std::move(b));
    }
    return out;
}

std::string probe_bitdepth_support(std::string_view format_id, std::string_view backend_id) {
    heif_compression_format fmt = heif_compression_HEVC;
    if (!libheif_format(format_id, fmt)) return {};
    const std::string key = std::string(format_id) + "/" + std::string(backend_id);
    {
        std::lock_guard<std::mutex> lock(probe_mutex());
        const auto it = probe_cache().find(key);
        if (it != probe_cache().end()) return it->second;
    }
    std::string result;
    std::string detail;
    for (const int depth : {8, 10, 12}) {
        std::string why;
        const bool ok = bitdepth_roundtrip_ok(fmt, backend_id, depth, why);
        if (ok) {
            if (!result.empty()) result += ",";
            result += std::to_string(depth);
        } else {
            detail += (detail.empty() ? "" : "; ") + std::to_string(depth) + "bit: " + why;
        }
    }
    if (result.empty()) {
        // The contract only allows "8" / "8,10" / "8,10,12": a backend that cannot even do
        // 8-bit is a broken environment -> report the minimum and shout in the log.
        result = "8";
        log_error(kStage, kFile, "8-bit probe failed; reporting minimum",
                  {{"format", format_id}, {"backend", backend_id}, {"detail", detail}});
    } else {
        log_info(kStage, kFile, "bit depth probe",
                 {{"format", format_id}, {"backend", backend_id}, {"supported", result},
                  {"unsupported", detail}});
    }
    {
        std::lock_guard<std::mutex> lock(probe_mutex());
        probe_cache()[key] = result;
    }
    return result;
}

}  // namespace pp
