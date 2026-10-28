// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 参数目录（catalog）公共接口。
//
// 契约：docs/m8-encoder-params-and-ssimulacra2-design.md C1/C2；
//       控件形态：docs/m8-param-control-map.md；数据：tools/param_catalog_specs/*.json
//       → tools/gen_param_catalog.py → codecs/param_catalog/*_catalog.inc（生成物，勿手改）。
//
// 本文件不冻结；目录 API 通过 params.h 加性转发（B0）。Stage A 只提供数据与查询，
// 不并入 introspect_backends()/static_formats()，保证零用户可见行为变化。
#pragma once
#include "core/params.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pp::catalog {

// 目录行类型：Text 在 Stage A 用 ParamType::Enum 占位，B0 映射到 ParamType::String。
enum class Type : unsigned char { Int, Float, Bool, Enum, Text };

// 呈现状态：Settable=可设置；ReadOnly=上游固定/推导值；Forbidden=禁止（预检错误）；
// NoEffect=单帧无效果。Stage A 只落 Settable/ReadOnly 行。
enum class Scope : unsigned char { Settable, ReadOnly, Forbidden, NoEffect };

// 控件形态 M/N/D/S/T/R（docs/m8-param-control-map.md §1.2）。
enum class Form : unsigned char { Mode, Number, Dropdown, Switch, Text, ReadOnly };

// 联动角色（§1.3）。
enum class Role : unsigned char { Anchor, Dependent, Independent };

// JXL 技术作用域；其余后端恒为 Both。
enum class TechScope : unsigned char { Both, Vardct, Modular };

// 分组词表单源（与 params.h kGroup* 共用显示层；B2 映射到折叠分组）。
inline constexpr const char *kGroupQuality = "质量/码率";
inline constexpr const char *kGroupAnalysis = "分析工具";
inline constexpr const char *kGroupPsy = "心理视觉";
inline constexpr const char *kGroupQuant = "量化/变换";
inline constexpr const char *kGroupLoop = "环路滤波";
inline constexpr const char *kGroupThread = "分块/线程";
inline constexpr const char *kGroupStream = "码流/兼容性";
inline constexpr const char *kGroupDebug = "调试/实验";
inline constexpr const char *kGroupReadOnly = "只读（上游固定）";

// 生成物行格式（tools/gen_param_catalog.py 输出；字段顺序即本结构声明顺序）。
struct CatalogSpec {
    std::string key;
    std::string engine;
    std::string label;
    Type type = Type::Int;
    double lo = 0.0;
    double hi = 0.0;
    double step = 1.0;
    std::vector<std::pair<std::string, ParamValue>> choices;
    std::string def;
    Form form = Form::Number;
    Role role = Role::Independent;
    Scope scope = Scope::Settable;
    TechScope tech = TechScope::Both;
    std::string group;
    std::string rule_ids;
    std::string reason;
    std::string tooltip;
};

// 已构建目录行：ParamDef + 形态/角色/状态/规则引用。
struct CatalogRow {
    ParamDef def;
    Type type = Type::Int;
    std::string engine_key;
    Scope scope = Scope::Settable;
    Form form = Form::Number;
    Role role = Role::Independent;
    TechScope tech = TechScope::Both;
    std::string rule_ids;
    std::string reason;
};

struct CatalogCounts {
    std::size_t total = 0;
    std::size_t settable = 0;
    std::size_t readonly = 0;
};

// 某 (format, backend) 的目录（静态、首次调用构建）。未知后端返回空。
const std::vector<CatalogRow> &catalog(std::string_view format_id, std::string_view backend_id);

// 按 key 查目录行（找不到返回 nullptr）；key 为 GUI/ParamSet 键（含前缀）。
const CatalogRow *find_row(std::string_view format_id, std::string_view backend_id,
                           std::string_view key);

// B0 合并用：目录内可设置行 → ParamDef（tech 过滤；Text 暂以 Enum 占位）。
std::vector<ParamDef> param_defs(std::string_view format_id, std::string_view backend_id,
                                 TechScope tech = TechScope::Both);

bool has_key(std::string_view format_id, std::string_view backend_id, std::string_view key);

CatalogCounts counts(std::string_view format_id, std::string_view backend_id);

// 词表/诊断（生成脚本与测试共用；非法值返回 "?"）。
std::string_view type_name(Type t);
std::string_view scope_name(Scope s);
std::string_view form_name(Form f);
std::string_view role_name(Role r);
std::string_view tech_name(TechScope t);
bool is_known_group(std::string_view group);

} // namespace pp::catalog
