// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.3 **已落地**（依据 docs/v0.3.0-design.md §3.3）
//   落地任务 = W1-T5/T6（事件/进度面）+ W1-T7（E3 调度面）；原标注
//   PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记（冻结头 SPDX 延续）。
//   本文件其余既有声明（RunSummary）不在裁定表内 → 维持 PP-FROZEN(M1) 只读。
//   本节标注 [重排]：结构重排，不再保证聚合初始化兼容（随本次解冻一次性接受）。
//
// W1-T7 已落地面（§8 整节：交错启动 / 自适应线程预算 / 取消语义 / 线程映射义务）：
//   ① `ScheduleConfig`（下表 §3.3 块）—— **已落地**（字段名/顺序/默认值逐字照设计）；
//      `Scheduler` 对外方法（start/wait/cancel/running/results/summary/set_event_callback）
//      签名一字未动，符合设计「对外方法不变（私有实现自由）」。
//   ② §8.2 纯函数 `Alloc alloc_threads(int T, int remaining, int inflight)`：声明在本文件、
//      定义在 scheduler.cpp，**公式逐字照 §8.2**（单测穷举）。
//   ③ §8.1 交错限速器：`StaggerLimiter`（可注入时钟 → 单测 fake clock 穷举时序）；
//      生产路径 = steady_clock 毫秒读数；`stagger_ms<=0` 关闭（恒立即启动）。
//   ④ §8.2 E3 路由（scheduler.cpp::worker）：取件 → 交错等到点 → probe → Alloc→并发闸（信号量
//      限 W）→ `RunScope::encode_threads=E` → 管线。与像素预算正交（2×frame 照旧）。
//   ⑤ §8.3 取消：阶段边界检查不变（编码内不中断）；交错等待/闸等待中被取消 → 立即出
//      Cancelled 终态事件、**不进管线**（`cv.notify_all()` 唤醒全部等待者）。
//
// W1-T5 已落地面（主对话裁定：设计 §4.4 为准，任务书步骤 7 与文件面冲突为笔误）：
//   `inflight` 同目标串行化键由"单 desired 路径"改为 **逐 (target.out_path)**；
//   `reserved` 按每个 out_path 登记；worker 以 §3.2 的三参 run_one_file/run_metadata_only
//   + EventFn{on_event, &RunScope{budget, reserved, cancelled}} 调用管线。E3/stagger/
//   线程预算（§8.1/§8.2）仍归 W1-T7，本任务不落。（历史记录：T7 已落地，见本节上方。）
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
#include <algorithm>
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

// PP-FROZEN(0.3.0) §3.3 · ScheduleConfig + Scheduler（**W1-T7 已落地**）
//   0.3.0 冻结形态（设计 §3.3 逐字抄录；剥去行首 "// " 前缀即设计原文）：
// clang-format off
// struct ScheduleConfig {
//     int  workers = 0;          // 0 → thread_budget 或硬件并发
//     int  stagger_ms = 150;
//     int  thread_budget = 0;    // 0 → hardware_concurrency
// };
// class Scheduler { /* submit(files, RunConfig) / running() / cancel() 不变 */ };
// clang-format on
//   落地口径（T7，逐条）：
//   * `workers`：worker 池大小；0 → thread_budget（>0 时）或 hardware_concurrency（逻辑核）。
//   * `stagger_ms`：§8.1 交错启动步距（设计 §9.3 设置域 0–2000 ms，默认 150）；0 = 关闭。
//     本结构体自身不裁剪上下界（引擎照传入值执行，避免静默改值），仅把 <=0 视为"关闭"。
//   * `thread_budget`：§8.2 的 T；0 → hardware_concurrency。
//   * 派生点 = `Scheduler::Impl` 构造（由 RunConfig 的 workers/stagger_ms/thread_budget 同名
//     字段取值）——`Scheduler(RunConfig, files)` 签名是 0.2 冻结形态，故不引入第二构造入口。
struct ScheduleConfig {
    int workers = 0;       // 0 → thread_budget 或硬件并发
    int stagger_ms = 150;  // 0 = 立即启动（§8.1）
    int thread_budget = 0; // 0 → hardware_concurrency
};

// PP-FROZEN(0.3.0) §8.2 · `Alloc` + `alloc_threads`（**W1-T7 已落地**；非 §3 表项）
//   设计 §8.2 逐字（行首 "// " 为注释包装）：
// clang-format off
// struct Alloc { int files; int encode_threads; };
// Alloc alloc_threads(int T, int remaining, int inflight) {
//     int W = std::min(T, remaining + inflight);          // 目标并行度
//     int E = 1;
//     if (W * 2 <= T) E = std::clamp(T / std::max(W,1), 2, 16);  // 队列浅 → 放大编码线程
//     return { W, E };
// }
// clang-format on
//   语义（§8.2）：remaining = 未取件数、inflight = 已取件未完成数（二者之和 = 未完成文件数）；
//   W = 目标并行度（并发闸宽度），E = 本次入闸文件应交给编码器的内部线程数（≥1）。
//   不变式（单测穷举）：W ≤ T；E == 1 或 2 ≤ E ≤ 16；W*2 > T ⇒ E == 1；E ≥ 2 ⇒ W*E ≤ T。
//   E3 契约读数（§8.2）：活跃 W ≤ T 且 ΣE ≤ T；worker 主线程数同样 ≤ T
//   （公式逐字冻结，故二者之和在 W*2 > T 分支可为 2W —— 设计契约句的 ε 口径，见 m4-report）。
//   `struct Alloc` 逐字照设计**无默认成员初值**：本函数是唯一构造入口（勿值初始化后用）。
struct Alloc {
    int files;
    int encode_threads;
};
Alloc alloc_threads(int T, int remaining, int inflight);

// PP-FROZEN(0.3.0) §8.1 · 交错启动限速器（**W1-T7 已落地**）
//   设计 §8.1 逐字：`next_start = max(now, last_start + stagger_ms)`（worker 取件后
//   spin/sleep 到 next_start）；`stagger_ms = 0` 关闭（恒立即启动）。
//   形态说明（T7 落地口径）：落为**可注入时钟**的小类 —— 单测按设计 §13
//   「交错限速器时序（fake clock）」用整数毫秒假时钟穷举，生产路径喂 steady_clock 毫秒读数，
//   同一份代码两条路径（无测试专用分支）。
//   线程安全：本类自身不加锁；调用方（`Scheduler::Impl`）在 `mu` 下调用 reserve_start_ms。
class StaggerLimiter {
public:
    explicit StaggerLimiter(int stagger_ms) noexcept
        : stagger_ms_(stagger_ms > 0 ? stagger_ms : 0) {}

    // 预约本文件的启动时刻（同一时钟的毫秒读数）：max(now_ms, last_start + stagger)。
    // 首个文件恒 = now_ms（无前驱 → 无偏移）。
    long long reserve_start_ms(long long now_ms) noexcept {
        const long long start =
            started_ ? std::max(now_ms, last_start_ms_ + static_cast<long long>(stagger_ms_))
                     : now_ms;
        last_start_ms_ = start;
        started_ = true;
        return start;
    }

    int stagger_ms() const noexcept { return stagger_ms_; }
    bool started() const noexcept { return started_; }

private:
    int stagger_ms_;
    long long last_start_ms_ = 0;
    bool started_ = false;
};
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
