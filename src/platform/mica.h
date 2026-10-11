// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M3-D6 — Win11 Mica backdrop + immersive dark titlebar (design §6.6).
// 非 Windows 平台为空操作（铁律六：平台分派内聚于同一文件，不分叉文件）。

#pragma once

namespace pp::platform {

// hwnd: 平台原生窗口句柄（Qt QWidget::winId()）。dark_titlebar: 标题栏用深色模式。
// 返回 true = backdrop 已应用（仅 Windows 11 22H2+ 生效）；false = 不支持或非 Windows。
// v1 口径：启动期应用一次；运行期系统主题切换监听留后续。
bool apply_window_backdrop(void *hwnd, bool dark_titlebar);

// M7-WP5（UX-13，docs/ux-review-14.md §13）：Win11 窗口圆角显式申请
// （DWMWA_WINDOW_CORNER_PREFERENCE = DWMWCP_ROUND）。无边框窗口不申请就没有圆角；
// 最大化时圆角自动切直角、多显示器 DPI 的圆角半径均归 DWM 处理。
// 主窗 + 四弹窗同走 platform/frameless 的 attach 同一收口（同一函数同一口径）。
// 返回 true = 已应用；false = 非 Windows / 无句柄 / 系统不支持（Win10 返回 E_INVALIDARG
// → **静默忽略**，不降级其它行为）。
bool apply_window_corner(void *hwnd);

} // namespace pp::platform
