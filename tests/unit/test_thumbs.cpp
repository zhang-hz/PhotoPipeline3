// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1b-U1 — probe + thumbnail unit tests (hand-written assertions).
//
// Contract: docs/m1b-tasks.md §2.1 (PP-FROZEN header) / §4.1:
//   * base/rgb8.png @96: rgba size, long edge <= target, from_embedded == false, no upscale;
//   * base/rgb16.tif: probe + thumbnail from the full-decode path;
//   * edge/ corrupt negatives: probe_ok == false with a non-empty error;
//   * meta/exif_full.jpg: probe_ok == true, thumbnail non-empty (from_embedded not asserted);
// plus the frozen §2.1 step-5 behaviours (no upscale, gray -> RGB, alpha -> white) and one
// synthetic fixture whose Exif IFD1 thumbnail exercises the embedded-preview path on real
// bytes (the shipped corpus contains no file with an embedded preview).
// Every failure prints "FAIL <case>: <detail>"; main() returns the failure count.
//
// M4-W2-T10（0.3.0）追加段：输入预览通道（§3.6 冻结形态 `decode_preview` + §6.1 的 LRU 缓存）
//   * 内嵌预览优先 + 「长边 ≥ max_px → 精确降采样」+「短于 max_px → 原生尺寸不放大」；
//   * LANCZOS3 降采样正确性（合成渐变 fixture：尺寸精确、边缘像素落在渐变端点、无 letterbox）；
//   * 全解码分支（无内嵌预览）的同一组断言；
//   * LRU 逐出（容量 3 的定向用例 + §6.1 默认 16 张上限的 17 文件用例）；
//   * 48MP（8000×6000）三档实测耗时（目标 250ms）：**只打印实际值与判定，不作硬断言**
//     （任务书口径：不达报实际值并说明）；硬断言的只有"出图/尺寸/像素正确"。
//   * 通道规则单一实现：decode_preview 与 make_thumbnail 在不缩放的同一路径上逐像素相等。
//
// fixture 口径（实测记录）：大内嵌预览（2600×2000，170KB）**必须挂 TIFF 宿主**——Exiv2 的
// JPEG/PNG 宿主走 EXIF APP1 的 64KB 上限（实测 attach 后 PreviewManager 报 previews=0），
// TIFF 宿主无此限制（实测 previews=1 [2600x2000 image/jpeg]）。

#include <exiv2/exiv2.hpp>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>
#include <OpenImageIO/imageio.h>

#include <QImage>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "core/thumbs.h"
#include "decode/oiio_reader.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string show(const fs::path &p) { return p.string(); }

std::string dims(const pp::ThumbImage &t) {
    return std::to_string(t.width) + "x" + std::to_string(t.height) +
           " rgba=" + std::to_string(t.rgba.size());
}

std::string dims(const QImage &img) {
    if (img.isNull())
        return "null";
    return std::to_string(img.width()) + "x" + std::to_string(img.height());
}

long long elapsed_ms(const std::chrono::steady_clock::time_point &t0) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 t0)
        .count();
}

// Repository corpus: walk up from the ctest working directory to find tests/golden.
fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec)) {
            return p / "tests" / "golden";
        }
        if (!p.has_parent_path() || p.parent_path() == p) {
            break;
        }
        p = p.parent_path();
    }
    return {};
}

// Scratch under the build dir (ctest runs in the build dir, like test_fsops).
fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

bool rgba_size_matches(const pp::ThumbImage &t) {
    return t.width > 0 && t.height > 0 &&
           t.rgba.size() ==
               static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4;
}

bool alpha_all_opaque(const pp::ThumbImage &t) {
    for (std::size_t i = 3; i < t.rgba.size(); i += 4) {
        if (t.rgba[i] != 255)
            return false;
    }
    return true;
}

bool gray_is_neutral(const pp::ThumbImage &t) {
    for (std::size_t i = 0; i + 3 < t.rgba.size(); i += 4) {
        if (t.rgba[i] != t.rgba[i + 1] || t.rgba[i] != t.rgba[i + 2])
            return false;
    }
    return true;
}

bool all_white(const pp::ThumbImage &t) {
    for (std::uint8_t v : t.rgba) {
        if (v != 255)
            return false;
    }
    return true;
}

// Attach tests/golden/base/photo.jpg as the IFD1 Exif thumbnail of a copy of
// meta/exif_full.jpg, so make_thumbnail() meets a real embedded preview.
fs::path build_preview_fixture(const fs::path &corpus, const fs::path &dir) {
    std::error_code ec;
    const fs::path host = dir / "host_with_preview.jpg";
    fs::copy_file(corpus / "meta" / "exif_full.jpg", host, fs::copy_options::overwrite_existing,
                  ec);
    if (ec)
        return {};
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(host.string());
        if (!image)
            return {};
        image->readMetadata();
        Exiv2::ExifData exif = image->exifData();
        Exiv2::ExifThumb thumb(exif);
        thumb.setJpegThumbnail((corpus / "base" / "photo.jpg").string(), Exiv2::URational(72, 1),
                               Exiv2::URational(72, 1), 2);
        image->setExifData(exif);
        image->writeMetadata();
    } catch (const Exiv2::Error &e) {
        std::printf("FAIL preview/fixture: exiv2: %s\n", e.what());
        ++g_failed;
        return {};
    } catch (...) {
        std::printf("FAIL preview/fixture: unknown exiv2 error\n");
        ++g_failed;
        return {};
    }
    return host;
}

// ------------------------------------------------------------------------------------------
// M4-W2-T10：预览通道 fixture 与取样工具
// ------------------------------------------------------------------------------------------

// 合成渐变（M4-W2-T10 预览用例的唯一几何来源）：
//   R = 30 + 200*x/(w-1)（水平斜坡）、G = 40 + 100*y/(h-1)（垂直斜坡）、B = 128。
// 两端点远离 0/255 ⇒ 任何 letterbox 黑边/白边都会在端点断言上立刻暴露。
std::vector<std::uint8_t> gradient_pixels(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t *p =
                px.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                             static_cast<std::size_t>(x)) *
                                3u;
            p[0] = static_cast<std::uint8_t>(30 + (200 * x) / (w - 1));
            p[1] = static_cast<std::uint8_t>(40 + (100 * y) / (h - 1));
            p[2] = 128;
        }
    }
    return px;
}

// 写渐变 JPEG（内嵌预览源；quality 交给 OIIO 默认档）或 zip 压缩 TIFF（大图宿主）。
bool write_gradient_file(const fs::path &path, int w, int h, const char *format) {
    auto out = OIIO::ImageOutput::create(path.string());
    if (!out)
        return false;
    OIIO::ImageSpec spec(w, h, 3, OIIO::TypeUInt8);
    if (std::string(format) == "tif")
        spec.attribute("compression", "zip:1"); // 48MP 未压缩 = 288MB；zip:1 实测 ~115ms/1.7MB
    if (!out->open(path.string(), spec))
        return false;
    const std::vector<std::uint8_t> px = gradient_pixels(w, h);
    const bool ok = out->write_image(OIIO::TypeUInt8, px.data());
    out->close();
    return ok;
}

// 把一段 JPEG 挂成 host 的 IFD1 内嵌预览（Exiv2 重写文件，主图像数据不重编码）。
fs::path attach_embedded_preview(const fs::path &host_src, const fs::path &preview_jpeg,
                                 const fs::path &dir, const char *name) {
    std::error_code ec;
    const fs::path host = dir / name;
    fs::copy_file(host_src, host, fs::copy_options::overwrite_existing, ec);
    if (ec)
        return {};
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(host.string());
        if (!image)
            return {};
        image->readMetadata();
        Exiv2::ExifData exif = image->exifData();
        Exiv2::ExifThumb thumb(exif);
        thumb.setJpegThumbnail(preview_jpeg.string(), Exiv2::URational(72, 1),
                               Exiv2::URational(72, 1), 2);
        image->setExifData(exif);
        image->writeMetadata();
    } catch (const Exiv2::Error &e) {
        std::printf("FAIL preview/attach: exiv2: %s\n", e.what());
        ++g_failed;
        return {};
    } catch (...) {
        std::printf("FAIL preview/attach: unknown exiv2 error\n");
        ++g_failed;
        return {};
    }
    return host;
}

// fixture 自校验：宿主文件里 Exiv2 能看到的**最大内嵌预览长边**（0 = 没挂上）。
// 挂载失败必须当场发现（否则用例会静默退化成"全解码分支"，断言全落空）。
int embedded_preview_long_edge(const fs::path &p) {
    try {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(p.string());
        if (!image)
            return 0;
        image->readMetadata();
        const Exiv2::PreviewManager manager(*image);
        int best = 0;
        for (const Exiv2::PreviewProperties &props : manager.getPreviewProperties())
            best = std::max(best, static_cast<int>(std::max(props.width_, props.height_)));
        return best;
    } catch (...) {
        return 0;
    }
}

// 渐变取样断言：水平中点行的左右端点（R 通道）+ 左上/左下（G 通道，垂直边排除）。
// tol 为 JPEG 压缩 + 降采样滤波容差。
bool gradient_endpoints_ok(const QImage &img, int tol, std::string *detail) {
    if (img.isNull()) {
        *detail = "image is null";
        return false;
    }
    const int w = img.width();
    const int h = img.height();
    const int r_left = img.pixelColor(0, h / 2).red();
    const int r_right = img.pixelColor(w - 1, h / 2).red();
    const int g_top = img.pixelColor(0, 0).green();
    const int g_bottom = img.pixelColor(0, h - 1).green();
    *detail = "R[0]=" + std::to_string(r_left) + " (exp 30±" + std::to_string(tol) +
              ") R[last]=" + std::to_string(r_right) + " (exp 230±" + std::to_string(tol) +
              ") G[top]=" + std::to_string(g_top) + " (exp 40±" + std::to_string(tol) +
              ") G[bottom]=" + std::to_string(g_bottom) + " (exp 140±" + std::to_string(tol) + ")";
    return std::abs(r_left - 30) <= tol && std::abs(r_right - 230) <= tol &&
           std::abs(g_top - 40) <= tol && std::abs(g_bottom - 140) <= tol;
}

constexpr int kBigWidth = 8000; // 48MP（8000×6000）
constexpr int kBigHeight = 6000;

} // namespace

int main() {
    const fs::path corpus = find_corpus();
    check(!corpus.empty(), "corpus/found",
          "tests/golden not found walking up from " + show(fs::current_path()));
    if (corpus.empty()) {
        std::printf("test_thumbs: FAILED (%d)\n", g_failed);
        return g_failed;
    }

    // ---- base/rgb8.png @96: probe ok, no upscale (64x64 source), RGBA size, no preview ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb8.png", 96);
        check(o.probe_ok, "rgb8/probe-ok", "error=" + o.error);
        check(o.error.empty(), "rgb8/probe-no-error", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "rgb8/thumb-non-empty", dims(o.thumb));
        check(o.thumb.width == 64 && o.thumb.height == 64, "rgb8/no-upscale-64x64",
              "expect 64x64, got " + dims(o.thumb));
        check(o.thumb.width <= 96 && o.thumb.height <= 96, "rgb8/long-edge-le-target",
              dims(o.thumb));
        check(rgba_size_matches(o.thumb), "rgb8/rgba-size", dims(o.thumb));
        check(alpha_all_opaque(o.thumb), "rgb8/alpha-composited-opaque", dims(o.thumb));
        check(!o.thumb.from_embedded, "rgb8/from-embedded-false", "expected full decode");
        check(o.info.width == 64 && o.info.height == 64 && o.info.channels == 3 &&
                  o.info.format == "png",
              "rgb8/probe-info",
              "w=" + std::to_string(o.info.width) + " h=" + std::to_string(o.info.height) +
                  " ch=" + std::to_string(o.info.channels) + " fmt=" + o.info.format);
    }

    // ---- base/rgb8.png @32: downscale honours the target ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb8.png", 32);
        check(o.probe_ok && o.thumb.width == 32 && o.thumb.height == 32 &&
                  rgba_size_matches(o.thumb),
              "rgb8/downscale-32x32", "expect 32x32, got " + dims(o.thumb) + " err=" + o.error);
    }

    // ---- base/rgb16.tif @96: 16-bit source, no upscale ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgb16.tif", 96);
        check(o.probe_ok, "rgb16/probe-ok", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "rgb16/thumb-non-empty", dims(o.thumb));
        check(o.thumb.width == 64 && o.thumb.height == 64, "rgb16/no-upscale-64x64",
              "expect 64x64, got " + dims(o.thumb));
        check(rgba_size_matches(o.thumb), "rgb16/rgba-size", dims(o.thumb));
        check(o.info.src_bitdepth == 16, "rgb16/probe-bitdepth",
              "src_bitdepth=" + std::to_string(o.info.src_bitdepth));
    }

    // ---- gray -> RGB (§2.1 step 5) ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/gray8.png", 96);
        check(o.probe_ok && rgba_size_matches(o.thumb), "gray8/thumb-shape",
              dims(o.thumb) + " err=" + o.error);
        check(gray_is_neutral(o.thumb), "gray8/gray-to-rgb", "found a non-neutral pixel");
    }

    // ---- alpha -> white composite (§2.1 step 5) ----
    // base/rgba8.png is a checkerboard of fully transparent black and opaque white cells;
    // compositing on white must yield an all-white thumbnail.
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "base/rgba8.png", 96);
        check(o.probe_ok && rgba_size_matches(o.thumb), "rgba8/thumb-shape",
              dims(o.thumb) + " err=" + o.error);
        check(o.info.has_alpha, "rgba8/probe-alpha", "has_alpha=false");
        check(all_white(o.thumb), "rgba8/alpha-on-white", "found a non-white pixel");
    }

    // ---- edge/ corrupt negatives: probe fails, no thumbnail, error reported ----
    {
        const char *const bad[] = {"edge/corrupt_trunc.jpg", "edge/corrupt_zero.png"};
        for (const char *rel : bad) {
            const pp::ThumbOutcome o = pp::make_thumbnail(corpus / rel, 96);
            check(!o.probe_ok, std::string("corrupt/") + rel + "/probe-fails",
                  "probe_ok=true for " + std::string(rel));
            check(!o.error.empty(), std::string("corrupt/") + rel + "/error-set",
                  "error was empty for " + std::string(rel));
            check(o.thumb.rgba.empty(), std::string("corrupt/") + rel + "/no-thumb", dims(o.thumb));
        }
    }

    // ---- non-ASCII path survives the Exiv2/OIIO round trip ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "edge" / "测试📸unicode.png", 96);
        check(o.probe_ok && !o.thumb.rgba.empty(), "unicode/probe-and-thumb",
              "err=" + o.error + " " + dims(o.thumb));
    }

    // ---- meta/exif_full.jpg: probe ok; thumbnail required, from_embedded not asserted ----
    {
        const pp::ThumbOutcome o = pp::make_thumbnail(corpus / "meta/exif_full.jpg", 96);
        check(o.probe_ok, "exif-jpg/probe-ok", "error=" + o.error);
        check(!o.thumb.rgba.empty(), "exif-jpg/thumb-non-empty", dims(o.thumb));
        check(rgba_size_matches(o.thumb) && o.thumb.width <= 96 && o.thumb.height <= 96,
              "exif-jpg/thumb-shape",
              dims(o.thumb) + " from_embedded=" + (o.thumb.from_embedded ? "true" : "false"));
    }

    // ---- embedded preview path (synthetic Exif IFD1 thumbnail) ----
    {
        const fs::path dir = make_temp_dir("thumbs_preview");
        const fs::path host = build_preview_fixture(corpus, dir);
        if (!host.empty()) {
            const pp::ThumbOutcome o = pp::make_thumbnail(host, 96);
            check(o.probe_ok, "preview/probe-ok", "error=" + o.error);
            check(o.thumb.from_embedded, "preview/from-embedded-true",
                  "expected the Exif IFD1 thumbnail to be used, got " + dims(o.thumb));
            check(o.thumb.width == 64 && o.thumb.height == 64 && rgba_size_matches(o.thumb),
                  "preview/thumb-64x64", "expect 64x64, got " + dims(o.thumb));
            // Downscaling the embedded preview must respect the target too.
            const pp::ThumbOutcome small = pp::make_thumbnail(host, 24);
            check(small.thumb.from_embedded && small.thumb.width == 24 && small.thumb.height == 24,
                  "preview/downscale-24x24", "got " + dims(small.thumb));
        }
    }

    // ======================================================================================
    // M4-W2-T10：输入预览通道（§3.6 `decode_preview` + §6.1 LRU）
    // ======================================================================================

    // ---- (0) fixture：渐变源（内嵌预览 2600×2000 / 1600×1200；48MP 主图 jpg/tif）----
    const fs::path tmp = make_temp_dir("thumbs_preview_03");
    const fs::path grad_2600 = tmp / "gradient_2600x2000.jpg";
    const fs::path grad_1600 = tmp / "gradient_1600x1200.jpg";
    const fs::path grad_48mp = tmp / "gradient_8000x6000.jpg";
    const fs::path tif_48mp = tmp / "gradient_8000x6000.tif";
    check(write_gradient_file(grad_2600, 2600, 2000, "jpg"), "preview03/fixture-preview-2600",
          "write failed: " + show(grad_2600));
    check(write_gradient_file(grad_1600, 1600, 1200, "jpg"), "preview03/fixture-preview-1600",
          "write failed: " + show(grad_1600));
    check(write_gradient_file(grad_48mp, kBigWidth, kBigHeight, "jpg"),
          "preview03/fixture-48mp-jpg", "write failed: " + show(grad_48mp));
    check(write_gradient_file(tif_48mp, kBigWidth, kBigHeight, "tif"), "preview03/fixture-48mp-tif",
          "write failed: " + show(tif_48mp));

    // 宿主：小 TIFF + 2600px 预览（内嵌优先/精确降采样）；48MP TIFF + 2600px / 1600px 预览（计时）
    const fs::path host_big = attach_embedded_preview(corpus / "base/rgb8.tif", grad_2600, tmp,
                                                      "host_small_big_preview.tif");
    const fs::path host48_big =
        attach_embedded_preview(tif_48mp, grad_2600, tmp, "host48_big_preview.tif");
    const fs::path host48_native =
        attach_embedded_preview(tif_48mp, grad_1600, tmp, "host48_native_preview.tif");
    // fixture 自校验：挂载必须真的成功（否则下面的"内嵌优先"断言会静默落空）
    check(embedded_preview_long_edge(host_big) == 2600, "preview03/fixture-attach-2600",
          "embedded preview long edge = " + std::to_string(embedded_preview_long_edge(host_big)) +
              " (expect 2600)");
    check(embedded_preview_long_edge(host48_big) == 2600, "preview03/fixture-attach-48mp-2600",
          "embedded preview long edge = " + std::to_string(embedded_preview_long_edge(host48_big)));
    check(embedded_preview_long_edge(host48_native) == 1600, "preview03/fixture-attach-48mp-1600",
          "embedded preview long edge = " +
              std::to_string(embedded_preview_long_edge(host48_native)));

    // ---- (1) 基础：无内嵌预览的 PNG → 全解码分支、不放大、probe 摘要与色彩空间 ----
    {
        const pp::PreviewImage p = pp::decode_preview_detailed(corpus / "base/rgb8.png", 2048);
        check(!p.image.isNull(), "preview03/rgb8-image", "error=" + p.error);
        check(p.error.empty(), "preview03/rgb8-no-error", "error=" + p.error);
        check(!p.from_embedded, "preview03/rgb8-full-decode",
              "from_embedded=true (no preview exists)");
        check(p.image.width() == 64 && p.image.height() == 64, "preview03/rgb8-no-upscale",
              "expect 64x64, got " + dims(p.image));
        check(p.image.format() == QImage::Format_RGBA8888, "preview03/rgb8-format",
              "format=" + std::to_string(static_cast<int>(p.image.format())));
        check(p.image.pixelColor(0, 0).alpha() == 255, "preview03/rgb8-alpha-opaque",
              "alpha != 255");
        check(p.info.width == 64 && p.info.height == 64 && p.info.channels == 3 &&
                  p.info.format == "png",
              "preview03/rgb8-info",
              "w=" + std::to_string(p.info.width) + " h=" + std::to_string(p.info.height) +
                  " ch=" + std::to_string(p.info.channels) + " fmt=" + p.info.format);
        check(p.color_space == "sRGB 假定", "preview03/rgb8-color-space",
              "expect 'sRGB 假定' (no ICC), got '" + p.color_space + "'");
    }

    // ---- (1b) 通道规则单一实现：不缩放的同一路径上，preview 与 thumbnail 逐像素相等 ----
    {
        const pp::ThumbOutcome t = pp::make_thumbnail(corpus / "base/rgb8.png", 96);
        const QImage p = pp::decode_preview(corpus / "base/rgb8.png", 2048);
        bool same = !p.isNull() && t.thumb.width == p.width() && t.thumb.height == p.height();
        std::size_t diff = 0;
        if (same) {
            for (int y = 0; y < p.height() && same; ++y) {
                for (int x = 0; x < p.width(); ++x) {
                    const std::uint8_t *q =
                        t.thumb.rgba.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width()) +
                         static_cast<std::size_t>(x)) *
                            4u;
                    const QColor c = p.pixelColor(x, y);
                    if (q[0] != c.red() || q[1] != c.green() || q[2] != c.blue()) {
                        same = false;
                        ++diff;
                        break;
                    }
                }
            }
        }
        check(same, "preview03/channel-rule-single-impl",
              "preview/thumbnail pixels differ (diff=" + std::to_string(diff) + ") " + dims(p));
    }

    // ---- (2) 内嵌预览优先 + LANCZOS3 降采样正确性（2600px 预览 → max_px 2048 / 64 / 门槛）----
    {
        const pp::PreviewImage big = pp::decode_preview_detailed(host_big, 2048);
        check(!big.image.isNull() && big.error.empty(), "preview03/embedded-image",
              "error=" + big.error);
        check(big.from_embedded, "preview03/embedded-preferred",
              "expected the 2600px embedded preview to win, got from_embedded=false");
        check(big.image.width() == 2048 && big.image.height() == 1575,
              "preview03/embedded-downscale-exact",
              "expect 2048x1575 (2600x2000 * 2048/2600), got " + dims(big.image));
        std::string detail;
        check(gradient_endpoints_ok(big.image, 12, &detail), "preview03/embedded-downscale-pixels",
              detail);
        // info = **源文件**的 probe 摘要（面板显示的是文件尺寸/位深，不是内嵌预览的尺寸）
        check(big.info.width == 64 && big.info.height == 64, "preview03/embedded-info-host",
              "host probe info: " + std::to_string(big.info.width) + "x" +
                  std::to_string(big.info.height) + " (expect the host's own 64x64)");

        // 短于 max_px 的请求：按原生尺寸呈现（不放大）
        const pp::PreviewImage small = pp::decode_preview_detailed(host_big, 64);
        check(small.from_embedded && small.image.width() == 64 && small.image.height() == 49,
              "preview03/embedded-64-native",
              "expect 64x49 (native, no upscale), got " + dims(small.image));
        std::string detail64;
        check(gradient_endpoints_ok(small.image, 12, &detail64), "preview03/embedded-64-pixels",
              detail64);

        // 门槛行为：长边恰 64px 的 IFD1 预览仍在门槛内（≥64 → 用；不放大 → 64px）
        const fs::path dir = make_temp_dir("thumbs_preview_threshold");
        const fs::path host64 = build_preview_fixture(corpus, dir);
        if (!host64.empty()) {
            const pp::PreviewImage p64 = pp::decode_preview_detailed(host64, 2048);
            check(p64.from_embedded && p64.image.width() == 64, "preview03/embedded-min-64px",
                  "长边 64px 的预览应在门槛内（不放大 → 64px），got " + dims(p64.image) +
                      " from_embedded=" + (p64.from_embedded ? "1" : "0"));
        }
    }

    // ---- (3) 全解码分支：48MP（无内嵌预览）→ LANCZOS3 到 2048 内 + 像素正确 ----
    {
        const pp::PreviewImage p = pp::decode_preview_detailed(grad_48mp, 2048);
        check(!p.image.isNull() && p.error.empty(), "preview03/48mp-image", "error=" + p.error);
        check(!p.from_embedded, "preview03/48mp-full-decode", "from_embedded=true");
        check(p.image.width() == 2048 && p.image.height() == 1536, "preview03/48mp-downscale",
              "expect 2048x1536 (8000x6000 * 0.256), got " + dims(p.image));
        std::string detail;
        check(gradient_endpoints_ok(p.image, 12, &detail), "preview03/48mp-downscale-pixels",
              detail);
        check(p.info.width == kBigWidth && p.info.height == kBigHeight, "preview03/48mp-info",
              "w=" + std::to_string(p.info.width) + " h=" + std::to_string(p.info.height));
    }

    // ---- (4) 失败面：损坏文件 → null QImage + 非空 error（不抛异常）----
    {
        const pp::PreviewImage p =
            pp::decode_preview_detailed(corpus / "edge/corrupt_zero.png", 2048);
        check(p.image.isNull(), "preview03/corrupt-image-null", "expected null image");
        check(!p.error.empty(), "preview03/corrupt-error-set", "error was empty");
        check(pp::decode_preview(corpus / "edge/corrupt_zero.png", 2048).isNull(),
              "preview03/corrupt-decode_preview-null", "expected null QImage from §3.6 冻结形态");
    }

    // ---- (5) LRU 逐出（定向：容量 3；顺序 = A,B,C → 命中 A → D 逐出 B → 取 B 逐出 C）----
    {
        const fs::path a = corpus / "base/rgb8.png";
        const fs::path b = corpus / "base/rgb16.png";
        const fs::path c = corpus / "base/gray8.png";
        const fs::path d = corpus / "base/rgba8.png";
        pp::PreviewCache cache(3, 96);
        check(cache.capacity() == 3 && cache.max_px() == 96, "lru/config",
              "cap=" + std::to_string(cache.capacity()) +
                  " max_px=" + std::to_string(cache.max_px()));
        const pp::PreviewImage ia = cache.get(a);
        const pp::PreviewImage ib = cache.get(b);
        const pp::PreviewImage ic = cache.get(c);
        check(!ia.image.isNull() && !ib.image.isNull() && !ic.image.isNull(), "lru/fill",
              dims(ia.image) + " " + dims(ib.image) + " " + dims(ic.image));
        check(cache.size() == 3 && cache.misses() == 3 && cache.hits() == 0 &&
                  cache.evictions() == 0,
              "lru/fill-counters",
              "size=" + std::to_string(cache.size()) + " hits=" + std::to_string(cache.hits()) +
                  " misses=" + std::to_string(cache.misses()) +
                  " evictions=" + std::to_string(cache.evictions()));
        check(cache.memory_bytes() > 0, "lru/memory-bytes",
              "bytes=" + std::to_string(cache.memory_bytes()));

        cache.get(a); // 命中 → A 变 MRU（此时 LRU 序：A,C,B）
        check(cache.hits() == 1 && cache.misses() == 3 && cache.size() == 3, "lru/hit-promotes",
              "hits=" + std::to_string(cache.hits()) + " misses=" + std::to_string(cache.misses()));

        cache.get(d); // 插入 D → 逐出最旧（B）
        check(cache.size() == 3 && cache.evictions() == 1 && cache.misses() == 4,
              "lru/evict-oldest",
              "size=" + std::to_string(cache.size()) + " misses=" + std::to_string(cache.misses()) +
                  " evictions=" + std::to_string(cache.evictions()));

        cache.get(b); // B 已被逐出 → miss，插入后逐出最旧（C）
        check(cache.misses() == 5 && cache.evictions() == 2 && cache.hits() == 1,
              "lru/evicted-is-miss",
              "hits=" + std::to_string(cache.hits()) + " misses=" + std::to_string(cache.misses()) +
                  " evictions=" + std::to_string(cache.evictions()));
        cache.get(a); // 仍在（被提升过）→ 命中
        cache.get(d); // 仍在 → 命中
        check(cache.hits() == 3 && cache.size() == 3, "lru/retained-hot-entries",
              "hits=" + std::to_string(cache.hits()) + " size=" + std::to_string(cache.size()));
    }

    // ---- (5b) §6.1 默认上限：16 张 / 2048px（17 个文件 → 恰好逐出 1 个最旧）----
    {
        pp::PreviewCache cache; // 默认 = kPreviewCacheCapacity(16) / kPreviewCacheMaxPx(2048)
        check(cache.capacity() == pp::kPreviewCacheCapacity &&
                  cache.max_px() == pp::kPreviewCacheMaxPx,
              "lru/defaults",
              "cap=" + std::to_string(cache.capacity()) +
                  " max_px=" + std::to_string(cache.max_px()));

        std::vector<fs::path> files;
        for (const char *name :
             {"anim.gif", "bmp24.bmp", "gray16.png", "gray16.tif", "gray8.png", "graya8.png",
              "jxl8.jxl", "multi.tif", "photo.jpg", "rgb16.png", "rgb16.tif", "rgb8.png",
              "rgb8.tif", "rgba16.png", "rgba8.png", "targa.tga"}) {
            files.emplace_back(corpus / "base" / name);
        }
        files.emplace_back(corpus / "edge" / "测试📸unicode.png");
        check(files.size() == 17, "lru/17-inputs",
              "file list size = " + std::to_string(files.size()));

        std::size_t loaded = 0;
        for (const fs::path &f : files) {
            if (!cache.get(f).image.isNull())
                ++loaded;
        }
        check(loaded == 17, "lru/17-loaded", "loaded=" + std::to_string(loaded));
        check(cache.size() == 16, "lru/16-capacity", "size=" + std::to_string(cache.size()));
        check(cache.evictions() == 1, "lru/17-evictions",
              "evictions=" + std::to_string(cache.evictions()));
        check(!cache.get(files.front()).image.isNull() && cache.misses() == 18,
              "lru/first-evicted-is-miss",
              "misses=" + std::to_string(cache.misses()) + " (17 fills + 1 evicted re-get)");
        std::printf("test_thumbs: preview LRU cache: size=%zu cap=%zu hits=%zu misses=%zu "
                    "evictions=%zu memory=%.1f MB (max_px=%d)\n",
                    cache.size(), cache.capacity(), cache.hits(), cache.misses(), cache.evictions(),
                    double(cache.memory_bytes()) / (1024.0 * 1024.0), cache.max_px());
    }

    // ---- (6) 48MP 实测耗时（目标 250ms；只打印实际值与判定，不作硬断言）----
    // 三档取证（同一台机器、同一进程、同一 max_px=2048；页面缓存对三档同样有利）：
    //   ① 48MP + 1600px 内嵌预览（相机常见档）→ 原生尺寸呈现，无重采样
    //   ② 48MP + 2600px 内嵌预览 → LANCZOS3 0.79 倍降采样
    //   ③ 48MP 无内嵌预览 → 全解码（OIIO 读整图）+ LANCZOS3 0.256 倍降采样
    {
        const auto timed = [](const char *label, const fs::path &p, int max_px) {
            const auto t0 = std::chrono::steady_clock::now();
            const pp::PreviewImage r = pp::decode_preview_detailed(p, max_px);
            const long long ms = elapsed_ms(t0);
            std::printf("test_thumbs: preview-48mp %-34s: %5lld ms  size=%-10s from_embedded=%d "
                        "verdict=%s\n",
                        label, ms, dims(r.image).c_str(), r.from_embedded ? 1 : 0,
                        ms <= 250 ? "MET(<=250)" : "MISSED(>250)");
            std::fflush(stdout);
            check(!r.image.isNull(), std::string("preview03/48mp-image-") + label,
                  "error=" + r.error + " path=" + show(p));
            return ms;
        };
        const long long ms_native = timed("embedded-1600px (no resample)", host48_native, 2048);
        const long long ms_embedded = timed("embedded-2600px (lanczos3 0.79x)", host48_big, 2048);
        const long long ms_full =
            timed("no-preview (full decode + lanczos3 0.256x)", grad_48mp, 2048);

        // 分段归因（解释"为什么"）：全解码分支 = 解码 + 重采样两段
        const auto t0 = std::chrono::steady_clock::now();
        const pp::ProbeOutcome probe = pp::probe_file(grad_48mp);
        const long long probe_ms = elapsed_ms(t0);
        const auto t1 = std::chrono::steady_clock::now();
        const pp::DecodeOutcome dec = pp::decode_float(grad_48mp, probe.info);
        const long long decode_ms = elapsed_ms(t1);
        const bool dec_ok = dec.error.empty() && dec.buf.initialized();
        std::printf(
            "test_thumbs: preview-48mp breakdown: probe=%lld ms, decode_float(48MP)=%lld ms "
            "(ok=%d), lanczos3(48MP->2048, nthreads=1, float32)=%lld ms, "
            "embedded1600=%lld ms, embedded2600=%lld ms; target=250 ms\n",
            probe_ms, decode_ms, dec_ok ? 1 : 0, ms_full > decode_ms ? ms_full - decode_ms : -1,
            ms_native, ms_embedded);
        std::fflush(stdout);
    }

    if (g_failed == 0) {
        std::printf("test_thumbs: OK\n");
        return 0;
    }
    std::printf("test_thumbs: FAILED (%d)\n", g_failed);
    return g_failed;
}
