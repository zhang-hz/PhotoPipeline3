// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — presets (M1-T2；M4-T13 落 schema v2 + v1 迁移)
//
// PP-FROZEN(0.3.0) §3.6 落地（依据 docs/v0.3.0-design.md §3.6；落地任务 = W3-T13）：
//   * `PresetData` 重排为 schema v2（`outputs: [OutputFormatSpec]` + `output_template`/
//     `split_by_format`）；`migrate_preset_v1()` 把 0.2 单格式形态零丢失地搬进 v2 单元素数组。
//   * 无损语义：随 `outputs[i].params` 承载（显式 schema 参数 `lossless`，0.3.0 新增；
//     内部管道键 `__lossless` 仍由参数引擎维护，见 src/core/params.h）。
#include "core/presets.h"

#include <algorithm>
#include <string>
#include <vector>

#include "core/fsops.h"
#include "core/params.h"

namespace pp {
namespace {

// 与 params.cpp 的 select_tech 同规则：显式 id → find_tech；空 id → 首选
// （lossless 时优先 lossless_capable 的技术，如 webp=lossless / jxl=modular）
const TechDef *pick_tech(const BackendDef &b, std::string_view id, bool lossless) {
    if (!id.empty())
        return find_tech(b, id);
    if (lossless) {
        for (const TechDef &t : b.techs)
            if (t.lossless_capable)
                return &t;
    }
    return b.techs.empty() ? nullptr : &b.techs.front();
}

bool contains(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

void push_unique(std::vector<std::string> &v, const std::vector<std::string> &add) {
    for (const std::string &k : add)
        if (!contains(v, k))
            v.push_back(k);
}

// 该 (format, backend, tech) 是否把无损暴露为**显式 schema 参数**（key = "lossless"）：
//   * 静态表：匹配到的技术声明了该 key（jxl/vardct+modular、webp/lossy+lossless —— T13 落地）；
//   * libheif 系（heif/avif）：技术来自运行时内省，静态表为空 → 按 param-catalog §3/§4.2 的惯例
//     视为声明（内省面本就暴露 `lossless` bool，enc_heif 消费它）；
//   * 其余格式（jpeg 无 lossless 技术、png/tiff/bmp 技术本身无损且 catalog 不列该参数）→ 不声明：
//     无损标志只落内部管道键 `__lossless`（谓词/编码器输入，不落盘、不参与未知键校验）。
bool declares_lossless(const FormatDef &f, const std::string &backend_id,
                       const std::string &tech_id) {
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return false;
    if (b->runtime_introspected)
        return true;
    const TechDef *t = tech_id.empty() ? pick_tech(*b, {}, false) : find_tech(*b, tech_id);
    if (!t)
        return false;
    for (const ParamDef &p : t->params)
        if (p.key == kLosslessParamKey)
            return true;
    return false;
}

// 多输出时的错误前缀（单输出消息与 0.2 逐字一致 → 旧断言/日志文本沿用）
std::string with_output_index(std::size_t i, std::size_t total, const std::string &msg) {
    if (total <= 1)
        return msg;
    return "output #" + std::to_string(i) + ": " + msg;
}

} // namespace

std::string validate_preset(const PresetData &p) {
    if (p.version != 2)
        return "unsupported preset version " + std::to_string(p.version) + " (expected 2)";
    if (p.outputs.empty())
        return "preset has no outputs";
    // §4.2 模板校验（未知 $ 符号 / ".." / 绝对路径 = 非法）：预设里的非法模板直接判无效
    std::string tmpl_err;
    if (!validate_output_template(p.output_template, &tmpl_err))
        return tmpl_err;
    const std::size_t total = p.outputs.size();
    for (std::size_t i = 0; i < total; ++i) {
        const OutputFormatSpec &spec = p.outputs[i];
        const FormatDef *f = find_format(spec.format_id);
        if (!f)
            return with_output_index(i, total, "unknown format '" + spec.format_id + "'");
        if (!find_backend(*f, spec.backend_id))
            return with_output_index(
                i, total, "unknown backend '" + spec.backend_id + "' for format '" + f->id + "'");
        if (std::find(f->bitdepths.begin(), f->bitdepths.end(), spec.out_bitdepth) ==
            f->bitdepths.end()) {
            std::string supported;
            for (const int d : f->bitdepths)
                supported += (supported.empty() ? "" : ", ") + std::to_string(d);
            return with_output_index(i, total,
                                     "bitdepth " + std::to_string(spec.out_bitdepth) +
                                         " is not supported by format '" + f->id +
                                         "' (supported: " + supported + ")");
        }
        // 技术存在性 + 参数范围/枚举/类型（TIFF tile 规则在内）；heif/avif 的运行时
        // 内省参数由 validate_params 跳过。无损标志取自该输出的参数集（显式参数优先）。
        const std::string err = validate_params(*f, spec.backend_id, spec.tech_id,
                                                lossless_flag(spec.params), spec.params);
        if (!err.empty())
            return with_output_index(i, total, err);
    }
    return {};
}

std::vector<std::string> normalize_preset(PresetData &p) {
    std::vector<std::string> touched;
    if (p.output_template.empty())
        p.output_template = "$format/$dir/$file"; // §3.2 同名默认（缺失 = 未配置）
    for (OutputFormatSpec &spec : p.outputs) {
        const FormatDef *f = find_format(spec.format_id);
        if (!f)
            continue;
        const BackendDef *b = find_backend(*f, spec.backend_id);
        if (!b)
            continue;
        if (spec.backend_id.empty())
            spec.backend_id = b->id;
        if (b->runtime_introspected && b->techs.empty())
            continue; // heif/avif：参数来自 introspect_backends（T7），静态表无法补齐
        const bool lossless = lossless_flag(spec.params);
        const TechDef *t = pick_tech(*b, spec.tech_id, lossless);
        if (!t)
            continue;
        if (spec.tech_id.empty())
            spec.tech_id = t->id;

        push_unique(touched,
                    fill_defaults(*f, spec.backend_id, spec.tech_id, lossless, spec.params));
        push_unique(touched, apply_locks(*f, spec.backend_id, spec.tech_id, lossless, spec.params));
    }
    return touched;
}

PresetData migrate_preset_v1(const PresetDataV1 &v1) {
    PresetData p;
    p.version = 2;
    p.name = v1.name;
    p.color_target = v1.color_target;
    p.conflict = v1.conflict;
    p.rules = v1.rules;
    // 0.2 = 单格式"现状直出"（§1 需求 3「默认行为兼容 v0.2」）→ 模板取单输出兼容形态
    // `$dir/$file`、分文件夹关；用户若要新结构，在输出页开关/模板框里改（§4.2 联动）。
    p.output_template = "$dir/$file";
    p.split_by_format = false;

    OutputFormatSpec spec;
    spec.format_id = v1.format_id;
    spec.backend_id = v1.backend_id;
    spec.tech_id = v1.tech_id;
    spec.out_bitdepth = v1.out_bitdepth;
    spec.params = v1.params;
    const FormatDef *f = find_format(v1.format_id);
    if (f != nullptr && declares_lossless(*f, v1.backend_id, v1.tech_id))
        spec.params[std::string(kLosslessParamKey)] = v1.lossless; // 显式 schema 参数（可落盘）
    spec.params[std::string(kLosslessKey)] = v1.lossless;          // 内部管道键（谓词/编码器输入）
    p.outputs.push_back(std::move(spec));
    return p;
}

} // namespace pp
