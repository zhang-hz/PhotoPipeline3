// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 0.4.2 需求 6：自适应线程规划（实现）。
//
// 设计事实（逐条可复核；完整背景见 thread_plan.h 文件头）：
//   F1 冻结公式 `alloc_threads`（src/core/scheduler.cpp，公式逐字照 docs/v0.3.0-design.md
//      §8.2）只按剩余文件数派生 W/E；本模块**复用**它为基值（W 恒等于它），不复制公式。
//   F2 同文件多输出并行度此前是 pipeline.cpp 里写死的"自动 = min(2, total)"。本模块把它
//      变成"线程预算内、按编码器实际并发能力分配"的规划量 P。
//   F3 编码器线程事实（src/codecs/encoder.h 的线程映射义务 + 各实现）：
//        jxl  = JxlThreadParallelRunner(E)（E=1 → nullptr，调用线程编码）；
//        avif = 插件 `threads` 参数（SVT-AV1/libaom 内省确认有该参数）；
//        webp = WebPConfig::thread_level = (E>1)（只有 0/1 两档 → 实际最多 2 线程）；
//        heif = HEVC(x265) 插件参数表**无 `threads`** → E 被忽略、后端保留默认线程池
//               （既有债，不入账；本模块把它的实际并发记为 1，如实不冒进）；
//        jpegli / oiio(png/tiff/bmp) = 无内部线程，调用线程编码（实际 1）。
//   F4 CPU 负载 = GetSystemTimes / /proc/stat 的区间利用率 EMA；只用于多输出并行度的
//      "饱和收敛/平静恢复"，不改 E 基值（饱和也可能是"正确地用满"，盲目降 E 会掉吞吐）。
//   F5 线程预算硬约束（与既有 E3 契约同族）：W×P×E ≤ T，且 W×est_threads ≤ T。基值 E
//      来自冻结公式，故 P=1 时与旧行为逐字一致；P>1 时 E 按预算拆分，总线程不增加。
//
// 不变式（tests/unit/test_thread_plan.cpp 穷举）：
//   W == alloc_threads(T, unfinished, 0).files；1 ≤ E ≤ 16；
//   1 ≤ P ≤ min(outputs, 8)；W×P×E ≤ T；deep(W*2>T) ⇒ E==1 且 P==1。
#include "core/thread_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "core/scheduler.h" // pp::Alloc / pp::alloc_threads（冻结契约的唯一实现）

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace pp {

namespace {

// E 分给某个编码器时它**真正会用到的并发线程数**（F3 的能力表；est 口径）。
int effective_threads(const EncoderThreadProfile &profile, int encode_threads) {
    if (!profile.scales_with_encode_threads)
        return (profile.max_effective_threads > 1 && encode_threads > 1)
                   ? profile.max_effective_threads
                   : 1;
    return std::clamp(encode_threads, 1, std::max(1, profile.max_effective_threads));
}

// P 路并行编码的线程占用上界 = 最重的 P 路编码器实际并发之和（顺序无关的保守上界）。
int est_concurrent_threads(const std::vector<int> &effective, int parallel) {
    std::vector<int> sorted = effective;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    int sum = 0;
    for (int i = 0; i < parallel && i < static_cast<int>(sorted.size()); ++i)
        sum += sorted[i];
    return sum;
}

} // namespace

// ---------------------------------------------------------------------------
// 编码器线程能力表（F3；format_id/backend_id 用 format_tables.cpp 的事实表用词）
// ---------------------------------------------------------------------------
EncoderThreadProfile encoder_thread_profile(std::string_view format_id) {
    // 能力由格式决定（heif 只有 x265；avif 两后端都走插件 threads）——backend_id 不入参，
    // 避免两个同类型 string_view 相邻参数可互换（clang-tidy
    // bugprone-easily-swappable-parameters）。
    if (format_id == "jxl")
        return {16, true, "jxl"};
    if (format_id == "avif")
        return {16, true, "avif"};
    if (format_id == "webp")
        return {2, false, "webp"};
    if (format_id == "heif")
        return {1, false, "x265"}; // 插件无 threads 参数 → E 不生效（默认池为既有债）
    if (format_id == "jpeg")
        return {1, false, "jpegli"};
    if (format_id == "png" || format_id == "tiff" || format_id == "bmp")
        return {1, false, "oiio"};
    return {1, false, "unknown"};
}

// ---------------------------------------------------------------------------
// CPU 实时负载（F4）
// ---------------------------------------------------------------------------
#if defined(_WIN32)
namespace {
std::uint64_t filetime_to_u64(const FILETIME &ft) {
    return (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
} // namespace

bool read_cpu_times(CpuTimes &out) {
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user) == 0)
        return false;
    out.idle = filetime_to_u64(idle);
    // kernel 时间**包含** idle 时间 → total = kernel + user，idle 单列。
    out.total = filetime_to_u64(kernel) + filetime_to_u64(user);
    return out.total > 0;
}
#else
bool read_cpu_times(CpuTimes &out) {
    std::FILE *f = std::fopen("/proc/stat", "rb");
    if (f == nullptr)
        return false;
    char line[512] = {};
    const char *got = std::fgets(line, sizeof line, f);
    std::fclose(f);
    if (got == nullptr)
        return false;
    unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0,
                       steal = 0;
    const int n = std::sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice,
                              &sys, &idle, &iowait, &irq, &softirq, &steal);
    if (n < 4)
        return false;
    out.idle = idle + iowait;
    out.total = user + nice + sys + idle + iowait + irq + softirq + steal;
    return out.total > 0;
}
#endif

double cpu_load_between(const CpuTimes &prev, const CpuTimes &cur) {
    if (cur.total <= prev.total)
        return -1.0; // 计数未推进（采样过密/平台粒度）→ 未知
    const std::uint64_t d_total = cur.total - prev.total;
    const std::uint64_t d_idle = cur.idle >= prev.idle ? cur.idle - prev.idle : 0;
    if (d_idle >= d_total)
        return 0.0;
    const double load = 1.0 - static_cast<double>(d_idle) / static_cast<double>(d_total);
    return std::clamp(load, 0.0, 1.0);
}

void CpuLoadEma::push(double sample) {
    if (!(sample >= 0.0)) // 含 NaN
        return;
    const double x = std::clamp(sample, 0.0, 1.0);
    value = (value < 0.0) ? x : (kAlpha * x + (1.0 - kAlpha) * value);
}

// ---------------------------------------------------------------------------
// 规划器主函数
// ---------------------------------------------------------------------------
ThreadPlan plan_threads(const ThreadPlanInput &in, ThreadPlanState &state) {
    const int T = std::max(1, in.thread_budget);
    const int unfinished = std::max(0, in.unfinished);
    const int outputs = std::max(1, in.output_count);

    // —— ① 契约基值：W 恒等于冻结公式（并发闸宽度语义零改动，F1）——
    const Alloc base = alloc_threads(T, unfinished, 0);
    ThreadPlan plan;
    plan.files = base.files;
    plan.encode_threads = base.encode_threads;
    plan.parallel_outputs = 1;
    plan.action = "base";
    plan.reason = "frozen-contract";

    if (plan.files <= 0) {
        // 无未完成文件（不会入闸）：零占用估算；E 保持冻结函数的返回值（W=0 的退化点上
        // 冻结公式仍会放大 E，本函数与它逐字一致 —— "单输出 = 契约零改动"的判据因此可穷举）。
        plan.est_threads = 0;
        plan.reason = "no-unfinished";
        return plan;
    }

    // —— ② CPU 采样状态机（F4；仅记录连续采样数，供多输出并行度收敛/恢复用）——
    const bool cpu_known = std::isfinite(in.cpu_load) && in.cpu_load >= 0.0;
    if (cpu_known) {
        state.saturated_streak =
            (in.cpu_load >= kCpuSaturatedLoad) ? state.saturated_streak + 1 : 0;
        state.calm_streak = (in.cpu_load <= kCpuCalmLoad) ? state.calm_streak + 1 : 0;
    } else {
        state.saturated_streak = 0;
        state.calm_streak = 0;
    }

    // —— ③ 深队列：文件级并行优先（=冻结行为：E=1；多输出不并行，避免 W×P 超订）——
    if (base.files * 2 > T) {
        plan.est_threads = plan.files; // 每文件 1 条编码线程（调用线程或 runner(1)）
        state.last = plan;
        state.has_last = true;
        return plan;
    }

    // —— ④ 浅队列：多输出并行度 P 规划（编码器能力 + 线程预算）——
    const int budget_per_file = std::max(1, T / plan.files); // 本文件可用的编码线程预算
    // 能力表：空 → 保守 {1,false}（未知编码器按"平编码器"处理，只会更小不会超订）。
    const std::vector<EncoderThreadProfile> *profiles = in.profiles;
    const EncoderThreadProfile kConservativeProfile{1, false, "unknown"};
    const auto profile_at = [profiles,
                             &kConservativeProfile](int i) -> const EncoderThreadProfile & {
        if (profiles != nullptr && i >= 0 && i < static_cast<int>(profiles->size()))
            return (*profiles)[static_cast<std::size_t>(i)];
        return kConservativeProfile;
    };

    int p_cap = std::min(outputs, kMaxParallelOutputs);
    p_cap = std::min(p_cap, budget_per_file); // P 路至少各 1 线程
    if (in.user_parallel_cap > 0)
        p_cap = std::min(p_cap, in.user_parallel_cap);
    // CPU 未知（单文件运行的首个决策 / 平台读数失败 / 采样区间过短）→ 回退旧行为上界
    // min(2, outputs)（Lead 裁定：不确定时不比旧路径更大；E 仍按预算拆分，不超订）。
    if (!cpu_known)
        p_cap = std::min(p_cap, std::max(1, std::min(2, outputs)));

    int planned_p = 1;
    int planned_e = plan.encode_threads;
    for (int p = p_cap; p >= 2; --p) {
        const int e = std::clamp(budget_per_file / p, 1, kMaxEncodeThreads);
        if (e * p > budget_per_file)
            continue; // 防御（floor 除法下不可达）
        std::vector<int> eff(static_cast<std::size_t>(outputs));
        for (int i = 0; i < outputs; ++i)
            eff[static_cast<std::size_t>(i)] = effective_threads(profile_at(i), e);
        if (est_concurrent_threads(eff, p) <= budget_per_file) {
            planned_p = p;
            planned_e = e;
            break;
        }
    }
    if (planned_p > 1) {
        plan.parallel_outputs = planned_p;
        plan.encode_threads = planned_e;
        plan.action = "multi";
        plan.reason = "shallow-queue-budget";
    }

    // —— ⑤ CPU 状态：饱和收敛 / 恢复防抖（只作用于 P；E 基值不动）——
    // 判据用 EMA（α=0.35）而非瞬时采样：跨过 0.90 需要连续多次高负载，天然防抖；
    // 变化再受"最小间隔"约束（≥250ms 才允许收敛、≥500ms 且平静连续 3 次才恢复）。
    //
    // 关键区分（0.4.2 需求6 的"不能太激进"解释）：
    //   * 含**不受 E 约束的编码器线程池**（heif/x265 插件无 threads 参数；未知编码器按保守
    //     处理）时，P 路并行会叠加多个不可控线程池 → CPU 饱和时收敛 P=1（真过订风险）；
    //   * 纯受控编码器（jxl/avif 吃 E、webp 上限 2、jpegli/OIIO 单线程）：W×P×E ≤ T 已在
    //     预算内，CPU 饱和只说明"正确地用满"，不应把并行度打下去（只会掉吞吐）；此时
    //     "不再加"= 不超过上一版 P（不因饱和而新增并行度）。
    const long long since_change =
        state.has_last ? (in.now_ms - state.last_change_ms) : kPlanMinChangeMs;
    const bool cpu_hot = cpu_known && in.cpu_load >= kCpuSaturatedLoad;
    const bool can_change = !state.has_last || since_change >= kPlanMinChangeMs;
    bool uncontrolled_pool = false;
    for (int i = 0; i < outputs; ++i) {
        const EncoderThreadProfile &pf = profile_at(i);
        if (std::string_view(pf.family) == "x265" || std::string_view(pf.family) == "unknown") {
            uncontrolled_pool = true;
            break;
        }
    }
    if (cpu_hot) {
        if (planned_p > 1) {
            if (uncontrolled_pool) {
                if (state.has_last && state.last.parallel_outputs > 1 && !can_change) {
                    // 防抖：未到最小变更间隔 → 沿用上一版 P（不再新增），记为 hold。
                    plan.parallel_outputs = state.last.parallel_outputs;
                    plan.encode_threads = state.last.encode_threads;
                    plan.action = "hold";
                    plan.reason = "cpu-saturated-debounce";
                } else {
                    plan.parallel_outputs = 1;
                    plan.encode_threads = base.encode_threads;
                    plan.action = "converge";
                    plan.reason = "cpu-saturated";
                }
            } else {
                // 受控编码器：饱和不改变预算内的 P；"不再加" = 不超过上一版 P。
                int keep_p = planned_p;
                if (state.has_last && state.last.parallel_outputs >= 1 &&
                    planned_p > state.last.parallel_outputs) {
                    keep_p = state.last.parallel_outputs;
                    if (keep_p < 2) {
                        plan.parallel_outputs = 1;
                        plan.encode_threads = base.encode_threads;
                    } else {
                        plan.parallel_outputs = keep_p;
                        plan.encode_threads =
                            std::clamp(budget_per_file / keep_p, 1, kMaxEncodeThreads);
                    }
                    plan.action = "hold";
                    plan.reason = "cpu-saturated-bounded";
                } else {
                    plan.parallel_outputs = planned_p;
                    plan.encode_threads = planned_e;
                    plan.action = "multi";
                    plan.reason = "cpu-saturated-budget-bounded";
                }
            }
        } else {
            plan.action = "base";
            plan.reason = "cpu-saturated";
        }
    } else if (state.last_was_converge) {
        const bool recovered =
            state.calm_streak >= kCpuCalmStreak && since_change >= kPlanRecoverMs;
        if (!recovered) {
            // 恢复防抖：CPU 尚未平静足够久 → 保持串行（不再加），并记录为 hold。
            plan.parallel_outputs = 1;
            plan.encode_threads = base.encode_threads;
            plan.action = "hold";
            plan.reason = "cpu-saturated-recovering";
        }
    }

    // —— ⑥ 占用估算（est 口径：P 路编码器实际并发之和 × W；见 thread_plan.h 不变式）——
    std::vector<int> eff(static_cast<std::size_t>(outputs));
    for (int i = 0; i < outputs; ++i)
        eff[static_cast<std::size_t>(i)] = effective_threads(profile_at(i), plan.encode_threads);
    plan.est_threads = plan.files * est_concurrent_threads(eff, plan.parallel_outputs);
    // est 下界防御：至少每个活跃文件 1 条线程；上界防御：不超过线程预算。
    plan.est_threads = std::clamp(plan.est_threads, plan.files, T);

    // —— ⑦ 迟滞状态更新（P/动作变化才记时间戳；E 随 remaining 的自然漂移不算"变化"）——
    const bool p_changed = !state.has_last || state.last.parallel_outputs != plan.parallel_outputs;
    const bool action_changed =
        !state.has_last || std::string_view(state.last.action) != std::string_view(plan.action);
    if (p_changed || action_changed)
        state.last_change_ms = in.now_ms;
    // 恢复期状态：收敛后的 hold 仍属"恢复防抖中"，直到真正回到 multi/base 才解除
    // （否则 hold 一次后下一调用就会立刻恢复并行，恢复间隔形同虚设）。
    const std::string_view action_view(plan.action);
    state.last_was_converge =
        (action_view == "converge") || (state.last_was_converge && action_view == "hold");
    state.last = plan;
    state.has_last = true;
    return plan;
}

} // namespace pp
