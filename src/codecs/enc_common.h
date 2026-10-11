// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — codecs/enc_common（M7-WP8 / B.7）：编码器族公共面单源。
//
// Contract: docs/deep-review-0.3.2.md 附录 B.7（enc_common 抽取清单）+ R-156（五连拷贝约
// 500 行）/ R-162（错误收尾四口径漂移）/ R-165（像素取样双口径）。落地口径（写进
// encoder.h 的 IEncoder 契约注释，此处为实现面单源）：
//   * **错误收尾单口径**（R-162）：失败 = `EncodeResult{bytes=0, error≠空, t 已填}`；
//     **错误级日志归 pipeline**（pipeline.cpp 的 `encode failed` 行），编码器不再 log_error
//     —— 消"失败双记"（旧 heif/oiio fail() 与 pipeline 各记一条）。E9 参数面的 log_warn
//     （warn_unknown_params / note_param）保留：那是配置缺陷证据，不是编码失败。
//   * **像素取样单口径**（R-165）：一切取样窗口都从 `img.roi()` 派生 —— 内存型编码器
//     （jpegli/jxl/webp）取整帧 `fetch_raster`；流式编码器（heif/oiio）逐行 `fetch_row`
//     （行窗 = roi 的第 y 行）。裁剪视图下两者取到同一像素面；pipeline 的工作缓冲
//     （roi == spec 窗）行为逐字不变。
//   * **落盘单口径**（R-167）：内存型产物一律 `write_file`（= fsops::write_atomic_file 的
//     唯一转发点）；库句柄清理用 `ScopeGuard`/`on_scope_exit`（消 webp 五出口手动清理）。
#pragma once
#include "codecs/encoder.h"
#include "core/types.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <OpenImageIO/imagebuf.h>

namespace pp::enc {

using Clock = std::chrono::steady_clock;

// 毫秒计时（编码器族唯一实现；旧五份逐字拷贝删除）。
double ms_since(Clock::time_point t0);

// 失败收尾（R-162 单口径）：bytes=0 + error；Timing 由调用方的 encode() 外壳统一补填
// （成败皆填，见各编码器 `encode()` 外壳注释）。
EncodeResult encode_error(std::string msg);

// 成功收尾：补 Timing（encode_ms/total_ms 同值口径，与 heif/oiio 旧 finish() 一致）。
void finish_encode(EncodeResult &res, Clock::time_point t0);

// E9：未识别参数键 → log_warn（永不进 EncodeResult.warnings，M1 裁定口径）。
// `known_keys` = 该编码器认识的键（"__" 前缀保留键一律跳过）。
void warn_unknown_params(const ParamSet &s, std::string_view encoder_id,
                         std::span<const std::string_view> known_keys);
// 同上，判据由谓词承载（jxl 的双表形态）。
void warn_unknown_params_if(const ParamSet &s, std::string_view encoder_id,
                            const std::function<bool(std::string_view)> &is_known);

// 参数值的日志文本（E9 note_param 用；monostate → "<empty>"）。
std::string param_value_text(const ParamValue &v);

// ------------------------------------------------------------------ pixels --
// 整帧取样（内存型编码器）：像素面 = `img.roi()`（裁剪视图口径，R-165）。
struct Raster {
    int width = 0, height = 0, channels = 0;
    std::vector<float> px; // interleaved float32
};

bool fetch_raster(const OIIO::ImageBuf &img, Raster &out, std::string &err);

// 逐行取样（流式编码器）：行窗 = `img.roi()` 的第 y 行（0-based），dst 需 ≥ w×nch。
bool fetch_row(const OIIO::ImageBuf &img, int y, float *dst, std::string &err);

// ------------------------------------------------------------------- 落盘 --
// 内存型产物的唯一落盘点（= fsops::write_atomic_file 转发：tmp→字节数校验→rename，失败清场）。
bool write_file(const std::filesystem::path &target, std::string_view data, std::string *err);

// 作用域清理守卫（R-167：库句柄/缓冲的 RAII 收口，消手动清理多出口）。
template <class F> class ScopeGuard {
public:
    explicit ScopeGuard(F f) : f_(std::move(f)) {}
    ~ScopeGuard() {
        if (active_)
            f_();
    }
    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;
    void dismiss() { active_ = false; }

private:
    F f_;
    bool active_ = true;
};

template <class F> ScopeGuard<F> on_scope_exit(F f) { return ScopeGuard<F>(std::move(f)); }

} // namespace pp::enc
