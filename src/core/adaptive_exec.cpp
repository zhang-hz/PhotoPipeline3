// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 自适应参数调整执行器（M10 / W3-T4；契约 docs/v0.4.0-design.md §9.4–§9.8）
//
// 本文件是**新文件**（不冻结；AGENTS §5.1「新能力尽量放新文件承载」）。上游 AST/校验在
// core/adaptive.{h,cpp}（W3-T3，本任务不改）；本文件只消费其公开面：
//   * `render_adaptive_policy`（规范化文本 → policy_sha 的输入）、
//   * `adaptive_coerce_set_value` / `adaptive_profile_from_value`（set 值归一单源）、
//   * `adaptive_search_domain` 的兜底域与 adaptive.cpp 的内部兜底表逐值一致（0–100 / 0–25 /
//     0–51 / 0–63）；目录可判定时以目录 ParamDef 为准。
//
// 分层口径：执行器**不落日志、不发事件、不碰 OutputResult** —— 只把逐轮事实放进
// AdaptiveRunResult（records/reason/error），日志/进度/侧车由 pipeline 与 harness 在 hooks
// 里落。这样单测可用假编码器/假评分逐轮断言算法与停止条件，而 pipeline 的集成面只剩接线。
#include "core/adaptive_exec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

#include "core/fsops.h"
#include "core/param_rules.h"

namespace pp {
namespace {

using Clock = std::chrono::steady_clock;

double ms_since(const Clock::time_point &t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 数值文本（日志/侧车/单测取值文本；与 adaptive.cpp 的 render_number 同口径：最多 10 位有效）。
std::string render_number(double v) {
    if (!std::isfinite(v))
        return "nan";
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// ===========================================================================
// SHA-256（FIPS 180-4；仅用于策略文本指纹 —— §9.1-9 的 policy_sha，不做安全用途）。
// 不引第三方、不复用 OIIO 的 SHA1（设计指定 SHA256；OIIO 只提供 SHA1）。
// ===========================================================================
class Sha256 {
public:
    void update(const void *data, std::size_t n) {
        const auto *p = static_cast<const unsigned char *>(data);
        total_ += n;
        while (n > 0) {
            const std::size_t take = std::min(n, sizeof(buf_) - buf_len_);
            std::memcpy(buf_ + buf_len_, p, take);
            buf_len_ += take;
            p += take;
            n -= take;
            if (buf_len_ == sizeof(buf_)) {
                block(buf_);
                buf_len_ = 0;
            }
        }
    }

    std::string hex_digest() {
        const std::uint64_t bits = total_ * 8ull;
        const unsigned char pad = 0x80;
        update(&pad, 1);
        const unsigned char zero = 0;
        while (buf_len_ != 56)
            update(&zero, 1);
        unsigned char len[8];
        for (int i = 0; i < 8; ++i)
            len[i] = static_cast<unsigned char>((bits >> (56 - 8 * i)) & 0xffu);
        update(len, sizeof(len)); // 收尾（此时 buf_len_ 归零，不再产生新的填充）
        static const char *kHex = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (std::uint32_t w : h_) {
            for (int i = 3; i >= 0; --i) {
                const unsigned char b = static_cast<unsigned char>((w >> (8 * i)) & 0xffu);
                out.push_back(kHex[b >> 4]);
                out.push_back(kHex[b & 0x0fu]);
            }
        }
        return out;
    }

private:
    static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void block(const unsigned char *p) {
        static const std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const std::size_t at = static_cast<std::size_t>(i) * 4;
            w[i] = (static_cast<std::uint32_t>(p[at]) << 24) |
                   (static_cast<std::uint32_t>(p[at + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[at + 2]) << 8) |
                   static_cast<std::uint32_t>(p[at + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + mj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    std::uint32_t h_[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                           0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::uint64_t total_ = 0;
    unsigned char buf_[64] = {};
    std::size_t buf_len_ = 0;
};

// ---------------------------------------------------------------------------
// 数值读取 / 参数目录（含 libheif 运行时内省；与 core/adaptive.cpp 的 format_key_index 同口径）
// ---------------------------------------------------------------------------
bool value_as_number(const ParamValue &v, double &out) {
    if (const auto *i = std::get_if<int64_t>(&v)) {
        out = static_cast<double>(*i);
        return true;
    }
    if (const auto *d = std::get_if<double>(&v)) {
        out = *d;
        return true;
    }
    return false;
}

bool is_runtime_format(std::string_view format_id) {
    const FormatDef *f = find_format(format_id);
    if (f == nullptr)
        return false;
    for (const BackendDef &b : f->backends) {
        if (b.runtime_introspected && b.techs.empty())
            return true;
    }
    return false;
}

// 进程级内省缓存（libheif 系实例化编码器的代价远高于一次 map 查找；同一进程结果稳定）。
const std::vector<BackendDef> &runtime_backends(std::string_view format_id) {
    static std::mutex m;
    static std::map<std::string, std::vector<BackendDef>> cache;
    const std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(std::string(format_id));
    if (it == cache.end())
        it = cache.emplace(std::string(format_id), introspect_backends(format_id)).first;
    return it->second;
}

const ParamDef *runtime_param_def(const AdaptiveExecTarget &t, std::string_view key) {
    if (!is_runtime_format(t.format_id))
        return nullptr;
    const std::vector<BackendDef> &live = runtime_backends(t.format_id);
    if (live.empty())
        return nullptr;
    const std::string want = t.backend_id.empty() ? std::string() : t.backend_id;
    const BackendDef *pick = nullptr;
    for (const BackendDef &b : live) {
        if (want.empty() || b.id == want) {
            pick = &b;
            break;
        }
    }
    if (pick == nullptr)
        return nullptr;
    const TechDef *tech = find_tech(*pick, "");
    const std::string_view suffix =
        key.find(':') == std::string_view::npos ? key : key.substr(key.find(':') + 1);
    if (tech != nullptr) {
        if (const ParamDef *d = find_param(*tech, key))
            return d;
        if (const ParamDef *d = find_param(*tech, suffix))
            return d;
    }
    for (const TechDef &tech2 : pick->techs) {
        if (const ParamDef *d = find_param(tech2, key))
            return d;
        if (const ParamDef *d = find_param(tech2, suffix))
            return d;
    }
    return nullptr;
}

// 目录 ParamDef（静态目录 first-wins → libheif 运行期内省）；找不到 = nullptr。
const ParamDef *catalog_param_def(const AdaptiveExecTarget &t, std::string_view key) {
    const FormatDef *f = find_format(t.format_id);
    if (f != nullptr) {
        if (const ParamDef *d = lookup_param_def(*f, t.backend_id, t.tech_id, key))
            return d;
    }
    return runtime_param_def(t, key);
}

// 每轮参数校验（**只读调用**）：validate_params（类型/范围/枚举/跨字段）→ 带上下文
// cross_validate（未知参数/跨字段中文文案）→ rules::preflight（m8 联动规则）。
std::string default_validate_round(const AdaptiveExecTarget &t, const ParamSet &params) {
    const FormatDef *f = find_format(t.format_id);
    if (f == nullptr)
        return "未知输出格式 '" + t.format_id + "'";
    std::string perr = validate_params(*f, t.backend_id, t.tech_id, t.lossless, params);
    if (!perr.empty())
        return perr;
    ParamContext ctx;
    ctx.format_id = t.format_id;
    ctx.backend_id = t.backend_id;
    ctx.tech_id = t.tech_id;
    ctx.bitdepth = t.out_bitdepth;
    ctx.lossless = t.lossless;
    ctx.width = t.width;
    ctx.height = t.height;
    for (const std::string &msg : cross_validate(ctx, params)) {
        if (!msg.empty())
            return msg;
    }
    for (const std::string &msg : rules::preflight(ctx, params)) {
        if (!msg.empty())
            return msg;
    }
    return {};
}

// ---------------------------------------------------------------------------
// 候选文件三件套的默认实现（同目录临时文件；core/fsops.h 的 I1 单源）
// ---------------------------------------------------------------------------
std::filesystem::path default_candidate_path(const std::filesystem::path &final_path, int round) {
    std::filesystem::path p = atomic_temp_path(final_path);
    if (p.empty())
        return p;
    // 轮次进文件名（§9.8 隔离：候选文件名含轮次，诊断可读）。atomic_temp_path 已保证同目录唯一。
    const std::string stem = p.stem().string();
    p.replace_filename(stem + "-r" + std::to_string(round) + p.extension().string());
    return p;
}

bool default_commit(const std::filesystem::path &cand, const std::filesystem::path &final_path,
                    std::string *err) {
    return commit_temp_file(cand, final_path, err);
}

// ---------------------------------------------------------------------------
// 候选序列（§9.5）。greedy 的步长减半依赖实测分数 → 由主循环驱动（见 Runner），此处只做
// 静态序列（linear/bisect/exp）。返回 false = 区间为空（start == to / 域退化为 0 宽）。
// ---------------------------------------------------------------------------
constexpr int kLinearStageCap = 16;       // §9.5：linear ≤16/阶段
constexpr int kBisectStageCap = 18;       // §9.5：bisect ≤18/阶段
constexpr double kExpStepFactor = 1.5;    // §9.5：exp 步长 ×1.5
constexpr double kExpStepMaxFactor = 4.0; // §9.5：步长上限 = 4×初始步长
constexpr int kGreedyMinStepDivisor = 16; // greedy 最小步长 = 初始步长 / 16（文档化常量）

void append_unique(std::vector<double> &out, double v) {
    if (out.empty() || out.back() != v)
        out.push_back(v);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
bool build_ladder(AdaptiveMethod method, double start, double to, double step, double lo, double hi,
                  std::vector<double> &out) {
    out.clear();
    if (!(step > 0.0) || !std::isfinite(step) || !std::isfinite(start) || !std::isfinite(to))
        return false;
    start = std::clamp(start, lo, hi);
    const double target = std::clamp(to, lo, hi);
    if (target == start)
        return false; // 空区间（from current 恰好等于 to 的运行期情形）
    const double dir = (target > start) ? 1.0 : -1.0;
    const double low = std::min(start, target), high = std::max(start, target);
    switch (method) {
    case AdaptiveMethod::Linear:
        for (int k = 1; k <= kLinearStageCap; ++k) {
            double v = start + dir * step * static_cast<double>(k);
            if ((dir > 0.0 && v >= target) || (dir < 0.0 && v <= target))
                v = target; // 钳到端点（终点含端点）
            v = std::clamp(v, low, high);
            const std::size_t before = out.size();
            append_unique(out, v);
            if (out.size() == before)
                break; // 步长小于浮点分辨率：不再产生新候选
            if (v == target)
                break;
        }
        break;
    case AdaptiveMethod::Bisect: {
        // 端点已知"差/好"侧（to = 好侧，单调假设）：每轮取剩余区间的中点；区间 <step 即止。
        // 末档补 to（最可能达标的值）—— 只在区间仍有余量时加，避免重复。
        double bad = start;
        double good = target;
        for (int k = 0; k < kBisectStageCap; ++k) {
            const double mid = (bad + good) / 2.0;
            if (std::abs(good - mid) < step || mid == bad || mid == good)
                break;
            append_unique(out, mid);
            bad = mid;
        }
        append_unique(out, target);
        break;
    }
    case AdaptiveMethod::Exp: {
        // 步长指数递增：v_k = v_{k-1} + dir·step_k，step_{k+1} = min(step_k×1.5, 4×初始步长)。
        double s = step;
        double v = start;
        for (int k = 0; k < 64; ++k) { // 保底上界（真实上界 = 全局 max_rounds）
            v += dir * s;
            if ((dir > 0.0 && v >= target) || (dir < 0.0 && v <= target)) {
                append_unique(out, target);
                break;
            }
            append_unique(out, std::clamp(v, low, high));
            s = std::min(s * kExpStepFactor, step * kExpStepMaxFactor);
        }
        break;
    }
    case AdaptiveMethod::Greedy:
        break; // 由主循环在线驱动
    }
    return !out.empty();
}

} // namespace

// ---------------------------------------------------------------------------
// 公开面：策略文本 SHA-256 / 兜底数值域
// ---------------------------------------------------------------------------
std::string adaptive_policy_sha256(std::string_view policy_text) {
    Sha256 sha;
    sha.update(policy_text.data(), policy_text.size());
    return sha.hex_digest();
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：key 输入 / lo,hi 输出域由名字固定
void adaptive_search_domain(std::string_view key, double &lo, double &hi) {
    // 与 core/adaptive.cpp 的 search_fallback_range 逐值一致（目录不可判定时的兜底域）。
    lo = 0.0;
    hi = 100.0; // quality 与未知键的兜底域
    if (key == "distance")
        hi = 25.0;
    else if (key == "x265:crf" || key == "x265:qp")
        hi = 51.0;
    else if (key == "aom:cq-level" || key == "qp")
        hi = 63.0;
}

// ---------------------------------------------------------------------------
// 执行器主体
// ---------------------------------------------------------------------------
namespace {

// 单轮结论（取消/硬失败是终态；编码失败可由前序成功轮回退）。
enum class RoundStep : std::uint8_t { Ok, EncodeFailed, HardFail, Cancelled };
enum class Stop : std::uint8_t {
    None,
    Met,
    MaxRounds,
    StageExhausted,
    Plateau,
    MetricStopped,
    EncodeFailed,
    HardFail,
    Cancelled
};

class Runner {
public:
    Runner(const AdaptivePolicy &policy, const ParamSet &base, const AdaptiveExecTarget &target,
           AdaptiveEncodeFn encode, AdaptiveScoreFn score, AdaptiveCancelFn cancel,
           const AdaptiveRunHooks &hooks)
        : policy_(policy), base_(base), target_(target), encode_(std::move(encode)),
          score_(std::move(score)), cancel_(std::move(cancel)), hooks_(hooks) {}

    AdaptiveRunResult run() {
        AdaptiveRunResult &res = res_;
        res.enabled = true;
        res.policy_version = policy_.version;
        res.target = policy_.target;
        res.max_rounds = policy_.max_rounds;
        res.output = std::string(adaptive_output_name(policy_.output));
        res.policy_sha = adaptive_policy_sha256(render_adaptive_policy(policy_));
        const Clock::time_point t0 = Clock::now();

        // §9.1/§9.6：本版只支持 output last（best = P1；validate 已拒绝，执行器再兜一层）。
        if (policy_.output != AdaptiveOutput::Last) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidPolicy);
            hard_error_ = "自适应 output best 未交付（P1）；请改 output last";
            return finish(t0);
        }
        if (policy_.stages.empty()) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidPolicy);
            hard_error_ = "自适应策略没有任何 stage";
            return finish(t0);
        }

        running_ = base_;
        for (const AdaptiveStage &stage : policy_.stages) {
            if (stop_ != Stop::None)
                break;
            if (cancelled())
                return cancel_all(t0);
            const bool all_set = std::all_of(
                stage.actions.begin(), stage.actions.end(),
                [](const AdaptiveAction &a) { return a.kind == AdaptiveActionKind::Set; });
            if (all_set) {
                if (!run_set_stage(stage))
                    break;
            } else if (!run_search_stage(stage)) {
                break;
            }
        }
        if (stop_ == Stop::Cancelled)
            return cancel_all(t0);
        // 边界（§9.8「单值 stage = 1 轮 / 空区间」）：策略未产生任何候选 → 用当前运行配置编一轮，
        // 保证"启用自适应"永不静默丢产物（该轮如实进 records，round=1/stage=init）。
        if (records_.empty() && stop_ != Stop::HardFail) {
            const RoundStep st =
                do_round(running_, "init", std::string(), std::string(), false, 0.0);
            if (st == RoundStep::Cancelled)
                return cancel_all(t0);
            if (st == RoundStep::HardFail)
                stop_ = Stop::HardFail;
            else if (st == RoundStep::EncodeFailed)
                stop_ = Stop::EncodeFailed;
        }
        return finish(t0);
    }

private:
    // ---- 环境事实 ----
    bool cancelled() const {
        if (!cancel_)
            return false;
        try {
            return cancel_();
        } catch (...) {
            return false; // 取消谓词异常不得影响决策（按未取消处理，如实由上层异常兜底）
        }
    }
    int max_rounds() const { return policy_.max_rounds; }

    // ---- 每轮参数校验 / 数值域 ----
    std::string validate_round(const ParamSet &params) const {
        if (hooks_.validate) {
            try {
                return hooks_.validate(params);
            } catch (const std::exception &e) {
                return std::string("validate threw: ") + e.what();
            } catch (...) {
                return "validate threw";
            }
        }
        return default_validate_round(target_, params);
    }

    // 返回 false = 域不可判定；current 不可判定时 *current 置 NaN（调用方给明确错误）。
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：current/lo/hi 三个输出参数由名字固定
    bool resolve_numeric(std::string_view key, const ParamSet &params, double &current, double &lo,
                         double &hi) const {
        current = std::numeric_limits<double>::quiet_NaN();
        if (hooks_.numeric_domain) {
            double c = 0.0, l = 0.0, h = 0.0;
            try {
                if (hooks_.numeric_domain(key, params, c, l, h)) {
                    current = c;
                    lo = l;
                    hi = h;
                    return std::isfinite(lo) && std::isfinite(hi) && hi > lo;
                }
            } catch (...) {
                // 落到默认实现
                (void)0; // 显式空操作：注释不计入语句
            }
        }
        const ParamDef *def = catalog_param_def(target_, key);
        if (def != nullptr && (def->type == ParamType::Int || def->type == ParamType::Float)) {
            lo = def->lo;
            hi = def->hi;
            const auto it = params.find(std::string(key));
            if (it != params.end() && value_as_number(it->second, current))
                return hi > lo;
            if (def->has_default && value_as_number(def->def, current))
                return hi > lo;
            return hi > lo; // 域可判定但 current 不可判定（current 保持 NaN）
        }
        adaptive_search_domain(key, lo, hi);
        const auto it = params.find(std::string(key));
        if (it != params.end() && value_as_number(it->second, current))
            return hi > lo;
        return hi > lo;
    }

    // search 的候选值写回（按目录类型归一；目录不可判定时按整数/浮点字面量）。
    std::string set_numeric(ParamSet &next, std::string_view key, double v) const {
        const ParamDef *def = catalog_param_def(target_, key);
        if (def != nullptr) {
            v = std::clamp(v, def->lo, def->hi);
            if (def->type == ParamType::Int) {
                next[std::string(key)] = static_cast<int64_t>(std::llround(v));
                return {};
            }
            if (def->type == ParamType::Float) {
                next[std::string(key)] = v;
                return {};
            }
            return "参数「" + std::string(key) + "」不是数值类型（不能 search）";
        }
        if (v == std::floor(v) && std::abs(v) < 9007199254740992.0)
            next[std::string(key)] = static_cast<int64_t>(std::llround(v));
        else
            next[std::string(key)] = v;
        return {};
    }

    // set 动作写回（值归一单源 = adaptive_coerce_set_value；profile 伪参数 → 保留键）。
    std::string apply_set(ParamSet &next, const AdaptiveAction &a) const {
        if (a.key == "profile") {
            int64_t prof = 0;
            if (!adaptive_profile_from_value(a.value, prof))
                return "无法识别的编码档位值（参数 profile）";
            next["__heif_profile"] = prof;
            return {};
        }
        const ParamDef *def = catalog_param_def(target_, a.key);
        if (def != nullptr) {
            ParamValue coerced;
            std::string why;
            if (!adaptive_coerce_set_value(a.value, *def, coerced, &why))
                return "参数「" + a.key + "」的取值不合法：" + why;
            next[a.key] = coerced;
            return {};
        }
        // 目录未声明（libheif 插件透传键等）：标量字面量直写；类型/范围由每轮 validate 兜底。
        if (std::holds_alternative<std::monostate>(a.value))
            return "参数「" + a.key + "」缺少取值";
        next[a.key] = a.value;
        return {};
    }

    // ---- 候选文件 ----
    std::filesystem::path make_candidate(int round) const {
        if (hooks_.candidate_path) {
            try {
                return hooks_.candidate_path(round);
            } catch (...) {
                return {};
            }
        }
        return default_candidate_path(target_.final_path, round);
    }
    bool commit_candidate(const std::filesystem::path &cand, std::string *err) const {
        if (hooks_.commit) {
            try {
                return hooks_.commit(cand, err);
            } catch (const std::exception &e) {
                if (err != nullptr)
                    *err = e.what();
                return false;
            } catch (...) {
                if (err != nullptr)
                    *err = "commit threw";
                return false;
            }
        }
        return default_commit(cand, target_.final_path, err);
    }
    void discard_candidate(const std::filesystem::path &cand) const {
        if (cand.empty())
            return;
        if (hooks_.discard) {
            try {
                hooks_.discard(cand);
            } catch (...) {
                // 清场失败不得影响决策（discard 默认实现本身 noexcept）
                (void)0; // 显式空操作：异常按契约吞掉（注释不计入语句）
            }
            return;
        }
        discard_temp_file(cand);
    }
    // keep_selected=false = 清场全部候选（硬失败/取消/提交失败：不落盘、不留残件）；
    // true = 只清非选定者（成功提交后选定候选已 rename 到最终路径）。
    void discard_all(bool keep_selected) {
        for (const std::filesystem::path &c : candidates_) {
            if (keep_selected && c == selected_.path)
                continue;
            discard_candidate(c);
        }
        candidates_.clear();
    }

    // ---- 单轮 ----
    RoundStep do_round(const ParamSet &params, const std::string &stage, const std::string &key,
                       const std::string &value, bool has_number, double number) {
        if (cancelled())
            return RoundStep::Cancelled;
        const int round = rounds_ + 1;
        const std::string verr = validate_round(params);
        if (!verr.empty()) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
            hard_error_ =
                "自适应第 " + std::to_string(round) + " 轮参数非法（stage " + stage + "）：" + verr;
            return RoundStep::HardFail;
        }
        const std::filesystem::path cand = make_candidate(round);
        if (cand.empty()) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
            hard_error_ =
                "无法创建候选临时文件路径（最终路径：" + target_.final_path.string() + "）";
            return RoundStep::HardFail;
        }

        AdaptiveRoundRecord rec;
        rec.round = round;
        rec.stage = stage;
        rec.key = key;
        rec.value = value;
        rec.has_number = has_number;
        rec.number = number;

        AdaptiveEncodeInput in;
        in.params = params;
        in.out = cand;
        in.round = round;
        in.stage = stage;
        AdaptiveEncodeOutput eo;
        try {
            if (encode_)
                eo = encode_(in);
            else
                eo.error = "no encoder callback";
        } catch (const std::exception &e) {
            eo.ok = false;
            eo.error = std::string("encode threw: ") + e.what();
        } catch (...) {
            eo.ok = false;
            eo.error = "encode threw";
        }
        rounds_ = round;
        rec.bytes = eo.bytes;
        rec.encode_ms = eo.encode_ms;
        last_reported_ = eo.progress_reported;
        if (!eo.ok) {
            rec.decision = "encode-failed";
            rec.note = eo.error.empty() ? std::string("编码器没有产出数据") : eo.error;
            push_record(rec);
            return RoundStep::EncodeFailed;
        }
        candidates_.push_back(cand);

        // 同步评分（§9.6：自适应必须拿到本轮分数才能决策；评分不可用按 §9.1-7 语义）。
        if (hooks_.on_scoring) {
            try {
                hooks_.on_scoring(round, max_rounds());
            } catch (...) {
                // 进度回调异常不得影响决策
                (void)0; // 显式空操作：异常按契约吞掉（注释不计入语句）
            }
        }
        AdaptiveScoreOutput so;
        try {
            if (score_)
                so = score_(cand);
            else {
                so.state = MetricState::Unavailable;
                so.note = "没有评分回调";
            }
        } catch (const std::exception &e) {
            so.state = MetricState::Failed;
            so.score = -1.0;
            so.note = std::string("指标内部错误: ") + e.what();
        } catch (...) {
            so.state = MetricState::Failed;
            so.score = -1.0;
            so.note = "指标内部错误";
        }
        rec.metric = so.state;
        rec.score = (so.state == MetricState::Ok) ? so.score : -1.0;
        rec.metric_ms = so.metric_ms;
        rec.note = (so.state == MetricState::Ok) ? std::string() : so.note;

        if (so.state != MetricState::Ok) {
            rec.decision = "unscored";
            select(cand, params, rec);
            push_record(rec);
            if (policy_.on_metric_unavailable == AdaptiveMetricUnavailable::Fail) {
                stop_ = Stop::HardFail;
                hard_reason_ = std::string(kAdaptiveReasonMetricUnavailable);
                hard_error_ = "adaptive: metric unavailable";
                return RoundStep::HardFail;
            }
            stop_ = Stop::MetricStopped; // §9.1-7：停止迭代，输出当前最后成功轮
            return RoundStep::Ok;
        }

        // 决策（§9.1-4）：达标即停 / 改善记录 / plateau 计数。
        const bool first = best_score_ < -1e300;
        const double gain = first ? 0.0 : (so.score - best_score_);
        last_improved_ = first || so.score > best_score_;
        if (so.score >= policy_.target) {
            rec.decision = "met";
        } else if (last_improved_) {
            rec.decision = "improve";
        } else {
            rec.decision = "no-improve";
        }
        if (policy_.plateau > 0.0 && !first) {
            if (gain < policy_.plateau) {
                if (++low_gain_ >= 2)
                    plateau_hit_ = true;
            } else {
                low_gain_ = 0;
            }
        }
        if (first || so.score > best_score_)
            best_score_ = so.score;
        select(cand, params, rec);
        push_record(rec);
        met_ = (so.score >= policy_.target);
        return RoundStep::Ok;
    }

    void select(const std::filesystem::path &cand, const ParamSet &params,
                const AdaptiveRoundRecord &rec) {
        selected_.path = cand;
        selected_.round = rec.round;
        selected_.score = rec.score;
        selected_.metric = rec.metric;
        selected_.bytes = rec.bytes;
        selected_.metric_ms = rec.metric_ms;
        selected_.note = rec.note;
        selected_.params = params;
    }

    void push_record(const AdaptiveRoundRecord &rec) {
        records_.push_back(rec);
        if (hooks_.on_round) {
            try {
                hooks_.on_round(records_.back(), max_rounds());
            } catch (...) {
                // 进度/日志回调异常不得影响决策
                (void)0; // 显式空操作：异常按契约吞掉（注释不计入语句）
            }
        }
    }

    // ---- stage 驱动 ----
    // 全 Set 阶段：全部 set 落到同一轮参数上，编一轮（§9.6 的伪代码：for kv → 一次 encode）。
    bool run_set_stage(const AdaptiveStage &stage) {
        ParamSet next = running_;
        std::string keys, values;
        for (const AdaptiveAction &a : stage.actions) {
            const std::string err = apply_set(next, a);
            if (!err.empty()) {
                stop_ = Stop::HardFail;
                hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
                hard_error_ = "stage " + stage.id + " 的 set " + a.key + " 失败：" + err;
                return false;
            }
            if (!keys.empty()) {
                keys += ",";
                values += "; ";
            }
            keys += a.key;
            values += a.key + "=" + param_value_text(next[a.key]);
        }
        if (rounds_ >= max_rounds()) {
            stop_ = Stop::MaxRounds;
            return true;
        }
        const RoundStep st = do_round(next, stage.id, keys, values, false, 0.0);
        if (st == RoundStep::Cancelled || st == RoundStep::HardFail)
            return false;
        if (st == RoundStep::EncodeFailed) {
            stop_ = Stop::EncodeFailed;
            return true;
        }
        if (met_) {
            stop_ = Stop::Met;
            return true;
        }
        if (plateau_hit_) {
            stop_ = Stop::Plateau;
            return true;
        }
        if (stop_ != Stop::None)
            return true; // MetricStopped
        running_ = next;
        return true;
    }

    // 含 search 的 stage：set 动作先落地（作为本轮及后续的基底），随后逐个 search 动作展开。
    bool run_search_stage(const AdaptiveStage &stage) {
        for (const AdaptiveAction &a : stage.actions) {
            if (stop_ != Stop::None)
                return true;
            if (cancelled()) {
                stop_ = Stop::Cancelled;
                return false;
            }
            if (a.kind == AdaptiveActionKind::Set) {
                const std::string err = apply_set(running_, a);
                if (!err.empty()) {
                    stop_ = Stop::HardFail;
                    hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
                    hard_error_ = "stage " + stage.id + " 的 set " + a.key + " 失败：" + err;
                    return false;
                }
                continue;
            }
            if (!run_search_action(stage, a))
                return false;
        }
        return true;
    }

    bool run_search_action(const AdaptiveStage &stage, const AdaptiveAction &a) {
        double current = 0.0, lo = 0.0, hi = 0.0;
        if (!resolve_numeric(a.key, running_, current, lo, hi)) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
            hard_error_ = "参数「" + a.key + "」的数值域不可判定（该格式目录未声明该键）";
            return false;
        }
        double start = a.from.current ? current : a.from.number;
        if (!std::isfinite(start)) {
            stop_ = Stop::HardFail;
            hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
            hard_error_ = "参数「" + a.key +
                          "」没有可用的当前值（`from current` 需要运行配置里显式设置该键）";
            return false;
        }
        start = std::clamp(start, lo, hi);
        const double to = std::clamp(a.to, lo, hi);
        int action_cap = a.max_rounds > 0 ? a.max_rounds
                                          : (a.method == AdaptiveMethod::Linear ? kLinearStageCap
                                             : a.method == AdaptiveMethod::Bisect
                                                 ? kBisectStageCap
                                                 : std::numeric_limits<int>::max());
        int stage_rounds = 0;

        if (a.method == AdaptiveMethod::Greedy) {
            const double dir = (to >= start) ? 1.0 : -1.0;
            double value = start;
            double step_now = a.step;
            const double min_step = a.step / static_cast<double>(kGreedyMinStepDivisor);
            while (stop_ == Stop::None) {
                if (cancelled()) {
                    stop_ = Stop::Cancelled;
                    return false;
                }
                if (rounds_ >= max_rounds()) {
                    stop_ = Stop::MaxRounds;
                    return true;
                }
                if (stage_rounds >= action_cap)
                    break;
                double cand = value + dir * step_now;
                if ((dir > 0.0 && cand >= to) || (dir < 0.0 && cand <= to))
                    cand = to;
                cand = std::clamp(cand, std::min(start, to), std::max(start, to));
                if (cand == value)
                    break;
                ParamSet next = running_;
                const std::string serr = set_numeric(next, a.key, cand);
                if (!serr.empty()) {
                    stop_ = Stop::HardFail;
                    hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
                    hard_error_ = serr;
                    return false;
                }
                const RoundStep st =
                    do_round(next, stage.id, a.key, render_number(cand), true, cand);
                if (!handle_step(st, next))
                    return false;
                if (stop_ != Stop::None)
                    return true;
                if (last_improved_) {
                    value = cand;
                } else {
                    step_now /= 2.0;
                    if (step_now < min_step)
                        break;
                }
                ++stage_rounds;
                if (cand == to)
                    break;
            }
            return true;
        }

        std::vector<double> ladder;
        if (!build_ladder(a.method, start, to, a.step, lo, hi, ladder))
            return true; // 空区间：该 search 动作无候选（非错误；stage 耗尽语义）
        for (const double v : ladder) {
            if (stop_ != Stop::None)
                return true;
            if (cancelled()) {
                stop_ = Stop::Cancelled;
                return false;
            }
            if (rounds_ >= max_rounds()) {
                stop_ = Stop::MaxRounds;
                return true;
            }
            if (stage_rounds >= action_cap)
                break;
            ParamSet next = running_;
            const std::string serr = set_numeric(next, a.key, v);
            if (!serr.empty()) {
                stop_ = Stop::HardFail;
                hard_reason_ = std::string(kAdaptiveReasonInvalidParams);
                hard_error_ = serr;
                return false;
            }
            const RoundStep st = do_round(next, stage.id, a.key, render_number(v), true, v);
            if (!handle_step(st, next))
                return false;
            if (stop_ != Stop::None)
                return true;
            ++stage_rounds;
        }
        return true;
    }

    // 单轮后的公共收尾：终态/继承参数。返回 false = 终止（取消/硬失败）。
    bool handle_step(RoundStep st, const ParamSet &next) {
        if (st == RoundStep::Cancelled) {
            stop_ = Stop::Cancelled;
            return false;
        }
        if (st == RoundStep::HardFail)
            return false; // stop_/hard_* 已由 do_round 置好
        if (st == RoundStep::EncodeFailed) {
            stop_ = Stop::EncodeFailed;
            return true;
        }
        if (met_) {
            stop_ = Stop::Met;
            return true;
        }
        if (plateau_hit_) {
            stop_ = Stop::Plateau;
            return true;
        }
        if (stop_ == Stop::MetricStopped)
            return true;
        running_ = next; // 成功轮 → 后续 stage 继承（"选定轮 = 最后一轮成功编码"的一致性）
        return true;
    }

    std::string reason_of_stop() const {
        switch (stop_) {
        case Stop::Met:
            return std::string(kAdaptiveReasonMet);
        case Stop::MaxRounds:
            return std::string(kAdaptiveReasonMaxRounds);
        case Stop::Plateau:
            return std::string(kAdaptiveReasonPlateau);
        case Stop::MetricStopped:
            return std::string(kAdaptiveReasonMetricUnavailable);
        case Stop::EncodeFailed:
            return std::string(kAdaptiveReasonEncodeFailed);
        case Stop::HardFail:
            return hard_reason_.empty() ? std::string(kAdaptiveReasonInvalidParams) : hard_reason_;
        case Stop::Cancelled:
            return std::string(kAdaptiveReasonCancelled);
        case Stop::None:
        case Stop::StageExhausted:
            break;
        }
        if (rounds_ >= max_rounds())
            return std::string(kAdaptiveReasonMaxRounds);
        return std::string(kAdaptiveReasonStageExhausted);
    }

    AdaptiveRunResult finish(const Clock::time_point &t0) {
        AdaptiveRunResult &res = res_;
        res.rounds = rounds_;
        res.records = records_;
        res.overhead_ms = ms_since(t0);
        if (stop_ == Stop::HardFail) {
            res.ok = false;
            res.reason = reason_of_stop();
            res.error = hard_error_;
            discard_all(/*keep_selected=*/false);
            return res;
        }
        if (selected_.round == 0) {
            res.ok = false;
            if (stop_ == Stop::EncodeFailed) {
                // 没有任何成功编码的轮（首轮即失败）→ 无产物；错误带编码器原文。
                res.reason = std::string(kAdaptiveReasonEncodeFailed);
                res.error =
                    "自适应编码失败（第 " + std::to_string(rounds_) + " 轮）：" +
                    (records_.empty() ? std::string("编码器没有产出数据") : records_.back().note);
            } else {
                res.reason = std::string(kAdaptiveReasonNoCandidate);
                res.error = "自适应策略未产生任何候选（search 区间为空）";
            }
            discard_all(/*keep_selected=*/false);
            return res;
        }
        if (cancelled()) {
            discard_all(/*keep_selected=*/false);
            res.ok = false;
            res.reason = std::string(kAdaptiveReasonCancelled);
            res.error = "adaptive cancelled";
            return res;
        }
        std::string commit_err;
        if (!commit_candidate(selected_.path, &commit_err)) {
            res.ok = false;
            res.reason = std::string(kAdaptiveReasonCommitFailed);
            res.error = "自适应选定候选提交失败：" + commit_err;
            discard_all(/*keep_selected=*/false);
            return res;
        }
        discard_all(/*keep_selected=*/true); // 其余候选清场（选定者已 rename 到最终路径）
        res.ok = true;
        res.reason = reason_of_stop();
        res.selected_round = selected_.round;
        res.final_score = selected_.score;
        res.final_metric = selected_.metric;
        res.final_bytes = selected_.bytes;
        res.selected_path = selected_.path;
        res.final_params = selected_.params;
        res.reason_note = selected_.note;
        res.progress_reported = last_reported_;
        res.metric_ms = selected_.metric_ms;
        return res;
    }

    AdaptiveRunResult cancel_all(const Clock::time_point &t0) {
        discard_all(/*keep_selected=*/false);
        AdaptiveRunResult &res = res_;
        res.rounds = rounds_;
        res.records = records_;
        res.overhead_ms = ms_since(t0);
        res.ok = false;
        res.reason = std::string(kAdaptiveReasonCancelled);
        res.error = "adaptive cancelled";
        return res;
    }

    // 参数值文本（set stage 的 record.value；与 adaptive.cpp 的 render_value 口径无关，
    // 只用于日志/侧车可读性）。
    static std::string param_value_text(const ParamValue &v) {
        if (const auto *b = std::get_if<bool>(&v))
            return *b ? "true" : "false";
        if (const auto *i = std::get_if<int64_t>(&v))
            return std::to_string(*i);
        if (const auto *d = std::get_if<double>(&v))
            return render_number(*d);
        if (const auto *s = std::get_if<std::string>(&v))
            return *s;
        return {};
    }

    const AdaptivePolicy &policy_;
    ParamSet base_;
    AdaptiveExecTarget target_;
    AdaptiveEncodeFn encode_;
    AdaptiveScoreFn score_;
    AdaptiveCancelFn cancel_;
    AdaptiveRunHooks hooks_;

    AdaptiveRunResult res_;
    ParamSet running_;
    std::vector<AdaptiveRoundRecord> records_;
    std::vector<std::filesystem::path> candidates_;

    struct Selection {
        std::filesystem::path path;
        ParamSet params;
        int round = 0;
        double score = -1.0;
        MetricState metric = MetricState::Off;
        std::uint64_t bytes = 0;
        double metric_ms = 0.0;
        std::string note;
    } selected_;

    int rounds_ = 0;
    double best_score_ = -std::numeric_limits<double>::max();
    int low_gain_ = 0;
    bool plateau_hit_ = false;
    bool met_ = false;
    bool last_improved_ = false;
    bool last_reported_ = false;
    Stop stop_ = Stop::None;
    std::string hard_reason_;
    std::string hard_error_;
};

// ---- dev-only 证据通道（进程级）----
std::mutex g_summary_mu;
std::map<std::string, std::shared_ptr<const AdaptiveRunResult>> g_summaries;
constexpr std::size_t kAdaptiveSummaryCap = 4096; // 大批量兜底：超上限不再登记（不覆盖旧值）

} // namespace

AdaptiveRunResult run_policy(const AdaptivePolicy &policy, const ParamSet &base_params,
                             const AdaptiveExecTarget &target, AdaptiveEncodeFn encode_fn,
                             AdaptiveScoreFn score_fn, AdaptiveCancelFn cancel_fn,
                             const AdaptiveRunHooks &hooks) {
    Runner runner(policy, base_params, target, std::move(encode_fn), std::move(score_fn),
                  std::move(cancel_fn), hooks);
    return runner.run();
}

void publish_adaptive_summary(std::string out_key, std::shared_ptr<const AdaptiveRunResult> run) {
    if (out_key.empty() || !run)
        return;
    const std::lock_guard<std::mutex> lock(g_summary_mu);
    if (g_summaries.size() >= kAdaptiveSummaryCap && g_summaries.find(out_key) == g_summaries.end())
        return;
    g_summaries[std::move(out_key)] = std::move(run);
}

std::shared_ptr<const AdaptiveRunResult> published_adaptive_summary(const std::string &out_key) {
    const std::lock_guard<std::mutex> lock(g_summary_mu);
    const auto it = g_summaries.find(out_key);
    return it == g_summaries.end() ? nullptr : it->second;
}

void clear_adaptive_summaries() {
    const std::lock_guard<std::mutex> lock(g_summary_mu);
    g_summaries.clear();
}

} // namespace pp
