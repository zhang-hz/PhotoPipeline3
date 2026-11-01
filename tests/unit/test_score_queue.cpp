// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/score_queue（M10-SCOREFIX / team task-20）单测：
//   per-run 代次取消/复位 + 丢弃原因三分类 + pending 计数 + drain/超时宽限语义。
//
// Contract: build/m10/engine-scorefix 任务书 §交付 1/2/4；docs/v0.4.0-design.md §4.2.2(2) /
//           §4.6 C3/C14。进程级单例的取消/关池是**终态操作**，故取消类用例用显式
//           begin_run()/reset_for_run() 开新代次，shutdown() 留在 main 末尾（后续无池用例）。
//
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（仓内单测惯例）。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

#include "core/pipeline.h"
#include "core/score_queue.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num(unsigned long long v) { return std::to_string(v); }

// 造一个"已编码成功、待评分"的行（任务结果载体）。
std::shared_ptr<pp::OutputResult> make_row(const std::string &out) {
    auto row = std::make_shared<pp::OutputResult>();
    row->format_id = "jpeg";
    row->out = out;
    row->ok = true;
    row->metric_state = pp::MetricState::Off;
    return row;
}

pp::ScoreTask sleep_task(const std::string &out, int ms, std::atomic<int> *ran,
                         std::shared_ptr<pp::OutputResult> *row_out = nullptr) {
    auto row = make_row(out);
    if (row_out != nullptr)
        *row_out = row;
    pp::ScoreTask task;
    task.out = out;
    task.need_bytes = 1 << 20; // 1 MiB（远小于容量，不触发配额拒绝）
    task.result = row;
    task.run = [row, ms, ran] {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        row->metric_state = pp::MetricState::Ok;
        row->metric_ssimulacra2 = 91.5;
        row->metric_ms = static_cast<double>(ms);
        if (ran != nullptr)
            ++(*ran);
    };
    return task;
}

// 等某行被池发布（丢弃原因断言用；不伪造 wait_idle：只给有限重试）。
bool await_published(pp::ScorePool &pool, const std::string &out, int timeout_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
            .count() < timeout_ms) {
        if (pool.published(out) != nullptr)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pool.published(out) != nullptr;
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    using namespace std::chrono_literals;
    pp::ScorePool &pool = pp::ScorePool::instance();

    // ---------------------------------------------------------------------
    // A. 基本面 + 旧布尔语义（既有调用点/测试零改动）
    // ---------------------------------------------------------------------
    check(pool.workers() >= 1 && pool.workers() <= 4, "a/workers",
          "K 必须落在 [1,4]，got " + num(static_cast<unsigned long long>(pool.workers())));
    check(pool.capacity() > 0, "a/capacity", "容量必须为正");
    check(!pool.cancelled(), "a/cancelled-default", "新进程未取消时 cancelled() 必须为 false");
    {
        pp::ScoreTask invalid; // 无执行体 = 编程错误
        check(pool.submit_ex(std::move(invalid)) == pp::ScorePool::SubmitStatus::InvalidTask,
              "a/invalid-task", "无 run 的任务必须返回 InvalidTask");
        pp::ScoreTask huge;
        huge.out = "scorefix/too-big.out";
        huge.need_bytes = pool.capacity() + 1;
        huge.run = [] {};
        check(pool.submit_ex(std::move(huge)) == pp::ScorePool::SubmitStatus::QuotaExceeded,
              "a/quota-exceeded", "超容量必须返回 QuotaExceeded（P0 兜底）");
    }

    // ---------------------------------------------------------------------
    // B. run1 取消 → run2 同进程评分正常（核心回归；旧实现 cancel 后恒 false）
    // ---------------------------------------------------------------------
    {
        const pp::ScoreRunPtr run1 = pool.begin_run();
        check(run1->generation() > 0, "b/generation-positive", "代次号必须递增");
        check(!run1->cancelled(), "b/run1-not-cancelled", "新代次必须未取消");

        // 占满 K 个 worker：让后续任务停在队列里，稳定测「取消丢弃队列」。
        std::atomic<bool> release{false};
        std::atomic<int> blockers_started{0};
        for (int i = 0; i < pool.workers(); ++i) {
            pp::ScoreTask block;
            block.out = "scorefix/block-" + std::to_string(i);
            block.need_bytes = 1 << 20;
            block.result = make_row(block.out);
            block.run = [&release, &blockers_started] {
                ++blockers_started;
                while (!release.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            };
            check(pool.submit(block), "b/block-submit", "占位任务应被接受");
        }
        // 等占位任务全部开跑（pending = K 个运行中）。
        const auto tw0 = std::chrono::steady_clock::now();
        while (blockers_started.load() < pool.workers() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - tw0)
                       .count() < 5000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(blockers_started.load() == pool.workers(), "b/blockers-started",
              "期望 " + std::to_string(pool.workers()) + " 个占位任务开跑，got " +
                  std::to_string(blockers_started.load()));

        auto queued_row = make_row("scorefix/queued-drop.out");
        pp::ScoreTask queued;
        queued.out = "scorefix/queued-drop.out";
        queued.need_bytes = 1 << 20;
        queued.result = queued_row;
        queued.run = [] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); };
        check(pool.submit(queued), "b/queued-submit", "队列任务应被接受");
        check(pool.pending() == static_cast<std::size_t>(pool.workers()) + 1, "b/pending-count",
              "pending 必须 = 运行中 + 排队，got " + num(pool.pending()));
        check(pool.pending_of_run(run1) == static_cast<std::size_t>(pool.workers()) + 1,
              "b/pending-of-run", "该代次 pending 计数必须一致");

        // 重复 cancel 幂等 + 只取消当前代次。
        pool.cancel_pending();
        pool.cancel_pending();
        check(pool.cancelled(), "b/run1-cancelled", "cancel_pending 后 cancelled() 必须为 true");
        check(pool.pending() == static_cast<std::size_t>(pool.workers()), "b/pending-after-cancel",
              "队列中的任务必须被丢弃（只剩运行中的占位任务），got " + num(pool.pending()));
        check(await_published(pool, "scorefix/queued-drop.out", 3000), "b/drop-published",
              "被丢弃任务必须发布带原因的 Unavailable 行");
        if (const auto row = pool.published("scorefix/queued-drop.out")) {
            check(row->metric_state == pp::MetricState::Unavailable, "b/drop-state",
                  "丢弃行必须 Unavailable（不产生半评分）");
            check(row->metric_note == "运行取消，评分未完成", "b/drop-note",
                  "note 必须是「运行取消，评分未完成」，got 「" + row->metric_note + "」");
        }
        check(pool.submit_ex(sleep_task("scorefix/after-cancel.out", 1, nullptr)) ==
                  pp::ScorePool::SubmitStatus::RunCancelled,
              "b/cancel-rejects", "取消代次内 submit 必须返回 RunCancelled");

        release.store(true, std::memory_order_release);
        check(pool.wait_idle(20s), "b/blockers-converge", "占位任务必须在超时前收敛");

        // —— run2：显式复位（GUI/dev_cli 同口径），同一 out 路径重新评分成功 ——
        const pp::ScoreRunPtr run2 = pool.reset_for_run();
        const pp::ScoreRunPtr &ignored = run2;
        check(!run2->cancelled(), "b/run2-not-cancelled", "reset_for_run 后新代次必须未取消");
        check(!pool.cancelled(), "b/run2-cancelled-flag",
              "reset_for_run 后 cancelled() 必须为 false");
        check(run2->generation() > run1->generation(), "b/run2-new-generation",
              "新代次号必须大于旧代次");
        check(pool.published("scorefix/queued-drop.out") == nullptr, "b/old-run-published-hidden",
              "旧代次的发布行不得出现在新运行");
        std::atomic<int> ran2{0};
        std::shared_ptr<pp::OutputResult> row2;
        pp::ScoreTask t2 = sleep_task("scorefix/queued-drop.out", 20, &ran2, &row2);
        check(pool.submit_ex(std::move(t2)) == pp::ScorePool::SubmitStatus::Accepted,
              "b/run2-submit", "run1 取消后 run2 提交必须被接受（旧实现恒 false）");
        check(pool.wait_idle(20s), "b/run2-converge", "run2 必须在超时前收敛");
        check(ran2.load() == 1, "b/run2-ran",
              "run2 任务必须执行一次，got " + std::to_string(ran2.load()));
        if (const auto pub2 = pool.published("scorefix/queued-drop.out")) {
            check(pub2->metric_state == pp::MetricState::Ok, "b/run2-state",
                  "run2 发布行必须是 Ok 评分");
            check(pub2->metric_ssimulacra2 > 0.0, "b/run2-score", "run2 必须有真实分数");
        } else {
            check(false, "b/run2-published", "run2 必须发布评分行");
        }
        (void)ignored;
        std::printf("SCOREFIX scorepool run1_gen=%llu cancelled=1 run2_gen=%llu run2_ran=%d "
                    "run2_state=%d run2_pending=%zu\n",
                    static_cast<unsigned long long>(run1->generation()),
                    static_cast<unsigned long long>(run2->generation()), ran2.load(),
                    pool.published("scorefix/queued-drop.out") != nullptr
                        ? static_cast<int>(pool.published("scorefix/queued-drop.out")->metric_state)
                        : -1,
                    pool.pending());
    }

    // ---------------------------------------------------------------------
    // C. 重复 cancel 幂等（同一代次内多次调用）
    // ---------------------------------------------------------------------
    {
        (void)pool.begin_run();
        pool.cancel_pending();
        pool.cancel_pending();
        pool.cancel_pending();
        check(pool.cancelled(), "c/cancelled", "重复 cancel 后仍必须是 cancelled");
        check(pool.pending() == 0, "c/pending-zero", "重复 cancel 后 pending 必须为 0");
        check(pool.wait_idle(5s), "c/wait-idle", "取消后的池必须可收敛");
    }

    // ---------------------------------------------------------------------
    // D. drain：正常收敛 / 超时强制取消 + 宽限收敛 / 宽限内仍不收敛（如实 pending>0）
    // ---------------------------------------------------------------------
    {
        (void)pool.begin_run();
        std::atomic<int> ran{0};
        check(pool.submit(sleep_task("scorefix/drain-ok.out", 120, &ran)), "d/submit-ok",
              "drain 正常路径任务应被接受");
        const pp::ScorePool::DrainResult ok = pool.drain(5s, 1s, nullptr, 10ms);
        check(ok.quiesced && !ok.forced_cancel, "d/drain-quiesced",
              "drain 必须在超时前自然收敛（不触发强制取消）");
        check(ok.pending_at_start == 1, "d/drain-pending-at-start",
              "drain 起始 pending 必须为 1，got " + num(ok.pending_at_start));
        check(ok.wait_ms >= 50, "d/drain-wait-ms",
              "drain 必须真实等待评分任务，wait_ms=" + std::to_string(ok.wait_ms));
        check(ok.pending == 0, "d/drain-pending-zero", "收敛后 pending 必须为 0");

        std::atomic<int> ran2{0};
        check(pool.submit(sleep_task("scorefix/drain-timeout.out", 150, &ran2)), "d/submit-timeout",
              "超时路径任务应被接受");
        const pp::ScorePool::DrainResult forced = pool.drain(20ms, 5s, nullptr, 10ms);
        check(forced.forced_cancel, "d/drain-forced", "20ms 超时必须触发强制取消");
        check(forced.quiesced, "d/drain-grace-quiesced", "宽限内任务必须收敛");
        check(forced.grace_ms > 0, "d/drain-grace-ms", "宽限耗时必须 >0");
        check(forced.pending == 0, "d/drain-forced-pending", "宽限收敛后 pending 必须为 0");

        // 宽限内仍不收敛：如实返回 pending>0（不粉饰成已收敛）。
        (void)pool.begin_run();
        pp::ScoreTask slow;
        slow.out = "scorefix/drain-still-busy.out";
        slow.need_bytes = 1 << 20;
        slow.result = make_row(slow.out);
        slow.run = [] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); };
        check(pool.submit(std::move(slow)), "d/submit-busy", "慢任务应被接受");
        const pp::ScorePool::DrainResult busy = pool.drain(20ms, 20ms, nullptr, 10ms);
        check(busy.forced_cancel && !busy.quiesced, "d/drain-still-busy",
              "宽限内不收敛必须返回 quiesced=false");
        check(busy.pending == 1, "d/drain-busy-pending",
              "未收敛时 pending 必须 >0，got " + num(busy.pending));
        check(pool.wait_idle(10s), "d/drain-busy-eventual", "慢任务最终必须收敛");
        std::printf("SCOREFIX drain ok(wait_ms=%llu pending=%zu) forced(grace_ms=%llu "
                    "quiesced=%d) busy(quiesced=%d pending=%zu)\n",
                    static_cast<unsigned long long>(ok.wait_ms), ok.pending,
                    static_cast<unsigned long long>(forced.grace_ms), forced.quiesced ? 1 : 0,
                    busy.quiesced ? 1 : 0, busy.pending);
    }

    // ---------------------------------------------------------------------
    // E. 关池：拒收新任务（PoolStopping）+ 队列任务写「评分池已关闭」原因
    //    （本段在 main 末尾：shutdown 对进程级池是终态操作）
    // ---------------------------------------------------------------------
    {
        (void)pool.begin_run();
        std::atomic<bool> release{false};
        std::atomic<int> started{0};
        for (int i = 0; i < pool.workers(); ++i) {
            pp::ScoreTask block;
            block.out = "scorefix/shutdown-block-" + std::to_string(i);
            block.need_bytes = 1 << 20;
            block.result = make_row(block.out);
            block.run = [&release, &started] {
                ++started;
                while (!release.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
            };
            check(pool.submit(block), "e/block-submit", "关池用例占位任务应被接受");
        }
        const auto t0 = std::chrono::steady_clock::now();
        while (started.load() < pool.workers() &&
               std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - t0)
                       .count() < 5000) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        auto queued_row = make_row("scorefix/shutdown-queued.out");
        pp::ScoreTask queued;
        queued.out = "scorefix/shutdown-queued.out";
        queued.need_bytes = 1 << 20;
        queued.result = queued_row;
        queued.run = [] {};
        check(pool.submit(queued), "e/queued-submit", "关池前队列任务应被接受");
        std::thread releaser([&release] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            release.store(true, std::memory_order_release);
        });
        pool.shutdown(); // 阻塞至 worker join（占位任务在 50ms 后放行）
        releaser.join();
        check(pool.wait_idle(5s), "e/shutdown-idle", "关池后池必须为空");
        pp::ScoreTask after;
        after.out = "scorefix/after-shutdown.out";
        after.need_bytes = 1 << 20;
        after.run = [] {};
        check(pool.submit_ex(std::move(after)) == pp::ScorePool::SubmitStatus::PoolStopping,
              "e/shutdown-rejects", "关池后 submit 必须返回 PoolStopping");
        if (const auto row = pool.published("scorefix/shutdown-queued.out")) {
            check(row->metric_state == pp::MetricState::Unavailable, "e/shutdown-state",
                  "关池丢弃行必须 Unavailable");
            check(row->metric_note == "评分池已关闭，评分未完成", "e/shutdown-note",
                  "note 必须是「评分池已关闭，评分未完成」，got 「" + row->metric_note + "」");
        } else {
            // 竞态：队列任务可能已被 worker 取走并执行完（1ms 内）；此时不产生丢弃行也不失败。
            std::printf("NOTE e/shutdown-queued: queued task was picked before shutdown\n");
        }
    }

    if (g_failed == 0)
        std::printf("OK scorefix score_queue\n");
    return g_failed;
}
