// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — M10 W2-T3（C20）：tune 静态事实表（新文件，不并入 enc_common）。
//
// 契约：docs/v0.4.0-design.md §6.3（C20）；事实来源 docs/param-catalog.md §3/§4 +
//       build/design-recon/heif/report.md §5（gitignored）。本表是**静态兜底**：
//   1) 运行时内省仍是第一来源（heif_encoder_list_parameters 给插件原生值集）；
//   2) 插件不支持但库支持/可仿真的值由本表补齐（Kind::Simulated / Kind::Disabled）；
//   3) 一致性由 tests/unit/test_heif_tune.cpp 机械断言：
//      原生事实 ⊆ 运行期接受集、运行期接受集 ⊆ 本表、Simulated/Disabled ∉ 运行期接受集。
//
// 本文件不冻结；值/标签/原因单源供 enc_heif（下拉选项 + 编码期处理）、paramform（置灰 +
// item tooltip）、param_rules（中文预检）与单测共用 —— 同一事实只写一次。
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace pp::tune {

// 类别：Native=插件原生接受；Simulated=插件不认、由引擎内部通道/注入实现；
// Disabled=本构建不可用（下拉保留位置 + 置灰 + 原因，不删除、不撒谎）。
enum class Kind : unsigned char { Native, Simulated, Disabled };

struct ValueFact {
    std::string_view value; // 实际下发值（插件字符串或内部通道值）
    std::string_view label; // 下拉显示标签（中文短标签）
    Kind kind = Kind::Native;
    std::string_view detail; // 中文说明：原生机制 / 仿真通道 / 置灰原因（恒非空）
};

struct BackendFacts {
    std::string_view backend; // 冻结后端 id：x265 / svt-av1 / libaom
    std::string_view tooltip; // 行 tooltip（机制解释；含插件强制项与仿真说明）
    std::vector<ValueFact> values;
};

// 后端事实（未知后端 = nullptr；调用方据此保留运行时内省原样）。
const BackendFacts *facts_for(std::string_view backend);

// 值事实（未知后端/未知值 = nullptr）。
const ValueFact *value_fact(std::string_view backend, std::string_view value);

// 行 tooltip（后端未知 = 空串）。
std::string tune_tooltip(std::string_view backend);

// Disabled 值的中文不可用说明（预检/编码器共用同一文案，点明原因与可选值）。
std::string unavailable_message(std::string_view backend, const ValueFact &fact);

// 一致性判定（单测/启动探针）：
//   * 每个 Native 值必须出现在 runtime（插件接受集）；
//   * 每个 runtime 值必须在本表且类别为 Native；
//   * Simulated/Disabled 值不得出现在 runtime。
// 失败时 why 写入逐条差异（中文）；成功返回 true。
bool check_runtime_native(std::string_view backend, const std::vector<std::string> &runtime,
                          std::string *why);

} // namespace pp::tune
