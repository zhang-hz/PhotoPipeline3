// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — ParamSchema (M0 frozen)
#pragma once
#include "core/types.h"
#include <functional>
#include <optional>
#include <utility>

namespace pp {

enum class ParamType { Int, Float, Bool, Enum };

struct ParamDef {
    std::string key;
    std::string label; // Chinese label, UTF-8
    ParamType type = ParamType::Int;
    ParamValue def;                                          // default (visual-transparent tier)
    double lo = 0, hi = 0, step = 1;                         // Int / Float
    std::vector<std::pair<std::string, ParamValue>> choices; // Enum {name, value}
    bool advanced = false;
    std::string tooltip; // Chinese explanation + English term
    // Predicates (optional; filled in M1, empty in M0)
    std::function<bool(const ParamSet &)> visible;
    std::function<std::optional<ParamValue>(const ParamSet &)> locked;
};

struct TechDef {
    std::string id, label;
    bool lossless_capable = false;
    std::vector<ParamDef> params;
};

struct BackendDef {
    std::string id, label;             // e.g. "svt-av1" / "libaom"
    bool runtime_introspected = false; // true for libheif-based formats
    std::vector<TechDef> techs;
};

struct FormatDef {
    std::string id, label, ext; // e.g. "jxl"
    std::vector<BackendDef> backends;
    std::vector<int> bitdepths; // {8} / {8,16} ...
    bool supports_alpha = false;
    bool supports_gray = false;
    std::string meta_path; // "exiv2" | "libheif" | "jxl-box" | "none"
};

// Static format tables. libheif-based formats (heif/avif) have empty techs and
// runtime_introspected=true; their params come from libheif at runtime (M1).
const std::vector<FormatDef> &static_formats();

// PP-FROZEN(block): 追加在 static_formats() 声明之后
// —— 参数引擎 ——
// 求 lock：无 locked 谓词或返回 nullopt → 未锁定；返回有值 → 该值为强制值
std::optional<ParamValue> eval_lock(const ParamDef &p, const ParamSet &s);
bool eval_visible(const ParamDef &p, const ParamSet &s);

// PP-FROZEN(0.3.0) §3.6 · 无损的两个承载面（**T13 定稿：内部键 `__lossless` 保留，不退场**）
//   * `kLosslessParamKey` = "lossless"：**显式 schema 参数**（format_tables.cpp 的 Bool，给
//     lossless-capable 的技术声明：jxl vardct/modular、webp lossy/lossless；heif/avif 由
//     libheif 运行时内省提供）。它是用户可见可设、可序列化（预设 v2 的 params）的面。
//   * `kLosslessKey` = "__lossless"：**内部管道键**（0.3.0 保留，不落盘、不显示、不进 values()）：
//     谓词（eval_visible/eval_lock）与编码器（enc_jxl/enc_webp）的既有输入。
//   两键的一致性口径（T13 复核项 5 措辞修正）：`apply_locks()` 是**引擎侧**的同步点（在选定技术
//   声明了显式键时按 lossless 入参写两键；default_params/normalize_preset 都经它）；表单
//   （paramform 的复选框 → `values[__lossless]`，声明时另写 `values[lossless]`）与输出页
//   （`spec_of()` 写 `__lossless`）在各自集合里写入**同一个** `sel.lossless`，随后同样经
//   apply_locks 收口 → 当前无漂移路径；读取统一走 lossless_flag()，禁止各调用方自行分叉。
//   去留裁定（T13 出口硬项之一，二选一）：**保留** —— 退场需要改 12 处谓词 + 2 个编码器的读取点，
//   且会让 0.2 形态调用方（无显式键）失去无损语义；保留 + 单点同步的代价最小、零行为漂移。
//   非声明格式（jpeg/png/tiff/bmp）的落盘口径见 src/ui/preset_io.cpp 的 `outputs[i].lossless`。
inline constexpr std::string_view kLosslessParamKey = "lossless";
inline constexpr std::string_view kLosslessKey = "__lossless";

// PP-FROZEN(0.3.0) · 无损标志读取（**处置顺序**：显式 schema 参数优先 → 内部管道键 → false）。
// 预设 v1 迁移、输出页 config_base/collect_preset、presets 校验/归一都走这一条，杜绝两张皮。
bool lossless_flag(const ParamSet &s);

// 取默认值全集（含 lossless 技术与技术默认选择）
ParamSet default_params(const FormatDef &f, const std::string &backend_id,
                        const std::string &tech_id, bool lossless);

// 应用锁定：把所有被锁定参数的当前值改写为强制值（返回被改写的 key 列表）
std::vector<std::string> apply_locks(const FormatDef &f, const std::string &backend_id,
                                     const std::string &tech_id, bool lossless, ParamSet &s);

// 类型安全取值（缺失或类型不符 → 返回 fallback）
int64_t param_int(const ParamSet &s, std::string_view key, int64_t fallback);
double param_float(const ParamSet &s, std::string_view key, double fallback);
bool param_bool(const ParamSet &s, std::string_view key, bool fallback);
std::string param_str(const ParamSet &s, std::string_view key, std::string_view fallback);

// 校验：范围/枚举成员/类型；返回空串=通过，否则错误描述（英文）
std::string validate_params(const FormatDef &f, const std::string &backend_id,
                            const std::string &tech_id, bool lossless, const ParamSet &s);

// 查找：找不到返回 nullptr
const FormatDef *find_format(std::string_view id);
const BackendDef *find_backend(const FormatDef &f, std::string_view id);
const TechDef *find_tech(const BackendDef &b, std::string_view id);

// 旧预设迁移：缺失 key 用默认值补齐（返回补的 key 列表）
std::vector<std::string> fill_defaults(const FormatDef &f, const std::string &backend_id,
                                       const std::string &tech_id, bool lossless, ParamSet &s);

// 参数快照（日志用）："key=value key=value"（按 key 字典序，value 统一字符串化）
std::string snapshot_params(const ParamSet &s);

// M2-T5: cross-field constraints the per-key predicates cannot express.
// Empty result = OK. Messages are user-facing (frozen texts).
std::vector<std::string> cross_validate(const ParamSet &values, const std::string &format_id,
                                        const std::string &tech_id);

} // namespace pp
