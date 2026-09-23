// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M1-T8 — scheduler contract unit tests (hand-written assertions).
// M4-W1-T7 — E3 调度面扩展（docs/v0.3.0-design.md §8.1/§8.2/§8.3 + §13 测试表）：
//   * I   `alloc_threads` 穷举（T/remaining/inflight 全组合 + 边界 + 设计表值）；
//   * J   `StaggerLimiter` 交错时序（fake clock：整数毫秒假时钟穷举）；
//   * K   交错启动的真钟下界（N 文件 × stagger 下界）+ 日志首文件偏移恒 0；
//   * L   取消语义：交错等待中被取消 → 立即唤醒（远早于 stagger 到点）、不进管线（无输出、
//        无 `file start` 行）；
//   * M   E3 读数不变式（日志面）：active ≤ W ≤ T、ΣE ≤ T、W*2 > T ⇒ E == 1（含并发闸上限）；
//   * N   §3.6 settings 持久化往返（stagger_ms/thread_budget；T7 文件面内唯一的测试文件）。
//
// Contract: docs/m1-tasks.md §3.10 (PP-FROZEN) + docs/v0.3.0-design.md §8（M4-W1-T7）.
// Every failure prints "FAIL <case>: <detail>"; main() returns the number of failures.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "core/fsops.h"
#include "core/logger.h"
#include "core/scheduler.h"
#include "core/settings.h"

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

// ---------------------------------------------------------------------------
// W1-T7 辅助：日志面读数（§8.1/§8.2 点位）
// ---------------------------------------------------------------------------
// 读一个日志目录里的全部 run-*.log（按文件名排序拼接；同一目录内只有本次用例的行）。
std::vector<std::string> read_run_logs(const fs::path &dir) {
    std::vector<std::string> lines;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const fs::directory_entry &e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".log")
            files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const fs::path &f : files) {
        std::ifstream in(f, std::ios::binary);
        if (!in)
            continue;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            lines.push_back(line);
        }
    }
    return lines;
}

bool has_token(const std::string &line, const std::string &tok) {
    return line.find(tok) != std::string::npos;
}

// 取行内 ` key=<整数>`（日志行尾的 k=v 字段；本用例的数值字段恒无引号）。
bool field_int(const std::string &line, const std::string &key, long long &out) {
    const std::string needle = " " + key + "=";
    const std::size_t at = line.find(needle);
    if (at == std::string::npos)
        return false;
    std::size_t p = at + needle.size();
    bool neg = false;
    if (p < line.size() && (line[p] == '-' || line[p] == '+')) {
        neg = line[p] == '-';
        ++p;
    }
    std::size_t q = p;
    while (q < line.size() && line[q] >= '0' && line[q] <= '9')
        ++q;
    if (q == p)
        return false;
    out = std::stoll(line.substr(p, q - p));
    if (neg)
        out = -out;
    return true;
}

struct AllocLine {
    long long alloc_files = 0, encode_threads = 0, active_files = 0, sum_encode_threads = 0,
              thread_budget = 0;
};

// 输出 `<sched> thread alloc` 行（§8.2 点位）解析成结构；非该行返回 false。
bool parse_alloc_line(const std::string &line, AllocLine &out) {
    if (!has_token(line, "[sched]") || !has_token(line, "thread alloc {"))
        return false;
    return field_int(line, "alloc_files", out.alloc_files) &&
           field_int(line, "encode_threads", out.encode_threads) &&
           field_int(line, "active_files", out.active_files) &&
           field_int(line, "sum_encode_threads", out.sum_encode_threads) &&
           field_int(line, "thread_budget", out.thread_budget);
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

    // ---- I. §8.2 alloc_threads：穷举（T/remaining/inflight 全组合）+ 边界 + 设计表值 ----
    {
        long long viol_w = 0, viol_unproc = 0, viol_e = 0, viol_guard = 0, viol_budget = 0;
        std::string ex_w, ex_e, ex_guard, ex_budget, ex_unproc;
        for (int T = 1; T <= 32; ++T) {
            for (int rem = 0; rem <= 32; ++rem) {
                for (int inf = 0; inf <= 32; ++inf) {
                    const pp::Alloc a = pp::alloc_threads(T, rem, inf);
                    const std::string tag = "T=" + std::to_string(T) +
                                            " rem=" + std::to_string(rem) +
                                            " inf=" + std::to_string(inf);
                    if (!(a.files <= T)) {
                        ++viol_w;
                        if (ex_w.empty())
                            ex_w = tag + " W=" + std::to_string(a.files);
                    }
                    if (!(a.files <= rem + inf)) {
                        ++viol_unproc;
                        if (ex_unproc.empty())
                            ex_unproc = tag + " W=" + std::to_string(a.files);
                    }
                    if (!(a.encode_threads == 1 ||
                          (a.encode_threads >= 2 && a.encode_threads <= 16))) {
                        ++viol_e;
                        if (ex_e.empty())
                            ex_e = tag + " E=" + std::to_string(a.encode_threads);
                    }
                    if (a.files * 2 > T && a.encode_threads != 1) {
                        ++viol_guard;
                        if (ex_guard.empty())
                            ex_guard = tag + " E=" + std::to_string(a.encode_threads);
                    }
                    if (a.encode_threads >= 2 && a.files >= 1 && a.files * a.encode_threads > T) {
                        ++viol_budget;
                        if (ex_budget.empty())
                            ex_budget = tag + " W=" + std::to_string(a.files) +
                                        " E=" + std::to_string(a.encode_threads);
                    }
                }
            }
        }
        check(viol_w == 0, "alloc/w-le-t", "W > T 出现 " + std::to_string(viol_w) + " 次: " + ex_w);
        check(viol_unproc == 0, "alloc/w-le-unprocessed",
              "W > remaining+inflight 出现 " + std::to_string(viol_unproc) + " 次: " + ex_unproc);
        check(viol_e == 0, "alloc/e-domain",
              "E 不在 {1} ∪ [2,16] 出现 " + std::to_string(viol_e) + " 次: " + ex_e);
        check(viol_guard == 0, "alloc/deep-queue-e1",
              "W*2 > T 时 E != 1 出现 " + std::to_string(viol_guard) + " 次: " + ex_guard);
        check(viol_budget == 0, "alloc/sum-le-t",
              "E >= 2 且 W*E > T 出现 " + std::to_string(viol_budget) + " 次: " + ex_budget);

        // 边界/退化点（0 与极大值；负值不进本例：T<1 不是合法预算，调用方解析后 T ≥ 1）
        const int t_vals[] = {0, 1, 2, 3, 4, 8, 16, 64, 1024};
        const int n_vals[] = {0, 1, 2, 3, 10000};
        long long viol_extreme = 0;
        std::string ex_extreme;
        for (int T : t_vals) {
            for (int rem : n_vals) {
                for (int inf : n_vals) {
                    const pp::Alloc a = pp::alloc_threads(T, rem, inf);
                    const std::string tag = "T=" + std::to_string(T) +
                                            " rem=" + std::to_string(rem) +
                                            " inf=" + std::to_string(inf);
                    const bool ok =
                        a.files <= T && a.files <= rem + inf &&
                        (a.encode_threads == 1 ||
                         (a.encode_threads >= 2 && a.encode_threads <= 16)) &&
                        !(a.files * 2 > T && a.encode_threads != 1) &&
                        !(a.encode_threads >= 2 && a.files >= 1 && a.files * a.encode_threads > T);
                    if (!ok) {
                        ++viol_extreme;
                        if (ex_extreme.empty())
                            ex_extreme = tag + " {W=" + std::to_string(a.files) +
                                         ",E=" + std::to_string(a.encode_threads) + "}";
                    }
                }
            }
        }
        check(viol_extreme == 0, "alloc/extremes",
              std::to_string(viol_extreme) + " 个退化点违例: " + ex_extreme);

        // 设计表值（§8.2 语义：深队列 → E=1；浅队列 → 放大；上限 16）
        struct Row {
            int T, rem, inf, W, E;
        };
        const Row rows[] = {
            {16, 32, 1, 16, 1},  // 深队列：全并行、编码器单线程（= v0.2 行为）
            {16, 2, 1, 3, 5},    // W*2=6 <= 16 → E = floor(16/3) = 5
            {16, 1, 1, 2, 8},    // E = 16/2 = 8
            {16, 0, 1, 1, 16},   // 单文件：E = 16（上限内）
            {32, 1, 1, 2, 16},   // E = 32/2 = 16 → 撞上限 16（x265/SVT/JXL 口径）
            {8, 1, 1, 2, 4},     // E = 8/2 = 4
            {4, 1, 1, 2, 2},     // E = 4/2 = 2（下限）
            {3, 1, 1, 2, 1},     // W*2=4 > 3 → 守卫分支 E = 1
            {2, 5, 0, 2, 1},     // 同上
            {1, 5, 0, 1, 1},     // 单线程预算：E 恒 1
            {2, 0, 1, 1, 2},     // W*2 = 2 <= 2 → E = 2
            {1024, 0, 1, 1, 16}, // 极大 T：E 仍 ≤ 16
        };
        long long viol_row = 0;
        std::string ex_row;
        for (const Row &r : rows) {
            const pp::Alloc a = pp::alloc_threads(r.T, r.rem, r.inf);
            if (a.files != r.W || a.encode_threads != r.E) {
                ++viol_row;
                if (ex_row.empty())
                    ex_row = "T=" + std::to_string(r.T) + " rem=" + std::to_string(r.rem) +
                             " inf=" + std::to_string(r.inf) + " 得 {W=" + std::to_string(a.files) +
                             ",E=" + std::to_string(a.encode_threads) +
                             "} 期望 {W=" + std::to_string(r.W) + ",E=" + std::to_string(r.E) + "}";
            }
        }
        check(viol_row == 0, "alloc/table", std::to_string(viol_row) + " 个表值不符: " + ex_row);
    }

    // ---- J. §8.1 交错限速器：fake clock 时序 ----
    {
        pp::StaggerLimiter lim(150);
        check(lim.stagger_ms() == 150 && !lim.started(), "stagger/init",
              "初始状态不符：stagger_ms=" + std::to_string(lim.stagger_ms()));
        // 首文件无前驱 → 恒 now；之后 next = max(now, last + 150)
        const long long s0 = lim.reserve_start_ms(1000);
        const long long s1 = lim.reserve_start_ms(1000);
        const long long s2 = lim.reserve_start_ms(1100);
        const long long s3 = lim.reserve_start_ms(2000);
        const long long s4 = lim.reserve_start_ms(2001);
        check(s0 == 1000, "stagger/first-is-now", "s0=" + std::to_string(s0));
        check(s1 == 1150, "stagger/second-staggered", "s1=" + std::to_string(s1));
        check(s2 == 1300, "stagger/behind-schedule",
              "s2=" + std::to_string(s2) + " 期望 1300（max(1100, 1150+150)）");
        check(s3 == 2000, "stagger/late-arrival-wins",
              "s3=" + std::to_string(s3) + " 期望 2000（max(2000, 1300+150)）");
        check(s4 == 2150, "stagger/late-then-staggered", "s4=" + std::to_string(s4));

        // 密集预约（同一假时刻 100 次）：严格 150ms 等差数列
        pp::StaggerLimiter dense(150);
        bool arithmetic = true;
        long long prev = 0;
        for (int i = 0; i < 100; ++i) {
            const long long s = dense.reserve_start_ms(0);
            if (i > 0 && s != prev + 150)
                arithmetic = false;
            prev = s;
        }
        check(arithmetic && prev == 99 * 150, "stagger/dense-arithmetic",
              "100 次同刻预约未形成 150ms 等差：末值 " + std::to_string(prev));

        // Pseudo-random 时序（确定性 LCG）：不变量 = s_i ≥ now_i 且 s_i ≥ s_{i-1} + stagger
        pp::StaggerLimiter rnd(37);
        unsigned long long seed = 12345;
        long long last = 0;
        bool invariants = true;
        for (int i = 0; i < 200; ++i) {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            const long long now = static_cast<long long>((seed >> 33) % 5000);
            const long long s = rnd.reserve_start_ms(now);
            if (i > 0 && !(s >= now && s >= last + 37))
                invariants = false;
            last = s;
        }
        check(invariants, "stagger/invariants",
              "存在 s < now 或 s < last + stagger 的预约（假时钟序列）");

        // stagger = 0（关闭）/ 负值（按关闭处理）：恒立即启动
        pp::StaggerLimiter off(0);
        check(off.stagger_ms() == 0 && off.reserve_start_ms(7) == 7 && off.reserve_start_ms(7) == 7,
              "stagger/disabled", "stagger_ms=0 时未立即启动");
        pp::StaggerLimiter neg(-5);
        check(neg.stagger_ms() == 0 && neg.reserve_start_ms(42) == 42, "stagger/negative-off",
              "负 stagger_ms 未按关闭处理");
    }

    // ---- K. §8.1 交错启动：真钟下界 + 首文件偏移 0（日志点位）----
    {
        const fs::path logdir = make_temp_dir("sched_stagger_log");
        pp::log_init(logdir, pp::LogLevel::Info);

        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file";
        cfg.out_root = tmp / "stagger";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.workers = 1; // 单 worker → 启动点完全由限速器决定
        cfg.stagger_ms = 150;
        const std::vector<fs::path> four = {inputs[0], inputs[1], inputs[2], inputs[3]};

        pp::Scheduler sched(cfg, entries_for(four, corpus));
        const auto t0 = std::chrono::steady_clock::now();
        sched.start();
        sched.wait();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        pp::log_shutdown();

        check(sched.summary().ok == four.size(), "stagger/all-ok",
              "ok=" + std::to_string(sched.summary().ok));
        // 启动点两两 ≥ stagger → 第 4 个文件不可能早于 3×150 = 450ms 起跑
        check(elapsed_ms >= 440.0, "stagger/lower-bound",
              "4 文件 × stagger 150ms 的整批耗时 " + std::to_string(elapsed_ms) +
                  "ms，期望 ≥ 450ms");

        std::vector<long long> offsets;
        for (const std::string &l : read_run_logs(logdir)) {
            if (!has_token(l, "[sched]") || !has_token(l, "file start {"))
                continue;
            long long v = 0;
            if (field_int(l, "stagger_offset_ms", v))
                offsets.push_back(v);
        }
        check(offsets.size() == four.size(), "stagger/start-lines",
              "`[sched] file start` 行数 " + std::to_string(offsets.size()) +
                  " != " + std::to_string(four.size()));
        check(!offsets.empty() && offsets[0] == 0, "stagger/first-offset-zero",
              "首文件交错偏移不为 0");
        bool nonneg = true;
        for (long long v : offsets)
            nonneg = nonneg && v >= 0;
        check(nonneg, "stagger/offsets-nonneg", "存在负的交错偏移");
    }

    // ---- L. §8.3 取消：交错等待中被取消 → 立即唤醒（远早于 stagger 到点）、不进管线 ----
    {
        const fs::path logdir = make_temp_dir("sched_cancel_stagger_log");
        pp::log_init(logdir, pp::LogLevel::Info);

        pp::RunConfig cfg;
        cfg.output_template = "$dir/$file";
        cfg.out_root = tmp / "cancel-stagger";
        cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.workers = 1;
        cfg.stagger_ms = 3000; // 第 2 个文件的启动点在 3s 之后
        const std::vector<fs::path> four = {inputs[0], inputs[1], inputs[2], inputs[3]};

        pp::Scheduler sched(cfg, entries_for(four, corpus));
        sched.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(400)); // 第 1 个文件已跑完
        const auto tc = std::chrono::steady_clock::now();
        sched.cancel();
        sched.wait();
        const double wake_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tc)
                .count();
        pp::log_shutdown();

        const pp::RunSummary sum = sched.summary();
        check(sum.total == four.size(), "cancel-stagger/total", std::to_string(sum.total));
        check(sum.failed == 0, "cancel-stagger/no-failures",
              "failed=" + std::to_string(sum.failed));
        check(sum.cancelled >= 1, "cancel-stagger/cancelled",
              "cancelled=" + std::to_string(sum.cancelled));
        // 未被唤醒则第 2 个文件要等到 3s 才出终态
        check(wake_ms < 1000.0, "cancel-stagger/wake-fast",
              "cancel() → wait() 用了 " + std::to_string(wake_ms) +
                  "ms（交错等待未被立即唤醒，期望 < 1000ms）");

        // 不进管线：只有第 1 个文件起跑、只有它的输出存在
        std::error_code ec;
        check(fs::exists(cfg.out_root / "base" / "rgb8.jpg", ec), "cancel-stagger/first-output",
              "第 1 个文件的输出缺失");
        bool extra_out = false;
        for (std::size_t i = 1; i < four.size(); ++i) {
            const fs::path out = cfg.out_root / "base" / (four[i].stem().string() + ".jpg");
            if (fs::exists(out, ec))
                extra_out = true;
        }
        check(!extra_out, "cancel-stagger/no-later-output", "被取消的文件产出了输出文件");
        int starts = 0;
        for (const std::string &l : read_run_logs(logdir)) {
            if (has_token(l, "[sched]") && has_token(l, "file start {"))
                ++starts;
        }
        check(starts == 1, "cancel-stagger/one-start",
              "`[sched] file start` 行数 " + std::to_string(starts) +
                  " != 1（被取消的文件进了管线）");
    }

    // ---- M. §8.2 E3 契约读数（日志面）：active ≤ W ≤ T、ΣE ≤ T、W*2 > T ⇒ E == 1 ----
    {
        // M1：workers(8) > thread_budget(2) → 闸必须压过池大小（并发 ≤ 2，E 恒 1）
        {
            const fs::path logdir = make_temp_dir("sched_e3_log");
            pp::log_init(logdir, pp::LogLevel::Info);
            pp::RunConfig cfg;
            cfg.output_template = "$dir/$file";
            cfg.out_root = tmp / "e3-cap";
            cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
            cfg.conflict = pp::ConflictPolicy::Overwrite;
            cfg.workers = 8;
            cfg.thread_budget = 2;
            cfg.stagger_ms = 0; // 本用例只测并发闸
            const std::vector<fs::path> six(inputs.begin(), inputs.begin() + 6);
            pp::Scheduler sched(cfg, entries_for(six, corpus));
            sched.start();
            sched.wait();
            pp::log_shutdown();

            const pp::RunSummary sum = sched.summary();
            check(sum.ok == six.size(), "e3-cap/all-ok",
                  "ok=" + std::to_string(sum.ok) + " failed=" + std::to_string(sum.failed));

            std::size_t n_alloc = 0;
            std::size_t n_bad = 0;
            long long max_active = 0, max_sum = 0, max_w = 0;
            std::string ex;
            for (const std::string &l : read_run_logs(logdir)) {
                AllocLine a;
                if (!parse_alloc_line(l, a))
                    continue;
                ++n_alloc;
                bool ok =
                    a.thread_budget == 2 && a.active_files <= a.alloc_files &&
                    a.alloc_files <= a.thread_budget && a.alloc_files >= 1 &&
                    (a.encode_threads == 1 || (a.encode_threads >= 2 && a.encode_threads <= 16)) &&
                    !(a.alloc_files * 2 > a.thread_budget && a.encode_threads != 1) &&
                    a.sum_encode_threads <= a.thread_budget &&
                    !(a.encode_threads >= 2 && a.active_files * a.encode_threads > a.thread_budget);
                if (!ok) {
                    ++n_bad;
                    if (ex.empty())
                        ex = l;
                }
                max_active = std::max(max_active, a.active_files);
                max_sum = std::max(max_sum, a.sum_encode_threads);
                max_w = std::max(max_w, a.alloc_files);
            }
            check(n_alloc == six.size(), "e3-cap/alloc-lines",
                  "`thread alloc` 行数 " + std::to_string(n_alloc) +
                      " != " + std::to_string(six.size()));
            check(n_bad == 0, "e3-cap/invariants",
                  std::to_string(n_bad) +
                      " 行违例（active ≤ W ≤ T / ΣE ≤ T / W*2>T ⇒ E=1）: " + ex);
            check(max_active <= 2 && max_w <= 2, "e3-cap/gate-cap",
                  "峰值并发 active=" + std::to_string(max_active) + " W=" + std::to_string(max_w) +
                      "，闸上限应为 2");
            check(max_sum <= 2, "e3-cap/sum-cap", "峰值 ΣE=" + std::to_string(max_sum) + " > T=2");
        }

        // M2：浅队列（2 文件 × T=16）→ E > 1 分支必须真的触发（放大编码线程）
        {
            const fs::path logdir = make_temp_dir("sched_e3_shallow_log");
            pp::log_init(logdir, pp::LogLevel::Info);
            pp::RunConfig cfg;
            cfg.output_template = "$dir/$file";
            cfg.out_root = tmp / "e3-shallow";
            cfg.outputs.push_back(pp::OutputFormatSpec{"jpeg", "", "", {}, 8});
            cfg.conflict = pp::ConflictPolicy::Overwrite;
            cfg.workers = 4;
            cfg.thread_budget = 16;
            cfg.stagger_ms = 0;
            const std::vector<fs::path> two = {inputs[0], inputs[1]};
            pp::Scheduler sched(cfg, entries_for(two, corpus));
            sched.start();
            sched.wait();
            pp::log_shutdown();

            check(sched.summary().ok == two.size(), "e3-shallow/all-ok",
                  "ok=" + std::to_string(sched.summary().ok));
            long long best_e = 0, max_sum = 0, max_active = 0;
            std::size_t n_alloc = 0;
            bool invariants = true;
            for (const std::string &l : read_run_logs(logdir)) {
                AllocLine a;
                if (!parse_alloc_line(l, a))
                    continue;
                ++n_alloc;
                best_e = std::max(best_e, a.encode_threads);
                max_sum = std::max(max_sum, a.sum_encode_threads);
                max_active = std::max(max_active, a.active_files);
                if (!(a.thread_budget == 16 && a.active_files <= a.alloc_files &&
                      a.alloc_files <= a.thread_budget && a.sum_encode_threads <= a.thread_budget))
                    invariants = false;
            }
            check(n_alloc == two.size(), "e3-shallow/alloc-lines", std::to_string(n_alloc));
            check(invariants, "e3-shallow/invariants", "active ≤ W ≤ T 或 ΣE ≤ T 违例");
            check(best_e >= 2 && best_e <= 16, "e3-shallow/e-amplified",
                  "浅队列未放大编码线程：E=" + std::to_string(best_e));
            check(max_sum <= 16, "e3-shallow/sum-cap", "ΣE=" + std::to_string(max_sum) + " > T=16");
            check(max_active <= 2, "e3-shallow/w",
                  "active=" + std::to_string(max_active) + "，W 应为 min(16, 未完成数)=2");
        }
        // M3：§3.1 jxl 线程映射的两条路径实跑留证（E=1 → nullptr；E>1 → runner 建成）
        {
            const std::vector<fs::path> two = {inputs[0], inputs[1]};
            // E=1：T=1 → W=1 → W*2 > T → E 恒 1 → 走 nullptr（0.2 行为）
            {
                const fs::path logdir = make_temp_dir("sched_jxl_e1_log");
                pp::log_init(logdir, pp::LogLevel::Info);
                pp::RunConfig cfg;
                cfg.output_template = "$dir/$file";
                cfg.out_root = tmp / "jxl-e1";
                cfg.outputs.push_back(pp::OutputFormatSpec{"jxl", "", "", {}, 8});
                cfg.conflict = pp::ConflictPolicy::Overwrite;
                cfg.workers = 1;
                cfg.thread_budget = 1;
                cfg.stagger_ms = 0;
                pp::Scheduler sched(cfg, entries_for(two, corpus));
                sched.start();
                sched.wait();
                pp::log_shutdown();

                const std::vector<std::string> lines = read_run_logs(logdir);
                std::size_t n_runner = 0, n_alloc = 0;
                bool all_e1 = true;
                for (const std::string &l : lines) {
                    if (has_token(l, "parallel runner created"))
                        ++n_runner;
                    AllocLine a;
                    if (parse_alloc_line(l, a)) {
                        ++n_alloc;
                        all_e1 = all_e1 && a.encode_threads == 1;
                    }
                }
                check(sched.summary().ok == two.size(), "jxl-e1/all-ok",
                      "ok=" + std::to_string(sched.summary().ok) +
                          " failed=" + std::to_string(sched.summary().failed));
                check(n_alloc == two.size() && all_e1, "jxl-e1/e-is-1",
                      "T=1 时 E 应恒 1（alloc 行数 " + std::to_string(n_alloc) + "）");
                check(n_runner == 0, "jxl-e1/nullptr-path",
                      "T=1（E=1）却出现了 parallel runner 行 " + std::to_string(n_runner) + " 条");
                std::error_code ec;
                check(fs::exists(cfg.out_root / "base" / "rgb8.jxl", ec) &&
                          fs::exists(cfg.out_root / "base" / "rgb16.jxl", ec),
                      "jxl-e1/outputs", "E=1（nullptr runner）路径的 jxl 产物缺失");
            }
            // E>1：T=16 → 浅队列（W ≤ 2）→ E ≥ 2 → runner 建成（多线程编码）
            {
                const fs::path logdir = make_temp_dir("sched_jxl_en_log");
                pp::log_init(logdir, pp::LogLevel::Info);
                pp::RunConfig cfg;
                cfg.output_template = "$dir/$file";
                cfg.out_root = tmp / "jxl-en";
                cfg.outputs.push_back(pp::OutputFormatSpec{"jxl", "", "", {}, 8});
                cfg.conflict = pp::ConflictPolicy::Overwrite;
                cfg.workers = 1;
                cfg.thread_budget = 16;
                cfg.stagger_ms = 0;
                pp::Scheduler sched(cfg, entries_for(two, corpus));
                sched.start();
                sched.wait();
                pp::log_shutdown();

                const std::vector<std::string> lines = read_run_logs(logdir);
                long long best_threads = 0;
                std::size_t n_runner = 0, n_alloc = 0;
                bool all_e_gt1 = true;
                for (const std::string &l : lines) {
                    AllocLine a;
                    if (parse_alloc_line(l, a)) {
                        ++n_alloc;
                        all_e_gt1 = all_e_gt1 && a.encode_threads >= 2;
                    }
                    if (!has_token(l, "parallel runner created"))
                        continue;
                    ++n_runner;
                    long long v = 0;
                    if (field_int(l, "threads", v))
                        best_threads = std::max(best_threads, v);
                }
                check(sched.summary().ok == two.size(), "jxl-en/all-ok",
                      "ok=" + std::to_string(sched.summary().ok) +
                          " failed=" + std::to_string(sched.summary().failed));
                check(n_alloc == two.size() && all_e_gt1, "jxl-en/e-amplified",
                      "T=16 浅队列时 E 应 ≥ 2（alloc 行数 " + std::to_string(n_alloc) + "）");
                check(n_runner >= 1 && best_threads >= 2 && best_threads <= 16, "jxl-en/runner",
                      "JxlThreadParallelRunner 未建成：runner 行 " + std::to_string(n_runner) +
                          " threads=" + std::to_string(best_threads));
                std::error_code ec;
                check(fs::exists(cfg.out_root / "base" / "rgb8.jxl", ec) &&
                          fs::exists(cfg.out_root / "base" / "rgb16.jxl", ec),
                      "jxl-en/outputs", "E>1（多线程 runner）路径的 jxl 产物缺失");
            }
        }
    }

    // ---- N. §3.6 settings 持久化：stagger_ms / thread_budget（T7 落地面）----
    // 说明：settings 往返的正式归属是 W3-T15（设置对话框）与 tests/unit/test_settings.cpp；
    // 本用例只覆盖 T7 新增的两个键（T7 文件面内唯一的测试文件），不重复 T15 的覆盖面。
    {
        const fs::path dir = make_temp_dir("sched_settings");
        const fs::path file = dir / "settings.ini";

        const pp::AppSettings def;
        check(def.stagger_ms == 150 && def.thread_budget == 0, "settings/defaults",
              "默认值不符：stagger_ms=" + std::to_string(def.stagger_ms) +
                  " thread_budget=" + std::to_string(def.thread_budget));

        pp::AppSettings s;
        s.stagger_ms = 2000; // §9.3 设置域上界
        s.thread_budget = 12;
        const std::string err = pp::save_settings(file, s);
        check(err.empty(), "settings/save", "err=" + err);
        const pp::AppSettings r = pp::load_settings(file);
        check(r.stagger_ms == 2000 && r.thread_budget == 12, "settings/roundtrip",
              "stagger_ms=" + std::to_string(r.stagger_ms) +
                  " thread_budget=" + std::to_string(r.thread_budget));

        // 二次保存幂等（既有口径）
        const std::string err2 = pp::save_settings(file, s);
        const pp::AppSettings r2 = pp::load_settings(file);
        check(err2.empty() && r2.stagger_ms == 2000 && r2.thread_budget == 12,
              "settings/idempotent", "err=" + err2);

        // 负值 → 0（引擎口径：stagger <= 0 = 关闭；thread_budget 0 = 自动）
        {
            std::ofstream f(file, std::ios::binary | std::ios::trunc);
            f << "stagger_ms=-4\nthread_budget=-9\n";
        }
        const pp::AppSettings c = pp::load_settings(file);
        check(c.stagger_ms == 0 && c.thread_budget == 0, "settings/negative-zeroed",
              "stagger_ms=" + std::to_string(c.stagger_ms) +
                  " thread_budget=" + std::to_string(c.thread_budget));

        // 缺失文件 → 默认值（既有契约，顺带覆盖新键）
        const pp::AppSettings missing = pp::load_settings(dir / "nope.ini");
        check(missing.stagger_ms == 150 && missing.thread_budget == 0, "settings/missing-defaults",
              "缺失文件未回落默认值");
    }

    if (g_failed == 0) {
        std::printf("test_scheduler_contract: all checks passed\n");
    } else {
        std::printf("test_scheduler_contract: %d check(s) failed\n", g_failed);
    }
    return g_failed;
}
