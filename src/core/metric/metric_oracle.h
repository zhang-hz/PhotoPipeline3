// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10-W1-A2 质量评分指标：HEAD 旧实现 oracle（测试对拍专用）。
//
// 用途：W1-A2 把指标模块从「物化两整帧 + 官方 6-scale 金字塔」改为流式/分块实现。
// 数值对拍要求新实现与**旧实现**在相同输入下的分数 |Δ| ≤ 0.01，因此旧实现必须
// 可执行地保留下来作为 oracle；它**不是**生产路径（生产路径 = metric::compute，
// 见 metric.cpp）。旧实现保留了 HEAD 的全部数值语义（官方 vendored
// ComputeSSIMULACRA2 本体 + 物化 Image3F/ImageBundle），典型峰值 130–160 B/px。
//
// 注意：本函数仅由 tests/unit/test_quality_metric*.cpp 调用；pipeline 不引用。
#pragma once

#include "core/metric/metric.h"

namespace pp::metric {

// HEAD（1208894）metric::compute 的逐字保留版本：物化参考/失真两整帧后交给
// vendored 官方 ComputeSSIMULACRA2。语义（State/score/ms/note）与旧版一致。
Result compute_legacy_oracle(const FrameSource &reference, const FrameSource &distorted);

} // namespace pp::metric
