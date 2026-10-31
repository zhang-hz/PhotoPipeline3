// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — JPEG XL uses_original_profile 回归单测（有损 VarDCT 强制 XYB）。
//
// 契约 / 根因（task-9；修复点 = src/codecs/enc_jxl.cpp 的 J8 修订）：
//   libjxl 在 JxlBasicInfo.uses_original_profile=true 时让 VarDCT 走非 XYB「原始配置」
//   路径，命中上游 libjxl issue #4552：有损 + uses_original_profile=true 的 VarDCT 出现
//   块状/色噪且体积暴涨，distance 越低越差。本仓输出恒嵌 ICC（ColorManager），旧推导
//   (lossless || modular || has_icc) 让**所有**有损 VarDCT 都踩中该路径。
//
// 本测试把修复后的行为钉死（断言物 = 写出的 JXL 码流头，不是 OIIO 的转述）：
//   1) 有损 VarDCT（带 ICC / 不带 ICC）→ uses_original_profile == 0（XYB 路径）；
//   2) lossless / modular → == 1（保留原色彩配置，既有语义不变）；
//   3) 显式 uses_original_profile=true + 有损 VarDCT → EncodeResult{bytes==0, error≠空}
//      且不落盘（不产坏件），错误文案点名参数并给出 tech=modular/lossless 出路；
//   4) 显式 false 与「键缺失」的默认路径产物逐字节一致（三态覆写语义未被破坏）；
//   5) XYB 仍随码流携带原色彩配置（标准空间存结构化枚举，否则存 ICC）：解码端
//      JXL_COLOR_PROFILE_TARGET_ORIGINAL 能取回合法 ICC ⇒ 强制 XYB 不是色彩降级
//      （宽色域目标仍可还原）。
//
// 读码头用 libjxl C API（jxl/decode.h）；事件循环写法与 src/decode/oiio_reader.cpp 的
// jxl_alpha_premultiplied() 同构（BASIC_INFO / COLOR_ENCODING 事件）。
// 输入是内存合成的 64x64 float32 渐变（同 test_enc_smoke.cpp 的做法）：无外部语料、
// 无网络；临时产物落 <cwd>/.cache/tmp/jxl-fix/test_jxl_original_profile（gitignored）。

#include <jxl/decode.h>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imageio.h>
#include <OpenImageIO/span.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "codecs/encoder.h"
#include "codecs/encoders.h"
#include "core/colormanager.h"
#include "core/logger.h"
#include "core/params.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;
int g_checks = 0;

// 失败行与既有单测同口径（"FAIL <case>: <detail>"）；成功不打印，逐条断言计数。
void check(bool ok, const std::string &case_name, const std::string &detail) {
    ++g_checks;
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
        std::fflush(stdout);
    }
}

void info(const std::string &text) {
    std::printf("info %s\n", text.c_str());
    std::fflush(stdout);
}

std::string num(long long v) { return std::to_string(v); }

// ---------------------------------------------------------------- fixtures --

// 64x64 float32 三通道渐变：够小（毫秒级编码）且含平滑色度，能走过 VarDCT/Modular 两条路径。
OIIO::ImageBuf make_rgb_buf() {
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;
    constexpr int kChannels = 3;
    OIIO::ImageSpec spec(kWidth, kHeight, kChannels, OIIO::TypeFloat);
    OIIO::ImageBuf buf(spec);
    std::vector<float> px(static_cast<std::size_t>(kWidth) * kHeight * kChannels);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * kChannels;
            px[i] = static_cast<float>(x) / static_cast<float>(kWidth - 1);
            px[i + 1] = static_cast<float>(y) / static_cast<float>(kHeight - 1);
            px[i + 2] = 0.25f + 0.5f * (static_cast<float>(x) + static_cast<float>(y)) /
                                    static_cast<float>(kWidth + kHeight);
        }
    }
    const OIIO::ROI roi(0, kWidth, 0, kHeight, 0, 1, 0, kChannels);
    OIIO::span<const std::byte> src(reinterpret_cast<const std::byte *>(px.data()),
                                    px.size() * sizeof(float));
    buf.set_pixels(roi, OIIO::TypeFloat, src);
    return buf;
}

// ------------------------------------------------------------- encode helper --

// 与 test_enc_smoke.cpp / test_enc_oiio.cpp 同构：把入参原样装进 OutputTarget，
// progress 留空、encode_threads=1（编码器内部线程关闭的调度器口径）。
pp::EncodeResult run_encode(pp::IEncoder &enc, OIIO::ImageBuf &buf, const pp::ParamSet &params,
                            const pp::MetadataPayloads &meta, const fs::path &out, int out_bitdepth,
                            const std::string &tech_id) {
    pp::OutputTarget target;
    target.format_id = enc.format().id;
    target.tech_id = tech_id;
    target.params = params;
    target.out_bitdepth = out_bitdepth;
    target.out_path = out;
    target.supports_alpha = enc.format().supports_alpha;
    pp::EncodeRequest req{buf, target, meta, {}, pp::ProgressFn{}, 1};
    return enc.encode(req);
}

// ------------------------------------------------------------- read back --

struct FileBytes {
    bool ok = false;
    std::string bytes;
    std::string error;
};

FileBytes read_file_bytes(const fs::path &path) {
    FileBytes out;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out.error = "cannot open " + path.string();
        return out;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out.bytes = ss.str();
    out.ok = !out.bytes.empty();
    if (!out.ok) {
        out.error = "empty file " + path.string();
    }
    return out;
}

// 合法 ICC 的极小判据：≥128 字节头 + offset 36 的 'acsp' 签名（libjxl 侧生成的
// sRGB 近似与内嵌原始 ICC 都满足；这里只证明「拿回的是一份可用的 ICC」）。
bool looks_like_icc(const std::string &bytes) {
    constexpr std::size_t kIccMinSize = 128;
    constexpr std::size_t kIccSignatureOffset = 36;
    return bytes.size() >= kIccMinSize && bytes.compare(kIccSignatureOffset, 4, "acsp") == 0;
}

struct JxlHeader {
    bool ok = false;
    std::string error;
    int uses_original_profile = -1;
    int bits_per_sample = -1;
    int num_color_channels = -1;
    bool have_container = false;
    bool original_profile_available = false;
    std::string original_icc;
};

struct JxlDecoderDeleter {
    void operator()(JxlDecoder *dec) const { JxlDecoderDestroy(dec); }
};

// 只解码头（不下像素）：BASIC_INFO → 字段断言；COLOR_ENCODING → 取回原始色彩配置 ICC。
JxlHeader probe_jxl_header(const fs::path &path) {
    JxlHeader out;
    const FileBytes file = read_file_bytes(path);
    if (!file.ok) {
        out.error = file.error;
        return out;
    }
    const std::string &bytes = file.bytes;
    std::unique_ptr<JxlDecoder, JxlDecoderDeleter> dec(JxlDecoderCreate(nullptr));
    if (!dec) {
        out.error = "JxlDecoderCreate failed";
        return out;
    }
    const int events =
        static_cast<int>(JXL_DEC_BASIC_INFO) | static_cast<int>(JXL_DEC_COLOR_ENCODING);
    if (JxlDecoderSubscribeEvents(dec.get(), events) != JXL_DEC_SUCCESS) {
        out.error = "JxlDecoderSubscribeEvents failed";
        return out;
    }
    if (JxlDecoderSetInput(dec.get(), reinterpret_cast<const std::uint8_t *>(bytes.data()),
                           bytes.size()) != JXL_DEC_SUCCESS) {
        out.error = "JxlDecoderSetInput failed";
        return out;
    }
    JxlDecoderCloseInput(dec.get());
    bool have_basic = false;
    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());
        if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo basic{};
            if (JxlDecoderGetBasicInfo(dec.get(), &basic) != JXL_DEC_SUCCESS) {
                out.error = "JxlDecoderGetBasicInfo failed";
                return out;
            }
            out.uses_original_profile = basic.uses_original_profile != 0 ? 1 : 0;
            out.bits_per_sample = static_cast<int>(basic.bits_per_sample);
            out.num_color_channels = static_cast<int>(basic.num_color_channels);
            out.have_container = basic.have_container != 0;
            have_basic = true;
            continue;
        }
        if (status == JXL_DEC_COLOR_ENCODING) {
            std::size_t icc_size = 0;
            if (JxlDecoderGetICCProfileSize(dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                                            &icc_size) == JXL_DEC_SUCCESS &&
                icc_size > 0) {
                out.original_icc.resize(icc_size);
                if (JxlDecoderGetColorAsICCProfile(
                        dec.get(), JXL_COLOR_PROFILE_TARGET_ORIGINAL,
                        reinterpret_cast<std::uint8_t *>(out.original_icc.data()),
                        icc_size) == JXL_DEC_SUCCESS) {
                    out.original_profile_available = true;
                } else {
                    out.original_icc.clear();
                }
            }
            break; // 头信息已齐（BASIC_INFO 在前，COLOR_ENCODING 之后才是像素数据）
        }
        if (status == JXL_DEC_NEED_MORE_INPUT) {
            out.error = "JxlDecoderProcessInput: NEED_MORE_INPUT（文件不完整？）";
            return out;
        }
        if (status == JXL_DEC_ERROR) {
            out.error = "JxlDecoderProcessInput: ERROR";
            return out;
        }
        if (status == JXL_DEC_SUCCESS) {
            out.error = "decoder finished before COLOR_ENCODING event";
            return out;
        }
    }
    out.ok = have_basic;
    if (!out.ok) {
        out.error = "BASIC_INFO event not produced";
    }
    return out;
}

// 统一断言「码流头 uses_original_profile == want」；失败时带上完整细节。
void check_uses_original_profile(const JxlHeader &hdr, int want, const std::string &case_name) {
    check(hdr.ok, case_name + "/header", hdr.error);
    check(hdr.uses_original_profile == want, case_name + "/uses_original_profile",
          "expected " + num(want) + ", got " + num(hdr.uses_original_profile) +
              " (0 = XYB path, 1 = keep original profile)");
}

int finish() {
    std::printf("SUMMARY checks=%d failed=%d\n", g_checks, g_failed);
    std::fflush(stdout);
    return g_failed;
}

// ------------------------------------------------------------------- cases --

int run_tests() {
    std::error_code ec;
    const fs::path dir =
        fs::current_path(ec) / ".cache" / "tmp" / "jxl-fix" / "test_jxl_original_profile";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    pp::log_init(dir / "log", pp::LogLevel::Warn);

    // 合法 ICC fixture：与产品实际嵌入的宽色域目标同一来源（ColorManager 生成 Display P3）。
    std::string icc_err;
    const std::string p3_icc = pp::load_target_icc(pp::ColorTarget::DisplayP3, icc_err);
    check(!p3_icc.empty() && icc_err.empty(), "jxl/orp/fixture/p3-icc",
          "Display P3 ICC unavailable: '" + icc_err + "'");
    if (p3_icc.empty()) {
        return finish();
    }
    check(looks_like_icc(p3_icc), "jxl/orp/fixture/p3-icc/valid", "fixture is not an ICC blob");

    pp::MetadataPayloads meta_icc;
    meta_icc.icc_profile = p3_icc;
    const pp::MetadataPayloads meta_none;

    const pp::FormatDef *fmt = pp::find_format("jxl");
    check(fmt != nullptr, "jxl/orp/fixture/format", "find_format('jxl') returned nullptr");
    if (fmt == nullptr) {
        return finish();
    }
    std::unique_ptr<pp::IEncoder> enc = pp::make_encoder("jxl", "libjxl");
    check(enc != nullptr, "jxl/orp/fixture/encoder", "make_encoder(jxl,libjxl) returned nullptr");
    if (!enc) {
        return finish();
    }

    // 目录行是 has_default=false（三态），所以产品默认工作集里**没有**该键；
    // 显式覆写才写键 —— 下面的「缺键 = 推导」断言依赖这个前提。
    pp::ParamSet vardct = pp::default_params(*fmt, "libjxl", "vardct", false);
    check(vardct.find("uses_original_profile") == vardct.end(),
          "jxl/orp/params/vardct/no-implicit-key",
          "default vardct param set must not carry uses_original_profile (three-state premise)");

    // ---- 1) 有损 VarDCT + ICC → XYB（本修复的核心断言；旧实现此处为 1） ----
    const fs::path out_default = dir / "lossy_vardct_icc_default.jxl";
    {
        OIIO::ImageBuf buf = make_rgb_buf();
        const pp::EncodeResult r =
            run_encode(*enc, buf, vardct, meta_icc, out_default, 8, "vardct");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/lossy_vardct_icc/encode", r.error);
        const JxlHeader hdr = probe_jxl_header(out_default);
        check_uses_original_profile(hdr, 0, "jxl/orp/lossy_vardct_icc");
        check(hdr.bits_per_sample == 8, "jxl/orp/lossy_vardct_icc/bits",
              "expected 8, got " + num(hdr.bits_per_sample));
        check(hdr.num_color_channels == 3, "jxl/orp/lossy_vardct_icc/channels",
              "expected 3, got " + num(hdr.num_color_channels));
        check(hdr.have_container, "jxl/orp/lossy_vardct_icc/container",
              "container flag missing (E7: boxes need BMFF)");
        // 「XYB 不是色彩降级」：原始色彩配置仍能从码流取回，且是一份合法 ICC。
        check(hdr.original_profile_available, "jxl/orp/lossy_vardct_icc/icc-kept",
              "no original color profile available from the decoder");
        check(looks_like_icc(hdr.original_icc), "jxl/orp/lossy_vardct_icc/icc-valid",
              "decoder-returned original profile is not an ICC blob");
        info("lossy vardct + ICC: bytes=" + num(static_cast<long long>(r.bytes)) +
             " uses_original_profile=0 restored_icc=" +
             num(static_cast<long long>(hdr.original_icc.size())));
    }

    // ---- 2) 显式 false 覆写 = 默认路径（逐字节相同） ----
    {
        pp::ParamSet explicit_false = vardct;
        explicit_false["uses_original_profile"] = false;
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossy_vardct_icc_explicit_false.jxl";
        const pp::EncodeResult r =
            run_encode(*enc, buf, explicit_false, meta_icc, out, 8, "vardct");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/lossy_vardct_icc/explicit_false/encode",
              r.error);
        check_uses_original_profile(probe_jxl_header(out), 0,
                                    "jxl/orp/lossy_vardct_icc/explicit_false");
        const FileBytes bytes_default = read_file_bytes(out_default);
        const FileBytes bytes_explicit = read_file_bytes(out);
        check(bytes_default.ok && bytes_explicit.ok,
              "jxl/orp/lossy_vardct_icc/explicit_false/readback",
              bytes_default.error + bytes_explicit.error);
        check(bytes_default.ok && bytes_explicit.ok && bytes_default.bytes == bytes_explicit.bytes,
              "jxl/orp/lossy_vardct_icc/explicit_false/identical",
              "explicit false output differs from the key-absent default path");
    }

    // ---- 3) 显式 true + 有损 VarDCT → 明确失败、不落盘 ----
    {
        pp::ParamSet explicit_true = vardct;
        explicit_true["uses_original_profile"] = true;
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossy_vardct_icc_explicit_true.jxl";
        const pp::EncodeResult r = run_encode(*enc, buf, explicit_true, meta_icc, out, 8, "vardct");
        check(r.bytes == 0 && !r.error.empty(), "jxl/orp/lossy_vardct_icc/explicit_true/rejected",
              "expected bytes=0 + error, got bytes=" + num(static_cast<long long>(r.bytes)) +
                  " error='" + r.error + "'");
        check(r.error.find("uses_original_profile") != std::string::npos,
              "jxl/orp/lossy_vardct_icc/explicit_true/names-param",
              "error must name uses_original_profile: '" + r.error + "'");
        check(r.error.find("modular") != std::string::npos,
              "jxl/orp/lossy_vardct_icc/explicit_true/hint",
              "error must offer tech=modular/lossless: '" + r.error + "'");
        check(!fs::exists(out, ec), "jxl/orp/lossy_vardct_icc/explicit_true/no-artifact",
              "rejected encode must not leave an output file: " + out.string());
        info("rejected explicit true: '" + r.error + "'");
    }

    // ---- 4) lossless（Modular + SetFrameLossless）→ 保留原配置（既有语义） ----
    {
        const pp::ParamSet lossless = pp::default_params(*fmt, "libjxl", "modular", true);
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossless_modular_icc.jxl";
        const pp::EncodeResult r = run_encode(*enc, buf, lossless, meta_icc, out, 8, "modular");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/lossless_icc/encode", r.error);
        check_uses_original_profile(probe_jxl_header(out), 1, "jxl/orp/lossless_icc");
    }

    // ---- 5) 有损 Modular → 同样保留原配置（Modular 不受本修复影响） ----
    {
        pp::ParamSet mod_lossy = pp::default_params(*fmt, "libjxl", "modular", false);
        pp::ParamSet explicit_true = mod_lossy;
        explicit_true["uses_original_profile"] = true;
        check(mod_lossy.find("distance") != mod_lossy.end(), "jxl/orp/modular_lossy/params",
              "modular default set has no distance key");
        mod_lossy["distance"] = 1.0;
        explicit_true["distance"] = 1.0;
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "modular_lossy_icc.jxl";
        const pp::EncodeResult r = run_encode(*enc, buf, mod_lossy, meta_icc, out, 8, "modular");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/modular_lossy/encode", r.error);
        check_uses_original_profile(probe_jxl_header(out), 1, "jxl/orp/modular_lossy");
        // 显式 true 在 Modular 上是合法覆写（J7 的 non-perceptual 场景依赖它），不得被拒。
        OIIO::ImageBuf buf2 = make_rgb_buf();
        const fs::path out2 = dir / "modular_lossy_icc_explicit_true.jxl";
        const pp::EncodeResult r2 =
            run_encode(*enc, buf2, explicit_true, meta_icc, out2, 8, "modular");
        check(r2.error.empty() && r2.bytes > 0, "jxl/orp/modular_lossy/explicit_true/allowed",
              r2.error);
        check_uses_original_profile(probe_jxl_header(out2), 1,
                                    "jxl/orp/modular_lossy/explicit_true");
    }

    // ---- 6) 无 ICC 的有损 VarDCT → 仍是 XYB（旧行为保留，防回归） ----
    {
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossy_vardct_no_icc.jxl";
        const pp::EncodeResult r = run_encode(*enc, buf, vardct, meta_none, out, 8, "vardct");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/lossy_vardct_no_icc/encode", r.error);
        check_uses_original_profile(probe_jxl_header(out), 0, "jxl/orp/lossy_vardct_no_icc");
    }

    // ---- 7) 16-bit 有损 VarDCT + ICC → XYB（T1.tif 实测场景；码流头位深对得上） ----
    {
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossy_vardct_icc_16bit.jxl";
        const pp::EncodeResult r = run_encode(*enc, buf, vardct, meta_icc, out, 16, "vardct");
        check(r.error.empty() && r.bytes > 0, "jxl/orp/lossy_vardct_icc_16bit/encode", r.error);
        const JxlHeader hdr = probe_jxl_header(out);
        check_uses_original_profile(hdr, 0, "jxl/orp/lossy_vardct_icc_16bit");
        check(hdr.bits_per_sample == 16, "jxl/orp/lossy_vardct_icc_16bit/bits",
              "expected 16, got " + num(hdr.bits_per_sample));
    }

    // ---- 8) J7 联动兜底：non-perceptual 只存在于原配置路径；有损 VarDCT 下给可读错误 ----
    {
        pp::ParamSet non_perceptual = vardct;
        non_perceptual["disable_perceptual_heuristics"] = true;
        OIIO::ImageBuf buf = make_rgb_buf();
        const fs::path out = dir / "lossy_vardct_disable_perceptual.jxl";
        const pp::EncodeResult r =
            run_encode(*enc, buf, non_perceptual, meta_icc, out, 8, "vardct");
        check(r.bytes == 0 && !r.error.empty(), "jxl/orp/lossy_vardct/disable_perceptual/rejected",
              "expected bytes=0 + error, got bytes=" + num(static_cast<long long>(r.bytes)) +
                  " error='" + r.error + "'");
        check(r.error.find("disable_perceptual_heuristics") != std::string::npos,
              "jxl/orp/lossy_vardct/disable_perceptual/names-param",
              "error must name disable_perceptual_heuristics: '" + r.error + "'");
        check(!fs::exists(out, ec), "jxl/orp/lossy_vardct/disable_perceptual/no-artifact",
              "rejected encode must not leave an output file: " + out.string());
    }

    return finish();
}

} // namespace

// bugprone-exception-escape：main 不允许异常外逸 —— 测试主体整体包在 try/catch 里，
// 意外异常打印 FAIL 行并返回非零（而不是 std::terminate）。
int main() {
    try {
        const int failed = run_tests();
        pp::log_shutdown();
        return failed;
    } catch (const std::exception &e) {
        std::printf("FAIL jxl/orp/exception: %s\n", e.what());
        return 1;
    } catch (...) {
        std::printf("FAIL jxl/orp/exception: unknown exception\n");
        return 1;
    }
}
