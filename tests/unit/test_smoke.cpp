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

    if (g_failed == 0) {
        std::printf("test_smoke: OK (8 formats, ParamSet round-trip)\n");
        return 0;
    }
    std::printf("test_smoke: FAILED (%d assertions)\n", g_failed);
    return 1;
}
