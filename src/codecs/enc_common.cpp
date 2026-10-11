// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — codecs/enc_common 实现（M7-WP8 / B.7）。
//
// Contract: docs/deep-review-0.3.2.md 附录 B.7 + R-156/R-162/R-165/R-167；
// 口径逐条见 enc_common.h 文件头。本文件只做无状态纯工具：不做参数语义、不碰编码库。
#include "codecs/enc_common.h"

#include <cstdio>
#include <string>

#include "core/fsops.h" // write_atomic_file（R-003 原子落盘的唯一实现）
#include "core/logger.h"

namespace pp::enc {

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

EncodeResult encode_error(std::string msg) {
    EncodeResult res;
    res.bytes = 0;
    res.error = std::move(msg);
    return res;
}

void finish_encode(EncodeResult &res, Clock::time_point t0) {
    res.t.encode_ms = ms_since(t0);
    res.t.total_ms = res.t.encode_ms; // 旧 heif/oiio finish() 口径（total == encode）
}

void warn_unknown_params(const ParamSet &s, std::string_view encoder_id,
                         std::span<const std::string_view> known_keys) {
    warn_unknown_params_if(s, encoder_id, [known_keys](std::string_view key) {
        for (std::string_view k : known_keys) {
            if (k == key)
                return true;
        }
        return false;
    });
}

void warn_unknown_params_if(const ParamSet &s, std::string_view encoder_id,
                            const std::function<bool(std::string_view)> &is_known) {
    for (const auto &[key, value] : s) {
        (void)value;
        if (key.rfind("__", 0) == 0) {
            continue; // reserved keys (§3.4 convention)
        }
        if (!is_known(key)) {
            log_warn("Encode", "enc_common.cpp", "unknown parameter ignored",
                     {{"encoder", encoder_id}, {"key", key}});
        }
    }
}

std::string param_value_text(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.10g", *d);
        return buf;
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return *s;
    return "<empty>";
}

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
    // R-165 单口径：整帧 = img.roi()（裁剪视图下与 fetch_row 同一像素面）。
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

bool fetch_row(const OIIO::ImageBuf &img, int y, float *dst, std::string &err) {
    if (!img.initialized()) {
        err = "input image buffer is not initialized";
        return false;
    }
    const OIIO::ImageSpec &spec = img.spec();
    const int nch = spec.nchannels;
    const OIIO::ROI roi = img.roi();
    const int w = static_cast<int>(roi.width());
    const int h = static_cast<int>(roi.height());
    if (w <= 0 || h <= 0 || y < 0 || y >= h) {
        err = "empty image region";
        return false;
    }
    // R-165 单口径：行窗 = img.roi() 的第 y 行（不是 spec.x/spec.y 的窗口）。
    const OIIO::ROI row_roi(roi.xbegin, roi.xend, roi.ybegin + y, roi.ybegin + y + 1, 0, 1, 0, nch);
    if (!img.get_pixels(row_roi, OIIO::TypeDesc::FLOAT, dst)) {
        err = img.geterror();
        if (err.empty()) {
            err = "ImageBuf::get_pixels failed";
        }
        return false;
    }
    return true;
}

bool write_file(const std::filesystem::path &target, std::string_view data, std::string *err) {
    return write_atomic_file(target, data, err);
}

} // namespace pp::enc
