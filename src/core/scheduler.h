// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.3（依据 docs/v0.3.0-design.md §3.3）
//   本文件内 ProgressInfo / FileEvent / ScheduleConfig / Scheduler 为 0.3.0 一次性解冻（D20）
//   授权变更面；落地任务 = W1-T7（E3 调度器：交错限速 + 自适应线程预算，含 §8.2 alloc_threads）。
//   对应任务落地后：把本文件内的 PP-THAWED 标记改标为 PP-FROZEN(0.3.0)（冻结头 SPDX 延续）。
//   本文件其余既有声明（RunSummary）不在裁定表内 → 维持 PP-FROZEN(M1) 只读。
//   本节标注 [重排]：结构重排，不再保证聚合初始化兼容（随本次解冻一次性接受）。
#pragma once
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>
#include "core/pipeline.h"

namespace pp {

// PP-THAWED(0.3.0-M4-D20) §3.3 · ProgressInfo
//   0.3.0 新增（三级进度模型的进度载体：运行级/源文件级/输出级，§7.1）。
//   落地任务 W1-T7（进度合成/节流见 W1-T6 的 core/progress.h）→ 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.3 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct ProgressInfo {                          // 追加
//     int   output_index = -1;                   // -1 = 源文件级阶段（probe/decode/color）
//     Stage stage = Stage::Queued;
//     float stage_frac  = 0.f;                   // 阶段内 0..1
//     float overall_frac = 0.f;                  // 文件整体 0..1（权重表 §7.2）
//     bool  synthetic   = false;                 // true = 合成进度（UI 斜纹）
// };
// clang-format on
//   注（0.2 现形差异，由 T6/T7 落地前裁定后处理）：`Stage::Queued` 在 core/types.h 现形的
//   Stage{Probe, Decode, Orient, Color, Flatten, Encode, MetaWrite, Done} 中**不存在**；
//   §3.6 未授权改 Stage（types.h 明确"不变"面）→ 需主对话裁定（M4-T1 已上报，W5 收口入 m4-report；本任务零改动）。
//   FileEvent（§3.3 的表头文件是本文件，但其声明物理位于 pipeline.h）的 progress 追加与
//   FileState 缺口见 src/core/pipeline.h 内 §3.3 块。
struct RunSummary {
    std::size_t total = 0, ok = 0, failed = 0, skipped = 0, cancelled = 0;
    uint64_t out_bytes = 0;
    double total_ms = 0, throughput_mb_s = 0, avg_file_ms = 0;
};

// PP-THAWED(0.3.0-M4-D20) §3.3 · ScheduleConfig + Scheduler
//   ScheduleConfig [重排]：0.3.0 新增（0.2 现形无此结构体：Scheduler 直接吃 RunConfig）；
//   Scheduler 对外方法不变：submit(files, RunConfig) / running() / cancel()（私有实现自由）。
//   落地任务 W1-T7 → 落地后改标 PP-FROZEN(0.3.0)。
//   0.3.0 冻结形态（设计 §3.3 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct ScheduleConfig {
//     int  workers = 0;          // 0 → thread_budget 或硬件并发
//     int  stagger_ms = 150;
//     int  thread_budget = 0;    // 0 → hardware_concurrency
// };
// class Scheduler { /* submit(files, RunConfig) / running() / cancel() 不变 */ };
// clang-format on
//   注：T7 另在本文件落地 §8.2 的纯函数 `struct Alloc { int files; int encode_threads; };`
//   与 `Alloc alloc_threads(int T, int remaining, int inflight)`（单测穷举；非 §3 表项，仅交叉引用）。
class Scheduler {
public:
    using EventCb = std::function<void(const FileEvent&)>;   // 任意线程调用；UI 负责 queued 转发

    Scheduler(RunConfig cfg, std::vector<FileEntry> files);
    ~Scheduler();

    void set_event_callback(EventCb cb);
    void start();          // 非阻塞；创建 worker 池（N=cfg.workers 或物理核数）
    void cancel();         // 置取消标志；已在编码中的文件跑完
    void wait();           // 阻塞至全部结束
    bool running() const;

    const std::vector<FileResult>& results() const;   // wait() 后有效（按输入顺序）
    RunSummary summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pp
