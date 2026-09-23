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
#pragma once
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
    bool acquire(uint64_t bytes, const std::function<bool()>& cancelled);
    void release(uint64_t bytes);

    uint64_t capacity() const;
    uint64_t used() const;      // 已占用
    uint64_t peak() const;      // 历史峰值（日志用）

    // float32 帧字节数 = w * h * channels * 4；旋转/合成峰值为 2×（G2）
    static uint64_t frame_bytes(int w, int h, int channels);
    static uint64_t default_capacity_bytes();  // min(可用RAM×50%, 8GB)

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    uint64_t capacity_, used_ = 0, peak_ = 0;
};

}  // namespace pp
