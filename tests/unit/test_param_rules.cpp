// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: M8 联动规则引擎（Stage B1；docs/m8-param-control-map.md）
//
// 覆盖：
//   1. 规则表不变量（每行有格式/动作/文案；格式/后端合法；动作合法）；
//   2. 锚点闭合（driver/target 键在参数目录 / 既有静态表 / libheif 原生面 / 引擎锚点表里）；
//   3. 目录 rule_ids ↔ 规则表覆盖（X7/X8/S5 登记为不落规则的已知项）；
//   4. ≥12 条正/反例：G3 无损（四后端）、G4 RC 互斥、X/A/S/J 依赖与警告、跨后端前缀；
//   5. preflight == evaluate().errors；旧 cross_validate 签名行为/文案零改动，
//      加性重载 = 旧通道 + 新引擎 errors。
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/param_catalog/anchors.inc"
#include "codecs/param_catalog/catalog.h"
#include "core/param_rules.h"
#include "core/params.h"

namespace pp::rules {
// 诊断面（实现见 src/core/param_rules.cpp）。接口冻结头 core/param_rules.h 只声明
// evaluate/preflight（B0 冻结，B1 不改）；测试以相同 extern 声明消费规则表快照。
// 每行 = {id, format, backend, action, drivers, targets, message}（drivers/targets 逗号分隔）。
std::vector<std::vector<std::string>> rule_table_dump();
} // namespace pp::rules

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

std::string join(const std::vector<std::string> &v) {
    std::string out;
    for (const std::string &s : v)
        out += (out.empty() ? "" : ",") + s;
    return out;
}

bool contains(const std::vector<std::string> &v, const std::string &needle) {
    return std::any_of(v.begin(), v.end(), [&needle](const std::string &s) {
        return s.find(needle) != std::string::npos;
    });
}

bool has_key(const std::set<std::string> &s, const std::string &key) { return s.count(key) != 0; }

// —— 期望构造器（Case 复用）——
// NOLINTBEGIN(bugprone-exception-escape)：这些 std::function 构造器在断言消息里
// 拼接字符串，分配失败会抛 bad_alloc；测试 main 有顶层 catch，属于测试工具链噪声。
using Check = std::function<bool(const pp::rules::RuleState &, std::string &)>;

Check no_errors() {
    return [](const pp::rules::RuleState &st, std::string &why) {
        if (st.errors.empty())
            return true;
        why = "errors=[" + join(st.errors) + "]";
        return false;
    };
}

Check err_has(const std::string &needle) {
    return [needle](const pp::rules::RuleState &st, std::string &why) {
        if (contains(st.errors, needle))
            return true;
        why = "errors=[" + join(st.errors) + "] 不含 '" + needle + "'";
        return false;
    };
}

Check warn_has(const std::string &needle) {
    return [needle](const pp::rules::RuleState &st, std::string &why) {
        if (contains(st.warnings, needle))
            return true;
        why = "warnings=[" + join(st.warnings) + "] 不含 '" + needle + "'";
        return false;
    };
}

Check hidden_has(const std::string &key) {
    return [key](const pp::rules::RuleState &st, std::string &why) {
        if (has_key(st.hidden, key))
            return true;
        why = "hidden 缺 " + key;
        return false;
    };
}

Check disabled_has(const std::string &key) {
    return [key](const pp::rules::RuleState &st, std::string &why) {
        if (has_key(st.disabled, key))
            return true;
        why = "disabled 缺 " + key;
        return false;
    };
}

Check locked_int(const std::string &key, int64_t value) {
    return [key, value](const pp::rules::RuleState &st, std::string &why) {
        const auto it = st.locked.find(key);
        if (it == st.locked.end()) {
            why = "locked 缺 " + key;
            return false;
        }
        const int64_t *v = std::get_if<int64_t>(&it->second);
        if (!v || *v != value) {
            why = "locked[" + key + "] 不是 int " + std::to_string(value);
            return false;
        }
        return true;
    };
}

Check locked_bool(const std::string &key, bool value) {
    return [key, value](const pp::rules::RuleState &st, std::string &why) {
        const auto it = st.locked.find(key);
        if (it == st.locked.end()) {
            why = "locked 缺 " + key;
            return false;
        }
        const bool *v = std::get_if<bool>(&it->second);
        if (!v || *v != value) {
            why = "locked[" + key + "] 不是 bool " + (value ? "true" : "false");
            return false;
        }
        return true;
    };
}

Check locked_str(const std::string &key, const std::string &value) {
    return [key, value](const pp::rules::RuleState &st, std::string &why) {
        const auto it = st.locked.find(key);
        if (it == st.locked.end()) {
            why = "locked 缺 " + key;
            return false;
        }
        const std::string *v = std::get_if<std::string>(&it->second);
        if (!v || *v != value) {
            why = "locked[" + key + "] 不是 \"" + value + "\"";
            return false;
        }
        return true;
    };
}
// NOLINTEND(bugprone-exception-escape)

// —— 用例模型 ——
struct Case {
    const char *name;
    const char *format = "";
    const char *backend = "";
    const char *tech = "";
    int bitdepth = 0;
    int width = 0;
    int height = 0;
    bool lossless = false;
    std::function<void(pp::ParamSet &)> setup = [](pp::ParamSet &) {};
    std::vector<Check> checks;
};

pp::ParamContext make_ctx(const Case &c) {
    pp::ParamContext ctx;
    ctx.format_id = c.format;
    ctx.backend_id = c.backend;
    ctx.tech_id = c.tech;
    ctx.bitdepth = c.bitdepth;
    ctx.width = c.width;
    ctx.height = c.height;
    ctx.lossless = c.lossless;
    return ctx;
}

// —— 锚点闭合用的键集合 ——
std::set<std::string> static_table_keys() {
    std::set<std::string> keys;
    for (const pp::FormatDef &f : pp::static_formats())
        for (const pp::BackendDef &b : f.backends)
            for (const pp::TechDef &t : b.techs)
                for (const pp::ParamDef &p : t.params)
                    keys.insert(p.key);
    return keys;
}

std::set<std::string> catalog_keys() {
    static const char *const kPairs[][2] = {
        {"heif", "x265"}, {"avif", "libaom"}, {"avif", "svt-av1"}, {"jxl", "libjxl"}};
    std::set<std::string> keys;
    for (const auto &pair : kPairs)
        for (const pp::catalog::CatalogRow &r : pp::catalog::catalog(pair[0], pair[1]))
            keys.insert(r.def.key);
    return keys;
}

// libheif 运行时内省面（**原生键**；design F4/F5/F6 冻结的 7 + 15 + 10 键，含 UI 隐藏项）。
// 规则表只引用其中会被写进 ParamSet 的键（目录行 x265:/aom: 另由 catalog_keys 覆盖）。
std::set<std::string> native_libheif_keys() {
    return {"quality",        "lossless",       "preset",
            "tune",           "tu-intra-depth", "complexity",
            "chroma",         "realtime",       "speed",
            "threads",        "min-q",          "max-q",
            "alpha-quality",  "alpha-min-q",    "alpha-max-q",
            "lossless-alpha", "auto-tiles",     "enable-intrabc",
            "tile-rows",      "tile-cols",      "qp"};
}

bool engine_in_anchors(std::string_view engine, const char *const *anchors, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (engine == anchors[i])
            return true;
    return false;
}

// 锚点闭合：key 必须可定位（目录键 / 静态表键 / libheif 原生键 / x265·aom 引擎锚点）。
bool anchor_closed(const std::string &key, const std::set<std::string> &cat,
                   const std::set<std::string> &stat, const std::set<std::string> &native,
                   std::string &where) {
    if (key.rfind("__", 0) == 0)
        return true; // 只读/保留键不是规则驱动键（规则表本就不引用）
    if (has_key(cat, key)) {
        where = "catalog";
        return true;
    }
    if (has_key(stat, key)) {
        where = "static";
        return true;
    }
    if (has_key(native, key)) {
        where = "libheif-native";
        return true;
    }
    if (key.rfind("x265:", 0) == 0) {
        const std::string engine = key.substr(5);
        if (engine_in_anchors(engine, pp::catalog::kX265Anchors, pp::catalog::kX265AnchorCount) ||
            (engine.rfind("no-", 0) == 0 &&
             engine_in_anchors(engine.substr(3), pp::catalog::kX265Anchors,
                               pp::catalog::kX265AnchorCount))) {
            where = "x265-anchor";
            return true;
        }
        return false;
    }
    if (key.rfind("aom:", 0) == 0) {
        if (engine_in_anchors(key.substr(4), pp::catalog::kAomAnchors,
                              pp::catalog::kAomAnchorCount)) {
            where = "aom-anchor";
            return true;
        }
        return false;
    }
    if (key.rfind("svt:", 0) == 0) {
        where = "svt-prefix(禁止)";
        return false; // svt: 不是合法透传通道（S6）
    }
    return false;
}

std::vector<std::string> split_commas(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (ch == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}

// 每个用例的通用不变量：preflight == evaluate().errors；错误消息非空。
void run_case(const Case &c) {
    pp::ParamSet values;
    if (c.setup)
        c.setup(values);
    const pp::ParamContext ctx = make_ctx(c);
    const pp::rules::RuleState st = pp::rules::evaluate(ctx, values);
    for (const Check &chk : c.checks) {
        std::string why;
        if (!chk(st, why))
            fail(c.name, why);
    }
    // preflight 与 evaluate().errors 同源（param_rules.h 契约）。
    const std::vector<std::string> pre = pp::rules::preflight(ctx, values);
    check(pre == st.errors, c.name,
          "preflight=[" + join(pre) + "] != evaluate.errors=[" + join(st.errors) + "]");
}

// ======================= 1) 规则表不变量 + 锚点闭合 + 覆盖 =======================
void test_rule_table_invariants() {
    const std::vector<std::vector<std::string>> rows = pp::rules::rule_table_dump();
    const std::string c = "rule-table";
    check(!rows.empty(), c, "规则表为空");

    // 合法 format/backend 组合（从静态格式表派生；目录后端 heif/x265、avif/libaom|svt-av1、
    // jxl/libjxl 都在静态表 backends 里）。
    std::set<std::string> formats = {""};
    std::set<std::pair<std::string, std::string>> combos;
    for (const pp::FormatDef &f : pp::static_formats()) {
        formats.insert(f.id);
        for (const pp::BackendDef &b : f.backends)
            combos.emplace(f.id, b.id);
    }
    const std::set<std::string> backends = {"", "x265", "libaom", "svt-av1", "libjxl"};
    const std::set<std::string> actions = {"hide", "disable", "lock", "warn", "error"};

    const std::set<std::string> cat = catalog_keys();
    const std::set<std::string> stat = static_table_keys();
    const std::set<std::string> native = native_libheif_keys();

    std::set<std::string> ids;
    std::size_t warn_or_error = 0;
    std::size_t locks = 0;
    for (const std::vector<std::string> &row : rows) {
        check(row.size() == 7, c, "规则行字段数 != 7");
        if (row.size() != 7)
            continue;
        const std::string &id = row[0];
        const std::string &format = row[1];
        const std::string &backend = row[2];
        const std::string &action = row[3];
        const std::string &drivers = row[4];
        const std::string &targets = row[5];
        const std::string &message = row[6];
        std::string ctx = c;
        ctx += "/";
        ctx += id;
        ctx += "(";
        ctx += format;
        ctx += "/";
        ctx += backend;
        ctx += ")";

        check(!id.empty(), ctx, "规则编号为空");
        check(formats.count(format) != 0, ctx, "非法 format '" + format + "'");
        check(backends.count(backend) != 0, ctx, "非法 backend '" + backend + "'");
        if (!format.empty() && !backend.empty()) {
            std::string combo_ctx = "format/backend 组合不在静态表：";
            combo_ctx += format;
            combo_ctx += "/";
            combo_ctx += backend;
            check(combos.count({format, backend}) != 0, ctx, combo_ctx);
        }
        check(actions.count(action) != 0, ctx, "非法 action '" + action + "'");
        check(!message.empty(), ctx, "规则文案为空");
        if (action == "warn" || action == "error") {
            ++warn_or_error;
            check(message.size() >= 4, ctx, "Warn/Error 文案过短");
        }
        if (action == "lock") {
            ++locks;
            check(!targets.empty(), ctx, "Lock 规则无目标键");
        }
        if (action == "hide" || action == "disable")
            check(!targets.empty() || !drivers.empty(), ctx, "Hide/Disable 规则无键");

        for (const std::string &key : split_commas(drivers)) {
            if (key.empty())
                continue;
            std::string where;
            check(anchor_closed(key, cat, stat, native, where), ctx, "driver 锚点不闭合：" + key);
        }
        for (const std::string &key : split_commas(targets)) {
            if (key.empty())
                continue;
            std::string where;
            check(anchor_closed(key, cat, stat, native, where), ctx, "target 锚点不闭合：" + key);
        }
        ids.insert(id);
    }
    check(warn_or_error >= 20, c, "Warn/Error 规则数偏少：" + std::to_string(warn_or_error));
    check(locks >= 4, c, "Lock 规则数偏少：" + std::to_string(locks));

    // 任务卡点名的规则族必须存在（B1 必做项 3 的覆盖清单）。
    static const char *const kRequired[] = {
        "G3", "G4", "X1", "X1B", "X2", "X3",  "X4",  "X6",  "X10", "X11", "X12", "X13", "A2",
        "A4", "A5", "A6", "A7",  "A9", "A10", "A12", "S1",  "S2",  "S4",  "S6",  "J3",  "J4",
        "J5", "J6", "J7", "J8",  "J9", "J10", "J11", "J12", "J13", "J17", "J18"};
    for (const char *required : kRequired)
        check(ids.count(required) != 0, c, std::string("规则族缺失：") + required);

    // 目录 rule_ids ↔ 规则表闭合（防"目录引用了不存在的规则"）。已知不落规则的三项：
    //   X7  = A16 裁定降级为 tooltip（依赖未暴露的 tu-inter-depth）；
    //   X8  = 依赖 libheif 自适应 CTU（逐文件、CTU 不进入 ParamContext）；
    //   S5  = 只读展示（threads 不暴露 / profile·level·tier 只读），无求值语义。
    static const std::set<std::string> kKnownNotRules = {"X7", "X8", "S5"};
    std::set<std::string> missing;
    for (const pp::FormatDef &f : pp::static_formats()) {
        for (const pp::BackendDef &b : f.backends) {
            for (const pp::catalog::CatalogRow &row : pp::catalog::catalog(f.id, b.id)) {
                for (const std::string &rid : split_commas(row.rule_ids)) {
                    if (rid.empty() || ids.count(rid) != 0 || kKnownNotRules.count(rid) != 0)
                        continue;
                    missing.insert(rid);
                }
            }
        }
    }
    check(missing.empty(), c,
          "目录引用的规则未实现：" +
              join(std::vector<std::string>(missing.begin(), missing.end())));
}

// ======================= 2) 正/反例（≥12 条机械求值用例） =======================
std::vector<Case> behavior_cases() {
    std::vector<Case> cases;

    // —— 干净集：四个后端默认态不得报错 ——
    cases.push_back({"clean/heif", "heif", "x265", "runtime", 8, 0, 0, false, {}, {no_errors()}});
    cases.push_back(
        {"clean/avif-libaom", "avif", "libaom", "runtime", 10, 0, 0, false, {}, {no_errors()}});
    cases.push_back(
        {"clean/avif-svt", "avif", "svt-av1", "runtime", 10, 0, 0, false, {}, {no_errors()}});
    cases.push_back({"clean/jxl",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     {},
                     {no_errors(), hidden_has("brotli_effort")}});

    // —— G3 无损：HEIF/x265 ——
    cases.push_back({"g3/heif-lossless",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     true,
                     {},
                     {no_errors(), hidden_has("quality"), hidden_has("x265:crf"),
                      locked_str("chroma", "444"), warn_has("G3")}});
    // —— G3 无损：AVIF/libaom ——
    cases.push_back({"g3/libaom-lossless",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     true,
                     {},
                     {no_errors(), hidden_has("quality"), locked_str("chroma", "444"),
                      locked_int("aom:aq-mode", 0), locked_bool("aom:enable-chroma-deltaq", false),
                      disabled_has("tune"), warn_has("G3")}});
    // —— G3/S2：SVT 无损 = 硬错误 ——
    cases.push_back({"g3/svt-lossless",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     true,
                     {},
                     {disabled_has("lossless"), err_has("S2")}});
    // —— G3 无损：JXL（distance 隐藏 / resampling=1 / modular_lossy_palette=false）——
    cases.push_back({"g3/jxl-lossless",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     true,
                     {},
                     {no_errors(), hidden_has("distance"), locked_int("resampling", 1),
                      locked_bool("modular_lossy_palette", false), warn_has("G3")}});
    // J2：无损 + uses_original_profile=false 必须被拦（库会拒绝）。
    cases.push_back({"g3/jxl-lossless-profile",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     true,
                     [](pp::ParamSet &s) { s["uses_original_profile"] = false; },
                     {err_has("J2")}});

    // —— G4/X1：x265 RC 三路径互斥 ——
    cases.push_back({"g4/x265-crf-qp",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:crf"] = 20.0;
                         s["x265:qp"] = 30;
                     },
                     {err_has("X1")}});
    cases.push_back({"g4/x265-crf-hides-quality",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["x265:crf"] = 20.0; },
                     {no_errors(), hidden_has("quality"), warn_has("G4")}});
    cases.push_back({"g4/libaom-cq-level",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["aom:cq-level"] = 30; },
                     {no_errors(), hidden_has("quality"), warn_has("A1")}});
    cases.push_back({"g4/svt-qp",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["qp"] = 30; },
                     {no_errors(), disabled_has("quality"), warn_has("S1")}});

    // —— X 规则：x265 依赖/警告 ——
    cases.push_back({"x/x1b-qpmin-qpmax",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:qpmin"] = 40;
                         s["x265:qpmax"] = 20;
                     },
                     {err_has("X1B")}});
    cases.push_back(
        {"x/x2-aq-mode-zero",
         "heif",
         "x265",
         "runtime",
         8,
         0,
         0,
         false,
         [](pp::ParamSet &s) {
             s["x265:aq-mode"] = 0;
             s["x265:aq-strength"] = 1.0;
             s["x265:rd-refine"] = true;
         },
         {no_errors(), disabled_has("x265:aq-strength"), disabled_has("x265:rd-refine")}});
    cases.push_back({"x/x3-psy-rdoq",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:rdoq-level"] = 0;
                         s["x265:psy-rdoq"] = 1.0;
                     },
                     {no_errors(), disabled_has("x265:psy-rdoq")}});
    cases.push_back({"x/x4-rd",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:rd"] = 2;
                         s["x265:cu-lossless"] = true;
                         s["x265:tskip"] = true;
                     },
                     {no_errors(), disabled_has("x265:cu-lossless"), disabled_has("x265:tskip")}});
    cases.push_back({"x/x4-rd-refine",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:rd"] = 4;
                         s["x265:rd-refine"] = true;
                     },
                     {no_errors(), disabled_has("x265:rd-refine")}});
    cases.push_back({"x/x6-ssim-rd",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:ssim-rd"] = true;
                         s["x265:psy-rd"] = 1.0;
                     },
                     {no_errors(), warn_has("X6")}});
    cases.push_back({"x/x10-x11-sao",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["x265:sao"] = false;
                         s["x265:limit-sao"] = true;
                         s["x265:selective-sao"] = 1;
                         s["x265:sao-non-deblock"] = true;
                     },
                     {no_errors(), disabled_has("x265:limit-sao"),
                      disabled_has("x265:selective-sao"), disabled_has("x265:sao-non-deblock")}});
    cases.push_back({"x/x12-high-tier",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["x265:high-tier"] = true; },
                     {no_errors(), disabled_has("x265:high-tier")}});
    cases.push_back({"x/x13-444-psy",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["chroma"] = std::string("444");
                         s["x265:psy-rd"] = 1.0;
                         s["x265:cbqpoffs"] = 3;
                     },
                     {no_errors(), warn_has("X13")}});
    cases.push_back({"x/x9-min-cu-size",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     100,
                     50,
                     false,
                     [](pp::ParamSet &s) { s["x265:min-cu-size"] = 16; },
                     {no_errors(), warn_has("X9")}});
    cases.push_back({"x/x15-psy",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["x265:psy-rdoq"] = 1.0; },
                     {no_errors(), warn_has("X15")}});

    // —— A 规则：libaom ——
    cases.push_back({"a/a4-auto-tiles",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["auto-tiles"] = true;
                         s["aom:tile-columns"] = 2;
                     },
                     {err_has("A4")}});
    cases.push_back({"a/a5-min-q",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["min-q"] = 40;
                         s["max-q"] = 20;
                     },
                     {err_has("A5")}});
    cases.push_back({"a/a5-cq-level",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["aom:cq-level"] = 64; },
                     {err_has("A5")}});
    cases.push_back({"a/a6-identity",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["aom:matrix-coefficients"] = std::string("identity");
                         s["chroma"] = std::string("420");
                     },
                     {err_has("A6")}});
    cases.push_back({"a/a7-deltaq",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["aom:deltaq-mode"] = 0;
                         s["aom:deltaq-strength"] = 100;
                     },
                     {no_errors(), warn_has("A7")}});
    cases.push_back({"a/a9-intra-tx",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["aom:use-intra-dct-only"] = true;
                         s["aom:use-intra-default-tx-only"] = true;
                     },
                     {no_errors(), warn_has("A9")}});
    cases.push_back({"a/a10-realtime",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["realtime"] = true; },
                     {no_errors(), warn_has("A10")}});
    cases.push_back({"a/a12-lossless-tune",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     true,
                     [](pp::ParamSet &s) { s["tune"] = std::string("iq"); },
                     {no_errors(), disabled_has("tune"), warn_has("A12")}});
    cases.push_back({"a/a8-qm",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["aom:enable-qm"] = false;
                         s["aom:qm-min"] = 2;
                     },
                     {no_errors(), disabled_has("aom:qm-min")}});

    // —— S 规则：SVT ——
    cases.push_back({"s/s4-tile-cols",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["tile-cols"] = 32; },
                     {err_has("S4")}});
    cases.push_back({"s/s4-tile-count",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["tile-rows"] = 8;
                         s["tile-cols"] = 32;
                     },
                     {err_has("S4")}});
    cases.push_back({"s/s3-speed",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["speed"] = 12; },
                     {no_errors(), warn_has("S3")}});
    cases.push_back({"s/s6-svt-prefix",
                     "avif",
                     "svt-av1",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["svt:speed"] = 8; },
                     {err_has("S6")}});
    cases.push_back({"s/s6-heif-svt-prefix",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["svt:speed"] = 8; },
                     {err_has("S6")}});

    // —— 跨后端前缀（C3/G1）——
    cases.push_back({"g1/heif-aom-prefix",
                     "heif",
                     "x265",
                     "runtime",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["aom:cq-level"] = 30; },
                     {err_has("跨后端")}});
    cases.push_back({"g1/avif-x265-prefix",
                     "avif",
                     "libaom",
                     "runtime",
                     10,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["x265:crf"] = 20.0; },
                     {err_has("跨后端")}});
    cases.push_back({"g1/jxl-prefix",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["x265:crf"] = 20.0; },
                     {err_has("x265:/aom:/svt:")}});

    // —— J 规则：JXL ——
    cases.push_back({"j/j3-upsampling",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["upsampling_mode"] = int64_t(0); },
                     {no_errors(), disabled_has("upsampling_mode")}});
    cases.push_back({"j/j4-center",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["group_order_center_x"] = 10; },
                     {no_errors(), disabled_has("group_order_center_x")}});
    cases.push_back({"j/j4-center-bounds",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     100,
                     100,
                     false,
                     [](pp::ParamSet &s) {
                         s["group_order"] = 1;
                         s["group_order_center_x"] = 100;
                     },
                     {err_has("J4")}});
    cases.push_back({"j/j6-effort11",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["effort"] = 11; },
                     {err_has("J6")}});
    cases.push_back({"j/j6-effort11-expert",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["effort"] = 11;
                         s["expert_options"] = true;
                     },
                     {no_errors()}});
    cases.push_back({"j/j7-perceptual",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["disable_perceptual_heuristics"] = true;
                         s["uses_original_profile"] = false;
                     },
                     {err_has("J7")}});
    cases.push_back({"j/j8-original-profile",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["uses_original_profile"] = true; },
                     {no_errors(), warn_has("J8")}});
    cases.push_back({"j/j9-lossy-modular",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     false,
                     {},
                     {no_errors(), hidden_has("modular_palette_colors"),
                      hidden_has("channel_colors_global_percent")}});
    cases.push_back({"j/j9-ma-tree",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["modular_ma_tree_learning_percent"] = 0.0; },
                     {err_has("J9")}});
    cases.push_back({"j/j11-predictor",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["modular_predictor"] = 4; },
                     {no_errors(), warn_has("J11")}});
    cases.push_back({"j/j10-color-transform",
                     "jxl",
                     "libjxl",
                     "modular",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["color_transform"] = std::string("YCoCg"); },
                     {no_errors(), warn_has("J10")}});
    cases.push_back({"j/j12-output-mode",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["output_mode"] = int64_t(2); },
                     {no_errors(), warn_has("J12")}});
    cases.push_back({"j/j13-frame-name",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["frame_name"] = std::string("hello"); },
                     {no_errors(), warn_has("J13")}});
    cases.push_back({"j/j13-frame-name-long",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["frame_name"] = std::string(1072, 'x'); },
                     {err_has("J13")}});
    cases.push_back({"j/j14-intrinsic",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["intrinsic_size"] = std::string("abc"); },
                     {err_has("J14")}});
    cases.push_back({"j/j14-intrinsic-half",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["intrinsic_size"] = std::string("100x0"); },
                     {err_has("J14")}});
    cases.push_back({"j/j15-buffering",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["buffering"] = int64_t(3); },
                     {err_has("J15")}});
    cases.push_back({"j/j17-hdr-partial",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["intensity_target"] = 255.0; },
                     {no_errors(), warn_has("J17")}});
    cases.push_back({"j/j17-hdr-full",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) {
                         s["intensity_target"] = 255.0;
                         s["min_nits"] = 0.1;
                         s["relative_to_max_display"] = true;
                         s["linear_below"] = 0.5;
                     },
                     {no_errors()}});
    cases.push_back({"j/j18-level5-16bit-lossless",
                     "jxl",
                     "libjxl",
                     "modular",
                     16,
                     0,
                     0,
                     true,
                     [](pp::ParamSet &s) { s["codestream_level"] = 5; },
                     {err_has("J18")}});
    cases.push_back({"j/j18-level10-16bit-lossless",
                     "jxl",
                     "libjxl",
                     "modular",
                     16,
                     0,
                     0,
                     true,
                     [](pp::ParamSet &s) { s["codestream_level"] = 10; },
                     {no_errors()}});
    cases.push_back({"j/j16-noise",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["noise"] = true; },
                     {no_errors(), warn_has("J16")}});
    cases.push_back({"j/j5-box-compress",
                     "jxl",
                     "libjxl",
                     "vardct",
                     8,
                     0,
                     0,
                     false,
                     [](pp::ParamSet &s) { s["box_compress"] = true; },
                     {no_errors()}});

    return cases;
}

// ======================= 3) cross_validate 两通道契约 =======================
void test_cross_validate_channels() {
    const std::string c = "cross-validate";
    // ① 旧签名行为/文案零改动（逐字；M2-T5 冻结文案）。
    pp::ParamSet w;
    w["qmin"] = int64_t(100);
    w["qmax"] = int64_t(50);
    const std::vector<std::string> old_msgs = pp::cross_validate(w, "webp", "lossy");
    check(old_msgs.size() == 1 && old_msgs[0] == "qmin 不能大于 qmax", c,
          "旧签名文案变化：[" + join(old_msgs) + "]");

    // ② 加性重载 = 旧通道 + 新引擎 errors（webp 无新规则 ⇒ 与旧通道逐字一致）。
    pp::ParamContext webp_ctx;
    webp_ctx.format_id = "webp";
    webp_ctx.backend_id = "libwebp";
    webp_ctx.tech_id = "lossy";
    check(pp::cross_validate(webp_ctx, w) == old_msgs, c,
          "webp 加性重载应与旧通道一致：[" + join(pp::cross_validate(webp_ctx, w)) + "]");

    // ③ JXL：加性重载把新引擎 J6 错误并入（旧签名不认识 backend/位深/无损上下文）。
    pp::ParamContext jxl_ctx;
    jxl_ctx.format_id = "jxl";
    jxl_ctx.backend_id = "libjxl";
    jxl_ctx.tech_id = "vardct";
    pp::ParamSet jxl;
    jxl["effort"] = 11;
    check(pp::cross_validate(jxl, "jxl", "vardct").empty(), c,
          "旧签名不应含 J6（新规则只经 ParamContext 重载）");
    check(contains(pp::cross_validate(jxl_ctx, jxl), "J6"), c,
          "加性重载应包含 J6：[" + join(pp::cross_validate(jxl_ctx, jxl)) + "]");

    // ④ 未知键通道保持旧语义（加性重载仍保留 "未知参数：" 消息）。
    pp::ParamSet u;
    u["zzz_bogus"] = int64_t(1);
    check(contains(pp::cross_validate(jxl_ctx, u), "未知参数：zzz_bogus"), c,
          "加性重载应保留旧通道的未知参数消息");
}

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    test_rule_table_invariants();

    const std::vector<Case> cases = behavior_cases();
    check(cases.size() >= 12, "cases/count", "正/反例数偏少：" + std::to_string(cases.size()));
    for (const Case &c : cases)
        run_case(c);

    test_cross_validate_channels();

    if (g_fail == 0)
        std::printf("test_param_rules: OK\n");
    else
        std::printf("test_param_rules: %d failure(s)\n", g_fail);
    return g_fail;
}
