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
#include <utility>
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

// M12 需求 5：JPEG 出厂 default 预设（presets/formats/jpeg/default.json）的基准值与策略文本。
// 基准 = jpegli/dct 静态默认全量 + distance=当前值（策略从 current 出发）；策略逐字取自工厂文件
// （注释行对语义无影响，这里保留以与文件对照）。
pp::ParamSet jpeg_base(double distance) {
    const pp::FormatDef *f = pp::find_format("jpeg");
    pp::ParamSet s = f != nullptr ? pp::default_params(*f, "jpegli", "dct", false) : pp::ParamSet{};
    s["distance"] = distance;
    return s;
}

pp::AdaptiveExecTarget jpeg_target() {
    pp::AdaptiveExecTarget t;
    t.format_id = "jpeg";
    t.backend_id = "jpegli";
    t.tech_id = "dct";
    t.out_bitdepth = 8;
    t.lossless = false;
    t.width = 64;
    t.height = 64;
    t.final_path = "out/fake.jpg";
    return t;
}

const char *kJpegDefaultPolicy =
    "# JPEG default：视觉距离从 3.0 起步（1–3 步长 1 粗扫），1 以下按 0.1 精扫到 0.1；"
    "取达到目标 90 分的最小体积（= 最大 distance）一轮。\n"
    "version 1\n"
    "target 90\n"
    "max_rounds 20\n"
    "output last\n"
    "on_metric_unavailable stop\n"
    "\n"
    "stage distance-down\n"
    "  search distance from current down to 0.1 step 0.1 breakpoint 1 step 1 max_rounds 20\n";

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
    std::vector<pp::AdaptivePlanInfo> plans;
    std::vector<std::pair<std::string, int>> stage_starts;
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
        // C 段（需求 1）：计划/阶段开始回调（逐条断言 hook 数据面）。
        hooks.on_plan = [this](const pp::AdaptivePlanInfo &p) { plans.push_back(p); };
        hooks.on_stage_start = [this](const std::string &stage_id, int round_before) {
            stage_starts.emplace_back(stage_id, round_before);
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

// ---------------------------------------------------------------------------
// 10. DSL v1.1：断点候选序列（需求 7；三条逐点钉死）
// ---------------------------------------------------------------------------
void expect_breakpoint_sequence(double current, const std::vector<double> &want,
                                const std::string &tag) {
    Rig rig;
    rig.base = jxl_base(current);
    rig.policy = dsl_policy("target 90\nmax_rounds 30\n"
                            "stage distance-down\n"
                            "  search distance from current down to 0.1 step 0.1 "
                            "breakpoint 1 step 2 breakpoint 10 step 5 max_rounds 30\n",
                            "jxl");
    (void)rig.run();
    const std::vector<double> v = rig.values();
    check(
        v.size() == want.size(), "bp/" + tag,
        "候选数 = " + std::to_string(v.size()) + "（期望 " + std::to_string(want.size()) + "）：" +
            [&] {
                std::string s;
                for (const double x : v)
                    s += num_text(x) + " ";
                return s;
            }());
    for (std::size_t i = 0; i < v.size() && i < want.size(); ++i)
        check(std::fabs(v[i] - want[i]) < 1e-9, "bp/" + tag + "/" + std::to_string(i),
              num_text(v[i]) + " != " + num_text(want[i]));
}

void case_breakpoint_sequences() {
    // current=1.0（JXL Default 的默认 distance）：0.9 … 0.1（9 轮；1.0 归 1–0.1 段，绝不能
    // 1.0→(-1) 越级后钳到 0.1）。
    expect_breakpoint_sequence(1.0, {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1}, "c1");
    // current=4.0：2, 1, 0.9 … 0.1（11 轮）。
    expect_breakpoint_sequence(4.0, {2.0, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1}, "c4");
    // current=12.0：10, 8, 6, 4, 2, 1, 0.9 … 0.1（15 轮；跨断点钳到 10/1 本身）。
    expect_breakpoint_sequence(
        12.0, {10.0, 8.0, 6.0, 4.0, 2.0, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1}, "c12");
}

// ---------------------------------------------------------------------------
// 11. DSL v1.1：显式方向「只降不升/只升不降」→ 0 轮（不是错误）
// ---------------------------------------------------------------------------
void case_direction_zero_rounds() {
    {
        Rig rig; // current=0.5，down to 0.8：起点已在终点下方 ⇒ 0 轮
        rig.base = jxl_base(0.5);
        rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage down\n"
                                "  search distance from current down to 0.8 step 0.1\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 1, "dir0/down_rounds", std::to_string(rig.inputs.size()));
        check(!rig.seen_records.empty() && rig.seen_records.front().stage == "init",
              "dir0/down_init",
              rig.seen_records.empty() ? "missing" : rig.seen_records.front().stage);
        check(r.ok && r.reason == pp::kAdaptiveReasonStageExhausted, "dir0/down_reason", r.reason);
    }
    {
        Rig rig; // current=2.0，up to 1.0：起点已在终点上方 ⇒ 0 轮
        rig.base = jxl_base(2.0);
        rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage up\n"
                                "  search distance from current up to 1.0 step 0.1\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 1, "dir0/up_rounds", std::to_string(rig.inputs.size()));
        check(r.ok && r.reason == pp::kAdaptiveReasonStageExhausted, "dir0/up_reason", r.reason);
    }
    {
        Rig rig; // 显式起点 == 终点（显式方向）：0 轮，非 search_noop
        rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage down\n"
                                "  search distance from 3.0 down to 3.0 step 0.5\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 1, "dir0/equal_rounds", std::to_string(rig.inputs.size()));
        check(r.ok, "dir0/equal_ok", "显式方向下 from==to 不得报错");
    }
}

// ---------------------------------------------------------------------------
// 12. DSL v1.1：动作级 max_rounds 覆盖 linear 阶段上界（16），仍受全局 max_rounds
// ---------------------------------------------------------------------------
void case_action_max_rounds_override() {
    { // 29 个候选：动作级 max_rounds 32 放行（>linear 默认阶段上界 16）；无动作级则被 16 截断。
        Rig rig;
        rig.base = jxl_base(3.0);
        rig.policy = dsl_policy("target 100\nmax_rounds 32\nstage scan\n"
                                "  search distance from current down to 0.1 step 0.1 "
                                "max_rounds 32\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 29, "cap/override", std::to_string(rig.inputs.size()));
        check(r.ok && r.reason == pp::kAdaptiveReasonStageExhausted, "cap/override_reason",
              r.reason);
    }
    { // 无动作级 max_rounds → linear 阶段上界 16 截断（旧行为不变）。
        Rig rig;
        rig.base = jxl_base(3.0);
        rig.policy = dsl_policy("target 100\nmax_rounds 32\nstage scan\n"
                                "  search distance from current down to 0.1 step 0.1\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 16, "cap/default16", std::to_string(rig.inputs.size()));
        check(r.ok, "cap/default16_ok", "16 轮后仍应输出最后一轮");
    }
    { // 全局 max_rounds 仍是最外层上界（动作级不能突破它）。
        Rig rig;
        rig.base = jxl_base(3.0);
        rig.policy = dsl_policy("target 100\nmax_rounds 10\nstage scan\n"
                                "  search distance from current down to 0.1 step 0.1 "
                                "max_rounds 32\n",
                                "jxl");
        const pp::AdaptiveRunResult r = rig.run();
        check(rig.inputs.size() == 10, "cap/global", std::to_string(rig.inputs.size()));
        check(r.reason == pp::kAdaptiveReasonMaxRounds, "cap/global_reason", r.reason);
    }
}

// ---------------------------------------------------------------------------
// 13. C 段（需求 1）：on_plan / on_stage_start / 逐轮 effective_params / selected 文本
// ---------------------------------------------------------------------------
void case_plan_stage_effective_fields() {
    Rig rig;
    rig.policy = dsl_policy("target 99\nmax_rounds 8\n"
                            "stage scan\n  search distance from current to 0.9 step 0.05\n"
                            "stage tweak\n  set epf 1\n",
                            "jxl");
    const pp::AdaptiveRunResult r = rig.run();
    // on_plan：恰好一次、字段齐全。
    check(rig.plans.size() == 1, "hook/plan_count", std::to_string(rig.plans.size()));
    if (!rig.plans.empty()) {
        const pp::AdaptivePlanInfo &p = rig.plans.front();
        check(p.policy_version == 1 && std::fabs(p.target - 99.0) < 1e-9 && p.max_rounds == 8,
              "hook/plan_facts", "policy_version/target/max_rounds");
        check(p.stage_count == 2 && p.stage_ids.size() == 2 && p.stage_ids[0] == "scan" &&
                  p.stage_ids[1] == "tweak",
              "hook/plan_stages",
              p.stage_ids.size() == 2 ? p.stage_ids[0] + "," + p.stage_ids[1] : "missing");
        check(p.output == "last" && p.policy_sha.size() == 64 && p.policy_sha == r.policy_sha,
              "hook/plan_sha", p.policy_sha);
    }
    // on_stage_start：每个 stage 一次；round_before = 已执行轮数（scan 0 / tweak 2）。
    check(rig.stage_starts.size() == 2, "hook/stage_count",
          std::to_string(rig.stage_starts.size()));
    if (rig.stage_starts.size() == 2) {
        check(rig.stage_starts[0].first == "scan" && rig.stage_starts[0].second == 0, "hook/stage0",
              rig.stage_starts[0].first + "/" + std::to_string(rig.stage_starts[0].second));
        check(rig.stage_starts[1].first == "tweak" && rig.stage_starts[1].second == 2,
              "hook/stage1",
              rig.stage_starts[1].first + "/" + std::to_string(rig.stage_starts[1].second));
    }
    // 逐轮 effective_params / candidate_path：search 轮 = "distance=<值>"，set 轮 = "epf=1"。
    bool saw_search_effective = false;
    for (const pp::AdaptiveRoundRecord &rec : rig.seen_records) {
        check(!rec.candidate_path.empty(), "hook/round_candidate", "candidate_path 为空");
        if (rec.stage == "scan") {
            saw_search_effective = rec.effective_params.rfind("distance=", 0) == 0 &&
                                   rec.effective_params.find(rec.value) != std::string::npos;
        } else if (rec.stage == "tweak") {
            check(rec.effective_params == "epf=1", "hook/round_set_effective",
                  rec.effective_params);
        }
    }
    check(saw_search_effective, "hook/round_search_effective", "search 轮 effective_params 不符");
    // selected 文本 + 中文终止原因（selected = 第 3 轮 set stage）。
    check(r.selected_params_text == "epf=1", "hook/selected_text", r.selected_params_text);
    check(!r.reason_detail.empty() && r.reason_detail.find("阶段耗尽") != std::string::npos,
          "hook/reason_detail", r.reason_detail);
    // init 基线轮：effective_params = 相对 base 的差量（空 = 未改写任何键）。
    {
        Rig init_rig;
        init_rig.base = jxl_base(0.5);
        init_rig.policy = dsl_policy("target 90\nmax_rounds 8\nstage down\n"
                                     "  search distance from current down to 0.8 step 0.1\n",
                                     "jxl");
        (void)init_rig.run();
        check(init_rig.seen_records.size() == 1 &&
                  init_rig.seen_records.front().effective_params.empty(),
              "hook/init_effective",
              init_rig.seen_records.empty() ? "missing"
                                            : init_rig.seen_records.front().effective_params);
    }
}

// ---------------------------------------------------------------------------
// 14. C 段：set 阶段有效参数字符串（profile 具名档位，不是 __ 保留键）
// ---------------------------------------------------------------------------
void case_set_stage_effective_params() {
    Rig rig;
    const pp::FormatDef *f = pp::find_format("heif");
    if (f == nullptr) {
        check(false, "set_eff/heif", "heif 格式缺失");
        return;
    }
    rig.base = pp::default_params(*f, "x265", "runtime", false);
    rig.target.format_id = "heif";
    rig.target.backend_id = "x265";
    rig.target.tech_id = "runtime";
    rig.target.out_bitdepth = 10;
    rig.target.final_path = "out/fake.heic";
    rig.policy = dsl_policy("target 99\nmax_rounds 4\n"
                            "stage chroma-444\n  set chroma 444\n  set profile Main444_10Still\n",
                            "heif");
    const pp::AdaptiveRunResult r = rig.run();
    check(rig.inputs.size() == 1, "set_eff/rounds",
          std::to_string(rig.inputs.size()) + " reason=" + r.reason + " error=" + r.error);
    check(!rig.seen_records.empty(), "set_eff/record", "无逐轮记录");
    if (!rig.seen_records.empty()) {
        const pp::AdaptiveRoundRecord &rec = rig.seen_records.front();
        check(rec.effective_params == "chroma=444,profile=Main444_10Still", "set_eff/text",
              rec.effective_params);
        check(rec.candidate_path == "cand-r1", "set_eff/candidate", rec.candidate_path);
    }
    check(r.selected_params_text == "chroma=444,profile=Main444_10Still", "set_eff/selected",
          r.selected_params_text);
}

// ---------------------------------------------------------------------------
// 28. M12 需求 5：JPEG 出厂 default 策略的候选阶梯（current=3.0，域 [0.1, 3.0]）
//     策略 = `search distance from current down to 0.1 step 0.1 breakpoint 1 step 1
//     max_rounds 20`：1–3 段步长 1（粗扫）→ 1 以下 0.1 精扫；共 11 个候选：
//     2, 1, 0.9, 0.8, …, 0.1（逐个断言，含跨断点钳位与端点钳位）。
//     该阶梯的**可达性**依赖 format_tables 的 jpegli distance 域 = [0.1, 3.0]（M12 从 0.3
//     放宽）：候选 0.2/0.1 若越界会在每轮 validate 处被拒（本用例用默认校验链，不注入 hook）。
// ---------------------------------------------------------------------------
void case_jpeg_default_ladder() {
    // —— 主用例：current = 3.0（工厂 JPEG default 的 distance）→ 11 候选 ——
    {
        Rig rig;
        rig.base = jpeg_base(3.0);
        rig.target = jpeg_target();
        rig.key = "distance";
        rig.policy = dsl_policy(kJpegDefaultPolicy, "jpeg");
        check(rig.policy.target == 90.0 && rig.policy.max_rounds == 20, "jpeg_ladder/policy_facts",
              "target/max_rounds 不符");
        const pp::AdaptiveRunResult r = rig.run();
        const std::vector<double> v = rig.values();
        const double want[] = {2.0, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1};
        check(
            v.size() == 11, "jpeg_ladder/count",
            "候选数 = " + std::to_string(v.size()) + "（期望 11）：" + [&] {
                std::string s;
                for (const double x : v)
                    s += num_text(x) + " ";
                return s;
            }());
        for (std::size_t i = 0; i < v.size() && i < 11; ++i)
            check(std::fabs(v[i] - want[i]) < 1e-9, "jpeg_ladder/value" + std::to_string(i),
                  num_text(v[i]) + " != " + num_text(want[i]));
        check(r.ok && r.reason == pp::kAdaptiveReasonStageExhausted, "jpeg_ladder/reason",
              "reason=" + r.reason);
        // 逐轮 hook 的全局 max_rounds = 20（11 个候选不被 20 截断；也不被线性阶段上界 16 截断
        // ——动作级 max_rounds 20 已显式放宽）。
        check(!rig.seen_max_rounds.empty() && rig.seen_max_rounds.front() == 20,
              "jpeg_ladder/max_rounds",
              rig.seen_max_rounds.empty() ? "hook 未调用"
                                          : std::to_string(rig.seen_max_rounds.front()));
        // output last：选定 = 最后一轮成功编码（distance 0.1）。
        check(r.selected_round == 11 && std::fabs(r.final_score - 80.0) < 1e-9,
              "jpeg_ladder/selected", "selected=" + std::to_string(r.selected_round));
        check(rig.commits.size() == 1 && rig.commits.front() == "cand-r11", "jpeg_ladder/commit",
              rig.commits.empty() ? "no commit" : rig.commits.front());
        check(rig.discards.size() == 10, "jpeg_ladder/discard",
              std::to_string(rig.discards.size()));
        // 断点值 1.0 必须被尝试（不得被步长跨过）：候选序列里恰有一个 1.0。
        int ones = 0;
        for (const double x : v)
            if (std::fabs(x - 1.0) < 1e-9)
                ++ones;
        check(ones == 1, "jpeg_ladder/breakpoint_tried", "1.0 出现次数 = " + std::to_string(ones));
    }
    // —— 跨断点钳位：current = 2.5 → 下一档 1.5，再下一档 0.5 会跨过断点 1.0 ⇒ 先钳到 1.0 ——
    {
        Rig rig;
        rig.base = jpeg_base(2.5);
        rig.target = jpeg_target();
        rig.key = "distance";
        rig.policy = dsl_policy(kJpegDefaultPolicy, "jpeg");
        (void)rig.run();
        const std::vector<double> v = rig.values();
        const double want[] = {1.5, 1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1};
        check(v.size() == 11, "jpeg_clamp/count",
              "候选数 = " + std::to_string(v.size()) + "（期望 11）");
        for (std::size_t i = 0; i < v.size() && i < 11; ++i)
            check(std::fabs(v[i] - want[i]) < 1e-9, "jpeg_clamp/value" + std::to_string(i),
                  num_text(v[i]) + " != " + num_text(want[i]));
    }
    // —— 跨断点钳位（起点在断点上方不足一步）：current = 1.05 → 首档钳到断点 1.0 ——
    //     若直接按断点段步长 1 走会跳到 0.05（跨过 1.0），执行器必须先把 1.0 作为候选。
    {
        Rig rig;
        rig.base = jpeg_base(1.05);
        rig.target = jpeg_target();
        rig.key = "distance";
        rig.policy = dsl_policy(kJpegDefaultPolicy, "jpeg");
        (void)rig.run();
        const std::vector<double> v = rig.values();
        const double want[] = {1.0, 0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1};
        check(v.size() == 10, "jpeg_endpoint/count",
              "候选数 = " + std::to_string(v.size()) + "（期望 10）");
        for (std::size_t i = 0; i < v.size() && i < 10; ++i)
            check(std::fabs(v[i] - want[i]) < 1e-9, "jpeg_endpoint/value" + std::to_string(i),
                  num_text(v[i]) + " != " + num_text(want[i]));
    }
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
    std::printf("CASE 23) 断点候选序列（需求 7）\n");
    case_breakpoint_sequences();
    std::printf("CASE 24) 显式方向 0 轮\n");
    case_direction_zero_rounds();
    std::printf("CASE 25) 动作级 max_rounds 覆盖阶段上界\n");
    case_action_max_rounds_override();
    std::printf("CASE 26) C 段 hooks/effective 字段\n");
    case_plan_stage_effective_fields();
    std::printf("CASE 27) set 阶段有效参数字符串\n");
    case_set_stage_effective_params();
    std::printf("CASE 28) JPEG 出厂 default 策略候选阶梯（M12 需求 5）\n");
    case_jpeg_default_ladder();

    std::printf("ADAPTIVE-EXEC %s (%d failures)\n", g_failed == 0 ? "OK" : "FAIL", g_failed);
    return g_failed == 0 ? 0 : 1;
}
