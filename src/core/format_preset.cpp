// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 格式级参数预设的 core 纯函数（M10 / W3-T1；契约 docs/v0.4.0-design.md §8 C23）
//
// 本文件只做"数据 + 规则"，不碰文件/JSON（那些在 src/ui/format_preset_io.*）：
//   * validate_format_preset —— 结构/版本/名字/格式/后端/技术/位深/逐键类型-范围-枚举/
//     未知键/跨技术残留键/heif_profile/自适应策略（parse+validate）/跨字段规则（cross_validate），
//     错误文案一律中文（UI 直接展示）。
//   * normalize_format_preset —— 复用 params 既有链（default_bitdepth_for/fill_defaults/
//     apply_locks/select_tech），只读调用；返回被补/被改写的 key 列表（给 UI 显示"已补齐 N 项"）。
//   * check_format_preset_applies_to —— format_id 匹配 + backend/tech/位深可用性。
//
// 口径注记（实现期决定，报告登记）：
//   * `params` 里 `__` 前缀的保留键**不参与**逐键校验（JSON IO 层直接忽略并由 reserved 重建）；
//   * libheif 系（heif/avif）的插件透传键（`x265:`/`aom:`）无目录定义，只做标量/未知键豁免
//     （与 core/params.cpp 的 `is_plugin_passthrough_key` 同口径）；
//   * 编码器内省不可用（无 libheif 插件）时，heif/avif 的键无法判定 ⇒ 放行（不误报），
//     但保留键/位深/策略等格式无关检查照常执行；
//   * 位深 0 = "不覆盖当前卡值"（合法），normalize 时补 default_bitdepth_for(format)。
#include "core/format_preset.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "codecs/heif_profile.h"
#include "core/adaptive.h"
#include "core/logger.h"
#include "core/params.h"
#include "core/pipeline.h" // default_bitdepth_for（位深默认单源）

namespace pp {
namespace {

constexpr std::string_view kStage = "preset";

bool is_heif_like(std::string_view format_id) { return format_id == "heif" || format_id == "avif"; }

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：format_id/key 由参数键语义固定
bool is_plugin_passthrough(std::string_view format_id, std::string_view key) {
    if (!is_heif_like(format_id))
        return false;
    return key.rfind("x265:", 0) == 0 || key.rfind("aom:", 0) == 0;
}

std::string format_id_list() {
    std::string out;
    for (const FormatDef &f : static_formats())
        out += (out.empty() ? "" : "/") + f.id;
    return out;
}

// 显示名 = 文件 stem：这里按"一个安全文件名词干"校验（中文可、路径分隔符/控制字符/保留
// 设备名/尾随点空格/超长一律拒绝）。save 直接用该名字拼 `<name>.json`，故必须在此拦住。
std::string validate_name(const std::string &name) {
    if (name.empty())
        return "格式预设名为空（文件名不能为空）";
    if (name == "." || name == "..")
        return "格式预设名不能是 \".\" 或 \"..\"";
    if (name.size() > 120)
        return "格式预设名过长（" + std::to_string(name.size()) + " 字节；上限 120）";
    if (name.front() == ' ' || name.back() == ' ' || name.back() == '.')
        return "格式预设名不能以空格/点开头或结尾（Windows 会静默截断）";
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20)
            return "格式预设名含有控制字符（0x" + std::to_string(static_cast<int>(u)) + "）";
        switch (c) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
        case '/':
        case '\\':
            return std::string("格式预设名含有非法文件名字符「") + c + "」";
        default:
            break;
        }
    }
    // Windows 保留设备名（大小写不敏感；带 .json 扩展名同样保留）。
    std::string lower;
    lower.reserve(name.size());
    for (const char c : name)
        lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
    static const char *const kReserved[] = {
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char *r : kReserved)
        if (lower == r)
            return "格式预设名「" + name + "」是 Windows 保留设备名，无法作为文件名";
    return {};
}

std::string zh_param_type(ParamType t) {
    switch (t) {
    case ParamType::Int:
        return "整数";
    case ParamType::Float:
        return "数值";
    case ParamType::Bool:
        return "布尔值";
    case ParamType::Enum:
        return "枚举值";
    case ParamType::String:
        return "字符串";
    }
    return "未知类型";
}

std::string zh_value_type(const ParamValue &v) {
    switch (v.index()) {
    case 1:
        return "布尔值";
    case 2:
        return "整数";
    case 3:
        return "数值";
    case 4:
        return "字符串";
    default:
        return "空值";
    }
}

std::string value_text(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[64];
        const auto r = std::to_chars(buf, buf + sizeof(buf), *d);
        return r.ec == std::errc() ? std::string(buf, r.ptr) : std::to_string(*d);
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return "\"" + *s + "\"";
    return "空值";
}

std::string choice_list(const ParamDef &def) {
    std::string out;
    for (std::size_t i = 0; i < def.choices.size() && i < 12; ++i) {
        if (!out.empty())
            out += " / ";
        out += def.choices[i].first;
    }
    if (def.choices.size() > 12)
        out += " / …";
    return out;
}

// 逐键中文校验：类型 → 范围 → 枚举成员（与 core/params.cpp 的 validate_one 同语义，
// 只是文案中文化；不复用英文版是为了 UI 直接可读，且不改既有英文冻结文案）。
std::string validate_value_zh(const ParamDef &def, const ParamValue &value) {
    switch (def.type) {
    case ParamType::Int: {
        const int64_t *i = std::get_if<int64_t>(&value);
        if (i == nullptr)
            return "参数「" + def.key + "」需要" + zh_param_type(def.type) + "（实际是" +
                   zh_value_type(value) + "）";
        const double x = static_cast<double>(*i);
        if (x < def.lo || x > def.hi)
            return "参数「" + def.key + "」的取值 " + value_text(value) + " 超出范围 [" +
                   value_text(ParamValue{def.lo}) + ", " + value_text(ParamValue{def.hi}) + "]";
        return {};
    }
    case ParamType::Float: {
        const int64_t *i = std::get_if<int64_t>(&value);
        const double *d = std::get_if<double>(&value);
        if (i == nullptr && d == nullptr)
            return "参数「" + def.key + "」需要" + zh_param_type(def.type) + "（实际是" +
                   zh_value_type(value) + "）";
        const double x = d != nullptr ? *d : static_cast<double>(*i);
        if (x < def.lo || x > def.hi)
            return "参数「" + def.key + "」的取值 " + value_text(value) + " 超出范围 [" +
                   value_text(ParamValue{def.lo}) + ", " + value_text(ParamValue{def.hi}) + "]";
        return {};
    }
    case ParamType::Bool:
        if (!std::holds_alternative<bool>(value))
            return "参数「" + def.key + "」需要" + zh_param_type(def.type) + "（实际是 " +
                   zh_value_type(value) + "）";
        return {};
    case ParamType::Enum: {
        if (def.choices.empty())
            return {}; // 空 choices 的 Enum = 无约束（不应出现；与 validate_one 同口径）
        const ParamValue &proto = def.choices.front().second;
        if (value.index() != proto.index())
            return "参数「" + def.key + "」需要" + zh_param_type(def.type) + "（实际是 " +
                   zh_value_type(value) + "）";
        const bool known = std::any_of(def.choices.begin(), def.choices.end(),
                                       [&value](const auto &c) { return c.second == value; });
        if (!known) {
            const std::string choices = choice_list(def);
            return "参数「" + def.key + "」的取值 " + value_text(value) + " 不在可选值内（可选：" +
                   (choices.empty() ? "—" : choices) + "）";
        }
        return {};
    }
    case ParamType::String:
        if (!std::holds_alternative<std::string>(value))
            return "参数「" + def.key + "」需要" + zh_param_type(def.type) + "（实际是 " +
                   zh_value_type(value) + "）";
        return {};
    }
    return {};
}

// —— 该格式全部后端×技术的参数定义并集（libheif 系追加运行时内省）——
// 说明：MSVC STL 的 std::map 移动构造被标注 noexcept，其内部 _Throw_bad_array_new_length
// （std::bad_alloc 的派生类）未被 clang-tidy 的 ignoreBadAlloc 覆盖 ⇒ 对隐式 move ctor 的误报。
// NOLINTNEXTLINE(bugprone-exception-escape)：MSVC STL map 移动构造 bad_array_new_length 误报
struct FormatParamIndex {
    bool determinable = false;
    std::map<std::string, ParamDef> defs;     // first-wins
    std::map<std::string, std::string> owner; // key → 技术 id（跨技术残留键定位用）
};

FormatParamIndex build_format_param_index(std::string_view format_id) {
    FormatParamIndex idx;
    const FormatDef *f = find_format(format_id);
    if (f == nullptr)
        return idx;
    bool needs_introspection = false;
    for (const BackendDef &b : f->backends) {
        if (b.runtime_introspected && b.techs.empty())
            needs_introspection = true;
        for (const TechDef &t : b.techs) {
            for (const ParamDef &p : t.params) {
                if (idx.defs.emplace(p.key, p).second)
                    idx.owner.emplace(p.key, t.id);
            }
        }
    }
    if (needs_introspection) {
        for (const BackendDef &lb : introspect_backends(format_id)) {
            for (const TechDef &t : lb.techs) {
                for (const ParamDef &p : t.params) {
                    if (idx.defs.emplace(p.key, p).second)
                        idx.owner.emplace(p.key, t.id);
                }
            }
        }
    }
    idx.determinable = !idx.defs.empty();
    return idx;
}

const FormatParamIndex &format_param_index(std::string_view format_id) {
    static std::mutex m;
    static std::map<std::string, FormatParamIndex> cache;
    const std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(std::string(format_id));
    if (it == cache.end())
        it = cache.emplace(std::string(format_id), build_format_param_index(format_id)).first;
    return it->second;
}

// 选中技术的声明键集合（静态表；heif/avif 的 live tech 也覆盖）。
bool tech_declares(const TechDef &t, std::string_view key) {
    return std::any_of(t.params.begin(), t.params.end(),
                       [key](const ParamDef &p) { return p.key == key; });
}

std::string validate_adaptive_field(const FormatPresetData &p, const FormatDef &f) {
    if (p.adaptive_enabled && p.adaptive_policy.empty())
        return "已启用自适应（adaptive.enabled=true）但策略为空；请填写策略文本或关闭开关";
    if (p.adaptive_policy.empty())
        return {};
    AdaptivePolicy policy;
    AdaptiveError aerr;
    if (!parse_and_validate_adaptive_policy(p.adaptive_policy, f.id, policy, &aerr)) {
        std::string pos;
        if (aerr.line > 0) {
            pos = "第 " + std::to_string(aerr.line) + " 行";
            if (aerr.col > 0)
                pos += "第 " + std::to_string(aerr.col) + " 列";
            pos += "：";
        }
        std::string msg = "自适应策略" + pos + aerr.message;
        if (!aerr.code.empty())
            msg += "（错误码 " + aerr.code + "）";
        if (!aerr.hint.empty())
            msg += "；建议：" + aerr.hint;
        return msg;
    }
    return {};
}

std::string validate_heif_profile(const FormatPresetData &p, const FormatDef &f) {
    if (!p.heif_profile.has_value())
        return {};
    if (!is_heif_like(f.id))
        return "heif_profile 只适用于 HEIF/AVIF（当前格式 " + f.id + "）";
    HeifProfile hp = HeifProfile::Auto;
    if (!heif_profile_from_int(*p.heif_profile, &hp))
        return "heif_profile 编号 " + std::to_string(*p.heif_profile) + " 越界（合法 0.." +
               std::to_string(kHeifProfileCount - 1) + "）";
    if (f.id == "avif") {
        if (hp != HeifProfile::Auto && !heif_profile_is_avif_signal(hp))
            return "heif_profile「" + heif_profile_label(hp) +
                   "」是 HEIF/HEVC 静图信号，不能用于 AVIF（AVIF 仅 Auto/Main/High/"
                   "Professional）";
        return {};
    }
    if (hp != HeifProfile::Auto && !heif_profile_is_hevc_signal(hp))
        return "heif_profile「" + heif_profile_label(hp) + "」是 AVIF 专用档位，不能用于 HEIF";
    std::string why;
    if (!heif_profile_still_available(hp, &why))
        return "heif_profile「" + heif_profile_label(hp) + "」不可用：" + why;
    return {};
}

} // namespace

// ===========================================================================
// 校验（保存/载入/开始运行三处同一套）
// ===========================================================================
std::string validate_format_preset(const FormatPresetData &p) {
    if (p.version <= 0)
        return "格式预设版本号非法（" + std::to_string(p.version) + "）";
    if (p.version > kFormatPresetVersion)
        return "不支持的格式预设版本 " + std::to_string(p.version) +
               "（当前支持 1；请升级 PhotoPipeline，或把文件降级为 v1）";
    if (const std::string name_err = validate_name(p.name); !name_err.empty())
        return name_err;
    const FormatDef *f = find_format(p.format_id);
    if (f == nullptr) {
        if (p.format_id.empty())
            return "格式预设缺少目标格式（format 字段为空）";
        return "未知格式「" + p.format_id + "」（可选：" + format_id_list() + "）";
    }
    const BackendDef *b = find_backend(*f, p.backend_id);
    if (b == nullptr)
        return "未知后端「" + p.backend_id + "」（格式 " + f->id + "）";
    const bool runtime_backend = b->runtime_introspected && b->techs.empty();
    if (!p.tech_id.empty()) {
        if (runtime_backend) {
            // libheif 系的静态表没有技术；运行时内省给出单一合成技术 "runtime"。
            if (p.tech_id != "runtime")
                return "格式 " + f->id +
                       " 的技术由运行时内省确定，预设只能留空或写 \"runtime\""
                       "（实际「" +
                       p.tech_id + "」）";
        } else if (find_tech(*b, p.tech_id) == nullptr) {
            return "未知技术「" + p.tech_id + "」（后端 " + b->id + "）";
        }
    }
    if (p.out_bitdepth != 0 &&
        std::find(f->bitdepths.begin(), f->bitdepths.end(), p.out_bitdepth) == f->bitdepths.end()) {
        std::string supported;
        for (const int d : f->bitdepths)
            supported += (supported.empty() ? "" : "/") + std::to_string(d);
        return "位深 " + std::to_string(p.out_bitdepth) + " 不被格式 " + f->id +
               " 支持（可选：0（不覆盖）/" + supported + "）";
    }

    const FormatParamIndex &index = format_param_index(f->id);
    const TechDef *selected_tech =
        (runtime_backend || p.tech_id.empty()) ? nullptr : find_tech(*b, p.tech_id);
    for (const auto &[key, value] : p.params) {
        if (key.rfind("__", 0) == 0)
            continue; // 保留键不落盘、不校验（由 JSON 层 reserved 重建）
        const auto def_it = index.defs.find(key);
        if (def_it == index.defs.end()) {
            if (is_plugin_passthrough(f->id, key)) {
                if (std::holds_alternative<std::monostate>(value))
                    return "参数「" + key + "」缺少取值";
                continue; // 透传键无目录定义（插件负责校验），只排除空值
            }
            if (!index.determinable) {
                // 内省不可用 → 无法判定（不误报），仅记日志；格式无关检查已通过。
                log_warn(kStage, "format_preset.cpp", "format preset param not verifiable",
                         {{"format", f->id}, {"key", key}});
                continue;
            }
            return "未知参数：" + key + "（格式 " + f->id + " 的参数表中没有该键）";
        }
        std::string value_err = validate_value_zh(def_it->second, value);
        if (!value_err.empty())
            return value_err;
        if (selected_tech != nullptr && !tech_declares(*selected_tech, key)) {
            const auto owner_it = index.owner.find(key);
            const std::string owner =
                owner_it == index.owner.end() ? std::string() : owner_it->second;
            if (!owner.empty() && owner != selected_tech->id) {
                std::string msg = "参数「";
                msg += key;
                msg += "」属于技术「";
                msg += owner;
                msg += "」，不属于当前技术「";
                msg += selected_tech->id;
                msg += "」（跨技术残留键；请切换技术或删除该键）";
                return msg;
            }
        }
    }

    if (const std::string profile_err = validate_heif_profile(p, *f); !profile_err.empty())
        return profile_err;
    if (const std::string adaptive_err = validate_adaptive_field(p, *f); !adaptive_err.empty())
        return adaptive_err;

    // 跨字段规则 + 未知参数（中文文案；与运行前预检同源）。
    // 上下文：位深用"预设值或格式默认"，lossless 从参数集读取，尺寸未知（0 → 尺寸相关规则跳过）。
    ParamSet ctx_params = p.params;
    if (p.heif_profile.has_value())
        ctx_params[std::string(kHeifProfileParamKey)] = *p.heif_profile;
    const bool lossless = lossless_flag(ctx_params);
    ParamContext ctx;
    ctx.format_id = f->id;
    ctx.backend_id = b->id;
    ctx.tech_id = p.tech_id;
    ctx.bitdepth = p.out_bitdepth > 0 ? p.out_bitdepth : default_bitdepth_for(f->id);
    ctx.lossless = lossless;
    ctx.width = 0;
    ctx.height = 0;
    const std::vector<std::string> cross_msgs = cross_validate(ctx, ctx_params);
    if (!cross_msgs.empty())
        return cross_msgs.front();
    return {};
}

// ===========================================================================
// 归一（补默认 + 锁 + 位深；复用 params 既有链，只读调用）
// ===========================================================================
std::vector<std::string> normalize_format_preset(FormatPresetData &p, std::string *err) {
    if (err != nullptr)
        err->clear();
    std::vector<std::string> touched;
    auto touch = [&touched](const std::string &key) {
        if (std::find(touched.begin(), touched.end(), key) == touched.end())
            touched.push_back(key);
    };
    const FormatDef *f = find_format(p.format_id);
    if (f == nullptr) {
        if (err != nullptr) {
            *err = p.format_id.empty() ? std::string("格式预设缺少目标格式（format 字段为空）")
                                       : "未知格式「" + p.format_id + "」";
        }
        return {};
    }
    const BackendDef *b = find_backend(*f, p.backend_id);
    if (b == nullptr) {
        if (err != nullptr)
            *err = "未知后端「" + p.backend_id + "」（格式 " + f->id + "）";
        return {};
    }
    if (p.backend_id.empty()) {
        p.backend_id = b->id;
        touch("backend");
    }
    if (p.out_bitdepth == 0) {
        p.out_bitdepth = default_bitdepth_for(f->id);
        if (std::find(f->bitdepths.begin(), f->bitdepths.end(), p.out_bitdepth) ==
            f->bitdepths.end()) {
            if (err != nullptr)
                *err = "格式 " + f->id + " 的默认位深 " + std::to_string(p.out_bitdepth) +
                       " 不在其能力列表内（实现缺陷）";
            return {};
        }
        touch("bitdepth");
    } else if (std::find(f->bitdepths.begin(), f->bitdepths.end(), p.out_bitdepth) ==
               f->bitdepths.end()) {
        if (err != nullptr)
            *err = "位深 " + std::to_string(p.out_bitdepth) + " 不被格式 " + f->id + " 支持";
        return {};
    }
    if (b->runtime_introspected && b->techs.empty())
        return touched; // heif/avif：参数来自运行时内省，静态表无法补齐（保持现有键）

    const bool lossless = lossless_flag(p.params);
    const TechDef *t = select_tech(*b, p.tech_id, lossless);
    if (t == nullptr) {
        if (err != nullptr)
            *err = "未知技术「" + p.tech_id + "」（后端 " + b->id + "）";
        return {};
    }
    if (p.tech_id.empty()) {
        p.tech_id = t->id;
        touch("tech");
    }
    for (const std::string &key : fill_defaults(*f, p.backend_id, p.tech_id, lossless, p.params))
        touch(key);
    for (const std::string &key : apply_locks(*f, p.backend_id, p.tech_id, lossless, p.params))
        touch(key);
    return touched;
}

// ===========================================================================
// 应用到目标格式卡的前置检查
// ===========================================================================
std::string check_format_preset_applies_to(const FormatPresetData &p, std::string_view format_id) {
    if (format_id.empty())
        return "无法应用格式预设：目标格式为空";
    if (p.format_id.empty())
        return "无法应用格式预设：预设缺少格式信息";
    if (p.format_id != format_id) {
        return "格式预设「" + p.name + "」属于格式 " + p.format_id + "，不能应用到格式 " +
               std::string(format_id);
    }
    const FormatDef *f = find_format(format_id);
    if (f == nullptr)
        return "未知格式「" + std::string(format_id) + "」";
    if (!p.backend_id.empty() && find_backend(*f, p.backend_id) == nullptr)
        return "预设的后端「" + p.backend_id + "」在格式 " + f->id + " 中不存在";
    if (!p.tech_id.empty()) {
        const BackendDef *b = find_backend(*f, p.backend_id);
        if (b == nullptr)
            return "预设的后端「" + p.backend_id + "」在格式 " + f->id + " 中不存在";
        const bool runtime_backend = b->runtime_introspected && b->techs.empty();
        if (runtime_backend) {
            if (p.tech_id != "runtime")
                return "预设的技术「" + p.tech_id + "」不适用于格式 " + f->id +
                       "（运行时内省技术只能留空或写 runtime）";
        } else if (find_tech(*b, p.tech_id) == nullptr) {
            return "预设的技术「" + p.tech_id + "」在格式 " + f->id + " 中不存在";
        }
    }
    if (p.out_bitdepth != 0 &&
        std::find(f->bitdepths.begin(), f->bitdepths.end(), p.out_bitdepth) == f->bitdepths.end())
        return "预设的位深 " + std::to_string(p.out_bitdepth) + " 不被格式 " + f->id + " 支持";
    return {};
}

// ===========================================================================
// 内置（出厂）格式预设库（M10 / W3-T5；设计 §8.3「内置库（出厂）」/ §9.7）
// ===========================================================================
const std::vector<FormatPresetData> &builtin_format_presets() {
    static const std::vector<FormatPresetData> presets = [] {
        std::vector<FormatPresetData> out;
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：参数语义由名字固定
        const auto add = [&out](std::string_view format_id, std::string_view backend_id,
                                std::string_view tech_id, int bitdepth, ParamSet overrides) {
            const FormatDef *f = find_format(format_id);
            if (f == nullptr)
                return;
            FormatPresetData p;
            p.version = kFormatPresetVersion;
            p.format_id = std::string(format_id);
            p.name = "内置示例 · " + f->label;
            p.backend_id = std::string(backend_id);
            p.tech_id = std::string(tech_id);
            p.out_bitdepth = bitdepth;
            // 静态表技术 → 默认参数全集（含锁）；libheif 系（heif/avif）的静态表没有技术，
            // default_params 只返回内部键 `__lossless`（下面剥掉）—— 运行时参数不写进内置库：
            // ① 内省值随环境/插件版本漂移；② 格式级参数并集按后端 first-wins，可能与具体后端
            // 的取值范围不一致（实测 avif：svt-av1 的 speed=12 vs 并集里 libaom 的 [0,9]，
            // 写进预设会被逐键校验拒绝）。故 libheif 系只写**与策略配套的安全初值**（见下表）。
            p.params = default_params(*f, p.backend_id, p.tech_id, /*lossless=*/false);
            for (auto &[key, value] : overrides)
                p.params[key] = value;
            // `__` 前缀是内部管道键（如 __lossless），不落盘也不进校验 —— 内置库只保留参数键。
            for (auto it = p.params.begin(); it != p.params.end();) {
                if (it->first.rfind("__", 0) == 0)
                    it = p.params.erase(it);
                else
                    ++it;
            }
            if (const AdaptiveDefaultPolicy *d = default_adaptive_policy_for(format_id)) {
                p.adaptive_enabled = true;
                p.adaptive_policy = d->text; // §9.7 逐字（含注释；出厂文件可直接给人读/改）
            }
            out.push_back(std::move(p));
        };
        // 位深/后端/技术与 GUI 默认一致（heif/avif 的 live tech 恒为 "runtime"）；
        // 参数覆盖 = 给 §9.7 策略留出提升空间的**安全初值**（quality/chroma 是策略第一/二阶段
        // 要搜的键；jpeg 的 quality_mode 必须显式钉住，否则策略搜 quality 不改变产物）。
        add("heif", "x265", "runtime", 10,
            ParamSet{{"quality", int64_t{85}}, {"chroma", std::string("420")}});
        add("avif", "svt-av1", "runtime", 10,
            ParamSet{{"quality", int64_t{85}}, {"chroma", std::string("420")}});
        add("jxl", "libjxl", "vardct", 16, ParamSet{{"distance", 1.0}});
        add("webp", "libwebp", "lossy", 8, ParamSet{{"quality", int64_t{85}}});
        add("jpeg", "jpegli", "dct", 8,
            ParamSet{{"quality_mode", std::string("quality")}, {"quality", int64_t{90}}});
        return out;
    }();
    return presets;
}

} // namespace pp
