// PhotoPipeline — 预设 JSON I/O（M1-T2；§3.12 冻结）
// 说明：本文件是唯一允许进入 pp_core 的 Qt 适配层（JSON 读写），使 UI、--dev harness、
//       单元测试三处复用同一份预设 I/O；src/core/ 目录本身仍零 Qt。
// PP-FROZEN(file)
#pragma once
#include <filesystem>
#include <string>
#include "core/presets.h"

namespace pp::ui {

// JSON 读写（QJsonDocument）；返回空串=成功
std::string save_preset(const std::filesystem::path& file, const PresetData& p);
std::string load_preset(const std::filesystem::path& file, PresetData& out);

// 目录扫描：*.json，返回按名排序的 (path, name) 列表
std::vector<std::pair<std::filesystem::path, std::string>>
list_presets(const std::filesystem::path& dir);

}  // namespace pp::ui
