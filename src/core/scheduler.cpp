// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M1-T8: worker-pool scheduler (docs/m1-tasks.md §3.10).
// M4-W1-T7: E3 adaptive thread budget + staggered start (docs/v0.3.0-design.md §8).
//
// Frozen semantics:
//   * workers = cfg.workers, else thread_budget, else hardware_concurrency (logical cores;
//     R-283 fix: the old "physical core count" wording never matched the implementation);
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
// step itself (early failure + budget/serialisation decisions); M10 / W1-T3（A4，additive
// `FileEntry::probe`）起该 probe 结果经缓存回填给 run_one_file 复用 —— 每文件只 probe 一次
// （旧口径"冻结接口无 spec 字段 → 每文件两次 probe"已由 A4 加性字段消除，见 pipeline.h）。
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
#include <cmath>
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
#include "core/score_queue.h"
#include "core/thread_plan.h" // 0.4.2 需求6：自适应线程规划（纯函数层）
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
        // R-175（M7-WP8/B.8）：PathCtx 组装收口 path_ctx_for（与 pipeline plan_targets 同源）；
        // fmt 缺失时回退 spec.format_id（旧行为：format_dir = fmt ? fmt->id : spec.format_id）。
        PathCtx ctx = path_ctx_for(fe, cfg, fmt);
        if (!fmt)
            ctx.format_dir = spec.format_id;
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
// M10-SCOREFIX：异步评分任务的事件出口（shared_ptr 生命周期令牌）
//   评分任务只持有 `std::shared_ptr<EventSink>`，**不持有 Scheduler::Impl***。Scheduler 析构
//   时 retire()：此后迟到的事件是 no-op（不再回调 GUI/调度器）；sink 本身由任务共享持有，
//   任何时刻释放都不会悬垂（评分任务已与 Impl 生命周期解耦）。
//   last_progress（终态高水位）随 sink 持有：文件线程的 emit 更新、finish 读取；
//   评分池线程补发的 OutputDone 也经同一 sink（只更新该槽位高水位，不推进文件终态）。
// ---------------------------------------------------------------------------
struct EventSink {
    using Cb = Scheduler::EventCb; // std::function<void(const FileEvent &)>

    mutable std::mutex mu;
    Cb cb;                                   // guarded by mu
    std::vector<ProgressInfo> last_progress; // guarded by mu
    bool alive = true;                       // guarded by mu
    // 0.4.2 / C6-②（M14；TSan 竞态债收口的调度器侧）：EventCb 的**跨线程串行化**。
    //   emit()/finish() 会被多个 worker 线程（文件线程）与评分池线程并发调用；旧实现只把
    //   `mu` 用于取 cb/兜高水位，回调本身在数据锁之外调用 ⇒ 消费端（GUI/测试 sink）会被并发
    //   回调（stress 探针实测 max_active=2；nightly tsan 的 9 条 race 同族）。此处用独立
    //   `recursive_mutex` 把回调调用收成同一时刻至多一个线程：
    //     * 不持数据锁 mu 调回调 ⇒ 回调内可安全再调 Scheduler 的读数/取消方法（锁序 mu → cb_mu，
    //       反向不存在）；
    //     * recursive ⇒ 回调内同线程再触发事件（幂等重入）不会自锁；
    //     * 契约：EventCb 不得阻塞等待另一个会 emit 事件的线程（GUI 走 queued 连接，天然满足；
    //       本条与 M4 起的既有「回调不得阻塞」约定一致）。
    std::recursive_mutex cb_mu;

    void set_callback(Cb c) {
        std::lock_guard<std::mutex> lk(mu);
        cb = std::move(c);
    }

    void retire() {
        std::lock_guard<std::mutex> lk(mu);
        alive = false;
        cb = {};
    }

    // 阶段/进度事件转发：index 由调度器回填、result 恒空（终态由 finish() 给）。
    void emit(std::size_t i, const FileEvent &e) {
        Cb c;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!alive)
                return;
            if (i < last_progress.size())
                last_progress[i] = e.progress;
            c = cb;
        }
        if (c) {
            FileEvent out{};
            out.index = i;
            out.state = e.state;
            out.result = nullptr;
            out.progress = e.progress;
            out.output = e.output;
            std::lock_guard<std::recursive_mutex> cb_lk(cb_mu); // C6-②：跨线程回调串行
            c(out);
        }
    }

    // 终态事件：附该槽位最后进度高水位（result 指向 Impl::results 的稳定槽位）。
    void finish(std::size_t i, FileState state, const FileResult *result) {
        Cb c;
        ProgressInfo pi;
        {
            std::lock_guard<std::mutex> lk(mu);
            if (!alive)
                return;
            c = cb;
            if (i < last_progress.size())
                pi = last_progress[i];
        }
        if (c) {
            std::lock_guard<std::recursive_mutex> cb_lk(cb_mu); // C6-②：跨线程回调串行
            c(FileEvent{i, state, result, pi});
        }
    }
};

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
    // M10-SCOREFIX：事件出口改为 shared_ptr（评分任务只持有它，不持有 Impl*）。
    // `cb`/`last_progress` 均已迁入 EventSink（sink 为空 = 未初始化，防御）。
    std::shared_ptr<EventSink> sink;
    // 本次运行的评分代次令牌（Scheduler::start 建立；析构时取消 + 有界等待）。
    ScoreRunPtr score_run;

    std::unique_ptr<PixelBudget> budget;

    std::vector<std::filesystem::path> reserved; // guarded by mu
    std::multiset<std::string> inflight;         // guarded by mu
    // W1-T6（§3.3/§7.1）：每槽位的进度高水位快照已迁入 EventSink::last_progress
    // （guarded by sink->mu；终态事件携带它 —— 消费端读终态即得"该文件最后的进度"）。

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

    // —— 0.4.2 需求6（M14）：自适应线程规划（CPU 实时负载 × 编码器能力 × 多输出并行）——
    // 规划逻辑本体 = core/thread_plan.h 的纯函数（单测穷举）；本处只持有运行期状态与采样桥。
    std::vector<EncoderThreadProfile> plan_profiles; // start() 由 cfg.outputs 派生；运行期只读
    ThreadPlanState plan_state;                      // guarded by mu（迟滞状态）
    CpuLoadEma cpu_ema;                              // guarded by mu（CPU 利用率 EMA）
    CpuTimes cpu_prev;                               // guarded by mu（上次原始计数）
    bool cpu_prev_valid = false;                     // guarded by mu
    ThreadPlan last_plan;                            // guarded by mu（调试/读数：最近一次规划）
    int peak_parallel_outputs = 1;                   // guarded by mu（峰值 P，调试读数）
    long long plan_converges = 0;                    // guarded by mu（CPU 饱和收敛次数）

    Clock::time_point t_start{};
    double elapsed_ms = 0; // guarded by mu (set by wait())

    Impl(RunConfig c, std::vector<FileEntry> f)
        : cfg(std::move(c)), files(std::move(f)), stagger(0) {
        results.resize(files.size());
        sink = std::make_shared<EventSink>();
        sink->last_progress.resize(files.size());
        for (std::size_t i = 0; i < files.size(); ++i)
            results[i].src = files[i].src;
        // §3.3：ScheduleConfig 由 RunConfig 的 workers/stagger_ms/thread_budget 同名派生。
        sc.workers = cfg.workers;
        sc.stagger_ms = cfg.stagger_ms;
        sc.thread_budget = cfg.thread_budget;
    }

    // §8.2：本次入闸的分配。必须在 mu 下调用（读 done/next/files）。
    //   remaining = 未取件数、inflight = 已取件未完成数（§8.2 公式的两个实参口径）。
    //   0.4.2 需求6：抽成 unfinished_locked()（并发闸 W 与规划器共用同一"未完成数"口径）。
    int unfinished_locked() const {
        const std::size_t taken = next.load(std::memory_order_relaxed);
        const std::size_t unprocessed = files.size() > taken ? files.size() - taken : 0;
        const std::size_t inflight_n = taken > done ? taken - done : 0;
        const std::size_t unfinished = unprocessed + inflight_n;
        // 输入数 ≤ INT_MAX 是内存可行批次的既有前提（events/日志同样按 int 打印）。
        return static_cast<int>(std::min<std::size_t>(unfinished, 0x7fffffff));
    }
    Alloc alloc_now_locked() const { return alloc_threads(thread_budget, unfinished_locked(), 0); }

    // —— 0.4.2 需求6：平台 CPU 负载采样（只在"入闸决策点"调用；无常驻采样线程）——
    // 返回 CPU 利用率 EMA 0..1；<0 = 未知（尚无两次有效读数 / 平台读数失败）。
    // 必须在 mu 下调用（写 cpu_prev/cpu_ema）。GetSystemTimes 读数开销微秒级，不引入等待。
    double sample_cpu_locked() {
        CpuTimes now;
        if (!read_cpu_times(now))
            return cpu_ema.value; // 平台失败 → 保持未知/上次值（规划器按保守路径处理）
        if (cpu_prev_valid) {
            const double load = cpu_load_between(cpu_prev, now);
            if (load >= 0.0)
                cpu_ema.push(load);
        }
        cpu_prev = now;
        cpu_prev_valid = true;
        return cpu_ema.value;
    }

    // 阶段/进度事件转发（W1-T6）：整事件转发（`progress` 随行），index 由调度器回填，
    // result 恒空（终态由 finish() 携带）。
    // R-288（M7-WP8 收敛）：进度单调化**单层**在 ProgressMux::overall_high（含 snapshot）；
    // 本层只缓存"该槽位最后进度"供终态携带，不再做第二套高水位比较。
    // W1-B1（A2）：`FileEvent::output`（shared_ptr<const OutputResult>）**原样转发** —— 共享
    // 所有权让 GUI 的排队连接安全（事件投递后任意时刻消费都不会悬垂）；逐输出终态事件
    // （state == OutputDone）不更新 last_progress 之外的任何状态（文件终态仍由 finish() 给）。
    // M10-SCOREFIX：转发体在 EventSink（shared_ptr）里 —— 评分任务的事件出口只持有 sink。
    void emit_event(std::size_t i, const FileEvent &e) {
        if (sink)
            sink->emit(i, e);
    }

    void emit_stage(std::size_t i, FileState s) { emit_event(i, FileEvent{i, s, nullptr, {}}); }

    // Stores the terminal result and emits the terminal event (pointer into results()).
    // 终态映射（§4.1 聚合 / §3.3 事件）：Verdict → FileState 走 file_state_of 单源（R-076，
    // M7-R2B：DoneWithErrors → FileState::DoneWithErrors，多输出部分成功独立档；旧口径把它
    // 有损映射为 Done，注释自认 FileState 无该值 —— 表缺口已由 M7-WP1 I7 加性补齐）。
    // FileResult.ok=false 让 RunSummary 与 dev harness 的失败计数仍把"任一输出失败"的文件
    // 算作失败（0.2 语义，零改动）。
    // W1-T6：终态事件附**最后进度高水位**（消费端无需另存进度流；W2/W3 运行页读终态即得终值）。
    // W1-T7：终态同时是"完成"计数点（§8.2 的 remaining/inflight 口径）—— 每条终态路径都经此，
    // 故 done 与 results 槽位在同一临界区内推进（并发闸的宽度只会随之收缩，不会漏计）。
    void finish(std::size_t i, FileOutcome outcome) {
        const FileState state = file_state_of(outcome.verdict);
        FileResult r = std::move(outcome.file);
        {
            std::lock_guard<std::mutex> lk(mu);
            results[i] = std::move(r);
            ++done;
        }
        if (sink)
            sink->finish(i, state, &results[i]);
    }

    // R-174（M7-WP8/B.8）：取消出口**单点** —— 四段复制的 Cancelled 终态构造合一。
    // 持键路径（闸等待中被取消）先在独立临界区放键再 notify —— 与主路径同一"放键即通知"
    // 口径；死锁序不变量（勿动顺序）：
    //   INVARIANT(D1)：desired-key 串行化 **先于** 并发闸登记 —— worker 绝不"持闸等键"
    //   （W=1 且两文件同键时会互等）；持键等闸不会成环（持键者必在闸内或即将入闸）。
    //   放闸/放键各自在同一临界区内完成，其后 notify_all。
    void cancel_exit(std::size_t i, FileEntry &fe, const std::vector<std::string> *held_keys) {
        if (held_keys != nullptr) {
            {
                std::lock_guard<std::mutex> lk2(mu);
                for (const std::string &k : *held_keys) {
                    const auto it = inflight.find(k);
                    if (it != inflight.end())
                        inflight.erase(it);
                }
            }
            cv.notify_all();
        }
        FileOutcome o;
        o.verdict = FileOutcome::Verdict::Cancelled;
        o.file.src = fe.src;
        o.file.cancelled = true;
        finish(i, std::move(o));
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
            cancel_exit(i, fe, nullptr); // R-174：取消出口单点
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
                // decode/encode；也不占用预算与并发闸）。R-174：出口单点。
                cancel_exit(i, fe, nullptr);
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
        // X-METADATA-ONLY-07：仅元数据不解码像素，worker 层不做 OIIO probe（CMYK/截断源
        // 不应被拒）；run_metadata_only 内部只在需要时做可选 probe。
        if (!cfg.metadata_only && !fe.probe_done) {
            ProbeOutcome po = probe_file(fe.src);
            if (!po.error.empty()) {
                FileOutcome o;
                o.verdict = FileOutcome::Verdict::Failed;
                o.file.src = fe.src;
                o.file.error = "probe failed: " + po.error;
                finish(i, std::move(o));
                continue;
            }
            // M10 / W1-T3（A4）：完整 probe 缓存回填（pipeline.cpp 的 stage_probe 直接复用，
            // 消除每文件第二次 probe —— HEIC 输入 pre-decode 3.27s → 目标 ≤1.0s 的同批修复）。
            // 缓存语义 = 同一次 run 内不变；文件被外部改写属用户竞态，如实记录、不处理。
            fe.probe = std::move(po);
            fe.info = fe.probe.info;
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
                cancel_exit(i, fe, nullptr); // R-174：取消出口单点（键尚未登记）
                continue;
            }
            for (const std::string &k : keys)
                inflight.insert(k);
        }

        // ---- §8.2 并发闸（信号量限 W = alloc_threads(T, remaining, inflight).files）----
        // 取件 → 算 Alloc → 更新闸 → EncodeRequest.encode_threads=E。闸宽度随队列深度动态收缩
        // （剩余文件少 → W 小、E 大）；不变式 active < W ⇒ 入闸后 active ≤ W ≤ T 且 ΣE ≤ T。
        // 0.4.2 需求6：入闸决策点上再经自适应规划器（core/thread_plan.h）派生 E/P ——
        //   * W（alloc.files）仍恒等于冻结公式（并发闸语义零改动）；
        //   * E/P 由"编码器能力 × CPU 实时负载 × 浅/深队列"决定，且 W×P×E ≤ T 硬约束；
        //   * CPU 未知/单文件运行 → 规划器回退契约基值（不冒进）。
        Alloc alloc{1, 1};
        ThreadPlan plan{};
        int active_now = 0; // 入闸后的读数（供日志行；TSan 口径：只在 mu 下读写）
        long long sum_now = 0;
        double cpu_load = -1.0;
        {
            std::unique_lock<std::mutex> lk(mu);
            cv.wait(lk,
                    [this] { return cancelled.load() || active_files < alloc_now_locked().files; });
            if (cancelled.load()) {
                lk.unlock();
                // 键已在上面登记 → 与主路径同样释放（否则同键文件永久互等）。
                // R-174：出口单点（持键放键 + notify 在 cancel_exit 内）。
                cancel_exit(i, fe, &keys);
                continue;
            }
            alloc = alloc_now_locked();
            // 自适应线程规划（入闸决策点；仅此处采样 CPU，无额外常驻线程）
            ThreadPlanInput pin;
            pin.thread_budget = thread_budget;
            pin.unfinished = unfinished_locked();
            pin.output_count = static_cast<int>(cfg.outputs.size());
            pin.user_parallel_cap = cfg.max_parallel_targets;
            pin.cpu_load = sample_cpu_locked();
            pin.now_ms = steady_now_ms();
            pin.profiles = plan_profiles.empty() ? nullptr : &plan_profiles;
            plan = plan_threads(pin, plan_state);
            cpu_load = pin.cpu_load;
            // 防御：W 恒等于冻结公式（规划器不改并发闸宽度；若不一致以契约为准）。
            if (plan.files != alloc.files)
                plan.files = alloc.files;
            ++active_files;
            // E3 ΣE 口径（0.4.2）：计入 P 路真实并发（多输出运行时 P×E），旧键语义不变。
            sum_encode_threads +=
                static_cast<long long>(plan.encode_threads) * plan.parallel_outputs;
            peak_active_files = std::max(peak_active_files, active_files);
            peak_sum_encode_threads = std::max(peak_sum_encode_threads, sum_encode_threads);
            peak_parallel_outputs = std::max(peak_parallel_outputs, plan.parallel_outputs);
            if (std::string_view(plan.action) == "converge")
                ++plan_converges;
            last_plan = plan;
            active_now = active_files;
            sum_now = sum_encode_threads;
        }
        // §8.2 日志点位：线程分配读数（E3 契约在运行日志中可查证）。
        // 键集**逐字不变**（回归基线 N11 按这些键归一；新增规划轨迹走 debug 行，见下）。
        log_info(kStageSched, kFile, "thread alloc",
                 {{"index", std::to_string(i)},
                  {"alloc_files", std::to_string(alloc.files)},
                  {"encode_threads", std::to_string(plan.encode_threads)},
                  {"active_files", std::to_string(active_now)},
                  {"sum_encode_threads", std::to_string(sum_now)},
                  {"thread_budget", std::to_string(thread_budget)}});
        // 0.4.2 需求6：规划轨迹（**debug 级** —— 回归基线跑 info 级，机器相关量不进基线；
        // 单测/实测用 --log-level debug 或 log_init(Debug) 复核）。
        log_debug(kStageSched, kFile, "thread plan",
                  {{"index", std::to_string(i)},
                   {"alloc_files", std::to_string(alloc.files)},
                   {"encode_threads", std::to_string(plan.encode_threads)},
                   {"parallel_outputs", std::to_string(plan.parallel_outputs)},
                   {"outputs", std::to_string(cfg.outputs.size())},
                   {"cpu_load_pct", cpu_load < 0.0 ? std::string("unknown")
                                                   : std::to_string(static_cast<int>(
                                                         std::lround(cpu_load * 100.0)))},
                   {"est_threads", std::to_string(plan.est_threads)},
                   {"action", plan.action},
                   {"reason", plan.reason}});

        RunScope scope;
        {
            std::lock_guard<std::mutex> lk(mu);
            scope.reserved = reserved;
        }
        scope.budget = budget.get();
        scope.cancelled = [this] { return cancelled.load(); };
        scope.encode_threads = plan.encode_threads;     // §3.1 线程映射义务的输入（E ≥ 1）
        scope.parallel_outputs = plan.parallel_outputs; // 0.4.2：多输出并行度 P（1 = 串行）

        FileOutcome outcome;
        try {
            // M10-SCOREFIX：只捕获 shared_ptr<EventSink> + 槽位下标（**不捕获 Impl***）。
            // 该 std::function 会被评分任务按值拷贝并在池线程调用；sink 的 shared_ptr 保证
            // 生命周期，Impl 析构时 sink->retire() 让迟到事件变 no-op。
            const std::shared_ptr<EventSink> ev_sink = sink;
            EventFn events{[ev_sink, i](const FileEvent &e) {
                               if (ev_sink)
                                   ev_sink->emit(i, e);
                           },
                           &scope};
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
            sum_encode_threads -=
                static_cast<long long>(plan.encode_threads) * plan.parallel_outputs;
            for (const std::string &k : keys) {
                const auto it = inflight.find(k);
                if (it != inflight.end())
                    inflight.erase(it);
            }
            // §4.4：reserved 按**每个 out_path** 登记（一源多输出 → 多条）。
            // R-005（P1）：**仅成功输出**入册 —— 失败输出没写出文件却占名，会让后续同名源
            // 被幽灵跳过（"skipped: output exists" 而磁盘上什么都没有，用户漏图）。
            // 批内同键串行由 inflight/desired_keys 保证（不靠 reserved）。
            for (const OutputResult &row : outcome.file.outputs) {
                if (row.ok && !row.out.empty())
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
    if (!impl_)
        return;
    // M10-SCOREFIX（所有权收口）：
    //   ① 评分任务的事件出口是 shared_ptr<EventSink>（不持有 Impl*）—— 这里 retire() 后，
    //      仍在跑的评分任务即便补发 OutputDone 也是 no-op（不会回调已销毁的消费端）；
    //   ② 本运行代次的评分任务取消 + **有界等待**（双保险：配额/参考帧尽快归还，发布表不再
    //      变动）。等待有界 = 关窗/异常退出不无限阻塞；超时如实 warn。
    if (impl_->sink)
        impl_->sink->retire();
    if (impl_->score_run) {
        ScorePool &pool = ScorePool::instance();
        pool.cancel_run(impl_->score_run);
        constexpr auto kShutdownWait = std::chrono::milliseconds(30 * 1000);
        const bool quiesced = pool.wait_run_idle(impl_->score_run, kShutdownWait);
        log_debug(kStage, kFile, "score run teardown",
                  {{"generation", std::to_string(impl_->score_run->generation())},
                   {"quiesced", quiesced ? "true" : "false"},
                   {"pending", std::to_string(pool.pending())}});
        if (!quiesced) {
            log_warn(
                kStage, kFile, "score tasks still running after scheduler shutdown",
                {{"pending", std::to_string(pool.pending())},
                 {"timeout_ms", std::to_string(static_cast<long long>(kShutdownWait.count()))}});
        }
    }
}

void Scheduler::set_event_callback(EventCb cb) {
    if (impl_ && impl_->sink)
        impl_->sink->set_callback(std::move(cb));
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
    // —— 0.4.2 需求6：规划器运行期状态初始化（编码器能力表 + CPU 基线读数）——
    // 能力表 = 本次运行各输出的编码器线程事实（thread_plan.h 的单源函数）；运行期只读。
    // CPU 基线 = start() 时先采一次原始计数，使首个入闸决策有可用区间（区间过短 → 未知，
    // 规划器按保守路径处理，不冒进）。
    d.plan_profiles.clear();
    d.plan_profiles.reserve(d.cfg.outputs.size());
    for (const OutputFormatSpec &spec : d.cfg.outputs)
        d.plan_profiles.push_back(encoder_thread_profile(spec.format_id));
    {
        std::lock_guard<std::mutex> lk(d.mu);
        d.plan_state.reset();
        d.cpu_ema.reset();
        d.cpu_prev_valid = false;
        d.peak_parallel_outputs = 1;
        d.plan_converges = 0;
        d.last_plan = ThreadPlan{};
        (void)d.sample_cpu_locked(); // 建立第一次读数（无前值时返回 unknown）
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

    // M10-SCOREFIX：一次运行 = 一个评分代次。GUI / dev_cli / 单测都经 Scheduler::start 进入
    // 运行，故"下一次 run 自动复位评分池"在这里兜底：上一代的取消状态不会延续到本次运行
    // （消除"取消一次 → 同进程后续运行评分全 Unavailable"的一次性缺陷），上一代队列被丢弃。
    // 显式接线（reset_for_run）另见 GUI on_start 与 dev_cli；重复 begin_run 只是多一个空代次，
    // 任务始终绑定 start() 建立的代次（pipeline submit 取 current_run）。
    if (d.cfg.quality_score && !d.cfg.metadata_only) {
        d.score_run = ScorePool::instance().begin_run();
        log_debug(kStage, kFile, "score pool run generation",
                  {{"generation", std::to_string(d.score_run->generation())}});
    }

    for (int i = 0; i < n; ++i)
        d.pool.emplace_back([this] { impl_->worker(); });
}

void Scheduler::cancel() {
    impl_->cancelled.store(true);
    impl_->cv.notify_all(); // wake workers blocked on the output-target serialisation
    // M10-SCOREFIX：运行取消同步取消本次运行的评分代次（在飞评分在检查点退出、队列丢弃；
    // 不产生半评分）。GUI 另调 ScorePool::cancel_pending()，两者对同一代次幂等。
    if (impl_->score_run)
        (void)ScorePool::instance().cancel_run(impl_->score_run);
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
        // 0.4.2 需求6：运行级规划读数（**debug 级**，不进回归基线；单测/实测按需开启）。
        {
            std::lock_guard<std::mutex> lk(d.mu);
            const double cpu = d.cpu_ema.value;
            log_debug(kStageSched, kFile, "thread plan report",
                      {{"thread_budget", std::to_string(d.thread_budget)},
                       {"peak_parallel_outputs", std::to_string(d.peak_parallel_outputs)},
                       {"converges", std::to_string(d.plan_converges)},
                       {"last_action", d.last_plan.action},
                       {"last_reason", d.last_plan.reason},
                       {"last_encode_threads", std::to_string(d.last_plan.encode_threads)},
                       {"last_parallel_outputs", std::to_string(d.last_plan.parallel_outputs)},
                       {"cpu_load_pct",
                        cpu < 0.0 ? std::string("unknown")
                                  : std::to_string(static_cast<int>(std::lround(cpu * 100.0)))}});
        }
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
    // M5-T12/A29（RunSummary::peak_bytes，加性）：与 wait() 的 `budget report` 行**同一统计点**
    // （PixelBudget::peak()），读数单源、不在本函数里另算。budget 由 start() 建（未 start = 空批
    // → 0）；PixelBudget::peak() 自持锁，UI 在运行中轮询 summary() 亦安全。
    s.peak_bytes = d.budget ? d.budget->peak() : 0;
    // R-078（M7-WP8，加性）：实测线程统计 —— 与 wait() 的 `e3 report` 行同一统计点
    // （peak_active_files / peak_sum_encode_threads），读数单源。threads_total_est =
    // 两族峰值之和的**估算口径**（SynthPump/x265 库内池不入账；发行说明须写明，见 handoff）。
    s.peak_active_files = d.peak_active_files;
    s.peak_sum_encode_threads = d.peak_sum_encode_threads;
    s.threads_total_est =
        static_cast<int>(static_cast<long long>(d.peak_active_files) + d.peak_sum_encode_threads);
    return s;
}

// R-076（M7-R2B）：终态映射单源（五值全表；test_scheduler_contract 钉住）。
// DoneWithErrors → FileState::DoneWithErrors = 一一配对（M7-WP1 I7 的枚举值在此接上消费面）。
FileState file_state_of(FileOutcome::Verdict v) {
    switch (v) {
    case FileOutcome::Verdict::Done:
        return FileState::Done;
    case FileOutcome::Verdict::DoneWithErrors:
        return FileState::DoneWithErrors;
    case FileOutcome::Verdict::Failed:
        return FileState::Failed;
    case FileOutcome::Verdict::Skipped:
        return FileState::Skipped;
    case FileOutcome::Verdict::Cancelled:
        return FileState::Cancelled;
    }
    return FileState::Failed; // 不可达（枚举全列举）；防御性兜底不改变既有语义
}

} // namespace pp
