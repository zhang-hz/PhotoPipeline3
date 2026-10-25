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

// 无损标志的两个承载面（kLosslessParamKey=显式 schema 参数 / kLosslessKey=内部管道键）在
// params.h 中声明（0.3.0/M4-T13 单源）；本文件不再自带字面量。

ParamSet::const_iterator find_key(const ParamSet &s, std::string_view key) {
    return s.find(std::string(key));
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

// libheif 插件私有点位：`<plugin>:<option>` 透传键（见 codecs/enc_heif.cpp 的透传循环）。
// 只对 libheif 系格式放行，其它格式仍按未知参数拒绝。
bool is_plugin_passthrough_key(std::string_view format_id, const std::string &key) {
    if (format_id != "heif" && format_id != "avif")
        return false;
    return key.rfind("x265:", 0) == 0 || key.rfind("svt:", 0) == 0 || key.rfind("aom:", 0) == 0;
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

// 该技术是否声明了显式无损参数（key = kLosslessParamKey，0.3.0/M4-T13 的 schema 面）。
// R-208：与导出的 declares_lossless(f, backend, tech) 共用同一判定内核（tech 级）。
bool tech_declares_lossless(const TechDef *t) {
    if (t == nullptr)
        return false;
    for (const ParamDef &p : t->params)
        if (p.key == kLosslessParamKey)
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// 跨字段规则单源（R-206/R-207，M7-WP2 建表；M7-R2B 收敛双通道）
// ---------------------------------------------------------------------------
// 旧形态：TIFF tile 规则内嵌 validate_params（英文串），webp/jpeg 规则内嵌 cross_validate
// （中文串）—— 同类约束两处实现、两种语言、裸字符串绑键（表改 key 规则静默失效）。
// M7-WP2 收敛为一张规则表 + 双通道归属数据（in_validate / in_cross）；
// **M7-R2B（R-206 改道定稿）**：通道归属退场 —— preset 通道（validate_params，en 文案）与
// cross_validate 通道（zh 文案）**共用全部规则**，两通道唯一差异 = 文案表现层（语言），
// 「同输入两通道判负一致」由 test_params/test_presets 强测试钉住。既有 en/zh 文本**逐字保留**
// （不放宽、不改写）；行为面新增 = 两通道各自补齐了过去缺失的规则判定（登记裁定）。
struct CrossRule {
    std::string_view format_id;    // 精确匹配；"" = 任意
    std::string_view tech_id;      // 与 effective_tech_id 解析结果精确匹配；"" = 任意
    bool require_all_keys = false; // true：全部键在场才判定（缺键 = 无法判定，跳过）
    std::vector<std::string_view> keys;
    std::function<std::string(const ParamSet &, bool english)> check; // "" = 未违反
};

const std::vector<CrossRule> &cross_rules() {
    static const std::vector<CrossRule> rules = {
        // TIFF tiling（§4.2/§4.7；原 validate_params 内嵌，英文文案逐字保留）
        {std::string_view("tiff"),
         std::string_view(),
         false,
         {kKeyTiffTileWidth},
         [](const ParamSet &s, bool en) -> std::string {
             const int64_t w = param_int(s, kKeyTiffTileWidth, 0);
             if (w > 0 && w % 16 != 0)
                 return en ? "param '" + std::string(kKeyTiffTileWidth) + "': value " +
                                 std::to_string(w) + " must be a positive multiple of 16"
                           : "param '" + std::string(kKeyTiffTileWidth) + "'：值 " +
                                 std::to_string(w) + " 必须是 16 的正倍数";
             return {};
         }},
        {std::string_view("tiff"),
         std::string_view(),
         false,
         {kKeyTiffTileHeight},
         [](const ParamSet &s, bool en) -> std::string {
             const int64_t h = param_int(s, kKeyTiffTileHeight, 0);
             if (h > 0 && h % 16 != 0)
                 return en ? "param '" + std::string(kKeyTiffTileHeight) + "': value " +
                                 std::to_string(h) + " must be a positive multiple of 16"
                           : "param '" + std::string(kKeyTiffTileHeight) + "'：值 " +
                                 std::to_string(h) + " 必须是 16 的正倍数";
             return {};
         }},
        {std::string_view("tiff"),
         std::string_view(),
         false,
         {kKeyTiffTileWidth, kKeyTiffTileHeight},
         [](const ParamSet &s, bool en) -> std::string {
             const int64_t w = param_int(s, kKeyTiffTileWidth, 0);
             const int64_t h = param_int(s, kKeyTiffTileHeight, 0);
             if ((w > 0) != (h > 0))
                 return en ? "param '" + std::string(kKeyTiffTileWidth) + "'/'" +
                                 std::string(kKeyTiffTileHeight) +
                                 "': both must be > 0 (tiled) or 0 (strips)"
                           : "param '" + std::string(kKeyTiffTileWidth) + "'/'" +
                                 std::string(kKeyTiffTileHeight) +
                                 "': 两者必须同为 > 0（tiled）或同为 0（strips）";
             return {};
         }},
        // 规则① webp + lossy：qmin ≤ qmax（原 cross_validate 内嵌，中文文案逐字保留）
        {std::string_view("webp"),
         std::string_view("lossy"),
         true,
         {kKeyQmin, kKeyQmax},
         [](const ParamSet &s, bool en) -> std::string {
             if (param_int(s, kKeyQmin, 0) > param_int(s, kKeyQmax, 0))
                 return en ? "param '" + std::string(kKeyQmin) + "' must be <= '" +
                                 std::string(kKeyQmax) + "'"
                           : "qmin 不能大于 qmax";
             return {};
         }},
        // 规则② jpeg：启用渐进式时必须启用哈夫曼表优化（原 cross_validate 内嵌）
        {std::string_view("jpeg"),
         std::string_view(),
         true,
         {kKeyProgressive, kKeyOptimizeCoding},
         [](const ParamSet &s, bool en) -> std::string {
             if (param_bool(s, kKeyProgressive, false) && !param_bool(s, kKeyOptimizeCoding, true))
                 return en ? "optimize_coding must be enabled when progressive is enabled"
                           : "启用渐进式时必须启用哈夫曼表优化";
             return {};
         }},
        // 规则③ jxl：无损 + resampling>1 = 假无损（JXL-01；编码器另有兜底锁 1）
        {std::string_view("jxl"),
         std::string_view(),
         false,
         {kLosslessParamKey, "resampling"},
         [](const ParamSet &s, bool en) -> std::string {
             if (lossless_flag(s) && param_int(s, "resampling", -1) > 1)
                 return en ? "lossless encoding cannot be combined with resampling > 1"
                           : "无损模式下不能降采样（resampling 必须为 1）";
             return {};
         }},
    };
    return rules;
}

bool rule_applies(const CrossRule &r, const std::string &format_id, const std::string &tech_id,
                  const ParamSet &values) {
    if (!r.format_id.empty() && r.format_id != format_id)
        return false;
    if (!r.tech_id.empty() && r.tech_id != effective_tech_id(format_id, tech_id))
        return false;
    if (r.require_all_keys) {
        for (std::string_view k : r.keys)
            if (values.find(std::string(k)) == values.end())
                return false; // 缺键 = 无法判定（缺失=默认值，由 fill_defaults 负责）
    }
    return true;
}

} // namespace

bool lossless_flag(const ParamSet &s) {
    // 显式 schema 参数优先；缺失（未声明该参数的格式，或 0.2 形态的调用方）→ 内部管道键
    return param_bool(s, kLosslessParamKey, param_bool(s, kLosslessKey, false));
}

// R-208（M7-WP2）：无损"显式 schema 参数"声明判定单源 —— 原 presets.cpp 的
// `declares_lossless` 与本文件的 `declares_lossless_param` 语义并存（后者只看静态技术表，
// 前者对 libheif 系恒真）。合并口径：
//   * 静态表：匹配到的技术声明了 kLosslessParamKey 才算（jxl/webp 的 lossless_param 行）；
//   * libheif 系（runtime_introspected）：技术来自运行时内省，静态表为空 → 视为声明
//     （内省面本就暴露 `lossless` bool，enc_heif 消费它；0.3.0 v1 迁移口径一致）；
//   * 其余格式（jpeg 无 lossless 技术、png/tiff/bmp 技术本身无损且不声明该参数）→ 不声明。
bool declares_lossless(const FormatDef &f, const std::string &backend_id,
                       const std::string &tech_id) {
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return false;
    if (b->runtime_introspected)
        return true;
    const TechDef *t = tech_id.empty() ? select_tech(*b, {}, false) : find_tech(*b, tech_id);
    return tech_declares_lossless(t);
}

// R-208（M7-WP2）：技术选择单源 —— 原 params.cpp 内部 `select_tech` 与 presets.cpp 的
// `pick_tech` 逐字重复（13 组镜像副本清单之一），现导出为唯一实现。
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

// R-195（M7-R2B）：ParamDef 查找决胜规则单源 —— **first-wins**（表序 = 优先级）。
// 旧双实现决胜相反（paramform/preset_io 首命中返回 vs dev_cli set_param 循环覆盖 =
// last-wins）；收敛后全部调用面经本函数（跨界转发登记见 handoff「跨界需求」）。
const ParamDef *find_param(const TechDef &t, std::string_view key) {
    for (const ParamDef &p : t.params)
        if (p.key == key)
            return &p;
    return nullptr;
}

const ParamDef *lookup_param_def(const FormatDef &f, const std::string &backend_id,
                                 const std::string &tech_id, std::string_view key) {
    const BackendDef *b = find_backend(f, backend_id);
    if (!b)
        return nullptr;
    if (const TechDef *t = find_tech(*b, tech_id))
        if (const ParamDef *p = find_param(*t, key))
            return p;                 // 命中技术内 first-wins
    for (const TechDef &t : b->techs) // 回退：后端全部技术并集扫描（first-wins，表序优先）
        if (const ParamDef *p = find_param(t, key))
            return p;
    return nullptr;
}

// R-211（M7-R2B）：位深单位判定与 bps↔bpp 换算单源（口径见 params.h 尾部加性块）。
BitdepthUnit bitdepth_unit(std::string_view format_id) {
    return format_id == "bmp" ? BitdepthUnit::BitsPerPixel : BitdepthUnit::BitsPerSample;
}

std::string_view bitdepth_unit_label(std::string_view format_id) {
    return bitdepth_unit(format_id) == BitdepthUnit::BitsPerPixel ? "bpp" : "bps";
}

int bitdepth_bpp(std::string_view format_id, int bitdepth) {
    // 存储值 → 每像素位数（RGB 三样本打包口径）：bmp 存储值已是 bpp → 恒等；其余 bps × 3。
    return bitdepth_unit(format_id) == BitdepthUnit::BitsPerPixel ? bitdepth : bitdepth * 3;
}

int bitdepth_bps(std::string_view format_id, int bitdepth) {
    // 存储值 → 每样本位数：非 bmp 恒等；bmp 24 bpp（RGB）→ 8 bps、8 bpp（灰度）→ 8 bps。
    if (bitdepth_unit(format_id) != BitdepthUnit::BitsPerPixel)
        return bitdepth;
    return bitdepth == 24 ? 8 : bitdepth;
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
    for (const ParamDef &p : t->params) {
        if (p.has_default) // AVIF-01：可选覆盖参数（has_default=false）不写默认值
            s[p.key] = p.def;
    }
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
    // 0.3.0/M4-T13：无损的**两个承载面同源写入**（单一写入点 —— 本函数是 default_params/
    // normalize_preset/表单 refresh 的共同出口，故两键恒一致）：
    //   * 内部管道键 `__lossless`：谓词/编码器的既有输入（0.2 起，保留）；
    //   * 显式 schema 参数 `lossless`：用户面（表单复选框）/预设 v2 序列化面；仅当选定技术
    //     声明了它才写（否则会变成该格式的"未知参数"）。
    if (const auto it = s.find(std::string(kLosslessKey));
        it == s.end() || !(it->second == ParamValue{lossless})) {
        s[std::string(kLosslessKey)] = lossless;
        changed.push_back(std::string(kLosslessKey));
    }
    if (tech_declares_lossless(t)) {
        probe[std::string(kLosslessParamKey)] = lossless;
        const auto it = s.find(std::string(kLosslessParamKey));
        if (it == s.end() || !(it->second == ParamValue{lossless})) {
            s[std::string(kLosslessParamKey)] = lossless;
            changed.push_back(std::string(kLosslessParamKey));
        }
    }
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
    // 跨字段规则（R-206：单源规则表，M7-R2B 起两通道共用全部规则；本通道取英文文案，
    // 逐字保留 0.3.2 现文案）。
    for (const CrossRule &r : cross_rules()) {
        if (!rule_applies(r, f.id, tech_id, s))
            continue;
        const std::string err = r.check(s, /*english=*/true);
        if (!err.empty())
            return err;
    }
    return {};
}

// ---------------------------------------------------------------------------
// M2-T5 §2.7：交叉参数约束（#16 销账；per-key 谓词表达不了的跨字段规则）
// ---------------------------------------------------------------------------
std::vector<std::string> cross_validate(const ParamSet &values, const std::string &format_id,
                                        const std::string &tech_id) {
    std::vector<std::string> out;

    // 跨字段规则（R-206：单源规则表，见本文件 cross_rules()；M7-R2B 起两通道共用全部规则，
    // 本通道取中文文案，逐字保留 0.3.2 现文案）。规则①（webp qmin ≤ qmax）与规则②（jpeg
    // 渐进式 ⇒ 哈夫曼表优化）已收进规则表；规则②的条件订正依据（M2-T5 父裁定）：真实约束来自
    // jpegli ——
    //   tools/cjpegli.cc:146 `progressive_level > 0 && !optimize_coding` → 报
    //   "--fixed_code must be used together with -p 0"；
    //   enc_jpegli.cpp `cinfo.optimize_coding = (progressive || optimize_coding)`
    //   ⇒ (true,true) 是规范化态、(true,false) 才是非法态。两参数默认值（true/true）
    //   天然不报（§4 T5）。
    for (const CrossRule &r : cross_rules()) {
        if (!rule_applies(r, format_id, tech_id, values))
            continue;
        const std::string msg = r.check(values, /*english=*/false);
        if (!msg.empty())
            out.push_back(msg);
    }

    // M4-T13 记账：**不**为"显式 lossless 参数 vs 技术能力"新增交叉规则。实测依据：
    //   cross_validate 的入参只有 (values, format_id, tech_id)，而 tech_id 允许为空 =
    //   "引擎按 lossless 自行选定技术"（select_tech：空 id + lossless → 首个 lossless_capable）。
    //   金样 multiformat-split/mirror/conflict（`--outputs jpeg:jpegli,webp:libwebp --lossless`，
    //   tech 为空）正是这一形态：参数集来自 webp/lossless，而 effective_tech_id("webp", "")
    //   解析出的是首技术 webp/lossy ⇒ 判"自相矛盾"会误杀三条冻结金样（实测 3/3 复现）。
    //   "lossless ⇒ lossless_capable 技术"不变式由**写入点**保证，不在此重复执法：
    //     * params.cpp   apply_locks()：两键唯一写入点（test_params 18② 断言）；
    //     * paramform.cpp user_set_lossless()/set_selection()：无损 ⇒ 自动切 lossless_capable
    //     技术；
    //     * presets.cpp  pick_tech()：预设归一时 lossless 优先 lossless_capable 技术。
    //   本函数语义与消息文本维持 M2-T5 冻结形态（下方规则③"未知参数"拒绝语义一字不动）。

    // 规则③ 未知参数（M2-T5 父裁定新增）：该 format 全部技术声明的键并集之外的键。
    //   * 保留键（"__" 前缀，如 __lossless）是引擎内部键，不参与判定；
    //   * libheif 插件的 `x265:` / `svt:` / `aom:` 透传键不在内省表里（heif_encoder_list_parameters
    //     只列强类型参数），但插件 set_parameter_string 明确接受并转发给编码器 —— 对 heif/avif
    //     不判未知（enc_heif 的透传路径消费；拒绝/拼错由插件的错误码回传并记日志）；
    //   * ParamSet 是 std::map → 迭代即字典序，多条消息天然稳定排序；
    //   * 该 format 任何技术都不认识的键才是未知（跨技术残留键在并集内 → 不报）。
    const DeclaredKeys &declared = declared_keys(format_id);
    if (declared.determinable) {
        for (const auto &[key, value] : values) {
            (void)value;
            if (key.rfind("__", 0) == 0)
                continue;
            if (is_plugin_passthrough_key(format_id, key))
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
        if (!p.has_default)
            continue; // 可选覆盖参数不得被填成范围下限（AVIF-01）
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
