// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M3-D6 — Win11 Mica backdrop + immersive dark titlebar (design §6.6).
// M7-WP5（UX-13）：+ 窗口圆角申请（DWMWA_WINDOW_CORNER_PREFERENCE，docs/ux-review-14.md §13）。

#include "platform/mica.h"

#ifdef _WIN32
#include <dwmapi.h>
#include <windows.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20 // Win10 1809+（旧 SDK 头缺失时兜底）
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38 // Win11 22H2+（旧 SDK 头缺失时兜底）
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2 // Mica（DWMSBT_NONE=1 / DWMSBT_TRANSIENTWINDOW=3 / AUTO=0）
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33 // Win11（旧 SDK 头缺失时兜底）
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2 // DWMWCP_DONOTROUND=1 / ROUNDSMALL=3
#endif
#endif

namespace pp::platform {

#ifdef _WIN32
bool apply_window_backdrop(void *hwnd, bool dark_titlebar) {
    if (hwnd == nullptr)
        return false;
    const HWND h = static_cast<HWND>(hwnd);

    // 深色标题栏（随系统深浅色，由调用方传入）；失败不阻断 backdrop。
    const BOOL dark = dark_titlebar ? TRUE : FALSE;
    ::DwmSetWindowAttribute(h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    // Mica 背景（Win11 22H2+；更早系统返回错误码，静默保持默认）。
    const int backdrop = DWMSBT_MAINWINDOW;
    const HRESULT hr =
        ::DwmSetWindowAttribute(h, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    return SUCCEEDED(hr);
}
#else
bool apply_window_backdrop(void *hwnd, bool dark_titlebar) {
    (void)hwnd;
    (void)dark_titlebar;
    return false; // 非 Windows：空操作（设计 §6.6 仅 Win11 实装）
}
#endif

#ifdef _WIN32
bool apply_window_corner(void *hwnd) {
    if (hwnd == nullptr)
        return false;
    const HWND h = static_cast<HWND>(hwnd);
    // UX-13：Win11 圆角须显式申请（无边框窗口默认无圆角）。Win10 无此属性 →
    // DwmSetWindowAttribute 返回 E_INVALIDARG → **静默忽略**（保持系统默认，不降级其它行为）。
    const int corner = DWMWCP_ROUND;
    const HRESULT hr =
        ::DwmSetWindowAttribute(h, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    return SUCCEEDED(hr);
}
#else
bool apply_window_corner(void *hwnd) {
    (void)hwnd;
    return false; // 非 Windows：无 DWM（圆角由窗口管理器自理）
}
#endif

} // namespace pp::platform
