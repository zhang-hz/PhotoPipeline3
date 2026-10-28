// SPDX-License-Identifier: BSD-3-Clause
// M8-C1 兼容层：官方 lib/jxl/base/printf_macros.h 的最小替身（/W4 下仅本宏）。
#pragma once
#ifndef PRIuS
// 官方在 MSVC 下亦用 "zu"（VS2015+ 支持）；这里统一 C99 口径。
#define PRIuS "zu"
#endif
