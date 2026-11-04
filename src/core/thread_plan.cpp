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
//      来自冻结公式，故 P=1 时与旧行为逐字一致；P>1 时总线程不增加。
//   F6 **M15 公平性修复（0.4.2 JXL 严重退化根因）**：0.4.2 的 P>1 分配是"每路 E=预算/P"的
//      名义均分 —— 但 jpegli/OIIO 无内部线程、heif/x265 用不受控固定池，**只有** jxl/avif 真正
//      吃 E（thread_plan.h 能力表）。多输出同文件时 jxl 的 E 从 16 被压到 8/4/2，JXL 单轮编码
//      慢 ~1.5-3x；配合格式预设默认自适应（target=90，最多 30 轮重编码）放大成数量级退化
//      （用户报 600s+）。修复 = 逐输出分配：每路保底 1、heavy 分走余下预算（P=4 的
//      jxl+heif+jpeg+png → E=[13,1,1,1]，恢复 0.4.1 的 JXL 线程预算量级）；线程预算硬约束
//      由"任意 P 路并发子集的有效线程之和 ≤ 每文件预算"保证，W×P×E≤T 的旧不变式零放宽。
//      P==1 / 单输出路径逐输出向量为空 ⇒ 0.4.2 旧口径逐字不变（产物字节不变的结构性保证）。
//
// 不变式（tests/unit/test_thread_plan.cpp 穷举）：
//   W == alloc_threads(T, unfinished, 0).files；1 ≤ E ≤ 16；
//   1 ≤ P ≤ min(outputs, 8)；W×P×E ≤ T；deep(W*2>T) ⇒ E==1 且 P==1。
#include "core/thread_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

// 编码器是否"真正吃 E"（scales_with_encode_threads；见 thread_plan.h 能力表）。
bool is_heavy(const EncoderThreadProfile &profile) {
    return profile.scales_with_encode_threads && profile.max_effective_threads > 1;
}

// —— 诊断开关（**默认关闭**；仅同机 A/B 取证用，生产/GUI/CI 从不设置）——
// PP_THREAD_PLAN_LEGACY=1 / 非 "0" ⇒ 逐输出分配退化为 0.4.2 旧口径"每路 E=预算/P"的名义
// 均分（JXL 被压到 8/4/2 的旧行为）。用于"同一二进制、同一输入、清空输出目录"的修改前后
// A/B；关闭时行为 = M15 修复后的公平分配。单次读环境（static），不影响单测确定性（默认未设）。
bool legacy_planner_requested() {
    static const bool legacy = [] {
        const char *value = std::getenv("PP_THREAD_PLAN_LEGACY");
        return value != nullptr && *value != '\0' && std::string_view(value) != "0";
    }();
    return legacy;
}

// —— M15 公平性修复：逐输出线程分配 ——
// 背景（根因）：0.4.2 的 `e = floor(每文件预算 / P)` 对**所有** target 一视同仁 ⇒ 同文件多输出
// 时 jxl/avif 的内部线程从 16 被压到 8/4/2，而 flat 编码器（jpegli/OIIO 无内部线程、heif/x265
// 用固定池）根本吃不到这份预算 —— 预算被"名义均分"浪费，heavy 编码器数量级变慢。
//
// 分配规则（保守、与任意并发子集无关）：
//   * 每路保底 1（= 调用线程编码 / 单线程编码器）；
//   * 余下预算（budget - P）优先给 heavy 编码器；
//   * 同时在跑的最多 k = min(P, heavy 数) 路 heavy，其余 (P-k) 路占 1 ⇒ 均分后任何 P 路并发
//     子集的实际并发之和 ≤ budget（floor 缩水只会更小）；
//   * heavy 缺席 ⇒ 全部 1 线程（P 路并行本身就是线程并行，flat 编码器不需要更多）。
// 例（T/W=16，输出 jxl+heif+jpeg+png，P=4）：[13,1,1,1]（0.4.2 = [4,4,4,4]）。
std::vector<int> allocate_output_threads(int budget, int parallel,
                                         const std::vector<EncoderThreadProfile> &profiles) {
    std::vector<int> per(profiles.size(), 1);
    if (parallel <= 1 || profiles.empty())
        return per;
    // 诊断 A/B：旧口径 = 名义均分（所有 target 同一个 E；jxl 被压到 8/4/2 的 0.4.2 行为）。
    if (legacy_planner_requested())
        return std::vector<int>(profiles.size(),
                                std::clamp(budget / parallel, 1, kMaxEncodeThreads));
    int heavy_count = 0;
    for (const EncoderThreadProfile &profile : profiles)
        heavy_count += is_heavy(profile) ? 1 : 0;
    const int heavy_concurrent = std::min(parallel, heavy_count);
    if (heavy_concurrent <= 0)
        return per; // 全是 flat 编码器
    const int flat_slots = parallel - heavy_concurrent;
    const int per_heavy =
        std::clamp((budget - flat_slots) / heavy_concurrent, 1, kMaxEncodeThreads);
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        const EncoderThreadProfile &profile = profiles[i];
        if (is_heavy(profile))
            per[i] = std::min(per_heavy, std::max(1, profile.max_effective_threads));
    }
    return per;
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

    // —— ④ 浅队列：多输出并行度 P 规划（编码器能力 + 线程预算 + 逐输出公平分配）——
    const int budget_per_file = std::max(1, T / plan.files); // 本文件可用的编码线程预算
    // 能力表：空/缺项 → 保守 {1,false}（未知编码器按"平编码器"处理，只会更小不会超订）。
    const EncoderThreadProfile kConservativeProfile{1, false, "unknown"};
    std::vector<EncoderThreadProfile> profiles(static_cast<std::size_t>(outputs),
                                               kConservativeProfile);
    if (in.profiles != nullptr) {
        const std::size_t n = std::min(in.profiles->size(), profiles.size());
        for (std::size_t i = 0; i < n; ++i)
            profiles[i] = (*in.profiles)[i];
    }

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
    std::vector<int> planned_per_output;
    for (int p = p_cap; p >= 2; --p) {
        // M15：逐输出分配（heavy 拿余下预算），est 用"最重 P 路实际并发之和"判预算。
        std::vector<int> per_output = allocate_output_threads(budget_per_file, p, profiles);
        std::vector<int> eff(static_cast<std::size_t>(outputs));
        for (int i = 0; i < outputs; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i);
            eff[idx] = effective_threads(profiles[idx], per_output[idx]);
        }
        if (est_concurrent_threads(eff, p) <= budget_per_file) {
            planned_p = p;
            // 均分参考值（旧口径的 E；单输出/串行路径仍由冻结公式给出）。
            planned_e = std::clamp(budget_per_file / p, 1, kMaxEncodeThreads);
            planned_per_output = std::move(per_output);
            break;
        }
    }
    if (planned_p > 1) {
        plan.parallel_outputs = planned_p;
        plan.encode_threads = planned_e;
        plan.encode_threads_per_output = planned_per_output;
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
        const EncoderThreadProfile &pf = profiles[static_cast<std::size_t>(i)];
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
                    plan.encode_threads_per_output = state.last.encode_threads_per_output;
                    plan.action = "hold";
                    plan.reason = "cpu-saturated-debounce";
                } else {
                    plan.parallel_outputs = 1;
                    plan.encode_threads = base.encode_threads;
                    plan.encode_threads_per_output.clear();
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
                        plan.encode_threads_per_output.clear();
                    } else {
                        plan.parallel_outputs = keep_p;
                        plan.encode_threads =
                            std::clamp(budget_per_file / keep_p, 1, kMaxEncodeThreads);
                        plan.encode_threads_per_output =
                            allocate_output_threads(budget_per_file, keep_p, profiles);
                    }
                    plan.action = "hold";
                    plan.reason = "cpu-saturated-bounded";
                } else {
                    plan.parallel_outputs = planned_p;
                    plan.encode_threads = planned_e;
                    plan.encode_threads_per_output = planned_per_output;
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
            plan.encode_threads_per_output.clear();
            plan.action = "hold";
            plan.reason = "cpu-saturated-recovering";
        }
    }

    // —— ⑥ 占用估算（est 口径：P 路编码器实际并发之和 × W；见 thread_plan.h 不变式）——
    // M15：P>1 用逐输出分配值（encode_threads_per_output），否则用均分参考值 encode_threads
    // （P==1 时二者等价于旧口径 → 单输出/串行路径的 est 与 0.4.2 逐字一致）。
    const bool per_output_ok = plan.parallel_outputs > 1 && plan.encode_threads_per_output.size() ==
                                                                static_cast<std::size_t>(outputs);
    if (plan.parallel_outputs > 1 && !per_output_ok)
        plan.encode_threads_per_output.clear(); // 防御：不自洽 → 回退均分参考值
    std::vector<int> eff(static_cast<std::size_t>(outputs));
    for (int i = 0; i < outputs; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i);
        const int e = per_output_ok ? plan.encode_threads_per_output[idx] : plan.encode_threads;
        eff[idx] = effective_threads(profiles[idx], e);
    }
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
