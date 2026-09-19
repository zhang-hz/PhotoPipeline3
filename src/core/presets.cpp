// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — presets (M1-T2)
#include "core/presets.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/params.h"

namespace pp {
namespace {

// 与 params.cpp 的 select_tech 同规则：显式 id → find_tech；空 id → 首选
// （lossless 时优先 lossless_capable 的技术，如 webp=lossless / jxl=modular）
const TechDef* pick_tech(const BackendDef& b, std::string_view id, bool lossless) {
    if (!id.empty()) return find_tech(b, id);
    if (lossless) {
        for (const TechDef& t : b.techs)
            if (t.lossless_capable) return &t;
    }
    return b.techs.empty() ? nullptr : &b.techs.front();
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

std::string validate_preset(const PresetData& p) {
    if (p.version != 1)
        return "unsupported preset version " + std::to_string(p.version) + " (expected 1)";
    const FormatDef* f = find_format(p.format_id);
    if (!f) return "unknown format '" + p.format_id + "'";
    if (!find_backend(*f, p.backend_id))
        return "unknown backend '" + p.backend_id + "' for format '" + f->id + "'";
    if (std::find(f->bitdepths.begin(), f->bitdepths.end(), p.out_bitdepth) == f->bitdepths.end()) {
        std::string supported;
        for (const int d : f->bitdepths) supported += (supported.empty() ? "" : ", ") + std::to_string(d);
        return "bitdepth " + std::to_string(p.out_bitdepth) + " is not supported by format '" +
               f->id + "' (supported: " + supported + ")";
    }
    // 技术存在性 + 参数范围/枚举/类型（TIFF tile 规则在内）；heif/avif 的运行时
    // 内省参数由 validate_params 跳过。
    return validate_params(*f, p.backend_id, p.tech_id, p.lossless, p.params);
}

std::vector<std::string> normalize_preset(PresetData& p) {
    std::vector<std::string> touched;
    const FormatDef* f = find_format(p.format_id);
    if (!f) return touched;
    const BackendDef* b = find_backend(*f, p.backend_id);
    if (!b) return touched;
    if (p.backend_id.empty()) p.backend_id = b->id;
    if (b->runtime_introspected && b->techs.empty())
        return touched;  // heif/avif：参数来自 introspect_backends（T7），静态表无法补齐
    const TechDef* t = pick_tech(*b, p.tech_id, p.lossless);
    if (!t) return touched;
    if (p.tech_id.empty()) p.tech_id = t->id;

    touched = fill_defaults(*f, p.backend_id, p.tech_id, p.lossless, p.params);
    const std::vector<std::string> locked =
        apply_locks(*f, p.backend_id, p.tech_id, p.lossless, p.params);
    for (const std::string& k : locked)
        if (!contains(touched, k)) touched.push_back(k);
    return touched;
}

}  // namespace pp
