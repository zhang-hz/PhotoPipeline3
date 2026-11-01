// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 格式级参数预设的 JSON/文件 I/O（M10 / W3-T1；契约 docs/v0.4.0-design.md §8 C23）
//
// 分层：core 侧 `core/format_preset.{h,cpp}` 零 Qt（数据 + 校验 + 归一），本文件是 Qt 适配层
// （QJsonDocument / 原子写 / 目录扫描 / 导入导出），与 `ui/preset_io.*`（全局预设）同构但
// **目录与 schema 完全独立**（互不扫描、互不触发写盘）。
//
// 存储（设计 §8.3）：`<data_dir()>/presets/formats/<format_id>/<name>.json`
//   * 路径经 `platform::presets_dir()`（便携优先落盘；data_dir 为空 → 空路径，显式报错，
//     绝不回落 CWD 相对名）；
//   * 子目录 `formats/` 使全局预设 `list_presets()`（只扫 presets/ 根层）看不到格式预设，
//     反之亦然。
//
// JSON v1（设计 §8.2）：
//   { "kind":"format-preset", "version":1, "name", "format", "backend", "tech", "bitdepth",
//     "params": { ... }, "reserved": {"heif_profile": N}, "adaptive": {"enabled","policy"} }
//   * `kind` 缺失/不符 ⇒ 拒绝并提示"这不是格式预设"（防止把全局预设选进格式预设下拉）；
//   * `version` 缺失按 1 读；`>1` 显式报错（不静默丢字段）；未知根字段忽略并记日志；
//   * `params` 的 `__` 前缀键一律忽略并由 `reserved` 重建（与 preset_io.cpp 同口径）；
//   * 空 adaptive（enabled=false 且 policy 空）不写字段 ⇒ 旧文件零变化；
//   * 保存前先 `validate_format_preset`，不过 ⇒ 提示且**不写盘**（不先污染后报错）；
//   * 加载做完整校验（坏文件在 list 里仍列出、标注「（损坏）」+ 中文原因）。
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/adaptive.h"
#include "core/format_preset.h"

namespace pp::ui {

// —— 存储路径（纯函数；data_dir 不可用 / 格式 id 非法 → 空路径）——
std::filesystem::path format_presets_root(); // <data_dir>/presets/formats
std::filesystem::path format_preset_dir(std::string_view format_id);
std::filesystem::path format_preset_path(std::string_view format_id, std::string_view name);

// —— 列举 ——
struct FormatPresetEntry {
    std::filesystem::path path;
    std::string name; // 正常 = 文件内的 name；损坏 = 文件 stem
    bool broken = false;
    std::string error; // 损坏原因（中文；broken=false 时为空）
};
// 扫 `<format>/` 下的 *.json（非隐藏、普通文件）；坏文件仍列出（broken=true + error），
// 按显示名（Qt 大小写敏感比较）排序。目录不存在/为空 → 空表。
std::vector<FormatPresetEntry> list_format_presets(std::string_view format_id);
// 指定目录版本（单测/工具用；语义与上面一致）。
std::vector<FormatPresetEntry> list_format_presets_in(const std::filesystem::path &dir);

// —— 保存 / 读取 / 删除 ——
// 保存到 `<data_dir>/presets/formats/<p.format_id>/<p.name>.json`（目录按需创建、原子写）。
// 保存前 validate；成功时 *saved_path = 最终路径。返回空串 = 成功，否则中文错误。
std::string save_format_preset(const FormatPresetData &p,
                               std::filesystem::path *saved_path = nullptr);
// 保存到显式根目录（= `presets/formats` 的父级语义；单测/导入用）。
std::string save_format_preset_to(const std::filesystem::path &presets_root,
                                  const FormatPresetData &p,
                                  std::filesystem::path *saved_path = nullptr);
// 读取 + 完整校验（结构/kind/version/字段类型/参数/策略）。失败不污染 out。
std::string load_format_preset(const std::filesystem::path &file, FormatPresetData &out);
// 删除（绝对路径守卫：拒绝空路径/相对路径/目录/不存在的文件）。
std::string delete_format_preset(const std::filesystem::path &file);

// —— 导入 / 导出（设计 §8.3：「校验 → 复制进目录」/「另存到用户路径」）——
// 导入：读取 source → 校验 → 以 name（空 = 用文件内 name）规范化保存进数据目录。
std::string import_format_preset(const std::filesystem::path &source, std::string_view name,
                                 std::filesystem::path *saved_path = nullptr);
// 导入到显式根目录（单测/工具用）。
std::string import_format_preset_to(const std::filesystem::path &presets_root,
                                    const std::filesystem::path &source, std::string_view name,
                                    std::filesystem::path *saved_path = nullptr);
// 导出：校验后把规范 JSON 写到 dest（目录按需创建；dest 为空/是目录 → 中文错误）。
std::string export_format_preset(const FormatPresetData &p, const std::filesystem::path &dest);

// —— 内置（出厂）预设的 seed 与只读判定（M10 / W3-T5；设计 §8.3「内置库（出厂）」/ §9.7）——
// 文件名前缀单源 = `core/format_preset.h` 的 `kFormatPresetBuiltinPrefix`（`_builtin-`）。
// 该前缀是**只读标记**：`save_format_preset*` 拒绝覆盖、`delete_format_preset` 拒绝删除
// （「另存…」/导出到用户路径不受限）；UI 侧同时把「删除」置灰并给出原因。
bool is_builtin_format_preset_path(const std::filesystem::path &file);
bool is_builtin_format_preset_name(std::string_view name);

struct FormatPresetSeedReport {
    int written = 0;                 // 本次新写出的文件数
    int skipped = 0;                 // 目标已存在 → 跳过（幂等：不覆盖用户/已 seed 文件）
    int failed = 0;                  // 校验/写盘失败数
    std::vector<std::string> errors; // 中文错误（每条含路径 + 原因；空 = 全成功）
};
// 幂等 seed：对 `builtin_format_presets()` 的每条，**仅当**目标文件不存在时写
// `<presets_root>/<format>/_builtin-<format>.json`（= 规范缩进 JSON，与 save 同一写盘函数）。
// 已存在（用户改过/上次已 seed）→ skipped，一个字节都不动；写盘前先 `validate_format_preset`，
// 不过即 failed 并记中文原因（不写坏文件、不静默）。
FormatPresetSeedReport seed_builtin_format_presets_to(const std::filesystem::path &presets_root);
// 数据目录版本（= `format_presets_root()`；端口不可用 → failed + 中文错误）。
FormatPresetSeedReport seed_builtin_format_presets();

// —— 自适应策略 JSON AST 导入（设计 §9.3；导出仍为 DSL 文本）——
// 解析 JSON AST 并做结构/注册表校验（format_id 空 = 只做与格式无关的校验）。
// 失败时 err 的 code 为 json_syntax / json_field / 或 adaptive 校验错误码。
bool parse_adaptive_policy_json(std::string_view json, std::string_view format_id,
                                AdaptivePolicy &out, AdaptiveError *err);
bool load_adaptive_policy_json(const std::filesystem::path &file, std::string_view format_id,
                               AdaptivePolicy &out, AdaptiveError *err);

} // namespace pp::ui
