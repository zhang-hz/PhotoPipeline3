// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — core/adaptive_exec（M10 / W3-T4）单测：假编码器 + 假评分逐轮断言。
//
// 契约：docs/v0.4.0-design.md §9.5（搜索算法/成本）· §9.6（执行器）· §9.8（语义完备性）·
//       §9.9-2（本文件的验收面）· §12.5。
// 覆盖（逐条对 §9.9-2）：
//   1. linear / bisect / exp / greedy 的候选序列（逐轮参数值）；
//   2. 达标即停（score >= target）/ max_rounds 触顶 / plateau（连续 2 轮改善 < 阈值）；
//   3. set 阶段（1 stage = 1 轮；多键合一轮）；
//   4. 评分不可用 stop（输出最后一轮成功编码、metric 记 Unavailable）/ fail（
//      error 逐字 `adaptive: metric unavailable`、无产物）；
//   5. 取消（丢弃全部候选、不落盘）；非法参数（硬失败、无产物）；
//   6. 候选三件套（选定者 commit 一次、其余 discard；commit 失败 → 无产物）；
//   7. output best 显式拒绝 / 空区间兜底一轮 / 编码失败回退最后一轮成功编码；
//   8. policy_sha（SHA-256 已知向量 + 规范化文本一致）与逐轮记录字段齐全。
// 每个失败打印 "FAIL <case>: <detail>"；main() 返回失败数（0 = 全绿）。
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "core/adaptive.h"
#include "core/adaptive_exec.h"
#include "core/params.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &case_name, const std::string &detail) {
    if (!ok) {
        ++g_failed;
        std::printf("FAIL %s: %s\n", case_name.c_str(), detail.c_str());
    }
}

std::string num_text(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return buf;
}

double param_number(const pp::ParamSet &p, const std::string &key) {
    const auto it = p.find(key);
    if (it == p.end())
        return std::nan("");
    if (const auto *i = std::get_if<int64_t>(&it->second))
        return static_cast<double>(*i);
    if (const auto *d = std::get_if<double>(&it->second))
        return *d;
    return std::nan("");
}

pp::ParamSet jxl_base(double distance) {
    const pp::FormatDef *f = pp::find_format("jxl");
    pp::ParamSet s =
        f != nullptr ? pp::default_params(*f, "libjxl", "vardct", false) : pp::ParamSet{};
    s["distance"] = distance;
    return s;
}

pp::AdaptiveExecTarget jxl_target() {
    pp::AdaptiveExecTarget t;
    t.format_id = "jxl";
    t.backend_id = "libjxl";
    t.tech_id = "vardct";
    t.out_bitdepth = 16;
    t.lossless = false;
    t.width = 64;
    t.height = 64;
    t.final_path = "out/fake.jxl";
    return t;
}

pp::AdaptivePolicy dsl_policy(const std::string &text, const std::string &format) {
    pp::AdaptivePolicy p;
    pp::AdaptiveError e;
    const bool ok = pp::parse_and_validate_adaptive_policy(text, format, p, &e);
    check(ok, "dsl_parse", e.code + " " + e.message + " @ " + std::to_string(e.line));
    return p;
}

// 假编码器 / 假评分 / 假候选三件套的测试夹具：一切副作用都在内存里（零磁盘）。
struct Rig {
    pp::AdaptivePolicy policy;
    pp::ParamSet base = jxl_base(1.0);
    pp::AdaptiveExecTarget target = jxl_target();
    std::string key = "distance"; // 逐轮比对数值的参数键

    int encode_fail_round = -1;  // 该轮编码失败（-1 = 全成功）
    int cancel_after_round = -1; // >=0：已编码轮数 >= 该值时取消
    bool commit_fails = false;   // commit 返回失败（IO 场景）
    std::string validate_error;  // 非空 → validate hook 一律返回该错误

    // 记录面
    std::vector<pp::AdaptiveEncodeInput> inputs;
    std::vector<std::string> commits;
    std::vector<std::string> discards;
    std::vector<pp::AdaptiveRoundRecord> seen_records;
    std::vector<int> seen_max_rounds;
    int score_calls = 0;

    // 评分策略：默认恒 80 分；测试可注入 params → 分数（便于构造单调/非单调场景）。
    std::function<double(const pp::ParamSet &, int round)> score_of;

    std::vector<double> values() const {
        std::vector<double> out;
        out.reserve(inputs.size());
        for (const pp::AdaptiveEncodeInput &in : inputs)
            out.push_back(param_number(in.params, key));
        return out;
    }

    pp::AdaptiveRunResult run() {
        pp::AdaptiveRunHooks hooks;
        hooks.on_round = [this](const pp::AdaptiveRoundRecord &r, int max_rounds) {
            seen_records.push_back(r);
            seen_max_rounds.push_back(max_rounds);
        };
        hooks.candidate_path = [](int round) {
            return std::filesystem::path("cand-r" + std::to_string(round));
        };
        hooks.commit = [this](const std::filesystem::path &p, std::string *err) {
            commits.push_back(p.string());
            if (commit_fails) {
                if (err != nullptr)
                    *err = "fake io error";
                return false;
            }
            return true;
        };
        hooks.discard = [this](const std::filesystem::path &p) { discards.push_back(p.string()); };
        if (!validate_error.empty()) {
            hooks.validate = [this](const pp::ParamSet &) { return validate_error; };
        }
        auto encode = [this](const pp::AdaptiveEncodeInput &in) {
            inputs.push_back(in);
            pp::AdaptiveEncodeOutput o;
            if (in.round == encode_fail_round) {
                o.ok = false;
                o.error = "fake encoder error";
                return o;
            }
            o.ok = true;
            o.bytes = 1000 + static_cast<std::uint64_t>(in.round);
            o.encode_ms = 10.0 + in.round;
            o.progress_reported = true;
            return o;
        };
        auto score_fn = [this](const std::filesystem::path &) {
            ++score_calls;
            pp::AdaptiveScoreOutput so;
            const int round = static_cast<int>(inputs.size());
            so.metric_ms = 5.0 + round;
            const double s = score_of ? score_of(inputs.back().params, round) : 80.0;
            if (std::isnan(s)) {
                so.state = pp::MetricState::Unavailable;
                so.score = -1.0;
                so.note = "假评分不可用";
                return so;
            }
            so.state = pp::MetricState::Ok;
            so.score = s;
            return so;
        };
        auto cancel = [this]() {
            return cancel_after_round >= 0 && static_cast<int>(inputs.size()) >= cancel_after_round;
        };
        return pp::run_policy(policy, base, target, encode, score_fn, cancel, hooks);
    }
};

// ---------------------------------------------------------------------------
// 1. 搜索算法：候选序列（§9.5）
// ---------------------------------------------------------------------------
void case_linear_sequence() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    const std::vector<double> v = rig.values();
    check(v.size() == 5, "linear/count", "轮数 = " + std::to_string(v.size()));
    const double want[] = {0.9, 0.8, 0.7, 0.6, 0.5};
    for (std::size_t i = 0; i < v.size() && i < 5; ++i) {
        check(std::fabs(v[i] - want[i]) < 1e-9, "linear/value" + std::to_string(i),
              num_text(v[i]) + " != " + num_text(want[i]));
    }
    check(r.reason == pp::kAdaptiveReasonStageExhausted, "linear/reason", r.reason);
    check(r.ok && r.selected_round == 5, "linear/selected",
          "selected=" + std::to_string(r.selected_round));
    check(rig.commits.size() == 1 && rig.commits.front() == "cand-r5", "linear/commit",
          rig.commits.empty() ? "no commit" : rig.commits.front());
    check(rig.discards.size() == 4, "linear/discard_rest", std::to_string(rig.discards.size()));
}

void case_met_stops() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    // 0.9 → 80 分（未达标）；0.8 → 91 分（达标即停）。
    rig.score_of = [](const pp::ParamSet &p, int) {
        const double d = param_number(p, "distance");
        return d > 0.85 ? 80.0 : 91.0;
    };
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 2, "met/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonMet, "met/reason", r.reason);
    check(r.met(), "met/met", "met() false");
    check(r.selected_round == 2 && std::fabs(r.final_score - 91.0) < 1e-9, "met/score",
          num_text(r.final_score));
    check(rig.seen_records.size() == 2 && rig.seen_records[1].decision == "met", "met/decision",
          rig.seen_records.size() > 1 ? rig.seen_records[1].decision : "missing");
    check(!rig.seen_max_rounds.empty() && rig.seen_max_rounds.front() == 8, "met/max_rounds_hook",
          rig.seen_max_rounds.empty() ? "hook 未调用"
                                      : std::to_string(rig.seen_max_rounds.front()));
}

void case_max_rounds() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 2\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 2, "max_rounds/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonMaxRounds, "max_rounds/reason", r.reason);
    check(r.ok && r.selected_round == 2, "max_rounds/selected", std::to_string(r.selected_round));
}

void case_plateau() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 8\nplateau 0.5\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    // 每轮只涨 0.2 / 0.1 分（< 0.5 阈值）→ 连续 2 轮后 plateau 终止（第 3 轮结束）。
    rig.score_of = [](const pp::ParamSet &, int round) {
        return 80.0 + (round == 1 ? 0.0 : (round == 2 ? 0.2 : 0.3));
    };
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 3, "plateau/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonPlateau, "plateau/reason", r.reason);
    check(r.ok && r.selected_round == 3, "plateau/selected", std::to_string(r.selected_round));
}

void case_bisect_sequence() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 18\nstage scan\n"
                            "  search distance from current to 0.5 step 0.05 method bisect\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    const std::vector<double> v = rig.values();
    const double want[] = {0.75, 0.625, 0.5625, 0.5};
    check(v.size() == 4, "bisect/count", std::to_string(v.size()));
    for (std::size_t i = 0; i < v.size() && i < 4; ++i) {
        check(std::fabs(v[i] - want[i]) < 1e-9, "bisect/value" + std::to_string(i),
              num_text(v[i]) + " != " + num_text(want[i]));
    }
    check(r.reason == pp::kAdaptiveReasonStageExhausted, "bisect/reason", r.reason);
}

void case_exp_sequence() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 16\nstage scan\n"
                            "  search distance from current to 0.0 step 0.1 method exp\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    const std::vector<double> v = rig.values();
    const double want[] = {0.9, 0.75, 0.525, 0.1875, 0.0};
    check(v.size() == 5, "exp/count", std::to_string(v.size()));
    for (std::size_t i = 0; i < v.size() && i < 5; ++i) {
        check(std::fabs(v[i] - want[i]) < 1e-9, "exp/value" + std::to_string(i),
              num_text(v[i]) + " != " + num_text(want[i]));
    }
    check(r.reason == pp::kAdaptiveReasonStageExhausted, "exp/reason", r.reason);
}

void case_greedy_sequence() {
    Rig rig;
    rig.policy = dsl_policy("target 100\nmax_rounds 5\nstage scan\n"
                            "  search distance from current to 0.0 step 0.2 method greedy\n",
                            "jxl");
    // 0.8→92 / 0.6→94（仍改善）；0.4→93（不改善 → 步长 0.2→0.1，基线值仍 0.6）；
    // 下一档 = 0.6-0.1 = 0.5（88，不改善 → 步长 0.05）；再下一档 0.55（88）→ max_rounds 触顶。
    rig.score_of = [](const pp::ParamSet &p, int) {
        const double d = param_number(p, "distance");
        if (std::fabs(d - 0.8) < 1e-9)
            return 92.0;
        if (std::fabs(d - 0.6) < 1e-9)
            return 94.0;
        if (std::fabs(d - 0.4) < 1e-9)
            return 93.0;
        return 88.0;
    };
    const pp::AdaptiveRunResult r = rig.run();
    const std::vector<double> v = rig.values();
    const double want[] = {0.8, 0.6, 0.4, 0.5, 0.55};
    check(v.size() == 5, "greedy/count", std::to_string(v.size()));
    for (std::size_t i = 0; i < v.size() && i < 5; ++i) {
        check(std::fabs(v[i] - want[i]) < 1e-9, "greedy/value" + std::to_string(i),
              num_text(v[i]) + " != " + num_text(want[i]));
    }
    check(r.reason == pp::kAdaptiveReasonMaxRounds, "greedy/reason", r.reason);
}

// ---------------------------------------------------------------------------
// 2. set 阶段（1 stage = 1 轮；多键合一轮）
// ---------------------------------------------------------------------------
void case_set_stage() {
    Rig rig;
    rig.policy =
        dsl_policy("target 99\nmax_rounds 4\nstage tune\n  set epf 2\n  set dots true\n", "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 1, "set/rounds", std::to_string(rig.inputs.size()));
    if (rig.inputs.size() == 1) {
        const auto it_epf = rig.inputs.front().params.find("epf");
        const auto it_dots = rig.inputs.front().params.find("dots");
        check(it_epf != rig.inputs.front().params.end() && std::get<int64_t>(it_epf->second) == 2,
              "set/epf", "epf 未写入 2");
        check(it_dots != rig.inputs.front().params.end() && std::get<bool>(it_dots->second),
              "set/dots", "dots 未写入 true");
    }
    check(rig.seen_records.size() == 1 && rig.seen_records.front().key == "epf,dots",
          "set/key_text", rig.seen_records.empty() ? "missing" : rig.seen_records.front().key);
    check(r.reason == pp::kAdaptiveReasonStageExhausted, "set/reason", r.reason);
    check(r.ok && r.selected_round == 1, "set/selected", std::to_string(r.selected_round));
}

// ---------------------------------------------------------------------------
// 3. 评分不可用：stop（输出最后成功轮）/ fail（无产物、逐字错误）
// ---------------------------------------------------------------------------
void case_metric_unavailable_stop() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 4\non_metric_unavailable stop\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.score_of = [](const pp::ParamSet &, int) { return std::nan(""); };
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 1, "mu_stop/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonMetricUnavailable, "mu_stop/reason", r.reason);
    check(r.ok, "mu_stop/ok", "应当输出最后一轮成功编码");
    check(r.final_metric == pp::MetricState::Unavailable, "mu_stop/metric",
          std::to_string(static_cast<int>(r.final_metric)));
    check(r.final_score < 0.0, "mu_stop/score", num_text(r.final_score));
    check(rig.seen_records.size() == 1 && rig.seen_records.front().decision == "unscored",
          "mu_stop/decision",
          rig.seen_records.empty() ? "missing" : rig.seen_records.front().decision);
    check(rig.commits.size() == 1, "mu_stop/commit", std::to_string(rig.commits.size()));
}

void case_metric_unavailable_fail() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 4\non_metric_unavailable fail\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.score_of = [](const pp::ParamSet &, int) { return std::nan(""); };
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonMetricUnavailable, "mu_fail/reason", r.reason);
    check(!r.ok, "mu_fail/ok", "fail 语义必须 无产物");
    check(r.error == "adaptive: metric unavailable", "mu_fail/error", r.error);
    check(rig.commits.empty(), "mu_fail/commit", "不得 commit");
    check(rig.discards.size() == 1, "mu_fail/discard", std::to_string(rig.discards.size()));
}

// ---------------------------------------------------------------------------
// 4. 取消 / 非法参数
// ---------------------------------------------------------------------------
void case_cancel_cleans_up() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.cancel_after_round = 2;
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 2, "cancel/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonCancelled, "cancel/reason", r.reason);
    check(!r.ok, "cancel/ok", "取消不得落盘");
    check(rig.commits.empty(), "cancel/commit", "取消不得 commit");
    check(rig.discards.size() == 2, "cancel/discard", std::to_string(rig.discards.size()));
}

void case_cancel_before_any_round() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.cancel_after_round = 0;
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.empty(), "cancel0/rounds", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonCancelled && !r.ok, "cancel0/reason", r.reason);
    check(rig.commits.empty(), "cancel0/commit", "取消不得 commit");
}

void case_invalid_params_no_product() {
    Rig rig;
    rig.policy = dsl_policy("target 95\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.validate_error = "参数不合法（假校验）";
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonInvalidParams, "invalid/reason", r.reason);
    check(!r.ok, "invalid/ok", "非法参数不得有产物");
    check(rig.inputs.empty(), "invalid/no_encode", "校验失败不得编码");
    check(rig.commits.empty() && rig.discards.empty(), "invalid/no_files",
          "校验失败不得产生候选文件");
}

// ---------------------------------------------------------------------------
// 5. 候选三件套 / 提交失败
// ---------------------------------------------------------------------------
void case_commit_failure() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 2\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.commit_fails = true;
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonCommitFailed, "commit_fail/reason", r.reason);
    check(!r.ok && !r.error.empty(), "commit_fail/ok", r.error);
    check(rig.commits.size() == 1, "commit_fail/calls", std::to_string(rig.commits.size()));
    check(rig.discards.size() == 2, "commit_fail/cleanup", std::to_string(rig.discards.size()));
}

void case_encode_failure_falls_back() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.encode_fail_round = 3;
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 3, "enc_fail/attempts", std::to_string(rig.inputs.size()));
    check(r.reason == pp::kAdaptiveReasonEncodeFailed, "enc_fail/reason", r.reason);
    check(r.ok && r.selected_round == 2, "enc_fail/fallback",
          "selected=" + std::to_string(r.selected_round));
    check(rig.commits.size() == 1 && rig.commits.front() == "cand-r2", "enc_fail/commit",
          rig.commits.empty() ? "none" : rig.commits.front());
    check(rig.seen_records.size() == 3 && rig.seen_records[2].decision == "encode-failed",
          "enc_fail/record", rig.seen_records.size() > 2 ? rig.seen_records[2].decision : "?");
}

void case_encode_failure_first_round() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 8\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.encode_fail_round = 1;
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonEncodeFailed && !r.ok, "enc_first/reason", r.reason);
    check(!r.error.empty() && rig.commits.empty(), "enc_first/no_product", r.error);
    check(rig.discards.empty(), "enc_first/no_discard", std::to_string(rig.discards.size()));
}

// ---------------------------------------------------------------------------
// 6. 边界：output best 拒绝 / 空区间兜底 / 记录字段 / policy_sha
// ---------------------------------------------------------------------------
void case_output_best_rejected() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 4\noutput last\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    rig.policy.output = pp::AdaptiveOutput::Best; // 绕过校验：执行器必须再拒绝一次
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonInvalidPolicy && !r.ok, "best/reason", r.reason);
    check(rig.inputs.empty() && rig.commits.empty(), "best/no_work", "best 不得执行任何轮");
}

void case_empty_ladder_fallback() {
    Rig rig;
    rig.policy = dsl_policy("target 90\nmax_rounds 4\nstage scan\n"
                            "  search distance from current to 0.5 step 0.1\n",
                            "jxl");
    // 运行期空区间：from current = 0.5 == to 0.5（策略校验无法预知 current）。
    rig.base = jxl_base(0.5);
    rig.policy.stages.front().actions.front().to = 0.5;
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 1, "empty/fallback_rounds", std::to_string(rig.inputs.size()));
    check(!rig.seen_records.empty() && rig.seen_records.front().stage == "init",
          "empty/fallback_stage",
          rig.seen_records.empty() ? "missing" : rig.seen_records.front().stage);
    check(r.ok, "empty/ok", "空区间仍须产出（基线一轮）");
}

void case_record_fields_and_sha() {
    Rig rig;
    const std::string text = "target 90\nmax_rounds 3\nstage scan\n"
                             "  search distance from current to 0.5 step 0.1\n";
    rig.policy = dsl_policy(text, "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    check(!r.records.empty(), "rec/not_empty", "无逐轮记录");
    if (!r.records.empty()) {
        const pp::AdaptiveRoundRecord &rec = r.records.front();
        check(rec.round == 1 && rec.stage == "scan" && rec.key == "distance", "rec/ident",
              rec.stage + "/" + rec.key);
        check(std::fabs(rec.number - 0.9) < 1e-9 && rec.has_number, "rec/number",
              num_text(rec.number));
        check(rec.bytes == 1001 && std::fabs(rec.encode_ms - 11.0) < 1e-9, "rec/cost",
              std::to_string(rec.bytes));
        check(std::fabs(rec.metric_ms - 6.0) < 1e-9, "rec/metric_ms", num_text(rec.metric_ms));
        check(rec.metric == pp::MetricState::Ok && std::fabs(rec.score - 80.0) < 1e-9, "rec/score",
              num_text(rec.score));
        check(rec.decision == "improve" || rec.decision == "no-improve", "rec/decision",
              rec.decision);
    }
    check(r.records.size() == rig.seen_records.size(), "rec/hook_count",
          std::to_string(rig.seen_records.size()));
    check(r.policy_version == 1 && std::fabs(r.target - 90.0) < 1e-9 && r.max_rounds == 3,
          "rec/policy_facts", "target/max_rounds 不符");
    check(r.output == "last", "rec/output", r.output);
    check(r.policy_sha.size() == 64, "rec/sha_len", r.policy_sha);
    check(r.policy_sha == pp::adaptive_policy_sha256(pp::render_adaptive_policy(rig.policy)),
          "rec/sha_value", r.policy_sha);
    // SHA-256 已知向量（FIPS 180-4）：空串 / "abc"。
    check(pp::adaptive_policy_sha256("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "sha/empty", pp::adaptive_policy_sha256(""));
    check(pp::adaptive_policy_sha256("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "sha/abc", pp::adaptive_policy_sha256("abc"));
}

// ---------------------------------------------------------------------------
// 7. 跨 stage：前者落地值被后者继承（选定轮 = 最后一轮成功编码）
// ---------------------------------------------------------------------------
void case_two_stages_inherit() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 8\n"
                            "stage scan\n  search distance from current to 0.9 step 0.05\n"
                            "stage tweak\n  set epf 1\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 3, "stages/rounds", std::to_string(rig.inputs.size()));
    if (rig.inputs.size() == 3) {
        const auto it = rig.inputs.back().params.find("epf");
        const double d = param_number(rig.inputs.back().params, "distance");
        check(it != rig.inputs.back().params.end() && std::get<int64_t>(it->second) == 1,
              "stages/set_applied", "epf 未落地");
        check(std::fabs(d - 0.9) < 1e-9, "stages/inherit", "distance = " + num_text(d));
    }
    check(r.ok && r.selected_round == 3, "stages/selected", std::to_string(r.selected_round));
}

// ---------------------------------------------------------------------------
// 8. **默认**每轮校验（validate_params + cross_validate + rules::preflight）拒绝坏基线参数
//    （不注入 hooks.validate：证明 pipeline 走的默认链本身有效）
// ---------------------------------------------------------------------------
void case_default_validate_rejects() {
    Rig rig;
    const pp::FormatDef *f = pp::find_format("webp");
    rig.base = f != nullptr ? pp::default_params(*f, "libwebp", "lossy", false) : pp::ParamSet{};
    rig.base["quality"] = std::string("not-a-number"); // 类型不符（Int 参数收到字符串）
    rig.key = "quality";
    rig.target.format_id = "webp";
    rig.target.backend_id = "libwebp";
    rig.target.tech_id = "lossy";
    rig.target.out_bitdepth = 8;
    rig.target.final_path = "out/fake.webp";
    rig.policy = dsl_policy("target 90\nmax_rounds 2\nstage s1\n  set method 4\n", "webp");
    const pp::AdaptiveRunResult r = rig.run();
    check(r.reason == pp::kAdaptiveReasonInvalidParams, "default_validate/reason", r.reason);
    check(!r.ok && rig.inputs.empty(), "default_validate/no_product",
          "默认校验必须拦住坏参数且不编码");
    check(!r.error.empty(), "default_validate/error", "错误消息为空");
}

// ---------------------------------------------------------------------------
// 9. 兜底数值域（目录不可判定时的域；与 adaptive.cpp 校验侧同值）
// ---------------------------------------------------------------------------
void case_search_domain_fallback() {
    double lo = -1.0, hi = -1.0;
    pp::adaptive_search_domain("quality", lo, hi);
    check(lo == 0.0 && hi == 100.0, "domain/quality", num_text(lo) + ".." + num_text(hi));
    pp::adaptive_search_domain("distance", lo, hi);
    check(lo == 0.0 && hi == 25.0, "domain/distance", num_text(lo) + ".." + num_text(hi));
    pp::adaptive_search_domain("x265:crf", lo, hi);
    check(lo == 0.0 && hi == 51.0, "domain/crf", num_text(lo) + ".." + num_text(hi));
    pp::adaptive_search_domain("aom:cq-level", lo, hi);
    check(lo == 0.0 && hi == 63.0, "domain/cq", num_text(lo) + ".." + num_text(hi));
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃时也要看到进度（无缓冲）
    std::printf("CASE 1) linear 序列\n");
    case_linear_sequence();
    std::printf("CASE 2) 达标即停\n");
    case_met_stops();
    std::printf("CASE 3) max_rounds\n");
    case_max_rounds();
    std::printf("CASE 4) plateau\n");
    case_plateau();
    std::printf("CASE 5) bisect 序列\n");
    case_bisect_sequence();
    std::printf("CASE 6) exp 序列\n");
    case_exp_sequence();
    std::printf("CASE 7) greedy 序列\n");
    case_greedy_sequence();
    std::printf("CASE 8) set 阶段\n");
    case_set_stage();
    std::printf("CASE 9) 评分不可用 stop\n");
    case_metric_unavailable_stop();
    std::printf("CASE 10) 评分不可用 fail\n");
    case_metric_unavailable_fail();
    std::printf("CASE 11) 取消清理\n");
    case_cancel_cleans_up();
    std::printf("CASE 12) 未开轮即取消\n");
    case_cancel_before_any_round();
    std::printf("CASE 13) 非法参数无产物\n");
    case_invalid_params_no_product();
    std::printf("CASE 14) 提交失败\n");
    case_commit_failure();
    std::printf("CASE 15) 编码失败回退\n");
    case_encode_failure_falls_back();
    std::printf("CASE 16) 首轮编码失败\n");
    case_encode_failure_first_round();
    std::printf("CASE 17) output best 拒绝\n");
    case_output_best_rejected();
    std::printf("CASE 18) 空区间兜底\n");
    case_empty_ladder_fallback();
    std::printf("CASE 19) 记录字段与 policy_sha\n");
    case_record_fields_and_sha();
    std::printf("CASE 20) 跨 stage 继承\n");
    case_two_stages_inherit();
    std::printf("CASE 21) 默认校验拒绝坏参数\n");
    case_default_validate_rejects();
    std::printf("CASE 22) 兜底数值域\n");
    case_search_domain_fallback();

    std::printf("ADAPTIVE-EXEC %s (%d failures)\n", g_failed == 0 ? "OK" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
