// SPDX-License-Identifier: GPL-3.0-or-later
// PhotoPipeline M3-D6 — Win11 Mica backdrop + immersive dark titlebar (design §6.6).

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

} // namespace pp::platform
