// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T8 — scheduler contract unit tests (hand-written assertions).
//
// Contract: docs/m1-tasks.md §3.10 (PP-FROZEN). Every failure prints
// "FAIL <case>: <detail>"; main() returns the number of failures.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "core/fsops.h"
#include "core/scheduler.h"

namespace fs = std::filesystem;

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

fs::path find_corpus() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    for (int i = 0; i < 8 && !p.empty(); ++i) {
        if (fs::is_directory(p / "tests" / "golden", ec))
            return p / "tests" / "golden";
        if (!p.has_parent_path() || p.parent_path() == p)
            break;
        p = p.parent_path();
    }
    return {};
}

fs::path make_temp_dir(const std::string &name) {
    std::error_code ec;
    const fs::path d = fs::current_path(ec) / ".pp_test_tmp" / name;
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    return d;
}

std::vector<pp::FileEntry> entries_for(const std::vector<fs::path> &files, const fs::path &base) {
    std::vector<pp::FileEntry> out;
    for (const fs::path &f : files) {
        pp::FileEntry fe;
        fe.src = f;
        fe.base_dir = base;
        out.push_back(std::move(fe));
    }
    return out;
}

} // namespace

int main() {
    const fs::path corpus = find_corpus();
    if (corpus.empty()) {
        std::printf("FAIL setup: tests/golden not found from %s\n",
                    fs::current_path().string().c_str());
        return 1;
    }
    const fs::path tmp = make_temp_dir("scheduler");

    // Names used by the cases below; a batch of 8 distinct inputs (the fixtures are distinct
    // enough for the harness-level assertions we make here).
    const std::vector<fs::path> inputs = {
        corpus / "base" / "rgb8.png",  corpus / "base" / "rgb16.png",
        corpus / "base" / "gray8.png", corpus / "base" / "gray16.png",
        corpus / "base" / "rgba8.png", corpus / "base" / "rgba16.png",
        corpus / "base" / "photo.jpg", corpus / "base" / "rgb8.tif",
    };

    // ---- A. results() is index-aligned with the input and all files succeed ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "basic";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.workers = 4;

        pp::Scheduler sched(cfg, entries_for(inputs, corpus));
        std::mutex ev_mu;
        std::vector<pp::FileEvent> events;
        sched.set_event_callback([&](const pp::FileEvent &ev) {
            std::lock_guard<std::mutex> lk(ev_mu);
            events.push_back(ev);
        });
        sched.start();
        sched.wait();

        const std::vector<pp::FileResult> &results = sched.results();
        check(results.size() == inputs.size(), "basic/results-length",
              std::to_string(results.size()) + " != " + std::to_string(inputs.size()));
        bool aligned = results.size() == inputs.size();
        for (std::size_t i = 0; i < results.size() && aligned; ++i) {
            if (results[i].src != inputs[i])
                aligned = false;
        }
        check(aligned, "basic/input-order", "results[i].src does not match inputs[i]");

        const pp::RunSummary sum = sched.summary();
        check(sum.total == inputs.size(), "basic/total", std::to_string(sum.total));
        check(sum.ok == inputs.size(), "basic/all-ok",
              "ok=" + std::to_string(sum.ok) + " failed=" + std::to_string(sum.failed));
        check(sum.failed == 0 && sum.skipped == 0 && sum.cancelled == 0, "basic/no-failures",
              "failed/skipped/cancelled must be 0");
        check(sum.out_bytes > 0, "basic/out-bytes", "no output bytes accounted");
        check(sum.total_ms > 0 && sum.throughput_mb_s > 0, "basic/throughput",
              "total_ms/throughput must be positive");
        check(sum.avg_file_ms > 0, "basic/avg", "avg_file_ms must be positive");
        check(!sched.running(), "basic/not-running", "running() after wait()");

        std::size_t terminal = 0;
        std::size_t done = 0;
        for (const pp::FileEvent &ev : events) {
            if (ev.result != nullptr) {
                ++terminal;
                check(!ev.result->out.empty(), "basic/terminal-result",
                      "terminal event without an output path");
            }
            if (ev.state == pp::FileState::Done)
                ++done;
        }
        check(terminal == inputs.size(), "basic/terminal-events",
              std::to_string(terminal) + " != " + std::to_string(inputs.size()));
        check(done == inputs.size(), "basic/done-events", std::to_string(done));
    }

    // ---- B. cancel() before start(): every file is Cancelled and nothing is written ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "cancel-pre";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.workers = 3;
        pp::Scheduler sched(cfg, entries_for(inputs, corpus));
        sched.cancel();
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.total == inputs.size(), "cancel-pre/total", std::to_string(sum.total));
        check(sum.cancelled == inputs.size(), "cancel-pre/cancelled",
              std::to_string(sum.cancelled));
        check(sum.ok == 0 && sum.failed == 0, "cancel-pre/none-run",
              "ok=" + std::to_string(sum.ok) + " failed=" + std::to_string(sum.failed));
        std::error_code ec;
        check(!fs::exists(tmp / "cancel-pre" / "base" / "rgb8.jpg", ec), "cancel-pre/no-output",
              "an output file was produced");
    }

    // ---- C. cancel() mid-run: ok + cancelled == total, no failures ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "cancel-mid";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.workers = 2;
        std::vector<fs::path> many;
        for (int i = 0; i < 40; ++i)
            many.push_back(inputs[static_cast<std::size_t>(i) % inputs.size()]);
        pp::Scheduler sched(cfg, entries_for(many, corpus));
        sched.start();
        sched.cancel();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.total == many.size(), "cancel-mid/total", std::to_string(sum.total));
        check(sum.ok + sum.cancelled == many.size(), "cancel-mid/accounting",
              "ok=" + std::to_string(sum.ok) + " cancelled=" + std::to_string(sum.cancelled));
        check(sum.failed == 0, "cancel-mid/no-failures", std::to_string(sum.failed));
    }

    // ---- D. ConflictPolicy::Skip marks existing outputs as skipped ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "skip";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.conflict = pp::ConflictPolicy::Skip;
        cfg.workers = 2;
        std::error_code ec;
        fs::create_directories(cfg.out_root / "base", ec);
        {
            std::ofstream f(cfg.out_root / "base" / "rgb8.jpg", std::ios::binary);
            f << "stale";
        }
        pp::Scheduler sched(cfg, entries_for({inputs[0]}, corpus));
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.skipped == 1 && sum.ok == 0, "skip/summary",
              "skipped=" + std::to_string(sum.skipped));
        check(sched.results().size() == 1 && sched.results()[0].skipped, "skip/flag",
              "result not marked skipped");
    }

    // ---- E. missing encoder → every file fails, order preserved ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "noenc";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "nope", "", {}, 8});
        cfg.workers = 2;
        pp::Scheduler sched(cfg, entries_for(inputs, corpus));
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.failed == inputs.size(), "noenc/failed", std::to_string(sum.failed));
        check(sched.results().size() == inputs.size(), "noenc/length", "results() length");
        check(!sched.results()[0].error.empty(), "noenc/error", "error string is empty");
    }

    // ---- F. small budget: no deadlock, no over-issue (files still complete) ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "budget";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.workers = 4;
        cfg.budget_bytes = 300000; // ~3 concurrent 64x64x3 files (2x frame = 98304 each)
        pp::Scheduler sched(cfg, entries_for(inputs, corpus));
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.ok == inputs.size(), "budget/all-ok",
              "ok=" + std::to_string(sum.ok) + " failed=" + std::to_string(sum.failed));
    }

    // ---- G. metadata-only mode through the scheduler ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "metaonly";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.metadata_only = true;
        cfg.workers = 2;
        pp::TagEdit e;
        e.key = "Exif.Image.Artist";
        e.value = std::string("M1-T8");
        cfg.rules.exif_edits.push_back(e);
        pp::Scheduler sched(cfg, entries_for({corpus / "base" / "photo.jpg"}, corpus));
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.ok == 1, "metaonly/ok", "failed=" + std::to_string(sum.failed));
        std::error_code ec;
        check(fs::exists(cfg.out_root / "base" / "photo.jpg", ec), "metaonly/output",
              "metadata-only output missing");
    }

    // ---- H. empty input list ----
    {
        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file"; // 0.3.0：0.2 兼容输出结构（本测例断言既有路径）
        cfg.out_root = tmp / "empty";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        pp::Scheduler sched(cfg, {});
        sched.start();
        sched.wait();
        const pp::RunSummary sum = sched.summary();
        check(sum.total == 0 && sum.ok == 0 && sum.failed == 0, "empty/summary", "non-zero counts");
        check(sched.results().empty(), "empty/results", "results() not empty");
    }

    if (g_failed == 0) {
        std::printf("test_scheduler_contract: all checks passed\n");
    } else {
        std::printf("test_scheduler_contract: %d check(s) failed\n", g_failed);
    }
    return g_failed;
}
