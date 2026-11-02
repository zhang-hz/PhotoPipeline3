// PhotoPipeline — 预设 JSON I/O（M1-T2；§3.12 冻结）
// 说明：本文件是唯一允许进入 pp_core 的 Qt 适配层（JSON 读写），使 UI、--dev harness、
//       单元测试三处复用同一份预设 I/O；src/core/ 目录本身仍零 Qt。
// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/presets.h"
#include <filesystem>
#include <string>

namespace pp::ui {

// JSON 读写（QJsonDocument）；返回空串=成功
std::string save_preset(const std::filesystem::path &file, const PresetData &p);
std::string load_preset(const std::filesystem::path &file, PresetData &out);

// 目录扫描：*.json，返回按名排序的 (path, name) 列表
std::vector<std::pair<std::filesystem::path, std::string>>
list_presets(const std::filesystem::path &dir);

// —— 0.4.2 / 需求 3 加性（大预设重设计；裁定登记 docs/m0.4.2-adjudications.md）——
// 把 PresetData 序列化为**落盘字节**（= save_preset 实际写入内容；QJsonDocument::Indented）。
// 单源：save_preset 内部同样调用本函数写盘，保证「已修改」比对基线与文件字节同源；
// 单测以它做 save→load→save 字节稳定断言（既有 save_preset/load_preset 签名与语义零变化）。
std::string serialize_preset(const PresetData &p);

} // namespace pp::ui
