// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `core/presets.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/presets.h` | `PresetData` **[重排]** → schema v2：`outputs: [OutputFormatSpec]` 数组 + `output_template`/`split_by_format`；提供 `migrate_preset_v1()`（0.2 单格式 JSON → v2 单元素数组），加载旧预设零丢失 |
// clang-format on
//   落地任务 = W3-T13（输出页：预设 v2（迁移），出口含 `test_presets` v2 迁移绿）
//   → 落地后改标 PP-FROZEN(0.3.0)。
//   [重排] PresetData：字段序列变化，不再保证聚合初始化兼容（一次性接受）。
//   本文件其余既有声明（validate_preset / normalize_preset）不在本表行内 → 维持 PP-FROZEN 只读。
#pragma once
#include "core/metadata.h"
#include "core/pipeline.h"
#include <optional>
#include <string>
#include <vector>

namespace pp {

// PP-THAWED(0.3.0-M4-D20) §3.6 · PresetData（schema v2，[重排]）+ migrate_preset_v1()
//   0.3.0 冻结形态（设计 §3.6 逐字抄录）：`PresetData` → schema v2：`outputs: [OutputFormatSpec]`
//   数组 + `output_template`/`split_by_format`；提供 `migrate_preset_v1()`（0.2 单格式 JSON →
//   v2 单元素数组），加载旧预设零丢失。
//   建议形态（§3.6 未逐字给出字段类型/默认值 + 是否保留 0.2 单格式字段 → T13
//   落地时确认，非本任务改动）：
// clang-format off
//     int version = 2;                                   // schema v2（0.2 现形 version = 1）
//     std::vector<OutputFormatSpec> outputs;             // §3.2 OutputFormatSpec 数组（替代 0.2 单格式字段）
//     std::string output_template = "$format/$dir/$file"; // §3.2 同名默认（§4.2）
//     bool        split_by_format = false;               // §3.2 同名默认
//     // migrate_preset_v1()：0.2 单格式 JSON（format_id/backend_id/tech_id/lossless/out_bitdepth/
//     //   params/color_target/conflict/rules）→ v2 单元素 outputs 数组；加载旧预设零丢失。
// clang-format on
//   注（0.2 现形差异，[重排]）：现形 `int version = 1;` + 逐字段 `format_id/backend_id/tech_id/
//   lossless/out_bitdepth/params` —— v2 后由 `outputs[0]`（OutputFormatSpec：无 lossless 字段，
//   见 §3.2）承载；`color_target`/`conflict`/`rules` 的保留形态由 T13 定稿。
struct PresetData {
    int version = 1;
    std::string name;
    std::string format_id, backend_id, tech_id;
    bool lossless = false;
    int out_bitdepth = 8;
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    ParamSet params;
    BatchRules rules;
};

// 校验：格式/后端/技术存在、参数合法、位深在该格式集合内、仅元数据模式格式受限
// 返回空串=通过
std::string validate_preset(const PresetData &p);

// 用当前格式表补齐缺失参数（返回补齐的 key 列表）
std::vector<std::string> normalize_preset(PresetData &p);

} // namespace pp
