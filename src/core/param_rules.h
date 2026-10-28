// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M8 联动规则引擎接口（B0 冻结；B1 实现，B2 消费）。
//
// 唯一规则源：参数可见性/可设置性/锁定/警告/错误全部从本接口求值；
// UI 实时态与运行前预检不得各写一套判断。规则数据/文案见
// docs/m8-param-control-map.md（X1–X17 / A1–A16 / S1–S6 / J1–J18）。
//
// 语义：
//   * evaluate() 纯函数，不读写全局状态；输入 = 当前上下文 + 当前 ParamSet；
//   * hidden/disabled 只影响 UI 呈现，不改变 ParamSet；locked 由 UI 显示为只读；
//   * warnings/errors 是用户可见中文；errors 非空 ⇒ 禁止开始运行（preflight 同源）。
//   * 旧 cross_validate(values, format_id, tech_id) 行为/文案零改动（B1 转发到新引擎时
//     仅新增规则，既有规则逐字保留）。
#pragma once
#include "core/params.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace pp::rules {

struct RuleState {
    std::set<std::string> hidden;                 // 行隐藏（键不出现）
    std::set<std::string> disabled;               // 行置灰（可见但不可改）
    std::map<std::string, ParamValue> locked;     // 强制值（UI 只读显示）
    std::vector<std::string> warnings;            // 中文提醒（不阻断）
    std::vector<std::string> errors;              // 中文错误（阻断运行）
};

// 求值：模式锚点（后端/技术/无损/RC/位深/色度）→ 档位锚点 → 依赖行。
RuleState evaluate(const ParamContext &ctx, const ParamSet &values);

// 运行前预检（批量只报一次的最外层由 UI 负责）；等价于 evaluate().errors。
std::vector<std::string> preflight(const ParamContext &ctx, const ParamSet &values);

} // namespace pp::rules
