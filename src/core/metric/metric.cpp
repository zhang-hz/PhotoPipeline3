// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10-W1-A2 质量评分指标实现（SSIMULACRA 2，流式/分块）。
//
// 唯一算法来源 = third_party/ssimulacra2/ 中 vendored 的 libjxl v0.12.0 官方源码
// （tools/ssimulacra2.cc：权重/常量/尺度/边界/公式不动；本文件只改执行顺序）：
//   * 逐行推进，用「官方 FastGaussianVertical 的同一递归 IIR 状态 + 2N+1 行 lookahead」
//     计算 15 个模糊平面（img1/img2/img1²/img2²/img1·img2 各 3 通道），
//     **不物化整幅 XYB 金字塔**；水平 pass 直接复用 vendored 官方 SIMD FastGaussian1D；
//   * 每就绪一行立即累加 108 范数（SSIM' 的 1/4 范数 + ringing/blurring 的 1/4 范数；
//     公式与官方 SSIMMap/EdgeDiffMap 逐字一致，累积顺序 = 行优先）；
//   * 尺度间：下一尺度 = 当前尺度**线性 sRGB** 帧的 2×2 盒式降采样（官方 Downsample
//     逐字同序），只物化 1/4^s 帧，处理完即释放。
// 峰值额外内存 ≈ 相邻两个尺度的线性 sRGB 帧（23MP 时 scale1 两帧共 ~137MB ≈ 6 B/px）
// + 每尺度 O(width) 的流式 scratch（~5MB@23MP），远低于 HEAD 旧实现的 130–160 B/px。
//
// 契约：docs/m8-encoder-params-and-ssimulacra2-design.md C9–C11；
//       docs/v0.4.0-design.md §4.2.2(1) 与 C2；接口 src/core/metric/metric.h。
// 数值 oracle：HEAD 旧实现逐字保留在 metric_oracle.cpp（测试对拍专用），
// 单测逐组对拍 |Δ|≤0.01；旧实现不进入生产调用链。
#include "core/metric/metric.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

// vendored 官方 gauss_blur.h：HWY_ALIGN_MAX 结构在 /W4 下触发 C4324（缺省填充告警），
// 属第三方代码形态，就地隔离，不改常量/接口。
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
#include "gauss_blur.h" // vendored 官方 SIMD 1D 递归高斯（水平 pass，数值单源）
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include "ssimulacra2.h" // vendored 官方 Msssim / Score（108 权重单源）
#include "xyb_row.h"     // compat 层单源：线性 sRGB → XYB 行转换

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace pp::metric {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMinSize = 8; // 官方库对 <8×8 会静默返回 100，必须前置拦截
constexpr int kNumScales = 6;
constexpr int kNumChannels = 3;
constexpr int kNumSignals = 5;                         // img1/img2/img1²/img2²/img1·img2
constexpr int kNumPlanes = kNumSignals * kNumChannels; // 15 个模糊平面
constexpr float kC2 = 0.0009f;                         // 官方 kC2
constexpr float kIntensityTarget = 255.0f;             // libjxl kDefaultIntensityTarget
constexpr double kGaussianSigma = 1.5;                 // 官方 blur sigma（ssimulacra2.cc）

double ElapsedMs(const Clock::time_point start) {
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

Result Unavailable(const Clock::time_point start, const std::string &note) {
    Result result;
    result.state = State::Unavailable;
    result.note = note;
    result.ms = ElapsedMs(start);
    return result;
}

Result Failed(const Clock::time_point start, const std::string &note) {
    Result result;
    result.state = State::Failed;
    result.note = note;
    result.ms = ElapsedMs(start);
    return result;
}

// ---------------------------------------------------------------- 缓冲与行工具

// 32B 对齐的 float 缓冲：vendored 官方 FastGaussian1D 的 hwy Store 要求输出 16B 对齐
// （HWY_CAPPED(float,4) → 128-bit），而 std::vector 的对齐保证由分配器决定；
// 这里显式按 32B 对齐分配，消除平台/运行库差异。
struct AlignedFree {
    void operator()(float *ptr) const noexcept {
        if (ptr != nullptr)
            ::operator delete(ptr, std::align_val_t(32));
    }
};
using AlignedFloats = std::unique_ptr<float, AlignedFree>;

AlignedFloats MakeAlignedZeros(size_t count) {
    float *ptr = static_cast<float *>(::operator new(count * sizeof(float), std::align_val_t(32)));
    std::memset(ptr, 0, count * sizeof(float));
    return AlignedFloats(ptr);
}

// 某尺度的线性 sRGB 帧（3 平面；只物化 scale>=1，内存 = 1/4^s 帧）。
struct Raster {
    int w = 0;
    int h = 0;
    std::vector<float> planes[kNumChannels];

    const float *Row(int c, int y) const {
        return planes[c].data() + static_cast<size_t>(y) * static_cast<size_t>(w);
    }
    float *Row(int c, int y) {
        return planes[c].data() + static_cast<size_t>(y) * static_cast<size_t>(w);
    }
};

void AllocateRaster(int w, int h, Raster *raster) {
    raster->w = w;
    raster->h = h;
    const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
    for (int c = 0; c < kNumChannels; ++c)
        raster->planes[c].assign(count, 0.0f);
}

struct RowTriplet {
    const float *rows[kNumChannels];
};

// 2×2 盒式降采样的一行：与官方 Downsample 逐字同序 ——
//   sum 按 iy（上→下）、ix（左→右）累加，坐标 clamp 到 src 边界，再乘 1/(2*2)。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：src_w/out_w 由名字与调用点语义区分
void DownsampleRow(const RowTriplet &top, const RowTriplet &bottom, int src_w, int out_w,
                   float *const out[kNumChannels]) {
    constexpr float normalize = 1.0f / 4.0f;
    for (int c = 0; c < kNumChannels; ++c) {
        const float *JXL_RESTRICT row0 = top.rows[c];
        const float *JXL_RESTRICT row1 = bottom.rows[c];
        float *JXL_RESTRICT out_row = out[c];
        for (int ox = 0; ox < out_w; ++ox) {
            const int x0 = std::min(2 * ox, src_w - 1);
            const int x1 = std::min(2 * ox + 1, src_w - 1);
            float sum = 0.0f;
            sum += row0[x0];
            sum += row0[x1];
            sum += row1[x0];
            sum += row1[x1];
            out_row[ox] = sum * normalize;
        }
    }
}

// 整帧 2×2 降采样（scale>=1：当前尺度线性帧 → 下一尺度线性帧）。
void DownsampleRaster(const Raster &src, Raster *dst) {
    const int out_w = (src.w + 1) / 2;
    const int out_h = (src.h + 1) / 2;
    AllocateRaster(out_w, out_h, dst);
    for (int oy = 0; oy < out_h; ++oy) {
        const int y0 = std::min(2 * oy, src.h - 1);
        const int y1 = std::min(2 * oy + 1, src.h - 1);
        RowTriplet top{{src.Row(0, y0), src.Row(1, y0), src.Row(2, y0)}};
        RowTriplet bottom{{src.Row(0, y1), src.Row(1, y1), src.Row(2, y1)}};
        float *rows[kNumChannels] = {dst->Row(0, oy), dst->Row(1, oy), dst->Row(2, oy)};
        DownsampleRow(top, bottom, src.w, out_w, rows);
    }
}

// 官方 MakePositiveXYB 的行版本：X←X*14+0.42；Y←Y+0.01；B←(B-Y)+0.55
// （B 用的是加 0.01 之前的 Y；逐像素运算顺序与官方逐字一致）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：x/y/b 三平面由 XYB 通道序固定
void MakePositiveXybRow(float *x, float *y, float *b, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const float xv = x[i];
        const float yv = y[i];
        const float bv = b[i];
        b[i] = (bv - yv) + 0.55f;
        x[i] = xv * 14.0f + 0.42f;
        y[i] = yv + 0.01f;
    }
}

// 线性 sRGB（3 平面行）→ positive XYB（原地，与官方 ToXYB + MakePositiveXYB 同源）。
void LinearRowsToPositiveXyb(const jpegxl::tools::XybPremulAbsorb &pa, float *r, float *g, float *b,
                             size_t count) {
    jpegxl::tools::LinearRgbRowToXyb(r, g, b, pa, count);
    MakePositiveXybRow(r, g, b, count);
}

// ---------------------------------------------------------------- AVX2 行内内核
//
// 下面的 AVX2 版本与上面的标量公式**逐位等价**：浮点运算保持与标量相同的单次舍入序
// （显式 FMA ⇔ std::fma(float)），cbrt 近似的位运算与 32 位整数乘法逐式对应；
// 尾 0–7 像素复用标量单源实现。非 AVX2 平台回落标量。
#if defined(__AVX2__)
namespace avx2 {

// NOLINTBEGIN(portability-simd-intrinsics)：AVX2 是本仓硬要求（x86-64-v3，AGENTS §1.4），SIMD
// 内核按设计直接使用 x86 intrinsic（同类告警在 src/core/simd/kernels.cpp 基线内）；可移植 SIMD
// 替代建议不适用于本仓「自研 AVX2 热路径」的既有决定。

// 32 位整数求逆指数 + 3 次牛顿迭代 + 最终修正（与 compat/xyb_row.h 的
// ScalarCubeRootAndAdd 逐式对应）。
inline __m256 CubeRootAndAdd(__m256 x, __m256 add) {
    const __m256i exp_bias = _mm256_set1_epi32(0x54800000);
    const __m256i exp_mul = _mm256_set1_epi32(0x002AAAAA);
    const __m256i zero_i = _mm256_setzero_si256();
    const __m256 one_third = _mm256_set1_ps(1.0f / 3.0f);
    const __m256 four_thirds = _mm256_set1_ps(4.0f / 3.0f);
    const __m256 sign = _mm256_castsi256_ps(_mm256_set1_epi32(static_cast<int>(0x80000000u)));

    const __m256i m1 = _mm256_castps_si256(x);
    __m256i m2 = _mm256_sub_epi32(exp_bias, _mm256_mullo_epi32(_mm256_srli_epi32(m1, 23), exp_mul));
    m2 = _mm256_andnot_si256(_mm256_cmpeq_epi32(m1, zero_i), m2);
    __m256 r = _mm256_castsi256_ps(m2);

    const __m256 xa_3 = _mm256_mul_ps(one_third, x);
    const __m256 neg_xa_3 = _mm256_xor_ps(xa_3, sign);
    for (int i = 0; i < 3; ++i) {
        const __m256 r2 = _mm256_mul_ps(r, r);
        r = _mm256_fmadd_ps(neg_xa_3, _mm256_mul_ps(r2, r2), _mm256_mul_ps(four_thirds, r));
    }
    __m256 r2 = _mm256_mul_ps(r, r);
    const __m256 neg_x = _mm256_xor_ps(x, sign);
    const __m256 neg_mul_add = _mm256_fmadd_ps(neg_x, _mm256_mul_ps(r2, r2), r);
    r = _mm256_fmadd_ps(one_third, neg_mul_add, r);
    r2 = _mm256_mul_ps(r, r);
    return _mm256_fmadd_ps(r2, x, add);
}

// 与 compat/xyb_row.h 的 LinearRgbRowToXyb 逐位等价（原地转换）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：r/g/b 三平面由 XYB 通道序固定
void LinearRgbRowToXyb(const jpegxl::tools::XybPremulAbsorb &pa, float *r, float *g, float *b,
                       size_t count) {
    const __m256 m00 = _mm256_set1_ps(pa.matrix[0][0]);
    const __m256 m01 = _mm256_set1_ps(pa.matrix[0][1]);
    const __m256 m02 = _mm256_set1_ps(pa.matrix[0][2]);
    const __m256 m10 = _mm256_set1_ps(pa.matrix[1][0]);
    const __m256 m11 = _mm256_set1_ps(pa.matrix[1][1]);
    const __m256 m12 = _mm256_set1_ps(pa.matrix[1][2]);
    const __m256 m20 = _mm256_set1_ps(pa.matrix[2][0]);
    const __m256 m21 = _mm256_set1_ps(pa.matrix[2][1]);
    const __m256 m22 = _mm256_set1_ps(pa.matrix[2][2]);
    const __m256 bias0 = _mm256_set1_ps(pa.bias[0]);
    const __m256 bias1 = _mm256_set1_ps(pa.bias[1]);
    const __m256 bias2 = _mm256_set1_ps(pa.bias[2]);
    const __m256 add0 = _mm256_set1_ps(pa.add[0]);
    const __m256 add1 = _mm256_set1_ps(pa.add[1]);
    const __m256 add2 = _mm256_set1_ps(pa.add[2]);
    const __m256 zero = _mm256_setzero_ps();
    const __m256 half = _mm256_set1_ps(0.5f);

    size_t x = 0;
    for (; x + 8 <= count; x += 8) {
        const __m256 rv = _mm256_loadu_ps(r + x);
        const __m256 gv = _mm256_loadu_ps(g + x);
        const __m256 bv = _mm256_loadu_ps(b + x);
        __m256 mixed0 =
            _mm256_fmadd_ps(m00, rv, _mm256_fmadd_ps(m01, gv, _mm256_fmadd_ps(m02, bv, bias0)));
        __m256 mixed1 =
            _mm256_fmadd_ps(m10, rv, _mm256_fmadd_ps(m11, gv, _mm256_fmadd_ps(m12, bv, bias1)));
        __m256 mixed2 =
            _mm256_fmadd_ps(m20, rv, _mm256_fmadd_ps(m21, gv, _mm256_fmadd_ps(m22, bv, bias2)));
        mixed0 = _mm256_max_ps(mixed0, zero);
        mixed1 = _mm256_max_ps(mixed1, zero);
        mixed2 = _mm256_max_ps(mixed2, zero);
        const __m256 t0 = CubeRootAndAdd(mixed0, add0);
        const __m256 t1 = CubeRootAndAdd(mixed1, add1);
        const __m256 t2 = CubeRootAndAdd(mixed2, add2);
        _mm256_storeu_ps(r + x, _mm256_mul_ps(half, _mm256_sub_ps(t0, t1)));
        _mm256_storeu_ps(g + x, _mm256_mul_ps(half, _mm256_add_ps(t0, t1)));
        _mm256_storeu_ps(b + x, t2);
    }
    if (x < count) {
        // 尾部 0–7 像素：拷到 8 元素临时行后复用标量单源公式（保持逐位一致）。
        float tr[8] = {};
        float tg[8] = {};
        float tb[8] = {};
        const size_t rest = count - x;
        for (size_t i = 0; i < rest; ++i) {
            tr[i] = r[x + i];
            tg[i] = g[x + i];
            tb[i] = b[x + i];
        }
        jpegxl::tools::LinearRgbRowToXyb(tr, tg, tb, pa, rest);
        for (size_t i = 0; i < rest; ++i) {
            r[x + i] = tr[i];
            g[x + i] = tg[i];
            b[x + i] = tb[i];
        }
    }
}

// 与 MakePositiveXybRow 逐位等价（原地）。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：x/y/b 三平面由 XYB 通道序固定
void MakePositiveXybRow(float *x, float *y, float *b, size_t count) {
    const __m256 k14 = _mm256_set1_ps(14.0f);
    const __m256 k042 = _mm256_set1_ps(0.42f);
    const __m256 k001 = _mm256_set1_ps(0.01f);
    const __m256 k055 = _mm256_set1_ps(0.55f);
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256 xv = _mm256_loadu_ps(x + i);
        const __m256 yv = _mm256_loadu_ps(y + i);
        const __m256 bv = _mm256_loadu_ps(b + i);
        _mm256_storeu_ps(b + i, _mm256_add_ps(_mm256_sub_ps(bv, yv), k055));
        _mm256_storeu_ps(x + i, _mm256_add_ps(_mm256_mul_ps(xv, k14), k042));
        _mm256_storeu_ps(y + i, _mm256_add_ps(yv, k001));
    }
    for (; i < count; ++i) {
        const float xv = x[i];
        const float yv = y[i];
        const float bv = b[i];
        b[i] = (bv - yv) + 0.55f;
        x[i] = xv * 14.0f + 0.42f;
        y[i] = yv + 0.01f;
    }
}

} // namespace avx2
// NOLINTEND(portability-simd-intrinsics)
#endif

// 线性行 → positive XYB 的派发：AVX2 可用时走逐位等价的向量化版本。
void LinearRowsToPositiveXybFast(const jpegxl::tools::XybPremulAbsorb &pa, float *r, float *g,
                                 float *b, size_t count) {
#if defined(__AVX2__)
    avx2::LinearRgbRowToXyb(pa, r, g, b, count);
    avx2::MakePositiveXybRow(r, g, b, count);
#else
    LinearRowsToPositiveXyb(pa, r, g, b, count);
#endif
}

// ---------------------------------------------------------------- 行源读取

// 读一行 sRGB 线性交错 float 并校验（与旧实现同一契约口径：异常/非有限 → Unavailable）。
bool ReadRgbRow(const FrameSource &source, int y, float *rgb, int width, std::string *note) {
    try {
        source.row(y, rgb);
    } catch (const std::exception &e) {
        *note = "行回调抛异常: " + std::string(e.what());
        return false;
    } catch (...) {
        *note = "行回调抛异常";
        return false;
    }
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(kNumChannels);
    for (size_t i = 0; i < count; ++i) {
        if (!std::isfinite(rgb[i])) {
            *note = "行回调返回非有限像素值";
            return false;
        }
    }
    return true;
}

// 交错 RGB → 3 平面行。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：rgb 为源、r/g/b 为三平面，语义由名字固定
void DeinterleaveRow(const float *rgb, int width, float *r, float *g, float *b) {
    for (int x = 0; x < width; ++x) {
        const size_t i = static_cast<size_t>(x) * static_cast<size_t>(kNumChannels);
        r[x] = rgb[i + 0];
        g[x] = rgb[i + 1];
        b[x] = rgb[i + 2];
    }
}

// ---------------------------------------------------------------- 单尺度流式指标

// 单尺度流式指标引擎 = 官方 ComputeSSIMULACRA2 内层循环的逐行状态机：
//   * 15 个模糊平面各自维护 3 段递归高斯（每段 2 个历史输出）的垂直 pass 状态；
//   * 水平 pass 直接调用 vendored 官方 FastGaussian1D（SIMD，数值单源）；
//   * 垂直 pass 按官方 FastGaussianVertical 的调度：前 radius-1 行预热（不产输出），
//     之后每行输入就绪即吐出 n = y-(radius-1) 行；底部边界再补 radius-1 行；
//   * 每吐出一行，用该行 15 个模糊值与缓存的 positive XYB 行算 SSIM'/边缘图并累加。
class ScaleMetric {
public:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：width/height 由名字与图像语义固定
    ScaleMetric(int width, int height)
        : w_(static_cast<size_t>(width)), h_(static_cast<size_t>(height)) {
        rg_ = jxl::CreateRecursiveGaussian(kGaussianSigma);
        radius_ = static_cast<size_t>(rg_.radius);
        ring_len_ = 2 * radius_ + 2;
        xyb_ring_len_ = radius_ + 2;
        // 行跨度向上取整到 8 个 float（32B）：FastGaussian1D 的 Store 要求 16B 对齐，
        // 缓冲本身按 32B 对齐分配，故所有行起点均满足对齐要求。
        stride_ = (w_ + 7) & ~static_cast<size_t>(7);
        rows_ = MakeAlignedZeros(static_cast<size_t>(kNumPlanes) * ring_len_ * stride_);
        state_.assign(static_cast<size_t>(kNumPlanes) * 6 * stride_, 0.0f);
        out_.assign(static_cast<size_t>(kNumPlanes) * stride_, 0.0f);
        sig_.assign(static_cast<size_t>(kNumPlanes) * stride_, 0.0f);
        xyb_ref_.assign(static_cast<size_t>(kNumChannels) * xyb_ring_len_ * stride_, 0.0f);
        xyb_dist_.assign(static_cast<size_t>(kNumChannels) * xyb_ring_len_ * stride_, 0.0f);
    }

    ScaleMetric(const ScaleMetric &) = delete;
    ScaleMetric &operator=(const ScaleMetric &) = delete;

    // 推入第 y 行（参考/失真各 3 条 positive XYB 平面行）。
    void PushPositiveXybRow(size_t y, const float *const *ref_xyb, const float *const *dist_xyb) {
        StoreXybRow(y, ref_xyb, dist_xyb);
        PushBlurInputs(y, ref_xyb, dist_xyb);
        if (y + 1 < radius_) {
            // 官方 FastGaussianVertical 预热段：n = y-(radius-1) ∈ [-radius+1, -1]，
            // 只喂 bottom 单输入（top 越界按 0），不产输出。
            VerticalStep(false, 0, true, y);
        } else {
            EmitRow(next_output_);
            ++next_output_;
        }
    }

    // 图像行耗尽后，官方底部边界还要再吐 radius-1 行（bottom 越界按 0）。
    void Flush() {
        while (next_output_ < h_) {
            EmitRow(next_output_);
            ++next_output_;
        }
    }

    MsssimScale Finish() const {
        MsssimScale scale{};
        const double one_per_pixel = 1.0 / (static_cast<double>(w_) * static_cast<double>(h_));
        for (int c = 0; c < kNumChannels; ++c) {
            scale.avg_ssim[c * 2 + 0] = one_per_pixel * ssim_sums_[c][0];
            scale.avg_ssim[c * 2 + 1] = std::sqrt(std::sqrt(one_per_pixel * ssim_sums_[c][1]));
            scale.avg_edgediff[c * 4 + 0] = one_per_pixel * edge_sums_[c][0];
            scale.avg_edgediff[c * 4 + 1] = std::sqrt(std::sqrt(one_per_pixel * edge_sums_[c][1]));
            scale.avg_edgediff[c * 4 + 2] = one_per_pixel * edge_sums_[c][2];
            scale.avg_edgediff[c * 4 + 3] = std::sqrt(std::sqrt(one_per_pixel * edge_sums_[c][3]));
        }
        return scale;
    }

private:
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：plane/k 由 ring 行寻址语义固定
    float *RingRow(int plane, size_t k) {
        const size_t slot = k % ring_len_;
        return rows_.get() + (static_cast<size_t>(plane) * ring_len_ + slot) * stride_;
    }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：plane/k 由 ring 行寻址语义固定
    const float *RingRowConst(int plane, size_t k) const {
        const size_t slot = k % ring_len_;
        return rows_.get() + (static_cast<size_t>(plane) * ring_len_ + slot) * stride_;
    }
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：ring/c/n 由 XYB ring 寻址语义固定
    const float *XybRow(const std::vector<float> &ring, int c, size_t n) const {
        const size_t slot = n % xyb_ring_len_;
        return ring.data() + (static_cast<size_t>(c) * xyb_ring_len_ + slot) * stride_;
    }

    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：ref/dist 两套 XYB 行由名字固定
    void StoreXybRow(size_t y, const float *const *ref_xyb, const float *const *dist_xyb) {
        const size_t slot = y % xyb_ring_len_;
        for (int c = 0; c < kNumChannels; ++c) {
            float *ref_dst =
                xyb_ref_.data() + (static_cast<size_t>(c) * xyb_ring_len_ + slot) * stride_;
            float *dist_dst =
                xyb_dist_.data() + (static_cast<size_t>(c) * xyb_ring_len_ + slot) * stride_;
            std::memcpy(ref_dst, ref_xyb[c], w_ * sizeof(float));
            std::memcpy(dist_dst, dist_xyb[c], w_ * sizeof(float));
        }
    }

    // 15 个模糊平面的水平 pass：直接复用官方 FastGaussian1D（SIMD）。
    // 先算 9 条乘积累加行（img1²/img2²/img1·img2），再逐平面滤波进垂直 pass 的 ring。
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：ref/dist 两套 XYB 行由名字固定
    void PushBlurInputs(size_t y, const float *const *ref_xyb, const float *const *dist_xyb) {
        float *sig = sig_.data();
        for (int c = 0; c < kNumChannels; ++c) {
            const float *r = ref_xyb[c];
            const float *d = dist_xyb[c];
            jxl::FastGaussian1D(rg_, w_, r, RingRow(c, y));
            jxl::FastGaussian1D(rg_, w_, d, RingRow(kNumChannels + c, y));

            // plane 6..8 = img1²；9..11 = img2²；12..14 = img1*img2
            float *sq_ref = sig + (static_cast<size_t>(6 + c) * stride_);
            float *sq_dist = sig + (static_cast<size_t>(9 + c) * stride_);
            float *cross = sig + (static_cast<size_t>(12 + c) * stride_);
            size_t x = 0;
#if defined(__AVX2__)
            // NOLINTBEGIN(portability-simd-intrinsics)：AVX2 是硬要求（同 avx2 命名空间块）
            for (; x + 8 <= w_; x += 8) {
                const __m256 rv = _mm256_loadu_ps(r + x);
                const __m256 dv = _mm256_loadu_ps(d + x);
                _mm256_storeu_ps(sq_ref + x, _mm256_mul_ps(rv, rv));
                _mm256_storeu_ps(sq_dist + x, _mm256_mul_ps(dv, dv));
                _mm256_storeu_ps(cross + x, _mm256_mul_ps(rv, dv));
            }
            // NOLINTEND(portability-simd-intrinsics)
#endif
            for (; x < w_; ++x) {
                const float rv = r[x];
                const float dv = d[x];
                sq_ref[x] = rv * rv;
                sq_dist[x] = dv * dv;
                cross[x] = rv * dv;
            }
            jxl::FastGaussian1D(rg_, w_, sq_ref, RingRow(6 + c, y));
            jxl::FastGaussian1D(rg_, w_, sq_dist, RingRow(9 + c, y));
            jxl::FastGaussian1D(rg_, w_, cross, RingRow(12 + c, y));
        }
    }

    // 官方 FastGaussianVertical 的单步（逐列递归 IIR，3 段串接）。
    // 注意：预热步的输出行同样写入 out_（随后丢弃），保证向量化内核无需空指针分支。
    void VerticalStep(bool has_top, size_t top_k, bool has_bot, size_t bot_k) {
        const float n2[kNumChannels] = {rg_.n2[0], rg_.n2[4], rg_.n2[8]};
        const float d1[kNumChannels] = {rg_.d1[0], rg_.d1[4], rg_.d1[8]};
        for (int p = 0; p < kNumPlanes; ++p) {
            const float *top = has_top ? RingRowConst(p, top_k) : nullptr;
            const float *bottom = has_bot ? RingRowConst(p, bot_k) : nullptr;
            float *state = state_.data() + static_cast<size_t>(p) * 6 * stride_;
            float *out = out_.data() + static_cast<size_t>(p) * stride_;
            VerticalFilterRow(top, bottom, n2, d1, state, out);
        }
    }

    // NOLINTBEGIN(bugprone-easily-swappable-parameters)：top/bottom 与 state/out 由名字与调用点固定
    void VerticalFilterRow(const float *JXL_RESTRICT top, const float *JXL_RESTRICT bottom,
                           const float n2[kNumChannels], const float d1[kNumChannels],
                           float *JXL_RESTRICT state, float *JXL_RESTRICT out) {
        size_t x = 0;
#if defined(__AVX2__)
        // NOLINTBEGIN(portability-simd-intrinsics)：AVX2 是硬要求（同 avx2 命名空间块）
        // 8-wide 版本：3 段递归 IIR 逐段向量化，与标量同公式、同 FMA 舍入序。
        const __m256 vn0 = _mm256_set1_ps(n2[0]);
        const __m256 vn1 = _mm256_set1_ps(n2[1]);
        const __m256 vn2 = _mm256_set1_ps(n2[2]);
        const __m256 vnd0 = _mm256_set1_ps(-d1[0]);
        const __m256 vnd1 = _mm256_set1_ps(-d1[1]);
        const __m256 vnd2 = _mm256_set1_ps(-d1[2]);
        const __m256 sign = _mm256_castsi256_ps(_mm256_set1_epi32(static_cast<int>(0x80000000u)));
        const __m256 zero = _mm256_setzero_ps();
        for (; x + 8 <= w_; x += 8) {
            __m256 sum = zero;
            if (top != nullptr)
                sum = _mm256_loadu_ps(top + x);
            if (bottom != nullptr)
                sum = _mm256_add_ps(sum, _mm256_loadu_ps(bottom + x));
            float *s0p = state + x;
            float *s0p2 = s0p + stride_;
            float *s1p = s0p2 + stride_;
            float *s1p2 = s1p + stride_;
            float *s2p = s1p2 + stride_;
            float *s2p2 = s2p + stride_;
            const __m256 p0 = _mm256_loadu_ps(s0p);
            const __m256 p1 = _mm256_loadu_ps(s1p);
            const __m256 p2 = _mm256_loadu_ps(s2p);
            const __m256 n0 = _mm256_fmadd_ps(
                vn0, sum, _mm256_fmadd_ps(vnd0, p0, _mm256_xor_ps(_mm256_loadu_ps(s0p2), sign)));
            const __m256 n1 = _mm256_fmadd_ps(
                vn1, sum, _mm256_fmadd_ps(vnd1, p1, _mm256_xor_ps(_mm256_loadu_ps(s1p2), sign)));
            const __m256 n2v = _mm256_fmadd_ps(
                vn2, sum, _mm256_fmadd_ps(vnd2, p2, _mm256_xor_ps(_mm256_loadu_ps(s2p2), sign)));
            _mm256_storeu_ps(s0p2, p0);
            _mm256_storeu_ps(s1p2, p1);
            _mm256_storeu_ps(s2p2, p2);
            _mm256_storeu_ps(s0p, n0);
            _mm256_storeu_ps(s1p, n1);
            _mm256_storeu_ps(s2p, n2v);
            // 官方 Add(y1, Add(y3, y5))：y0 + (y1 + y2)。
            _mm256_storeu_ps(out + x, _mm256_add_ps(n0, _mm256_add_ps(n1, n2v)));
        }
        // NOLINTEND(portability-simd-intrinsics)
#endif
        for (; x < w_; ++x) {
            float sum = 0.0f;
            if (top != nullptr)
                sum += top[x];
            if (bottom != nullptr)
                sum += bottom[x];
            float y0 = 0.0f;
            float y1 = 0.0f;
            float y2 = 0.0f;
            for (int s = 0; s < kNumChannels; ++s) {
                float *prev = state + static_cast<size_t>(2 * s) * stride_ + x;
                float *prev2 = state + static_cast<size_t>(2 * s + 1) * stride_ + x;
                // 官方：y = MulAdd(n2, sum, NegMulSub(d1, prev, prev2))，其中 Highway 的
                // NegMulSub(a,b,c) = -a*b - c ⇒ y = n2*sum - d1*prev - prev2。
                const float next = std::fma(n2[s], sum, std::fma(-d1[s], *prev, -*prev2));
                *prev2 = *prev;
                *prev = next;
                if (s == 0) {
                    y0 = next;
                } else if (s == 1) {
                    y1 = next;
                } else {
                    y2 = next;
                }
            }
            if (out != nullptr)
                out[x] = y0 + (y1 + y2); // 官方 Add(y1, Add(y3, y5))
        }
    }
    // NOLINTEND(bugprone-easily-swappable-parameters)

    void EmitRow(size_t n) {
        const bool has_top = n >= radius_ + 1;     // top = n-radius-1 >= 0
        const bool has_bot = n + radius_ - 1 < h_; // bottom = n+radius-1 < h
        const size_t top_k = has_top ? n - radius_ - 1 : 0;
        const size_t bot_k = has_bot ? n + radius_ - 1 : 0;
        VerticalStep(has_top, top_k, has_bot, bot_k);
        Accumulate(n);
    }

    // 官方 SSIMMap + EdgeDiffMap 的逐行累加（公式/求和顺序逐字一致，累积用 double）。
    void Accumulate(size_t n) {
        const float *img1[kNumChannels] = {XybRow(xyb_ref_, 0, n), XybRow(xyb_ref_, 1, n),
                                           XybRow(xyb_ref_, 2, n)};
        const float *img2[kNumChannels] = {XybRow(xyb_dist_, 0, n), XybRow(xyb_dist_, 1, n),
                                           XybRow(xyb_dist_, 2, n)};
        for (int c = 0; c < kNumChannels; ++c) {
            const float *mu1 = out_.data() + static_cast<size_t>(c) * stride_;
            const float *mu2 = out_.data() + static_cast<size_t>(kNumChannels + c) * stride_;
            const float *s11 = out_.data() + static_cast<size_t>(6 + c) * stride_;
            const float *s22 = out_.data() + static_cast<size_t>(9 + c) * stride_;
            const float *s12 = out_.data() + static_cast<size_t>(12 + c) * stride_;

            double ssim_sum1 = 0.0;
            double ssim_sum4 = 0.0;
            for (size_t x = 0; x < w_; ++x) {
                const float mu1v = mu1[x];
                const float mu2v = mu2[x];
                const float mu11 = mu1v * mu1v;
                const float mu22 = mu2v * mu2v;
                const float mu12 = mu1v * mu2v;
                const float num_m = 1.0f - (mu1v - mu2v) * (mu1v - mu2v);
                const float num_s = 2.0f * (s12[x] - mu12) + kC2;
                const float denom_s = (s11[x] - mu11) + (s22[x] - mu22) + kC2;
                double d = 1.0 - (num_m * num_s / denom_s);
                d = std::max(d, 0.0);
                ssim_sum1 += d;
                const double d2 = d * d;
                ssim_sum4 += d2 * d2;
            }
            ssim_sums_[c][0] += ssim_sum1;
            ssim_sums_[c][1] += ssim_sum4;

            const float *edge1 = img1[c];
            const float *edge2 = img2[c];
            double edge_sum[4] = {0.0, 0.0, 0.0, 0.0};
            for (size_t x = 0; x < w_; ++x) {
                const double d1 = (1.0 + static_cast<double>(std::fabs(edge2[x] - mu2[x]))) /
                                      (1.0 + static_cast<double>(std::fabs(edge1[x] - mu1[x]))) -
                                  1.0;
                const double artifact = std::max(d1, 0.0);
                edge_sum[0] += artifact;
                const double artifact2 = artifact * artifact;
                edge_sum[1] += artifact2 * artifact2;
                const double detail_lost = std::max(-d1, 0.0);
                edge_sum[2] += detail_lost;
                const double detail_lost2 = detail_lost * detail_lost;
                edge_sum[3] += detail_lost2 * detail_lost2;
            }
            for (int i = 0; i < 4; ++i)
                edge_sums_[c][i] += edge_sum[i];
        }
    }

    jxl::RecursiveGaussian rg_;
    size_t radius_ = 0;
    size_t ring_len_ = 0;
    size_t xyb_ring_len_ = 0;
    size_t stride_ = 0;
    size_t w_ = 0;
    size_t h_ = 0;
    size_t next_output_ = 0;
    AlignedFloats rows_;
    std::vector<float> state_;
    std::vector<float> out_;
    std::vector<float> sig_;
    std::vector<float> xyb_ref_;
    std::vector<float> xyb_dist_;
    double ssim_sums_[kNumChannels][2] = {};
    double edge_sums_[kNumChannels][4] = {};
};

// ---------------------------------------------------------------- 单尺度驱动

// scale 0：行回调 → 线性 sRGB 行 →（可选）降采样出下一尺度 + positive XYB → 流式指标。
bool ProcessScale0(const FrameSource &reference, const FrameSource &distorted, int width,
                   int height, const jpegxl::tools::XybPremulAbsorb &pa, bool build_next,
                   Raster *next_ref, Raster *next_dist, MsssimScale *out_scale, std::string *note) {
    const size_t w = static_cast<size_t>(width);
    std::vector<float> ilv_ref(w * static_cast<size_t>(kNumChannels));
    std::vector<float> ilv_dist(w * static_cast<size_t>(kNumChannels));
    std::vector<float> lin_ref(static_cast<size_t>(kNumChannels) * w);
    std::vector<float> lin_dist(static_cast<size_t>(kNumChannels) * w);
    std::vector<float> prev_ref(static_cast<size_t>(kNumChannels) * w);
    std::vector<float> prev_dist(static_cast<size_t>(kNumChannels) * w);
    float *ref_rows[kNumChannels] = {lin_ref.data(), lin_ref.data() + w, lin_ref.data() + 2 * w};
    float *dist_rows[kNumChannels] = {lin_dist.data(), lin_dist.data() + w,
                                      lin_dist.data() + 2 * w};
    float *prev_ref_rows[kNumChannels] = {prev_ref.data(), prev_ref.data() + w,
                                          prev_ref.data() + 2 * w};
    float *prev_dist_rows[kNumChannels] = {prev_dist.data(), prev_dist.data() + w,
                                           prev_dist.data() + 2 * w};

    ScaleMetric metric(width, height);
    for (int y = 0; y < height; ++y) {
        if (!ReadRgbRow(reference, y, ilv_ref.data(), width, note))
            return false;
        if (!ReadRgbRow(distorted, y, ilv_dist.data(), width, note))
            return false;
        DeinterleaveRow(ilv_ref.data(), width, ref_rows[0], ref_rows[1], ref_rows[2]);
        DeinterleaveRow(ilv_dist.data(), width, dist_rows[0], dist_rows[1], dist_rows[2]);

        if (build_next) {
            const int out_w = next_ref->w;
            const bool odd_row = (y & 1) != 0;
            if (!odd_row) {
                for (int c = 0; c < kNumChannels; ++c) {
                    std::memcpy(prev_ref_rows[c], ref_rows[c], w * sizeof(float));
                    std::memcpy(prev_dist_rows[c], dist_rows[c], w * sizeof(float));
                }
            }
            if (odd_row || y == height - 1) {
                const int oy = y / 2;
                for (int side = 0; side < 2; ++side) {
                    const float *const *cur = side == 0 ? ref_rows : dist_rows;
                    const float *const *prev = side == 0 ? prev_ref_rows : prev_dist_rows;
                    RowTriplet top{{odd_row ? prev[0] : cur[0], odd_row ? prev[1] : cur[1],
                                    odd_row ? prev[2] : cur[2]}};
                    RowTriplet bottom{{cur[0], cur[1], cur[2]}};
                    Raster *dst = side == 0 ? next_ref : next_dist;
                    float *rows[kNumChannels] = {dst->Row(0, oy), dst->Row(1, oy), dst->Row(2, oy)};
                    DownsampleRow(top, bottom, width, out_w, rows);
                }
            }
        }

        for (int side = 0; side < 2; ++side) {
            float **rows = side == 0 ? ref_rows : dist_rows;
            LinearRowsToPositiveXybFast(pa, rows[0], rows[1], rows[2], w);
        }
        metric.PushPositiveXybRow(static_cast<size_t>(y), ref_rows, dist_rows);
    }
    metric.Flush();
    *out_scale = metric.Finish();
    return true;
}

// scale>=1：从已物化的线性 sRGB 尺度帧读行 → positive XYB → 流式指标。
void ProcessScaleFromRaster(const Raster &ref, const Raster &dist,
                            const jpegxl::tools::XybPremulAbsorb &pa, MsssimScale *out_scale) {
    const size_t w = static_cast<size_t>(ref.w);
    std::vector<float> lin_ref(static_cast<size_t>(kNumChannels) * w);
    std::vector<float> lin_dist(static_cast<size_t>(kNumChannels) * w);
    float *ref_rows[kNumChannels] = {lin_ref.data(), lin_ref.data() + w, lin_ref.data() + 2 * w};
    float *dist_rows[kNumChannels] = {lin_dist.data(), lin_dist.data() + w,
                                      lin_dist.data() + 2 * w};
    ScaleMetric metric(ref.w, ref.h);
    for (int y = 0; y < ref.h; ++y) {
        for (int c = 0; c < kNumChannels; ++c) {
            std::memcpy(ref_rows[c], ref.Row(c, y), w * sizeof(float));
            std::memcpy(dist_rows[c], dist.Row(c, y), w * sizeof(float));
        }
        LinearRowsToPositiveXybFast(pa, ref_rows[0], ref_rows[1], ref_rows[2], w);
        LinearRowsToPositiveXybFast(pa, dist_rows[0], dist_rows[1], dist_rows[2], w);
        metric.PushPositiveXybRow(static_cast<size_t>(y), ref_rows, dist_rows);
    }
    metric.Flush();
    *out_scale = metric.Finish();
}

// ---------------------------------------------------------------- 主流程

Result ComputeImpl(const FrameSource &reference, const FrameSource &distorted,
                   const Clock::time_point start) {
    if (reference.width <= 0 || reference.height <= 0 || distorted.width <= 0 ||
        distorted.height <= 0) {
        return Unavailable(start, "尺寸无效");
    }
    if (reference.width != distorted.width || reference.height != distorted.height) {
        return Unavailable(start, "参考/失真尺寸不一致");
    }
    if (reference.width < kMinSize || reference.height < kMinSize) {
        return Unavailable(start, "尺寸小于 8×8");
    }
    if (!reference.row || !distorted.row) {
        return Unavailable(start, "行回调为空");
    }

    const jpegxl::tools::XybPremulAbsorb pa =
        jpegxl::tools::ComputeXybPremulAbsorb(kIntensityTarget);

    Msssim msssim;
    // 官方 ComputeSSIMULACRA2 的尺度迭代语义（逐字对齐）：for scale 先检查**上一尺度**
    // 图像的 dims（<8 则 break），再对上一尺度线性帧做 2×2 降采样得到本轮图像并计算。
    // 因此「第一个 <8 的尺度」仍会被计算一次：96×80 → … → 6×5 共 5 个尺度，
    // 8×8 输入 → 8×8/4×4 共 2 个尺度（与 HEAD 旧实现一致）。
    Raster pending_ref; // scale0 期间物化的 scale1 线性帧（官方在 scale1 迭代开头才降采样）
    Raster pending_dist;
    Raster prev_ref; // 上一轮（scale>=1）已处理尺度的线性帧
    Raster prev_dist;
    int prev_w = reference.width; // 上一尺度图像 dims（scale0 时 = 输入 dims）
    int prev_h = reference.height;
    for (int scale = 0; scale < kNumScales; ++scale) {
        if (prev_w < kMinSize || prev_h < kMinSize)
            break;
        MsssimScale scale_metric{};
        int scale_w = 0;
        int scale_h = 0;
        if (scale == 0) {
            scale_w = prev_w;
            scale_h = prev_h;
            const bool build_pending = (scale + 1 < kNumScales);
            Raster next_ref;
            Raster next_dist;
            if (build_pending) {
                AllocateRaster((scale_w + 1) / 2, (scale_h + 1) / 2, &next_ref);
                AllocateRaster((scale_w + 1) / 2, (scale_h + 1) / 2, &next_dist);
            }
            std::string note;
            if (!ProcessScale0(reference, distorted, scale_w, scale_h, pa, build_pending, &next_ref,
                               &next_dist, &scale_metric, &note)) {
                return Unavailable(start, note);
            }
            pending_ref = std::move(next_ref);
            pending_dist = std::move(next_dist);
        } else {
            Raster this_ref;
            Raster this_dist;
            if (scale == 1) {
                // 用 swap 取代 move 赋值：pending_* 在 scale==0 迭代填充、在 scale==1 迭代取走
                // （两条分支属不同迭代），clang-tidy 的 bugprone-use-after-move 对「循环内
                // 上一轮 move、下一轮再用」会误报；swap 与 move 赋值等价（this_ref 此刻为空）。
                std::swap(this_ref, pending_ref);
                std::swap(this_dist, pending_dist);
            } else {
                DownsampleRaster(prev_ref, &this_ref);
                DownsampleRaster(prev_dist, &this_dist);
            }
            scale_w = this_ref.w;
            scale_h = this_ref.h;
            ProcessScaleFromRaster(this_ref, this_dist, pa, &scale_metric);
            prev_ref = std::move(this_ref);
            prev_dist = std::move(this_dist);
        }
        msssim.scales.push_back(scale_metric);
        prev_w = scale_w;
        prev_h = scale_h;
    }

    if (msssim.scales.empty()) {
        return Failed(start, "指标内部错误: 没有可计算的尺度");
    }
    const double score = msssim.Score();
    if (!std::isfinite(score)) {
        return Failed(start, "指标内部错误: 分数非有限");
    }

    Result result;
    result.state = State::Ok;
    result.score = score;
    result.ms = ElapsedMs(start);
    return result;
}

} // namespace

Result compute(const FrameSource &reference, const FrameSource &distorted) {
    const Clock::time_point start = Clock::now();
    try {
        return ComputeImpl(reference, distorted, start);
    } catch (const std::exception &) {
        return Failed(start, "指标内部异常");
    } catch (...) {
        return Failed(start, "指标内部异常");
    }
}

const char *metric_name() { return "ssimulacra2"; }

int metric_version() { return 1; }

const char *state_name(State state) {
    switch (state) {
    case State::Off:
        return "off";
    case State::Ok:
        return "ok";
    case State::Unavailable:
        return "unavailable";
    case State::Failed:
        return "failed";
    }
    return "unavailable";
}

} // namespace pp::metric
