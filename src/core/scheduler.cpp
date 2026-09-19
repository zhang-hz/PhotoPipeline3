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
// TODO(M2): FileEntry has no spec field in the frozen interface, so the first_spec from the
// worker's probe cannot be handed to run_one_file → every file is probed twice (spec only, no
// pixel work). Revisit together with the UI batch (either extend FileEntry additively or give
// run_one_file a "probe already done" contract that still exposes the spec).

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

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Lexical identity of the output path a file will aim for. Files that share it are never run
// concurrently, which makes batch-internal conflict resolution (ConflictPolicy::Rename with the
// `reserved` snapshot) deterministic without changing the frozen pipeline signature.
// TODO(M2): the guard is lexical and per-desired-path only: two *different* desired paths whose
// rename sequences overlap (a.jpg + "a (1).jpg" as separate inputs) can still race. A batch-wide
// output allocator owned by the scheduler would remove the approximation.
std::string desired_key(const FileEntry& fe, const RunConfig& cfg) {
    const FormatDef* fmt = find_format(cfg.format_id);
    const std::string ext =
        cfg.metadata_only ? fe.src.extension().string() : (fmt ? fmt->ext : std::string());
    return mirror_path(fe.src, fe.base_dir, cfg.out_root, ext).string();
}

}  // namespace

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
    EventCb cb;  // guarded by mu

    std::unique_ptr<IEncoder> enc;
    std::unique_ptr<PixelBudget> budget;

    std::vector<std::filesystem::path> reserved;  // guarded by mu
    std::multiset<std::string> inflight;          // guarded by mu

    Clock::time_point t_start{};
    double elapsed_ms = 0;  // guarded by mu (set by wait())

    Impl(RunConfig c, std::vector<FileEntry> f) : cfg(std::move(c)), files(std::move(f)) {
        results.resize(files.size());
        for (std::size_t i = 0; i < files.size(); ++i) results[i].src = files[i].src;
    }

    EventCb callback_copy() {
        std::lock_guard<std::mutex> lk(mu);
        return cb;
    }

    void emit_stage(std::size_t i, FileState s) {
        const EventCb c = callback_copy();
        if (c) c(FileEvent{i, s, nullptr});
    }

    // Stores the terminal result and emits the terminal event (pointer into results()).
    void finish(std::size_t i, FileResult r) {
        const FileState state = r.cancelled ? FileState::Cancelled
                                : r.skipped ? FileState::Skipped
                                : r.ok      ? FileState::Done
                                            : FileState::Failed;
        EventCb c;
        {
            std::lock_guard<std::mutex> lk(mu);
            results[i] = std::move(r);
            c = cb;
        }
        if (c) c(FileEvent{i, state, &results[i]});
    }

    void worker();
};

void Scheduler::Impl::worker() {
    for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= files.size()) break;

        FileEntry& fe = files[i];
        if (cancelled.load()) {
            FileResult r;
            r.src = fe.src;
            r.cancelled = true;
            finish(i, std::move(r));
            continue;
        }

        // ---- probe (spec only) ----
        emit_stage(i, FileState::Probing);
        if (!fe.probe_done) {
            ProbeOutcome po = probe_file(fe.src);
            if (!po.error.empty()) {
                FileResult r;
                r.src = fe.src;
                r.error = "probe failed: " + po.error;
                finish(i, std::move(r));
                continue;
            }
            fe.info = po.info;
            fe.probe_done = true;
        }

        // ---- serialise identical output targets ----
        const std::string key = desired_key(fe, cfg);
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk, [&] {
                return cancelled.load() || inflight.find(key) == inflight.end();
            });
            if (cancelled.load()) {
                lk.unlock();
                FileResult r;
                r.src = fe.src;
                r.cancelled = true;
                finish(i, std::move(r));
                continue;
            }
            inflight.insert(key);
        }

        std::vector<std::filesystem::path> reserved_snapshot;
        {
            std::lock_guard<std::mutex> lk(mu);
            reserved_snapshot = reserved;
        }

        FileResult r;
        try {
            if (cfg.metadata_only) {
                r = run_metadata_only(fe, cfg, reserved_snapshot,
                                      [this] { return cancelled.load(); },
                                      [this, i](FileState s) { emit_stage(i, s); });
            } else {
                r = run_one_file(fe, cfg, enc.get(), budget.get(), reserved_snapshot,
                                 [this] { return cancelled.load(); },
                                 [this, i](FileState s) { emit_stage(i, s); });
            }
        } catch (const std::exception& e) {
            r = FileResult{};
            r.src = fe.src;
            r.error = std::string("worker: unexpected exception: ") + e.what();
        } catch (...) {
            r = FileResult{};
            r.src = fe.src;
            r.error = "worker: unexpected non-standard exception";
        }

        {
            std::lock_guard<std::mutex> lk(mu);
            const auto it = inflight.find(key);
            if (it != inflight.end()) inflight.erase(it);
            if (!r.out.empty()) reserved.push_back(r.out);
        }
        cv.notify_all();
        finish(i, std::move(r));
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
    Impl& d = *impl_;
    bool expected = false;
    if (!d.running.compare_exchange_strong(expected, true)) return;  // idempotent

    d.t_start = Clock::now();
    d.enc = make_encoder(d.cfg.format_id, d.cfg.backend_id);
    if (!d.cfg.metadata_only && !d.enc) {
        log_error(kStage, kFile, "no encoder registered for format; every file will fail",
                  {{"format", d.cfg.format_id}, {"backend", d.cfg.backend_id}});
    }
    d.budget = std::make_unique<PixelBudget>(
        d.cfg.budget_bytes != 0 ? d.cfg.budget_bytes : PixelBudget::default_capacity_bytes());
    log_info(kStage, kFile, "start",
             {{"files", std::to_string(d.files.size())},
              {"workers", std::to_string(d.cfg.workers)},
              {"budget_bytes", std::to_string(d.budget->capacity())},
              {"format", d.cfg.format_id},
              {"mode", d.cfg.metadata_only ? "metadata-only" : "transcode"}});

    int n = d.cfg.workers > 0 ? d.cfg.workers
                              : static_cast<int>(std::thread::hardware_concurrency());
    if (n <= 0) n = 1;
    if (static_cast<std::size_t>(n) > d.files.size()) {
        n = static_cast<int>(std::max<std::size_t>(1, d.files.size()));
    }
    for (int i = 0; i < n; ++i) d.pool.emplace_back([this] { impl_->worker(); });
}

void Scheduler::cancel() {
    impl_->cancelled.store(true);
    impl_->cv.notify_all();  // wake workers blocked on the output-target serialisation
}

void Scheduler::wait() {
    Impl& d = *impl_;
    for (std::thread& t : d.pool) {
        if (t.joinable()) t.join();
    }
    d.pool.clear();
    {
        std::lock_guard<std::mutex> lk(d.mu);
        if (d.elapsed_ms == 0 && d.running.load()) d.elapsed_ms = ms_since(d.t_start);
    }
    d.running.store(false);
    if (d.budget) {
        log_info(kStage, kFile, "budget report",
                 {{"capacity_bytes", std::to_string(d.budget->capacity())},
                  {"peak_bytes", std::to_string(d.budget->peak())},
                  {"used_bytes", std::to_string(d.budget->used())}});
    }
}

bool Scheduler::running() const { return impl_->running.load(); }

const std::vector<FileResult>& Scheduler::results() const { return impl_->results; }

RunSummary Scheduler::summary() const {
    const Impl& d = *impl_;
    RunSummary s;
    s.total = d.results.size();
    for (const FileResult& r : d.results) {
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
    double ms = 0;
    {
        std::lock_guard<std::mutex> lk(d.mu);
        ms = d.elapsed_ms;
    }
    if (ms <= 0) ms = ms_since(d.t_start);
    s.total_ms = ms;
    s.avg_file_ms = s.total > 0 ? ms / static_cast<double>(s.total) : 0.0;
    s.throughput_mb_s =
        ms > 0 ? (static_cast<double>(s.out_bytes) / 1.0e6) / (ms / 1000.0) : 0.0;
    return s;
}

}  // namespace pp
