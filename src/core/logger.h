// PP-FROZEN(file)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pp {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

using LogFields = std::initializer_list<std::pair<std::string_view, std::string_view>>;

// PP-THAWED(0.3.0-M4-D20) §3.6 · core/logger.h —— 追加点位（无签名改动）
//   裁定原文（本行为逐字抄录；行首 "// " 为注释包装）：
// clang-format off
// | `core/logger.h` | 追加点位：交错启动偏移、线程分配、进度快照（§7.4） |
// clang-format on
//   W1-T6 **已落**：进度快照点位 —— pipeline.cpp 每输出一条 debug 行 `progress snapshot`
//     （键：progress_reported / progress_max_row / progress_samples / progress_synthetic /
//     encode_est_ms；§7.4「运行日志每文件落 progress_max_row/progress_reported 快照（debug 级）」，
//     **不逐行落盘**）；另 `decode progress: immediate 1 …`（§7.2 无读行回调的注明）亦为 debug。
//     本文件零改动（既有 log_write/log_debug + LogFields 足够承载，不新增函数/签名）。
//   W1-T7 **待落**：交错启动偏移（§8.1）+ 线程分配（§8.2）点位 → 该行在 T7 落地前保持
//     PP-THAWED(0.3.0-M4-D20)（T7 落地后整体改标 PP-FROZEN(0.3.0)）。
//   0.2 现形 API 零改动（log_init/log_write/log_set_level/level_from_env_or/… 签名一字不改；
//   `stage` 实参沿用既有口径；本任务只加注释，不新增函数）。
//   点位形态（设计逐字摘录，行首 "// " 为注释包装）：
// clang-format off
// §8.1 交错启动：`[sched] file 启动（交错偏移 +Δms）`
// §7.4 进度快照：运行日志每文件落 `progress_max_row/progress_reported` 快照（debug 级）；
//      **进度事件不得淹没日志**（不逐行落盘）
// §8.2 线程分配：worker 取件 → 计算 Alloc → 更新并发闸（信号量限 W）→ `EncodeRequest.encode_threads=E`
//      （运行日志含交错偏移 + 线程分配读数）
// clang-format on
// log_dir 不存在则创建；min_level 可被环境变量 PP_LOG_LEVEL 覆盖
// （取值 trace/debug/info/warn/error/critical，大小写不敏感）
// 文件名 run-YYYYMMDD-HHMMSS.log；目录内保留最近 20 个 run 文件，多余的删最旧
// 行格式：HH:MM:SS.mmm [lvl] [tid] [stage] [file] message {k=v k=v}（英文）
// warn 及以上立即 flush；日志文件打开失败时降级为 stderr 输出，不得抛异常
void log_init(const std::filesystem::path &log_dir, LogLevel min_level = LogLevel::Info);
void log_shutdown();
void log_set_level(LogLevel lv);
LogLevel log_level();

void log_write(LogLevel lv, std::string_view stage, std::string_view file, std::string_view msg,
               LogFields fields = {}) noexcept;

inline void log_trace(std::string_view s, std::string_view f, std::string_view m,
                      LogFields fl = {}) {
    log_write(LogLevel::Trace, s, f, m, fl);
}
inline void log_debug(std::string_view s, std::string_view f, std::string_view m,
                      LogFields fl = {}) {
    log_write(LogLevel::Debug, s, f, m, fl);
}
inline void log_info(std::string_view s, std::string_view f, std::string_view m,
                     LogFields fl = {}) {
    log_write(LogLevel::Info, s, f, m, fl);
}
inline void log_warn(std::string_view s, std::string_view f, std::string_view m,
                     LogFields fl = {}) {
    log_write(LogLevel::Warn, s, f, m, fl);
}
inline void log_error(std::string_view s, std::string_view f, std::string_view m,
                      LogFields fl = {}) {
    log_write(LogLevel::Error, s, f, m, fl);
}

// 版本清单（运行头日志用）：返回 "key=value" 列表
std::vector<std::pair<std::string, std::string>> library_versions();

// PP-FROZEN(block): M1-T1 新增声明（§3.1 正文约定"Qt 版本由 pp::set_qt_version_string
// 在 main 里注入"，但 §3.1 代码块未列出；core 不依赖 Qt，故由 UI/main 注入）
// 注入后 library_versions() 增加 "qt" 项；未注入则不输出该项
void set_qt_version_string(std::string version);

// PP-FROZEN(block): M2-T3 新增声明（§2.3 PP_LOG_LEVEL 启动期一次性解析；既有签名零变动）
// 读 PP_LOG_LEVEL（大小写不敏感）：trace|debug|info|warn|error（另接受上方 M1a 冻结注释已
// 文档化的 critical）→ 返回覆盖后的级别；未设置 → 原样返回 fallback，行为与 M1a 完全一致。
// 非法值 → stderr 一行 `PP_LOG_LEVEL 无效："<原值>"，已忽略`，返回 fallback。
// 调用点：main.cpp 在 load_settings/参数解析之后、log_init 之前（GUI 与 --dev/--ui-smoke 共用）。
LogLevel level_from_env_or(LogLevel fallback);

} // namespace pp
