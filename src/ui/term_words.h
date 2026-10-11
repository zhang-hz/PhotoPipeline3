// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline — 终态词表（M7-WP6 / R-139 三面共用一张）
//
// 原三套词汇：`完成/取消`（filelistmodel 的 §3.1 状态表）、`已完成/已取消`（page_run 的状态
// 胶囊）、`成功 N`（mainwindow 状态栏计数桶）。收敛到本表后只有一个定义点，其余全部引用 ——
// 同一事实只定义一次（单源思想，AGENTS.md §7.3）。
//
// 字符串为 UTF-8 **源串**（供 QCoreApplication::translate 查表；中文界面下译文与源串同值）。
// 本头只依赖 <string_view> 之外的零设施：刻意不 include 任何 UI 头，避免把 filelistmodel 的
// FileRow 等名字带进 page_run 的同名局部结构（实测 C2872 歧义）。
#pragma once

namespace pp::ui::term_words {

inline constexpr const char *kDone = "已完成";      // 全部输出成功
inline constexpr const char *kPartial = "部分成功"; // 多输出部分成功（R-076）
inline constexpr const char *kSkipped = "跳过";     // 冲突策略 Skip
inline constexpr const char *kFailed = "失败";      // 任一输出失败
inline constexpr const char *kCancelled = "已取消"; // 用户取消
inline constexpr const char *kQueued = "排队";
inline constexpr const char *kRunning = "进行中";
inline constexpr const char *kZeroBytes = "0 字节"; // R-069：完成但产物 0 字节（琥珀异常）

} // namespace pp::ui::term_words
