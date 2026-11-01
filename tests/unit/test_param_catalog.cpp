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
#include "core/param_rules.h"
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

// W2-T1（C16）：HEIF 用户面普通键（enc_heif 消费；不是 x265 透传键，engine 必须为空）。
// 目录生成器与 enc_heif 用同一组键名；测试在此逐字锁定，防未来误当透传键处理。
bool heif_ui_key(const std::string &key) {
    return key == "heif_grid" || key == "heif_tile_w" || key == "heif_tile_h";
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
    {"heif", "x265", 43, 39, 4},
    {"avif", "libaom", 57, 51, 6},
    {"avif", "svt-av1", 3, 0, 3},
    // W2-T5（C22）：JXL 目录 16 → 14（删 noise/expert_options/uses_original_profile/
    // group_order_center_x/y/buffering，新增 gaborish/dots/patches 三态 + codestream_level 只读）。
    {"jxl", "libjxl", 14, 13, 1},
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
                if (libheif_passthrough && heif_ui_key(r.def.key)) {
                    check(r.engine_key.empty(), "row/engine",
                          row_ctx + " HEIF UI key must not have engine");
                } else if (libheif_passthrough) {
                    check(!r.engine_key.empty(), "row/engine",
                          row_ctx + " settable row lacks engine");
                    check(!forbidden_engine(e.backend, r.engine_key), "row/forbidden",
                          row_ctx + " forbidden engine " + r.engine_key);
                } else {
                    check(r.engine_key.empty(), "row/engine",
                          row_ctx + " non-libheif backend engine must be empty");
                }
                if (std::string_view(e.backend) == "x265" && !heif_ui_key(r.def.key)) {
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
    // W2-T1（HEIF grid +3）与 W2-T5（JXL 16→14）合并后的总数：116+3−2 = 117。
    check(total_rows == 117, "count/global",
          "total rows=" + std::to_string(total_rows) + " expect 117");
    check(total_settable == 103, "count/global",
          "settable=" + std::to_string(total_settable) + " expect 103");
    check(total_readonly == 14, "count/global",
          "readonly=" + std::to_string(total_readonly) + " expect 14");

    // 合并辅助：只返回可设置行，且 tech 过滤正确。
    check(pp::catalog::param_defs("jxl", "libjxl").size() == 13, "merge/jxl",
          "jxl param_defs size != 13");
    check(pp::catalog::param_defs("heif", "x265").size() == 39, "merge/x265",
          "x265 param_defs size != 39");
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

    // W2-T1（C16）：grid 行可见性联动 —— grid=关闭 时 tile 行隐藏；默认（未设置=自动）可见。
    pp::ParamContext ctx;
    ctx.format_id = "heif";
    ctx.backend_id = "x265";
    ctx.tech_id = "runtime";
    ctx.bitdepth = 8;
    ctx.width = 1024;
    ctx.height = 576;
    pp::ParamSet vals;
    vals["heif_grid"] = static_cast<int64_t>(0); // 关闭
    const pp::rules::RuleState off = pp::rules::evaluate(ctx, vals);
    check(off.hidden.count("heif_tile_w") == 1, "grid/visibility/hide_w",
          "grid=关闭 时 heif_tile_w 应隐藏");
    check(off.hidden.count("heif_tile_h") == 1, "grid/visibility/hide_h",
          "grid=关闭 时 heif_tile_h 应隐藏");
    check(off.hidden.count("heif_grid") == 0, "grid/visibility/grid_visible",
          "分块模式行自身不得隐藏");
    vals.clear();
    const pp::rules::RuleState auto_mode = pp::rules::evaluate(ctx, vals);
    check(auto_mode.hidden.count("heif_tile_w") == 0, "grid/visibility/default_visible",
          "默认（自动）时 tile 行应可见");
    check(auto_mode.errors.empty(), "grid/visibility/default_clean",
          "默认配置不得报错：" + std::to_string(auto_mode.errors.size()));

    // 合法手动 tile（Level=自动）→ 允许 + 警告（iPhone 接受度）+ 对齐提醒；无错误。
    vals["heif_grid"] = static_cast<int64_t>(2); // 总是
    vals["heif_tile_w"] = static_cast<int64_t>(1000);
    vals["heif_tile_h"] = static_cast<int64_t>(288);
    const pp::rules::RuleState manual = pp::rules::evaluate(ctx, vals);
    check(manual.errors.empty(), "grid/manual/no_error", "合法手动 tile 不应报错（Level=自动）");
    check(!manual.warnings.empty(), "grid/manual/warn", "手动 tile 应有对齐/兼容性提醒");

    // W2-T5（C22）：JXL 目录行治理的机械断言（键集合/三态/只读/类型/分组）。
    {
        // ① 删除的 12 个结构性 no-op 键不得出现在目录（也不再并入静态表）。
        const char *removed[] = {"keep_invisible",        "noise",          "expert_options",
                                 "uses_original_profile", "group_order",    "group_order_center_x",
                                 "group_order_center_y",  "progressive_ac", "qprogressive_ac",
                                 "progressive_dc",        "responsive",     "buffering"};
        for (const char *key : removed) {
            check(pp::catalog::find_row("jxl", "libjxl", key) == nullptr, "jxl/removed",
                  std::string("removed key still in catalog: ") + key);
        }
        // ② 三态：gaborish/dots/patches 在目录内、has_default=false、Switch、Vardct。
        for (const char *key : {"gaborish", "dots", "patches"}) {
            const pp::catalog::CatalogRow *r = pp::catalog::find_row("jxl", "libjxl", key);
            check(r != nullptr, "jxl/three-state/present", std::string("missing ") + key);
            if (r != nullptr) {
                check(!r->def.has_default, "jxl/three-state/has_default",
                      std::string(key) + " must be three-state (has_default=false)");
                check(r->def.type == pp::ParamType::Bool, "jxl/three-state/type",
                      std::string(key) + " must be Bool");
                check(r->form == pp::catalog::Form::Switch, "jxl/three-state/form",
                      std::string(key) + " must use three-state Switch");
                check(r->tech == pp::catalog::TechScope::Vardct, "jxl/three-state/tech",
                      std::string(key) + " must be VarDCT-only");
            }
        }
        // ③ codestream_level：只读行（实际 level 由库推导），目录不再有可设置行。
        const pp::catalog::CatalogRow *lvl =
            pp::catalog::find_row("jxl", "libjxl", "codestream_level");
        check(lvl != nullptr, "jxl/level/present", "codestream_level readonly row missing");
        if (lvl != nullptr) {
            check(lvl->scope == pp::catalog::Scope::ReadOnly, "jxl/level/scope",
                  "codestream_level must be ReadOnly");
            check(lvl->form == pp::catalog::Form::ReadOnly, "jxl/level/form",
                  "codestream_level must use ReadOnly form");
            check(lvl->def.type == pp::ParamType::Enum && !lvl->def.choices.empty(),
                  "jxl/level/type", "codestream_level must expose an enum value text");
        }
        // ④ 类型修正：channel_colors_* = Float（libjxl 只有 Float setter）。
        for (const char *key : {"channel_colors_global_percent", "channel_colors_group_percent"}) {
            const pp::catalog::CatalogRow *r = pp::catalog::find_row("jxl", "libjxl", key);
            check(r == nullptr, "jxl/channel-catalog",
                  std::string(key) + " 应在静态表（非目录），此处只断言静态表类型");
            const pp::FormatDef *f = pp::find_format("jxl");
            const pp::BackendDef *b = f ? pp::find_backend(*f, "libjxl") : nullptr;
            const pp::TechDef *t = b ? pp::find_tech(*b, "modular") : nullptr;
            const pp::ParamDef *p = t ? pp::find_param(*t, key) : nullptr;
            check(p != nullptr && p->type == pp::ParamType::Float, "jxl/channel-type",
                  std::string(key) + " must be Float");
        }
        // ⑤ color_transform 枚举收窄为 auto/YCoCg。
        {
            const pp::FormatDef *f = pp::find_format("jxl");
            const pp::BackendDef *b = f ? pp::find_backend(*f, "libjxl") : nullptr;
            const pp::TechDef *t = b ? pp::find_tech(*b, "modular") : nullptr;
            const pp::ParamDef *p = t ? pp::find_param(*t, "color_transform") : nullptr;
            check(p != nullptr && p->choices.size() == 2, "jxl/color-transform/enum",
                  "color_transform must have exactly auto/YCoCg");
            if (p != nullptr && p->choices.size() == 2) {
                check(p->choices[0].first == "auto" && p->choices[1].first == "YCoCg",
                      "jxl/color-transform/values", "color_transform choices must be auto/YCoCg");
            }
        }
        // ⑥ 实验/危险组：5 键同组 + 红字「禁止作为自适应目标」；HDR 4 键同组 + intensity 红字。
        for (const char *key :
             {"upsampling_mode", "output_mode", "disable_perceptual_heuristics"}) {
            const pp::catalog::CatalogRow *r = pp::catalog::find_row("jxl", "libjxl", key);
            check(r != nullptr && r->def.group == pp::catalog::kGroupDebug, "jxl/danger/group",
                  std::string(key) + " must be in debug/danger group");
            check(r != nullptr && r->def.tooltip.find("禁止作为自适应目标") != std::string::npos,
                  "jxl/danger/tooltip", std::string(key) + " tooltip must carry the deny marker");
        }
        for (const char *key :
             {"intensity_target", "min_nits", "relative_to_max_display", "linear_below"}) {
            const pp::catalog::CatalogRow *r = pp::catalog::find_row("jxl", "libjxl", key);
            check(r != nullptr && r->def.group == pp::catalog::kGroupPsy, "jxl/hdr/group",
                  std::string(key) + " must be in HDR/Psychovisual group");
        }
        {
            const pp::catalog::CatalogRow *it =
                pp::catalog::find_row("jxl", "libjxl", "intensity_target");
            check(it != nullptr && it->def.tooltip.find("会改变像素与评分") != std::string::npos,
                  "jxl/hdr/intensity-red", "intensity_target tooltip must warn about pixels/score");
        }
        // ⑦ brotli_effort 作用域 = Both（静态表两技术都声明），且仅 box_compress=1 可见。
        {
            const pp::FormatDef *f = pp::find_format("jxl");
            const pp::BackendDef *b = f ? pp::find_backend(*f, "libjxl") : nullptr;
            check(b != nullptr, "jxl/brotli/backend", "libjxl backend missing");
            if (b != nullptr) {
                for (const char *tech : {"vardct", "modular"}) {
                    const pp::TechDef *t = pp::find_tech(*b, tech);
                    const pp::ParamDef *p = t ? pp::find_param(*t, "brotli_effort") : nullptr;
                    check(p != nullptr, "jxl/brotli/scope",
                          std::string("brotli_effort missing in ") + tech);
                    if (p != nullptr) {
                        pp::ParamSet s;
                        check(!pp::eval_visible(*p, s), "jxl/brotli/hidden",
                              "brotli_effort must be hidden when box_compress is off");
                        s["box_compress"] = true;
                        check(pp::eval_visible(*p, s), "jxl/brotli/visible",
                              "brotli_effort must be visible when box_compress=1");
                    }
                }
            }
        }
    }

    if (g_fail != 0)
        std::printf("test_param_catalog: %d failure(s)\n", g_fail);
    return g_fail;
}
