// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/score_queue（M10 / W1-B1-T5：进程级评分池实现）
//
// 设计依据：docs/v0.4.0-design.md §4.2.2(2) / §4.7；裁定 A6（内部常量，不改 settings.h）。
// M10-SCOREFIX（task-20）：per-run 代次取消/复位 + 丢弃原因三分类 + 可测 drain 语义。
//
// 目录：
//   1. 单例与线程池（K = min(4, max(1, hardware/4))；任务队列 + 评分配额）
//   2. 代次（ScoreRun）/ 提交状态 / 取消与复位
//   3. 看门狗（C15：评分期 ~150ms 一次 heartbeat；`scoring=true`，不推进 frac）
//   4. 收敛 / drain / 发布（证据面）
//
// 关键不变量：
//   * 文件线程**永不**在 submit() 上等待（池满只排队；配额拿不到由 worker 分片等待，且
//     单任务 > 整池容量在 submit 就被拒）；
//   * 评分任务的任何失败/取消都只落在该 OutputResult 的 metric_* 字段上（产物成败不受影响）；
//   * published() 只发布**当前代次**的行、只在任务 run 返回后出现（不发布半评分）；
//   * 取消按代次隔离：`cancel_pending()` 不越界到下一代；`begin_run()` 复位取消状态并丢弃
//     上一代队列（消除"取消一次 → 同进程后续运行评分全 Unavailable"的一次性缺陷）。

#include "core/score_queue.h"

#include "core/logger.h"

#include <algorithm>
#include <thread>
#include <utility>

namespace pp {

namespace {

constexpr std::string_view kStage = "score";
constexpr std::string_view kFile = "score_queue.cpp";

// 评分池并发度：K = min(4, max(1, hardware/4))（§4.2.2(2)；内部常量，不暴露 UI）。
int pool_workers() {
    const unsigned hw = std::thread::hardware_concurrency();
    const int cores = hw == 0 ? 4 : static_cast<int>(hw);
    return std::clamp(cores / 4, 1, 4);
}

// 看门狗周期（C15 判据 ≤500ms 的一半以下；心跳本身不推进任何 frac）。
constexpr int kWatchdogTickMs = 150;
// worker 等待配额的切片：分片等待让取消/关池在 ≤50ms 内脱身（旧实现一次等 10 分钟，
// shutdown 时若配额已满会卡住 join）。
constexpr int kQuotaWaitSliceMs = 50;

// —— M10-SCOREFIX：丢弃/取消原因文案（调用方与池内共用；不得写成"内存不足"）——
constexpr char kNoteRunCancelled[] = "运行取消，评分未完成";
constexpr char kNotePoolStopped[] = "评分池已关闭，评分未完成";
constexpr char kNoteQuotaWaitTimeout[] = "评分等待内存配额超时，评分未完成";

} // namespace

// ---------------------------------------------------------------------------
// 1. 单例 / 线程池
// ---------------------------------------------------------------------------
ScorePool &ScorePool::instance() {
    static ScorePool pool; // 进程级懒建立（C++11 起线程安全的局部静态）
    return pool;
}

ScorePool::ScorePool()
    : quota_(ScoreQuota::default_capacity_bytes(PixelBudget::default_capacity_bytes())),
      workers_(pool_workers()) {
    pool_.reserve(static_cast<std::size_t>(workers_));
    for (int i = 0; i < workers_; ++i)
        pool_.emplace_back([this] { worker_loop(); });
    watchdog_ = std::thread([this] { watchdog_loop(); });
    log_debug(kStage, kFile, "score pool started",
              {{"workers", std::to_string(workers_)},
               {"quota_bytes", std::to_string(quota_.capacity())}});
}

ScorePool::~ScorePool() {
    try {
        shutdown();
    } catch (...) {
        // 析构期间不得让异常逃逸（否则 std::terminate）：join 失败按“尽力清理”处理；
        // 正常路径语义不变（shutdown 成功即与原实现相同）。
        (void)0; // 显式空操作：注释不计入语句
    }
}

int ScorePool::workers() const { return workers_; }
std::uint64_t ScorePool::capacity() const { return quota_.capacity(); }
std::uint64_t ScorePool::used() const { return quota_.used(); }
std::uint64_t ScorePool::peak() const { return quota_.peak(); }

std::size_t ScorePool::pending() const {
    std::lock_guard<std::mutex> lk(mu_);
    return queue_.size() + running_.size();
}

// ---------------------------------------------------------------------------
// 2. 代次 / 提交状态 / 取消与复位（M10-SCOREFIX）
// ---------------------------------------------------------------------------
const char *ScorePool::submit_status_name(SubmitStatus s) {
    switch (s) {
    case SubmitStatus::Accepted:
        return "accepted";
    case SubmitStatus::PoolStopping:
        return "pool-stopping";
    case SubmitStatus::RunCancelled:
        return "run-cancelled";
    case SubmitStatus::QuotaExceeded:
        return "quota-exceeded";
    case SubmitStatus::InvalidTask:
        return "invalid-task";
    }
    return "invalid-task";
}

// 调用方必须持 mu_。
ScoreRunPtr ScorePool::ensure_current_locked() {
    if (!current_run_)
        current_run_ = std::shared_ptr<ScoreRun>(new ScoreRun(next_generation_++));
    return current_run_;
}

ScoreRunPtr ScorePool::current_run() {
    std::lock_guard<std::mutex> lk(mu_);
    return ensure_current_locked();
}

ScoreRunPtr ScorePool::begin_run() {
    std::deque<ScoreTask> dropped;
    ScoreRunPtr next;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (current_run_)
            current_run_->cancelled_.store(true, std::memory_order_release);
        dropped = take_queued_locked(nullptr); // 上一代（及更早）的待执行任务全部作废
        generation = next_generation_++;
        next = std::shared_ptr<ScoreRun>(new ScoreRun(generation));
        current_run_ = next;
        published_.clear(); // 发布表按运行代次隔离（旧行不得并入新运行的 sidecar）
    }
    cv_.notify_all();
    idle_cv_.notify_all();
    mark_discarded(dropped, kNoteRunCancelled);
    log_debug(
        kStage, kFile, "score pool run generation began",
        {{"generation", std::to_string(generation)}, {"dropped", std::to_string(dropped.size())}});
    return next;
}

ScoreRunPtr ScorePool::reset_for_run() { return begin_run(); }

std::size_t ScorePool::pending_of_run(const ScoreRunPtr &run) const {
    if (!run)
        return 0;
    std::lock_guard<std::mutex> lk(mu_);
    std::size_t n = 0;
    for (const ScoreTask &t : queue_) {
        if (t.run_state && t.run_state->generation() == run->generation())
            ++n;
    }
    for (const RunItem &it : running_) {
        if (it.task.run_state && it.task.run_state->generation() == run->generation())
            ++n;
    }
    return n;
}

bool ScorePool::queued_has_run_locked(const ScoreRunPtr &run) const {
    for (const ScoreTask &t : queue_) {
        if (t.run_state == run)
            return true;
    }
    return false;
}

bool ScorePool::running_has_run_locked(const ScoreRunPtr &run) const {
    for (const RunItem &it : running_) {
        if (it.task.run_state == run)
            return true;
    }
    return false;
}

std::deque<ScoreTask> ScorePool::take_queued_locked(const ScoreRunPtr &run) {
    std::deque<ScoreTask> dropped;
    std::deque<ScoreTask> keep;
    for (ScoreTask &t : queue_) {
        if (!run || (t.run_state && t.run_state->generation() == run->generation()))
            dropped.push_back(std::move(t));
        else
            keep.push_back(std::move(t));
    }
    queue_.swap(keep);
    return dropped;
}

ScorePool::SubmitStatus ScorePool::submit_ex(ScoreTask task) {
    if (!task.run)
        return SubmitStatus::InvalidTask;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopping_.load(std::memory_order_acquire))
            return SubmitStatus::PoolStopping;
        if (!task.run_state)
            task.run_state = ensure_current_locked();
        // 旧代次（begin_run 之后到达）或已取消代次 → 拒收；调用方写"运行取消"文案。
        if (task.run_state != current_run_ || task.run_state->cancelled())
            return SubmitStatus::RunCancelled;
        // 单任务需求 > 整池容量 → 永远等不到（设计 §4.2.2(2) 的 P0 兜底）。
        if (task.need_bytes > quota_.capacity())
            return SubmitStatus::QuotaExceeded;
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
    return SubmitStatus::Accepted;
}

bool ScorePool::submit(ScoreTask task) {
    return submit_ex(std::move(task)) == SubmitStatus::Accepted;
}

bool ScorePool::cancelled() const {
    std::lock_guard<std::mutex> lk(mu_);
    return current_run_ && current_run_->cancelled();
}

void ScorePool::cancel_pending() {
    std::deque<ScoreTask> dropped;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ScoreRunPtr cur = ensure_current_locked();
        cur->cancelled_.store(true, std::memory_order_release);
        dropped = take_queued_locked(cur); // 只作废**当前代次**的队列
    }
    cv_.notify_all();
    idle_cv_.notify_all();
    mark_discarded(dropped, kNoteRunCancelled);
}

bool ScorePool::cancel_run(const ScoreRunPtr &run) {
    if (!run)
        return false;
    bool newly = false;
    std::deque<ScoreTask> dropped;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!run->cancelled_.exchange(true, std::memory_order_acq_rel))
            newly = true;
        dropped = take_queued_locked(run);
    }
    cv_.notify_all();
    idle_cv_.notify_all();
    mark_discarded(dropped, kNoteRunCancelled);
    return newly;
}

void ScorePool::mark_discarded(const std::deque<ScoreTask> &dropped, const char *note) {
    for (const ScoreTask &task : dropped) {
        publish_row(task, note); // 只落发布表（当前代次），不发 OutputDone（消费端终态/收尾定稿）
    }
}

// note == nullptr → 发布任务自己的结果行；否则把 note 写进该行（MetricState::Unavailable，
// 产物成败不受影响）。代次过滤：旧代次的迟到发布不进入当前运行的发布表。
void ScorePool::publish_row(const ScoreTask &task, const char *note) {
    if (!task.result)
        return;
    if (note != nullptr) {
        task.result->metric_state = MetricState::Unavailable;
        task.result->metric_ssimulacra2 = -1.0;
        task.result->metric_ms = 0.0;
        task.result->metric_note = note;
    }
    std::lock_guard<std::mutex> lk(mu_);
    const std::uint64_t generation = task.run_state
                                         ? task.run_state->generation()
                                         : (current_run_ ? current_run_->generation() : 0);
    if (!current_run_ || generation != current_run_->generation())
        return; // 旧代次的迟到行/丢弃行：不污染新运行
    PublishedRow &slot = published_[task.out];
    if (slot.generation > generation)
        return;
    slot.generation = generation;
    slot.row = std::make_shared<const OutputResult>(*task.result);
}

void ScorePool::erase_running(const ScoreTask &task) {
    for (auto it = running_.begin(); it != running_.end(); ++it) {
        if (it->task.result == task.result && it->task.out == task.out) {
            running_.erase(it);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// 3. worker（取件 → 代次检查 → 配额分片等待 → 执行 → 发布）
// ---------------------------------------------------------------------------
void ScorePool::worker_loop() {
    for (;;) {
        ScoreTask task;
        bool cancelled_at_pop = false;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [this] { return stopping_.load() || !queue_.empty(); });
            if (stopping_.load())
                return;
            task = std::move(queue_.front());
            queue_.pop_front();
            if (task.run_state && task.run_state->cancelled()) {
                cancelled_at_pop = true;
            } else {
                // M10-SCOREFIX：先登记 running（配额等待也算 pending）——`wait_idle` 不会把
                // "已出队、还在等配额"的任务漏掉（旧实现的 pending 空窗会让收尾提前放行）。
                RunItem item;
                item.task = task;
                item.bytes = 0;
                running_.push_back(std::move(item));
            }
        }
        if (cancelled_at_pop) {
            // 竞态：cancel 与出队交错 → 这里补齐丢弃原因（不产生半评分、不跑任务体）。
            publish_row(task, kNoteRunCancelled);
            idle_cv_.notify_all();
            continue;
        }

        // 配额：分片阻塞等待（并发评分互相排队；取消/关池 ≤50ms 脱身）。
        bool acquired = (task.need_bytes == 0);
        const char *abandon_note = nullptr;
        while (!acquired) {
            if (stopping_.load(std::memory_order_acquire)) {
                abandon_note = kNotePoolStopped;
                break;
            }
            if (task.run_state && task.run_state->cancelled()) {
                abandon_note = kNoteRunCancelled;
                break;
            }
            acquired =
                quota_.acquire(task.need_bytes, std::chrono::milliseconds(kQuotaWaitSliceMs));
        }
        if (!acquired) {
            publish_row(task, abandon_note != nullptr ? abandon_note : kNoteQuotaWaitTimeout);
            {
                std::lock_guard<std::mutex> lk(mu_);
                erase_running(task);
            }
            idle_cv_.notify_all();
            continue;
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            for (RunItem &it : running_) {
                if (it.task.result == task.result && it.task.out == task.out) {
                    it.bytes = task.need_bytes;
                    break;
                }
            }
        }
        try {
            if (task.run)
                task.run();
        } catch (const std::exception &e) {
            log_error(kStage, kFile, "score task threw; metric not published",
                      {{"out", task.out}, {"error", e.what()}});
        } catch (...) {
            log_error(kStage, kFile, "score task threw (non-standard); metric not published",
                      {{"out", task.out}});
        }
        if (task.need_bytes > 0)
            quota_.release(task.need_bytes);
        {
            std::lock_guard<std::mutex> lk(mu_);
            erase_running(task);
        }
        publish_row(task, nullptr);
        idle_cv_.notify_all();
    }
}

// ---------------------------------------------------------------------------
// 4. 看门狗（C15：评分期心跳；scoring=true、heartbeat=true，不推进任何 frac）
// ---------------------------------------------------------------------------
void ScorePool::watchdog_loop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lk(mu_);
            if (watchdog_cv_.wait_for(lk, std::chrono::milliseconds(kWatchdogTickMs),
                                      [this] { return watchdog_stop_; })) {
                return;
            }
        }
        std::vector<std::function<void()>> beats;
        {
            std::lock_guard<std::mutex> lk(mu_);
            beats.reserve(running_.size());
            for (const RunItem &it : running_) {
                if (it.task.heartbeat)
                    beats.push_back(it.task.heartbeat);
            }
        }
        for (const auto &beat : beats) {
            try {
                beat();
            } catch (...) {
                // 心跳失败不得影响评分任务
                (void)0; // 显式空操作：注释不计入语句
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 5. 收敛 / drain / 发布 / 关池
// ---------------------------------------------------------------------------
bool ScorePool::wait_idle(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(mu_);
    return idle_cv_.wait_for(lk, timeout, [this] { return queue_.empty() && running_.empty(); });
}

bool ScorePool::wait_run_idle(const ScoreRunPtr &run, std::chrono::milliseconds timeout) {
    if (!run)
        return true;
    std::unique_lock<std::mutex> lk(mu_);
    return idle_cv_.wait_for(lk, timeout, [this, &run] {
        return !queued_has_run_locked(run) && !running_has_run_locked(run);
    });
}

ScorePool::DrainResult ScorePool::drain(std::chrono::milliseconds timeout,
                                        std::chrono::milliseconds grace,
                                        const std::function<void()> &pump,
                                        std::chrono::milliseconds slice) {
    const auto t0 = std::chrono::steady_clock::now();
    const auto elapsed = [&t0] {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - t0)
                                              .count());
    };
    DrainResult r;
    r.pending_at_start = pending();
    bool quiesced = wait_idle(slice);
    while (!quiesced && elapsed() < static_cast<std::uint64_t>(timeout.count())) {
        if (pump)
            pump();
        quiesced = wait_idle(slice);
    }
    r.wait_ms = elapsed();
    if (!quiesced) {
        // 超时兜底：取消当前代次（在飞任务在检查点退出）后再给宽限。
        // 日志由调用方（GUI on_scheduler_done）按既有行名/字段输出（T9b 证据面保留）。
        r.forced_cancel = true;
        cancel_pending();
        const std::uint64_t grace_start = elapsed();
        quiesced = wait_idle(slice);
        while (!quiesced && elapsed() - grace_start < static_cast<std::uint64_t>(grace.count())) {
            if (pump)
                pump();
            quiesced = wait_idle(slice);
        }
        r.grace_ms = elapsed() - grace_start;
    }
    r.quiesced = quiesced;
    r.pending = pending();
    return r;
}

std::shared_ptr<const OutputResult> ScorePool::published(const std::string &out) const {
    std::lock_guard<std::mutex> lk(mu_);
    const auto it = published_.find(out);
    if (it == published_.end())
        return nullptr;
    if (!current_run_ || it->second.generation != current_run_->generation())
        return nullptr; // 旧代次的发布行对当前运行不可见
    return it->second.row;
}

void ScorePool::shutdown() {
    std::deque<ScoreTask> dropped;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopping_.exchange(true))
            return; // 幂等
        watchdog_stop_ = true;
        if (current_run_)
            current_run_->cancelled_.store(true, std::memory_order_release);
        dropped = take_queued_locked(nullptr);
    }
    cv_.notify_all();
    watchdog_cv_.notify_all();
    idle_cv_.notify_all();
    mark_discarded(dropped, kNotePoolStopped);
    for (std::thread &t : pool_) {
        if (t.joinable())
            t.join();
    }
    pool_.clear();
    if (watchdog_.joinable())
        watchdog_.join();
}

} // namespace pp
