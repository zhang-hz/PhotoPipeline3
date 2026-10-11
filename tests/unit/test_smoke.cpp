// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M0 smoke test: frozen format tables and ParamValue round-trip.
//
// Frozen contract: docs/m0-tasks.md §12.5. Hand-written assertions only (no third-party
// test framework); exit code 0 = all assertions passed, 1 = at least one failure.

#include <cstdio>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "core/params.h"
#include "core/types.h"

namespace {

int g_failed = 0;

void check(bool ok, const std::string &what) {
    if (!ok) {
        ++g_failed;
        std::printf("ASSERT FAILED: %s\n", what.c_str());
    }
}

template <typename T>
bool holds_value(const pp::ParamSet &ps, const std::string &key, const T &expect) {
    const auto it = ps.find(key);
    if (it == ps.end() || !std::holds_alternative<T>(it->second)) {
        return false;
    }
    return std::get<T>(it->second) == expect;
}

} // namespace

int main() {
    // 1. static_formats(): exactly the 8 frozen formats.
    const std::vector<pp::FormatDef> &fmts = pp::static_formats();
    check(fmts.size() == 8,
          "static_formats().size() == 8 (got " + std::to_string(fmts.size()) + ")");
    std::set<std::string> ids;
    for (const pp::FormatDef &f : fmts) {
        ids.insert(f.id);
    }
    const std::set<std::string> want = {"jpeg", "jxl", "png",  "tiff",
                                        "webp", "bmp", "heif", "avif"};
    check(ids == want, "format id set == {jpeg,jxl,png,tiff,webp,bmp,heif,avif}");

    // 2. every format has bitdepths and a known meta_path.
    for (const pp::FormatDef &f : fmts) {
        check(!f.bitdepths.empty(), f.id + ".bitdepths non-empty");
        const bool meta_ok = f.meta_path == "exiv2" || f.meta_path == "libheif" ||
                             f.meta_path == "jxl-box" || f.meta_path == "none";
        check(meta_ok,
              f.id + ".meta_path in {exiv2,libheif,jxl-box,none} (got " + f.meta_path + ")");
    }

    // 3. ParamSet / ParamValue round-trip.
    pp::ParamSet ps;
    ps["int"] = static_cast<int64_t>(-7);
    ps["float"] = 1.5;
    ps["bool"] = true;
    ps["string"] = std::string("值-utf8");
    ps["mono"] = std::monostate{};
    check(holds_value<int64_t>(ps, "int", -7), "ParamValue int64 round-trip");
    check(holds_value<double>(ps, "float", 1.5), "ParamValue double round-trip");
    check(holds_value<bool>(ps, "bool", true), "ParamValue bool round-trip");
    check(holds_value<std::string>(ps, "string", std::string("值-utf8")),
          "ParamValue string round-trip");
    check(holds_value<std::monostate>(ps, "mono", std::monostate{}),
          "ParamValue monostate round-trip");
    check(ps.size() == 5, "ParamSet holds 5 entries");

    // 4. M7-WP6 / UX-8（断言只增；既有 1–3 节逐字不动）：ParamDef::group 显式分组
    //    契约 = docs/ux-review-14.md §8 改进方案 2「分组由 schema 显式字段驱动，
    //    废除 key 词根启发式」。判据：
    //    * 每条 ParamDef 的 group 必须落在 kGroup* 词表内（空串 = 未分类 = 缺陷）；
    //    * 核心参数恒 kGroupCore；高级参数恒落四个子分组之一（二级折叠的四段）。
    {
        const std::set<std::string> all_groups = {
            std::string(pp::kGroupCore), std::string(pp::kGroupProgressive),
            std::string(pp::kGroupNoise), std::string(pp::kGroupDecodeBudget),
            std::string(pp::kGroupEncodeTools)};
        const std::set<std::string> sub_groups = {
            std::string(pp::kGroupProgressive), std::string(pp::kGroupNoise),
            std::string(pp::kGroupDecodeBudget), std::string(pp::kGroupEncodeTools)};
        int params_seen = 0;
        for (const pp::FormatDef &f : fmts) {
            for (const pp::BackendDef &b : f.backends) {
                for (const pp::TechDef &t : b.techs) {
                    for (const pp::ParamDef &p : t.params) {
                        ++params_seen;
                        check(all_groups.count(p.group) == 1,
                              f.id + "." + p.key + ".group 落在 kGroup* 词表内（got '" + p.group +
                                  "'）");
                        if (p.advanced) {
                            check(sub_groups.count(p.group) == 1,
                                  f.id + "." + p.key + "（advanced）group ∈ 四个子分组（got '" +
                                      p.group + "'）");
                        } else {
                            check(p.group == std::string(pp::kGroupCore),
                                  f.id + "." + p.key + "（core）group == kGroupCore（got '" +
                                      p.group + "'）");
                        }
                    }
                }
            }
        }
        check(params_seen >= 60,
              "static 表内 ParamDef 条目数 >= 60（got " + std::to_string(params_seen) + "）");
    }

    if (g_failed == 0) {
        std::printf("test_smoke: OK (8 formats, ParamSet round-trip)\n");
        return 0;
    }
    std::printf("test_smoke: FAILED (%d assertions)\n", g_failed);
    return 1;
}
