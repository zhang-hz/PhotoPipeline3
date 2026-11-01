// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 格式级参数预设（M10 / W3-T1；契约 docs/v0.4.0-design.md §8「C23」）
//
// 设计口径（逐条对应设计 §8.1–§8.4；本文件是 core 纯函数，**零 Qt**）：
//   * 新能力新文件：格式预设不碰冻结的 `core/presets.h`（全局预设 v2 零改动，裁定 A8）；
//     JSON 读取/原子写/列举/导入导出在 `src/ui/format_preset_io.{h,cpp}`（Qt 适配层）。
//   * 作用域 = `format_id`（8 个格式各一张参数卡 ⇒ format 为唯一键）；文件另存
//     backend/tech/bitdepth，载入作为**整卡模板**，不兼容时显式中文报错。
//   * 内容 = 该格式卡的可运行快照：后端/技术/位深 + 核心+高级参数（含三态"无键=未设置"）
//     + 保留键 `heif_profile` + 自适应策略（§9）。**不含全局项**
//     （color/conflict/template/rules/metadata_only —— 属全局预设职责）。
//   * 无隐式叠加：格式预设载入只写当前格式卡；与全局预设目录互不扫描。
//   * 校验链（保存/载入/开始运行三处同一套；错误一律中文，UI 直接展示）：
//       ① 结构/版本 → ② `check_format_preset_applies_to`（format 匹配/backend/tech 可用）
//       → ③ `normalize_format_preset`（补默认 + 锁 + 位深，复用 params 既有链，只读调用）
//       → ④ `validate_format_preset`（逐键类型/范围/枚举/未知键/跨技术残留键/策略文本）
//       → ⑤ 自适应策略 parse + validate（失败 = 保存/载入被阻断，不落盘）。
//
// 与 JSON 的边界（`format_preset_io`，设计 §8.2）：
//   `{kind:"format-preset", version:1, name, format, backend, tech, bitdepth, params,
//     reserved:{heif_profile}, adaptive:{enabled,policy}}`；
//   `kind` 缺失/不符 ⇒ 按"不是格式预设"拒绝并提示（防止下拉选错文件）；
//   `version>1` 显式报错；未知根字段忽略并记日志；`params` 的 `__` 前缀键忽略并由 `reserved`
//   重建（与 `ui/preset_io.cpp` 同口径）；空 adaptive 不写字段（旧文件零变化）。
//   M12：出厂（内置）预设**不再是 C++ 硬编码库** —— 单一真源 = 仓库 `presets/formats/**` 的
//   JSON 数据文件（人类可直接阅读/编辑/复制），随发行包分发并在启动时按字节复制到
//   `<data_dir>/presets/formats/`（seed；见 `ui/format_preset_io.h`）。core 只保留"数据 + 规则"，
//   不再持有任何出厂预设内容；每个格式的出厂默认预设名 = `default`（见下）。
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.h"

namespace pp {

// 文件标识（`kind`）与版本：v1 = 0.4.0 格式预设；>1 显式报错，不静默丢字段。
inline constexpr std::string_view kFormatPresetKind = "format-preset";
inline constexpr int kFormatPresetVersion = 1;
// HEIF/AVIF 编码档位的保留键名（JSON 里的 `reserved.heif_profile`；params 内的
// `__heif_profile` 不落盘，读取时忽略并由该字段重建 —— 与全局预设输出级字段同口径）。
inline constexpr std::string_view kFormatPresetHeifProfileReserved = "heif_profile";

// ===========================================================================
// 出厂默认预设名（M12 单一真源；简报 §2.4）
// ===========================================================================
// 每个格式恰有一个出厂默认预设：文件名 `default.json`、JSON `name` 也是 `default`；
// "格式卡创建/首启时自动选中并载入它 / 不可删除（但可直接「保存」修改）"的判定全部引用这里
// （UI 删除保护 + 自动选中 + I/O 层删除守卫；禁止各写一份字符串）。
// 判定**大小写不敏感**：Windows 文件系统大小写不敏感（`Default.json` 与 `default.json` 是
// 同一个文件），默认预设的保护/自愈必须覆盖同一批文件；参数可为 JSON 的 `name` 字段，也可为
// 文件名 stem。
inline constexpr std::string_view kFormatPresetDefaultName = "default";
bool is_default_format_preset_name(std::string_view name);

struct FormatPresetData {
    int version = kFormatPresetVersion;  // 文件版本；>1 显式报错
    std::string name;                    // 显示名（= 文件 stem；UI 保存时校验合法性）
    std::string format_id;               // 必须等于目标卡片 id
    std::string backend_id, tech_id;     // 空 = 载入时解析为当前首选
    int out_bitdepth = 0;                // 0 = 不覆盖（保留当前卡值）；normalize 补默认
    ParamSet params;                     // 含三态"无键=未设置"；`__` 保留键不落盘
    std::optional<int64_t> heif_profile; // 保留键 __heif_profile（HEIF/AVIF 档位序号）
    bool adaptive_enabled = false;       // 自适应开关（策略非空才允许 true）
    std::string adaptive_policy;         // 行式 DSL 文本（§9.2）；空 = 关闭
};

// 校验（空串 = 通过；否则中文错误，可直接进 UI）。
// 覆盖：版本/名字合法性/格式存在/backend/tech 可用/位深合法/逐键类型-范围-枚举/
//       未知键/跨技术残留键/heif_profile 取值/自适应开关与策略文本（parse+validate）。
// 说明：`params` 中 `__` 前缀的保留键**不参与**逐键校验（由 IO 层的 reserved 重建）；
//       libheif 系（heif/avif）的插件透传键（`x265:`/`aom:` 前缀）不做类型校验（与
//       `cross_validate` 同口径）；编码器内省不可用时无法判定的键放行（不误报）。
std::string validate_format_preset(const FormatPresetData &p);

// 归一（§8.4 第 3 步；复用 params 既有链，只读调用 `fill_defaults`/`apply_locks`）：
//   * `backend_id` 空 → 该格式首选后端；
//   * `out_bitdepth` 0 → `default_bitdepth_for(format)`（单源；bmp 24bpp 口径一致）；
//   * `tech_id` 空 → 按 lossless 的首选技术（heif/avif 的运行时技术保持空）；
//   * 缺失参数按表补齐默认值、应用锁定值（返回被补/被改写的 key 列表，已去重）；
//   * 无法归一（未知格式/后端/技术、位深不在格式能力内）→ `*err` 中文错误、返回空表。
// 本函数**不改** name/params 的保留键/heif_profile/adaptive 字段；幂等（再调一次返回空表）。
std::vector<std::string> normalize_format_preset(FormatPresetData &p, std::string *err);

// 应用到目标格式卡的前置检查（空串 = 可应用；否则中文错误）：
//   * `format_id` 为空或与 `p.format_id` 不同 → 拒绝（不允许跨格式套用）；
//   * 预设显式 backend/tech 在该格式下不可用 → 拒绝；
//   * 预设显式位深不在该格式能力内 → 拒绝。
std::string check_format_preset_applies_to(const FormatPresetData &p, std::string_view format_id);

// ★ 出厂预设内容不在本文件：见 M12 修订 —— 工厂数据文件 = 仓库 `presets/formats/**`
//   （单一真源，随发行包分发；启动 seed 见 `ui/format_preset_io.h` 的
//   `factory_format_presets_root()` / `seed_factory_format_presets*()`）。

} // namespace pp
