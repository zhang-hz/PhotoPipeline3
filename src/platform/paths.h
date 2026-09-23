// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>

namespace pp::platform {

// exe 目录可写 → 便携模式：<exe>/settings.ini、<exe>/presets/、<exe>/logs/
// 只读 → 回退：Linux $XDG_DATA_HOME/PhotoPipeline（默认 ~/.local/share/PhotoPipeline）；
//           Windows %APPDATA%/PhotoPipeline（M3，design §8.5）
// 结果缓存；返回值为已确保存在的目录
std::filesystem::path data_dir();
std::filesystem::path settings_file(); // data_dir()/settings.ini
std::filesystem::path presets_dir();   // data_dir()/presets
std::filesystem::path logs_dir();      // data_dir()/logs
std::filesystem::path executable_dir();

} // namespace pp::platform
