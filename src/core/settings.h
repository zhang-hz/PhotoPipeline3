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
//
// PP-FROZEN(0.3.0)【追加面登记 · M5-T11/A28】—— `ui_theme_mode`（追加在结构体末尾）：
//   裁定原文（docs/m5-adjudications.md A28）：「主题选择持久化：按 C6 追加 AppSettings 键
//   （`ui_theme_mode`: system/light/dark，默认 system=现行为）+ 主窗启动读取点；`test_settings`
//   往返扩展」。落地形态 = 本文件末尾一个 `std::string` 字段 + settings.cpp 的
//   settings_pairs/apply_pair 各一行（词表外/空值保留默认）；**既有键零改动**、INI 未知键
//   原样保留的存储契约零改动 → §3.6 面的既有冻结语义不变（与 A29 的 RunSummary::peak_bytes
//   「末尾追加 = 冻结面登记」同口径）。
//
// PP-FROZEN(0.3.0)【追加面登记 · M6-T11（契约 C6「设置加键规则」）】—— `map_style` /
//   `map_sat_labels`（追加在结构体末尾）：
//   契约原文（docs/m6-map-upgrade.md §3 C6）：「新增 `map_tile_source` / `map_geocoder` /
//   `map_style` / `map_sat_labels`；默认值=现行为；`test_settings` 随码扩展；老 INI 可读
//   （缺键回落默认）；`amap_key` 脱敏规则不变」。T11 任务面 = 其中 **`map_style` /
//   `map_sat_labels` 两项**（`map_tile_source`/`map_geocoder` 按任务拆分后续落；既有
//   `map_provider` 语义不动）。
//   落地形态 = 本文件末尾两个字段 + settings.cpp 的 settings_pairs/apply_pair 各两行
//   （`map_style` 词表外/空值保留默认）；**既有键零改动**、INI 未知键原样保留的存储契约
//   零改动 → §3.6 面的既有冻结语义不变（与 A28 的 ui_theme_mode「末尾追加 = 冻结面登记」
//   同口径）。
#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "platform/paths.h" // platform::data_dir()（§3.6：class_file 默认 data_dir()/classes.json）

namespace pp {

// PP-FROZEN(0.3.0) §3.6 · AppSettings（`class_file`，**追加在结构体末尾**，既有键不动）
//   0.3.0 冻结形态（设计 §3.6 逐字抄录）：
// clang-format off
// `AppSettings` 追加：`stagger_ms`、`thread_budget`、`split_by_format`、`output_template`、`class_file`（默认 `data_dir()/classes.json`）
// clang-format on
//   路径单源（§6.2 的"持久化 `data_dir()/classes.json`"经本字段落地）：默认值 = 本函数，
//   `core/classify` 的 save/load 只收路径参数（不自行拼 data_dir()），故改设置即改注册表位置。
//   `data_dir()` 不可用（两分支都创建失败）→ **返回空串**（M7-R2B / WP5 跨界 1 收口，R-309
//   同款病灶）：不再拼 CWD 相对名 "classes.json"（旧降级会把注册表静默写进当前工作目录）。
//   回落语义显式化：空串 = **无默认位置**（与 settings_file()/presets_dir()/logs_dir() 的空
//   路径口径同一，见 platform/paths.h `path_under` 契约）—— 消费方按空路径处理（不落盘/报错），
//   本层不抛异常。派生走 platform::path_under 单源（WP5 A1）。
inline std::string default_class_file() {
    return platform::path_under(platform::data_dir(), "classes.json").string();
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
    int workers = 0;   // 0=自动（线程预算或逻辑核数，见 scheduler alloc_threads；非物理核数）
    int budget_gb = 0; // 0=自动 min(RAM×50%, 8GB)
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
    // 分类注册表路径（§6.2 持久化单源）。
    // R-050（M7-WP1）：默认值改**惰性** —— 原 `= default_class_file()` 会让纯数据结构的
    // 默认构造（含单测、UI 快照）就经 platform::data_dir() **建真实目录**（文件系统副作用
    // 逃出构造语义）。现默认空串 = "未配置"，由消费点在使用时经 default_class_file() 显式
    // 解析（mainwindow.cpp:1545-1548 既有口径：class_file.empty() ? default_class_file()
    // : class_file）；load/save 的空值语义不变（空 = 未配置 = 默认位置，见 settings.cpp
    // apply_pair 的 class_file 分支）。
    std::string class_file;
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
    // —— 0.3.0 / M5-T11 追加（**末尾**；开发期裁定 A28，按契约 C6「新设置项走 AppSettings
    //    追加键」落地）——
    // `ui_theme_mode`：手动主题档（§9「遵循系统主题 + 手动切换（设置·通用）」的持久面）。
    // 词表 = `system` | `light` | `dark`，默认 `system` = 现行为（跟随系统；§9.2）。
    // 非词表值在加载期丢弃（保留默认，见 settings.cpp）；消费点 = MainWindow 启动期
    // （ui/theme.h 的持久化档注入）+ 设置对话框「通用」页签的三档写回。
    std::string ui_theme_mode = "system";
    // —— M6-T11 追加（**末尾**；契约 C6：默认 = 现行为，老 INI 缺键回落默认，脱敏不变）——
    // `map_style`：GPS 卡图层档的跨进程记忆（§5.2「切换即 set_tile_style；记忆跨进程」）。
    // 词表 = `road` | `satellite` | `hybrid`（与 mapwidget/layers.h 的 `TileStyle`
    // 同序同义，也与 GPS 卡图层 Seg 的段序 矢量|卫星|混合 一致），默认 `road` = 现行为
    // （图层初始档 = 矢量）。非词表值在加载期丢弃（保留默认，见 settings.cpp）；消费点 =
    // MainWindow 启动期回填 + 换层写回 + 设置对话框「地图」页签的图层行。
    std::string map_style = "road";
    // `map_sat_labels`：混合图层的「叠标注」开关（默认 true = 现行为：混合 = 影像 + 标注双层）。
    // 持久化面在本任务落地（设置对话框读写）；mapwidget 侧消费归 W-M5（§7 T13）——
    // 本版只保证跨进程读写往返，不动图层绘制。
    bool map_sat_labels = true;
    // —— M8 追加（**末尾**；契约 C6 / A28 先例）：SSIMULACRA 2 输出评分开关。
    // GUI 默认开；`--dev` 默认关（`--score` 开启）。评分逻辑/字段消费在 Stage C。
    bool quality_score = true;
};

// 极简 INI：`key=value` 单层，`#` 注释；未知键保留原样写回（向前兼容）
AppSettings load_settings(const std::filesystem::path &file);
std::string save_settings(const std::filesystem::path &file, const AppSettings &s);
std::string settings_to_string(const AppSettings &s); // 日志快照用

// —— M7-R2.1-F4 / R-137 加性追加（末尾；裁定登记 docs/m7-adjudications.md 十一节）——
// map_style 词表**单源**（road|satellite|hybrid；下标序 = GPS 卡图层 Seg 段序「矢量|卫星|
// 混合」= mapwidget/layers.h 的 TileStyle 枚举序）。旧四处并存（settings.cpp 持久层校验 /
// mainwindow.cpp 档位表 / settings_dialog Seg / layers.cpp name↔enum）靠注释维系同序 ——
// 收口为：**词表与序在本表**；settings.cpp 校验读本表；mapwidget/layers.cpp 的
// tile_style_name/from_name 读本表（name↔enum 映射的消费面仍是 layers API）；mainwindow
// 档位索引走 layers API（本地 MapStyleRow 表退役）。分层注记：core 不反向依赖 mapwidget，
// 评审「以 layers.h 为单源」在 settings.cpp 侧受分层约束 → 调整为「词表在 core、映射在
// layers」（layers.cpp 为 mapwidget 侧唯一映射实现，字符串字面量随本表退役）。
inline constexpr const char *kMapStyleIds[] = {"road", "satellite", "hybrid"};
inline constexpr std::size_t kMapStyleCount = 3;
inline bool is_map_style_id(std::string_view v) {
    for (std::size_t i = 0; i < kMapStyleCount; ++i) {
        if (v == kMapStyleIds[i])
            return true;
    }
    return false;
}

} // namespace pp
