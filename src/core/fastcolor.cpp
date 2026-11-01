// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10-W1-T2c：评分色彩归一快速内核实现。
//
// 方案（设计 §4.2.2(3) 的选项 C：预计算变换矩阵 + 快速 EOTF/TRC 表）：
//   绝大多数源/目标 ICC 是 **matrix-shaper** 类型（rXYZ/gXYZ/bXYZ + rTRC/gTRC/bTRC；
//   内建 sRGB、AdobeRGB1998、本程序生成的 Display P3 全属此类）：解码 = 逐通道 TRC 反曲线
//   （1D 表 + 线性插值）→ 源线性 RGB → 3×3 矩阵（源色料 → D50 PCS → sRGB 线性）→ clamp 到
//   [0,1]（精确路径的 lcms 输出经 sRGB 编码 + EOTF 后的等价结果）。
//   A2B0/B2A0 LUT 型 profile、灰度/非 RGB profile、缺 tag、解析失败 → 构建失败
//   （`get_shared()` 返回 nullptr）→ pipeline 回退精确 lcms。
//
// 为什么不用 3D CLUT 三线性插值：D-1 修正后的实测（本目录 report.md §3）显示 33³ CLUT 的
// 插值残差在色域边界处 max|Δlinear|≈0.012/ΔE76≈1.2，SSIMULACRA2 分数差在常规有损区间仍可达
// 0.07 量级（>0.01 验收线）；matrix-shaper 解析路线数值上与 lcms 同式，实测分数差 ≤1e-4 量级。
//
// 口径与回退契约见 core/fastcolor.h 头注释；数值对拍（|Δscore|≤0.01、ΔE 双口径）见
// tests/unit/test_quality_metric.cpp 与 build/m10/w1-fastcolor/report.md。

#include "core/fastcolor.h"

#include <OpenImageIO/hash.h>
#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/span.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/colormanager.h"
#include "core/logger.h"
namespace pp {

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::string_view kLogStage = "color";
constexpr std::string_view kLogFile = "fastcolor.cpp";

// TRC 解码表：每通道 [0,1] 等距 kTrcSamples 段 + 线性插值。sRGB/AdobeRGB 的 TRC 在
// 4096 段下插值误差 ~1e-7 量级（远低于验收容差；见单测 fastcolor/accuracy）。
constexpr int kTrcSamples = 16384;
// 接口兼容占位（matrix-shaper 路线无网格；`build(icc, grid, ...)` 的 grid 参数被忽略）。
constexpr int kDefaultGrid = 33;

// 进程级内核缓存（按源 ICC 字节 SHA1 复用；失败也缓存，避免反复重试 lcms）。
struct ClutCache {
    std::mutex mu;
    std::map<std::string, std::shared_ptr<const FastSrgbLinear>> map;
    FastSrgbLinear::Stats stats;
};

ClutCache &clut_cache() {
    static ClutCache cache;
    return cache;
}

std::string fmt_double(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.4g", v);
    return buf;
}

bool env_forced_exact() {
    const char *raw = std::getenv("PP_FASTCOLOR");
    if (raw == nullptr || *raw == '\0')
        return false;
    std::string v(raw);
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return v == "0" || v == "off" || v == "false" || v == "no";
}

// ---------------------------------------------------------------- ICC 解析 --
// 只认 matrix-shaper ICC（RGB colorspace + rXYZ/gXYZ/bXYZ + rTRC/gTRC/bTRC）；任何 LUT 型
// （A2B0/A2B1/A2B2）或结构不符 → 不支持（回退精确 lcms）。

uint32_t be32(const std::string &b, std::size_t off) {
    return (static_cast<uint32_t>(static_cast<unsigned char>(b[off])) << 24) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b[off + 1])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b[off + 2])) << 8) |
           static_cast<uint32_t>(static_cast<unsigned char>(b[off + 3]));
}
uint16_t be16(const std::string &b, std::size_t off) {
    return static_cast<uint16_t>((static_cast<uint32_t>(static_cast<unsigned char>(b[off])) << 8) |
                                 static_cast<uint32_t>(static_cast<unsigned char>(b[off + 1])));
}
double s15fixed16(const std::string &b, std::size_t off) {
    return static_cast<double>(static_cast<int32_t>(be32(b, off))) / 65536.0;
}

struct TrcCurve {
    enum class Kind : std::uint8_t {
        Unsupported,
        Identity,
        Gamma,
        Samples,
        Parametric
    } kind = Kind::Unsupported;
    double gamma = 1.0;
    std::vector<double> samples; // 采样曲线（u16 / 65535，域 [0,1]）
    // parametricCurveType（ICC v4 / v2 扩展）：Y = f(X)，类型 0..4
    int para_type = -1;
    std::array<double, 7> para{}; // g,a,b,c,d,e,f

    bool supported() const { return kind != Kind::Unsupported; }

    double eval(double x) const {
        switch (kind) {
        case Kind::Identity:
            return x;
        case Kind::Gamma:
            return std::pow(std::max(0.0, x), gamma);
        case Kind::Samples: {
            if (samples.empty())
                return x;
            const double t = std::clamp(x, 0.0, 1.0) * (static_cast<double>(samples.size()) - 1.0);
            const std::size_t i = static_cast<std::size_t>(t);
            if (i + 1 >= samples.size())
                return samples.back();
            const double f = t - static_cast<double>(i);
            return samples[i] + f * (samples[i + 1] - samples[i]);
        }
        case Kind::Parametric: {
            const double g = para[0];
            const double a = para[1];
            const double b = para[2];
            const double c = para[3];
            const double d = para[4];
            const double e = para[5];
            const double f = para[6];
            const auto y = [](double base, double exp) {
                return std::pow(std::max(0.0, base), exp);
            };
            switch (para_type) {
            case 0:
                return y(x, g);
            case 1:
                return x >= -b / a ? y(a * x + b, g) : 0.0;
            case 2:
                return x >= -b / a ? y(a * x + b, g) + c : c;
            case 3:
                return x >= d ? y(a * x + b, g) : c * x;
            case 4:
                return x >= d ? y(a * x + b, g) + e : c * x + f;
            default:
                return x;
            }
        }
        case Kind::Unsupported:
            break;
        }
        return x;
    }
};

bool parse_curve(const std::string &icc, std::size_t off, std::size_t size, TrcCurve &out,
                 std::string &err) {
    if (size < 12 || off + 12 > icc.size()) {
        err = "TRC tag truncated";
        return false;
    }
    const uint32_t type = be32(icc, off);
    const char sig[5] = {
        static_cast<char>((type >> 24) & 0xff), static_cast<char>((type >> 16) & 0xff),
        static_cast<char>((type >> 8) & 0xff), static_cast<char>(type & 0xff), '\0'};
    if (std::strcmp(sig, "curv") == 0) {
        const uint32_t count = be32(icc, off + 8);
        if (count == 0) {
            out.kind = TrcCurve::Kind::Identity;
            return true;
        }
        if (static_cast<std::size_t>(12) + 2 * static_cast<std::size_t>(count) > size ||
            off + 12 + 2 * static_cast<std::size_t>(count) > icc.size()) {
            err = "curv table truncated";
            return false;
        }
        if (count == 1) {
            out.kind = TrcCurve::Kind::Gamma;
            out.gamma = static_cast<double>(be16(icc, off + 12)) / 256.0;
            return true;
        }
        out.kind = TrcCurve::Kind::Samples;
        out.samples.resize(count);
        for (uint32_t i = 0; i < count; ++i) {
            const std::size_t at = off + 12 + 2 * static_cast<std::size_t>(i);
            out.samples[i] = static_cast<double>(be16(icc, at)) / 65535.0;
        }
        return true;
    }
    if (std::strcmp(sig, "para") == 0) {
        const uint16_t func = be16(icc, off + 8);
        static constexpr int kParams[5] = {1, 3, 4, 5, 7};
        if (func > 4) {
            err = "parametric curve type unsupported";
            return false;
        }
        const int n = kParams[func];
        if (size < static_cast<std::size_t>(12) + 4 * static_cast<std::size_t>(n) ||
            off + 12 + 4 * static_cast<std::size_t>(n) > icc.size()) {
            err = "para table truncated";
            return false;
        }
        out.kind = TrcCurve::Kind::Parametric;
        out.para_type = func;
        for (int i = 0; i < n; ++i) {
            out.para[static_cast<std::size_t>(i)] =
                s15fixed16(icc, off + 12 + 4 * static_cast<std::size_t>(i));
        }
        return true;
    }
    err = "TRC tag type unsupported (need curv/para)";
    return false;
}

struct IccMatrixProfile {
    std::array<double, 9> colorant{}; // 列主：列 = r/g/b 色料 XYZ（D50 PCS）
    TrcCurve curves[3];
};

// 3×3 矩阵工具（行主序 m[3i+j]）。
bool invert3(const std::array<double, 9> &m, std::array<double, 9> &inv) {
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                       m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::fabs(det) < 1e-18)
        return false;
    const double d = 1.0 / det;
    inv[0] = (m[4] * m[8] - m[5] * m[7]) * d;
    inv[1] = (m[2] * m[7] - m[1] * m[8]) * d;
    inv[2] = (m[1] * m[5] - m[2] * m[4]) * d;
    inv[3] = (m[5] * m[6] - m[3] * m[8]) * d;
    inv[4] = (m[0] * m[8] - m[2] * m[6]) * d;
    inv[5] = (m[2] * m[3] - m[0] * m[5]) * d;
    inv[6] = (m[3] * m[7] - m[4] * m[6]) * d;
    inv[7] = (m[1] * m[6] - m[0] * m[7]) * d;
    inv[8] = (m[0] * m[4] - m[1] * m[3]) * d;
    return true;
}

std::array<double, 9> mul3(const std::array<double, 9> &a, const std::array<double, 9> &b) {
    std::array<double, 9> out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            out[3 * i + j] = a[3 * i + 0] * b[3 * 0 + j] + a[3 * i + 1] * b[3 * 1 + j] +
                             a[3 * i + 2] * b[3 * 2 + j];
        }
    }
    return out;
}

// sRGB（D65 原色、D50 适配）色料矩阵——ICC sRGB 内建 profile 的标准值（与 lcms2 内建一致）。
std::array<double, 9> srgb_d50_colorants() {
    // 列 = r/g/b 色料 XYZ；行主序展开为 [[Xr,Xg,Xb],[Yr,Yg,Yb],[Zr,Zg,Zb]]
    return {0.4360747, 0.3850649, 0.1430804, 0.2225045, 0.7168786,
            0.0606169, 0.0139322, 0.0971045, 0.7141733};
}

bool parse_matrix_profile(const std::string &icc, IccMatrixProfile &out, std::string &err) {
    if (icc.size() < 132) {
        err = "ICC too short";
        return false;
    }
    const std::string colorspace = icc.substr(16, 4);
    if (colorspace != "RGB ") {
        err = "ICC color space is not RGB (" + colorspace + ")";
        return false;
    }
    if (icc.substr(36, 4) != "acsp") {
        err = "ICC missing 'acsp' signature";
        return false;
    }
    const uint32_t tag_count = be32(icc, 128);
    const std::size_t table_end = 132 + 12 * static_cast<std::size_t>(tag_count);
    if (tag_count == 0 || table_end > icc.size()) {
        err = "ICC tag table truncated";
        return false;
    }
    struct Entry {
        std::string sig;
        std::size_t off = 0, size = 0;
    };
    std::vector<Entry> entries;
    entries.reserve(tag_count);
    for (uint32_t i = 0; i < tag_count; ++i) {
        const std::size_t base = 132 + 12 * static_cast<std::size_t>(i);
        Entry e;
        e.sig = icc.substr(base, 4);
        e.off = be32(icc, base + 4);
        e.size = be32(icc, base + 8);
        if (e.off + e.size > icc.size()) {
            err = "ICC tag '" + e.sig + "' out of bounds";
            return false;
        }
        entries.push_back(std::move(e));
    }
    const auto find = [&entries](const char *sig) -> const Entry * {
        for (const Entry &e : entries) {
            if (e.sig == sig)
                return &e;
        }
        return nullptr;
    };
    // LUT 型 profile：lcms 会走 A2B/B2A 管线（非 matrix-shaper），不支持 → 回退精确路径。
    for (const char *lut_tag : {"A2B0", "A2B1", "A2B2", "B2A0", "B2A1", "B2A2"}) {
        if (find(lut_tag) != nullptr) {
            err = std::string("ICC has LUT tag ") + lut_tag + " (matrix-shaper only)";
            return false;
        }
    }
    const char *xyz_tags[3] = {"rXYZ", "gXYZ", "bXYZ"};
    for (int c = 0; c < 3; ++c) {
        const Entry *e = find(xyz_tags[c]);
        if (e == nullptr || e->size < 20 || icc.substr(e->off, 4) != "XYZ ") {
            err = std::string("ICC missing/invalid ") + xyz_tags[c];
            return false;
        }
        out.colorant[3 * 0 + c] = s15fixed16(icc, e->off + 8);
        out.colorant[3 * 1 + c] = s15fixed16(icc, e->off + 12);
        out.colorant[3 * 2 + c] = s15fixed16(icc, e->off + 16);
    }
    const char *trc_tags[3] = {"rTRC", "gTRC", "bTRC"};
    for (int c = 0; c < 3; ++c) {
        const Entry *e = find(trc_tags[c]);
        if (e == nullptr) {
            err = std::string("ICC missing ") + trc_tags[c];
            return false;
        }
        if (!parse_curve(icc, e->off, e->size, out.curves[c], err)) {
            err.insert(0, ": ");
            err.insert(0, trc_tags[c]);
            return false;
        }
    }
    return true;
}

} // namespace

float srgb_eotf(float v) {
    // 与 pipeline 既有评分精确路径逐字相同（IEC 61966-2-1 解扩展）。
    const float c = std::clamp(v, 0.0f, 1.0f);
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

bool FastSrgbLinear::forced_exact() { return env_forced_exact(); }

FastSrgbLinear::Stats FastSrgbLinear::stats() {
    ClutCache &cache = clut_cache();
    std::lock_guard<std::mutex> lk(cache.mu);
    return cache.stats;
}

void FastSrgbLinear::clear_cache() {
    ClutCache &cache = clut_cache();
    std::lock_guard<std::mutex> lk(cache.mu);
    cache.map.clear();
    cache.stats = Stats{};
}

std::shared_ptr<const FastSrgbLinear> FastSrgbLinear::get_shared(const std::string &src_icc) {
    if (src_icc.empty())
        return nullptr; // 假定 sRGB：评分行适配层走精确 EOTF 快路径，无需本内核
    ClutCache &cache = clut_cache();
    if (env_forced_exact()) {
        std::lock_guard<std::mutex> lk(cache.mu);
        ++cache.stats.disabled;
        return nullptr;
    }
    const std::string key = OIIO::SHA1::digest(src_icc.data(), src_icc.size());
    std::lock_guard<std::mutex> lk(cache.mu);
    const auto it = cache.map.find(key);
    if (it != cache.map.end()) {
        ++cache.stats.hits;
        return it->second;
    }
    std::string err;
    std::shared_ptr<const FastSrgbLinear> fast = build(src_icc, kDefaultGrid, err);
    if (!fast) {
        ++cache.stats.build_fail;
        log_warn(kLogStage, kLogFile, "fast sRGB-linear kernel unavailable; exact lcms fallback",
                 {{"icc_sha1", key}, {"error", err}});
        cache.map.emplace(key, nullptr); // 失败也缓存（不反复尝试 lcms）
        return nullptr;
    }
    ++cache.stats.builds;
    log_info(kLogStage, kLogFile, "fast sRGB-linear kernel built",
             {{"icc_sha1", key},
              {"engine", std::string(fast->engine())},
              {"precompute_ms", fmt_double(fast->build_ms())}});
    cache.map.emplace(key, fast);
    return fast;
}

std::shared_ptr<const FastSrgbLinear> FastSrgbLinear::build(const std::string &src_icc, int grid,
                                                            std::string &err) {
    (void)grid; // kDefaultGrid 仅为接口/日志兼容保留；matrix-shaper 路线无需网格
    err.clear();
    if (src_icc.empty()) {
        err = "empty source ICC (assumed-sRGB needs no kernel)";
        return nullptr;
    }
    IccMatrixProfile profile;
    const Clock::time_point t0 = Clock::now();
    if (!parse_matrix_profile(src_icc, profile, err))
        return nullptr;
    std::array<double, 9> srgb_inv{};
    if (!invert3(srgb_d50_colorants(), srgb_inv)) {
        err = "cannot invert sRGB colorant matrix";
        return nullptr;
    }
    std::shared_ptr<FastSrgbLinear> fast(new FastSrgbLinear());
    fast->m_ = mul3(srgb_inv, profile.colorant); // 源线性 RGB → sRGB 线性
    // TRC 解码表（每通道）：编码值 → 源线性值（double 表，避免 float 量化噪声）。
    for (int c = 0; c < 3; ++c) {
        fast->trc_lut_[static_cast<std::size_t>(c)].resize(static_cast<std::size_t>(kTrcSamples) +
                                                           1);
        for (int i = 0; i <= kTrcSamples; ++i) {
            const double x = static_cast<double>(i) / static_cast<double>(kTrcSamples);
            fast->trc_lut_[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                profile.curves[c].eval(x);
        }
    }
    // 标定（M10-W1-T2c）：解析矩阵路线与 lcms 的残差实测为**系统性相对比例**（~9e-5，随线性
    // 值成比例；来源 = lcms 内建 sRGB 色料矩阵/白点自适应的实现细节与硬编码标准常量差异），
    // 用 lcms 实测 6³ 样本做逐通道增益标定 → 线性最大误差从 ~4.3e-4 压到 ~1e-6 量级。
    {
        constexpr int kCalN = 10;
        constexpr int kCalSamples = kCalN * kCalN * kCalN;
        std::vector<float> enc(static_cast<std::size_t>(kCalSamples) * 3);
        int idx = 0;
        for (int r = 0; r < kCalN; ++r) {
            for (int g = 0; g < kCalN; ++g) {
                for (int b = 0; b < kCalN; ++b, ++idx) {
                    enc[static_cast<std::size_t>(idx) * 3 + 0] =
                        static_cast<float>(r) / static_cast<float>(kCalN - 1);
                    enc[static_cast<std::size_t>(idx) * 3 + 1] =
                        static_cast<float>(g) / static_cast<float>(kCalN - 1);
                    enc[static_cast<std::size_t>(idx) * 3 + 2] =
                        static_cast<float>(b) / static_cast<float>(kCalN - 1);
                }
            }
        }
        std::vector<float> exact_lin = enc; // 同一批样本经 lcms → sRGB 编码 → 精确 EOTF
        {
            OIIO::ImageSpec spec(kCalSamples, 1, 3, OIIO::TypeDesc::FLOAT);
            OIIO::ImageBuf buf(spec,
                               OIIO::image_span<float>(exact_lin.data(), 3,
                                                       static_cast<std::uint32_t>(kCalSamples), 1));
            std::vector<Warning> ignored;
            std::string xerr;
            const ColorOutcome co = ColorManager::instance().transform(
                buf, src_icc, /*src_is_gray=*/false, ColorTarget::SRGB, ColorOptions{}, &ignored,
                &xerr);
            if (!co.error.empty()) {
                err = "calibration probe failed: " + co.error;
                return nullptr;
            }
            if (buf.localpixels() != exact_lin.data() &&
                !buf.get_pixels(buf.roi(), OIIO::TypeDesc::FLOAT, exact_lin.data())) {
                err = "calibration probe readback failed";
                return nullptr;
            }
            for (float &v : exact_lin)
                v = srgb_eotf(v);
        }
        // 3×3 最小二乘校正：y ≈ K · t（t = 解析矩阵路径线性输出，未 clamp）；K 吸收
        // lcms 内建 sRGB 色料/白点定义与硬编码标准常量的端到端差异（只用域内样本）。
        double ata[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
        double aty[3][3] = {{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
        int used = 0;
        for (int s = 0; s < kCalSamples; ++s) {
            const float in[3] = {enc[static_cast<std::size_t>(s) * 3 + 0],
                                 enc[static_cast<std::size_t>(s) * 3 + 1],
                                 enc[static_cast<std::size_t>(s) * 3 + 2]};
            double d[3] = {0.0, 0.0, 0.0};
            for (int c = 0; c < 3; ++c) {
                const double x = static_cast<double>(std::clamp(in[c], 0.0f, 1.0f)) * kTrcSamples;
                const int i = std::min(static_cast<int>(x), kTrcSamples - 1);
                const double f = x - static_cast<double>(i);
                const std::vector<double> &lut = fast->trc_lut_[static_cast<std::size_t>(c)];
                d[c] =
                    lut[static_cast<std::size_t>(i)] +
                    f * (lut[static_cast<std::size_t>(i) + 1] - lut[static_cast<std::size_t>(i)]);
            }
            double t[3] = {0.0, 0.0, 0.0};
            bool in_domain = true;
            for (int c = 0; c < 3; ++c) {
                t[c] = fast->m_[3 * c + 0] * d[0] + fast->m_[3 * c + 1] * d[1] +
                       fast->m_[3 * c + 2] * d[2];
                const double y = exact_lin[static_cast<std::size_t>(s) * 3 + c];
                if (t[c] < 0.02 || t[c] > 0.98 || y < 0.02 || y > 0.98)
                    in_domain = false;
            }
            if (!in_domain)
                continue;
            ++used;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j)
                    ata[i][j] += t[i] * t[j];
                for (int c = 0; c < 3; ++c)
                    aty[c][i] += exact_lin[static_cast<std::size_t>(s) * 3 + c] * t[i];
            }
        }
        if (used < 8) {
            err = "insufficient calibration samples";
            return nullptr;
        }
        std::array<double, 9> ata_arr{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                ata_arr[3 * i + j] = ata[i][j];
        std::array<double, 9> ata_inv{};
        if (!invert3(ata_arr, ata_inv)) {
            err = "calibration matrix singular";
            return nullptr;
        }
        std::array<double, 9> k{};
        for (int c = 0; c < 3; ++c) {
            for (int j = 0; j < 3; ++j)
                k[3 * c + j] = ata_inv[3 * j + 0] * aty[c][0] + ata_inv[3 * j + 1] * aty[c][1] +
                               ata_inv[3 * j + 2] * aty[c][2];
            // 合理性护栏：对角增益应接近 1（防止标定样本退化导致颜色整体偏色）。
            if (!(k[3 * c + c] > 0.9 && k[3 * c + c] < 1.1)) {
                err = "implausible calibration gain";
                return nullptr;
            }
        }
        fast->m_ = mul3(k, fast->m_);
    }
    fast->ready_ = true;
    fast->build_ms_ = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    return fast;
}

void FastSrgbLinear::transform_pixel(float &r, float &g, float &b) const {
    if (!ready_)
        return;
    const auto decode = [this](int c, float v) -> double {
        const std::vector<double> &lut = trc_lut_[static_cast<std::size_t>(c)];
        const double x = static_cast<double>(std::clamp(v, 0.0f, 1.0f)) * kTrcSamples;
        int i = static_cast<int>(x);
        if (i >= kTrcSamples)
            return lut[static_cast<std::size_t>(kTrcSamples)];
        const double f = x - static_cast<double>(i);
        return lut[static_cast<std::size_t>(i)] +
               f * (lut[static_cast<std::size_t>(i) + 1] - lut[static_cast<std::size_t>(i)]);
    };
    const double lr = decode(0, r);
    const double lg = decode(1, g);
    const double lb = decode(2, b);
    // 源线性 RGB → sRGB 线性（D50 PCS 下两色料矩阵相乘）→ clamp [0,1]（精确路径 lcms 的
    // sRGB 编码 + 精确 EOTF 对线性值等价于 clamp；编码单调故越界语义一致）。
    r = static_cast<float>(std::clamp(m_[0] * lr + m_[1] * lg + m_[2] * lb, 0.0, 1.0));
    g = static_cast<float>(std::clamp(m_[3] * lr + m_[4] * lg + m_[5] * lb, 0.0, 1.0));
    b = static_cast<float>(std::clamp(m_[6] * lr + m_[7] * lg + m_[8] * lb, 0.0, 1.0));
}

void FastSrgbLinear::transform_rows(float *rgb, int width, int rows) const {
    if (!ready() || rgb == nullptr || width <= 0 || rows <= 0)
        return;
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(rows);
    for (std::size_t i = 0; i < n; ++i) {
        transform_pixel(rgb[3 * i + 0], rgb[3 * i + 1], rgb[3 * i + 2]);
    }
}

} // namespace pp
