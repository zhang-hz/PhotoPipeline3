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

namespace {

// task-4 §3（Windows 版本兼容加固）：DWMWA_SYSTEMBACKDROP_TYPE 是 Win11 22H2（build 22621+）
// 才支持的属性，更早版本调用只会返回 E_INVALIDARG / 无效果。**按版本门控**：旧系统/异常环境
// 不调用（保持系统默认，失败静默）；Win11 22H2+ 调用路径与视觉逐字不变。
// 版本来源 = ntdll!RtlGetVersion（GetVersionEx 受 manifest/兼容层影响不可信；只用其返回值判断
// 一次，无额外系统调用/阻塞；模块已由加载器映射，GetModuleHandle 为内存查找）。
struct RtlOsVersionInfoW {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
};
using RtlGetVersionFn = LONG(WINAPI *)(RtlOsVersionInfoW *);

bool windows_build_at_least(ULONG min_build) {
    static const RtlGetVersionFn fn = []() -> RtlGetVersionFn {
        const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr)
            return nullptr;
        // 直接 FARPROC → 函数指针（不经 void*：tidy bugprone-casting-through-void）。
        return reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(ntdll, "RtlGetVersion"));
    }();
    if (fn == nullptr)
        return false; // 取不到版本 → 保守按旧系统处理（不调用 22H2+ 专属属性）
    RtlOsVersionInfoW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) != 0) // STATUS_SUCCESS == 0
        return false;
    if (info.dwMajorVersion != 10)
        return info.dwMajorVersion > 10; // 未来大版本按支持处理（属性向后兼容）
    return info.dwBuildNumber >= min_build;
}

constexpr ULONG kWin11_22H2Build =
    22621; // Win11 22H2 = build 22621（DWMWA_SYSTEMBACKDROP_TYPE 起始版本）

} // namespace
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

    // Mica 背景（Win11 22H2+）：先按版本门控（旧系统静默保持默认；Win11 视觉不变）。
    if (!windows_build_at_least(kWin11_22H2Build))
        return false;
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
