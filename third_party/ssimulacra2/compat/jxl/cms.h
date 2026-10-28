// SPDX-License-Identifier: BSD-3-Clause
// M8-C1 兼容层：`<jxl/cms.h>` 替身。
//
// 官方 `JxlGetDefaultCms()` 由独立的 jxl_cms 库导出（pp_core 不链接该库），而 vendored
// ssimulacra2.cc 仅把该接口作为 `ToXYB(..., const JxlCmsInterface&, ...)` 的占位参数。
// 契约输入 = sRGB 线性 RGB，compat ToXYB 不做任何 ICC/CMS 变换，因此这里返回一个
// 空接口即可；不使用其函数指针（绝不静默做错色彩变换）。
#pragma once

#include <jxl/cms_interface.h>

inline const JxlCmsInterface* JxlGetDefaultCms() {
  static const JxlCmsInterface kEmptyCms{};
  return &kEmptyCms;
}
