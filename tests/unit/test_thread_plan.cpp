// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline 0.4.2 需求 6 —— 自适应线程规划（src/core/thread_plan.{h,cpp}）单测。
//
// 覆盖（与 thread_plan.h 的"不变式"节逐条对应）：
//   A. 编码器线程能力表（事实单源：jxl/avif 可扩展、webp 上限 2、heif/x265 忽略 E、
//      jpegli/OIIO 无内部线程、未知格式保守）；
//   B. CPU 区间利用率纯函数（正常/零增量/空闲上限/计数回退防御）；
//   C. CPU 利用率 EMA（首值直接取、后续 α 混合、非法样本忽略）；
//   D. 平台 CPU 读数桥（当前平台可读、总量单调、区间利用率域）；
//   E. 规划器不变式穷举矩阵（T × 未完成数 × 输出数 × CPU 负载 × 编码器组合 × 用户上限）；
//   F. 设计表值（深/浅队列、多输出并行、预算拆分、旧行为回退）；
//   G. 迟滞/防抖：CPU 饱和收敛（最小间隔/沿用上一版）→ 平静恢复（连续采样 + 恢复间隔）；
//   H. 契约兼容：单输出时规划器的 W/E 与冻结 alloc_threads 逐字一致。
//
// 失败打印 "FAIL <case>: <detail>"；main() 返回失败数（与既有单测风格一致）。
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/scheduler.h" // pp::Alloc / pp::alloc_threads（冻结契约）
#include "core/thread_plan.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::vector<pp::EncoderThreadProfile> profiles_for(const std::vector<std::string> &formats) {
    std::vector<pp::EncoderThreadProfile> out;
    out.reserve(formats.size());
    for (const std::string &f : formats)
        out.push_back(pp::encoder_thread_profile(f));
    return out;
}

std::string tag(int T, int unfinished, int outputs, double cpu, int cap) {
    return "T=" + std::to_string(T) + " unf=" + std::to_string(unfinished) +
           " out=" + std::to_string(outputs) + " cpu=" + std::to_string(cpu) +
           " cap=" + std::to_string(cap);
}

// 测试辅助构造器：位置参数按 (T, unfinished, outputs, cpu, cap, profiles) 文档化，
// 调用点均为一行字面量自解释。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) —— 新文件须 0 告警故结构性豁免。
pp::ThreadPlanInput make_input(int T, int unfinished, int outputs, double cpu, int cap,
                               const std::vector<pp::EncoderThreadProfile> *profiles,
                               long long now_ms = 0) {
    pp::ThreadPlanInput in;
    in.thread_budget = T;
    in.unfinished = unfinished;
    in.output_count = outputs;
    in.user_parallel_cap = cap;
    in.cpu_load = cpu;
    in.now_ms = now_ms;
    in.profiles = profiles;
    return in;
}

} // namespace

// 测试入口：异常即测试失败（与既有测试 main 同模式；新文件须 0 告警故结构性豁免）。
// NOLINTNEXTLINE(bugprone-exception-escape) —— M13 先例：入口结构性 NOLINT 带理由。
int main() {
    // ---- A. 编码器线程能力表 ----
    {
        struct Row {
            const char *format, *backend, *family;
            int max_threads;
            bool scales;
        };
        const Row rows[] = {
            {"jxl", "libjxl", "jxl", 16, true},       {"avif", "svt-av1", "avif", 16, true},
            {"avif", "libaom", "avif", 16, true},     {"webp", "libwebp", "webp", 2, false},
            {"heif", "x265", "x265", 1, false}, // 插件无 threads 参数（E 被忽略）
            {"jpeg", "jpegli", "jpegli", 1, false},   {"png", "oiio", "oiio", 1, false},
            {"tiff", "oiio", "oiio", 1, false},       {"bmp", "oiio", "oiio", 1, false},
            {"gif", "whatever", "unknown", 1, false}, // 未知 → 保守
        };
        for (const Row &r : rows) {
            const pp::EncoderThreadProfile p = pp::encoder_thread_profile(r.format);
            const std::string t = std::string("profile/") + r.format + ":" + r.backend;
            check(p.max_effective_threads == r.max_threads, t + "/max",
                  "max_effective_threads=" + std::to_string(p.max_effective_threads) + " 期望 " +
                      std::to_string(r.max_threads));
            check(p.scales_with_encode_threads == r.scales, t + "/scales",
                  std::string("scales=") + (p.scales_with_encode_threads ? "true" : "false"));
            check(std::string(p.family) == r.family, t + "/family",
                  std::string("family=") + p.family + " 期望 " + r.family);
        }
    }

    // ---- B. CPU 区间利用率 ----
    {
        pp::CpuTimes a{100, 1000};
        pp::CpuTimes b{200, 2000};
        check(std::fabs(pp::cpu_load_between(a, b) - 0.9) < 1e-9, "cpu/normal",
              "期望 0.9，得 " + std::to_string(pp::cpu_load_between(a, b)));
        check(pp::cpu_load_between(a, a) < 0.0, "cpu/no-progress", "总量未增长应返回 -1（未知）");
        // 全空闲：Δidle == Δtotal → 0
        pp::CpuTimes c{0, 100};
        pp::CpuTimes d{100, 200};
        check(std::fabs(pp::cpu_load_between(c, d)) < 1e-12, "cpu/all-idle",
              "全空闲应为 0，得 " + std::to_string(pp::cpu_load_between(c, d)));
        // idle 计数回退（防御）：Δidle 取 0 → 满载 1.0（不会出现负值/NaN）
        pp::CpuTimes e{500, 1000};
        pp::CpuTimes f{100, 1100};
        const double lf = pp::cpu_load_between(e, f);
        check(lf >= 0.0 && lf <= 1.0, "cpu/idle-regression-clamped",
              "越界值 " + std::to_string(lf));
    }

    // ---- C. CPU 利用率 EMA ----
    {
        pp::CpuLoadEma ema;
        check(!ema.known() && ema.value < 0.0, "ema/initial", "初始应为未知");
        ema.push(0.5);
        check(std::fabs(ema.value - 0.5) < 1e-9, "ema/first", "首值应直接取");
        ema.push(0.9);
        const double want = pp::CpuLoadEma::kAlpha * 0.9 + (1.0 - pp::CpuLoadEma::kAlpha) * 0.5;
        check(std::fabs(ema.value - want) < 1e-9, "ema/mix", "EMA 混合值不符");
        const double before = ema.value;
        ema.push(-1.0);
        ema.push(std::numeric_limits<double>::quiet_NaN());
        check(std::fabs(ema.value - before) < 1e-12, "ema/ignore-invalid", "非法样本不得改变 EMA");
        ema.push(2.0); // 越界样本夹到 1.0
        check(ema.value >= before && ema.value <= 1.0, "ema/clamp", "越界样本未夹取");
    }

    // ---- D. 平台 CPU 读数桥（当前平台必须可读；总量单调；区间利用率在域内）----
    {
        pp::CpuTimes t0;
        check(pp::read_cpu_times(t0) && t0.total > 0, "cpu/read", "平台 CPU 读数失败");
        pp::CpuTimes t1;
        bool ok = pp::read_cpu_times(t1);
        check(ok && t1.total >= t0.total, "cpu/monotonic",
              "两次读数总量倒退（" + std::to_string(t0.total) + " -> " + std::to_string(t1.total) +
                  "）");
        const double load = pp::cpu_load_between(t0, t1);
        check(load >= -1.0 && load <= 1.0, "cpu/load-domain",
              "区间利用率越界 " + std::to_string(load));
    }

    // ---- E. 规划器不变式穷举矩阵 ----
    {
        const std::vector<std::vector<std::string>> format_sets = {
            {"jxl"},
            {"heif"},
            {"webp"},
            {"jpeg"},
            {"jxl", "heif", "avif", "webp", "png", "tiff", "bmp", "jpeg"},
        };
        const int t_vals[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
        const int unf_vals[] = {0, 1, 2, 3, 4, 5, 8, 12, 16, 24, 40};
        const int out_vals[] = {1, 2, 3, 4, 6, 8};
        const int cap_vals[] = {0, 2};
        const double cpu_vals[] = {-1.0, 0.2, 0.5, 0.95};
        long long cases = 0, viol_w = 0, viol_domain = 0, viol_budget = 0, viol_deep = 0,
                  viol_est = 0, viol_unknown = 0, viol_hot = 0;
        std::string ex_w, ex_domain, ex_budget, ex_deep, ex_est, ex_unknown, ex_hot;
        for (int T : t_vals) {
            for (int unf : unf_vals) {
                for (int outs : out_vals) {
                    for (int cap : cap_vals) {
                        for (double cpu : cpu_vals) {
                            for (const auto &set : format_sets) {
                                ++cases;
                                std::vector<pp::EncoderThreadProfile> profs = profiles_for(set);
                                bool uncontrolled = false;
                                for (const auto &p : profs)
                                    uncontrolled = uncontrolled ||
                                                   std::string(p.family) == "x265" ||
                                                   std::string(p.family) == "unknown";
                                pp::ThreadPlanState state; // 每组独立状态（确定性）
                                const pp::ThreadPlanInput in =
                                    make_input(T, unf, outs, cpu, cap, &profs, 1000);
                                const pp::ThreadPlan plan = pp::plan_threads(in, state);
                                const pp::Alloc base = pp::alloc_threads(T, std::max(0, unf), 0);
                                const std::string tg =
                                    tag(T, unf, outs, cpu, cap) + " set=" + set[0];
                                if (plan.files != base.files) {
                                    ++viol_w;
                                    if (ex_w.empty())
                                        ex_w = tg + " W=" + std::to_string(plan.files) +
                                               " base=" + std::to_string(base.files);
                                }
                                if (plan.files <= 0) {
                                    if (plan.est_threads != 0 || plan.encode_threads < 1) {
                                        ++viol_est;
                                        if (ex_est.empty())
                                            ex_est = tg;
                                    }
                                    continue;
                                }
                                const bool domain_ok =
                                    plan.encode_threads >= 1 &&
                                    plan.encode_threads <= pp::kMaxEncodeThreads &&
                                    plan.parallel_outputs >= 1 &&
                                    plan.parallel_outputs <=
                                        std::min(outs, pp::kMaxParallelOutputs) &&
                                    plan.files <= plan.est_threads && plan.est_threads <= T;
                                if (!domain_ok) {
                                    ++viol_domain;
                                    if (ex_domain.empty())
                                        ex_domain =
                                            tg + " {E=" + std::to_string(plan.encode_threads) +
                                            ",P=" + std::to_string(plan.parallel_outputs) +
                                            ",est=" + std::to_string(plan.est_threads) + "}";
                                }
                                if (static_cast<long long>(plan.files) * plan.encode_threads *
                                        plan.parallel_outputs >
                                    T) {
                                    ++viol_budget;
                                    if (ex_budget.empty())
                                        ex_budget = tg;
                                }
                                const bool deep = base.files * 2 > T;
                                if (deep &&
                                    (plan.encode_threads != 1 || plan.parallel_outputs != 1)) {
                                    ++viol_deep;
                                    if (ex_deep.empty())
                                        ex_deep = tg;
                                }
                                if (!deep && plan.parallel_outputs > 1 &&
                                    plan.encode_threads * plan.parallel_outputs >
                                        std::max(1, T / plan.files)) {
                                    ++viol_budget;
                                    if (ex_budget.empty())
                                        ex_budget = tg + "（P>1 但 E*P 超每文件预算）";
                                }
                                if (cpu < 0.0 && plan.parallel_outputs > std::min(2, outs)) {
                                    ++viol_unknown;
                                    if (ex_unknown.empty())
                                        ex_unknown =
                                            tg + " P=" + std::to_string(plan.parallel_outputs);
                                }
                                if (cpu >= 0.9 && uncontrolled && plan.parallel_outputs > 1) {
                                    ++viol_hot;
                                    if (ex_hot.empty())
                                        ex_hot = tg;
                                }
                            }
                        }
                    }
                }
            }
        }
        check(viol_w == 0, "matrix/W==base",
              std::to_string(viol_w) + " 例 W 偏离冻结公式: " + ex_w);
        check(viol_domain == 0, "matrix/domain",
              std::to_string(viol_domain) + " 例 E/P/est 越界: " + ex_domain);
        check(viol_budget == 0, "matrix/budget",
              std::to_string(viol_budget) + " 例 W×P×E>T 或 E×P 超预算: " + ex_budget);
        check(viol_deep == 0, "matrix/deep-queue",
              std::to_string(viol_deep) + " 例深队列 E/P 不为 1: " + ex_deep);
        check(viol_est == 0, "matrix/est", std::to_string(viol_est) + " 例 est 异常");
        check(viol_unknown == 0, "matrix/cpu-unknown-legacy-bound",
              std::to_string(viol_unknown) + " 例 CPU 未知时 P 超旧行为上界: " + ex_unknown);
        check(viol_hot == 0, "matrix/cpu-hot-converge",
              std::to_string(viol_hot) + " 例 CPU 饱和仍启用 P>1: " + ex_hot);
        std::printf("INFO matrix cases=%lld\n", cases);
    }

    // ---- F. 设计表值（深/浅队列、多输出并行、预算拆分、旧行为回退）----
    {
        struct Row {
            int T, unf, outs, cap;
            double cpu;
            std::vector<std::string> formats;
            int want_W, want_E, want_P, want_est;
            const char *want_action;
        };
        const std::vector<Row> rows = {
            // 单输出：浅队列 = 冻结基值（1 图 1 格式 jxl：E=16）
            {16, 1, 1, 0, -1.0, {"jxl"}, 1, 16, 1, 16, "base"},
            // 单输出：深队列 = 文件级并行、E=1
            {16, 100, 1, 0, 0.5, {"jxl"}, 16, 1, 1, 16, "base"},
            // 1 图 4 格式，CPU 未知 → P 回退旧上界 2，但 E 按预算拆分（旧路径 2×16=32 超订）
            {16, 1, 4, 0, -1.0, {"jxl", "jxl", "jxl", "jxl"}, 1, 8, 2, 16, "multi"},
            // 1 图 4 格式，CPU 平静（已知）→ 4 路并行、每路 4 线程（W×P×E = 16 = T）
            {16, 1, 4, 0, 0.4, {"jxl", "jxl", "jxl", "jxl"}, 1, 4, 4, 16, "multi"},
            // 1 图 4 个"平编码器"（png/tiff/bmp/jpeg）：每路 1 线程，est 只占 4
            {16, 1, 4, 0, 0.4, {"png", "tiff", "bmp", "jpeg"}, 1, 4, 4, 4, "multi"},
            // 1 图 4 个 heif（x265 忽略 E）：名义预算 16，实际并发按 4 计
            {16, 1, 4, 0, 0.4, {"heif", "heif", "heif", "heif"}, 1, 4, 4, 4, "multi"},
            // 2 图 2 格式：W=2 → 每文件预算 8 → P=2/E=4（W×P×E=16）
            {16, 2, 2, 0, 0.4, {"jxl", "jxl"}, 2, 4, 2, 16, "multi"},
            // 用户显式上限 = 1 → 强制串行（旧口径：max_parallel_targets>0 语义保留）
            {16, 1, 4, 1, 0.4, {"jxl", "jxl", "jxl", "jxl"}, 1, 16, 1, 16, "base"},
            // 深队列多输出：P 必须为 1（否则 W×P 超订）
            {16, 100, 4, 0, 0.4, {"jxl", "jxl", "jxl", "jxl"}, 16, 1, 1, 16, "base"},
            // 单线程预算：全 1
            {1, 1, 4, 0, -1.0, {"jxl", "jxl", "jxl", "jxl"}, 1, 1, 1, 1, "base"},
            // 大预算单文件 4 路：每路预算 8（W×P×E=32）→ est=32
            {32, 1, 4, 0, 0.4, {"jxl", "jxl", "jxl", "jxl"}, 1, 8, 4, 32, "multi"},
            // CPU 饱和 + 纯受控编码器（jxl）：饱和不缩小预算内并行（"不再加"=不超过上一版）
            {16, 1, 4, 0, 0.95, {"jxl", "jxl", "jxl", "jxl"}, 1, 4, 4, 16, "multi"},
            // CPU 饱和 + 含 x265（heif）不受控线程池：收敛 P=1（E 回契约基值；est 按实际并发 1 计）
            {16, 1, 4, 0, 0.95, {"heif", "heif", "heif", "heif"}, 1, 16, 1, 1, "converge"},
        };
        for (const Row &r : rows) {
            std::vector<pp::EncoderThreadProfile> profs = profiles_for(r.formats);
            pp::ThreadPlanState state;
            const pp::ThreadPlanInput in =
                make_input(r.T, r.unf, r.outs, r.cpu, r.cap, &profs, 1000);
            const pp::ThreadPlan plan = pp::plan_threads(in, state);
            const std::string tg = tag(r.T, r.unf, r.outs, r.cpu, r.cap);
            check(plan.files == r.want_W, "table/W " + tg,
                  "{W=" + std::to_string(plan.files) + "} 期望 " + std::to_string(r.want_W));
            check(plan.encode_threads == r.want_E, "table/E " + tg,
                  "{E=" + std::to_string(plan.encode_threads) + "} 期望 " +
                      std::to_string(r.want_E));
            check(plan.parallel_outputs == r.want_P, "table/P " + tg,
                  "{P=" + std::to_string(plan.parallel_outputs) + "} 期望 " +
                      std::to_string(r.want_P));
            check(plan.est_threads == r.want_est, "table/est " + tg,
                  "{est=" + std::to_string(plan.est_threads) + "} 期望 " +
                      std::to_string(r.want_est));
            check(std::string(plan.action) == r.want_action, "table/action " + tg,
                  std::string("action=") + plan.action + " 期望 " + r.want_action);
        }
    }

    // ---- G. 迟滞/防抖：CPU 饱和收敛 → 平静恢复（用含 x265 的 heif 输出触发"不受控池"路径）----
    {
        std::vector<pp::EncoderThreadProfile> profs =
            profiles_for({"heif", "heif", "heif", "heif"});
        pp::ThreadPlanState state;
        const int T = 16, unf = 1, outs = 4;
        // 平静 → 4 路并行
        pp::ThreadPlan p1 = pp::plan_threads(make_input(T, unf, outs, 0.4, 0, &profs, 1000), state);
        check(p1.parallel_outputs == 4 && std::string(p1.action) == "multi", "hyst/calm-multi",
              "平静时应 4 路并行，得 P=" + std::to_string(p1.parallel_outputs) +
                  " action=" + p1.action);
        // 饱和但距上次变更 <250ms → 沿用上一版（hold），不新增
        pp::ThreadPlan p2 =
            pp::plan_threads(make_input(T, unf, outs, 0.95, 0, &profs, 1100), state);
        check(p2.parallel_outputs == 4 && std::string(p2.action) == "hold", "hyst/hot-debounce",
              "P=" + std::to_string(p2.parallel_outputs) + " action=" + p2.action);
        // 到最小间隔（hold 更新了变更时刻，故推进到 1400）→ 收敛 P=1（E 回契约基值 16）
        pp::ThreadPlan p3 =
            pp::plan_threads(make_input(T, unf, outs, 0.95, 0, &profs, 1400), state);
        check(p3.parallel_outputs == 1 && p3.encode_threads == 16 &&
                  std::string(p3.action) == "converge",
              "hyst/hot-converge",
              "P=" + std::to_string(p3.parallel_outputs) +
                  " E=" + std::to_string(p3.encode_threads) + " action=" + p3.action);
        // 平静恢复：连续 3 次平静 + ≥500ms 才恢复
        pp::ThreadPlan p4 = pp::plan_threads(make_input(T, unf, outs, 0.4, 0, &profs, 1500), state);
        check(p4.parallel_outputs == 1 && std::string(p4.action) == "hold", "hyst/recover-hold1",
              "P=" + std::to_string(p4.parallel_outputs) + " action=" + p4.action);
        pp::ThreadPlan p5 = pp::plan_threads(make_input(T, unf, outs, 0.4, 0, &profs, 1700), state);
        pp::ThreadPlan p6 = pp::plan_threads(make_input(T, unf, outs, 0.4, 0, &profs, 1800), state);
        check(p5.parallel_outputs == 1 && p6.parallel_outputs == 1, "hyst/recover-wait",
              "平静采样数不足时不得恢复并行");
        pp::ThreadPlan p7 = pp::plan_threads(make_input(T, unf, outs, 0.4, 0, &profs, 2100), state);
        check(p7.parallel_outputs == 4 && std::string(p7.action) == "multi", "hyst/recovered",
              "恢复条件满足后应回到 4 路，得 P=" + std::to_string(p7.parallel_outputs) +
                  " action=" + p7.action);
    }

    // ---- H. 契约兼容：单输出时 W/E 与冻结 alloc_threads 逐字一致 ----
    {
        long long cases = 0, mismatches = 0;
        std::string ex;
        for (int T = 1; T <= 32; ++T) {
            for (int unf = 0; unf <= 40; ++unf) {
                for (double cpu : {-1.0, 0.2, 0.5, 0.7, 0.95}) {
                    std::vector<pp::EncoderThreadProfile> profs = profiles_for({"jxl"});
                    pp::ThreadPlanState state;
                    const pp::ThreadPlan plan =
                        pp::plan_threads(make_input(T, unf, 1, cpu, 0, &profs, 1000), state);
                    const pp::Alloc base = pp::alloc_threads(T, unf, 0);
                    ++cases;
                    if (plan.files != base.files || plan.encode_threads != base.encode_threads ||
                        plan.parallel_outputs != 1) {
                        ++mismatches;
                        if (ex.empty())
                            ex = tag(T, unf, 1, cpu, 0) + " {W=" + std::to_string(plan.files) +
                                 ",E=" + std::to_string(plan.encode_threads) +
                                 "} base {W=" + std::to_string(base.files) +
                                 ",E=" + std::to_string(base.encode_threads) + "}";
                    }
                }
            }
        }
        check(mismatches == 0, "contract/single-output",
              std::to_string(mismatches) + " 例与冻结 alloc_threads 不一致: " + ex);
        std::printf("INFO contract cases=%lld\n", cases);
    }

    std::printf("thread_plan tests: fail=%d\n", g_failed);
    return g_failed;
}
