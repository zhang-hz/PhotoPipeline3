// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — parameter engine (M1-T2)
//
// 谓词输入约定（§4.2）：ParamDef::visible / ParamDef::locked 只能看到 ParamSet，
// 而 JXL 的无损锁定规则依赖"无损"开关。约定以保留键 "__lossless"（Bool）承载该
// 标志：
//   * default_params() 把 "__lossless" 写入返回集合（表单引擎/日志直接用它的输出
//     求值谓词即可）；
//   * apply_locks() / validate_params() / fill_defaults() 在求值谓词时把 lossless
//     参数注入内部副本，不污染调用方集合；
//   * 预设 JSON 不序列化 "__" 前缀的保留键（顶层 "lossless" 字段已表达该语义）。
#include "core/params.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace pp {

// codecs/encoders.h:18 —— libheif 运行时内省（heif/avif 的参数只存在于编码器插件里）。
// 按 pipeline.cpp 既有惯例做局部声明：core 不 include codecs 头，符号由 pp_core 在链接期解析。
std::vector<BackendDef> introspect_backends(std::string_view format_id);

namespace {

constexpr std::string_view kLosslessKey = "__lossless";

ParamSet::const_iterator find_key(const ParamSet &s, std::string_view key) {
    return s.find(std::string(key));
}

// 技术选择：显式 id → find_tech；空 id → 首选（lossless 时优先 lossless_capable 的技术）
const TechDef *select_tech(const BackendDef &b, std::string_view tech_id, bool lossless) {
    if (!tech_id.empty())
        return find_tech(b, tech_id);
    if (lossless) {
        for (const TechDef &t : b.techs)
            if (t.lossless_capable)
                return &t;
    }
    return b.techs.empty() ? nullptr : &b.techs.front();
}

std::string type_name(const ParamValue &v) {
    switch (v.index()) {
    case 0:
        return "empty";
    case 1:
        return "bool";
    case 2:
        return "int";
    case 3:
        return "float";
    case 4:
        return "string";
    default:
        return "unknown";
    }
}

std::string value_to_string(const ParamValue &v) {
    if (const bool *b = std::get_if<bool>(&v))
        return *b ? "true" : "false";
    if (const int64_t *i = std::get_if<int64_t>(&v))
        return std::to_string(*i);
    if (const double *d = std::get_if<double>(&v)) {
        char buf[64];
        const std::to_chars_result r = std::to_chars(buf, buf + sizeof(buf), *d);
        if (r.ec == std::errc())
            return std::string(buf, r.ptr);
        return std::to_string(*d);
    }
    if (const std::string *s = std::get_if<std::string>(&v))
        return *s;
    return {};
}

std::string checked_value(const ParamValue &v) {
    if (const std::string *s = std::get_if<std::string>(&v))
        return "\"" + *s + "\"";
    return value_to_string(v);
}

// 单参数校验：类型 → 范围（Int/Float）→ 枚举成员
std::string validate_one(const ParamDef &p, const ParamValue &v) {
    switch (p.type) {
    case ParamType::Int: {
        const int64_t *i = std::get_if<int64_t>(&v);
        if (!i)
            return "expected int, got " + type_name(v);
        if (static_cast<double>(*i) < p.lo || static_cast<double>(*i) > p.hi)
            return "value " + std::to_string(*i) + " out of range [" + value_to_string(p.lo) +
                   ", " + value_to_string(p.hi) + "]";
        break;
    }
    case ParamType::Float: {
        // 整型数值按加宽接受（UI 里 1 与 1.0 等价；不静默截断反向转换）
        const int64_t *i = std::get_if<int64_t>(&v);
        const double *d = std::get_if<double>(&v);
        if (!i && !d)
            return "expected float, got " + type_name(v);
        const double x = d ? *d : static_cast<double>(*i);
        if (x < p.lo || x > p.hi)
            return "value " + value_to_string(x) + " out of range [" + value_to_string(p.lo) +
                   ", " + value_to_string(p.hi) + "]";
        break;
    }
    case ParamType::Bool: {
        if (!std::holds_alternative<bool>(v))
            return "expected bool, got " + type_name(v);
        break;
    }
    case ParamType::Enum: {
        if (p.choices.empty())
            break; // 空 choices 的 Enum 视为无约束（不应出现）
        const ParamValue &proto = p.choices.front().second;
        if (v.index() != proto.index())
            return "expected " + type_name(proto) + ", got " + type_name(v);
        const bool known = std::any_of(p.choices.begin(), p.choices.end(),
                                       [&v](const auto &c) { return c.second == v; });
        if (!known)
            return "value " + checked_value(v) + " is not a valid choice";
        break;
    }
    }
    return {};
}

// ---------------------------------------------------------------------------
// M2-T5 §2.7：cross_validate 的静态表 + 运行时内省键并集
// ---------------------------------------------------------------------------

// "该 format 全部技术声明的参数键并集"（未知参数判定的唯一依据）：
//   * 静态表：**全部** backend × **全部** tech —— 同 format 其它技术声明的键因此合法
//     （切技术/载入预设的残留键不得判未知，M2-T5 父裁定的关键防误报点）；
//   * libheif 系（heif/avif）：静态表 techs 为空，键只能来自 introspect_backends()。
struct DeclaredKeys {
    bool determinable = false; // false = 无法判定 → 跳过未知参数规则（防误报）
    std::set<std::string> keys;
};

DeclaredKeys compute_declared_keys(std::string_view format_id) {
    DeclaredKeys out;
    const FormatDef *f = find_format(format_id);
    if (!f)
        return out; // 未知 format：格式名校验归 validate_params，这里不判
    bool needs_introspection = false;
    for (const BackendDef &b : f->backends) {
        if (b.runtime_introspected && b.techs.empty())
            needs_introspection = true;
        for (const TechDef &t : b.techs)
            for (const ParamDef &p : t.params)
                out.keys.insert(p.key);
    }
    if (needs_introspection) {
        const std::vector<BackendDef> live = introspect_backends(format_id);
        if (live.empty())
            return out; // 内省不可用（无编码器插件）→ 无法判定，宁可不报
        for (const BackendDef &b : live)
            for (const TechDef &t : b.techs)
                for (const ParamDef &p : t.params)
                    out.keys.insert(p.key);
    }
    out.determinable = true;
    return out;
}

// 进程内缓存：内省会实例化 libheif 编码器，代价远高于一次 map 查找；结果对同一进程稳定。
const DeclaredKeys &declared_keys(std::string_view format_id) {
    static std::mutex m;
    static std::map<std::string, DeclaredKeys> cache;
    const std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(std::string(format_id));
    if (it == cache.end())
        it = cache.emplace(std::string(format_id), compute_declared_keys(format_id)).first;
    return it->second; // std::map 节点地址稳定；插入后不再改写 → 并发只读安全
}

// tech_id 空 = 首选技术（find_backend/find_tech 的既有约定：空 → 第一个）。
std::string effective_tech_id(const std::string &format_id, const std::string &tech_id) {
    if (!tech_id.empty())
        return tech_id;
    const FormatDef *f = find_format(format_id);
    const BackendDef *b = f ? find_backend(*f, "") : nullptr;
    const TechDef *t = b ? find_tech(*b, "") : nullptr;
    return t ? t->id : std::string();
}

} // namespace

const FormatDef *find_format(std::string_view id) {
    for (const FormatDef &f : static_formats())
        if (f.id == id)
            return &f;
    return nullptr;
}

const BackendDef *find_backend(const FormatDef &f, std::string_view id) {
    if (id.empty())
        return f.backends.empty() ? nullptr : &f.backends.front(); // 空=首选
    for (const BackendDef &b : f.backends)
        if (b.id == id)
            return &b;
    return nullptr;
}

const TechDef *find_tech(const BackendDef &b, std::string_view id) {
    if (id.empty())
        return b.techs.empty() ? nullptr : &b.techs.front(); // 空=首选
    for (const TechDef &t : b.techs)
        if (t.id == id)
            return &t;
    return nullptr;
}

std::optional<ParamValue> eval_lock(const ParamDef &p, const ParamSet &s) {
    if (!p.locked)
        return std::nullopt;
    return p.locked(s);
}

bool eval_visible(const ParamDef &p, const ParamSet &s) {
    if (!p.visible)
        return true;
    return p.visible(s);
}

int64_t param_int(const ParamSet &s, std::string_view key, int64_t fallback) {
    const auto it = find_key(s, key);
    if (it == s.end())
        return fallback;
    if (const int64_t *v = std::get_if<int64_t>(&it->second))
        return *v;
    return fallback;
}

double param_float(const ParamSet &s, std::string_view key, double fallback) {
    const auto it = find_key(s, key);
    if (it == s.end())
        return fallback;
    if (const double *v = std::get_if<double>(&it->second))
        return *v;
    if (const int64_t *i = std::get_if<int64_t>(&it->second))
        return static_cast<double>(*i);
    return fallback;
}

bool param_bool(const ParamSet &s, std::string_view key, bool fallback) {
    const auto it = find_key(s, key);
    if (it == s.end())
        return fallback;
    if (const bool *v = std::get_if<bool>(&it->second))
        return *v;
    return fallback;
}

std::string param_str(const ParamSet &s, std::string_view key, std::string_view fallback) {
    const auto it = find_key(s, key);
    if (it == s.end())
        return std::string(fallback);
    if (const std::string *v = std::get_if<std::string>(&it->second))
        return *v;
    return std::string(fallback);
}

ParamSet default_params(const FormatDef &f, const std::string &backend_id,
                        const std::string &tech_id, bool lossless) {
    ParamSet s;
    s[std::string(kLosslessKey)] = lossless;
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return s;
    const TechDef *t = select_tech(*b, tech_id, lossless);
    if (!t)
        return s; // 运行时内省参数（heif/avif → T7 introspect_backends）
    for (const ParamDef &p : t->params)
        s[p.key] = p.def;
    apply_locks(f, backend_id, tech_id, lossless, s);
    return s;
}

std::vector<std::string> apply_locks(const FormatDef &f, const std::string &backend_id,
                                     const std::string &tech_id, bool lossless, ParamSet &s) {
    std::vector<std::string> changed;
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return changed;
    const TechDef *t = select_tech(*b, tech_id, lossless);
    if (!t)
        return changed;
    ParamSet probe = s;
    probe[std::string(kLosslessKey)] = lossless; // 谓词输入（不写入调用方集合）
    for (const ParamDef &p : t->params) {
        const std::optional<ParamValue> forced = eval_lock(p, probe);
        if (!forced)
            continue;
        const auto it = s.find(p.key);
        if (it == s.end() || !(it->second == *forced)) {
            s[p.key] = *forced;
            changed.push_back(p.key);
        }
    }
    return changed;
}

std::string validate_params(const FormatDef &f, const std::string &backend_id,
                            const std::string &tech_id, bool lossless, const ParamSet &s) {
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return "unknown backend '" + backend_id + "' for format '" + f.id + "'";
    const TechDef *t = select_tech(*b, tech_id, lossless);
    if (!t) {
        // libheif 系（heif/avif）：技术/参数由 introspect_backends 运行时生成（T7），
        // 静态表无法判定 → 跳过（不报错）。
        if (b->runtime_introspected && b->techs.empty())
            return {};
        return "unknown tech '" + tech_id + "' for backend '" + b->id + "'";
    }
    for (const ParamDef &p : t->params) {
        const auto it = s.find(p.key);
        if (it == s.end())
            continue; // 缺失=用默认值（fill_defaults/normalize_preset 负责补齐）
        // 表驱动迭代 → "__" 前缀的保留键（__lossless 等）天然不参与范围/类型校验
        const std::string err = validate_one(p, it->second);
        if (!err.empty())
            return "param '" + p.key + "': " + err;
    }
    // TIFF tiling（§4.2/§4.7）：>0 时必须是 16 的倍数；两参数须同时启用或同时为 0
    if (f.id == "tiff") {
        const int64_t w = param_int(s, "tiff_tile_width", 0);
        const int64_t h = param_int(s, "tiff_tile_height", 0);
        if (w > 0 && w % 16 != 0)
            return "param 'tiff_tile_width': value " + std::to_string(w) +
                   " must be a positive multiple of 16";
        if (h > 0 && h % 16 != 0)
            return "param 'tiff_tile_height': value " + std::to_string(h) +
                   " must be a positive multiple of 16";
        if ((w > 0) != (h > 0))
            return "param 'tiff_tile_width'/'tiff_tile_height': both must be > 0 (tiled) or 0 "
                   "(strips)";
    }
    return {};
}

// ---------------------------------------------------------------------------
// M2-T5 §2.7：交叉参数约束（#16 销账；per-key 谓词表达不了的跨字段规则）
// ---------------------------------------------------------------------------
std::vector<std::string> cross_validate(const ParamSet &values, const std::string &format_id,
                                        const std::string &tech_id) {
    std::vector<std::string> out;

    // 规则① webp + lossy：qmin ≤ qmax（与 WebPValidateConfig 同名约束一致）。
    // 两键缺一 → 无法判定（缺失=默认值，由 fill_defaults/validate_params 负责）。
    if (format_id == "webp" && effective_tech_id(format_id, tech_id) == "lossy" &&
        values.count("qmin") != 0 && values.count("qmax") != 0) {
        if (param_int(values, "qmin", 0) > param_int(values, "qmax", 0))
            out.push_back("qmin 不能大于 qmax");
    }

    // 规则② jpeg：启用渐进式时必须启用哈夫曼表优化。
    // M2-T5 父裁定订正（§2.7 原文条件写反）：真实约束来自 jpegli ——
    //   tools/cjpegli.cc:146 `progressive_level > 0 && !optimize_coding` → 报
    //   "--fixed_code must be used together with -p 0"；
    //   enc_jpegli.cpp:304 `cinfo.optimize_coding = (progressive || optimize_coding)`
    //   ⇒ (true,true) 是规范化态、(true,false) 才是非法态。
    // 因此 jpeg 两参数默认值（true/true，format_tables.cpp:88-100）天然不报（§4 T5）。
    if (format_id == "jpeg" && values.count("progressive") != 0 &&
        values.count("optimize_coding") != 0 && param_bool(values, "progressive", false) &&
        !param_bool(values, "optimize_coding", true)) {
        out.push_back("启用渐进式时必须启用哈夫曼表优化");
    }

    // 规则③ 未知参数（M2-T5 父裁定新增）：该 format 全部技术声明的键并集之外的键。
    //   * 保留键（"__" 前缀，如 __lossless）是引擎内部键，不参与判定；
    //   * ParamSet 是 std::map → 迭代即字典序，多条消息天然稳定排序；
    //   * 该 format 任何技术都不认识的键才是未知（跨技术残留键在并集内 → 不报）。
    const DeclaredKeys &declared = declared_keys(format_id);
    if (declared.determinable) {
        for (const auto &[key, value] : values) {
            (void)value;
            if (key.rfind("__", 0) == 0)
                continue;
            if (declared.keys.find(key) == declared.keys.end())
                out.push_back("未知参数：" + key);
        }
    }
    return out;
}

std::vector<std::string> fill_defaults(const FormatDef &f, const std::string &backend_id,
                                       const std::string &tech_id, bool lossless, ParamSet &s) {
    std::vector<std::string> added;
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return added;
    const TechDef *t = select_tech(*b, tech_id, lossless);
    if (!t)
        return added;
    for (const ParamDef &p : t->params) {
        if (s.find(p.key) == s.end()) {
            s[p.key] = p.def;
            added.push_back(p.key);
        }
    }
    return added;
}

std::string snapshot_params(const ParamSet &s) {
    std::string out; // std::map 已按 key 字典序迭代
    for (const auto &[k, v] : s) {
        if (k.rfind("__", 0) == 0)
            continue; // 保留键（__lossless）不进日志快照
        if (!out.empty())
            out.push_back(' ');
        out += k;
        out.push_back('=');
        out += value_to_string(v);
    }
    return out;
}

} // namespace pp
