// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 0.4.2 需求 6：自适应线程规划（调度器并行优化的纯函数层）。
//
// 背景（docs/v0.3.0-design.md §8.2 的落地边界）：
//   * 冻结契约 `alloc_threads(int T, int remaining, int inflight)` 只按"剩余文件数"决定
//     W（并发文件数）与 E（每文件编码线程数）：文件多 → W 大 / E=1；队列浅 → 收缩 W、
//     放大 E。它**不看编码器类型，也不看 CPU 实时负载**。
//   * 同文件多输出并行（pipeline.cpp `stage_encode_loop`）此前把并行度固定为"自动 =
//     min(2, outputs)"，与线程预算/CPU 解耦 ⇒ 文件数多时 2 路 × E 线程会超订，文件少时
//     只用 2 路填不满。
//
// 本模块的职责 = 在**不改冻结公式、不改并发闸宽度语义**的前提下，为每个"入闸文件"产出：
//   * `files`           = 并发闸宽度（恒等于 alloc_threads 的 files，契约零改动）；
//   * `encode_threads`  = 每个编码任务分到的编码器内部线程数 E；
//   * `parallel_outputs`= 同文件内同时编码的 target 数 P（0/1 = 串行 = 旧行为）；
//   * `est_threads`     = 本次分配的线程占用估算（W × Σ[P 个最重编码器的实际并发] 的上界）。
//
// 规则（保守、防抖、可单测；完整说明见 thread_plan.cpp 头部与 0.4.2 报告）：
//   ① 文件多（深队列 W*2 > T）→ E=1、P=1（文件级并行优先，= 0.2/冻结行为）；
//   ② 队列浅（W*2 ≤ T）→ E 基值照冻结公式；多输出时 P 从"输出数/用户上限"向下试，
//      **逐输出**分配线程（M15 公平性修复）：每路保底 1；重编码器（jxl/avif）分走余下
//      预算（任何 P 路并发子集的实际并发之和 ≤ T/W）；encode_threads 保留均分参考值
//      floor((T/W)/P)，故 W×P×E ≤ T 与 W×est_threads ≤ T 同时成立；
//   ③ 编码器能力表：jxl/libjxl、avif(svt-av1/libaom) 会真正吃 E 个线程；webp 只有
//      thread_level 0/1（实际上限 2）；heif/x265 的插件参数表无 `threads`（E 被忽略，
//      x265 保留自己的默认池）；jpegli / OIIO(png/tiff/bmp) 无内部线程（实际 1）。
//      ⇒ 同样预算下，"平编码器"（每路 1 线程）可以并行更多路，重度编码器按 E 拆分。
//   ④ CPU 实时负载（0..1 EMA，Windows GetSystemTimes / Linux /proc/stat）：
//      * 多输出并行度 P 只在 **CPU 未饱和**时启用新的并行度；EMA ≥0.90（跨过该值天然需要
//        连续多次高负载采样，已含防抖）时分两类处理：
//        - 含**不受 E 约束的编码器线程池**（heif/x265；未知编码器按保守处理）→ 收敛 P=1
//          （P 路并行会叠加多个不可控线程池 = 真过订风险）；上一版 P>1 且距上次变更
//          <250ms → 先 "hold" 沿用上一版，到点再收敛；
//        - 纯受控编码器（jxl/avif 吃 E、webp 上限 2、jpegli/OIIO 单线程）→ 预算内
//          W×P×E ≤ T 不因饱和而缩小（饱和可能正是"正确地用满"），"不再加" = 不超过上一版 P；
//      * CPU 饱和**不改 E 基值**：E 基值由契约公式给出，盲目降 E 会把吞吐打下去
//        （保守优先，如实写入报告）；
//      * 恢复：CPU 平静（≤0.60）连续 ≥3 次采样且距上次变更 ≥500ms 才允许 P 回升；
//      * CPU 未知（单文件运行的首个决策 / 平台读数失败）→ P 回退旧行为上界 min(2, outputs)，
//        不冒进（E 仍按预算拆分，故不会重现旧路径的 P×E 超订）；
//   ⑤ 迟滞：CPU 驱动的变化受"最小变更间隔 + 连续采样数"双重约束，避免逐文件震荡；
//      契约基值的自然漂移（remaining 减少导致 E 增大）不受迟滞影响（与旧行为一致）。
//
// 线程安全：本模块无全局状态（CpuLoadEma / ThreadPlanState 由调用方持有），纯函数可单测；
// 平台 CPU 读数只读 OS 计数，无锁。
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace pp {

// ---------------------------------------------------------------------------
// 编码器内部线程能力（需求 6「结合编码器特性」的事实单源）
// ---------------------------------------------------------------------------
// `max_effective_threads` = 编码器**真正能用到的并发线程上限**（不是调度器预留值）：
//   * jxl/libjxl      → 16（JxlThreadParallelRunner；E=1 时 nullptr = 调用线程编码）；
//   * avif/svt-av1、avif/libaom → 16（插件 `threads` 参数，内省核实）；
//   * webp/libwebp    → 2（WebPConfig::thread_level 只有 0/1 两档）；
//   * heif/x265       → 1（HEVC 插件参数表无 `threads`，E 被忽略、默认池不受约束；
//                        该池属既有债，不入账）；
//   * jpeg/jpegli、png|tiff|bmp/oiio → 1（无内部线程，调用线程编码）。
// `scales_with_encode_threads` = E 增大时实际并发是否随之增大（webp 只有 0/1 两档 → false，
// 但 E>1 时仍会多出 1 条线程，故 max_effective_threads=2）。
struct EncoderThreadProfile {
    int max_effective_threads = 1;
    bool scales_with_encode_threads = false;
    const char *family =
        "unknown"; // 稳定短词（日志/单测）：jxl/avif/svt-av1/libaom/webp/x265/jpegli/oiio/unknown
};

// format_id（src/core/format_tables.cpp 的事实表用词）→ 线程能力。
// 未知格式 → 保守 {1,false,"unknown"}（"假设平编码器"只会让并行度更小，不会超订）。
// M14 收口：不再收 backend_id（能力由格式决定；两个同类型 string_view 相邻参数可互换 =
// clang-tidy bugprone-easily-swappable-parameters，直接去掉未用参数）。
EncoderThreadProfile encoder_thread_profile(std::string_view format_id);

// ---------------------------------------------------------------------------
// CPU 实时负载（0..1）—— 纯函数 + 平台读数
// ---------------------------------------------------------------------------
// 读数口径：`idle` = 空闲 tick，`total` = 总 tick（含 idle）。
//   * Windows：GetSystemTimes，idle = idle，total = kernel + user（kernel 含 idle）；
//   * Linux：/proc/stat 首行 `cpu`，idle = idle + iowait，total = 各字段之和。
struct CpuTimes {
    std::uint64_t idle = 0;
    std::uint64_t total = 0;
};

// 平台 CPU 累计计数（只读 OS，无锁）。false = 平台不支持/读数失败（调用方视为"未知"）。
bool read_cpu_times(CpuTimes &out);

// 两次计数之间的利用率 0..1；无法计算（总量未增长）→ -1。
double cpu_load_between(const CpuTimes &prev, const CpuTimes &cur);

// 指数滑动平均（α = 0.35 ≈ 最近 ~3 次采样主导）。采样周期由调用方决定
// （本工程 = 每次"入闸决策"一次；单文件长编码期间不采样，如实写入报告）。
struct CpuLoadEma {
    static constexpr double kAlpha = 0.35;
    double value = -1.0;      // <0 = 未知（尚无两次读数）
    void push(double sample); // sample < 0 / NaN 忽略
    bool known() const { return value >= 0.0; }
    void reset() { value = -1.0; }
};

// ---------------------------------------------------------------------------
// 规划器（纯函数；输入 → 输出 + 迟滞状态）
// ---------------------------------------------------------------------------
// 阈值/常量（单测钉住；改判据须登记追认）。
inline constexpr int kMaxEncodeThreads = 16;       // E 上限（与冻结公式 §8.2 一致）
inline constexpr int kMaxParallelOutputs = 8;      // P 上限（防"输出很多"时逐文件线程爆炸）
inline constexpr double kCpuSaturatedLoad = 0.90;  // "饱和"判据
inline constexpr double kCpuCalmLoad = 0.60;       // "平静"判据（恢复条件）
inline constexpr int kCpuSaturatedStreak = 2;      // 连续饱和采样数（防抖）
inline constexpr int kCpuCalmStreak = 3;           // 连续平静采样数（恢复防抖）
inline constexpr long long kPlanMinChangeMs = 250; // CPU 驱动变化的最小间隔
inline constexpr long long kPlanRecoverMs = 500;   // 恢复的最小间隔

// 规划输入（每文件入闸时一组；全部为只读量）。
struct ThreadPlanInput {
    int thread_budget = 1;     // T（≥1，调用方已解析 0→逻辑核）
    int unfinished = 0;        // 未完成文件数 = remaining + inflight（≥0）
    int output_count = 1;      // 本次运行的输出数（cfg.outputs.size()）
    int user_parallel_cap = 0; // RunConfig::max_parallel_targets（0 = 自动）
    double cpu_load = -1.0;    // CPU EMA 0..1；<0 = 未知
    long long now_ms = 0;      // 调用方单调毫秒时钟（SteadyClock）
    // 本运行各输出的编码器能力（可选；空 = 全部按 {1,false} 保守处理）。
    const std::vector<EncoderThreadProfile> *profiles = nullptr;
};

struct ThreadPlan {
    int files = 1;            // W（并发闸宽度；恒 = alloc_threads 的 files）
    int encode_threads = 1;   // E（均分参考值：= floor(预算/P)；P==1 时 = 契约基值；
                              //    P>1 时实际逐输出值见 encode_threads_per_output）
    int parallel_outputs = 1; // P（同文件同时编码的 target 数；1 = 串行）
    int est_threads = 1;      // 线程占用估算上界 = W × (P 路编码器实际并发上界之和)
    // —— M15 加性字段（**末尾**；冻结面外的本模块结构体，允许加性扩展）——
    // 逐输出编码线程数，与 ThreadPlanInput::profiles / RunConfig::outputs 同序：
    //   * 空 = 全部 target 使用 encode_threads（0.4.2 旧口径；单输出 / P==1 路径恒为空）；
    //   * 非空（P > 1）= 按编码器能力公平分配：flat 编码器 1 线程（jpegli/OIIO/x265 固定池），
    //     heavy 编码器（jxl/avif，scales_with_encode_threads）分走余下预算。
    // 公平性（M15 修复）：0.4.2 的 `e = 预算/P` 把 jxl 从 16 压到 8/4/2 —— 对"只吃 E"的
    // 编码器不公平；逐输出分配让 heavy 在混合输出时接近满 E（见 plan_threads 不变式）。
    std::vector<int> encode_threads_per_output;
    // 决策词表（稳定英文短词，日志/单测用；新增词只能追加）：
    //   "base"   契约基值（无多输出并行，或无需调整）
    //   "multi"  多输出并行（浅队列、预算内）
    //   "hold"   恢复防抖中（CPU 尚未平静足够久，先保持 P=1）
    //   "converge" CPU 饱和收敛（P 减 1，迟滞后生效）
    const char *action = "base";
    const char *reason = ""; // 稳定英文短词，说明本决策的主因
};

// 迟滞状态（调用方持有；每次 plan_threads 调用更新）。
struct ThreadPlanState {
    bool has_last = false;
    ThreadPlan last;                // 上次产出的规划（比较用）
    long long last_change_ms = 0;   // 上次 CPU 驱动变化（converge/hold 切换）的时刻
    int saturated_streak = 0;       // 连续饱和采样数（CPU 已知时维护）
    int calm_streak = 0;            // 连续平静采样数
    bool last_was_converge = false; // 上一次决策是否 CPU 饱和收敛（恢复防抖用）
    void reset() { *this = ThreadPlanState{}; }
};

// 主入口：输入 + 状态 → 本次规划（并更新状态）。纯函数（除 state 的参数外无副作用）。
// 不变式（单测穷举，写入报告）：
//   * files == alloc_threads(T, unfinished, 0).files（契约零改动）；
//   * 1 ≤ encode_threads ≤ 16；1 ≤ parallel_outputs ≤ min(output_count, 8)；
//   * files × parallel_outputs × encode_threads ≤ T（均分参考值的旧不变式，零放宽）；
//   * est_threads ≥ files 且 est_threads ≤ T（est = W × P 路实际并发上界之和）；
//   * 深队列（files*2 > T）⇒ encode_threads == 1 且 parallel_outputs == 1；
//   * parallel_outputs > 1 ⇒ encode_threads * parallel_outputs ≤ max(1, T / files)；
//   * parallel_outputs > 1 ⇒ encode_threads_per_output.size() == output_count，
//     每项 ∈ [1,16]，且任意 P 路并发的 effective_threads 之和 ≤ max(1, T / files)
//     （heavy 存在的下界：heavy 的 E ≥ floor((每文件预算 - (P - 最小并发 heavy 数)) / 并发 heavy
//     数)）；
//   * parallel_outputs == 1 ⇒ encode_threads_per_output 为空（旧口径逐字不变）；
//   * CPU 未知 ⇒ parallel_outputs ≤ min(2, output_count)（旧行为上界）；
//   * CPU 饱和（EMA ≥0.90）且输出含不受控线程池（heif/x265/未知）⇒ P == 1；
ThreadPlan plan_threads(const ThreadPlanInput &in, ThreadPlanState &state);

} // namespace pp
