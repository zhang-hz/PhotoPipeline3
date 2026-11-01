// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 格式级参数预设的 JSON/文件 I/O（M10 / W3-T1；契约 docs/v0.4.0-design.md §8 C23）
//
// 分层：core 侧 `core/format_preset.{h,cpp}` 零 Qt（数据 + 校验 + 归一），本文件是 Qt 适配层
// （QJsonDocument / 原子写 / 目录扫描 / 导入导出），与 `ui/preset_io.*`（全局预设）同构但
// **目录与 schema 完全独立**（互不扫描、互不触发写盘）。
//
// 存储（设计 §8.3；M12 修订）：
//   * 数据目录 = `<data_dir()>/presets/formats/<format_id>/<name>.json`
//     （`platform::presets_dir()`，便携优先落盘；data_dir 为空 → 空路径，显式报错，
//     绝不回落 CWD 相对名）；
//   * **工厂（出厂）目录 = `<exe 目录>/presets/formats/<format_id>/<stem>.json`** —— 随发行包
//     分发的仓库数据文件（`presets/formats/**`，人类可直接阅读/编辑/复制），是出厂预设的
//     单一真源；启动时经 `seed_factory_format_presets*()` 按字节复制到数据目录；
//   * 子目录 `formats/` 使全局预设 `list_presets()`（只扫 presets/ 根层）看不到格式预设，
//     反之亦然。
//
// JSON v1（设计 §8.2）：
//   { "kind":"format-preset", "version":1, "name", "format", "backend", "tech", "bitdepth",
//     "params": { ... }, "reserved": {"heif_profile": N}, "adaptive": {"enabled","policy"} }
//   * `kind` 缺失/不符 ⇒ 拒绝并提示"这不是格式预设"（防止把全局预设选进格式预设下拉）；
//   * `version` 缺失按 1 读；`>1` 显式报错（不静默丢字段）；未知根字段忽略并记日志；
//   * `params` 的 `__` 前缀键一律忽略并由 `reserved` 重建（与 preset_io.cpp 同口径）；
//   * `adaptive.policy` 写出 = **JSON 字符串数组，一行一个元素**（需求 3：可读、零 \n 转义）；
//     读入兼容旧字符串形态（按 \n / \r\n 拆行）；数组元素内嵌换行 ⇒ 显式报错（保行号映射）；
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

// —— 工厂（出厂）预设目录（M12；exe 旁，随发行包分发）——
// `factory_format_presets_root() = <exe 目录>/presets/formats`（`platform::executable_dir()`
// 为空 → 空路径 = 不可用，调用方跳过 seed 复制）；`_dir(format_id)` 再做路径段安全检查。
std::filesystem::path factory_format_presets_root();
std::filesystem::path factory_format_preset_dir(std::string_view format_id);

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
// M12：**出厂默认预设（文件名 stem = `default`，单源 `pp::is_default_format_preset_name`）不可
// 删除**（中文拒绝；但它不是只读模板 —— 可直接「保存」修改它，或用「另存」复制一份）。
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

// —— 工厂（出厂）预设：字节复制 seed + 旧版迁移（M12；简报 §2/§4.1）——
// 背景：出厂预设不再是 C++ 硬编码库/只读模板，单一真源 = 随发行包分发的仓库 JSON 数据文件。
// 启动时把 exe 旁的工厂目录按**原字节**复制到数据目录（不重新格式化：人类可读、可 git diff 的
// 工厂文本逐字节落地）；此后一切读写都只针对数据目录（用户改过的 `default.json` 永不被覆盖）。
struct FormatPresetSeedReport {
    int written = 0;                 // 本次新复制到数据目录的工厂文件数（原字节、原子写）
    int skipped = 0;                 // 目标已存在 → 跳过（幂等：一个字节都不动）
    int removed_legacy = 0;          // 迁移删除的旧版只读内置模板（`_builtin-*.json`）数
    int failed = 0;                  // 校验/读/写/清理失败数
    std::vector<std::string> errors; // 中文错误（每条含路径 + 原因；空 = 全成功）
};
// 幂等 seed（启动期一次；失败只记日志、不弹框、不阻塞 UI；绝不写坏文件）：
//   ① 迁移清理：删除 `presets_root` 各格式子目录下全部旧版 `_builtin-*.json`（0.4.0.alpha1 的
//      只读出厂模板；数量记 `removed_legacy`，删除失败记 failed + 中文原因）。
//   ② `presets_root` 为空 → failed + 中文错误（目录不可用；不回落 CWD）。
//   ③ `factory_root` 为空，或与 `presets_root` 指向同一目录（便携模式 data_dir == exe 目录）
//      → 跳过复制（只做 ①；`skipped` 不计数）。
//   ④ 否则逐格式/逐文件名排序处理：目标不存在 → 先 `load_format_preset` 完整校验（含格式与
//      目录一致性），合法则原字节原子写 → written++；目标已存在 → skipped++（用户改过的文件
//      一个字节都不动）；工厂文件非法/读写失败 → failed++ + 中文原因。
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)：factory_root/presets_root 由 seed 语义固定
FormatPresetSeedReport seed_factory_format_presets_to(const std::filesystem::path &factory_root,
                                                      const std::filesystem::path &presets_root);
// 数据目录版本（factory_root = factory_format_presets_root() → format_presets_root()）。
FormatPresetSeedReport seed_factory_format_presets();

// —— 自适应策略 JSON AST 导入（设计 §9.3；导出仍为 DSL 文本）——
// 解析 JSON AST 并做结构/注册表校验（format_id 空 = 只做与格式无关的校验）。
// 失败时 err 的 code 为 json_syntax / json_field / 或 adaptive 校验错误码。
bool parse_adaptive_policy_json(std::string_view json, std::string_view format_id,
                                AdaptivePolicy &out, AdaptiveError *err);
bool load_adaptive_policy_json(const std::filesystem::path &file, std::string_view format_id,
                               AdaptivePolicy &out, AdaptiveError *err);

} // namespace pp::ui
