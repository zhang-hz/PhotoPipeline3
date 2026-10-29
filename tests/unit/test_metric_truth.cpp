// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: **评分真实性**（M8-W4，用户点名「禁止默认造假」的机械化回归）。
//
// 与 test_quality_metric.cpp（C1 指标核心算法/边界/并发）的分工：
//   * test_quality_metric = 「指标算得对不对」；
//   * 本文件 = 「评分口径是否真实」——把 C9 的参考/失真语义钉成不变量，专门防
//     「参考被降采样 / 参考取自失真结果 / 444 与 420 打同一分」这类**假评分**回归。
//
// 覆盖（全部确定性、无外部文件、无编码器依赖；断言只增不减）：
//   1. 恒等 = 100.0（逐样本相同，含 8×8 边界）；
//   2. 有损分数随质量单调：同一参考下量化级 2→128，分数严格递增且全部 < 100；
//   3. **色度抽样必须被扣分**：参考保留全色度、失真做 4:2:0 色度抽样（亮度不动）⇒ 分数
//      显著低于 100（≥5 分），且低于「亮度量化但色度保留」的失真 ⇒ 444 在指标语义上
//      必然优于 420（同亮度质量下）；
//   4. **假参考必须被揭穿**：若把参考本身也做 4:2:0 抽样，同一失真会变成 100.0；真参考下
//      该失真 < 95 ⇒ 参考来源不同导致 ≥5 分差异（这就是「参考用 420 量化后结果输入 =
//      没有意义」的机械化表述；若管线把参考换成降采样结果，本断言会失败）；
//   5. 参考/失真互换不等：SSIMULACRA 2 的 artifact/blur 项权重不对称 ⇒ 模糊失真的两个
//      方向分数不同（若实现意外变成对称，本断言会失败，提醒重新审视指标接入）；
//   6. 确定性：同输入两次 compute 分数逐位相同。
//
// 失败打印 "FAIL <case>: <detail>"，main 返回失败数（与既有单测同体例）。
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/metric/metric.h"

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// ---------------------------------------------------------------- 像素工具
// 输入口径与 metric.h 一致：**sRGB 线性**交错 float RGB，[0,1]。
struct Image {
    int w = 0;
    int h = 0;
    std::vector<float> px; // interleaved RGB
};

uint32_t NextRand(uint32_t *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

float RandUnit(uint32_t *state) { return static_cast<float>(NextRand(state) >> 8) / 16777216.0f; }

float SrgbEncode(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    return (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float SrgbDecode(float encoded) {
    const float c = std::clamp(encoded, 0.0f, 1.0f);
    return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// 确定性「纯色度」极端图（Lead 实测口径：color.png 那种红蓝高频棋盘）：
// 2×2 块在两种**等亮度、反色度**的颜色间交替（线性域 R=(0.8,0,0) 与 G=(0,0.407,0)，
// 0.299×0.8 = 0.587×0.407 ≈ 0.239）⇒ 亮度近似恒定、色度在 Nyquist 交替；4:2:0 色度抽样
// 会把两种颜色平均成灰，是「色度降采样」的最纯考验（不含亮度混淆）。
Image MakeChromaOnlyImage(int w, int h) {
    Image img;
    img.w = w;
    img.h = h;
    img.px.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                   static_cast<std::size_t>(x)) *
                                  3;
            const bool even = (((x / 2) + (y / 2)) & 1) == 0;
            if (even) {
                img.px[i + 0] = 0.80f;
                img.px[i + 1] = 0.0f;
                img.px[i + 2] = 0.0f;
            } else {
                img.px[i + 0] = 0.0f;
                img.px[i + 1] = 0.407f;
                img.px[i + 2] = 0.0f;
            }
        }
    }
    return img;
}

// 确定性「高色度细节 + 亮度渐变」图：2px 周期红/绿棋盘是色度 Nyquist，4:2:0 抽样必然损伤；
// 亮度用平滑渐变，保证亮度通道本身接近无损（公平对比 444/420 的差异来源）。
Image MakeChromaDetailImage(int w, int h) {
    Image img;
    img.w = w;
    img.h = h;
    img.px.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    uint32_t state = 0xC0FFEEu;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                   static_cast<std::size_t>(x)) *
                                  3;
            const bool even = (((x / 2) + (y / 2)) & 1) == 0;
            const float ramp = static_cast<float>(x) / static_cast<float>(w - 1);
            const float jitter = (RandUnit(&state) - 0.5f) * (1.0f / 255.0f);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (even) {
                r = 0.85f;
                g = 0.05f;
                b = 0.55f;
            } else {
                r = 0.05f;
                g = 0.85f;
                b = 0.20f;
            }
            img.px[i + 0] = std::clamp(r * (0.35f + 0.65f * ramp) + jitter, 0.0f, 1.0f);
            img.px[i + 1] = std::clamp(g * (0.35f + 0.65f * ramp) + jitter, 0.0f, 1.0f);
            img.px[i + 2] = std::clamp(b * (0.35f + 0.65f * ramp) + jitter, 0.0f, 1.0f);
        }
    }
    return img;
}

// 亮度+色度统一量化到 levels 级（编码域量化，模拟有损编码的量化损失）。
Image QuantizeImage(const Image &src, int levels) {
    Image out = src;
    const float q = static_cast<float>(levels - 1);
    for (float &v : out.px) {
        const float e = SrgbEncode(v);
        v = SrgbDecode(std::round(e * q) / q);
    }
    return out;
}

// RGB → BT.601 全范围 YCbCr（与 enc_heif 同型；数值为 8-bit 编码域）。
struct YCbCr {
    std::vector<float> y, cb, cr;
    int w = 0, h = 0;
};

YCbCr ToYCbCr(const Image &img) {
    YCbCr out;
    out.w = img.w;
    out.h = img.h;
    const std::size_t n = static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h);
    out.y.resize(n);
    out.cb.resize(n);
    out.cr.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float r = SrgbEncode(img.px[3 * i + 0]) * 255.0f;
        const float g = SrgbEncode(img.px[3 * i + 1]) * 255.0f;
        const float b = SrgbEncode(img.px[3 * i + 2]) * 255.0f;
        out.y[i] = 0.299f * r + 0.587f * g + 0.114f * b;
        out.cb[i] = -0.168736f * r - 0.331264f * g + 0.5f * b + 128.0f;
        out.cr[i] = 0.5f * r - 0.418688f * g - 0.081312f * b + 128.0f;
    }
    return out;
}

Image FromYCbCr(const YCbCr &in) {
    Image out;
    out.w = in.w;
    out.h = in.h;
    const std::size_t n = static_cast<std::size_t>(in.w) * static_cast<std::size_t>(in.h);
    out.px.resize(n * 3);
    for (std::size_t i = 0; i < n; ++i) {
        const float cb = in.cb[i] - 128.0f;
        const float cr = in.cr[i] - 128.0f;
        const float y = in.y[i];
        const float r = y + 1.402f * cr;
        const float g = y - 0.344136f * cb - 0.714136f * cr;
        const float b = y + 1.772f * cb;
        out.px[3 * i + 0] = SrgbDecode(std::clamp(r, 0.0f, 255.0f) / 255.0f);
        out.px[3 * i + 1] = SrgbDecode(std::clamp(g, 0.0f, 255.0f) / 255.0f);
        out.px[3 * i + 2] = SrgbDecode(std::clamp(b, 0.0f, 255.0f) / 255.0f);
    }
    return out;
}

// 模拟 4:2:0：Cb/Cr 2×2 盒下采样 + 最近邻上采样（亮度/色度边界同型；亮度不动）。
// quant_levels > 0 时再对抽样后的色度做粗量化（模拟 420 码流里色度平面的量化+块效应——
// 真实 HEIF 420 的分数损失主要来自这项复合失真，而不是纯降采样；见 docs/m8-metric-truth.md）。
Image Chroma420Roundtrip(const Image &src, int quant_levels = 0) {
    YCbCr in = ToYCbCr(src);
    auto down_up = [&in, quant_levels](std::vector<float> *plane) {
        const int w = in.w;
        const int h = in.h;
        const int cw = (w + 1) / 2;
        const int ch = (h + 1) / 2;
        std::vector<float> small(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch), 0.0f);
        for (int by = 0; by < ch; ++by) {
            for (int bx = 0; bx < cw; ++bx) {
                float sum = 0.0f;
                int count = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    const int y = 2 * by + dy;
                    if (y >= h)
                        continue;
                    for (int dx = 0; dx < 2; ++dx) {
                        const int x = 2 * bx + dx;
                        if (x >= w)
                            continue;
                        sum += (*plane)[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(x)];
                        ++count;
                    }
                }
                small[static_cast<std::size_t>(by) * static_cast<std::size_t>(cw) +
                      static_cast<std::size_t>(bx)] = sum / static_cast<float>(count);
            }
        }
        if (quant_levels > 1) {
            const float step = 255.0f / static_cast<float>(quant_levels - 1);
            for (float &v : small) {
                v = std::round(std::clamp(v, 0.0f, 255.0f) / step) * step;
            }
        }
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                (*plane)[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                         static_cast<std::size_t>(x)] =
                    small[static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(cw) +
                          static_cast<std::size_t>(x / 2)];
            }
        }
    };
    down_up(&in.cb);
    down_up(&in.cr);
    return FromYCbCr(in);
}

// 3×3 盒式模糊两遍（对线性值；模拟「清晰/模糊」这一非对称类型失真）。
Image BlurImage(const Image &src, int radius) {
    Image out = src;
    for (int pass = 0; pass < 2; ++pass) {
        Image tmp = out;
        for (int y = 0; y < out.h; ++y) {
            for (int x = 0; x < out.w; ++x) {
                float sum[3] = {0.0f, 0.0f, 0.0f};
                int count = 0;
                for (int dy = -radius; dy <= radius; ++dy) {
                    const int yy = y + dy;
                    if (yy < 0 || yy >= out.h)
                        continue;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int xx = x + dx;
                        if (xx < 0 || xx >= out.w)
                            continue;
                        const std::size_t i =
                            (static_cast<std::size_t>(yy) * static_cast<std::size_t>(out.w) +
                             static_cast<std::size_t>(xx)) *
                            3;
                        for (int c = 0; c < 3; ++c)
                            sum[c] += out.px[i + static_cast<std::size_t>(c)];
                        ++count;
                    }
                }
                const std::size_t i =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.w) +
                     static_cast<std::size_t>(x)) *
                    3;
                for (int c = 0; c < 3; ++c)
                    tmp.px[i + static_cast<std::size_t>(c)] = sum[c] / static_cast<float>(count);
            }
        }
        out = tmp;
    }
    return out;
}

pp::metric::FrameSource SourceOf(const Image &img) {
    pp::metric::FrameSource src;
    src.width = img.w;
    src.height = img.h;
    src.row = [&img](int y, float *rgb) {
        const std::size_t offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) * 3;
        std::memcpy(rgb, img.px.data() + offset,
                    static_cast<std::size_t>(img.w) * 3 * sizeof(float));
    };
    return src;
}

double Score(const Image &ref, const Image &dist, const std::string &case_name) {
    const pp::metric::Result r = pp::metric::compute(SourceOf(ref), SourceOf(dist));
    check(r.state == pp::metric::State::Ok, case_name + "/state",
          std::string("应返回 Ok，得到 ") + pp::metric::state_name(r.state) + " note=" + r.note);
    return r.state == pp::metric::State::Ok ? r.score : -999.0;
}

// ---------------------------------------------------------------- 断言
void TestIdentity() {
    const int dims[][2] = {{8, 8}, {64, 48}, {129, 127}};
    for (const auto &d : dims) {
        const std::string c = "identity/" + std::to_string(d[0]) + "x" + std::to_string(d[1]);
        const Image img = MakeChromaDetailImage(d[0], d[1]);
        const double s = Score(img, img, c);
        check(s == 100.0, c, "自比应恰为 100.0，得到 " + num(s));
    }
}

void TestQualityLadderMonotonic() {
    const int w = 96;
    const int h = 64;
    const Image ref = QuantizeImage(MakeChromaDetailImage(w, h), 256);
    double previous = -1.0e300; // 上一级哨兵（levels=2 的分数可为负，不能拿 -1 当上一级）
    for (const int levels : {2, 4, 8, 16, 32, 64, 128}) {
        const Image dist = QuantizeImage(ref, levels);
        const std::string c = "ladder/levels=" + std::to_string(levels);
        const double s = Score(ref, dist, c);
        check(s < 100.0, c, "有损量化必须 < 100，得到 " + num(s));
        check(s > previous, c,
              "分数应随量化级增大单调递增：上一级 " + num(previous) + " -> 本级 " + num(s));
        std::printf("MEASURE ladder levels=%d score=%.6f\n", levels, s);
        previous = s;
    }
}

// C9 真值语义：444（色度保留）在指标语义上必然优于 420（色度抽样）——用纯色度极端图，
// 排除亮度混淆。注意（如实登记）：SSIMULACRA 2 对**纯色度降采样**的惩罚不算大（官方实现
// 在 XYB 里给色度通道的中高频权重有限），真正大的分差来自 420 码流里色度**量化+块效应**
// 的复合失真（见 docs/m8-metric-truth.md 的 encoder-level 证据：同一 420 请求下
// 解码色度 MAE 3.28、444 为 3.17，但伪影结构性更强 ⇒ 71.55 vs 57.41）。
void TestChromaSamplingIsPunished() {
    const int w = 128;
    const int h = 128;
    const Image ref = MakeChromaOnlyImage(w, h);
    // 420 复合色度失真 = 4:2:0 抽样 + 粗量化（8 级）；亮度不动，排除亮度质量混淆。
    const Image chroma420 = Chroma420Roundtrip(ref, 8);
    const double lossy_420 = Score(ref, chroma420, "chroma420/score");
    const double identity = Score(ref, ref, "chroma420/identity");
    check(identity == 100.0, "chroma420/identity", "同一输入应为 100.0，得到 " + num(identity));
    // 色度极端内容 + 420 复合失真 ⇒ 必须显著扣分（<90 = 至少 10 分），
    // 这是「444 与 420 不应打同一分」的指标级下限。
    check(lossy_420 < 90.0, "chroma420/score",
          "4:2:0 复合色度失真应显著扣分（<90），得到 " + num(lossy_420));
    std::printf("MEASURE chroma420 chroma_only_score=%.6f\n", lossy_420);
}

// 假参考反证（用户点名的「禁止默认造假」机械回证）：把参考也做 4:2:0 复合色度失真后，
// 同一失真立刻变成 100.0；真参考下该失真 <90 ⇒ 参考来源不同导致 ≥10 分差异。若管线把
// 参考换成降采样/产物结果，本断言会失败。
void TestFakeReferenceIsDetectable() {
    const int w = 128;
    const int h = 128;
    const Image ref = MakeChromaOnlyImage(w, h);
    const Image dist = Chroma420Roundtrip(ref, 8);
    const Image fake_ref = Chroma420Roundtrip(ref, 8); // 与失真逐样本相同（假参考极端形态）
    const double true_score = Score(ref, dist, "fake-ref/true");
    const double fake_score = Score(fake_ref, dist, "fake-ref/fake");
    check(fake_score == 100.0, "fake-ref/fake",
          "假参考与失真逐样本相同应得 100.0，得到 " + num(fake_score));
    check(true_score < 90.0, "fake-ref/true",
          "真参考下 420 复合失真应显著扣分（<90），得到 " + num(true_score));
    check(true_score < fake_score - 5.0, "fake-ref/inflation",
          "真参考下 420 失真必须比假参考低 ≥5 分：true=" + num(true_score) +
              " fake=" + num(fake_score));
    std::printf("MEASURE fake_ref true=%.6f fake=%.6f inflation=%.6f\n", true_score, fake_score,
                fake_score - true_score);
}

// SSIMULACRA 2 的 artifact/blur 项权重不对称 ⇒ 参考/失真互换分数不同（方向性真值）。
void TestReferenceDirectionMatters() {
    const int w = 96;
    const int h = 80;
    const Image ref = MakeChromaDetailImage(w, h);
    const Image blurred = BlurImage(ref, 2);
    const double forward = Score(ref, blurred, "direction/forward"); // 清晰参考 vs 模糊失真
    const double reverse = Score(blurred, ref, "direction/reverse"); // 模糊参考 vs 清晰失真
    check(forward != reverse, "direction/asymmetry",
          "参考/失真互换分数不应相等：forward=" + num(forward) + " reverse=" + num(reverse));
    check(std::abs(forward - reverse) > 0.01, "direction/margin",
          "方向差异应可观测（>0.01 分）：Δ=" + num(std::abs(forward - reverse)));
    std::printf("MEASURE direction forward=%.6f reverse=%.6f delta=%.6f\n", forward, reverse,
                std::abs(forward - reverse));
}

void TestDeterminism() {
    const int w = 97;
    const int h = 61;
    const Image ref = MakeChromaDetailImage(w, h);
    const Image dist = QuantizeImage(ref, 8);
    const pp::metric::Result a = pp::metric::compute(SourceOf(ref), SourceOf(dist));
    const pp::metric::Result b = pp::metric::compute(SourceOf(ref), SourceOf(dist));
    check(a.state == pp::metric::State::Ok && b.state == pp::metric::State::Ok, "determinism/state",
          "两次调用都应 Ok");
    check(a.score == b.score, "determinism/score",
          "同输入两次分数应逐位相同：" + num(a.score) + " vs " + num(b.score));
}

} // namespace

int main() {
    // clang-tidy bugprone-exception-escape：main 不应逃逸异常（行回调 std::function 分配失败等）。
    try {
        TestIdentity();
        TestQualityLadderMonotonic();
        TestChromaSamplingIsPunished();
        TestFakeReferenceIsDetectable();
        TestReferenceDirectionMatters();
        TestDeterminism();
    } catch (...) {
        std::printf("test_metric_truth: unexpected exception\n");
        return 1;
    }
    if (g_fail == 0) {
        std::printf("test_metric_truth: OK\n");
    } else {
        std::printf("test_metric_truth: %d failure(s)\n", g_fail);
    }
    return g_fail;
}
