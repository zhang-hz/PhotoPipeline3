// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — unit tests: M8 参数目录（Stage A 数据/形态/锚点/计数不变量）
// 手写断言；失败打印 "FAIL <case>: <detail>"，main 返回失败数。
#include <cstdio>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/param_catalog/anchors.inc"
#include "codecs/param_catalog/catalog.h"
#include "core/params.h"

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

bool in_anchors(const std::string &name, const char *const *arr, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        if (name == arr[i])
            return true;
    }
    return false;
}

// x265_param_parse 通用 no- 前缀：no-deblock 等价于 deblock=false。
bool in_x265_anchors(const std::string &name) {
    if (in_anchors(name, pp::catalog::kX265Anchors, pp::catalog::kX265AnchorCount))
        return true;
    return name.rfind("no-", 0) == 0 &&
           in_anchors(name.substr(3), pp::catalog::kX265Anchors, pp::catalog::kX265AnchorCount);
}

bool forbidden_engine(const std::string &backend, const std::string &engine) {
    static const std::set<std::string> x265_bad = {
        "annexb",          "input-csp",     "total-frames", "ctu",
        "log-level",       "log",           "input-res",    "pools",
        "frame-threads",   "preset",        "tune",         "profile",
        "qpfile",          "zonefile",      "abr-ladder",   "svt",
        "nalu-file",       "csv",           "stats",        "lambda-file",
        "analysis-save",   "analysis-load", "film-grain",   "dhdr10-info",
        "fovea-gaze-file", "zones",         "bitrate",      "vbv-maxrate",
        "vbv-bufsize",     "vbv-init",      "uhd-bd",       "dolby-vision-profile",
        "intra-refresh",   "pic-struct"};
    static const std::set<std::string> aom_bad = {"input-chroma-subsampling-x",
                                                  "input-chroma-subsampling-y",
                                                  "denoise-noise-level",
                                                  "denoise-block-size",
                                                  "enable-dnl-denoising",
                                                  "passes",
                                                  "film-grain-testing",
                                                  "film-grain-table",
                                                  "film-grain",
                                                  "tune",
                                                  "end-usage",
                                                  "usage",
                                                  "profile",
                                                  "bit-depth",
                                                  "lag-in-frames",
                                                  "kf-max-dist",
                                                  "kf-min-dist",
                                                  "full-still-picture-hdr",
                                                  "resize-mode",
                                                  "superres-mode",
                                                  "superres-denominator",
                                                  "superres-kf-denominator",
                                                  "superres-qthresh",
                                                  "superres-kf-qthresh"};
    return backend == "x265" ? x265_bad.count(engine) != 0 : aom_bad.count(engine) != 0;
}

struct Expected {
    const char *format;
    const char *backend;
    std::size_t total;
    std::size_t settable;
    std::size_t readonly;
};

constexpr Expected kExpected[] = {
    {"heif", "x265", 40, 36, 4},
    {"avif", "libaom", 57, 51, 6},
    {"avif", "svt-av1", 3, 0, 3},
    {"jxl", "libjxl", 16, 16, 0},
};

} // namespace

int main() { // NOLINT(bugprone-exception-escape) — 测试入口允许异常终止（非产品路径）
    check(pp::catalog::kX265AnchorCount >= 200, "anchors/x265",
          "x265 anchors too small: " + std::to_string(pp::catalog::kX265AnchorCount));
    check(pp::catalog::kAomAnchorCount >= 120, "anchors/aom",
          "aom anchors too small: " + std::to_string(pp::catalog::kAomAnchorCount));

    std::set<std::string> all_keys;
    std::size_t total_rows = 0;
    std::size_t total_settable = 0;
    std::size_t total_readonly = 0;

    for (const Expected &e : kExpected) {
        const auto &rows = pp::catalog::catalog(e.format, e.backend);
        const auto counts = pp::catalog::counts(e.format, e.backend);
        const std::string ctx = std::string(e.format) + "/" + e.backend;
        check(counts.total == e.total, "count/total",
              ctx + " total=" + std::to_string(counts.total) + " expect " +
                  std::to_string(e.total));
        check(counts.settable == e.settable, "count/settable",
              ctx + " settable=" + std::to_string(counts.settable) + " expect " +
                  std::to_string(e.settable));
        check(counts.readonly == e.readonly, "count/readonly",
              ctx + " readonly=" + std::to_string(counts.readonly) + " expect " +
                  std::to_string(e.readonly));
        check(rows.size() == e.total, "count/rows", ctx + " rows size mismatch");
        total_rows += counts.total;
        total_settable += counts.settable;
        total_readonly += counts.readonly;

        for (const pp::catalog::CatalogRow &r : rows) {
            const std::string row_ctx = ctx + ":" + r.def.key;
            check(!r.def.key.empty(), "row/key", row_ctx + " empty key");
            check(all_keys.insert(r.def.key).second, "row/duplicate", row_ctx + " duplicate key");
            check(!r.def.label.empty(), "row/label", row_ctx + " empty label");
            check(!r.reason.empty(), "row/reason", row_ctx + " empty reason");
            check(!r.def.group.empty() && pp::catalog::is_known_group(r.def.group), "row/group",
                  row_ctx + " bad group: " + r.def.group);
            check(!r.def.has_default, "row/has_default",
                  row_ctx + " catalog rows must not have default");
            check(pp::catalog::form_name(r.form) != "?", "row/form", row_ctx + " bad form");
            check(pp::catalog::role_name(r.role) != "?", "row/role", row_ctx + " bad role");
            check(pp::catalog::scope_name(r.scope) != "?", "row/scope", row_ctx + " bad scope");
            check(pp::catalog::type_name(r.type) != "?", "row/type", row_ctx + " bad type");

            if (r.scope == pp::catalog::Scope::Settable) {
                check(r.form != pp::catalog::Form::ReadOnly, "row/form",
                      row_ctx + " settable + readonly form");
                check(!r.def.tooltip.empty(), "row/tooltip",
                      row_ctx + " settable row lacks tooltip");
                const bool libheif_passthrough = std::string_view(e.backend) == "x265" ||
                                                 std::string_view(e.backend) == "libaom";
                if (libheif_passthrough) {
                    check(!r.engine_key.empty(), "row/engine",
                          row_ctx + " settable row lacks engine");
                    check(!forbidden_engine(e.backend, r.engine_key), "row/forbidden",
                          row_ctx + " forbidden engine " + r.engine_key);
                } else {
                    check(r.engine_key.empty(), "row/engine",
                          row_ctx + " non-libheif backend engine must be empty");
                }
                if (std::string_view(e.backend) == "x265") {
                    check(in_x265_anchors(r.engine_key), "row/anchor",
                          row_ctx + " engine not in x265 anchors: " + r.engine_key);
                } else if (std::string_view(e.backend) == "libaom") {
                    check(in_anchors(r.engine_key, pp::catalog::kAomAnchors,
                                     pp::catalog::kAomAnchorCount),
                          "row/anchor", row_ctx + " engine not in aom anchors: " + r.engine_key);
                }
            } else if (r.scope == pp::catalog::Scope::ReadOnly) {
                check(r.form == pp::catalog::Form::ReadOnly, "row/form",
                      row_ctx + " readonly scope must use readonly form");
                check(r.engine_key.empty(), "row/engine", row_ctx + " readonly row has engine");
                check(r.type == pp::catalog::Type::Enum, "row/type",
                      row_ctx + " readonly type must be enum");
            }

            switch (r.form) {
            case pp::catalog::Form::Number:
                check(r.type == pp::catalog::Type::Int || r.type == pp::catalog::Type::Float,
                      "form/type", row_ctx + " number requires int/float");
                check(r.def.lo <= r.def.hi, "form/range", row_ctx + " lo > hi");
                check(r.def.step > 0.0, "form/step", row_ctx + " step <= 0");
                break;
            case pp::catalog::Form::Dropdown:
            case pp::catalog::Form::Mode:
                check(r.type == pp::catalog::Type::Enum && !r.def.choices.empty(), "form/type",
                      row_ctx + " dropdown/mode requires enum choices");
                break;
            case pp::catalog::Form::Switch:
                check(r.type == pp::catalog::Type::Bool, "form/type",
                      row_ctx + " switch requires bool");
                break;
            case pp::catalog::Form::Text:
                check(r.type == pp::catalog::Type::Text, "form/type",
                      row_ctx + " text requires text");
                check(r.def.key == "frame_name" || r.def.key == "intrinsic_size", "form/text",
                      row_ctx + " unexpected text row");
                break;
            case pp::catalog::Form::ReadOnly:
                check(r.scope == pp::catalog::Scope::ReadOnly, "form/type",
                      row_ctx + " readonly form requires readonly scope");
                break;
            }
        }
    }

    check(all_keys.size() == total_rows, "count/unique", "duplicate keys across backends");
    check(total_rows == 116, "count/global",
          "total rows=" + std::to_string(total_rows) + " expect 116");
    check(total_settable == 103, "count/global",
          "settable=" + std::to_string(total_settable) + " expect 103");
    check(total_readonly == 13, "count/global",
          "readonly=" + std::to_string(total_readonly) + " expect 13");

    // 合并辅助：只返回可设置行，且 tech 过滤正确。
    check(pp::catalog::param_defs("jxl", "libjxl").size() == 16, "merge/jxl",
          "jxl param_defs size != 16");
    check(pp::catalog::param_defs("heif", "x265").size() == 36, "merge/x265",
          "x265 param_defs size != 36");
    for (const pp::catalog::CatalogRow &r : pp::catalog::catalog("heif", "x265")) {
        check(r.tech == pp::catalog::TechScope::Both, "merge/tech",
              "x265 rows must be tech=both: " + r.def.key);
    }

    // 键查询 + 不存在的键。
    check(pp::catalog::find_row("heif", "x265", "x265:crf") != nullptr, "lookup/hit",
          "x265:crf not found");
    check(pp::catalog::find_row("heif", "x265", "svt:speed") == nullptr, "lookup/miss",
          "svt:speed must not exist");
    check(pp::catalog::has_key("avif", "svt-av1", "__svt:profile"), "lookup/svt",
          "__svt:profile missing");

    if (g_fail != 0)
        std::printf("test_param_catalog: %d failure(s)\n", g_fail);
    return g_fail;
}
