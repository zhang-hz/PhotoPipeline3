// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "core/metadata.h"
#include "core/pipeline.h"

namespace pp {

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
std::string validate_preset(const PresetData& p);

// 用当前格式表补齐缺失参数（返回补齐的 key 列表）
std::vector<std::string> normalize_preset(PresetData& p);

}  // namespace pp
