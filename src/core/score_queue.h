// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/score_queue（M10 / W1-B1-T5：进程级评分池；M10-SCOREFIX 修订）
//
// Contract: docs/v0.4.0-design.md §4.2.2(2)（独立评分池 + 异步）+ §4.6 C3/C14/C15 +
//           docs/m10-adjudications.md A6（本版评分池配置走内部常量，不改 settings.h）。
// 本文件是**新文件**（不冻结；AGENTS §5.1「新能力尽量放新文件承载」）。
//
// 目标（逐条可验收）：
//   1. 评分**不阻塞输出完成**：pipeline 在"编码 + metawrite"后先发逐输出 OutputDone
//      （不含 metric），随后把评分任务投给本池；评分完成由池内线程发第二次 OutputDone
//      （含 metric，`scoring=false`）→ UI 同帧刷新评分列（C14）。
//   2. 独立内存配额：池持有自己的 `ScoreQuota`（容量 = min(文件预算容量/2, 4 GiB)；设计
//      §4.2.2(2) 的 score_budget 口径）。单任务需求 > 整池容量 → 提交被拒（调用方把该输出
//      标 Unavailable，**文件仍成功**，P0 兜底）；并发评分互相排队（阻塞 acquire，不把大图
//      评分误判为不可用）。
//   3. 并发度：K = min(4, max(1, hardware/4)) —— 内部常量，不暴露 UI（A6）。
//   4. 无长静默（C15）：池内**看门狗**线程每 ~150ms 为运行中的任务调用其 heartbeat 回调
//      （`progress.scoring=true` + `heartbeat=true`；不推进任何 frac）。
//   5. 取消（**per-run 代次**，M10-SCOREFIX）：`cancel_pending()` 只取消**当前运行代次**
//      （`begin_run()` / `reset_for_run()` 建立的新代次）；队列中该代次的任务诚实丢弃并把
//      原因写回其 OutputResult（"运行取消，评分未完成"），运行中的任务经 `ScoreRun::cancelled()`
//      在检查点退出。**下一次运行开始自动复位**（Scheduler::start / dev_cli / GUI 三处接线），
//      故"取消一次 → 同进程后续运行评分全部 Unavailable"的一次性缺陷已消除。
//   6. 丢弃原因区分（M10-SCOREFIX）：`submit_ex()` 返回 `SubmitStatus`
//      （Accepted / PoolStopping / RunCancelled / QuotaExceeded）——调用方按三类原因写准确
//      中文 note（配额不足 / 运行取消 / 池关闭），不得一律写"内存不足"。
//   7. 所有权（M10-SCOREFIX）：评分任务**不持有 Scheduler::Impl\*** —— 事件出口是
//      Scheduler 侧 shared_ptr `EventSink`（退役后迟到事件为 no-op），取消谓词是 shared_ptr
//      `ScoreRun` 代次令牌，参考帧是 shared_ptr。Scheduler 析构仍会 `cancel_run` + 有界
//      `wait_run_idle`（双保险，见 scheduler.cpp）。
//   8. 证据面：每个任务完成后发布其最终 OutputResult（按产物路径键 + 代次过滤），供 `--dev`
//      在 `Scheduler::wait()` 之后把侧车补齐（sidecar 的 metric 对象）—— 见 dev_cli.cpp。
//
// 线程纪律（§8.2 E3 的 ε 类）：池线程是评分/监控线程，不参与编码、不占 `encode_threads`；
// 进程级单例在首次使用时懒建立，退出时由静态析构停线程（shutdown 显式可调用）。
#pragma once
#include "core/pipeline.h" // OutputResult（任务结果行的类型面）
#include "core/pixelbudget.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace pp {

// —— M10-SCOREFIX：一次运行（generation）的取消令牌 ——
// 值语义 = shared_ptr 拷贝；`cancelled_` 只由 ScorePool 翻转（begin_run/cancel_pending/
// cancel_run/shutdown），任务侧只读 → 评分任务不需要也不持有 Scheduler 的任何指针。
class ScoreRun {
public:
    bool cancelled() const noexcept { return cancelled_.load(std::memory_order_acquire); }
    std::uint64_t generation() const noexcept { return generation_; }

private:
    friend class ScorePool;
    explicit ScoreRun(std::uint64_t gen) : generation_(gen) {}
    std::atomic<bool> cancelled_{false};
    const std::uint64_t generation_;
};
using ScoreRunPtr = std::shared_ptr<ScoreRun>;

// 一个评分任务。
//   * `run`：池内执行体（必须自带异常兜底；内部完成产物回读 + 色彩归一 + 指标 + 事件回发）；
//   * `result`：任务填充的**最终行**（含 metric 字段）；池在 run 返回后发布一份 const 副本；
//   * `heartbeat`：可选，看门狗周期调用（发 scoring=true 的 Progress 心跳；可为空）；
//   * `need_bytes`：该任务的内存需求（配额记账；含参考帧快照 + 指标内部缓冲）；
//   * `run_state`：所属运行代次（`submit` 时为空 → 绑定当前代次）。取消/丢弃按代次判定。
struct ScoreTask {
    std::string out; // 产物路径（发布键；同一次 run 内唯一）
    std::uint64_t need_bytes = 0;
    std::shared_ptr<OutputResult> result;
    std::function<void()> run;
    std::function<void()> heartbeat;
    ScoreRunPtr run_state; // M10-SCOREFIX（加性字段）
};

class ScorePool {
public:
    // —— M10-SCOREFIX：提交结果（丢弃原因三分类；调用方据此写准确中文 note）——
    enum class SubmitStatus {
        Accepted = 0,  // 已入队
        PoolStopping,  // 池正在关闭（进程退出/显式 shutdown）
        RunCancelled,  // 该任务所属运行代次已取消（用户取消/上一代已结束）
        QuotaExceeded, // 单任务需求 > 整池容量（永远等不到 → P0 兜底）
        InvalidTask,   // 无执行体（编程错误）
    };
    static const char *submit_status_name(SubmitStatus s); // "accepted"/"pool-stopping"/…

    // —— M10-SCOREFIX：drain 结果（GUI 收尾的可测语义；超时/宽限读数）——
    struct DrainResult {
        bool quiesced = false;      // 池是否已收敛（队列空 + 无在飞任务）
        bool forced_cancel = false; // 是否触发超时兜底（取消 + 宽限）
        std::uint64_t wait_ms = 0;  // 正常等待耗时
        std::uint64_t grace_ms = 0; // 超时后宽限耗时
        std::size_t pending_at_start = 0;
        std::size_t pending = 0; // 返回时的 pending（>0 = 未收敛，如实行事）
    };

    // 进程级单例（懒建立；K/容量见文件头）。
    static ScorePool &instance();

    int workers() const;            // K = min(4, max(1, hw/4))
    std::uint64_t capacity() const; // 评分配额容量（字节）
    std::uint64_t used() const;     // 当前占用
    std::uint64_t peak() const;     // 历史峰值（证据用）
    std::size_t pending() const;    // 队列中 + 运行中（含等待配额的已出队任务）

    // —— M10-SCOREFIX：per-run 代次 ——
    // 开始新运行代次：取消上一代（队列丢弃 + 在飞任务经令牌退出）、清空上一代的发布表，
    // 返回新代次的令牌。GUI/dev_cli 在运行开始前调用（Scheduler::start 也会调用一次）。
    ScoreRunPtr begin_run();
    ScoreRunPtr reset_for_run(); // = begin_run()（旧名别名；GUI/dev_cli/单测接线用）
    // 当前代次令牌（无 → 懒建立隐式代次 0，供直接调用 run_one_file 的单测/工具使用）。
    ScoreRunPtr current_run();
    std::size_t pending_of_run(const ScoreRunPtr &run) const; // 该代次队列 + 在飞任务数

    // 提交（**不阻塞**文件线程）。`submit_ex` 给出精确丢弃原因；`submit` 为旧布尔面
    // （== Accepted），既有调用点/测试零改动。
    SubmitStatus submit_ex(ScoreTask task);
    bool submit(ScoreTask task); // 兼容旧面：仅 Accepted 返回 true

    // 等池收敛（队列清空 + 无运行任务）。超时返回 false（调用方如实记录，不伪造完整证据）。
    bool wait_idle(std::chrono::milliseconds timeout);
    // 等指定代次收敛（Scheduler 析构用；不等待其他代次）。
    bool wait_run_idle(const ScoreRunPtr &run, std::chrono::milliseconds timeout);

    // 取消：只取消**当前代次**的待执行任务（队列丢弃）并置该代次 cancelled；在飞任务由
    // 任务自身的 `ScoreRun::cancelled()` 谓词退出。幂等（重复调用无副作用）。
    void cancel_pending();
    // 取消指定代次（幂等）；返回本次是否由未取消变为取消。
    bool cancel_run(const ScoreRunPtr &run);
    bool cancelled() const; // 当前代次是否已取消（无代次 → false）

    // —— M10-SCOREFIX：可测的收尾（GUI 关运行页/关窗用）——
    // 语义：① 分片 wait_idle + pump（GUI 事件泵）直到 timeout；② 仍未收敛 → cancel_pending()
    // 后再给 grace 宽限；③ 返回如实读数（不把"没等到"粉饰成"已收敛"）。
    DrainResult drain(std::chrono::milliseconds timeout, std::chrono::milliseconds grace,
                      const std::function<void()> &pump, std::chrono::milliseconds slice);

    // 已发布的评分结果（按产物路径；只返回**当前代次**的行）；无 → nullptr。
    // `--dev` 用它把 sidecar 的 metric 补齐。
    std::shared_ptr<const OutputResult> published(const std::string &out) const;

    // 停止接收新任务 + 丢弃队列 + join 全部线程（析构/测试收尾；幂等）。
    // 队列中被丢弃的任务会把"评分池已关闭，评分未完成"写回其结果行（不产生半评分）。
    void shutdown();

    ScorePool(const ScorePool &) = delete;
    ScorePool &operator=(const ScorePool &) = delete;

private:
    ScorePool();
    ~ScorePool();
    void worker_loop();
    void watchdog_loop();

    struct RunItem {
        ScoreTask task;
        std::uint64_t bytes = 0; // 已扣的配额（结束归还）
    };

    ScoreRunPtr ensure_current_locked();
    // 取走队列中属于 run 的任务（run==nullptr → 全部）；返回被丢弃的任务。
    std::deque<ScoreTask> take_queued_locked(const ScoreRunPtr &run);
    bool queued_has_run_locked(const ScoreRunPtr &run) const;
    bool running_has_run_locked(const ScoreRunPtr &run) const;
    void mark_discarded(const std::deque<ScoreTask> &dropped, const char *note);
    void publish_row(const ScoreTask &task, const char *note); // note=nullptr → 发布任务结果
    void erase_running(const ScoreTask &task);

    mutable std::mutex mu_;
    std::condition_variable cv_;      // worker 取任务
    std::condition_variable idle_cv_; // wait_idle
    std::deque<ScoreTask> queue_;
    std::vector<RunItem> running_; // 含"已出队、正在等配额"的任务（pending 不漏计）
    // 发布表（按产物路径 + 代次过滤；M10-SCOREFIX：旧代次迟到发布不污染新运行）。
    struct PublishedRow {
        std::uint64_t generation = 0;
        std::shared_ptr<const OutputResult> row;
    };
    std::map<std::string, PublishedRow> published_;

    ScoreQuota quota_;
    int workers_ = 1;
    std::atomic<bool> stopping_{false};
    ScoreRunPtr current_run_; // guarded by mu_
    std::uint64_t next_generation_ = 1;
    bool watchdog_stop_ = false;
    std::condition_variable watchdog_cv_;

    std::vector<std::thread> pool_;
    std::thread watchdog_;
};

} // namespace pp
