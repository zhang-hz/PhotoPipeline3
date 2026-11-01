// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10-W1-A2：HEAD 旧实现 oracle（测试对拍专用，非生产路径）。
//
// 本文件是 HEAD（1208894）src/core/metric/metric.cpp 的**逐字保留**版本：
// 物化参考/失真两整帧（Image3F）+ vendored 官方 ComputeSSIMULACRA2（含 6-scale
// 金字塔物化，典型峰值 130–160 B/px）。W1-A2 之后生产路径改用流式实现
// （metric.cpp），本文件只作为 oracle 在单测里跑同一组输入做 |Δ|≤0.01 对拍。
//
// 保留原因：验收要求“与现实现 oracle 对拍”。若删掉旧实现，就没有可执行的真值。
// 生产调用链（pipeline.cpp → metric::compute）**不引用**本文件的符号。
#include "core/metric/metric_oracle.h"

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
    const Msssim msssim = std::move(computed).value_();
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

Result compute_legacy_oracle(const FrameSource &reference, const FrameSource &distorted) {
    const Clock::time_point start = Clock::now();
    try {
        return ComputeImpl(reference, distorted, start);
    } catch (const std::exception &) {
        return Failed(start, "指标内部异常");
    } catch (...) {
        return Failed(start, "指标内部异常");
    }
}

} // namespace pp::metric
