// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `core/pixelbudget.h`
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/pixelbudget.h` | **不变** |
// clang-format on
//   → 本行裁决 = 0.3.0 **不变**：维持 PP-FROZEN，**零改动**（本任务只加本注释，不改任何东西）。
//   配套语义（§4.1/§8.2）：像素预算公式与 2×frame 峰值不变；与线程分配正交（内存背压照旧
//   `2×frame` acquire）。
//   —— M10 / W1-T1 加性追加（**既有 PixelBudget 零改动**）：文件末尾追加 `ScoreQuota`（评分
//   独立内存配额，非阻塞）+ `default_score_quota()`；依据 docs/v0.4.0-design.md §4.2.2(2)/§4.3
//   与 W1-T1 任务卡（P0 热修：文件 need 恢复 2×frame，评分内存独立记账 + 大图 Unavailable 兜底）。
//   本文件既有类/签名/语义一字未动 → 不触发既有冻结面；裁定登记见 docs/m10-adjudications.md。
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>

namespace pp {

// 全局像素预算（令牌池）。容量 = min(可用RAM×50%, 8GB)，由 Scheduler 构造时决定。
class PixelBudget {
public:
    explicit PixelBudget(uint64_t capacity_bytes);

    // 阻塞直到可用；期间每秒检查一次 cancelled（true → 返回 false，不扣额）
    // bytes > capacity → 直接返回 false（调用方报错，不得死等）
    bool acquire(uint64_t bytes, const std::function<bool()> &cancelled);
    void release(uint64_t bytes);

    uint64_t capacity() const;
    uint64_t used() const; // 已占用
    uint64_t peak() const; // 历史峰值（日志用）

    // float32 帧字节数 = w * h * channels * 4；旋转/合成峰值为 2×（G2）
    static uint64_t frame_bytes(int w, int h, int channels);
    static uint64_t default_capacity_bytes(); // min(可用RAM×50%, 8GB)

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    uint64_t capacity_, used_ = 0, peak_ = 0;
};

// ============================================================================================
// M10 / W1-T1（P0 热修，**末尾加性追加**；裁定登记 docs/m10-adjudications.md A17 同批）：
// 评分独立内存配额（非阻塞令牌池）。
//
// 背景（docs/v0.4.0-design.md §4.2.1 / §4.3）：HEAD 把评分模块的 ~160 B/px 内存预留
// **记进文件像素预算**（`need = 2×frame + 160B/px`）→ 24MP 评分批并发被压到 1、>45MP
// 评分文件直接 `pixel budget exceeded` 失败（P0）。热修后文件预算恢复 `2×frame`，
// 评分内存改由本配额独立记账：
//   * 容量 = min(文件预算容量 / 2, 4 GiB)（§4.2.2(2) 的 score_budget 口径的首版实现）；
//   * **非阻塞** `try_acquire()`：配额不足/被并发占满 → 立即返回 false，调用方把该输出评分
//     降级为 `MetricState::Unavailable`（产物仍成功）；绝不阻塞文件线程（不再"因评分而串行"）；
//   * 线程安全与 PixelBudget 同口径（内部 mutex；release 唤醒无消费者 —— 非阻塞语义）。
// 消费点 = pipeline.cpp 的 score_output_row（见 kMetricBytesPerPixel / A1 说明）；W1-T5 的
// 进程级 ScorePool 落地后，由其池内配额取代本单例的记账（接口保持加性：类不删）。
// ============================================================================================
class ScoreQuota {
public:
    explicit ScoreQuota(uint64_t capacity_bytes);

    // 非阻塞记账：bytes 能放下 → 扣额并返回 true；否则 false（不等待、不扣额）。
    bool try_acquire(uint64_t bytes);
    // —— M10 / W1-B1（加性；docs/v0.4.0-design.md §4.2.2(2)）——
    // 阻塞记账（评分池 worker 用）：容量装得下就等（放不下 = 并发评分互相排队，而不是把
    // 大图评分直接判 Unavailable）；单任务 bytes > 整池容量 → 立即 false（等也等不到）；
    // 超时 → false 且不扣额。release() 会唤醒等待者。
    bool acquire(uint64_t bytes, std::chrono::milliseconds timeout);
    void release(uint64_t bytes);

    uint64_t capacity() const;
    uint64_t used() const; // 已占用
    uint64_t peak() const; // 历史峰值（日志/证据用）

    // 容量单源：min(文件预算容量 / 2, 4 GiB)。未知容量（0）→ 4 GiB（与 PixelBudget 的
    // "未知按 ceiling" 同口径）。4 GiB = 22.8MP 单任务 ~3.65GB 的可行上界；>45MP 评分
    // （160B/px ≈ 7.7GB）必然 Unavailable —— 即设计要求的 P0 兜底。
    static uint64_t default_capacity_bytes(uint64_t file_capacity_bytes);

private:
    mutable std::mutex mu_;
    std::condition_variable cv_; // M10 / W1-B1：阻塞 acquire 的唤醒面（非阻塞路径不用）
    uint64_t capacity_, used_ = 0, peak_ = 0;
};

// 进程级默认评分配额（首次调用时构造，容量 = default_capacity_bytes(PixelBudget 默认容量)）。
// pipeline 的评分段使用它；单测可直接构造独立实例验证记账语义。
ScoreQuota &default_score_quota();

} // namespace pp
