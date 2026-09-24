// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — probe + thumbnail (contract: docs/m1b-tasks.md §2.1 PP-FROZEN).
//
// Pipeline (docs/m1b-tasks.md §2.1):
//   1) probe_file(src): failure -> {probe_ok=false, error};
//   2) embedded preview (Exiv2 PreviewManager, largest preview with long edge >= 64)
//      decoded through an OIIO in-memory reader and resampled to the target;
//   3) otherwise a full float32 decode (pp::decode_float) and resample to the target;
//   4) thumbnail failure after a successful probe -> empty thumb, empty error;
//   5) never upscale (scale = min(target/w, target/h, 1.0)); gray -> RGB; alpha -> white.
//
// API facts checked against the installed headers (vcpkg_installed/x64-linux/include) and
// the Exiv2 0.28.8 sources (src/preview.cpp, official repository):
//   * Exiv2::PreviewManager::getPreviewProperties() is sorted ascending by pixel count and
//     has already decoded/validated the preview dimensions (Loader::readDimensions()).
//   * OIIO: Filesystem::IOMemReader + ImageInput::open(name, config, ioproxy) for memory
//     decoding, ImageBufAlgo::resample() for the box-ish resample (bilinear, fast).
//
// M4-W2-T10（0.3.0）追加：输入预览通道（§3.6 解冻行 `decode_preview` + §6.1 的 LRU 缓存）。
//   本 TU 现在有两条通道，**通道规则单一实现**：
//     * 缩略图（make_thumbnail，M1b 冻结）：resample（双线性）+ ThumbImage（紧凑 vector）；
//     * 预览（decode_preview/decode_preview_detailed/PreviewCache）：LANCZOS3 + QImage。
//   两条通道共用同一份「通道展开 / 灰度→RGB / alpha→白底 / 不放大」实现（expand_to_rgba8 +
//   fit_target），只有滤波器和输出容器不同；共享内嵌预览候选规则（find_preview，≥64px）。
//   §6.1 细则的落地口径与边界（逐条对应，见 core/thumbs.h 的落地处置块）：
//     * 内嵌预览优先，候选门槛 = kPreviewEmbeddedMinLongEdge（64px，与缩略图同值）；
//     * 短于 max_px 的内嵌预览按原生尺寸呈现（**不放大**）；≥ max_px 时 LANCZOS3 精确降采样；
//     * 无内嵌预览 → 复用**既有冻结的全解码入口** decode_float（读整图 float32；管线与预览
//       不会出现两套解码口径）→ LANCZOS3 降采样（实测：48MP 全解码 ≈ 0.33s，见 test_thumbs）；
//     * 全程不做色彩变换（像素 = 源像素，sRGB 假定；徽标「输入 · 未修改像素」的字面语义）；
//       色彩空间只出**描述名**（ICC 描述经 lcms2 读元数据，无 ICC → "sRGB 假定"）；
//     * M4-W2-fix（性能，第 12 条）：降采样 = **多线程 LANCZOS3**（IBA::resize nthreads=0，
//       本机 16 核）；滤波器/中间精度/不放大/白底语义不变（只并行化，不改像素结果）。
//       预览池自身仍是 ≤2 并发（§6.1「不与转码抢核」的"池并发"口径不变）。

#include "core/thumbs.h"

#include <QImage>

#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>
#include <exiv2/exiv2.hpp>
#include <exiv2/preview.hpp>
#include <lcms2.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/simd/simd.h" // M4-W5-T19：LANCZOS3 降采样热路径（§11.2）
#include "decode/oiio_reader.h"

namespace pp {
namespace {

// §2.1 step 2: only embedded previews whose long edge is >= 64 px are considered.
// 0.3.0：数字的唯一来源是冻结头里的 kPreviewEmbeddedMinLongEdge（缩略图与预览共用一个门槛）。
constexpr std::size_t kMinPreviewLongEdge = static_cast<std::size_t>(kPreviewEmbeddedMinLongEdge);

struct TargetSize {
    int w = 0;
    int h = 0;
};

// §2.1 step 5: scale = min(target/w, target/h, 1.0) — never upscale.
TargetSize fit_target(int w, int h, int target) {
    assert(w > 0 && h > 0 && target > 0);
    const double scale =
        std::min({1.0, static_cast<double>(target) / w, static_cast<double>(target) / h});
    if (scale >= 1.0)
        return {w, h};
    TargetSize ts;
    ts.w = std::clamp(static_cast<int>(std::lround(w * scale)), 1, target);
    ts.h = std::clamp(static_cast<int>(std::lround(h * scale)), 1, target);
    return ts;
}

std::uint8_t to_u8(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// 通道展开（缩略图与预览的**唯一实现**）：1..4 通道 float32 → 8bit RGBA。
// 规则（§2.1 step 5，两条通道逐字一致）：gray → RGB；gray+alpha → RGB；alpha 合成到白底；
// 输出 alpha 恒 255；不做色彩变换（sRGB 假定）。dst_stride 让 ThumbImage 的紧凑 vector 与
// QImage 的 scanLine（可能带行对齐填充）共用本函数。
void expand_to_rgba8(const float *px, int width, int height, int nch, std::uint8_t *dst,
                     std::size_t dst_stride) {
    assert(px != nullptr && dst != nullptr && width > 0 && height > 0 && nch >= 1 && nch <= 4);
    for (int y = 0; y < height; ++y) {
        std::uint8_t *row = dst + static_cast<std::size_t>(y) * dst_stride;
        const float *src_row = px + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) *
                                        static_cast<std::size_t>(nch);
        for (int x = 0; x < width; ++x) {
            const float *p = src_row + static_cast<std::size_t>(x) * static_cast<std::size_t>(nch);
            float r = p[0];
            float g = p[0];
            float b = p[0];
            float a = 1.0f;
            if (nch >= 3) {
                g = p[1];
                b = p[2];
                if (nch == 4)
                    a = p[3];
            } else if (nch == 2) {
                a = p[1]; // gray + alpha
            }
            // alpha -> white composite (§2.1 step 5)
            a = std::clamp(a, 0.0f, 1.0f);
            if (a < 1.0f) {
                const float bg = 1.0f - a;
                r = r * a + bg;
                g = g * a + bg;
                b = b * a + bg;
            }
            std::uint8_t *q = row + static_cast<std::size_t>(x) * 4u;
            q[0] = to_u8(r);
            q[1] = to_u8(g);
            q[2] = to_u8(b);
            q[3] = 255;
        }
    }
}

// Decode 1..4 channel float pixels into 8-bit RGBA on a white background, downsampled so
// that the longest edge is <= target (steps 4/5). Returns false if no thumbnail can be made.
bool to_rgba8(const OIIO::ImageBuf &src, int target, ThumbImage &out) {
    if (!src.initialized())
        return false;
    const OIIO::ImageSpec &spec = src.spec();
    const int sw = spec.width;
    const int sh = spec.height;
    const int nch = spec.nchannels;
    if (sw <= 0 || sh <= 0)
        return false;
    assert(nch >= 1 && nch <= 4); // pp::decode_float()/the preview reader enforce 1..4
    if (nch < 1 || nch > 4)
        return false;

    const TargetSize ts = fit_target(sw, sh, target);
    const OIIO::ROI roi(0, ts.w, 0, ts.h); // destination ROI (== whole image)

    OIIO::ImageBuf scaled;
    const OIIO::ImageBuf *work = &src;
    if (ts.w != sw || ts.h != sh) {
        OIIO::ImageBuf dst(OIIO::ImageSpec(ts.w, ts.h, nch, OIIO::TypeFloat));
        if (!OIIO::ImageBufAlgo::resample(dst, src, /*interpolate=*/true, roi))
            return false;
        scaled = std::move(dst);
        work = &scaled;
    }

    const std::size_t pixels = static_cast<std::size_t>(ts.w) * static_cast<std::size_t>(ts.h);
    std::vector<float> px(pixels * static_cast<std::size_t>(nch));
    if (!work->get_pixels(roi, OIIO::TypeFloat, px.data()))
        return false;

    out.width = ts.w;
    out.height = ts.h;
    out.rgba.assign(pixels * 4, 255); // alpha is composited away -> always 255
    expand_to_rgba8(px.data(), ts.w, ts.h, nch, out.rgba.data(),
                    static_cast<std::size_t>(ts.w) * 4u);
    return true;
}

// Decode an in-memory embedded preview to float32. `data` must stay alive for the call (the
// ImageInput borrows it through the IOProxy). Shared by the thumbnail and preview channels.
bool preview_bytes_to_buf(const void *data, std::size_t size, const std::string &name_hint,
                          OIIO::ImageBuf &out) {
    if (data == nullptr || size == 0)
        return false;

    OIIO::Filesystem::IOMemReader reader(data, size);
    auto in = OIIO::ImageInput::open(name_hint, nullptr, &reader);
    if (!in)
        return false;

    const OIIO::ImageSpec spec = in->spec(); // copy: still valid after close()
    if (spec.width <= 0 || spec.height <= 0 || spec.nchannels < 1 || spec.nchannels > 4) {
        in->close();
        return false;
    }
    std::vector<float> px(static_cast<std::size_t>(spec.width) *
                          static_cast<std::size_t>(spec.height) *
                          static_cast<std::size_t>(spec.nchannels));
    const bool ok = in->read_image(0, 0, 0, spec.nchannels, OIIO::TypeFloat, px.data());
    in->close();
    if (!ok)
        return false;

    // The spec must describe the memory actually handed to the ImageBuf: float32, own copy.
    OIIO::ImageSpec fspec = spec;
    fspec.set_format(OIIO::TypeFloat);
    OIIO::ImageBuf buf(fspec);
    if (!buf.set_pixels(OIIO::ROI(0, spec.width, 0, spec.height), OIIO::TypeFloat, px.data()))
        return false;
    out = std::move(buf);
    return true;
}

// Decode an in-memory embedded preview to float32 and turn it into a thumbnail (M1b path).
bool preview_bytes_to_thumb(const void *data, std::size_t size, const std::string &name_hint,
                            int target, ThumbImage &out) {
    OIIO::ImageBuf buf;
    if (!preview_bytes_to_buf(data, size, name_hint, buf))
        return false;
    return to_rgba8(buf, target, out);
}

// §2.1 step 2: largest embedded preview with a long edge >= 64 px. Exiv2 fills width/height
// by decoding the preview itself, so 0 (unknown) means "cannot check the rule" -> skip; the
// full decode below still produces a valid thumbnail in that case.
bool find_preview(const Exiv2::Image &image, Exiv2::PreviewProperties &out) {
    const Exiv2::PreviewManager manager(image);
    const Exiv2::PreviewPropertiesList props = manager.getPreviewProperties(); // ascending
    for (auto it = props.rbegin(); it != props.rend(); ++it) {
        if (it->width_ == 0 || it->height_ == 0)
            continue;
        if (std::max(it->width_, it->height_) < kMinPreviewLongEdge)
            continue;
        out = *it;
        return true;
    }
    return false;
}

// Returns true only when a usable thumbnail was produced from an embedded preview; every
// failure (no Exiv2 support for the container, no preview, undecodable preview) falls
// through silently to the full decode (step 3).
bool try_embedded_preview(const std::filesystem::path &src, int target, ThumbImage &out) {
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(src.string());
        if (!image)
            return false;
        image->readMetadata();

        Exiv2::PreviewProperties props;
        if (!find_preview(*image, props))
            return false;

        const Exiv2::PreviewManager manager(*image);
        const Exiv2::PreviewImage preview = manager.getPreviewImage(props);
        if (preview.pData() == nullptr || preview.size() == 0)
            return false;

        std::string hint = "embedded-preview";
        hint += preview.extension().empty() ? std::string(".jpg") : preview.extension();
        return preview_bytes_to_thumb(preview.pData(), preview.size(), hint, target, out);
    } catch (const Exiv2::Error &) {
        return false;
    } catch (const std::exception &) {
        return false;
    } catch (...) {
        return false;
    }
}

// ------------------------------------------------------------------------------------------
// 0.3.0（M4-W2-T10）：输入预览通道
// ------------------------------------------------------------------------------------------

// §6.1：float32 源 → LANCZOS3 缩放到 max_px 内（不放大）→ 8bit RGBA（白底、sRGB 假定）。
//   * 滤波器 LANCZOS3 是 §6.1 指定的降采样滤波器；内部全程 float32（铁律）。
//   * M4-W5-T19 接线（design §11.2 热路径表第 4 行）：降采样实现从 OIIO
//     `ImageBufAlgo::resize(filtername=lanczos3, dst_datatype=float, nthreads=0)` 换成
//     **pp::simd::downscale**（LANCZOS3 水平 + 垂直两遍卷积，AVX2；唯一运行期分派层）。
//     语义等价（硬要求）：权重公式、支撑按 ratio 展宽、权重归一化、边缘 clamp、两遍分离
//     逐式对齐 OIIO 的 resize_（逐条出处见 core/simd/resample.cpp 头注）；test_simd 的
//     OIIO 交叉断言实测上界 max|Δ| = 1.073e-06（200×150→64×48，见该测试的 printf 行）。
//     **只改"怎么算"不改"算什么"**：滤波器 lanczos3、float32 中间面、不放大、白底、
//     sRGB 假定逐条不变。
//   * 线程面（W2-fix 第 12 条的修订，如实记录）：原 nthreads=0 的多线程并行落在 OIIO 内部；
//     自研两遍实现当前**单线程**（§11.2 未要求 downscale 并行）。耗时实测见 T19 selfChecks
//     （48MP→2048 与 W2-fix 记录的 OIIO 单段 499ms 基线对照）。
bool scale_to_rgba8(const OIIO::ImageBuf &src, int max_px, QImage &out) {
    const OIIO::ImageSpec spec = src.spec(); // 文件型 ImageBuf：此处触发惰性 spec 读取
    const int sw = spec.width;
    const int sh = spec.height;
    const int nch = spec.nchannels;
    if (sw <= 0 || sh <= 0 || nch < 1 || nch > 4)
        return false;

    const TargetSize ts = fit_target(sw, sh, max_px);
    const OIIO::ROI roi(0, ts.w, 0, ts.h);
    const std::size_t pixels = static_cast<std::size_t>(ts.w) * static_cast<std::size_t>(ts.h);
    std::vector<float> px(pixels * static_cast<std::size_t>(nch));

    if (ts.w == sw && ts.h == sh) {
        if (!src.get_pixels(roi, OIIO::TypeFloat, px.data()))
            return false;
    } else {
        // 源面：零拷贝取 ImageBuf 的本地缓冲（全解码通道 decode_float 与内嵌预览通道
        // preview_bytes_to_buf 都落在本地内存）；非本地（文件型）时回退一份整帧拷贝
        // （语义不变，内存 +1 帧 —— 如实记账，见 T19 偏差账）。
        std::vector<float> owned;
        const float *src_px = nullptr;
        if (src.localpixels() != nullptr && spec.format == OIIO::TypeDesc::FLOAT) {
            src_px = static_cast<const float *>(src.localpixels());
        } else {
            owned.resize(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) *
                         static_cast<std::size_t>(nch));
            const OIIO::ROI full(0, sw, 0, sh);
            if (!src.get_pixels(full, OIIO::TypeFloat, owned.data()))
                return false;
            src_px = owned.data();
        }
        pp::simd::downscale(src_px, sw, sh, nch, px.data(), ts.w, ts.h, nch);
    }

    QImage image(ts.w, ts.h, QImage::Format_RGBA8888);
    if (image.isNull())
        return false;
    expand_to_rgba8(px.data(), ts.w, ts.h, nch, image.bits(),
                    static_cast<std::size_t>(image.bytesPerLine()));
    out = std::move(image);
    return true;
}

// §6.1「先内嵌预览」：Exiv2 候选（find_preview，≥64px 取最大）经 OIIO 内存读为 float32 后
// 走同一条 LANCZOS3/不放大/白底路径。任何失败（无候选/容器不支持/预览不可解）静默回退全解码。
bool try_embedded_preview_image(const std::filesystem::path &src, int max_px, QImage &out) {
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(src.string());
        if (!image)
            return false;
        image->readMetadata();

        Exiv2::PreviewProperties props;
        if (!find_preview(*image, props))
            return false;

        const Exiv2::PreviewManager manager(*image);
        const Exiv2::PreviewImage preview = manager.getPreviewImage(props);
        if (preview.pData() == nullptr || preview.size() == 0)
            return false;

        std::string hint = "embedded-preview";
        hint += preview.extension().empty() ? std::string(".jpg") : preview.extension();
        OIIO::ImageBuf buf;
        if (!preview_bytes_to_buf(preview.pData(), preview.size(), hint, buf))
            return false;
        return scale_to_rgba8(buf, max_px, out);
    } catch (const Exiv2::Error &) {
        return false;
    } catch (const std::exception &) {
        return false;
    } catch (...) {
        return false;
    }
}

// §6.1「否则 OIIO 读整图后 LANCZOS3 降采样」：复用**既有冻结的全解码入口** decode_float
// （读整图为 float32，通道面/CMYK/多页语义与管线逐字一致 → 预览与管线不会出现两套解码口径），
// 再走同一条 LANCZOS3/不放大/白底路径。
//   内存口径（实测记录，M4-W2-T10）：48MP 输入的全解码瞬时 ≈ 48M×3×4B = 576MB/张，与缩略图通道
//   既有的 NOTE(perf) 同口径（管线自身的像素预算是 2×frame，预览池 ≤2 张在途 → 峰值与管线同量级；
//   稳态由 LRU 上限约束）。"读整图为 float32"是铁律「内部全程 float32」的直接后果。
bool decode_full_to_rgba8(const std::filesystem::path &src, const ImageInfo &info, int max_px,
                          QImage &out) {
    const DecodeOutcome decoded = decode_float(src, info);
    if (!decoded.error.empty() || !decoded.buf.initialized())
        return false;
    return scale_to_rgba8(decoded.buf, max_px, out);
}

// 源色彩空间**描述名**（只读元数据；像素路径不做任何变换）：
//   * ICC → lcms2 的 profile description ASCII 名（如 "Display P3" / "sRGB IEC61966-2.1"）；
//   * 无 ICC / 读取失败 → "sRGB 假定"（缩略图口径 v1：sRGB 假定，与既有决策一致）。
std::string color_space_name(const std::string &icc) {
    if (icc.empty())
        return "sRGB 假定";
    cmsHPROFILE profile =
        cmsOpenProfileFromMem(icc.data(), static_cast<cmsUInt32Number>(icc.size()));
    if (profile == nullptr)
        return "ICC";
    char buffer[256] = {};
    const cmsUInt32Number written =
        cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", buffer,
                               static_cast<cmsUInt32Number>(sizeof(buffer)));
    cmsCloseProfile(profile);
    if (written == 0)
        return "ICC";
    std::string name(buffer); // NUL 截断（lcms2 写入的是 C 串）
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
        name.pop_back();
    return name.empty() ? std::string("ICC") : name;
}

// 缓存键：纯字符串归一化（不做 IO/不解析符号链接），使 UI 侧同一路径的重复请求稳定命中。
std::string cache_key(const std::filesystem::path &src) { return src.lexically_normal().string(); }

} // namespace

ThumbOutcome make_thumbnail(const std::filesystem::path &src, int target_long_edge) {
    assert(target_long_edge > 0); // internal invariant (UI default: 96)
    if (target_long_edge <= 0)
        target_long_edge = 1; // defensive; keeps fit_target's clamp sane

    ThumbOutcome out;

    // 1) probe
    const ProbeOutcome probe = probe_file(src);
    out.info = probe.info;
    if (!probe.error.empty()) {
        out.probe_ok = false;
        out.error = probe.error;
        return out;
    }
    out.probe_ok = true;

    // 2) embedded preview first
    ThumbImage thumb;
    if (try_embedded_preview(src, target_long_edge, thumb)) {
        thumb.from_embedded = true;
        out.thumb = std::move(thumb);
        return out;
    }

    // 3) full float32 decode + resample
    // NOTE(perf): a full float32 decode of a large input (tens of MP) costs hundreds of MB per
    //           in-flight thumbnail but stays inside the thumbnail memory budget; a
    //           reduced-resolution decode (ImageCache/mip level) is not needed for M2.
    const DecodeOutcome decoded = decode_float(src, probe.info);
    if (!decoded.error.empty() || !decoded.buf.initialized()) {
        return out; // 4) thumb empty, error stays empty (probe itself succeeded)
    }
    thumb = ThumbImage{};
    if (to_rgba8(decoded.buf, target_long_edge, thumb)) {
        thumb.from_embedded = false;
        out.thumb = std::move(thumb);
    }
    return out;
}

// ============================================================================================
// PP-FROZEN(0.3.0) §3.6 · 输入预览（0.3.0 追加面；见 core/thumbs.h 的冻结形态与 §6.1 摘录）
// ============================================================================================

PreviewImage decode_preview_detailed(const std::filesystem::path &src, int max_px) {
    PreviewImage out;
    if (max_px <= 0)
        max_px = kPreviewCacheMaxPx; // 非法入参 → 冻结口径的默认上限（不抛异常）

    const ProbeOutcome probe = probe_file(src);
    out.info = probe.info;
    out.color_space = color_space_name(
        probe.first_spec.has_value() ? icc_from_spec(*probe.first_spec) : std::string());
    if (!probe.error.empty()) {
        out.error = probe.error; // CMYK / 打不开 / …：probe 的英文详情（UI 侧加中文前缀）
        return out;
    }

    try {
        // §6.1「优先内嵌预览」
        if (try_embedded_preview_image(src, max_px, out.image)) {
            out.from_embedded = true;
            return out;
        }
        // §6.1「否则 OIIO 读整图后 LANCZOS3 降采样到 max_px 内」
        if (!decode_full_to_rgba8(src, probe.info, max_px, out.image)) {
            out.image = QImage();
            out.error = "preview decode failed: " + src.filename().string();
        }
    } catch (const std::exception &e) {
        out.image = QImage();
        out.error = std::string("preview decode failed: ") + e.what();
    } catch (...) {
        out.image = QImage();
        out.error = "preview decode failed: unknown error";
    }
    return out;
}

QImage decode_preview(const std::filesystem::path &src, int max_px) {
    return decode_preview_detailed(src, max_px).image; // §3.6 冻结形态（失败 = null QImage）
}

// ------------------------------------------------------------------------------------------
// PreviewCache（§6.1「LRU 内存缓存（16 张 2048px 上限）」）
// ------------------------------------------------------------------------------------------

struct PreviewCache::Impl {
    struct Entry {
        std::string key; // 归一化路径（cache_key）
        QImage image;    // 8bit RGBA8888（隐式共享；拷贝廉价）
        bool from_embedded = false;
        ImageInfo info;
        std::string color_space;
    };

    Impl(std::size_t cap, int px) : capacity(cap > 0 ? cap : 1), max_px(px > 0 ? px : 1) {}

    std::size_t capacity = kPreviewCacheCapacity;
    int max_px = kPreviewCacheMaxPx;
    mutable std::mutex mu;
    std::list<Entry> lru;                                       // front = MRU，back = LRU
    std::map<std::string, std::list<Entry>::iterator> index;    // key → LRU 位置
    std::size_t hits = 0, misses = 0, evictions = 0, bytes = 0; // 记账（测试/诊断）
};

PreviewCache::PreviewCache(std::size_t capacity, int max_px)
    : impl_(std::make_unique<Impl>(capacity, max_px)) {}

PreviewCache::~PreviewCache() = default;

PreviewImage PreviewCache::get(const std::filesystem::path &src) {
    Impl &im = *impl_;
    const std::string key = cache_key(src);
    {
        const std::lock_guard<std::mutex> lock(im.mu);
        const auto it = im.index.find(key);
        if (it != im.index.end()) {
            im.lru.splice(im.lru.begin(), im.lru, it->second); // 命中 → 提升为最近使用
            ++im.hits;
            const Impl::Entry &entry = *it->second;
            PreviewImage hit;
            hit.image = entry.image;
            hit.from_embedded = entry.from_embedded;
            hit.info = entry.info;
            hit.color_space = entry.color_space;
            return hit;
        }
        ++im.misses;
    }

    // 解码在锁外（2 个预览 worker 可并发 miss，不互相阻塞）
    PreviewImage result = decode_preview_detailed(src, im.max_px);
    if (!result.error.empty() || result.image.isNull())
        return result; // 失败结果不入缓存（文件暂时读不了不该被钉死）

    const std::lock_guard<std::mutex> lock(im.mu);
    if (const auto it = im.index.find(key); it != im.index.end()) {
        // 并发线程刚刚插入同键：刷新内容并提升（不新增条目）
        Impl::Entry &entry = *it->second;
        im.bytes -= static_cast<std::size_t>(entry.image.sizeInBytes());
        entry.image = result.image;
        entry.from_embedded = result.from_embedded;
        entry.info = result.info;
        entry.color_space = result.color_space;
        im.bytes += static_cast<std::size_t>(entry.image.sizeInBytes());
        im.lru.splice(im.lru.begin(), im.lru, it->second);
        return result;
    }
    im.lru.push_front(
        Impl::Entry{key, result.image, result.from_embedded, result.info, result.color_space});
    im.index.emplace(key, im.lru.begin());
    im.bytes += static_cast<std::size_t>(result.image.sizeInBytes());
    while (im.lru.size() > im.capacity) { // LRU 逐出最旧
        const auto oldest = std::prev(im.lru.end());
        im.bytes -= static_cast<std::size_t>(oldest->image.sizeInBytes());
        im.index.erase(oldest->key);
        im.lru.erase(oldest);
        ++im.evictions;
    }
    return result;
}

std::size_t PreviewCache::size() const {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->lru.size();
}

std::size_t PreviewCache::capacity() const { return impl_->capacity; }

int PreviewCache::max_px() const { return impl_->max_px; }

std::size_t PreviewCache::hits() const {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->hits;
}

std::size_t PreviewCache::misses() const {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->misses;
}

std::size_t PreviewCache::evictions() const {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->evictions;
}

std::size_t PreviewCache::memory_bytes() const {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->bytes;
}

void PreviewCache::clear() {
    const std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->lru.clear();
    impl_->index.clear();
    impl_->bytes = 0; // 命中/未命中/逐出计数保留（诊断口径 = 生命周期累计）
}

} // namespace pp
