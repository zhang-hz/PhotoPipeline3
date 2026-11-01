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

// ===========================================================================
// 内置（出厂）格式预设库（M10 / W3-T5；设计 §8.3「内置库（出厂）」/ §9.7）
// ===========================================================================
// 文件名前缀：内置预设落在 `<data_dir>/presets/formats/<format>/_builtin-<slug>.json`，
// `<slug>` = format_id（一格式一条）。前缀是**只读标记**：UI/IO 拒绝覆盖与删除该前缀的
// 文件（改动请走「另存…」；见 `ui/format_preset_io.*` 与 `ui/mainwindow_presets.cpp`）。
inline constexpr std::string_view kFormatPresetBuiltinPrefix = "_builtin-";

// 5 条内置示例预设（HEIF / AVIF / JXL / WebP / JPEG；PNG/TIFF/BMP 无 —— 无损、策略无意义）：
//   * 名称 = 「内置示例 · <格式标签>」（标签取 `FormatDef::label`）；
//   * 后端/技术/位深 = 该格式的**安全默认**（heif x265+runtime@10、avif svt-av1+runtime@10、
//     jxl libjxl+vardct@16、webp libwebp+lossy@8、jpeg jpegli+dct@8）；
//   * `params` = 静态表格式的默认参数全集（`default_params`，`__` 内部键已剥除），再叠加少量
//     **给 §9.7 策略留出提升空间**的安全初值（heif/avif quality 85 + chroma 420；
//     jxl distance 1.0；webp quality 85；jpeg quality_mode=quality + quality 90 —— 策略搜的是
//     quality，默认 distance 模式下改 quality 不改变输出，故显式钉住模式）。
//     libheif 系（heif/avif）**只写这些安全初值**：运行时内省参数随插件版本漂移，且格式级参数
//     并集按后端 first-wins 可能与具体后端取值范围冲突（实测 avif 的 `speed`），写进预设会被
//     逐键校验拒绝 —— 其余参数由运行时默认值承担（不写 = 不覆盖）。
//   * `adaptive_enabled = true`、`adaptive_policy = default_adaptive_policy_for(format)->text`
//     （§9.7 逐字，含注释；空格式不在库内）。
// 返回值稳定（进程内静态构造一次），调用方不得修改；校验恒通过（单测钉住）。
const std::vector<FormatPresetData> &builtin_format_presets();

} // namespace pp
