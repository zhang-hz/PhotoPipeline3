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

// M7-WP5（R-309，docs/deep-review-0.3.2.md「paths 空 data_dir 被 settings_file 静默转 CWD
// 相对路径」，加性追加）：派生路径的**纯函数真源**（settings_file()/presets_dir()/logs_dir()
// 与单测共用同一条策略，测试不再寄生 call_once 缓存）。
//   * dir 非空 → dir/leaf；
//   * dir 为空 → 返回**空路径**（不再回落 CWD 相对名 —— 那会在用户当前目录里悄悄读写
//     settings.ini）。空 = "无可用目录"，调用方按既有口径各自兜底（load_settings 空路径 =
//     全默认；save_settings 空目标 = 报错；log_init 空目录 = 回退 stderr；list_presets 空 =
//     空表）。
std::filesystem::path path_under(const std::filesystem::path &dir, const char *leaf);

} // namespace pp::platform
