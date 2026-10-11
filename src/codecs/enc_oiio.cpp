// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T7: OpenImageIO encoders (PNG / TIFF / BMP).
//
// Mapping (docs/m1-tasks.md §3.8 + §7; re-verified against the installed OIIO 3.1.17,
// vcpkg_installed/x64-windows-avx2/include/OpenImageIO/oiioversion.h:45-47 —— R-010/R-011
// 复核后订正：旧注释自述 verified 3.1.14，且两个属性键名与上游消费点不符/未核对):
//   PNG  : compressionLevel -> ImageSpec attribute "png:compressionLevel" (clamped 0..9)
//          （pngoutput.cpp:165-169 唯一读取点，裸键 "compressionLevel" 全库无人消费 —— R-010）
//   TIFF : compression -> ImageSpec attribute "compression" (name table:
//          none/lzw/zip/ccittrle/packbits; an unknown name is an ERROR here, OIIO would
//          silently fall back to deflate), deflate_level -> "tiff:zipquality",
//          predictor -> "tiff:Predictor"（tiffoutput.cpp:1026-1029 put_parameter 逐字键名，
//          大小写不敏感匹配；在 open() 的自动 predictor 规则（tiffoutput.cpp:685-701）之后
//          处理（extra_attribs 循环 tiffoutput.cpp:936-937），故用户值**覆盖**自动选择
//          —— R-011 复核结论：控制面真实存在，接线保留并补字节差测试），
//          tiling -> spec.tile_width/height
//          (both > 0 and multiples of 16, otherwise ERROR)
//   BMP  : no parameters
//   ICC  : ImageSpec attribute "ICCProfile" (uint8 array); EXIF/XMP are NOT written here
//          (E7: those go through the Exiv2 post-write path of §3.7).
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoder_registry.h"
#include "codecs/encoders.h"
#include "core/fsops.h" // R-003：atomic_temp_path / commit / discard
#include "core/logger.h"
#include "core/params.h"
#include "core/simd/simd.h" // M4-W5-T19：量化热路径（§11.2）

namespace pp {
namespace {

constexpr std::string_view kStage = "encode";
constexpr std::string_view kFile = "enc_oiio.cpp";

double ms_since(const std::chrono::steady_clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// Log-only rendering of a parameter value (run-log fields for E9 notes).
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

// OIIO 3.1.14 TIFF writer name table (tiffoutput.cpp:301-338); "deflate" is only an alias
// accepted by ImageSpec::decode_compression_metadata, unknown names silently fall back to
// deflate -> we validate up front (task book: no silent fallback).
bool tiff_compression_known(std::string_view name) {
    static constexpr std::string_view kNames[] = {"none", "lzw", "zip", "ccittrle", "packbits"};
    return std::any_of(std::begin(kNames), std::end(kNames),
                       [name](std::string_view n) { return n == name; });
}

// W1-T6（§3.1/§7.2）：OIIO 写进度中继。
//   * OIIO 的 `ProgressCallback` 是 C 函数指针（`bool(void*, float)`），故用薄中继结构把
//     `EncodeRequest::progress`（std::function）接进去；
//   * 返回值恒 false = **永不中止写盘**（§8.3：取消语义只在阶段边界检查，编码内不中断）；
//   * `called_with_progress()`：只有在收到 portion > 0 的回调后才算"编码器报了真实进度"
//     （OIIO 起手会发一个 0.0 采样；完全不发回调的插件 —— 支持 "rectangles" 的那些 —— 如实
//     记为未报，§3.1 的 progress_reported=false）。
struct OiioProgressRelay {
    const ProgressFn *progress = nullptr;
    bool called = false; // 收到过回调（含 0.0）
    bool moved = false;  // 收到过 portion > 0 的回调
    bool called_with_progress() const { return moved; }
};

bool oiio_progress_trampoline(void *opaque, float portion_done) {
    auto *relay = static_cast<OiioProgressRelay *>(opaque);
    if (relay == nullptr)
        return false;
    relay->called = true;
    if (portion_done > 0.0f)
        relay->moved = true;
    if (relay->progress != nullptr && *relay->progress) {
        try {
            (*relay->progress)(portion_done);
        } catch (...) {
            // 进度回调不参与编码成败（§3.1 "尽力而为"）：吞掉并继续写盘。
            // R-301：`warned` 用 atomic —— 多 worker 并发写盘时本中继可被并行触发，
            // 普通 bool 的 read-then-write 是数据竞争（TSan 可见）；atomic_exchange
            // 单点判定"只告警一次"，语义不变。
            static std::atomic<bool> warned{false};
            if (!warned.exchange(true, std::memory_order_relaxed)) {
                log_warn(kStage, kFile, "progress callback threw; ignored", {{"source", "oiio"}});
            }
        }
    }
    return false;
}

// M4-W5-T19 接线（design §11.2 热路径表第 2 行）：量化改由 **pp::simd::quantize8 /
// quantize16**（唯一运行期分派层）承载 —— AVX2 8 样本/迭代（`_mm256_cvtps_epi32` + 饱和
// 打包）。原 `quantize` 标量表达式（double `lround(clamp(x,0,1)·maxv)`）随之删除，其口径
// 与 simd 契约的关系见下方像素段的接线注释与 core/simd/simd.h 的 quantize 契约块。

// Parameter keys declared by the static format table for (format, backend) -> E9 detection.
std::vector<std::string> known_param_keys(std::string_view format_id, std::string_view backend_id) {
    std::vector<std::string> keys;
    const FormatDef *f = find_format(format_id);
    if (!f)
        return keys;
    const BackendDef *b = find_backend(*f, backend_id);
    if (!b)
        return keys;
    const TechDef *t = find_tech(*b, "");
    if (!t)
        return keys;
    for (const ParamDef &p : t->params)
        keys.push_back(p.key);
    return keys;
}

class OiioEncoder final : public IEncoder {
public:
    OiioEncoder(std::string format_id, std::string backend_id)
        : format_id_(std::move(format_id)), backend_id_(std::move(backend_id)) {}

    const FormatDef &format() const override {
        const FormatDef *f = find_format(format_id_);
        assert(f != nullptr);
        static const FormatDef kFallback{};
        return f ? *f : kFallback;
    }

    EncodeResult encode(const EncodeRequest &req) override;

private:
    // E1: immutable -> safe to share one instance across worker threads.
    const std::string format_id_;
    const std::string backend_id_;
};

EncodeResult OiioEncoder::encode(const EncodeRequest &req) {
    EncodeResult res;
    const auto t0 = std::chrono::steady_clock::now();
    auto finish = [&]() {
        res.t.encode_ms = ms_since(t0);
        res.t.total_ms = res.t.encode_ms;
        return res;
    };
    auto fail = [&](const std::string &msg) {
        log_error(kStage, kFile, "encode failed",
                  {{"format", format_id_}, {"error", msg}, {"path", req.target.out_path.string()}});
        res.bytes = 0;
        res.error = msg;
        return finish();
    };
    // E9 / parameter problems are configuration defects: log them (structured), never turn
    // them into EncodeResult.warnings — those are reserved for per-image quality or semantic
    // deviations (main-dialogue ruling, §3.8 T6 落地口径 ⑥).
    auto note_param = [&](const std::string &msg, const std::string &key,
                          const std::string &value) {
        log_warn(kStage, kFile, msg, {{"format", format_id_}, {"param", key}, {"value", value}});
    };

    // W1-T6 接线（design §3.1 / §7.2）：OIIO 写面 = **真实进度**，走 OIIO 自身的
    // `ImageOutput::write_image(..., ProgressCallback, void*)`（imageio.h:66 明示该回调
    // "called periodically by read_image and write_image"，实现见 imageoutput.cpp:651/668/703/712：
    // 起手 0.0、逐写块、收尾 1.0）。粒度 = OIIO 的写块（约 64MB 或整 strip 行数，见
    // imageoutput.cpp:673-680 的 chunk 计算）—— **不改写为逐行 write_scanlines**：那会打散
    // TIFF 的 strip 并行压缩路径（tiffoutput.cpp:1471-1518 的 parallelize 条件要求整 strip
    // 边界），构成性能回退风险（§11.3 禁回退）。png/tiff/bmp 三个插件都不支持 "rectangles"
    // （各自 supports() 实测），故其上 write_image 必然走该回调路径。
    // —— E3 线程映射（§3.1 正文，W1-T7 落地）——
    //   §3.1：oiio = **无内部线程**（按正文口径）→ 映射义务 = "如实记录 E 不生效"。
    //   证据与残留缺口（不擅改，如实记录）：OIIO 确实**没有** `ImageOutput` 级的"编码线程数"
    //   参数，本编码器无法把 E 传进写路径（png/tiff/bmp 插件各自无该参数）；但 OIIO 有
    //   **全局** `attribute("threads")` 线程池（imageio.h 的全局属性；`ImageOutput::threads(n)`
    //   是该池的 write 侧 fan-out 策略，imageio.h:3394 起），默认 = 逻辑核 → TIFF strip 压缩等
    //   大块写在库内可自行 fan-out。该池是**进程级、跨格式**的，改它属于超范围（会影响解码与
    //   其它写路径），故本任务按正文只记日志；若主对话裁定把 E 路由到 `out->threads(E)`，
    //   落点就是这一处（T7 已上报，见 m4-report 偏差与例外）。
    //   E == 1（深队列分支，= 0.2 行为）→ 不产生任何日志；E > 1 → 一条 info 行。
    OiioProgressRelay relay{&req.progress};
    if (req.encode_threads > 1) {
        log_info(kStage, kFile, "encoder has no internal threading; encode_threads ignored",
                 {{"encoder", "oiio"}, {"encode_threads", std::to_string(req.encode_threads)}});
    }

    try {
        if (!req.img.initialized() || req.img.spec().width <= 0 || req.img.spec().height <= 0)
            return fail("empty input image");
        const OIIO::ImageSpec &ispec = req.img.spec();
        const int w = ispec.width, h = ispec.height, nch = ispec.nchannels;
        assert(w > 0 && h > 0);
        assert(nch >= 1 && nch <= 4);
        if (nch < 1 || nch > 4)
            return fail("unsupported channel count " + std::to_string(nch) + " (expected 1..4)");

        // ---- output data type (E3: unsupported bit depth is an error, never a silent downgrade)
        // R-090：BMP 的 out_bitdepth 是**每像素位数（bpp）口径**（24=RGB 24bpp / 8=灰度 8bpp），
        // 其余格式是**每样本位数（bps）口径**（8/16）—— 同名 int 双语义，见 params.h 的
        // FormatDef::bitdepths 注释与 format_tables.cpp 的 bmp 行。错误文案必须两个值都列。
        OIIO::TypeDesc out_type = OIIO::TypeDesc::UINT8;
        if (format_id_ == "bmp") {
            if (req.target.out_bitdepth != 24 && req.target.out_bitdepth != 8)
                return fail("bmp: unsupported bitdepth " + std::to_string(req.target.out_bitdepth) +
                            " (expected 24 or 8)");
            out_type = OIIO::TypeDesc::UINT8;
        } else {
            if (req.target.out_bitdepth == 8)
                out_type = OIIO::TypeDesc::UINT8;
            else if (req.target.out_bitdepth == 16)
                out_type = OIIO::TypeDesc::UINT16;
            else
                return fail(format_id_ + ": unsupported bitdepth " +
                            std::to_string(req.target.out_bitdepth) + " (expected 8 or 16)");
        }
        const int bps = static_cast<int>(out_type.size());

        OIIO::ImageSpec spec(w, h, nch, out_type);
        switch (nch) {
        case 1:
            spec.channelnames = {"Y"};
            break;
        case 2:
            spec.channelnames = {"Y", "A"};
            spec.alpha_channel = 1;
            break;
        case 3:
            spec.channelnames = {"R", "G", "B"};
            break;
        default:
            spec.channelnames = {"R", "G", "B", "A"};
            spec.alpha_channel = 3;
            break;
        }

        // ---- format parameters ----
        if (format_id_ == "png") {
            int64_t level = param_int(req.target.params, "compressionLevel", 6);
            const int clamped = static_cast<int>(std::clamp<int64_t>(level, 0, 9));
            if (clamped != level)
                note_param("compressionLevel clamped", "compressionLevel",
                           std::to_string(level) + " -> " + std::to_string(clamped));
            // R-010：键名 = "png:compressionLevel"（pngoutput.cpp:165-169 唯一读取点）。
            // 旧裸键 "compressionLevel" 全库无人消费 → UI「zlib 压缩级别」静默无效。
            spec.attribute("png:compressionLevel", clamped);
        } else if (format_id_ == "tiff") {
            const std::string compression = param_str(req.target.params, "compression", "lzw");
            if (!tiff_compression_known(compression))
                return fail("tiff: invalid compression '" + compression +
                            "' (allowed: none, lzw, zip, ccittrle, packbits)");
            spec.attribute("compression", compression);
            if (compression == "zip") {
                const int64_t level = param_int(req.target.params, "deflate_level", 6);
                spec.attribute("tiff:zipquality",
                               static_cast<int>(std::clamp<int64_t>(level, 1, 9)));
            }
            const int64_t predictor = param_int(req.target.params, "predictor", 2);
            // R-011（M7-WP2 复核 + 补充）：键名逐字取上游消费点 "tiff:Predictor"
            // （tiffoutput.cpp:1026-1029 put_parameter），且在自动 predictor 规则
            // （tiffoutput.cpp:685-701）**之后**处理 ⇒ none/horizontal 两档真实生效
            // （test_enc_oiio 的字节差断言钉住）。float(3) 是**整型样本下不可用**的档：
            // libtiff 的 PREDICTOR_FLOATINGPOINT 仅支持 SAMPLEFORMAT_IEEEFP，本编码器
            // 输出 8/16 位整型 ⇒ 上游 write 期报 "Floating point Predictor not supported"
            // 拒绝（R-011 "假选项"支线）——此处前置校验，给出明确文案而不是让 libtiff 报
            // 英文底层错。
            if (predictor != 1 && predictor != 2 && predictor != 3)
                return fail("tiff: invalid predictor " + std::to_string(predictor) +
                            " (allowed: 1=none, 2=horizontal, 3=float)");
            if (compression == "lzw" || compression == "zip") {
                if (predictor == 3)
                    return fail("tiff: predictor=float (3) is not usable here — libtiff's "
                                "floating point predictor requires float samples, and this "
                                "encoder writes 8/16-bit integer samples (use 1=none or "
                                "2=horizontal)");
                spec.attribute("tiff:Predictor", static_cast<int>(predictor));
            }
            const int64_t tw = param_int(req.target.params, "tiff_tile_width", 0);
            const int64_t th = param_int(req.target.params, "tiff_tile_height", 0);
            if ((tw > 0) != (th > 0))
                return fail("tiff: tile width and height must both be > 0 (tiled) or both be 0 "
                            "(strips); got width=" +
                            std::to_string(tw) + " height=" + std::to_string(th));
            if (tw > 0) {
                if (tw % 16 != 0)
                    return fail("tiff: tile width " + std::to_string(tw) +
                                " must be a positive multiple of 16");
                if (th % 16 != 0)
                    return fail("tiff: tile height " + std::to_string(th) +
                                " must be a positive multiple of 16");
                spec.tile_width = static_cast<int>(tw);
                spec.tile_height = static_cast<int>(th);
            }
        }
        // png/tiff/bmp: no other parameters (bmp has none at all)

        // ---- ICC (E5); EXIF/XMP deliberately not written here (E7) ----
        if (!req.meta.icc_profile.empty()) {
            const size_t n = req.meta.icc_profile.size();
            spec.attribute(
                "ICCProfile", OIIO::TypeDesc(OIIO::TypeDesc::UINT8, static_cast<int>(n)),
                OIIO::cspan<std::byte>(
                    reinterpret_cast<const std::byte *>(req.meta.icc_profile.data()), n));
        }

        // ---- E9: unknown parameters are ignored with a warning ----
        const std::vector<std::string> known = known_param_keys(format_id_, backend_id_);
        for (const auto &[key, value] : req.target.params) {
            if (key.rfind("__", 0) == 0)
                continue; // reserved keys (§3.4)
            if (std::find(known.begin(), known.end(), key) == known.end())
                note_param("unrecognised parameter ignored", key, param_value_text(value));
        }

        // ---- pixels: float32 -> target integer type, one conversion only (R11) ----
        // M4-W5-T19 接线（design §11.2 热路径表第 2 行「float→int 舍入（encode 前）」）：
        // 逐像素 ConstIterator + 标量 quantize 改为**逐行 get_pixels**（连续 float 行）+
        // **pp::simd::quantize8 / quantize16**（唯一运行期分派层，AVX2 8 样本/迭代）。
        // 内存面与旧实现同量级（至多多一行缓冲，不是整帧）；输出布局逐字不变（交织、
        // xstride = nch·bps）——16 位面直接写进输出缓冲（同 strides），无额外中间帧。
        // 量化口径：simd 契约 = v 非正（含 NaN）→ 0 / v ≥ 1 → maxv / 否则 (uint)(v·maxv+0.5f)
        // （float 乘加截断）；原标量 `quantize` 是 double `lround(clamp·maxv)`。两者在金样
        // 覆盖的整数源（rgb8/rgb16/multi.tif 等 8/16 位 PNG/TIFF/BMP）上逐位一致；实测金样
        // 20 对全绿（含 png16-lossless / tiff16-lzw / bmp-exact / multipage-png 的 exact 断言）。
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * nch * bps);
        std::vector<float> rowf(static_cast<size_t>(w) * static_cast<size_t>(nch));
        for (int y = 0; y < h; ++y) {
            // R-163：`EncodeRequest::cancelled` 真实消费 —— 行级提前退出（写盘前无半成品）。
            if (req.cancelled && req.cancelled())
                return fail("encode cancelled");
            const OIIO::ROI rroi(ispec.x, ispec.x + w, ispec.y + y, ispec.y + y + 1, 0, 1, 0, nch);
            if (!req.img.get_pixels(rroi, OIIO::TypeFloat, rowf.data()))
                return fail("cannot read pixels (row " + std::to_string(y) +
                            "): " + req.img.geterror());
            const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(nch);
            uint8_t *dst =
                pixels.data() + static_cast<std::size_t>(y) * n * static_cast<std::size_t>(bps);
            if (bps == 1) {
                pp::simd::quantize8(rowf.data(), dst, n);
            } else { // bps == 2：输出面本身即 u16 数组（xstride = nch·2）→ 直接落写
                pp::simd::quantize16(rowf.data(), reinterpret_cast<uint16_t *>(dst), n, 65535);
            }
        }

        // ---- write through OIIO（W1-T6：带进度中继；回调返回 false = 不中止）----
        // R-003（M7-WP2 接线）：流式产物先写**同目录临时文件**（fsops::atomic_temp_path），
        // 成功后 commit_temp_file 原子替换最终路径；任一步失败 discard_temp_file 清场。
        if (req.cancelled && req.cancelled())
            return fail("encode cancelled");
        const std::filesystem::path tmp_path = atomic_temp_path(req.target.out_path);
        if (tmp_path.empty())
            return fail("cannot derive temp path for: " + req.target.out_path.string());
        std::unique_ptr<OIIO::ImageOutput> out = OIIO::ImageOutput::create(format_id_);
        if (!out) {
            discard_temp_file(tmp_path);
            return fail("OpenImageIO has no output plugin for '" + format_id_ + "'");
        }
        auto fail_tmp = [&](const std::string &msg) {
            out.reset(); // 先关句柄再清场（Windows 上占用中的文件删不掉）
            discard_temp_file(tmp_path);
            return fail(msg);
        };
        if (!out->open(tmp_path.string(), spec))
            return fail_tmp("OIIO open failed for " + req.target.out_path.string() + ": " +
                            out->geterror());
        const OIIO::stride_t xstride = static_cast<OIIO::stride_t>(nch) * bps;
        const OIIO::stride_t ystride = xstride * w;
        if (!out->write_image(out_type, pixels.data(), xstride, ystride, OIIO::AutoStride,
                              &oiio_progress_trampoline, &relay))
            return fail_tmp("OIIO write_image failed for " + req.target.out_path.string() + ": " +
                            out->geterror());
        if (!out->close())
            return fail_tmp("OIIO close failed for " + req.target.out_path.string() + ": " +
                            out->geterror());

        std::error_code ec;
        const uint64_t bytes = static_cast<uint64_t>(std::filesystem::file_size(tmp_path, ec));
        if (ec || bytes == 0)
            return fail_tmp("output file missing or empty: " + req.target.out_path.string());
        {
            std::string cerr;
            if (!commit_temp_file(tmp_path, req.target.out_path, &cerr))
                return fail_tmp("commit failed: " + cerr);
        }
        res.bytes = bytes;
        res.progress_reported =
            relay.called_with_progress(); // 真回调才报，短路（rectangles 插件）不报
        log_debug(kStage, kFile, "encoded",
                  {{"format", format_id_},
                   {"size", std::to_string(w) + "x" + std::to_string(h)},
                   {"channels", std::to_string(nch)},
                   // R-090：报 out_bitdepth 原值并标注单位（bmp = bpp，其余 = bps），
                   // 旧口径 bps*8 对 bmp 恒报 8，与所选 24 对不上。
                   {"bitdepth", std::to_string(req.target.out_bitdepth)},
                   {"bitdepth_unit", format_id_ == "bmp" ? "bpp" : "bps"},
                   {"sample_bitdepth", std::to_string(bps * 8)},
                   {"bytes", std::to_string(res.bytes)}});
        return finish();
    } catch (const std::exception &ex) {
        return fail(std::string("exception: ") + ex.what());
    } catch (...) {
        return fail("unknown exception");
    }
}

std::unique_ptr<IEncoder> make_png() { return std::make_unique<OiioEncoder>("png", "oiio"); }
std::unique_ptr<IEncoder> make_tiff() { return std::make_unique<OiioEncoder>("tiff", "oiio"); }
std::unique_ptr<IEncoder> make_bmp() { return std::make_unique<OiioEncoder>("bmp", "oiio"); }

// Static self-registration (§3.17); factories live in this anonymous namespace, so the
// macro must be expanded inside namespace pp for the pasted name to resolve.
PP_REGISTER_ENCODER("png", "oiio", make_png);
PP_REGISTER_ENCODER("tiff", "oiio", make_tiff);
PP_REGISTER_ENCODER("bmp", "oiio", make_bmp);

} // namespace

// No link anchor here (M2-T13): self-registration is guaranteed by the link form, not by
// a referenced symbol — every consumer links pp_core whole-archive via the
// pp_core_registered interface target (CMakeLists §2b), so this TU's PP_REGISTER_ENCODER
// initialiser always runs.
} // namespace pp
