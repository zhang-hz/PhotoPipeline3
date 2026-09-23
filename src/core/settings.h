// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-THAWED(0.3.0-M4-D20) —— 解冻裁定表 §3.6 行 `core/settings.h`（依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/settings.h` | `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`） |
// clang-format on
//   落地任务 = W3-T15（设置追加项）/ W2-T11（class_file 消费方）→ 落地后改标 PP-FROZEN(0.3.0)。
//   本文件其余既有键（workers/budget_gb/flatten_gray/log_level/…）语义不变 → 维持 PP-FROZEN。
#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace pp {

// PP-THAWED(0.3.0-M4-D20) §3.6 · AppSettings 追加字段（**追加在结构体末尾**，既有键不动）
//   —— 0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   建议形态（§3.6 未逐字给出类型/默认值 → 按同名裁决面取值，T15 落地时确认，非本任务改动）：
// clang-format off
//     int         stagger_ms     = 150;                       // §3.2 RunConfig 同名默认；设置项范围 0–2000（§9.3）
//     int         thread_budget  = 0;                         // §3.2；0 = 自动（逻辑核）
//     bool        split_by_format = false;                    // §3.2；设置项=分文件夹默认结构（§9.3）
//     std::string output_template = "$format/$dir/$file";     // §3.2；路径模板（§4.2）
//     std::filesystem::path class_file = data_dir() / "classes.json";  // §3.6 明文默认值
// clang-format on
//   注：`data_dir()` 来自 `src/platform/paths.h`（现形既有声明）；本文件当前未 include 它 ——
//   T15 落地时补 include（0.3.0 的 settings.h 首次引入 path 类型默认值）。
//   联动（§9.3 设置对话框追加）：交错启动 ms（0–2000）、线程预算（0=自动）、分文件夹默认结构。
struct AppSettings {
    int workers = 0;                 // 0=物理核数
    int budget_gb = 0;               // 0=自动 min(RAM×50%, 8GB)
    double flatten_gray = 1.0;       // alpha 合成底色
    std::string log_level = "info";
    std::string map_provider = "osm";    // "osm" | "amap"
    std::string amap_key;
    int tile_cache_mb = 64;
    bool rotate_orientation = true;
    // 记住上次会话（共识 §3.9）
    std::string last_format = "jxl";
    std::string last_preset;             // 预设文件路径（可空）
    std::string last_out_root;           // 上次输出根目录（可空）
};

// 极简 INI：`key=value` 单层，`#` 注释；未知键保留原样写回（向前兼容）
AppSettings load_settings(const std::filesystem::path& file);
std::string save_settings(const std::filesystem::path& file, const AppSettings& s);
std::string settings_to_string(const AppSettings& s);   // 日志快照用

}  // namespace pp
