// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/simd 单测（M4-T3 探测层 + M4-W5-T19 五对一致性）。
//
// Contract: docs/v0.3.0-design.md §11.2（运行期 cpu_has_avx2() 分派 + 5 对一致性判据）/
//           docs/m4-tasks.md §3 W5-T19（本任务出口：单测绿）。
// 框架：延续仓库既有口径 —— 手写断言，无第三方测试框架；失败逐条打印，
//       main() 返回失败条数（0 = 全绿）。
//
// 断言范围：
//   ① 编译期基线确认（本 TU 与 pp_core 同源，__AVX2__ 必须成立）；
//   ② cpu_has_avx2() 在本机（基线机）必须为 true + 幂等缓存；
//   ③ [W5-T19] 五对热路径的 avx2/ref 一致性（§11.2 判据，逐条）：
//      flatten / interleave / transpose 逐位相等、quantize 舍入一致、
//      downscale 容差 ≤1e-5；
//   ④ [W5-T19] 独立于 ref/avx2 对照的**语义**断言（防止两条实现"错得一样"）：
//      · 常量图在 downscale 后保持不变（权重归一化 ⇒ 直流增益 1）；
//      · flatten/interleave 的通道落位对着手算真值；
//      · quantize 对着 to_u8/to_u16 的原始表达式（独立重写）；
//      · downscale 与 OIIO `ImageBufAlgo::resize(lanczos3)` 的交叉对照（thumbs 原实现口径，
//        容差 1e-3 —— 只反映求和次序差异，实测上界见 printf 行）。
//
// 判据取"逐位相等"而不是放宽容差：ref 与 avx2 用同一算子序列（std::fma ↔ _mm256_fmadd_ps），
// 差异在数学上恒为 0 —— 容差不会替实现缺陷打掩护（见 simd.h 的位精确口径块）。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include <OpenImageIO/imagebuf.h>
#include <OpenImageIO/imagebufalgo.h>

#include "core/simd/simd.h"

#if !defined(__AVX2__)
#error "test_simd 必须带 AVX2 基线编译（§11.1）：__AVX2__ 未定义说明 flags 链断了。"
#endif

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

// 确定性伪随机（xorshift32；不依赖 <random> 的实现差异，用例可复现）。
struct Rng {
    std::uint32_t s;
    explicit Rng(std::uint32_t seed) : s(seed ? seed : 0x9e3779b9u) {}
    std::uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float unit() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    // [-0.5, 1.5) —— 覆盖越界值（flatten/quantize 的 clamp 面）
    float wide() { return unit() * 2.0f - 0.5f; }
};

// 每 97 个样本插入一个特殊值（NaN / ±inf / 0 / 1 / -0.0f）——clamp 与 NaN 判据的边界。
float with_specials(Rng &r, std::size_t i) {
    float v = r.wide();
    if (i % 97 == 3)
        v = std::numeric_limits<float>::quiet_NaN();
    else if (i % 97 == 11)
        v = std::numeric_limits<float>::infinity();
    else if (i % 97 == 19)
        v = -std::numeric_limits<float>::infinity();
    else if (i % 97 == 29)
        v = 0.0f;
    else if (i % 97 == 41)
        v = 1.0f;
    else if (i % 97 == 53)
        v = -0.0f;
    else if (i % 97 == 67)
        v = 1.0f + r.unit();
    return v;
}

std::vector<float> random_floats(Rng &r, std::size_t n, bool specials) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = specials ? with_specials(r, i) : r.wide();
    return v;
}

bool bits_equal(const std::vector<float> &a, const std::vector<float> &b, double *max_diff) {
    if (a.size() != b.size())
        return false;
    bool same = true;
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0)
            same = false;
        const double d = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (!(d <= worst)) // NaN 也判为"不等"（worst 保持有限）
            worst = std::isnan(d) ? 1e30 : d;
    }
    if (max_diff)
        *max_diff = worst;
    return same;
}

bool u8_equal(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

bool u16_equal(const std::vector<std::uint16_t> &a, const std::vector<std::uint16_t> &b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(std::uint16_t)) == 0;
}

// ---- ③-1 flatten：逐位相等（含原地缩通道的别名形态）----
void test_flatten() {
    const int channels[2] = {2, 4};
    const std::size_t sizes[6] = {0, 1, 7, 8, 9, 257};
    for (int ch : channels) {
        for (std::size_t npix : sizes) {
            for (float bg : {0.0f, 0.25f, 1.0f, -0.5f, 2.0f}) {
                Rng r(0x1000u + static_cast<std::uint32_t>(npix) * 31u +
                      static_cast<std::uint32_t>(ch));
                const std::vector<float> src = random_floats(r, npix * ch, true);
                const int out_ch = (ch == 2) ? 1 : 3;
                std::vector<float> a(npix * out_ch, -7.0f), b(npix * out_ch, -7.0f);
                pp::simd::flatten_ref(src.data(), a.data(), npix, ch, bg);
                pp::simd::flatten_avx2(src.data(), b.data(), npix, ch, bg);
                check(bits_equal(a, b, nullptr), "flatten/bitwise",
                      "npix=" + std::to_string(npix) + " ch=" + std::to_string(ch) +
                          " bg=" + std::to_string(bg));
                // 原地（src == dst）：§4.3 的内存纪律形态，必须与上面逐位一致
                std::vector<float> inplace = src;
                pp::simd::flatten_avx2(inplace.data(), inplace.data(), npix, ch, bg);
                inplace.resize(npix * static_cast<std::size_t>(out_ch));
                check(bits_equal(a, inplace, nullptr), "flatten/inplace-alias",
                      "npix=" + std::to_string(npix) + " ch=" + std::to_string(ch) +
                          " bg=" + std::to_string(bg));
            }
        }
    }
    // ④ 手算真值：单像素 RGBA，a = 0.25，bg = 0.5 → c*0.25 + 0.5*0.75
    {
        const float src[4] = {1.0f, 0.4f, 0.0f, 0.25f};
        float out[3] = {0, 0, 0};
        pp::simd::flatten(src, out, 1, 4, 0.5f);
        const float e0 = std::fma(1.0f, 0.25f, 0.5f * 0.75f);
        const float e1 = std::fma(0.4f, 0.25f, 0.5f * 0.75f);
        const float e2 = std::fma(0.0f, 0.25f, 0.5f * 0.75f);
        check(out[0] == e0 && out[1] == e1 && out[2] == e2, "flatten/known-truth",
              "got [" + std::to_string(out[0]) + "," + std::to_string(out[1]) + "," +
                  std::to_string(out[2]) + "]");
        // alpha clamp：a = 3 → 视作 1（保留源色）；a = -1 → 视作 0（纯底色）
        const float s2[4] = {0.2f, 0.2f, 0.2f, 3.0f};
        float o2[3] = {0, 0, 0};
        pp::simd::flatten(s2, o2, 1, 4, 0.5f);
        check(o2[0] == 0.2f, "flatten/alpha-clamp-hi", "got " + std::to_string(o2[0]));
        const float s3[4] = {0.2f, 0.2f, 0.2f, -1.0f};
        float o3[3] = {0, 0, 0};
        pp::simd::flatten(s3, o3, 1, 4, 0.5f);
        check(o3[0] == 0.5f, "flatten/alpha-clamp-lo", "got " + std::to_string(o3[0]));
    }
}

// ---- ③-2 quantize：舍入一致 + 对照独立重写的 to_u8/to_u16 表达式 ----
void test_quantize() {
    const std::size_t n = 4096;
    Rng r(0x2000u);
    const std::vector<float> v = random_floats(r, n, true);
    std::vector<std::uint8_t> a8(n, 0), b8(n, 0), ref8(n, 0);
    pp::simd::quantize8_ref(v.data(), a8.data(), n);
    pp::simd::quantize8_avx2(v.data(), b8.data(), n);
    check(u8_equal(a8, b8), "quantize8/bitwise", "avx2 与 ref 不一致");
    // 独立重写（不调用 simd 模块），逐字复刻 enc_jpegli 的 to_u8
    for (std::size_t i = 0; i < n; ++i) {
        const float x = v[i];
        ref8[i] = (!(x > 0.0f)) ? std::uint8_t{0}
                                : (x >= 1.0f ? std::uint8_t{255}
                                             : static_cast<std::uint8_t>(x * 255.0f + 0.5f));
    }
    check(u8_equal(a8, ref8), "quantize8/matches-to_u8", "与 to_u8 原表达式不一致");

    for (int maxv : {255, 1023, 4095, 65535}) {
        std::vector<std::uint16_t> a16(n, 0), b16(n, 0);
        pp::simd::quantize16_ref(v.data(), a16.data(), n, maxv);
        pp::simd::quantize16_avx2(v.data(), b16.data(), n, maxv);
        check(u16_equal(a16, b16), "quantize16/bitwise",
              "maxv=" + std::to_string(maxv) + " avx2 与 ref 不一致");
        for (std::size_t i = 0; i < n; ++i) {
            const float x = v[i];
            const std::uint16_t want =
                (!(x > 0.0f))
                    ? std::uint16_t{0}
                    : (x >= 1.0f ? static_cast<std::uint16_t>(maxv)
                                 : static_cast<std::uint16_t>(x * static_cast<float>(maxv) + 0.5f));
            if (a16[i] != want) {
                check(false, "quantize16/matches-to_u16",
                      "maxv=" + std::to_string(maxv) + " i=" + std::to_string(i) +
                          " got=" + std::to_string(a16[i]) + " want=" + std::to_string(want));
                break;
            }
        }
    }
    // 边界样本：0 / 1 / 恰好半步（k + 0.5）/ 负 / NaN / ±inf
    {
        const float nan_v = std::numeric_limits<float>::quiet_NaN();
        const float inf_v = std::numeric_limits<float>::infinity();
        const float edge[12] = {
            0.0f,    1.0f,    0.5f / 255.0f, 1.5f / 255.0f, 254.5f / 255.0f, 255.0f / 255.0f,
            -0.001f, 1.0001f, nan_v,         inf_v,         -inf_v,          0.5f};
        std::vector<std::uint8_t> a8e(12, 0), b8e(12, 0);
        pp::simd::quantize8_ref(edge, a8e.data(), 12);
        pp::simd::quantize8_avx2(edge, b8e.data(), 12);
        check(u8_equal(a8e, b8e), "quantize8/edge-bitwise", "边界样本不一致");
        check(a8e[0] == 0 && a8e[1] == 255 && a8e[6] == 0 && a8e[7] == 255 && a8e[8] == 0 &&
                  a8e[9] == 255 && a8e[10] == 0,
              "quantize8/edge-values",
              "0/1/负/NaN/+inf/-inf 判据错: " + std::to_string(a8e[0]) + "," +
                  std::to_string(a8e[1]) + "," + std::to_string(a8e[6]) + "," +
                  std::to_string(a8e[7]) + "," + std::to_string(a8e[8]) + "," +
                  std::to_string(a8e[9]) + "," + std::to_string(a8e[10]));
    }
}

// ---- ③-3 transpose：逐位相等（含非 8 对齐边缘）----
void test_transpose() {
    const int sizes[6][2] = {{1, 1}, {7, 9}, {8, 8}, {13, 5}, {16, 24}, {37, 41}};
    for (const auto &sz : sizes) {
        const int sw = sz[0], sh = sz[1];
        Rng r(0x3000u + static_cast<std::uint32_t>(sw * 100 + sh));
        const std::vector<float> src = random_floats(r, static_cast<std::size_t>(sw) * sh, true);
        std::vector<float> a(static_cast<std::size_t>(sw) * sh, -1.0f);
        std::vector<float> b(static_cast<std::size_t>(sw) * sh, -2.0f);
        pp::simd::transpose8_ref(src.data(), sw, sh, a.data());
        pp::simd::transpose8_avx2(src.data(), sw, sh, b.data());
        check(bits_equal(a, b, nullptr), "transpose/bitwise",
              std::to_string(sw) + "x" + std::to_string(sh));
        // 索引真值（含 NaN 也要逐位搬对）
        bool ok = true;
        for (int y = 0; y < sh && ok; ++y) {
            for (int x = 0; x < sw && ok; ++x) {
                const std::size_t si = static_cast<std::size_t>(y) * sw + x;
                const std::size_t di = static_cast<std::size_t>(x) * sh + y;
                if (std::memcmp(&b[di], &src[si], sizeof(float)) != 0)
                    ok = false;
            }
        }
        check(ok, "transpose/index-truth", std::to_string(sw) + "x" + std::to_string(sh));
    }
}

// ---- ③-4 downscale：容差 ≤1e-5（ref vs avx2）+ §11.2 语义断言 + OIIO 交叉对照 ----
void test_downscale() {
    struct Case {
        int sw, sh, sc, dw, dh;
    };
    const Case cases[] = {
        {37, 23, 3, 13, 8},  {64, 64, 1, 17, 17},   {48, 31, 4, 48, 31}, // 同尺寸（权重退化为恒等）
        {64, 64, 3, 21, 21}, {200, 150, 3, 64, 48}, {33, 27, 2, 41, 33}, // 末例为放大
    };
    for (const Case &c : cases) {
        Rng r(0x4000u + static_cast<std::uint32_t>(c.sw * 7 + c.sh * 13 + c.sc));
        const std::vector<float> src =
            random_floats(r, static_cast<std::size_t>(c.sw) * c.sh * c.sc, false);
        const std::size_t dn = static_cast<std::size_t>(c.dw) * c.dh * c.sc;
        std::vector<float> a(dn, -3.0f), b(dn, -4.0f);
        pp::simd::downscale_ref(src.data(), c.sw, c.sh, c.sc, a.data(), c.dw, c.dh, c.sc);
        pp::simd::downscale_avx2(src.data(), c.sw, c.sh, c.sc, b.data(), c.dw, c.dh, c.sc);
        double max_diff = 0.0;
        const bool same = bits_equal(a, b, &max_diff);
        check(max_diff <= 1e-5, "downscale/tolerance-1e-5",
              std::to_string(c.sw) + "x" + std::to_string(c.sh) + "→" + std::to_string(c.dw) + "x" +
                  std::to_string(c.dh) + " max_diff=" + std::to_string(max_diff));
        check(same, "downscale/bitwise",
              "ref 与 avx2 应逐位相等（同算子序列）：" + std::to_string(c.sw) + "→" +
                  std::to_string(c.dw));
    }
    // ④ 语义断言（独立于 ref/avx2 对照）：常量图 → 常量（权重归一化 ⇒ 直流增益 1）
    {
        const int sw = 96, sh = 72, dw = 29, dh = 21;
        std::vector<float> src(static_cast<std::size_t>(sw) * sh * 3, 0.25f);
        std::vector<float> out(static_cast<std::size_t>(dw) * dh * 3, -1.0f);
        pp::simd::downscale(src.data(), sw, sh, 3, out.data(), dw, dh, 3);
        double worst = 0.0;
        for (float v : out)
            worst = std::max(worst, std::fabs(static_cast<double>(v) - 0.25));
        check(worst <= 1e-6, "downscale/constant-preserved",
              "常量图降采样后偏离 0.25（权重未归一化或支撑错）: " + std::to_string(worst));
        // 同尺寸（ratio = 1）：权重为冲击核（lanczos3 的整数抽头只有中心非零）⇒ 近似恒等；
        // NDC 的 (x+0.5)/n×n 往返有舍入，故不是逐位恒等（OIIO 的同一公式亦然）。
        std::vector<float> ident(static_cast<std::size_t>(sw) * sh * 3, 0.0f);
        pp::simd::downscale(src.data(), sw, sh, 3, ident.data(), sw, sh, 3);
        double worst_ident = 0.0;
        for (std::size_t i = 0; i < ident.size(); ++i)
            worst_ident = std::max(worst_ident, std::fabs(static_cast<double>(ident[i]) - src[i]));
        check(worst_ident <= 1e-5, "downscale/identity-at-ratio-1",
              "ratio=1 的最大偏差 " + std::to_string(worst_ident) + " > 1e-5");
    }
    // ④ 与 OIIO `ImageBufAlgo::resize(lanczos3)` 交叉对照（thumbs 的被替换实现口径）。
    //    差异只应来自求和次序；阈值 1e-3（实测上界打印在下方 OK 行里）。
    {
        const int sw = 200, sh = 150, sc = 3, dw = 64, dh = 48;
        Rng r(0x5000u);
        const std::vector<float> src =
            random_floats(r, static_cast<std::size_t>(sw) * sh * sc, false);
        const std::size_t dn = static_cast<std::size_t>(dw) * dh * sc;

        OIIO::ImageBuf sbuf(OIIO::ImageSpec(sw, sh, sc, OIIO::TypeFloat));
        sbuf.set_pixels(OIIO::ROI(0, sw, 0, sh), OIIO::TypeFloat, src.data());
        OIIO::ImageBuf obuf;
        const OIIO::ImageBufAlgo::KWArgs opts{{OIIO::ParamValue("filtername", "lanczos3")},
                                              {OIIO::ParamValue("dst_datatype", "float")}};
        const bool ok_oiio =
            OIIO::ImageBufAlgo::resize(obuf, sbuf, opts, OIIO::ROI(0, dw, 0, dh), /*nthreads=*/0);
        check(ok_oiio, "downscale/oiio-resize-ran", "IBA::resize 失败: " + obuf.geterror());
        if (ok_oiio) {
            std::vector<float> oiio(dn, 0.0f);
            std::vector<float> mine(dn, 0.0f);
            obuf.get_pixels(OIIO::ROI(0, dw, 0, dh), OIIO::TypeFloat, oiio.data());
            pp::simd::downscale(src.data(), sw, sh, sc, mine.data(), dw, dh, sc);
            double worst = 0.0;
            for (std::size_t i = 0; i < dn; ++i)
                worst = std::max(worst, std::fabs(static_cast<double>(oiio[i]) - mine[i]));
            check(worst <= 1e-3, "downscale/oiio-lanczos3-equivalent",
                  "与 OIIO resize(lanczos3) 的最大偏差 " + std::to_string(worst) + " > 1e-3");
            std::printf(
                "test_simd: downscale vs OIIO resize(lanczos3) max|Δ| = %.3e（%dx%d→%dx%d）\n",
                worst, sw, sh, dw, dh);
        }
    }
}

// ---- ③-5 interleave：逐位相等（全部 (nch, out_ch) 组合）+ 通道落位真值 ----
void test_interleave() {
    const int chans[4] = {1, 2, 3, 4};
    const int outs[3] = {1, 3, 4};
    for (int nch : chans) {
        for (int out_ch : outs) {
            for (std::size_t npix :
                 {std::size_t{1}, std::size_t{7}, std::size_t{8}, std::size_t{33}}) {
                Rng r(0x6000u + static_cast<std::uint32_t>(nch * 100 + out_ch * 10 + npix));
                const std::vector<float> src = random_floats(r, npix * nch, true);
                std::vector<float> a(npix * out_ch, -9.0f), b(npix * out_ch, -8.0f);
                pp::simd::interleave_ref(src.data(), nch, a.data(), out_ch, npix);
                pp::simd::interleave_avx2(src.data(), nch, b.data(), out_ch, npix);
                check(bits_equal(a, b, nullptr), "interleave/bitwise",
                      "nch=" + std::to_string(nch) + " out_ch=" + std::to_string(out_ch) +
                          " npix=" + std::to_string(npix));
            }
        }
    }
    // ④ 落位真值：nch=4 → out_ch=3（丢 alpha）；nch=1 → out_ch=4（复制 + 不透明）
    {
        const float s[8] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
        float q[6] = {0, 0, 0, 0, 0, 0};
        pp::simd::interleave(s, 4, q, 3, 2);
        check(q[0] == 0.1f && q[1] == 0.2f && q[2] == 0.3f && q[3] == 0.5f && q[4] == 0.6f &&
                  q[5] == 0.7f,
              "interleave/drop-alpha", "RGBA→RGB 落位错");
        const float g[2] = {0.25f, 0.5f};
        float r4[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        pp::simd::interleave(g, 1, r4, 4, 2);
        check(r4[0] == 0.25f && r4[1] == 0.25f && r4[2] == 0.25f && r4[3] == 1.0f &&
                  r4[4] == 0.5f && r4[5] == 0.5f && r4[6] == 0.5f && r4[7] == 1.0f,
              "interleave/gray-to-rgba", "灰度→RGBA 落位错");
        const float ga[4] = {0.25f, 0.5f, 0.75f, 0.9f}; // 2 像素：gray+alpha
        float g3[8] = {0, 0, 0, 0, 0, 0, 0, 0};
        pp::simd::interleave(ga, 2, g3, 4, 2);
        check(g3[0] == 0.25f && g3[1] == 0.25f && g3[2] == 0.25f && g3[3] == 0.5f &&
                  g3[4] == 0.75f && g3[5] == 0.75f && g3[6] == 0.75f && g3[7] == 0.9f,
              "interleave/gray-alpha-to-rgba", "灰度+alpha→RGBA 落位错");
        float g1[2] = {0, 0};
        pp::simd::interleave(ga, 2, g1, 1, 2);
        check(g1[0] == 0.25f && g1[1] == 0.75f, "interleave/gray-alpha-to-gray",
              "灰度+alpha→灰度 落位错");
    }
}

} // namespace

int main() {
    // ① 编译期基线：能编译到这里就说明 __AVX2__ 成立（上面的 #error 是闸门）。
    std::printf("test_simd: 编译期基线 __AVX2__ = 已定义（x86-64-v3 / /arch:AVX2）\n");

    // ② 本机支持：基线机器（i7-14700K，Raptor Lake）必须报告 AVX2。
    const bool has_avx2 = pp::simd::cpu_has_avx2();
    check(has_avx2, "cpu_has_avx2/this-machine",
          "本机报告无 AVX2 —— 基线产物无法安全运行（CPUID leaf7 EBX[5] / XCR0 检查）");
    std::printf("test_simd: cpu_has_avx2() = %s\n", has_avx2 ? "true" : "false");

    // ③ 幂等：缓存路径与首次探测必须给同一答案（排除静态量初始化竞态/脏读）。
    for (int i = 0; i < 3; ++i) {
        check(pp::simd::cpu_has_avx2() == has_avx2, "cpu_has_avx2/idempotent",
              "第 " + std::to_string(i + 1) + " 次调用与首次不一致");
    }

    // ③/④ 五对热路径一致性 + 语义断言（W5-T19）
    test_flatten();
    test_quantize();
    test_transpose();
    test_downscale();
    test_interleave();

    if (g_failed == 0) {
        std::printf("test_simd: OK (探测层 + 5 对 avx2/ref 一致性；W5-T19)\n");
        return 0;
    }
    std::printf("test_simd: FAILED (%d)\n", g_failed);
    return g_failed;
}
