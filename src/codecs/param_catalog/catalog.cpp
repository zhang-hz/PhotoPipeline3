// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 参数目录实现（数据由 tools/gen_param_catalog.py 生成）。
#include "codecs/param_catalog/catalog.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <utility>

namespace pp::catalog {
namespace {

ParamValue parse_def(const CatalogSpec &s) {
    switch (s.type) {
    case Type::Int:
        if (s.def.empty())
            return int64_t{0};
        try {
            return static_cast<int64_t>(std::stoll(s.def));
        } catch (...) {
            return int64_t{0};
        }
    case Type::Float:
        if (s.def.empty())
            return 0.0;
        try {
            return std::stod(s.def);
        } catch (...) {
            return 0.0;
        }
    case Type::Bool:
        return s.def == "1" || s.def == "true";
    case Type::Enum: {
        for (const auto &c : s.choices) {
            if (c.first == s.def)
                return c.second;
        }
        return s.choices.empty() ? ParamValue{std::monostate{}} : s.choices.front().second;
    }
    case Type::Text:
        return s.def;
    }
    return ParamValue{std::monostate{}};
}

ParamType param_type(Type t) {
    switch (t) {
    case Type::Int:
        return ParamType::Int;
    case Type::Float:
        return ParamType::Float;
    case Type::Bool:
        return ParamType::Bool;
    case Type::Enum:
        return ParamType::Enum;
    case Type::Text:
        return ParamType::String;
    }
    return ParamType::Int;
}

CatalogRow to_row(const CatalogSpec &s) {
    CatalogRow r;
    r.type = s.type;
    r.engine_key = s.engine;
    r.scope = s.scope;
    r.form = s.form;
    r.role = s.role;
    r.tech = s.tech;
    r.rule_ids = s.rule_ids;
    r.reason = s.reason;
    r.def.key = s.key;
    r.def.label = s.label;
    r.def.type = param_type(s.type);
    r.def.def = parse_def(s);
    r.def.lo = s.lo;
    r.def.hi = s.hi;
    r.def.step = s.step;
    r.def.choices = s.choices;
    r.def.advanced = true;
    r.def.tooltip = s.tooltip;
    r.def.group = s.group;
    r.def.has_default = false;
    return r;
}

#define PP_ROW CatalogSpec

std::vector<CatalogSpec> x265_specs() {
    return {
#include "x265_catalog.inc"
    };
}

std::vector<CatalogSpec> aom_specs() {
    return {
#include "aom_catalog.inc"
    };
}

std::vector<CatalogSpec> svt_specs() {
    return {
#include "svt_catalog.inc"
    };
}

std::vector<CatalogSpec> jxl_specs() {
    return {
#include "jxl_catalog.inc"
    };
}

#undef PP_ROW

using CatalogTable = std::map<std::pair<std::string, std::string>, std::vector<CatalogRow>>;

const CatalogTable &table() {
    static const CatalogTable built = [] {
        CatalogTable t;
        auto add = [&t](std::string_view format, std::string_view backend,
                        const std::vector<CatalogSpec> &specs) {
            std::vector<CatalogRow> rows;
            rows.reserve(specs.size());
            for (const CatalogSpec &s : specs)
                rows.push_back(to_row(s));
            t.emplace(std::make_pair(std::string(format), std::string(backend)), std::move(rows));
        };
        add("heif", "x265", x265_specs());
        add("avif", "libaom", aom_specs());
        add("avif", "svt-av1", svt_specs());
        add("jxl", "libjxl", jxl_specs());
        return t;
    }();
    return built;
}

const std::vector<CatalogRow> &empty_rows() {
    static const std::vector<CatalogRow> empty;
    return empty;
}

} // namespace

const std::vector<CatalogRow> &catalog(std::string_view format_id, std::string_view backend_id) {
    const auto it = table().find({std::string(format_id), std::string(backend_id)});
    return it == table().end() ? empty_rows() : it->second;
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters)：全链查找的三个 string_view
// （format/backend/key）语义固定、调用点全部具名，属可读性成本而非缺陷。
const CatalogRow *find_row(std::string_view format_id, std::string_view backend_id,
                           std::string_view key) {
    const auto &rows = catalog(format_id, backend_id);
    const auto it = std::find_if(rows.begin(), rows.end(),
                                 [key](const CatalogRow &r) { return r.def.key == key; });
    return it == rows.end() ? nullptr : &*it;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

bool has_key(std::string_view format_id, std::string_view backend_id, std::string_view key) {
    return find_row(format_id, backend_id, key) != nullptr;
}

std::vector<ParamDef> param_defs(std::string_view format_id, std::string_view backend_id,
                                 TechScope tech) {
    std::vector<ParamDef> out;
    for (const CatalogRow &r : catalog(format_id, backend_id)) {
        if (r.scope != Scope::Settable)
            continue;
        if (tech != TechScope::Both && r.tech != TechScope::Both && r.tech != tech)
            continue;
        out.push_back(r.def);
    }
    return out;
}

CatalogCounts counts(std::string_view format_id, std::string_view backend_id) {
    CatalogCounts c;
    for (const CatalogRow &r : catalog(format_id, backend_id)) {
        ++c.total;
        if (r.scope == Scope::Settable)
            ++c.settable;
        else if (r.scope == Scope::ReadOnly)
            ++c.readonly;
    }
    return c;
}

std::string_view type_name(Type t) {
    switch (t) {
    case Type::Int:
        return "int";
    case Type::Float:
        return "float";
    case Type::Bool:
        return "bool";
    case Type::Enum:
        return "enum";
    case Type::Text:
        return "text";
    }
    return "?";
}

std::string_view scope_name(Scope s) {
    switch (s) {
    case Scope::Settable:
        return "settable";
    case Scope::ReadOnly:
        return "readonly";
    case Scope::Forbidden:
        return "forbidden";
    case Scope::NoEffect:
        return "noeffect";
    }
    return "?";
}

std::string_view form_name(Form f) {
    switch (f) {
    case Form::Mode:
        return "mode";
    case Form::Number:
        return "number";
    case Form::Dropdown:
        return "dropdown";
    case Form::Switch:
        return "switch";
    case Form::Text:
        return "text";
    case Form::ReadOnly:
        return "readonly";
    }
    return "?";
}

std::string_view role_name(Role r) {
    switch (r) {
    case Role::Anchor:
        return "anchor";
    case Role::Dependent:
        return "dependent";
    case Role::Independent:
        return "independent";
    }
    return "?";
}

std::string_view tech_name(TechScope t) {
    switch (t) {
    case TechScope::Both:
        return "both";
    case TechScope::Vardct:
        return "vardct";
    case TechScope::Modular:
        return "modular";
    }
    return "?";
}

bool is_known_group(std::string_view group) {
    return group == kGroupQuality || group == kGroupAnalysis || group == kGroupPsy ||
           group == kGroupQuant || group == kGroupLoop || group == kGroupThread ||
           group == kGroupStream || group == kGroupDebug || group == kGroupReadOnly;
}

} // namespace pp::catalog
