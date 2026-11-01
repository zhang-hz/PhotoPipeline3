// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10 / W3-T3：自适应策略核心单测（DSL 解析/渲染/校验/AST/注册表/默认库）
//
// 设计契约：docs/v0.4.0-design.md §9.2（DSL v1 EBNF）/ §9.3（AST + JSON 导入）/ §9.4（允许/
// 禁止目标键）/ §9.7（默认策略库）/ §9.9-1（验收探针）。覆盖：
//   * 设计三例解析 → AST 逐字段；
//   * 每格式默认库解析 → AST 逐字段（HEIF/AVIF/JXL/WebP/JPEG；PNG/TIFF/BMP 无）；
//   * 非法样例错误码/行号/中文消息（错误码全表机械断言）；
//   * DSL→AST→DSL 幂等；JSON AST 导入同语义；
//   * 禁止表全表断言（每一项都硬拒绝）+ 允许面数据驱动（例 3 的 x265:psy-rd 合法）。
//
// 注：JSON AST 导入的 Qt 解析在 `ui/format_preset_io.cpp`（core 零 Qt）；本测试按仓内先例
// 直接包含该 .cpp（test_icons/test_meta_format 同做法），断言跑的就是生产代码。
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "core/adaptive.h"
#include "core/format_preset.h"
#include "core/params.h"
// NOLINTNEXTLINE(bugprone-suspicious-include)：测试白盒包含（同既有模式）
#include "ui/format_preset_io.cpp" // 单实现（JSON AST 导入）

namespace {

int g_fail = 0;

void fail(const std::string &c, const std::string &d) {
    std::printf("FAIL %s: %s\n", c.c_str(), d.c_str());
    ++g_fail;
}

void check(bool ok, const std::string &c, const std::string &d) {
    if (!ok)
        fail(c, d);
}

void case_begin(const char *name) {
    std::fprintf(stderr, "CASE %s\n", name);
    std::fflush(stderr);
}

bool code_registered(const std::string &code) {
    for (const std::string_view k : pp::adaptive_error_codes())
        if (k == code)
            return true;
    return false;
}

void check_code(const pp::AdaptiveError &e, const std::string &expect, const std::string &c,
                const std::string &name) {
    check(e.code == expect, c,
          name + "：错误码期望 " + expect + "，实际 " + e.code + "（" + e.message + "）");
    if (!e.code.empty())
        check(code_registered(e.code), c, name + "：错误码未登记：" + e.code);
    check(!e.message.empty(), c, name + "：message 不得为空");
    check(!e.hint.empty(), c, name + "：hint 不得为空");
}

// 非法样例：解析/校验必须失败，并给出期望的 code/line。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
void expect_invalid(const std::string &name, const std::string &text, const std::string &format,
                    const std::string &code, int line) {
    const std::string c = "invalid/" + name;
    pp::AdaptivePolicy p;
    pp::AdaptiveError e;
    const bool ok = pp::parse_and_validate_adaptive_policy(text, format, p, &e);
    check(!ok, c, "应当失败（实际通过）");
    if (ok)
        return;
    check_code(e, code, c, name);
    check(e.line == line, c,
          "行号期望 " + std::to_string(line) + "，实际 " + std::to_string(e.line) + "（" +
              e.message + "）");
}

// 合法样例：parse + validate 通过。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
pp::AdaptivePolicy expect_valid(const std::string &name, const std::string &text,
                                const std::string &format) {
    const std::string c = "valid/" + name;
    pp::AdaptivePolicy p;
    pp::AdaptiveError e;
    const bool ok = pp::parse_and_validate_adaptive_policy(text, format, p, &e);
    check(ok, c, "应当通过，实际失败：" + e.code + " " + e.message + " / " + e.hint);
    return p;
}

const char *kExample1 = R"(# 我的 HEIF 照片策略：先提质量，再上 4:4:4
version 1
target 90
max_rounds 8
output last
on_metric_unavailable stop

stage quality-up
  search quality from current to 98 step 2

stage chroma-444
  set chroma 444
  set profile Main444_10Still
)";

const char *kExample2 = R"(version 1
target 90
max_rounds 10
output last
plateau 0.05

stage distance-scan
  search distance from current to 0.05 step 0.1

stage distance-bisect
  search distance from current to 0.05 step 0.02 method bisect
)";

const char *kExample3 = R"(target 90
stage fallback
  set chroma 444
  set x265:psy-rd 2.0
  set x265:aq-mode 2
)";

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    using namespace pp;

    // 1) 设计三例：解析 → AST 逐字段
    case_begin("1) 设计三例解析 → AST");
    {
        const std::string c = "example1";
        const AdaptivePolicy p = expect_valid("example1", kExample1, "heif");
        check(p.version == 1 && p.target == 90.0 && p.max_rounds == 8, c, "全局选项");
        check(p.output == AdaptiveOutput::Last &&
                  p.on_metric_unavailable == AdaptiveMetricUnavailable::Stop && p.plateau == 0.0,
              c, "输出/评分不可用/plateau 默认");
        check(p.stages.size() == 2, c, "stage 数");
        if (p.stages.size() == 2) {
            const AdaptiveStage &s0 = p.stages[0];
            check(s0.id == "quality-up" && s0.actions.size() == 1, c, "stage0 id/动作数");
            const AdaptiveAction &a0 = s0.actions[0];
            check(a0.kind == AdaptiveActionKind::Search && a0.key == "quality" && a0.from.current &&
                      a0.to == 98.0 && a0.step == 2.0 && a0.method == AdaptiveMethod::Linear &&
                      a0.max_rounds == 0,
                  c, "stage0 search 字段");
            const AdaptiveStage &s1 = p.stages[1];
            check(s1.id == "chroma-444" && s1.actions.size() == 2, c, "stage1 id/动作数");
            check(s1.actions[0].kind == AdaptiveActionKind::Set && s1.actions[0].key == "chroma" &&
                      std::get_if<int64_t>(&s1.actions[0].value) != nullptr &&
                      *std::get_if<int64_t>(&s1.actions[0].value) == 444,
                  c, "set chroma 444（裸数字 → int64）");
            check(s1.actions[1].key == "profile" &&
                      std::get_if<std::string>(&s1.actions[1].value) != nullptr &&
                      *std::get_if<std::string>(&s1.actions[1].value) == "Main444_10Still",
                  c, "set profile 档位名");
            check(a0.line == 9 && s1.actions[0].line == 12 && s1.actions[1].line == 13, c,
                  "动作行号定位（实际 " + std::to_string(a0.line) + "/" +
                      std::to_string(s1.actions[0].line) + "/" +
                      std::to_string(s1.actions[1].line) + "）");
        }

        const std::string c2 = "example2";
        const AdaptivePolicy q = expect_valid("example2", kExample2, "jxl");
        check(q.max_rounds == 10 && q.plateau == 0.05, c2, "max_rounds/plateau");
        check(q.stages.size() == 2, c2, "stage 数");
        if (q.stages.size() == 2) {
            check(q.stages[0].actions[0].step == 0.1 &&
                      q.stages[0].actions[0].method == AdaptiveMethod::Linear,
                  c2, "stage0");
            check(q.stages[1].actions[0].step == 0.02 &&
                      q.stages[1].actions[0].method == AdaptiveMethod::Bisect,
                  c2, "stage1 bisect");
        }

        const std::string c3 = "example3";
        const AdaptivePolicy r = expect_valid("example3", kExample3, "heif");
        check(r.version == 1 && r.target == 90.0 && r.max_rounds == 8, c3, "省略项取默认值");
        check(r.stages.size() == 1 && r.stages[0].actions.size() == 3, c3, "stage/动作数");
        if (r.stages.size() == 1 && r.stages[0].actions.size() == 3) {
            const AdaptiveAction &psy = r.stages[0].actions[1];
            check(psy.key == "x265:psy-rd" && std::get_if<double>(&psy.value) != nullptr &&
                      *std::get_if<double>(&psy.value) == 2.0,
                  c3, "set x265:psy-rd 2.0（float）");
            const AdaptiveAction &aq = r.stages[0].actions[2];
            check(aq.key == "x265:aq-mode" && std::get_if<int64_t>(&aq.value) != nullptr &&
                      *std::get_if<int64_t>(&aq.value) == 2,
                  c3, "set x265:aq-mode 2（int）");
        }
    }

    // 2) 默认策略库：每格式解析 → AST 逐字段；PNG/TIFF/BMP 无
    case_begin("2) 默认策略库（§9.7）");
    {
        struct Expect {
            const char *format;
            int stages;
            int actions;
            int max_rounds;
            double plateau;
        };
        const Expect expects[] = {
            {"heif", 2, 3, 8, 0.0}, {"avif", 3, 3, 8, 0.0}, {"jxl", 2, 2, 10, 0.05},
            {"webp", 2, 2, 8, 0.0}, {"jpeg", 1, 1, 8, 0.0},
        };
        for (const Expect &e : expects) {
            const std::string c = std::string("default/") + e.format;
            const AdaptiveDefaultPolicy *def = default_adaptive_policy_for(e.format);
            check(def != nullptr, c, "缺少默认策略");
            if (def == nullptr)
                continue;
            check(def->text.find('#') != std::string::npos, c, "出厂策略应含人类可读注释");
            const AdaptivePolicy p =
                expect_valid(std::string("default-") + e.format, def->text, e.format);
            check(p.target == 90.0, c, "target 90");
            check(p.max_rounds == e.max_rounds, c, "max_rounds = " + std::to_string(e.max_rounds));
            check(p.plateau == e.plateau, c, "plateau");
            check(static_cast<int>(p.stages.size()) == e.stages, c, "stage 数");
            int actions = 0;
            for (const AdaptiveStage &s : p.stages)
                actions += static_cast<int>(s.actions.size());
            check(actions == e.actions, c, "动作数");
            // DSL→AST→DSL 幂等（默认库文本含注释，渲染丢弃注释后仍语义等价）
            const std::string r1 = render_adaptive_policy(p);
            AdaptivePolicy p2;
            AdaptiveError e2;
            check(parse_adaptive_policy(r1, p2, &e2), c, "render 后应可再解析：" + e2.message);
            check(render_adaptive_policy(p2) == r1, c, "render→parse→render 幂等");
        }
        check(default_adaptive_policy_for("png") == nullptr, "default/png", "PNG 无默认策略");
        check(default_adaptive_policy_for("tiff") == nullptr, "default/tiff", "TIFF 无默认策略");
        check(default_adaptive_policy_for("bmp") == nullptr, "default/bmp", "BMP 无默认策略");
        check(default_adaptive_policy_for("gif") == nullptr, "default/gif", "未知格式无默认策略");
        check(default_adaptive_policies().size() == 5, "default/count", "默认库 5 条");
    }

    // 3) DSL→AST→DSL 幂等（三例 + 带引号字符串）
    case_begin("3) DSL→AST→DSL 幂等");
    {
        for (const char *text : {kExample1, kExample2, kExample3}) {
            const std::string c = "idempotent";
            AdaptivePolicy p;
            AdaptiveError e;
            check(parse_adaptive_policy(text, p, &e), c, std::string("parse: ") + e.message);
            const std::string r1 = render_adaptive_policy(p);
            AdaptivePolicy p2;
            check(parse_adaptive_policy(r1, p2, &e), c, std::string("reparse: ") + e.message);
            check(render_adaptive_policy(p2) == r1, c, "render 不稳定");
        }
        // 字符串 vs 裸数字：渲染必须带引号才能保持 AST 类型（string ≠ int64）。
        AdaptivePolicy p;
        AdaptiveError e;
        check(parse_adaptive_policy("stage a\n  set chroma \"444\"\n", p, &e), "quoted",
              "parse: " + e.message);
        const std::string r = render_adaptive_policy(p);
        check(r.find("set chroma \"444\"") != std::string::npos, "quoted", "字符串必须带引号渲染");
        AdaptivePolicy p2;
        check(parse_adaptive_policy(r, p2, &e), "quoted", "reparse: " + e.message);
        check(std::get_if<std::string>(&p2.stages[0].actions[0].value) != nullptr, "quoted",
              "往返后仍是字符串");
        // 裸词枚举渲染为裸词（set tune psnr）
        AdaptivePolicy q;
        check(parse_and_validate_adaptive_policy("stage a\n  set tune psnr\n", "heif", q, &e),
              "bare", "parse tune: " + e.message);
        check(render_adaptive_policy(q).find("set tune psnr") != std::string::npos, "bare",
              "枚举裸词应保持裸词");
    }

    // 4) 非法样例：错误码 + 行号（覆盖 parse 与 validate 两段）
    case_begin("4) 非法样例错误码/行号");
    {
        expect_invalid("empty", "", "heif", "empty_policy", 1);
        expect_invalid("too-large", std::string(17000, '#') + "\n", "heif", "policy_too_large", 1);
        expect_invalid("target-0", "target 0\nstage a\n  set chroma 444\n", "heif", "target_range",
                       1);
        expect_invalid("target-101", "target 101\nstage a\n  set chroma 444\n", "heif",
                       "target_range", 1);
        expect_invalid("target-nan", "target abc\nstage a\n  set chroma 444\n", "heif",
                       "invalid_option_value", 1);
        expect_invalid("max-rounds-0", "max_rounds 0\nstage a\n  set chroma 444\n", "heif",
                       "max_rounds_range", 1);
        expect_invalid("max-rounds-33", "max_rounds 33\nstage a\n  set chroma 444\n", "heif",
                       "max_rounds_range", 1);
        expect_invalid("version-2", "version 2\nstage a\n  set chroma 444\n", "heif",
                       "unsupported_version", 1);
        expect_invalid("version-abc", "version abc\nstage a\n  set chroma 444\n", "heif",
                       "invalid_option_value", 1);
        expect_invalid("output-middle", "output middle\nstage a\n  set chroma 444\n", "heif",
                       "invalid_option_value", 1);
        expect_invalid("omu-maybe", "on_metric_unavailable maybe\nstage a\n  set chroma 444\n",
                       "heif", "invalid_option_value", 1);
        expect_invalid("plateau-neg", "plateau -1\nstage a\n  set chroma 444\n", "heif",
                       "plateau_range", 1);
        expect_invalid("duplicate-option", "target 90\ntarget 91\nstage a\n  set chroma 444\n",
                       "heif", "duplicate_option", 2);
        expect_invalid("option-after-stage", "stage a\n  set chroma 444\ntarget 90\n", "heif",
                       "option_after_stage", 3);
        expect_invalid("unknown-statement", "bogus 1\nstage a\n  set chroma 444\n", "heif",
                       "unknown_statement", 1);
        expect_invalid("bad-stage-id", "stage Bad\n  set chroma 444\n", "heif", "bad_stage_id", 1);
        expect_invalid("duplicate-stage", "stage a\nstage a\n  set chroma 444\n", "heif",
                       "duplicate_stage", 2);
        expect_invalid("action-outside-stage", "set chroma 444\n", "heif", "action_outside_stage",
                       1);
        expect_invalid("no-stage", "target 90\n", "heif", "no_stage", 0);
        expect_invalid("empty-stage", "stage a\n", "heif", "empty_stage", 1);
        expect_invalid("empty-stage-mid", "stage a\n  set chroma 444\nstage b\n", "heif",
                       "empty_stage", 3);
        expect_invalid("search-no-step", "stage a\n  search quality from current to 98\n", "heif",
                       "search_syntax", 2);
        expect_invalid("search-bad-method",
                       "stage a\n  search quality from current to 98 step 1 method foo\n", "heif",
                       "bad_method", 2);
        expect_invalid("search-bad-from", "stage a\n  search quality from abc to 98 step 1\n",
                       "heif", "bad_number", 2);
        expect_invalid("search-stray",
                       "stage a\n  search quality from current to 98 step 1 method linear x\n",
                       "heif", "stray_token", 2);
        expect_invalid("search-action-max-rounds",
                       "stage a\n  search quality from current to 98 step 1 max_rounds 0\n", "heif",
                       "action_max_rounds", 2);
        expect_invalid("set-missing-value", "stage a\n  set chroma\n", "heif", "missing_value", 2);
        expect_invalid("unterminated-string", "stage a\n  set tune \"psnr\n", "heif",
                       "unterminated_string", 2);
        expect_invalid("bad-escape", "stage a\n  set tune \"p\\q\"\n", "heif", "bad_escape", 2);
        // —— validate 段（语法正确、语义非法）——
        expect_invalid("step-zero", "stage a\n  search quality from current to 98 step 0\n", "heif",
                       "step_range", 2);
        expect_invalid("search-from-range", "stage a\n  search quality from 101 to 98 step 1\n",
                       "heif", "search_range", 2);
        expect_invalid("search-to-range", "stage a\n  search quality from current to 101 step 1\n",
                       "heif", "search_range", 2);
        expect_invalid("search-noop", "stage a\n  search quality from 90 to 90 step 1\n", "heif",
                       "search_noop", 2);
        expect_invalid("not-searchable", "stage a\n  search chroma from current to 2 step 1\n",
                       "heif", "not_searchable", 2);
        expect_invalid("unknown-search-key", "stage a\n  search bogus from current to 2 step 1\n",
                       "heif", "unknown_key", 2);
        expect_invalid("search-key-wrong-format",
                       "stage a\n  search x265:crf from current to 10 step 1\n", "jpeg",
                       "key_not_in_format", 2);
        expect_invalid("forbidden-lossless", "stage a\n  set lossless true\n", "jxl",
                       "forbidden_key", 2);
        expect_invalid("forbidden-reserved", "stage a\n  set __heif_profile 20\n", "heif",
                       "forbidden_key", 2);
        expect_invalid("tech", "stage a\n  set tech runtime\n", "heif", "tech_not_supported", 2);
        expect_invalid("set-type", "stage a\n  set quality 50.5\n", "jpeg", "set_value_type", 2);
        expect_invalid("set-range", "stage a\n  set quality 500\n", "jpeg", "set_value_range", 2);
        expect_invalid("enum-value", "stage a\n  set chroma 999\n", "jpeg", "enum_value", 2);
        expect_invalid("profile-wrong-format", "stage a\n  set profile Main444_10Still\n", "jxl",
                       "profile_key_format", 2);
        expect_invalid("profile-value", "stage a\n  set profile Nope\n", "heif", "profile_value",
                       2);
        expect_invalid("profile-unavailable", "stage a\n  set profile Main10Still\n", "heif",
                       "profile_unavailable", 2);
        expect_invalid("profile-avif-signal", "stage a\n  set profile Rext444_10\n", "avif",
                       "profile_value", 2);
        expect_invalid("unknown-set-key", "stage a\n  set bogus 1\n", "jpeg", "unknown_key", 2);
        expect_invalid("output-best", "output best\nstage a\n  set chroma 444\n", "heif",
                       "output_best_unsupported", 0);
        // col 定位：search 缺 step 时错误列号 ≥1。
        {
            AdaptivePolicy p;
            AdaptiveError e;
            check(!parse_and_validate_adaptive_policy(
                      "stage a\n  search quality from current to 98\n", "heif", p, &e),
                  "col", "应当失败");
            check(e.line == 2 && e.col >= 1, "col",
                  "行列定位异常：line=" + std::to_string(e.line) + " col=" + std::to_string(e.col));
        }
    }

    // 5) 禁止表全表断言（§9.4 硬拒绝）
    case_begin("5) 禁止表全表断言");
    {
        const std::string c = "forbidden-all";
        check(adaptive_forbidden_keys().size() >= 30, c, "禁止表条目过少");
        const char *required[] = {"lossless",
                                  "resampling",
                                  "upsampling_mode",
                                  "intensity_target",
                                  "min_nits",
                                  "relative_to_max_display",
                                  "linear_below",
                                  "keep_invisible",
                                  "noise",
                                  "photon_noise",
                                  "disable_perceptual_heuristics",
                                  "expert_options",
                                  "codestream_level",
                                  "buffering",
                                  "output_mode",
                                  "frame_name",
                                  "intrinsic_size"};
        for (const char *key : required) {
            bool found = false;
            for (const std::string_view k : adaptive_forbidden_keys())
                if (k == key)
                    found = true;
            check(found, c, std::string("禁止表缺项：") + key);
        }
        for (const std::string_view key : adaptive_forbidden_keys()) {
            const std::string text = "stage a\n  set " + std::string(key) + " 1\n";
            AdaptivePolicy p;
            AdaptiveError e;
            const bool ok = parse_and_validate_adaptive_policy(text, "jxl", p, &e);
            check(!ok, c, "禁止键应硬拒绝：" + std::string(key));
            if (!ok) {
                check(e.code == "forbidden_key", c,
                      "禁止键 " + std::string(key) + " 错误码 = " + e.code + "（" + e.message +
                          "）");
                check(e.line == 2, c, "禁止键 " + std::string(key) + " 行号");
            }
        }
        // `__` 前缀一律硬拒绝（profile 用伪键 "profile" 表达）
        for (const char *key : {"__heif_profile", "__lossless", "__heif_grid", "__aom:tune"}) {
            const std::string text = std::string("stage a\n  set ") + key + " 1\n";
            AdaptivePolicy p;
            AdaptiveError e;
            check(!parse_and_validate_adaptive_policy(text, "heif", p, &e), c,
                  std::string("保留键应硬拒绝：") + key);
            check(e.code == "forbidden_key", c, std::string("保留键错误码：") + key);
        }
    }

    // 6) 允许面：数据驱动（目录键 − 禁止表）+ search 白名单
    case_begin("6) 允许面/search 白名单");
    {
        // 设计 §9.2 例 3 的键（x265 透传）必须合法。
        expect_valid("heif-passthrough", "stage a\n  set x265:psy-rd 2.0\n  set x265:aq-mode 2\n",
                     "heif");
        expect_valid("heif-category-keys", "stage a\n  set chroma 444\n  set tune psnr\n", "heif");
        expect_valid("heif-profile", "stage a\n  set profile Main444_10Still\n", "heif");
        expect_valid("avif-profile", "stage a\n  set profile High\n", "avif");
        expect_valid("jxl-enum", "stage a\n  set epf 0\n  set dots true\n  set gaborish false\n",
                     "jxl");
        expect_valid("jxl-modular", "stage a\n  set modular_predictor 5\n", "jxl");
        expect_valid("webp-int", "stage a\n  set method 6\n", "webp");
        // search 白名单：数值质量键可搜索；目录内其它数值键（effort）不可搜索。
        expect_valid("search-distance", "stage a\n  search distance from 1.0 to 0.5 step 0.1\n",
                     "jxl");
        expect_invalid("search-effort", "stage a\n  search effort from current to 9 step 1\n",
                       "jxl", "not_searchable", 2);
        // 数值键 set 也合法（§9.4 第一类）
        expect_valid("set-numeric", "stage a\n  set distance 0.5\n", "jxl");
    }

    // 7) 值归一（校验/执行器共用单源）
    case_begin("7) adaptive_coerce_set_value");
    {
        const std::string c = "coerce";
        const FormatDef *jpeg = find_format("jpeg");
        check(jpeg != nullptr, c, "jpeg format");
        const ParamDef *quality_mode =
            jpeg != nullptr ? lookup_param_def(*jpeg, "jpegli", "dct", "quality_mode") : nullptr;
        check(quality_mode != nullptr, c, "quality_mode def");
        const ParamDef *chroma =
            jpeg != nullptr ? lookup_param_def(*jpeg, "jpegli", "dct", "chroma") : nullptr;
        check(chroma != nullptr, c, "chroma def");
        const ParamDef *dct =
            jpeg != nullptr ? lookup_param_def(*jpeg, "jpegli", "dct", "dct_method") : nullptr;
        check(dct != nullptr, c, "dct_method def");
        ParamValue out;
        std::string why;
        if (quality_mode != nullptr) {
            check(adaptive_coerce_set_value(ParamValue{std::string("quality")}, *quality_mode, out,
                                            &why) &&
                      std::get_if<std::string>(&out) != nullptr &&
                      *std::get_if<std::string>(&out) == "quality",
                  c, "字符串枚举");
            check(!adaptive_coerce_set_value(ParamValue{int64_t{1}}, *quality_mode, out, &why), c,
                  "字符串枚举不接受数字");
        }
        if (chroma != nullptr) {
            check(adaptive_coerce_set_value(ParamValue{int64_t{444}}, *chroma, out, &why) &&
                      *std::get_if<std::string>(&out) == "444",
                  c, "int → 字符串枚举（444）");
            check(!adaptive_coerce_set_value(ParamValue{int64_t{999}}, *chroma, out, &why), c,
                  "非法枚举值");
        }
        if (dct != nullptr) {
            check(adaptive_coerce_set_value(ParamValue{std::string("IFAST")}, *dct, out, &why) &&
                      *std::get_if<int64_t>(&out) == 1,
                  c, "中文/英文标签匹配（IFAST）");
            check(adaptive_coerce_set_value(ParamValue{std::string("1")}, *dct, out, &why) &&
                      *std::get_if<int64_t>(&out) == 1,
                  c, "数字字符串 → int 枚举");
            check(adaptive_coerce_set_value(ParamValue{int64_t{2}}, *dct, out, &why) &&
                      *std::get_if<int64_t>(&out) == 2,
                  c, "int 枚举");
        }
        // Int / Float / Bool
        const ParamDef *effort = nullptr;
        const FormatDef *jxl = find_format("jxl");
        if (jxl != nullptr)
            effort = lookup_param_def(*jxl, "libjxl", "vardct", "effort");
        check(effort != nullptr, c, "effort def");
        if (effort != nullptr) {
            check(adaptive_coerce_set_value(ParamValue{2.0}, *effort, out, &why) &&
                      *std::get_if<int64_t>(&out) == 2,
                  c, "整值 double → Int");
            check(!adaptive_coerce_set_value(ParamValue{2.5}, *effort, out, &why), c,
                  "小数 → Int 拒绝");
            check(adaptive_coerce_set_value(ParamValue{std::string("3")}, *effort, out, &why) &&
                      *std::get_if<int64_t>(&out) == 3,
                  c, "数字字符串 → Int");
        }
        const ParamDef *bool_def = nullptr;
        const FormatDef *webp = find_format("webp");
        if (webp != nullptr)
            bool_def = lookup_param_def(*webp, "libwebp", "lossy", "sharp_yuv");
        check(bool_def != nullptr, c, "webp bool def");
        if (bool_def != nullptr) {
            check(
                adaptive_coerce_set_value(ParamValue{std::string("true")}, *bool_def, out, &why) &&
                    std::get_if<bool>(&out) != nullptr && *std::get_if<bool>(&out),
                c, "字符串 true → Bool");
            check(!adaptive_coerce_set_value(ParamValue{int64_t{1}}, *bool_def, out, &why), c,
                  "Int 不能当 Bool");
        }
    }

    // 8) profile 伪参数：22 个档位名 ↔ 序号；大小写不敏感
    case_begin("8) profile 档位名表");
    {
        const std::string c = "profile";
        for (int64_t i = 0; i < 22; ++i) {
            const std::string name = adaptive_profile_name(i);
            check(!name.empty(), c, "档位名缺失：" + std::to_string(i));
            if (name.empty())
                continue;
            int64_t back = -1;
            check(adaptive_profile_from_value(ParamValue{name}, back) && back == i, c,
                  "档位名往返：" + name);
            check(adaptive_profile_from_value(ParamValue{std::string("MAIN")}, back) && back == 1,
                  c, "档位名大小写不敏感");
        }
        int64_t v = -1;
        check(adaptive_profile_from_value(ParamValue{int64_t{20}}, v) && v == 20, c, "数字序号");
        check(adaptive_profile_from_value(ParamValue{std::string("20")}, v) && v == 20, c,
              "数字字符串序号");
        check(!adaptive_profile_from_value(ParamValue{int64_t{22}}, v), c, "越界拒绝");
        check(!adaptive_profile_from_value(ParamValue{std::string("Nope")}, v), c, "未知名拒绝");
        check(adaptive_profile_name(22).empty(), c, "越界名返回空");
    }

    // 9) JSON AST 导入同语义（§9.3）
    case_begin("9) JSON AST 导入");
    {
        const std::string c = "json-import";
        const std::string json = R"({
  "version": 1,
  "target": 90,
  "max_rounds": 8,
  "output": "last",
  "on_metric_unavailable": "stop",
  "plateau": 0,
  "stages": [
    {"id": "quality-up", "actions": [
      {"kind": "search", "key": "quality", "from": "current", "to": 98, "step": 2}
    ]},
    {"id": "chroma-444", "actions": [
      {"kind": "set", "key": "chroma", "value": 444},
      {"kind": "set", "key": "profile", "value": "Main444_10Still"}
    ]}
  ]
})";
        AdaptivePolicy from_json;
        AdaptiveError e;
        check(pp::ui::parse_adaptive_policy_json(json, "heif", from_json, &e), c,
              "JSON 导入失败：" + e.code + " " + e.message + " / " + e.hint);
        AdaptivePolicy from_dsl;
        check(parse_and_validate_adaptive_policy(kExample1, "heif", from_dsl, &e), c,
              "DSL 解析失败：" + e.message);
        check(render_adaptive_policy(from_json) == render_adaptive_policy(from_dsl), c,
              "JSON AST 与 DSL 语义不一致");
        check(from_json.stages.size() == 2 && from_json.stages[1].actions.size() == 2, c,
              "JSON stage/动作数");
        // 错误面：JSON 语法 / 字段类型 / 语义（禁止键）
        AdaptivePolicy bad;
        check(!pp::ui::parse_adaptive_policy_json("{\"stages\": [", "heif", bad, &e), c,
              "坏 JSON 应失败");
        check(e.code == "json_syntax", c, "坏 JSON code = " + e.code);
        check(!pp::ui::parse_adaptive_policy_json(R"({"target":90})", "heif", bad, &e), c,
              "缺 stages 应失败");
        check(e.code == "json_field", c, "缺 stages code = " + e.code);
        check(
            !pp::ui::parse_adaptive_policy_json(
                R"({"stages":[{"id":"a","actions":[{"kind":"set","key":"lossless","value":true}]}]})",
                "jxl", bad, &e),
            c, "禁止键 JSON 应失败");
        check(e.code == "forbidden_key", c, "禁止键 JSON code = " + e.code);
        check(
            !pp::ui::parse_adaptive_policy_json(
                R"({"stages":[{"id":"a","actions":[{"kind":"set","key":"chroma","value":[1]}]}]})",
                "heif", bad, &e),
            c, "value 非标量应失败");
        check(e.code == "json_field", c, "value 非标量 code = " + e.code);
    }

    std::printf("ADAPTIVE-POLICY %s (%d failures)\n", g_fail == 0 ? "OK" : "FAIL", g_fail);
    return g_fail == 0 ? 0 : 1;
}
