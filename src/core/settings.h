// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PP-FROZEN(0.3.0)【部分落地】—— 解冻裁定表 §3.6 行 `core/settings.h`
//   （依据 docs/v0.3.0-design.md §3.6）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/settings.h` | `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`） |
// clang-format on
//   落地分解（**本行为多任务共享行**，故本文件按字段粒度标注冻结状态）：
//     * `stagger_ms` / `thread_budget` —— **W1-T7 已落地**（含持久化读写）= 下方两块
//       PP-FROZEN(0.3.0) 声明（T7 文件面：settings.h/settings.cpp）。
//     * `split_by_format` / `output_template` / `class_file` —— 待 W3-T15（设置对话框追加项）
//       / W2-T11（class_file 消费方）落地；本文件在此**如实保留待落状态**（不提前改标冻结，
//       避免"假冻结"）。T15 落地后整行改标 PP-FROZEN(0.3.0)。
//   本文件其余既有键（workers/budget_gb/flatten_gray/log_level/…）语义不变 → 维持 PP-FROZEN。
#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace pp {

// PP-FROZEN(0.3.0) §3.6 · AppSettings（`stagger_ms`/`thread_budget` 两项，**追加在结构体末尾**，
//   既有键不动）—— 0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   §3.6 未逐字给出类型/默认值 → 类型与默认值取 §3.2 RunConfig 同名裁决面（逐字一致）：
//     `stagger_ms = 150`（§8.1/§9.3 设置域 0–2000 ms）、`thread_budget = 0`（0 = 逻辑核）。
//   INI 键名 = 字段名（settings.cpp 的 settings_pairs/apply_pair，既有约定）。
//   注：本任务只落**持久化面**（settings.h/settings.cpp 在文件面内）；把设置值接进
//   `RunConfig`（main.cpp / GUI 设置对话框）不在本任务文件面 → W3-T15 接线，见 m4-report。
struct AppSettings {
    int workers = 0;           // 0=物理核数
    int budget_gb = 0;         // 0=自动 min(RAM×50%, 8GB)
    double flatten_gray = 1.0; // alpha 合成底色
    std::string log_level = "info";
    std::string map_provider = "osm"; // "osm" | "amap"
    std::string amap_key;
    int tile_cache_mb = 64;
    bool rotate_orientation = true;
    // 记住上次会话（共识 §3.9）
    std::string last_format = "jxl";
    std::string last_preset;   // 预设文件路径（可空）
    std::string last_out_root; // 上次输出根目录（可空）
    // —— 0.3.0 / W1-T7 追加（**末尾**，§3.6 行前两项）——
    int stagger_ms = 150;  // §8.1 交错启动步距（设置域 0–2000 ms；0 = 关闭）
    int thread_budget = 0; // §8.2 线程预算 T（0 = 逻辑核）
};

// 极简 INI：`key=value` 单层，`#` 注释；未知键保留原样写回（向前兼容）
AppSettings load_settings(const std::filesystem::path &file);
std::string save_settings(const std::filesystem::path &file, const AppSettings &s);
std::string settings_to_string(const AppSettings &s); // 日志快照用

} // namespace pp
