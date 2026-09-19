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
    std::vector<TechDef> techs;
    bool runtime_introspected = false;  // true for libheif-based formats
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

}  // namespace pp
