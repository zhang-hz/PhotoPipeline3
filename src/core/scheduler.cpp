// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T8: worker-pool scheduler (docs/m1-tasks.md §3.10).
// M4-W1-T7: E3 adaptive thread budget + staggered start (docs/v0.3.0-design.md §8).
//
// Frozen semantics:
//   * workers = cfg.workers or the physical core count;
//   * files are taken in input order (atomic ticket = G14);
//   * per file the sequence is probe → budget.acquire(2×frame) → run → release, with the
//     cancel flag checked at the acquire point; probe failures are reported as a terminal
//     Failed event without touching the budget;
//   * after cancel() the still-unprocessed files are marked Cancelled and each one emits its
//     own terminal event;
//   * results() is as long as the input (index-aligned, all entries carry their src path);
//   * terminal events carry a pointer to the stable results() slot.
//
// Mapping note: the acquire/release half of the §3.10 sequence lives inside run_one_file /
// run_metadata_only (RAII release on every exit path, cancel checked inside acquire), because
// the pixel budget must cover the whole decode→encode window of one file; acquiring in the
// worker *and* in the pipeline would double-charge the pool. The worker performs the probe
// step itself (early failure + budget/serialisation decisions) and run_one_file re-probes for
// the OIIO spec it needs.
// NOTE(design): FileEntry has no spec field in the frozen interface, so the first_spec from the
// worker's probe cannot be handed to run_one_file → every file is probed twice (spec only, no
// pixel work). One spec for the whole batch is the established fact of the frozen interface;
// the double probe is accepted.
//
// —— M4-W1-T7（§8 调度面，逐条）——
//   §8.1 交错启动：worker **取件后**在全局限速器上预约启动时刻
//     （`next_start = max(now, last_start + stagger_ms)`，StaggerLimiter 在 scheduler.h），
//     然后 `cv.wait_until` 等到点再开跑 → 启动点两两相隔 ≥ stagger_ms。`stagger_ms<=0` 关闭
//     （恒 now）。取消路径立即唤醒所有等待者（cancel() 的 notify_all + 谓词含 cancelled）：
//     **交错等待中被取消 → 立即出 Cancelled 终态事件、不进管线**（§8.3）。
//     日志 `[sched] file start`（键 stagger_offset_ms = 实际等待 Δms，§8.1 点位）。
//   §8.2 自适应线程预算：取件 → `alloc_threads(T, remaining, inflight)`（纯函数，公式逐字照
//     §8.2）→ 并发闸（本文件 `active_files < W` 的动态信号量）→ `RunScope::encode_threads = E`
//     → 管线逐 target 交给编码器映射（§3.1 线程映射义务）。日志 `[sched] thread alloc`
//     （键 alloc_files/encode_threads/active_files/sum_encode_threads/thread_budget，§8.2 点位）。
//     不变式：active ≤ W ≤ T 且 ΣE ≤ T（E ≥ 2 ⇒ W*E ≤ T；E == 1 ⇒ ΣE = active ≤ T）。
//   §8.3 取消语义不变：阶段边界检查（管线内）、编码内不中断；新增的取消面只有上面两处等待
//     （交错等待 / 闸等待），二者都在等待中被取消 → Cancelled 终态、不进管线。
//   死锁序（不变量，勿动）：desired-key 串行化 **先于** 并发闸 —— worker 不会"持闸等键"，
//     否则 W=1 且两文件同键时会互等。放闸/放键在同一临界区内完成，其后 notify_all。
//
// E3 accounting note (§8.2 契约句 vs 逐字公式)：公式 W = min(T, remaining+inflight) 使
//   W*2 > T 分支的 E == 1（＝v0.2 行为），此时编码线程 ΣE = active ≤ T、worker 主线程数
//   ≤ T，但二者之和可达 2W（> T）。契约句的 "≤ T + ε" 与逐字公式的这一差额记入 m4-report；
//   本文件按**逐字公式**实现（任务书步骤 2「公式不许改」），读数如实入日志与单测。

#include "core/scheduler.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "codecs/encoders.h"
#include "core/fsops.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/pixelbudget.h"
#include "decode/oiio_reader.h"

namespace pp {

namespace {

constexpr std::string_view kStage = "scheduler";
constexpr std::string_view kFile = "scheduler.cpp";
// §8.1/§8.2 两个点位的 stage 取设计逐字标记 `[sched]`（日志行渲染为 `[sched] [scheduler.cpp]
// …`，grep 口径 = 设计原文；其余既有行沿用 "scheduler" 不动）。
constexpr std::string_view kStageSched = "sched";

// #27/#30 (docs/m2-tasks.md §3, m1b-report §7.5): a run shorter than this is not worth a budget
// line. Purely a logging threshold — no counting/summary semantics depend on it.
constexpr double kBudgetReportMinMs = 1000.0;

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// §8.1/§8.2 的毫秒读数：steady_clock 的单调毫秒（StaggerLimiter 的 now_ms 口径，无起点要求）。
long long steady_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
        .count();
}

// Lexical identity of the output paths a file will aim for. Files that share any of them are
// never run concurrently, which makes batch-internal conflict resolution (ConflictPolicy::Rename
// with the `reserved` snapshot) deterministic without changing the frozen pipeline signature.
// M4-T5 §4.4：键由"单 desired 路径"改为**逐 (target.out_path) 键** —— 一源多输出时不同输出可
// 并行写不同路径；同一源的多个 target 由同一 worker 串行执行，天然无内部竞态。
// 口径澄清（主对话 T5 第三次裁定，复核项 6 追认）：键 = render_output_path 产出的**冲突解析前
// desired 路径**；target.out_path 则是**冲突解析后的最终写路径**（pipeline.cpp 写入、编码器落盘
// 用）。二者角色不同，设计 §4.4「按 (target.out_path) 键」是用词混淆。用 desired 更保守：两个源
// 命中同一 desired ⇒ 必然串行 ⇒ rename 序号不随调度漂移（若按最终路径并发，序号才会漂移）。
// NOTE(limit): the guard is lexical and per-desired-path only: two *different* desired paths
// whose rename sequences overlap (a.jpg + "a (1).jpg" as separate inputs) can still race.
// Accepted: Linux file systems are case-sensitive, so lexical identity covers the realistic
// conflicts.
std::vector<std::string> desired_keys(const FileEntry &fe, const RunConfig &cfg) {
    std::vector<std::string> keys;
    keys.reserve(cfg.outputs.size());
    for (const OutputFormatSpec &spec : cfg.outputs) {
        const FormatDef *fmt = find_format(spec.format_id);
        const std::string ext =
            cfg.metadata_only ? fe.src.extension().string() : (fmt ? fmt->ext : std::string());
        PathCtx ctx;
        ctx.format_dir = fmt ? fmt->id : spec.format_id;
        ctx.rel_dir = relative_dir(fe.src, fe.base_dir);
        ctx.stem = fe.src.stem().string();
        ctx.ext = ext;
        const std::filesystem::path desired =
            render_output_path(cfg.output_template, ctx, cfg.out_root);
        if (!desired.empty())
            keys.push_back(desired.string());
    }
    // 同一文件的两个输出撞到同一 desired（例如同格式重复选择）→ 去重，避免多插一次永不释放
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return keys;
}

} // namespace

// ---------------------------------------------------------------------------
// §8.2 自适应线程分配（纯函数；公式逐字照 docs/v0.3.0-design.md §8.2，不许改）
//   W = min(T, remaining + inflight)（目标并行度）; E = 1；W*2 ≤ T 时
//   E = clamp(T / max(W,1), 2, 16)（队列浅 → 收缩并行、放大编码线程）。
//   调用方契约：T ≥ 1（线程预算，0 → 逻辑核由调用点解析）、remaining/inflight ≥ 0。
// ---------------------------------------------------------------------------
Alloc alloc_threads(int T, int remaining, int inflight) {
    int W = std::min(T, remaining + inflight); // 目标并行度
    int E = 1;
    if (W * 2 <= T)
        E = std::clamp(T / std::max(W, 1), 2, 16); // 队列浅 → 放大编码线程
    return {W, E};
}

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct Scheduler::Impl {
    RunConfig cfg;
    std::vector<FileEntry> files;
    std::vector<FileResult> results;

    mutable std::mutex mu;
    std::condition_variable cv;
    std::atomic<bool> cancelled{false};
    std::atomic<bool> running{false};
    std::atomic<std::size_t> next{0};

    std::vector<std::thread> pool;
    EventCb cb; // guarded by mu

    std::unique_ptr<PixelBudget> budget;

    std::vector<std::filesystem::path> reserved; // guarded by mu
    std::multiset<std::string> inflight;         // guarded by mu
    // W1-T6（§3.3/§7.1）：每槽位的进度高水位快照（guarded by mu）。终态事件携带它 ——
    // 消费端读终态即得"该文件最后的进度"，且进度流恒单调（progress 事件与阶段事件都更新）。
    std::vector<ProgressInfo> last_progress;

    // —— §8 调度面（W1-T7）——
    ScheduleConfig sc;         // §3.3：由 cfg 的同名字段派生（构造一次，运行期只读）
    int thread_budget = 1;     // §8.2 的 T（0 → hardware_concurrency，start() 解析后写死）
    StaggerLimiter stagger{0}; // §8.1：start() 按 sc.stagger_ms 重建
    // 并发闸/E3 读数（guarded by mu）
    std::size_t done = 0;             // 已完成（终态已出）文件数 → W 的分母口径
    int active_files = 0;             // 已入闸（活跃）文件数
    long long sum_encode_threads = 0; // 活跃编码线程合计（ΣE，E3 读数）
    int peak_active_files = 0;        // 峰值（wait() 的 e3 report 用）
    long long peak_sum_encode_threads = 0;

    Clock::time_point t_start{};
    double elapsed_ms = 0; // guarded by mu (set by wait())

    Impl(RunConfig c, std::vector<FileEntry> f)
        : cfg(std::move(c)), files(std::move(f)), stagger(0) {
        results.resize(files.size());
        last_progress.resize(files.size());
        for (std::size_t i = 0; i < files.size(); ++i)
            results[i].src = files[i].src;
        // §3.3：ScheduleConfig 由 RunConfig 的 workers/stagger_ms/thread_budget 同名派生。
        sc.workers = cfg.workers;
        sc.stagger_ms = cfg.stagger_ms;
        sc.thread_budget = cfg.thread_budget;
    }

    // §8.2：本次入闸的分配。必须在 mu 下调用（读 done/next/files）。
    //   remaining = 未取件数、inflight = 已取件未完成数（§8.2 公式的两个实参口径）。
    Alloc alloc_now_locked() const {
        const std::size_t taken = next.load(std::memory_order_relaxed);
        const std::size_t unprocessed = files.size() > taken ? files.size() - taken : 0;
        const std::size_t inflight_n = taken > done ? taken - done : 0;
        return alloc_threads(thread_budget, static_cast<int>(unprocessed),
                             static_cast<int>(inflight_n));
    }

    EventCb callback_copy() {
        std::lock_guard<std::mutex> lk(mu);
        return cb;
    }

    // 阶段/进度事件转发（W1-T6）：整事件转发（`progress` 随行），index 由调度器回填，
    // result 恒空（终态由 finish() 携带）。进度高水位在此更新（monotonic 契约，§7.3）。
    void emit_event(std::size_t i, const FileEvent &e) {
        EventCb c;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (i < last_progress.size() && e.progress.overall_frac > last_progress[i].overall_frac)
                last_progress[i] = e.progress;
            c = cb;
        }
        if (c) {
            FileEvent out{};
            out.index = i;
            out.state = e.state;
            out.result = nullptr;
            out.progress = e.progress;
            c(out);
        }
    }

    void emit_stage(std::size_t i, FileState s) { emit_event(i, FileEvent{i, s, nullptr, {}}); }

    // Stores the terminal result and emits the terminal event (pointer into results()).
    // 终态映射（§4.1 聚合 / §3.3 事件）：DoneWithErrors 落到 FileState::Done（FileState 无该值，
    // 属 §3.3 表外缺口，T7 定稿）；FileResult.ok=false 让 RunSummary 与 dev harness 的失败计数
    // 仍把"任一输出失败"的文件算作失败（0.2 语义）。
    // W1-T6：终态事件附**最后进度高水位**（消费端无需另存进度流；W2/W3 运行页读终态即得终值）。
    // W1-T7：终态同时是"完成"计数点（§8.2 的 remaining/inflight 口径）—— 每条终态路径都经此，
    // 故 done 与 results 槽位在同一临界区内推进（并发闸的宽度只会随之收缩，不会漏计）。
    void finish(std::size_t i, FileOutcome outcome) {
        const FileState state =
            outcome.verdict == FileOutcome::Verdict::Cancelled ? FileState::Cancelled
            : outcome.verdict == FileOutcome::Verdict::Skipped ? FileState::Skipped
            : outcome.verdict == FileOutcome::Verdict::Failed  ? FileState::Failed
                                                               : FileState::Done;
        FileResult r = std::move(outcome.file);
        EventCb c;
        ProgressInfo pi;
        {
            std::lock_guard<std::mutex> lk(mu);
            results[i] = std::move(r);
            ++done;
            c = cb;
            if (i < last_progress.size())
                pi = last_progress[i];
        }
        if (c)
            c(FileEvent{i, state, &results[i], pi});
    }

    void worker();
};

void Scheduler::Impl::worker() {
    for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= files.size())
            break;

        FileEntry &fe = files[i];
        if (cancelled.load()) {
            FileOutcome o;
            o.verdict = FileOutcome::Verdict::Cancelled;
            o.file.src = fe.src;
            o.file.cancelled = true;
            finish(i, std::move(o));
            continue;
        }

        // ---- §8.1 交错启动（worker 取件后等到 next_start 再开跑）----
        // 全局限速器：next_start = max(now, last_start + stagger_ms)。等待用 cv.wait_until，
        // 谓词含 cancelled → cancel() 的 notify_all
        // 即刻唤醒（§8.1「取消路径立即唤醒所有等待者」）。
        long long stagger_offset_ms = 0;
        int stagger_ms_now = 0; // 生效步距（锁内取一次；日志行不碰共享的限速器状态）
        {
            std::unique_lock<std::mutex> lk(mu);
            const long long now_ms = steady_now_ms();
            const long long start_ms = stagger.reserve_start_ms(now_ms);
            stagger_offset_ms = start_ms - now_ms;
            stagger_ms_now = stagger.stagger_ms();
            if (stagger_offset_ms > 0) {
                cv.wait_until(lk, Clock::now() + std::chrono::milliseconds(stagger_offset_ms),
                              [this] { return cancelled.load(); });
            }
            if (cancelled.load()) {
                lk.unlock();
                // §8.3：交错等待中被取消 → 立即出 Cancelled 终态事件，**不进管线**（无 probe/
                // decode/encode；也不占用预算与并发闸）。
                FileOutcome o;
                o.verdict = FileOutcome::Verdict::Cancelled;
                o.file.src = fe.src;
                o.file.cancelled = true;
                finish(i, std::move(o));
                continue;
            }
        }
        // §8.1 日志点位：`[sched] file start`（交错偏移 = 实际等待 Δms；首文件恒 0）。
        log_info(kStageSched, kFile, "file start",
                 {{"index", std::to_string(i)},
                  {"src", fe.src.string()},
                  {"stagger_offset_ms", std::to_string(stagger_offset_ms)},
                  {"stagger_ms", std::to_string(stagger_ms_now)}});

        // ---- probe (spec only) ----
        emit_stage(i, FileState::Probing);
        if (!fe.probe_done) {
            ProbeOutcome po = probe_file(fe.src);
            if (!po.error.empty()) {
                FileOutcome o;
                o.verdict = FileOutcome::Verdict::Failed;
                o.file.src = fe.src;
                o.file.error = "probe failed: " + po.error;
                finish(i, std::move(o));
                continue;
            }
            fe.info = po.info;
            fe.probe_done = true;
        }

        // ---- serialise identical output targets (§4.4: per target.out_path) ----
        // 不变量（勿动顺序）：键 **先于** 并发闸 —— worker 绝不"持闸等键"（W=1 时两文件同键
        // 会互等）；持键等闸不会成环（持键者必在闸内或即将入闸）。
        const std::vector<std::string> keys = desired_keys(fe, cfg);
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&] {
                if (cancelled.load())
                    return true;
                for (const std::string &k : keys) {
                    if (inflight.find(k) != inflight.end())
                        return false;
                }
                return true;
            });
            if (cancelled.load()) {
                lk.unlock();
                FileOutcome o;
                o.verdict = FileOutcome::Verdict::Cancelled;
                o.file.src = fe.src;
                o.file.cancelled = true;
                finish(i, std::move(o));
                continue;
            }
            for (const std::string &k : keys)
                inflight.insert(k);
        }

        // ---- §8.2 并发闸（信号量限 W = alloc_threads(T, remaining, inflight).files）----
        // 取件 → 算 Alloc → 更新闸 → EncodeRequest.encode_threads=E。闸宽度随队列深度动态收缩
        // （剩余文件少 → W 小、E 大）；不变式 active < W ⇒ 入闸后 active ≤ W ≤ T 且 ΣE ≤ T。
        Alloc alloc{1, 1};
        int active_now = 0; // 入闸后的读数（供日志行；TSan 口径：只在 mu 下读写）
        long long sum_now = 0;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk,
                    [this] { return cancelled.load() || active_files < alloc_now_locked().files; });
            if (cancelled.load()) {
                lk.unlock();
                { // 键已在上面登记 → 与主路径同样释放（否则同键文件永久互等）
                    std::lock_guard<std::mutex> lk2(mu);
                    for (const std::string &k : keys) {
                        const auto it = inflight.find(k);
                        if (it != inflight.end())
                            inflight.erase(it);
                    }
                }
                cv.notify_all();
                FileOutcome o;
                o.verdict = FileOutcome::Verdict::Cancelled;
                o.file.src = fe.src;
                o.file.cancelled = true;
                finish(i, std::move(o));
                continue;
            }
            alloc = alloc_now_locked();
            ++active_files;
            sum_encode_threads += alloc.encode_threads;
            peak_active_files = std::max(peak_active_files, active_files);
            peak_sum_encode_threads = std::max(peak_sum_encode_threads, sum_encode_threads);
            active_now = active_files;
            sum_now = sum_encode_threads;
        }
        // §8.2 日志点位：线程分配读数（E3 契约在运行日志中可查证）。
        log_info(kStageSched, kFile, "thread alloc",
                 {{"index", std::to_string(i)},
                  {"alloc_files", std::to_string(alloc.files)},
                  {"encode_threads", std::to_string(alloc.encode_threads)},
                  {"active_files", std::to_string(active_now)},
                  {"sum_encode_threads", std::to_string(sum_now)},
                  {"thread_budget", std::to_string(thread_budget)}});

        RunScope scope;
        {
            std::lock_guard<std::mutex> lk(mu);
            scope.reserved = reserved;
        }
        scope.budget = budget.get();
        scope.cancelled = [this] { return cancelled.load(); };
        scope.encode_threads = alloc.encode_threads; // §3.1 线程映射义务的输入（E ≥ 1）

        FileOutcome outcome;
        try {
            EventFn events{[this, i](const FileEvent &e) { emit_event(i, e); }, &scope};
            if (cfg.metadata_only) {
                outcome = run_metadata_only(fe, cfg, events);
            } else {
                outcome = run_one_file(fe, cfg, events);
            }
        } catch (const std::exception &e) {
            outcome = FileOutcome{};
            outcome.file.src = fe.src;
            outcome.file.error = std::string("worker: unexpected exception: ") + e.what();
        } catch (...) {
            outcome = FileOutcome{};
            outcome.file.src = fe.src;
            outcome.file.error = "worker: unexpected non-standard exception";
        }

        {
            std::lock_guard<std::mutex> lk(mu);
            // §8.2 放闸（与本文件所有退出路径一致：管线调用被 try/catch 全包，其后再无 return）
            --active_files;
            sum_encode_threads -= alloc.encode_threads;
            for (const std::string &k : keys) {
                const auto it = inflight.find(k);
                if (it != inflight.end())
                    inflight.erase(it);
            }
            // §4.4：reserved 按**每个 out_path** 登记（一源多输出 → 多条）
            for (const OutputResult &row : outcome.file.outputs) {
                if (!row.out.empty())
                    reserved.push_back(row.out);
            }
        }
        cv.notify_all();
        finish(i, std::move(outcome));
    }
}

// ---------------------------------------------------------------------------
// public interface
// ---------------------------------------------------------------------------
Scheduler::Scheduler(RunConfig cfg, std::vector<FileEntry> files)
    : impl_(std::make_unique<Impl>(std::move(cfg), std::move(files))) {}

Scheduler::~Scheduler() {
    cancel();
    wait();
}

void Scheduler::set_event_callback(EventCb cb) {
    std::lock_guard<std::mutex> lk(impl_->mu);
    impl_->cb = std::move(cb);
}

void Scheduler::start() {
    Impl &d = *impl_;
    bool expected = false;
    if (!d.running.compare_exchange_strong(expected, true))
        return; // idempotent

    d.t_start = Clock::now();
    d.budget = std::make_unique<PixelBudget>(
        d.cfg.budget_bytes != 0 ? d.cfg.budget_bytes : PixelBudget::default_capacity_bytes());

    // —— §8.1/§8.2 调度面解析（构造期只派生字段，运行期只读）——
    // T（线程预算）= thread_budget > 0 ? thread_budget : hardware_concurrency（§3.3 注释）
    const int hw = static_cast<int>(std::thread::hardware_concurrency());
    d.thread_budget = d.sc.thread_budget > 0 ? d.sc.thread_budget : (hw > 0 ? hw : 1);
    d.stagger = StaggerLimiter(d.sc.stagger_ms);
    // worker 池大小：workers > 0 → workers；否则 thread_budget 或硬件并发（§3.3 注释）
    int n = d.sc.workers > 0 ? d.sc.workers
                             : (d.sc.thread_budget > 0 ? d.sc.thread_budget : (hw > 0 ? hw : 1));
    if (n <= 0)
        n = 1;
    if (static_cast<std::size_t>(n) > d.files.size()) {
        n = static_cast<int>(std::max<std::size_t>(1, d.files.size()));
    }

    // 编码器由 run_one_file 逐 target 构造（E1：无状态、可跨 worker 共享；构造代价平凡），
    // 这里只把格式清单落进运行头日志，便于 batcli/CI 断言本次运行的目标形态。
    std::string formats;
    for (const OutputFormatSpec &spec : d.cfg.outputs) {
        if (!formats.empty())
            formats += ",";
        formats += spec.format_id;
        if (!spec.backend_id.empty())
            formats += ":" + spec.backend_id;
    }
    log_info(kStage, kFile, "start",
             {{"files", std::to_string(d.files.size())},
              {"workers", std::to_string(d.cfg.workers)},
              {"workers_resolved", std::to_string(n)},
              {"stagger_ms", std::to_string(d.stagger.stagger_ms())},
              {"thread_budget", std::to_string(d.thread_budget)},
              {"budget_bytes", std::to_string(d.budget->capacity())},
              {"outputs", std::to_string(d.cfg.outputs.size())},
              {"formats", formats},
              {"template", d.cfg.output_template},
              {"mode", d.cfg.metadata_only ? "metadata-only" : "transcode"}});

    for (int i = 0; i < n; ++i)
        d.pool.emplace_back([this] { impl_->worker(); });
}

void Scheduler::cancel() {
    impl_->cancelled.store(true);
    impl_->cv.notify_all(); // wake workers blocked on the output-target serialisation
}

void Scheduler::wait() {
    Impl &d = *impl_;
    for (std::thread &t : d.pool) {
        if (t.joinable())
            t.join();
    }
    d.pool.clear();
    bool finished_run = false;
    double elapsed_ms = 0;
    {
        std::lock_guard<std::mutex> lk(d.mu);
        finished_run = d.running.exchange(false);
        if (d.elapsed_ms == 0 && finished_run)
            d.elapsed_ms = ms_since(d.t_start);
        elapsed_ms = d.elapsed_ms;
    }
    // #27/#30: report at most once per run — the UI calls wait() explicitly and ~Scheduler calls
    // it again (cancel + wait), so the second call must not repeat the line — and only for runs
    // long enough to make the numbers meaningful (elapsed < 1 s: no report). Logging only.
    if (d.budget && finished_run && elapsed_ms >= kBudgetReportMinMs) {
        log_info(kStage, kFile, "budget report",
                 {{"capacity_bytes", std::to_string(d.budget->capacity())},
                  {"peak_bytes", std::to_string(d.budget->peak())},
                  {"used_bytes", std::to_string(d.budget->used())}});
    }
    // §8.2 E3 读数（运行级，一次）：峰值并发/编码线程上界 + T/交错步距。
    // 每次运行的**逐文件**读数在 worker 的 `[sched] file start` / `[sched] thread alloc` 行。
    // 注：peak_* 与 thread_budget 含运行期/机器相关量（线程预算默认 = 逻辑核），回归基线需
    //     按机重生成（tools/baseline/*，T17 口径）—— 故本行不参与任何 CI 断言。
    if (finished_run) {
        std::size_t done_n = 0;
        int peak_files = 0;
        long long peak_threads = 0;
        int active_now = 0;
        {
            std::lock_guard<std::mutex> lk(d.mu);
            done_n = d.done;
            peak_files = d.peak_active_files;
            peak_threads = d.peak_sum_encode_threads;
            active_now = d.active_files;
        }
        log_info(kStage, kFile, "e3 report",
                 {{"thread_budget", std::to_string(d.thread_budget)},
                  {"stagger_ms", std::to_string(d.stagger.stagger_ms())},
                  {"peak_active_files", std::to_string(peak_files)},
                  {"peak_sum_encode_threads", std::to_string(peak_threads)},
                  {"active_files_end", std::to_string(active_now)},
                  {"files_done", std::to_string(done_n)}});
    }
}

bool Scheduler::running() const { return impl_->running.load(); }

const std::vector<FileResult> &Scheduler::results() const { return impl_->results; }

RunSummary Scheduler::summary() const {
    Impl &d = *impl_;
    RunSummary s;
    // The UI polls summary() while the batch runs, so the result slots must be read under the
    // same mutex the workers store them with (results() itself is documented as wait()-only).
    std::lock_guard<std::mutex> lk(d.mu);
    s.total = d.results.size();
    for (const FileResult &r : d.results) {
        if (r.ok) {
            ++s.ok;
        } else if (r.skipped) {
            ++s.skipped;
        } else if (r.cancelled) {
            ++s.cancelled;
        } else {
            ++s.failed;
        }
        s.out_bytes += r.out_bytes;
    }
    const double ms = d.elapsed_ms > 0 ? d.elapsed_ms : ms_since(d.t_start);
    s.total_ms = ms;
    s.avg_file_ms = s.total > 0 ? ms / static_cast<double>(s.total) : 0.0;
    s.throughput_mb_s = ms > 0 ? (static_cast<double>(s.out_bytes) / 1.0e6) / (ms / 1000.0) : 0.0;
    return s;
}

} // namespace pp
