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

#include "ui/run_rows_model.h"  // M7-WP9（B.2）：运行页行模型纯语义
#include "ui/run_status_text.h" // M7-WP9（B.2）：运行页文案/数值格式纯函数

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

    // 5. M7-WP9 / 附录 B.2（断言只增；既有 1–4 节逐字不动）：run_rows_model + run_status_text
    //    抽出后的纯数据语义单测（R-109：UI 数据语义抽入可链接单元 → tests 直接断言）。判据：
    //    * 结算判定 / 0 字节判定 / 终态映射 = 逐真值表（docs/deep-review-0.3.2.md 附录 B.2
    //    首两行）；
    //    * 进度折算 = 折算公式 + 边界（空批 / 越界 clamp）；
    //    * 12 态文案矩阵机械面 = 阶段词表 + 数值格式族逐值（译文未装 → 源文逐字）。
    {
        using pp::ui::runpage::fmt_bytes;
        using pp::ui::runpage::fmt_duration;
        using pp::ui::runpage::fmt_mem_bytes;
        using pp::ui::runpage::format_name;
        using pp::ui::runpage::is_terminal_state;
        using pp::ui::runpage::out_completion;
        using pp::ui::runpage::out_zero_bytes;
        using pp::ui::runpage::OutKind;
        using pp::ui::runpage::run_frac;
        using pp::ui::runpage::settled_kind;
        using pp::ui::runpage::shared_stage_text;
        using pp::ui::runpage::write_stage_text;

        // 5.1 结算判定（OutKind 真值表）
        check(!settled_kind(OutKind::Queued), "settled_kind(Queued) == false");
        check(!settled_kind(OutKind::Active), "settled_kind(Active) == false");
        check(settled_kind(OutKind::Done), "settled_kind(Done) == true");
        check(settled_kind(OutKind::Failed), "settled_kind(Failed) == true");
        check(settled_kind(OutKind::Skipped), "settled_kind(Skipped) == true");
        check(settled_kind(OutKind::Cancelled), "settled_kind(Cancelled) == true");

        // 5.2 0 字节判定（修 3-10：仅「Done 且 ≤0 字节」判异常）
        check(out_zero_bytes(OutKind::Done, 0), "out_zero_bytes(Done, 0) == true");
        check(out_zero_bytes(OutKind::Done, -1), "out_zero_bytes(Done, -1) == true");
        check(!out_zero_bytes(OutKind::Done, 1), "out_zero_bytes(Done, 1) == false");
        check(!out_zero_bytes(OutKind::Failed, 0), "out_zero_bytes(Failed, 0) == false");
        check(!out_zero_bytes(OutKind::Queued, 0), "out_zero_bytes(Queued, 0) == false");

        // 5.3 终态映射（FileState 12 态 → 终态四档）
        check(is_terminal_state(pp::FileState::Done), "is_terminal_state(Done)");
        check(is_terminal_state(pp::FileState::Skipped), "is_terminal_state(Skipped)");
        check(is_terminal_state(pp::FileState::Failed), "is_terminal_state(Failed)");
        check(is_terminal_state(pp::FileState::Cancelled), "is_terminal_state(Cancelled)");
        check(!is_terminal_state(pp::FileState::Queued), "!is_terminal_state(Queued)");
        check(!is_terminal_state(pp::FileState::Encoding), "!is_terminal_state(Encoding)");

        // 5.4 进度折算（§7.1/§7.2：已结算 1.0；未结算按权重归一；运行级 = Σ/总数 clamp）
        check(out_completion(true, 0.f, 0.f) == 1.0, "out_completion(settled) == 1.0");
        check(out_completion(false, 0.f, 0.f) == 0.0, "out_completion(0,0) == 0.0");
        check(out_completion(false, 1.f, 1.f) == 1.0, "out_completion(1,1) == 1.0");
        check(out_completion(false, 0.5f, 0.f) > 0.0 && out_completion(false, 0.5f, 0.f) < 1.0,
              "out_completion(0.5,0) in (0,1)");
        check(run_frac(2, 1, 1.0) == 0.5, "run_frac(2 files x1 fmt, sum=1) == 0.5");
        check(run_frac(0, 1, 0.0) == 0.0, "run_frac(空批) == 0.0");
        check(run_frac(1, 1, 2.0) == 1.0, "run_frac(sum>总数) clamp -> 1.0");
        check(run_frac(1, 1, -1.0) == 0.0, "run_frac(sum<0) clamp -> 0.0");

        // 5.5 数值格式族（修 3-10 字节口径 + A29 峰值内存「—」口径）
        check(fmt_bytes(0) == QStringLiteral("0 B"), "fmt_bytes(0) == 0 B");
        check(fmt_bytes(512) == QStringLiteral("512 B"), "fmt_bytes(512) == 512 B");
        check(fmt_bytes(2048) == QStringLiteral("2 KB"), "fmt_bytes(2048) == 2 KB");
        check(fmt_mem_bytes(0) == QStringLiteral("—"), "fmt_mem_bytes(0) == —（未发生的量不编数）");
        check(fmt_mem_bytes(2048) == QStringLiteral("2 KB"), "fmt_mem_bytes(2048) == 2 KB");
        check(fmt_duration(9800) == QStringLiteral("9.8 s"), "fmt_duration(9800ms) == 9.8 s");
        check(fmt_duration(61000) == QStringLiteral("01:01"),
              "fmt_duration(61s) == 01:01（mm:ss 双段各补零 2 位）");

        // 5.6 格式名（mockup .ofmt 口径）与阶段词表（§3.4）
        check(format_name(QStringLiteral("jpeg")) == QStringLiteral("JPEG"), "format_name(jpeg)");
        check(format_name(QStringLiteral("webp")) == QStringLiteral("WebP"), "format_name(webp)");
        check(format_name(QStringLiteral("jxl")) == QStringLiteral("JXL"), "format_name(jxl)");
        check(format_name(QStringLiteral("avif")) == QStringLiteral("AVIF"), "format_name(avif)");
        check(format_name(QString()) == QStringLiteral("—"), "format_name(空) == —");
        check(shared_stage_text(pp::Stage::Probe) == QString::fromUtf8("探测"), "shared(Probe)");
        check(shared_stage_text(pp::Stage::Decode) == QString::fromUtf8("解码 → 色彩"),
              "shared(Decode)");
        check(shared_stage_text(pp::Stage::Flatten) == QString::fromUtf8("处理中"),
              "shared(Flatten -> 默认档)");
        check(write_stage_text(pp::Stage::Flatten) == QString::fromUtf8("合成"), "write(Flatten)");
        check(write_stage_text(pp::Stage::MetaWrite) == QString::fromUtf8("写入"),
              "write(MetaWrite)");
    }

    if (g_failed == 0) {
        std::printf("test_smoke: OK (8 formats, ParamSet round-trip)\n");
        return 0;
    }
    std::printf("test_smoke: FAILED (%d assertions)\n", g_failed);
    return 1;
}
