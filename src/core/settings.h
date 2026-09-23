// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace pp {

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
