// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0) —— 解冻裁定表 §3.6 行 `core/presets.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/presets.h` | `PresetData` **[重排]** → schema v2：`outputs: [OutputFormatSpec]` 数组 + `output_template`/`split_by_format`；提供 `migrate_preset_v1()`（0.2 单格式 JSON → v2 单元素数组），加载旧预设零丢失 |
// clang-format on
//   落地任务 = W3-T13（输出页：预设 v2（迁移），出口含 `test_presets` v2 迁移绿）
//   → **已落地**（原标注 PP-THAWED(0.3.0-M4-D20) 随落地再冻结为本标记；冻结头 SPDX 延续）。
//   [重排] PresetData：字段序列变化，不再保证聚合初始化兼容（一次性接受）。
//
//   T13 落地口径（逐条；设计未逐字给出处）：
//     * `version = 2`（0.2 现形 = 1）。v2 的 `outputs` ≥ 1 项（仅元数据模式 = 1 项，§3.2 硬校验）；
//       `color_target`/`conflict`/`rules`/`name` 字段名与语义与 0.2 一致（全局唯一项）。
//     * **`lossless` 不进 PresetData**：§3.2 的 `OutputFormatSpec` 无该成员，无损语义随
//       `outputs[i].params` 承载（`pp::kLosslessParamKey`；0.3.0 起为显式 schema 参数，
//       见 src/core/params.h 与 docs/param-catalog.md）。
//     * `output_template`/`split_by_format` 字段默认值与 §3.2 RunConfig 同名同默认
//       （`$format/$dir/$file` / false）；`split_by_format` 与模板的 `$format` 段联动由 UI 维护
//       （§4.2），本层只存不管。**`migrate_preset_v1()` 的产出是唯一例外**：0.2 是"现状直出"
//       单输出形态（无格式子目录），迁移取 `$dir/$file`（见下方迁移块说明与 T13 复核项 3）。
//     * `validate_preset()` 追加**模板校验**（§4.2「未知 `$` 符号=校验错误」）：预设里带非法模板
//       = 预设无效，不得静默通过。
//     * 多输出时错误消息带 `output #N: ` 前缀（单输出消息与 0.2 逐字一致，便于旧断言沿用）。
//   本文件其余既有声明（validate_preset / normalize_preset）不在本表行内 → 签名不变，
//   语义随 schema v2 收敛（见上）。
#pragma once
#include "core/metadata.h"
#include "core/pipeline.h"
#include <optional>
#include <string>
#include <vector>

namespace pp {

// PP-FROZEN(0.3.0) §3.6 · PresetData（schema v2，[重排]）+ migrate_preset_v1()
//   0.3.0 冻结形态（设计 §3.6 逐字抄录）：`PresetData` → schema v2：`outputs: [OutputFormatSpec]`
//   数组 + `output_template`/`split_by_format`；提供 `migrate_preset_v1()`（0.2 单格式 JSON →
//   v2 单元素数组），加载旧预设零丢失。
struct PresetData {
    int version = 2;
    std::string name;
    // §3.2 OutputFormatSpec 数组（替代 0.2 的 format_id/backend_id/tech_id/params/out_bitdepth）
    std::vector<OutputFormatSpec> outputs;
    std::string output_template = "$format/$dir/$file"; // §3.2 同名默认（§4.2）
    bool split_by_format = false;                       // §3.2 同名默认
    ColorTarget color_target = ColorTarget::KeepOriginal;
    ConflictPolicy conflict = ConflictPolicy::Rename;
    BatchRules rules;
};

// 0.2 单格式形态（schema v1）：**只用于迁移**。JSON 文本由 ui/preset_io 解析（src/core 零 Qt），
// 迁移纯函数在 core 侧（可单测；R31 的"零丢失"由 test_presets 逐字段断言）。
struct PresetDataV1 {
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

// v1 → v2（§3.6）：单格式字段落进 `outputs[0]`；`lossless` 落进 `outputs[0].params`——
//   * 该 (format, backend, tech) **声明**了显式 schema 参数 `lossless`（jxl/webp 静态表；heif/avif
//     由 libheif 内省）时写显式键：它可落盘（`outputs[0].params.lossless`）、可校验；
//   * 未声明时（jpeg 无无损技术；png/tiff/bmp 技术本身无损、静态表不声明该参数，写进 params 会被
//     "未知参数"拒绝）退回**内部管道键** `__lossless`（谓词/编码器输入；保留键本身不落盘）。
//     其**保真落盘点**是输出级的 `outputs[i].lossless` JSON 字段（preset_io 写/读；T13 复核项 1
//     修复：0.2 顶层 lossless 在这些格式上经 load→save→load 不再静默回退 false）。
// `output_template`/`split_by_format`：0.2 无此概念（单输出 = 现状直出，无格式子目录），故迁移
// 取**单格式兼容形态** `$dir/$file` / false —— 这是**有意偏离**结构体默认值（`$format/$dir/$file`）
// 的一处，依据是 §1 需求 3「默认行为兼容 v0.2」（见下方实现注释与 tests 1b 断言）。
// 零丢失：v1 每个字段都有归宿。
PresetData migrate_preset_v1(const PresetDataV1 &v1);

// 校验：版本=2、outputs 非空、逐输出格式/后端/技术/位深/参数合法、模板合法
// 返回空串=通过
std::string validate_preset(const PresetData &p);

// 用当前格式表补齐缺失参数（逐输出；返回补齐的 key 列表）
std::vector<std::string> normalize_preset(PresetData &p);

} // namespace pp
