// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 质量评分指标实现（SSIMULACRA 2）。
//
// 唯一算法实现 = third_party/ssimulacra2/ 中 vendored 的 libjxl v0.12.0 官方源码
// （tools/ssimulacra2.cc 逐字保留，权重/常量/尺度/边界不动；compat/ 只替换 libjxl
//  容器、Status、ToXYB、高斯模糊与内存管理这些外围依赖）。
// 契约：docs/m8-encoder-params-and-ssimulacra2-design.md C9–C11；接口 src/core/metric/metric.h。
//
// 输入契约（metric.h）：参考/失真两侧均为 sRGB **线性** 交错 float RGB（行回调）。
// 尺寸不等 / 任一边 <8×8 / 行回调异常或返回非有限值 / 内部失败 ⇒ Unavailable/Failed，
// 绝不让官方库的 “<8px 静默 100” 漏到结果里。
//
// 峰值内存：官方算法在 scale 0 需要物化多幅全分辨率临时图（约 150 B/px，12MP≈1.8GB），
// 高于 C10 的 2×frame 预算；C1 按任务卡“允许内部物化，但必须报告”实测记录于
// build/m8/c1/perf.log，流式/分块优化建议列入 C2/D 阶段（见 README「性能与内存」）。
#include "core/metric/metric.h"

#include <jxl/memory_manager.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <exception>
#include <string>
#include <vector>

#include "lib/jxl/image.h"
#include "lib/jxl/image_bundle.h"
#include "no_memory_manager.h"
#include "ssimulacra2.h"

namespace pp::metric {
namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMinSize = 8; // 官方库对 <8×8 会静默返回 100，必须前置拦截

double ElapsedMs(const Clock::time_point start) {
    const auto end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// 把行回调读入 jxl::Image3F（三平面去交错）。返回 false 时 *note 为中文短原因。
bool FillImage(const FrameSource &source, jxl::Image3F *image, std::string *note) {
    const size_t width = static_cast<size_t>(source.width);
    std::vector<float> rgb(width * 3);
    for (int y = 0; y < source.height; ++y) {
        try {
            source.row(y, rgb.data());
        } catch (const std::exception &e) {
            *note = "行回调抛异常: " + std::string(e.what());
            return false;
        } catch (...) {
            *note = "行回调抛异常";
            return false;
        }
        float *row0 = image->PlaneRow(0, static_cast<size_t>(y));
        float *row1 = image->PlaneRow(1, static_cast<size_t>(y));
        float *row2 = image->PlaneRow(2, static_cast<size_t>(y));
        for (size_t x = 0; x < width; ++x) {
            const float r = rgb[3 * x + 0];
            const float g = rgb[3 * x + 1];
            const float b = rgb[3 * x + 2];
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
                *note = "行回调返回非有限像素值";
                return false;
            }
            row0[x] = r;
            row1[x] = g;
            row2[x] = b;
        }
    }
    return true;
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

    JxlMemoryManager *const memory_manager = jpegxl::tools::NoMemoryManager();
    const size_t width = static_cast<size_t>(reference.width);
    const size_t height = static_cast<size_t>(reference.height);

    auto ref_create = jxl::Image3F::Create(memory_manager, width, height);
    if (!ref_create.ok())
        return Failed(start, "指标内部错误: 参考缓冲分配失败");
    jxl::Image3F ref_image = std::move(ref_create).value_();

    auto dist_create = jxl::Image3F::Create(memory_manager, width, height);
    if (!dist_create.ok())
        return Failed(start, "指标内部错误: 失真缓冲分配失败");
    jxl::Image3F dist_image = std::move(dist_create).value_();

    std::string note;
    if (!FillImage(reference, &ref_image, &note)) {
        return Unavailable(start, note);
    }
    if (!FillImage(distorted, &dist_image, &note)) {
        return Unavailable(start, note);
    }

    // SDR 强度目标 = libjxl kDefaultIntensityTarget(255)；契约输入已归一化到 0..1。
    jxl::ImageMetadata metadata;
    metadata.intensity_target = jxl::kDefaultIntensityTarget;

    jxl::ImageBundle ref_bundle(memory_manager, &metadata);
    jxl::ImageBundle dist_bundle(memory_manager, &metadata);
    if (!ref_bundle.SetFromImage(std::move(ref_image), jxl::ColorEncoding::LinearSRGB(false)) ||
        !dist_bundle.SetFromImage(std::move(dist_image), jxl::ColorEncoding::LinearSRGB(false))) {
        return Failed(start, "指标内部错误: ImageBundle 构造失败");
    }

    jxl::StatusOr<Msssim> computed = ComputeSSIMULACRA2(ref_bundle, dist_bundle);
    if (!computed.ok()) {
        return Failed(start, "指标内部错误: ComputeSSIMULACRA2 失败");
    }
    const double score = std::move(computed).value_().Score();
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
