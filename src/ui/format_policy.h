// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 输出页格式能力/预选策略纯核（M7-R2E2 / 附录 B.3「format_policy」）
//
// 契约（docs/deep-review-0.3.2.md R-189/R-191/E3-10/E3-13）：位深交集 / 后端归一 /
// avif+alpha 预选与提示的**判定谓词**从 page_output.cpp 抽为纯函数单源 ——
//   * R-191：「avif 提示谓词两份拷贝」（update_alpha_hint vs update_metadata_only_state
//     内联）收敛为 alpha_combo()；预选触发条件收敛为 avif_preselect_needed()；
//   * 位深交集 / 静态-内省后端归一（§2.9.2/§2.9.7）为纯核，缓存与 IO 留在 UI 层（CapabilityCache
//     由调用方持有，可清空 —— 评审对「函数级 static 进程期缓存不可重置」的收口）。
// header-only（R-109：非 Widget 数据语义可单测）；tests/unit/test_smoke.cpp 直接断言真值表。
#pragma once

#include <algorithm>
#include <charconv>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "codecs/encoders.h" // pp::BackendDef / pp::TechDef
#include "core/params.h"     // pp::FormatDef

namespace pp::ui::fmtpolicy {

// "8,10,16" → {8,10,16}（坏 token 静默跳过；空段跳过）—— probe_bitdepth_support 的载荷解析
inline std::vector<int> parse_csv_ints(const std::string &csv) {
    std::vector<int> out;
    std::size_t pos = 0;
    while (pos <= csv.size()) {
        const std::size_t comma = csv.find(',', pos);
        const std::size_t len = (comma == std::string::npos) ? std::string::npos : comma - pos;
        const std::string tok = csv.substr(pos, len);
        if (!tok.empty()) {
            int value = 0;
            const char *first = tok.data();
            const char *last = tok.data() + tok.size();
            const std::from_chars_result res = std::from_chars(first, last, value);
            if (res.ec == std::errc() && res.ptr == last)
                out.push_back(value);
        }
        if (comma == std::string::npos)
            break;
        pos = comma + 1;
    }
    return out;
}

// §2.9.2：可选位深 = 静态允许集 ∩ 运行期探测（保持静态集顺序）
inline std::vector<int> intersect_bitdepths(const std::vector<int> &static_set,
                                            const std::vector<int> &probed) {
    std::vector<int> allowed;
    for (const int d : static_set) {
        if (std::find(probed.begin(), probed.end(), d) != probed.end())
            allowed.push_back(d);
    }
    return allowed;
}

// §2.9.7：内省后端按**静态表顺序**归一（静态序优先；内省独有项随后按内省序追加）
inline std::vector<pp::BackendDef>
merge_backend_order(const std::vector<pp::BackendDef> &static_order,
                    const std::vector<pp::BackendDef> &live) {
    std::vector<pp::BackendDef> ordered;
    for (const pp::BackendDef &sb : static_order) {
        for (const pp::BackendDef &lb : live) {
            if (lb.id == sb.id) {
                // M7-R2.5：内省标签可能很长（如 libheif 的 "AOMedia Project A…"），
                // 160px 后端下拉会硬裁。静态表标签（libaom / SVT-AV1）是产品文案单源，
                // 合并时以静态标签为准；id/参数/能力仍取运行时内省结果。
                pp::BackendDef merged = lb;
                if (!sb.label.empty())
                    merged.label = sb.label;
                ordered.push_back(std::move(merged));
            }
        }
    }
    for (const pp::BackendDef &lb : live) {
        if (std::none_of(ordered.begin(), ordered.end(),
                         [&](const pp::BackendDef &b) { return b.id == lb.id; }))
            ordered.push_back(lb);
    }
    return ordered;
}

// R-191 单源：「avif + alpha + 10 位」组合事实（提示可见性 / 预选条件的共同前置）
inline bool alpha_combo(const std::string &format_id, bool metadata_only, bool batch_has_alpha,
                        int bitdepth) {
    return !metadata_only && batch_has_alpha && format_id == "avif" && bitdepth == 10;
}

// §9.1/R-066：avif+alpha 预选触发条件（用户手改后端 / 预设落值路径不预选）。
// libaom_available = 该格式生效后端表含 libaom（UI 侧查表喂入）。
inline bool avif_preselect_needed(const std::string &format_id, bool metadata_only,
                                  bool batch_has_alpha, int bitdepth, const std::string &backend,
                                  bool user_backend_override, bool in_preset_apply,
                                  bool libaom_available) {
    if (user_backend_override || in_preset_apply)
        return false;
    if (!alpha_combo(format_id, metadata_only, batch_has_alpha, bitdepth))
        return false;
    return backend == "svt-av1" && libaom_available;
}

} // namespace pp::ui::fmtpolicy
