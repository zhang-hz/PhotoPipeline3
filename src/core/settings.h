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
//     * `stagger_ms` / `thread_budget` —— **W1-T7 已落地**（含持久化读写）= 下方
//       PP-FROZEN(0.3.0) 声明（T7 文件面：settings.h/settings.cpp）。
//     * `class_file` —— **W1-T8 已落地**（默认 `data_dir()/classes.json` + 持久化读写
//       + `core/classify` 消费单源）= 下方 PP-FROZEN(0.3.0) 字段声明。
//     * `split_by_format` / `output_template` —— **W3-T15 已落地**（持久化读写 +
//       设置对话框「分文件夹默认结构」项 + MainWindow 启动期喂给输出页默认值）=
//       下方 PP-FROZEN(0.3.0) 字段声明。
//   整行五项至此全部落地 → 本文件 §3.6 面**整体再冻结**为 PP-FROZEN(0.3.0)。
//   本文件其余既有键（workers/budget_gb/flatten_gray/log_level/…）语义不变 → 维持 PP-FROZEN。
//   追加顺序说明（【机械性】记账）：§3 的「加性字段一律追加在结构体末尾」使然 —— T7 追加
//   stagger_ms/thread_budget、T8 追加 class_file 都落在末尾；T15 的两项因此续在 class_file
//   **之后**（而不是 §3.6 行内的字面次序 split_by_format→output_template→class_file）。
//   结构体布局/INI 键序均按追加时间递增，语义与 §3.6 行等价。
#pragma once
#include <filesystem>
#include <map>
#include <string>

#include "platform/paths.h" // platform::data_dir()（§3.6：class_file 默认 data_dir()/classes.json）

namespace pp {

// PP-FROZEN(0.3.0) §3.6 · AppSettings（`class_file`，**追加在结构体末尾**，既有键不动）
//   0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   路径单源（§6.2 的"持久化 `data_dir()/classes.json`"经本字段落地）：默认值 = 本函数，
//   `core/classify` 的 save/load 只收路径参数（不自行拼 data_dir()），故改设置即改注册表位置。
//   `data_dir()` 不可用（两分支都创建失败）→ 退化为相对名 "classes.json"（与 settings_file()
//   同口径的降级：调用方按相对路径处理，本层不抛异常）。
inline std::string default_class_file() {
    return (std::filesystem::path(platform::data_dir()) / "classes.json").string();
}

// PP-FROZEN(0.3.0) §3.6 · AppSettings（`stagger_ms`/`thread_budget` 两项，**追加在结构体末尾**，
//   既有键不动）—— 0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   §3.6 未逐字给出类型/默认值 → 类型与默认值取 §3.2 RunConfig 同名裁决面（逐字一致）：
//     `stagger_ms = 150`（§8.1/§9.3 设置域 0–2000 ms）、`thread_budget = 0`（0 = 逻辑核）。
//   INI 键名 = 字段名（settings.cpp 的 settings_pairs/apply_pair，既有约定）。
// PP-FROZEN(0.3.0) §3.6 · AppSettings（`split_by_format`/`output_template` 两项，**追加在
//   结构体末尾**，既有键不动）—— W3-T15 落地；0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   §3.6 未逐字给出类型/默认值 → 类型与默认值取 §3.2 RunConfig 同名裁决面（逐字一致）：
//     `output_template = "$format/$dir/$file"`（§4.2）、`split_by_format = false`；
//   INI 键名 = 字段名；`output_template` 的非法值在加载期丢弃（保留默认，见 settings.cpp）。
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
    // —— 0.3.0 / W1-T8 追加（**末尾**，§3.6 行最后一项）——
    // 分类注册表路径（§6.2 持久化单源；默认 data_dir()/classes.json）
    std::string class_file = default_class_file();
    // —— 0.3.0 / W3-T15 追加（**末尾**，§3.6 行中间两项；追加顺序见文件头说明）——
    // 分文件夹默认结构（§9.3 设置项「分文件夹默认结构」= 这两项的配对面）：
    //   * `output_template`：路径模板默认值（§4.2 符号表；设置对话框择一写入，
    //     加载时经 `validate_output_template()` 判负 → 保留默认，非法模板不落库）；
    //   * `split_by_format`：「按格式分文件夹」开关 —— 存储层只做布尔解析（哑存储），
    //     与模板 `$format` 段的配平由设置对话框/输出页维护（§4.2 联动）；它是**派生态**
    //     （= 模板含 $format 段），启动期不单独落值（模板即真值，见 mainwindow.cpp
    //     restore_session 的长注释：两个都落会把首启默认结构静默改成平铺）。
    // 默认值取 §3.2 RunConfig 同名面（逐字一致）：`$format/$dir/$file` / false；
    // core/presets.h（T13）同口径。消费点 = MainWindow 启动期喂给输出页默认值
    // （restore_session；随后被 last_preset 覆盖，§2.14）。
    std::string output_template = "$format/$dir/$file"; // §3.2 同名默认（§4.2）
    bool split_by_format = false;                       // §3.2 同名默认
};

// 极简 INI：`key=value` 单层，`#` 注释；未知键保留原样写回（向前兼容）
AppSettings load_settings(const std::filesystem::path &file);
std::string save_settings(const std::filesystem::path &file, const AppSettings &s);
std::string settings_to_string(const AppSettings &s); // 日志快照用

} // namespace pp
