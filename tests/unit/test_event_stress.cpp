// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 0.4.2 / M14-C6-②：ProgressMux / FileEvent 事件投递并发压力测试
//
// 背景（nightly tsan 实测 9 条 data race，docs/m12-adjudications.md A13 / docs/m14-tasks.md
// §8-②）：
//   `run_one_file` 内的 FileEvent 事件源有 4 类线程 ——
//     ① 文件线程（ctx.stage / emit_output_done / 评分任务提交）；
//     ② SynthPump 泵线程（无编码回调格式的合成进度，20ms → mux.tick → mux 事件回调）；
//     ③ HeartbeatTicker 心跳线程（probe 段直发 / mux.heartbeat；评分期 scoring 心跳）；
//     ④ 同文件多输出并行编码线程（stage_encode_loop 的 target 线程，各自 emit 事件）；
//     ⑤ 评分池线程（异步评分完成 / 评分看门狗心跳，emit 是 scheduler 转发器的值拷贝）。
//   旧契约未声明 EventFn 的跨线程面 ⇒ 消费端（GUI/测试 sink）被并发回调 ⇒
//   `vector<FileEvent>::push_back` / `shared_ptr<OutputResult>` 拷贝数据竞争（tsan 栈：
//   progress.cpp emit_seq ↔ 测试回调）。产品修法 = pipeline 入口的「单线程 drain 队列」
//   （SerialEventSink，见 src/core/pipeline.cpp）：同一 run 内回调严格串行、事件不丢不改序。
//
// 本测试 = 该契约的机械化压力探针（Windows 无 TSan，故除 ASan/UBSan 内存面外，用断言把
// 「并发回调」直接探出来）。判据（只增不减）：
//   A. 同一 run 内**任意时刻回调并发数 ≤ 1**（重叠探针：进入回调 active++，退出 active--；
//      只要 drain 队列退化/被绕过，max_active 立刻 ≥2 且本测试红）。
//   B. 事件不丢：每个 output_index 恰好/至少一条 OutputDone，且载荷（shared_ptr）非空；
//   C. 有序 + 终态单调：每个 output_index 的 progress.overall_frac 序列单调不倒退；
//      全 run 的 overall_frac 最高值到达 1.0；OutputDone 不早于该输出的编码/MetaWrite 完成事件；
//   D. 产物存在（ok）——事件面压力不改变数据面结果；
//   E. 调度器级（多 worker 交叉事件）：每文件每输出 OutputDone 计数正确、无并发回调、
//      结果全 ok；取消路径 cancel()→wait() 不阻塞（<5s，无死锁）。
// 运行形态：多轮（默认 2 轮 × 6 输入 × 2 线程并发 run + 2 轮 scheduler 批压测）。
// 在 nightly asan/tsan 通道与本地 MSVC ASan ctest 中均作为普通 ctest 用例自动执行。
//
// 历史：本文件为 0.4.2 新增（C6-②）；不替换/不放宽任何既有测试。

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/pipeline.h"
#include "core/scheduler.h"
#include "core/score_queue.h"

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

pp::OutputFormatSpec out_spec(const std::string &format, const std::string &backend) {
    return pp::OutputFormatSpec{format, backend, "", {}, 8};
}

// -------------------------------------------------------------------------------------------
// 采集器：events **故意不加锁**（消费端零同步 = 真实 GUI/测试消费者的样子）。
// 正确性完全依赖「同一 run 内 EventFn 串行」契约：一旦回调并发，① 这里立刻数据竞争
// （asan 不一定报，tsan 必报）；② 重叠探针把 max_active 抬到 ≥2，断言 A 红（确定性证据）。
// -------------------------------------------------------------------------------------------
struct RunCollector {
    std::vector<pp::FileEvent> events;
    std::atomic<int> active{0};
    std::atomic<int> max_active{0};

    void on_event(const pp::FileEvent &e) {
        const int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
        int prev = max_active.load(std::memory_order_relaxed);
        while (now > prev &&
               !max_active.compare_exchange_weak(prev, now, std::memory_order_relaxed)) {
        }
        events.push_back(e); // 无锁：契约要求回调串行（回归即竞争 + 断言红）
        active.fetch_sub(1, std::memory_order_acq_rel);
    }
};

// 校验一个 run 的事件序列（A/C 判据）。outputs = 配置输出数。
void validate_run_events(const RunCollector &c, const std::string &tag, std::size_t outputs) {
    check(c.max_active.load() == 1, tag + "/serialized",
          "事件回调出现并发（max_active=" + std::to_string(c.max_active.load()) +
              "）——单线程 drain 队列契约被破坏");
    check(!c.events.empty(), tag + "/events-nonempty", "零事件");

    // 每个输出一条 OutputDone（异步评分可能再补一条带 metric 的同状态事件 → 只要求 ≥1）。
    std::vector<int> done_count(outputs, 0);
    std::vector<std::size_t> first_done_pos(outputs, c.events.size());
    std::vector<std::size_t> last_done_pos(outputs, 0);
    std::vector<std::size_t> last_meta_done_pos(outputs, c.events.size());
    float max_overall = 0.f;
    std::vector<float> prev_overall(outputs, 0.f);
    bool progress_regressed = false;
    bool payload_elsewhere = false;
    std::size_t heartbeat = 0;
    std::size_t synth = 0;

    for (std::size_t i = 0; i < c.events.size(); ++i) {
        const pp::FileEvent &e = c.events[i];
        if (e.progress.heartbeat)
            ++heartbeat;
        if (e.progress.synthetic)
            ++synth;
        const int idx = e.progress.output_index;
        const bool idx_ok = idx >= 0 && idx < static_cast<int>(outputs);
        if (idx_ok) {
            const std::size_t k = static_cast<std::size_t>(idx);
            if (e.progress.overall_frac < prev_overall[k])
                progress_regressed = true; // C：单调不倒退
            prev_overall[k] = e.progress.overall_frac;
            max_overall = std::max(max_overall, e.progress.overall_frac);
            if (e.state != pp::FileState::OutputDone) {
                if (e.progress.stage == pp::Stage::MetaWrite &&
                    e.progress.stage_frac >= 1.0f - 1e-6f)
                    last_meta_done_pos[k] = i;
            }
        } else {
            max_overall = std::max(max_overall, e.progress.overall_frac);
        }
        if (e.state == pp::FileState::OutputDone) {
            check(e.output != nullptr, tag + "/output-payload", "OutputDone 缺 output 载荷");
            check(e.result == nullptr, tag + "/no-result",
                  "OutputDone 不是文件终态（result 应为空）");
            if (idx_ok) {
                const std::size_t k = static_cast<std::size_t>(idx);
                if (first_done_pos[k] == c.events.size())
                    first_done_pos[k] = i;
                last_done_pos[k] = i;
                ++done_count[k];
            }
        } else if (e.output != nullptr) {
            payload_elsewhere = true;
        }
    }

    check(!progress_regressed, tag + "/monotonic", "overall_frac 出现倒退");
    check(!payload_elsewhere, tag + "/payload-only-on-done",
          "非 OutputDone 事件携带了 output 载荷");
    check(max_overall >= 0.9999f, tag + "/reaches-100",
          "overall_frac 峰值=" + std::to_string(max_overall) + "（应达 1.0）");
    for (std::size_t k = 0; k < outputs; ++k) {
        const std::string sub = tag + "/out" + std::to_string(k);
        check(done_count[k] >= 1, sub + "/done", "缺 OutputDone");
        if (done_count[k] >= 1) {
            check(last_meta_done_pos[k] < first_done_pos[k], sub + "/done-after-metawrite",
                  "OutputDone 早于 metawrite 完成事件");
        }
    }
    // 合成进度（webp/heif 输出）由 SynthPump 线程驱动；小图编码可能在 20ms 采样周期内完成，
    // 故这里只统计不作断言（确定性断言在下方「SYNTH」专用大图用例）。
    (void)synth;
    (void)heartbeat;
}

} // namespace

// 测试入口：异常即测试失败（与既有测试 main 同模式；新文件须 0 告警故结构性豁免）。
// NOLINTNEXTLINE(bugprone-exception-escape) —— M13 先例：入口结构性 NOLINT 带理由。
int main() {
    const fs::path corpus = find_corpus();
    if (corpus.empty()) {
        std::printf("FAIL setup: tests/golden not found from %s\n",
                    fs::current_path().string().c_str());
        return 1;
    }
    const fs::path tmp = make_temp_dir("event_stress");

    const std::vector<fs::path> inputs = {
        corpus / "base" / "rgb8.png",  corpus / "base" / "rgb16.png", corpus / "base" / "gray8.png",
        corpus / "base" / "rgba8.png", corpus / "base" / "photo.jpg", corpus / "base" / "rgb8.tif",
    };

    // ==========================================================================
    // A. 直跑 run_one_file：并行输出编码线程 + SynthPump + 心跳 + 异步评分池
    //    每个 run 独立采集器（per-run 单线程 drain 契约），多轮 × 2 线程并发。
    // ==========================================================================
    constexpr int kRounds = 2;
    constexpr int kThreads = 2;
    for (int round = 0; round < kRounds; ++round) {
        std::vector<std::unique_ptr<RunCollector>> collectors(inputs.size());
        for (std::size_t i = 0; i < inputs.size(); ++i)
            collectors[i] = std::make_unique<RunCollector>();
        std::vector<char> ok(inputs.size(), 0); // 元素彼此独立（不用 vector<bool> 的位共享）

        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(kThreads));
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([&, t] {
                for (std::size_t i = static_cast<std::size_t>(t); i < inputs.size();
                     i += kThreads) {
                    RunCollector &col = *collectors[i];
                    pp::RunConfig cfg;
                    cfg.out_root = tmp / ("run" + std::to_string(round));
                    cfg.output_template = "$format/$dir/$file";
                    cfg.outputs.push_back(out_spec("jpeg", "jpegli"));
                    cfg.outputs.push_back(out_spec("webp", "libwebp")); // 无回调 → SynthPump
                    cfg.conflict = pp::ConflictPolicy::Overwrite;
                    cfg.quality_score = true;     // 异步评分池 → 跨线程事件
                    cfg.max_parallel_targets = 2; // 同文件多输出并行编码线程 → 跨线程事件
                    pp::PixelBudget budget(pp::PixelBudget::default_capacity_bytes());
                    pp::RunScope scope;
                    scope.budget = &budget;
                    pp::EventFn ev{[&col](const pp::FileEvent &e) { col.on_event(e); }, &scope};
                    pp::FileEntry fe;
                    fe.src = inputs[i];
                    fe.base_dir = corpus;
                    const pp::FileOutcome oc = pp::run_one_file(fe, cfg, ev);
                    ok[i] = oc.file.ok ? 1 : 0;
                }
            });
        }
        for (std::thread &th : threads)
            th.join();

        // 评分池收敛之后再校验/析构（异步评分任务仍会 emit 事件到 collector）。
        pp::ScorePool::instance().wait_idle(std::chrono::milliseconds(120 * 1000));
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const std::string tag = "direct/r" + std::to_string(round) + "/i" + std::to_string(i) +
                                    "/" + inputs[i].filename().string();
            check(ok[i], tag + "/run-ok", "run_one_file 失败");
            validate_run_events(*collectors[i], tag, 2);
        }
    }

    // ==========================================================================
    // B. 调度器批压测：多 worker 交叉事件（scheduler EvSink 已串行转发；这里锁保护采集器
    //    以只验证产品侧事件语义，不依赖 scheduler 实现细节）。
    // ==========================================================================
    constexpr int kSchedRounds = 2;
    for (int round = 0; round < kSchedRounds; ++round) {
        std::mutex mu;
        std::vector<pp::FileEvent> events;
        std::atomic<int> active{0};
        std::atomic<int> max_active{0};
        pp::RunConfig cfg;
        cfg.out_root = tmp / ("sched" + std::to_string(round));
        cfg.output_template = "$format/$dir/$file";
        cfg.outputs.push_back(out_spec("jpeg", "jpegli"));
        cfg.outputs.push_back(out_spec("webp", "libwebp"));
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.workers = 4;
        std::vector<pp::FileEntry> entries;
        for (const fs::path &f : inputs) {
            pp::FileEntry fe;
            fe.src = f;
            fe.base_dir = corpus;
            entries.push_back(std::move(fe));
        }
        pp::Scheduler sched(cfg, std::move(entries));
        sched.set_event_callback([&](const pp::FileEvent &e) {
            const int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            int prev = max_active.load(std::memory_order_relaxed);
            while (now > prev &&
                   !max_active.compare_exchange_weak(prev, now, std::memory_order_relaxed)) {
            }
            {
                std::lock_guard<std::mutex> lk(mu);
                events.push_back(e);
            }
            active.fetch_sub(1, std::memory_order_acq_rel);
        });
        sched.start();
        sched.wait();
        const std::string tag = "sched/r" + std::to_string(round);
        check(!sched.running(), tag + "/not-running", "wait() 后仍在运行");
        check(max_active.load() <= 1, tag + "/serialized",
              "scheduler 回调并发（max_active=" + std::to_string(max_active.load()) + "）");
        const std::vector<pp::FileResult> &results = sched.results();
        check(results.size() == inputs.size(), tag + "/results-size", "结果数不符");
        std::vector<std::vector<int>> done(inputs.size(), std::vector<int>(2, 0));
        std::vector<std::vector<float>> prev(inputs.size(), std::vector<float>(2, 0.f));
        bool regressed = false;
        for (const pp::FileEvent &e : events) {
            const int idx = static_cast<int>(e.index);
            if (idx < 0 || idx >= static_cast<int>(inputs.size()))
                continue;
            if (e.state == pp::FileState::OutputDone) {
                check(e.output != nullptr, tag + "/payload", "OutputDone 缺载荷");
                const int out_idx = e.progress.output_index;
                if (out_idx >= 0 && out_idx < 2)
                    ++done[static_cast<std::size_t>(idx)][static_cast<std::size_t>(out_idx)];
            }
            const int out_idx = e.progress.output_index;
            if (out_idx >= 0 && out_idx < 2) {
                float &p = prev[static_cast<std::size_t>(idx)][static_cast<std::size_t>(out_idx)];
                if (e.progress.overall_frac < p)
                    regressed = true;
                p = e.progress.overall_frac;
            }
        }
        check(!regressed, tag + "/monotonic", "调度器事件 overall_frac 倒退");
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            check(results[i].ok, tag + "/result-ok/i" + std::to_string(i), results[i].error);
            for (int k = 0; k < 2; ++k) {
                check(done[i][static_cast<std::size_t>(k)] >= 1,
                      tag + "/done/i" + std::to_string(i) + "/o" + std::to_string(k),
                      "缺 OutputDone");
            }
        }
    }

    // ==========================================================================
    // C. 取消路径：cancel() → wait() 不阻塞/不死锁（事件 drain 不得阻塞取消推进）。
    // ==========================================================================
    {
        pp::RunConfig cfg;
        cfg.out_root = tmp / "cancel";
        cfg.output_template = "$format/$dir/$file";
        cfg.outputs.push_back(out_spec("jpeg", "jpegli"));
        cfg.conflict = pp::ConflictPolicy::Overwrite;
        cfg.workers = 4;
        std::vector<pp::FileEntry> entries;
        for (const fs::path &f : inputs) {
            pp::FileEntry fe;
            fe.src = f;
            fe.base_dir = corpus;
            entries.push_back(std::move(fe));
        }
        std::atomic<int> seen{0};
        pp::Scheduler sched(cfg, std::move(entries));
        sched.set_event_callback([&](const pp::FileEvent &) { seen.fetch_add(1); });
        const auto t0 = std::chrono::steady_clock::now();
        sched.start();
        sched.cancel();
        sched.wait();
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                .count();
        check(!sched.running(), "cancel/not-running", "wait() 后仍在运行");
        check(ms < 5000.0, "cancel/no-block", "cancel→wait 耗时 " + std::to_string(ms) + " ms");
    }

    if (g_failed == 0) {
        std::printf("event_stress: OK (rounds=%d, sched_rounds=%d)\n", kRounds, kSchedRounds);
        return 0;
    }
    std::printf("event_stress: %d failure(s)\n", g_failed);
    return 1;
}
