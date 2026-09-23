// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T8: worker-pool scheduler (docs/m1-tasks.md §3.10).
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

// #27/#30 (docs/m2-tasks.md §3, m1b-report §7.5): a run shorter than this is not worth a budget
// line. Purely a logging threshold — no counting/summary semantics depend on it.
constexpr double kBudgetReportMinMs = 1000.0;

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
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

    Clock::time_point t_start{};
    double elapsed_ms = 0; // guarded by mu (set by wait())

    Impl(RunConfig c, std::vector<FileEntry> f) : cfg(std::move(c)), files(std::move(f)) {
        results.resize(files.size());
        last_progress.resize(files.size());
        for (std::size_t i = 0; i < files.size(); ++i)
            results[i].src = files[i].src;
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

        RunScope scope;
        {
            std::lock_guard<std::mutex> lk(mu);
            scope.reserved = reserved;
        }
        scope.budget = budget.get();
        scope.cancelled = [this] { return cancelled.load(); };

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
              {"budget_bytes", std::to_string(d.budget->capacity())},
              {"outputs", std::to_string(d.cfg.outputs.size())},
              {"formats", formats},
              {"template", d.cfg.output_template},
              {"mode", d.cfg.metadata_only ? "metadata-only" : "transcode"}});

    int n =
        d.cfg.workers > 0 ? d.cfg.workers : static_cast<int>(std::thread::hardware_concurrency());
    if (n <= 0)
        n = 1;
    if (static_cast<std::size_t>(n) > d.files.size()) {
        n = static_cast<int>(std::max<std::size_t>(1, d.files.size()));
    }
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
