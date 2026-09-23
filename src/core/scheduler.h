// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.3（依据 docs/v0.3.0-design.md §3.3）
//   本文件内 ScheduleConfig / Scheduler 为 0.3.0 一次性解冻（D20）授权变更面；
//   落地任务 = W1-T7（E3 调度器：交错限速 + 自适应线程预算，含 §8.2 alloc_threads）。
//   对应任务落地后：把本文件内的 PP-THAWED 标记改标为 PP-FROZEN(0.3.0)（冻结头 SPDX 延续）。
//   本文件其余既有声明（RunSummary）不在裁定表内 → 维持 PP-FROZEN(M1) 只读。
//   本节标注 [重排]：结构重排，不再保证聚合初始化兼容（随本次解冻一次性接受）。
//
// W1-T5 已落地面（主对话裁定：设计 §4.4 为准，任务书步骤 7 与文件面冲突为笔误）：
//   `inflight` 同目标串行化键由"单 desired 路径"改为 **逐 (target.out_path)**；
//   `reserved` 按每个 out_path 登记；worker 以 §3.2 的三参 run_one_file/run_metadata_only
//   + EventFn{on_event, &RunScope{budget, reserved, cancelled}} 调用管线。E3/stagger/
//   线程预算（§8.1/§8.2）仍归 W1-T7，本任务不落。
//
// W1-T6 已落地面（§3.3 的**进度面**，逐条）：
//   ① `ProgressInfo`（下表 §3.3 块）—— **已落地**，标记改标 PP-FROZEN(0.3.0)。
//      物理声明在 `src/core/progress.h`（设计 §3.3 的表头文件是本文件；搬迁理由与披露见
//      progress.h 文件头：`FileEvent::progress` 需要完整类型，而 scheduler.h 必须包含
//      pipeline.h → 留在本文件会成包含环。字段名/顺序/语义/默认值逐字照设计，唯一例外 =
//      `Stage::Queued`（现形 Stage 无该值，见 progress.h §3.3 块末注，【语义性】）。
//   ② `FileEvent::progress`（§3.3 的第二个块）—— 声明物理位于 `src/core/pipeline.h`
//      （T5 已披露的同一处置），字段与注释已按 §3.3 落地并改标 PP-FROZEN(0.3.0)。
//   ③ 本文件（scheduler.cpp）的**进度转发**：worker 的 EventFn 适配器不再只取 `state`，
//      而是整事件转发（progress 字段随行）；调度器为每个批内槽位维护"进度高水位"快照，
//      终态事件携带它（消费端读终态即得最后进度，且进度流恒单调）。
//   ④ §7.4 的 `FileEvent{state=Progress}` 依赖 `FileState::Progress` —— 现形 FileState
//      （pipeline.h）原无该值；主对话已裁定授权（W0 口径 b：枚举**末尾**追加），
//      见 pipeline.h 的 FileState / FileEvent 块（记【语义性】表缺口修复）。
#pragma once
#include "core/pipeline.h"
#include "core/progress.h"
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace pp {

// PP-FROZEN(0.3.0) §3.3 · ProgressInfo（**W1-T6 已落地**）
//   零改动声明：物理声明在 `src/core/progress.h`（搬迁理由见本文件头 W1-T6 ①）；此处仅留
//   交叉引用，避免同一结构两处声明。设计 §3.3 逐字形态（行首 "// " 为注释包装）：
// clang-format off
// struct ProgressInfo {                          // 追加
//     int   output_index = -1;                   // -1 = 源文件级阶段（probe/decode/color）
//     Stage stage = Stage::Queued;
//     float stage_frac  = 0.f;                   // 阶段内 0..1
//     float overall_frac = 0.f;                  // 文件整体 0..1（权重表 §7.2）
//     bool  synthetic   = false;                 // true = 合成进度（UI 斜纹）
// };
// clang-format on
//   唯一现形差异 = `Stage stage = Stage::Queued;` 的默认值（core/types.h 的 Stage 无 Queued，
//   且该文件不在 §3 解冻表内）→ 落地取 `Stage::Probe`，【语义性】表缺口，详见 progress.h。
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
//   与 `Alloc alloc_threads(int T, int remaining, int inflight)`（单测穷举；非 §3
//   表项，仅交叉引用）。
class Scheduler {
public:
    using EventCb = std::function<void(const FileEvent &)>; // 任意线程调用；UI 负责 queued 转发

    Scheduler(RunConfig cfg, std::vector<FileEntry> files);
    ~Scheduler();

    void set_event_callback(EventCb cb);
    void start();  // 非阻塞；创建 worker 池（N=cfg.workers 或物理核数）
    void cancel(); // 置取消标志；已在编码中的文件跑完
    void wait();   // 阻塞至全部结束
    bool running() const;

    const std::vector<FileResult> &results() const; // wait() 后有效（按输入顺序）
    RunSummary summary() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pp
