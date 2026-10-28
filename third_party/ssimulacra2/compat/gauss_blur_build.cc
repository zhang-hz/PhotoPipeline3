// SPDX-License-Identifier: BSD-3-Clause
// PhotoPipeline M8-C1 —— vendored 官方 gauss_blur.cc 的包装 TU（Highway 多目标编译单元）。
//
// 官方文件本身逐字保留（仅 include 行适配，见 third_party/ssimulacra2/README.md）；
// 此处只做“第三方代码告警隔离”，不改变任何编译语义或数值行为。
// Highway 的 multi-target 机制通过 HWY_TARGET_INCLUDE 反复包含本文件；因此本包装 TU
// 必须是独立 TU（不能与 ssimulacra2.cc 合并）。
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#endif

#include "../src/gauss_blur.cc"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
