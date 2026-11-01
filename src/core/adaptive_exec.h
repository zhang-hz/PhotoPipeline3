// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 自适应参数调整**执行器**（M10 / W3-T4）
//
// 契约：docs/v0.4.0-design.md §9.4（允许/禁止键）· §9.5（搜索算法与成本）·
//       §9.6（执行器/候选文件/进度/日志/侧车）· §9.8（语义完备性清单）· §12.5（验收探针）。
// 上游 AST/校验：src/core/adaptive.h（W3-T3；本文件**不改**它，只消费其公开面）。
//
// 定位
//   把"策略 → 逐轮编码 + 评分 → 选定候选"抽成**可注入回调的纯执行器**：编码（encode_fn）、
//   评分（score_fn）、取消（cancel_fn）、候选文件三件套（candidate/commit/discard）、
//   每轮参数校验（validate）与数值域读取（numeric_domain）全部可注入 —— 单测用假编码器
//   逐轮断言候选序列/停止条件，pipeline 用真编码器 + 同步评分（§9.6）。
//
// 语义要点（逐条对应设计，刻意无歧义）
//   * 一轮 = 应用参数 → 编码到候选临时文件（同目录；不写元数据）→ 评分 → 决策（§9.1-3）；
//   * 终止：达标即停（`score >= target`，浮点按 ≥）/ 全局 max_rounds / 每 stage 上界
//     （linear ≤16、bisect ≤18；exp/greedy 受 max_rounds）/ plateau（连续 2 轮改善 < 阈值）/
//     stage 耗尽进下一 stage、末 stage 耗尽 = 失败终止（§9.1-4/§9.5）；
//   * 输出：本版只支持 `output last`（`best` = P1，validate 已拒绝；执行器再显式拒绝一次）；
//     失败终止输出**最后一轮成功编码**；取消 = 丢弃全部候选、不落盘（§9.1-5/§9.1-6）；
//   * 评分不可用（Unavailable/Failed）：`on_metric_unavailable stop`（默认）= 停止迭代、
//     输出当前最后成功轮、metric 记 Unavailable；`fail` = 该输出失败，
//     error 逐字 `adaptive: metric unavailable`（§9.1-7）；
//   * 每轮参数校验 = `validate_params` + 带上下文 `cross_validate` + `rules::preflight`
//     （只读调用；非法 = 硬失败、无产物 —— 策略生成了非法参数属配置错误，不静默降级）；
//   * 正交性（§9.4/§9.8）：执行器只改策略声明的键（ParamSet 副本），位深/色彩/分辨率等
//     全局项根本不在写入面内；search 只走 adaptive 的 search 白名单键（validate 已判）。
//   * 确定性（§9.1-9）：不碰编码器线程策略；同样的 base/策略/输入 ⇒ 同样的候选序列。
//   * 候选文件（§9.6）：同目录 `atomic_temp_path(最终路径)`；选定者 commit、其余 discard、
//     取消清理全部；默认实现 = core/fsops.h 的 I1 三件套（可注入覆盖）。
//
// 日志/侧车（§9.6；由 pipeline/harness 在 hooks 里落，执行器只给数据）：
//   * 逐轮：round/stage/key/value/score/state/bytes/encode_ms/metric_ms/decision（+policy_sha）；
//   * 汇总：rounds/target/selected/policy_sha(前 16)/final_score/overhead_ms/reason。
//   * dev-only 证据通道：`publish_adaptive_summary()`（进程级按产物路径发布；先例 =
//     core/score_queue.h 的 `ScorePool::published`，供 `--dev` 侧车的 `adaptive` 对象使用）。
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/adaptive.h"
#include "core/params.h"
#include "core/pipeline.h" // MetricState（评分状态单源；不重复定义）

namespace pp {

// 日志里的 policy_sha 截断长度（§9.1-9：策略文本 SHA256 前 16 位进日志）。
inline constexpr std::size_t kAdaptivePolicyShaLogChars = 16;

// ---------------------------------------------------------------------------
// 执行上下文（目标/格式事实；pipeline 填真值，单测填真实格式 + 假编码器）
// ---------------------------------------------------------------------------
struct AdaptiveExecTarget {
    std::string format_id;
    std::string backend_id;
    std::string tech_id;
    int out_bitdepth = 8;
    bool lossless = false;
    int width = 0, height = 0;        // 像素尺寸（preflight 的尺寸相关规则用；0 = 未知）
    std::filesystem::path final_path; // 最终产物路径（候选临时文件与之同目录）
};

// 逐轮记录（日志/侧车/单测断言面；§9.6 的 round 字段集 + metric 状态）。
struct AdaptiveRoundRecord {
    int round = 0;     // 1-based 全局轮次（跨 stage 累计）
    std::string stage; // stage id
    std::string key;   // search = 参数键；set stage = 逗号连接的键列表
    std::string value; // search = 取值的文本形式；set stage = "k=v; k=v"
    bool has_number = false;
    double number = 0.0; // search 的数值（set stage 恒 false/0）
    MetricState metric = MetricState::Off;
    double score = -1.0; // metric == Ok 时有效
    std::uint64_t bytes = 0;
    double encode_ms = 0.0;
    double metric_ms = 0.0;
    std::string decision; // met / improve / no-improve / unscored / encode-failed
    std::string note;     // 评分不可用的中文短原因 / 编码失败原因
    // —— C 段加性（需求 1 迭代日志；pipeline 只读）——
    // 本轮生效参数文本：search = "x265:crf=18" / set stage = "chroma=444,profile=Main444_10Still"；
    // 基线 init 轮（策略未产生候选时补编的一轮）= 相对 base 的差量文本（无改动则空串）。
    std::string effective_params;
    // 本轮候选临时文件路径文本（candidate_path hook 的返回值；空 = 无候选文件）。
    std::string candidate_path;
};

// ---------------------------------------------------------------------------
// 计划摘要（C 段加性）：run_policy 入口的 on_plan 载荷（日志/侧车「plan」行）。
// ---------------------------------------------------------------------------
struct AdaptivePlanInfo {
    int policy_version = 0;             // 策略版本（1）
    double target = 0.0;                // 目标分
    int max_rounds = 0;                 // 全局轮数上界
    int stage_count = 0;                // stage 数
    std::string output;                 // "last"
    std::string policy_sha;             // 规范化策略文本 SHA-256（64 hex；日志取前 16）
    std::vector<std::string> stage_ids; // 按执行顺序的 stage id
};

// ---------------------------------------------------------------------------
// 注入面：编码 / 评分 / 取消
// ---------------------------------------------------------------------------
struct AdaptiveEncodeInput {
    ParamSet params;           // 本轮完整参数（base 副本 + 本轮动作）
    std::filesystem::path out; // 候选临时文件（同目录；调用方负责"不写元数据"）
    int round = 0;             // 1-based
    std::string stage;         // stage id
};

struct AdaptiveEncodeOutput {
    bool ok = false;
    std::string error; // 非空 = 该轮编码失败（执行器据此决定是否可回退）
    std::uint64_t bytes = 0;
    double encode_ms = 0.0;
    bool progress_reported = false; // 编码器是否报了真实行级进度（mux 合成标识口径）
};

struct AdaptiveScoreOutput {
    MetricState state = MetricState::Unavailable;
    double score = -1.0; // state == Ok 时有效
    double metric_ms = 0.0;
    std::string note; // 中文短原因（state != Ok 时）
};

using AdaptiveEncodeFn = std::function<AdaptiveEncodeOutput(const AdaptiveEncodeInput &)>;
// 评分：入参 = 候选文件路径；**同步**返回分数（§9.6：自适应轮次必须拿到分数才能决策）。
using AdaptiveScoreFn = std::function<AdaptiveScoreOutput(const std::filesystem::path &)>;
using AdaptiveCancelFn = std::function<bool()>;

// ---------------------------------------------------------------------------
// 注入面：hooks（进度/日志/校验/数值域/候选文件；全可空 = 用执行器默认实现）
// ---------------------------------------------------------------------------
struct AdaptiveRunHooks {
    // 每轮结束（记录已填好）：发 ProgressInfo（adaptive_round/adaptive_max）、写 info 日志、
    // 收集侧车记录都在这里做。
    std::function<void(const AdaptiveRoundRecord &, int max_rounds)> on_round;
    // 同步评分开始（发一条 scoring=true 的 Progress 心跳；§9.6「评分期心跳」）。
    std::function<void(int round, int max_rounds)> on_scoring;
    // 每轮参数校验（返回空串 = 通过）。默认 = validate_params + cross_validate + rules::preflight。
    std::function<std::string(const ParamSet &)> validate;
    // search 的 current/数值域解析（返回 false = 交给执行器默认实现：目录 ParamDef → 兜底域）。
    // 供 pipeline 追加 libheif 运行期内省键（heif/avif 的 quality/x265:crf 等）。
    std::function<bool(std::string_view key, const ParamSet &params, double &current, double &lo,
                       double &hi)>
        numeric_domain;
    // 候选文件三件套（默认 = core/fsops.h：atomic_temp_path / commit_temp_file /
    // discard_temp_file；同目录、唯一名）。
    std::function<std::filesystem::path(int round)> candidate_path;
    std::function<bool(const std::filesystem::path &candy, std::string *err)> commit;
    std::function<void(const std::filesystem::path &candy)> discard;
    // —— C 段加性（需求 1 迭代日志；默认空 = 不回调；异常被执行器吞掉、不影响决策）——
    // 计划摘要：run_policy 入口、任何轮次/阶段之前**恰好一次**（非法策略也会先收到）。
    std::function<void(const AdaptivePlanInfo &)> on_plan;
    // stage 开始：每个 stage 真正开始处理前**恰好一次**（含全 set stage；0 候选的 search
    // stage 同样回调）；round_before = 该 stage 开始前已执行的轮数（1-based 下一轮 = +1）。
    std::function<void(const std::string &stage_id, int round_before)> on_stage_start;
};

// 终止原因词表（结果 reason；日志/侧车/单测共用）。
//   met              达标（score >= target）——成功
//   max_rounds       全局轮数触顶 —— 输出最后一轮成功编码（§9.1-4）
//   stage_exhausted  阶段耗尽 —— 同上
//   plateau          连续 2 轮改善 < 阈值 —— 同上
//   metric_unavailable 评分不可用（stop）—— 输出最后一轮成功编码，metric 记 Unavailable
//   encode_failed    某轮编码失败 —— 有前序成功轮则输出之；无则失败
//   cancelled        取消 —— 丢弃全部候选、不落盘
//   invalid_policy   策略本身不可执行（空 stage / output best / 校验失败）
//   invalid_params   某轮参数非法（validate/preflight 拒绝）—— 硬失败、无产物
//   no_candidate     策略未产生任何候选（区间为空等；执行器会补一轮基线编码）
//   commit_failed    选定候选 commit 失败（IO）
inline constexpr std::string_view kAdaptiveReasonMet = "met";
inline constexpr std::string_view kAdaptiveReasonMaxRounds = "max_rounds";
inline constexpr std::string_view kAdaptiveReasonStageExhausted = "stage_exhausted";
inline constexpr std::string_view kAdaptiveReasonPlateau = "plateau";
inline constexpr std::string_view kAdaptiveReasonMetricUnavailable = "metric_unavailable";
inline constexpr std::string_view kAdaptiveReasonEncodeFailed = "encode_failed";
inline constexpr std::string_view kAdaptiveReasonCancelled = "cancelled";
inline constexpr std::string_view kAdaptiveReasonInvalidPolicy = "invalid_policy";
inline constexpr std::string_view kAdaptiveReasonInvalidParams = "invalid_params";
inline constexpr std::string_view kAdaptiveReasonNoCandidate = "no_candidate";
inline constexpr std::string_view kAdaptiveReasonCommitFailed = "commit_failed";

// 执行结果（pipeline 据此写 OutputResult / 日志 / 侧车）。
struct AdaptiveRunResult {
    bool enabled = false;   // run_policy 真的执行了（policy 非空）
    bool ok = false;        // 选定候选已 commit 到最终路径
    std::string reason;     // 见上表（恒非空）
    int rounds = 0;         // 实际执行轮数
    int selected_round = 0; // 0 = 无选定
    double final_score = -1.0;
    MetricState final_metric = MetricState::Off;
    std::uint64_t final_bytes = 0;
    double metric_ms = 0.0;              // 选定轮评分耗时（pipeline 写 row.metric_ms）
    bool progress_reported = false;      // 选定轮编码器是否报过真实行级进度（mux 合成标识口径）
    std::filesystem::path selected_path; // commit 前 = 选定候选路径
    std::string error;                   // 非空 = 该输出失败/取消（pipeline 直接写 row.error）
    std::string reason_note;             // 中文短原因（日志/侧车 note）
    std::string policy_sha;              // 规范化策略文本 SHA-256（64 hex；日志取前 16）
    // 策略面事实（侧车 adaptive 对象：policy_version/target/max_rounds/output）
    int policy_version = 0;
    double target = 0.0;
    int max_rounds = 0;
    std::string output; // "last"（本版唯一）
    double overhead_ms = 0.0;
    ParamSet final_params; // 选定轮参数（日志/诊断；侧车不落）
    std::vector<AdaptiveRoundRecord> records;
    // —— C 段加性（需求 1 迭代日志）——
    std::string
        selected_params_text; // 选定轮有效参数文本（= 选定 record.effective_params；无选定 = ""）
    std::string
        reason_detail; // 终止原因中文一句话（success/fail/cancel 都填；稳定英文码仍在 reason）

    bool met() const { return ok && reason == kAdaptiveReasonMet; }
};

// ---------------------------------------------------------------------------
// 执行器：策略 × 基线参数 × 目标 → 逐轮候选 → 选定候选（未 commit 指针在 result 里）
//   * `policy` 必须已经过 `validate_adaptive_policy(policy, format_id, ...)`（格式作用域检查
//     在 adaptive.h 的注册表里；执行器只做运行期可判定部分：空 stage / best / 每轮校验）。
//   * 失败不抛异常；一切失败写 `reason`/`error`（诚实、不静默）。
// ---------------------------------------------------------------------------
AdaptiveRunResult run_policy(const AdaptivePolicy &policy, const ParamSet &base_params,
                             const AdaptiveExecTarget &target, AdaptiveEncodeFn encode_fn,
                             AdaptiveScoreFn score_fn, AdaptiveCancelFn cancel_fn,
                             const AdaptiveRunHooks &hooks);

// 规范化策略文本的 SHA-256（64 位小写 hex；§9.1-9 的 policy_sha）。空串亦返回合法摘要。
std::string adaptive_policy_sha256(std::string_view policy_text);

// 自适应搜索键的**兜底数值域**（目录查不到时用；与 adaptive.h 校验侧的兜底口径一致）：
// quality 0–100 / distance 0–25 / x265:crf·x265:qp 0–51 / aom:cq-level·qp 0–63 /
// 未知键 0–100。`current` 语义见 run_policy 的 numeric_domain 默认实现。
void adaptive_search_domain(std::string_view key, double &lo, double &hi);

// ---------------------------------------------------------------------------
// dev-only 证据通道（进程级；仅 `--dev` 侧车的 `adaptive` 对象使用）
//   与 ScorePool::published 同构：按产物路径发布本次运行的自适应摘要，harness 在写侧车时
//   并回。**不参与引擎语义**；未启用自适应的运行永不写该表（默认路径零影响）。
// ---------------------------------------------------------------------------
void publish_adaptive_summary(std::string out_key, std::shared_ptr<const AdaptiveRunResult> run);
std::shared_ptr<const AdaptiveRunResult> published_adaptive_summary(const std::string &out_key);
void clear_adaptive_summaries();

} // namespace pp
