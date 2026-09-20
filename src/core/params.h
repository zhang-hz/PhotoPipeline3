// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — ParamSchema (M0 frozen)
#pragma once
#include <functional>
#include <optional>
#include <utility>
#include "core/types.h"

namespace pp {

enum class ParamType { Int, Float, Bool, Enum };

struct ParamDef {
    std::string key;
    std::string label;      // Chinese label, UTF-8
    ParamType   type = ParamType::Int;
    ParamValue  def;                            // default (visual-transparent tier)
    double      lo = 0, hi = 0, step = 1;        // Int / Float
    std::vector<std::pair<std::string, ParamValue>> choices;  // Enum {name, value}
    bool        advanced = false;
    std::string tooltip;    // Chinese explanation + English term
    // Predicates (optional; filled in M1, empty in M0)
    std::function<bool(const ParamSet&)> visible;
    std::function<std::optional<ParamValue>(const ParamSet&)> locked;
};

struct TechDef {
    std::string id, label;
    bool lossless_capable = false;
    std::vector<ParamDef> params;
};

struct BackendDef {
    std::string id, label;  // e.g. "svt-av1" / "libaom"
    bool runtime_introspected = false;  // true for libheif-based formats
    std::vector<TechDef> techs;
};

struct FormatDef {
    std::string id, label, ext;  // e.g. "jxl"
    std::vector<BackendDef> backends;
    std::vector<int> bitdepths;  // {8} / {8,16} ...
    bool supports_alpha = false;
    bool supports_gray  = false;
    std::string meta_path;       // "exiv2" | "libheif" | "jxl-box" | "none"
};

// Static format tables. libheif-based formats (heif/avif) have empty techs and
// runtime_introspected=true; their params come from libheif at runtime (M1).
const std::vector<FormatDef>& static_formats();

// PP-FROZEN(block): 追加在 static_formats() 声明之后
// —— 参数引擎 ——
// 求 lock：无 locked 谓词或返回 nullopt → 未锁定；返回有值 → 该值为强制值
std::optional<ParamValue> eval_lock(const ParamDef& p, const ParamSet& s);
bool eval_visible(const ParamDef& p, const ParamSet& s);

// 取默认值全集（含 lossless 技术与技术默认选择）
ParamSet default_params(const FormatDef& f, const std::string& backend_id,
                        const std::string& tech_id, bool lossless);

// 应用锁定：把所有被锁定参数的当前值改写为强制值（返回被改写的 key 列表）
std::vector<std::string> apply_locks(const FormatDef& f, const std::string& backend_id,
                                     const std::string& tech_id, bool lossless, ParamSet& s);

// 类型安全取值（缺失或类型不符 → 返回 fallback）
int64_t param_int(const ParamSet& s, std::string_view key, int64_t fallback);
double  param_float(const ParamSet& s, std::string_view key, double fallback);
bool    param_bool(const ParamSet& s, std::string_view key, bool fallback);
std::string param_str(const ParamSet& s, std::string_view key, std::string_view fallback);

// 校验：范围/枚举成员/类型；返回空串=通过，否则错误描述（英文）
std::string validate_params(const FormatDef& f, const std::string& backend_id,
                            const std::string& tech_id, bool lossless, const ParamSet& s);

// 查找：找不到返回 nullptr
const FormatDef* find_format(std::string_view id);
const BackendDef* find_backend(const FormatDef& f, std::string_view id);
const TechDef* find_tech(const BackendDef& b, std::string_view id);

// 旧预设迁移：缺失 key 用默认值补齐（返回补的 key 列表）
std::vector<std::string> fill_defaults(const FormatDef& f, const std::string& backend_id,
                                       const std::string& tech_id, bool lossless, ParamSet& s);

// 参数快照（日志用）："key=value key=value"（按 key 字典序，value 统一字符串化）
std::string snapshot_params(const ParamSet& s);

// M2-T5: cross-field constraints the per-key predicates cannot express.
// Empty result = OK. Messages are user-facing (frozen texts).
std::vector<std::string> cross_validate(const ParamSet& values,
                                        const std::string& format_id,
                                        const std::string& tech_id);

}  // namespace pp
