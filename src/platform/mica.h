// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M3-D6 — Win11 Mica backdrop + immersive dark titlebar (design §6.6).
// 非 Windows 平台为空操作（铁律六：平台分派内聚于同一文件，不分叉文件）。

#pragma once

namespace pp::platform {

// hwnd: 平台原生窗口句柄（Qt QWidget::winId()）。dark_titlebar: 标题栏用深色模式。
// 返回 true = backdrop 已应用（仅 Windows 11 22H2+ 生效）；false = 不支持或非 Windows。
// v1 口径：启动期应用一次；运行期系统主题切换监听留后续。
bool apply_window_backdrop(void* hwnd, bool dark_titlebar);

}  // namespace pp::platform
