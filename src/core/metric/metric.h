// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 质量评分指标接口（C0 冻结；C1 实现，C2 消费）。
//
// 唯一实现：third_party/ssimulacra2（libjxl 0.12 官方源码 + compat 层，BSD-3）。
// 语义契约：docs/m8-encoder-params-and-ssimulacra2-design.md C9–C11。
//   * 输入为 **sRGB 线性** 交错 float RGB（行回调），参考/失真两侧必须走同一条
//     ColorManager 目标→sRGB 变换（C9）；
//   * 参考必须是“编码器实际收到的量化后样本”（C9），否则真无损不会得到 100；
//   * 分数 ∈ (−inf, 100]，负分合法；逐样本相同 = 100.0；<8×8 不允许调用（库会静默 100）。
//   * 实现应流式/分块，峰值内存落在现有 2×frame 预算内（C10）；无法满足时必须报告实测。
#pragma once
#include <functional>
#include <string>

namespace pp::metric {

enum class State : unsigned char {
    Off = 0,         // 评分关闭 / 仅元数据
    Ok = 1,          // 有有效分数
    Unavailable = 2, // 回读/尺寸/取消等导致无法评分（不算产物失败）
    Failed = 3,      // 指标内部错误（不算产物失败）
};

struct Result {
    State state = State::Off;
    double score = -1.0; // state==Ok 时有效
    double ms = 0.0;
    std::string note; // 中文短原因（Unavailable/Failed）
};

// 行源：填充第 y 行（0-based）的 width 个 sRGB 线性 RGB 交错 float（长度 width*3）。
// 回调不得抛异常；由调用方保证 y ∈ [0,height)。
using RowFn = std::function<void(int y, float *rgb)>;

struct FrameSource {
    int width = 0;
    int height = 0;
    RowFn row;
};

// 计算 SSIMULACRA 2。前置：两侧尺寸一致且 width,height >= 8；否则返回 Unavailable。
// 线程安全：纯函数（内部无全局可变状态）；在 worker 线程内调用。
Result compute(const FrameSource &reference, const FrameSource &distorted);

// 诊断名（日志/sidecar 用）："ssimulacra2" / 算法版本 1。
const char *metric_name();
int metric_version();
const char *state_name(State s);

} // namespace pp::metric
